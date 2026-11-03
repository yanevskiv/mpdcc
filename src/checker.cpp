// Checks a syntax tree: its names, types, operations and who serves them.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "checker.h"

#include "lexer.h"
#include "parser.h"

#include <set>
#include <sstream>
#include <utility>

// Name for traces: the op, after its module if it has one.
std::string OpInfo::full_name() const
{
    return m_module.empty() ? m_name : m_module + "." + m_name;
}

// Whether every formal is val.
bool OpInfo::all_values() const
{
    for (const OpFormal &formal : m_formals) {
        if (formal.m_mode != "val") {
            return false;
        }
    }
    return true;
}

// Prepare to check.
Checker::Checker(Diagnostics &diagnostics)
    : m_diagnostics(diagnostics)
{}

// Check aliases, constants, ops, then every module, and return the program unless an error.
std::unique_ptr<Program> Checker::check(Ast &ast, const std::vector<Define> &defines)
{
    m_program = std::make_unique<Program>();
    m_program->m_ast = &ast;
    push_scope();
    check_aliases();
    check_constants(defines);
    collect_ops();
    for (Module &module : ast.m_modules) {
        check_module(module);
    }
    check_served();
    pop_scope();
    if (m_diagnostics.error_count()) {
        return nullptr;
    }
    return std::move(m_program);
}

// Resolve each alias to a base type, in order.
void Checker::check_aliases()
{
    for (TypeAlias &alias : m_program->m_ast->m_aliases) {
        BaseType type = resolve(alias.m_type);
        if (type == BaseType::Sem) {
            m_diagnostics.error(alias.m_location, "ERR_SEMA_SEM_ALIAS",
                                "a type can't name sem; declare semaphores with sem");
        }
        if (!m_aliases.emplace(alias.m_name, type).second) {
            m_diagnostics.error(alias.m_location, "ERR_SEMA_REDECLARED",
                                "the type " + alias.m_name + " is declared twice");
        }
    }
}

// Check each constant, replacing its value with -D's, and add -D names not in the program.
void Checker::check_constants(const std::vector<Define> &defines)
{
    std::map<std::string, const Define *> overrides;
    for (const Define &define : defines) {
        overrides[define.m_name] = &define;
    }
    std::vector<ConstantDecl> &constants = m_program->m_ast->m_constants;
    std::set<std::string> present;
    for (const ConstantDecl &constant : constants) {
        present.insert(constant.m_name);
    }
    for (const Define &define : defines) {
        if (present.count(define.m_name)) {
            continue;
        }
        ConstantDecl added;
        added.m_name = define.m_name;
        constants.push_back(std::move(added));
        present.insert(define.m_name);
    }
    for (ConstantDecl &constant : constants) {
        auto override = overrides.find(constant.m_name);
        if (override != overrides.end()) {
            auto value = define_value(*override->second);
            if (!value) {
                continue;
            }
            value->m_location = constant.m_location;
            constant.m_value = std::move(value);
        }
        if (!constant.m_value) {
            continue;
        }
        m_static = true;
        BaseType type = check_expression(*constant.m_value);
        m_static = false;
        if (constant.m_type.m_base != BaseType::None || !constant.m_type.m_alias.empty()) {
            BaseType declared = resolve(constant.m_type);
            if (!assignable(declared, type)) {
                m_diagnostics.error(constant.m_location, "ERR_SEMA_TYPE",
                                    "the constant " + constant.m_name + " is " +
                                        type_name(declared) + " but its value is " +
                                        type_name(type));
            }
            type = declared;
        }
        if (type == BaseType::String) {
            m_diagnostics.error(constant.m_location, "ERR_SEMA_STRING",
                                "a constant can't be a string");
        }
        constant.m_symbol = declare(SymbolKind::Constant, constant.m_name, constant.m_location,
                                    type);
    }
}

// Parse -D's value, which must be a literal, possibly negative.
std::unique_ptr<Expression> Checker::define_value(const Define &define)
{
    std::ostringstream ignored;
    Diagnostics quiet("-D", define.m_value, ignored);
    std::vector<Token> tokens = Lexer(define.m_value, quiet).tokenize();
    std::unique_ptr<Expression> value;
    if (!quiet.error_count()) {
        value = Parser(tokens, quiet).parse_lone_expression();
    }
    Expression *literal = value.get();
    if (literal && literal->m_kind == ExpressionKind::Unary && literal->m_text == "-") {
        literal = literal->m_operands[0].get();
    }
    bool ok = literal && (literal->m_kind == ExpressionKind::Integer ||
                          literal->m_kind == ExpressionKind::Real ||
                          literal->m_kind == ExpressionKind::Character ||
                          literal->m_kind == ExpressionKind::Boolean);
    if (!ok || quiet.error_count()) {
        m_diagnostics.error(Location(), "ERR_SEMA_DEFINE",
                            "-D " + define.m_name + "=" + define.m_value +
                                ": the value must be a number, a character, true or false");
        return nullptr;
    }
    return value;
}

