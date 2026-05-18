// layout.hpp -- inserts INDENT/DEDENT tokens into a raw token stream.
//
// The schemlang grammar uses Python-style block structure (see "Indentation"
// section in docs/schemlang.ebnf): two more spaces of indent than the
// enclosing block opens an INDENT block; matching dedents close them. Tabs
// are rejected (handled earlier, in preprocessor.cpp).

#pragma once

#include <vector>

#include "diagnostics.hpp"
#include "tokens.hpp"

namespace schemlang {

// In/Out: a vector of raw tokens from lexer.cpp. Returns a new vector with
// Indent/Dedent tokens inserted at the appropriate boundaries, and with the
// EndOfFile sentinel appended.
std::vector<Token> apply_layout(std::string_view filename,
                                std::vector<Token> raw,
                                DiagnosticBag& diag);

} // namespace schemlang
