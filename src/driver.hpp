// driver.hpp -- a small, reusable façade over the preprocess/lex/layout/parse
// pipeline. Both the command-line driver and the LSP server (and the WASM
// bindings) call into this API rather than building the pipeline manually.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "diagnostics.hpp"
#include "tokens.hpp"

namespace schemlang {

struct CheckOutput {
    std::vector<Diagnostic> diagnostics;
    bool                    ok = false;
};

struct TokenizeOutput {
    std::vector<Token>      tokens;
    std::vector<Diagnostic> diagnostics;
    bool                    ok = false;
};

// Run the full pipeline (preprocess -> lex -> layout -> parse).
// `filename` is only used to label diagnostics.
CheckOutput    check_source(std::string_view filename, std::string_view source);

// Run only the lex/layout phases. Useful for `--tokens` and for editor
// integrations that want a token stream.
TokenizeOutput tokenize_source(std::string_view filename, std::string_view source);

} // namespace schemlang