// Number the ops of every module, then match each proc to its op.
void Checker::collect_ops()
{
    std::set<std::string> modules;
    for (Module &module : m_program->m_ast->m_modules) {
        if (!module.m_name.empty() && !modules.insert(module.m_name).second) {
            m_diagnostics.error(module.m_location, "ERR_SEMA_REDECLARED",
                                "the module " + module.m_name + " is declared twice");
        }
        auto &ops = m_ops[module.m_name];
        for (OpDecl &decl : module.m_ops) {
            OpInfo &op = m_program->m_ops.emplace_back();
            op.m_name = decl.m_name;
            op.m_module = module.m_name;
            op.m_location = decl.m_location;
            op.m_number = static_cast<int>(m_program->m_ops.size());
            for (const Formal &formal : decl.m_formals) {
                OpFormal checked;
                checked.m_mode = formal.m_mode;
                checked.m_type = resolve(formal.m_type);
                if (checked.m_type == BaseType::Sem) {
                    m_diagnostics.error(formal.m_type.m_location, "ERR_SEMA_SEM_FORMAL",
                                        "a semaphore can't be passed to an op");
                }
                op.m_formals.push_back(checked);
            }
            if (decl.m_has_returns) {
                op.m_returns = resolve(decl.m_returns);
                if (op.m_returns == BaseType::Sem) {
                    m_diagnostics.error(decl.m_returns.m_location, "ERR_SEMA_SEM_FORMAL",
                                        "an op can't return a semaphore");
                }
            }
            decl.m_info = &op;
            if (!ops.emplace(op.m_name, &op).second) {
                m_diagnostics.error(decl.m_location, "ERR_SEMA_REDECLARED",
                                    "the op " + op.full_name() + " is declared twice");
            }
        }
    }
    for (Module &module : m_program->m_ast->m_modules) {
        auto &ops = m_ops[module.m_name];
        for (ProcDecl &proc : module.m_procs) {
            auto found = ops.find(proc.m_name);
            if (found == ops.end()) {
                m_diagnostics.error(proc.m_location, "ERR_SEMA_PROC_NO_OP",
                                    "the proc " + proc.m_name + " serves no op of " +
                                        (module.m_name.empty() ? std::string("the top level")
                                                               : module.m_name));
                m_diagnostics.note(proc.m_location, "declare it first with op " + proc.m_name +
                                                        "(...);");
                continue;
            }
            OpInfo &op = *found->second;
            if (op.m_proc) {
                m_diagnostics.error(proc.m_location, "ERR_SEMA_TWO_PROCS",
                                    "the op " + op.full_name() + " has two procs");
                continue;
            }
            op.m_proc = &proc;
            proc.m_info = &op;
            if (proc.m_formals.size() != op.m_formals.size()) {
                m_diagnostics.error(proc.m_location, "ERR_SEMA_FORMAL_COUNT",
                                    "the proc " + proc.m_name + " names " +
                                        std::to_string(proc.m_formals.size()) +
                                        " formals, but its op has " +
                                        std::to_string(op.m_formals.size()));
            }
            if (proc.m_returns.empty() != (op.m_returns == BaseType::None)) {
                m_diagnostics.error(proc.m_location, "ERR_SEMA_RETURNS",
                                    op.m_returns == BaseType::None
                                        ? "the proc " + proc.m_name +
                                              " names a result, but its op returns nothing"
                                        : "the proc " + proc.m_name +
                                              " must name its result: returns NAME");
            }
        }
    }
}

// Check a module's variables, then its procs and processes, in its own scope.
void Checker::check_module(Module &module)
{
    m_module = module.m_name;
    if (!module.m_name.empty()) {
        push_scope();
    }
    m_static = true;
    for (auto &declaration : module.m_declarations) {
        check_declaration(*declaration);
    }
    m_static = false;
    for (ProcDecl &proc : module.m_procs) {
        check_proc(proc);
    }
    for (ProcessDecl &process : module.m_processes) {
        check_process(process);
    }
    if (!module.m_name.empty()) {
        pop_scope();
    }
    m_module.clear();
}

