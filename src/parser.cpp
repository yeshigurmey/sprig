#include "parser.h"

#include <string>

namespace sprig {

namespace {

// Binary operator table. A higher number binds tighter, so `1 + 2 * 3` groups
// as `1 + (2 * 3)` because `*` (6) beats `+` (5). Returns 0 for "not a binary operator".
int precedenceOf(TokenKind kind) {
    switch (kind) {
        case TokenKind::PipePipe: return 1;
        case TokenKind::AmpAmp: return 2;
        case TokenKind::EqEq:
        case TokenKind::BangEq: return 3;
        case TokenKind::Lt:
        case TokenKind::LtEq:
        case TokenKind::Gt:
        case TokenKind::GtEq: return 4;
        case TokenKind::Plus:
        case TokenKind::Minus: return 5;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent: return 6;
        default: return 0;
    }
}

BinaryOp binaryOpFor(TokenKind kind) {
    switch (kind) {
        case TokenKind::PipePipe: return BinaryOp::Or;
        case TokenKind::AmpAmp: return BinaryOp::And;
        case TokenKind::EqEq: return BinaryOp::Eq;
        case TokenKind::BangEq: return BinaryOp::Ne;
        case TokenKind::Lt: return BinaryOp::Lt;
        case TokenKind::LtEq: return BinaryOp::Le;
        case TokenKind::Gt: return BinaryOp::Gt;
        case TokenKind::GtEq: return BinaryOp::Ge;
        case TokenKind::Plus: return BinaryOp::Add;
        case TokenKind::Minus: return BinaryOp::Sub;
        case TokenKind::Star: return BinaryOp::Mul;
        case TokenKind::Slash: return BinaryOp::Div;
        default: return BinaryOp::Mod;
    }
}

}  // namespace

// ------------------------------------------------------------- token helpers

const Token& Parser::peekNext() const {
    // The token list always ends with Eof, so clamping to the last index is safe.
    const std::size_t next = pos_ + 1 < tokens_.size() ? pos_ + 1 : tokens_.size() - 1;
    return tokens_[next];
}

const Token& Parser::advance() {
    const Token& tok = tokens_[pos_];
    if (tok.kind != TokenKind::Eof) ++pos_;
    return tok;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) return false;
    advance();
    return true;
}

const Token& Parser::expect(TokenKind kind, const char* context) {
    if (check(kind)) return advance();
    throw SyntaxError(peek().loc, std::string("expected ") + tokenKindName(kind) + " " + context +
                                      ", but found " + tokenKindName(peek().kind));
}

// ------------------------------------------------------------------ top level

// program := (function | statement)* EOF
std::unique_ptr<Program> Parser::parseProgram() {
    auto program = std::make_unique<Program>();
    program->main = std::make_unique<FunctionDecl>();
    program->main->name = "<main>";
    program->main->returnType = Type::Void;
    program->main->loc = peek().loc;
    program->main->body = std::make_unique<BlockStmt>(peek().loc);

    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Fn)) {
            program->functions.push_back(function());
        } else {
            program->main->body->stmts.push_back(statement());
        }
    }
    return program;
}

// function := "fn" IDENT "(" (param ("," param)*)? ")" ("->" type)? block
// param    := IDENT ":" type
std::unique_ptr<FunctionDecl> Parser::function() {
    auto fn = std::make_unique<FunctionDecl>();
    fn->loc = expect(TokenKind::Fn, "to start a function").loc;
    fn->name = expect(TokenKind::Ident, "for the function name").text;

    expect(TokenKind::LParen, "after the function name");
    if (!check(TokenKind::RParen)) {
        do {
            Param param;
            const Token& name = expect(TokenKind::Ident, "for a parameter name");
            param.name = name.text;
            param.loc = name.loc;
            expect(TokenKind::Colon, "after the parameter name (parameters need a type, like `n: int`)");
            param.type = parseType();
            fn->params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "after the parameters");

    fn->returnType = match(TokenKind::Arrow) ? parseType() : Type::Void;
    fn->body = block();
    return fn;
}

// type := "int" | "bool"
Type Parser::parseType() {
    if (match(TokenKind::KwInt)) return Type::Int;
    if (match(TokenKind::KwBool)) return Type::Bool;
    throw SyntaxError(peek().loc,
                      std::string("expected a type ('int' or 'bool'), but found ") + tokenKindName(peek().kind));
}

// ----------------------------------------------------------------- statements

StmtPtr Parser::statement() {
    switch (peek().kind) {
        case TokenKind::Let: return letStatement();
        case TokenKind::If: return ifStatement();
        case TokenKind::While: return whileStatement();
        case TokenKind::Return: return returnStatement();
        case TokenKind::Print: return printStatement();
        case TokenKind::LBrace: return block();
        case TokenKind::Fn:
            throw SyntaxError(peek().loc, "functions can only be declared at the top level of the file");
        default:
            break;
    }

    // `x = expr;` and `x + 1;` both start with a name, so one token is not
    // enough to tell them apart. Looking one token further ahead settles it.
    if (check(TokenKind::Ident) && peekNext().kind == TokenKind::Assign) {
        const Token& name = advance();
        const SourceLoc loc = name.loc;
        std::string varName = name.text;
        advance();  // the '='
        ExprPtr value = expression();
        expect(TokenKind::Semicolon, "after the assignment");
        return std::make_unique<AssignStmt>(std::move(varName), std::move(value), loc);
    }

    const SourceLoc loc = peek().loc;
    ExprPtr expr = expression();
    expect(TokenKind::Semicolon, "after the expression");
    return std::make_unique<ExprStmt>(std::move(expr), loc);
}

// block := "{" statement* "}"
std::unique_ptr<BlockStmt> Parser::block() {
    const Token& open = expect(TokenKind::LBrace, "to start a block");
    auto result = std::make_unique<BlockStmt>(open.loc);
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        result->stmts.push_back(statement());
    }
    expect(TokenKind::RBrace, "to close the block");
    return result;
}

