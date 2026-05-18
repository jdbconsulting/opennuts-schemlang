// preprocessor.hpp -- comment stripping pass.
//
// Replaces comments with spaces so that the resulting buffer has *exactly*
// the same length as the original, letting us reuse offset->line/column
// arithmetic without bookkeeping. Newlines inside block comments are
// preserved.

#pragma once

#include <string>

#include "diagnostics.hpp"

namespace schemlang {

// Returns a transformed copy of `source` with line comments (`#...` to EOL)
// and nested block comments (`{- ... -}`) replaced by spaces. Tabs trigger
// an error (the language forbids them; see schemlang.ebnf "Indentation").
// On any reported error the function still returns a best-effort stripped
// buffer so downstream phases can continue gathering diagnostics.
std::string strip_comments(std::string_view filename,
                           std::string_view source,
                           DiagnosticBag& diag);

} // namespace schemlang
