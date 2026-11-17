#include "sema.h"

#include <string>
#include <unordered_map>

namespace sprig {

namespace {

// The operand of a bytecode instruction that names a local is 16 bits wide.
constexpr int kMaxLocals = 65535;
// The argument count in a call instruction is 8 bits wide.
constexpr std::size_t kMaxParams = 255;

struct VarInfo {
    Type type;
    int slot;
};

struct FunctionInfo {
    int index;
    const FunctionDecl* decl;
};

class Analyzer {
public:
    explicit Analyzer(Program& program) : program_(program) {}

    std::vector<Diagnostic> run() {
        // Pass 1: record every function signature before looking inside any
        // body. This is what lets a function call another one that is defined
        // further down the file, and what makes recursion work.
        for (std::size_t i = 0; i < program_.functions.size(); ++i) {
            const FunctionDecl& fn = *program_.functions[i];
            if (!functions_.emplace(fn.name, FunctionInfo{static_cast<int>(i), &fn}).second) {
                error(fn.loc, "function '" + fn.name + "' is defined more than once");
            }
            if (fn.params.size() > kMaxParams) {
                error(fn.loc, "function '" + fn.name + "' has more than 255 parameters");
            }
        }

        // Pass 2: check each body.
        for (auto& fn : program_.functions) checkFunction(*fn);
        checkFunction(*program_.main);
        return std::move(diagnostics_);
    }

private:
    // ------------------------------------------------------------- functions

    void checkFunction(FunctionDecl& fn) {
        current_ = &fn;
        nextSlot_ = 0;
        scopes_.clear();
        scopes_.emplace_back();

        // Parameters take the first slots of the frame, in order. The VM relies
        // on this: the caller pushes the arguments and they become slots 0..n-1.
        for (const Param& param : fn.params) declare(param.name, param.type, param.loc);

        // The body shares the parameter scope, so `let n = ...` inside a
        // function that already has a parameter `n` is reported as a clash.
        for (auto& stmt : fn.body->stmts) checkStmt(*stmt);

        if (fn.returnType != Type::Void && !alwaysReturns(*fn.body)) {
            error(fn.loc, "function '" + fn.name + "' can reach the end without returning a value");
        }

        fn.numLocals = nextSlot_;
        scopes_.clear();
    }

    // True when every path through the statement ends in a `return`.
    // Deliberately simple: a `while` never counts, even `while true`, because
    // proving a loop always runs is a harder problem than this checker takes on.
    static bool alwaysReturns(const Stmt& stmt) {
        switch (stmt.kind) {
            case Stmt::Kind::Return:
                return true;
            case Stmt::Kind::Block:
                for (const auto& child : static_cast<const BlockStmt&>(stmt).stmts) {
                    if (alwaysReturns(*child)) return true;
                }
                return false;
            case Stmt::Kind::If: {
                const auto& ifStmt = static_cast<const IfStmt&>(stmt);
                return ifStmt.elseBranch && alwaysReturns(*ifStmt.thenBranch) && alwaysReturns(*ifStmt.elseBranch);
            }
            default:
                return false;
        }
    }

    // ---------------------------------------------------------------- scopes

    int declare(const std::string& name, Type type, SourceLoc loc) {
        if (scopes_.back().count(name) != 0) {
            error(loc, "'" + name + "' is already declared in this scope");
        }
        if (nextSlot_ >= kMaxLocals) {
            error(loc, "too many local variables in one function");
            return 0;
        }
        const int slot = nextSlot_++;
        // On a clash the newer declaration wins, which avoids a pile of follow up errors.
        scopes_.back()[name] = VarInfo{type, slot};
        return slot;
    }

    // Searches from the innermost scope outwards, so an inner variable hides
    // an outer one with the same name.
    const VarInfo* lookup(const std::string& name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        return nullptr;
    }

    // ------------------------------------------------------------ statements

    void checkBlock(BlockStmt& block) {
        scopes_.emplace_back();
        for (auto& stmt : block.stmts) checkStmt(*stmt);
        scopes_.pop_back();
    }

    void checkCondition(Expr& cond, const char* what) {
        const Type type = checkExpr(cond);
        if (type != Type::Bool && type != Type::Error) {
            error(cond.loc, std::string("the condition of ") + what + " must be a bool, but this is " +
                                describe(type));
        }
    }

