#include <gtest/gtest.h>

#include <string>

#include "test_util.h"

namespace {

// Compiles with the optimizer on and prints the resulting tree.
std::string optimized(const std::string& source) {
    const sprig::CompileResult result = sprig::compile(source);
    if (!result.ok()) return "COMPILE ERROR: " + result.diagnostics.front().message;
    return sprig::printProgram(*result.ast);
}

}  // namespace

// ----------------------------------------------------------- constant folding

TEST(Optimizer, FoldsArithmetic) {
    EXPECT_EQ(optimized("print(1 + 2 * 3);"), "(print 7)\n");
    EXPECT_EQ(optimized("print((10 - 4) / 3 % 2);"), "(print 0)\n");
    EXPECT_EQ(optimized("print(-(2 + 3));"), "(print -5)\n");
}

TEST(Optimizer, FoldsComparisonsAndLogic) {
    EXPECT_EQ(optimized("print(2 < 3);"), "(print true)\n");
    EXPECT_EQ(optimized("print(1 == 2 || !(3 >= 4));"), "(print true)\n");
    EXPECT_EQ(optimized("print(true == false);"), "(print false)\n");
}

TEST(Optimizer, DoesNotFoldDivisionByZero) {
    // The error has to happen at run time, on the right line, so it stays in the tree.
    EXPECT_EQ(optimized("print(1 / 0);"), "(print (/ 1 0))\n");
    EXPECT_EQ(optimized("print(1 % 0);"), "(print (% 1 0))\n");
}

TEST(Optimizer, FoldingWrapsLikeTheVmDoes) {
    EXPECT_EQ(optimized("print(9223372036854775807 + 1);"), "(print -9223372036854775808)\n");
}

TEST(Optimizer, RemovesIdentityOperations) {
    const std::string fn = "fn f(x: int) -> int { return ";
    EXPECT_EQ(optimized(fn + "x + 0; }"), "(fn f (x:int) int (block (return x)))\n");
    EXPECT_EQ(optimized(fn + "0 + x; }"), "(fn f (x:int) int (block (return x)))\n");
    EXPECT_EQ(optimized(fn + "x * 1; }"), "(fn f (x:int) int (block (return x)))\n");
    EXPECT_EQ(optimized(fn + "x - 0; }"), "(fn f (x:int) int (block (return x)))\n");
    EXPECT_EQ(optimized(fn + "x / 1; }"), "(fn f (x:int) int (block (return x)))\n");
}

TEST(Optimizer, MergesConstantsAcrossAChain) {
    // (x * 60) * 24 -> x * 1440, and the same for addition.
    EXPECT_EQ(optimized("fn f(x: int) -> int { return x * 60 * 24; }"),
              "(fn f (x:int) int (block (return (* x 1440))))\n");
    EXPECT_EQ(optimized("fn f(x: int) -> int { return 1 + x + 2; }"), "(fn f (x:int) int (block (return (+ x 3))))\n");
    // The merged constant can itself turn out to be an identity.
    EXPECT_EQ(optimized("fn f(x: int) -> int { return x + 5 + -5; }"), "(fn f (x:int) int (block (return x)))\n");
}

TEST(Optimizer, KeepsMultiplyByZeroBecauseTheOtherSideMayHaveEffects) {
    EXPECT_EQ(optimized("fn noisy() -> int { print(1); return 2; } print(noisy() * 0);"),
              "(fn noisy () int (block (print 1) (return 2)))\n(print (* (call noisy) 0))\n");
}

TEST(Optimizer, SimplifiesShortCircuitWithKnownLeftSide) {
    const std::string fn = "fn f(b: bool) -> bool { return ";
    EXPECT_EQ(optimized(fn + "true && b; }"), "(fn f (b:bool) bool (block (return b)))\n");
    EXPECT_EQ(optimized(fn + "false && b; }"), "(fn f (b:bool) bool (block (return false)))\n");
    EXPECT_EQ(optimized(fn + "false || b; }"), "(fn f (b:bool) bool (block (return b)))\n");
    EXPECT_EQ(optimized(fn + "true || b; }"), "(fn f (b:bool) bool (block (return true)))\n");
}

// ------------------------------------------------------- constant propagation

