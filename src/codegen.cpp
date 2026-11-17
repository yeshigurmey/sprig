#include "codegen.h"

#include <algorithm>
#include <unordered_map>

namespace sprig {

namespace {

class Generator {
public:
    Module run(const Program& program) {
        // User functions keep their index from the type checker, which is the
        // index every CallExpr already stores. The hidden main goes last.
        for (const auto& fn : program.functions) module_.functions.push_back(compileFunction(*fn, false));
        module_.mainIndex = static_cast<int>(module_.functions.size());
        module_.functions.push_back(compileFunction(*program.main, true));
        return std::move(module_);
    }

private:
    Function compileFunction(const FunctionDecl& decl, bool isMain) {
        fn_ = Function{};
        fn_.name = decl.name;
        fn_.arity = static_cast<int>(decl.params.size());
        fn_.numLocals = decl.numLocals;
        isMain_ = isMain;
        depth_ = 0;
        line_ = decl.loc.line;

        for (const auto& stmt : decl.body->stmts) genStmt(*stmt);

        // What happens when execution reaches the closing brace:
        //   main            -> the program is finished
        //   void function   -> return to the caller
        //   other functions -> impossible, the type checker proved every path
        //                      returns. Trap makes that assumption loud instead
        //                      of letting the VM run off the end of the code.
        if (isMain) {
            emit(Op::Halt);
        } else if (decl.returnType == Type::Void) {
            emit(Op::RetVoid);
        } else {
            emit(Op::Trap);
        }
        return std::move(fn_);
    }

    // --------------------------------------------------------------- emitting

    // How each instruction changes the number of values on the stack.
    // Call is not listed because its effect depends on the function called.
    static int stackEffect(Op op) {
        switch (op) {
            case Op::Const:
            case Op::Load:
                return +1;
            case Op::Store:
            case Op::Pop:
            case Op::Add:
            case Op::Sub:
            case Op::Mul:
            case Op::Div:
            case Op::Mod:
            case Op::Eq:
            case Op::Ne:
            case Op::Lt:
            case Op::Le:
            case Op::Gt:
            case Op::Ge:
            case Op::JumpIfFalse:
            case Op::Ret:
            case Op::PrintInt:
            case Op::PrintBool:
                return -1;
            default:
                return 0;
        }
    }

    // Tracks the stack depth as code is emitted and remembers the highest
    // point. The VM uses that maximum to check for stack overflow once per
    // call instead of once per push, which is the same trick the JVM uses.
    void adjustDepth(int delta) {
        depth_ += delta;
        fn_.maxStack = std::max(fn_.maxStack, depth_);
    }

    void emitByte(std::uint8_t byte) {
        fn_.code.push_back(byte);
        fn_.lines.push_back(line_);
    }

    void emit(Op op) {
        emitByte(static_cast<std::uint8_t>(op));
        adjustDepth(stackEffect(op));
    }

    void emitU16(int value) {
        emitByte(static_cast<std::uint8_t>(value & 0xff));
        emitByte(static_cast<std::uint8_t>((value >> 8) & 0xff));
    }

    void emitWithOperand(Op op, int operand) {
        emit(op);
        emitU16(operand);
    }

    void emitConst(std::int64_t value) {
        // Reuse an existing pool entry when the same number shows up again.
        auto found = constantIndex_.find(value);
        if (found == constantIndex_.end()) {
            if (module_.constants.size() >= 65536) {
                throw SyntaxError(SourceLoc{line_, 1}, "program has too many distinct constants");
            }
            found = constantIndex_.emplace(value, static_cast<int>(module_.constants.size())).first;
            module_.constants.push_back(value);
        }
        emitWithOperand(Op::Const, found->second);
    }

    // Forward jumps are a chicken and egg problem: the jump is emitted before
    // the code it jumps over, so the target is not known yet. The fix is to
    // emit a placeholder, remember where it is, and fill it in afterwards.
    std::size_t emitJump(Op op) {
        emit(op);
        const std::size_t operandAt = fn_.code.size();
        emitU16(0xffff);
        return operandAt;
    }

