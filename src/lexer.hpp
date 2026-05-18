// lexer.hpp -- text -> vector<Token>. Uses lexy (the C++ parsing DSL) for
// the actual character-level pattern matching. Layout (Indent/Dedent) is
// inserted separately by layout.cpp.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "diagnostics.hpp"
#include "tokens.hpp"

namespace schemlang {

// A "raw" token is what comes out of the lexy grammar; it differs from the
// final Token only in that no Indent/Dedent has been computed yet.
// Newline tokens are present after every logical end-of-line.
std::vector<Token> tokenize(std::string_view filename,
                            std::string_view stripped_source,
                            DiagnosticBag& diag);

} // namespace schemlang
