// Writes a checked program as an Ada main procedure.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "emitter.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

// Ada 2022's reserved words.
constexpr std::string_view reservedWords[] = {
    "abort",     "abs",        "abstract",  "accept",    "access", "aliased",  "all",
    "and",       "array",      "at",        "begin",     "body",   "case",     "constant",
    "declare",   "delay",      "delta",     "digits",    "do",     "else",     "elsif",
    "end",       "entry",      "exception", "exit",      "for",    "function", "generic",
    "goto",      "if",         "in",        "interface", "is",     "limited",  "loop",
    "mod",       "new",        "not",       "null",      "of",     "or",       "others",
    "out",       "overriding", "package",   "parallel",  "pragma", "private",  "procedure",
    "protected", "raise",      "range",     "record",    "rem",    "renames",  "requeue",
    "return",    "reverse",    "select",    "separate",  "some",   "subtype",  "synchronized",
    "tagged",    "task",       "terminate", "then",      "type",   "until",    "use",
    "when",      "while",      "with",      "xor",
};

// Names of the runtime and of the generated code, which no user name may take.
constexpr std::string_view fixedNames[] = {
    "Ada",       "Mpd_Runtime", "Standard",   "Integer",     "Boolean",  "Character",
    "Long_Float", "Natural",    "Positive",   "True",        "False",    "Pid",
    "Value",     "Value_Array", "Max_Args",   "Val",         "To_Int",   "To_Bool",
    "To_Char",   "To_Real",     "Completion", "Invocation",  "Invocation_Access",
    "Semaphore", "Init",        "Set_Op_Name", "Op_Name",    "Spawn_Name", "Register",
    "Name_Of",   "Start",       "Wait_Start", "Tracing",     "Trace",    "Running",
    "Done",      "Fail",        "Hook",       "Write_Line",  "Random",   "P",
    "V",         "New_Invocation", "Enqueue", "Await",       "Free",     "Finish",
    "Pending",   "Seize",       "Release",    "First",       "Remove",   "Version",
    "Sleep",     "Img",         "Self",       "Index",       "Result",   "Ignored",
    "E",         "Op_Count",    "Started",    "Copy",        "Number",   "Found",
    "Key",       "Best_Key",    "Cursor",
};

