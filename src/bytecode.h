// The instruction set of the Sprig virtual machine, and the compiled form of a program.
//
// The VM is a stack machine. Instructions do not name registers. They pop
// their inputs off a stack and push their result back, the same model the JVM
// and CPython use. `1 + 2 * 3` compiles to:
//
//     Const 1    stack: 1
//     Const 2    stack: 1 2
//     Const 3    stack: 1 2 3
//     Mul        stack: 1 6
//     Add        stack: 7
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sprig {

// Each instruction is one opcode byte followed by its operands, if any.
// A 16-bit operand is stored as two bytes, low byte first.
enum class Op : std::uint8_t {
    Const,        // u16 index   push constants[index]
    Load,         // u16 slot    push the local variable in that slot
    Store,        // u16 slot    pop a value into that local variable
    Pop,          //             discard the top of the stack

    Add,          //             pop b, pop a, push a + b   (same shape for the next four)
    Sub,
    Mul,
    Div,          //             runtime error if b is 0
    Mod,          //             runtime error if b is 0
    Neg,          //             pop a, push -a
    Not,          //             pop a, push !a

    Eq,           //             pop b, pop a, push 1 if a == b else 0   (same shape for the next five)
    Ne,
    Lt,
    Le,
    Gt,
    Ge,

    Jump,         // u16 target  continue at byte offset `target` in this function
    JumpIfFalse,  // u16 target  pop a; if a is 0, continue at `target`

    Call,         // u16 function index, u8 argument count
    Ret,          //             pop the return value, leave the function, push the value for the caller
    RetVoid,      //             leave the function

    PrintInt,     //             pop a, print it as a number
    PrintBool,    //             pop a, print "true" or "false"

    Halt,         //             stop the program (end of the top level code)
    Trap,         //             should never run; planted after functions that must return a value
};

const char* opName(Op op);

// Number of operand bytes that follow the opcode.
int operandBytes(Op op);

// One compiled function.
struct Function {
    std::string name;
    int arity = 0;        // number of parameters
    int numLocals = 0;    // parameters + local variables: the size of its stack frame
    int maxStack = 0;     // most temporary values it ever has on the stack at once
    std::vector<std::uint8_t> code;
    // lines[i] is the source line that produced code[i]. One entry per byte is
    // wasteful but simple, and it is only read when reporting a runtime error.
    std::vector<int> lines;
};

// A whole compiled program.
struct Module {
    std::vector<std::int64_t> constants;  // shared by all functions
    std::vector<Function> functions;
    int mainIndex = 0;                    // where execution starts
};

// Human readable listing of the bytecode, for --dump-bytecode and for debugging.
std::string disassemble(const Module& module);

}  // namespace sprig
