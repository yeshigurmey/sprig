// The abstract syntax tree: the program as a tree of nodes instead of flat text.
//
// Design note: every node stores a `kind` tag and later stages `switch` on it,
// then static_cast to the concrete struct. This is the same approach LLVM and
// Clang use. The alternative is the Visitor pattern with virtual functions.
// The tag approach keeps each pass (type checker, optimizer, code generator)
// in one file as one readable switch, which is easier to follow and to debug.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "diagnostics.h"

namespace sprig {

// Sprig has two value types. Void is only for functions that return nothing.
// Error marks an expression that already failed type checking, so we do not
// report a second, confusing error about the same mistake.
enum class Type { Void, Int, Bool, Error };

const char* typeName(Type type);

// ---------------------------------------------------------------- expressions

struct Expr {
    enum class Kind { IntLit, BoolLit, Var, Unary, Binary, Call };

    virtual ~Expr() = default;

    Kind kind;
    SourceLoc loc;
    Type type = Type::Error;  // filled in by the type checker

protected:
    Expr(Kind k, SourceLoc where) : kind(k), loc(where) {}
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLit : Expr {
    IntLit(std::int64_t v, SourceLoc where) : Expr(Kind::IntLit, where), value(v) {}
    std::int64_t value;
};

struct BoolLit : Expr {
    BoolLit(bool v, SourceLoc where) : Expr(Kind::BoolLit, where), value(v) {}
    bool value;
};

struct VarExpr : Expr {
    VarExpr(std::string n, SourceLoc where) : Expr(Kind::Var, where), name(std::move(n)) {}
    std::string name;
    int slot = -1;  // index of this variable in its function's stack frame (set by the type checker)
};

enum class UnaryOp { Neg, Not };

struct UnaryExpr : Expr {
    UnaryExpr(UnaryOp o, ExprPtr e, SourceLoc where)
        : Expr(Kind::Unary, where), op(o), operand(std::move(e)) {}
    UnaryOp op;
    ExprPtr operand;
};

enum class BinaryOp { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, And, Or };

const char* binaryOpSymbol(BinaryOp op);

struct BinaryExpr : Expr {
    BinaryExpr(BinaryOp o, ExprPtr l, ExprPtr r, SourceLoc where)
        : Expr(Kind::Binary, where), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
    BinaryOp op;
    ExprPtr lhs;
    ExprPtr rhs;
};

struct CallExpr : Expr {
    CallExpr(std::string name, std::vector<ExprPtr> a, SourceLoc where)
        : Expr(Kind::Call, where), callee(std::move(name)), args(std::move(a)) {}
    std::string callee;
    std::vector<ExprPtr> args;
    int fnIndex = -1;  // which function this calls (set by the type checker)
};

// ----------------------------------------------------------------- statements

struct Stmt {
    enum class Kind { Let, Assign, If, While, Return, Print, ExprStmt, Block };

    virtual ~Stmt() = default;

    Kind kind;
    SourceLoc loc;

protected:
    Stmt(Kind k, SourceLoc where) : kind(k), loc(where) {}
};
using StmtPtr = std::unique_ptr<Stmt>;

struct BlockStmt : Stmt {
    explicit BlockStmt(SourceLoc where) : Stmt(Kind::Block, where) {}
    std::vector<StmtPtr> stmts;
};

struct LetStmt : Stmt {
    LetStmt(std::string n, bool annotated, Type t, ExprPtr e, SourceLoc where)
        : Stmt(Kind::Let, where), name(std::move(n)), hasAnnotation(annotated), varType(t), init(std::move(e)) {}
    std::string name;
    bool hasAnnotation;  // true for `let x: int = ...`, false for `let x = ...`
    Type varType;        // from the annotation, or inferred from the initializer
    ExprPtr init;
    int slot = -1;
};

struct AssignStmt : Stmt {
    AssignStmt(std::string n, ExprPtr e, SourceLoc where)
        : Stmt(Kind::Assign, where), name(std::move(n)), value(std::move(e)) {}
    std::string name;
    ExprPtr value;
    int slot = -1;
};

struct IfStmt : Stmt {
    IfStmt(ExprPtr c, StmtPtr t, StmtPtr e, SourceLoc where)
        : Stmt(Kind::If, where), cond(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}
    ExprPtr cond;
    StmtPtr thenBranch;  // always a block
    StmtPtr elseBranch;  // a block, another if (for `else if`), or null
};

struct WhileStmt : Stmt {
    WhileStmt(ExprPtr c, StmtPtr b, SourceLoc where)
        : Stmt(Kind::While, where), cond(std::move(c)), body(std::move(b)) {}
    ExprPtr cond;
    StmtPtr body;  // always a block
};

struct ReturnStmt : Stmt {
    ReturnStmt(ExprPtr e, SourceLoc where) : Stmt(Kind::Return, where), value(std::move(e)) {}
    ExprPtr value;  // null for a bare `return;`
};

struct PrintStmt : Stmt {
    PrintStmt(ExprPtr e, SourceLoc where) : Stmt(Kind::Print, where), value(std::move(e)) {}
    ExprPtr value;
};

struct ExprStmt : Stmt {
    ExprStmt(ExprPtr e, SourceLoc where) : Stmt(Kind::ExprStmt, where), expr(std::move(e)) {}
    ExprPtr expr;
};

// ------------------------------------------------------------------ top level

struct Param {
    std::string name;
    Type type = Type::Error;
    SourceLoc loc;
};

struct FunctionDecl {
    std::string name;
    std::vector<Param> params;
    Type returnType = Type::Void;
    std::unique_ptr<BlockStmt> body;
    SourceLoc loc;
    int numLocals = 0;  // parameters + every `let` in the body (set by the type checker)
};

struct Program {
    std::vector<std::unique_ptr<FunctionDecl>> functions;
    // Statements written outside any function. The parser wraps them in a
    // hidden function called "<main>" so later stages only deal with functions.
    std::unique_ptr<FunctionDecl> main;
};

// Prints the tree as nested parentheses, for example `(let x (+ 1 (* 2 3)))`.
// Used by --dump-ast, and by the tests to check what the parser and optimizer produced.
std::string printProgram(const Program& program);
std::string printExpr(const Expr& expr);

}  // namespace sprig