// Declare a proc's formals and its result, then check its body.
void Checker::check_proc(ProcDecl &proc)
{
    push_scope();
    if (proc.m_info) {
        const OpInfo &op = *proc.m_info;
        for (size_t i = 0; i < proc.m_formals.size() && i < op.m_formals.size(); i++) {
            Symbol *formal = declare(SymbolKind::Formal, proc.m_formals[i],
                                     proc.m_formal_locations[i], op.m_formals[i].m_type);
            formal->m_mode = op.m_formals[i].m_mode;
            proc.m_formal_symbols.push_back(formal);
        }
        if (!proc.m_returns.empty() && op.m_returns != BaseType::None) {
            proc.m_returns_symbol =
                declare(SymbolKind::Returns, proc.m_returns, proc.m_location, op.m_returns);
        }
    }
    check_block(proc.m_body);
    pop_scope();
}

// Check a process's bounds, declare its index, then check its body.
void Checker::check_process(ProcessDecl &process)
{
    push_scope();
    if (!process.m_index.empty()) {
        check_typed(*process.m_from, BaseType::Int, "as the first index");
        check_typed(*process.m_to, BaseType::Int, "as the last index");
        process.m_index_symbol =
            declare(SymbolKind::Index, process.m_index, process.m_location, BaseType::Int);
    }
    check_block(process.m_body);
    pop_scope();
}

// Warn about ops that no proc, in or receive serves, and check process names are distinct.
void Checker::check_served()
{
    for (OpInfo &op : m_program->m_ops) {
        if (!op.m_proc && !op.m_by_in) {
            m_diagnostics.warning(op.m_location, "WARN_OP_NOT_SERVED",
                                  "nothing serves the op " + op.full_name() +
                                      "; its invocations wait forever");
        }
    }
    std::set<std::string> names;
    for (Module &module : m_program->m_ast->m_modules) {
        for (ProcessDecl &process : module.m_processes) {
            if (!names.insert(process.m_name).second) {
                m_diagnostics.error(process.m_location, "ERR_SEMA_REDECLARED",
                                    "two processes are named " + process.m_name);
            }
        }
    }
}

// Turn int, bool, char, real, sem or an alias into a base type.
BaseType Checker::resolve(const TypeName &type)
{
    if (type.m_alias.empty()) {
        return type.m_base;
    }
    auto alias = m_aliases.find(type.m_alias);
    if (alias == m_aliases.end()) {
        m_diagnostics.error(type.m_location, "ERR_SEMA_UNKNOWN_TYPE",
                            "no type is named " + type.m_alias);
        m_diagnostics.note(type.m_location,
                           "the types are int, bool, char and real; declare others with type " +
                               type.m_alias + " = int;");
        return BaseType::Int;
    }
    return alias->second;
}

// Open a scope.
void Checker::push_scope()
{
    m_scopes.emplace_back();
}

// Close the innermost scope.
void Checker::pop_scope()
{
    m_scopes.pop_back();
}

// Declare a symbol, reporting a second declaration in the same scope.
Symbol *Checker::declare(SymbolKind kind, const std::string &name, const Location &location,
                         BaseType type, bool array)
{
    Symbol &symbol = m_program->m_symbols.emplace_back();
    symbol.m_kind = kind;
    symbol.m_name = name;
    symbol.m_location = location;
    symbol.m_type = type;
    symbol.m_array = array;
    auto &scope = m_scopes.back();
    auto old = scope.find(name);
    if (old != scope.end()) {
        m_diagnostics.error(location, "ERR_SEMA_REDECLARED", name + " is declared twice");
        m_diagnostics.note(old->second->m_location, "the first declaration");
        return &symbol;
    }
    scope.emplace(name, &symbol);
    return &symbol;
}

// Find a symbol, innermost scope first.
Symbol *Checker::lookup(const std::string &name) const
{
    for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope) {
        auto found = scope->find(name);
        if (found != scope->end()) {
            return found->second;
        }
    }
    return nullptr;
}

