#include <gtest/gtest.h>

#include <string>

#include "test_util.h"

using sprig::RuntimeError;
using sprig::testing::run;

namespace {

// Runs a program both with and without the optimizer and checks that the two
// agree before returning the output. Every test below gets this check for free.
std::string runBoth(const std::string& source) {
    const std::string plain = run(source, false);
    const std::string optimized = run(source, true);
    EXPECT_EQ(plain, optimized) << "optimizer changed the behaviour of:\n" << source;
    return plain;
}

}  // namespace

TEST(VM, PrintsIntsAndBools) {
    EXPECT_EQ(runBoth("print(42); print(true); print(false); print(-7);"), "42\ntrue\nfalse\n-7\n");
}

TEST(VM, Arithmetic) {
    EXPECT_EQ(runBoth("print(2 + 3 * 4 - 10 / 2);"), "9\n");
    EXPECT_EQ(runBoth("print(17 % 5); print(17 / 5);"), "2\n3\n");
}

TEST(VM, DivisionTruncatesTowardZero) {
    EXPECT_EQ(runBoth("print(-7 / 2); print(-7 % 2); print(7 / -2); print(7 % -2);"), "-3\n-1\n-3\n1\n");
}

TEST(VM, IntegersWrapOnOverflow) {
    EXPECT_EQ(runBoth("let max = 9223372036854775807; print(max + 1);"), "-9223372036854775808\n");
    EXPECT_EQ(runBoth("let max = 9223372036854775807; print(max * 2);"), "-2\n");
    // The same sums with a value the optimizer cannot see through.
    EXPECT_EQ(runBoth("fn id(n: int) -> int { return n; } print(id(9223372036854775807) + 1);"),
              "-9223372036854775808\n");
}

TEST(VM, MostNegativeIntDividedByMinusOneDoesNotCrash) {
    // On x86 this exact division raises a hardware exception if done naively.
    const std::string min = "fn min() -> int { return -9223372036854775807 - 1; } ";
    EXPECT_EQ(runBoth(min + "print(min() / -1);"), "-9223372036854775808\n");
    EXPECT_EQ(runBoth(min + "print(min() % -1);"), "0\n");
}

TEST(VM, Variables) {
    EXPECT_EQ(runBoth("let x = 1; let y = x + 1; x = y * 10; print(x); print(y);"), "20\n2\n");
}

TEST(VM, IfElse) {
    const std::string classify = "fn classify(n: int) -> int {"
                                 "  if n < 0 { return -1; } else if n == 0 { return 0; } else { return 1; }"
                                 "} ";
    EXPECT_EQ(runBoth(classify + "print(classify(-5)); print(classify(0)); print(classify(9));"), "-1\n0\n1\n");
}

TEST(VM, WhileLoop) {
    EXPECT_EQ(runBoth("let i = 0; let sum = 0; while i < 5 { sum = sum + i; i = i + 1; } print(sum);"), "10\n");
}

TEST(VM, NestedLoops) {
    EXPECT_EQ(runBoth("let count = 0; let i = 0;"
                      "while i < 4 { let j = 0; while j < 3 { count = count + 1; j = j + 1; } i = i + 1; }"
                      "print(count);"),
              "12\n");
}

TEST(VM, FunctionCallsAndRecursion) {
    EXPECT_EQ(runBoth("fn fact(n: int) -> int { if n <= 1 { return 1; } return n * fact(n - 1); } print(fact(10));"),
              "3628800\n");
    EXPECT_EQ(runBoth("fn fib(n: int) -> int { if n < 2 { return n; } return fib(n - 1) + fib(n - 2); }"
                      "print(fib(20));"),
              "6765\n");
}

TEST(VM, MutualRecursion) {
    EXPECT_EQ(runBoth("fn even(n: int) -> bool { if n == 0 { return true; } return odd(n - 1); }"
                      "fn odd(n: int) -> bool { if n == 0 { return false; } return even(n - 1); }"
                      "print(even(10)); print(odd(7)); print(even(3));"),
              "true\ntrue\nfalse\n");
}

TEST(VM, ArgumentsArePassedInOrder) {
    EXPECT_EQ(runBoth("fn sub(a: int, b: int) -> int { return a - b; } print(sub(10, 3));"), "7\n");
}

TEST(VM, ArgumentsAreCopies) {
    // Changing a parameter inside a function must not affect the caller's variable.
    EXPECT_EQ(runBoth("fn bump(n: int) -> int { n = n + 1; return n; }"
                      "let x = 5; let y = bump(x); x = x + 0; print(x); print(y);"),
              "5\n6\n");
}

TEST(VM, EachCallGetsItsOwnLocals) {
    // If frames shared storage, the inner call would overwrite `mine`.
    EXPECT_EQ(runBoth("fn depth(n: int) -> int {"
                      "  let mine = n * 100;"
                      "  if n > 0 { let below = depth(n - 1); mine = mine + 0 * below; }"
                      "  return mine;"
                      "} print(depth(3));"),
              "300\n");
}

