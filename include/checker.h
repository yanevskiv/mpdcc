// Checks a syntax tree: its names, types, operations and who serves them.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_CHECKER_H
#define MPDCC_CHECKER_H

#include "ast.h"
#include "diagnostics.h"
#include "program.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Checks a syntax tree and fills in what its names refer to.
class Checker
{
public:
    // Prepare to check, reporting to diagnostics.
    explicit Checker(Diagnostics &diagnostics);

    // Check a program with -D constants, or return null after an error.
    std::unique_ptr<Program> check(Ast &ast, const std::vector<Define> &defines);

private:
    // Resolve the type aliases.
    void check_aliases();

    // Check the constants, overriding them with -D.
    void check_constants(const std::vector<Define> &defines);

    // Parse the value of -D as a literal.
    std::unique_ptr<Expression> define_value(const Define &define);

    // Number every module's ops and match the procs to them.
    void collect_ops();

    // Check one module's declarations, procs and processes.
    void check_module(Module &module);

    // Check a proc's formals and body.
    void check_proc(ProcDecl &proc);

    // Check a process's index and body.
    void check_process(ProcessDecl &process);

    // Warn about ops that nothing serves.
    void check_served();

    // Turn a written type into a base type.
    BaseType resolve(const TypeName &type);

    // Open a scope.
    void push_scope();

    // Close the innermost scope.
    void pop_scope();

    // Declare a symbol in the innermost scope.
    Symbol *declare(SymbolKind kind, const std::string &name, const Location &location,
                    BaseType type, bool array = false);

    // Find a symbol, innermost scope first.
    Symbol *lookup(const std::string &name) const;

    // Find an op by its module and name, or report it.
    OpInfo *find_op(const std::string &module, const std::string &name, const Location &location);

    // Check a list of statements in a new scope.
    void check_block(std::vector<std::unique_ptr<Statement>> &statements);

    // Check one statement.
    void check_statement(Statement &statement);

    // Check a declaration and declare its names.
    void check_declaration(Statement &statement);

    // Check an assignment.
    void check_assignment(Statement &statement);

    // Check a for loop.
    void check_for(Statement &statement);

    // Check call and send.
    void check_invocation_statement(Statement &statement);

    // Check receive.
    void check_receive(Statement &statement);

    // Check an in statement.
    void check_in(Statement &statement);

    // Check NAME(arguments): a call, P, V, or a hook.
    void check_name_call(Statement &statement);

    // Check an invocation's arguments against its op's formals.
    void check_arguments(Expression &invocation, OpInfo &op);

    // Mark an op as served by in or receive.
    void serve_by_in(OpInfo &op, const Location &location);

    // Check an expression and return its type.
    BaseType check_expression(Expression &expression);

    // Check an expression that must have a type.
    void check_typed(Expression &expression, BaseType type, std::string_view context);

    // Check an operator's operands and return its type.
    BaseType check_binary(Expression &expression);

    // Check an invocation used as a value.
    BaseType check_value_invocation(Expression &expression);

    // Check that an expression is a variable that can be assigned.
    bool check_lvalue(Expression &expression, std::string_view context);

    // Check that an expression names a semaphore.
    bool check_semaphore(Expression &expression);

    // Whether a value of one type can be stored in another.
    static bool assignable(BaseType target, BaseType value);

    // Whether a type is int or real.
    static bool numeric(BaseType type);

    // Name a type for diagnostics.
    static std::string type_name(BaseType type);

    // Destination of diagnostics.
    Diagnostics &m_diagnostics;

    // Program being built.
    std::unique_ptr<Program> m_program;

    // Type aliases by name.
    std::map<std::string, BaseType, std::less<>> m_aliases;

    // Scopes, outermost first.
    std::vector<std::map<std::string, Symbol *, std::less<>>> m_scopes;

    // Ops by module, then by name; the top level is "".
    std::map<std::string, std::map<std::string, OpInfo *, std::less<>>, std::less<>> m_ops;

    // Module being checked.
    std::string m_module;

    // Whether a guard or a scheduling expression is being checked.
    bool m_in_guard = false;

    // Whether a constant or a module's variable is being checked, before any process runs.
    bool m_static = false;
};

#endif