// Find M.op, or op in the current module and then at the top level.
OpInfo *Checker::find_op(const std::string &module, const std::string &name,
                         const Location &location)
{
    if (!module.empty()) {
        auto ops = m_ops.find(module);
        if (ops == m_ops.end()) {
            m_diagnostics.error(location, "ERR_SEMA_UNKNOWN_MODULE", "no module is named " + module);
            return nullptr;
        }
        auto op = ops->second.find(name);
        if (op == ops->second.end()) {
            m_diagnostics.error(location, "ERR_SEMA_UNKNOWN_OP",
                                "the module " + module + " has no op " + name);
            return nullptr;
        }
        return op->second;
    }
    for (const std::string &where : {m_module, std::string()}) {
        auto ops = m_ops.find(where);
        if (ops == m_ops.end()) {
            continue;
        }
        auto op = ops->second.find(name);
        if (op != ops->second.end()) {
            return op->second;
        }
    }
    m_diagnostics.error(location, "ERR_SEMA_UNKNOWN_OP", "no op is named " + name);
    for (const auto &[other, ops] : m_ops) {
        if (!other.empty() && ops.count(name)) {
            m_diagnostics.note(location, "outside its module, write " + other + "." + name);
        }
    }
    return nullptr;
}

// Check statements in a new scope.
void Checker::check_block(std::vector<std::unique_ptr<Statement>> &statements)
{
    push_scope();
    for (auto &statement : statements) {
        check_statement(*statement);
    }
    pop_scope();
}

// Check one statement by its kind.
void Checker::check_statement(Statement &statement)
{
    switch (statement.m_kind) {
        case StatementKind::Declaration:
            check_declaration(statement);
            break;
        case StatementKind::Block:
            check_block(statement.m_body);
            break;
        case StatementKind::Skip:
        case StatementKind::Hook:
        case StatementKind::Nap:
            break;
        case StatementKind::Assign:
            check_assignment(statement);
            break;
        case StatementKind::If:
            check_typed(*statement.m_condition, BaseType::Bool, "as the condition of if");
            check_block(statement.m_body);
            check_block(statement.m_else);
            break;
        case StatementKind::While:
            check_typed(*statement.m_condition, BaseType::Bool, "as the condition of while");
            check_block(statement.m_body);
            break;
        case StatementKind::For:
            check_for(statement);
            break;
        case StatementKind::Call:
        case StatementKind::Send:
            check_invocation_statement(statement);
            break;
        case StatementKind::Receive:
            check_receive(statement);
            break;
        case StatementKind::In:
            check_in(statement);
            break;
        case StatementKind::NameCall:
            check_name_call(statement);
            break;
        case StatementKind::P:
        case StatementKind::V:
            check_semaphore(*statement.m_target);
            break;
        case StatementKind::Write:
            for (auto &argument : statement.m_arguments) {
                check_expression(*argument);
            }
            break;
    }
}

// Check each name's size and initial value, then declare it.
void Checker::check_declaration(Statement &statement)
{
    BaseType type = resolve(statement.m_type);
    for (Declarator &declarator : statement.m_declarators) {
        if (declarator.m_size) {
            check_typed(*declarator.m_size, BaseType::Int, "as the size of an array");
        }
        if (declarator.m_initial) {
            BaseType value = check_expression(*declarator.m_initial);
            BaseType expected = type == BaseType::Sem ? BaseType::Int : type;
            if (!assignable(expected, value)) {
                m_diagnostics.error(declarator.m_initial->m_location, "ERR_SEMA_TYPE",
                                    declarator.m_name + " is " + type_name(type) +
                                        ", but its initial value is " + type_name(value));
            }
        }
        declarator.m_symbol = declare(SymbolKind::Variable, declarator.m_name,
                                      declarator.m_location, type, declarator.m_size != nullptr);
    }
}

// Check x = e, x += e, x -= e, x *= e, x++ and x--.
void Checker::check_assignment(Statement &statement)
{
    if (!check_lvalue(*statement.m_target, "assigned")) {
        if (statement.m_value) {
            check_expression(*statement.m_value);
        }
        return;
    }
    BaseType target = statement.m_target->m_type;
    const std::string &op = statement.m_operator;
    if (op == "++" || op == "--") {
        if (target != BaseType::Int) {
            m_diagnostics.error(statement.m_location, "ERR_SEMA_TYPE",
                                op + " needs an int, not " + type_name(target));
        }
        return;
    }
    BaseType value = check_expression(*statement.m_value);
    if (op != "=" && (!numeric(target) || !numeric(value))) {
        m_diagnostics.error(statement.m_location, "ERR_SEMA_TYPE",
                            op + " needs numbers, not " + type_name(target) + " and " +
                                type_name(value));
        return;
    }
    if (!assignable(target, value)) {
        m_diagnostics.error(statement.m_value->m_location, "ERR_SEMA_TYPE",
                            "can't assign " + type_name(value) + " to " + type_name(target));
    }
}