TEST(VM, VoidFunctions) {
    EXPECT_EQ(runBoth("fn hello(n: int) { if n == 0 { return; } print(n); } hello(0); hello(7);"), "7\n");
}

TEST(VM, UnusedCallResultIsDiscarded) {
    // If the result were left on the stack, the following arithmetic would go wrong.
    EXPECT_EQ(runBoth("fn five() -> int { return 5; } five(); five(); print(1 + 2);"), "3\n");
}

TEST(VM, AndShortCircuits) {
    const std::string loud = "fn loud(value: bool, tag: int) -> bool { print(tag); return value; } ";
    EXPECT_EQ(runBoth(loud + "print(loud(false, 1) && loud(true, 2));"), "1\nfalse\n");
    EXPECT_EQ(runBoth(loud + "print(loud(true, 1) && loud(false, 2));"), "1\n2\nfalse\n");
}

TEST(VM, OrShortCircuits) {
    const std::string loud = "fn loud(value: bool, tag: int) -> bool { print(tag); return value; } ";
    EXPECT_EQ(runBoth(loud + "print(loud(true, 1) || loud(false, 2));"), "1\ntrue\n");
    EXPECT_EQ(runBoth(loud + "print(loud(false, 1) || loud(true, 2));"), "1\n2\ntrue\n");
}

TEST(VM, ShortCircuitGuardsADivision) {
    EXPECT_EQ(runBoth("fn safe(a: int, b: int) -> bool { return b != 0 && a / b > 1; }"
                      "print(safe(10, 0)); print(safe(10, 2));"),
              "false\ntrue\n");
}

TEST(VM, LogicInsideLargerExpressions) {
    // Exercises the stack depth bookkeeping for && and || nested in arithmetic and calls.
    EXPECT_EQ(runBoth("fn pick(c: bool, a: int, b: int) -> int { if c { return a; } return b; }"
                      "fn t() -> bool { return true; } fn f() -> bool { return false; }"
                      "print(1 + pick(t() && (f() || t()), 10, 20) + pick(f() || (t() && f()), 100, 200));"),
              "211\n");
}

TEST(VM, TopLevelReturnStopsTheProgram) {
    EXPECT_EQ(runBoth("print(1); return; print(2);"), "1\n");
}

// -------------------------------------------------------------- runtime errors

TEST(VM, DivisionByZeroIsARuntimeErrorWithTheRightLine) {
    for (const bool optimize : {false, true}) {
        try {
            run("let a = 10;\nlet b = 0;\nprint(a / b);\n", optimize);
            FAIL() << "expected a RuntimeError";
        } catch (const RuntimeError& error) {
            EXPECT_STREQ(error.what(), "division by zero");
            EXPECT_EQ(error.line, 3);
        }
    }
}

TEST(VM, RemainderByZeroIsARuntimeError) {
    EXPECT_THROW(run("fn zero() -> int { return 0; } print(5 % zero());"), RuntimeError);
}

TEST(VM, OutputBeforeARuntimeErrorIsKept) {
    const sprig::CompileResult result = sprig::compile("print(1); print(2 / (1 - 1)); print(3);");
    ASSERT_TRUE(result.ok());
    std::ostringstream out;
    sprig::VM vm(result.module, out);
    vm.setInstructionLimit(sprig::testing::kInstructionLimit);
    EXPECT_THROW(vm.run(), RuntimeError);
    EXPECT_EQ(out.str(), "1\n");
}

TEST(VM, RunawayRecursionIsReportedInsteadOfCrashing) {
    try {
        run("fn forever(n: int) -> int { return forever(n + 1); } print(forever(0));");
        FAIL() << "expected a RuntimeError";
    } catch (const RuntimeError& error) {
        EXPECT_NE(std::string(error.what()).find("stack overflow"), std::string::npos);
    }
}

TEST(VM, DeepButFiniteRecursionWorks) {
    EXPECT_EQ(runBoth("fn count(n: int) -> int { if n == 0 { return 0; } return 1 + count(n - 1); }"
                      "print(count(50000));"),
              "50000\n");
}

TEST(VM, InstructionLimitStopsAnInfiniteLoop) {
    const sprig::CompileResult result = sprig::compile("let n = 0; while true { n = n + 1; }");
    ASSERT_TRUE(result.ok());
    std::ostringstream out;
    sprig::VM vm(result.module, out);
    vm.setInstructionLimit(1000);
    EXPECT_THROW(vm.run(), RuntimeError);
    EXPECT_EQ(vm.stats().instructions, 1000u);
}

// ----------------------------------------------------------------------- stats

TEST(VM, CountsInstructionsAndCalls) {
    const sprig::CompileResult result = sprig::compile("fn f() { } f(); f(); f();");
    ASSERT_TRUE(result.ok());
    std::ostringstream out;
    sprig::VM vm(result.module, out);
    vm.setInstructionLimit(sprig::testing::kInstructionLimit);
    vm.run();
    EXPECT_EQ(vm.stats().calls, 3u);
    EXPECT_EQ(vm.stats().maxFrames, 2u);
    // Three calls, three returns, one halt.
    EXPECT_EQ(vm.stats().instructions, 7u);
}
