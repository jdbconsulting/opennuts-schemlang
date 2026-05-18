// parser.hpp -- recursive-descent parser implementing docs/schemlang.ebnf.
//
// The parser is *syntax only* for this first pass: it walks the EBNF, makes
// sure the token stream conforms, and records diagnostics. No AST is
// returned. Static checks (kind admissibility, name resolution, the
// constraint substrate) are out of scope here.
//
// Error-recovery strategy: on a syntax mistake we record a diagnostic and
// resynchronise to the next likely safe point — usually the next Newline
// at the current indent. This lets `--check` keep listing every error in
// one go rather than stopping after the first.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "diagnostics.hpp"
#include "tokens.hpp"

namespace schemlang {

// Parse a full file. Returns true if no errors were recorded.
bool parse_file(std::string_view filename,
                const std::vector<Token>& tokens,
                DiagnosticBag& diag);

} // namespace schemlang
