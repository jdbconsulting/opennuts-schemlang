// layout.cpp -- Python-style indentation -> INDENT / DEDENT.
//
// Algorithm:
//   * Walk the raw token stream a line at a time. A "line" is the run of
//     tokens between (and not including) Newline tokens.
//   * If the line is empty (only whitespace, which here means no tokens
//     between two Newlines), keep the Newline but do nothing to the indent
//     stack. Multiple consecutive Newlines collapse into one (the parser
//     doesn't care about exact count).
//   * Otherwise, take the column of the *first* token on the line as that
//     line's indent.
//   * Compare with the top of the indent stack:
//       - equal:   no change.
//       - greater: emit one Indent, push the new column. (We don't enforce
//         "exactly +2 columns" here; the lexer/preprocessor have already
//         taken care of tab rejection, and many real files use a different
//         indent step per block.) The grammar says two spaces; we keep
//         that as documented in the EBNF and warn on deviation only if
//         strict mode is enabled (TODO).
//       - less:    emit Dedent tokens until the stack top equals the new
//         indent. If no exact match exists, that's a layout error.
//   * At EOF, emit Dedents until the stack has a single 0-indent frame,
//     then an EndOfFile sentinel.
//
// Newline tokens *between* lines are preserved (the grammar uses NEWLINE
// as a statement terminator). Newline tokens at the file head and around
// blank lines are kept; the parser treats them as no-ops.

#include "layout.hpp"

#include <cstdio>

namespace schemlang {

std::vector<Token> apply_layout(std::string_view filename,
                                std::vector<Token> raw,
                                DiagnosticBag& diag) {
    std::vector<Token> out;
    out.reserve(raw.size() + 16);

    std::vector<std::uint32_t> indent_stack = {1}; // column 1 == base block
    int bracket_depth = 0;

    auto is_open = [](TokenKind k) {
        return k == TokenKind::LBrace   || k == TokenKind::LBracket ||
               k == TokenKind::LParen;
    };
    auto is_close = [](TokenKind k) {
        return k == TokenKind::RBrace   || k == TokenKind::RBracket ||
               k == TokenKind::RParen;
    };

    std::size_t i = 0;
    bool at_line_start = true;     // newline state

    while (i < raw.size()) {
        Token& tok = raw[i];

        if (tok.kind == TokenKind::Newline) {
            // Newlines inside (, [, or { are mere whitespace per the usual
            // Python convention. We drop them and don't re-evaluate indent.
            if (bracket_depth > 0) {
                ++i;
                continue;
            }
            out.push_back(std::move(tok));
            at_line_start = true;
            ++i;
            continue;
        }

        // Track bracket depth so the *next* Newline can be classified.
        if (is_open(tok.kind))   ++bracket_depth;
        else if (is_close(tok.kind) && bracket_depth > 0) --bracket_depth;

        // We don't apply indentation logic to tokens inside brackets:
        // a multi-line bracketed list isn't a block; it's one logical line.
        if (at_line_start && bracket_depth >= 0 /* always true */) {
            std::uint32_t col = tok.pos.column;
            std::uint32_t top = indent_stack.back();
            if (col > top) {
                out.push_back({TokenKind::Indent, "<INDENT>", tok.pos});
                indent_stack.push_back(col);
            } else if (col < top) {
                while (!indent_stack.empty() && indent_stack.back() > col) {
                    out.push_back({TokenKind::Dedent, "<DEDENT>", tok.pos});
                    indent_stack.pop_back();
                }
                if (indent_stack.empty() || indent_stack.back() != col) {
                    diag.error(std::string(filename), tok.pos,
                               "inconsistent indentation: no enclosing block "
                               "starts at this column");
                    indent_stack.push_back(col);
                }
            }
            at_line_start = false;
        }
        out.push_back(std::move(tok));
        ++i;
    }

    // Close remaining open blocks.
    SourcePos eof_pos{};
    if (!out.empty()) {
        eof_pos = out.back().pos;
        eof_pos.offset += static_cast<std::uint32_t>(out.back().text.size());
        eof_pos.column += static_cast<std::uint32_t>(out.back().text.size());
    }
    while (indent_stack.size() > 1) {
        out.push_back({TokenKind::Dedent, "<DEDENT>", eof_pos});
        indent_stack.pop_back();
    }
    out.push_back({TokenKind::EndOfFile, "<EOF>", eof_pos});
    return out;
}

} // namespace schemlang
