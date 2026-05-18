// lexer.cpp -- Schemlang lexer.
//
// We use lexy (https://github.com/foonathan/lexy) for the actual character
// pattern recognition. The lexer is driven from a small hand-written loop
// that:
//   1. Skips spaces (but not newlines).
//   2. Tracks the start position of each token.
//   3. Tries each lexy "token production" in order until one matches.
//   4. Emits a Token{ kind, text, pos } per match.
//
// Comments have already been stripped by preprocessor.cpp, so the only
// "skip" characters we encounter are ' ', '\t' (which preprocessor would
// have rejected) and '\r' (already gone). Newlines become Newline tokens
// so the subsequent layout pass can see them.
//
// Why driver-loop rather than one giant `dsl::list(p<token>)` grammar?
//   * Easier to recover from invalid characters (we can advance one byte
//     and continue collecting diagnostics).
//   * Each lexy match runs over a small range of input — measurable per
//     token — and we get accurate positions naturally.
//   * Keywords are matched via a single identifier lookup, which is more
//     compact than ~100 lexy alternations.

#include "lexer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <lexy/action/match.hpp>
#include <lexy/action/parse.hpp>
#include <lexy/callback.hpp>
#include <lexy/dsl.hpp>
#include <lexy/input/string_input.hpp>

namespace schemlang {
namespace {

namespace dsl = lexy::dsl;

// ---------------------------------------------------------------------------
// lexy grammars for the regex-like lexeme shapes. Each grammar takes a
// single contiguous input and either matches successfully (consuming some
// prefix) or fails. We call them via `lexy::match` which returns just a
// success/failure boolean *and* the number of consumed characters via a
// reader. We pull both pieces from `lexy::parse` instead.
// ---------------------------------------------------------------------------

// IDENT = [A-Za-z_][A-Za-z0-9_]*
struct g_identifier {
    static constexpr auto rule = dsl::identifier(
        dsl::ascii::alpha_underscore,
        dsl::ascii::alpha_digit_underscore);
    static constexpr auto value = lexy::as_string<std::string>;
};

// (Number literals are driven by match_number() below. We can't write a
// minimal lexy production cleanly because '..' (range) must not be eaten
// as a decimal point, which the lexy 'identifier'-style grammars don't
// allow us to express without arbitrary lookahead.)

// (String literals don't need a lexy grammar -- we drive the scan in C++
// because the layout is trivial: opening '"', then chars or backslash
// escapes, then closing '"'. See match_string() below.)

// ---------------------------------------------------------------------------
// Keyword table.
// ---------------------------------------------------------------------------

using KwMap = std::unordered_map<std::string_view, TokenKind>;

const KwMap& keyword_map() {
    static const KwMap m = {
        {"include",          TokenKind::KwInclude},
        {"as",               TokenKind::KwAs},
        {"only",             TokenKind::KwOnly},
        {"define",           TokenKind::KwDefine},
        {"alias",            TokenKind::KwAlias},
        {"where",            TokenKind::KwWhere},
        {"prefer",           TokenKind::KwPrefer},
        {"wrapper",          TokenKind::KwWrapper},
        {"soft",             TokenKind::KwSoft},
        {"weight",           TokenKind::KwWeight},
        {"designators",      TokenKind::KwDesignators},
        {"designators_lock", TokenKind::KwDesignatorsLock},
        {"start_at",         TokenKind::KwStartAt},
        {"reserve",          TokenKind::KwReserve},
        {"prefix",           TokenKind::KwPrefix},
        {"designator",       TokenKind::KwDesignator},
        {"designator_prefix",TokenKind::KwDesignatorPrefix},
        {"description",      TokenKind::KwDescription},
        {"hint",             TokenKind::KwHint},
        {"constraint",       TokenKind::KwConstraint},
        {"over",             TokenKind::KwOver},
        {"generate",         TokenKind::KwGenerate},
        {"for",              TokenKind::KwFor},
        {"if",               TokenKind::KwIf},
        {"else",             TokenKind::KwElse},
        {"match",            TokenKind::KwMatch},
        {"case",             TokenKind::KwCase},
        {"default",          TokenKind::KwDefault},
        {"in",               TokenKind::KwIn},
        {"use",              TokenKind::KwUse},
        {"pin",              TokenKind::KwPin},
        {"swap_group",       TokenKind::KwSwapGroup},
        {"raw_pin",          TokenKind::KwRawPin},
        {"assume",           TokenKind::KwAssume},
        {"view",             TokenKind::KwView},
        {"lane",             TokenKind::KwLane},
        {"role",             TokenKind::KwRole},
        {"port",             TokenKind::KwPort},
        {"bus",              TokenKind::KwBus},
        {"bank",             TokenKind::KwBank},
        {"pins",             TokenKind::KwPins},
        {"provides",         TokenKind::KwProvides},
        {"pool",             TokenKind::KwPool},
        {"per_bus",          TokenKind::KwPerBus},
        {"parameter",        TokenKind::KwParameter},
        {"override",         TokenKind::KwOverride},
        {"remove",           TokenKind::KwRemove},
        {"package",          TokenKind::KwPackage},
        {"parent",           TokenKind::KwParent},
        {"derive",           TokenKind::KwDerive},
        {"resolution",       TokenKind::KwResolution},
        {"drives",           TokenKind::KwDrives},
        {"receives",         TokenKind::KwReceives},
        {"bidir",            TokenKind::KwBidir},
        {"cardinality",      TokenKind::KwCardinality},
        {"extends",          TokenKind::KwExtends},
        {"splice",           TokenKind::KwSplice},
        {"with",             TokenKind::KwWith},
        {"host_side",        TokenKind::KwHostSide},
        {"peri_side",        TokenKind::KwPeriSide},
        {"and",              TokenKind::KwAnd},
        {"or",               TokenKind::KwOr},
        {"not",              TokenKind::KwNot},
        {"exists",           TokenKind::KwExists},
        {"forall",           TokenKind::KwForall},
        {"sum",              TokenKind::KwSum},
        {"max",              TokenKind::KwMax},
        {"min",              TokenKind::KwMin},
        {"count",            TokenKind::KwCount},
        {"avg",              TokenKind::KwAvg},
        {"any",              TokenKind::KwAny},
        {"all",              TokenKind::KwAll},
        {"none",             TokenKind::KwNone},
        {"prefers",          TokenKind::KwPrefers},
        {"true",             TokenKind::KwTrue},
        {"false",            TokenKind::KwFalse},
        {"pin_prefer",       TokenKind::KwPinPrefer},
    };
    return m;
}

// ---------------------------------------------------------------------------
// Helpers for advancing through the source while keeping a SourcePos.
// ---------------------------------------------------------------------------

struct LexCursor {
    std::string_view text;
    std::size_t      i      = 0;
    std::uint32_t    line   = 1;
    std::uint32_t    column = 1;

