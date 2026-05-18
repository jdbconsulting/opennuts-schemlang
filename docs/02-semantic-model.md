# Semantic Model

This document defines what *exists* in the language and what each thing
*means*. Concrete syntax is the next document; here we are agnostic
about brackets.

The model has three layers:

* **Source layer** — the entities you write: `define`, `include`,
  parameters, generators, `derive` rules, `resolution` blocks, `prefer`
  rules.
* **Elaboration layer** — what the source denotes: a typed hypergraph
  with named bus instances, materialized wrapper choices, and an
  attached **constraint store** (variables with domains, hard
  constraints, soft constraints with weights).
* **Solution layer** — what the solver produces from the elaboration
  layer: a partition over pins, a binding for every variable the
  solver could bind, and a *residual constraint system* containing
  whatever the solver left unbound. The residual is empty for
  fully-determined boards.

A program in the source layer is a *recipe* for producing the
elaboration layer. Compilation is *(parse → elaborate → extract
constraints → solve → emit)*. Underconstrained programs produce a
non-empty residual; overconstrained programs produce a UNSAT minimal
core. The classical "compile-time error" is the special case of an
overconstrained program where the core is a single contradiction.

See `01-design-principles.md` §1.1 for the constraint-substrate at a
glance and §1, principles 10–13 for the non-negotiables that drive
this layering.

---

## 1. The seven kinds

The universe of named things has exactly seven kinds. Three are
*structural* (the shape of the schematic), three are *data* (the
content that flows through it), and one is *intent* (machine-readable
design guidance for downstream tools). Each kind has a distinct
syntactic form (see `03-syntax.md`); here we refer to them abstractly.

| Kind            | Group       | What it is                                                                | Real-world analog                              |
|-----------------|-------------|---------------------------------------------------------------------------|------------------------------------------------|
| **Component**   | structural  | A part you can buy. Has pins (logical) and at least one package binding.  | "STM32H743", "ERJ-2RKF1002X"                   |
| **Package**     | structural  | Physical pin layout. Maps pin numbers to logical pins on a component.     | "QFN-48", "SOIC-8"                             |
| **Circuit**     | structural  | A typed bundle of nets and/or a reusable subcircuit: lanes + roles, ports, instances, connections, constraints, and an optionally-empty body. Subsumes both the "interface" (lanes + roles, no body) and "module" (ports + body) of older drafts. | "SPI", "I2C", "boot flash slot", "buck regulator stage" |
| **Net**         | data        | An equivalence class of pins that are electrically the same node.         | The wire labelled `VCC_3V3`                    |
| **Parameter**   | data        | A typed, possibly-unitful value attached to a definition or instance.     | `Vnom = 3.3 V`, `R = 10 k ±1%`                |
| **Constraint**  | data        | A predicate over parameters and nets, evaluated at elaboration time.      | "VCC ∈ [2.7 V, 3.6 V]", "Σ I_gpio ≤ 100 mA"    |
| **Hint**        | intent      | A typed annotation attached to one or more elaborated objects, with structured args and an optional free-text payload, consumed by downstream tools (typesetters, layout engines, NLP-augmented assistants). | "place mcu near USB", "render power tree on its own sheet" |

These seven are exhaustive. Anything else (a "harness", a "pin group",
a "DRC rule", a "place hint") is expressible as one of these or as a
library on top.

> Why seven, why these? The three structural kinds (Component,
> Package, Circuit) form an orthogonal basis: physical part vs.
> layout footprint vs. typed-graph-fragment-with-optional-internals.
> The three data kinds (Net, Parameter, Constraint) describe the
> *content* that flows through structure. The seventh — Hint — is
> the typed channel for *intent that resists formal encoding*:
> design rationale, layout guidance, signal-class designation, sheet
> partitioning. Hints don't change the elaborated graph; they ride
> alongside it.
>
> Earlier drafts had Interface and Module as separate kinds; the
> distinction tracked "type-only contract" (`(SPI)`, `(POWER)`)
> versus "implementation circuit" (`<boot_flash_slot>`). The split
> turned out to be a phase distinction, not a kind distinction: a
> wrapper that adds I2C pull-ups or conditional SPI source
> termination is *both* a contract and an implementation. Folding
> them into one Circuit kind made inheritance, `prefer`-driven
> wrapper substitution, and conditional structure all fall out of
> mechanisms the language already had.

---

## 2. Components and packages

A **component** is a description of a buyable die plus its logical
pins. Logical pins are named (`VCC`, `CLK`, `IO0`); they are *not*
numbered. A component declares one or more **package bindings**: a
mapping from pin numbers (on a specific physical footprint) to logical
pin names, with optional swap groups.

```
component  ::= name × pins × params × circuit_views × package_bindings × designator_prefix × description
pin        ::= name × pin_role × params
package_binding ::= package × (pin_number → pin_name) × swap_groups
```

Why separate? The same die ships in QFN, BGA, and WLCSP; the same WSON8
hosts dozens of flash dies. Package and component vary independently.

Logical pin names appear in connections; pin numbers appear only inside
package bindings (and inside generators that need them).

A component may also declare **circuit views**: "the pins
`{CS, CLK, MISO, MOSI}` form an `<SPI>` view, in role
`peripheral`, named `boot`." Circuit views are zero-cost: they are
sugar for connecting the underlying pins one-by-one through the
named circuit's lane signature.

Components also carry a **designator prefix** — a one- or two-letter
code (`R`, `C`, `U`, `J`, `Y`, …) used to derive each instance's
reference designator on the silkscreen / BOM (`R7`, `U1`, …). The
prefix is part of the component's type, not its instances; instances
either auto-number off it or carry an explicit override. The standard
prefix table and the auto-numbering algorithm are specified in
`05-naming-conventions.md` §5; designators are otherwise inert
metadata that does not participate in elaboration or constraint
solving.

**Pin roles** are a closed enumeration: `power_in`, `power_out`,
`ground`, `analog_in`, `analog_out`, `digital_in`, `digital_out`,
`digital_io`, `passive` (R/L/C terminals), `nc`. Pin roles drive
default DRC checks (e.g. `power_in` must connect to a net carrying a
`<POWER>` view or a single `power_out`).

### 2.6 Capabilities and pin pools

A component may declare **capabilities**: not concrete bus instances,
but the *contract* under which bus instances can be produced on
demand. This is the model for FPGAs, CPLDs, and flexible-I/O MCUs,
where the count of any given peripheral is determined by the board,
not by the part.

```
capability ::= name × circuit × role × pool × per_bus_constraints
pool       ::= filter over a component's pin set
per_bus    ::= [ lane × pool_ref ] × [ predicate ]
```

Semantically, a capability is a **factory of circuit instances**.
Each factory has:

* a **circuit type** and a **role** (`<I2C> as host`);
* a **pool** — a filter over the component's pin set, defining the
  set of legal pins for any allocation;
* a **per-bus binding** of each lane of the circuit's signature to
  a draw from the pool, plus zero or more predicates that the
  resulting allocation must satisfy.

A component may declare multiple capabilities. Their pools may
overlap (often they do — many capabilities filter "any digital I/O").

#### 2.6.1 Allocation semantics

Capabilities are not realized in the source. They are realized at
elaboration time, in two passes:

1. **Reference collection.** The elaborator scans the connected
   netlist for paths of the form
   `<inst>.<lowercase_circuit>.<name>`, where `<inst>` is an instance
   of a component declaring `provides <Circuit> as <role>`. Each
   distinct `(inst, circuit, name)` triple becomes one **allocation
   request**.
2. **Discharge.** All requests for a given component instance are
   handed to the linear-resource solver as a single problem: every
   request needs one pin per lane, drawn from the relevant pool,
   such that all per-bus predicates hold and no pin is drawn twice
   across all requests. Failure to satisfy is a static error
   identifying the conflicting pair.

The solver is the same CSP regime described in §6 and §12 for swap
groups, bank choice, and parameter binding; capability allocation
is just a larger instance of that problem.

#### 2.6.2 Disjointness across capabilities

