// The syntax tree of an MPD program, as the parser builds it.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_AST_H
#define MPDCC_AST_H

#include "token.h"

#include <memory>
#include <string>
#include <vector>

struct Symbol;
struct OpInfo;

// Describes a type of values: the base types and the semaphore.
enum class BaseType {
    None,
    Int,
    Bool,
    Char,
    Real,
    Sem,
    String,
};

// Describes a type as written: a base type or the name of a type alias.
struct TypeName
{
    // Base type, or None for an alias.
    BaseType m_base = BaseType::None;

    // Name of the alias, such as elem.
    std::string m_alias;

    // Position of the type.
    Location m_location;
};

// Describes the kind of an expression.
enum class ExpressionKind {
    Integer,
    Real,
    Character,
    Boolean,
    String,
    Name,
    Index,
    Unary,
    Binary,
    Invoke,
    Pending,
    Random,
};

// Describes an expression and, after checking, its type and what it names.
struct Expression
{
    // Kind of the expression.
    ExpressionKind m_kind = ExpressionKind::Integer;

    // Position of the expression.
    Location m_location;

    // Value of an integer, a character or a Boolean.
    long long m_integer = 0;

    // Value of a real number.
    double m_real = 0;

    // Name, operator or the text of a string.
    std::string m_text;

    // Module before the dot in M.op, or empty.
    std::string m_module;

    // Index, operands or arguments.
    std::vector<std::unique_ptr<Expression>> m_operands;

    // Type, after checking.
    BaseType m_type = BaseType::None;

    // Variable, constant or formal a name refers to, after checking.
    Symbol *m_symbol = nullptr;

    // Operation an invocation or ?op refers to, after checking.
    OpInfo *m_op = nullptr;
};

// Describes one name in a declaration, with its array size and initial value.
struct Declarator
{
    // Declared name.
    std::string m_name;

    // Position of the name.
    Location m_location;

    // Size of an array, or null for a single value.
    std::unique_ptr<Expression> m_size;

    // Initial value, or null.
    std::unique_ptr<Expression> m_initial;

    // Symbol, after checking.
    Symbol *m_symbol = nullptr;
};

struct Statement;

// Describes one arm of an in statement: op(formals) and B by e -> statements.
struct Arm
{
    // Operation the arm serves.
    std::string m_op;

    // Module before the dot, or empty.
    std::string m_module;

    // Position of the operation's name.
    Location m_location;

    // Names of the formals.
    std::vector<std::string> m_formals;

    // Positions of the formals.
    std::vector<Location> m_formal_locations;

    // Name after returns, or empty.
    std::string m_returns;

    // Synchronization expression after and, or null.
    std::unique_ptr<Expression> m_guard;

    // Scheduling expression after by, or null.
    std::unique_ptr<Expression> m_by;

    // Statements after the arrow.
    std::vector<std::unique_ptr<Statement>> m_body;

    // Operation, after checking.
    OpInfo *m_info = nullptr;

    // Symbols of the formals, after checking.
    std::vector<Symbol *> m_formal_symbols;

    // Symbol of the returns name, after checking.
    Symbol *m_returns_symbol = nullptr;
};

// Describes the kind of a statement.
enum class StatementKind {
    Declaration,
    Block,
    Skip,
    Assign,
    If,
    While,
    For,
    Call,
    Send,
    Receive,
    In,
    NameCall,
    P,
    V,
    Write,
    Nap,
    Hook,
};

// Describes a statement; each kind uses some of the fields.
struct Statement
{
    // Kind of the statement.
    StatementKind m_kind = StatementKind::Skip;

    // Position of the statement.
    Location m_location;

    // Type of a declaration.
    TypeName m_type;

    // Names of a declaration.
    std::vector<Declarator> m_declarators;

    // Statements of a block, of a loop, or after if.
    std::vector<std::unique_ptr<Statement>> m_body;

    // Statements after else.
    std::vector<std::unique_ptr<Statement>> m_else;

    // Target of an assignment, P or V.
    std::unique_ptr<Expression> m_target;

    // Assignment operator: =, +=, -=, *=, ++ or --.
    std::string m_operator;

