# Design Principles

This document fixes the *why* before the *what*. Every later decision in
syntax and semantics is justified by appeal to one of these principles.
If a feature does not serve them, it does not belong in the core
language; it belongs in a library.

---

## 1. The thesis

> **A schematic is a typed hypergraph constrained by physical reality.
> The job of the language is to let humans record that hypergraph and
> those constraints compactly, and the job of the elaborator is to
> solve the resulting constraint system.**

* **Compact**: top-level intent fits on a screen. Repetition is removed
  by abstraction, not by file count.
* **Typed**: every pin, net, bus, parameter, and component has a type.
  Mismatches are caught at compile time, not in DRC, and certainly not
  on the bench.
* **Constraint-driven**: parameters with domains, derived attributes,
  pin pools, bank voltages, wrapper choices, and pin-swap freedoms are
  *constraint variables* in a single store, not values evaluated
  eagerly. The elaborator is a solver: it picks values that satisfy
  every hard constraint while maximizing the weight of soft
  preferences. Boards may legitimately compile to a *partially-solved*
  netlist plus a residual constraint system that downstream tools
  (placement, routing, BOM resolution) finish solving.
* **Predictable**: the same source elaborates to the same constraint
  system, and the solver's decisions are traceable — every wrapper
  choice, voltage assignment, and pin pick can be retrieved as a
  chain of source-located constraints. Re-elaboration produces
  identical netlists for fully-determined designs.

A schematic *is* the hypergraph. A design *is* the constraint system
that picks one. The language is just notation for both.

### 1.1 The constraint substrate at a glance

Underneath the surface syntax sits a small, well-understood
constraint-satisfaction substrate:

* **Variables** with finite-domain (`Vio ∈ {1.8 V, 2.5 V, 3.3 V}`),
  continuous-range (`R_pu ∈ [1 k, 100 k]`), or singleton (concrete
  literal) domains.
* **Hard constraints** — equalities, inequalities, finite-domain
  predicates, linear arithmetic over rationals — that *must* hold.
* **Soft constraints** with non-negative weights, contributing to a
  preference score the solver maximizes.
* **Underspecified outputs** — when constraints leave variables free,
  the netlist is emitted alongside a *residual constraint system*
  that downstream tools consume.

The substrate is intentionally *not* full SMT: no nonlinear
arithmetic over reals, no quantifier alternation. Finite domain plus
linear arithmetic plus soft constraints covers every documented use
case while staying solvable on real boards by off-the-shelf tooling
(MiniZinc, OR-Tools, hand-rolled CSP). See `02-semantic-model.md`
§12 for the formal description of the substrate and §11 for the
elaborator pipeline that consumes it.

---

## 2. Optimization targets, in priority order

These are the user-visible properties we trade other things to achieve.

1. **Readable top level.** A board file should read like a block
   diagram, not like wiring. Authors of the *board* should rarely touch
   pin numbers.
2. **Parameters earn their place.** A parameter belongs in the language
   only if the elaborator uses it to discharge a check or to produce
   the netlist. A part's `Vmin/Vmax` is in scope because we can prove
   the rail satisfies it. Ambient temperature, MTBF, propagation
   delay, lifecycle stage, and most of the rest of the datasheet are
   *out* of scope: nothing in the language tests them, so they would
   accumulate as decorative drift. Datasheets are reference material;
   the language is not a wiki.
3. **Vendor-deviation friendliness.** An eval board may deviate from
   a typical method for connecting a chip-to-chip bus. The language
   needs to support implementation deviations over a standard pattern
   in a compact and reviewable way.
4. **Composable abstractions.** Circuits (protocols, wrappers,
   subcircuits) and components compose and nest without privileged
   forms.
5. **Boring, learnable surface.** Ten productions of grammar should
   cover 95% of real boards. Power users reach for the same primitives
   as novices, just more of them.
6. **Static safety.** Type errors, unconnected required pins, ambiguous
   bus matings, and over-budget banks are detected before generation.
7. **Procedural power, on demand.** Generators are first-class but
   bounded — no Turing-complete elaboration, no runtime side effects.
