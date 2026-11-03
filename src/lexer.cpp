// Splits an MPD source into tokens.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

// Largest integer literal, Ada's Integer'Last.
constexpr long long maxInteger = 2147483647LL;

// Keywords, matched in any case, since the slides write Module, Body and Process.
const std::map<std::string, TokenKind, std::less<>> keywords = {
    {"module", TokenKind::Module},   {"body", TokenKind::Body},
    {"end", TokenKind::End},         {"op", TokenKind::Op},
    {"proc", TokenKind::Proc},       {"process", TokenKind::Process},
    {"returns", TokenKind::Returns}, {"call", TokenKind::Call},
    {"send", TokenKind::Send},       {"receive", TokenKind::Receive},
    {"in", TokenKind::In},           {"ni", TokenKind::Ni},
    {"and", TokenKind::And},         {"by", TokenKind::By},
    {"if", TokenKind::If},           {"else", TokenKind::Else},
    {"while", TokenKind::While},     {"for", TokenKind::For},
    {"to", TokenKind::To},           {"skip", TokenKind::Skip},
    {"true", TokenKind::True},       {"false", TokenKind::False},
    {"const", TokenKind::Const},     {"type", TokenKind::Type},
    {"int", TokenKind::Int},         {"bool", TokenKind::Bool},
    {"char", TokenKind::Char},       {"real", TokenKind::RealType},
    {"sem", TokenKind::Sem},         {"write", TokenKind::Write},
};

// Tokens spelled with one non-ASCII character, as on the slides.
const std::map<unsigned, TokenKind> unicodeSymbols = {
    {0x2192, TokenKind::Arrow},     {0x25A1, TokenKind::Box},
    {0x2260, TokenKind::NotEqual},  {0x2264, TokenKind::LessEqual},
    {0x2265, TokenKind::GreaterEqual},
};

// Spellings of the kinds of tokens, for diagnostics.
const std::map<TokenKind, std::string_view> kindNames = {
    {TokenKind::Eof, "end of file"},
    {TokenKind::Identifier, "identifier"},
    {TokenKind::Integer, "integer"},
    {TokenKind::Real, "real number"},
    {TokenKind::Character, "character"},
    {TokenKind::String, "string"},
    {TokenKind::LeftParen, "'('"},
    {TokenKind::RightParen, "')'"},
    {TokenKind::LeftBracket, "'['"},
    {TokenKind::RightBracket, "']'"},
    {TokenKind::LeftBrace, "'{'"},
    {TokenKind::RightBrace, "'}'"},
    {TokenKind::Comma, "','"},
    {TokenKind::Semicolon, "';'"},
    {TokenKind::Dot, "'.'"},
    {TokenKind::Arrow, "'->' or '→'"},
    {TokenKind::Box, "'[]' or '□'"},
    {TokenKind::Assign, "'='"},
    {TokenKind::PlusAssign, "'+='"},
    {TokenKind::MinusAssign, "'-='"},
    {TokenKind::StarAssign, "'*='"},
    {TokenKind::PlusPlus, "'++'"},
    {TokenKind::MinusMinus, "'--'"},
    {TokenKind::Equal, "'=='"},
    {TokenKind::NotEqual, "'!='"},
    {TokenKind::Less, "'<'"},
    {TokenKind::LessEqual, "'<='"},
    {TokenKind::Greater, "'>'"},
    {TokenKind::GreaterEqual, "'>='"},
    {TokenKind::Plus, "'+'"},
    {TokenKind::Minus, "'-'"},
    {TokenKind::Star, "'*'"},
    {TokenKind::Slash, "'/'"},
    {TokenKind::Percent, "'%'"},
    {TokenKind::AndAnd, "'&&'"},
    {TokenKind::OrOr, "'||'"},
    {TokenKind::Bang, "'!'"},
    {TokenKind::Question, "'?'"},
    {TokenKind::Module, "'module'"},
    {TokenKind::Body, "'body'"},
    {TokenKind::End, "'end'"},
    {TokenKind::Op, "'op'"},
    {TokenKind::Proc, "'proc'"},
    {TokenKind::Process, "'process'"},
    {TokenKind::Returns, "'returns'"},
    {TokenKind::Call, "'call'"},
    {TokenKind::Send, "'send'"},
    {TokenKind::Receive, "'receive'"},
    {TokenKind::In, "'in'"},
    {TokenKind::Ni, "'ni'"},
    {TokenKind::And, "'and'"},
    {TokenKind::By, "'by'"},
    {TokenKind::If, "'if'"},
    {TokenKind::Else, "'else'"},
    {TokenKind::While, "'while'"},
    {TokenKind::For, "'for'"},
    {TokenKind::To, "'to'"},
    {TokenKind::Skip, "'skip'"},
    {TokenKind::True, "'true'"},
    {TokenKind::False, "'false'"},
    {TokenKind::Const, "'const'"},
    {TokenKind::Type, "'type'"},
    {TokenKind::Int, "'int'"},
    {TokenKind::Bool, "'bool'"},
    {TokenKind::Char, "'char'"},
    {TokenKind::RealType, "'real'"},
    {TokenKind::Sem, "'sem'"},
    {TokenKind::Write, "'write'"},
};