When two capabilities on the same component share a pool — e.g. all
of `<I2C>`, `<SPI>`, and `<UART>` filter "any digital I/O" — their
allocations are disjoint by the linear-resource discipline: a pin
drawn for `fpga.i2c.imu.SDA` is not available to `fpga.spi.boot.SCK`.
This is exactly the same property that prevents two MCU buses from
both claiming the same alt-function pin.

#### 2.6.3 Cardinality is not a language concern

A capability does **not** carry a maximum count. The schemlang
elaborator does not pretend to know how many soft-I2C controllers a
given FPGA fits — that is a function of LUT/BRAM budget, signal
complexity, and synthesis settings, none of which the language
models. If too many allocations exhaust the pin pool, the elaborator
reports the failure with the conflict; if too many exhaust the
*logic* budget, the FPGA toolchain reports it. Each tool catches
what it can authoritatively know. The language does not add a soft
cap that lies about precision.

#### 2.6.4 Why this and not extended `bus_decl`

`bus_decl` (component bodies; see `03-syntax.md` §3.1) models a
fixed-count peripheral
with an enumerated alt-function table. It is the right shape for
MCUs and for any silicon with a published pin matrix. Extending it
to cover the FPGA case would require either source-level enumeration
of the cross product (combinatorial blow-up, leaky abstraction) or
the same predicate-and-pool machinery `provides` already provides.
Keeping the two forms separate makes the *intent* of each part
visible at a glance: a part that declares `bus` has fixed
peripherals; a part that declares `provides` has a factory.

The two may unify in a later revision (an MCU with one `bus`
allocation amounts to a `provides` with a single-element pool), but
the current draft keeps them separate to avoid migrating the
existing alt-function tables before there is a real implementation
to migrate them with.

---

## 3. Circuits

A **circuit** is the unified kind for typed graph fragments and
reusable subcircuits. It subsumes what older drafts called
*interfaces* (lanes + roles, no body) and *modules* (ports + body).

```
circuit  ::= name × parents × type_params × value_params
           × lanes × roles × ports × body
body     ::= declarations × connections × constraints × generators
```

A circuit is *type-flavored* if it has lanes and roles and an empty
body — that's what `<SPI>`, `<I2C>`, `<POWER>` look like. A circuit
is *implementation-flavored* if it has ports and a body — that's
what `<boot_flash_slot>` looks like. A circuit is *wrapper-flavored*
if it inherits a type-flavored parent and adds a body — that's what
`<spi_with_termination>` and `<i2c_link>` look like. All three are
the same kind, distinguished by what their bodies do.

### 3.1 Lanes and roles (the "type-flavored" part)

* **Lanes** are the named nets in a circuit's signature: `CLK`,
  `MOSI`, `MISO`, `CS_N`, `SDA`, `SCL`, `POSITIVE`, `NEGATIVE`. Each
  lane has a *signal type* (digital, differential pair, power
  rail, ...).
* **Roles** are perspectives on the lanes. `<SPI>` has roles `host`
  and `peripheral`; `host.CLK` drives, `peripheral.CLK` receives.
  Each participant in a bus instance is tagged with one role.
* **Roles are N-ary, with declared cardinalities.** A role can
  appear zero, one, or many times on a single bus. Cardinality is
  declared on the role; a bus that violates a role's cardinality is
  a UNSAT participant-count constraint, reported with all
  contributing connect sites.
* **Parents** support nominal subtyping: `<3V3> : <POWER>` means a
  `<3V3>` is a `<POWER>` for connection purposes, plus a refinement
  predicate. `<QSPI> : <SPI>` extends the lane set.

A protocol declares cardinality with the role:

```
define <I2C>
  lane SDA, SCL : digital
  role host
    cardinality = 1                 # exactly one master
    drives    { SCL }
    bidir     { SDA }
  role peripheral
    cardinality >= 1                # one or more slaves on the same bus
    receives  { SCL }
    bidir     { SDA }
```

`cardinality` accepts `= N`, `>= N`, `<= N`, `in [LO, HI]`, or `*`
(unbounded). The default for a `role` declaration without an
explicit cardinality is `cardinality = 1` — strict point-to-point.
SPI is point-to-point on host (`= 1`) and peripheral
(`= 1`); UART is `= 1` and `= 1`; I2C is `= 1` and `>= 1`; a
multi-master I2C variant uses `<MULTI_MASTER_I2C>` with
`host: cardinality >= 1`.

Connecting circuit values whose types are role-compatible is
structural unification on matching lane names, joined into a
single **bus instance** (see §4). The role tags must satisfy the
protocol's cardinality constraint for the bus instance.

```
host_spi  : <SPI> as host        ⟶ {CLK: out, MOSI: out, MISO: in,  CS: out}
slave_spi : <SPI> as peripheral  ⟶ {CLK: in,  MOSI: in,  MISO: out, CS: in }
host_spi <-> slave_spi           # OK: roles are dual; cardinality 1+1 satisfied
```

A `<QSPI> as host` and `<SPI> as peripheral` may not silently mate
in either direction. An explicit `as <SPI>` projection is required.

### 3.1.1 Derived attributes (`derive`)

A port or a role may publish **derived attributes**: named values
that the elaborator computes from the port's structural environment
and posts as equality constraints when a bus instance is
materialized. This is the mechanism by which a wrapper's `vref`
port can be auto-bound to whatever rail the host's bank actually
runs on, without forcing the connect site to spell it out.

```
role host
  cardinality = 1
  drives  { SCL }
  bidir   { SDA }
  derive vref = bank(SDA).vio_port      # a <POWER> port path
  derive Vio  = bank(SDA).Vio            # a voltage variable
```

Or attached to a specific bus port on a component:

```
bus i2c0 : <I2C> as host
  lane SDA <-> bank2.PB7
  lane SCL <-> bank2.PB8
  derive vref = bank2.vio_port
```

Reading rules:

* The right-hand side of a `derive` is a path expression evaluated
  in the port's elaboration scope. It may reference any constraint
  variable visible there (bank Vios, supply ports, sibling
  parameters).
* When a wrapper `<W>` is materialized for a bus instance whose
  participants publish `derive P = …` for a name `P` that matches
  one of `<W>`'s non-lane ports or parameters, the elaborator posts
  an equality constraint between `bus_instance.P` and each
  participant's derivation. With N participants, there are N
  equalities; the solver unifies them.
* A name a wrapper does not understand is silently ignored — a port
  can publish `derive vref` even if some wrappers it might pair
  with don't have a `vref` port. (This keeps device models from
  needing to know about every wrapper that could wrap them.)
* A wrapper port the bus instance does *not* receive a derivation
  for must be supplied at the connect site, or the wrapper
  declaration must mark the port `optional`. An unbound mandatory
  wrapper port is a UNSAT obligation, reported with the wrapper
  definition and the connect site.

`derive` always produces *equality constraints* — never assignments.
Two derivations of the same wrapper port from different participants
must be unifiable; if they aren't, the elaborator reports the chain
back to source. The classical "all I2C participants must share Vio"
check is the unifiability of `derive Vio = …` across participants,
not a comparison of literals.

### 3.2 Ports, instances, and bodies (the "implementation-flavored" part)

* **Type parameters** allow generics: `<flash_slot> over PART : <SPI_flash>`.
* **Value parameters** are typed: `parameter R_pullup : resistance`.
* **Ports** are circuit-typed: `port spi : <SPI> as host`.
* **Body** is the same kind of stuff that lives at top level:
  declarations of instances, connections, constraints, and
  generators.

Circuits elaborate *applicatively*: instantiating a circuit copies
its body into the parent hypergraph, with parameter substitution.
Two instantiations of the same circuit produce two disjoint
sub-hypergraphs; their internal nets do not collide.

Circuits are not macros: they are checked against their declared
signature *once*, and instantiations only need to verify they pass
arguments matching the signature.

### 3.3 Lane endpoints and splicing (the "wrapper-flavored" part)

A circuit that is *both* type-flavored (declares lanes + roles) and
has a body is a **wrapper**. It sits between two endpoints of the
same protocol, with the lanes flowing through it.

For each lane of a wrapper, two implicit endpoints are introduced
inside the body:

* `lane.host_side` — connects to whatever the host endpoint
  contributes to that lane.
* `lane.peri_side` — connects to whatever the peripheral endpoint
  contributes.