// Check the bounds, then the condition and the body with the loop's variable declared.
void Checker::check_for(Statement &statement)
{
    check_typed(*statement.m_from, BaseType::Int, "as the first value of for");
    check_typed(*statement.m_to, BaseType::Int, "as the last value of for");
    push_scope();
    statement.m_index =
        declare(SymbolKind::Index, statement.m_variable, statement.m_location, BaseType::Int);
    if (statement.m_condition) {
        check_typed(*statement.m_condition, BaseType::Bool, "after st");
    }
    check_block(statement.m_body);
    pop_scope();
}

// Check call and send; send can't wait for results.
void Checker::check_invocation_statement(Statement &statement)
{
    Expression &invocation = *statement.m_invocation;
    OpInfo *op = find_op(invocation.m_module, invocation.m_text, invocation.m_location);
    if (!op) {
        for (auto &argument : invocation.m_operands) {
            check_expression(*argument);
        }
        return;
    }
    invocation.m_op = op;
    check_arguments(invocation, *op);
    if (statement.m_kind == StatementKind::Send &&
        (op->m_returns != BaseType::None || !op->all_values())) {
        m_diagnostics.error(statement.m_location, "ERR_SEMA_SEND_RESULTS",
                            "send doesn't wait, so it can't invoke " + op->full_name() +
                                ", which has results");
        m_diagnostics.note(statement.m_location, "use call");
    }
}

// Check receive op(variables): each variable gets a formal's value.
void Checker::check_receive(Statement &statement)
{
    Expression &invocation = *statement.m_invocation;
    OpInfo *op = find_op(invocation.m_module, invocation.m_text, invocation.m_location);
    for (auto &argument : invocation.m_operands) {
        check_lvalue(*argument, "received into");
    }
    if (!op) {
        return;
    }
    invocation.m_op = op;
    serve_by_in(*op, invocation.m_location);
    if (op->m_returns != BaseType::None || !op->all_values()) {
        m_diagnostics.error(statement.m_location, "ERR_SEMA_RECEIVE_RESULTS",
                            "receive can't answer " + op->full_name() +
                                ", which has results; serve it with in");
    }
    if (invocation.m_operands.size() != op->m_formals.size()) {
        m_diagnostics.error(statement.m_location, "ERR_SEMA_ARGUMENT_COUNT",
                            "receive names " + std::to_string(invocation.m_operands.size()) +
                                " variables, but " + op->full_name() + " has " +
                                std::to_string(op->m_formals.size()) + " formals");
        return;
    }
    for (size_t i = 0; i < op->m_formals.size(); i++) {
        Expression &target = *invocation.m_operands[i];
        if (target.m_type != BaseType::None && target.m_type != op->m_formals[i].m_type) {
            m_diagnostics.error(target.m_location, "ERR_SEMA_TYPE",
                                "the formal is " + type_name(op->m_formals[i].m_type) +
                                    ", but the variable is " + type_name(target.m_type));
        }
    }
}

// Check each arm: its op, formals, result, guard, priority and body.
void Checker::check_in(Statement &statement)
{
    for (Arm &arm : statement.m_arms) {
        OpInfo *op = find_op(arm.m_module, arm.m_op, arm.m_location);
        push_scope();
        if (op) {
            arm.m_info = op;
            serve_by_in(*op, arm.m_location);
            if (arm.m_formals.size() != op->m_formals.size()) {
                m_diagnostics.error(arm.m_location, "ERR_SEMA_FORMAL_COUNT",
                                    "the arm names " + std::to_string(arm.m_formals.size()) +
                                        " formals, but " + op->full_name() + " has " +
                                        std::to_string(op->m_formals.size()));
            }
            for (size_t i = 0; i < arm.m_formals.size() && i < op->m_formals.size(); i++) {
                Symbol *formal = declare(SymbolKind::Formal, arm.m_formals[i],
                                         arm.m_formal_locations[i], op->m_formals[i].m_type);
                formal->m_mode = op->m_formals[i].m_mode;
                arm.m_formal_symbols.push_back(formal);
            }
            if (arm.m_returns.empty() != (op->m_returns == BaseType::None)) {
                m_diagnostics.error(arm.m_location, "ERR_SEMA_RETURNS",
                                    op->m_returns == BaseType::None
                                        ? "the arm names a result, but " + op->full_name() +
                                              " returns nothing"
                                        : "the arm must name the result of " + op->full_name() +
                                              ": " + arm.m_op + "(...) returns NAME");
            } else if (!arm.m_returns.empty()) {
                arm.m_returns_symbol =
                    declare(SymbolKind::Returns, arm.m_returns, arm.m_location, op->m_returns);
            }
        }
        m_in_guard = true;
        if (arm.m_guard) {
            check_typed(*arm.m_guard, BaseType::Bool, "as the guard after and");
        }
        if (arm.m_by) {
            check_typed(*arm.m_by, BaseType::Int, "as the priority after by");
        }
        m_in_guard = false;
        check_block(arm.m_body);
        pop_scope();
    }
}

