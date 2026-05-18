# Schemlang — language design (draft v0.2)

Schemlang is a textual DSL for electrical schematics, designed around
one thesis:

> **A schematic is a typed hypergraph constrained by physical reality.
> The job of the language is to let humans record that hypergraph and
> those constraints compactly, and the job of the elaborator is to
> solve the resulting constraint system.**

This `docs/` tree is a self-contained design proposal. It keeps the
bracket-shape mnemonic from the original sketch (`{Component}`,
`[Package]`, `<Circuit>`) — collapsing the original `(Interface)` and
`<Module>` kinds into one **Circuit** kind — and grounds everything
else in a small set of well-understood semantic principles plus a
finite-domain constraint-satisfaction substrate.

## Reading order

1. **[`01-design-principles.md`](01-design-principles.md)** —
   *Read this first.* Optimization targets, the seven informing
   methodologies (typed hypergraphs, ML modules, HM unification,
   refinement types, linear resources, finite-domain CSP, natural
   language as a typed channel), prior-art comparison, and
   non-negotiables. §1.1 introduces the constraint substrate at a
   glance; principles 10–13 in §5 enshrine the elaborator-as-solver
   model.

2. **[`02-semantic-model.md`](02-semantic-model.md)** — The seven
   kinds of named things in the language (three structural, three
   data, one intent) and what they *mean*. The Circuit kind (§3)
   subsumes "interface" and "module" of older drafts; §3.1.1
   introduces `derive` for port-published equality constraints;
   §3.4 covers bus-instance formation (multi-drop, N-ary,
   wrapper-per-bus); §3.5 covers wrapper resolution with `prefer`,
   per-port preferences, and the `resolution` block. §2.6 covers
   capabilities and pin pools (FPGAs and flexible-I/O MCUs).
   §5.1 introduces *interval-valued* parameters (tolerances) with
   first-class symmetric / asymmetric / one-sided / mixed forms,
   and §6.1 specifies the matching containment semantics for `in`
   over interval operands — the rule that catches "3.3 V +/- 3 %
   rail into a chip with Vmax = 3.3 V" at compile time. §11
   specifies the elaboration pipeline as parse → elaborate →
   extract CSP → solve → emit. §12 specifies the CSP substrate.
   §13 specifies the residual constraint system. §14 specifies the
   `--explain` modes and UNSAT cores.

3. **[`03-syntax.md`](03-syntax.md)** — Concrete syntax, grammar, and
   a section-by-section diff against the original sketch. §2.2
   covers multi-parent inheritance, `override`, and `remove`;
   §3.4 covers `provides` (soft-peripheral capabilities); §4 covers
   the unified Circuit kind, including N-ary roles with
   `cardinality`, `derive` rules, `prefer wrapper` per-port
   preferences, and `resolution` blocks; §6.4 covers the
   connect-site argument block (parameter overrides, wrapper
   choices, pin pinning); §7 covers parameters as constraint
   variables and soft constraints; §11 covers `prefer` for
   abstract-to-concrete binding (components and circuits); §13
   covers `hint` declarations.

4. **[`04-cookbook.md`](04-cookbook.md)** — Sixteen worked recipes
   for the use cases that motivated the project, including
   constraint-driven scenarios: pin swap, peripheral bank
   assignment, derived buses, Thevenin termination, eval-board
   variants, refinement-checked rail windows, current budgets,
   procedural arrays, net labelling, custom DRC, project-wide
   vendor preferences via `prefer`, typed-intent hints, soft
   peripherals on FPGAs via `provides`, conditional SPI source
   termination via `splice` and a `prefer`-bound wrapper, I2C
   pull-ups as a multi-drop protocol-required wrapper with
   per-port preferences, and bank voltages as finite-domain
   constraint variables (Recipe 16).

5. **[`05-naming-conventions.md`](05-naming-conventions.md)** — How
   to name everything: identifiers (including the two flavors of
   circuit naming — `<UPPER>` for protocol contracts,
   `<snake_case>` for implementation circuits), descriptions
   (per-category templates), footprints (IPC-7351B and disjoint
   extensions for through-hole, mechanical, off-board, and custom),
   the standard parameter vocabulary, reference designators
   (auto-numbering, overrides, lock files), and the catalog of
   standard hint kinds (§7).

6. **[`schemlang.ebnf`](schemlang.ebnf)** — The canonical formal
   grammar in EBNF, complete with lexical tokens, layout-sensitive
   `NEWLINE`/`INDENT`/`DEDENT` pseudo-tokens, and notes for
   implementers. Hand-implementable as LL(2). Treat
   `03-syntax.md` as the prose specification and this file as the
   machine-readable counterpart; the two should agree.

## Running examples

The [`../examples/`](../examples/) tree exercises every concept:

* `examples/std/power.schemlang` — `<POWER>` family with refinements
  (`<3V3>`, `<5V0>`, …) and N-ary roles (one source, many sinks).
* `examples/std/spi.schemlang` — `<SPI>`, `<QSPI>`, `<OCTOSPI>`,
  `<MULTI_TARGET_SPI>` (cardinality >= 1 on peripheral), plus the
  `<spi_with_termination>` wrapper that conditionally splices
  source-terminating resistors.