    char peek(std::size_t off = 0) const {
        return (i + off < text.size()) ? text[i + off] : '\0';
    }
    SourcePos pos() const {
        return {static_cast<std::uint32_t>(i), line, column};
    }
    bool at_end() const { return i >= text.size(); }
    void advance(std::size_t n = 1) {
        for (std::size_t k = 0; k < n && i < text.size(); ++k) {
            if (text[i] == '\n') { ++line; column = 1; }
            else                 { ++column; }
            ++i;
        }
    }
};

// Try to match `g_identifier` starting at `from`. Returns the number of
// characters consumed (0 on failure).
std::size_t match_identifier(std::string_view src, std::size_t from) {
    if (from >= src.size()) return 0;
    char c = src[from];
    if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) return 0;
    auto input = lexy::string_input<lexy::ascii_encoding>(
        src.data() + from, src.size() - from);
    auto result = lexy::parse<g_identifier>(input, lexy::noop);
    if (!result.has_value()) return 0;
    // `result.value()` is the captured string.
    return result.value().size();
}

// Match a NUMBER token starting at `from`. We do this by scanning explicitly
// (lexy still validates the *digits-only* leading run via g_number indirectly).
// Returns (length, has_frac_or_exp). length == 0 means "no match".
struct NumberMatch {
    std::size_t length      = 0;
    bool        has_decimal = false; // true if frac or exp present
};

NumberMatch match_number(std::string_view src, std::size_t from) {
    std::size_t p = from;
    auto is_digit = [&](char ch) { return ch >= '0' && ch <= '9'; };

    if (p >= src.size() || !is_digit(src[p])) return {};
    // Integer part.
    while (p < src.size() && is_digit(src[p])) ++p;

    NumberMatch m;
    m.length = p - from;

    // Fractional part.  But: we must NOT eat '..' (range operator).
    if (p + 1 < src.size() && src[p] == '.' && src[p + 1] != '.' && is_digit(src[p + 1])) {
        ++p;
        while (p < src.size() && is_digit(src[p])) ++p;
        m.length      = p - from;
        m.has_decimal = true;
    }
    // Exponent part.
    if (p < src.size() && (src[p] == 'e' || src[p] == 'E')) {
        std::size_t q = p + 1;
        if (q < src.size() && (src[q] == '+' || src[q] == '-')) ++q;
        if (q < src.size() && is_digit(src[q])) {
            ++q;
            while (q < src.size() && is_digit(src[q])) ++q;
            p             = q;
            m.length      = p - from;
            m.has_decimal = true;
        }
    }
    return m;
}

// Match a STRING token starting at `from`. Returns (length, contents).
// length is 0 on failure (no leading quote or unterminated).
struct StringMatch {
    std::size_t length   = 0;
    std::string contents;
};

StringMatch match_string(std::string_view src, std::size_t from,
                         std::string_view filename,
                         SourcePos start_pos,
                         DiagnosticBag& diag) {
    if (from >= src.size() || src[from] != '"') return {};
    // Walk character-by-character, decoding escapes into `contents`.
    StringMatch m;
    m.contents.reserve(16);
    std::size_t p = from + 1;
    bool closed  = false;
    while (p < src.size()) {
        char c = src[p];
        if (c == '"') { ++p; closed = true; break; }
        if (c == '\n') break;             // preprocessor will have caught this
        if (c == '\\' && p + 1 < src.size()) {
            char e = src[p + 1];
            switch (e) {
                case '"':  m.contents.push_back('"');  p += 2; continue;
                case '\\': m.contents.push_back('\\'); p += 2; continue;
                case 'n':  m.contents.push_back('\n'); p += 2; continue;
                case 'r':  m.contents.push_back('\r'); p += 2; continue;
                case 't':  m.contents.push_back('\t'); p += 2; continue;
                case 'u': {
                    // \u{XXXX}: validate-only; we don't actually decode the
                    // codepoint for `--tokens` (the surrounding lexeme is
                    // displayed verbatim).
                    std::size_t q = p + 2;
                    if (q >= src.size() || src[q] != '{') {
                        diag.error(std::string(filename), start_pos,
                                   "expected '{' after \\u in string");
                        m.contents.append(src.substr(p, 2));
                        p += 2;
                        continue;
                    }
                    ++q;
                    std::size_t hex_start = q;
                    while (q < src.size() &&
                           ((src[q] >= '0' && src[q] <= '9') ||
                            (src[q] >= 'a' && src[q] <= 'f') ||
                            (src[q] >= 'A' && src[q] <= 'F'))) {
                        ++q;
                    }
                    if (q == hex_start || q >= src.size() || src[q] != '}') {
                        diag.error(std::string(filename), start_pos,
                                   "malformed \\u{...} escape");
                    }
                    if (q < src.size() && src[q] == '}') ++q;
                    m.contents.append(src.substr(p, q - p));
                    p = q;
                    continue;
                }
                default:
                    diag.error(std::string(filename), start_pos,
                               std::string("unknown escape sequence '\\") + e + "'");
                    m.contents.push_back(e);
                    p += 2;
                    continue;
            }
        }
        m.contents.push_back(c);
        ++p;
    }
    if (!closed) {
        diag.error(std::string(filename), start_pos,
                   "unterminated string literal");
        return {};
    }
    m.length = p - from;
    return m;
}

} // namespace