// Decide what NAME(arguments) is: a call of an op, P or V, random, or a hook.
void Checker::check_name_call(Statement &statement)
{
    Expression &invocation = *statement.m_invocation;
    const std::string &name = invocation.m_text;
    bool isOp = m_ops[m_module].count(name) || m_ops[""].count(name);
    if (isOp) {
        statement.m_kind = StatementKind::Call;
        check_invocation_statement(statement);
        return;
    }
    bool isP = name == "P" || name == "wait";
    bool isV = name == "V" || name == "signal";
    if ((isP || isV) && invocation.m_operands.size() == 1) {
        Expression &target = *invocation.m_operands[0];
        Symbol *symbol =
            target.m_kind == ExpressionKind::Name || target.m_kind == ExpressionKind::Index
                ? lookup(target.m_text)
                : nullptr;
        if (symbol && symbol->m_type == BaseType::Sem) {
            statement.m_kind = isP ? StatementKind::P : StatementKind::V;
            statement.m_target = std::move(invocation.m_operands[0]);
            statement.m_invocation.reset();
            check_semaphore(*statement.m_target);
            return;
        }
    }
    if (name == "nap" && invocation.m_operands.size() == 1) {
        statement.m_kind = StatementKind::Nap;
        statement.m_value = std::move(invocation.m_operands[0]);
        statement.m_invocation.reset();
        check_typed(*statement.m_value, BaseType::Int, "as the milliseconds of nap");
        return;
    }
    statement.m_kind = StatementKind::Hook;
    statement.m_name = name;
    statement.m_operator = "()";
    statement.m_arguments = std::move(invocation.m_operands);
    statement.m_invocation.reset();
    for (auto &argument : statement.m_arguments) {
        check_expression(*argument);
    }
}

// Check the count, the types and, for var and res, that the arguments are variables.
void Checker::check_arguments(Expression &invocation, OpInfo &op)
{
    if (invocation.m_operands.size() != op.m_formals.size()) {
        m_diagnostics.error(invocation.m_location, "ERR_SEMA_ARGUMENT_COUNT",
                            op.full_name() + " takes " + std::to_string(op.m_formals.size()) +
                                " arguments, not " +
                                std::to_string(invocation.m_operands.size()));
        for (auto &argument : invocation.m_operands) {
            check_expression(*argument);
        }
        return;
    }
    for (size_t i = 0; i < op.m_formals.size(); i++) {
        Expression &argument = *invocation.m_operands[i];
        const OpFormal &formal = op.m_formals[i];
        if (formal.m_mode == "val") {
            BaseType type = check_expression(argument);
            if (!assignable(formal.m_type, type)) {
                m_diagnostics.error(argument.m_location, "ERR_SEMA_TYPE",
                                    "argument " + std::to_string(i + 1) + " of " +
                                        op.full_name() + " is " + type_name(formal.m_type) +
                                        ", not " + type_name(type));
            }
            continue;
        }
        if (check_lvalue(argument, ("passed as " + formal.m_mode).c_str()) &&
            argument.m_type != formal.m_type) {
            m_diagnostics.error(argument.m_location, "ERR_SEMA_TYPE",
                                "argument " + std::to_string(i + 1) + " of " + op.full_name() +
                                    " is " + formal.m_mode + " " + type_name(formal.m_type) +
                                    ", not " + type_name(argument.m_type));
        }
    }
}

// Mark an op as served by in or receive, which a proc already serving it forbids.
void Checker::serve_by_in(OpInfo &op, const Location &location)
{
    op.m_by_in = true;
    if (op.m_proc) {
        m_diagnostics.error(location, "ERR_SEMA_PROC_AND_IN",
                            "the op " + op.full_name() + " is served by a proc, so in and "
                                                         "receive can't serve it");
        m_diagnostics.note(op.m_proc->m_location, "the proc");
    }
}