// Format a code point as U+XXXX.
static std::string code_point_name(unsigned codePoint)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "U+%04X", codePoint);
    return buffer;
}

// Prepare to split one source.
Lexer::Lexer(std::string_view source, Diagnostics &diagnostics)
    : m_source(source)
    , m_diagnostics(diagnostics)
{}

// Split the whole source, skipping blanks and comments.
std::vector<Token> Lexer::tokenize()
{
    while (m_position < m_source.size()) {
        unsigned char c = peek();
        if (std::isspace(c)) {
            advance();
            continue;
        }
        if (skip_comment()) {
            continue;
        }
        Location location = here();
        if (std::isalpha(c)) {
            scan_identifier(location);
        } else if (std::isdigit(c)) {
            scan_number(location);
        } else if (c == '\'') {
            scan_character(location);
        } else if (c == '"') {
            scan_string(location);
        } else if (c >= 0x80) {
            scan_symbol(location);
        } else {
            scan_punctuation(location);
        }
    }
    Token eof;
    eof.m_location = here();
    m_tokens.push_back(eof);
    return std::move(m_tokens);
}

// Name a kind of token.
std::string_view Lexer::kind_name(TokenKind kind)
{
    auto name = kindNames.find(kind);
    return name == kindNames.end() ? "token" : name->second;
}

// Print each token's position, kind and value.
void Lexer::dump(const std::vector<Token> &tokens, std::ostream &stream)
{
    for (const Token &token : tokens) {
        stream << token.m_location.m_line << ':' << token.m_location.m_column << ' '
               << kind_name(token.m_kind);
        if (token.m_kind == TokenKind::Identifier || token.m_kind == TokenKind::String) {
            stream << ' ' << token.m_text;
        } else if (token.m_kind == TokenKind::Integer || token.m_kind == TokenKind::Character) {
            stream << ' ' << token.m_integer;
        } else if (token.m_kind == TokenKind::Real) {
            stream << ' ' << token.m_real;
        }
        stream << '\n';
    }
}

// Look at a byte ahead.
unsigned char Lexer::peek(size_t ahead) const
{
    size_t at = m_position + ahead;
    return at < m_source.size() ? static_cast<unsigned char>(m_source[at]) : 0;
}

// Move past bytes, counting a column per UTF-8 character.
void Lexer::advance(size_t count)
{
    for (size_t i = 0; i < count && m_position < m_source.size(); i++, m_position++) {
        unsigned char c = m_source[m_position];
        if (c == '\n') {
            m_line++;
            m_column = 1;
        } else if ((c & 0xC0) != 0x80) {
            m_column++;
        }
    }
}

// Return the current position.
Location Lexer::here() const
{
    Location location;
    location.m_line = m_line;
    location.m_column = m_column;
    return location;
}

// Add a token and move past its bytes.
void Lexer::push(TokenKind kind, const Location &location, size_t count)
{
    Token token;
    token.m_kind = kind;
    token.m_location = location;
    token.m_text = std::string(m_source.substr(m_position, count));
    m_tokens.push_back(token);
    advance(count);
}

// Decode one UTF-8 character.
bool Lexer::decode(unsigned &codePoint, size_t &length) const
{
    unsigned char c = peek();
    if (c < 0x80) {
        codePoint = c;
        length = 1;
        return true;
    }
    if ((c & 0xE0) == 0xC0) {
        length = 2;
        codePoint = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        length = 3;
        codePoint = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        length = 4;
        codePoint = c & 0x07;
    } else {
        length = 1;
        return false;
    }
    for (size_t i = 1; i < length; i++) {
        unsigned char next = peek(i);
        if ((next & 0xC0) != 0x80) {
            length = i;
            return false;
        }
        codePoint = (codePoint << 6) | (next & 0x3F);
    }
    return true;
}

