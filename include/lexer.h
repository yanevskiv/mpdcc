// Splits an MPD source into tokens.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_LEXER_H
#define MPDCC_LEXER_H

#include "diagnostics.h"
#include "token.h"

#include <cstddef>
#include <ostream>
#include <string_view>
#include <vector>

// Splits MPD source into tokens, with the slides' symbols and their ASCII spellings.
class Lexer
{
public:
    // Prepare to split one source.
    Lexer(std::string_view source, Diagnostics &diagnostics);

    // Split the whole source.
    std::vector<Token> tokenize();

    // Name a kind of token for diagnostics.
    static std::string_view kind_name(TokenKind kind);

    // Print tokens one per line, for -fdump-tokens.
    static void dump(const std::vector<Token> &tokens, std::ostream &stream);

private:
    // Look at a byte ahead of the current position.
    unsigned char peek(size_t ahead = 0) const;

    // Move past bytes, counting lines and characters.
    void advance(size_t count = 1);

    // Return the current position.
    Location here() const;

    // Add a token spelled by the bytes at the current position and move past them.
    void push(TokenKind kind, const Location &location, size_t count);

    // Decode the UTF-8 character at the current position.
    bool decode(unsigned &codePoint, size_t &length) const;

    // Scan an identifier or a keyword.
    void scan_identifier(const Location &location);

    // Scan an integer or a real number.
    void scan_number(const Location &location);

    // Scan one character of a quoted literal, decoding escapes.
    bool scan_quoted(char quote, std::string &text);

    // Scan a character literal.
    void scan_character(const Location &location);

    // Scan a string literal.
    void scan_string(const Location &location);

    // Scan a non-ASCII character: one of the slides' symbols, or an error.
    void scan_symbol(const Location &location);

    // Scan an ASCII operator or punctuation mark.
    void scan_punctuation(const Location &location);

    // Skip a comment, if one starts at the current position.
    bool skip_comment();

    // Text being split.
    std::string_view m_source;

    // Destination of lexical errors.
    Diagnostics &m_diagnostics;

    // Offset of the next byte.
    size_t m_position = 0;

    // Line of the next character.
    int m_line = 1;

    // Column of the next character.
    int m_column = 1;

    // Tokens scanned so far.
    std::vector<Token> m_tokens;
};

#endif