    // Value of an assignment.
    std::unique_ptr<Expression> m_value;

    // Condition of if and while.
    std::unique_ptr<Expression> m_condition;

    // Variable of a for loop.
    std::string m_variable;

    // Bounds of a for loop.
    std::unique_ptr<Expression> m_from;
    std::unique_ptr<Expression> m_to;

    // Symbol of the for loop's variable, after checking.
    Symbol *m_index = nullptr;

    // Invocation of call, send, receive and NAME(args): an Invoke expression.
    std::unique_ptr<Expression> m_invocation;

    // Arms of an in statement.
    std::vector<Arm> m_arms;

    // Values that write prints, or a hook's arguments.
    std::vector<std::unique_ptr<Expression>> m_arguments;

    // Name of a hook.
    std::string m_name;
};

// Describes one formal of an op declaration.
struct Formal
{
    // Mode: val, var or res.
    std::string m_mode = "val";

    // Type of the formal.
    TypeName m_type;

    // Name, or empty, since a declaration may give only types.
    std::string m_name;
};

// Describes op NAME(formals) returns type.
struct OpDecl
{
    // Name of the operation.
    std::string m_name;

    // Position of the name.
    Location m_location;

    // Formals.
    std::vector<Formal> m_formals;

    // Whether the operation returns a value.
    bool m_has_returns = false;

    // Type of the value it returns.
    TypeName m_returns;

    // Operation, after checking.
    OpInfo *m_info = nullptr;
};

// Describes proc NAME(names) returns name { ... }, the body that serves an op.
struct ProcDecl
{
    // Name of the operation the proc serves.
    std::string m_name;

    // Position of the name.
    Location m_location;

    // Names of the formals.
    std::vector<std::string> m_formals;

    // Positions of the formals.
    std::vector<Location> m_formal_locations;

    // Name after returns, or empty.
    std::string m_returns;

    // Statements of the body.
    std::vector<std::unique_ptr<Statement>> m_body;

    // Operation, after checking.
    OpInfo *m_info = nullptr;

    // Symbols of the formals, after checking.
    std::vector<Symbol *> m_formal_symbols;

    // Symbol of the returns name, after checking.
    Symbol *m_returns_symbol = nullptr;
};

// Describes process NAME[i = a to b] { ... }, started with the program.
struct ProcessDecl
{
    // Name of the process.
    std::string m_name;

    // Position of the name.
    Location m_location;

    // Index of an array of processes, or empty.
    std::string m_index;

    // Bounds of the index.
    std::unique_ptr<Expression> m_from;
    std::unique_ptr<Expression> m_to;

    // Statements of the body.
    std::vector<std::unique_ptr<Statement>> m_body;

    // Symbol of the index, after checking.
    Symbol *m_index_symbol = nullptr;
};

// Describes module NAME ops body ... end, or the program's top level, named "".
struct Module
{
    // Name of the module, empty for the top level.
    std::string m_name;

    // Position of the name.
    Location m_location;

    // Operations it declares.
    std::vector<OpDecl> m_ops;

    // Declarations of its variables and semaphores.
    std::vector<std::unique_ptr<Statement>> m_declarations;

    // Procs serving its operations.
    std::vector<ProcDecl> m_procs;

    // Its processes.
    std::vector<ProcessDecl> m_processes;
};

// Describes const NAME = value.
struct ConstantDecl
{
    // Name of the constant.
    std::string m_name;

    // Position of the name.
    Location m_location;

    // Type, if given.
    TypeName m_type;

    // Value.
    std::unique_ptr<Expression> m_value;

    // Symbol, after checking.
    Symbol *m_symbol = nullptr;
};

// Describes type NAME = type.
struct TypeAlias
{
    // Name of the alias.
    std::string m_name;

    // Position of the name.
    Location m_location;

    // Type it names.
    TypeName m_type;
};

// Describes a whole program.
struct Ast
{
    // Constants, in order.
    std::vector<ConstantDecl> m_constants;

    // Type aliases, in order.
    std::vector<TypeAlias> m_aliases;

    // The top level first, then the modules in order.
    std::vector<Module> m_modules;
};

#endif
