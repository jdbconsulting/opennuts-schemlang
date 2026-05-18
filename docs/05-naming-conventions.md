# Naming Conventions

This document defines the *recommended* names used across a Schemlang
project. The language enforces lexical rules (`[A-Za-z_][A-Za-z0-9_]*`,
case conventions for instance vs. net vs. type, see `03-syntax.md` §10);
**this** document goes the rest of the way: when you have a free
choice of identifier or string, what should you call it?

The conventions below are recommendations, not hard rules. Linters
(`schemc lint`) check them and emit warnings, never errors. Libraries
that ship as part of the standard distribution **do** follow them
strictly — that is what makes parts findable across vendors.

References:

* IPC-7351B — *Generic Requirements for Surface Mount Design and Land
  Pattern Standard* (the canonical SMT footprint naming convention;
  full text in `references/`).
* IPC-7251 — *Generic Requirements for Through-Hole Design and Land
  Pattern Standard* (its through-hole sibling).
* IEEE 315 / ASME Y32.2 / IEC 60617 — reference designator letter
  codes.
* JEDEC JEP30 — package outline naming.

---

## 1. Identifiers (recap)

| Use                            | Convention                                         | Example                                               |
|--------------------------------|----------------------------------------------------|-------------------------------------------------------|
| Instance name                  | `snake_case`                                       | `boot_flash`, `pull_cs`                               |
| Net label                      | `SCREAMING_SNAKE` with prefix                      | `NET_3V3`, `NET_GND`, `NET_USB_DP`                    |
| Component type `{T}`           | `MFR_PARTNUM` or stable family                     | `W25N512GVEIG`, `STM32H743VIT6`                       |
| Generic / category type `{T}`  | `SCREAMING_SNAKE`                                  | `RESISTOR`, `CAPACITOR`, `LED_RED`                    |
| Catalog alias `{T}`            | `SHORT_FORM`                                       | `R_10k`, `C_100n`, `LED_RED_0603`                     |
| Package type `[P]`             | IPC-7351B name (§3)                                | `RESC1005X40N`, `SOIC127P600X175-8N`                  |
| Circuit — protocol `<C>`       | `UPPER` (acronym ok), the contract name            | `SPI`, `QSPI`, `I2C`, `USB2`, `POWER`, `3V3`          |
| Circuit — implementation `<C>` | `snake_case`, what the circuit *does*              | `boot_flash_slot`, `buck_3v3`, `spi_with_termination` |
| Parameter name                 | `snake_case` per §4                                | `R_pullup`, `Vmin`, `f_max`                           |
| Pin name                       | `UPPER`, `_N` suffix for active-low                | `CS_N`, `VCC`, `IO0`, `PA5`                            |
| Designator prefix              | one or two uppercase letters                       | `R`, `C`, `U`, `J`, `Y`                               |
| Soft-bus allocation name       | `snake_case`, the bus's *purpose*                  | `imu`, `env`, `boot`, `debug`, `gps`                  |

Four non-obvious rules:

1. **Net labels start with `NET_`**. The `NET_` prefix on the net is
   redundant from the parser's view, but makes searches and BOM
   exports unambiguous: a grep for `\bVCC_3V3\b` matches both pin
   names and net labels; `\bNET_VCC_3V3\b` matches only the net.
2. **Catalog aliases are `_`-separated, value-first**. `R_10k`, not
   `10k_R` or `R_10K_RES`. Sorts well alphabetically; reads like a
   short BOM line.
