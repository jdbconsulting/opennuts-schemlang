# Concrete Syntax

This document evolves the README sketch into a full grammar. Where the
README's choice serves the principles, we keep it; where it fights
them, we change it and explain why.

The sketch is preserved at the heart of the language:

* `{Component}` — components are in curly braces.
* `[Package]` — packages are in square brackets.
* `<Circuit>` — circuits (typed graph fragments and reusable
  subcircuits) are in angle brackets. This single kind subsumes
  what older drafts called *interfaces* (lanes + roles, no body) and
  *modules* (ports + body) — see `02-semantic-model.md` §3.6 for
  the rationale.
* lowercase `instance_name` — instances and pin/lane names.
* UPPERCASE `NETNAME` — labelled nets.

Mnemonic: **shape == kind**. The bracket pair tells you, at a glance,
which of the seven semantic kinds you're looking at. (Round
parentheses, formerly used for interfaces, are now reserved for
expression grouping.)

Within `<Circuit>`, naming convention telegraphs the circuit's
flavor: `<UPPER>` names a protocol contract (`<SPI>`, `<I2C>`,
`<POWER>`, `<3V3>`); `<snake_case>` names an implementation circuit
(`<boot_flash_slot>`) or a wrapper that decorates a protocol
(`<spi_with_termination>`).

We use `_` as the only word separator. `-` is reserved for arithmetic
and ranges. ASCII identifiers only in the core; UTF-8 identifiers are
permitted in libraries but discouraged.

---

## 1. Files and top-level form

A file is a sequence of top-level **statements**. There are ten
top-level statement forms:

```
top_stmt ::=
    | include_stmt
    | define_stmt
    | alias_stmt
    | prefer_stmt              # §11: project-wide abstract→concrete binding
    | designators_stmt         # §12: ranges and reservations
    | designators_lock_stmt    # §12: locked auto-assignments
    | hint_stmt                # §13: typed design intent for downstream tools
    | constraint_stmt
    | generator_stmt
    | use_stmt                 # top-level instantiation, only legal in board files
```

A file with a `use_stmt` is a *board* file (it produces a netlist). A
file without a `use_stmt` is a *library* file (it just exports
definitions). The compiler will refuse to emit a netlist for a
file with no `use_stmt`.

There is exactly one source-file extension: **`.schemlang`**. Designator
locks, project preferences, and library indexes are all blocks inside
`.schemlang` files, not separate file formats.

### 1.1 `include`

```
include  "path/to/file.schemlang"
include  "std/spi.schemlang" as bus
include  "std/power.schemlang" only { POWER, GND }
```

Path is resolved relative to the including file, then against the
project's `include_path` list. Selective and aliased imports are the
recommended style for any include used by name.

---

## 2. The `define` statement

`define` introduces one new named entity. Its first token is the kind
sigil pair around the new name; what follows is the body, indented.

```
define {NAME}                  # a component
define [NAME]                  # a package
define <NAME>                  # a circuit (protocol, wrapper, or implementation)
```

Optional **inheritance** uses `:` and one or more parents of the same
kind, comma-separated. Order is significant.

```
define <3V3> : <POWER>                                # refinement of a protocol
define {STM32H743VIT6} : {STM32H743}                  # concrete refining concrete
define <buck_3v3> : <buck_template>                   # impl from impl
define <i2c_link> : <I2C>                             # wrapper from protocol
define <smart_link> : <boot_flash_slot>, <thevenin_terminated>, <temp_logged>
```

Inheritance is **left-to-right linearization**: the body of the first
parent is laid down, then the second on top of it, and so on; finally
the new definition's own body is laid down on top of all parents.
"On top of" means: same-named contributions from a later layer
override (or conflict with) earlier ones. The full conflict and
removal rules — and how they vary by kind — are in §2.2.

### 2.1 The `define` body

A body is a sequence of indented statements. Indentation is two spaces,
significant (Python-style). The body's grammar depends on the kind:

| Kind        | Body productions                                              |
|-------------|---------------------------------------------------------------|
| Component   | `pin`, `parameter`, `package_binding`, `view`, `bus`, `bank`, `provides`, `constraint`, `designator_prefix`, `description` |
| Package     | `pin_number`, `pad_shape` (library), `swap_group`             |
| Circuit     | `lane`, `role`, `parameter`, `port`, instance decl, `connect`, `splice`, `constraint`, `generator`, `description` |

A circuit body may freely mix lane/role declarations (the type-flavored
fragment) with ports, instances, connections, and splices (the
implementation-flavored fragment). Different *uses* of a circuit
exercise different fragments — empty-bodied protocol definitions
ignore the impl-flavored productions; pure-implementation circuits
ignore lanes/roles — but the grammar admits all of them in one body.
That's what makes wrappers (a circuit with both lanes and a body)
expressible without a separate kind.

We deliberately **forbid** mixed bodies *across* kinds (e.g. you
cannot inline a `<circuit>` definition inside a `{component}` body).
Every kind is declared at top level of some file. This keeps the
grammar local.

### 2.2 Inheritance, override, and removal

The merge rules in this section are deliberately the same shape across
all three structural kinds. What *differs* between kinds is which
body productions exist and how some of them combine; that table is
at the end of this section.

#### 2.2.1 Linearization

```
define <X> : <A>, <B>, <C>
  ...body...
```

Conceptually elaborates to: empty → apply A → apply B → apply C → apply
own body. Each "apply" is a per-production merge with the prior
accumulator (rules below). The own-body layer is therefore the *final
say* in any conflict — by construction.

Diamond inheritance (two parents share an ancestor) is unproblematic:
an identical contribution from two paths deduplicates. A *non-identical*
contribution from two paths is a conflict and must be resolved
explicitly (see §2.2.4). We do not use C3 or any cleverer linearization;
plain left-to-right is enough because conflicts are surfaced rather
than silently broken.

The parent list is required to be **acyclic**. Cycles are a hard error
detected during name resolution.

#### 2.2.2 Per-production merge rules

Each body production has a documented merge behavior:

| Production           | Merge rule                                              |
|----------------------|---------------------------------------------------------|
| `parameter`          | **Override-by-name.** Later layer's value/predicate replaces earlier. Identical contributions deduplicate. Differing contributions across *parents* (without override in body) is an error. |
| `pin`                | **Union-by-name.** Same-named pin must agree on role and aliases or it is a conflict. Per-pin parameters merge by the parameter rule. |
| `lane`               | **Union-by-name.** Same-named lane must agree on signal type. |
| `role`               | **Union-by-name.** Same-named role merges its `drives` / `receives` / `bidir` sets; a lane appearing in two different direction sets across layers is a conflict. |
| `alias` (circuit)    | **Union-by-name.** Conflicts on RHS are errors. |
| `port`               | **Union-by-name.** Conflicts on type or role are errors. |
| `instance`           | **Union-by-name.** Same-named instance is a conflict; resolve via `override` or `remove`+redeclare. |
| `connect`            | **Union-as-edges.** Connections are accumulated; duplicate `<->` edges deduplicate. |
| `splice`             | **Override-by-lane.** A later layer's `splice CLK with R_clk` replaces an earlier one on the same lane. |
| `constraint`         | **Union-as-set.** Named constraints (`constraint name: …`) override by name; anonymous constraints accumulate. |
| `view` (component)   | **Union-by-(circuit, name).** A view is identified by its circuit type plus its instance name; if a name is omitted, the lowercased circuit name is the implicit name (e.g. `view <POWER> as sink` ≡ name `power`). |
| `package_binding`    | **Union-by-package.** Same package binding from multiple parents must agree pin-for-pin or it is a conflict. |
| `bus` / `bank` / `provides` (component) | **Union-by-name.** A redeclaration replaces the entire definition. |
| `swap_group`         | **Union-as-set.** Identical groups deduplicate. |
| `description`        | **Override.** Later wins; if identical, deduplicate. |
| `generator`          | **Append.** Generators are not named; they accumulate. |

Two principles to remember from this table:

1. Anything keyed by name **overrides** if redeclared in the same body.
2. Anything that came from *separate parents* with the *same name* and
   *different content* is a conflict that needs explicit resolution.

#### 2.2.3 The `override` keyword

To redeclare an inherited member with new content in the body, prefix
the redeclaration with `override`:

```
define <my_link> : <boot_flash_slot>
  override parameter R_pullup : resistance = 4.7 k ± 1 %
  override port pwr : <5V0> as sink         # type-narrowing port refinement
```

`override` is required in two situations and merely allowed in a third:

* **Required** when changing the *type* of an inherited member
  (`port`, `bus`, `parameter`-type-not-just-value).
* **Required** to resolve a conflict between two parents.
* **Allowed** (but optional) for changing only the value of an
  inherited parameter; without `override`, the body's `parameter R_pullup = 4.7 k`
  also wins, but `override` makes the intent explicit and is recommended
  for readability.

`override` on a name that is *not* present in any parent is an error.
This catches typos that would otherwise silently introduce new members.

#### 2.2.4 The `remove` form

To drop an inherited member entirely, use `remove`:

