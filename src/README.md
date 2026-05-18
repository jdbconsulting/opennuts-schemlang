# Schemlang CLI

A C++ command-line parser for the Schemlang DSL. First pass: syntax
checking and tokenization, built on top of
[lexy](https://github.com/foonathan/lexy).

## Pipeline

The CLI runs the source through four phases, in order:

1. `preprocessor.cpp` — strips line comments (`# ...`) and nested block
   comments (`{- ... -}`), preserving offsets so diagnostics keep
   their line/column numbers. Tabs are rejected here.
2. `lexer.cpp` — turns the preprocessed text into a vector of raw
   tokens. Identifier and number recognition is driven via lexy
   productions (see `g_identifier`); operators, keywords and strings
   are matched by a thin driver loop wrapping the lexy match.
3. `layout.cpp` — inserts `INDENT`/`DEDENT`/`NEWLINE` tokens for
   Python-style block structure. Newlines inside `(...)`, `[...]` and
   `{...}` are suppressed, matching the standard "implicit line
   continuation inside brackets" convention.
4. `parser.cpp` — a recursive-descent parser implementing each
   production from `docs/schemlang.ebnf`. Errors are collected with
   precise locations and recovery to the next statement boundary.

## CLI surface

```text
schemlang --tokens FILE   print the tokenized form of FILE
schemlang --check  FILE   parse FILE and report syntax errors
schemlang --lsp           run as a stdio LSP server
```

`--check` exits 0 and prints `OK` when the file parses cleanly. On
syntax errors it prints diagnostics with caret markers and exits 1.

`--tokens` prints one line per token in the form

```text
KIND                LINE:COL   lexeme
```

with `<NEWLINE>`, `<INDENT>`, `<DEDENT>` and `<EOF>` shown for layout
tokens.

`--lsp` speaks JSON-RPC over stdin/stdout per the Language Server
Protocol (`Content-Length: N\r\n\r\n<body>`). Supported messages:

* `initialize` / `initialized` / `shutdown` / `exit`
* `textDocument/didOpen`, `didChange` (Full text sync), `didSave`,
  `didClose`
* `textDocument/publishDiagnostics` (server → client)

The matching VS Code extension lives under `vscode-extension/` and is a
thin `vscode-languageclient` shim that launches `schemlang --lsp`.

## Reusable driver API

Both the CLI and the LSP server (and the WASM build) call into
`driver.hpp`:

```cpp
schemlang::check_source(filename, source);     // returns diagnostics + ok
schemlang::tokenize_source(filename, source);  // returns tokens too
```

This keeps the four pipeline phases — preprocess, lex, layout, parse —
in one place.

## WASM target

When configured with `emcmake`, the build produces a JS+WASM module
that exposes the same `check` / `tokenize` calls to JavaScript via
Embind:

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
# → build-wasm/bin/schemlang_wasm.js  + .wasm
```

From Node or the browser:

```js
const createSchemlangModule = require("./schemlang_wasm.js");
createSchemlangModule().then(M => {
    const r = M.check("define <UART>\n  lane TX : digital\n", "uart.schemlang");
    console.log(r.ok, r.diagnostics);
});
```

## Notable deviations from the strict EBNF

The hand-written examples under `examples/` use a few constructs the
EBNF in `docs/schemlang.ebnf` doesn't admit. The parser accepts them
intentionally; they are commented in `parser.cpp`.

* **Sigil-bracketed names with digits or hyphens.** The EBNF says
  `IDENT` (letter-or-underscore lead) inside `{}`/`[]`/`<>`. Real
  code uses `<3V3>`, `[SOD-123]`, `{ERJ-PHF}`. We accept any run of
  identifier / integer / `-` tokens between the sigil brackets.

* **Comma-separated `lane` declarations.** EBNF: one identifier per
  `lane`. Examples: `lane SDA, SCL : digital`.

* **`derive` inside `bus`, `provides`, and `view` bodies.** Used
  routinely in the standard library; the EBNF only allows it in role
  bodies.

* **`alias` of a path** (`alias vio_port = vdd_main`,
  `alias MOSI = IO0`). The EBNF restricts `alias` to sigil-bracketed
  names.

* **Hint attribute as `IDENT = arg_value`.** The EBNF uses
  `IDENT arg_value`. Examples write both, so we accept the optional
  `=`.

* **Typed declaration `IDENT : <Type> as role`** as a body statement
  (e.g. `rail_3v3 : <3V3> as source` in `eval_board.schemlang`).

* **Keyword-shaped names in path positions.** E.g. `bank(SDA)`,
  `mcu.spi1.bank`. The parser accepts any keyword in path-segment and
  function-call-head positions because all schemlang keywords are
  lexically valid identifiers.

* **Generator ranges as full expressions.** EBNF: `range_lit | path |
  set`. Examples: `for i in 0 .. n_bypass - 1`. Both endpoints are
  parsed as expressions.