// let := "let" IDENT (":" type)? "=" expression ";"
StmtPtr Parser::letStatement() {
    const SourceLoc loc = advance().loc;  // 'let'
    std::string name = expect(TokenKind::Ident, "after 'let'").text;

    bool annotated = false;
    Type declared = Type::Error;
    if (match(TokenKind::Colon)) {
        annotated = true;
        declared = parseType();
    }

    expect(TokenKind::Assign, "in the 'let' statement (every variable needs a starting value)");
    ExprPtr init = expression();
    expect(TokenKind::Semicolon, "after the 'let' statement");
    return std::make_unique<LetStmt>(std::move(name), annotated, declared, std::move(init), loc);
}

// if := "if" expression block ("else" (if | block))?
StmtPtr Parser::ifStatement() {
    const SourceLoc loc = advance().loc;  // 'if'
    ExprPtr cond = expression();
    StmtPtr thenBranch = block();
    StmtPtr elseBranch;
    if (match(TokenKind::Else)) {
        elseBranch = check(TokenKind::If) ? ifStatement() : StmtPtr(block());
    }
    return std::make_unique<IfStmt>(std::move(cond), std::move(thenBranch), std::move(elseBranch), loc);
}

// while := "while" expression block
StmtPtr Parser::whileStatement() {
    const SourceLoc loc = advance().loc;  // 'while'
    ExprPtr cond = expression();
    StmtPtr body = block();
    return std::make_unique<WhileStmt>(std::move(cond), std::move(body), loc);
}

// return := "return" expression? ";"
StmtPtr Parser::returnStatement() {
    const SourceLoc loc = advance().loc;  // 'return'
    ExprPtr value;
    if (!check(TokenKind::Semicolon)) value = expression();
    expect(TokenKind::Semicolon, "after the 'return' statement");
    return std::make_unique<ReturnStmt>(std::move(value), loc);
}

// print := "print" "(" expression ")" ";"
StmtPtr Parser::printStatement() {
    const SourceLoc loc = advance().loc;  // 'print'
    expect(TokenKind::LParen, "after 'print'");
    ExprPtr value = expression();
    expect(TokenKind::RParen, "after the value to print");
    expect(TokenKind::Semicolon, "after the 'print' statement");
    return std::make_unique<PrintStmt>(std::move(value), loc);
}

// ---------------------------------------------------------------- expressions

ExprPtr Parser::expression() { return binary(1); }

// Precedence climbing. Parse one operand, then keep absorbing operators as long
// as they bind at least as tightly as `minPrecedence`. The right hand side is
// parsed with `precedence + 1`, which is what makes every operator left
// associative: `10 - 4 - 3` becomes `(10 - 4) - 3`, not `10 - (4 - 3)`.
ExprPtr Parser::binary(int minPrecedence) {
    ExprPtr lhs = unary();
    for (;;) {
        const int precedence = precedenceOf(peek().kind);
        if (precedence == 0 || precedence < minPrecedence) return lhs;

        const Token& opToken = advance();
        const BinaryOp op = binaryOpFor(opToken.kind);
        const SourceLoc loc = opToken.loc;
        ExprPtr rhs = binary(precedence + 1);
        lhs = std::make_unique<BinaryExpr>(op, std::move(lhs), std::move(rhs), loc);
    }
}

// unary := ("-" | "!") unary | primary
ExprPtr Parser::unary() {
    if (check(TokenKind::Minus) || check(TokenKind::Bang)) {
        const Token& opToken = advance();
        const UnaryOp op = opToken.kind == TokenKind::Minus ? UnaryOp::Neg : UnaryOp::Not;
        const SourceLoc loc = opToken.loc;
        ExprPtr operand = unary();
        return std::make_unique<UnaryExpr>(op, std::move(operand), loc);
    }
    return primary();
}

// primary := INT | "true" | "false" | IDENT | IDENT "(" args ")" | "(" expression ")"
ExprPtr Parser::primary() {
    const Token& tok = peek();
    switch (tok.kind) {
        case TokenKind::Int: {
            advance();
            return std::make_unique<IntLit>(tok.value, tok.loc);
        }
        case TokenKind::True:
            advance();
            return std::make_unique<BoolLit>(true, tok.loc);
        case TokenKind::False:
            advance();
            return std::make_unique<BoolLit>(false, tok.loc);
        case TokenKind::Ident: {
            const SourceLoc loc = tok.loc;
            std::string name = tok.text;
            advance();
            if (!match(TokenKind::LParen)) return std::make_unique<VarExpr>(std::move(name), loc);

            std::vector<ExprPtr> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(expression());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "after the arguments");
            return std::make_unique<CallExpr>(std::move(name), std::move(args), loc);
        }
        case TokenKind::LParen: {
            advance();
            ExprPtr inner = expression();
            expect(TokenKind::RParen, "to close the parenthesis");
            return inner;
        }
        default:
            throw SyntaxError(tok.loc, std::string("expected an expression, but found ") + tokenKindName(tok.kind));
    }
}

}  // namespace sprig