The default body wires `lane.host_side <-> lane.peri_side` for every
lane: a wrapper with no body is the identity (a wire-through). The
body suppresses the default wire on a lane by writing any explicit
connection involving `lane.host_side` or `lane.peri_side`.

The common case — series insertion of a two-terminal component —
gets a sugar:

```
splice CLK with R_clk
```

is equivalent to

```
lane.host_side(CLK) <-> R_clk.T1
R_clk.T2            <-> lane.peri_side(CLK)
```

with the default through-wire on `CLK` suppressed. Asymmetric and
multi-pin cases use the explicit endpoint references directly.

### 3.4 Bus instances and wrapper materialization

A **bus instance** is the elaboration-layer entity that mediates
between participating ports of a circuit type. Wrappers attach to
*bus instances*, not to individual `<->` edges. This is what makes
multi-drop buses (I2C, multi-target SPI, daisy-chained UART)
correct: one wrapper per bus, regardless of the number of edges.

#### 3.4.1 How bus instances are formed

Two `<->` lines that name the same path on one side refer to the
*same* bus instance:

```
fpga.i2c.imu  <-> imu.i2c        # creates bus-instance "fpga.i2c.imu"
fpga.i2c.imu  <-> env.i2c        # joins the existing bus-instance
fpga.i2c.imu  <-> connector.i2c  # joins again; now three participants
```

Two `<->` lines whose left-hand and right-hand paths produce a
shared port reference (e.g. via a named instance of a wrapper, or
via a `bus_decl` allocation) likewise belong to the same bus
instance. The elaborator builds an equivalence over connect sites
and turns each equivalence class into one bus-instance entity.

A bus instance carries:

* a *circuit type* (the abstract protocol or a concrete wrapper);
* a *wrapper-choice variable* (see §3.5) ranging over the abstract
  and any in-scope wrappers; resolved by `prefer`, per-port
  `prefer wrapper = …`, or the abstract's `resolution` block;
* a *role-multiset* — for each role of the protocol, the multiset
  of participants tagged with that role;
* a *parameter store* — the wrapper's parameters as constraint
  variables, with constraints contributed by `derive` rules,
  connect-site overrides, and the wrapper itself;
* the lane unification — every participant's lanes joined per
  protocol signature;
* zero or more *non-lane wrapper ports* (`vref`, etc.) bound to
  participants' `derive` rules and/or connect-site arguments.

The elaborator emits each bus instance once per equivalence class.

#### 3.4.2 Cardinality enforcement

For each role `R` of the bus's circuit type with declared
cardinality `K`, the role-multiset's count of participants tagged
`R` must satisfy `K`. An I2C bus with two participants tagged
`host` is a UNSAT constraint, reported with both source locations.

A `cardinality = 1` role (the default) reduces to point-to-point;
`cardinality = 0` is illegal (it makes the role useless);
`cardinality >= 1` is the multi-drop case.

#### 3.4.3 Wrapper selection

The wrapper-choice variable for a bus instance ranges over: the
bus's abstract circuit, plus every concrete wrapper that inherits
from it (transitively) and is in lexical scope at the bus's
declaration site. The variable is resolved by §3.5.

The default wrapper-choice variable's domain is finite (it lists
in-scope candidates) and the variable is solver-bound just like
any other constraint variable: hard-bound by the most-local
`prefer <C> = <X>`, soft-bound by participants' `prefer wrapper = …`
declarations weighted against the abstract's `resolution` rules,
or — if nothing constrains it — bound to the abstract itself.

#### 3.4.4 The implicit wrapper, restated

If `prefer <I2C> = <i2c_link>` is in scope at a bus instance's
declaration site, the bus instance's wrapper-choice is hard-bound
to `<i2c_link>`, and the elaborator materializes one
`<i2c_link>` body per instance. Lane unification proceeds normally;
the wrapper's parameters and non-lane ports receive constraints
from `derive` rules and connect-site overrides; the lanes flow
through.

For `<SPI>` and other point-to-point protocols, the bus instance
has exactly two participants and the wrapper sees them as `host`
and `peripheral`. For `<I2C>` and other multi-drop protocols, the
bus instance has N participants and the wrapper sees a single
`host` plus N−1 `peripheral`s.

This makes "every I2C connection has pull-ups" a one-line project
policy: declare `prefer <I2C> = <i2c_link>` near the project root,
and every bus instance whose type is `<I2C>` materializes one
`<i2c_link>` body. The pull-ups appear once per *bus*, not once
per `<->` edge — which is electrically correct.

Suppressing the wrapper on a specific bus is a local
`prefer <I2C> = <I2C>` (binding the abstract to itself), an
explicit instantiation of a different wrapper, or a connect-site
`prefer <I2C> = <X>` clause on the bus's declaring connection
(see `03-syntax.md` §6.4 for the connect-site argument block).

### 3.5 Wrapper resolution and per-port preferences

A bus instance's wrapper-choice variable is bound by, in order of
descending priority:

1. **Connect-site explicit `prefer`.** A `prefer <C> = <X>` clause
   in the connect-site argument block hard-binds the wrapper for
   that bus instance. Highest priority.
2. **Lexical-scope `prefer`.** The most-local enclosing
   `prefer <C> = <X>` rule binds the wrapper unless overridden at
   the connect site.
3. **Resolution rule on the abstract.** The abstract circuit's
   `resolution` block (§3.5.1) reads each participant's
   `prefer wrapper = …` declarations and produces a soft-weighted
   choice.
4. **Default.** The abstract itself, used as an empty-bodied
   protocol.

Each of (1)–(3) contributes constraints with descending weights to
the wrapper-choice variable; the solver picks the maximum-weight
satisfiable assignment. Ties at the highest weight emit a
`wrapper_choice_ambiguous` UNSAT core listing the candidates and
the contributing sources.

#### 3.5.1 The `resolution` block

A protocol circuit may carry a `resolution` block that
characterizes how participant preferences combine:

```
define <I2C>
  ...
  resolution
    case any(prefers <i2c_link>)        => <i2c_link>     weight 10
    case all(prefers <I2C>)             => <I2C>          weight 5
    default                             => <i2c_link>     weight 1
```

Each `case` posts a soft-equality constraint on
`bus_instance.wrapper` with the named weight, gated by a
quantifier over the role-multiset:

* `any(prefers <X>)` — at least one participant has
  `prefer wrapper = <X>` declared on its port.
* `all(prefers <X>)` — every participant has
  `prefer wrapper = <X>` declared on its port.
* `none(prefers <X>)` — no participant prefers `<X>`.
* `count(prefers <X>) >= N` — at least N participants prefer `<X>`.

The `default` case posts a baseline weight that always applies, so
the variable has *some* binding even when no participant expresses
a preference. Cases are ordered; the *weight*, not the *order*,
determines who wins. Ties are reported at solve time.

Resolution rules **are soft**, deliberately. A user who needs
deterministic behavior in the face of conflicting participant
preferences uses a scope-level `prefer` (priority 2) or
connect-site `prefer` (priority 1) to override.

#### 3.5.2 Per-port wrapper preferences

A circuit-typed port (or a `bus_decl` participant) may declare:

```
port i2c : <I2C> as peripheral
  prefer wrapper = <I2C>            # "I have internal pull-ups"
```

When this port joins a bus instance, its declaration contributes a
soft-equality `bus_instance.wrapper = <I2C>` with the device-model
author's chosen weight (typically the role's default; a sensor
that *strongly* claims internal pull-ups can declare
`prefer wrapper = <I2C> weight 8` to outvote the default).

The elaborator does not interpret what "internal pull-ups" means;
it treats the preference as data and lets the resolution function
combine it with the other participants'. A user who wants to
*disable* internal pull-ups changes a sensor parameter
(`pullups_enabled = false`), which the device model translates
into a different `prefer wrapper = …` value (typically
`<i2c_link>`). Disabling the wrapper itself is a separate
override at scope or connect-site level — these are different
intentions and the language keeps them distinct.

#### 3.5.3 Why a soft-default conservative wrapper