```
remove pin VCC                       # drop a pin
remove parameter R_pullup            # drop a parameter (no value, not optional)
remove port spi                      # drop a port
remove instance pull_cs              # drop an instance
remove view <POWER> pwr              # drop a named view
remove constraint vcc_window         # drop a named constraint
remove package [WSON8]               # drop a package binding
remove parent <thevenin_terminated>  # drop *all* contributions from one parent
```

`remove` is checked: removing a member that something else (in a parent
or in the body) still depends on is an error. The most common cases:

* removing a `port` that an `instance` connects to → error;
* removing an `instance` referenced by a `connect` → error;
* removing a `parameter` referenced by a `constraint` → error.

The fix is to also remove the dependents, or to redeclare the removed
member with new content (in which case use `override`, not `remove`).

The `remove parent` form is the nuclear option: it strips *every*
contribution from a named parent. It's useful when a multi-parent
mixin turns out to clash and you want to drop one wholesale rather
than enumerate its members. Equivalent to never having included the
parent — except that the parent's name remains for documentation.

#### 2.2.5 Per-kind merge specializations

The merge table in §2.2.2 is the union of all kinds' rules. In
practice, only the productions valid for a given kind apply. Here is
the per-kind summary, focused on the parts that differ:

| Kind        | What multi-parent buys you                                                | Special-case rules                                                                                                  |
|-------------|---------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------|
| Component   | Mix in feature pin groups (e.g. `{has_temp_diode}`, `{has_otp}`)          | A given pin number in a package may come from only one source; aliases and roles must agree across layers.         |
| Package     | Compose pin-number ranges with shared swap groups                         | Pin numbers must not collide across parents unless content is identical.                                            |
| Circuit     | Compose lane sets, role tables, ports, and instances; build wrappers      | `lane` signal types **must agree** across parents; refinement parameters (`Vnom`, etc., including any tolerance clause) follow override-by-name. Two parents declaring the same `instance` name is a conflict; use `override instance name = {NewType}` to resolve. `splice <lane>` overrides by lane name. |

#### 2.2.6 Worked example: vendor eval board with surgical edits

Combining everything:

```
include "vendors/efinix/ti375_evk.schemlang"
include "addons/temp_logging.schemlang"   # exports <temp_logging> circuit
include "regulators/buck_3v3.schemlang"

define <my_compute> : <ti375_evk>, <temp_logging>
  # Replace the LDO with a buck of the same port shape.
  override instance power_stage = <buck_3v3>(I_max = 2.0 A)

  # Drop the USB stage entirely.
  remove instance usb_stage

  # Tighten the rail tolerance vs. the reference design. Tolerance is
  # part of the parameter declaration (§7.1.1); override re-states the
  # nominal with the new tolerance.
  override parameter Vnom : voltage = 3.3 V +/- 1 %

  # Resolve a conflict: both parents define `status_led`; we pick
  # the eval board's wiring, drop the temp_logging contribution.
  remove parent <temp_logging>.instance status_led
```

Last line shows fine-grained `remove parent` selection: the
contribution being dropped is qualified by which parent supplied it,
which lets you keep the rest of `<temp_logging>` intact.

---

## 3. Components and packages

### 3.1 Component body

```
define {W25N512GVEIG}
  description       "MEM FLASH 512MBIT SPI/QUAD WSON-8"
  designator_prefix U
  parameter Tr : temperature_range = -40 °C .. 85 °C

  [WSON8]
    pin 1  CS_N         role digital_in
    pin 2  DO  IO1     role digital_io
    pin 3  WP_N IO2     role digital_io
    pin 4  GND         role ground
    pin 5  DI  IO0     role digital_io
    pin 6  CLK         role digital_in
    pin 7  HOLD_N IO3   role digital_io
    pin 8  VCC         role power_in
      parameter Vmin : voltage = 2.7 V
      parameter Vmax : voltage = 3.6 V

  view <POWER> as peripheral
    POSITIVE  VCC
    NEGATIVE  GND

  view <QSPI> as peripheral
    CS_N  CS_N
    CLK  CLK
    IO0  IO0
    IO1  IO1
    IO2  IO2
    IO3  IO3
```

Notable changes from the README:

* `[WSON8]` introduces a **package binding block**, not a "footprint
  for the component." The package itself is defined separately (in a
  package library).