8. **Intent travels with structure.** Design intent that resists
   formal encoding — "this is the analog front-end," "place this LDO
   close to its load," "treat this clock as critical and route it
   first," "render the power tree on its own sheet" — belongs *in
   the source*, not in PR comments, not in a sibling CAD project,
   not in a tribal-knowledge wiki. The language carries first-class
   **hints** alongside the netlist; downstream tools (typesetters,
   layout assistants, review bots, NLP-augmented agents) read them
   and act on them. A `git diff` of layout intent is the diff of a
   `.schemlang` file.
9. **Constraint-driven, not eager.** The language records
   constraints; the elaborator solves them. Bank voltages,
   wrapper choices, pin assignments, and parameter values that
   look like literals are constraint *variables* with declared
   domains. A check like "all I2C lanes share a Vio" doesn't
   compare two known values; it asserts an equality between two
   variables and lets the solver discover whether the design has a
   solution. Underconstrained inputs produce *partially-solved*
   outputs (netlist + residual CSP); overconstrained inputs
   produce a minimal UNSAT core, not a single-line error.
10. **Decisions are explainable.** Every solver decision — wrapper
    chosen, voltage picked, pin assigned — must be retrievable as
    a chain of source-located constraints. An `--explain` mode is
    a first-class deliverable, not a debug afterthought.

What we explicitly **do not** optimize for:

* Being a general-purpose programming language.
* *Producing* layout geometry. We describe schematics and the *intent*
  behind their layout; converting intent to placed-and-routed copper
  is a downstream tool's job.
* Round-tripping with every legacy EDA tool. We export, we don't pretend
  to be EDIF.

---

## 3. The seven informing methodologies

Each of these contributes one specific idea. Citing them is not
ornamental; it tells future contributors *where to read* when they need
to extend the design.

### 3.1 Typed hypergraphs (algebraic graph theory)

A schematic is a hypergraph: nets are hyperedges, pins are
half-edges attached to component nodes. Most schematic tools treat this
graph implicitly. We make it the official semantic object. Everything
else — circuits (protocols, wrappers, subcircuits) — is a structured
way of *building or naming pieces of* this graph.

Implication: `connect` is the only primitive that touches the graph.
Everything else is sugar.

### 3.2 Hindley–Milner unification (type inference, ML family)

When you write `mcu.spi1 <-> flash.spi`, the elaborator unifies two
records of nets field-by-field. When you write `vcc <-> u1.VDD`, it
unifies a net with a pin's net-equivalence class. The underlying
operation in both cases is the same: structural unification with
occurs-check.

Implication: the connect operator works at every abstraction level
without special cases. A pin is a 1-field record. A circuit is an
N-field record (its lanes). A circuit port is a record of records.
All compose.

### 3.3 ML-style modules / functors

A circuit is a *functor*: a parameterized template over its input
ports, parameters, and (for protocol circuits) lanes that, when
applied, produces a concrete sub-hypergraph. Functors compose;
functors can be passed around; functors are typed by their
signatures. A protocol circuit is a degenerate functor whose body is
empty; an implementation circuit is one whose body is non-empty; a
wrapper circuit is one whose body decorates a protocol it inherits.

Implication: circuits are not macros. They have signatures. They can
be called with different argument types, but only when the types
satisfy the declared signature. The same `<->` that unifies two pins
threads through a circuit's body when the circuit's body is non-empty
(see `02-semantic-model.md` §3.4 for `prefer`-driven wrapping).

### 3.4 Refinement types (Liquid Haskell, F\*)

A `<POWER>` is a base type. A `<3V3> : <POWER>` is the same base type
*refined* by the predicate `Vnom == 3.3`. A part's `VCC` pin requires
`<POWER> where Vmin <= V <= Vmax`. Connecting a `<3V3>` rail to that
pin discharges the predicate at compile time.

Implication: datasheet windows are not comments. They are predicates
that the elaborator can prove or disprove against the chosen rails.

### 3.5 Linear / affine resources

Each physical pin is connected to exactly one net. Each pin's current
budget is a finite resource consumed by loads. Each peripheral instance
on an MCU can be assigned to at most one bank.