* `examples/std/i2c.schemlang` — `<I2C>` protocol with
  `cardinality >= 1` on peripheral, `derive` rules on both roles,
  a `resolution` block biasing conservative, and the
  `<i2c_link>` wrapper.
* `examples/std/uart.schemlang` — `<UART>` and `<UART_FC>`.
* `examples/std/passives.schemlang` — generic `{RESISTOR}` and
  `{CAPACITOR}` with package bindings, swap groups, and aliased
  catalog entries.
* `examples/parts/winbond_w25n.schemlang` — full datasheet capture
  for W25N512GVEIG, with `derive Vio = pwr.Vnom` on the SPI/QSPI
  views, plus a `<boot_flash_slot>` reusable wiring circuit.
* `examples/parts/stm32h7.schemlang` — STM32H743 with multi-bank
  peripherals, GPIO bank current budgets, and a power-block
  subcircuit. Bank Vio is constrained to follow `vdd_main.Vnom`.
* `examples/parts/xilinx_xc7a35t.schemlang` — Artix-7 FPGA in
  CSG324: per-bank Vio as finite-domain constraint variables, a
  shared digital-I/O pool, `provides` factories with `derive`
  rules that publish bank Vio and `vio_port` to bus instances.
* `examples/boards/eval_board.schemlang` — top-level board tying
  it all together, with project-root `prefer` rules, soft bank
  preferences, and connect-site argument blocks for wrapper
  parameters.
* `examples/boards/fpga_board.schemlang` — multi-drop I2C on
  FPGA, with bank Vios partially bound (some constrained, some
  left to the residual), per-bus connect-site overrides, and a
  scope-level `prefer <I2C> = <i2c_link>`.
* `examples/boards/custom_spi_link.schemlang` — a small deviation
  circuit that inherits `<boot_flash_slot>` and adds a stiffer
  pull-up, a clock fan-out, and a Thevenin termination on MOSI.

## What this draft is not yet

* **Not** a parser implementation. The grammar in `03-syntax.md` is
  designed to be implementable in a layout-preprocessor + LL(2)
  combinator parser, but no compiler exists.
* **Not** a layout layer. Pad geometry, placement, and routing
  *output* are declared out of scope of the language core; they
  belong in downstream tools. Layout *intent* (placement guidance,
  sheet partitioning, signal classification) **is** in scope and
  travels with the source as first-class typed hints — see
  Cookbook Recipe 12 and `05-naming-conventions.md` §7 for the
  catalog of standard kinds.
* **Not** a simulation language. SPICE-class behavioral models are
  not in scope; refinement-type predicates and aggregate constraints
  cover the static checks we care about.
* **Not** a full SMT solver. The constraint substrate is finite
  domain plus linear arithmetic plus weighted soft constraints —
  small enough to hand-implement, big enough to cover every
  documented use case. Nonlinear refinement predicates are
  evaluated numerically once their inputs are concrete and emitted
  as deferred obligations otherwise. See `02-semantic-model.md`
  §12.4.

## Open questions worth your attention

1. **Bus role mismatches**: should `<SPI> as host` ↔ `<SPI> as host`
   be a hard error, or warn-and-allow for "wire crossover" cables?
   Current proposal: hard error, requires explicit `crossover`
   adapter (or a `<MULTI_MASTER_SPI>` variant with appropriate
   cardinality).
2. **Net labels across circuits**: a `NET_VCC_3V3` in two circuits
   is currently *not* connected unless explicitly wired. Is that
   surprising for users coming from KiCad? (Cookbook Recipe 9 makes
   the case for keeping it strict; worth a usability discussion.)
3. **Pin-swap solver complexity**: the bank/swap assignment problem
   is NP-hard in general. Do we cap it (heuristic + user override)
   or ship a real CSP/SAT solver? Reference implementations should
   pick a backend (OR-Tools CP-SAT is the recommended starting
   point).
4. **Generator boundedness**: requiring all generators to be
   statically bounded is conservative. Do we ever want
   length-polymorphic generators (e.g. an N-channel array where N
   comes from a top-level parameter)? Likely yes; need to nail down
   the typing rule and how the bound interacts with constraint
   variables.
5. **Wrapper-loop detection**: a circuit-flavored `prefer` rule that
   binds `<C>` to a wrapper containing a direct `<-> ` between two
   `<C>`-typed sub-ports could in principle trigger infinite
   wrapping. The current proposal is to detect the cycle at
   elaboration and force the inner connection to use the abstract
   type via `prefer <C> = <C>` in the wrapper's body, but this
   needs a careful spec.
6. **Residual interchange format**: SMT-LIB 2 with FD theory plus a
   JSON manifest is the proposed default. MiniZinc and FlatZinc are
   listed as alternatives. Is one canonical form mandatory, or do
   we ship a `--residual-format` flag and let downstream tools
   declare what they consume?
7. **Soft-weight calibration**: default weights (5 for per-port
   preferences, 1 for resolution defaults) are educated guesses.
   Real boards may want to tune these; the current spec leaves
   weight conventions as documentation, not enforcement. If
   conflict patterns are common in practice, we may need a
   project-level "weight policy" block.

## Status

This is a draft. Nothing in it is final. Comments, counterproposals,
and "this won't work because…" are exactly what should follow.
