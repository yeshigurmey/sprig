#include "vm.h"

#include <cassert>
#include <ostream>

#include "arith.h"
#include "diagnostics.h"

namespace sprig {

VM::VM(const Module& module, std::ostream& out) : module_(module), out_(out), stack_(kStackSlots, 0) {}

// Layout of the value stack while a function runs:
//
//     base                      base + numLocals                 sp
//      |                               |                          |
//      v                               v                          v
//   ...| param 0 | param 1 | local 2 | temp | temp | temp |  (free)
//
// A function's variables sit at fixed offsets from `base`, and the temporary
// values of whatever expression is being evaluated are stacked on top of them.
// Values carry no type tag: the type checker already proved each instruction
// gets the kind of value it expects, so every slot is a plain 64-bit integer.
void VM::run() {
    const Function* fn = &module_.functions[static_cast<std::size_t>(module_.mainIndex)];
    if (static_cast<std::size_t>(fn->numLocals) + static_cast<std::size_t>(fn->maxStack) > kStackSlots) {
        throw RuntimeError(0, "stack overflow");
    }

    // The hot state lives in local variables so the compiler can keep it in
    // CPU registers. It is written back to frames_ only when a call happens.
    const std::uint8_t* code = fn->code.data();
    std::int64_t* stack = stack_.data();
    std::size_t ip = 0;
    std::size_t base = 0;
    std::size_t sp = static_cast<std::size_t>(fn->numLocals);
    std::uint64_t executed = 0;
    const std::uint64_t limit = instructionLimit_;

    frames_.clear();
    frames_.push_back(Frame{fn, 0, 0});
    stats_ = VMStats{};
    stats_.maxFrames = 1;

    const auto readU16 = [&]() {
        const std::size_t value = static_cast<std::size_t>(code[ip]) | (static_cast<std::size_t>(code[ip + 1]) << 8);
        ip += 2;
        return value;
    };
    // The byte just before `ip` belongs to the instruction being executed.
    const auto currentLine = [&]() { return fn->lines[ip - 1]; };

    for (;;) {
        // Debug builds verify the bound that code generation promised. Release
        // builds compile this away, which is why the bound has to be right.
        assert(sp >= base + static_cast<std::size_t>(fn->numLocals));
        assert(sp <= base + static_cast<std::size_t>(fn->numLocals) + static_cast<std::size_t>(fn->maxStack));

        if (executed == limit) {
            stats_.instructions = executed;
            throw RuntimeError(fn->lines[ip], "instruction limit reached (possible infinite loop)");
        }
        ++executed;
        const Op op = static_cast<Op>(code[ip++]);
        switch (op) {
            case Op::Const:
                stack[sp++] = module_.constants[readU16()];
                break;
            case Op::Load:
                stack[sp++] = stack[base + readU16()];
                break;
            case Op::Store:
                stack[base + readU16()] = stack[--sp];
                break;
            case Op::Pop:
                --sp;
                break;

            case Op::Add:
                --sp;
                stack[sp - 1] = arith::add(stack[sp - 1], stack[sp]);
                break;
            case Op::Sub:
                --sp;
                stack[sp - 1] = arith::sub(stack[sp - 1], stack[sp]);
                break;
            case Op::Mul:
                --sp;
                stack[sp - 1] = arith::mul(stack[sp - 1], stack[sp]);
                break;
            case Op::Div:
                --sp;
                if (stack[sp] == 0) {
                    stats_.instructions = executed;
                    throw RuntimeError(currentLine(), "division by zero");
                }
                stack[sp - 1] = arith::div(stack[sp - 1], stack[sp]);
                break;
            case Op::Mod:
                --sp;
                if (stack[sp] == 0) {
                    stats_.instructions = executed;
                    throw RuntimeError(currentLine(), "remainder by zero");
                }
                stack[sp - 1] = arith::mod(stack[sp - 1], stack[sp]);
                break;
            case Op::Neg:
                stack[sp - 1] = arith::neg(stack[sp - 1]);
                break;
            case Op::Not:
                stack[sp - 1] = stack[sp - 1] == 0 ? 1 : 0;
                break;

            case Op::Eq:
                --sp;
                stack[sp - 1] = stack[sp - 1] == stack[sp] ? 1 : 0;
                break;
            case Op::Ne:
                --sp;
                stack[sp - 1] = stack[sp - 1] != stack[sp] ? 1 : 0;
                break;
            case Op::Lt:
                --sp;
                stack[sp - 1] = stack[sp - 1] < stack[sp] ? 1 : 0;
                break;
            case Op::Le:
                --sp;
                stack[sp - 1] = stack[sp - 1] <= stack[sp] ? 1 : 0;
                break;
            case Op::Gt:
                --sp;
                stack[sp - 1] = stack[sp - 1] > stack[sp] ? 1 : 0;
                break;
            case Op::Ge:
                --sp;
                stack[sp - 1] = stack[sp - 1] >= stack[sp] ? 1 : 0;
                break;

            case Op::Jump:
                ip = readU16();
                break;
            case Op::JumpIfFalse: {
                const std::size_t target = readU16();
                if (stack[--sp] == 0) ip = target;
                break;
            }

            case Op::Call: {
                const Function* callee = &module_.functions[readU16()];
                const std::size_t argc = code[ip++];

                // The arguments are already on top of the stack, in order. The
                // new frame simply starts where they are, so they become the
                // callee's first slots without being copied anywhere.
                const std::size_t newBase = sp - argc;
                const std::size_t needed =
                    newBase + static_cast<std::size_t>(callee->numLocals) + static_cast<std::size_t>(callee->maxStack);
                if (needed > kStackSlots || frames_.size() >= kMaxFrames) {
                    stats_.instructions = executed;
                    throw RuntimeError(currentLine(), "stack overflow (recursion went too deep) while calling '" +
                                                          callee->name + "'");
                }

                frames_.back().ip = ip;  // where to continue after the callee returns
                frames_.push_back(Frame{callee, 0, newBase});
                ++stats_.calls;
                if (frames_.size() > stats_.maxFrames) stats_.maxFrames = frames_.size();

                // Locals that are not parameters start at zero. Every `let`
                // stores a value before any read, so this is only for tidiness.
                for (std::size_t i = newBase + argc; i < newBase + static_cast<std::size_t>(callee->numLocals); ++i) {
                    stack[i] = 0;
                }

                fn = callee;
                code = fn->code.data();
                ip = 0;
                base = newBase;
                sp = newBase + static_cast<std::size_t>(fn->numLocals);
                break;
            }
            case Op::Ret:
            case Op::RetVoid: {
                const std::int64_t result = op == Op::Ret ? stack[sp - 1] : 0;

                // Throw away the whole frame (arguments, locals, temporaries)
                // by moving sp back to where the frame began.
                sp = base;
                frames_.pop_back();
                const Frame& caller = frames_.back();
                fn = caller.fn;
                code = fn->code.data();
                ip = caller.ip;
                base = caller.base;

                if (op == Op::Ret) stack[sp++] = result;
                break;
            }

            case Op::PrintInt:
                out_ << stack[--sp] << '\n';
                break;
            case Op::PrintBool:
                out_ << (stack[--sp] != 0 ? "true" : "false") << '\n';
                break;

            case Op::Halt:
                stats_.instructions = executed;
                return;
            case Op::Trap:
                stats_.instructions = executed;
                throw RuntimeError(currentLine(), "internal error: function '" + fn->name +
                                                      "' ended without returning (this is a compiler bug)");
        }
    }
}

}  // namespace sprig
