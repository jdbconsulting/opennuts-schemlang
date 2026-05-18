# Cookbook

Recipes for the situations that motivated the project. Each recipe
has the same shape:

1. **Problem** — what the user is trying to express.
2. **Idiom** — the recommended way to express it.
3. **Why** — which design principle this serves and what alternative
   we are rejecting.

The examples here are illustrative snippets. Full, runnable files live
in [`../examples/`](../examples/).

---

## Recipe 1 — Pin-swappable equivalents (resistor terminals, NAND inputs)

### Problem

A resistor's two terminals are interchangeable. The two inputs of a
NAND gate are interchangeable. The four spare gates in a 74HC00 are
interchangeable. Forcing the user to pick "pin 1 vs. pin 2" pollutes
the source with arbitrary choices.

### Idiom

Declare the equivalence at the package level for terminal symmetry,
and at the component level for logical equivalence:

```
define [0402]
  pin 1 ; pin 2
  swap_group { 1, 2 }

define {NAND2_74HC00}
  # four gates, each gate's two inputs are swappable
  generate for g in 0 .. 3
    pin (3 + g*3)  IN_A[g]   role digital_in
    pin (4 + g*3)  IN_B[g]   role digital_in
    pin (2 + g*3)  OUT[g]    role digital_out
    swap_group { IN_A[g], IN_B[g] }     # per-gate swap
  swap_group { gate[0], gate[1], gate[2], gate[3] }   # gate-level swap
```

In the *board* file you write:

```
u1.IN_A[0] <-> data
u1.IN_B[0] <-> enable
```

…and the elaborator may freely re-letter A↔B, or even pick a different
gate, to satisfy routing constraints.

### Why

Principle 4.3.5 (linear/affine resources) says equivalence classes are
modeled as elaborator freedom. The user states *what is equivalent*;
the elaborator chooses. Authoring is short, and the choice is
recoverable from the elaborated netlist.

We reject "always use pin 1 first" conventions: they make valid
schematics fail when a designer tries to load-balance routing, and
they bury the symmetry from later analysis.

---

## Recipe 2 — Multiple bus instances on a peripheral-rich MCU

### Problem

An STM32H7 has, e.g., six SPI peripherals. Each can be routed to
several alternate-function pin groups ("banks"). Encoding this as
"SPI1 is on PA5/PA6/PA7, *or* PE12/PE13/PE14" inside the part
definition — and letting the board author choose by saying
`mcu.spi1 <-> flash.spi` — captures real datasheet structure.

### Idiom

```
define {STM32H743}
  # ... pins, packages ...

  # Each bus instance has one or more candidate bank assignments.
  bus spi1 : <SPI> as host
    bank A  { CLK = PA5, MOSI = PA7, MISO = PA6, CS_N = PA4 }
    bank E  { CLK = PE12, MOSI = PE14, MISO = PE13, CS_N = PE11 }

  bus spi2 : <SPI> as host
    bank B  { CLK = PB13, MOSI = PB15, MISO = PB14, CS_N = PB12 }
    bank I  { CLK = PI1,  MOSI = PI3,  MISO = PI2,  CS_N = PI0 }

  # Banks may share pins. The elaborator must pick at most one bank
  # per bus, and pin-disjoint banks across all assigned buses.
```

In the board:

```
mcu.spi1 <-> boot_flash.spi
```

…and the elaborator picks bank A or E based on (a) which pins are
already taken, (b) any user-pinned constraints, and (c) explicit
preferences expressed via:

```
prefer mcu.spi1.bank == E
```

### Why

Principle 4.3.5 again: the bank choice is a freedom; the constraint is
"each bus uses one of its declared banks; pins are linear." This is
the same machinery as pin swap, just at coarser granularity.