const char* token_kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::EndOfFile:        return "EOF";
        case TokenKind::Newline:          return "NEWLINE";
        case TokenKind::Indent:           return "INDENT";
        case TokenKind::Dedent:           return "DEDENT";
        case TokenKind::Identifier:       return "IDENT";
        case TokenKind::IntegerLit:       return "INT";
        case TokenKind::NumberLit:        return "NUMBER";
        case TokenKind::StringLit:        return "STRING";
        case TokenKind::LBrace:           return "{";
        case TokenKind::RBrace:           return "}";
        case TokenKind::LBracket:         return "[";
        case TokenKind::RBracket:         return "]";
        case TokenKind::LParen:           return "(";
        case TokenKind::RParen:           return ")";
        case TokenKind::LAngle:           return "<";
        case TokenKind::RAngle:           return ">";
        case TokenKind::Comma:            return ",";
        case TokenKind::Colon:            return ":";
        case TokenKind::Dot:              return ".";
        case TokenKind::At:               return "@";
        case TokenKind::Equals:           return "=";
        case TokenKind::EqualsEq:         return "==";
        case TokenKind::BangEq:           return "!=";
        case TokenKind::LessEq:           return "<=";
        case TokenKind::GreaterEq:        return ">=";
        case TokenKind::BidirArrow:       return "<->";
        case TokenKind::FatArrow:         return "=>";
        case TokenKind::DotDot:           return "..";
        case TokenKind::Plus:             return "+";
        case TokenKind::Minus:            return "-";
        case TokenKind::Star:             return "*";
        case TokenKind::Slash:            return "/";
        case TokenKind::Caret:            return "^";
        case TokenKind::Percent:          return "%";
        case TokenKind::PlusSlashMinus:   return "+/-";
        case TokenKind::PlusMinus:        return "+-";
        case TokenKind::KwInclude:        return "include";
        case TokenKind::KwAs:             return "as";
        case TokenKind::KwOnly:           return "only";
        case TokenKind::KwDefine:         return "define";
        case TokenKind::KwAlias:          return "alias";
        case TokenKind::KwWhere:          return "where";
        case TokenKind::KwPrefer:         return "prefer";
        case TokenKind::KwWrapper:        return "wrapper";
        case TokenKind::KwSoft:           return "soft";
        case TokenKind::KwWeight:         return "weight";
        case TokenKind::KwDesignators:    return "designators";
        case TokenKind::KwDesignatorsLock:return "designators_lock";
        case TokenKind::KwStartAt:        return "start_at";
        case TokenKind::KwReserve:        return "reserve";
        case TokenKind::KwPrefix:         return "prefix";
        case TokenKind::KwDesignator:     return "designator";
        case TokenKind::KwDesignatorPrefix:return "designator_prefix";
        case TokenKind::KwDescription:    return "description";
        case TokenKind::KwHint:           return "hint";
        case TokenKind::KwConstraint:     return "constraint";
        case TokenKind::KwOver:           return "over";
        case TokenKind::KwGenerate:       return "generate";
        case TokenKind::KwFor:            return "for";
        case TokenKind::KwIf:             return "if";
        case TokenKind::KwElse:           return "else";
        case TokenKind::KwMatch:          return "match";
        case TokenKind::KwCase:           return "case";
        case TokenKind::KwDefault:        return "default";
        case TokenKind::KwIn:             return "in";
        case TokenKind::KwUse:            return "use";
        case TokenKind::KwPin:            return "pin";
        case TokenKind::KwSwapGroup:      return "swap_group";
        case TokenKind::KwRawPin:         return "raw_pin";
        case TokenKind::KwAssume:         return "assume";
        case TokenKind::KwView:           return "view";
        case TokenKind::KwLane:           return "lane";
        case TokenKind::KwRole:           return "role";
        case TokenKind::KwPort:           return "port";
        case TokenKind::KwBus:            return "bus";
        case TokenKind::KwBank:           return "bank";
        case TokenKind::KwPins:           return "pins";
        case TokenKind::KwProvides:       return "provides";
        case TokenKind::KwPool:           return "pool";
        case TokenKind::KwPerBus:         return "per_bus";
        case TokenKind::KwParameter:      return "parameter";
        case TokenKind::KwOverride:       return "override";
        case TokenKind::KwRemove:         return "remove";
        case TokenKind::KwPackage:        return "package";
        case TokenKind::KwParent:         return "parent";
        case TokenKind::KwDerive:         return "derive";
        case TokenKind::KwResolution:     return "resolution";
        case TokenKind::KwDrives:         return "drives";
        case TokenKind::KwReceives:       return "receives";
        case TokenKind::KwBidir:          return "bidir";
        case TokenKind::KwCardinality:    return "cardinality";
        case TokenKind::KwExtends:        return "extends";
        case TokenKind::KwSplice:         return "splice";
        case TokenKind::KwWith:           return "with";
        case TokenKind::KwHostSide:       return "host_side";
        case TokenKind::KwPeriSide:       return "peri_side";
        case TokenKind::KwAnd:            return "and";
        case TokenKind::KwOr:             return "or";
        case TokenKind::KwNot:            return "not";
        case TokenKind::KwExists:         return "exists";
        case TokenKind::KwForall:         return "forall";
        case TokenKind::KwSum:            return "sum";
        case TokenKind::KwMax:            return "max";
        case TokenKind::KwMin:            return "min";
        case TokenKind::KwCount:          return "count";
        case TokenKind::KwAvg:            return "avg";
        case TokenKind::KwAny:            return "any";
        case TokenKind::KwAll:            return "all";
        case TokenKind::KwNone:           return "none";
        case TokenKind::KwPrefers:        return "prefers";
        case TokenKind::KwTrue:           return "true";
        case TokenKind::KwFalse:          return "false";
        case TokenKind::KwPinPrefer:      return "pin_prefer";
    }
    return "?";
}