* `pin N name [aliases...] role <pin_role>` is a single line. Multiple
  aliases (the README's `DO IO1`) are first-class: any alias is a
  legal name in connections.
* Per-pin parameters are indented under the pin.
* `view <CIRCUIT> as <role>` replaces the README's bare `(POWER)` block.
  The `as <role>` is required: a peripheral SPI is not the same type
  as a host SPI.
* `designator_prefix U` declares the silkscreen prefix for instances of
  this component. Auto-numbering and override semantics are in
  `05-naming-conventions.md` §5; recap below in §4.4.
* `description` follows the per-category templates in
  `05-naming-conventions.md` §2 — first token is the category, fields
  are space-separated, values include their units.

### 3.2 Package body

Packages live in a separate file and look like:

```
define [WSON8]
  pin 1
    pad   rectangle 0.45 0.30 mm
    at    -2.0  1.5  mm
  pin 2
    ...
  swap_group { 2, 5 }     # example: DO/DI legitimately swappable on this die? no — illustrative only
```

Pad and placement geometry are not part of the language core; they
live in the package library and use a small DSL we'll specify in a
separate document. The relevant point for the *language* is: packages
declare pin numbers and optional swap groups.

### 3.3 Swap groups

A `swap_group { ... }` declares that the listed pins are
interchangeable for routing purposes. The elaborator may permute them
to satisfy other constraints. Swap groups can live on packages
(physically symmetric pads) or on components (logically interchangeable
pins, e.g. spare gates).

### 3.4 Soft-peripheral capabilities (`provides`)

Some parts — FPGAs, CPLDs, and MCUs with flexible I/O matrices — do
not ship with a fixed list of named peripherals. They can synthesize
many UARTs, SPI controllers, or I2C buses on demand, drawing pins
from a shared pool subject to electrical rules (same bank Vio, same
port group, etc.). For these parts the MCU-style `bus` form
(enumerated alt-function tables) is the wrong shape, because *the
peripheral count is not a property of the part*; it is a property of
how many buses the board instantiates.

The `provides` declaration is the right shape: the component declares
*what makes a legal bus*; the board determines *how many*.

```
provides <I2C> as host
  pool pins where role == digital_io
  per_bus
    SDA in pool
    SCL in pool
    where bank(SDA) == bank(SCL)
    where bank(SDA).Vio in { 1.8 V, 2.5 V, 3.3 V }
```

Reading rules:

* **`pool <filter>`** names the candidate set of pins. The filter is
  a path (typically `pins`, the implicit set of the enclosing
  component's pins) optionally narrowed by a `where` clause. Pin
  predicates may reference any pin attribute — `role`, `bank`, or a
  library-defined parameter such as `supports_i2c`.
* **`per_bus`** lists the lanes of the bus and binds each to a draw
  from the pool. The lane names must match the lanes of the
  circuit in the role you declared (`<I2C>.host` here).
* Trailing **`where <expr>`** clauses are predicates the elaborator
  must satisfy *for every allocation*. They reference the lane
  identifiers introduced in `per_bus`.

A component may declare multiple `provides` blocks (one per
circuit type or role). Pools across blocks may overlap freely — they
all reference the same underlying pin set, and the linear-resource
discharge handles disjointness automatically:

```
define {XC7A35T}
  ...
  provides <I2C> as host
    pool pins where role == digital_io
    per_bus
      SDA in pool
      SCL in pool
      where bank(SDA) == bank(SCL)

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

#### 3.4.1 Allocation at the board

A `<component>.<lowercase_circuit>.<name>` reference allocates a fresh
bus instance. The `<name>` is a meaningful identifier chosen by the
board author — what the bus is *for*, not its index:

```
fpga.i2c.imu        <-> sensor1.i2c
fpga.i2c.env        <-> sensor2.i2c
fpga.spi.boot       <-> flash.spi
fpga.uart.debug     <-> debug_header.uart
fpga.uart.gps       <-> gps.uart
fpga.uart.lte       <-> lte_modem.uart
```

Two `<->` lines that mention the same name refer to the *same*
allocation; mentioning a new name allocates a *new* one. Counting is
implicit. There is no `i2c0..i2c99` to thread through — and no
ceiling baked into the source. A part that "runs out" of pins or
logic is caught at synthesis time by the FPGA toolchain, which is
where that judgment belongs.

#### 3.4.2 Pin overrides

If layout demands specific pins for a particular allocation, attach
the constraint to the allocation via the connect-site argument
block (§6.4):

```
fpga.i2c.imu <-> sensor1.i2c
  pin_prefer SDA = fpga.IO_L5P_T0_14     # soft hint to the solver
  pin        SCL = fpga.IO_L5N_T0_14     # hard equality
```

`pin_prefer` participates in the assignment solver as a tie-break
weight; `pin` is a hard equality the solver must satisfy or fail.
Both forms identify the lane by its circuit-side name (`SDA`,
`SCL`) and the FPGA pin by its package-side name.

The keyword is `pin_prefer` (not `prefer`) at the connect site, so
it can't be confused with `prefer <Abstract> = <Concrete>` —
which is also legal in the connect-site argument block but
overrides the *wrapper choice* for the bus, not a pin assignment.

#### 3.4.3 `provides` vs. `bus`

| | `bus` (§ component body)                  | `provides` (§3.4)                    |
|---|------------------------------------------|--------------------------------------|
| Models                              | MCU peripheral with fixed alt-fn table     | FPGA-style soft-peripheral factory   |
| Count                               | one named instance per declaration         | unbounded, materialized at the board |
| Pin choice                          | from an enumerated list of bank rows       | from a pool, subject to predicates   |
| Reference                           | `mcu.spi1`                                 | `fpga.spi.<name>`                    |
| Solver regime                       | choose-one-row from a small table          | discharge a linear-resource problem  |

A part with both kinds of peripherals (a few hard ones plus a
flexible I/O matrix) may use both forms in the same body.

---

## 4. Circuits

A circuit is one kind with three flavors of body, distinguished only
by what gets declared. The grammar admits all three side-by-side; a
single circuit may exercise more than one.

### 4.1 Protocol-flavored: lanes, roles, optional refinement

A protocol circuit declares lanes and roles. Body is otherwise empty.

```
define <SPI>
  lane CS_N   : digital
  lane CLK   : digital
  lane MOSI  : digital
  lane MISO  : digital

  role host
    cardinality = 1
    drives   { CS_N, CLK, MOSI }
    receives { MISO }

  role peripheral
    cardinality = 1
    receives { CS_N, CLK, MOSI }
    drives   { MISO }
```

A `role` block lists the lanes by direction and the role's
participant count via `cardinality`. The cardinality form accepts:

```
cardinality = N           # exactly N participants in this role on a bus
cardinality >= N          # at least N
cardinality <= N          # at most N
cardinality in [LO, HI]   # closed range
cardinality = *           # unbounded (any count, including zero)
```

The default is `cardinality = 1` (point-to-point). Multi-drop
protocols declare `cardinality >= 1` (or higher minima) for the
roles that admit multiple participants. The elaborator enforces
the cardinality on each bus instance; a violation is a UNSAT
participant-count constraint reported with all contributing
connect sites.

```
define <I2C>
  lane SDA, SCL : digital

  role host
    cardinality = 1
    drives    { SCL }
    bidir     { SDA }
  role peripheral
    cardinality >= 1
    receives  { SCL }
    bidir     { SDA }
```

Every lane must appear in exactly one direction set per role.
The `bidir` direction is allowed for lanes like I²C `SDA`.

Protocol circuits inherit:

```
define <QSPI> : <SPI>
  lane IO0 : digital
  lane IO1 : digital
  lane IO2 : digital
  lane IO3 : digital
  # MOSI/MISO are deprecated names; SPI mode reuses IO0/IO1
  alias MOSI = IO0
  alias MISO = IO1

  role host       extends <SPI>.host
    drives { IO2, IO3 }     # in QSPI mode, all lanes are bidir; simplification
  role peripheral extends <SPI>.peripheral
    drives { IO2, IO3 }
```

`alias L = M` inside a circuit declares an additional name for an
existing lane. This is how `<QSPI>` can be projected back onto `<SPI>`
without renaming.

#### 4.1.1 Refinement

```
define <3V3> : <POWER>
  parameter Vnom : voltage = 3.3 V
  constraint POSITIVE.V in [3.3 V × 0.97, 3.3 V × 1.03]
```

`<3V3>` is a refinement: it is a `<POWER>` plus a predicate. Connecting
a `<3V3>` net to a pin requiring `<POWER>` is fine; the reverse
requires an explicit downcast (`as <3V3>` with a check).

#### 4.1.2 Derived attributes (`derive`)

A role or a `bus`-port may publish **derived attributes** that the
elaborator posts as equality constraints when the port joins a bus
instance. This is the mechanism by which a wrapper's `vref` port
auto-binds to whatever rail the host's bank actually carries,
without forcing the connect site to spell it out.

Inside a role:

```
define <I2C>
  lane SDA, SCL : digital

  role host
    cardinality = 1
    drives  { SCL }
    bidir   { SDA }
    derive vref = bank(SDA).vio_port    # a <POWER> port path
    derive Vio  = bank(SDA).Vio          # a voltage variable

  role peripheral
    cardinality >= 1
    receives { SCL }
    bidir    { SDA }
    derive vref = supply.vio_port        # typically the device's own VDD pin
    derive Vio  = supply.Vio
```

Inside a `bus`-port on a component (overrides or augments the
role-level derivations):

```
bus i2c0 : <I2C> as host
  lane SDA <-> bank2.PB7
  lane SCL <-> bank2.PB8
  derive vref = bank2.vio_port
```

Reading rules:

* The right-hand side is a path expression evaluated in the port's
  scope. It may reference any constraint variable visible there
  (banks' Vio variables, supply ports, sibling parameters).
* The name on the left matches a non-lane port or parameter name on
  the wrapper that materializes for the bus instance. Names a
  wrapper does not understand are silently ignored — a port can
  publish `derive vref` even for wrappers without a `vref`.
* Multiple participants' derivations of the same name produce
  multiple equality constraints; the solver unifies them. If they
  cannot be unified the elaborator emits a UNSAT core citing each
  participant's source.
* A wrapper's mandatory non-lane port that gets no derivation and
  no connect-site override is an UNSAT obligation, reported with
  the wrapper definition and the bus instance's source.

`derive` always posts equality constraints. It is not eager
evaluation: `derive Vio = bank(SDA).Vio` does not "fetch" a
voltage, it asserts that the wrapper's `Vio` and the bank's
`Vio` are the same variable.

#### 4.1.3 Per-port wrapper preferences (`prefer wrapper`)

A circuit-typed port (or a `bus`-port participant) may declare a
soft preference for the wrapper that mediates its bus instance:

```
port i2c : <I2C> as peripheral
  prefer wrapper = <I2C>             # "I have internal pull-ups, no wrapper needed"

bus i2c0 : <I2C> as host
  prefer wrapper = <i2c_link>        # "bare CMOS GPIO, please pull me up"
  prefer wrapper = <i2c_link>(R_pu = 2.2 k) weight 8   # stronger preference, with params
```

The form is `prefer wrapper = <Type>` optionally followed by
`(<call-args>)` and `weight <int>`. Default weight is 5. The
preference contributes a soft equality constraint on the bus
instance's wrapper-choice variable; it can be outvoted by other
participants' preferences and by the abstract's `resolution`
block (§4.1.4), and is hard-overridden by lexical-scope `prefer`
or connect-site `prefer` (§11).

Per-port preferences travel with the device model — a sensor
declares its preference once, every board gets it for free.

#### 4.1.4 Wrapper resolution (`resolution`)

A protocol circuit may carry a `resolution` block describing how
to combine participant preferences into a wrapper choice:

```
define <I2C>
  lane SDA, SCL : digital
  ...

  resolution
    case any(prefers <i2c_link>)  => <i2c_link>     weight 10
    case all(prefers <I2C>)       => <I2C>          weight 5
    default                       => <i2c_link>     weight 1
```

Each `case` posts a soft-equality constraint on the bus
instance's wrapper-choice variable, gated by a quantifier over
the role-multiset:

* `any(prefers <X>)` — at least one participant has
  `prefer wrapper = <X>` declared.
* `all(prefers <X>)` — every participant prefers `<X>`.
* `none(prefers <X>)` — no participant prefers `<X>`.
* `count(prefers <X>) >= N` / `<= N` — count constraint over
  participants.

The `default` case posts a baseline weight (always satisfied) so
the variable always has *some* binding. Cases are evaluated
together, not in order; the *weight*, not the case order,
determines who wins.

Resolution rules are soft. A user who wants deterministic
behavior in the face of conflict uses a scope-level or
connect-site `prefer` (§11).

The standard library biases conservative: `<I2C>`'s default
resolution prefers `<i2c_link>` (the wrapper) — see
`02-semantic-model.md` §3.5.3 for the rationale.

### 4.2 Implementation-flavored: ports, instances, connections

An implementation circuit has typed ports and a body that wires up
internal instances. This is what older drafts called a *module*.

```
define <boot_flash_slot>
  # type and value parameters
  type     PART : {SPI_flash}    # subtyping over a "kind" is library-defined
  parameter R_pullup : resistance = 10 k ± 1%

  # ports (typed by other circuits)
  port pwr  : <3V3>  as peripheral
  port spi  : <SPI>  as peripheral

  # instances
  flash    = {PART}
  pull_cs  = {R_pullup}     # abstract; resolved by `prefer` at the project root

  # connections
  flash.<POWER> <-> pwr
  flash.<SPI>   <-> spi
  pull_cs.1     <-> spi.CS_N
  pull_cs.2     <-> pwr.POSITIVE

  # constraints
  constraint flash.Vmin <= pwr.Vnom <= flash.Vmax
```

An implementation circuit:

* declares its **signature** (parameters + ports) at the top of the
  body, before any instances;
* then declares **instances** with `name = {Type}` or `<Type>`;
* then declares **connections** with `<->`;
* then declares **constraints**;
* may interleave `generate` blocks anywhere instances or connections
  could appear.

### 4.3 Wrapper-flavored: a protocol with a body

A wrapper circuit inherits from a protocol circuit and adds a body —
typically pull-ups, series termination, level translation, or
conditional structure that depends on parameters of the connection
(trace length, edge rate, reference rail).

```
define <i2c_link> : <I2C>
  parameter Vio  : voltage    = 3.3 V
  parameter R_pu : resistance = 4.7 k ± 1 %

  pull_sda = {RESISTOR}(R = R_pu)
  pull_scl = {RESISTOR}(R = R_pu)

  pull_sda.T1 <-> SDA               # pull-ups are *incident* on the lane
  pull_sda.T2 <-> NET_VIO           # the rail must be supplied at the connect site
  pull_scl.T1 <-> SCL
  pull_scl.T2 <-> NET_VIO
```

For *series* insertion (an item between the host and peripheral sides
of a lane), use `splice`:

```
define <spi_with_termination> : <SPI>
  parameter trace_length : length    = 0 mm
  parameter f_max        : frequency = 1 MHz
  parameter velocity     : dimensionless = 0.6              # of c
  parameter Z0           : resistance     = 50

  parameter elec_len : dimensionless =
      trace_length * f_max / (velocity * speed_of_light)

  generate if elec_len > 0.1
    R_clk  = {RESISTOR}(R = Z0)
    R_mosi = {RESISTOR}(R = Z0)
    R_cs   = {RESISTOR}(R = Z0)

    splice CLK   with R_clk
    splice MOSI  with R_mosi
    splice CS_N   with R_cs
    # MISO is driven by the peripheral; terminate at that end if needed
```

#### 4.3.1 `splice` semantics

`splice <lane> with <component>` is sugar for series insertion of a
two-terminal component on a lane that the wrapper exposes:

```
splice CLK with R_clk
```

is equivalent to:

```
CLK.host_side  <-> R_clk.T1
R_clk.T2       <-> CLK.peri_side
# (the implicit through-wire CLK.host_side <-> CLK.peri_side is suppressed)
```

`<component>` must be two-terminal (`.T1`, `.T2`). For asymmetric or
multi-pin insertion (level shifters, FET passgates, transformers),
write the explicit endpoint references; any explicit connection
involving `<lane>.host_side` or `<lane>.peri_side` suppresses the
default through-wire on that lane.

Pull-ups and other *incident* (parallel) connections do **not** need
`splice`: they reference the lane directly and add an additional pin
to its net.

#### 4.3.2 Implicit wrapping at the bus instance

A **bus instance** (`02-semantic-model.md` §3.4) is the
elaboration-layer entity that mediates a circuit-typed connection.
Multiple `<->` lines that name the same path on one side join the
same bus instance:

```
fpga.i2c.imu <-> imu.i2c           # creates bus-instance "fpga.i2c.imu"
fpga.i2c.imu <-> env.i2c           # joins the existing bus-instance
fpga.i2c.imu <-> connector.i2c     # joins again; three participants now
```

Wrappers attach to *bus instances*, not to individual `<->` edges.
A multi-drop bus gets one wrapper materialized for the whole bus,
not one per edge.

When the bus instance is formed, the elaborator looks up the
prevailing concrete type for `<C>` via `prefer` (§11) and per-port
preferences (§4.1.3) combined by the abstract's `resolution` block
(§4.1.4). The chosen wrapper materializes once with all
participants' lanes unified.

* If no `prefer` rule is in scope and `<C>`'s own body is empty,
  the wrapper is the trivial wire-through and no instance is
  materialized.
* If `<C>`'s own body is non-empty (e.g. `<I2C>` ships with default
  pull-ups), it is the wrapper.
* If a project-level `prefer <C> = <wrapper>` is in scope, that
  wrapper is used (hard binding).
* Otherwise the resolution combines participant `prefer wrapper`
  declarations.

The `host` and `peri` ports of a wrapper instance are implicit; users
ordinarily never name them. They become visible only when an
allocation is bound to a name (`link = <i2c_link>(...)`), in which
case `link.host <-> mcu.i2c1` and `link.peri <-> sensor.i2c` are the
explicit forms.

For multi-drop wrappers (e.g. `<i2c_link>` with `cardinality >= 1`
on peripheral), `peri` is a multiset; the wrapper's body sees a
single host and N peripheral participants, each unified per lane.

#### 4.3.3 Role cardinality enforcement

Each role's cardinality is enforced at the bus instance. A
`<SPI>` bus with two host participants is a UNSAT
participant-count constraint reported with both source locations:

```
ERROR: <SPI>.host has cardinality = 1; bus mcu.spi.boot has 2 participants
       tagged 'host'.
   1. mcu.spi1   <-> mcu_spi.spi          at boards/test.schemlang:42
   2. fpga.spi.boot <-> mcu_spi.spi       at boards/test.schemlang:43
   Resolve by giving one of the hosts a different bus name, or by
   choosing a multi-master variant of the protocol.
```

`cardinality = *` (unbounded) is permitted but discouraged for
electrical protocols — it suppresses a useful sanity check. Use it
only for genuinely unbounded multi-drop scenarios (CAN, RS-485 with
a dynamic node count).

### 4.4 Designator overrides on instances

Instances of components whose type declared a `designator_prefix`
receive an auto-assigned reference designator (`R1`, `C7`, `U3`, …)
during elaboration. The auto-assignment, lock-file, and reservation
behavior is specified in `05-naming-conventions.md` §5; this section
covers the source-level forms.

Two equivalent forms exist for explicit override:

```
mcu = {STM32H743VIT6}
  designator U1                     # body-field form
  description_override "MAIN MCU"   # (other body fields can follow)

power_led = {LED_RED} @ D5          # inline form, postfix `@`
```

The body form is recommended when the instance has other attributes
(parameter overrides, comments) that would already break it onto
multiple lines; the inline `@` form is recommended for one-off pins
in dense board files.

Override rules (per `05-naming-conventions.md` §5.3):

* Two instances claiming the same designator is an error.
* An override whose prefix disagrees with the component's
  `designator_prefix` is an error (`{CAPACITOR} @ R3` fails).
* An overridden number is removed from the auto-assignment pool.

### 4.5 Instantiation and use

```
use <board_top>          # elaborate this circuit as the top-level netlist
```

The `use` statement is only legal in a board file and only once. It
takes circuit-instantiation arguments if needed:

```
use <board_top>(variant = "rev_b")
```

---

## 6. Connections

### 6.1 The connect operator

`<->` connects two ports, two pins, or any combination. It is **always
symmetric**.

```
mcu.spi1     <-> flash.spi              # bus to bus
vcc          <-> u1.VDD                 # net to pin
mcu.PA5      <-> flash.IO0              # pin to pin
NET_3V3      <-> reg.OUT                # labeled net to pin
```

When the operands are bus-typed, unification is structural (lane by
lane), and the result is a **bus instance** (`02-semantic-model.md`
§3.4) — not a per-edge wire-up. Multiple `<->` lines naming the
same path on one side join the same bus instance:

```
fpga.i2c.imu <-> imu.i2c            # creates bus instance "fpga.i2c.imu"
fpga.i2c.imu <-> env.i2c            # joins the existing bus instance
fpga.i2c.imu <-> connector.i2c      # joins again; three participants now
```

When operand types differ, the language attempts a unique
projection; ambiguous projections are an error.

### 6.2 Net labels

Naming a net is optional. Two ways to give a name:

```
NET_3V3 = reg.OUT                       # introduce a label
NET_3V3 <-> mcu.VDD                     # subsequently used as a name
```

A net label is **not a connection**. Two pins both connected to a net
called `VCC` in different circuits are not joined unless someone
writes `<->` between them or routes them through a circuit port.

### 6.3 Many-pin shorthand

```
{ a.1, b.1, c.1 } <-> NET_GND
```

Curly-braced **set literals** are syntactic sugar for "connect each
member to the right-hand side." This avoids repetitive lines without
introducing a new operator.

### 6.4 Connect-site argument block

A `<->` line may carry an indented argument block whose content
overrides parameters and bindings on the bus instance the
connection joins. The block applies to the *bus*, not the *edge*;
two `<->` lines on the same bus name with conflicting argument-block
contents is a UNSAT conflict (reported with both source locations).

```
mcu.i2c0 <-> imu.i2c
  vref         = mcu.vio_3v3        # bind a non-lane wrapper port to a path
  parameter R_pu = 2.2 k            # override a wrapper parameter
  parameter Vio  = 3.3 V             # another wrapper parameter
  prefer <I2C>   = <i2c_link>(R_pu = 2.2 k)  # override wrapper choice for this bus
  pin     SCL    = fpga.PB7          # pin a lane to a specific package pin
  pin_prefer SDA = fpga.PB8          # soft pin preference (CSP tie-break)
```

The block admits one or more of:

| Form                                  | Effect on the bus instance                              |
|---------------------------------------|---------------------------------------------------------|
| `<name> = <path>`                     | Bind the wrapper's non-lane port `<name>` to `<path>` (e.g. `vref = mcu.vio_3v3`). |
| `parameter <name> = <value>`          | Override a wrapper parameter `<name>` to `<value>`.     |
| `prefer <abstract> = <concrete>`      | Hard-override the wrapper choice for this bus instance (priority 1, beats scope-level `prefer`). May supply call-args. |
| `prefer wrapper = <wrapper>`          | Equivalent shorthand when the abstract is implied by the connection. |
| `pin <lane> = <pin_path>`             | Hard equality: the named lane must use this package pin. |
| `pin_prefer <lane> = <pin_path>`      | Soft preference: solver tie-break weight on this lane's pin choice. |
| `hint <kind> [<text>] [<block>]`      | Attach a hint to the bus instance (target is implicit). |
| `derive <name> = <path>`              | Add a derive equality to this connection's contribution to the bus (rarely needed; usually published on the role). |

The lookup namespace for `<name>` (in the bare `<name> = <path>`
form and in `parameter <name> = …`) is the wrapper definition's
non-lane ports and parameters. Lane names (`SDA`, `SCL`, `CLK`,
…) are reserved for `pin` / `pin_prefer` and are never looked up
as wrapper ports.

#### 6.4.1 Mandatory wrapper-port obligations

If a wrapper materializes for the bus and has a mandatory non-lane
port (one without `optional` modifier and without a matching
`derive` from any participant), the elaborator emits an UNSAT
obligation citing the wrapper definition and the bus instance. The
fix is one of:

* Add a `<port_name> = <path>` line to a connect-site argument
  block on the bus.
* Add a `derive <port_name> = …` to a participating port or role.
* Mark the port `optional` in the wrapper definition.

#### 6.4.2 Multiple connect sites contributing to one bus

Each `<->` line that joins a bus contributes to the bus's
constraint store. Argument-block entries from different lines on
the same bus accumulate. Conflicting same-name entries (e.g. two
different values for `parameter R_pu`) are an UNSAT conflict
unless one is marked `override`:

```
mcu.i2c0 <-> imu.i2c
  parameter R_pu = 4.7 k

mcu.i2c0 <-> env.i2c
  override parameter R_pu = 2.2 k      # explicitly wins; logged in explain
```

Without the `override`, the elaborator emits:

```
ERROR: bus mcu.i2c0 has conflicting parameter R_pu values:
   1. parameter R_pu = 4.7 k      at boards/foo.schemlang:42
   2. parameter R_pu = 2.2 k      at boards/foo.schemlang:50
   Add `override` on one to express which wins.
```

---

## 7. Parameters and constraints

### 7.1 Parameter syntax

A parameter has an optional *domain clause* and an optional *default
clause*. The two are independent — write either, both, or neither:

```
parameter NAME : TYPE = VALUE                       # default only (singleton)
parameter NAME : TYPE = VALUE TOLERANCE             # default with tolerance (interval-valued; §7.1.1)
parameter NAME : TYPE in [LO, HI]                   # domain only — continuous range
parameter NAME : TYPE in { V1, V2, V3 }             # domain only — finite enumerated set
parameter NAME : TYPE <= UPPER                      # domain only — half-open
parameter NAME : TYPE in [LO, HI] = VALUE           # domain + default
parameter NAME : TYPE in [LO, HI] = VALUE TOLERANCE # domain + toleranced default
parameter NAME : TYPE                               # neither (free variable; expects derive/bind)
parameter NAME : TYPE
  domain { V1, V2, V3 }                             # explicit domain block (equivalent to `in {...}`)
```

`TYPE` is one of the unit-types from the semantic model
(`voltage`, `current`, ...) or a user-defined refinement.

Every `parameter` is a **constraint variable** in the elaboration
store, not a literal value. A parameter declared with a singleton
domain (`= 4.7 k`) is a variable bound by exactly one hard
constraint; one declared with a range or set is bound only when
other constraints (derivations, connect-site overrides, refinement
checks) narrow the domain to a single value. See
`02-semantic-model.md` §5 and §12 for the substrate's variable
types.

A parameter with no domain restriction (`parameter Vio : voltage`)
is a free variable whose domain is the type's full natural range
(physical voltages in R+). Such parameters expect to be bound by
something else: a connect-site `parameter Vio = ...`, a
`derive Vio = ...` contributed by a port, or a constraint that
narrows the domain.

#### 7.1.1 Tolerance forms

A parameter may carry a **tolerance clause** after its nominal value.
A toleranced parameter is *interval-valued* — every reference to it
exposes `.Vnom`, `.Vlo`, and `.Vhi`, and `in` constraints over it
read as containment of that interval (see `02-semantic-model.md`
§5.1 and §6.1).

The grammar is intentionally permissive — engineers should not have
to translate datasheet tolerance forms into a single canonical
spelling:

```
parameter R   : resistance = 4.7 k +/- 1 %             # symmetric percent
parameter Vin : voltage    = 3.3 V +/- 100 mV          # symmetric absolute
parameter Vin : voltage    = 3.3 V +- 100 mV           # alias spelling for symmetric
parameter Vin : voltage    = 3.3 V + 5 %, - 1 %        # asymmetric percent
parameter Vin : voltage    = 3.3 V + 0.1 V, - 0.05 V   # asymmetric absolute
parameter Vin : voltage    = 3.3 V + 5 %               # one-sided up   (Vlo == Vnom)
parameter Vin : voltage    = 3.3 V - 0.1 V             # one-sided down (Vhi == Vnom)
parameter Vin : voltage    = 3.3 V + 5 %, - 0.1 V      # mixed kinds
```

Rules:

* `+/-` (canonical) and `+-` (alias) introduce a symmetric tolerance.
* A comma-separated list of one or two directional terms — `+ <amount>`
  and/or `- <amount>` — introduces an asymmetric or one-sided
  tolerance. Order is free; each direction may appear at most once.
* Each `<amount>` is a percentage (`5 %`, applied to the nominal) or
  an absolute quantity in the parameter's natural unit (`0.1 V`,
  `50 mA`, `2 k`). Percent and absolute amounts may be mixed across
  the two sides.
* An omitted direction defaults to **zero** (the bound on that side
  equals the nominal). This is the right reading for one-sided
  datasheet figures (e.g. an LDO output whose data sheet only quotes
  a positive overshoot tolerance).

A parameter without a tolerance clause is point-valued
(`Vlo == Vnom == Vhi`) and behaves identically to the existing
non-toleranced semantics.

`02-semantic-model.md` §5.1 has the full desugaring table.

### 7.2 Constraint syntax

```
constraint <expr>                          # unnamed hard constraint
constraint <name> : <expr>                 # named hard constraint (override-by-name)
constraint over <set> { <expr> }           # aggregate hard constraint
constraint <expr> soft <weight>            # unnamed soft constraint
constraint <name> : <expr> soft <weight>   # named soft constraint
```

`<expr>` is built from comparisons (`==`, `<=`, `<`, `>=`, `>`, `in`),
arithmetic (`+`, `-`, `*`, `/`, parens), aggregations (`sum`, `max`,
`min`, `count`), and parameter references via `name.parameter`.
Field access on an interval-valued operand exposes the worst-case
bounds: `net.V.Vlo`, `net.V.Vhi`, and `net.V.Vnom`.

The `in` operator has *interval-aware* semantics: when at least one
operand is interval-valued (a toleranced parameter, a net inheriting
from a toleranced rail, etc.), `x in [lo, hi]` reads as containment
(`lo <= x.Vlo AND x.Vhi <= hi`), not point membership. For
point-valued operands the meaning collapses to ordinary
`lo <= x <= hi`. This is the rule that makes "a 3.3 V +/- 3 % rail
into a part with `Vmax = 3.3 V`" correctly fail at compile time.
See `02-semantic-model.md` §6.1 for the worked example.

Hard constraints must hold; soft constraints carry non-negative
weights and the solver maximizes their sum. See
`02-semantic-model.md` §6 and §12.3 for the substrate's hard /
soft distinction.

Aggregation example:

```
constraint over { p in mcu.bank_A.pins } {
    sum(p.I_load) <= mcu.bank_A.I_max
}
```

The `over` set syntax is a small comprehension: it iterates a path
expression that the elaborator can resolve at compile time.

### 7.3 Soft preferences as syntactic sugar

Common soft-preference patterns desugar to `constraint … soft …`:

| Surface form                                                  | Desugars to                                                            |
|---------------------------------------------------------------|------------------------------------------------------------------------|
| `prefer wrapper = <X>` (in port body)                         | `constraint bus_instance.wrapper == <X> soft 5`                        |
| `prefer wrapper = <X> weight 8` (in port body)                | `constraint bus_instance.wrapper == <X> soft 8`                        |
| `pin_prefer SDA = fpga.PB7` (in connect-site)                 | `constraint bus_instance.SDA.pin == fpga.PB7 soft 5`                   |
| `case any(prefers <X>) => <Y> weight W` (in resolution)       | conditional soft equality on the wrapper-choice variable, weight W     |

The desugarings are documented here for the curious; in normal
source you write the surface forms and let the elaborator translate.

### 7.4 Refinement constraints and deferred numeric obligations

Some refinement predicates involve nonlinear arithmetic:

```
constraint pull_strength : R_pu * C_bus < 1 / (3 * f_max)
```

The substrate (§12.4) does not admit nonlinear arithmetic. The
elaborator handles such constraints in two steps:

1. If all referenced variables are concrete (singleton domains) by
   the time the solver reaches the constraint, evaluate it as a
   numeric check; pass if `R_pu × C_bus < 1 / (3 × f_max)` holds,
   fail with a UNSAT obligation otherwise.
2. If any referenced variable is unbound after solving, emit the
   constraint to the residual (§13) as a *deferred numeric
   obligation*. Downstream tools that bind the missing variables
   are responsible for re-checking.

This is how refinement-style predicates coexist with a strictly
linear substrate: they're eagerly checked when concrete, and
deferred otherwise.

---

## 8. Generators

### 8.1 `generate for`

```
generate for i in 0 .. 7
  led[i]     = {LED_RED}
  res[i]     = {RES_330}
  led[i].A  <-> res[i].1
  res[i].2  <-> VCC
  led[i].K  <-> mcu.gpio[i]
```

Indices may be integers (with `..` ranges) or members of an
enumeration. `[i]` after an instance name introduces an array-indexed
family.

### 8.2 `generate if` and `generate match`

```
generate if variant == "rev_b"
  bypass = {C_100nF}
  bypass.1 <-> mcu.VDD
  bypass.2 <-> GND

generate match mcu.package
  case [LQFP-100] : ...
  case [BGA-169]  : ...
```

`if` and `match` are also expanded statically. The condition must be
fixed by the time elaboration reaches the block.

---

## 9. Aliases

The README's `alias {JUMPER} {ERJ-2GE0R00X}` is supported as:

```
alias {JUMPER}     = {ERJ-2GE0R00X}
alias {10k}        = {ERJ-2RKF1002X}
alias <POWER_5V>   = <POWER> where Vnom == 5.0 V
```

An alias creates a new name in the local scope that resolves to the
right-hand side. It is *purely lexical* — it does not create a new
component or circuit.

---

## 9.1 Abstract aliases

An alias may target a *kind* (an abstract component type) instead of
a specific MPN. Such an alias is **abstract** — it represents a
requirement, not a chosen part:

```
alias {R_10k}      = {RESISTOR}(R = 10 k, Tol = 1 %)
alias {C_100n}     = {CAPACITOR}(C = 100 nF, V_max = 16 V)
alias {LDO_3V3}    = {LDO}(V_out = 3.3 V, I_max = 300 mA)
```

Abstract aliases compile only when a `prefer` rule (§11) is in scope
that picks a concrete implementation, or when used in a context that
already constrains the choice. Without resolution, the elaborator
errors with the unresolved alias and the location that needs it.

A **concrete** alias targets an MPN and is always usable directly:

```
alias {R_10k_PRECISION} = {ERJ-2RKF1002X}     # specific Panasonic part
```

This is the same `alias` form syntactically; the abstract/concrete
distinction is determined by whether the right-hand side names a
concrete component (one with no further unresolved abstracts) or not.

---

## 10. Lexical conventions

* Comments: `#` to end of line; `{- ... -}` for block comments. (Earlier
  drafts used `--` for line comments; the new spelling matches Python /
  shell tradition and frees the `#` glyph from the active-low pin
  convention discussed below.)
* Strings: double-quoted UTF-8.
* Numbers: `1`, `1.5`, `1.5e-3`, `10k`, `2.2u` (with SI prefix when a
  unit follows: `10 k`, `2.2 uF`).
* Units: written after the number with a space. **The language is
  ASCII-only** — `u` is the SI prefix for micro (`uF`, `uH`, `us`),
  `Ohm` is the unit symbol when one is needed. The Unicode glyphs `µ`
  / `μ` / `Ω` do not appear in source. **Resistance values omit
  the unit entirely**: when the parameter is typed `resistance` (or
  the context unambiguously implies ohms), `R = 4.7 k` and `R = 22`
  are read as 4.7 kilo-ohms and 22 ohms.
* Tolerances are written `+/-` (or the shorter `+-`); `±` is not a
  lexeme. Example: `parameter R : resistance = 10 k +/- 1 %`.
* Indentation: two spaces. Tabs are an error.
* Identifiers: `[A-Za-z_][A-Za-z0-9_]*`. The `#` glyph is *not* part of
  any identifier; active-low signals use the `_N` suffix convention
  (`CS_N`, `WP_N`, `HOLD_N`, `RESET_N`).

---

## 11. Project preferences (`prefer`)

A `prefer` declaration binds an **abstract** type to a **concrete**
one for the rest of the elaboration. It applies uniformly to both
component kinds (`{ABSTRACT} = {VENDOR}`) and circuit kinds
(`<PROTOCOL> = <wrapper>`). The two uses are the same form with the
same scoping rules; the second is what makes "every I2C bus has
pull-ups" or "every SPI link gets conditional source termination" a
one-line project policy.

```
# project.schemlang or board file
include "vendors/panasonic/erj.schemlang"
include "vendors/murata/grm.schemlang"
include "vendors/diodes_inc/ap2127k.schemlang"
include "std/spi.schemlang"
include "std/i2c.schemlang"

# Vendor selection for abstract component types:
prefer  {RESISTOR}     = {ERJ-PHF}                  # thick film, 1 % stocked
prefer  {CAPACITOR}    = {GRM}                      # Murata GRM (X7R/C0G auto)
prefer  {LDO}          = {AP2127K}                  # Diodes Inc fixed-V LDO

# Wrapper selection for protocol circuits:
prefer  <I2C>          = <i2c_link>                 # pull-ups on every I2C bus
prefer  <SPI>          = <spi_with_termination>     # conditional source term
```

The right-hand side of a component-`prefer` is a **parametric concrete
component** — a vendor part defined to accept the same parameters the
abstract carries (`R`, `Tol`, `P_max`, …) and pick the right MPN. The
right-hand side of a circuit-`prefer` is a **wrapper circuit** that
inherits from the abstract protocol and adds a body
(pull-ups, splices, conditional structure). See `02-semantic-model.md`
§3.4 for how wrappers are inserted at direct connections.

### 11.1 Resolution rules

`prefer` statements at the **top-level** or inside a **define-body**
are *hard* bindings; they compose **lexically**, innermost-wins.
This is the normal case.

`prefer` statements at a **connect-site** (in a connect-site
argument block, §6.4) are *also* hard bindings, with priority
strictly higher than any enclosing scope. They override the
scope-level binding for that one bus instance only.

`prefer` statements with an explicit `soft <weight>` modifier
desugar to soft equality constraints (§7.3). These are how
participants and resolution rules contribute their preferences;
authors don't usually write `soft` by hand at the `prefer`
keyword.

The full priority order for a wrapper-choice variable (from
strongest to weakest):

1. **Connect-site** `prefer <C> = <X>` in an argument block.
2. **Lexical-scope** `prefer <C> = <X>` (innermost-wins among
   nested scopes).
3. **Resolution-block soft cases** (§4.1.4) on `<C>`'s abstract,
   evaluated against participants' `prefer wrapper = …`
   declarations.
4. **Default** — the abstract `<C>` itself.

The wrapper-choice variable's solver-bound value is the
maximum-weight satisfiable assignment under hard constraints (1
and 2 are hard at infinite priority; 3 is soft; 4 is the default
of the abstract carrying the singleton self-binding).

* When two `prefer` rules in the same scope target the same abstract
  *with hard binding*, it is an error. Resolve with explicit
  override at the next scope.
* When two soft cases at the same weight target the same
  wrapper-choice variable with conflicting RHSs, the elaborator
  emits `wrapper_choice_ambiguous` listing the candidates.
* When no `prefer` rule covers an abstract use, behavior depends on
  the kind:
  * **Components**: the elaborator errors with a list of needed
    bindings and the source locations that referenced them. A
    netlist needs concrete parts.
  * **Circuits**: the abstract's own body is used. A connection
    between two `<SPI>` ports with no `prefer <SPI>` in scope and
    `<SPI>` itself empty-bodied is just wires — that's fine.

### 11.2 Where `prefer` lives

Three idiomatic locations:

1. **Per-project `prefer.schemlang`** at the root, included by every
   board file: the team-wide vendor and protocol-wrapper policy.
2. **Inside the board file**, near the top after `include` statements:
   board-specific deviations from team policy (e.g. this board has
   pre-existing pull-ups on its I2C lines, so `prefer <I2C> = <I2C>`
   to suppress the default wrapper).
3. **Inside a circuit body**, scoped to that subsystem: e.g. a noisy
   analog front-end that wants C0G capacitors regardless of the rest
   of the board's GRM choice, or an isolated I2C segment that uses a
   different wrapper than the rest of the design.

### 11.3 Example resolution

```
include "std/passives.schemlang"   # exports abstract {RESISTOR}, {CAPACITOR}
include "std/i2c.schemlang"        # exports <I2C> and <i2c_link>
include "vendors/panasonic/erj.schemlang"

alias {R_10k} = {RESISTOR}(R = 10 k, Tol = 1 %)
prefer {RESISTOR} = {ERJ-PHF}
prefer <I2C>      = <i2c_link>(R_pu = 4.7 k)

# in a circuit body somewhere:
pull = {R_10k}
# elaborates to {ERJ-PHF}(R = 10 k, Tol = 1 %), then to a specific
# MPN (e.g. ERJ-PB6F1002V) via the vendor library's MPN selector.

mcu.i2c0 <-> sensor.i2c
# elaborates to a fresh <i2c_link>(R_pu = 4.7 k) instance whose
# host side is unified with mcu.i2c0 and peri side with sensor.i2c;
# the wrapper's two pull-up resistors are themselves {R_pu}-valued
# {RESISTOR}s and resolve via the {RESISTOR} prefer rule above.
```

The vendor library's responsibility is to define parametric concrete
parts (`{ERJ-PHF}`) that pick stocked MPNs from parameters. The
protocol library's responsibility is to define wrappers
(`<i2c_link>`) that decorate the protocol's lanes. The language does
not specify how — these are library concerns.

---

## 12. Designator management

Two top-level forms manage reference designators across a board.
Both live in `.schemlang` files; there are no separate file formats.

### 12.1 `designators` — ranges and reservations

```
designators
  start_at  R = 100              # first auto-R is R100
  reserve   C = { 1..10 }        # never auto-assign C1..C10
  prefix    test_point = TP      # map an instance-name pattern to a prefix
```

Used to satisfy assembly conventions (e.g. "R1..R99 are reserved for
the analog board, R100+ for the digital board"). Default behavior
covers most cases.

### 12.2 `designators_lock` — stable auto-assignments

```
designators_lock <eval_board>
  R1 = flash_slot.pull_cs
  R2 = led_array.res[0]
  R3 = led_array.res[1]
  ...
  U1 = mcu                       # explicit @ U1 in source
  U2 = reg                       # explicit `designator U2` in source
  U3 = flash_slot.flash          # auto-assigned, locked
```

When a `designators_lock` block exists for a circuit, the elaborator
treats its bindings as authoritative for that circuit's instances.
Newly-added instances pick up the next available number per prefix.
A binding for an instance that no longer exists triggers a
`stale_lock` warning; a missing binding for a new instance triggers
an `unlocked_instance` info note.

The compiler emits a fresh `<board>.designators.schemlang` next to
the board source on first build, containing exactly such a block.
Commit it to source control. The naming-conventions doc covers the
full lifecycle in `05-naming-conventions.md` §5.4.

---

## 13. Hints

A **hint** attaches typed, machine-readable design intent to one or
more elaborated objects. Hints carry structured arguments and an
optional free-text payload; they are emitted alongside the netlist
for downstream tools (typesetters, layout engines, NLP-augmented
review/layout assistants) to consume. The semantic story is in
`02-semantic-model.md` §7.5; the catalog of standard kinds is in
`05-naming-conventions.md` §7.

### 13.1 Inline form (one line)

```
hint <kind> <targets> ["<text>"]
```

Examples:

```
hint placement     mcu                              "center; near USB; rotate 90°"
hint annotate      afe                              "low-noise stage; keep guard ring intact"
hint signal_class  { mcu.spi1.CLK, mcu.spi1.MOSI }  "high_speed"
hint near          adc, ref_buffer                  "tight coupling — keep within 5 mm"
```

`<kind>` is an `IDENT`. `<targets>` is a single path / type-ref or a
brace-delimited set. The trailing string is optional and is the
free-text payload.

### 13.2 Block form (structured args)

When a hint kind takes structured arguments, use the indented form:

```
hint placement mcu
  position  center
  near      usb_connector
  rotation  90°
  text      "anchor of the design"
```

Each indented line is `<key> <value>`, where the value is any
`arg_value` (number with units, string, boolean, path, sigil-name,
range). The `text` key, if present, is the free-text payload — same
role as the trailing string in the inline form.

The block form may follow inline targets:

```
hint sheet { reg, mcu_pwr, bulk_caps }
  name         "Power Tree"
  page_number  2
  text         "5V → 3V3 LDO and bulk decoupling"
```

### 13.3 Hint as a body field of an instance

Hints attached to a single instance are most concisely written
inside the instance's body:

```
mcu = {STM32H743VIT6} @ U1
  hint placement  "center; near USB; rotate 90°"
  hint annotate   "main MCU; firmware loads from flash_slot"
```

Inside an instance body, the target is implicit (the surrounding
instance), so it is omitted.

### 13.4 `override` and `remove`

Hints follow the same merge story as other body productions:

```
define <my_compute> : <ti375_evk>
  override hint placement mcu  "right-of-center; near USB-C"
  remove   hint annotate  status_led
```

For kinds whose declared merge rule is *override-only* (e.g.
`placement`), an unmarked redeclaration is an error. For kinds whose
default is *accumulate* (e.g. `annotate`), redeclarations append; use
`override` to *replace* and `remove` to drop a parent's contribution.
The merge rule is declared by the kind in its library entry; see
`05-naming-conventions.md` §7.

### 13.5 What hints cannot do

* Hints **cannot** affect the netlist. They never connect, never
  unify, never participate in constraint discharge.
* Hints **cannot** be referenced from constraints. If you need a
  predicate, write a `constraint`; hints are not predicates.
* Hints **cannot** be silently ignored at elaboration time, but
  *unknown* hint kinds are passed through without error — forward
  compatibility wins over strict validation.

---

## 14. Grammar (concise)

```
file        := top_stmt*
top_stmt    := include | define | alias | prefer
             | designators | designators_lock
             | hint | constraint | generator | use

include     := "include" STRING ("as" IDENT)? ("only" "{" IDENT_LIST "}")?

prefer      := "prefer" sigil_name "=" sigil_name (call_args)? ("soft" INT)?

designators := "designators" NEWLINE INDENT designator_rule+ DEDENT
designator_rule :=
      "start_at" IDENT "=" INT
    | "reserve"  IDENT "=" "{" INT_LIST "}"
    | "prefix"   IDENT "=" IDENT

designators_lock := "designators_lock" sigil_circuit NEWLINE INDENT lock_binding+ DEDENT
lock_binding     := IDENT "=" path

hint        := "hint" IDENT hint_targets? hint_text? hint_block?
hint_targets:= target_term ("," target_term)*
target_term := path | sigil_name | set_literal
hint_text   := STRING
hint_block  := NEWLINE INDENT hint_attr+ DEDENT
hint_attr   := IDENT arg_value


define      := "define" sigil_name parents? NEWLINE INDENT body DEDENT
parents     := ":" sigil_name ("," sigil_name)*
sigil_name  := "{" IDENT "}" | "[" IDENT "]" | "<" IDENT ">"

body        := body_stmt*
body_stmt   := pin | parameter | package_binding | view
             | lane | role | port | instance | connect | splice
             | constraint | generator
             | description | designator_prefix | swap_group | alias
             | bus | bank | provides
             | hint | derive | resolution
             | prefer | port_prefer
             | override_stmt | remove_stmt
             | NEWLINE

derive       := "derive" IDENT "=" path
resolution   := "resolution" NEWLINE INDENT resolution_case+ DEDENT
resolution_case
             := "case" resolution_pred "=>" sigil_name (call_args)?
                 ("weight" INT)?
              | "default" "=>" sigil_name (call_args)? ("weight" INT)?
resolution_pred
             := ("any" | "all" | "none") "(" "prefers" sigil_name ")"
              | "count" "(" "prefers" sigil_name ")" comp_op INT
port_prefer  := "prefer" "wrapper" "=" sigil_name (call_args)? ("weight" INT)?

provides    := "provides" sigil_circuit "as" IDENT NEWLINE INDENT
                  "pool" filter_expr NEWLINE
                  "per_bus" NEWLINE INDENT lane_candidate+ ("where" expr NEWLINE)* DEDENT
               DEDENT
filter_expr := path ("where" expr)?
lane_candidate := IDENT "in" "pool" NEWLINE

splice      := "splice" IDENT "with" path           # IDENT names a lane;
                                                    # path names a 2-terminal component instance

description       := "description" STRING
designator_prefix := "designator_prefix" IDENT

override_stmt := "override" body_stmt
remove_stmt   := "remove" remove_target
remove_target :=
      "pin"        IDENT
    | "parameter"  IDENT
    | "lane"       IDENT
    | "role"       IDENT
    | "port"       IDENT
    | "instance"   IDENT
    | "view"       sigil_circuit IDENT?
    | "constraint" IDENT
    | "package"    sigil_pkg
    | "splice"     IDENT
    | "hint"       IDENT hint_targets?
    | "parent"     sigil_name ("." remove_target)?

pin         := "pin" pin_id IDENT (IDENT)* ("role" pin_role)? param_block?
package_binding := sigil_pkg NEWLINE INDENT pin+ swap_group* DEDENT
view        := "view" sigil_circuit "as" IDENT (IDENT)? NEWLINE INDENT mapping+ DEDENT
mapping     := IDENT IDENT
lane        := "lane" IDENT ":" type_expr
role        := "role" IDENT ("extends" sigil_circuit "." IDENT)? NEWLINE INDENT role_body_stmt+ DEDENT
role_body_stmt := direction_set | cardinality | derive
direction_set := ("drives"|"receives"|"bidir") "{" IDENT_LIST "}"
cardinality   := "cardinality" cardinality_form
cardinality_form
             := "=" (INT | "*")
              | ">=" INT
              | "<=" INT
              | "in" "[" INT "," INT "]"
port        := "port" IDENT ":" sigil_circuit "as" IDENT
instance    := IDENT "=" sigil_name (call_args)? ("@" IDENT)?
            # optional indented body (parameter overrides, designator, etc.)
            # introduced by NEWLINE INDENT … DEDENT when present
connect     := connect_term "<->" connect_term connect_args?
connect_term:= path | lane_endpoint | net_label | set_literal
lane_endpoint := IDENT "." ("host_side" | "peri_side")
connect_args := NEWLINE INDENT connect_arg+ DEDENT
connect_arg  := "parameter" IDENT "=" arg_value
              | "prefer" sigil_name "=" sigil_name (call_args)?     # wrapper override
              | "prefer" "wrapper" "=" sigil_name (call_args)?      # wrapper override (shorthand)
              | "pin"        IDENT "=" path                          # hard pin equality
              | "pin_prefer" IDENT "=" path                          # soft pin preference
              | "derive"     IDENT "=" path                          # post a derive equality
              | "hint"       IDENT hint_text? hint_block?            # attach hint to bus
              | "override"   connect_arg                              # explicit override of conflict
              | IDENT "=" path                                        # bind wrapper non-lane port
parameter   := "parameter" IDENT (":" type_expr)? param_domain? param_tol?
            # optional domain block: NEWLINE INDENT "domain" "{" arg_value_list "}" DEDENT
param_domain := "=" arg_value
             | "in" range_or_set
             | "<=" arg_value
             | ">=" arg_value
range_or_set := "[" arg_value "," arg_value "]"
              | "{" arg_value ("," arg_value)* "}"
              | arg_value ".." arg_value
constraint  := "constraint" (IDENT ":")? expr ("soft" INT)?
             | "constraint" "over" set "{" expr "}" ("soft" INT)?
generator   := "generate" ("for" generator_for | "if" expr | "match" path)
alias       := "alias" sigil_name "=" sigil_name ("where" expr)?
use         := "use" sigil_circuit ("(" call_args ")")?
```

This grammar is **LL(2)** with the indentation handled by a layout
preprocessor (similar to Haskell's). It is deliberately small enough to
hand-implement.

The condensed productions above are a reading aid; the canonical
machine-readable grammar — including lexical tokens, layout
pseudo-tokens, expression precedence, and implementer notes — lives
in [`schemlang.ebnf`](schemlang.ebnf). When the two disagree, the
EBNF wins for parser implementation and this section wins for
language meaning; please file a bug to reconcile.

---

## 15. Diff against the README sketch

For reviewers comparing against the original sketch:

* **Indentation is significant** instead of relying on shape alone.
  Two-space indent.
* **`include` takes a quoted path** instead of a bare filename, with
  optional `as` and `only`.
* **`alias` uses `=`** between operands (was: `alias {a} {b}`).
* **`view <CIRCUIT> as <role>`** replaces the bare `(POWER)` block in
  components, to make role explicit. The `()` sigil is gone — the
  former Interface kind is now part of the unified Circuit kind, so
  views project a component's pins onto a circuit type.
* **One Circuit kind for both protocols and subcircuits.** The README
  separated `(harness)` from `<subcircuit>`; the language unifies them.
  `<UPPER>` names protocol contracts; `<snake_case>` names
  implementations and wrappers. See §4.
* **`port` is required** in implementation circuits to declare
  external interfaces; the `define <compute>` body in the README
  implicitly used parent scope, which we forbid.
* **Package binding is a block (`[NAME]`)** with pins inside, instead
  of a single tag floated above pin lines.
* **Pin lines have an explicit role** (`digital_in`, `power_in`, etc.).
* **Parameters are typed with units**, not bare numbers.
* **Generators have explicit `generate` keyword** to keep parser
  simple.
* **`use <Top>`** replaces a bare `<compute>` at end of file as the
  netlist entry point.
* **Multiple parents are allowed** in `define X : A, B, C`, with
  documented left-to-right merge semantics (§2.2).
* **`override` and `remove`** are first-class body forms that make
  derivation surgical: refine a parameter, drop an instance, strip an
  entire parent's contributions. The README sketch had no story for
  these.
* **`designator_prefix`** on a component type declares its silkscreen
  letter (`R`, `C`, `U`, …); instances either auto-number off it or
  carry an explicit `designator` body field / postfix `@` override.
  Naming conventions and the prefix table live in
  `05-naming-conventions.md`.
* **`prefer <Abstract> = <Concrete>`** binds an abstract type to a
  concrete one for the rest of the elaboration — the project-wide
  policy mechanism. Applies uniformly to components ("all resistors
  are Panasonic ERJ-PHF") and to circuits ("every I2C bus has
  pull-ups via `<i2c_link>`"; "every SPI link runs through
  `<spi_with_termination>`"). See §11.
* **`splice <lane> with <component>`** is sugar for series insertion
  on a wrapper circuit's lane, suppressing the implicit through-wire
  and putting the component between `lane.host_side` and
  `lane.peri_side`. Used by wrappers like `<spi_with_termination>`.
* **`designators` and `designators_lock`** are top-level blocks (in
  `.schemlang` files, never side-files) for designator ranges and
  locked auto-assignments respectively.
* **One file format**: `.schemlang` is the single source extension.
  No `.lock`, `.config`, `.bom`, or other side-files.
* **`hint <kind> <targets> ["text"]`** is a first-class form for
  attaching typed, machine-readable design intent (placement
  guidance, sheet partitioning, signal-class designation,
  annotations) to elaborated objects. Hints are emitted alongside
  the netlist for downstream tools — including NLP-augmented
  layout/review assistants — to consume. The README sketch had no
  story for intent-bearing metadata.
* **`provides <C> as <role>`** declares a soft-peripheral capability
  on a component: a candidate-pin pool plus per-allocation
  predicates. Boards instantiate buses on demand by writing
  `<comp>.<lowercase_circuit>.<name>`; the elaborator picks pins from
  the pool subject to the predicates and to linear-resource
  disjointness. Right shape for FPGAs, CPLDs, and flexible-I/O MCUs,
  for which the MCU `bus` form (fixed alt-function tables) was the
  wrong shape.
* **`cardinality`** on a `role` declaration makes participant
  counts explicit. The default is `1` (point-to-point); multi-drop
  protocols declare `>= 1` (or higher minima) on the role that
  admits multiple devices. The elaborator enforces the cardinality
  on each bus instance. (Bus instances themselves are an
  elaboration concept, formalized in `02-semantic-model.md` §3.4.)
* **`derive <name> = <path>`** in a role or `bus`-port body
  publishes a derived attribute that the elaborator turns into an
  equality constraint when the port joins a bus instance. The
  classic case is `derive vref = bank(SDA).vio_port` on
  `<I2C>.host`, which auto-binds an `<i2c_link>` wrapper's `vref`
  port without forcing every connect site to spell it out.
* **`prefer wrapper = <X>`** in a port body declares a soft
  preference for the wrapper that mediates the port's bus
  instance. A sensor with internal pull-ups declares
  `prefer wrapper = <I2C>` (the empty-bodied protocol);
  a bare-CMOS GPIO declares `prefer wrapper = <i2c_link>`.
  Combined with the abstract's `resolution` block, this makes
  per-port preferences soft-vote into the per-bus wrapper choice.
* **`resolution` block** on a protocol circuit defines how
  participant preferences combine into a wrapper choice. Cases
  use `any/all/none/count(prefers <X>)` quantifiers and
  `weight <int>` to bias the solver. A `default` case posts a
  baseline weight that always applies. See §4.1.4.
* **Connect-site argument block** (§6.4) under `<->` lets a
  connection override wrapper parameters, bind wrapper non-lane
  ports, override wrapper choice, or pin/soft-pin specific
  lanes. Replaces ad-hoc per-connection configuration with a
  unified, formalized grammar.
* **Soft constraints** with explicit `soft <weight>` annotation,
  in addition to soft preferences (`prefer wrapper = …`,
  `pin_prefer …`) that desugar to the same form. The solver
  maximizes the sum of satisfied soft weights; ties fall back to
  documented lexicographic tie-break.
* **Constraint substrate** is finite-domain plus linear arithmetic
  plus weighted soft constraints. A board may compile to a
  fully-resolved netlist *or* a partially-resolved netlist plus
  a residual constraint system for downstream tools. See
  `02-semantic-model.md` §11–§14.

The shape-as-kind mnemonic (`{}`, `[]`, `()`, `<>`) is preserved
exactly.
