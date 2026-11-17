#include "lexer.h"

#include <cctype>
#include <limits>
#include <unordered_map>

namespace sprig {

const char* tokenKindName(TokenKind kind) {
    switch (kind) {
        case TokenKind::Int: return "integer";
        case TokenKind::Ident: return "identifier";
        case TokenKind::Fn: return "'fn'";
        case TokenKind::Let: return "'let'";
        case TokenKind::If: return "'if'";
        case TokenKind::Else: return "'else'";
        case TokenKind::While: return "'while'";
        case TokenKind::Return: return "'return'";
        case TokenKind::Print: return "'print'";
        case TokenKind::True: return "'true'";
        case TokenKind::False: return "'false'";
        case TokenKind::KwInt: return "'int'";
        case TokenKind::KwBool: return "'bool'";
        case TokenKind::LParen: return "'('";
        case TokenKind::RParen: return "')'";
        case TokenKind::LBrace: return "'{'";
        case TokenKind::RBrace: return "'}'";
        case TokenKind::Comma: return "','";
        case TokenKind::Colon: return "':'";
        case TokenKind::Semicolon: return "';'";
        case TokenKind::Arrow: return "'->'";
        case TokenKind::Plus: return "'+'";
        case TokenKind::Minus: return "'-'";
        case TokenKind::Star: return "'*'";
        case TokenKind::Slash: return "'/'";
        case TokenKind::Percent: return "'%'";
        case TokenKind::Bang: return "'!'";
        case TokenKind::Assign: return "'='";
        case TokenKind::EqEq: return "'=='";
        case TokenKind::BangEq: return "'!='";
        case TokenKind::Lt: return "'<'";
        case TokenKind::LtEq: return "'<='";
        case TokenKind::Gt: return "'>'";
        case TokenKind::GtEq: return "'>='";
        case TokenKind::AmpAmp: return "'&&'";
        case TokenKind::PipePipe: return "'||'";
        case TokenKind::Eof: return "end of file";
    }
    return "?";
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    for (;;) {
        Token tok = next();
        const bool done = tok.kind == TokenKind::Eof;
        tokens.push_back(std::move(tok));
        if (done) break;
    }
    return tokens;
}

char Lexer::advance() {
    const char c = src_[pos_++];
    if (c == '\n') {
        ++line_;
        col_ = 1;
    } else {
        ++col_;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() != expected) return false;
    advance();
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peekNext() == '/') {
            // Line comment: throw away everything up to the newline.
            while (!atEnd() && peek() != '\n') advance();
        } else {
            return;
        }
    }
}

Token Lexer::makeToken(TokenKind kind, std::size_t start, SourceLoc loc) const {
    Token tok;
    tok.kind = kind;
    tok.text = src_.substr(start, pos_ - start);
    tok.loc = loc;
    return tok;
}

Token Lexer::number(std::size_t start, SourceLoc loc) {
    // Build the value digit by digit and check for overflow before each step.
    // Doing the check first matters: signed overflow is undefined behaviour in
    // C++, so we must never compute a value that does not fit.
    const std::int64_t max = std::numeric_limits<std::int64_t>::max();
    std::int64_t value = src_[start] - '0';
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
        const int digit = advance() - '0';
        if (value > (max - digit) / 10) {
            throw SyntaxError(loc, "integer literal is too large for a 64-bit int");
        }
        value = value * 10 + digit;
    }
    if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') {
        throw SyntaxError(loc, "a name cannot start with a digit");
    }
    Token tok = makeToken(TokenKind::Int, start, loc);
    tok.value = value;
    return tok;
}

Token Lexer::identifierOrKeyword(std::size_t start, SourceLoc loc) {
    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"fn", TokenKind::Fn},         {"let", TokenKind::Let},
        {"if", TokenKind::If},         {"else", TokenKind::Else},
        {"while", TokenKind::While},   {"return", TokenKind::Return},
        {"print", TokenKind::Print},   {"true", TokenKind::True},
        {"false", TokenKind::False},   {"int", TokenKind::KwInt},
        {"bool", TokenKind::KwBool},
    };

    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();

    Token tok = makeToken(TokenKind::Ident, start, loc);
    const auto it = keywords.find(tok.text);
    if (it != keywords.end()) tok.kind = it->second;
    return tok;
}

Token Lexer::next() {
    skipWhitespaceAndComments();

    const SourceLoc loc{line_, col_};
    const std::size_t start = pos_;
    if (atEnd()) return makeToken(TokenKind::Eof, start, loc);

    const char c = advance();
    if (std::isdigit(static_cast<unsigned char>(c))) return number(start, loc);
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return identifierOrKeyword(start, loc);

    switch (c) {
        case '(': return makeToken(TokenKind::LParen, start, loc);
        case ')': return makeToken(TokenKind::RParen, start, loc);
        case '{': return makeToken(TokenKind::LBrace, start, loc);
        case '}': return makeToken(TokenKind::RBrace, start, loc);
        case ',': return makeToken(TokenKind::Comma, start, loc);
        case ':': return makeToken(TokenKind::Colon, start, loc);
        case ';': return makeToken(TokenKind::Semicolon, start, loc);
        case '+': return makeToken(TokenKind::Plus, start, loc);
        case '*': return makeToken(TokenKind::Star, start, loc);
        case '/': return makeToken(TokenKind::Slash, start, loc);
        case '%': return makeToken(TokenKind::Percent, start, loc);
        // The rest need one character of lookahead: "-" vs "->", "=" vs "==", and so on.
        case '-': return makeToken(match('>') ? TokenKind::Arrow : TokenKind::Minus, start, loc);
        case '!': return makeToken(match('=') ? TokenKind::BangEq : TokenKind::Bang, start, loc);
        case '=': return makeToken(match('=') ? TokenKind::EqEq : TokenKind::Assign, start, loc);
        case '<': return makeToken(match('=') ? TokenKind::LtEq : TokenKind::Lt, start, loc);
        case '>': return makeToken(match('=') ? TokenKind::GtEq : TokenKind::Gt, start, loc);
        case '&':
            if (match('&')) return makeToken(TokenKind::AmpAmp, start, loc);
            throw SyntaxError(loc, "expected '&&' (a single '&' is not an operator)");
        case '|':
            if (match('|')) return makeToken(TokenKind::PipePipe, start, loc);
            throw SyntaxError(loc, "expected '||' (a single '|' is not an operator)");
        default:
            break;
    }

    throw SyntaxError(loc, std::string("unexpected character '") + c + "'");
}

}  // namespace sprig
