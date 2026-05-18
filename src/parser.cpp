// parser.cpp -- Schemlang syntax-only recursive-descent parser.
//
// This implements every production from docs/schemlang.ebnf, plus a small
// number of permissive extensions to match what the examples in
// `examples/` actually use. The EBNF is treated as the spec; deviations
// are commented where they happen.
//
// Structure:
//   * Parser::parse_*() functions correspond 1:1 to EBNF productions.
//   * Token consumption goes through expect()/match()/check().
//   * The parser swallows multiple consecutive Newline tokens at every
//     statement boundary (blank lines).
//   * On syntax errors, parse_top_stmt() / parse_body_stmt() catch the
//     ParseError, record a diagnostic and synchronise to the next
//     Newline at the current indent.

#include "parser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace schemlang {

namespace {

const char* kind_label(TokenKind k) { return token_kind_name(k); }

class Parser {
public:
    Parser(std::string_view filename,
           const std::vector<Token>& toks,
           DiagnosticBag& diag)
        : filename_(filename), toks_(toks), diag_(diag) {}

    bool parse_file() {
        // file ::= top_stmt*
        skip_newlines();
        while (!at_eof()) {
            try {
                parse_top_stmt();
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_top();
            }
            skip_newlines();
        }
        return !diag_.has_errors();
    }

private:
    // -----------------------------------------------------------------
    // Token-stream primitives.
    // -----------------------------------------------------------------
    std::string_view           filename_;
    const std::vector<Token>&  toks_;
    DiagnosticBag&             diag_;
    std::size_t                i_ = 0;

    const Token& peek(std::size_t off = 0) const {
        return (i_ + off < toks_.size()) ? toks_[i_ + off] : toks_.back();
    }
    bool at_eof() const { return peek().kind == TokenKind::EndOfFile; }
    bool check(TokenKind k) const { return peek().kind == k; }
    bool check_at(std::size_t off, TokenKind k) const { return peek(off).kind == k; }
    bool match(TokenKind k) {
        if (check(k)) { ++i_; return true; }
        return false;
    }
    const Token& consume() {
        const Token& t = peek();
        if (i_ < toks_.size()) ++i_;
        return t;
    }
    const Token& expect(TokenKind k, const char* what) {
        if (!check(k)) {
            std::string msg = "expected ";
            msg += what;
            msg += ", got '";
            msg += peek().text.empty() ? kind_label(peek().kind) : peek().text;
            msg += "'";
            throw ParseError(peek().pos, std::move(msg));
        }
        return consume();
    }
    [[noreturn]] void error_at(const Token& t, std::string msg) {
        throw ParseError(t.pos, std::move(msg));
    }

    // -----------------------------------------------------------------
    // Layout helpers.
    // -----------------------------------------------------------------
    void skip_newlines() {
        while (check(TokenKind::Newline)) ++i_;
    }
    void expect_newline() {
        if (check(TokenKind::EndOfFile)) return;
        if (!check(TokenKind::Newline)) {
            error_at(peek(), "expected end of line");
        }
        skip_newlines();
    }
    void expect_indent() {
        skip_newlines();
        if (!match(TokenKind::Indent)) {
            error_at(peek(), "expected indented block");
        }
    }
    bool at_dedent_or_eof() const {
        return check(TokenKind::Dedent) || at_eof();
    }
    // Attempts to start an indented child block. Skips any intervening
    // (blank-line) Newlines and consumes the Indent on success. Returns
    // false if no Indent follows: caller usually then expects a Newline.
    bool try_open_block() {
        std::size_t j = i_;
        while (j < toks_.size() && toks_[j].kind == TokenKind::Newline) ++j;
        if (j < toks_.size() && toks_[j].kind == TokenKind::Indent) {
            i_ = j + 1;
            return true;
        }
        return false;
    }
    // After a child block, expect a Dedent.
    void close_block() {
        if (check(TokenKind::Newline)) skip_newlines();
        if (!match(TokenKind::Dedent)) {
            // If layout was inconsistent we may already be at EOF.
            if (!at_eof()) {
                error_at(peek(), "expected dedent (block close)");
            }
        }
    }

    // Recover by skipping until we are back at a sensible point: a Newline
    // at the current block, a Dedent, or EOF.
    void synchronize_top() {
        int indent_depth = 0;
        while (!at_eof()) {
            if (check(TokenKind::Newline) && indent_depth == 0) {
                consume();
                return;
            }
            if (check(TokenKind::Indent))      ++indent_depth;
            else if (check(TokenKind::Dedent)) --indent_depth;
            if (indent_depth < 0) return;
            consume();
        }
    }
    void synchronize_in_block() {
        int indent_depth = 0;
        while (!at_eof()) {
            if (indent_depth == 0 && (check(TokenKind::Newline) ||
                                       check(TokenKind::Dedent))) {
                if (check(TokenKind::Newline)) consume();
                return;
            }
            if (check(TokenKind::Indent))      ++indent_depth;
            else if (check(TokenKind::Dedent)) --indent_depth;
            if (indent_depth < 0) return;
            consume();
        }
    }

    // -----------------------------------------------------------------
    // Top-level dispatch.
    // -----------------------------------------------------------------
    void parse_top_stmt() {
        switch (peek().kind) {
            case TokenKind::KwInclude:          parse_include_stmt(); return;
            case TokenKind::KwDefine:           parse_define_stmt(); return;
            case TokenKind::KwAlias:            parse_alias_stmt(); return;
            case TokenKind::KwPrefer:           parse_prefer_stmt(); return;
            case TokenKind::KwDesignators:      parse_designators_stmt(); return;
            case TokenKind::KwDesignatorsLock:  parse_designators_lock_stmt(); return;
            case TokenKind::KwHint:             parse_hint_stmt(); return;
            case TokenKind::KwConstraint:       parse_constraint_stmt(); return;
            case TokenKind::KwGenerate:         parse_generator_stmt(); return;
            case TokenKind::KwUse:              parse_use_stmt(); return;
            default:
                error_at(peek(), "expected a top-level statement");
        }
    }

    // §2 include
    void parse_include_stmt() {
        expect(TokenKind::KwInclude, "'include'");
        expect(TokenKind::StringLit, "string path");
        if (match(TokenKind::KwAs)) {
            expect(TokenKind::Identifier, "alias identifier after 'as'");
        } else if (match(TokenKind::KwOnly)) {
            expect(TokenKind::LBrace, "'{' after 'only'");
            parse_ident_list();
            expect(TokenKind::RBrace, "'}' to close 'only' list");
        }
        expect_newline();
    }

    void parse_ident_list() {
        expect(TokenKind::Identifier, "identifier");
        while (match(TokenKind::Comma)) {
            expect(TokenKind::Identifier, "identifier after ','");
        }
    }

    // §3 define
    void parse_define_stmt() {
        expect(TokenKind::KwDefine, "'define'");
        TokenKind sigil = parse_sigil_name();
        if (match(TokenKind::Colon)) {
            parse_sigil_name();
            while (match(TokenKind::Comma)) parse_sigil_name();
        }
        expect_newline();
        expect_indent();
        parse_body(sigil);
        close_block();
    }

    // Reads the "name" inside a sigil. Per the EBNF that's an IDENT
    // (letter-or-underscore lead). In practice the standard library and
    // examples use names that the lexer can't capture as a single IDENT:
    //   * `<3V3>`, `<5V0>` — digit-leading: split into INT + IDENT.
    //   * `{ERJ-PHF}`, `[SOD-123]`, `[LQFP-100]` — embedded hyphens: split
    //     into IDENT/INT, '-', IDENT/INT.
    // We accept any run of ident/number/hyphen tokens here.
    void parse_sigil_inner_ident(const char* role) {
        bool consumed_any = false;
        while (true) {
            TokenKind k = peek().kind;
            if (k == TokenKind::Identifier || k == TokenKind::IntegerLit ||
                k == TokenKind::NumberLit  || k == TokenKind::Minus) {
                consume();
                consumed_any = true;
            } else if (is_keyword(k)) {
                // Allow keywords as part of a name inside a sigil.
                consume();
                consumed_any = true;
            } else {
                break;
            }
        }
        if (!consumed_any) {
            error_at(peek(), std::string("expected ") + role + " name inside sigil");
        }
    }

