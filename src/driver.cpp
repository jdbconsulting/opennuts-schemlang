#include "driver.hpp"

#include "layout.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "preprocessor.hpp"

namespace schemlang {

CheckOutput check_source(std::string_view filename, std::string_view source) {
    DiagnosticBag diag;
    std::string stripped = strip_comments(filename, source, diag);
    auto raw_tokens      = tokenize(filename, stripped, diag);
    auto tokens          = apply_layout(filename, std::move(raw_tokens), diag);
    parse_file(filename, tokens, diag);

    CheckOutput out;
    out.diagnostics = diag.items();
    out.ok          = !diag.has_errors();
    return out;
}

TokenizeOutput tokenize_source(std::string_view filename, std::string_view source) {
    DiagnosticBag diag;
    std::string stripped = strip_comments(filename, source, diag);
    auto raw_tokens      = tokenize(filename, stripped, diag);
    auto tokens          = apply_layout(filename, std::move(raw_tokens), diag);

    TokenizeOutput out;
    out.tokens      = std::move(tokens);
    out.diagnostics = diag.items();
    out.ok          = !diag.has_errors();
    return out;
}

} // namespace schemlang