// Check an expression and record its type.
BaseType Checker::check_expression(Expression &expression)
{
    BaseType type = BaseType::None;
    switch (expression.m_kind) {
        case ExpressionKind::Integer:
            type = BaseType::Int;
            break;
        case ExpressionKind::Real:
            type = BaseType::Real;
            break;
        case ExpressionKind::Character:
            type = BaseType::Char;
            break;
        case ExpressionKind::Boolean:
            type = BaseType::Bool;
            break;
        case ExpressionKind::String:
            type = BaseType::String;
            break;
        case ExpressionKind::Name:
        case ExpressionKind::Index: {
            Symbol *symbol = lookup(expression.m_text);
            if (!symbol) {
                m_diagnostics.error(expression.m_location, "ERR_SEMA_UNKNOWN_NAME",
                                    "nothing is named " + expression.m_text);
                break;
            }
            expression.m_symbol = symbol;
            bool indexed = expression.m_kind == ExpressionKind::Index;
            if (indexed) {
                check_typed(*expression.m_operands[0], BaseType::Int, "as an index");
            }
            if (indexed != symbol->m_array) {
                m_diagnostics.error(expression.m_location, "ERR_SEMA_INDEX",
                                    indexed ? symbol->m_name + " isn't an array"
                                            : "the array " + symbol->m_name + " needs an index");
                break;
            }
            if (symbol->m_type == BaseType::Sem) {
                m_diagnostics.error(expression.m_location, "ERR_SEMA_SEM_VALUE",
                                    "the semaphore " + symbol->m_name +
                                        " has no value to read; use P and V");
                break;
            }
            type = symbol->m_type;
            break;
        }
        case ExpressionKind::Unary: {
            BaseType operand = check_expression(*expression.m_operands[0]);
            if (expression.m_text == "!") {
                if (operand != BaseType::Bool && operand != BaseType::None) {
                    m_diagnostics.error(expression.m_location, "ERR_SEMA_TYPE",
                                        "! needs a bool, not " + type_name(operand));
                }
                type = BaseType::Bool;
            } else {
                if (!numeric(operand) && operand != BaseType::None) {
                    m_diagnostics.error(expression.m_location, "ERR_SEMA_TYPE",
                                        "- needs a number, not " + type_name(operand));
                }
                type = numeric(operand) ? operand : BaseType::Int;
            }
            break;
        }
        case ExpressionKind::Binary:
            type = check_binary(expression);
            break;
        case ExpressionKind::Invoke:
            type = check_value_invocation(expression);
            break;
        case ExpressionKind::Pending: {
            OpInfo *op = find_op(expression.m_module, expression.m_text, expression.m_location);
            expression.m_op = op;
            type = BaseType::Int;
            if (m_static) {
                m_diagnostics.error(expression.m_location, "ERR_SEMA_INVOKE_STATIC",
                                    "a constant or a module's variable can't count invocations, "
                                    "since no process runs yet");
            }
            break;
        }
        case ExpressionKind::Random:
            type = BaseType::Int;
            break;
    }
    expression.m_type = type;
    return type;
}

// Check an expression that must have a given type.
void Checker::check_typed(Expression &expression, BaseType type, std::string_view context)
{
    BaseType actual = check_expression(expression);
    if (actual != BaseType::None && !assignable(type, actual)) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_TYPE",
                            "expected " + type_name(type) + " " + std::string(context) +
                                ", not " + type_name(actual));
    }
}

// Check arithmetic, comparisons and logic.
BaseType Checker::check_binary(Expression &expression)
{
    BaseType left = check_expression(*expression.m_operands[0]);
    BaseType right = check_expression(*expression.m_operands[1]);
    const std::string &op = expression.m_text;
    if (left == BaseType::None || right == BaseType::None) {
        return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ? BaseType::Int
                                                                            : BaseType::Bool;
    }
    auto mismatch = [&](const std::string &needs) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_TYPE",
                            op + " needs " + needs + ", not " + type_name(left) + " and " +
                                type_name(right));
    };
    if (op == "&&" || op == "||") {
        if (left != BaseType::Bool || right != BaseType::Bool) {
            mismatch("bools");
        }
        return BaseType::Bool;
    }
    if (op == "%") {
        if (left != BaseType::Int || right != BaseType::Int) {
            mismatch("ints");
        }
        return BaseType::Int;
    }
    if (op == "+" || op == "-" || op == "*" || op == "/") {
        if (!numeric(left) || !numeric(right)) {
            mismatch("numbers");
            return BaseType::Int;
        }
        return left == BaseType::Real || right == BaseType::Real ? BaseType::Real : BaseType::Int;
    }
    bool comparable = (numeric(left) && numeric(right)) || (left == right && left != BaseType::String);
    if (!comparable) {
        mismatch("two values of the same type");
    } else if ((op == "<" || op == "<=" || op == ">" || op == ">=") && left == BaseType::Bool) {
        mismatch("numbers or characters");
    }
    return BaseType::Bool;
}

