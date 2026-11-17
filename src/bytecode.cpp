#include "bytecode.h"

#include <iomanip>
#include <sstream>

namespace sprig {

const char* opName(Op op) {
    switch (op) {
        case Op::Const: return "Const";
        case Op::Load: return "Load";
        case Op::Store: return "Store";
        case Op::Pop: return "Pop";
        case Op::Add: return "Add";
        case Op::Sub: return "Sub";
        case Op::Mul: return "Mul";
        case Op::Div: return "Div";
        case Op::Mod: return "Mod";
        case Op::Neg: return "Neg";
        case Op::Not: return "Not";
        case Op::Eq: return "Eq";
        case Op::Ne: return "Ne";
        case Op::Lt: return "Lt";
        case Op::Le: return "Le";
        case Op::Gt: return "Gt";
        case Op::Ge: return "Ge";
        case Op::Jump: return "Jump";
        case Op::JumpIfFalse: return "JumpIfFalse";
        case Op::Call: return "Call";
        case Op::Ret: return "Ret";
        case Op::RetVoid: return "RetVoid";
        case Op::PrintInt: return "PrintInt";
        case Op::PrintBool: return "PrintBool";
        case Op::Halt: return "Halt";
        case Op::Trap: return "Trap";
    }
    return "?";
}

int operandBytes(Op op) {
    switch (op) {
        case Op::Const:
        case Op::Load:
        case Op::Store:
        case Op::Jump:
        case Op::JumpIfFalse:
            return 2;
        case Op::Call:
            return 3;
        default:
            return 0;
    }
}

std::string disassemble(const Module& module) {
    std::ostringstream out;
    for (const Function& fn : module.functions) {
        out << "== " << fn.name << " (params: " << fn.arity << ", locals: " << fn.numLocals
            << ", max stack: " << fn.maxStack << ") ==\n";

        std::size_t ip = 0;
        while (ip < fn.code.size()) {
            const Op op = static_cast<Op>(fn.code[ip]);
            const auto u16At = [&](std::size_t at) {
                return static_cast<int>(fn.code[at]) | (static_cast<int>(fn.code[at + 1]) << 8);
            };

            // Build the operand text first so instructions without operands
            // do not end up with trailing padding.
            std::ostringstream operand;
            switch (op) {
                case Op::Const: {
                    const int index = u16At(ip + 1);
                    operand << index << "  (= " << module.constants[static_cast<std::size_t>(index)] << ")";
                    break;
                }
                case Op::Load:
                case Op::Store:
                    operand << "slot " << u16At(ip + 1);
                    break;
                case Op::Jump:
                case Op::JumpIfFalse:
                    operand << "-> " << std::setw(4) << std::setfill('0') << u16At(ip + 1);
                    break;
                case Op::Call: {
                    const int index = u16At(ip + 1);
                    operand << module.functions[static_cast<std::size_t>(index)].name << "  (args: "
                            << static_cast<int>(fn.code[ip + 3]) << ")";
                    break;
                }
                default:
                    break;
            }

            out << std::setw(4) << std::setfill('0') << ip << std::setfill(' ') << "  line " << std::setw(3)
                << fn.lines[ip] << "  ";
            if (operand.str().empty()) {
                out << opName(op);
            } else {
                out << std::left << std::setw(12) << opName(op) << std::right << operand.str();
            }
            out << '\n';
            ip += 1 + static_cast<std::size_t>(operandBytes(op));
        }
        out << '\n';
    }
    return out.str();
}

}  // namespace sprig
