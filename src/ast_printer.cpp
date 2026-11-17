#include <sstream>

#include "ast.h"

namespace sprig {

const char* typeName(Type type) {
    switch (type) {
        case Type::Void: return "void";
        case Type::Int: return "int";
        case Type::Bool: return "bool";
        case Type::Error: return "<error>";
    }
    return "?";
}

const char* binaryOpSymbol(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*";
        case BinaryOp::Div: return "/";
        case BinaryOp::Mod: return "%";
        case BinaryOp::Eq: return "==";
        case BinaryOp::Ne: return "!=";
        case BinaryOp::Lt: return "<";
        case BinaryOp::Le: return "<=";
        case BinaryOp::Gt: return ">";
        case BinaryOp::Ge: return ">=";
        case BinaryOp::And: return "&&";
        case BinaryOp::Or: return "||";
    }
    return "?";
}

namespace {

void writeExpr(std::ostream& out, const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::IntLit:
            out << static_cast<const IntLit&>(expr).value;
            break;
        case Expr::Kind::BoolLit:
            out << (static_cast<const BoolLit&>(expr).value ? "true" : "false");
            break;
        case Expr::Kind::Var:
            out << static_cast<const VarExpr&>(expr).name;
            break;
        case Expr::Kind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            out << '(' << (unary.op == UnaryOp::Neg ? "-" : "!") << ' ';
            writeExpr(out, *unary.operand);
            out << ')';
            break;
        }
        case Expr::Kind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            out << '(' << binaryOpSymbol(binary.op) << ' ';
            writeExpr(out, *binary.lhs);
            out << ' ';
            writeExpr(out, *binary.rhs);
            out << ')';
            break;
        }
        case Expr::Kind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            out << "(call " << call.callee;
            for (const auto& arg : call.args) {
                out << ' ';
                writeExpr(out, *arg);
            }
            out << ')';
            break;
        }
    }
}

void writeStmt(std::ostream& out, const Stmt& stmt) {
    switch (stmt.kind) {
        case Stmt::Kind::Let: {
            const auto& let = static_cast<const LetStmt&>(stmt);
            out << "(let " << let.name << ' ';
            writeExpr(out, *let.init);
            out << ')';
            break;
        }
        case Stmt::Kind::Assign: {
            const auto& assign = static_cast<const AssignStmt&>(stmt);
            out << "(set " << assign.name << ' ';
            writeExpr(out, *assign.value);
            out << ')';
            break;
        }
        case Stmt::Kind::If: {
            const auto& ifStmt = static_cast<const IfStmt&>(stmt);
            out << "(if ";
            writeExpr(out, *ifStmt.cond);
            out << ' ';
            writeStmt(out, *ifStmt.thenBranch);
            if (ifStmt.elseBranch) {
                out << ' ';
                writeStmt(out, *ifStmt.elseBranch);
            }
            out << ')';
            break;
        }
        case Stmt::Kind::While: {
            const auto& loop = static_cast<const WhileStmt&>(stmt);
            out << "(while ";
            writeExpr(out, *loop.cond);
            out << ' ';
            writeStmt(out, *loop.body);
            out << ')';
            break;
        }
        case Stmt::Kind::Return: {
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            out << "(return";
            if (ret.value) {
                out << ' ';
                writeExpr(out, *ret.value);
            }
            out << ')';
            break;
        }
        case Stmt::Kind::Print:
            out << "(print ";
            writeExpr(out, *static_cast<const PrintStmt&>(stmt).value);
            out << ')';
            break;
        case Stmt::Kind::ExprStmt:
            writeExpr(out, *static_cast<const ExprStmt&>(stmt).expr);
            break;
        case Stmt::Kind::Block: {
            const auto& block = static_cast<const BlockStmt&>(stmt);
            out << "(block";
            for (const auto& child : block.stmts) {
                out << ' ';
                writeStmt(out, *child);
            }
            out << ')';
            break;
        }
    }
}

void writeFunction(std::ostream& out, const FunctionDecl& fn) {
    out << "(fn " << fn.name << " (";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) out << ' ';
        out << fn.params[i].name << ':' << typeName(fn.params[i].type);
    }
    out << ") " << typeName(fn.returnType) << ' ';
    writeStmt(out, *fn.body);
    out << ')';
}

}  // namespace

std::string printExpr(const Expr& expr) {
    std::ostringstream out;
    writeExpr(out, expr);
    return out.str();
}

std::string printProgram(const Program& program) {
    std::ostringstream out;
    for (const auto& fn : program.functions) {
        writeFunction(out, *fn);
        out << '\n';
    }
    // Top level statements are printed one per line without the hidden wrapper.
    for (const auto& stmt : program.main->body->stmts) {
        writeStmt(out, *stmt);
        out << '\n';
    }
    return out.str();
}

}  // namespace sprig