In the standard library, the default `<I2C>` resolution biases
toward `<i2c_link>` (i.e. *any participant prefers the wrapper ⇒
wrapper wins*). The reasoning: device models that claim "I have
internal pull-ups" are routinely too weak for actual I2C bus
capacitance at 400 kHz or above, and the safe answer when in doubt
is "add the external pull-ups." A team that consciously decides
otherwise overrides at scope or connect-site level. The language's
*default* is the conservative choice.

### 3.6 Why circuits, not interfaces and modules

Earlier drafts had Interface and Module as separate kinds. The split
tracked "type-only contract" vs. "implementation circuit" and
matched the ML signatures-vs-structures distinction. It read well
on paper but produced a spurious distinction in practice: a wrapper
that adds I2C pull-ups or conditional source termination is *both*
a contract (it has the lanes and roles of `<I2C>` or `<SPI>`) and
an implementation (it has resistors inside). Older syntax forced
such wrappers to be modules with two interface-typed ports plus
explicit pass-through wiring; the new `<wrapper> : <PROTOCOL>` form
inherits the contract and only writes the part that's new.

The unification also collapses three previously-separate
mechanisms — interface views on components, module ports on
subcircuits, and "implicit wrappers" for protocol decoration — into
one: every connection threads through a prevailing circuit value,
which is by default the trivial empty-bodied one but can be a
wrapper when project policy says so.

---

## 4. Nets

A **net** is an equivalence class of pins. The user rarely names nets
directly; named nets are useful for debugging, probing, and labelled
signals (`VCC`, `GND`).

Operationally:

* Every pin starts in its own singleton net.
* Each `connect` (`<->`) merges the equivalence classes of its
  operands.
* After elaboration, the netlist is the partition.

A net can carry an **annotation**: a signal type (`<POWER>`,
`<DIFF_PAIR_2V5>`, `<DIGITAL_3V3>`, `<GND>`). When a connection
involves typed nets, types must unify. When typed nets meet untyped
nets, the typed annotation propagates.

There is exactly one global "ground reference per voltage domain"
concept; everything else is just nets.

---

## 5. Parameters and units

Parameters carry **type, unit, domain, and (optionally) predicates**.
*Every parameter is a constraint variable* in the elaboration store,
not a literal value. A parameter declared with a singleton domain
(e.g. `parameter R_pullup : resistance = 10 k`) is a variable whose
domain happens to contain one value; one declared with a range
(`in [2.7 V, 3.6 V]`) is a variable whose domain is a continuous
interval; one declared with an enumerated set
(`in {1.8 V, 2.5 V, 3.3 V}`) is a finite-domain variable.

```
parameter R_pullup : resistance = 10 k +/- 1%             # variable, domain ~= [9.9 k, 10.1 k]
parameter Vnom     : voltage    in [2.7 V, 3.6 V]         # variable, continuous domain
parameter Vio      : voltage    in {1.8 V, 2.5 V, 3.3 V}  # variable, finite domain
parameter Tr       : temperature_range = -40 deg C .. 85 deg C   # variable, range domain
parameter Imax     : current    <= 100 mA                 # variable, half-open domain
```

The base types form a small algebra:

* Scalars: `voltage`, `current`, `resistance`, `capacitance`,
  `inductance`, `power`, `frequency`, `time`, `temperature`,
  `dimensionless`.
* Ranges: `lo .. hi` on any scalar with order.
* Enumerations and tagged unions for catalog-style choices, used
  primarily as finite domains.
* **Tolerances** (§5.1) — a first-class form for declaring the
  positive and/or negative spread of a parameter around its
  nominal. A toleranced parameter is *interval-valued*.

Units are part of the type, not strings. Mixing volts and amps is a
type error. Unit algebra (`V / A == Ohm`) is built in. Resistance
values in source are written without a unit (the type system infers
ohms): `R = 4.7 k` and `R = 22` are unambiguous in any context whose
parameter is typed `resistance`. The Unicode `Ω` symbol is not a
lexeme; the rare case where unit algebra must be written explicitly
uses the ASCII `Ohm`.

Parameters are visible inside the definition that owns them and inside
constraint expressions. They can be referenced from generators
*only if the elaborator can prove the parameter is bound at
generator-expansion time* — generators are statically expanded
(see §7), so they can't depend on values the solver has yet to
pick. A parameter with a domain wider than one value used in a
generator condition without being narrowed by a prior constraint
is a static error.

### 5.1 Tolerances and interval-valued variables

A parameter may carry a **tolerance clause** after its nominal value.
A toleranced parameter is *interval-valued*: every reference to it
exposes three fields — `.Vnom` (the declared centre), `.Vlo` (the
worst-case lower bound), and `.Vhi` (the worst-case upper bound).
Constraints over the parameter range over the *interval*, not the
nominal point (see §6.1 for the containment semantics of `in`).

The tolerance clause is intentionally permissive about how engineers
spell tolerances on the page. All of the following are accepted, and
all desugar to the same `(Vnom, Vlo, Vhi)` triple:

```
parameter Vnom : voltage = 3.3 V +/- 3 %                  # symmetric percent
parameter Vnom : voltage = 3.3 V +/- 100 mV               # symmetric absolute
parameter Vnom : voltage = 3.3 V + 5 %, - 1 %             # asymmetric percent
parameter Vnom : voltage = 3.3 V + 0.1 V, - 0.05 V        # asymmetric absolute
parameter Vnom : voltage = 3.3 V + 5 %                    # one-sided up; Vlo == Vnom
parameter Vnom : voltage = 3.3 V - 0.1 V                  # one-sided down; Vhi == Vnom
parameter Vnom : voltage = 3.3 V + 5 %, - 0.1 V           # mixed kinds
```

The grammar:

* `+/-` (canonical) and `+-` (alias) introduce a **symmetric** tolerance.
* A comma-separated list of one or two **directional** terms — `+ <amount>`,
  `- <amount>` — introduces an asymmetric or one-sided tolerance.
  Order is free; each direction may appear at most once.
* Each `<amount>` is either a percentage (`5 %`, applied to the
  nominal) or an absolute quantity in the parameter's natural unit
  (`0.1 V`, `50 mA`, `2 k`).
