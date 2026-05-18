# Schemlang

A schematic description DSL — see [`docs/schemlang.ebnf`](docs/schemlang.ebnf)
for the formal grammar and [`docs/01-design-principles.md`](docs/01-design-principles.md)
for the underlying ideas. This repo ships:

- A C++ syntax checker and tokenizer (`schemlang`).
- A Language Server Protocol server bundled in the same binary
  (`schemlang --lsp`).
- A WebAssembly build of the same parser, suitable for use from
  JavaScript.
- A VS Code extension that wires the LSP server into the editor.

```
.schemlang ──▶ preprocess ──▶ lex ──▶ layout ──▶ parse ──▶ diagnostics
                                                              │
                                  CLI ◀──┬──────┬─────────────┤
                                  LSP ◀──┤      │
                                  WASM ◀─┘      │
                                  VS Code ◀─── LSP
```

The implementation details (pipeline phases, parser deviations from the
strict EBNF, CLI flags, etc.) live in [`src/README.md`](src/README.md).

## Layout

| Path                  | What's in it                                            |
|-----------------------|---------------------------------------------------------|
| `docs/`               | Language spec, design notes, cookbook.                  |
| `examples/`           | Real-world `.schemlang` files used as the test corpus.  |
| `src/`                | C++ sources for the CLI, LSP server and WASM bindings.  |
| `vscode-extension/`   | TypeScript scaffold for the VS Code language client.    |
| `references/`         | Background reading.                                     |

## Building the native CLI / LSP server

Requires CMake ≥ 3.24 and a C++20 compiler (anything newer is auto-detected).
Dependencies (`lexy`, `nlohmann/json`) are fetched by CMake — no manual setup.

```bash
cmake -S . -B build
cmake --build build -j
```

Outputs `build/bin/schemlang`. Usage:

```bash
build/bin/schemlang --tokens examples/std/uart.schemlang   # dump tokens
build/bin/schemlang --check  examples/std/uart.schemlang   # syntax check
build/bin/schemlang --lsp                                  # speak LSP on stdio
```

## Building the WASM target

Requires Emscripten. The user's emsdk lives at `~/emsdk`:

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j
```

Outputs `build-wasm/bin/schemlang_wasm.js` and `schemlang_wasm.wasm`. Use
from Node or the browser:

```js
const createSchemlangModule = require("./build-wasm/bin/schemlang_wasm.js");
createSchemlangModule().then(M => {
    const r = M.check("define <UART>\n  lane TX : digital\n", "uart.schemlang");
    console.log(r.ok, r.diagnostics);

    const t = M.tokenize("define <X>\n  port a : in\n", "x.schemlang");
    console.log(t.tokens);
});
```

Both calls return plain JS objects:

```ts
check(source, filename?) -> {
    ok: boolean,
    diagnostics: { severity, line, column, endLine, endColumn, message, file }[]
}

tokenize(source, filename?) -> {
    ok: boolean,
    tokens: { kind, text, line, column }[],
    diagnostics: ...
}
```

`severity` follows the LSP convention: `1 = Error`, `2 = Warning`,
`3 = Information`.

## Building the VS Code extension

```bash
cd vscode-extension
npm install
npm run compile
```

Then open `vscode-extension/` in VS Code and press <kbd>F5</kbd> to launch
an Extension Development Host on the `examples/` folder. Configure the
server path in settings if `schemlang` isn't on `PATH`:

```json
{ "schemlang.serverPath": "/absolute/path/to/build/bin/schemlang" }
```

More detail in [`vscode-extension/README.md`](vscode-extension/README.md).

## Quick sanity check

```bash
for f in examples/std/*.schemlang examples/parts/*.schemlang examples/boards/*.schemlang; do
    printf "%-50s " "$f"
    build/bin/schemlang --check "$f"
done
```

Every example in the tree should print `OK`.
