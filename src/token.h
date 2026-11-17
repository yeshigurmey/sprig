// Tokens are the "words" of the language. The lexer produces them, the parser consumes them.
#pragma once

#include <cstdint>
#include <string>

#include "diagnostics.h"

namespace sprig {

enum class TokenKind {
    // Things that carry a value
    Int,
    Ident,

    // Keywords
    Fn,
    Let,
    If,
    Else,
    While,
    Return,
    Print,
    True,
    False,
    KwInt,
    KwBool,

    // Punctuation
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Colon,
    Semicolon,
    Arrow,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Assign,
    EqEq,
    BangEq,
    Lt,
    LtEq,
    Gt,
    GtEq,
    AmpAmp,
    PipePipe,

    Eof,
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;       // exact characters from the source
    std::int64_t value = 0; // only meaningful when kind == Int
    SourceLoc loc;
};

// Human readable name, used in error messages and in --dump-tokens.
const char* tokenKindName(TokenKind kind);

}  // namespace sprig
