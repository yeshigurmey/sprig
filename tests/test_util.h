// Small helpers shared by the test files.
#pragma once

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

#include "driver.h"
#include "vm.h"

namespace sprig::testing {

// Far more instructions than any test needs. Giving every VM in the tests this
// limit turns an accidental infinite loop into a failed test instead of a hang.
constexpr std::uint64_t kInstructionLimit = 50'000'000;

// Compiles and runs a program, returning everything it printed.
// Fails loudly (by throwing) if the program does not compile, so a typo in a
// test program shows up as a clear message instead of an empty output.
inline std::string run(const std::string& source, bool optimize = true) {
    CompileOptions options;
    options.optimize = optimize;
    const CompileResult result = compile(source, options);
    if (!result.ok()) {
        throw std::runtime_error("test program failed to compile: " + result.diagnostics.front().message);
    }
    std::ostringstream out;
    VM vm(result.module, out);
    vm.setInstructionLimit(kInstructionLimit);
    vm.run();
    return out.str();
}

// Returns the compile errors for a program, joined with newlines. Empty means it compiled.
inline std::string errorsOf(const std::string& source) {
    const CompileResult result = compile(source);
    std::string all;
    for (const Diagnostic& diagnostic : result.diagnostics) {
        if (!all.empty()) all += '\n';
        all += diagnostic.message;
    }
    return all;
}

}  // namespace sprig::testing