    void checkStmt(Stmt& stmt) {
        switch (stmt.kind) {
            case Stmt::Kind::Let: {
                auto& let = static_cast<LetStmt&>(stmt);
                // Check the initializer before declaring the name, so that
                // `let x = x + 1;` refers to an outer x (or is an error), never to itself.
                const Type initType = checkExpr(*let.init);
                if (initType == Type::Void) {
                    error(let.init->loc, "cannot store the result of a function that returns nothing");
                } else if (let.hasAnnotation && initType != Type::Error && initType != let.varType) {
                    error(let.init->loc, "'" + let.name + "' is declared as " + typeName(let.varType) +
                                             ", but the value is " + describe(initType));
                }
                if (!let.hasAnnotation) let.varType = initType == Type::Void ? Type::Error : initType;
                let.slot = declare(let.name, let.varType, let.loc);
                break;
            }
            case Stmt::Kind::Assign: {
                auto& assign = static_cast<AssignStmt&>(stmt);
                const Type valueType = checkExpr(*assign.value);
                const VarInfo* var = lookup(assign.name);
                if (!var) {
                    error(assign.loc, "cannot assign to '" + assign.name + "' because it is not declared (use 'let' first)");
                    break;
                }
                assign.slot = var->slot;
                if (valueType != Type::Error && var->type != Type::Error && valueType != var->type) {
                    error(assign.value->loc, "'" + assign.name + "' is " + describe(var->type) +
                                                 ", so it cannot be assigned " + describe(valueType));
                }
                break;
            }
            case Stmt::Kind::If: {
                auto& ifStmt = static_cast<IfStmt&>(stmt);
                checkCondition(*ifStmt.cond, "an 'if'");
                checkStmt(*ifStmt.thenBranch);
                if (ifStmt.elseBranch) checkStmt(*ifStmt.elseBranch);
                break;
            }
            case Stmt::Kind::While: {
                auto& loop = static_cast<WhileStmt&>(stmt);
                checkCondition(*loop.cond, "a 'while'");
                checkStmt(*loop.body);
                break;
            }
            case Stmt::Kind::Return: {
                auto& ret = static_cast<ReturnStmt&>(stmt);
                const Type expected = current_->returnType;
                if (!ret.value) {
                    if (expected != Type::Void) {
                        error(ret.loc, "function '" + current_->name + "' must return " + describe(expected));
                    }
                    break;
                }
                const Type actual = checkExpr(*ret.value);
                if (expected == Type::Void) {
                    error(ret.loc, current_ == program_.main.get()
                                       ? std::string("code outside a function cannot return a value")
                                       : "function '" + current_->name + "' does not return a value");
                } else if (actual != Type::Error && actual != expected) {
                    error(ret.value->loc, "function '" + current_->name + "' returns " + typeName(expected) +
                                              ", but this value is " + describe(actual));
                }
                break;
            }
            case Stmt::Kind::Print: {
                auto& print = static_cast<PrintStmt&>(stmt);
                if (checkExpr(*print.value) == Type::Void) {
                    error(print.value->loc, "cannot print the result of a function that returns nothing");
                }
                break;
            }
            case Stmt::Kind::ExprStmt:
                checkExpr(*static_cast<ExprStmt&>(stmt).expr);
                break;
            case Stmt::Kind::Block:
                checkBlock(static_cast<BlockStmt&>(stmt));
                break;
        }
    }

    // ----------------------------------------------------------- expressions

    // Works out the type of an expression, records it on the node, and returns it.
    Type checkExpr(Expr& expr) {
        expr.type = computeType(expr);
        return expr.type;
    }

    Type computeType(Expr& expr) {
        switch (expr.kind) {
            case Expr::Kind::IntLit:
                return Type::Int;
            case Expr::Kind::BoolLit:
                return Type::Bool;
            case Expr::Kind::Var: {
                auto& var = static_cast<VarExpr&>(expr);
                const VarInfo* info = lookup(var.name);
                if (!info) {
                    error(var.loc, "'" + var.name + "' is not declared");
                    return Type::Error;
                }
                var.slot = info->slot;
                return info->type;
            }
            case Expr::Kind::Unary: {
                auto& unary = static_cast<UnaryExpr&>(expr);
                const Type operand = checkExpr(*unary.operand);
                const Type wanted = unary.op == UnaryOp::Neg ? Type::Int : Type::Bool;
                if (operand == Type::Error) return wanted;
                if (operand != wanted) {
                    error(unary.loc, std::string("'") + (unary.op == UnaryOp::Neg ? "-" : "!") + "' needs " +
                                         describe(wanted) + ", but this is " + describe(operand));
                }
                return wanted;
            }
            case Expr::Kind::Binary:
                return checkBinary(static_cast<BinaryExpr&>(expr));
            case Expr::Kind::Call:
                return checkCall(static_cast<CallExpr&>(expr));
        }
        return Type::Error;
    }