* A direction that's omitted defaults to **zero** — the bound on
  that side is the nominal itself, not an unbounded interval. This
  is the right default for one-sided datasheet figures (e.g. an
  RTD's resistance `100 + 5 %` with no down-tolerance).

Desugaring:

| Form                     | Vlo                          | Vhi                          |
|--------------------------|------------------------------|------------------------------|
| `N`                      | `N`                          | `N`                          |
| `N +/- p %`              | `N * (1 - p/100)`            | `N * (1 + p/100)`            |
| `N +/- a` (absolute)     | `N - a`                      | `N + a`                      |
| `N + p %, - q %`         | `N * (1 - q/100)`            | `N * (1 + p/100)`            |
| `N + a, - b`             | `N - b`                      | `N + a`                      |
| `N + p %, - b`           | `N - b`                      | `N * (1 + p/100)`            |
| `N + p %`                | `N`                          | `N * (1 + p/100)`            |
| `N - q %`                | `N * (1 - q/100)`            | `N`                          |

(The same rules apply to any unit-typed scalar: voltage, current,
resistance, frequency, etc.)

After desugaring the variable has the constraint `Vlo <= X <= Vhi` in
the store. Tolerance forms are interchangeable; in particular
`+/- p %` is exactly the same store-state as `+ p %, - p %`.

A parameter without a tolerance clause is **point-valued**:
`Vlo == Vnom == Vhi`. The interval-valued machinery of §6.1
collapses gracefully to ordinary point semantics in that case.

---

## 6. Constraints

A **constraint** is a predicate the elaborator records and the
solver discharges. There are three classes by *origin* and two
classes by *strength*.

By origin:

1. **Local constraints** — written next to a pin or component: "this
   pin's net must satisfy `V in [Vmin, Vmax]`."
2. **Aggregate constraints** — sums or counts across a structure:
   "sum of I over all pins in bank A <= I_bank_max."
3. **Topological constraints** — properties of the resulting graph:
   "every input has exactly one driver"; "no two `power_out` pins on
   the same net."

### 6.1 Interval semantics: `in` is containment

A subtle but consequential rule: when at least one operand of `in`
is **interval-valued** (§5.1), the predicate is read as *interval
containment*, not point membership. Concretely:

```
x in [lo, hi]
  desugars (when x is interval-valued) to:
    lo <= x.Vlo  AND  x.Vhi <= hi
  desugars (when x is point-valued) to:
    lo <= x      AND  x      <= hi
```

This is the difference between *"there exists an operating point at
which the chip is happy"* (point intersection — wrong) and *"under
every operating condition within the rail's tolerance, the chip is
happy"* (interval containment — what we mean). Toleranced rails
plus tolerant pins compose correctly only with the containment
reading.

A worked example. A `<3V3>` rail with `Vnom = 3.3 V +/- 3 %` makes
its `POSITIVE.V` an interval-valued variable with `Vlo = 3.201 V`,
`Vhi = 3.399 V`. A part with `parameter Vmax : voltage = 3.3 V` and
`parameter Vmin : voltage = 1.6 V` writes the obvious
`constraint net.V in [Vmin, Vmax]`. Because `net.V` is interval-
valued (it inherits the rail's interval), the constraint posts as
`1.6 <= 3.201 AND 3.399 <= 3.3`. The second clause is false, so the
solver returns UNSAT with a core that names exactly the rail-pin
pairing — a genuine "this rail is too high for this part on its
worst day" diagnostic, not a false-positive SAT hiding a real bug.

The bounds are also accessible **directly** via field syntax for
constraints that need them explicitly:

```
constraint net.V.Vhi <= flash.Vmax_abs        # absolute-max derating
constraint net.V.Vlo >= flash.Vbrownout       # operating min
constraint pull_window : pull.Vhi - pull.Vlo <= 0.05 V   # rail flatness
```

Field access on a point-valued operand gives back the same scalar
three times (`Vlo == Vnom == Vhi`), so the explicit forms work with
or without tolerance.

#### Net-level interval aggregation

A net's interval is the **union** of every interval-valued source
that drives it. If two `<3V3>` rails with different tolerances feed
the same net, the net inherits the wider interval (and a separate
hard constraint requires their `Vnom` agree, with a UNSAT core
naming both rails if not).

#### `Vmax_abs` / `Vmin_abs` (absolute-max ratings)

Datasheets typically distinguish *recommended operating range*
(`Vmin`, `Vmax`) from *absolute maximum ratings* (`Vmax_abs`,
`Vmin_abs`) — the destruction thresholds. Schemlang treats both as
optional standard-vocabulary parameters (§5 and `05-naming-
conventions.md` §4.1):

* `Vmin` / `Vmax` — recommended operating window. Violation is a
  hard error: the part is being operated outside its spec.
* `Vmax_abs` / `Vmin_abs` — absolute maximum / minimum. Violation
  is a hard error with severity *destructive*: the part is being
  damaged. The explain trace flags it specially.

A part that declares both gets two layered checks for free; the
`Vmax_abs` predicate gives derating analysis (e.g. "rail must stay
20 % below abs-max under tolerance corner") a clean place to live:

```
constraint net.V.Vhi <= 0.8 * flash.Vmax_abs soft 5    # 20% derating goal
```

The soft form expresses "we'd like derating; warn if no headroom"
without hard-failing.

By strength:

* **Hard constraints** must hold; the solver will not produce a
  solution that violates a hard constraint. UNSAT is a failure
  with a minimal core.
* **Soft constraints** carry non-negative weights; the solver
  picks an assignment maximizing the sum of satisfied soft
  weights. Ties at the maximum weight are reported with the
  candidates.

`prefer X = Y soft <weight>`, `resolution` cases (§3.5.1), and
per-port `prefer wrapper = …` declarations all desugar to soft
constraints. The default weight rules are documented per source.

The elaborator's role is *constraint extraction*: walk the
program, post each constraint (with its source location) to the
store, then dispatch to the solver. The solver returns one of:

* **Unique solution** — every variable is bound; the netlist is
  fully determined.
* **Multiple solutions** — some variables remain unbound after
  hard-constraint discharge; the solver picks the
  maximum-soft-weight assignment, and reports any unbound variables
  as residuals (§13).
* **UNSAT** — no assignment satisfies all hard constraints; the
  solver returns a *minimal core* (a smallest subset of hard
  constraints whose conjunction is unsatisfiable), each annotated
  with its source location.

Users can define their own constraint predicates as part of a library.
The core language provides only the substrate (Boolean and arithmetic
over the parameter algebra; see §12 for the formal substrate).

---

## 7. Hints

A **hint** is a typed annotation attached to one or more elaborated
objects (instances, nets, circuits). It carries structured arguments
and an optional free-text payload. The elaborator does not interpret
hint payloads; it just attaches them to the right objects in the
output and emits them for downstream tools.

```
hint ::= name × kind × targets × args × text_payload?
kind     ::= IDENT                       (* `placement`, `sheet`, `signal_class`, `annotate`, ... *)
targets  ::= [ path | type_ref ]         (* one or more elaborated objects *)
args     ::= [ key × value ]             (* structured key/value pairs; kind-specific *)
```

### 7.1 What hints are *not*

Hints **do not**:

* change the elaborated hypergraph;
* affect the netlist;
* participate in unification or constraint discharge;
* prevent compilation if the consumer doesn't recognize the kind.

A hint a downstream tool doesn't recognize is silently passed through
(or dropped, at the tool's option). This is intentional: forward
compatibility with new hint kinds matters more than strict validation.

### 7.2 Why a free-text payload

Some intent — *"keep the guard ring intact on the AFE,"* *"this clock
is critical; route it first and away from switching nodes"* — does
not compress into a tidy enumeration. The payload is free text so
humans write what they mean; modern NLP-augmented downstream tools
(layout assistants, design-review bots) extract actionable structure
from the prose. A non-NLP renderer ignores the payload and uses only
the structured args; the language degrades gracefully.

### 7.3 Kinds and their args

The *kind* tells a consumer what to do; the *args* are kind-specific.
The standard library declares an open set of starter kinds
(`placement`, `sheet`, `signal_class`, `annotate`, `group`, `near`,
`keepout`, `priority`) — see `05-naming-conventions.md` §7. Users can
add new kinds; collisions across libraries are resolved like any other
identifier (lexically scoped, last-include-wins).

### 7.4 Scoping and merging

Hints obey the same lexical scoping and inheritance/override rules as
other body productions (see §10):

* A hint declared in circuit `C` sees `C`'s instances by lexical name.
* A child circuit that inherits from a parent inherits the parent's
  hints, and may `override` or `remove` them.
* Multiple hints with the same `(kind, targets)` accumulate by
  default. A specific kind may declare itself **override-only** (e.g.
  `placement`: it makes no sense to have two placements for one part)
  in which case re-declaration without `override` is an error.

### 7.5 Where hints are emitted

After elaboration, the compiler emits a *hint stream* alongside the
flat netlist: a list of `(hint_id, kind, target_path, args,
text_payload)` records. Downstream tools consume the stream
selectively — a typesetter reads `placement`, `sheet`, `near`; a
layout engine reads `signal_class`, `keepout`; an NLP-aware
review-bot reads `annotate` and `rationale`. The compiler does not
prescribe a wire format here; it is a stable in-memory artifact that
tools agree on.

---

## 8. Generators

A **generator** is a bounded loop or conditional that produces
declarations and connections:

```
generator ::= for | if | match
```

Generators do not have unbounded recursion. The language expansion
phase fully unrolls them; if the bounds are not statically determinable
(modulo parameters fixed at the call site), it is an error.

Generators are *declarative*: the body of a `for` is a set of
declarations and connections, not statements with side effects.
Two iterations of a `for` produce two independent declarations whose
order is irrelevant to the final hypergraph.

---

## 9. Includes and namespaces

A `.schemlang` file is a **package of definitions** (in the
namespace-of-names sense, not the physical-footprint sense). It
exports a set of named
definitions. `include` is *qualified import* by default, with an
optional alias:

```
include "std/spi.schemlang"           # exports under file's local name
include "std/spi.schemlang" as bus    # exports under "bus."
include "std/spi.schemlang" {SPI, QSPI}   # selective import
```

There is no global namespace and no transitive re-export by default.
Cyclic includes are an error. The include graph is a DAG.

`.schemlang` is the only source-file extension. Designator locks,
project preferences, library indexes — every persistent artifact —
lives as a block in a `.schemlang` file.

### 9.1 Abstract types and `prefer`

A type is **concrete** when it denotes a fully-determined elaboration
(a buyable MPN, an empty-bodied protocol circuit), and **abstract**
when it names a contract that a concrete value must satisfy. The
distinction applies to both components and circuits:

| Abstract                    | Example concrete       | Concrete declares                             |
|-----------------------------|------------------------|-----------------------------------------------|
| `{RESISTOR}`                | `{ERJ-PHF}`            | parametric vendor part                        |
| `{CAPACITOR}`               | `{GRM}`                | parametric vendor part                        |
| `<I2C>`  (lanes only)       | `<i2c_link>`           | wrapper with pull-ups, conditional            |
| `<SPI>`  (lanes only)       | `<spi_with_termination>` | wrapper with conditional source termination |
| `<3V3>`  (rail contract)    | `<3v3_with_decoupling>`| local decoupling on every connection          |

The `prefer` form binds an abstract to a concrete:

```
prefer {RESISTOR} = {ERJ-PHF}              # vendor part for any resistor
prefer <I2C>      = <i2c_link>             # pull-ups on every I2C bus
prefer <SPI>      = <spi_with_termination> # conditional source termination
```

`prefer` is a binding from an abstract type to a concrete type that
supplies the same contract. Resolution is **lexical** and
**innermost-wins**: the elaborator walks outward from each abstract
use to find the nearest enclosing `prefer` rule.

Semantically, `prefer` is the language's analog of a typeclass
instance declaration: it tells the elaborator how to discharge an
abstract obligation. An abstract use with no enclosing `prefer` is
either resolved to the abstract itself (for circuit kinds whose
abstract form is a perfectly-good empty-bodied protocol — connecting
two `<I2C>` ports just makes wires) or a static error (for
component kinds, where you must commit to a vendor part to produce
a netlist).

The full surface syntax is in `03-syntax.md` §11; the use cases are
in `04-cookbook.md` Recipe 11 (vendor parts) and Recipes 14–15
(wrappers).

---

## 10. Inheritance and merging

Any of the three structural kinds (Component, Package, Circuit)
may declare zero or more **parents** of the same kind, in an
ordered list. The semantics of a definition with parents is the
semantics of a single body produced by **merging** the parent bodies
left-to-right and then layering the new body on top. Merging is a
total function over a small product structure, so the semantic model
does not change in size — just the source-level convenience for
producing such bodies.

### 10.1 The merge algebra

A definition body is, semantically, a record of named member sets:

```
Body = { params: Map<Name, Param>,
         pins:   Map<Name, Pin>,           # components
         lanes:  Map<Name, Lane>,          # type-flavored circuits
         roles:  Map<Name, Role>,          # type-flavored circuits
         ports:  Map<Name, Port>,          # impl-flavored circuits
         insts:  Map<Name, Instance>,      # impl-flavored circuits
         hints:  Bag<Hint>                 # any kind                    
              ⊎ Map<(Kind,Targets), Hint>, # for override-only kinds
         conns:  Set<ConnectEdge>,         # circuits / components
         cstrs:  Map<Name, Constraint>     # (anonymous constraints get fresh names)
              ⊎ Bag<Constraint>,
         views:  Map<(IntfType, Name), View>,
         pkgs:   Map<PackageType, PackageBinding>,
         swaps:  Set<SwapGroup>,
         gens:   List<Generator> }
```

The merge of two bodies `A ⊕ B` (B applied on top of A) is field-wise:

* For map-keyed fields: `A ⊕ B = A.update(B)`. If a key exists in both,
  the values must be **compatible** (defined per-field; e.g. a `Lane`
  is compatible if its signal type is identical) or marked
  `override`-d in `B`. Incompatible-and-not-overridden is a *merge
  conflict*.
* For set/bag fields: `A ⊕ B = A ∪ B`, with deduplication of equal
  elements.
* For ordered list fields (`gens`): `A ⊕ B = A ++ B`.

`override(name)` in `B` means "delete from `A` first, then insert from
`B`." `remove(name)` in `B` means "delete from `A`." A `B` containing
`remove(x)` and an entry for `x` is an error (use `override` to express
"replace").

Merging is associative when no conflicts arise (so `A ⊕ B ⊕ C`
is unambiguous left-to-right) and commutative for the union/bag
fields. It is **not** commutative for the map-keyed fields with
overrides, which is the whole point of "order matters."

### 10.2 Conflict checking

After computing the merged body, the elaborator runs a per-field
*compatibility pass*:

* `params`: same name across layers must have compatible type and a
  consistent value/predicate, unless the topmost layer is an
  `override`. Differing values from two parents — without a body-level
  resolution — is reported with both source locations.
* `lanes`: signal types must match.
* `pins`: roles, aliases, and pin-numbering must match.
* `roles`: a lane may not appear in two different direction sets
  across layers.
* `ports`, `insts`: same name with different type is an error;
  identical content deduplicates; conflicting content requires
  `override` or `remove` in the body.
* `pkgs`: same package type from multiple parents must agree pin-for-pin.

Conflicts are static errors with two source spans: the prior layer
that introduced the member and the layer that disagreed. A single
`override`/`remove` in the new body always suffices to silence the
error.

### 10.3 Why this works without C3 or any clever MRO

C3 linearization solves the problem of *implicit* method resolution in
the presence of multiple inheritance. We don't have implicit
resolution: every member has a name, every conflict surfaces, every
override is written explicitly. The simplest possible linearization
(left-to-right with body last) is therefore sufficient and more
predictable for source-level review than C3.

Diamond inheritance is fine: an identical contribution from an
ancestor reached by two parents merges with itself by deduplication.
Non-identical contributions surface as conflicts and are resolved
explicitly.

### 10.4 Removal as semantic deletion

`remove name` is a body-level operator that *deletes* a key from the
merge accumulator. After removal:

* a member with that name is no longer present;
* references from later layers to that name resolve as if the member
  was never declared;
* references from *prior* layers to that name (e.g. a parent's
  `connect` mentioning a removed instance) fail elaboration with a
  "removed dependent" error.

The fix for the last case is to also remove or override the dependent.

`remove parent <P>` is shorthand for "delete every key whose source
attribution is `<P>`." Source attribution is tracked through the merge
so that removing one mixin's contributions doesn't disturb others —
even when those others happened to also touch the same name (their
contribution survives).

### 10.5 Effect on the elaboration pipeline

The pipeline gains a step **3a** between type-checking and elaboration:

* **3a. Resolve parents and merge bodies.** For every definition with
  parents, compute the merged body via §10.1, run the conflict pass
  per §10.2, apply `override` and `remove` operators, then proceed to
  elaboration with the merged body as if it were the original source.

Everything downstream (elaboration, constraint extraction, solving)
is unchanged. Inheritance is purely a source-level abstraction;
semantically, every program denotes the same constraint system it
would have denoted if all parents had been inlined and conflicts
resolved by hand.

---

## 11. The elaboration pipeline

Compilation of a board file proceeds in eight named passes. Passes
1–4 are *structural*: they produce the elaborated tree and bus
instances. Passes 5–7 are the *constraint pipeline*: they extract
constraints, dispatch to the solver, and post-process. Pass 8
emits.

```
1. Parse              – parse every reachable file into ASTs.
2. Resolve            – name resolution in lexical scope; build include DAG.
3. Type-check         – check definitions (components, packages, circuits) in
                        dependency order.
3a. Merge             – for each definition with parents, compute the merged body
                        (§9), apply `override`/`remove`, report conflicts.
4. Elaborate          – walk the tree from the `use`-named top circuit,
                        instantiating circuits, expanding generators (statically
                        bounded), materializing pins, and forming bus instances
                        (§3.4) for each connect-equivalence class.
4a. Materialize       – for each component with `provides`, register allocation
                        requests; for each bus instance, register its
                        wrapper-choice variable, lane unifications, and
                        non-lane port obligations.
5. Extract            – walk the elaborated tree posting variables, domains, hard
                        constraints, soft constraints, and weights to the
                        constraint store. Each posting carries source location.
6. Solve              – dispatch the constraint store to the solver. The solver
                        returns one of:
                          (a) FULL: every variable bound; produce a fully-resolved
                              netlist.
                          (b) PARTIAL: hard constraints satisfied, soft weights
                              maximized, but some variables remain unbound; emit
                              the netlist plus a residual constraint system (§7).
                          (c) UNSAT: no assignment satisfies all hard constraints;
                              emit a minimal core (subset of constraints whose
                              conjunction is unsatisfiable), each cited to source.
6a. Wrapper resolve   – for each bus instance whose wrapper-choice variable was
                        bound to a wrapper `<W>`, materialize one body of `<W>`
                        and re-walk lane unifications. (Wrappers materialized
                        before solving may add new constraints; the solver re-runs
                        until a fixed point. Termination is guaranteed because
                        the wrapper graph is acyclic — see `README.md`
                        open question on wrapper-loop detection.)
7. Post-process       – check residual constraints for `# require-resolved`;
                        prepare hint stream; gather designator assignments.
8. Emit               – write:
                          * the flat netlist (every fully-resolved net),
                          * the residual constraint system (any variables not
                            uniquely bound; empty for FULL solves),
                          * the hint stream (typed design intent),
                          * the explain trace (every solver decision linked to
                            the chain of contributing constraints).
                        See §7.5 for the hint stream and §13 for the residual
                        format.
```

Passes 1–3a, 7, and 8 cannot fail with "no valid assignment" errors;
they fail only with parse errors, name-resolution errors, type
errors, and merge conflicts. The constraint pipeline (5–6a) is the
only place a board can be reported UNSAT or PARTIAL.

A `--require-resolved` flag forces an UNSAT exit when the solver
returns PARTIAL — production builds before fab use this. A
`--explain <subject>` flag retrieves a chain of contributing
constraints for any solver decision (a wrapper choice, a voltage
binding, a pin pick, an MPN selection); see §14 for the explain
contract.

### 11.1 Solver substrate

The solver consumes a finite-domain CSP plus linear arithmetic over
rationals plus weighted soft constraints. The full substrate
(variables, constraint forms, accepted aggregations) is specified
in §12 below. Implementation is intentionally underspecified; any
solver capable of FD + linear-rational + MaxSMT-of-soft-equality
suffices. Reference implementations may use OR-Tools (CP-SAT),
MiniZinc with a backend of choice, or a hand-rolled CSP for
small problems.

### 11.2 Determinism

Two invocations of the elaborator on the same source must produce
the same output. Determinism is achieved by:

* **Stable iteration order** in extraction (depth-first traversal
  of the elaborated tree).
* **Total ordering** on constraints (extraction order + source
  location).
* **Deterministic tie-break** in the solver — when multiple
  maximum-weight assignments exist, the solver picks the one
  lexicographically smallest under a documented variable order
  (typically declaration order).
* **Reproducible residuals** — the residual format is canonicalized
  (variables in extraction order; constraints sorted by source
  location).

Determinism is non-negotiable: identical source ⇒ identical
designators ⇒ identical netlist ⇒ identical residual.

---

## 12. The constraint substrate

The substrate is the formal target of constraint extraction (§10
pass 5) and the input the solver consumes. It is intentionally
small.

### 12.1 Variables

A variable has a *kind*, a *domain*, and a *source location*.

| Kind         | Domain shape                                       | Examples                               |
|--------------|----------------------------------------------------|----------------------------------------|
| `enum`       | finite set of named values                         | wrapper-choice variable; bank choice  |
| `fd_int`     | finite set of integers                             | pin index in a swap group              |
| `fd_pin`     | finite set of pins (typed)                         | `<I2C>.SDA` allocation from a pool     |
| `fd_quantity`| finite set of unit-bearing values                  | `Vio in {1.8 V, 2.5 V, 3.3 V}`         |
| `range_q`    | continuous interval over rationals with a unit     | `R_pu in [1 k, 100 k]`               |
| `bool`       | `{true, false}`                                    | "is wrapper materialized for bus B"     |

`fd_quantity` and `range_q` are *unit-typed*: a `voltage` variable
cannot equal a `current` variable except via dimensional analysis
that produces a third unit. Unit algebra is part of the substrate.

A variable's domain may be narrowed by hard constraints during
solving (domain propagation). It is never widened.

### 12.2 Hard constraints

The hard-constraint vocabulary the substrate accepts:

| Form                          | Meaning                                                                      |
|-------------------------------|------------------------------------------------------------------------------|
| `eq(x, y)`                    | `x == y` for compatibly-typed `x, y`                                         |
| `neq(x, y)`                   | `x != y`                                                                     |
| `le(x, y)` / `lt(x, y)`       | `x <= y` / `x < y` (only for ordered domains)                                |
| `ge(x, y)` / `gt(x, y)`       | `x >= y` / `x > y`                                                           |
| `in_set(x, S)`                | `x ∈ S` for a finite literal set `S`                                         |
| `linear(c1·x1 + … + cn·xn ⊙ k)` | linear (in)equality with rational coefficients; `⊙` is `==`/`<=`/`<`/`>=`/`>` |
| `alldiff(x1, …, xn)`          | distinct values across a list (for pin-pool draws and similar)               |
| `sum(set, expr) ⊙ k`          | sum of `expr` over a comprehension, ordered ⊙ a constant or variable         |
| `count(set, predicate) ⊙ k`   | count of elements satisfying a predicate                                     |
| `implies(p, q)`               | logical implication; `p` must be a finite-domain literal predicate           |

Refinement predicates desugar into combinations of these. Nonlinear
arithmetic (`R · C`, `R^2 / P`) is **not** in the substrate; it is
reduced to a numeric check once its variables are concrete and
emitted as a *deferred numeric obligation* in the residual otherwise.

### 12.3 Soft constraints

A soft constraint is a hard-constraint form annotated with a
non-negative integer **weight**:

```
soft eq(bus.wrapper, <i2c_link>) weight 10
```

The solver maximizes the sum of satisfied soft weights subject to
all hard constraints. Convention:

| Source                                     | Default weight |
|--------------------------------------------|----------------|
| `default` case in a `resolution` block     | 1              |
| `resolution` block, non-default case       | as declared    |
| Per-port `prefer wrapper = …`              | 5              |
| Lexical scope `prefer X = Y`               | encoded as *hard* (priority above soft) |
| Connect-site `prefer X = Y`                | encoded as *hard* (priority above lexical) |

Lexical and connect-site `prefer` rules are *hard*, not soft, and
priority among them is established by lexical nesting (innermost
wins; ties at the same scope are an error per §9.1). Per-port
preferences and resolution rules are soft; they only matter when
no hard binding applies.

### 12.4 What's not in the substrate

Deliberately out of scope:

* **Nonlinear arithmetic over reals.** `R · C < 1 / (3 · f_max)` is
  a deferred numeric obligation, not a substrate-level constraint.
  Once `R`, `C`, and `f_max` are concrete (which they typically are
  by the end of solve), the obligation becomes a numeric check.
* **Quantifier alternation.** `forall x. exists y. P(x, y)` is not
  representable. Aggregations (`sum`, `count`, `alldiff`) are
  bounded over compile-time-known sets, so they expand to flat
  conjunctions.
* **Unbounded data structures.** Lists, maps, recursive types are
  source-language conveniences; they expand to flat constraints
  before reaching the substrate.
* **Probabilistic reasoning.** Tolerance bands are interval
  constraints, not distributions. Yield analysis is downstream.

### 12.5 Tractability

Worked instances of the substrate on real boards are tractable for
the same reason MiniZinc-style FD solvers are tractable on
realistic CSPs: domains are small (≤ tens of values per
variable), constraints are mostly local (per-bus, per-bank, per
component), linear arithmetic is bounded by the number of pins and
parameters. A reference implementation that times out on a board
should be improvable to acceptable performance with standard CP
techniques (variable ordering, constraint propagation,
restart heuristics).

---

## 13. Residual constraint systems

When the solver returns PARTIAL (some variables remain unbound after
hard-constraint discharge and soft-weight maximization), the
elaborator emits a *residual constraint system* alongside the
netlist. The residual is the contract by which downstream tools
(placement, routing, BOM resolution, fab) finish constraining the
design.

### 13.1 What goes into the residual

Three things can land in the residual:

1. **Unbound variables** with their narrowed domains. A bank Vio
   that started at `{1.8 V, 2.5 V, 3.3 V}` and was narrowed to
   `{1.8 V, 3.3 V}` by hard constraints, but not further narrowed
   by soft preferences, is in the residual with that domain.
2. **Deferred numeric obligations**. A nonlinear refinement like
   `R · C < 1 / (3 · f_max)` whose terms were not all bound goes
   to the residual as a numeric predicate to be checked once
   placement (or the board author) constrains the missing terms.
3. **Soft constraints with non-zero residual weight** — i.e. soft
   preferences the solver couldn't satisfy because of a hard
   conflict. These are emitted as warnings ("preferred X but had
   to pick Y; reasons: …") rather than as a residual to solve.

### 13.2 Residual format

The residual is on-disk in a neutral CSP exchange format. The
canonical choice is SMT-LIB 2 with FD theory, optionally with a
sidecar JSON manifest that maps variables back to source paths.
Implementations may also emit MiniZinc or FlatZinc; the contract
with downstream tools is "any solver that can solve our hard
constraints can finish our design."

```
;; design.residual.smt2
;; Generated by schemc; do not hand-edit.
(declare-fun fpga.bank_14.Vio () Real)
(assert (or (= fpga.bank_14.Vio 1.8) (= fpga.bank_14.Vio 3.3)))

(declare-fun mcu.spi1.bank () Int)
(assert (in mcu.spi1.bank #{0 1}))   ;; A or E
;; ... etc.
```

```
;; design.residual.manifest.json
{
  "schemlang_version": "0.1",
  "source": "boards/eval_board.schemlang",
  "variables": [
    { "name": "fpga.bank_14.Vio",
      "kind": "fd_quantity",
      "unit": "V",
      "source": "examples/parts/xilinx_xc7a35t.schemlang:113" },
    ...
  ],
  "deferred": [
    { "predicate": "(< (* R_pu C_bus) (/ 1.0 (* 3.0 f_max)))",
      "depends_on": ["i2c_link_3.R_pu", "i2c_link_3.C_bus", "i2c_link_3.f_max"],
      "source": "examples/std/i2c.schemlang:55" }
  ]
}
```

### 13.3 Round-tripping

Downstream tools that bind residual variables (e.g. placement
chooses `bank_14.Vio = 3.3 V`) emit a *resolution file*: a
`.schemlang` block that posts the additional constraints. Re-running
schemc with the resolution file included produces the same netlist
plus a smaller residual (or none).

```
;; placement.resolutions.schemlang
include "boards/eval_board.schemlang"

constraint fpga.bank_14.Vio == 3.3 V    # bound by placement
constraint mcu.spi1.bank   == E         # bound by placement
```

This is the same shape Yosys/nextpnr use: synthesis emits a netlist
plus open constraints, place-and-route binds the constraints, and
the binding is auditable as source.

---

## 14. Explain mode

Every solver decision must be explainable. The elaborator emits an
*explain trace* alongside the netlist; an `--explain <subject>`
mode retrieves the chain of constraints that pinned a particular
variable.

### 14.1 Explain subjects

`--explain` accepts one of:

| Subject form                | Returns                                                                    |
|-----------------------------|----------------------------------------------------------------------------|
| `wrapper <bus_path>`        | The wrapper picked for that bus instance, with all contributing `prefer`s, resolution-block firings, per-port preferences, and weight totals. |
| `voltage <bank_path>`       | The voltage variable's binding, with the chain of hard equalities (derives, bus-Vio agreements) that forced it. |
| `pin <component>.<pin>`     | The lane (or function) bound to a pin, with the swap/bank choice and the soft preferences that broke ties. |
| `mpn <abstract_path>`       | Why an abstract resolved to a particular MPN, with `prefer` lineage. |
| `unsat`                     | The minimal unsatisfiable core, each constraint cited to source. |

### 14.2 Trace format

The explain trace is JSON Lines (one record per solver decision),
co-emitted with the netlist:

```
{"decision_id": "wrapper.bus_3", "kind": "wrapper_choice", "value": "<i2c_link>",
 "contributors": [
   {"source": "boards/eval_board.schemlang:24", "kind": "scope_prefer",
    "rule": "prefer <I2C> = <i2c_link>", "priority": "hard:scope"},
   {"source": "examples/parts/some_imu.schemlang:42", "kind": "port_prefer",
    "rule": "prefer wrapper = <i2c_link>", "weight": 5},
   ...
 ],
 "alternatives_considered": ["<I2C>", "<i2c_link_pull4k7>"]}
```

A decision-traversal tool can render this as a tree, an
explanation page, or feed it to an NLP-augmented review bot.

### 14.3 UNSAT cores

When the solver returns UNSAT, the elaborator emits a *minimal
unsatisfiable core*: a smallest subset of hard constraints whose
conjunction is unsatisfiable. Each constraint in the core is cited
to source. The user-visible report shows the core in a
human-readable form:

```
ERROR: design is overconstrained.

Minimal unsatisfiable core (3 constraints):
  1. derive fpga.i2c.imu.Vio = bank(SDA).Vio
       at examples/std/i2c.schemlang:25
       binds: fpga.i2c.imu.Vio = fpga.bank_14.Vio
  2. derive fpga.i2c.imu.Vio = imu.vdd_io
       at examples/parts/imu.schemlang:18
       binds: fpga.i2c.imu.Vio = imu.vdd_io = 1.8 V
  3. constraint fpga.bank_14.Vio in {3.3 V}
       at boards/eval_board.schemlang:67
       (added by another I2C bus that landed on bank_14)

Resolution: pick a different bank for fpga.i2c.imu, or insert a
level-shifter wrapper, or relax one of the participating Vios.
```

### 14.4 What happens to soft preferences that lost

When the solver picks a soft-weight assignment, the *un*satisfied
soft preferences are emitted as warnings (info-level for
single-vote losses, warning-level for losses by ≥ 2 weight units).
These appear in the build report as "preferred X but bound Y;
contributors: …" — not actionable errors, but visible.

---

## 15. Worked semantic example

The README's `winbond.SchLang` example, in this model, denotes:

* a `Component` named `W25N512GVEIG` with eight logical pins and one
  package binding `WSON8`;
* one `<POWER>` view named (implicitly) `pwr`, in role
  `peripheral`, with `pwr.POSITIVE = VCC`, `pwr.NEGATIVE = GND`;
* one `<QSPI>` view in role `peripheral`, with `CS_N`, `CLK`,
  `IO0..IO3` mapped to the corresponding pins;
* parameters `Tr = -40 °C .. 85 °C`, and on pin 8: `Vmin = 2.7 V`,
  `Vmax = 3.6 V`;
* a derived constraint `net(pin8).V ∈ [2.7 V, 3.6 V]`.

When `main.schemlang` writes `boot_flash.<SPI> <-> fpga.<SPI>.boot`,
elaboration:

1. resolves `boot_flash` to the `<QSPI>` view,
2. projects it onto `<SPI>` lanes (SPI is a sub-bundle of QSPI),
3. forms a bus instance `fpga.<SPI>.boot` with two participants
   tagged `host` and `peripheral`;
4. registers the bus instance's wrapper-choice variable; if
   `prefer <SPI> = <wrapper>` is in scope it becomes a hard
   binding, otherwise it carries soft constraints from
   resolution rules and per-port preferences;
5. allocates pins from the FPGA's `provides <SPI>` pool subject
   to `provides`'s where-clauses (lanes share a bank, bank Vio
   in the supported set);
6. checks role cardinality (1 host + 1 peripheral satisfies
   `<SPI>`'s default `1 + 1`);
7. propagates the `Vmin/Vmax` constraint onto the FPGA bank's
   `Vio` variable (a finite-domain variable over
   `{1.8 V, 2.5 V, 3.3 V}`);
8. enqueues an unsatisfied obligation if no `<POWER>` rail has yet
   been wired to `boot_flash.pwr`.

That last point is the principle in action: an unconnected required
power port is a *type error*, surfaced at compile time.
