# Schemlang for VS Code

Language support for [Schemlang](../docs/schemlang.ebnf) — syntax
highlighting plus live diagnostics from the C++ language server.

## Architecture

The extension is a tiny shim around `vscode-languageclient` that spawns the
native `schemlang` binary with `--lsp` and talks JSON-RPC to it over
stdin/stdout. All parsing and diagnostics come from the same C++ code that
powers the CLI; the editor only renders them.

```
.schemlang file ──▶ VS Code ──stdio──▶ schemlang --lsp ──▶ diagnostics
                       ▲                                       │
                       └────────── publishDiagnostics ─────────┘
```

## Building

```bash
# 1. Build the C++ server.
cd ..
cmake -S . -B build && cmake --build build
# Make sure the binary is on PATH or set "schemlang.serverPath" in settings.

# 2. Build the extension.
cd vscode-extension
npm install
npm run compile
```

## Trying it locally

1. Open the `vscode-extension/` folder in VS Code.
2. Press <kbd>F5</kbd> to launch an Extension Development Host.
3. Open any `.schemlang` file in the new window.
4. The status bar should say *Schemlang Language Server: Running*. Syntax
   errors appear in the Problems pane.

## Server binary resolution

`schemlang.serverPath` (default `"schemlang"`) is resolved in order:

1. If it's an absolute path, used as-is.
2. If it's a relative path, resolved against each open workspace folder.
3. If it's the default name, the extension looks for the freshly-built
   binary under each workspace folder at `build/bin/schemlang`,
   `build/schemlang`, `build-debug/bin/schemlang`,
   `build-release/bin/schemlang`, or `out/bin/schemlang`.
4. Finally, falls back to `$PATH`.

This means a typical `cmake -S . -B build && cmake --build build` is
enough — no setting required.

## Settings

| Setting                     | Default       | Description |
|-----------------------------|---------------|-------------|
| `schemlang.serverPath`      | `schemlang`   | Path (absolute, workspace-relative, or PATH-resolvable name) of the `schemlang` binary. |
| `schemlang.trace.server`    | `off`         | Set to `messages` or `verbose` to log JSON-RPC traffic to the output pane. |