// Scan an identifier or a keyword, rejecting a non-ASCII letter glued to it.
void Lexer::scan_identifier(const Location &location)
{
    size_t count = 0;
    while (std::isalnum(peek(count)) || peek(count) == '_') {
        count++;
    }
    std::string word(m_source.substr(m_position, count));
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto keyword = keywords.find(lower);
    push(keyword == keywords.end() ? TokenKind::Identifier : keyword->second, location, count);
    if (peek() >= 0x80) {
        unsigned codePoint = 0;
        size_t length = 1;
        decode(codePoint, length);
        m_diagnostics.error(here(), "ERR_LEX_NON_ASCII_IDENT",
                            "a non-ASCII character (" + code_point_name(codePoint) +
                                ") right after '" + word + "'");
        m_diagnostics.note(here(), "identifiers are ASCII letters, digits and '_'");
        advance(length);
    }
}

// Scan an integer or a real number such as 2.5 or 1e3.
void Lexer::scan_number(const Location &location)
{
    size_t count = 0;
    while (std::isdigit(peek(count))) {
        count++;
    }
    bool real = false;
    if (peek(count) == '.' && std::isdigit(peek(count + 1))) {
        real = true;
        count++;
        while (std::isdigit(peek(count))) {
            count++;
        }
    }
    unsigned char sign = peek(count + 1);
    if ((peek(count) == 'e' || peek(count) == 'E') &&
        (std::isdigit(sign) || ((sign == '+' || sign == '-') && std::isdigit(peek(count + 2))))) {
        real = true;
        count += 2;
        while (std::isdigit(peek(count))) {
            count++;
        }
    }
    std::string text(m_source.substr(m_position, count));
    push(real ? TokenKind::Real : TokenKind::Integer, location, count);
    Token &token = m_tokens.back();
    if (real) {
        token.m_real = std::strtod(text.c_str(), nullptr);
    } else {
        long long value = 0;
        auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (status != std::errc() || end != text.data() + text.size() || value > maxInteger) {
            m_diagnostics.error(location, "ERR_LEX_INT_RANGE",
                                "the integer " + text + " is larger than 2147483647");
            value = 0;
        }
        token.m_integer = value;
    }
    if (std::isalpha(peek()) || peek() == '_') {
        m_diagnostics.error(here(), "ERR_LEX_BAD_NUMBER",
                            "a letter right after the number " + text);
        while (std::isalnum(peek()) || peek() == '_') {
            advance();
        }
    }
}

// Scan one character up to the closing quote, decoding \n, \t, \\ and \' or \".
bool Lexer::scan_quoted(char quote, std::string &text)
{
    unsigned char c = peek();
    if (!c || c == '\n' || c == static_cast<unsigned char>(quote)) {
        return false;
    }
    if (c != '\\') {
        text += static_cast<char>(c);
        advance();
        return true;
    }
    unsigned char escaped = peek(1);
    switch (escaped) {
        case 'n':
            text += '\n';
            break;
        case 't':
            text += '\t';
            break;
        case '0':
            text += '\0';
            break;
        case '\\':
        case '\'':
        case '"':
            text += static_cast<char>(escaped);
            break;
        default:
            m_diagnostics.error(here(), "ERR_LEX_BAD_ESCAPE",
                                std::string("unknown escape '\\") + static_cast<char>(escaped) +
                                    "'; the escapes are \\n, \\t, \\0, \\\\, \\' and \\\"");
            text += static_cast<char>(escaped);
            break;
    }
    advance(escaped ? 2 : 1);
    return true;
}

// Scan a character literal such as 'a' or '\n'.
void Lexer::scan_character(const Location &location)
{
    size_t start = m_position;
    advance();
    std::string text;
    while (scan_quoted('\'', text)) {
    }
    if (peek() != '\'') {
        m_diagnostics.error(location, "ERR_LEX_UNTERMINATED",
                            "a character literal with no closing quote");
        return;
    }
    advance();
    if (text.size() != 1 || static_cast<unsigned char>(text[0]) >= 0x80) {
        m_diagnostics.error(location, "ERR_LEX_BAD_CHAR_LITERAL",
                            "a character literal holds one ASCII character");
        return;
    }
    Token token;
    token.m_kind = TokenKind::Character;
    token.m_location = location;
    token.m_text = std::string(m_source.substr(start, m_position - start));
    token.m_integer = static_cast<unsigned char>(text[0]);
    m_tokens.push_back(token);
}

// Scan a string literal, which only write prints.
void Lexer::scan_string(const Location &location)
{
    advance();
    std::string text;
    while (scan_quoted('"', text)) {
    }
    if (peek() != '"') {
        m_diagnostics.error(location, "ERR_LEX_UNTERMINATED", "a string with no closing quote");
        return;
    }
    advance();
    Token token;
    token.m_kind = TokenKind::String;
    token.m_location = location;
    token.m_text = text;
    m_tokens.push_back(token);
}