    void patchJump(std::size_t operandAt) { writeTarget(operandAt, fn_.code.size()); }

    void emitJumpTo(Op op, std::size_t target) {
        emit(op);
        const std::size_t operandAt = fn_.code.size();
        emitU16(0);
        writeTarget(operandAt, target);
    }

    void writeTarget(std::size_t operandAt, std::size_t target) {
        if (target > 0xffff) {
            throw SyntaxError(SourceLoc{line_, 1}, "function '" + fn_.name + "' is too large to compile");
        }
        fn_.code[operandAt] = static_cast<std::uint8_t>(target & 0xff);
        fn_.code[operandAt + 1] = static_cast<std::uint8_t>((target >> 8) & 0xff);
    }

    // ------------------------------------------------------------ statements

    // Every statement leaves the stack exactly as deep as it found it.
    void genStmt(const Stmt& stmt) {
        line_ = stmt.loc.line;
        switch (stmt.kind) {
            case Stmt::Kind::Let: {
                const auto& let = static_cast<const LetStmt&>(stmt);
                genExpr(*let.init);
                emitWithOperand(Op::Store, let.slot);
                break;
            }
            case Stmt::Kind::Assign: {
                const auto& assign = static_cast<const AssignStmt&>(stmt);
                genExpr(*assign.value);
                emitWithOperand(Op::Store, assign.slot);
                break;
            }
            case Stmt::Kind::If: {
                //       <cond>
                //       JumpIfFalse else
                //       <then>
                //       Jump end          (only when there is an else)
                // else: <else>
                // end:
                const auto& ifStmt = static_cast<const IfStmt&>(stmt);
                genExpr(*ifStmt.cond);
                const std::size_t toElse = emitJump(Op::JumpIfFalse);
                genStmt(*ifStmt.thenBranch);
                if (ifStmt.elseBranch) {
                    const std::size_t toEnd = emitJump(Op::Jump);
                    patchJump(toElse);
                    genStmt(*ifStmt.elseBranch);
                    patchJump(toEnd);
                } else {
                    patchJump(toElse);
                }
                break;
            }
            case Stmt::Kind::While: {
                // start: <cond>
                //        JumpIfFalse end
                //        <body>
                //        Jump start
                // end:
                const auto& loop = static_cast<const WhileStmt&>(stmt);
                const std::size_t start = fn_.code.size();
                genExpr(*loop.cond);
                const std::size_t toEnd = emitJump(Op::JumpIfFalse);
                genStmt(*loop.body);
                emitJumpTo(Op::Jump, start);
                patchJump(toEnd);
                break;
            }
            case Stmt::Kind::Return: {
                const auto& ret = static_cast<const ReturnStmt&>(stmt);
                if (ret.value) {
                    genExpr(*ret.value);
                    emit(Op::Ret);
                } else {
                    emit(isMain_ ? Op::Halt : Op::RetVoid);
                }
                break;
            }
            case Stmt::Kind::Print: {
                const auto& print = static_cast<const PrintStmt&>(stmt);
                genExpr(*print.value);
                // The type is known at compile time, so the choice of how to
                // print is made here and the VM never has to ask what a value is.
                emit(print.value->type == Type::Bool ? Op::PrintBool : Op::PrintInt);
                break;
            }
            case Stmt::Kind::ExprStmt: {
                const auto& exprStmt = static_cast<const ExprStmt&>(stmt);
                genExpr(*exprStmt.expr);
                // The value is unused, so remove it to keep the stack balanced.
                if (exprStmt.expr->type != Type::Void) emit(Op::Pop);
                break;
            }
            case Stmt::Kind::Block:
                for (const auto& child : static_cast<const BlockStmt&>(stmt).stmts) genStmt(*child);
                break;
        }
    }

    // ----------------------------------------------------------- expressions

