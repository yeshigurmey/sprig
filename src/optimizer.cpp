#include "optimizer.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "arith.h"

namespace sprig {

namespace {

bool isLiteral(const Expr& expr) {
    return expr.kind == Expr::Kind::IntLit || expr.kind == Expr::Kind::BoolLit;
}

bool isIntLit(const Expr& expr, std::int64_t value) {
    return expr.kind == Expr::Kind::IntLit && static_cast<const IntLit&>(expr).value == value;
}

bool isBoolLit(const Expr& expr, bool value) {
    return expr.kind == Expr::Kind::BoolLit && static_cast<const BoolLit&>(expr).value == value;
}

// Bools are folded as 0 or 1 so one helper can read either kind of literal.
std::int64_t literalValue(const Expr& expr) {
    if (expr.kind == Expr::Kind::IntLit) return static_cast<const IntLit&>(expr).value;
    return static_cast<const BoolLit&>(expr).value ? 1 : 0;
}

ExprPtr makeInt(std::int64_t value, SourceLoc loc) {
    auto lit = std::make_unique<IntLit>(value, loc);
    lit->type = Type::Int;
    return lit;
}

ExprPtr makeBool(bool value, SourceLoc loc) {
    auto lit = std::make_unique<BoolLit>(value, loc);
    lit->type = Type::Bool;
    return lit;
}

// A value known at compile time.
struct Constant {
    Type type;
    std::int64_t value;
};

// Optimizes one function. Slots are numbered per function, so the bookkeeping
// below has to be per function too.
class FunctionOptimizer {
public:
    explicit FunctionOptimizer(OptStats& stats) : stats_(stats) {}

    void run(FunctionDecl& fn) {
        collectReassigned(*fn.body);
        optimizeBlock(*fn.body);
    }

private:
    // ---------------------------------------------------- which variables change?

    // Constant propagation is only safe for a variable that keeps its first
    // value forever. So before touching anything, find every variable that is
    // the target of an assignment anywhere in the function.
    void collectReassigned(const Stmt& stmt) {
        switch (stmt.kind) {
            case Stmt::Kind::Assign:
                reassigned_.insert(static_cast<const AssignStmt&>(stmt).slot);
                break;
            case Stmt::Kind::Block:
                for (const auto& child : static_cast<const BlockStmt&>(stmt).stmts) collectReassigned(*child);
                break;
            case Stmt::Kind::If: {
                const auto& ifStmt = static_cast<const IfStmt&>(stmt);
                collectReassigned(*ifStmt.thenBranch);
                if (ifStmt.elseBranch) collectReassigned(*ifStmt.elseBranch);
                break;
            }
            case Stmt::Kind::While:
                collectReassigned(*static_cast<const WhileStmt&>(stmt).body);
                break;
            default:
                break;
        }
    }

    // ------------------------------------------------------------ expressions

    // Takes ownership of an expression and returns the (possibly new) optimized
    // one. Children are optimized first, so by the time a node is examined its
    // operands are already as simple as they can get.
    ExprPtr optimizeExpr(ExprPtr expr) {
        switch (expr->kind) {
            case Expr::Kind::IntLit:
            case Expr::Kind::BoolLit:
                return expr;

            case Expr::Kind::Var: {
                const auto known = constants_.find(static_cast<const VarExpr&>(*expr).slot);
                if (known == constants_.end()) return expr;
                ++stats_.constantsPropagated;
                return known->second.type == Type::Int ? makeInt(known->second.value, expr->loc)
                                                       : makeBool(known->second.value != 0, expr->loc);
            }

            case Expr::Kind::Unary: {
                auto& unary = static_cast<UnaryExpr&>(*expr);
                unary.operand = optimizeExpr(std::move(unary.operand));
                if (!isLiteral(*unary.operand)) return expr;
                ++stats_.expressionsFolded;
                const std::int64_t value = literalValue(*unary.operand);
                return unary.op == UnaryOp::Neg ? makeInt(arith::neg(value), expr->loc)
                                                : makeBool(value == 0, expr->loc);
            }

            case Expr::Kind::Binary:
                return optimizeBinary(std::move(expr));

            case Expr::Kind::Call: {
                auto& call = static_cast<CallExpr&>(*expr);
                for (auto& arg : call.args) arg = optimizeExpr(std::move(arg));
                return expr;
            }
        }
        return expr;
    }

    ExprPtr optimizeBinary(ExprPtr expr) {
        auto& binary = static_cast<BinaryExpr&>(*expr);
        binary.lhs = optimizeExpr(std::move(binary.lhs));
        binary.rhs = optimizeExpr(std::move(binary.rhs));
        return simplifyBinary(std::move(expr));
    }

