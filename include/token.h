// Tokens and their positions in an MPD source.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_TOKEN_H
#define MPDCC_TOKEN_H

#include <string>

// Describes a position in an MPD source file.
struct Location
{
    // Line counted from 1, or 0 when there is no position.
    int m_line = 0;

    // Column in characters counted from 1.
    int m_column = 0;
};

// Describes the kind of an MPD token.
enum class TokenKind {
    Eof,
    Identifier,
    Integer,
    Real,
    Character,
    String,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Comma,
    Semicolon,
    Dot,
    Arrow,
    Box,
    Assign,
    PlusAssign,
    MinusAssign,
    StarAssign,
    PlusPlus,
    MinusMinus,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    AndAnd,
    OrOr,
    Bang,
    Question,
    Module,
    Body,
    End,
    Op,
    Proc,
    Process,
    Returns,
    Call,
    Send,
    Receive,
    In,
    Ni,
    And,
    By,
    If,
    Else,
    While,
    For,
    To,
    Skip,
    True,
    False,
    Const,
    Type,
    Int,
    Bool,
    Char,
    RealType,
    Sem,
    Write,
};

// Describes an MPD token, its spelling and its value.
struct Token
{
    // Kind of the token.
    TokenKind m_kind = TokenKind::Eof;

    // Spelling of the token in the source, or the text of a string.
    std::string m_text;

    // Position of the token's first character.
    Location m_location;

    // Value of an integer or the code of a character.
    long long m_integer = 0;

    // Value of a real number.
    double m_real = 0;
};

#endif