// Fold a name to lower case.
static std::string lower(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Take the reserved words and the fixed names.
NameTable::NameTable()
{
    for (std::string_view word : reservedWords) {
        reserve(word);
    }
    for (std::string_view name : fixedNames) {
        reserve(name);
    }
}

// Make a fresh name, adding _2, _3, ... while it clashes.
std::string NameTable::make(std::string_view base)
{
    std::string name = sanitize(base);
    std::string candidate = name;
    for (int n = 2; taken(candidate); n++) {
        candidate = name + "_" + std::to_string(n);
    }
    reserve(candidate);
    return candidate;
}

// Take a name.
void NameTable::reserve(std::string_view name)
{
    m_taken.insert(lower(name));
}

// Check whether a name is taken.
bool NameTable::taken(std::string_view name) const
{
    return m_taken.count(lower(name)) > 0;
}

// Drop underscores Ada forbids and start with a letter.
std::string NameTable::sanitize(std::string_view name)
{
    std::string result;
    for (char c : name) {
        if (c == '_' && (result.empty() || result.back() == '_')) {
            continue;
        }
        result += c;
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    if (result.empty() || !std::isalpha(static_cast<unsigned char>(result[0]))) {
        result = "V_" + result;
    }
    return result;
}

// Prepare to write.
AdaWriter::AdaWriter(Program &program, std::string unit, std::string file)
    : m_program(program)
    , m_unit(std::move(unit))
    , m_file(std::move(file))
{}

// Write the header, the declarations, the bodies and the main statements.
std::string AdaWriter::write()
{
    name_everything();
    line("--  Ada translation of " + m_file + " by mpdcc.");
    line("pragma Warnings (Off);");
    line("with Ada.Exceptions;");
    line("with Mpd_Runtime; use Mpd_Runtime;");
    line("");
    line("procedure " + m_unit + " is");
    m_indent++;
    write_constants();
    write_variables();
    write_specs();
    for (const OpInfo &op : m_program.m_ops) {
        write_invokers(op);
    }
    for (Module &module : m_program.m_ast->m_modules) {
        for (const ProcDecl &proc : module.m_procs) {
            if (proc.m_info) {
                write_proc(proc);
            }
        }
    }
    for (const OpInfo &op : m_program.m_ops) {
        if (op.m_proc && op.m_returns == BaseType::None && op.all_values()) {
            write_spawn(op);
        }
    }
    for (Module &module : m_program.m_ast->m_modules) {
        for (const ProcessDecl &process : module.m_processes) {
            write_process(process);
        }
    }
    m_indent--;
    write_main();
    return m_out.str();
}

// Check that a name is lower-case Ada: letters, digits and single inner underscores.
bool AdaWriter::valid_unit(std::string_view name)
{
    if (name.empty() || !std::islower(static_cast<unsigned char>(name[0])) || name.back() == '_') {
        return false;
    }
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = name[i];
        bool ok = std::islower(c) || std::isdigit(c) || (c == '_' && name[i - 1] != '_');
        if (!ok) {
            return false;
        }
    }
    NameTable reserved;
    return !reserved.taken(name);
}

// Lower-case a file's name, turning other characters into single underscores.
std::string AdaWriter::unit_from(std::string_view name)
{
    std::string unit;
    for (char c : name) {
        unsigned char u = static_cast<unsigned char>(c);
        char next = std::isalnum(u) && u < 0x80 ? static_cast<char>(std::tolower(u)) : '_';
        if (next == '_' && (unit.empty() || unit.back() == '_')) {
            continue;
        }
        unit += next;
    }
    while (!unit.empty() && unit.back() == '_') {
        unit.pop_back();
    }
    if (unit.empty() || !std::islower(static_cast<unsigned char>(unit[0]))) {
        unit = "mpd_" + unit;
    }
    if (!valid_unit(unit)) {
        unit += "_main";
    }
    return unit;
}

// Name the ops first, with every subprogram their names derive, then the symbols.
void AdaWriter::name_everything()
{
    m_names.reserve(m_unit);
    for (OpInfo &op : m_program.m_ops) {
        std::string base = NameTable::sanitize(op.m_module.empty() ? op.m_name
                                                                   : op.m_module + "_" + op.m_name);
        std::string candidate = base;
        const char *prefixes[] = {"Op_", "Invoke_", "Send_To_", "Proc_", "Spawn_"};
        auto clashes = [&](const std::string &name) {
            for (const char *prefix : prefixes) {
                if (m_names.taken(prefix + name) || m_names.taken(prefix + name + "_Value") ||
                    m_names.taken(prefix + name + "_Access")) {
                    return true;
                }
            }
            return false;
        };
        for (int n = 2; clashes(candidate); n++) {
            candidate = base + "_" + std::to_string(n);
        }
        op.m_ada = candidate;
        for (const char *prefix : prefixes) {
            m_names.reserve(prefix + candidate);
            m_names.reserve(prefix + candidate + "_Value");
            m_names.reserve(prefix + candidate + "_Access");
        }
    }
    for (ConstantDecl &constant : m_program.m_ast->m_constants) {
        name_symbol(constant.m_symbol);
    }
    for (Module &module : m_program.m_ast->m_modules) {
        for (ProcessDecl &process : module.m_processes) {
            m_names.reserve("Process_" + NameTable::sanitize(process.m_name));
            m_names.reserve("Process_" + NameTable::sanitize(process.m_name) + "_Access");
        }
    }
    for (Module &module : m_program.m_ast->m_modules) {
        name_statements(module.m_declarations);
        for (ProcDecl &proc : module.m_procs) {
            for (Symbol *formal : proc.m_formal_symbols) {
                name_symbol(formal);
            }
            name_symbol(proc.m_returns_symbol);
            name_statements(proc.m_body);
        }
        for (ProcessDecl &process : module.m_processes) {
            name_symbol(process.m_index_symbol);
            name_statements(process.m_body);
        }
    }
}

// Name the symbols that statements declare, at any depth.
void AdaWriter::name_statements(std::vector<std::unique_ptr<Statement>> &statements)
{
    for (auto &statement : statements) {
        for (Declarator &declarator : statement->m_declarators) {
            name_symbol(declarator.m_symbol);
        }
        name_symbol(statement->m_index);
        name_statements(statement->m_body);
        name_statements(statement->m_else);
        for (Arm &arm : statement->m_arms) {
            for (Symbol *formal : arm.m_formal_symbols) {
                name_symbol(formal);
            }
            name_symbol(arm.m_returns_symbol);
            name_statements(arm.m_body);
        }
    }
}

// Name a symbol after its source name.
void AdaWriter::name_symbol(Symbol *symbol)
{
    if (symbol && symbol->m_ada.empty()) {
        symbol->m_ada = m_names.make(symbol->m_name);
    }
}

// Write a line indented by three spaces a level.
void AdaWriter::line(std::string_view text)
{
    if (!text.empty()) {
        m_out << std::string(static_cast<size_t>(m_indent) * 3, ' ') << text;
    }
    m_out << '\n';
}

// Write each constant and each op's number.
void AdaWriter::write_constants()
{
    for (const ConstantDecl &constant : m_program.m_ast->m_constants) {
        if (!constant.m_symbol || !constant.m_value) {
            continue;
        }
        const Symbol &symbol = *constant.m_symbol;
        line(symbol.m_ada + " : constant " + ada_type(symbol.m_type) + " := " +
             converted(*constant.m_value, symbol.m_type) + ";");
    }
    for (const OpInfo &op : m_program.m_ops) {
        line("Op_" + op.m_ada + " : constant := " + std::to_string(op.m_number) + ";");
    }
    line("Op_Count : constant := " + std::to_string(m_program.m_ops.size()) + ";");
    line("");
}

// Write the variables of the top level and of each module, volatile since tasks share them.
void AdaWriter::write_variables()
{
    for (Module &module : m_program.m_ast->m_modules) {
        for (const auto &declaration : module.m_declarations) {
            write_declaration(*declaration, true);
        }
    }
    line("");
}

// Write the specs of the procs, the tasks a send spawns, and the processes.
void AdaWriter::write_specs()
{
    for (const OpInfo &op : m_program.m_ops) {
        if (!op.m_proc) {
            continue;
        }
        std::string spec = "procedure Proc_" + op.m_ada + " (Self : Pid";
        const ProcDecl &proc = *op.m_proc;
        for (size_t i = 0; i < proc.m_formal_symbols.size(); i++) {
            const Symbol &formal = *proc.m_formal_symbols[i];
            std::string mode = formal.m_mode == "var" ? "in out " : formal.m_mode == "res" ? "out " : "";
            std::string name = formal.m_mode == "val" ? formal.m_ada + "_In" : formal.m_ada;
            spec += "; " + name + " : " + mode + ada_type(formal.m_type);
        }
        if (proc.m_returns_symbol) {
            spec += "; " + proc.m_returns_symbol->m_ada + " : out " +
                    ada_type(proc.m_returns_symbol->m_type);
        }
        line(spec + ");");
        if (op.m_returns == BaseType::None && op.all_values()) {
            line("task type Spawn_" + op.m_ada + " (Inv : Invocation_Access);");
            line("type Spawn_" + op.m_ada + "_Access is access Spawn_" + op.m_ada + ";");
        }
    }
    for (Module &module : m_program.m_ast->m_modules) {
        for (const ProcessDecl &process : module.m_processes) {
            std::string name = "Process_" + NameTable::sanitize(process.m_name);
            line("task type " + name + (process.m_index.empty() ? "" : " (Index : Integer)") + ";");
            line("type " + name + "_Access is access " + name + ";");
        }
    }
    line("");
}

// Write Invoke_X, which calls the op and waits, Invoke_X_Value for expressions, and Send_To_X.
void AdaWriter::write_invokers(const OpInfo &op)
{
    std::string params = "Self : Pid";
    std::string trace = "\"" + op.full_name() + "(\"";
    for (size_t i = 0; i < op.m_formals.size(); i++) {
        const OpFormal &formal = op.m_formals[i];
        std::string arg = "Arg_" + std::to_string(i + 1);
        std::string mode = formal.m_mode == "var" ? "in out " : formal.m_mode == "res" ? "out " : "";
        params += "; " + arg + " : " + mode + ada_type(formal.m_type);
        trace += std::string(i ? " & \", \"" : "") + " & " +
                 (formal.m_mode == "res" ? std::string("\"_\"") : "Img (" + arg + ")");
    }
    trace += " & \")\"";
    std::string withResult = params;
    if (op.m_returns != BaseType::None) {
        withResult += "; Result : out " + ada_type(op.m_returns);
    }
    std::string name = "Invoke_" + op.m_ada;
    line("procedure " + name + " (" + withResult + ") is");
    m_indent++;
    if (!op.m_proc) {
        line("Inv : Invocation_Access := New_Invocation (Op_" + op.m_ada + ", Self, True);");
    }
    m_indent--;
    line("begin");
    m_indent++;
    line("if Tracing then");
    line("   Trace (Self, \"calls \" & " + trace + ");");
    line("end if;");
    if (op.m_proc) {
        std::string call = "Proc_" + op.m_ada + " (Self";
        for (size_t i = 0; i < op.m_formals.size(); i++) {
            call += ", Arg_" + std::to_string(i + 1);
        }
        if (op.m_returns != BaseType::None) {
            call += ", Result";
        }
        line(call + ");");
    } else {
        for (size_t i = 0; i < op.m_formals.size(); i++) {
            if (op.m_formals[i].m_mode != "res") {
                line("Inv.Args (" + std::to_string(i + 1) + ") := Val (Arg_" +
                     std::to_string(i + 1) + ");");
            }
        }
        line("Enqueue (Inv);");
        line("Await (Self, Inv, \"call " + op.full_name() + "\");");
        for (size_t i = 0; i < op.m_formals.size(); i++) {
            if (op.m_formals[i].m_mode != "val") {
                line("Arg_" + std::to_string(i + 1) + " := " + from_value(op.m_formals[i].m_type) +
                     " (Inv.Args (" + std::to_string(i + 1) + "));");
            }
        }
        if (op.m_returns != BaseType::None) {
            line("Result := " + from_value(op.m_returns) + " (Inv.Result);");
        }
        line("Free (Inv);");
    }
    m_indent--;
    line("end " + name + ";");
    line("");
    if (op.m_returns != BaseType::None && op.all_values()) {
        line("function " + name + "_Value (" + params + ") return " + ada_type(op.m_returns) + " is");
        line("   Result : " + ada_type(op.m_returns) + ";");
        line("begin");
        std::string call = "   " + name + " (Self";
        for (size_t i = 0; i < op.m_formals.size(); i++) {
            call += ", Arg_" + std::to_string(i + 1);
        }
        line(call + ", Result);");
        line("   return Result;");
        line("end " + name + "_Value;");
        line("");
    }
    if (op.m_returns != BaseType::None || !op.all_values()) {
        return;
    }
    line("procedure Send_To_" + op.m_ada + " (" + params + ") is");
    line("   Inv : constant Invocation_Access := New_Invocation (Op_" + op.m_ada + ", Self, False);");
    line("begin");
    m_indent++;
    line("if Tracing then");
    line("   Trace (Self, \"sends \" & " + trace + ");");
    line("end if;");
    for (size_t i = 0; i < op.m_formals.size(); i++) {
        line("Inv.Args (" + std::to_string(i + 1) + ") := Val (Arg_" + std::to_string(i + 1) + ");");
    }
    if (op.m_proc) {
        line("declare");
        line("   Started : constant Spawn_" + op.m_ada + "_Access := new Spawn_" + op.m_ada +
             " (Inv);");
        line("begin");
        line("   null;");
        line("end;");
    } else {
        line("Enqueue (Inv);");
    }
    m_indent--;
    line("end Send_To_" + op.m_ada + ";");
    line("");
}

// Write a proc as a procedure that runs in its caller's process.
void AdaWriter::write_proc(const ProcDecl &proc)
{
    const OpInfo &op = *proc.m_info;
    std::string spec = "procedure Proc_" + op.m_ada + " (Self : Pid";
    for (const Symbol *formal : proc.m_formal_symbols) {
        std::string mode = formal->m_mode == "var" ? "in out " : formal->m_mode == "res" ? "out " : "";
        std::string name = formal->m_mode == "val" ? formal->m_ada + "_In" : formal->m_ada;
        spec += "; " + name + " : " + mode + ada_type(formal->m_type);
    }
    if (proc.m_returns_symbol) {
        spec += "; " + proc.m_returns_symbol->m_ada + " : out " +
                ada_type(proc.m_returns_symbol->m_type);
    }
    line(spec + ") is");
    m_indent++;
    for (const Symbol *formal : proc.m_formal_symbols) {
        if (formal->m_mode == "val") {
            line(formal->m_ada + " : " + ada_type(formal->m_type) + " := " + formal->m_ada + "_In;");
        }
    }
    m_indent--;
    line("begin");
    m_indent++;
    for (const Symbol *formal : proc.m_formal_symbols) {
        if (formal->m_mode == "res") {
            line(formal->m_ada + " := " + default_value(formal->m_type) + ";");
        }
    }
    if (proc.m_returns_symbol) {
        line(proc.m_returns_symbol->m_ada + " := " +
             default_value(proc.m_returns_symbol->m_type) + ";");
    }
    write_statements(proc.m_body);
    m_indent--;
    line("end Proc_" + op.m_ada + ";");
    line("");
}

// Write the task that runs a proc for a send, as a process of its own.
void AdaWriter::write_spawn(const OpInfo &op)
{
    std::string name = "Spawn_" + op.m_ada;
    line("task body " + name + " is");
    line("   Self : constant Pid := Register (Spawn_Name (Op_" + op.m_ada + "));");
    line("begin");
    m_indent++;
    std::string call = "Proc_" + op.m_ada + " (Self";
    for (size_t i = 0; i < op.m_formals.size(); i++) {
        call += ", " + from_value(op.m_formals[i].m_type) + " (Inv.Args (" + std::to_string(i + 1) +
                "))";
    }
    line(call + ");");
    line("declare");
    line("   Copy : Invocation_Access := Inv;");
    line("begin");
    line("   Free (Copy);");
    line("end;");
    line("Done (Self);");
    m_indent--;
    line("exception");
    line("   when E : others =>");
    line("      Fail (Self, Ada.Exceptions.Exception_Name (E) & \": \" & "
         "Ada.Exceptions.Exception_Message (E));");
    line("end " + name + ";");
    line("");
}

// Write a process's task body: register, wait for the start, run, and mark the end.
void AdaWriter::write_process(const ProcessDecl &process)
{
    std::string name = "Process_" + NameTable::sanitize(process.m_name);
    line("task body " + name + " is");
    m_indent++;
    if (process.m_index.empty()) {
        line("Self : constant Pid := Register (" + string_literal(process.m_name) + ");");
    } else {
        line("Self : constant Pid := Register (" + string_literal(process.m_name + "(") +
             " & Img (Index) & \")\");");
        line(process.m_index_symbol->m_ada + " : constant Integer := Index;");
    }
    m_indent--;
    line("begin");
    m_indent++;
    line("Wait_Start;");
    write_statements(process.m_body);
    line("Done (Self);");
    m_indent--;
    line("exception");
    line("   when E : others =>");
    line("      Fail (Self, Ada.Exceptions.Exception_Name (E) & \": \" & "
         "Ada.Exceptions.Exception_Message (E));");
    line("end " + name + ";");
    line("");
}

// Write the main statements: initialize the runtime, create the processes, and start them.
void AdaWriter::write_main()
{
    line("begin");
    m_indent++;
    line("Init (Op_Count);");
    for (const OpInfo &op : m_program.m_ops) {
        line("Set_Op_Name (Op_" + op.m_ada + ", " + string_literal(op.full_name()) + ");");
    }
    for (Module &module : m_program.m_ast->m_modules) {
        for (const ProcessDecl &process : module.m_processes) {
            std::string name = "Process_" + NameTable::sanitize(process.m_name);
            if (process.m_index.empty()) {
                line("declare");
                line("   Started : constant " + name + "_Access := new " + name + ";");
                line("begin");
                line("   null;");
                line("end;");
                continue;
            }
            line("for Number in " + expression(*process.m_from) + " .. " +
                 expression(*process.m_to) + " loop");
            line("   declare");
            line("      Started : constant " + name + "_Access := new " + name + " (Number);");
            line("   begin");
            line("      null;");
            line("   end;");
            line("end loop;");
        }
    }
    line("Start;");
    m_indent--;
    line("end " + m_unit + ";");
}

// Write statements, wrapping those after a declaration in declare ... begin ... end.
void AdaWriter::write_statements(const std::vector<std::unique_ptr<Statement>> &statements,
                                 size_t from)
{
    bool wrote = false;
    for (size_t i = from; i < statements.size(); i++) {
        const Statement &statement = *statements[i];
        if (statement.m_kind == StatementKind::Declaration) {
            line("declare");
            m_indent++;
            write_declaration(statement, false);
            m_indent--;
            line("begin");
            m_indent++;
            write_statements(statements, i + 1);
            m_indent--;
            line("end;");
            return;
        }
        if (statement.m_kind == StatementKind::Skip) {
            continue;
        }
        write_statement(statement);
        wrote = true;
    }
    if (!wrote) {
        line("null;");
    }
}

// Write one statement.
void AdaWriter::write_statement(const Statement &statement)
{
    switch (statement.m_kind) {
        case StatementKind::Declaration:
        case StatementKind::Skip:
        case StatementKind::NameCall:
            line("null;");
            break;
        case StatementKind::Block:
            line("declare");
            line("begin");
            m_indent++;
            write_statements(statement.m_body);
            m_indent--;
            line("end;");
            break;
        case StatementKind::Assign: {
            const Expression &target = *statement.m_target;
            std::string name = expression(target);
            const std::string &op = statement.m_operator;
            if (op == "++" || op == "--") {
                line(name + " := " + name + (op == "++" ? " + 1;" : " - 1;"));
            } else if (op == "=") {
                line(name + " := " + converted(*statement.m_value, target.m_type) + ";");
            } else {
                line(name + " := " + name + " " + op.substr(0, 1) + " " +
                     converted(*statement.m_value, target.m_type) + ";");
            }
            break;
        }
        case StatementKind::If:
            line("if " + expression(*statement.m_condition) + " then");
            m_indent++;
            write_statements(statement.m_body);
            m_indent--;
            if (!statement.m_else.empty()) {
                line("else");
                m_indent++;
                write_statements(statement.m_else);
                m_indent--;
            }
            line("end if;");
            break;
        case StatementKind::While:
            line("while " + expression(*statement.m_condition) + " loop");
            m_indent++;
            write_statements(statement.m_body);
            m_indent--;
            line("end loop;");
            break;
        case StatementKind::For:
            line("for " + statement.m_index->m_ada + " in " + expression(*statement.m_from) +
                 " .. " + expression(*statement.m_to) + " loop");
            m_indent++;
            if (statement.m_condition) {
                line("if " + expression(*statement.m_condition) + " then");
                m_indent++;
                write_statements(statement.m_body);
                m_indent--;
                line("end if;");
            } else {
                write_statements(statement.m_body);
            }
            m_indent--;
            line("end loop;");
            break;
        case StatementKind::Call:
        case StatementKind::Send:
            write_invocation(statement);
            break;
        case StatementKind::Receive:
        case StatementKind::In:
            write_in(statement);
            break;
        case StatementKind::P: {
            std::string target = expression(*statement.m_target);
            std::string what = "\"P(" + statement.m_target->m_text + ")\"";
            if (statement.m_target->m_kind == ExpressionKind::Index) {
                what = "\"P(" + statement.m_target->m_text + "[\" & Img (" +
                       expression(*statement.m_target->m_operands[0]) + ") & \"])\"";
            }
            line("P (" + target + ", Self, " + what + ");");
            break;
        }
        case StatementKind::V:
            line("V (" + expression(*statement.m_target) + ");");
            break;
        case StatementKind::Nap:
            line("delay Duration (" + expression(*statement.m_value) + ") / 1000.0;");
            break;
        case StatementKind::Write: {
            std::string text;
            for (const auto &argument : statement.m_arguments) {
                text += std::string(text.empty() ? "" : " & \" \" & ") + image(*argument);
            }
            line("Write_Line (" + (text.empty() ? std::string("\"\"") : text) + ");");
            break;
        }
        case StatementKind::Hook: {
            std::string text = string_literal(statement.m_name);
            if (statement.m_operator == "()") {
                text = string_literal(statement.m_name + "(");
                for (size_t i = 0; i < statement.m_arguments.size(); i++) {
                    text += std::string(i ? " & \", \"" : "") + " & " +
                            image(*statement.m_arguments[i]);
                }
                text += " & \")\"";
            }
            line("Hook (Self, " + text + ");");
            break;
        }
    }
}

// Declare each name: a semaphore, a value or an array, with its initial value or a default.
void AdaWriter::write_declaration(const Statement &statement, bool shared)
{
    std::string aspect = shared ? " with Volatile" : "";
    for (const Declarator &declarator : statement.m_declarators) {
        const Symbol &symbol = *declarator.m_symbol;
        std::string bounds;
        if (declarator.m_size) {
            bounds = "array (0 .. " + expression(*declarator.m_size) + " - 1) of ";
        }
        if (symbol.m_type == BaseType::Sem) {
            std::string initial = declarator.m_initial ? expression(*declarator.m_initial) : "0";
            line(symbol.m_ada + " : " + bounds + "Semaphore (" + initial + ");");
            continue;
        }
        std::string initial = declarator.m_initial ? converted(*declarator.m_initial, symbol.m_type)
                                                   : default_value(symbol.m_type);
        if (declarator.m_size) {
            initial = "(others => " + initial + ")";
        }
        line(symbol.m_ada + " : " + bounds + ada_type(symbol.m_type) + " := " + initial + aspect +
             ";");
    }
}

// Write call op(...) or send op(...), discarding a returned value.
void AdaWriter::write_invocation(const Statement &statement)
{
    const Expression &invocation = *statement.m_invocation;
    const OpInfo &op = *invocation.m_op;
    if (statement.m_kind == StatementKind::Send) {
        line("Send_To_" + op.m_ada + " (Self" + arguments(invocation) + ");");
        return;
    }
    if (op.m_returns == BaseType::None) {
        line("Invoke_" + op.m_ada + " (Self" + arguments(invocation) + ");");
        return;
    }
    line("declare");
    line("   Ignored : " + ada_type(op.m_returns) + ";");
    line("begin");
    line("   Invoke_" + op.m_ada + " (Self" + arguments(invocation) + ", Ignored);");
    line("end;");
}

// Write an in statement: under the runtime's lock, scan every arm's queue for the oldest
// invocation whose guard holds, sleep until a new invocation if there is none, then serve it.
void AdaWriter::write_in(const Statement &statement)
{
    std::vector<const Arm *> arms;
    Arm receive;
    if (statement.m_kind == StatementKind::Receive) {
        receive.m_info = statement.m_invocation->m_op;
        arms.push_back(&receive);
    } else {
        for (const Arm &arm : statement.m_arms) {
            arms.push_back(&arm);
        }
    }
    std::string invocation = m_names.make("Inv");
    std::string chosen = m_names.make("Arm");
    std::string seen = m_names.make("Seen");
    std::string what = statement.m_kind == StatementKind::Receive ? "receive " : "in ";
    for (size_t i = 0; i < arms.size(); i++) {
        what += std::string(i ? ", " : "") + arms[i]->m_info->full_name();
    }
    line("declare");
    line("   " + invocation + " : Invocation_Access;");
    line("   " + chosen + " : Natural := 0;");
    line("   " + seen + " : Natural;");
    line("begin");
    m_indent++;
    line("loop");
    m_indent++;
    line("Seize;");
    for (size_t i = 0; i < arms.size(); i++) {
        write_scan(*arms[i], i + 1, invocation, chosen);
    }
    line("if " + chosen + " /= 0 then");
    line("   Remove (" + invocation + ");");
    line("   Release;");
    line("   exit;");
    line("end if;");
    line(seen + " := Version;");
    line("Release;");
    line("Sleep (Self, " + seen + ", " + string_literal(what) + ");");
    m_indent--;
    line("end loop;");
    if (statement.m_kind == StatementKind::Receive) {
        const OpInfo &op = *statement.m_invocation->m_op;
        std::string trace = string_literal("receives " + op.full_name() + "(");
        const auto &targets = statement.m_invocation->m_operands;
        for (size_t i = 0; i < targets.size() && i < op.m_formals.size(); i++) {
            line(expression(*targets[i]) + " := " + from_value(op.m_formals[i].m_type) + " (" +
                 invocation + ".Args (" + std::to_string(i + 1) + "));");
            trace += std::string(i ? " & \", \"" : "") + " & Img (" + expression(*targets[i]) + ")";
        }
        line("if Tracing then");
        line("   Trace (Self, " + trace + " & \") from \" & Name_Of (" + invocation + ".Caller));");
        line("end if;");
        line("Finish (" + invocation + ");");
        m_indent--;
        line("end;");
        return;
    }
    line("case " + chosen + " is");
    m_indent++;
    for (size_t k = 0; k < arms.size(); k++) {
        const Arm &arm = *arms[k];
        const OpInfo &op = *arm.m_info;
        line("when " + std::to_string(k + 1) + " =>");
        m_indent++;
        line("declare");
        m_indent++;
        write_formals(arm, invocation, false);
        m_indent--;
        line("begin");
        m_indent++;
        std::string trace = string_literal("serves " + op.full_name() + "(");
        for (size_t i = 0; i < arm.m_formal_symbols.size(); i++) {
            const Symbol &formal = *arm.m_formal_symbols[i];
            trace += std::string(i ? " & \", \"" : "") + " & " +
                     (formal.m_mode == "res" ? std::string("\"_\"") : "Img (" + formal.m_ada + ")");
        }
        line("if Tracing then");
        line("   Trace (Self, " + trace + " & \") from \" & Name_Of (" + invocation + ".Caller));");
        line("end if;");
        write_statements(arm.m_body);
        for (size_t i = 0; i < arm.m_formal_symbols.size(); i++) {
            const Symbol &formal = *arm.m_formal_symbols[i];
            if (formal.m_mode != "val") {
                line(invocation + ".Args (" + std::to_string(i + 1) + ") := Val (" + formal.m_ada +
                     ");");
            }
        }
        if (arm.m_returns_symbol) {
            line(invocation + ".Result := Val (" + arm.m_returns_symbol->m_ada + ");");
        }
        m_indent--;
        line("end;");
        m_indent--;
    }
    line("when others =>");
    line("   null;");
    m_indent--;
    line("end case;");
    line("Finish (" + invocation + ");");
    m_indent--;
    line("end;");
}

// Scan one arm's queue: the first invocation whose guard holds, or the one with the smallest
// priority; it wins if it is older than the best of the arms before.
void AdaWriter::write_scan(const Arm &arm, size_t number, const std::string &invocation,
                           const std::string &chosen)
{
    const OpInfo &op = *arm.m_info;
    std::string better = "(" + chosen + " = 0 or else Cursor.Seq < " + invocation + ".Seq)";
    line("declare");
    line("   Cursor : Invocation_Access := First (Op_" + op.m_ada + ");");
    if (arm.m_by) {
        line("   Found : Invocation_Access;");
        line("   Key, Best_Key : Integer := 0;");
    }
    line("begin");
    m_indent++;
    line("while Cursor /= null loop");
    m_indent++;
    bool binds = arm.m_guard || arm.m_by;
    if (binds) {
        line("declare");
        m_indent++;
        write_formals(arm, "Cursor", true);
        m_indent--;
        line("begin");
        m_indent++;
    }
    std::string guard = arm.m_guard ? expression(*arm.m_guard) : "True";
    if (arm.m_by) {
        line("if " + guard + " then");
        line("   Key := " + expression(*arm.m_by) + ";");
        line("   if Found = null or else Key < Best_Key then");
        line("      Found := Cursor;");
        line("      Best_Key := Key;");
        line("   end if;");
        line("end if;");
    } else {
        line("if " + guard + " then");
        line("   if " + better + " then");
        line("      " + invocation + " := Cursor;");
        line("      " + chosen + " := " + std::to_string(number) + ";");
        line("   end if;");
        line("   exit;");
        line("end if;");
    }
    if (binds) {
        m_indent--;
        line("end;");
    }
    line("Cursor := Cursor.Next;");
    m_indent--;
    line("end loop;");
    if (arm.m_by) {
        line("if Found /= null and then (" + chosen + " = 0 or else Found.Seq < " + invocation +
             ".Seq) then");
        line("   " + invocation + " := Found;");
        line("   " + chosen + " := " + std::to_string(number) + ";");
        line("end if;");
    }
    m_indent--;
    line("end;");
}

// Declare an arm's formals and its result from an invocation's arguments.
void AdaWriter::write_formals(const Arm &arm, const std::string &invocation, bool constant)
{
    for (size_t i = 0; i < arm.m_formal_symbols.size(); i++) {
        const Symbol &formal = *arm.m_formal_symbols[i];
        std::string value = formal.m_mode == "res"
                                ? default_value(formal.m_type)
                                : from_value(formal.m_type) + " (" + invocation + ".Args (" +
                                      std::to_string(i + 1) + "))";
        line(formal.m_ada + " : " + (constant ? "constant " : "") + ada_type(formal.m_type) +
             " := " + value + ";");
    }
    if (arm.m_returns_symbol) {
        line(arm.m_returns_symbol->m_ada + " : " + ada_type(arm.m_returns_symbol->m_type) + " := " +
             default_value(arm.m_returns_symbol->m_type) + ";");
    }
}

// Spell an expression, in parentheses where it has operators.
std::string AdaWriter::expression(const Expression &e)
{
    switch (e.m_kind) {
        case ExpressionKind::Integer:
            return std::to_string(e.m_integer);
        case ExpressionKind::Real: {
            char buffer[64];
            std::snprintf(buffer, sizeof buffer, "%.17g", e.m_real);
            std::string text = buffer;
            size_t exponent = text.find_first_of("eE");
            std::string mantissa = text.substr(0, exponent);
            if (mantissa.find('.') == std::string::npos) {
                mantissa += ".0";
            }
            if (exponent == std::string::npos) {
                return mantissa;
            }
            return mantissa + "E" + text.substr(exponent + 1);
        }
        case ExpressionKind::Character: {
            long long c = e.m_integer;
            if (c >= 32 && c < 127) {
                return std::string("'") + static_cast<char>(c) + "'";
            }
            return "Character'Val (" + std::to_string(c) + ")";
        }
        case ExpressionKind::Boolean:
            return e.m_integer ? "True" : "False";
        case ExpressionKind::String:
            return string_literal(e.m_text);
        case ExpressionKind::Name:
            return e.m_symbol ? e.m_symbol->m_ada : e.m_text;
        case ExpressionKind::Index:
            return e.m_symbol->m_ada + " (" + expression(*e.m_operands[0]) + ")";
        case ExpressionKind::Unary:
            if (e.m_text == "!") {
                return "(not " + expression(*e.m_operands[0]) + ")";
            }
            return "(- " + expression(*e.m_operands[0]) + ")";
        case ExpressionKind::Binary: {
            const Expression &left = *e.m_operands[0];
            const Expression &right = *e.m_operands[1];
            const std::string &op = e.m_text;
            bool real = left.m_type == BaseType::Real || right.m_type == BaseType::Real;
            std::string a = real ? converted(left, BaseType::Real) : expression(left);
            std::string b = real ? converted(right, BaseType::Real) : expression(right);
            std::string ada = op;
            if (op == "%") {
                ada = "rem";
            } else if (op == "==") {
                ada = "=";
            } else if (op == "!=") {
                ada = "/=";
            } else if (op == "&&") {
                ada = "and then";
            } else if (op == "||") {
                ada = "or else";
            }
            return "(" + a + " " + ada + " " + b + ")";
        }
        case ExpressionKind::Invoke:
            return "Invoke_" + e.m_op->m_ada + "_Value (Self" + arguments(e) + ")";
        case ExpressionKind::Pending:
            return "Pending (Op_" + e.m_op->m_ada + ")";
        case ExpressionKind::Random:
            return "Random (" + expression(*e.m_operands[0]) + ")";
    }
    return "0";
}

// Convert an int to a real when a real is wanted.
std::string AdaWriter::converted(const Expression &e, BaseType type)
{
    if (type == BaseType::Real && e.m_type == BaseType::Int) {
        return "Long_Float (" + expression(e) + ")";
    }
    return expression(e);
}

// Spell the arguments, each val one converted to its formal's type.
std::string AdaWriter::arguments(const Expression &invocation)
{
    std::string text;
    const OpInfo &op = *invocation.m_op;
    for (size_t i = 0; i < invocation.m_operands.size() && i < op.m_formals.size(); i++) {
        text += ", " + converted(*invocation.m_operands[i], op.m_formals[i].m_type);
    }
    return text;
}

// Spell an Ada type.
std::string AdaWriter::ada_type(BaseType type)
{
    switch (type) {
        case BaseType::Bool:
            return "Boolean";
        case BaseType::Char:
            return "Character";
        case BaseType::Real:
            return "Long_Float";
        default:
            return "Integer";
    }
}

// Spell a type's default value.
std::string AdaWriter::default_value(BaseType type)
{
    switch (type) {
        case BaseType::Bool:
            return "False";
        case BaseType::Char:
            return "Character'Val (0)";
        case BaseType::Real:
            return "0.0";
        default:
            return "0";
    }
}

// Spell the conversion from a runtime Value.
std::string AdaWriter::from_value(BaseType type)
{
    switch (type) {
        case BaseType::Bool:
            return "To_Bool";
        case BaseType::Char:
            return "To_Char";
        case BaseType::Real:
            return "To_Real";
        default:
            return "To_Int";
    }
}

// Spell an Ada string, printable characters quoted and the others as Character'Val.
std::string AdaWriter::string_literal(std::string_view text)
{
    std::string result;
    std::string quoted;
    auto flush = [&]() {
        if (!quoted.empty()) {
            result += std::string(result.empty() ? "" : " & ") + "\"" + quoted + "\"";
            quoted.clear();
        }
    };
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u >= 32 && u < 127) {
            quoted += c == '"' ? std::string("\"\"") : std::string(1, c);
            continue;
        }
        flush();
        result += std::string(result.empty() ? "" : " & ") + "Character'Val (" +
                  std::to_string(u) + ")";
    }
    flush();
    if (result.empty()) {
        return "\"\"";
    }
    if (result.rfind("Character'Val", 0) == 0) {
        result = "\"\" & " + result;
    }
    return result;
}

// Spell the image of a value, or a string as it is.
std::string AdaWriter::image(const Expression &e)
{
    if (e.m_type == BaseType::String) {
        return string_literal(e.m_text);
    }
    return "Img (" + expression(e) + ")";
}
