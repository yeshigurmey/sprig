// Stage 6: run the bytecode.
#pragma once

#include <cstdint>
#include <iosfwd>
#include <vector>

#include "bytecode.h"

namespace sprig {

// Counters collected while a program runs, shown by --stats. They make the
// effect of the optimizer measurable without relying on noisy wall clock time.
struct VMStats {
    std::uint64_t instructions = 0;  // bytecode instructions executed
    std::uint64_t calls = 0;         // function calls made
    std::size_t maxFrames = 0;       // deepest the call stack got
};

class VM {
public:
    // Output from `print` goes to `out`, which lets tests capture it in a string.
    VM(const Module& module, std::ostream& out);

    // Runs the program from the start of main to its end.
    // Throws RuntimeError on division by zero or when recursion goes too deep.
    void run();

    const VMStats& stats() const { return stats_; }

    // Stops the program with a RuntimeError once it has executed this many
    // instructions. Off by default. The tests turn it on so that a bug which
    // creates an infinite loop fails a test instead of hanging the whole suite.
    void setInstructionLimit(std::uint64_t limit) { instructionLimit_ = limit; }

    // Limits. Public so tests and docs can refer to them.
    static constexpr std::size_t kStackSlots = 1 << 20;  // about 8 MB of values
    static constexpr std::size_t kMaxFrames = 100000;

private:
    // The bookkeeping for one active function call.
    struct Frame {
        const Function* fn;
        std::size_t ip;    // index of the next byte to execute in fn->code
        std::size_t base;  // where this function's slot 0 lives in stack_
    };

    const Module& module_;
    std::ostream& out_;
    std::vector<std::int64_t> stack_;
    std::vector<Frame> frames_;
    VMStats stats_;
    std::uint64_t instructionLimit_ = UINT64_MAX;
};

}  // namespace sprig