TEST(Optimizer, PropagatesVariablesThatNeverChange) {
    EXPECT_EQ(optimized("let n = 10; print(n * 2);"), "(print 20)\n");
}

TEST(Optimizer, PropagatesThroughAChainOfDefinitions) {
    EXPECT_EQ(optimized("let a = 2; let b = a * 3; let c = b + a; print(c);"), "(print 8)\n");
}

TEST(Optimizer, DoesNotPropagateReassignedVariables) {
    EXPECT_EQ(optimized("let n = 10; n = n + 1; print(n);"), "(let n 10)\n(set n (+ n 1))\n(print n)\n");
}

TEST(Optimizer, ReassignmentAnywhereInTheFunctionBlocksPropagation) {
    // The assignment comes after the print in the text, but the loop means it
    // can run before the print does. Propagating i = 0 here would be a bug.
    EXPECT_EQ(optimized("let i = 0; while i < 3 { print(i); i = i + 1; }"),
              "(let i 0)\n(while (< i 3) (block (print i) (set i (+ i 1))))\n");
}

TEST(Optimizer, ShadowedVariablesAreTrackedSeparately) {
    // The inner x is a constant, the outer x is reassigned. They must not be confused.
    EXPECT_EQ(optimized("let x = 1; { let x = 5; print(x); } x = 2; print(x);"),
              "(let x 1)\n(block (print 5))\n(set x 2)\n(print x)\n");
}

// ------------------------------------------------------ dead code elimination

TEST(Optimizer, RemovesBranchThatCanNeverRun) {
    EXPECT_EQ(optimized("if false { print(1); }"), "");
    EXPECT_EQ(optimized("if true { print(1); } else { print(2); }"), "(block (print 1))\n");
    EXPECT_EQ(optimized("if false { print(1); } else { print(2); }"), "(block (print 2))\n");
}

TEST(Optimizer, RemovesDebugCodeGuardedByAConstantFlag) {
    EXPECT_EQ(optimized("let debug = false; if debug { print(999); } print(1);"), "(print 1)\n");
}

TEST(Optimizer, RemovesLoopThatNeverRuns) {
    EXPECT_EQ(optimized("while false { print(1); } print(2);"), "(print 2)\n");
}

TEST(Optimizer, RemovesCodeAfterReturn) {
    EXPECT_EQ(optimized("fn f() -> int { return 1; print(2); return 3; }"), "(fn f () int (block (return 1)))\n");
}

TEST(Optimizer, RemovesStatementsWithNoEffect) {
    EXPECT_EQ(optimized("1 + 2; print(3);"), "(print 3)\n");
}

TEST(Optimizer, KeepsCallsWhoseResultIsUnused) {
    EXPECT_EQ(optimized("fn f() -> int { print(1); return 2; } f();"),
              "(fn f () int (block (print 1) (return 2)))\n(call f)\n");
}

// -------------------------------------------------------------------- overall

TEST(Optimizer, ReportsWhatItDid) {
    const sprig::CompileResult result = sprig::compile("let n = 4; if n > 10 { print(0); } print(n + 1);");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.optStats.constantsPropagated, 2);
    EXPECT_EQ(result.optStats.expressionsFolded, 2);
    EXPECT_EQ(result.optStats.statementsRemoved, 2);  // the `let` and the `if`
}

TEST(Optimizer, OptimizedProgramExecutesFewerInstructions) {
    const std::string source = "let size = 8; let i = 0; let total = 0;"
                               "while i < size * size { total = total + i * 2 * 3; i = i + 1; }"
                               "print(total);";

    std::uint64_t instructions[2] = {0, 0};
    std::string output[2];
    for (int level = 0; level < 2; ++level) {
        sprig::CompileOptions options;
        options.optimize = level == 1;
        const sprig::CompileResult result = sprig::compile(source, options);
        ASSERT_TRUE(result.ok());
        std::ostringstream out;
        sprig::VM vm(result.module, out);
        vm.setInstructionLimit(sprig::testing::kInstructionLimit);
        vm.run();
        instructions[level] = vm.stats().instructions;
        output[level] = out.str();
    }

    EXPECT_EQ(output[0], output[1]);
    EXPECT_LT(instructions[1], instructions[0]);
}
