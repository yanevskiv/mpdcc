// A checked program's symbols and operations.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_PROGRAM_H
#define MPDCC_PROGRAM_H

#include "ast.h"

#include <deque>
#include <string>
#include <vector>

// Describes what a name stands for.
enum class SymbolKind {
    Constant,
    Variable,
    Formal,
    Index,
    Returns,
};

// Describes a named value: a constant, a variable, a formal or an index.
struct Symbol
{
    // Kind of the symbol.
    SymbolKind m_kind = SymbolKind::Variable;

    // Name in the source.
    std::string m_name;

    // Position of the declaration.
    Location m_location;

    // Type of the value, or of each element of an array.
    BaseType m_type = BaseType::Int;

    // Whether it is an array.
    bool m_array = false;

    // Mode of a formal: val, var or res.
    std::string m_mode = "val";

    // Name in the Ada program.
    std::string m_ada;
};

// Describes one formal of an operation, after checking.
struct OpFormal
{
    // Mode: val, var or res.
    std::string m_mode = "val";

    // Type.
    BaseType m_type = BaseType::Int;
};

// Describes an operation and how it is served.
struct OpInfo
{
    // Name of the operation.
    std::string m_name;

    // Module that declares it, empty for the top level.
    std::string m_module;

    // Position of its declaration.
    Location m_location;

    // Number, from 1, in the Ada program.
    int m_number = 0;

    // Formals.
    std::vector<OpFormal> m_formals;

    // Type it returns, or None.
    BaseType m_returns = BaseType::None;

    // Proc that serves it, or null.
    ProcDecl *m_proc = nullptr;

    // Whether an in or receive statement serves it.
    bool m_by_in = false;

    // Name in the Ada program, the base of its subprograms' names.
    std::string m_ada;

    // Name for traces, such as BoundedBuffer.deposit.
    std::string full_name() const;

    // Whether every formal is val, so an expression can invoke it.
    bool all_values() const;
};

// Describes NAME=VALUE from -D.
struct Define
{
    // Name of the constant.
    std::string m_name;

    // Its value, as written.
    std::string m_value;
};

// Holds the symbols and operations a checked syntax tree points into.
struct Program
{
    // The syntax tree.
    Ast *m_ast = nullptr;

    // Symbols, in a deque so pointers to them stay valid.
    std::deque<Symbol> m_symbols;

    // Operations, numbered in order.
    std::deque<OpInfo> m_ops;
};

#endif
