#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "lexer.h"

using sprig::Lexer;
using sprig::SyntaxError;
using sprig::Token;
using sprig::TokenKind;

namespace {

std::vector<TokenKind> kindsOf(const std::string& source) {
    std::vector<TokenKind> kinds;
    for (const Token& tok : Lexer(source).tokenize()) kinds.push_back(tok.kind);
    return kinds;
}

}  // namespace

TEST(Lexer, EmptyInputIsJustEof) {
    EXPECT_EQ(kindsOf(""), (std::vector<TokenKind>{TokenKind::Eof}));
    EXPECT_EQ(kindsOf("   \n\t  "), (std::vector<TokenKind>{TokenKind::Eof}));
}

TEST(Lexer, RecognizesKeywordsAndNames) {
    EXPECT_EQ(kindsOf("fn let if else while return print true false int bool"),
              (std::vector<TokenKind>{TokenKind::Fn, TokenKind::Let, TokenKind::If, TokenKind::Else, TokenKind::While,
                                      TokenKind::Return, TokenKind::Print, TokenKind::True, TokenKind::False,
                                      TokenKind::KwInt, TokenKind::KwBool, TokenKind::Eof}));
}

TEST(Lexer, KeywordPrefixIsStillAName) {
    // "iffy" and "letter" start with keywords but are ordinary identifiers.
    const auto tokens = Lexer("iffy letter _x x1").tokenize();
    ASSERT_EQ(tokens.size(), 5u);
    for (std::size_t i = 0; i < 4; ++i) EXPECT_EQ(tokens[i].kind, TokenKind::Ident);
    EXPECT_EQ(tokens[0].text, "iffy");
    EXPECT_EQ(tokens[2].text, "_x");
}

TEST(Lexer, TwoCharacterOperatorsWinOverOneCharacterOnes) {
    EXPECT_EQ(kindsOf("== = != ! <= < >= > -> - && ||"),
              (std::vector<TokenKind>{TokenKind::EqEq, TokenKind::Assign, TokenKind::BangEq, TokenKind::Bang,
                                      TokenKind::LtEq, TokenKind::Lt, TokenKind::GtEq, TokenKind::Gt, TokenKind::Arrow,
                                      TokenKind::Minus, TokenKind::AmpAmp, TokenKind::PipePipe, TokenKind::Eof}));
}

TEST(Lexer, OperatorsNeedNoSpaces) {
    EXPECT_EQ(kindsOf("a<=b"),
              (std::vector<TokenKind>{TokenKind::Ident, TokenKind::LtEq, TokenKind::Ident, TokenKind::Eof}));
}

TEST(Lexer, ReadsIntegerValues) {
    const auto tokens = Lexer("0 42 9223372036854775807").tokenize();
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].value, 0);
    EXPECT_EQ(tokens[1].value, 42);
    EXPECT_EQ(tokens[2].value, std::numeric_limits<std::int64_t>::max());
}

TEST(Lexer, RejectsIntegerThatDoesNotFit) {
    // One more than the largest 64-bit signed value.
    EXPECT_THROW(Lexer("9223372036854775808").tokenize(), SyntaxError);
    EXPECT_THROW(Lexer("99999999999999999999999999").tokenize(), SyntaxError);
}

TEST(Lexer, SkipsComments) {
    EXPECT_EQ(kindsOf("1 // this is ignored + - *\n2"),
              (std::vector<TokenKind>{TokenKind::Int, TokenKind::Int, TokenKind::Eof}));
    EXPECT_EQ(kindsOf("// only a comment"), (std::vector<TokenKind>{TokenKind::Eof}));
}

TEST(Lexer, SlashAloneIsDivision) {
    EXPECT_EQ(kindsOf("6 / 2"),
              (std::vector<TokenKind>{TokenKind::Int, TokenKind::Slash, TokenKind::Int, TokenKind::Eof}));
}

TEST(Lexer, TracksLineAndColumn) {
    const auto tokens = Lexer("let x\n  = 10;").tokenize();
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].loc.line, 1);
    EXPECT_EQ(tokens[0].loc.col, 1);
    EXPECT_EQ(tokens[1].loc.col, 5);   // x
    EXPECT_EQ(tokens[2].loc.line, 2);  // =
    EXPECT_EQ(tokens[2].loc.col, 3);
    EXPECT_EQ(tokens[3].loc.col, 5);   // 10
}

TEST(Lexer, ReportsWhereTheBadCharacterIs) {
    try {
        Lexer("let x = 1;\nlet y = @;").tokenize();
        FAIL() << "expected a SyntaxError";
    } catch (const SyntaxError& error) {
        EXPECT_EQ(error.loc.line, 2);
        EXPECT_EQ(error.loc.col, 9);
        EXPECT_NE(std::string(error.what()).find("'@'"), std::string::npos);
    }
}

TEST(Lexer, RejectsSingleAmpersandAndPipe) {
    EXPECT_THROW(Lexer("a & b").tokenize(), SyntaxError);
    EXPECT_THROW(Lexer("a | b").tokenize(), SyntaxError);
}

TEST(Lexer, RejectsNameStartingWithDigit) {
    EXPECT_THROW(Lexer("let 2fast = 1;").tokenize(), SyntaxError);
}
