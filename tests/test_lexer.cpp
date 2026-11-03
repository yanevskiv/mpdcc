// Tests of the lexer.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

// Split a source and return the kinds of its tokens, without the end of file.
static std::vector<TokenKind> kinds_of(const std::string &source, std::string *messages = nullptr)
{
    std::ostringstream stream;
    Diagnostics diagnostics("test.mpd", source, stream);
    std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    if (messages) {
        *messages = stream.str();
    }
    std::vector<TokenKind> kinds;
    for (const Token &token : tokens) {
        if (token.m_kind != TokenKind::Eof) {
            kinds.push_back(token.m_kind);
        }
    }
    return kinds;
}

TEST_CASE("Keywords match in any case, as the slides write Module and Process", "[lexer]")
{
    using K = TokenKind;
    std::vector<TokenKind> expected = {K::Module, K::Body, K::End, K::Process, K::Proc, K::Op,
                                       K::In,     K::Ni,   K::And, K::By,      K::Identifier};
    CHECK(kinds_of("Module Body End Process Proc op in ni and by elem") == expected);
}

TEST_CASE("Both spellings of the arrow and the box, and the C operators", "[lexer]")
{
    using K = TokenKind;
    std::vector<TokenKind> expected = {
        K::Arrow,       K::Arrow,     K::Box,       K::Box,     K::LeftBracket, K::RightBracket,
        K::Equal,       K::NotEqual,  K::LessEqual, K::GreaterEqual, K::AndAnd, K::OrOr,
        K::PlusPlus,    K::MinusMinus, K::PlusAssign, K::MinusAssign, K::StarAssign, K::Percent,
        K::Question,    K::Dot,       K::Assign,    K::Bang,
    };
    CHECK(kinds_of("-> → [] □ [ ] == != <= >= && || ++ -- += -= *= % ? . = !") == expected);
}

TEST_CASE("Comments start with #, // or /*", "[lexer]")
{
    using K = TokenKind;
    std::vector<TokenKind> expected = {K::Identifier, K::Identifier, K::Identifier};
    CHECK(kinds_of("a # one\nb // two\n/* three\n */ c") == expected);
    std::string messages;
    kinds_of("a /* never closed", &messages);
    CHECK(contains(messages, "ERR_LEX_UNTERMINATED"));
}

TEST_CASE("Characters and strings decode their escapes", "[lexer]")
{
    std::ostringstream stream;
    std::string source = R"('a' '\n' '\'' "say \"hi\"\n")";
    Diagnostics diagnostics("test.mpd", source, stream);
    std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    REQUIRE(tokens.size() == 5);
    CHECK(tokens[0].m_integer == 'a');
    CHECK(tokens[1].m_integer == '\n');
    CHECK(tokens[2].m_integer == '\'');
    CHECK(tokens[3].m_kind == TokenKind::String);
    CHECK(tokens[3].m_text == "say \"hi\"\n");
    CHECK(stream.str().empty());
}

TEST_CASE("Numbers: integers, reals and integers that are too large", "[lexer]")
{
    using K = TokenKind;
    std::vector<TokenKind> expected = {K::Integer, K::Real, K::Real, K::Real};
    CHECK(kinds_of("42 2.5 1e3 4.5E-2") == expected);
    std::string messages;
    kinds_of("2147483648", &messages);
    CHECK(contains(messages, "ERR_LEX_INT_RANGE"));
}

TEST_CASE("Lexical errors: a lone & or |, a bad escape, a Cyrillic letter", "[lexer]")
{
    std::string messages;
    kinds_of("a & b", &messages);
    CHECK(contains(messages, "ERR_LEX_BAD_CHAR"));
    CHECK(contains(messages, "&& and ||"));
    kinds_of(R"('\q')", &messages);
    CHECK(contains(messages, "ERR_LEX_BAD_ESCAPE"));
    kinds_of("рrocess", &messages);
    CHECK(contains(messages, "U+0440"));
}
