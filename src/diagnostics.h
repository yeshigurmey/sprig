// Shared error types for every stage of the compiler.
#pragma once

#include <stdexcept>
#include <string>

namespace sprig {

// A position in the source file. Lines and columns start at 1 because that is
// what editors show, so an error message can be pasted straight into "go to line".
struct SourceLoc {
    int line = 1;
    int col = 1;
};

// One reported problem. The type checker collects many of these so the user
// sees every type error in one run instead of fixing them one at a time.
struct Diagnostic {
    SourceLoc loc;
    std::string message;
};

// Thrown by the lexer and the parser. Those two stages stop at the first error
// because once the token stream stops making sense, later errors are mostly noise.
class SyntaxError : public std::runtime_error {
public:
    SyntaxError(SourceLoc where, const std::string& message)
        : std::runtime_error(message), loc(where) {}

    SourceLoc loc;
};

// Thrown by the VM while a program is running (division by zero, runaway recursion).
class RuntimeError : public std::runtime_error {
public:
    RuntimeError(int sourceLine, const std::string& message)
        : std::runtime_error(message), line(sourceLine) {}

    int line;
};

}  // namespace sprig
