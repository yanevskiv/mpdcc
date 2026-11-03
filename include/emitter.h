// Writes a checked program as an Ada main procedure.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_EMITTER_H
#define MPDCC_EMITTER_H

#include "program.h"

#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Hands out Ada names that clash neither with Ada's reserved words nor with each other.
class NameTable
{
public:
    // Start with Ada's reserved words and the runtime's names taken.
    NameTable();

    // Make a fresh name from a base, adding _2, _3, ... on a clash.
    std::string make(std::string_view base);

    // Take a name so make never returns it.
    void reserve(std::string_view name);

    // Check whether a name, in any case, is taken.
    bool taken(std::string_view name) const;

    // Turn any text into a legal Ada identifier.
    static std::string sanitize(std::string_view name);

private:
    // Names taken, in lower case, since Ada ignores case.
    std::set<std::string> m_taken;
};

// Writes a checked program as one Ada main procedure using Mpd_Runtime.
class AdaWriter
{
public:
    // Prepare to write a program as the Ada unit unit, translated from file.
    AdaWriter(Program &program, std::string unit, std::string file);

    // Write the whole main procedure.
    std::string write();

    // Check whether a name can be a GNAT unit and its file name: lower-case Ada.
    static bool valid_unit(std::string_view name);

    // Make a unit name from a file's name.
    static std::string unit_from(std::string_view name);

private:
    // Give Ada names to the constants, variables, formals and ops.
    void name_everything();

    // Give Ada names to the symbols a list of statements declares.
    void name_statements(std::vector<std::unique_ptr<Statement>> &statements);

    // Name one symbol from its source name.
    void name_symbol(Symbol *symbol);

    // Write a line at the current indentation.
    void line(std::string_view text);

    // Write the constants and the op numbers.
    void write_constants();

    // Write the top level's and the modules' variables.
    void write_variables();

    // Write the specs of procs, spawned tasks and processes.
    void write_specs();

    // Write the subprograms that invoke each op by call and by send.
    void write_invokers(const OpInfo &op);

    // Write a proc's body.
    void write_proc(const ProcDecl &proc);

    // Write the body of the task that a send to a proc spawns.
    void write_spawn(const OpInfo &op);

    // Write a process's task body.
    void write_process(const ProcessDecl &process);

    // Write the main procedure's statements, which start the processes.
    void write_main();

    // Write a list of statements, opening declare blocks for declarations.
    void write_statements(const std::vector<std::unique_ptr<Statement>> &statements, size_t from = 0);

    // Write one statement.
    void write_statement(const Statement &statement);

    // Write the declarations of one declaration statement.
    void write_declaration(const Statement &statement, bool shared);

    // Write call or send.
    void write_invocation(const Statement &statement);

    // Write an in statement, or receive as an in statement with one arm.
    void write_in(const Statement &statement);

    // Write the code that looks for the oldest acceptable invocation of one arm.
    void write_scan(const Arm &arm, size_t number, const std::string &invocation,
                    const std::string &chosen);

    // Declare an arm's formals from an invocation's arguments.
    void write_formals(const Arm &arm, const std::string &invocation, bool constant);

    // Spell an expression in Ada.
    std::string expression(const Expression &expression);

    // Spell an expression converted to a type, an int to a real if needed.
    std::string converted(const Expression &expression, BaseType type);

    // Spell the arguments of an invocation of an op, after Self.
    std::string arguments(const Expression &invocation);

    // Spell an Ada type.
    static std::string ada_type(BaseType type);

    // Spell a type's default value.
    static std::string default_value(BaseType type);

    // Spell the conversion of a runtime Value to a type.
    static std::string from_value(BaseType type);

    // Spell an Ada string literal.
    static std::string string_literal(std::string_view text);

    // Spell the image of a value for traces and hooks.
    std::string image(const Expression &expression);

    // Program being written.
    Program &m_program;

    // Name of the Ada unit.
    std::string m_unit;

    // Name of the source file, for the header.
    std::string m_file;

    // Names given so far.
    NameTable m_names;

    // Text written so far.
    std::ostringstream m_out;

    // Current indentation, in levels of three spaces.
    int m_indent = 0;
};

#endif
