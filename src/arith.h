// Integer arithmetic rules for Sprig, shared by the optimizer and the VM.
//
// Both of them need to add, multiply and divide. If each had its own copy of
// the rules, an expression the optimizer computes at compile time could give a
// different answer from the same expression run by the VM. Keeping one copy
// here makes that kind of disagreement impossible.
#pragma once

#include <cstdint>
#include <limits>

namespace sprig::arith {

// Sprig ints wrap around on overflow (two's complement), like Java's long.
// In C++ signed overflow is undefined behaviour, so the maths is done on
// unsigned values, where wrapping is well defined, and converted back.
inline std::int64_t add(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}

inline std::int64_t sub(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}

inline std::int64_t mul(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}

inline std::int64_t neg(std::int64_t a) { return sub(0, a); }

// Division and remainder. The caller must check for b == 0 first, because that
// is a runtime error in Sprig rather than a value.
//
// INT64_MIN / -1 is the one other case that overflows (the true answer does
// not fit), and on x86 it crashes the process. It is defined here to wrap.
inline std::int64_t div(std::int64_t a, std::int64_t b) {
    if (b == -1) return neg(a);
    return a / b;
}

inline std::int64_t mod(std::int64_t a, std::int64_t b) {
    if (b == -1) return 0;
    return a % b;
}

}  // namespace sprig::arith
