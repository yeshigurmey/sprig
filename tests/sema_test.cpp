#include <gtest/gtest.h>

#include <string>

#include "test_util.h"

using sprig::testing::errorsOf;

namespace {

// Checks that a program is rejected and that the message mentions `fragment`.
::testing::AssertionResult rejects(const std::string& source, const std::string& fragment) {
    const std::string errors = errorsOf(source);
    if (errors.empty()) return ::testing::AssertionFailure() << "program compiled, but an error was expected";
    if (errors.find(fragment) == std::string::npos) {
        return ::testing::AssertionFailure() << "wanted an error containing \"" << fragment << "\", got: " << errors;
    }
    return ::testing::AssertionSuccess();
}

}  // namespace

// ------------------------------------------------------------ valid programs

TEST(Sema, AcceptsWellTypedProgram) {
    EXPECT_EQ(errorsOf("let x = 1; let ok: bool = x < 2; if ok { print(x); }"), "");
}

TEST(Sema, FunctionsCanBeCalledBeforeTheyAreDefined) {
    EXPECT_EQ(errorsOf("print(twice(4)); fn twice(n: int) -> int { return n * 2; }"), "");
}

TEST(Sema, MutualRecursionIsAllowed) {
    EXPECT_EQ(errorsOf("fn even(n: int) -> bool { if n == 0 { return true; } return odd(n - 1); }"
                       "fn odd(n: int) -> bool { if n == 0 { return false; } return even(n - 1); }"),
              "");
}

TEST(Sema, InnerScopeMayShadowOuterVariable) {
    EXPECT_EQ(errorsOf("let x = 1; { let x = true; print(x); } print(x + 1);"), "");
}

TEST(Sema, BoolsCanBeComparedForEquality) {
    EXPECT_EQ(errorsOf("print(true == false); print(1 != 2);"), "");
}

// --------------------------------------------------------------------- names

TEST(Sema, UndeclaredVariable) {
    EXPECT_TRUE(rejects("print(y);", "'y' is not declared"));
}

TEST(Sema, VariableIsNotVisibleAfterItsBlockEnds) {
    EXPECT_TRUE(rejects("{ let inner = 1; } print(inner);", "'inner' is not declared"));
}

TEST(Sema, VariableCannotBeUsedInItsOwnInitializer) {
    EXPECT_TRUE(rejects("let x = x + 1;", "'x' is not declared"));
}

TEST(Sema, RedeclarationInSameScope) {
    EXPECT_TRUE(rejects("let x = 1; let x = 2;", "already declared"));
}

TEST(Sema, ParameterCannotBeRedeclaredInBody) {
    EXPECT_TRUE(rejects("fn f(n: int) { let n = 2; }", "already declared"));
}

TEST(Sema, AssignmentToUndeclaredVariable) {
    EXPECT_TRUE(rejects("x = 5;", "not declared"));
}

TEST(Sema, FunctionsCannotSeeTopLevelVariables) {
    // Sprig has no globals: a function only sees its parameters and its own locals.
    EXPECT_TRUE(rejects("let total = 0; fn bump() { total = total + 1; }", "not declared"));
}

TEST(Sema, UndefinedFunction) {
    EXPECT_TRUE(rejects("print(missing(1));", "function 'missing' is not defined"));
}

TEST(Sema, DuplicateFunction) {
    EXPECT_TRUE(rejects("fn f() { } fn f() { }", "defined more than once"));
}

// --------------------------------------------------------------------- types

TEST(Sema, ArithmeticNeedsInts) {
    EXPECT_TRUE(rejects("print(1 + true);", "'+' needs int on both sides"));
    EXPECT_TRUE(rejects("print(-true);", "'-' needs an int"));
}

TEST(Sema, LogicNeedsBools) {
    EXPECT_TRUE(rejects("print(1 && 2);", "'&&' needs bool on both sides"));
    EXPECT_TRUE(rejects("print(!5);", "'!' needs a bool"));
}