3. **Soft-bus allocations are named for purpose, never for index.**
   On a part with a `provides` capability, write
   `fpga.i2c.imu`, `fpga.uart.gps`, not `fpga.i2c0`, `fpga.uart3`.
   The elaborator counts the allocations itself; humans should be
   reading what each bus is *for*. (Hard-decl `bus` peripherals on
   MCUs keep their datasheet names — `mcu.spi1`, `mcu.i2c2` — because
   that's what the silicon and reference manual actually call them.)
4. **Case telegraphs role for circuits.** A circuit with `UPPER` name
   is a *contract* — the protocol or rail others connect to. A
   circuit with `snake_case` name is an *implementation* — a
   subcircuit you instantiate to do work. Both are legal. The case
   is your hint to readers about which kind of thing they're looking
   at. (See §1.1 below.)

### 1.1 Two flavors of circuit, one kind

The unified `<C>` kind covers both pure-contract circuits (lanes,
roles, no body — what older drafts called "interfaces") and
implementation circuits (parameters, ports, instances, body — what
older drafts called "modules"). They are the same kind of thing,
distinguished only by what their bodies do.

| Flavor          | Body content                                                    | Naming         | Example                                                     |
|-----------------|-----------------------------------------------------------------|----------------|-------------------------------------------------------------|
| Protocol        | lanes + roles + cardinality + derives + resolution              | `UPPER`        | `<SPI>`, `<I2C>`, `<POWER>`, `<3V3>`                        |
| Wrapper         | `: <PROTOCOL>`, splices/pull-ups, conditional, derives          | `snake_case`   | `<spi_with_termination>`, `<i2c_link>`                      |
| Implementation  | ports + instances + connections                                 | `snake_case`   | `<boot_flash_slot>`, `<buck_3v3>`, `<stm32h7_power_block>`  |

Wrappers and implementations are the same kind of object; they just
serve different purposes in a project. Protocol circuits typically
declare role cardinalities, `derive` rules for port-published
attributes, and a `resolution` block that biases wrapper choice
across multi-drop participants.

---

## 2. Descriptions

Every component definition should carry a one-line **description**
suitable for a BOM. Descriptions follow a category-specific template.
The first token is always the **category**, in capitals, so a BOM
sorted alphabetically groups parts by category.

### 2.1 Templates by category

Format key:

* `<>` = required field, lowercased in description
* `[]` = optional field
* fields are space-separated
* values include their unit suffix (`100nF`, `2.7V`) without a space
* tolerance is `±N%` or `±NΔ` with the unit
* dielectric uses EIA codes (`X7R`, `C0G`, `Y5V`)

| Category    | Template                                                                     |
|-------------|------------------------------------------------------------------------------|
| Resistor    | `RES <subtype> <value> <tolerance> <power> [tcr] <eia_size>`                |
| Capacitor   | `CAP <subtype> <value> <tolerance> <voltage> <dielectric> <eia_size>`       |
| Inductor    | `IND <subtype> <value> <tolerance> <i_sat> <eia_size>`                      |
| Ferrite     | `FB <impedance@freq> <i_max> <eia_size>`                                    |
| Diode       | `DIODE <subtype> <v_r> <i_f> <package>`                                     |
| LED         | `LED <color> <v_f> <i_f> [intensity] <package>`                             |
| BJT         | `BJT <polarity> <v_ceo> <i_c> <package>`                                    |
| MOSFET      | `FET <polarity> <v_ds> <i_d> <r_ds_on> <package>`                           |
| Crystal     | `XTAL <freq> <tolerance> <load_cap> <esr> <package>`                        |
| Oscillator  | `OSC <freq> <tolerance> <supply> <package>`                                 |
| Voltage Reg | `LDO|BUCK|BOOST <v_out> <i_max> <variant> <package>`                        |
| Op-amp      | `OPAMP <variant> <gbw> <supply> <channels> <package>`                       |
| Logic IC    | `IC <function> <variant> <supply> <package>`                                |
| MCU/SoC     | `MCU <family> <variant> <package>`                                          |
| Memory      | `MEM <subtype> <density> <interface> <package>`                             |
| Connector   | `CONN <function> <positions> <pitch> <orientation> <gender> <package>`      |
| Switch      | `SW <subtype> <poles> <throws> <actuator> <package>`                        |
| Relay       | `RELAY <coil_v> <contact_form> <contact_rating> <package>`                  |
| Test point  | `TP <type> <package>`                                                       |
| Mechanical  | `MECH <function> <package>`                                                 |

### 2.2 Examples

```
description "RES THICK_FILM 10kOhm ±1% 0.1W ±100ppm 0402"
description "CAP CER 100nF ±10% 16V X7R 0402"
description "CAP TANT 10uF ±20% 16V SMD-A"
description "IND CHIP 4.7uH ±20% 1.2A 0603"
description "DIODE SCHOTTKY 40V 1A SOD-123"
description "LED RED 1.8V 20mA 0603"
description "FET N-CH 30V 5A 50mOhm SOT-23"
description "XTAL 25MHz ±20ppm 8pF 80Ohm 5032-2L"
description "LDO 3.3V 300mA FIXED SOT-23-5"
description "OPAMP RAIL-RAIL 1MHz 3-36V SINGLE SOT-23-5"
description "IC FLASH 512MBIT SPI/QUAD WSON-8"
description "MCU STM32H7 480MHZ LQFP-100"
description "CONN USB-C RECEPTACLE 24P 0.4MM HORIZONTAL FEMALE SMT"
description "SW TACTILE SPST 4P MOMENTARY SMD-6X6"
description "TP THM 1.5MM"
description "MECH MOUNT-HOLE M3"
```

### 2.3 Adding a new category

Add a row to the table above, keep the first token short and unique,
and prefer the same field order as siblings. Categories are not
hierarchical — `LED` is its own category, not "DIODE LED" — because
BOM grouping by first-token is more useful than tree-structured
descriptions.

---

## 3. Footprint (package) names

### 3.1 IPC-7351B for SMT

Use IPC-7351B land pattern names verbatim for surface-mount packages.
The IPC-7351B convention encodes the *physical envelope* in the name,
so two packages that fit interchangeably get equal names regardless
of who manufactured the silicon they hold.

The full grammar is in IPC-7351B §3.1.5.5; the short form is:

```
<COMP_PREFIX>[<LEAD_PITCH>P]<BODY_LENGTH>X<BODY_WIDTH>[X<HEIGHT>]-<PIN_COUNT>[<DENSITY>]
```

Where:

* dimensions are in **0.01 mm** (so `1005` = 1.00 × 0.50 mm),
* `<DENSITY>` is `L` (least, smallest pads) / `N` (nominal) /
  `M` (most, largest pads),
* `<COMP_PREFIX>` is one of the IPC prefixes:

| Prefix   | Meaning                                          |
|----------|--------------------------------------------------|
| `RESC`   | Chip resistor                                    |
| `CAPC`   | Chip capacitor                                   |
| `INDC`   | Chip inductor                                    |
| `DIOC`   | Chip diode                                       |
| `LEDC`   | Chip LED                                         |
| `FERC`   | Chip ferrite bead                                |
| `SOIC`   | Small Outline IC (gull-wing, 1.27 mm pitch)      |
| `SOP`    | Small Outline Package (gull-wing, other pitches) |
| `TSOP`   | Thin SOP                                         |
| `TSSOP`  | Thin Shrink SOP                                  |
| `SSOP`   | Shrink SOP                                       |
| `MSOP`   | Mini SOP                                         |
| `QSOP`   | Quarter-size SOP                                 |
| `SOT`    | Small Outline Transistor                         |
| `SC`     | Small Outline (sub-SOT)                          |
| `QFP`    | Quad Flat Pack                                   |
| `LQFP`   | Low-profile QFP                                  |
| `TQFP`   | Thin QFP                                         |
| `BQFP`   | Bumpered QFP                                     |
| `CQFP`   | Ceramic QFP                                      |
| `QFN`    | Quad Flat No-lead                                |
| `DFN`    | Dual Flat No-lead                                |
| `SON`    | Small Outline No-lead                            |
| `WSON`   | Very thin SON                                    |
| `BGA`    | Ball Grid Array                                  |
| `FBGA`   | Fine pitch BGA                                   |
| `CGA`    | Column Grid Array                                |
| `LGA`    | Land Grid Array                                  |
| `RESCAV` | Concave (wraparound) chip resistor array         |
| `CAPCAV` | Concave chip capacitor array                     |
| `RESCAF` | Flat chip resistor array                         |
| `CAPCAF` | Flat chip capacitor array                        |

Examples:

| Name                      | Decoded                                                     |
|---------------------------|-------------------------------------------------------------|
| `RESC1005X40N`            | Chip resistor, 1.00 × 0.50 × 0.40 mm, nominal density (0402) |
| `RESC1608X55N`            | Chip resistor, 1.60 × 0.80 × 0.55 mm (0603)                  |
| `CAPC2012X145N`           | Chip cap, 2.00 × 1.25 × 1.45 mm (0805)                       |
| `SOIC127P600X175-8N`      | SOIC, 1.27 mm pitch, 6.00 × 1.75 mm body, 8 pin, nominal     |
| `SON127P600X150-8N`       | SON (WSON), 1.27 mm pitch, 6 × 5 × 1.5 mm, 8 pin             |
| `QFN50P500X500X100-32N`   | QFN, 0.50 mm pitch, 5 × 5 × 1.0 mm body, 32 pin              |
| `TQFP50P1200X1200X100-100N`| TQFP, 0.50 mm pitch, 12 × 12 × 1.0 mm, 100 pin              |
| `BGA80P900X900X120-256N`  | BGA, 0.80 mm pitch, 9 × 9 mm, 1.20 mm height, 256 ball       |

### 3.2 Extensions beyond IPC-7351B

IPC-7351B is **SMT-only**. The conventions below extend it to cover
the rest of the part landscape. They follow the IPC convention's
spirit — physical envelope encoded into the name — but use disjoint
prefix namespaces so they cannot collide with IPC names.

#### 3.2.1 Through-hole (per IPC-7251 spirit)

Prefix the part type with `TH` (Through-Hole). Body and lead
dimensions are still in 0.01 mm. The lead-bend-radius and pad-shape
modifier comes from IPC-7251.

| Prefix     | Meaning                                                       |
|------------|---------------------------------------------------------------|
| `THRA`     | Through-hole resistor, axial                                  |
| `THCAR`    | Through-hole capacitor, axial                                 |
| `THCRD`    | Through-hole capacitor, radial                                |
| `THDIA`    | Through-hole diode, axial                                     |
| `THDIO`    | Through-hole diode, other (DO-201, etc.)                      |
| `THDS`     | Through-hole DIP, narrow (300 mil)                            |
| `THDSL`    | Through-hole DIP, wide (600 mil)                              |
| `THSIP`    | Through-hole SIP                                              |
| `THTO`     | Through-hole TO-style transistor (TO-92, TO-220, etc.)        |
| `THXTAL`   | Through-hole crystal/oscillator can                            |
| `THCONNH`  | Through-hole connector, header                                |
| `THCONNS`  | Through-hole connector, socket                                |

Examples:

```
THRA-1080-070-D5         # 10.80 mm body, 0.70 mm lead, 5.0 mm pitch
THDS-300-8               # DIP, 300 mil row spacing, 8 pin
THTO-220-V               # TO-220 vertical
THCONNH-254-1X10         # 2.54 mm pitch header, 1×10
```

#### 3.2.2 Mechanical, off-board, and footprint-less

Some "components" are land patterns without a real part: test points,
mounting holes, fiducials, jumpers. They use a `MECH-` prefix:

| Pattern                  | Meaning                                  |
|--------------------------|------------------------------------------|
| `MECH-TP-<diam>`         | Test point, given diameter in 0.01 mm    |
| `MECH-FID-<diam>`        | Fiducial mark                            |
| `MECH-MH-<screw>`        | Mounting hole for `<screw>` (e.g. M3)    |
| `MECH-SLOT-<L>X<W>`      | Slotted hole                             |
| `MECH-EDGE-<L>`          | Board edge keepout marker                |

Off-board "footprints" (cable terminations, pads where a wire is
soldered) use `WIRE-`:

| Pattern                  | Meaning                                   |
|--------------------------|-------------------------------------------|
| `WIRE-PAD-<W>X<L>`       | Solder pad for a wire                     |
| `WIRE-CRIMP-<series>`    | Crimp-terminal land                       |

#### 3.2.3 Vendor-specific or atypical packages

When a part comes in a package IPC has not catalogued, use the form:

```
<VENDOR>-<PART_PACKAGE_CODE>[-<DENSITY>]
```

Examples: `MOLEX-503480`, `JST-SH`, `AMPHENOL-MMCX`. `<DENSITY>`
is dropped if there is no valid IPC density classification.

#### 3.2.4 Custom revisions

When you need a tweaked version of a standard footprint (oversized
pads, thermal relief variations, deliberate non-IPC pads), append a
**revision suffix** of the form `-VR<N>`:

```
QFN50P500X500X100-32N-VR1   # our internal V1 of the standard QFN
```

The base name remains IPC-conformant; the suffix says "this matches
the named footprint in pin layout, but the geometry is intentionally
modified." The `schemc` linter flags any `-VR<N>` footprint that
disagrees with the base's pin count or pitch.

### 3.3 Library layout for footprints

Project libraries should mirror the prefix structure:

```
footprints/
  smt/
    chip/                     RESC*, CAPC*, INDC*, DIOC*
    sop/                      SOIC*, SOP*, TSOP*, TSSOP*
    qfp/                      QFP*, LQFP*, TQFP*, BQFP*
    qfn/                      QFN*, DFN*, SON*, WSON*
    bga/                      BGA*, FBGA*, CGA*, LGA*
  th/                         TH* (through-hole)
  mech/                       MECH-*
  wire/                       WIRE-*
  vendor/                     <VENDOR>-* (one folder per vendor)
```

The standard library follows this; user libraries are encouraged to.

---

## 4. Standardized parameter names

> **Principle.** *Extra component parameters are sometimes useful and
> usually wrong.* Every parameter on a part definition must be
> consumed by an elaborator check, a refinement predicate, or a
> connection-driven aggregation. If you cannot point at the line that
> uses a parameter, it does not belong here. (See
> `01-design-principles.md` §5.7.)

Under that principle, the standard parameter vocabulary is small.
The names below are reserved for these meanings; if you need a name
not on this list, you almost certainly need to either justify it
against a check or drop it.

### 4.1 The minimal standard vocabulary

| Name         | Type           | Where it appears                                  | What check uses it                                                       |
|--------------|----------------|---------------------------------------------------|--------------------------------------------------------------------------|
| `R`          | resistance     | `{RESISTOR}` and refinements                      | Power dissipation, voltage divider, pull strength                        |
| `C`          | capacitance    | `{CAPACITOR}` and refinements                     | Decoupling obligations, load-cap matching                                |
| `L`          | inductance     | `{INDUCTOR}` and refinements                      | Filter cutoff constraints                                                 |
| `Tol`        | percent        | Any value-bearing component                       | Value-window inequalities                                                 |
| `P_max`      | power          | Resistors                                         | Power dissipation constraint                                              |
| `V_max`      | voltage        | Capacitors, varistors                             | Voltage-across-component constraint                                       |
| `Vnom`       | voltage        | Power-rail circuit refinements (`<3V3>`)          | Defines the rail's centre value (toleranced via the `+/-` form)           |
| `Vlo`        | voltage        | *Auto-derived* on any toleranced parameter        | Worst-case lower bound. Read-only; do **not** declare directly.           |
| `Vhi`        | voltage        | *Auto-derived* on any toleranced parameter        | Worst-case upper bound. Read-only; do **not** declare directly.           |
| `Vmin`       | voltage        | Power-input pins                                  | Recommended-operating-window lower edge (containment check)               |
| `Vmax`       | voltage        | Power-input pins                                  | Recommended-operating-window upper edge (containment check)               |
| `Vmax_abs`   | voltage        | Power-input pins (optional)                       | Absolute-maximum / destruction threshold; powers derating analysis        |
| `Vmin_abs`   | voltage        | Power-input pins (optional)                       | Absolute-minimum / brown-out destruction threshold (rare; symmetric)      |
| `Vio`        | voltage        | Banks (`bank A.Vio`); bus roles (`derive Vio = …`) | Cross-participant agreement on bus reference voltage                     |
| `vref`       | `<POWER>` port | Wrapper bodies; bus roles (`derive vref = …`)     | Wrapper auto-binding to host's bank `vio_port`                            |
| `vio_port`   | `<POWER>` view | Banks (alias for the bank's <POWER> view)          | Path target of `derive vref = bank(...).vio_port` rules                   |
| `I_max`      | current        | Banks (`bank A.I_max`), pins                      | Aggregate current-budget constraint                                       |
| `I_load`     | current        | Per-pin / per-net                                 | Sum into the bank budget above                                            |

Sixteen *declared* parameter names plus `Vlo`/`Vhi` (auto-derived on
every toleranced parameter — engineers read them, never write them).
The standard library uses these and only these for the language-
driven checks; everything else belongs in a library that explicitly
justifies its additions.

A few notes on the new entries:

* **`Vnom` carries its own tolerance.** Earlier drafts split nominal
  and tolerance into `Vnom` + `Vtol`. The first-class tolerance form
  (`03-syntax.md` §7.1.1) folds them into one declaration:
  `parameter Vnom : voltage = 3.3 V +/- 3 %`. The store still ends
  up with `Vlo` and `Vhi`, but the *declared* surface is one
  parameter, not two.
* **`Vmax_abs` is opt-in.** Most parts don't need it; the `Vmin`/
  `Vmax` window is enough for an operating-range check. Add
  `Vmax_abs` only when the datasheet quotes a distinct
  absolute-maximum rating *and* you intend the linter to fire on
  it (or you want to write a derating soft constraint, e.g.
  `constraint net.V.Vhi <= 0.8 * Vmax_abs soft 5`).
* **`Vio`/`vref`/`vio_port`** are the standard names that make
  protocol-wrapper auto-binding (Recipe 15) work without bespoke
  per-protocol conventions; a part that uses them benefits from
  every protocol wrapper that consumes them.

### 4.2 Naming rules for new parameters

When you must add a new parameter (with a justification — see
"earned" rule above), follow these:

* Use uppercase for physics-quantity parameters (`R`, `C`, `Vmin`).
* Use lowercase only when the parameter is genuinely a non-physical
  identifier (rare and discouraged at the part level — most such
  things belong in a sibling tooling layer, not in source).
* Subscripts go after `_` (`I_load`, `Vmin`); avoid Greek letters in
  the core.
* If a parameter applies to multiple part families, use the **same
  name** in all of them, not a family-prefixed variant.

### 4.3 What does *not* belong here

The following are explicitly **not** part of the standard parameter
vocabulary, and the linter will warn if it sees them on a part
definition:

* Manufacturer/MPN/URL/lifecycle/RoHS/cost — these are catalog
  metadata. Track them in the BOM tool, not the schematic.
* Thermal/environmental parameters (`Tj_max`, `θja`, `MSL`, `Ts`) —
  the elaborator does not run a thermal model.
* Timing parameters (`t_pd`, `t_su`, `t_h`) — the elaborator does
  not run a timing analysis.
* Frequency limits (`f_max`) — unless your library defines a check
  that consumes them. If it doesn't, drop them.
* Per-family "absolute maximum" trivia (`Vbr`, `Vds`, `Vceo`) — they
  are not connected to any check the elaborator performs.

If a future check is added that needs one of these, the parameter
becomes earned and joins the standard list. Until then it is drift.

---

## 5. Reference designators

A **reference designator** is the human-facing identity of an instance
on the silkscreen / BOM / assembly drawings (`R7`, `C12`, `U1`).
Designators are **derived from the language model**, not authored by
default; you can override per-instance when needed.

### 5.1 Standard prefix table

Per IEEE 315-1975 / ASME Y32.2 / IEC 60617:

| Prefix | Component family                                            |
|--------|-------------------------------------------------------------|
| `A`    | Sub-assembly / pre-built circuit board                      |
| `AT`   | Attenuator                                                  |
| `BT`   | Battery                                                     |
| `C`    | Capacitor                                                   |
| `CB`   | Circuit breaker                                             |
| `D`    | Diode (LED, Zener, signal, rectifier)                       |
| `DS`   | Display (7-segment, dot matrix)                             |
| `F`    | Fuse                                                        |
| `FB`   | Ferrite bead                                                |
| `FL`   | Filter (multi-element)                                      |
| `J`    | Connector — receptacle / jack (board side)                  |
| `K`    | Relay                                                       |
| `L`    | Inductor                                                    |
| `LS`   | Loudspeaker / buzzer                                        |
| `M`    | Motor                                                       |
| `MK`   | Microphone                                                  |
| `P`    | Connector — plug (cable / removable)                        |
| `Q`    | Transistor (BJT, FET)                                       |
| `R`    | Resistor                                                    |
| `RT`   | Thermistor                                                  |
| `RV`   | Varistor                                                    |
| `S`    | Switch                                                      |
| `T`    | Transformer                                                 |
| `TC`   | Thermocouple                                                |
| `TP`   | Test point                                                  |
| `U`    | Integrated circuit (default for ICs without subtype)        |
| `VR`   | Voltage regulator (when distinguished from a generic `U`)   |
| `W`    | Cable / wire / jumper                                       |
| `X`    | Socket (e.g. IC socket, crystal socket)                     |
| `Y`    | Crystal / oscillator                                        |
| `Z`    | Composite / unspecified                                     |

The standard library declares each generic `{T}` with the appropriate
prefix:

```
define {RESISTOR}
  designator_prefix R
  ...

define {CAPACITOR}
  designator_prefix C
  ...

define {LED}
  designator_prefix D       # LEDs are diodes
  ...

define {STM32H743VIT6}
  designator_prefix U
  ...
```

### 5.2 Auto-numbering

In a board's elaborated netlist, every instance whose component type
declares a `designator_prefix` receives an auto-assigned number:

* Numbers start at **1** per prefix.
* Assignment order is **depth-first traversal** of the elaborated
  circuit tree, then declaration order within a circuit body.
* Numbers are stable across re-elaborations: identical source ⇒
  identical designators.
* Generated arrays (`led[i]` for `i in 0..7`) are numbered as a
  contiguous run by index order.

A board with twelve resistors gets `R1`..`R12` automatically. The
elaborator emits a `<board>.designators.schemlang` file alongside the
netlist containing a `designators_lock` block; commit it to keep
assignments stable across edits (see §5.4).

### 5.3 Explicit override

To pin a specific designator on an instance, use the `designator`
body field on the instance:

```
mcu = {STM32H743VIT6}
  designator U1            # pin to U1 regardless of elaboration order

power_led = {LED_RED}
  designator D5
```

Or, equivalently, the inline form using `@`:

```
mcu       = {STM32H743VIT6}  @ U1
power_led = {LED_RED}        @ D5
```

The `@` is read "at" — "this instance lives at designator U1." Pick
whichever form fits the visual rhythm of the surrounding code; the
inline form is recommended for one-off pins, the body form for
instances that have other attributes too.

Override rules:

* An override **takes** that designator out of the auto-pool: another
  resistor will not be auto-assigned `R7` if you wrote `r_term @ R7`.
* Two instances with the same explicit designator is an error.
* An override that disagrees with its component's `designator_prefix`
  (e.g. `c_byp = {CAPACITOR} @ R3`) is an error.

### 5.4 Locking auto-assigned designators

To keep silkscreen numbers stable across edits without writing an `@`
override on every instance, declare a `designators_lock` block in a
sibling `.schemlang` file (or at the bottom of the board file
itself). The elaborator generates this block on first build and
respects it on every subsequent build:

```
# boards/eval_board.designators.schemlang
designators_lock <eval_board>
  R1 = flash_slot.pull_cs
  R2 = led_array.res[0]
  R3 = led_array.res[1]
  ...
  U1 = mcu                  # explicit @ U1 in source
  U2 = reg                  # explicit `designator U2` in source
  U3 = flash_slot.flash     # auto-assigned, locked
```

It is a regular `.schemlang` file. Includes pull it into the build
like any other source. We do **not** introduce a separate `.lock`
file format — by principle, "everything shall be `.schemlang`, and
`.schemlang` shall be everything."

When a `designators_lock` block exists for a circuit, the elaborator
**prefers** its bindings: an unchanged instance keeps its number even
if you add new instances in front of it. Reordering or renaming an
instance issues a warning; removing one leaves a stale binding the
linter will flag.

Workflow:

* On first elaboration, the compiler writes
  `boards/<board>.designators.schemlang` next to the board source.
* Commit it to version control alongside the board file.
* To regenerate: delete the file and rebuild.
* To pin an auto-assigned number permanently against future edits:
  promote the binding to an explicit `@` override at the instance
  declaration site, then remove that line from the lock block.

The lock block is not the only place such a binding can live. You can
hand-author `designators_lock` blocks anywhere — they are just
declarations in source. The compiler-generated file is a
*convenience*: a single location for the auto-assignments so they
don't pollute the architecture-level board file.

### 5.5 Designator ranges and reservations (board-level)

Boards may declare reserved ranges or starting numbers for prefixes:

```
designators
  start_at  R = 100        # first auto-R is R100
  reserve   C = { 1..10 }  # never auto-assign C1..C10
  prefix    test_point = TP # alias an instance-name pattern to a prefix
```

This is also a regular block in a `.schemlang` file (no separate
config file). Default behavior covers most boards; reach for this only
when assembly or BOM tooling needs specific ranges.

---

## 6. Linter behavior

`schemc lint` walks the project and reports:

* **warnings** — descriptions that don't match a known template, types
  that don't follow case conventions, parameters not on the standard
  list (§4.1) without a `# justified: <check>` comment,
  footprint names that aren't IPC-conformant, hint kinds not in the
  standard catalog (§7) without a `library_kind` declaration.
* **info** — auto-renaming suggestions where a description could be
  reformatted to fit a template losslessly; passive `annotate` hints
  with no consumer in the configured downstream toolchain.
* **errors** — only for collisions (two `@ R7` overrides on a board,
  duplicate `prefer` rules in the same scope, stale or duplicated
  `designators_lock` bindings, two `placement` hints on the same
  instance without `override`).

Style violations never block elaboration. They surface in the build
report so they're impossible to ignore but easy to defer.

---

## 7. Hint kinds

The hint system (`02-semantic-model.md` §7, `03-syntax.md` §13) is
deliberately open: any `IDENT` is a valid hint kind, and unknown
kinds are passed through to downstream tools without error. This
section catalogues the **standard kinds** every Schemlang installation
recognizes, plus the conventions for adding your own.

### 7.1 Standard kinds

| Kind            | Targets                          | Structured args                                          | Free text                                  | Merge rule    |
|-----------------|----------------------------------|----------------------------------------------------------|--------------------------------------------|---------------|
| `placement`     | one component instance           | `position`, `near`, `rotation`, `side`                   | informal placement guidance                | override-only |
| `sheet`         | one or more instances / circuits | `name`, `page_number`                                    | one-line sheet description                 | override-only (per `name`) |
| `signal_class`  | one or more nets / pins / buses  | `name`, `max_length`, `match_length_group`, `impedance`  | designer's rationale                       | accumulate    |
| `near`          | two or more instances            | `max_distance`                                           | why these belong together                  | accumulate    |
| `keepout`       | one instance / circuit           | `radius`, `region`, `from_signals`                       | what's being protected                     | accumulate    |
| `group`         | two or more instances / circuits | `name`                                                   | why these are grouped                      | accumulate    |
| `priority`      | any                              | `level` ∈ {`critical`, `high`, `normal`, `low`}          | informal context                           | override-only |
| `annotate`      | any                              | (none required)                                          | free-text note                             | accumulate    |
| `rationale`     | any                              | (none required)                                          | *why* the design is this way               | accumulate    |
| `review_note`   | any                              | `for` (a reviewer name or role)                          | the review prompt                          | accumulate    |
| `revision_note` | any                              | `rev`, `date`                                            | what changed and why                       | accumulate    |
| `bom_note`      | one or more components           | `column` (target BOM column)                             | BOM-export-time annotation                 | accumulate    |
| `assembly_note` | one or more components / circuits | `step`                                                  | assembly-drawing annotation                | accumulate    |

Notes on the table:

* **Targets** — the *kind* prescribes how many and what type of
  target it expects. The elaborator checks this; a `placement` on a
  net is an error.
* **Structured args** — keys and value types library-defined.
  The catalog above lists the standard keys; downstream tools may
  recognize more.
* **Merge rule** —
  *override-only* kinds may have at most one declaration per target;
  re-declaration without `override` is an error.
  *accumulate* kinds simply collect across the inheritance chain;
  `remove hint <kind> <target>` drops a parent's contribution.

### 7.2 Naming rules for new kinds

If a project needs a hint kind not on the list above:

1. Use `snake_case` (consistent with parameter and implementation-circuit names).
2. Pick a noun or short noun phrase that names *what the hint
   provides*, not *who consumes it* — `signal_class`, not
   `kicad_layout_hint`.
3. Document the kind alongside the project's library code: target
   arity, expected args, the merge rule, and which downstream tool
   reads it. The compiler does not enforce per-kind typing for
   non-standard kinds — the linter does, against the project's
   manifest. The standard kinds in §7.1 are pre-registered.
4. Avoid kind names that suggest a specific output format
   (`kicad_*`, `altium_*`, `pdf_*`). Hints describe *intent*, not
   *rendering*. A kind named for a tool ages badly.

### 7.3 Free-text style

Hint payloads are read by humans *and* by NLP-augmented downstream
tools. Aim for the same register as a good code comment:

* **Imperative or declarative, not narrative.**
  *Yes:* "center; near USB-C; rotate 90°"
  *Yes:* "high-speed; route as 100 Ohm diff pair"
  *No:* "We thought it would be nice to put the MCU somewhere
  central, maybe close to the USB connector if there's room…"
* **Use units.** "max 5 mm" not "very short". An NLP layer can read
  units; "very short" can't be enforced.
* **Mention the *signal* or *constraint*, not just the part.**
  *Yes:* "mcu.spi1.CLK is critical; route first."
  *No:* "this part is important."

### 7.4 What does *not* belong as a hint

* **Anything the elaborator can check.** Voltage windows, current
  budgets, length matching with a hard tolerance — these are
  `constraint`s, not hints.
* **Library metadata** (manufacturer, MPN, datasheet URL).
  These are component-level fields (or BOM-export concerns), not
  hints.
* **Comments.** A `#` line comment is fine for "TODO refactor this
  circuit"; it doesn't need to be a hint. Hints are for intent that
  *travels with* the design across tools; comments stay in the
  source.

---

## 8. Summary of recommendations

If you remember nothing else:

1. **Descriptions** start with a CATEGORY token; templates per family
   live in §2.1.
2. **Footprints** follow IPC-7351B (SMT) and the disjoint extensions
   in §3.2 for through-hole, mechanical, off-board, and custom.
3. **Parameter names** come from the small standard list in §4.1.
   *Extra component parameters are sometimes useful and usually
   wrong.* New parameters need a check that consumes them, or they
   shouldn't exist.
4. **Designators** are auto-assigned from a `designator_prefix` on
   the component type. Override only when you need a specific
   silkscreen mark; otherwise let `designators.lock` keep them stable.
5. **Hints** are typed intent that travels with the design. Use the
   standard kinds in §7.1 first; declare new ones with `library_kind`
   when truly novel; never use a hint where a `constraint` would do.