We reject duplicating SPI definitions for each bank ("`spi1_a`,
`spi1_e`") because that loses the fact that they are mutually
exclusive instantiations of the *same* peripheral.

---

## Recipe 3 — Connecting a standard bus

### Problem

A common case the user called out: "connect this SPI bus to that
controller using standard blocks and harnesses."

### Idiom

```
include "std/spi.schemlang" only { SPI }

mcu.spi1 <-> flash.boot          # both ports are typed <SPI>
```

That's it. No pin lists. No "MOSI to MOSI." The `<SPI>` circuit
specifies the lanes and the role pairing; structural unification does
the rest.

### Why

Principle 4.3.2 (unification at every level). One operator, every
abstraction. The board file reads as block diagram.

---

## Recipe 4 — Deviating from a standard bus (split clock, stiffer pull-up)

### Problem

The reference design connects `flash.boot` directly to `mcu.spi1`. Your
board must:

* split the SPI clock to a second flash via a fanout buffer;
* use a 4.7 k pull-up on `CS_N` instead of the typical 10 k;
* add a Thevenin termination on `MOSI` for a long trace.

You want the *intent* ("almost standard, with these three deltas") to
be visible in the diff.

### Idiom

Wrap the standard pattern into an implementation circuit, then
*extend or refine* it:

```
# std/spi_link.schemlang
define <spi_link>
  type     PERIPHERAL : {SPI flash}     # placeholder kind
  parameter R_pullup : resistance = 10 k
  port host : <SPI> as host
  port pwr  : <3V3> as peripheral

  flash    = {PERIPHERAL}
  pull_cs  = {R_pullup}

  flash.<SPI>   <-> host
  pull_cs.1     <-> host.CS_N
  pull_cs.2     <-> pwr.POSITIVE
  flash.<POWER> <-> pwr
```

Your board's local override:

```
# boards/my_board.schemlang
include "std/spi_link.schemlang"

define <my_spi_link> : <spi_link>
  parameter R_pullup = 4.7 k          # refines the parent

  # Add a clock fan-out
  buf     = {SN74LVC1G07}
  flash2  = {W25N512GVEIG}
  buf.A   <-> host.CLK
  buf.Y   <-> flash2.CLK

  # Thevenin on MOSI
  rt_top  = {RES_100}
  rt_bot  = {RES_100}
  rt_top.1 <-> host.MOSI
  rt_top.2 <-> pwr.POSITIVE
  rt_bot.1 <-> host.MOSI
  rt_bot.2 <-> pwr.NEGATIVE
```

Your board file then says:

```
mcu.spi1 <-> link.host
link = <my_spi_link>(PERIPHERAL = {W25N512GVEIG})
```

### Why

Principle 4.3.3 (circuits-as-functors): refinement preserves the parent
contract while declaring local additions. The diff against the
reference is exactly the three things you changed; nothing else is
copied. Anyone reviewing your design sees "this is the standard SPI
link, with three named deltas."

We reject copy-and-edit ("`my_spi_link.schemlang`" with the entire
parent's contents repeated) because it loses the lineage and quietly
drifts.

---

## Recipe 5 — Encoding "vendor eval board, with one change"

### Problem

You have a microcontroller eval board's reference schematic. Your
production board is "the eval board, but with USB removed and the LDO
swapped to a buck regulator."

### Idiom

Make the eval board a library circuit, then compose with substitution:

```
# vendors/efinix/ti375_evk.schemlang
define <ti375_evk>
  port pwr_in : <POWER>
  port boot   : <SPI> as host
  port user_io : <GPIO[8]>

  # ... full reference design body ...

  power_stage = <ldo_3v3>
  usb_stage   = <usb_cdc>
```

Your board:

```
include "vendors/efinix/ti375_evk.schemlang"

define <my_compute> : <ti375_evk>
  # Replace the LDO with a buck stage of the same port shape.
  override instance power_stage = <buck_3v3>(I_max = 2.0 A)

  # Remove USB explicitly (errors if anything still references it).
  remove instance usb_stage
```

Two body forms (formalized in `03-syntax.md` §2.2):

* `override instance name = <NewType>(...)` — *replace* a child
  instance from the parent with a new one of compatible signature.
  The replacement must have the same port shape; the elaborator
  type-checks this.
* `remove instance name` — drop an instance from a parent circuit. The
  instance's ports must be unreferenced after the drop, or elaboration
  errors. Equivalent forms exist for `port`, `parameter`, `pin`,
  `view`, `constraint`, `package`, `splice`, and `parent` (see §2.2.4).

### Why

Principle 4.3.3 (functors) and 4.3.5 (linearity). Substitution and
removal are checked: you cannot remove a regulator that something
depends on without also removing the dependent or providing a
replacement. The vendor's design is captured once; your fork is a
small, audited, reviewable diff.

### Variant — mixin composition with multiple parents

When the change you want is "the eval board, *plus* this orthogonal
feature circuit," reach for multiple parents instead of a single chain:

```
include "vendors/efinix/ti375_evk.schemlang"
include "addons/temp_logging.schemlang"   # exports <temp_logging>

define <my_compute> : <ti375_evk>, <temp_logging>
  # Replace the LDO; type-checks against the parent's port shape.
  override instance power_stage = <buck_3v3>(I_max = 2.0 A)

  # Both parents define `status_led`; we keep the eval board's wiring
  # and drop only the temp_logging contribution by name.
  remove parent <temp_logging>.instance status_led
```

Order matters: parents are layered left-to-right and your body is the
final say. Same-named contributions from different parents that
disagree must be resolved with `override` or `remove`. The merge rules
are spelled out in `03-syntax.md` §2.2.

---

## Recipe 6 — Rail windows as compile-time checks (with tolerance)

### Problem

A part requires `Vcc in [2.7 V, 3.6 V]`. Your power tree feeds it
from a `<3V3>` rail at `3.3 V +/- 3 %`. You want to *prove* this is
fine at compile time, before the board is fabbed — and you want
the proof to account for the rail's tolerance correctly. *"The rail
is 3.3 V and the part accepts up to 3.3 V"* is **not** safe; the
rail's high tolerance corner is 3.4 V.

This is the canonical case where a parameter on a part has earned
its place: the elaborator uses `Vmin`/`Vmax` to discharge a
*containment* check on the rail's interval. If the parameter is
not driving a check like this, it does not belong here — see
principle 7 in `01-design-principles.md`.

### Idiom

The two parameters that participate in the check go on the pin:

```
define {W25N512GVEIG}
  ...
  pin 8 VCC role power_in
    parameter Vmin : voltage = 2.7 V
    parameter Vmax : voltage = 3.6 V
    constraint net.V in [Vmin, Vmax]      # "the net I'm on" is `net`
```

The rail's refinement carries its tolerance via the **first-class
tolerance form** (see `03-syntax.md` §7.1.1):

```
define <3V3> : <POWER>
  parameter Vnom : voltage = 3.3 V +/- 3 %
```

Two things happen automatically:

1. `<3V3>.Vnom` is now an *interval-valued* variable with
   `Vlo = 3.201 V`, `Vhi = 3.399 V`.
2. The net that this rail drives inherits that interval; the pin's
   `net.V` is interval-valued too.

When the elaborator unifies `flash.VCC`'s net with the `<3V3>` net,
the constraint `net.V in [Vmin, Vmax]` reads as **containment**
(`02-semantic-model.md` §6.1):

```
   3.201 V >= 2.7 V    AND    3.399 V <= 3.6 V       # both true; pass
```

If the part instead specified `Vmax = 3.3 V` (a typical 1.6 V – 3.3 V
VCCIO chip), the second clause becomes `3.399 <= 3.3`, false — the
elaborator catches the mistake:

```
ERROR: V window violation at chip.VCCIO (boards/foo.schemlang:42)
   recommended: [1.6 V, 3.3 V]
   provided:    [3.201 V, 3.399 V]   from rail <3V3> at power.schemlang:39
   overshoot:   0.099 V at the rail's high tolerance corner
   fix candidates:
     * tighten <3V3>.Vnom's tolerance to 0 %  (impossible)
     * lower <3V3>.Vnom to a value with positive headroom
     * choose a part with a higher Vmax
     * pick a different rail (`<2V5>`, `<1V8>`, ...)
```

The same machinery handles the other side. A part with `Vmin = 3.0 V`
on a `<3V3>` at `3.3 V +/- 10 %` (Vlo = 2.97 V) fails on the lower
side: `2.97 < 3.0`.

### Asymmetric tolerances

Some rails — bandgaps, switching regulators with asymmetric
overshoot/undershoot — declare the two sides separately. Schemlang
takes any of:

```
parameter Vnom : voltage = 3.3 V + 5 %, - 1 %             # asymmetric percent
parameter Vnom : voltage = 3.3 V + 0.1 V, - 0.05 V        # asymmetric absolute
parameter Vnom : voltage = 3.3 V + 5 %                    # one-sided up
parameter Vnom : voltage = 3.3 V + 5 %, - 0.1 V           # mixed kinds
```

All desugar to the same `(Vnom, Vlo, Vhi)` triple. See
`03-syntax.md` §7.1.1 for the full table.

### Derating with `Vmax_abs`

A part that quotes both *recommended operating range* and *absolute
maximum ratings* gets two layered checks for free. Add the optional
parameters:

```
define {W25N512GVEIG}
  ...
  pin 8 VCC role power_in
    parameter Vmin     : voltage = 2.7 V
    parameter Vmax     : voltage = 3.6 V
    parameter Vmax_abs : voltage = 4.6 V        # destruction threshold
    constraint net.V in [Vmin, Vmax]            # operating range (HARD)
    constraint net.V.Vhi <= Vmax_abs            # destruction threshold (HARD)
    constraint net.V.Vhi <= 0.8 * Vmax_abs soft 5   # 20% derating (SOFT)
```

The first hard constraint is the recommended-window check; the
second is the destruction-threshold check; the soft constraint is
classic 20 % derating for reliability. The explain trace classifies
violations by severity:

```
ERROR (destructive): V exceeds Vmax_abs at chip.VCC ...
ERROR (out-of-spec): V exceeds Vmax at chip.VCC ...
WARN  (no derating): V within 20% of Vmax_abs at chip.VCC ...
```

### Why

Principle 4.3.4 (refinement types) and principle 7 (parameters earn
their place). `Vmin`/`Vmax` are the two values the elaborator needs
to prove the operating-range safety property. `Vmax_abs` is
optional — included only when the datasheet quotes a distinct
absolute-maximum rating *and* you want derating analysis to fire.
The rest of the W25N's datasheet (temperature ratings, MTBF, MSL,
package mass) stays out of the language because nothing in the
language tests it.

The interval-valued semantics of `in` (`02-semantic-model.md` §6.1)
is what makes this recipe correct. With point semantics the rail's
worst case would silently disappear: `[3.201, 3.399] ∩ [1.6, 3.3]`
is non-empty, so a naive "point membership" reading would call the
chip safe even though it sees up to 3.4 V. Containment is the
default precisely because it's the reading that catches real bugs.

The rule of thumb: if you cannot point at the elaborator check that
consumes a parameter, it belongs in the datasheet, not in the
source.

---

## Recipe 7 — Bank current budgets

### Problem

The MCU's IO bank A can sink at most 100 mA total. Drive 12 LEDs from
bank A, each at 10 mA, and you'll cook it. You want this caught.

### Idiom

Aggregate constraint at the bank level:

```
define {STM32H743}
  ...
  bank A
    pins { PA0, PA1, ..., PA15 }
    parameter I_max : current = 100 mA
    constraint over { p in pins } { sum(p.I_load) <= I_max }
```

LED loads expose their current:

```
define {LED_RED}
  pin A role digital_in   # driven from a GPIO
  parameter I_typ : current = 10 mA
  constraint A.net.I_load == I_typ
```

The elaborator sums `I_load` for every load on every pin in bank A. If
the sum exceeds 100 mA, error.

### Why

Principle 4.3.4 + 4.3.5: aggregate constraints over linear resources.
This is exactly the structure: each load contributes a fixed, finite
amount; the sum has a hard upper bound.

We reject treating this as a separate "DRC rule file" because that
divorces the constraint from the data that supports it. The *part*
knows its bank; the *part* declares the budget.

---

## Recipe 8 — A procedurally generated array

### Problem

64 channels of identical analog front-end. Authoring 64 copies is
ridiculous; copy-paste makes review impossible.

### Idiom

```
define <afe_channel>
  port in_p, in_n : analog
  port out        : analog
  port pwr        : <3V3>
  # ... opamp + RC + reference ...

define <afe_array_64>
  port in_p[64], in_n[64] : analog
  port out[64]            : analog
  port pwr                : <3V3>

  generate for i in 0 .. 63
    ch[i] = <afe_channel>
    ch[i].in_p <-> in_p[i]
    ch[i].in_n <-> in_n[i]
    ch[i].out  <-> out[i]
    ch[i].pwr  <-> pwr
```

### Why

Principle 4.2.7 (procedural power, on demand). The generator expands
to 64 instances at compile time. The *source* is six lines.

Compare with the alternative of a Python script that emits 64 copies:
that loses type-checking, loses lineage, and forces a build step
outside the language. Generators are first-class and fully checked.

---

## Recipe 9 — Naming a debug net you actually want labelled

### Problem

You want `VCC_3V3_MCU` to appear as a netname in exports, even though
the elaborator could derive everything without it.

### Idiom

```
NET_VCC_3V3_MCU = mcu.VDD                # introduces a net label
NET_VCC_3V3_MCU <-> reg.OUT
NET_VCC_3V3_MCU <-> bulk_cap.1
```

The `=` declares the label; subsequent `<->` use it like any other
name. The label is exported as the net's preferred name; if multiple
labels exist on the same net, the first declared wins, and the
elaborator emits a warning naming the alternates.

### Why

Principle 4.5: source is the source of truth. Net naming is a *labeling
operation* on the equivalence class, never a connection mechanism.
Two pins both attached to `VCC` in different unrelated circuits are
**not** joined unless physically connected.

---

## Recipe 10 — Custom DRC predicates

### Problem

Your team has internal rules: every IO pin used as a high-speed signal
must have a series resistor populated; every ADC reference must have
≥1 uF of bypass. These rules apply across every board.

### Idiom

Write the rules as library predicates:

```
# std/checks.schemlang

define <high_speed_link>
  port a, b : digital
  parameter f_max : frequency = 100 MHz
  constraint exists r : {RESISTOR}.
              r.terminals connects { a, b }

define <adc_reference>
  port vref : analog
  parameter c_min : capacitance = 1 uF
  constraint sum_caps_on(vref) >= c_min
```

Then in your board:

```
constraint <high_speed_link>(a = mcu.PA5, b = adc.SCLK, f_max = 200 MHz)
constraint <adc_reference>(vref = adc.VREF)
```

Constraints can themselves be parameterized circuits. Custom DRC is
just a library, and a library is just more `define` statements.

### Why

Principle 4.2.5 (boring, learnable surface) and 4.4 (no new syntax for
new families). The entire team-DRC story uses the same primitives as
component definitions.

---

## Recipe 11 — Project-wide vendor preferences

### Problem

Your team has standardized on Panasonic ERJ-PHF resistors and Murata
GRM ceramic capacitors. You want every `{R_10k}` and `{C_100n}` in
every board to land on those families automatically — without
copy-pasting MPN choices into every alias, and without a side
configuration file.

### Idiom

Declare the abstract aliases in a shared library, declare the
preferences in a project-level file, and include both:

```
# std/passives.schemlang  (library)
define {RESISTOR}
  designator_prefix R
  parameter R   : resistance
  parameter Tol : percent = 1 %
  ...

alias {R_10k}  = {RESISTOR}(R = 10 k)
alias {R_4k7}  = {RESISTOR}(R = 4.7 k)
alias {C_100n} = {CAPACITOR}(C = 100 nF, V_max = 16 V)
```

```
# vendors/panasonic/erj.schemlang  (vendor library)
define {ERJ-PHF}                  # parametric concrete type
  designator_prefix R
  # maps incoming (R, Tol, P_max) → a stocked MPN at elaboration time
  parameter R   : resistance
  parameter Tol : percent = 1 %
  ...
  # (vendor library logic for MPN selection lives here)
```

```
# prefer.schemlang  (root of the project, included by every board)
include "std/passives.schemlang"
include "vendors/panasonic/erj.schemlang"
include "vendors/murata/grm.schemlang"

prefer {RESISTOR}  = {ERJ-PHF}
prefer {CAPACITOR} = {GRM}
```

```
# boards/eval_board.schemlang
include "../prefer.schemlang"
include "parts/stm32h7.schemlang"

define <eval_board>
  pull = {R_10k}              # elaborates to {ERJ-PHF}(R = 10 k, Tol = 1 %)
  byp  = {C_100n}             # elaborates to {GRM}(C = 100 nF, V_max = 16 V)
  ...
```

### Local override

A specific subsystem can override project policy lexically. The
analog front-end wants C0G capacitors; the rest of the board uses
the team-default GRM:

```
define <afe_channel>
  prefer {CAPACITOR} = {GRM_C0G}    # innermost wins, scoped to this circuit
  c1 = {C_100p}
  ...
```

`prefer` rules in inner scopes shadow outer ones. The rest of the
board is unaffected.

### Why

Principle 4.4 (small core, rich library) and the new principle
"parameters earn their place." Abstract aliases capture the
*requirement* — what value, what tolerance — without committing to a
vendor. The `prefer` mechanism resolves requirements to vendor parts
at the project boundary, where vendor choice belongs. Switching from
Panasonic to Yageo is a single line in `prefer.schemlang`, not a
search-and-replace across boards.

We reject side-configuration files for the same reason we reject
side-files everywhere: principle 4.5.6 says "everything shall be
`.schemlang`." A `prefer` rule is just another top-level statement.

---

## Recipe 12 — Layout and design intent that travels with the source

### Problem

You know things about the design that *aren't* electrical
constraints, but that downstream tools (a typesetter generating
schematics, a layout engine placing parts, an LLM reviewer
explaining the board) absolutely need:

* "Render the power tree on its own sheet, named *Power Tree*."
* "The MCU is the anchor of the layout — center it; rotate 90°; keep
  it near the USB-C connector."
* "These two SPI lanes are clock-and-data; route them as a matched
  pair within 5 mm of each other."
* "The AFE is a low-noise stage. Don't break the guard ring or move
  the bypass caps when auto-routing."

In every other EDA flow this lives in side-files: layout-tool
projects, sheet-organizer XMLs, free-form margin notes that get lost
between revisions. Schemlang puts it in the source.

### Idiom

Use first-class `hint` declarations. The kind tells the consumer
*what* the hint provides; structured args constrain the answer where
possible; the trailing string is free text for humans and
NLP-augmented tools that can read it.

```
# boards/eval_board.schemlang
define <eval_board>
  mcu       = {STM32H743VIT6} @ U1
    hint placement  "center; near USB-C; rotate 90°"
    hint annotate   "anchor of the layout"
    hint priority   level = critical

  reg       = {LDO_3V3}      @ U10
  flash     = <boot_flash_slot>
  usb_conn  = {USB_C_RECEPTACLE}

  # sheet partitioning
  hint sheet { reg, byp_3v3, bulk_caps }
    name         "Power Tree"
    page_number  2
    text         "5V → 3V3 LDO and decoupling"

  hint sheet flash
    name         "Storage"
    page_number  3

  # signal-class designation; layout reads `name` and `max_length`
  hint signal_class { mcu.spi1.CLK, mcu.spi1.MOSI, mcu.spi1.MISO }
    name              "spi_high_speed"
    max_length        50.0 mm
    match_length_group spi1
    text              "boot SPI; route together; keep away from switching nodes"

  # placement coupling
  hint near reg, bulk_caps
    max_distance      3.0 mm
    text              "tight ESR loop; do not separate"
```

### Override and remove

A bespoke variant inherits the parent's hints and surgically refines
them — same merge story as everything else:

```
define <my_compute> : <eval_board>
  override hint placement mcu  "right-of-center; near USB-C"
  remove   hint annotate  status_led
  hint     review_note    rf_section  for = "rf_lead"
                          text = "is the loop antenna keepout big enough?"
```

`placement` is *override-only* (one placement per part), so the
`override` keyword is required. `annotate` is *accumulate*, so we
explicitly `remove` the parent's status-LED note. `review_note` on
the `rf_section` circuit accumulates onto whatever the parent
already had.

### Hints in circuits see lexical scope

Inside a circuit body, hints address the circuit's own ports and
instances by name:

```
define <boot_flash_slot>
  port flash_bus : <SPI> as host
  flash = {W25N512GVEIG}
  hint annotate flash  "footprint also accepts 1Gb sibling W25N01GV"
  hint placement flash "near MCU; SPI lanes < 50 mm to host"
```

Including `<boot_flash_slot>` in a board automatically pulls in
those hints, retargeted to the circuit's instance path.

### What the elaborator does (and doesn't) do

The elaborator:

* checks target arity per kind (e.g. `placement` rejects two
  targets);
* enforces merge rules (override-only kinds reject duplicates
  without `override`);
* emits the hint stream alongside the netlist, with each target
  resolved to a fully-qualified path.

The elaborator does **not**:

* try to interpret free text;
* fail if a downstream tool ignores a kind it doesn't know;
* let a hint participate in a constraint, unify with anything, or
  modify a connection.

If a hint *should* affect electrical correctness, it isn't a hint —
it's a `constraint`. See `02-semantic-model.md` §7.1.

### Why

This is principle "intent travels with structure" (design-principle
§1.8). EDA tools have been throwing intent away for forty years
because there was no place to put it that travelled with the
schematic across vendors and revisions. Hints fix that:

1. **Machine-readable enough** for a typesetter, layout engine, or
   LLM-augmented assistant to consume directly.
2. **Lexically scoped** — a hint declared in circuit `C` follows
   instances of `C` everywhere. No global registry, no orphaned
   metadata.
3. **Versionable** — hints diff cleanly because they live in the
   source. A reviewer can ask "why is this `priority: critical`?"
   on a pull request.
4. **Forward-compatible** — unknown kinds pass through silently. A
   board can ship hints intended for a layout tool that hasn't been
   written yet without breaking today's compile.

The free-text payload is deliberate. NLP-augmented tools can extract
structure from prose; a non-NLP tool ignores the prose and uses only
the structured args. Both work; neither is forced to wait for the
other.

---

## Recipe 13 — Soft peripherals on FPGAs

### Problem

An FPGA does not have a fixed list of named buses. It can synthesize
any number of UARTs, SPI controllers, or I2C buses on demand,
drawing pins from a shared pool subject to electrical rules: lanes
of the same bus must share a bank Vio, the bank Vio must match the
bus's signaling, and no pin may serve two buses. The MCU-style
pattern of "pre-declare `i2c1`, `i2c2`, …, `i2c99` with closed
alt-function tables" is the wrong shape: the count is set by the
*board*, not the part.

### Idiom

Declare the part's *capability*, not a fixed number of instances.
The component says what makes a legal bus; the board determines how
many.

```
# parts/xilinx_xc7a35t.schemlang
define {XC7A35T}
  designator_prefix U
  description "IC FPGA XILINX ARTIX-7 33K-LC CSG324"

  bank bank_14
    parameter Vio : voltage
    constraint Vio in { 1.8 V, 2.5 V, 3.3 V }
    pins { IO_L1P_T0_14, IO_L1N_T0_14, IO_L2P_T0_14, ... }
  bank bank_15
    parameter Vio : voltage
    constraint Vio in { 1.8 V, 2.5 V, 3.3 V }
    pins { ... }
  bank bank_34
    parameter Vio : voltage
    pins { ... }
  bank bank_35
    parameter Vio : voltage
    pins { ... }

  # Capabilities: factories of bus instances.
  provides <I2C> as host
    pool pins where role == digital_io
    per_bus
      SDA in pool
      SCL in pool
      where bank(SDA) == bank(SCL)
      where bank(SDA).Vio in { 1.8 V, 2.5 V, 3.3 V }

  provides <SPI> as host
    pool pins where role == digital_io
    per_bus
      SCK  in pool
      MOSI in pool
      MISO in pool
      CS   in pool
      where bank(SCK) == bank(MOSI) == bank(MISO) == bank(CS)

  provides <UART> as host
    pool pins where role == digital_io
    per_bus
      TX in pool
      RX in pool
      where bank(TX) == bank(RX)
```

At the board, allocate by name — the *purpose* of the bus, not its
index:

```
define <eval_board>
  fpga    = {XC7A35T}
    parameter bank_14.Vio = 3.3 V
    parameter bank_15.Vio = 1.8 V
    parameter bank_34.Vio = 3.3 V
    parameter bank_35.Vio = 3.3 V

  imu     = {LIS3DH}
  env     = {SHT40}
  flash   = {W25N512GVEIG}
  debug   = <debug_header>
  gps     = {U-BLOX_M10}
  lte     = {QUECTEL_BG95}

  fpga.i2c.imu        <-> imu.i2c
  fpga.i2c.env        <-> env.i2c
  fpga.spi.boot       <-> flash.spi
  fpga.uart.debug     <-> debug.uart
  fpga.uart.gps       <-> gps.uart
  fpga.uart.lte       <-> lte.uart
```

Two `<->` lines that mention the same `fpga.i2c.imu` refer to the
*same* allocation. Mentioning a new name allocates a new one. The
elaborator counts the allocations itself.

### Pin overrides where layout demands them

Most allocations want the elaborator to pick. When layout pins a
specific lane, attach the constraint to the allocation:

```
fpga.i2c.imu <-> imu.i2c
  prefer SDA = fpga.IO_L5P_T0_14         # soft hint to the solver
  pin    SCL = fpga.IO_L5N_T0_14         # hard equality

fpga.spi.boot <-> flash.spi
  pin    SCK  = fpga.IO_L1P_T0_14        # pinned to a high-speed bank
  pin    MOSI = fpga.IO_L1N_T0_14
```

### Mixing buses, banks, and Vio

Because all three capabilities filter the same `digital_io` pool,
the linear-resource discharge automatically prevents pin reuse
across them. Bank Vio enters as a constraint on the allocation: an
I2C bus picked from `bank_15` (1.8 V) cannot satisfy
`bank_15.Vio == 3.3 V`, so the solver picks pins from a 3.3 V bank
instead — or fails with a clear error when there are no candidates
left.

### Variant — flexible-I/O MCUs

The same shape covers MCUs with reconfigurable I/O matrices
(NXP MIMXRT, Microchip SAMD, RP2040 PIO):

```
provides <I2C> as host
  pool pins where role == digital_io and supports_i2c == true
  per_bus
    SDA in pool
    SCL in pool
    where port_group(SDA) == port_group(SCL)
```

A part with both fixed peripherals and a flexible matrix uses both
forms in the same body — `bus i2c1` for the silicon controllers
with closed alt-function tables, `provides <I2C> as host` for the
PIO-driven ones.

### Why

This is principle 1.5 ("Linear resources / affine obligations") in
its load-bearing role: a pin is a token consumed at most once across
all allocations on a part, and the elaborator discharges that
discipline as a SAT-style assignment problem — no different in kind
from MCU bank choice, just larger.

What the language explicitly does *not* do is bake in a maximum
allocation count. A `max_count = 16` on `provides <UART>` would be
pretend precision: whether sixteen UARTs fits in your XC7A35T
depends on baud rate, FIFOs, gate-level optimizations, and the FPGA
toolchain's mood, none of which the schematic language can know. If
the design exhausts the pin pool, the elaborator fails with a
specific conflict; if it exhausts logic, the FPGA flow fails with a
specific message there. Each tool catches what it can authoritatively
know. We keep the language honest by not pretending otherwise.

---

## Recipe 14 — Conditional source termination on SPI

### Problem

A direct `mcu.spi <-> flash.spi` is fine for a 10 mm trace at 10 MHz.
At 80 MHz over 100 mm, you want 22 Ω series source termination on
the host-driven lanes (CLK, MOSI, CS_N) — but you don't want every
SPI link in the project to grow three resistors. You want the
*physics* (trace length, frequency, edge rate) to drive whether
termination shows up, and you want the policy expressed once and
applied to every connection that meets it.

### Idiom

Define a wrapper circuit that inherits from the protocol and uses
`generate if` over an electrical-length parameter to splice
resistors onto the lanes:

```
# std/spi.schemlang
define <SPI>
  lane CS_N, CLK, MOSI, MISO : digital
  role host       drives { CS_N, CLK, MOSI } receives { MISO }
  role peripheral receives { CS_N, CLK, MOSI } drives   { MISO }

define <spi_with_termination> : <SPI>
  parameter trace_length : length    = 0 mm
  parameter f_max        : frequency = 10 MHz
  parameter edge_rate    : time      = 2 ns
  parameter Z0           : resistance = 50
  parameter R_term       : resistance = 22    # override per link as needed

  # Electrical length, normalized to a wavelength-equivalent at f_max.
  parameter elec_len : dimensionless =
      trace_length * f_max / (0.6 * speed_of_light)

  generate if elec_len > 0.1 or edge_rate < 1 ns
    R_clk  = {RESISTOR}(R = R_term)
    R_mosi = {RESISTOR}(R = R_term)
    R_cs   = {RESISTOR}(R = R_term)

    splice CLK   with R_clk
    splice MOSI  with R_mosi
    splice CS_N   with R_cs
    # MISO is driven by the peripheral; terminate at that end if needed.
```

Apply via `prefer` to make every `<SPI>` link pass through the
wrapper:

```
# project root
prefer <SPI> = <spi_with_termination>(R_term = 22)
```

Then a connection that meets the threshold gets termination; one
that doesn't is just wires:

```
mcu.spi1 <-> flash.spi
  parameter trace_length = 110 mm
  parameter f_max        = 80 MHz
  # elec_len ≈ 0.49 → splices generate, three resistors land on the lanes

mcu.spi2 <-> debug_header.spi
  parameter trace_length = 30 mm
  parameter f_max        = 1 MHz
  # elec_len ≈ 0.0017 → no splices, wrapper degenerates to wire-through
```

### Why

`splice` is the right primitive for *series* insertion: it suppresses
the implicit through-wire and inserts the component between
`lane.host_side` and `lane.peri_side` without forcing the user to name
those endpoints. `generate if` keeps the splices conditional on
parameters of the actual link (length, frequency), not a global flag.
`prefer` lifts the wrapper to a project-wide policy without forcing
the connection-site author to think about it.

We reject hand-editing every SPI connection to "add three resistors
when the trace is long" because the policy becomes invisible. A
new engineer can't tell which connections were terminated on
purpose and which were forgotten. With the `prefer` wrapper, every
link gets the same examination; the source visibly says so.

This is principle 1.6 ("intent travels with structure") in its
electrical form: the *circuit-quality decision* — terminate when
electrically long — lives next to the protocol, not in a tribal
checklist.

---

## Recipe 15 — Protocol-required components: I2C pull-ups (multi-drop)

### Problem

I2C is open-drain *and* multi-drop: every bus requires pull-up
resistors on SDA and SCL, *one set per bus*, sized to the rail and
bus speed and bus capacitance. Forgetting them produces a
non-functional bus that DRC-by-pin-role can't catch. Naively
adding a pull-up at every `<->` edge is also wrong — three sensors
on one bus must share one pair of pull-ups, not three.

### Idiom

Define a wrapper circuit with N-ary roles and `derive` rules that
auto-bind the wrapper's `vref` from each participant. The protocol's
`resolution` block lets devices express preferences (e.g. "I have
internal pull-ups, please") without the user spelling it out:

```
# std/i2c.schemlang
define <I2C>
  lane SDA, SCL : digital

  role host
    cardinality = 1
    drives    { SCL }
    bidir     { SDA }
    derive vref = bank(SDA).vio_port
    derive Vio  = bank(SDA).Vio

  role peripheral
    cardinality >= 1                     # multi-drop!
    receives  { SCL }
    bidir     { SDA }
    derive vref = supply.vio_port
    derive Vio  = supply.Vio

  resolution
    case any(prefers <i2c_link>) => <i2c_link> weight 10
    case all(prefers <I2C>)      => <I2C>      weight 5
    default                      => <i2c_link> weight 1


define <i2c_link> : <I2C>
  parameter Vio   : voltage    in {1.8 V, 2.5 V, 3.3 V, 5.0 V}
  parameter R_pu  : resistance = 4.7 k ± 1 %
  parameter f_max : frequency  = 400 kHz
  parameter C_bus : capacitance = 100 pF

  port vref : <POWER> as peripheral

  pull_sda = {RESISTOR}(R = R_pu)
  pull_scl = {RESISTOR}(R = R_pu)

  pull_sda.T1 <-> SDA
  pull_sda.T2 <-> vref.POSITIVE
  pull_scl.T1 <-> SCL
  pull_scl.T2 <-> vref.POSITIVE

  # Refinement: pull-up RC vs. bus speed. Nonlinear; deferred to a
  # numeric check or to the residual.
  constraint pull_strength : R_pu * C_bus < 1 / (3 * f_max)
  constraint vref.Vnom == Vio
```

Apply once, project-wide:

```
# project.schemlang
prefer <I2C> = <i2c_link>(R_pu = 4.7 k)
```

Then *one* `<i2c_link>` materializes per *bus instance*, regardless
of the number of `<->` edges that join the bus:

```
fpga.i2c.sensors <-> imu.i2c
fpga.i2c.sensors <-> env.i2c
fpga.i2c.sensors <-> baro.i2c
# One <i2c_link> wrapper, one pair of pull-ups, three peripherals.
# vref is auto-bound: imu.i2c.derive vref = imu.supply.vio_port,
# env.derive vref = env.supply.vio_port, baro.derive vref = …, and
# the host's derive vref = fpga.bank(SDA).vio_port. The elaborator
# unifies all four; if they don't unify (different Vios), the
# elaborator emits an UNSAT core citing each participant.
```

### Per-device wrapper preferences

A device with internal pull-ups expresses that on its port:

```
define {ISOLATED_IMU}
  ...
  view <I2C> as peripheral i2c
    SDA  SDA
    SCL  SCL
    derive Vio = supply.Vio
  bus_port i2c
    prefer wrapper = <I2C>            # "I have internal pull-ups"
```

If this isolator is the *only* peripheral on a bus, the
resolution's `all(prefers <I2C>)` case fires and the bus uses the
empty-bodied protocol — no external pull-ups. If it shares a bus
with bare-CMOS peripherals that prefer `<i2c_link>` (or don't
declare a preference), the `any(prefers <i2c_link>)` case wins
(weight 10 > weight 5) and the wrapper is materialized — the
isolator's internal pull-ups parallel the external ones, which is
electrically harmless.

### Connect-site override

A user who wants *deterministic* behavior can pin the wrapper at
the connect site, ignoring participants' soft preferences:

```
fpga.i2c.iso <-> iso_imu.i2c
  prefer <I2C> = <I2C>          # HARD: suppress wrapper for THIS bus only
```

A connect-site `prefer` is at priority 1 (highest), so it beats
both project-level `prefer` and the resolution rules.

### Different rails on the same board

Multi-Vio bus on the same board: the connect-site argument block
overrides parameters per bus:

```
fpga.i2c.lv <-> low_voltage_sensor.i2c
  parameter Vio  = 1.8 V
  parameter R_pu = 2.2 k
  # The host's bank Vio for this bus auto-narrows via derive
  # equality; the solver picks fpga.bank_*.Vio = 1.8 V on whichever
  # bank the lane allocator chose.
```

### Why

I2C pull-ups are *protocol-required*, not *circuit-quality* like
the SPI termination of Recipe 14. The right place to encode them is
in the protocol's wrapper, attached to the *bus instance* (not the
edge). Multi-drop is the default expectation, not a special case;
`derive` automates the boring `vref` plumbing; the `resolution`
block makes "this device has internal pull-ups" express
itself naturally.

This recipe is the same shape as Recipe 11 (project-wide vendor
preferences), just at a different kind. `prefer` was originally
component-only; in the unified language it applies to circuits too,
and "every I2C bus has pull-ups, except where overridden" is a
one-line policy plus a few targeted overrides.

We reject the alternative — making I2C pull-ups a special-cased
language feature that the elaborator inserts automatically — because
it violates principle 4.4 (no new syntax for new families). Pull-ups
are just components; lanes are just nets; wrappers are just circuits;
`prefer` is just the binding mechanism we already had. Composition
beats special cases.

---

## Recipe 16 — Bank voltages as constraint variables

### Problem

An FPGA with four I/O banks, each with a Vio you can set to 1.8 V,
2.5 V, or 3.3 V. The board has eight buses landing across all four
banks. Some buses *must* run at a specific voltage (e.g. an LVDS
display); some are flexible (any of the three works fine).

You want:

1. The elaborator to enforce that every bus's bank Vio matches its
   participants (a 3.3 V sensor on a 1.8 V bank is a hard error).
2. The elaborator to *choose* a Vio for each bank that satisfies
   every bus on it, when the choice is unique.
3. The elaborator to leave the choice *open* when multiple values
   are valid, and emit the unbound Vios to the residual so
   placement can finish the design.

### Idiom

Declare each bank's `Vio` as a finite-domain constraint variable.
Ports that participate in a bus instance publish their `Vio` view
via `derive`; the elaborator unifies them.

```
define {XC7A35T}
  ...
  bank bank_14
    parameter Vio : voltage in { 1.8 V, 2.5 V, 3.3 V }
    pins { ... }
    alias vio_port = vio_14         # the <POWER> view for this bank

  ...

  provides <I2C> as host
    pool pins where role == digital_io
    per_bus
      SDA in pool ; SCL in pool
      where bank(SDA) == bank(SCL)
    derive vref = bank(SDA).vio_port
    derive Vio  = bank(SDA).Vio
```

A 3.3 V I2C peripheral declares `derive Vio = supply.Vio` on its
peripheral role; its `supply.Vio` is a singleton at 3.3 V.

When the bus instance forms, the elaborator posts:

```
host.derive Vio == bank(SDA).Vio   # where bank(SDA) is the lane allocation
peri[i].derive Vio == 3.3 V         # per peripheral
```

…and unifies the LHSs. Result: `bank(SDA).Vio = 3.3 V`. If the
allocator put SDA on `bank_14`, `bank_14.Vio` is now bound. If the
allocator could put SDA on `bank_14`, `bank_15`, or `bank_34`, the
choice itself is a constraint variable and propagation through the
pin-pool allocation may collapse it.

### Solving outcomes

* **FULL solve.** Every bank's Vio binds to a single value; the
  netlist is fully determined; the residual is empty.
* **PARTIAL solve.** Some banks' Vios remain in a narrowed domain
  (e.g. `bank_15.Vio in {1.8 V, 2.5 V}` because no bus on it
  pins down further). The netlist emits, the residual contains
  the open variables, and `--require-resolved` rejects the build
  (unless placement is allowed to finish).
* **UNSAT.** Two buses on the same bank insist on incompatible
  Vios. The elaborator emits a minimal core citing each
  participating bus's `derive Vio` and the bank-shared
  `bank(SDA) == bank(SCL)` constraint:

```
ERROR: design is overconstrained.
Minimal unsatisfiable core:
  1. fpga.i2c.imu.bank == fpga.i2c.env.bank   (allocated to bank_14)
       at parts/xilinx_xc7a35t.schemlang:135
  2. fpga.i2c.imu.Vio  == 3.3 V               via imu.i2c derive Vio
       at parts/some_imu.schemlang:42
  3. fpga.i2c.env.Vio  == 1.8 V               via env.i2c derive Vio
       at parts/some_env.schemlang:38

Resolution: relax `bank(SDA) == bank(SCL)` (move env to a
different bank) or change one of the Vios.
```

### Why

This is the constraint-driven model in its most load-bearing
form. The elaborator does not pre-compute Vios; it records
constraints and lets the solver find a satisfying assignment.
Designs that *can* compile do; designs that *can't* explain why
they can't with three constraints, not a generic "voltage
mismatch" message. Designs that are *underconstrained* compile to
a netlist plus a residual, deferring the choice to placement —
which has more information to make it.

We reject the alternative — eager evaluation of bank Vios at
elaboration time, with a dedicated "bank voltage solver" — because
it doesn't generalize. The same machinery that picks bank Vios
also picks wrapper choices, MPN bindings, pin-swap permutations,
and bank-A-vs-bank-E peripheral allocations. One CSP, one solver,
one explain trace. See `02-semantic-model.md` §11–§14.

---

## When to break the rules

Every recipe above can be subverted with two escape hatches:

1. **`raw_pin <pin_number>`** in a circuit body lets you connect to a
   pin number directly, skipping logical names. Useful when wiring an
   unhinged eval board where a pin's role is actively misused.
2. **`assume <constraint>`** asserts a predicate without proof. Use
   sparingly; it appears in the build report as an "assumed" line.

If you find yourself using either repeatedly, you have either
discovered a missing library pattern (write it) or a missing language
feature (open a discussion).

A third "break-glass" form is **`prefer concrete`** instead of
`prefer abstract = concrete` — when a single instance must use a
specific MPN regardless of project policy:

```
boot_xtal = {NX3225GA-25.000M}   # specific MPN bypasses any prefer
```

This is just instantiating a concrete type directly, which is always
allowed; it sidesteps the abstract→concrete resolution because there
is no abstract to resolve.