    // Every expression leaves exactly one value on the stack (none for a void call).
    void genExpr(const Expr& expr) {
        line_ = expr.loc.line;
        switch (expr.kind) {
            case Expr::Kind::IntLit:
                emitConst(static_cast<const IntLit&>(expr).value);
                break;
            case Expr::Kind::BoolLit:
                emitConst(static_cast<const BoolLit&>(expr).value ? 1 : 0);
                break;
            case Expr::Kind::Var:
                emitWithOperand(Op::Load, static_cast<const VarExpr&>(expr).slot);
                break;
            case Expr::Kind::Unary: {
                const auto& unary = static_cast<const UnaryExpr&>(expr);
                genExpr(*unary.operand);
                line_ = expr.loc.line;
                emit(unary.op == UnaryOp::Neg ? Op::Neg : Op::Not);
                break;
            }
            case Expr::Kind::Binary:
                genBinary(static_cast<const BinaryExpr&>(expr));
                break;
            case Expr::Kind::Call: {
                const auto& call = static_cast<const CallExpr&>(expr);
                for (const auto& arg : call.args) genExpr(*arg);
                line_ = expr.loc.line;
                emitWithOperand(Op::Call, call.fnIndex);
                emitByte(static_cast<std::uint8_t>(call.args.size()));
                // The call consumes the arguments and leaves the result, if there is one.
                adjustDepth(-static_cast<int>(call.args.size()));
                if (expr.type != Type::Void) adjustDepth(+1);
                break;
            }
        }
    }

    void genBinary(const BinaryExpr& binary) {
        // && and || must not evaluate the right side when the left side already
        // decides the answer, so they compile to jumps, not to an instruction.
        if (binary.op == BinaryOp::And) {
            //        <lhs>
            //        JumpIfFalse false
            //        <rhs>
            //        Jump end
            // false: Const 0
            // end:
            genExpr(*binary.lhs);
            const std::size_t toFalse = emitJump(Op::JumpIfFalse);
            genExpr(*binary.rhs);
            const std::size_t toEnd = emitJump(Op::Jump);
            // The two paths are alternatives, but they are emitted one after
            // the other. Undo the first path's push before counting the second.
            adjustDepth(-1);
            patchJump(toFalse);
            emitConst(0);
            patchJump(toEnd);
            return;
        }
        if (binary.op == BinaryOp::Or) {
            //       <lhs>
            //       JumpIfFalse rhs
            //       Const 1
            //       Jump end
            // rhs:  <rhs>
            // end:
            genExpr(*binary.lhs);
            const std::size_t toRhs = emitJump(Op::JumpIfFalse);
            emitConst(1);
            const std::size_t toEnd = emitJump(Op::Jump);
            adjustDepth(-1);
            patchJump(toRhs);
            genExpr(*binary.rhs);
            patchJump(toEnd);
            return;
        }

        genExpr(*binary.lhs);
        genExpr(*binary.rhs);
        line_ = binary.loc.line;
        switch (binary.op) {
            case BinaryOp::Add: emit(Op::Add); break;
            case BinaryOp::Sub: emit(Op::Sub); break;
            case BinaryOp::Mul: emit(Op::Mul); break;
            case BinaryOp::Div: emit(Op::Div); break;
            case BinaryOp::Mod: emit(Op::Mod); break;
            case BinaryOp::Eq: emit(Op::Eq); break;
            case BinaryOp::Ne: emit(Op::Ne); break;
            case BinaryOp::Lt: emit(Op::Lt); break;
            case BinaryOp::Le: emit(Op::Le); break;
            case BinaryOp::Gt: emit(Op::Gt); break;
            case BinaryOp::Ge: emit(Op::Ge); break;
            case BinaryOp::And:
            case BinaryOp::Or:
                break;  // handled above
        }
    }

    Module module_;
    std::unordered_map<std::int64_t, int> constantIndex_;
    Function fn_;
    bool isMain_ = false;
    int depth_ = 0;
    int line_ = 1;
};

}  // namespace

Module generate(const Program& program) { return Generator().run(program); }

}  // namespace sprig