TEST(Sema, ComparisonNeedsInts) {
    EXPECT_TRUE(rejects("print(true < false);", "'<' needs int on both sides"));
}

TEST(Sema, EqualityNeedsMatchingTypes) {
    EXPECT_TRUE(rejects("print(1 == true);", "cannot compare an int with a bool"));
}

TEST(Sema, ConditionMustBeBool) {
    EXPECT_TRUE(rejects("if 1 { }", "condition of an 'if' must be a bool"));
    EXPECT_TRUE(rejects("while 0 { }", "condition of a 'while' must be a bool"));
}

TEST(Sema, AnnotationMustMatchInitializer) {
    EXPECT_TRUE(rejects("let x: int = true;", "declared as int"));
}

TEST(Sema, AssignmentMustKeepTheVariableType) {
    EXPECT_TRUE(rejects("let x = 1; x = false;", "cannot be assigned a bool"));
}

// ----------------------------------------------------------------- functions

TEST(Sema, WrongNumberOfArguments) {
    EXPECT_TRUE(rejects("fn f(a: int) { } f(1, 2);", "takes 1 argument(s), but 2 were given"));
}

TEST(Sema, WrongArgumentType) {
    EXPECT_TRUE(rejects("fn f(a: int, b: bool) { } f(1, 2);", "argument 2 of 'f' must be bool"));
}

TEST(Sema, ReturnTypeMustMatch) {
    EXPECT_TRUE(rejects("fn f() -> int { return true; }", "returns int"));
    EXPECT_TRUE(rejects("fn f() -> int { return; }", "must return an int"));
    EXPECT_TRUE(rejects("fn f() { return 1; }", "does not return a value"));
}

TEST(Sema, EveryPathMustReturn) {
    EXPECT_TRUE(rejects("fn f(n: int) -> int { if n > 0 { return 1; } }", "can reach the end without returning"));
    EXPECT_EQ(errorsOf("fn f(n: int) -> int { if n > 0 { return 1; } else { return 2; } }"), "");
    EXPECT_EQ(errorsOf("fn f(n: int) -> int { if n > 0 { return 1; } return 2; }"), "");
}

TEST(Sema, VoidResultCannotBeUsedAsAValue) {
    EXPECT_TRUE(rejects("fn f() { } let x = f();", "returns nothing"));
    EXPECT_TRUE(rejects("fn f() { } print(f());", "returns nothing"));
}

TEST(Sema, TopLevelCodeCannotReturnAValue) {
    EXPECT_TRUE(rejects("return 1;", "outside a function"));
    EXPECT_EQ(errorsOf("return;"), "");
}

// ----------------------------------------------------------------- reporting

TEST(Sema, ReportsEveryErrorNotJustTheFirst) {
    const sprig::CompileResult result = sprig::compile("print(a);\nprint(b);\nprint(1 + true);");
    ASSERT_EQ(result.diagnostics.size(), 3u);
    EXPECT_EQ(result.diagnostics[0].loc.line, 1);
    EXPECT_EQ(result.diagnostics[1].loc.line, 2);
    EXPECT_EQ(result.diagnostics[2].loc.line, 3);
}

TEST(Sema, OneMistakeProducesOneError) {
    // `y` is undeclared. The surrounding `+` and `let` must not pile on with
    // their own complaints about the same expression.
    const sprig::CompileResult result = sprig::compile("let x = y + 1; print(x * 2);");
    EXPECT_EQ(result.diagnostics.size(), 1u);
}

TEST(Sema, DiagnosticShowsTheSourceLineAndACaret) {
    const std::string source = "let x = 1;\nprint(y);\n";
    const sprig::CompileResult result = sprig::compile(source);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(sprig::formatDiagnostic("demo.sprig", source, result.diagnostics[0]),
              "demo.sprig:2:7: error: 'y' is not declared\n"
              "    print(y);\n"
              "          ^\n");
}