    Type checkBinary(BinaryExpr& binary) {
        const Type lhs = checkExpr(*binary.lhs);
        const Type rhs = checkExpr(*binary.rhs);
        const bool broken = lhs == Type::Error || rhs == Type::Error;
        const std::string symbol = binaryOpSymbol(binary.op);

        switch (binary.op) {
            case BinaryOp::Add:
            case BinaryOp::Sub:
            case BinaryOp::Mul:
            case BinaryOp::Div:
            case BinaryOp::Mod:
                requireBoth(binary, lhs, rhs, Type::Int, broken);
                return Type::Int;
            case BinaryOp::Lt:
            case BinaryOp::Le:
            case BinaryOp::Gt:
            case BinaryOp::Ge:
                requireBoth(binary, lhs, rhs, Type::Int, broken);
                return Type::Bool;
            case BinaryOp::And:
            case BinaryOp::Or:
                requireBoth(binary, lhs, rhs, Type::Bool, broken);
                return Type::Bool;
            case BinaryOp::Eq:
            case BinaryOp::Ne:
                // Equality works on ints or on bools, as long as both sides agree.
                if (!broken && (lhs != rhs || lhs == Type::Void)) {
                    error(binary.loc, "'" + symbol + "' cannot compare " + describe(lhs) + " with " + describe(rhs));
                }
                return Type::Bool;
        }
        return Type::Error;
    }

    void requireBoth(const BinaryExpr& binary, Type lhs, Type rhs, Type wanted, bool broken) {
        if (broken || (lhs == wanted && rhs == wanted)) return;
        error(binary.loc, std::string("'") + binaryOpSymbol(binary.op) + "' needs " + typeName(wanted) +
                              " on both sides, but got " + describe(lhs) + " and " + describe(rhs));
    }

    Type checkCall(CallExpr& call) {
        // Check the arguments first so their own mistakes are reported even if the call is bad.
        std::vector<Type> argTypes;
        argTypes.reserve(call.args.size());
        for (auto& arg : call.args) argTypes.push_back(checkExpr(*arg));

        const auto found = functions_.find(call.callee);
        if (found == functions_.end()) {
            error(call.loc, "function '" + call.callee + "' is not defined");
            return Type::Error;
        }

        const FunctionDecl& target = *found->second.decl;
        call.fnIndex = found->second.index;

        if (call.args.size() != target.params.size()) {
            error(call.loc, "function '" + call.callee + "' takes " + std::to_string(target.params.size()) +
                                " argument(s), but " + std::to_string(call.args.size()) + " were given");
            return target.returnType;
        }
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            if (argTypes[i] != Type::Error && argTypes[i] != target.params[i].type) {
                error(call.args[i]->loc, "argument " + std::to_string(i + 1) + " of '" + call.callee + "' must be " +
                                             typeName(target.params[i].type) + ", but this is " + describe(argTypes[i]));
            }
        }
        return target.returnType;
    }

    // --------------------------------------------------------------- helpers

    static std::string describe(Type type) {
        switch (type) {
            case Type::Int: return "an int";
            case Type::Bool: return "a bool";
            case Type::Void: return "nothing (a call to a function with no return value)";
            case Type::Error: return "an invalid expression";
        }
        return "?";
    }

    void error(SourceLoc loc, std::string message) {
        diagnostics_.push_back(Diagnostic{loc, std::move(message)});
    }

    Program& program_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    std::vector<std::unordered_map<std::string, VarInfo>> scopes_;
    const FunctionDecl* current_ = nullptr;
    int nextSlot_ = 0;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace

std::vector<Diagnostic> analyze(Program& program) { return Analyzer(program).run(); }

}  // namespace sprig