std::vector<Token> tokenize(std::string_view filename,
                            std::string_view source,
                            DiagnosticBag& diag) {
    std::vector<Token> out;
    out.reserve(source.size() / 4 + 16);

    LexCursor c{source};

    auto emit = [&](TokenKind k, SourcePos p, std::string text) {
        out.push_back({k, std::move(text), p});
    };

    while (!c.at_end()) {
        char ch = c.peek();

        // ---- whitespace inside a line ---------------------------------
        if (ch == ' ') { c.advance(); continue; }

        // ---- newline --------------------------------------------------
        if (ch == '\n') {
            emit(TokenKind::Newline, c.pos(), "\n");
            c.advance();
            continue;
        }

        SourcePos start = c.pos();

        // ---- punctuation / operators ---------------------------------
        // Multi-char operators first, so '<->' beats '<' and '<=' beats '<'.
        if (ch == '<') {
            if (c.peek(1) == '-' && c.peek(2) == '>') {
                emit(TokenKind::BidirArrow, start, "<->");
                c.advance(3); continue;
            }
            if (c.peek(1) == '=') {
                emit(TokenKind::LessEq, start, "<=");
                c.advance(2); continue;
            }
            emit(TokenKind::LAngle, start, "<");
            c.advance(); continue;
        }
        if (ch == '>') {
            if (c.peek(1) == '=') {
                emit(TokenKind::GreaterEq, start, ">=");
                c.advance(2); continue;
            }
            emit(TokenKind::RAngle, start, ">");
            c.advance(); continue;
        }
        if (ch == '=') {
            if (c.peek(1) == '=') {
                emit(TokenKind::EqualsEq, start, "==");
                c.advance(2); continue;
            }
            if (c.peek(1) == '>') {
                emit(TokenKind::FatArrow, start, "=>");
                c.advance(2); continue;
            }
            emit(TokenKind::Equals, start, "=");
            c.advance(); continue;
        }
        if (ch == '!' && c.peek(1) == '=') {
            emit(TokenKind::BangEq, start, "!=");
            c.advance(2); continue;
        }
        if (ch == '+' && c.peek(1) == '/' && c.peek(2) == '-') {
            emit(TokenKind::PlusSlashMinus, start, "+/-");
            c.advance(3); continue;
        }
        if (ch == '+' && c.peek(1) == '-') {
            emit(TokenKind::PlusMinus, start, "+-");
            c.advance(2); continue;
        }
        if (ch == '.' && c.peek(1) == '.') {
            emit(TokenKind::DotDot, start, "..");
            c.advance(2); continue;
        }
        switch (ch) {
            case '{': emit(TokenKind::LBrace,   start, "{"); c.advance(); continue;
            case '}': emit(TokenKind::RBrace,   start, "}"); c.advance(); continue;
            case '[': emit(TokenKind::LBracket, start, "["); c.advance(); continue;
            case ']': emit(TokenKind::RBracket, start, "]"); c.advance(); continue;
            case '(': emit(TokenKind::LParen,   start, "("); c.advance(); continue;
            case ')': emit(TokenKind::RParen,   start, ")"); c.advance(); continue;
            case ',': emit(TokenKind::Comma,    start, ","); c.advance(); continue;
            case ':': emit(TokenKind::Colon,    start, ":"); c.advance(); continue;
            case '.': emit(TokenKind::Dot,      start, "."); c.advance(); continue;
            case '@': emit(TokenKind::At,       start, "@"); c.advance(); continue;
            case '+': emit(TokenKind::Plus,     start, "+"); c.advance(); continue;
            case '-': emit(TokenKind::Minus,    start, "-"); c.advance(); continue;
            case '*': emit(TokenKind::Star,     start, "*"); c.advance(); continue;
            case '/': emit(TokenKind::Slash,    start, "/"); c.advance(); continue;
            case '^': emit(TokenKind::Caret,    start, "^"); c.advance(); continue;
            case '%': emit(TokenKind::Percent,  start, "%"); c.advance(); continue;
            default: break;
        }

        // ---- string literal ------------------------------------------
        if (ch == '"') {
            auto m = match_string(source, c.i, filename, start, diag);
            if (m.length == 0) {
                // Recover: skip one char.
                c.advance();
                continue;
            }
            emit(TokenKind::StringLit, start, std::move(m.contents));
            c.advance(m.length);
            continue;
        }

        // ---- number --------------------------------------------------
        if (ch >= '0' && ch <= '9') {
            auto m = match_number(source, c.i);
            assert(m.length > 0);
            std::string text(source.substr(c.i, m.length));
            emit(m.has_decimal ? TokenKind::NumberLit : TokenKind::IntegerLit,
                 start, std::move(text));
            c.advance(m.length);
            continue;
        }

        // ---- identifier / keyword ------------------------------------
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            auto len = match_identifier(source, c.i);
            assert(len > 0);
            std::string text(source.substr(c.i, len));
            auto const& kws = keyword_map();
            auto it = kws.find(text);
            TokenKind kind = (it != kws.end()) ? it->second : TokenKind::Identifier;
            emit(kind, start, std::move(text));
            c.advance(len);
            continue;
        }

        // ---- anything else is a lexical error ------------------------
        {
            std::string msg = "unexpected character '";
            msg.push_back(ch);
            msg += "'";
            diag.error(std::string(filename), start, std::move(msg));
            c.advance();
        }
    }

    // Always end with a Newline (cheap; idempotent for layout pass).
    if (out.empty() || out.back().kind != TokenKind::Newline) {
        SourcePos eofpos{static_cast<std::uint32_t>(source.size()), c.line, c.column};
        out.push_back({TokenKind::Newline, "\n", eofpos});
    }
    return out;
}

} // namespace schemlang