Implication: we model "use it once" facts as linear obligations the
elaborator must discharge. Over-allocation (two SPI peripherals
claiming the same bank, three loads exceeding a pin's `I_max`) is a
type error, not a runtime warning.

### 3.6 Finite-domain constraint satisfaction (CP-style)

Several decisions a board makes are not facts but *choices over a
finite set*: which bank Vio (1.8 V / 2.5 V / 3.3 V), which wrapper
to insert (`<I2C>` vs. `<i2c_link>`), which alt-function row to
allocate, which pins from a pool to draw, which sibling MPN to bind
an abstract resistor to. Each is a variable over a small finite
domain, constrained by structural and refinement obligations from
the rest of the design.

Implication: the elaborator is a CSP solver. Hard constraints must
hold; soft constraints (per-port `prefer`, project-level `prefer`,
connect-site overrides) contribute to a weighted score the solver
maximizes. The substrate is finite-domain plus linear arithmetic
plus soft constraints — small enough to hand-implement, big enough
to cover every documented use case. We *do not* admit nonlinear
arithmetic or quantifier alternation; refinement predicates that
involve nonlinear math (`R · C < 1 / (3 · f_max)`) reduce to
numeric checks once their variables are concrete, and stay as
deferred obligations in the residual CSP otherwise.

### 3.7 Natural language as a typed channel

Modern natural-language processing has crossed a threshold: free-text
annotations can be reliably consumed by downstream tools as
*structured intent*, not just inert documentation. We exploit this by
giving **hints** the same treatment as components and circuits — they
have kinds, scopes, override semantics, and merge rules — while their
*payload* may be free natural language addressed to a (human or AI)
reader downstream.

```
hint placement  mcu       "place center; near USB; rotate 90°"
hint sheet      "Power"   reg, mcu_pwr, bulk_caps
hint signal_class critical mcu.spi1.CLK, mcu.spi1.MOSI
hint annotate   afe       "low-noise stage; keep guard ring intact"
```

A schematic-typesetting tool reads `placement` and `sheet` hints to
choose a 2D arrangement. A layout assistant — including LLM-driven
ones — reads `annotate` and `signal_class` payloads to make
constraint-aware placement and routing decisions. The hint is *typed*
(`placement`, `sheet`, `signal_class`, …) so the consumer can
dispatch; the payload is free text so the human can write what they
mean.

Implication: the language commits to being the source of truth for
*intent*, not just topology. Where prior tools relegated layout
guidance to GUI annotations or sidecar files, we treat it as
first-class source. Whatever downstream renderer or layout engine
reads a `.schemlang` file should produce *better* output the more
hints are present, not the same output regardless of them.

This methodology is forward-looking: it presumes a downstream
ecosystem (typesetter, layout-aware AI assistant, design-review bot)
that will be built around the language. We bake hints into the core
so that ecosystem has something to read.

---

## 4. Prior art, honestly assessed

We are not the first to attempt this. We borrow what works.

| System                              | What we take                                  | What we leave |
|-------------------------------------|-----------------------------------------------|---------------|
| **VHDL / SystemVerilog (modports)** | Directional ports; named bus contracts        | Simulation semantics, process model |
| **Chisel / SpinalHDL**              | Generator-as-program; bundle types            | Host-language coupling (Scala) |
| **SKiDL**                           | Pythonic ergonomics; net unification          | No type system; full-language complexity |
| **Atopile (`ato`)**                 | First-class interfaces and modules in a DSL   | Limited refinement / constraint expressiveness; we collapse interfaces and modules into one Circuit kind |
| **JITX**                            | Formal pin types, package-pin separation, parametric circuits | Lisp-y surface, closed ecosystem |
| **EDIF / SPICE subckt**             | Flat netlist as elaboration target            | As a *source* language |
| **MiniZinc / OR-Tools / SAT**       | CSP / FD substrate; soft constraints; UNSAT cores; explain modes | We don't expose the solver as syntax; we record constraints and dispatch to the solver |
| **Yosys / nextpnr**                 | Two-phase compile (synthesis → place-and-route) where each phase has a defined contract with the next | We aim at a comparable contract: schemlang → (netlist, residual CSP) → placement |
| **R7RS Scheme, Lua**                | Small core, rich library philosophy           | Dynamic typing |

Two prior systems — Atopile and JITX — already prove that a small DSL
with interfaces, modules, and parameters can express real boards. We
diverge from both by:

* committing harder to **refinement types** (§3.4) so the
  *language-checked* parameters of a part — voltage windows, pin
  current budgets — are part of the type system rather than
  afterthought DRCs, while the rest of the datasheet stays in the
  datasheet;
* committing harder to **unification at every level** (§3.2) so the
  same `connect` works on pins, nets, buses, and module ports; and
* committing harder to **a small core, large library** stance, so the
  surface language never grows new keywords just to express a new
  family of parts; and
* committing to **first-class hints** (§3.7) as the typed channel
  between source and downstream typesetters, layout engines, and
  NLP-aware assistants — a category of intent that prior systems
  banished to comments or GUI annotations; and
* committing to a **constraint-recording surface with a CSP-solver
  backend** (§3.6, non-negotiables §5.10–§5.12), so that bank
  voltages, wrapper choices, pin assignments, and parameter values
  are *variables* the solver picks rather than literals the user
  must guess. Underconstrained inputs produce partial solutions
  with a residual constraint system; overconstrained inputs produce
  minimal UNSAT cores. The "two-phase compile" contract is the
  same shape as Yosys → nextpnr.

---

## 5. Non-negotiables

These are the hills on which we will die. Reviewers should reject any
proposal that violates them.

1. **No magic.** If a board file produces a net, you can point to the
   line that produced it. No implicit globals. No autoconnect by name.
2. **Names are scoped.** Every identifier resolves in a lexical scope
   you can see. `VCC` in `<flash>` is not `VCC` in `<mcu>` unless
   you wired them.
3. **Unification, not assignment.** `<->` is symmetric. There is no
   driver/driven distinction in connection (direction is *type-level*
   metadata on bus roles, not an evaluation order).
4. **Nothing in the core that can live in a library.** Power types,
   bus types, common DRC predicates, footprint shapes — all library.
   The core has: `define`, `include`, `connect`, parameter, constraint,
   generator.
5. **Source is the source of truth.** No GUI state, no hidden
   annotations. A `git diff` of a `.schemlang` file is the diff of the
   schematic.
6. **One file format.** `.schemlang` is the only file the language
   produces or consumes. Designator locks, project preferences,
   library indexes, package geometry — all of it is `.schemlang`.
   "Everything shall be `.schemlang`, and `.schemlang` shall be
   everything."
7. **Parameters earn their place.** Every parameter declared in a
   library or part definition must participate in a check, a
   constraint, or netlist emission. Decorative parameters (datasheet
   trivia, lifecycle metadata, vendor URLs) belong outside the
   language. *Extra component parameters are sometimes useful and
   usually wrong.*
8. **Hints are first-class.** Annotations that drive typesetting,
   layout, sheet partitioning, signal-class designation, or other
   downstream-tool behavior are declared in source, with the same
   lexical scoping, override semantics, and merge rules as
   components and circuits. They are the machine-readable channel
   between the schematic source and any downstream renderer or
   layout assistant. Never put them in a comment, a side-file, or
   GUI state.
9. **One kind for typed graph fragments.** What other languages split
   into "interface" (lanes + roles, no body) and "module" (ports +
   body) is one kind here: **Circuit**. The split is a phase
   distinction, not a kind distinction — a wrapper that adds
   pull-ups or conditional termination is *both* a contract and an
   implementation. Folding them lets `prefer <SPI> = <wrapper>` and
   `prefer <I2C> = <i2c_link>` work as one-line project policies,
   not bespoke language features.
10. **The elaborator is a solver.** Schemlang records constraints;
    it does not evaluate them eagerly. A line that reads
    `parameter Vio : voltage in {1.8 V, 2.5 V, 3.3 V}` declares a
    *variable*, not a fact; an equality `derive vref = bank.vio`
    posts a constraint, it doesn't fetch a value. The elaboration
    pipeline (`02-semantic-model.md` §11) extracts a CSP from the
    program and dispatches to a solver. Every change to the
    surface syntax must answer the question "what variables and
    constraints does this contribute?"
11. **Underconstrained outputs are a valid answer.** A schemlang
    compile produces a *(netlist, residual CSP)* pair. The
    fully-resolved case (residual is empty) is the cleanup
    result, not the only valid result. A board file may
    intentionally leave bank voltages or pin assignments to
    placement, and the residual format is the contract by which
    placement (or any downstream tool) finishes the design. A
    `--require-resolved` flag asks the elaborator to fail if the
    residual is non-empty — production builds before fab use it.
12. **Decisions are explainable, with sources.** Every solver
    decision must be retrievable as a chain of source-located
    constraints, viewable via an `--explain <subject>` mode.
    Wrapper choice, voltage assignment, pin pick, MPN binding —
    if the elaborator picked it, the elaborator must be able to
    show *why*. UNSAT failures emit minimal cores, not the first
    contradiction the solver tripped over. This is non-negotiable
    because constraint-driven systems become opaque the moment
    explanation is treated as optional.
13. **No pretend precision.** If the language cannot
    authoritatively know a fact, it does not encode it as a hard
    constraint. A maximum count of soft I2C controllers on an
    FPGA is *not* in the language because the FPGA toolchain
    actually knows that number; the language reports the
    pin-pool exhaustion it can prove and stops. Same for thermal
    derating, MTBF, MSL, and any other parameter that depends on
    a physical model the elaborator does not run.

---

## 6. Anti-principles (things we are choosing *against*)

Stated explicitly so we can refuse them with a citation.

* **Net naming as connection.** In some tools, two pins acquire the
  same `VCC` netname and are thereby connected. We do not do that.
  Connection is `<->`. Names are labels, not edges.
* **Schematic as drawing.** A schematic is a graph, not a 2D picture.
  Pixel coordinates and rendered geometry are *output*, never source.
  Declarative layout *intent* — "near the load," "on the analog
  sheet," "high-speed group" — *is* source, expressed via hints
  (§3.7, non-negotiable §5.8). The language carries intent; rendering
  tools convert intent into geometry. We never check absolute
  coordinates into a `.schemlang` file.
* **Hints as comments.** A `#` comment is invisible to tooling. A
  hint is structured, typed, and addressable. If you want a
  downstream tool to act on what you wrote, it must be a hint, not a
  comment.
* **String-typed parameters.** `parameter Vmin "2.7V"` is a bug
  factory. Parameters have *types with units*.
* **Implicit pin numbers.** A pin number is a property of the *package*,
  not the part. The same die in two packages must not require two part
  definitions; it requires two package bindings.
* **Forking to customize.** If a vendor eval board needs a stiffer
  pull-up, the answer is a 3-line override module that *includes* the
  reference, not a copied-and-edited file.
* **Datasheet-as-source-code.** Every parameter on a part is a
  liability: it must be maintained, kept in sync with the vendor PDF,
  and reviewed in every diff. We refuse the temptation to encode
  every datasheet field into a `.schemlang` file. If a value is not
  referenced by an elaborator check, it does not belong here.
* **Side-files for what could be source.** No `.lock`, `.bom`,
  `.config`, `.preferences`. If the tool needs to remember it, it is a
  block in a `.schemlang` file. (One nuance: the *residual constraint
  system* a partially-solved compile emits is an output artifact, not
  source — it is consumed by downstream tools and need not round-trip.
  Like the netlist, it is regenerated on every build.)
* **Eager evaluation of constrained values.** A `parameter Vio :
  voltage` whose domain is `{1.8 V, 2.5 V, 3.3 V}` is *not* a value
  to fetch and compare against; it is a variable to constrain. Code
  that reads "if `vio == 3.3 V`" inside a generator is suspect — the
  variable may not be bound yet — and the elaborator rejects such
  expressions when they would inspect a value the solver has not
  yet picked. Use refinement constraints, not generator-time
  comparisons, to encode value-dependent structure.
* **Pretend precision.** Hard constraints encode facts the language
  can authoritatively check. A `max_count = 16` on a `provides
  <UART>` capability would lie about something the FPGA toolchain
  actually knows; we don't write it. If a fact is downstream of the
  language, it stays downstream. The residual constraint system is
  the honest channel for "we left this open on purpose."
* **Solver decisions buried in the binary.** Picking a wrapper or a
  voltage without an `--explain` trace is opaque magic. Every
  solver decision is retrievable; UNSAT failures emit minimal
  cores. Anything else is unreviewable.

---

## 7. How to read the rest of the docs

* [`02-semantic-model.md`](02-semantic-model.md) — what *exists* in the
  language: components, packages, circuits, nets, parameters,
  constraints, hints. Read this before syntax.
* [`03-syntax.md`](03-syntax.md) — concrete syntax, evolved from the
  README sketch, with rationale for each shape.
* [`04-cookbook.md`](04-cookbook.md) — recipes for pin swap, derived
  buses, splicing/termination, I2C pull-ups, eval-board variants,
  generators, and FPGA soft peripherals.
* [`../examples/`](../examples/) — worked examples that exercise
  every feature.