// Check op(arguments) or random(n) used as a value.
BaseType Checker::check_value_invocation(Expression &expression)
{
    bool isOp = !expression.m_module.empty() || m_ops[m_module].count(expression.m_text) ||
                m_ops[""].count(expression.m_text);
    if (!isOp && expression.m_text == "random") {
        expression.m_kind = ExpressionKind::Random;
        if (expression.m_operands.size() != 1) {
            m_diagnostics.error(expression.m_location, "ERR_SEMA_ARGUMENT_COUNT",
                                "random takes one argument, n, and returns 0 to n - 1");
            return BaseType::Int;
        }
        check_typed(*expression.m_operands[0], BaseType::Int, "as the argument of random");
        return BaseType::Int;
    }
    OpInfo *op = find_op(expression.m_module, expression.m_text, expression.m_location);
    if (!op) {
        for (auto &argument : expression.m_operands) {
            check_expression(*argument);
        }
        return BaseType::None;
    }
    expression.m_op = op;
    if (m_static) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_INVOKE_STATIC",
                            "a constant or a module's variable can't invoke " + op->full_name() +
                                ", since no process runs yet");
    }
    if (m_in_guard) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_INVOKE_IN_GUARD",
                            "a guard or a priority can't invoke " + op->full_name());
    }
    if (op->m_returns == BaseType::None || !op->all_values()) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_INVOKE_VALUE",
                            op->m_returns == BaseType::None
                                ? op->full_name() + " returns nothing, so it isn't a value"
                                : op->full_name() + " has var or res formals; invoke it with call");
    }
    check_arguments(expression, *op);
    return op->m_returns == BaseType::None ? BaseType::Int : op->m_returns;
}

// Check that an expression is a variable, an element, a formal or a result.
bool Checker::check_lvalue(Expression &expression, std::string_view context)
{
    if (expression.m_kind != ExpressionKind::Name && expression.m_kind != ExpressionKind::Index) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_NOT_VARIABLE",
                            "only a variable can be " + std::string(context));
        check_expression(expression);
        return false;
    }
    BaseType type = check_expression(expression);
    Symbol *symbol = expression.m_symbol;
    if (!symbol || type == BaseType::None) {
        return false;
    }
    if (symbol->m_kind == SymbolKind::Constant || symbol->m_kind == SymbolKind::Index) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_NOT_VARIABLE",
                            symbol->m_name + " is " +
                                (symbol->m_kind == SymbolKind::Constant ? "a constant"
                                                                        : "an index") +
                                ", so it can't be " + std::string(context));
        return false;
    }
    return true;
}

// Check that an expression is a semaphore or an element of an array of them.
bool Checker::check_semaphore(Expression &expression)
{
    Symbol *symbol = lookup(expression.m_text);
    if (!symbol || symbol->m_type != BaseType::Sem) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_NOT_SEM",
                            "P and V need a semaphore");
        return false;
    }
    expression.m_symbol = symbol;
    bool indexed = expression.m_kind == ExpressionKind::Index;
    if (indexed) {
        check_typed(*expression.m_operands[0], BaseType::Int, "as an index");
    }
    if (indexed != symbol->m_array) {
        m_diagnostics.error(expression.m_location, "ERR_SEMA_INDEX",
                            indexed ? symbol->m_name + " isn't an array"
                                    : "the array " + symbol->m_name + " needs an index");
        return false;
    }
    expression.m_type = BaseType::Sem;
    return true;
}

// A real takes an int; otherwise the types must match.
bool Checker::assignable(BaseType target, BaseType value)
{
    return target == value || (target == BaseType::Real && value == BaseType::Int) ||
           value == BaseType::None;
}

// Whether a type is int or real.
bool Checker::numeric(BaseType type)
{
    return type == BaseType::Int || type == BaseType::Real;
}

// Name a type.
std::string Checker::type_name(BaseType type)
{
    switch (type) {
        case BaseType::Int:
            return "int";
        case BaseType::Bool:
            return "bool";
        case BaseType::Char:
            return "char";
        case BaseType::Real:
            return "real";
        case BaseType::Sem:
            return "sem";
        case BaseType::String:
            return "a string";
        case BaseType::None:
            break;
    }
    return "nothing";
}