// Scan one of the slides' symbols, or report the character, with a hint for look-alikes.
void Lexer::scan_symbol(const Location &location)
{
    unsigned codePoint = 0;
    size_t length = 1;
    if (!decode(codePoint, length)) {
        m_diagnostics.error(location, "ERR_LEX_BAD_UTF8", "a byte that isn't UTF-8");
        advance(length);
        return;
    }
    auto symbol = unicodeSymbols.find(codePoint);
    if (symbol != unicodeSymbols.end()) {
        push(symbol->second, location, length);
        return;
    }
    m_diagnostics.error(location, "ERR_LEX_BAD_CHAR",
                        "unexpected character " + std::string(m_source.substr(m_position, length)) +
                            " (" + code_point_name(codePoint) + ")");
    if ((codePoint >= 0x400 && codePoint <= 0x4FF) || codePoint < 0x250) {
        m_diagnostics.note(location, "identifiers are ASCII; a Cyrillic or accented letter looks "
                                     "like a Latin one but isn't");
    }
    advance(length);
}

// Scan an ASCII operator, preferring the longest spelling.
void Lexer::scan_punctuation(const Location &location)
{
    using Kind = TokenKind;
    unsigned char c = peek();
    unsigned char next = peek(1);
    switch (c) {
        case '(':
            return push(Kind::LeftParen, location, 1);
        case ')':
            return push(Kind::RightParen, location, 1);
        case '[':
            return next == ']' ? push(Kind::Box, location, 2) : push(Kind::LeftBracket, location, 1);
        case ']':
            return push(Kind::RightBracket, location, 1);
        case '{':
            return push(Kind::LeftBrace, location, 1);
        case '}':
            return push(Kind::RightBrace, location, 1);
        case ',':
            return push(Kind::Comma, location, 1);
        case ';':
            return push(Kind::Semicolon, location, 1);
        case '.':
            return push(Kind::Dot, location, 1);
        case '?':
            return push(Kind::Question, location, 1);
        case '%':
            return push(Kind::Percent, location, 1);
        case '/':
            return push(Kind::Slash, location, 1);
        case '-':
            if (next == '>') {
                return push(Kind::Arrow, location, 2);
            }
            if (next == '-') {
                return push(Kind::MinusMinus, location, 2);
            }
            return next == '=' ? push(Kind::MinusAssign, location, 2) : push(Kind::Minus, location, 1);
        case '+':
            if (next == '+') {
                return push(Kind::PlusPlus, location, 2);
            }
            return next == '=' ? push(Kind::PlusAssign, location, 2) : push(Kind::Plus, location, 1);
        case '*':
            return next == '=' ? push(Kind::StarAssign, location, 2) : push(Kind::Star, location, 1);
        case '=':
            return next == '=' ? push(Kind::Equal, location, 2) : push(Kind::Assign, location, 1);
        case '!':
            return next == '=' ? push(Kind::NotEqual, location, 2) : push(Kind::Bang, location, 1);
        case '<':
            return next == '=' ? push(Kind::LessEqual, location, 2) : push(Kind::Less, location, 1);
        case '>':
            return next == '=' ? push(Kind::GreaterEqual, location, 2)
                               : push(Kind::Greater, location, 1);
        case '&':
            if (next == '&') {
                return push(Kind::AndAnd, location, 2);
            }
            break;
        case '|':
            if (next == '|') {
                return push(Kind::OrOr, location, 2);
            }
            break;
        default:
            break;
    }
    m_diagnostics.error(location, "ERR_LEX_BAD_CHAR",
                        std::string("unexpected character '") + static_cast<char>(c) + "'");
    if (c == '&' || c == '|') {
        m_diagnostics.note(location, "conditions are joined with && and ||");
    }
    advance();
}

// Skip a # or // comment up to the end of its line, or a /* ... */ comment.
bool Lexer::skip_comment()
{
    if (peek() == '/' && peek(1) == '*') {
        Location location = here();
        advance(2);
        while (peek() && !(peek() == '*' && peek(1) == '/')) {
            advance();
        }
        if (!peek()) {
            m_diagnostics.error(location, "ERR_LEX_UNTERMINATED", "a /* comment with no */");
            return true;
        }
        advance(2);
        return true;
    }
    if (peek() != '#' && !(peek() == '/' && peek(1) == '/')) {
        return false;
    }
    while (peek() && peek() != '\n') {
        advance();
    }
    return true;
}