    // Returns true for any "ident-shaped" token: a plain identifier or any
    // keyword. The schemlang grammar reserves a lot of words, but many of
    // them appear naturally as field, lane, role, or pin names in user
    // code (e.g. `mcu.spi1.bank`, `derive Vio = bank(...)`). Accept them
    // wherever an identifier is otherwise expected (path segments, function
    // call heads, etc.).
    static bool is_keyword(TokenKind k) {
        switch (k) {
            case TokenKind::EndOfFile:        case TokenKind::Newline:
            case TokenKind::Indent:           case TokenKind::Dedent:
            case TokenKind::Identifier:
            case TokenKind::IntegerLit:       case TokenKind::NumberLit:
            case TokenKind::StringLit:
            case TokenKind::LBrace: case TokenKind::RBrace:
            case TokenKind::LBracket: case TokenKind::RBracket:
            case TokenKind::LParen: case TokenKind::RParen:
            case TokenKind::LAngle: case TokenKind::RAngle:
            case TokenKind::Comma: case TokenKind::Colon:
            case TokenKind::Dot: case TokenKind::At: case TokenKind::Equals:
            case TokenKind::EqualsEq: case TokenKind::BangEq:
            case TokenKind::LessEq: case TokenKind::GreaterEq:
            case TokenKind::BidirArrow: case TokenKind::FatArrow:
            case TokenKind::DotDot: case TokenKind::Plus: case TokenKind::Minus:
            case TokenKind::Star: case TokenKind::Slash: case TokenKind::Caret:
            case TokenKind::Percent:
            case TokenKind::PlusSlashMinus: case TokenKind::PlusMinus:
                return false;
            default:
                return true;
        }
    }
    static bool is_ident_like(TokenKind k) {
        return k == TokenKind::Identifier || is_keyword(k);
    }
    // Consume one identifier-like token (either Identifier or a keyword)
    // and ignore its specific TokenKind value.
    const Token& consume_ident_like(const char* what) {
        if (!is_ident_like(peek().kind)) {
            error_at(peek(), std::string("expected ") + what);
        }
        return consume();
    }

    // Returns the sigil kind we just parsed: LBrace ({}), LBracket ([]), or
    // LAngle (<>). Used by parse_body() to know which production set is
    // permitted (we still accept the EBNF union but this lets us inform
    // the user where appropriate).
    TokenKind parse_sigil_name() {
        if (match(TokenKind::LBrace)) {
            parse_sigil_inner_ident("component");
            expect(TokenKind::RBrace,  "'}' to close component sigil");
            return TokenKind::LBrace;
        }
        if (match(TokenKind::LBracket)) {
            parse_sigil_inner_ident("package");
            expect(TokenKind::RBracket, "']' to close package sigil");
            return TokenKind::LBracket;
        }
        if (match(TokenKind::LAngle)) {
            parse_sigil_inner_ident("circuit");
            expect(TokenKind::RAngle,   "'>' to close circuit sigil");
            return TokenKind::LAngle;
        }
        error_at(peek(), "expected a sigil-bracketed name: '{...}', '[...]' or '<...>'");
    }
    void parse_sigil_circuit() {
        expect(TokenKind::LAngle, "'<' (circuit sigil)");
        parse_sigil_inner_ident("circuit");
        expect(TokenKind::RAngle, "'>' to close circuit sigil");
    }
    void parse_sigil_pkg() {
        expect(TokenKind::LBracket, "'[' (package sigil)");
        parse_sigil_inner_ident("package");
        expect(TokenKind::RBracket, "']' to close package sigil");
    }