    // Simplifies one binary node whose operands are already optimized.
    ExprPtr simplifyBinary(ExprPtr expr) {
        auto& binary = static_cast<BinaryExpr&>(*expr);
        const SourceLoc loc = expr->loc;

        // Both sides known: just compute the answer now.
        if (isLiteral(*binary.lhs) && isLiteral(*binary.rhs)) {
            ExprPtr folded = foldLiterals(binary.op, literalValue(*binary.lhs), literalValue(*binary.rhs), loc);
            if (!folded) return expr;
            ++stats_.expressionsFolded;
            return folded;
        }

        const bool commutative = binary.op == BinaryOp::Add || binary.op == BinaryOp::Mul;

        // Put the constant on the right (`2 * x` becomes `x * 2`) so the rules
        // below only have to look for it in one place. Swapping is safe
        // because evaluating a literal has no effect that could be reordered.
        if (commutative && isLiteral(*binary.lhs)) std::swap(binary.lhs, binary.rhs);

        // Reassociation: `(x * 60) * 24` becomes `x * 1440`.
        // This is only correct because Sprig ints wrap on overflow, which keeps
        // + and * associative. In a language where overflow is an error it
        // would be wrong: the regrouped version could overflow at a different point.
        if (commutative && isLiteral(*binary.rhs) && binary.lhs->kind == Expr::Kind::Binary) {
            auto& inner = static_cast<BinaryExpr&>(*binary.lhs);
            if (inner.op == binary.op && isLiteral(*inner.rhs)) {
                const std::int64_t a = literalValue(*inner.rhs);
                const std::int64_t b = literalValue(*binary.rhs);
                inner.rhs = makeInt(binary.op == BinaryOp::Add ? arith::add(a, b) : arith::mul(a, b), loc);
                ++stats_.expressionsFolded;
                // The merged constant might be 0 or 1, so run the rules again on the result.
                return simplifyBinary(std::move(binary.lhs));
            }
        }

        // One side known: some operations still simplify. Each rule below only
        // drops an expression when doing so cannot skip a function call or a
        // possible division by zero.
        const Expr& lhs = *binary.lhs;
        const Expr& rhs = *binary.rhs;
        switch (binary.op) {
            case BinaryOp::And:
                // `false && x` never evaluates x at run time, so x can go.
                if (isBoolLit(lhs, false)) return simplified(makeBool(false, loc));
                if (isBoolLit(lhs, true)) return simplified(std::move(binary.rhs));
                if (isBoolLit(rhs, true)) return simplified(std::move(binary.lhs));
                break;
            case BinaryOp::Or:
                if (isBoolLit(lhs, true)) return simplified(makeBool(true, loc));
                if (isBoolLit(lhs, false)) return simplified(std::move(binary.rhs));
                if (isBoolLit(rhs, false)) return simplified(std::move(binary.lhs));
                break;
            case BinaryOp::Add:
            case BinaryOp::Sub:
                if (isIntLit(rhs, 0)) return simplified(std::move(binary.lhs));
                break;
            case BinaryOp::Mul:
                // Note there is no `x * 0 -> 0` rule: x might be a call that prints something.
                if (isIntLit(rhs, 1)) return simplified(std::move(binary.lhs));
                break;
            case BinaryOp::Div:
                if (isIntLit(rhs, 1)) return simplified(std::move(binary.lhs));
                break;
            default:
                break;
        }
        return expr;
    }

    ExprPtr simplified(ExprPtr result) {
        ++stats_.expressionsFolded;
        return result;
    }

    // Returns null when the operation must be left for run time.
    static ExprPtr foldLiterals(BinaryOp op, std::int64_t a, std::int64_t b, SourceLoc loc) {
        switch (op) {
            case BinaryOp::Add: return makeInt(arith::add(a, b), loc);
            case BinaryOp::Sub: return makeInt(arith::sub(a, b), loc);
            case BinaryOp::Mul: return makeInt(arith::mul(a, b), loc);
            case BinaryOp::Div:
                // Dividing by zero is a runtime error. Folding it would either
                // crash the compiler or hide the error, so leave it alone.
                if (b == 0) return nullptr;
                return makeInt(arith::div(a, b), loc);
            case BinaryOp::Mod:
                if (b == 0) return nullptr;
                return makeInt(arith::mod(a, b), loc);
            case BinaryOp::Eq: return makeBool(a == b, loc);
            case BinaryOp::Ne: return makeBool(a != b, loc);
            case BinaryOp::Lt: return makeBool(a < b, loc);
            case BinaryOp::Le: return makeBool(a <= b, loc);
            case BinaryOp::Gt: return makeBool(a > b, loc);
            case BinaryOp::Ge: return makeBool(a >= b, loc);
            case BinaryOp::And: return makeBool(a != 0 && b != 0, loc);
            case BinaryOp::Or: return makeBool(a != 0 || b != 0, loc);
        }
        return nullptr;
    }

