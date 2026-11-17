// Stage 2: tokens -> abstract syntax tree.
#pragma once

#include <memory>
#include <vector>

#include "ast.h"
#include "token.h"

namespace sprig {

// A hand written recursive descent parser: one function per grammar rule.
// Throws SyntaxError at the first token that does not fit the grammar.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::unique_ptr<Program> parseProgram();

private:
    // Declarations and statements
    std::unique_ptr<FunctionDecl> function();
    StmtPtr statement();
    std::unique_ptr<BlockStmt> block();
    StmtPtr letStatement();
    StmtPtr ifStatement();
    StmtPtr whileStatement();
    StmtPtr returnStatement();
    StmtPtr printStatement();
    Type parseType();

    // Expressions
    ExprPtr expression();
    ExprPtr binary(int minPrecedence);
    ExprPtr unary();
    ExprPtr primary();

    // Token helpers
    const Token& peek() const { return tokens_[pos_]; }
    const Token& peekNext() const;
    bool check(TokenKind kind) const { return peek().kind == kind; }
    bool match(TokenKind kind);
    const Token& advance();
    const Token& expect(TokenKind kind, const char* context);

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

}  // namespace sprig
