#include <gtest/gtest.h>

#include <string>

#include "ast.h"
#include "lexer.h"
#include "parser.h"

using sprig::Lexer;
using sprig::Parser;
using sprig::SyntaxError;

namespace {

// Parses a program and prints the tree back as text. Comparing that text is a
// compact way to check the exact shape of the tree the parser built.
std::string parse(const std::string& source) {
    Parser parser(Lexer(source).tokenize());
    return sprig::printProgram(*parser.parseProgram());
}

// Parses a single expression statement and prints just the expression.
std::string parseExpr(const std::string& expression) {
    std::string printed = parse(expression + ";");
    if (!printed.empty() && printed.back() == '\n') printed.pop_back();
    return printed;
}

std::string syntaxErrorOf(const std::string& source) {
    try {
        parse(source);
    } catch (const SyntaxError& error) {
        return error.what();
    }
    return "";
}

}  // namespace

// ---------------------------------------------------------------- expressions

TEST(Parser, MultiplicationBindsTighterThanAddition) {
    EXPECT_EQ(parseExpr("1 + 2 * 3"), "(+ 1 (* 2 3))");
    EXPECT_EQ(parseExpr("1 * 2 + 3"), "(+ (* 1 2) 3)");
}

TEST(Parser, ParenthesesOverridePrecedence) {
    EXPECT_EQ(parseExpr("(1 + 2) * 3"), "(* (+ 1 2) 3)");
}

TEST(Parser, BinaryOperatorsAreLeftAssociative) {
    // Getting this wrong would make 10 - 4 - 3 equal 9 instead of 3.
    EXPECT_EQ(parseExpr("10 - 4 - 3"), "(- (- 10 4) 3)");
    EXPECT_EQ(parseExpr("100 / 10 / 5"), "(/ (/ 100 10) 5)");
}

TEST(Parser, ComparisonBindsLooserThanArithmetic) {
    EXPECT_EQ(parseExpr("a + 1 < b * 2"), "(< (+ a 1) (* b 2))");
}

TEST(Parser, LogicalOperatorPrecedence) {
    // && binds tighter than ||, and both are looser than comparisons.
    EXPECT_EQ(parseExpr("a || b && c"), "(|| a (&& b c))");
    EXPECT_EQ(parseExpr("a == 1 && b != 2"), "(&& (== a 1) (!= b 2))");
}

TEST(Parser, UnaryOperators) {
    EXPECT_EQ(parseExpr("-a"), "(- a)");
    EXPECT_EQ(parseExpr("!a"), "(! a)");
    EXPECT_EQ(parseExpr("--a"), "(- (- a))");
    // Unary minus applies to the 2 only, not to the whole product.
    EXPECT_EQ(parseExpr("-2 * 3"), "(* (- 2) 3)");
    EXPECT_EQ(parseExpr("1 - -2"), "(- 1 (- 2))");
}

TEST(Parser, FunctionCalls) {
    EXPECT_EQ(parseExpr("f()"), "(call f)");
    EXPECT_EQ(parseExpr("f(1, x + 2)"), "(call f 1 (+ x 2))");
    EXPECT_EQ(parseExpr("f(g(1)) + 1"), "(+ (call f (call g 1)) 1)");
}

// ----------------------------------------------------------------- statements

TEST(Parser, LetWithAndWithoutAnnotation) {
    EXPECT_EQ(parse("let x = 5;"), "(let x 5)\n");
    EXPECT_EQ(parse("let ok: bool = true;"), "(let ok true)\n");
}

TEST(Parser, AssignmentIsDistinguishedFromExpression) {
    EXPECT_EQ(parse("x = x + 1;"), "(set x (+ x 1))\n");
    EXPECT_EQ(parse("x == 1;"), "(== x 1)\n");
}

TEST(Parser, IfElseChain) {
    EXPECT_EQ(parse("if a { print(1); } else if b { print(2); } else { print(3); }"),
              "(if a (block (print 1)) (if b (block (print 2)) (block (print 3))))\n");
}

TEST(Parser, WhileLoop) {
    EXPECT_EQ(parse("while i < 3 { i = i + 1; }"), "(while (< i 3) (block (set i (+ i 1))))\n");
}

TEST(Parser, FunctionDeclaration) {
    EXPECT_EQ(parse("fn add(a: int, b: int) -> int { return a + b; }"),
              "(fn add (a:int b:int) int (block (return (+ a b))))\n");
    EXPECT_EQ(parse("fn hello() { print(1); return; }"), "(fn hello () void (block (print 1) (return)))\n");
}

TEST(Parser, FunctionsAndTopLevelCodeCanBeMixed) {
    EXPECT_EQ(parse("print(1); fn f() { } print(2);"), "(fn f () void (block))\n(print 1)\n(print 2)\n");
}

// --------------------------------------------------------------------- errors

TEST(Parser, MissingSemicolon) {
    EXPECT_NE(syntaxErrorOf("let x = 5").find("expected ';'"), std::string::npos);
}

TEST(Parser, MissingClosingParenthesis) {
    EXPECT_NE(syntaxErrorOf("print((1 + 2);").find("expected ')'"), std::string::npos);
}

TEST(Parser, MissingExpression) {
    EXPECT_NE(syntaxErrorOf("let x = ;").find("expected an expression"), std::string::npos);
    EXPECT_NE(syntaxErrorOf("1 + ;").find("expected an expression"), std::string::npos);
}

TEST(Parser, UnclosedBlock) {
    EXPECT_NE(syntaxErrorOf("if true { print(1);").find("expected '}'"), std::string::npos);
}

TEST(Parser, ParametersNeedTypes) {
    EXPECT_NE(syntaxErrorOf("fn f(a) { }").find("expected ':'"), std::string::npos);
    EXPECT_NE(syntaxErrorOf("fn f(a: string) { }").find("expected a type"), std::string::npos);
}

TEST(Parser, LetNeedsAnInitializer) {
    EXPECT_NE(syntaxErrorOf("let x: int;").find("expected '='"), std::string::npos);
}

TEST(Parser, NestedFunctionsAreRejected) {
    EXPECT_NE(syntaxErrorOf("fn outer() { fn inner() { } }").find("top level"), std::string::npos);
}

TEST(Parser, ErrorPointsAtTheOffendingToken) {
    try {
        parse("let x = 1;\nlet y = (2 + ;");
        FAIL() << "expected a SyntaxError";
    } catch (const SyntaxError& error) {
        EXPECT_EQ(error.loc.line, 2);
        EXPECT_EQ(error.loc.col, 14);
    }
}