    // body ::= body_stmt*
    void parse_body(TokenKind enclosing_sigil) {
        while (!at_dedent_or_eof()) {
            try {
                parse_body_stmt(enclosing_sigil);
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
    }

    void parse_body_stmt(TokenKind enclosing_sigil) {
        (void)enclosing_sigil; // reserved for future per-kind admissibility
        skip_newlines();
        if (at_dedent_or_eof()) return;

        // Dispatch by leading token.
        switch (peek().kind) {
            case TokenKind::KwDescription:        return parse_description();
            case TokenKind::KwDesignatorPrefix:   return parse_designator_prefix();
            case TokenKind::KwPin:                return parse_pin_stmt();
            case TokenKind::KwSwapGroup:          return parse_swap_group();
            case TokenKind::KwView:               return parse_view();
            case TokenKind::KwLane:               return parse_lane();
            case TokenKind::KwRole:               return parse_role();
            case TokenKind::KwPort:               return parse_port();
            case TokenKind::KwBus:                return parse_bus_decl();
            case TokenKind::KwBank:               return parse_bank_decl();
            case TokenKind::KwProvides:           return parse_provides_decl();
            case TokenKind::KwSplice:             return parse_splice_stmt();
            case TokenKind::KwConstraint:         return parse_constraint_stmt();
            case TokenKind::KwGenerate:           return parse_generator_stmt();
            case TokenKind::KwParameter:          return parse_parameter();
            case TokenKind::KwAlias:              return parse_alias_stmt();
            case TokenKind::KwHint:               return parse_hint_stmt();
            case TokenKind::KwDerive:             return parse_derive_stmt();
            case TokenKind::KwResolution:         return parse_resolution_stmt();
            case TokenKind::KwPrefer:             return parse_prefer_stmt();
            case TokenKind::KwOverride:           return parse_override_stmt();
            case TokenKind::KwRemove:             return parse_remove_stmt();
            case TokenKind::KwAssume:             return parse_assume_stmt();
            // Package bindings open with a package sigil.
            case TokenKind::LBracket:             return parse_package_binding();
            default: break;
        }
        // Connect / instance / net_label — all start with a path-shaped
        // thing or a set literal.
        if (check(TokenKind::Identifier) || check(TokenKind::LBrace)) {
            return parse_identifier_starting_body_stmt();
        }
        error_at(peek(), "unexpected token at start of body statement");
    }

    // -----------------------------------------------------------------
    // §4 components: description, designator_prefix
    // -----------------------------------------------------------------
    void parse_description() {
        expect(TokenKind::KwDescription, "'description'");
        expect(TokenKind::StringLit, "description string");
        expect_newline();
    }

    void parse_designator_prefix() {
        expect(TokenKind::KwDesignatorPrefix, "'designator_prefix'");
        expect(TokenKind::Identifier, "identifier after 'designator_prefix'");
        expect_newline();
    }

    // -----------------------------------------------------------------
    // §4 / §5 package binding + pin + swap_group + param_block
    // -----------------------------------------------------------------
    void parse_package_binding() {
        parse_sigil_pkg();
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                if (check(TokenKind::KwPin))               parse_pin_stmt();
                else if (check(TokenKind::KwSwapGroup))    parse_swap_group();
                else if (check(TokenKind::KwGenerate))     parse_generator_stmt();
                else if (check(TokenKind::Newline))        skip_newlines();
                else error_at(peek(),
                              "expected 'pin', 'swap_group' or 'generate' in package binding");
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }

    // pin ::= 'pin' pin_id IDENT IDENT* pin_role? param_block? NEWLINE
    // pin_id is INT inside a package_binding (component body) and may be
    // empty inside a package definition (`define [Pkg]`). To be tolerant
    // we accept INT, IDENT or STRING for the pin number (examples use
    // string interpolation `"${col}${row}"` and generator variables).
    void parse_pin_stmt() {
        expect(TokenKind::KwPin, "'pin'");
        // pin_id (optional)
        switch (peek().kind) {
            case TokenKind::IntegerLit:
            case TokenKind::Identifier:
            case TokenKind::StringLit:
                consume();
                break;
            default: break;
        }
        // Logical names (zero or more identifiers). Stop on 'role' kw,
        // Newline, or Indent.
        while (check(TokenKind::Identifier)) {
            consume();
        }
        if (match(TokenKind::KwRole)) {
            parse_role_kind();
        }
        if (try_open_block()) {
            while (!at_dedent_or_eof()) {
                if (check(TokenKind::KwParameter))      parse_parameter();
                else if (check(TokenKind::KwConstraint)) parse_constraint_stmt();
                else if (check(TokenKind::Newline))      skip_newlines();
                else error_at(peek(), "expected 'parameter' or 'constraint' in pin block");
            }
            close_block();
        } else {
            expect_newline();
        }
    }

    void parse_role_kind() {
        if (!check(TokenKind::Identifier)) {
            error_at(peek(), "expected role kind after 'role'");
        }
        static const std::unordered_set<std::string> kinds = {
            "power_in", "power_out", "ground",
            "analog_in", "analog_out",
            "digital_in", "digital_out", "digital_io",
            "passive", "nc",
        };
        if (!kinds.count(peek().text)) {
            error_at(peek(), "unknown pin role kind '" + peek().text + "'");
        }
        consume();
    }

    void parse_swap_group() {
        expect(TokenKind::KwSwapGroup, "'swap_group'");
        expect(TokenKind::LBrace, "'{' to start swap group");
        auto consume_member = [&] {
            if (check(TokenKind::Identifier) || check(TokenKind::IntegerLit)) {
                consume();
            } else {
                error_at(peek(), "expected identifier or pin number in swap_group");
            }
        };
        consume_member();
        while (match(TokenKind::Comma)) consume_member();
        expect(TokenKind::RBrace, "'}' to close swap_group");
        expect_newline();
    }

    // -----------------------------------------------------------------
    // §4 view
    // -----------------------------------------------------------------
    void parse_view() {
        expect(TokenKind::KwView, "'view'");
        parse_sigil_circuit();
        expect(TokenKind::KwAs, "'as'");
        expect(TokenKind::Identifier, "role name after 'as'");
        // optional view_name
        if (check(TokenKind::Identifier)) consume();
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                if (check(TokenKind::KwDerive))       parse_derive_stmt();
                else if (check(TokenKind::Newline))   skip_newlines();
                else                                  parse_view_mapping();
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }
    void parse_view_mapping() {
        expect(TokenKind::Identifier, "lane name");
        expect(TokenKind::Identifier, "pin-or-lane name in view mapping");
        expect_newline();
    }

    // -----------------------------------------------------------------
    // §6 lane (in circuit body)
    // -----------------------------------------------------------------
    // EBNF says one IDENT; examples (`lane SDA, SCL : digital`) allow a
    // comma-separated list.
    void parse_lane() {
        expect(TokenKind::KwLane, "'lane'");
        expect(TokenKind::Identifier, "lane name");
        while (match(TokenKind::Comma)) {
            expect(TokenKind::Identifier, "lane name after ','");
        }
        expect(TokenKind::Colon, "':' before lane type");
        parse_type_expr();
        expect_newline();
    }

    // -----------------------------------------------------------------
    // §6 role (circuit body)
    // -----------------------------------------------------------------
    void parse_role() {
        expect(TokenKind::KwRole, "'role'");
        expect(TokenKind::Identifier, "role name");
        if (match(TokenKind::KwExtends)) {
            parse_sigil_circuit();
            expect(TokenKind::Dot, "'.' after parent circuit in 'extends'");
            expect(TokenKind::Identifier, "parent role name");
        }
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                parse_role_body_stmt();
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }
    void parse_role_body_stmt() {
        switch (peek().kind) {
            case TokenKind::KwDrives:
            case TokenKind::KwReceives:
            case TokenKind::KwBidir:        return parse_direction_set();
            case TokenKind::KwCardinality:  return parse_cardinality_stmt();
            case TokenKind::KwDerive:       return parse_derive_stmt();
            case TokenKind::KwPrefer:       return parse_prefer_stmt();
            case TokenKind::KwParameter:    return parse_parameter();
            case TokenKind::Newline:        skip_newlines(); return;
            default:
                error_at(peek(),
                         "expected direction set, cardinality, derive, prefer "
                         "or parameter in role body");
        }
    }
    void parse_direction_set() {
        if (!(check(TokenKind::KwDrives) || check(TokenKind::KwReceives) ||
              check(TokenKind::KwBidir))) {
            error_at(peek(), "expected 'drives', 'receives' or 'bidir'");
        }
        consume();
        expect(TokenKind::LBrace, "'{' to start direction set");
        parse_ident_list();
        expect(TokenKind::RBrace, "'}' to close direction set");
        expect_newline();
    }
    void parse_cardinality_stmt() {
        expect(TokenKind::KwCardinality, "'cardinality'");
        // cardinality_form ::= '=' (INT | '*') | '>=' INT | '<=' INT | 'in' '[' INT ',' INT ']'
        if (match(TokenKind::Equals)) {
            if (match(TokenKind::Star)) { /* ok */ }
            else expect(TokenKind::IntegerLit, "integer or '*' after '='");
        } else if (match(TokenKind::GreaterEq)) {
            expect(TokenKind::IntegerLit, "integer after '>='");
        } else if (match(TokenKind::LessEq)) {
            expect(TokenKind::IntegerLit, "integer after '<='");
        } else if (match(TokenKind::KwIn)) {
            expect(TokenKind::LBracket, "'[' after 'in'");
            expect(TokenKind::IntegerLit, "low bound integer");
            expect(TokenKind::Comma, "',' between bounds");
            expect(TokenKind::IntegerLit, "high bound integer");
            expect(TokenKind::RBracket, "']' to close range");
        } else {
            error_at(peek(), "expected '=', '>=', '<=' or 'in' after 'cardinality'");
        }
        expect_newline();
    }
    // derive_stmt ::= 'derive' IDENT '=' path
    void parse_derive_stmt() {
        expect(TokenKind::KwDerive, "'derive'");
        expect(TokenKind::Identifier, "name after 'derive'");
        expect(TokenKind::Equals, "'=' in derive statement");
        parse_path();
        expect_newline();
    }

    // -----------------------------------------------------------------
    // §6 resolution
    // -----------------------------------------------------------------
    void parse_resolution_stmt() {
        expect(TokenKind::KwResolution, "'resolution'");
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                parse_resolution_case();
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }
    void parse_resolution_case() {
        if (match(TokenKind::KwCase)) {
            parse_resolution_pred();
        } else if (match(TokenKind::KwDefault)) {
            // continues with =>
        } else if (check(TokenKind::Newline)) {
            skip_newlines();
            return;
        } else {
            error_at(peek(), "expected 'case' or 'default' in resolution");
        }
        expect(TokenKind::FatArrow, "'=>' after resolution head");
        parse_sigil_name();
        if (check(TokenKind::LParen)) parse_call_args();
        if (match(TokenKind::KwWeight)) {
            expect(TokenKind::IntegerLit, "integer after 'weight'");
        }
        expect_newline();
    }
    void parse_resolution_pred() {
        if (match(TokenKind::KwAny) || match(TokenKind::KwAll) || match(TokenKind::KwNone)) {
            expect(TokenKind::LParen, "'(' after quantifier");
            expect(TokenKind::KwPrefers, "'prefers' in resolution predicate");
            parse_sigil_name();
            expect(TokenKind::RParen, "')' to close resolution predicate");
        } else if (match(TokenKind::KwCount)) {
            expect(TokenKind::LParen, "'(' after 'count'");
            expect(TokenKind::KwPrefers, "'prefers' in resolution predicate");
            parse_sigil_name();
            expect(TokenKind::RParen, "')' to close 'count(...)'");
            parse_comp_op();
            expect(TokenKind::IntegerLit, "integer after comparison");
        } else {
            error_at(peek(), "expected 'any', 'all', 'none' or 'count' after 'case'");
        }
    }
    void parse_comp_op() {
        switch (peek().kind) {
            case TokenKind::EqualsEq:
            case TokenKind::BangEq:
            case TokenKind::LessEq:
            case TokenKind::GreaterEq:
            case TokenKind::LAngle:
            case TokenKind::RAngle:
                consume(); return;
            default:
                error_at(peek(), "expected a comparison operator");
        }
    }

    // -----------------------------------------------------------------
    // §7 ports / instances / buses / banks / provides
    // -----------------------------------------------------------------
    void parse_port() {
        expect(TokenKind::KwPort, "'port'");
        parse_ident_list();
        expect(TokenKind::Colon, "':' before port type");
        parse_sigil_circuit();
        expect(TokenKind::KwAs, "'as' before role name");
        expect(TokenKind::Identifier, "role name");
        expect_newline();
    }

    void parse_bus_decl() {
        expect(TokenKind::KwBus, "'bus'");
        expect(TokenKind::Identifier, "bus name");
        expect(TokenKind::Colon, "':' before bus type");
        parse_sigil_circuit();
        expect(TokenKind::KwAs, "'as' before role name");
        expect(TokenKind::Identifier, "role name");
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                if (check(TokenKind::KwBank))         parse_bank_assignment();
                else if (check(TokenKind::KwDerive)) parse_derive_stmt();
                else if (check(TokenKind::Newline))  skip_newlines();
                else error_at(peek(), "expected 'bank' or 'derive' inside bus declaration");
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }

    void parse_bank_assignment() {
        expect(TokenKind::KwBank, "'bank'");
        expect(TokenKind::Identifier, "bank name");
        expect(TokenKind::LBrace, "'{' to open bank assignment");
        parse_lane_pin_map();
        while (match(TokenKind::Comma)) parse_lane_pin_map();
        expect(TokenKind::RBrace, "'}' to close bank assignment");
        expect_newline();
    }
    void parse_lane_pin_map() {
        expect(TokenKind::Identifier, "lane name");
        expect(TokenKind::Equals, "'=' in lane = pin");
        expect(TokenKind::Identifier, "pin name");
    }

    // §7 bank_decl (component body) — disambiguated from bank_assignment
    // by context (we only enter parse_bank_decl from a component body).
    void parse_bank_decl() {
        expect(TokenKind::KwBank, "'bank'");
        expect(TokenKind::Identifier, "bank name");
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                if (check(TokenKind::KwPins)) {
                    consume();
                    expect(TokenKind::LBrace, "'{' after 'pins'");
                    parse_ident_list();
                    expect(TokenKind::RBrace, "'}' to close pin list");
                    expect_newline();
                } else if (check(TokenKind::KwParameter)) {
                    parse_parameter();
                } else if (check(TokenKind::KwConstraint)) {
                    parse_constraint_stmt();
                } else if (check(TokenKind::KwAlias)) {
                    parse_alias_stmt();
                } else if (check(TokenKind::Newline)) {
                    skip_newlines();
                } else {
                    error_at(peek(), "expected 'pins', 'parameter', 'constraint' or 'alias' "
                                     "in bank body");
                }
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }

    // §7 provides
    void parse_provides_decl() {
        expect(TokenKind::KwProvides, "'provides'");
        parse_sigil_circuit();
        expect(TokenKind::KwAs, "'as' before role name");
        expect(TokenKind::Identifier, "role name");
        expect_newline();
        expect_indent();
        // pool_clause + per_bus_clause, plus optional derive_stmts.
        bool saw_pool = false, saw_per_bus = false;
        while (!at_dedent_or_eof()) {
            try {
                if (check(TokenKind::KwPool)) {
                    parse_pool_clause();
                    saw_pool = true;
                } else if (check(TokenKind::KwPerBus)) {
                    parse_per_bus_clause();
                    saw_per_bus = true;
                } else if (check(TokenKind::KwDerive)) {
                    parse_derive_stmt();
                } else if (check(TokenKind::Newline)) {
                    skip_newlines();
                } else {
                    error_at(peek(), "expected 'pool', 'per_bus' or 'derive' inside provides");
                }
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        (void)saw_pool; (void)saw_per_bus;
        close_block();
    }
    void parse_pool_clause() {
        expect(TokenKind::KwPool, "'pool'");
        parse_path();
        if (match(TokenKind::KwWhere)) parse_expr();
        expect_newline();
    }
    void parse_per_bus_clause() {
        expect(TokenKind::KwPerBus, "'per_bus'");
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                if (check(TokenKind::KwWhere)) {
                    consume();
                    parse_expr();
                    expect_newline();
                } else if (check(TokenKind::Identifier)) {
                    consume(); // lane name
                    expect(TokenKind::KwIn, "'in' after lane name");
                    expect(TokenKind::KwPool, "'pool' after 'in'");
                    expect_newline();
                } else if (check(TokenKind::Newline)) {
                    skip_newlines();
                } else {
                    error_at(peek(), "expected lane candidate or 'where' clause in per_bus");
                }
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }

    // -----------------------------------------------------------------
    // §7 splice
    // -----------------------------------------------------------------
    void parse_splice_stmt() {
        expect(TokenKind::KwSplice, "'splice'");
        expect(TokenKind::Identifier, "lane name after 'splice'");
        expect(TokenKind::KwWith, "'with' in splice statement");
        parse_path();
        expect_newline();
    }

    // -----------------------------------------------------------------
    // Body statements that start with an IDENT or set literal:
    //   * instance:   IDENT ('[' index ']')? '=' sigil_name ...
    //   * net_label:  IDENT '=' path
    //   * connect:    connect_term '<->' connect_term ...
    //   * lane_endpoint:  IDENT '.' (host_side|peri_side) — only as
    //                     LHS of a connect, handled inside parse_path()/
    //                     parse_connect_term.
    // -----------------------------------------------------------------
    void parse_identifier_starting_body_stmt() {
        // We need to decide between four shapes:
        //
        //   (a) IDENT ('[' anything ']')? '=' <sigil_open>     → instance
        //   (b) IDENT path_segment* '=' anything                → net_label
        //   (c) any connect_term '<->' connect_term             → connect
        //   (d) IDENT ':' sigil_circuit 'as' role_name          → typed
        //       declaration (used as a shorthand for ports in body)
        //
        // Connect is detected by a fast scan over the leading term: any
        // '<->' at depth 0 before a Newline/Indent/Dedent wins.
        if (looks_like_connect_stmt()) {
            return parse_connect_stmt();
        }
        std::size_t save = i_;
        consume_ident_like("identifier");
        if (match(TokenKind::LBracket)) {
            parse_index_expr();
            expect(TokenKind::RBracket, "']' after index in LHS");
        }
        if (check(TokenKind::Colon)) {
            // Typed declaration: `name : <Type> as role`.
            consume();
            parse_sigil_circuit();
            expect(TokenKind::KwAs, "'as' before role name");
            consume_ident_like("role name");
            expect_newline();
            return;
        }
        if (!check(TokenKind::Equals)) {
            i_ = save;
            error_at(peek(), "unexpected expression at statement position");
        }
        consume(); // '='
        // Instance if RHS starts with a sigil; otherwise net_label.
        if (check(TokenKind::LBrace) || check(TokenKind::LBracket) ||
            check(TokenKind::LAngle)) {
            parse_instance_tail();
        } else {
            parse_path();
            expect_newline();
        }
    }

    // Returns true if the upcoming sequence (starting at i_) looks like a
    // connect statement: a connect_term followed by '<->'. We scan
    // ahead with depth tracking for brackets so we don't get confused by
    // sigils.
    bool looks_like_connect_stmt() const {
        // A set literal '{' .. '}' on the LHS unambiguously begins a
        // connect_term. (Cannot be an instance/net_label.)
        if (check(TokenKind::LBrace)) return true;

        std::size_t j = i_;
        int depth = 0;
        while (j < toks_.size()) {
            TokenKind k = toks_[j].kind;
            if (depth == 0) {
                if (k == TokenKind::BidirArrow) return true;
                if (k == TokenKind::Newline)    return false;
                if (k == TokenKind::Indent)     return false;
                if (k == TokenKind::Dedent)     return false;
                if (k == TokenKind::EndOfFile)  return false;
                if (k == TokenKind::Equals)     return false; // instance / net_label
            }
            switch (k) {
                case TokenKind::LBrace:
                case TokenKind::LBracket:
                case TokenKind::LParen:
                case TokenKind::LAngle:    ++depth; break;
                case TokenKind::RBrace:
                case TokenKind::RBracket:
                case TokenKind::RParen:
                case TokenKind::RAngle:    if (depth > 0) --depth; break;
                default: break;
            }
            ++j;
        }
        return false;
    }

    // instance tail: parsed *after* `IDENT ('[' idx ']')? '='`.
    void parse_instance_tail() {
        parse_sigil_name();
        if (check(TokenKind::LParen)) parse_call_args();
        if (match(TokenKind::At)) {
            expect(TokenKind::Identifier, "designator after '@'");
        }
        if (try_open_block()) {
            while (!at_dedent_or_eof()) {
                try {
                    parse_instance_field();
                } catch (ParseError const& e) {
                    diag_.error(std::string(filename_), e.where(), e.what());
                    synchronize_in_block();
                }
                skip_newlines();
            }
            close_block();
        } else {
            expect_newline();
        }
    }

    void parse_instance_field() {
        switch (peek().kind) {
            case TokenKind::KwDesignator:
                consume();
                expect(TokenKind::Identifier, "designator value");
                expect_newline();
                return;
            case TokenKind::KwParameter:
                return parse_parameter();
            case TokenKind::KwDescription:
                return parse_description();
            case TokenKind::KwHint:
                return parse_hint_stmt();
            case TokenKind::Newline:
                skip_newlines();
                return;
            default:
                error_at(peek(),
                         "expected 'designator', 'parameter', 'description' or 'hint' "
                         "in instance body");
        }
    }

    // -----------------------------------------------------------------
    // Connect.
    // -----------------------------------------------------------------
    void parse_connect_stmt() {
        parse_connect_term();
        expect(TokenKind::BidirArrow, "'<->'");
        parse_connect_term();
        if (try_open_block()) {
            while (!at_dedent_or_eof()) {
                try {
                    parse_connect_arg();
                } catch (ParseError const& e) {
                    diag_.error(std::string(filename_), e.where(), e.what());
                    synchronize_in_block();
                }
                skip_newlines();
            }
            close_block();
        } else {
            expect_newline();
        }
    }

    void parse_connect_term() {
        if (match(TokenKind::LBrace)) {
            parse_connect_term();
            while (match(TokenKind::Comma)) parse_connect_term();
            expect(TokenKind::RBrace, "'}' to close set literal");
            return;
        }
        // Otherwise: a path. Could be lane_endpoint (IDENT '.' host_side|peri_side)
        // or a generic path. We parse a generic path; the lane_endpoint
        // form is a special case where the final segment is 'host_side'
        // or 'peri_side'.
        parse_path();
    }

    void parse_connect_arg() {
        if (match(TokenKind::KwOverride)) {
            parse_connect_arg();        // override + inner connect_arg
            return;
        }
        if (match(TokenKind::KwParameter)) {
            expect(TokenKind::Identifier, "parameter name");
            expect(TokenKind::Equals, "'=' after parameter name");
            parse_arg_value();
            expect_newline();
            return;
        }
        if (match(TokenKind::KwPrefer)) {
            // prefer wrapper = sigil_name  OR  prefer sigil_name = sigil_name
            if (match(TokenKind::KwWrapper)) {
                expect(TokenKind::Equals, "'=' after 'wrapper'");
                parse_sigil_name();
                if (check(TokenKind::LParen)) parse_call_args();
            } else {
                parse_sigil_name();
                expect(TokenKind::Equals, "'=' between sigil names");
                parse_sigil_name();
                if (check(TokenKind::LParen)) parse_call_args();
            }
            expect_newline();
            return;
        }
        if (match(TokenKind::KwPin)) {
            expect(TokenKind::Identifier, "pin name after 'pin'");
            expect(TokenKind::Equals, "'='");
            parse_path();
            expect_newline();
            return;
        }
        if (match(TokenKind::KwPinPrefer)) {
            expect(TokenKind::Identifier, "pin name after 'pin_prefer'");
            expect(TokenKind::Equals, "'='");
            parse_path();
            expect_newline();
            return;
        }
        if (match(TokenKind::KwDerive)) {
            expect(TokenKind::Identifier, "name after 'derive'");
            expect(TokenKind::Equals, "'='");
            parse_path();
            expect_newline();
            return;
        }
        if (check(TokenKind::KwHint)) {
            return parse_hint_stmt();
        }
        if (check(TokenKind::Newline)) {
            skip_newlines();
            return;
        }
        // Last resort: IDENT '=' path  (binds wrapper non-lane port)
        if (check(TokenKind::Identifier) && check_at(1, TokenKind::Equals)) {
            consume(); // IDENT
            consume(); // '='
            parse_path();
            expect_newline();
            return;
        }
        error_at(peek(), "unexpected token in connect-arguments block");
    }

    // -----------------------------------------------------------------
    // §9 parameter
    // -----------------------------------------------------------------
    void parse_parameter() {
        expect(TokenKind::KwParameter, "'parameter'");
        expect(TokenKind::Identifier, "parameter name");
        if (match(TokenKind::Colon)) parse_type_expr();
        // Optional domain_clause.
        if (check(TokenKind::KwIn) || check(TokenKind::LessEq) ||
            check(TokenKind::GreaterEq) || check(TokenKind::LAngle) ||
            check(TokenKind::RAngle)) {
            parse_domain_clause();
        }
        // Optional default_clause.
        if (match(TokenKind::Equals)) {
            parse_arg_value();
            if (looks_like_tolerance()) parse_param_tol();
        }
        // Optional param_domain_block: NEWLINE INDENT 'domain' '{' ... '}' DEDENT
        {
            std::size_t save = i_;
            if (try_open_block()) {
                if (check(TokenKind::Identifier) && peek().text == "domain") {
                    consume();
                    expect(TokenKind::LBrace, "'{' after 'domain'");
                    parse_arg_value();
                    while (match(TokenKind::Comma)) parse_arg_value();
                    expect(TokenKind::RBrace, "'}' to close domain");
                    expect_newline();
                    close_block();
                    return;
                }
                // Not a domain block after all; rewind.
                i_ = save;
            }
        }
        expect_newline();
    }

    void parse_domain_clause() {
        if (match(TokenKind::KwIn)) {
            parse_range_or_set();
        } else if (match(TokenKind::LessEq) || match(TokenKind::GreaterEq) ||
                   match(TokenKind::LAngle)  || match(TokenKind::RAngle)) {
            parse_arg_value();
        } else {
            error_at(peek(), "expected 'in', '<=' or '>=' for parameter domain");
        }
    }

    void parse_range_or_set() {
        // range_lit | '{' arg_value (',' arg_value)* '}'
        if (match(TokenKind::LBracket)) {
            parse_arg_value();
            expect(TokenKind::Comma, "',' between range bounds");
            parse_arg_value();
            expect(TokenKind::RBracket, "']' to close range");
            return;
        }
        if (match(TokenKind::LBrace)) {
            parse_arg_value();
            while (match(TokenKind::Comma)) parse_arg_value();
            expect(TokenKind::RBrace, "'}' to close set");
            return;
        }
        // Bare range form: arg_value '..' arg_value
        parse_arg_value();
        expect(TokenKind::DotDot, "'..' in range literal");
        parse_arg_value();
    }

    bool looks_like_tolerance() const {
        return check(TokenKind::PlusSlashMinus) || check(TokenKind::PlusMinus) ||
               check(TokenKind::Plus) || check(TokenKind::Minus);
    }
    void parse_param_tol() {
        if (match(TokenKind::PlusSlashMinus) || match(TokenKind::PlusMinus)) {
            parse_tol_amount();
            return;
        }
        // asym_tol ::= tol_side (',' tol_side)*
        parse_tol_side();
        while (match(TokenKind::Comma)) parse_tol_side();
    }
    void parse_tol_side() {
        if (!match(TokenKind::Plus) && !match(TokenKind::Minus)) {
            error_at(peek(), "expected '+' or '-' in tolerance side");
        }
        parse_tol_amount();
    }
    // tol_amount ::= NUMBER '%' | QUANTITY
    void parse_tol_amount() {
        if (!(check(TokenKind::IntegerLit) || check(TokenKind::NumberLit))) {
            error_at(peek(), "expected number in tolerance amount");
        }
        consume();
        // either '%' or an optional unit identifier
        if (match(TokenKind::Percent)) return;
        if (check(TokenKind::Identifier)) consume();
    }

    // arg_value ::= QUANTITY | NUMBER | STRING | BOOL | sigil_name | path | range_lit
    void parse_arg_value() {
        if (check(TokenKind::StringLit) ||
            check(TokenKind::KwTrue) || check(TokenKind::KwFalse)) {
            consume();
            return;
        }
        if (check(TokenKind::LBrace) || check(TokenKind::LBracket) ||
            check(TokenKind::LAngle)) {
            // sigil_name (or '[' range bracket — disambiguate)
            if (check(TokenKind::LBracket)) {
                // range_lit: '[' arg_value ',' arg_value ']'
                consume();
                parse_arg_value();
                expect(TokenKind::Comma, "',' between range bounds");
                parse_arg_value();
                expect(TokenKind::RBracket, "']' to close range literal");
                return;
            }
            parse_sigil_name();
            return;
        }
        // Numeric. Could become a QUANTITY (with optional SI/unit) or a
        // range_lit (NUMBER '..' NUMBER) or a percent.
        if (check(TokenKind::IntegerLit) || check(TokenKind::NumberLit) ||
            check(TokenKind::Minus)) {
            if (check(TokenKind::Minus)) consume();
            if (!(check(TokenKind::IntegerLit) || check(TokenKind::NumberLit))) {
                error_at(peek(), "expected a number after '-'");
            }
            consume();
            // Optional unit (one IDENT) or percent.
            if (check(TokenKind::Identifier)) {
                consume();
            } else if (match(TokenKind::Percent)) {
                /* ok */
            }
            // Optional range tail: '..' arg_value
            if (match(TokenKind::DotDot)) parse_arg_value();
            return;
        }
        // path
        if (check(TokenKind::Identifier)) {
            parse_path();
            return;
        }
        error_at(peek(), "expected an argument value");
    }

    // -----------------------------------------------------------------
    // type_expr — keywords + sigils + array dimensions
    // -----------------------------------------------------------------
    void parse_type_expr() {
        // Either an identifier that names a built-in type (we keep these
        // as IDENTs at lex time) or a sigil_name. Some type names happen
        // to be keywords in our lexer (`count`, `none`, etc.), so we
        // accept any ident-like token.
        if (check(TokenKind::LAngle)) {
            parse_sigil_circuit();
        } else if (check(TokenKind::LBrace) || check(TokenKind::LBracket)) {
            parse_sigil_name();
        } else if (is_ident_like(peek().kind)) {
            consume();
        } else {
            error_at(peek(), "expected a type");
        }
        // Array-dimension suffix(es).
        while (match(TokenKind::LBracket)) {
            expect(TokenKind::IntegerLit, "integer length in type");
            expect(TokenKind::RBracket, "']' to close array dimension");
        }
    }

    // -----------------------------------------------------------------
    // §10 constraints
    // -----------------------------------------------------------------
    void parse_constraint_stmt() {
        expect(TokenKind::KwConstraint, "'constraint'");
        parse_constraint_form();
        if (match(TokenKind::KwSoft)) {
            expect(TokenKind::IntegerLit, "integer weight after 'soft'");
        }
        expect_newline();
    }
    void parse_constraint_form() {
        // 'over' set_comp '{' expr '}'
        if (match(TokenKind::KwOver)) {
            expect(TokenKind::LBrace, "'{' for set comprehension");
            expect(TokenKind::Identifier, "loop var in set comprehension");
            expect(TokenKind::KwIn, "'in' in set comprehension");
            parse_path();
            expect(TokenKind::RBrace, "'}' to close set comprehension");
            expect(TokenKind::LBrace, "'{' to open over-body");
            parse_expr();
            expect(TokenKind::RBrace, "'}' to close over-body");
            return;
        }
        // sigil_circuit call_args -- invokes a check circuit
        if (check(TokenKind::LAngle) &&
            check_at(1, TokenKind::Identifier) &&
            check_at(2, TokenKind::RAngle) &&
            check_at(3, TokenKind::LParen)) {
            parse_sigil_circuit();
            parse_call_args();
            return;
        }
        // Optional constraint_name 'IDENT:' (no whitespace required).
        if (check(TokenKind::Identifier) && check_at(1, TokenKind::Colon)) {
            consume();   // name
            consume();   // ':'
        }
        parse_expr();
    }

    // -----------------------------------------------------------------
    // Expressions.  Precedence:
    //   or < and < not < comp < add < mul < pow < unary < primary
    // -----------------------------------------------------------------
    void parse_expr() { parse_or_expr(); }
    void parse_or_expr() {
        parse_and_expr();
        while (match(TokenKind::KwOr)) parse_and_expr();
    }
    void parse_and_expr() {
        parse_not_expr();
        while (match(TokenKind::KwAnd)) parse_not_expr();
    }
    void parse_not_expr() {
        if (match(TokenKind::KwNot)) { parse_not_expr(); return; }
        parse_comp_expr();
    }
    void parse_comp_expr() {
        // quant_expr or add_expr (comp_op add_expr)* or add_expr 'in' range_lit
        if (match(TokenKind::KwExists) || match(TokenKind::KwForall)) {
            expect(TokenKind::Identifier, "bound variable in quantifier");
            expect(TokenKind::Colon, "':' in quantifier");
            parse_sigil_name();
            expect(TokenKind::Dot, "'.' before quantifier body");
            parse_expr();
            return;
        }
        parse_add_expr();
        if (match(TokenKind::KwIn)) {
            parse_range_lit_or_set();
            return;
        }
        while (is_comp_op(peek().kind)) {
            consume();
            parse_add_expr();
        }
    }
    static bool is_comp_op(TokenKind k) {
        switch (k) {
            case TokenKind::EqualsEq:
            case TokenKind::BangEq:
            case TokenKind::LessEq:
            case TokenKind::GreaterEq:
            case TokenKind::LAngle:
            case TokenKind::RAngle:
                return true;
            default: return false;
        }
    }
    void parse_range_lit_or_set() {
        if (match(TokenKind::LBracket)) {
            parse_expr();
            expect(TokenKind::Comma, "',' between range bounds");
            parse_expr();
            expect(TokenKind::RBracket, "']' to close range");
        } else if (match(TokenKind::LBrace)) {
            parse_expr();
            while (match(TokenKind::Comma)) parse_expr();
            expect(TokenKind::RBrace, "'}' to close set");
        } else {
            // bare arg_value '..' arg_value
            parse_arg_value();
            if (match(TokenKind::DotDot)) parse_arg_value();
        }
    }
    void parse_add_expr() {
        parse_mul_expr();
        while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
            consume();
            parse_mul_expr();
        }
    }
    void parse_mul_expr() {
        parse_pow_expr();
        while (check(TokenKind::Star) || check(TokenKind::Slash)) {
            consume();
            parse_pow_expr();
        }
    }
    void parse_pow_expr() {
        parse_unary_expr();
        while (match(TokenKind::Caret)) parse_unary_expr();
    }
    void parse_unary_expr() {
        if (match(TokenKind::Minus)) { parse_unary_expr(); return; }
        parse_primary();
    }
    void parse_primary() {
        // QUANTITY / NUMBER / INT
        if (check(TokenKind::IntegerLit) || check(TokenKind::NumberLit)) {
            consume();
            // optional SI prefix and/or unit (one IDENT) and/or percent
            if (check(TokenKind::Identifier)) consume();
            (void)match(TokenKind::Percent);
            return;
        }
        if (check(TokenKind::StringLit) ||
            check(TokenKind::KwTrue) || check(TokenKind::KwFalse)) {
            consume();
            return;
        }
        // Aggregate calls (sum/max/min/count/avg).
        switch (peek().kind) {
            case TokenKind::KwSum:
            case TokenKind::KwMax:
            case TokenKind::KwMin:
            case TokenKind::KwCount:
            case TokenKind::KwAvg: {
                consume();
                expect(TokenKind::LParen, "'(' after aggregate");
                parse_expr_or_set();
                expect(TokenKind::RParen, "')' to close aggregate");
                return;
            }
            default: break;
        }
        if (match(TokenKind::LParen)) {
            parse_expr();
            expect(TokenKind::RParen, "')' to close parenthesised expression");
            return;
        }
        // Identifier-headed: path or function_call. Accept any ident-like.
        if (is_ident_like(peek().kind)) {
            consume();
            // function call?
            if (match(TokenKind::LParen)) {
                if (!check(TokenKind::RParen)) {
                    parse_expr();
                    while (match(TokenKind::Comma)) parse_expr();
                }
                expect(TokenKind::RParen, "')' to close function call");
            }
            // path tail
            while (true) {
                if (match(TokenKind::Dot)) {
                    if (check(TokenKind::LAngle)) {
                        parse_sigil_circuit();
                    } else {
                        consume_ident_like("name after '.'");
                    }
                } else if (match(TokenKind::LBracket)) {
                    parse_index_expr();
                    expect(TokenKind::RBracket, "']' to close index");
                } else if (match(TokenKind::LParen)) {
                    if (!check(TokenKind::RParen)) {
                        parse_expr();
                        while (match(TokenKind::Comma)) parse_expr();
                    }
                    expect(TokenKind::RParen, "')' to close function call");
                } else {
                    break;
                }
            }
            return;
        }
        // Sigil names can appear as values in some contexts.
        if (check(TokenKind::LBrace) || check(TokenKind::LBracket) ||
            check(TokenKind::LAngle)) {
            parse_sigil_name();
            return;
        }
        error_at(peek(), "expected an expression");
    }
    void parse_expr_or_set() {
        // set_comp_inline: IDENT 'in' path
        if (check(TokenKind::Identifier) && check_at(1, TokenKind::KwIn)) {
            consume(); consume();
            parse_path();
            return;
        }
        parse_expr();
    }

    // Range literal as a stand-alone production (used by some callers).
    void parse_range_lit() {
        if (match(TokenKind::LBracket)) {
            parse_arg_value();
            expect(TokenKind::Comma, "',' between range bounds");
            parse_arg_value();
            expect(TokenKind::RBracket, "']' to close range");
        } else {
            parse_arg_value();
            expect(TokenKind::DotDot, "'..' in range literal");
            parse_arg_value();
        }
    }

    // -----------------------------------------------------------------
    // §11 generators
    // -----------------------------------------------------------------
    void parse_generator_stmt() {
        expect(TokenKind::KwGenerate, "'generate'");
        if (match(TokenKind::KwFor)) {
            expect(TokenKind::Identifier, "loop variable in 'for'");
            expect(TokenKind::KwIn, "'in' after loop variable");
            parse_for_range();
            expect_newline();
            expect_indent();
            parse_body(TokenKind::LBrace); // sigil arg unused here
            close_block();
            return;
        }
        if (match(TokenKind::KwIf)) {
            parse_expr();
            expect_newline();
            expect_indent();
            parse_body(TokenKind::LBrace);
            close_block();
            // Optional else.
            skip_newlines();
            if (match(TokenKind::KwElse)) {
                expect_newline();
                expect_indent();
                parse_body(TokenKind::LBrace);
                close_block();
            }
            return;
        }
        if (match(TokenKind::KwMatch)) {
            parse_path();
            expect_newline();
            expect_indent();
            while (!at_dedent_or_eof()) {
                try {
                    if (check(TokenKind::Newline)) { skip_newlines(); continue; }
                    expect(TokenKind::KwCase, "'case' in generator match");
                    parse_case_pattern();
                    expect(TokenKind::Colon, "':' after case pattern");
                    expect_newline();
                    expect_indent();
                    parse_body(TokenKind::LBrace);
                    close_block();
                } catch (ParseError const& e) {
                    diag_.error(std::string(filename_), e.where(), e.what());
                    synchronize_in_block();
                }
                skip_newlines();
            }
            close_block();
            return;
        }
        error_at(peek(), "expected 'for', 'if' or 'match' after 'generate'");
    }
    void parse_for_range() {
        if (check(TokenKind::LBrace)) {
            consume();
            parse_arg_value();
            while (match(TokenKind::Comma)) parse_arg_value();
            expect(TokenKind::RBrace, "'}' to close set");
            return;
        }
        if (check(TokenKind::LBracket)) {
            consume();
            parse_expr();
            expect(TokenKind::Comma, "',' between range bounds");
            parse_expr();
            expect(TokenKind::RBracket, "']' to close range");
            return;
        }
        // A bare range or a path. Either way, both endpoints (and a
        // singleton) can be a full expression. This lets us accept
        // `0 .. n_bypass - 1` which the strict EBNF doesn't.
        parse_expr();
        if (match(TokenKind::DotDot)) parse_expr();
    }
    void parse_case_pattern() {
        if (check(TokenKind::LBrace) || check(TokenKind::LBracket) ||
            check(TokenKind::LAngle)) {
            parse_sigil_name();
            return;
        }
        if (check(TokenKind::IntegerLit) || check(TokenKind::NumberLit) ||
            check(TokenKind::StringLit) ||
            check(TokenKind::KwTrue) || check(TokenKind::KwFalse) ||
            check(TokenKind::Minus)) {
            parse_arg_value();
            return;
        }
        parse_path();
    }

    // -----------------------------------------------------------------
    // §12 alias
    //
    // The EBNF restricts the LHS to a sigil_name and the RHS to a
    // sigil_name (with optional call_args). The examples additionally use
    // alias for lane- and port-renaming, e.g. `alias MOSI = IO0` and
    // `alias vio_port = vdd_main`. We accept both forms.
    // -----------------------------------------------------------------
    void parse_alias_stmt() {
        expect(TokenKind::KwAlias, "'alias'");
        if (check(TokenKind::LBrace) || check(TokenKind::LBracket) ||
            check(TokenKind::LAngle)) {
            parse_sigil_name();
            expect(TokenKind::Equals, "'=' in alias");
            parse_sigil_name();
            if (check(TokenKind::LParen)) parse_call_args();
        } else {
            // Plain alias of a path.
            expect(TokenKind::Identifier, "alias name");
            expect(TokenKind::Equals, "'=' in alias");
            parse_path();
        }
        if (match(TokenKind::KwWhere)) parse_expr();
        expect_newline();
    }

    // -----------------------------------------------------------------
    // §13 prefer
    // -----------------------------------------------------------------
    void parse_prefer_stmt() {
        expect(TokenKind::KwPrefer, "'prefer'");
        // Form (b): 'wrapper' '=' sigil_name ...
        if (match(TokenKind::KwWrapper)) {
            expect(TokenKind::Equals, "'=' after 'wrapper'");
            parse_sigil_name();
            if (check(TokenKind::LParen)) parse_call_args();
            if (match(TokenKind::KwWeight)) {
                expect(TokenKind::IntegerLit, "integer weight");
            }
            expect_newline();
            return;
        }
        // Form (a): prefer_bind ::= sigil_name '=' sigil_name ...
        // Form (c): prefer expr ('soft' INT)?
        // We may need to skip past a sigil_name to see if '=' follows.
        if (check(TokenKind::LBrace) || check(TokenKind::LBracket) ||
            check(TokenKind::LAngle)) {
            std::size_t after = scan_past_sigil(i_);
            if (after != i_ && after < toks_.size() &&
                toks_[after].kind == TokenKind::Equals) {
                parse_sigil_name();
                expect(TokenKind::Equals, "'=' in prefer binding");
                parse_sigil_name();
                if (check(TokenKind::LParen)) parse_call_args();
                if (match(TokenKind::KwSoft)) {
                    expect(TokenKind::IntegerLit, "integer weight after 'soft'");
                }
                expect_newline();
                return;
            }
        }
        parse_expr();
        if (match(TokenKind::KwSoft)) {
            expect(TokenKind::IntegerLit, "integer weight after 'soft'");
        }
        expect_newline();
    }

    // Returns the index just past the matching close of a sigil pair that
    // begins at `from`. Returns `from` (no advance) if the token at
    // `from` is not a sigil open. Returns toks_.size() on unterminated.
    std::size_t scan_past_sigil(std::size_t from) const {
        if (from >= toks_.size()) return from;
        TokenKind open = toks_[from].kind;
        TokenKind close;
        switch (open) {
            case TokenKind::LBrace:   close = TokenKind::RBrace;   break;
            case TokenKind::LBracket: close = TokenKind::RBracket; break;
            case TokenKind::LAngle:   close = TokenKind::RAngle;   break;
            default:                  return from;
        }
        std::size_t j = from + 1;
        int depth = 1;
        while (j < toks_.size() && depth > 0) {
            if (toks_[j].kind == open)      ++depth;
            else if (toks_[j].kind == close) --depth;
            else if (toks_[j].kind == TokenKind::Newline) return toks_.size();
            ++j;
        }
        return j;
    }

    // -----------------------------------------------------------------
    // §14 designators
    // -----------------------------------------------------------------
    void parse_designators_stmt() {
        expect(TokenKind::KwDesignators, "'designators'");
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                if (match(TokenKind::KwStartAt)) {
                    expect(TokenKind::Identifier, "prefix after 'start_at'");
                    expect(TokenKind::Equals, "'=' after prefix");
                    expect(TokenKind::IntegerLit, "integer after '='");
                    expect_newline();
                } else if (match(TokenKind::KwReserve)) {
                    expect(TokenKind::Identifier, "prefix after 'reserve'");
                    expect(TokenKind::Equals, "'=' after prefix");
                    expect(TokenKind::LBrace, "'{' to start reserved list");
                    parse_int_or_range_list();
                    expect(TokenKind::RBrace, "'}' to close reserved list");
                    expect_newline();
                } else if (match(TokenKind::KwPrefix)) {
                    expect(TokenKind::Identifier, "kind name after 'prefix'");
                    expect(TokenKind::Equals, "'=' after kind name");
                    expect(TokenKind::Identifier, "designator prefix");
                    expect_newline();
                } else if (check(TokenKind::Newline)) {
                    skip_newlines();
                } else {
                    error_at(peek(), "expected 'start_at', 'reserve' or 'prefix' in designators");
                }
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }
    void parse_int_or_range_list() {
        auto one = [&] {
            expect(TokenKind::IntegerLit, "integer");
            if (match(TokenKind::DotDot)) {
                expect(TokenKind::IntegerLit, "integer after '..'");
            }
        };
        one();
        while (match(TokenKind::Comma)) one();
    }

    void parse_designators_lock_stmt() {
        expect(TokenKind::KwDesignatorsLock, "'designators_lock'");
        parse_sigil_circuit();
        expect_newline();
        expect_indent();
        while (!at_dedent_or_eof()) {
            try {
                if (check(TokenKind::Newline)) { skip_newlines(); continue; }
                expect(TokenKind::Identifier, "designator binding name");
                expect(TokenKind::Equals, "'=' in lock binding");
                parse_path();
                expect_newline();
            } catch (ParseError const& e) {
                diag_.error(std::string(filename_), e.where(), e.what());
                synchronize_in_block();
            }
            skip_newlines();
        }
        close_block();
    }

    // -----------------------------------------------------------------
    // §15 hints
    //
    // EBNF (strict):
    //     hint_stmt ::= 'hint' IDENT hint_targets? hint_text? hint_block?
    //     hint_attr ::= IDENT arg_value NEWLINE
    //
    // We additionally accept `IDENT '=' arg_value` for hint_attr (used in
    // the eval_board example: `level = critical`) and inline single-attr
    // hints of the form `hint kind attr = value`.
    // -----------------------------------------------------------------
    void parse_hint_stmt() {
        expect(TokenKind::KwHint, "'hint'");
        expect(TokenKind::Identifier, "hint kind");
        // Optional inline single attribute: IDENT '=' arg_value
        if (check(TokenKind::Identifier) && check_at(1, TokenKind::Equals)) {
            consume(); // attr name
            consume(); // '='
            parse_arg_value();
            expect_newline();
            return;
        }
        // hint_targets?
        if (looks_like_hint_target()) {
            parse_hint_target();
            while (match(TokenKind::Comma)) parse_hint_target();
        }
        // hint_text?
        if (check(TokenKind::StringLit)) consume();
        // hint_block?
        if (try_open_block()) {
            while (!at_dedent_or_eof()) {
                try {
                    if (check(TokenKind::Newline)) { skip_newlines(); continue; }
                    expect(TokenKind::Identifier, "attribute name in hint block");
                    // Either 'IDENT arg_value' (EBNF) or 'IDENT = arg_value'.
                    (void)match(TokenKind::Equals);
                    parse_arg_value();
                    expect_newline();
                } catch (ParseError const& e) {
                    diag_.error(std::string(filename_), e.where(), e.what());
                    synchronize_in_block();
                }
                skip_newlines();
            }
            close_block();
        } else {
            expect_newline();
        }
    }

    bool looks_like_hint_target() const {
        return check(TokenKind::Identifier) ||
               check(TokenKind::LBrace) ||
               check(TokenKind::LBracket) ||
               check(TokenKind::LAngle);
    }
    void parse_hint_target() {
        if (check(TokenKind::LBrace)) {
            // set_literal
            consume();
            parse_connect_term();
            while (match(TokenKind::Comma)) parse_connect_term();
            expect(TokenKind::RBrace, "'}' to close hint target set");
            return;
        }
        if (check(TokenKind::LBracket) || check(TokenKind::LAngle)) {
            parse_sigil_name();
            return;
        }
        parse_path();
    }

    // -----------------------------------------------------------------
    // §16 use
    // -----------------------------------------------------------------
    void parse_use_stmt() {
        expect(TokenKind::KwUse, "'use'");
        parse_sigil_circuit();
        if (check(TokenKind::LParen)) parse_call_args();
        expect_newline();
    }

    // -----------------------------------------------------------------
    // §17 path  (extended in two directions:
    //    * the head may be any ident-like token (covers `bank(...)`,
    //      `count(...)`, `mcu.spi1.bank` etc.);
    //    * a path may contain mid-path function-call segments `(args)`. )
    // -----------------------------------------------------------------
    void parse_path() {
        consume_ident_like("identifier (start of path)");
        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) {
                parse_expr();
                while (match(TokenKind::Comma)) parse_expr();
            }
            expect(TokenKind::RParen, "')' to close call");
        }
        while (true) {
            if (match(TokenKind::Dot)) {
                if (check(TokenKind::LAngle)) {
                    parse_sigil_circuit();
                } else {
                    consume_ident_like("name after '.'");
                }
            } else if (match(TokenKind::LBracket)) {
                parse_index_expr();
                expect(TokenKind::RBracket, "']' after index");
            } else if (match(TokenKind::LParen)) {
                if (!check(TokenKind::RParen)) {
                    parse_expr();
                    while (match(TokenKind::Comma)) parse_expr();
                }
                expect(TokenKind::RParen, "')' to close call");
            } else {
                break;
            }
        }
    }
    void parse_index_expr() {
        if (check(TokenKind::IntegerLit) || check(TokenKind::NumberLit)) {
            consume();
            if (match(TokenKind::DotDot)) {
                if (check(TokenKind::IntegerLit) || check(TokenKind::NumberLit))
                    consume();
                else error_at(peek(), "expected integer after '..'");
            }
            return;
        }
        if (check(TokenKind::Identifier)) { consume(); return; }
        error_at(peek(), "expected integer, identifier, or range as index");
    }

    // -----------------------------------------------------------------
    // call_args
    // -----------------------------------------------------------------
    void parse_call_args() {
        expect(TokenKind::LParen, "'('");
        if (!check(TokenKind::RParen)) {
            parse_call_arg();
            while (match(TokenKind::Comma)) parse_call_arg();
        }
        expect(TokenKind::RParen, "')'");
    }
    void parse_call_arg() {
        // IDENT '=' arg_value | arg_value
        if (check(TokenKind::Identifier) && check_at(1, TokenKind::Equals)) {
            consume();   // IDENT
            consume();   // '='
        }
        parse_arg_value();
    }

    // -----------------------------------------------------------------
    // §3.2 override / remove
    // -----------------------------------------------------------------
    void parse_override_stmt() {
        expect(TokenKind::KwOverride, "'override'");
        // override_stmt ::= 'override' body_stmt
        parse_body_stmt(TokenKind::LBrace);
    }
    void parse_remove_stmt() {
        expect(TokenKind::KwRemove, "'remove'");
        parse_remove_target();
        expect_newline();
    }
    void parse_remove_target() {
        switch (peek().kind) {
            case TokenKind::KwPin:
            case TokenKind::KwParameter:
            case TokenKind::KwLane:
            case TokenKind::KwRole:
            case TokenKind::KwPort:
            case TokenKind::KwBus:
            case TokenKind::KwBank:
            case TokenKind::KwConstraint:
            case TokenKind::KwSplice:
            case TokenKind::KwDerive:
                consume();
                expect(TokenKind::Identifier, "identifier after remove target");
                return;
            case TokenKind::KwView:
                consume();
                parse_sigil_circuit();
                if (check(TokenKind::Identifier)) consume();
                return;
            case TokenKind::KwPackage:
                consume();
                parse_sigil_pkg();
                return;
            case TokenKind::KwProvides:
                consume();
                parse_sigil_circuit();
                expect(TokenKind::KwAs, "'as' after provides target");
                expect(TokenKind::Identifier, "role name");
                return;
            case TokenKind::Identifier:
                // 'instance' IDENT — but 'instance' isn't a keyword for us.
                // Accept any IDENT as a recovery target.
                consume();
                if (check(TokenKind::Identifier)) consume();
                return;
            case TokenKind::KwResolution:
                consume();
                return;
            case TokenKind::KwHint:
                consume();
                expect(TokenKind::Identifier, "hint kind");
                if (looks_like_hint_target()) {
                    parse_hint_target();
                    while (match(TokenKind::Comma)) parse_hint_target();
                }
                return;
            case TokenKind::KwParent:
                consume();
                parse_sigil_name();
                if (match(TokenKind::Dot)) parse_remove_target();
                return;
            default:
                error_at(peek(), "expected a remove target");
        }
    }

    // -----------------------------------------------------------------
    // §18 escape hatches (`assume`)
    // -----------------------------------------------------------------
    void parse_assume_stmt() {
        expect(TokenKind::KwAssume, "'assume'");
        parse_expr();
        expect_newline();
    }
};

} // namespace

bool parse_file(std::string_view filename,
                const std::vector<Token>& tokens,
                DiagnosticBag& diag) {
    Parser p(filename, tokens, diag);
    return p.parse_file();
}

} // namespace schemlang
