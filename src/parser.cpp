// Parses tokens into a syntax tree.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "parser.h"

#include "lexer.h"

#include <string>
#include <utility>

// Binary operators by level of precedence, loosest first.
static const std::vector<std::vector<TokenKind>> binaryLevels = {
    {TokenKind::OrOr},
    {TokenKind::AndAnd},
    {TokenKind::Equal, TokenKind::NotEqual},
    {TokenKind::Less, TokenKind::LessEqual, TokenKind::Greater, TokenKind::GreaterEqual},
    {TokenKind::Plus, TokenKind::Minus},
    {TokenKind::Star, TokenKind::Slash, TokenKind::Percent},
};

// Spell an operator as the syntax tree keeps it.
static std::string operator_text(TokenKind kind)
{
    switch (kind) {
        case TokenKind::OrOr:
            return "||";
        case TokenKind::AndAnd:
            return "&&";
        case TokenKind::Equal:
            return "==";
        case TokenKind::NotEqual:
            return "!=";
        case TokenKind::Less:
            return "<";
        case TokenKind::LessEqual:
            return "<=";
        case TokenKind::Greater:
            return ">";
        case TokenKind::GreaterEqual:
            return ">=";
        case TokenKind::Plus:
            return "+";
        case TokenKind::Minus:
            return "-";
        case TokenKind::Star:
            return "*";
        case TokenKind::Slash:
            return "/";
        case TokenKind::Percent:
            return "%";
        default:
            return "?";
    }
}

// Make an expression of a kind at a position.
static std::unique_ptr<Expression> make_expression(ExpressionKind kind, const Location &location)
{
    auto expression = std::make_unique<Expression>();
    expression->m_kind = kind;
    expression->m_location = location;
    return expression;
}

// Make a statement of a kind at a position.
static std::unique_ptr<Statement> make_statement(StatementKind kind, const Location &location)
{
    auto statement = std::make_unique<Statement>();
    statement->m_kind = kind;
    statement->m_location = location;
    return statement;
}

// Prepare to parse tokens.
Parser::Parser(const std::vector<Token> &tokens, Diagnostics &diagnostics)
    : m_tokens(tokens)
    , m_diagnostics(diagnostics)
{}

// Parse constants, aliases, modules and the top level's members up to the end of the file.
std::unique_ptr<Ast> Parser::parse_program()
{
    auto ast = std::make_unique<Ast>();
    ast->m_modules.emplace_back();
    try {
        while (!is(TokenKind::Eof)) {
            if (is(TokenKind::Const)) {
                parse_constant(*ast);
            } else if (is(TokenKind::Type)) {
                parse_alias(*ast);
            } else if (is(TokenKind::Module)) {
                parse_module(*ast);
            } else if (!parse_member(ast->m_modules[0])) {
                fail(peek().m_location, "ERR_PARSE_TOP_LEVEL",
                     "expected a constant, a type, an op, a module, a proc, a process or a "
                     "declaration, not " +
                         std::string(Lexer::kind_name(peek().m_kind)));
            }
        }
    } catch (const DiagnosticsAbort &) {
        return nullptr;
    }
    return ast;
}

// Parse one expression with nothing after it.
std::unique_ptr<Expression> Parser::parse_lone_expression()
{
    try {
        auto expression = parse_expression();
        if (!is(TokenKind::Eof)) {
            fail(peek().m_location, "ERR_PARSE_TRAILING", "unexpected text after the value");
        }
        return expression;
    } catch (const DiagnosticsAbort &) {
        return nullptr;
    }
}

// Look at a token ahead, or the end of the file past the last.
const Token &Parser::peek(size_t ahead) const
{
    size_t at = m_position + ahead;
    return at < m_tokens.size() ? m_tokens[at] : m_tokens.back();
}

// Check the kind of a token ahead.
bool Parser::is(TokenKind kind, size_t ahead) const
{
    return peek(ahead).m_kind == kind;
}

// Move past the current token, but never past the end of the file.
const Token &Parser::next()
{
    const Token &token = peek();
    if (m_position + 1 < m_tokens.size()) {
        m_position++;
    }
    return token;
}

// Move past the current token if it has a kind.
bool Parser::accept(TokenKind kind)
{
    if (is(kind)) {
        next();
        return true;
    }
    return false;
}