    // ------------------------------------------------------------- statements

    void optimizeBlock(BlockStmt& block) {
        std::vector<StmtPtr> kept;
        kept.reserve(block.stmts.size());
        bool returned = false;
        for (auto& stmt : block.stmts) {
            if (returned) {
                // Anything after a `return` in the same block can never run.
                ++stats_.statementsRemoved;
                continue;
            }
            StmtPtr optimized = optimizeStmt(std::move(stmt));
            if (!optimized) continue;
            returned = optimized->kind == Stmt::Kind::Return;
            kept.push_back(std::move(optimized));
        }
        block.stmts = std::move(kept);
    }

    // Returns the optimized statement, or null if the statement was removed.
    StmtPtr optimizeStmt(StmtPtr stmt) {
        switch (stmt->kind) {
            case Stmt::Kind::Let: {
                auto& let = static_cast<LetStmt&>(*stmt);
                let.init = optimizeExpr(std::move(let.init));
                if (isLiteral(*let.init) && reassigned_.count(let.slot) == 0) {
                    // This variable is a constant in disguise. Remember its
                    // value so every later read is replaced by it. A variable
                    // can only be read after its `let`, so every read will be
                    // replaced, which means the `let` itself is no longer needed.
                    constants_[let.slot] = Constant{let.init->type, literalValue(*let.init)};
                    ++stats_.statementsRemoved;
                    return nullptr;
                }
                return stmt;
            }
            case Stmt::Kind::Assign: {
                auto& assign = static_cast<AssignStmt&>(*stmt);
                assign.value = optimizeExpr(std::move(assign.value));
                return stmt;
            }
            case Stmt::Kind::If: {
                auto& ifStmt = static_cast<IfStmt&>(*stmt);
                ifStmt.cond = optimizeExpr(std::move(ifStmt.cond));
                if (isLiteral(*ifStmt.cond)) {
                    // The condition is known, so only one branch can ever run.
                    ++stats_.statementsRemoved;
                    if (isBoolLit(*ifStmt.cond, true)) return optimizeStmt(std::move(ifStmt.thenBranch));
                    if (ifStmt.elseBranch) return optimizeStmt(std::move(ifStmt.elseBranch));
                    return nullptr;
                }
                ifStmt.thenBranch = optimizeStmt(std::move(ifStmt.thenBranch));
                if (ifStmt.elseBranch) ifStmt.elseBranch = optimizeStmt(std::move(ifStmt.elseBranch));
                return stmt;
            }
            case Stmt::Kind::While: {
                auto& loop = static_cast<WhileStmt&>(*stmt);
                loop.cond = optimizeExpr(std::move(loop.cond));
                if (isBoolLit(*loop.cond, false)) {
                    ++stats_.statementsRemoved;
                    return nullptr;
                }
                loop.body = optimizeStmt(std::move(loop.body));
                return stmt;
            }
            case Stmt::Kind::Return: {
                auto& ret = static_cast<ReturnStmt&>(*stmt);
                if (ret.value) ret.value = optimizeExpr(std::move(ret.value));
                return stmt;
            }
            case Stmt::Kind::Print: {
                auto& print = static_cast<PrintStmt&>(*stmt);
                print.value = optimizeExpr(std::move(print.value));
                return stmt;
            }
            case Stmt::Kind::ExprStmt: {
                auto& exprStmt = static_cast<ExprStmt&>(*stmt);
                exprStmt.expr = optimizeExpr(std::move(exprStmt.expr));
                // A bare value such as `5;` or `x;` does nothing at all.
                if (isLiteral(*exprStmt.expr) || exprStmt.expr->kind == Expr::Kind::Var) {
                    ++stats_.statementsRemoved;
                    return nullptr;
                }
                return stmt;
            }
            case Stmt::Kind::Block:
                optimizeBlock(static_cast<BlockStmt&>(*stmt));
                return stmt;
        }
        return stmt;
    }

    OptStats& stats_;
    std::unordered_set<int> reassigned_;
    std::unordered_map<int, Constant> constants_;
};

}  // namespace

OptStats optimize(Program& program) {
    OptStats stats;
    for (auto& fn : program.functions) FunctionOptimizer(stats).run(*fn);
    FunctionOptimizer(stats).run(*program.main);
    return stats;
}

}  // namespace sprig
