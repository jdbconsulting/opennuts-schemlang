#include "preprocessor.hpp"

namespace schemlang {

namespace {

struct Cursor {
    std::string_view src;
    std::size_t      i      = 0;
    std::uint32_t    line   = 1;
    std::uint32_t    column = 1;

    char peek(std::size_t off = 0) const {
        return (i + off < src.size()) ? src[i + off] : '\0';
    }
    bool match2(char a, char b) const {
        return peek(0) == a && peek(1) == b;
    }
    SourcePos pos() const {
        return {static_cast<std::uint32_t>(i), line, column};
    }
    void advance() {
        if (i >= src.size()) return;
        if (src[i] == '\n') { ++line; column = 1; }
        else                { ++column; }
        ++i;
    }
};

} // namespace

std::string strip_comments(std::string_view filename,
                           std::string_view source,
                           DiagnosticBag& diag) {
    std::string out;
    out.resize(source.size(), ' ');

    Cursor c{source};
    // Block-comment depth. `{- ... -}` may nest; '#' inside a block comment
    // has no special meaning.
    int  block_depth        = 0;
    SourcePos block_open_pos{};
    bool in_string          = false;
    bool string_escape      = false;
    SourcePos string_open_pos{};

    auto copy_here = [&]() {
        // Copy the current character through to the output buffer.
        out[c.i] = c.src[c.i];
    };

    while (c.i < c.src.size()) {
        char ch = c.peek();

        // ---------------- inside a string literal ----------------
        if (in_string) {
            copy_here();
            if (string_escape) {
                string_escape = false;
                c.advance();
                continue;
            }
            if (ch == '\\') {
                string_escape = true;
                c.advance();
                continue;
            }
            if (ch == '"') {
                in_string = false;
                c.advance();
                continue;
            }
            if (ch == '\n') {
                // The string is unterminated; leave the error reporting
                // to the lexer (which knows the exact character that
                // broke the literal). Exit string mode to recover.
                in_string = false;
                continue;          // re-process the '\n' as plain text
            }
            c.advance();
            continue;
        }

        // ---------------- inside a block comment ----------------
        if (block_depth > 0) {
            if (c.match2('{', '-')) {
                ++block_depth;
                c.advance(); c.advance();
                continue;
            }
            if (c.match2('-', '}')) {
                --block_depth;
                c.advance(); c.advance();
                continue;
            }
            if (ch == '\n') {
                // Preserve newlines so line numbers stay correct.
                out[c.i] = '\n';
            }
            c.advance();
            continue;
        }

        // ---------------- regular text ----------------
        if (ch == '\t') {
            diag.error(std::string(filename), c.pos(),
                       "tab characters are not allowed (use two spaces per indent level)");
            // Replace with a space to keep column arithmetic predictable.
            // (column already accounts for this character.)
            c.advance();
            continue;
        }
        if (ch == '\r') {
            // Strip CR; treat CRLF as LF. (The output keeps a space here.)
            c.advance();
            continue;
        }
        if (ch == '#') {
            // Line comment: blank out to end of line, keep the newline.
            while (c.i < c.src.size() && c.peek() != '\n') {
                c.advance();
            }
            continue;
        }
        if (c.match2('{', '-')) {
            block_depth   = 1;
            block_open_pos = c.pos();
            c.advance(); c.advance();
            continue;
        }
        if (ch == '"') {
            in_string       = true;
            string_open_pos = c.pos();
            copy_here();
            c.advance();
            continue;
        }
        // Default: copy through.
        copy_here();
        c.advance();
    }

    if (block_depth > 0) {
        diag.error(std::string(filename), block_open_pos,
                   "unterminated block comment ({- ... -})");
    }
    // Unterminated strings are reported by the lexer.
    (void)string_open_pos;
    return out;
}

} // namespace schemlang
