// lsp_server.hpp -- minimal Language Server Protocol server.
//
// Reads JSON-RPC 2.0 messages from stdin (framed by Content-Length
// headers per the LSP spec), runs the schemlang pipeline on each open
// document, and publishes diagnostics back to the client over stdout.
//
// Currently supported requests / notifications:
//   * initialize           (request)
//   * initialized          (notification)
//   * shutdown             (request)
//   * exit                 (notification)
//   * textDocument/didOpen
//   * textDocument/didChange   (Full text sync only)
//   * textDocument/didSave
//   * textDocument/didClose
//
// Outgoing notifications:
//   * textDocument/publishDiagnostics
//
// This is intentionally tiny so it builds with FetchContent without
// pulling in a full LSP framework. It's enough to drive the VS Code
// extension in `vscode-extension/`.

#pragma once

namespace schemlang {

// Runs the server until the client sends `exit` (or stdin closes).
// Returns the process exit code: 0 on a clean `shutdown`+`exit` pair,
// non-zero if `exit` arrives without a prior `shutdown`.
int run_lsp_server();

} // namespace schemlang