// Move past a token that must have a kind.
const Token &Parser::expect(TokenKind kind, std::string_view context)
{
    if (!is(kind)) {
        fail(peek().m_location, "ERR_PARSE_EXPECTED",
             "expected " + std::string(Lexer::kind_name(kind)) + " " + std::string(context) +
                 ", not " + std::string(Lexer::kind_name(peek().m_kind)));
    }
    return next();
}

// Report an error and stop the parse.
void Parser::fail(const Location &location, std::string_view code, std::string_view message)
{
    m_diagnostics.error(location, code, message);
    throw DiagnosticsAbort();
}

// Check whether a type keyword starts at a token.
bool Parser::starts_type(size_t ahead) const
{
    switch (peek(ahead).m_kind) {
        case TokenKind::Int:
        case TokenKind::Bool:
        case TokenKind::Char:
        case TokenKind::RealType:
        case TokenKind::Sem:
            return true;
        default:
            return false;
    }
}

// A declaration starts with a type keyword, or with an alias followed by a name.
bool Parser::starts_declaration(size_t ahead) const
{
    return starts_type(ahead) ||
           (is(TokenKind::Identifier, ahead) && is(TokenKind::Identifier, ahead + 1));
}

// A mode is val, var, res or result followed by a type.
bool Parser::is_mode(size_t ahead) const
{
    if (!is(TokenKind::Identifier, ahead)) {
        return false;
    }
    const std::string &text = peek(ahead).m_text;
    bool mode = text == "val" || text == "var" || text == "res" || text == "result";
    return mode && (starts_type(ahead + 1) || is(TokenKind::Identifier, ahead + 1));
}

// Parse const [type] NAME = value;.
void Parser::parse_constant(Ast &ast)
{
    next();
    ConstantDecl constant;
    if (starts_type(0) || (is(TokenKind::Identifier) && is(TokenKind::Identifier, 1))) {
        constant.m_type = parse_type("in a constant");
    }
    const Token &name = expect(TokenKind::Identifier, "naming the constant");
    constant.m_name = name.m_text;
    constant.m_location = name.m_location;
    expect(TokenKind::Assign, "after the constant's name");
    constant.m_value = parse_expression();
    expect(TokenKind::Semicolon, "after the constant");
    ast.m_constants.push_back(std::move(constant));
}

// Parse type NAME = type;.
void Parser::parse_alias(Ast &ast)
{
    next();
    TypeAlias alias;
    const Token &name = expect(TokenKind::Identifier, "naming the type");
    alias.m_name = name.m_text;
    alias.m_location = name.m_location;
    expect(TokenKind::Assign, "after the type's name");
    alias.m_type = parse_type("in a type");
    expect(TokenKind::Semicolon, "after the type");
    ast.m_aliases.push_back(std::move(alias));
}

// Parse module NAME ops body members end [NAME] [;].
void Parser::parse_module(Ast &ast)
{
    next();
    Module module;
    const Token &name = expect(TokenKind::Identifier, "naming the module");
    module.m_name = name.m_text;
    module.m_location = name.m_location;
    while (is(TokenKind::Op)) {
        module.m_ops.push_back(parse_op());
    }
    expect(TokenKind::Body, "after the module's ops");
    while (!is(TokenKind::End)) {
        if (is(TokenKind::Eof)) {
            fail(module.m_location, "ERR_PARSE_NO_END",
                 "the module " + module.m_name + " has no end");
        }
        if (!parse_member(module)) {
            fail(peek().m_location, "ERR_PARSE_MODULE_BODY",
                 "expected an op, a proc, a process or a declaration in the body of " +
                     module.m_name + ", not " + std::string(Lexer::kind_name(peek().m_kind)));
        }
    }
    next();
    if (is(TokenKind::Identifier)) {
        const Token &end = next();
        if (end.m_text != module.m_name) {
            fail(end.m_location, "ERR_PARSE_END_NAME",
                 "end " + end.m_text + " closes the module " + module.m_name);
        }
    }
    accept(TokenKind::Semicolon);
    ast.m_modules.push_back(std::move(module));
}

// Parse an op, a proc, a process, a declaration or a stray semicolon.
bool Parser::parse_member(Module &module)
{
    if (accept(TokenKind::Semicolon)) {
        return true;
    }
    if (is(TokenKind::Op)) {
        module.m_ops.push_back(parse_op());
    } else if (is(TokenKind::Proc) ||
               (is(TokenKind::Process) && is(TokenKind::Identifier, 1) && is(TokenKind::LeftParen, 2))) {
        module.m_procs.push_back(parse_proc());
    } else if (is(TokenKind::Process)) {
        module.m_processes.push_back(parse_process());
    } else if (starts_declaration(0)) {
        module.m_declarations.push_back(parse_declaration());
    } else {
        return false;
    }
    return true;
}

