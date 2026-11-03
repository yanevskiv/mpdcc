// Parses tokens into a syntax tree.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_PARSER_H
#define MPDCC_PARSER_H

#include "ast.h"
#include "diagnostics.h"
#include "token.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

// Parses MPD tokens into a syntax tree, by recursive descent.
class Parser
{
public:
    // Prepare to parse tokens.
    Parser(const std::vector<Token> &tokens, Diagnostics &diagnostics);

    // Parse a whole program, or return null after an error.
    std::unique_ptr<Ast> parse_program();

    // Parse one expression with nothing after it, such as the value of -D.
    std::unique_ptr<Expression> parse_lone_expression();

private:
    // Look at a token ahead of the current one.
    const Token &peek(size_t ahead = 0) const;

    // Check the kind of a token ahead.
    bool is(TokenKind kind, size_t ahead = 0) const;

    // Move past the current token.
    const Token &next();

    // Move past the current token if it has a kind.
    bool accept(TokenKind kind);

    // Move past a token that must have a kind, or stop with an error.
    const Token &expect(TokenKind kind, std::string_view context);

    // Report an error and stop the parse.
    [[noreturn]] void fail(const Location &location, std::string_view code,
                           std::string_view message);

    // Check whether a type starts at a token.
    bool starts_type(size_t ahead) const;

    // Check whether a declaration, type name ..., starts at a token.
    bool starts_declaration(size_t ahead) const;

    // Check whether an identifier ahead is a mode: val, var, res or result.
    bool is_mode(size_t ahead) const;

    // Parse const [type] NAME = value;.
    void parse_constant(Ast &ast);

    // Parse type NAME = type;.
    void parse_alias(Ast &ast);

    // Parse module NAME ops body ... end NAME;.
    void parse_module(Ast &ast);

    // Parse what may appear in a module's body or at the top level.
    bool parse_member(Module &module);

    // Parse op NAME(formals) returns type;.
    OpDecl parse_op();

    // Parse proc NAME(names) returns name { ... }.
    ProcDecl parse_proc();

    // Parse process NAME[i = a to b] { ... }.
    ProcessDecl parse_process();

    // Parse a type: int, bool, char, real, sem or an alias.
    TypeName parse_type(std::string_view context);

    // Parse (NAME, NAME, ...), the formals of a proc or an arm.
    void parse_names(std::vector<std::string> &names, std::vector<Location> &locations);

    // Parse { statements }.
    std::vector<std::unique_ptr<Statement>> parse_block();

    // Parse one statement.
    std::unique_ptr<Statement> parse_statement();

    // Parse type name [size] = value, ...;.
    std::unique_ptr<Statement> parse_declaration();

    // Parse if (condition) statement else statement.
    std::unique_ptr<Statement> parse_if();

    // Parse while (condition) statement.
    std::unique_ptr<Statement> parse_while();

    // Parse for [i = a to b st condition] statement.
    std::unique_ptr<Statement> parse_for();

    // Parse in arm [] arm ... ni.
    std::unique_ptr<Statement> parse_in();

    // Parse op(formals) returns name and guard by priority -> statements.
    Arm parse_arm();

    // Parse write(values);.
    std::unique_ptr<Statement> parse_write();

    // Parse a statement that starts with a name.
    std::unique_ptr<Statement> parse_named();

    // Parse NAME or M.NAME and (arguments), an invocation.
    std::unique_ptr<Expression> parse_invocation(std::string_view context);

    // Parse (expression, ...).
    void parse_arguments(std::vector<std::unique_ptr<Expression>> &arguments);

    // Parse an expression.
    std::unique_ptr<Expression> parse_expression();

    // Parse operands joined by the operators of one level of precedence.
    std::unique_ptr<Expression> parse_binary(int level);

    // Parse -x, !x or a primary expression.
    std::unique_ptr<Expression> parse_unary();

    // Parse a literal, a name, an element, an invocation, ?op or (expression).
    std::unique_ptr<Expression> parse_primary();

    // Tokens being parsed.
    const std::vector<Token> &m_tokens;

    // Destination of parse errors.
    Diagnostics &m_diagnostics;

    // Index of the current token.
    size_t m_position = 0;
};

#endif
