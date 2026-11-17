// Stage 1: source text -> tokens.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "token.h"

namespace sprig {

class Lexer {
public:
    explicit Lexer(std::string source) : src_(std::move(source)) {}

    // Turns the whole file into tokens. The last token is always Eof, which
    // lets the parser look ahead without checking for the end every time.
    // Throws SyntaxError on a character or number it cannot handle.
    std::vector<Token> tokenize();

private:
    Token next();
    void skipWhitespaceAndComments();
    Token makeToken(TokenKind kind, std::size_t start, SourceLoc loc) const;
    Token number(std::size_t start, SourceLoc loc);
    Token identifierOrKeyword(std::size_t start, SourceLoc loc);

    bool atEnd() const { return pos_ >= src_.size(); }
    char peek() const { return atEnd() ? '\0' : src_[pos_]; }
    char peekNext() const { return pos_ + 1 < src_.size() ? src_[pos_ + 1] : '\0'; }
    char advance();
    bool match(char expected);

    std::string src_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
};

}  // namespace sprig