// Parse op NAME([mode] type [name], ...) [returns type];.
OpDecl Parser::parse_op()
{
    next();
    OpDecl op;
    const Token &name = expect(TokenKind::Identifier, "naming the op");
    op.m_name = name.m_text;
    op.m_location = name.m_location;
    expect(TokenKind::LeftParen, "after the op's name");
    if (!is(TokenKind::RightParen)) {
        do {
            Formal formal;
            if (is_mode(0)) {
                std::string mode = next().m_text;
                formal.m_mode = mode == "result" ? "res" : mode;
            }
            formal.m_type = parse_type("of a formal");
            if (is(TokenKind::Identifier)) {
                formal.m_name = next().m_text;
            }
            op.m_formals.push_back(std::move(formal));
        } while (accept(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "after the op's formals");
    if (accept(TokenKind::Returns)) {
        op.m_has_returns = true;
        op.m_returns = parse_type("after returns");
    }
    expect(TokenKind::Semicolon, "after the op");
    return op;
}

// Parse proc NAME(names) [returns name] { ... }, or Process NAME(names) as on the slides.
ProcDecl Parser::parse_proc()
{
    next();
    ProcDecl proc;
    const Token &name = expect(TokenKind::Identifier, "naming the op the proc serves");
    proc.m_name = name.m_text;
    proc.m_location = name.m_location;
    parse_names(proc.m_formals, proc.m_formal_locations);
    if (accept(TokenKind::Returns)) {
        proc.m_returns = expect(TokenKind::Identifier, "after returns").m_text;
    }
    proc.m_body = parse_block();
    return proc;
}

// Parse process NAME [ [i = a to b] ] { ... }.
ProcessDecl Parser::parse_process()
{
    next();
    ProcessDecl process;
    const Token &name = expect(TokenKind::Identifier, "naming the process");
    process.m_name = name.m_text;
    process.m_location = name.m_location;
    if (accept(TokenKind::LeftBracket)) {
        process.m_index = expect(TokenKind::Identifier, "naming the process's index").m_text;
        expect(TokenKind::Assign, "after the index");
        process.m_from = parse_expression();
        expect(TokenKind::To, "between the index's bounds");
        process.m_to = parse_expression();
        expect(TokenKind::RightBracket, "after the index's bounds");
    }
    process.m_body = parse_block();
    return process;
}

// Parse int, bool, char, real, sem or the name of an alias.
TypeName Parser::parse_type(std::string_view context)
{
    TypeName type;
    type.m_location = peek().m_location;
    switch (peek().m_kind) {
        case TokenKind::Int:
            type.m_base = BaseType::Int;
            break;
        case TokenKind::Bool:
            type.m_base = BaseType::Bool;
            break;
        case TokenKind::Char:
            type.m_base = BaseType::Char;
            break;
        case TokenKind::RealType:
            type.m_base = BaseType::Real;
            break;
        case TokenKind::Sem:
            type.m_base = BaseType::Sem;
            break;
        case TokenKind::Identifier:
            type.m_alias = peek().m_text;
            break;
        default:
            fail(peek().m_location, "ERR_PARSE_TYPE",
                 "expected a type " + std::string(context) + ", not " +
                     std::string(Lexer::kind_name(peek().m_kind)));
    }
    next();
    return type;
}

// Parse (NAME, NAME, ...).
void Parser::parse_names(std::vector<std::string> &names, std::vector<Location> &locations)
{
    expect(TokenKind::LeftParen, "before the formals");
    if (!is(TokenKind::RightParen)) {
        do {
            const Token &name = expect(TokenKind::Identifier, "naming a formal");
            names.push_back(name.m_text);
            locations.push_back(name.m_location);
        } while (accept(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "after the formals");
}

// Parse { statements }.
std::vector<std::unique_ptr<Statement>> Parser::parse_block()
{
    const Token &open = expect(TokenKind::LeftBrace, "to open a block");
    std::vector<std::unique_ptr<Statement>> statements;
    while (!is(TokenKind::RightBrace)) {
        if (is(TokenKind::Eof)) {
            fail(open.m_location, "ERR_PARSE_UNCLOSED", "this '{' is never closed");
        }
        statements.push_back(parse_statement());
    }
    next();
    return statements;
}

// Parse one statement, choosing by its first token.
std::unique_ptr<Statement> Parser::parse_statement()
{
    const Token &first = peek();
    switch (first.m_kind) {
        case TokenKind::LeftBrace: {
            auto block = make_statement(StatementKind::Block, first.m_location);
            block->m_body = parse_block();
            return block;
        }
        case TokenKind::Semicolon:
            next();
            return make_statement(StatementKind::Skip, first.m_location);
        case TokenKind::Skip:
            next();
            expect(TokenKind::Semicolon, "after skip");
            return make_statement(StatementKind::Skip, first.m_location);
        case TokenKind::If:
            return parse_if();
        case TokenKind::While:
            return parse_while();
        case TokenKind::For:
            return parse_for();
        case TokenKind::In:
            return parse_in();
        case TokenKind::Write:
            return parse_write();
        case TokenKind::Call:
        case TokenKind::Send:
        case TokenKind::Receive: {
            next();
            StatementKind kind = first.m_kind == TokenKind::Call   ? StatementKind::Call
                                 : first.m_kind == TokenKind::Send ? StatementKind::Send
                                                                   : StatementKind::Receive;
            auto statement = make_statement(kind, first.m_location);
            statement->m_invocation = parse_invocation("after " + first.m_text);
            expect(TokenKind::Semicolon, "after the invocation");
            return statement;
        }
        default:
            break;
    }
    if (starts_declaration(0)) {
        return parse_declaration();
    }
    if (is(TokenKind::Identifier)) {
        return parse_named();
    }
    fail(first.m_location, "ERR_PARSE_STATEMENT",
         "expected a statement, not " + std::string(Lexer::kind_name(first.m_kind)));
}

// Parse type name [size] = value, ...;.
std::unique_ptr<Statement> Parser::parse_declaration()
{
    auto statement = make_statement(StatementKind::Declaration, peek().m_location);
    statement->m_type = parse_type("in a declaration");
    do {
        Declarator declarator;
        const Token &name = expect(TokenKind::Identifier, "in a declaration");
        declarator.m_name = name.m_text;
        declarator.m_location = name.m_location;
        if (accept(TokenKind::LeftBracket)) {
            declarator.m_size = parse_expression();
            expect(TokenKind::RightBracket, "after the array's size");
        }
        if (accept(TokenKind::Assign)) {
            declarator.m_initial = parse_expression();
        }
        statement->m_declarators.push_back(std::move(declarator));
    } while (accept(TokenKind::Comma));
    expect(TokenKind::Semicolon, "after the declaration");
    return statement;
}

// Parse if (condition) statement [else statement].
std::unique_ptr<Statement> Parser::parse_if()
{
    auto statement = make_statement(StatementKind::If, next().m_location);
    expect(TokenKind::LeftParen, "after if");
    statement->m_condition = parse_expression();
    expect(TokenKind::RightParen, "after the condition");
    statement->m_body.push_back(parse_statement());
    if (accept(TokenKind::Else)) {
        statement->m_else.push_back(parse_statement());
    }
    return statement;
}

// Parse while (condition) statement.
std::unique_ptr<Statement> Parser::parse_while()
{
    auto statement = make_statement(StatementKind::While, next().m_location);
    expect(TokenKind::LeftParen, "after while");
    statement->m_condition = parse_expression();
    expect(TokenKind::RightParen, "after the condition");
    statement->m_body.push_back(parse_statement());
    return statement;
}

// Parse for [i = a to b] or for [i = a to b st condition], as on the slides.
std::unique_ptr<Statement> Parser::parse_for()
{
    auto statement = make_statement(StatementKind::For, next().m_location);
    expect(TokenKind::LeftBracket, "after for");
    const Token &variable = expect(TokenKind::Identifier, "naming the loop's variable");
    statement->m_variable = variable.m_text;
    expect(TokenKind::Assign, "after the loop's variable");
    statement->m_from = parse_expression();
    expect(TokenKind::To, "between the loop's bounds");
    statement->m_to = parse_expression();
    if (is(TokenKind::Identifier) && peek().m_text == "st") {
        next();
        statement->m_condition = parse_expression();
    }
    expect(TokenKind::RightBracket, "after the loop's bounds");
    statement->m_body.push_back(parse_statement());
    return statement;
}

// Parse in arm [] arm ... ni, with an optional semicolon after ni.
std::unique_ptr<Statement> Parser::parse_in()
{
    auto statement = make_statement(StatementKind::In, next().m_location);
    statement->m_arms.push_back(parse_arm());
    while (accept(TokenKind::Box)) {
        statement->m_arms.push_back(parse_arm());
    }
    expect(TokenKind::Ni, "to close the in statement");
    accept(TokenKind::Semicolon);
    return statement;
}

// Parse op(formals) [returns name] [and guard] [by priority] -> statements.
Arm Parser::parse_arm()
{
    Arm arm;
    const Token &name = expect(TokenKind::Identifier, "naming the op an arm serves");
    arm.m_location = name.m_location;
    arm.m_op = name.m_text;
    if (accept(TokenKind::Dot)) {
        arm.m_module = arm.m_op;
        arm.m_op = expect(TokenKind::Identifier, "after the module's name").m_text;
    }
    parse_names(arm.m_formals, arm.m_formal_locations);
    if (accept(TokenKind::Returns)) {
        arm.m_returns = expect(TokenKind::Identifier, "after returns").m_text;
    }
    if (accept(TokenKind::And)) {
        arm.m_guard = parse_expression();
    }
    if (accept(TokenKind::By)) {
        arm.m_by = parse_expression();
    }
    expect(TokenKind::Arrow, "after the arm's op and guard");
    while (!is(TokenKind::Box) && !is(TokenKind::Ni)) {
        if (is(TokenKind::Eof)) {
            fail(arm.m_location, "ERR_PARSE_NO_NI", "the in statement has no ni");
        }
        arm.m_body.push_back(parse_statement());
    }
    return arm;
}

// Parse write(value, ...);.
std::unique_ptr<Statement> Parser::parse_write()
{
    auto statement = make_statement(StatementKind::Write, next().m_location);
    parse_arguments(statement->m_arguments);
    expect(TokenKind::Semicolon, "after write");
    return statement;
}

// Parse NAME;, NAME(arguments);, M.op(arguments); or an assignment.
std::unique_ptr<Statement> Parser::parse_named()
{
    const Token &name = peek();
    if (is(TokenKind::Semicolon, 1)) {
        next();
        next();
        auto hook = make_statement(StatementKind::Hook, name.m_location);
        hook->m_name = name.m_text;
        return hook;
    }
    if (is(TokenKind::LeftParen, 1) || (is(TokenKind::Dot, 1) && is(TokenKind::LeftParen, 3))) {
        bool qualified = is(TokenKind::Dot, 1);
        auto statement = make_statement(qualified ? StatementKind::Call : StatementKind::NameCall,
                                        name.m_location);
        statement->m_invocation = parse_invocation("");
        expect(TokenKind::Semicolon, "after the call");
        return statement;
    }
    auto statement = make_statement(StatementKind::Assign, name.m_location);
    statement->m_target = parse_primary();
    if (statement->m_target->m_kind != ExpressionKind::Name &&
        statement->m_target->m_kind != ExpressionKind::Index) {
        fail(name.m_location, "ERR_PARSE_STATEMENT", "expected a statement");
    }
    const Token &op = next();
    switch (op.m_kind) {
        case TokenKind::Assign:
        case TokenKind::PlusAssign:
        case TokenKind::MinusAssign:
        case TokenKind::StarAssign:
            statement->m_operator = op.m_text;
            statement->m_value = parse_expression();
            break;
        case TokenKind::PlusPlus:
        case TokenKind::MinusMinus:
            statement->m_operator = op.m_text;
            break;
        default:
            fail(op.m_location, "ERR_PARSE_ASSIGNMENT",
                 "expected '=', '+=', '-=', '*=', '++' or '--' after " + name.m_text + ", not " +
                     std::string(Lexer::kind_name(op.m_kind)));
    }
    expect(TokenKind::Semicolon, "after the assignment");
    return statement;
}

// Parse NAME(arguments) or M.NAME(arguments).
std::unique_ptr<Expression> Parser::parse_invocation(std::string_view context)
{
    const Token &name = expect(TokenKind::Identifier, context.empty() ? "naming an op" : context);
    auto invocation = make_expression(ExpressionKind::Invoke, name.m_location);
    invocation->m_text = name.m_text;
    if (accept(TokenKind::Dot)) {
        invocation->m_module = invocation->m_text;
        invocation->m_text = expect(TokenKind::Identifier, "after the module's name").m_text;
    }
    parse_arguments(invocation->m_operands);
    return invocation;
}

// Parse (expression, ...).
void Parser::parse_arguments(std::vector<std::unique_ptr<Expression>> &arguments)
{
    expect(TokenKind::LeftParen, "before the arguments");
    if (!is(TokenKind::RightParen)) {
        do {
            arguments.push_back(parse_expression());
        } while (accept(TokenKind::Comma));
    }
    expect(TokenKind::RightParen, "after the arguments");
}

// Parse an expression, from the loosest level of precedence.
std::unique_ptr<Expression> Parser::parse_expression()
{
    return parse_binary(0);
}

// Parse operands joined by the operators of one level, left to right.
std::unique_ptr<Expression> Parser::parse_binary(int level)
{
    if (level == static_cast<int>(binaryLevels.size())) {
        return parse_unary();
    }
    auto left = parse_binary(level + 1);
    for (;;) {
        bool found = false;
        for (TokenKind kind : binaryLevels[level]) {
            found = found || is(kind);
        }
        if (!found) {
            return left;
        }
        const Token &op = next();
        auto binary = make_expression(ExpressionKind::Binary, op.m_location);
        binary->m_text = operator_text(op.m_kind);
        binary->m_operands.push_back(std::move(left));
        binary->m_operands.push_back(parse_binary(level + 1));
        left = std::move(binary);
    }
}

// Parse -x, +x, !x or a primary expression.
std::unique_ptr<Expression> Parser::parse_unary()
{
    if (is(TokenKind::Minus) || is(TokenKind::Bang) || is(TokenKind::Plus)) {
        const Token &op = next();
        auto operand = parse_unary();
        if (op.m_kind == TokenKind::Plus) {
            return operand;
        }
        auto unary = make_expression(ExpressionKind::Unary, op.m_location);
        unary->m_text = op.m_kind == TokenKind::Minus ? "-" : "!";
        unary->m_operands.push_back(std::move(operand));
        return unary;
    }
    return parse_primary();
}

// Parse a literal, a name, an element, an invocation, ?op or (expression).
std::unique_ptr<Expression> Parser::parse_primary()
{
    const Token &token = peek();
    switch (token.m_kind) {
        case TokenKind::Integer: {
            next();
            auto expression = make_expression(ExpressionKind::Integer, token.m_location);
            expression->m_integer = token.m_integer;
            return expression;
        }
        case TokenKind::Real: {
            next();
            auto expression = make_expression(ExpressionKind::Real, token.m_location);
            expression->m_real = token.m_real;
            return expression;
        }
        case TokenKind::Character: {
            next();
            auto expression = make_expression(ExpressionKind::Character, token.m_location);
            expression->m_integer = token.m_integer;
            return expression;
        }
        case TokenKind::String: {
            next();
            auto expression = make_expression(ExpressionKind::String, token.m_location);
            expression->m_text = token.m_text;
            return expression;
        }
        case TokenKind::True:
        case TokenKind::False: {
            next();
            auto expression = make_expression(ExpressionKind::Boolean, token.m_location);
            expression->m_integer = token.m_kind == TokenKind::True;
            return expression;
        }
        case TokenKind::LeftParen: {
            next();
            auto expression = parse_expression();
            expect(TokenKind::RightParen, "to close the parenthesis");
            return expression;
        }
        case TokenKind::Question: {
            next();
            auto pending = make_expression(ExpressionKind::Pending, token.m_location);
            pending->m_text = expect(TokenKind::Identifier, "naming an op after '?'").m_text;
            if (accept(TokenKind::Dot)) {
                pending->m_module = pending->m_text;
                pending->m_text = expect(TokenKind::Identifier, "after the module's name").m_text;
            }
            return pending;
        }
        case TokenKind::Identifier:
            break;
        default:
            fail(token.m_location, "ERR_PARSE_EXPRESSION",
                 "expected an expression, not " + std::string(Lexer::kind_name(token.m_kind)));
    }
    if (is(TokenKind::LeftParen, 1) || (is(TokenKind::Dot, 1) && is(TokenKind::Identifier, 2))) {
        return parse_invocation("");
    }
    next();
    if (accept(TokenKind::LeftBracket)) {
        auto index = make_expression(ExpressionKind::Index, token.m_location);
        index->m_text = token.m_text;
        index->m_operands.push_back(parse_expression());
        expect(TokenKind::RightBracket, "after the index");
        return index;
    }
    auto name = make_expression(ExpressionKind::Name, token.m_location);
    name->m_text = token.m_text;
    return name;
}
