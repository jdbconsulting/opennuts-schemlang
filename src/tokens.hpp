// tokens.hpp -- token kinds and the Token type produced by the lexer.
//
// The lexer first emits "raw" tokens (no layout) for keywords, identifiers,
// numbers, strings, and operators. A separate layout pass (layout.cpp) then
// inserts Newline/Indent/Dedent tokens per Python-style indentation rules so
// that the parser can match the grammar verbatim from docs/schemlang.ebnf.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace schemlang {

// One source-text position. `offset` is into the *original* source buffer;
// `line` and `column` are 1-based for human-readable diagnostics.
struct SourcePos {
    std::uint32_t offset = 0;
    std::uint32_t line   = 1;
    std::uint32_t column = 1;
};

// Every token kind the language knows about. The exact set is determined by
// the EBNF in docs/schemlang.ebnf. We keep keyword recognition in the lexer
// (faster, and the parser can dispatch on Kind only).
enum class TokenKind : std::uint16_t {
    // -- Special / layout ----------------------------------------------------
    EndOfFile,
    Newline,
    Indent,
    Dedent,

    // -- Literals / names ----------------------------------------------------
    Identifier,
    IntegerLit,
    NumberLit,    // floating-point or integer with frac/exp; never carries unit
    StringLit,    // contents stored already unescaped

    // -- Punctuation ---------------------------------------------------------
    LBrace,       // '{'
    RBrace,       // '}'
    LBracket,     // '['
    RBracket,     // ']'
    LParen,       // '('
    RParen,       // ')'
    LAngle,       // '<'  (also used as comparison; parser handles ambiguity)
    RAngle,       // '>'  (also used as comparison)
    Comma,        // ','
    Colon,        // ':'
    Dot,          // '.'
    At,           // '@'
    Equals,       // '='

    // -- Multi-character operators ------------------------------------------
    EqualsEq,     // '=='
    BangEq,       // '!='
    LessEq,       // '<='
    GreaterEq,    // '>='
    BidirArrow,   // '<->'
    FatArrow,     // '=>'
    DotDot,       // '..'
    Plus,         // '+'
    Minus,        // '-'
    Star,         // '*'
    Slash,        // '/'
    Caret,        // '^'
    Percent,      // '%'
    PlusSlashMinus,   // '+/-'   (canonical symmetric tolerance)
    PlusMinus,        // '+-'    (alternative spelling of '+/-')

    // -- Keywords ------------------------------------------------------------
    // Keep in the same group order as the EBNF for ease of cross-reference.
    KwInclude,
    KwAs,
    KwOnly,
    KwDefine,
    KwAlias,
    KwWhere,
    KwPrefer,
    KwWrapper,
    KwSoft,
    KwWeight,
    KwDesignators,
    KwDesignatorsLock,
    KwStartAt,
    KwReserve,
    KwPrefix,
    KwDesignator,
    KwDesignatorPrefix,
    KwDescription,
    KwHint,
    KwConstraint,
    KwOver,
    KwGenerate,
    KwFor,
    KwIf,
    KwElse,
    KwMatch,
    KwCase,
    KwDefault,
    KwIn,
    KwUse,
    KwPin,
    KwSwapGroup,
    KwRawPin,
    KwAssume,
    KwView,
    KwLane,
    KwRole,
    KwPort,
    KwBus,
    KwBank,
    KwPins,
    KwProvides,
    KwPool,
    KwPerBus,
    KwParameter,
    KwOverride,
    KwRemove,
    KwPackage,         // only as a remove_target tag in EBNF
    KwParent,
    KwDerive,
    KwResolution,
    KwDrives,
    KwReceives,
    KwBidir,
    KwCardinality,
    KwExtends,
    KwSplice,
    KwWith,
    KwHostSide,        // 'host_side' (only meaningful in lane_endpoint)
    KwPeriSide,        // 'peri_side'
    KwAnd,
    KwOr,
    KwNot,
    KwExists,
    KwForall,
    KwSum,
    KwMax,
    KwMin,
    KwCount,
    KwAvg,
    KwAny,
    KwAll,
    KwNone,
    KwPrefers,
    KwTrue,
    KwFalse,
    KwPinPrefer,       // 'pin_prefer'  (connect_arg)
};

const char* token_kind_name(TokenKind k);

// One token in the post-layout stream.
//   - kind:   what flavour it is
//   - text:   the original lexeme (for Identifier / IntegerLit / NumberLit /
//             StringLit this carries the value; for keywords/operators it is
//             just the source-form for diagnostics and for `--tokens` output)
//   - pos:    location of the *start* of the token in the source buffer
struct Token {
    TokenKind   kind;
    std::string text;
    SourcePos   pos;
};

} // namespace schemlang
