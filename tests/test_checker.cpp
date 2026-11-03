// Tests of the checker.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

// A checked program with the tree it points into, and the messages.
struct Checked
{
    // The syntax tree.
    std::unique_ptr<Ast> m_ast;

    // The program, or null after an error.
    std::unique_ptr<Program> m_program;

    // Errors and warnings.
    std::string m_messages;
};

// Parse and check a source with -D constants.
static Checked check(const std::string &source, const std::vector<Define> &defines = {})
{
    Checked result;
    std::ostringstream stream;
    Diagnostics diagnostics("test.mpd", source, stream);
    std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    result.m_ast = Parser(tokens, diagnostics).parse_program();
    if (result.m_ast) {
        result.m_program = Checker(diagnostics).check(*result.m_ast, defines);
    }
    result.m_messages = stream.str();
    return result;
}

// The messages of a source that must not check.
static std::string errors_of(const std::string &source)
{
    Checked result = check(source);
    CHECK_FALSE(result.m_program);
    return result.m_messages;
}

TEST_CASE("Ops are numbered in order, and procs and in statements serve them", "[checker]")
{
    Checked result = check(R"(
op top(int);
module M
    op a(int x);
    op b() returns int;
body
    proc b() returns r { r = 1; }
    process S { in a(x) -> skip; ni }
end M;
process C { call M.a(M.b()); in top(v) -> skip; ni }
)");
    INFO(result.m_messages);
    REQUIRE(result.m_program);
    const auto &ops = result.m_program->m_ops;
    REQUIRE(ops.size() == 3);
    CHECK(ops[0].full_name() == "top");
    CHECK(ops[1].full_name() == "M.a");
    CHECK(ops[1].m_by_in);
    CHECK(ops[2].m_proc);
    CHECK(ops[2].m_returns == BaseType::Int);
}

TEST_CASE("NAME(arguments) is a call of an op, P or V on a semaphore, nap, or a hook",
          "[checker]")
{
    Checked result = check(R"(
op go();
sem s = 1;
process C { go(); P(s); signal(s); nap(5); work(1, 'x'); }
process S { receive go(); }
)");
    INFO(result.m_messages);
    REQUIRE(result.m_program);
    const auto &body = result.m_ast->m_modules[0].m_processes[0].m_body;
    CHECK(body[0]->m_kind == StatementKind::Call);
    CHECK(body[1]->m_kind == StatementKind::P);
    CHECK(body[2]->m_kind == StatementKind::V);
    CHECK(body[3]->m_kind == StatementKind::Nap);
    CHECK(body[4]->m_kind == StatementKind::Hook);
    CHECK(body[4]->m_arguments.size() == 2);
}

TEST_CASE("-D overrides a constant, and adds one the program lacks", "[checker]")
{
    Checked result = check("const int N = 5;", {{"N", "7"}, {"M", "-2"}});
    REQUIRE(result.m_program);
    const auto &constants = result.m_ast->m_constants;
    REQUIRE(constants.size() == 2);
    CHECK(constants[0].m_value->m_integer == 7);
    CHECK(constants[1].m_name == "M");
    Checked bad = check("const int N = 5;", {{"N", "x + 1"}});
    CHECK_FALSE(bad.m_program);
    CHECK(contains(bad.m_messages, "ERR_SEMA_DEFINE"));
}

TEST_CASE("Ops: unknown, outside their module, with the wrong arguments", "[checker]")
{
    CHECK(contains(errors_of("process C { call nowhere(); }"), "ERR_SEMA_UNKNOWN_OP"));
    std::string messages = errors_of(R"(
module M op a(); body process S { in a() -> skip; ni } end M;
process C { call a(); }
)");
    CHECK(contains(messages, "ERR_SEMA_UNKNOWN_OP"));
    CHECK(contains(messages, "write M.a"));
    CHECK(contains(errors_of("process C { call N.a(); }"), "ERR_SEMA_UNKNOWN_MODULE"));
    CHECK(contains(errors_of("op a(int); process C { call a(1, 2); }"), "ERR_SEMA_ARGUMENT_COUNT"));
    CHECK(contains(errors_of("op a(int); process C { call a(true); }"), "ERR_SEMA_TYPE"));
    CHECK(contains(errors_of("op a(var int); process C { call a(3); }"), "ERR_SEMA_NOT_VARIABLE"));
}

TEST_CASE("Procs: no op, two for one op, the wrong formals or result", "[checker]")
{
    CHECK(contains(errors_of("proc a() { skip; }"), "ERR_SEMA_PROC_NO_OP"));
    CHECK(contains(errors_of("op a(); proc a() { skip; } proc a() { skip; }"),
                   "ERR_SEMA_TWO_PROCS"));
    CHECK(contains(errors_of("op a(int); proc a() { skip; }"), "ERR_SEMA_FORMAL_COUNT"));
    CHECK(contains(errors_of("op a() returns int; proc a() { skip; }"), "ERR_SEMA_RETURNS"));
}

TEST_CASE("Who serves what: a proc and in at once, send or receive with results", "[checker]")
{
    CHECK(contains(errors_of("op a(); proc a() { skip; } process S { in a() -> skip; ni }"),
                   "ERR_SEMA_PROC_AND_IN"));
    CHECK(contains(errors_of("op a() returns int; proc a() returns r { r = 1; } "
                             "process C { send a(); }"),
                   "ERR_SEMA_SEND_RESULTS"));
    CHECK(contains(errors_of("op a(res int); process C { int x; receive a(x); }"),
                   "ERR_SEMA_RECEIVE_RESULTS"));
}

TEST_CASE("Guards, priorities and module variables can't invoke ops", "[checker]")
{
    std::string guard = errors_of(R"(
op a(int); op b() returns int;
proc b() returns r { r = 1; }
process S { in a(x) and b() > x -> skip; ni }
)");
    CHECK(contains(guard, "ERR_SEMA_INVOKE_IN_GUARD"));
    std::string module = errors_of(R"(
module M op b() returns int; body
    proc b() returns r { r = 1; }
    int x = b();
end M;
)");
    CHECK(contains(module, "ERR_SEMA_INVOKE_STATIC"));
}

TEST_CASE("Types: assignments, operators, arrays and semaphores", "[checker]")
{
    CHECK(contains(errors_of("process C { bool b = 3; }"), "ERR_SEMA_TYPE"));
    CHECK(contains(errors_of("process C { real r = 2.5 % 2; }"), "ERR_SEMA_TYPE"));
    CHECK(contains(errors_of("process C { int a[3]; a = 1; }"), "ERR_SEMA_INDEX"));
    CHECK(contains(errors_of("sem s; process C { int x = s; }"), "ERR_SEMA_SEM_VALUE"));
    CHECK(contains(errors_of("const int N = 1; process C { N = 2; }"), "ERR_SEMA_NOT_VARIABLE"));
    CHECK(contains(errors_of("process C { int x; int x; }"), "ERR_SEMA_REDECLARED"));
    CHECK(contains(errors_of("process C { elem x; }"), "ERR_SEMA_UNKNOWN_TYPE"));
    Checked real = check("process C { real r = 1; r += 2; bool b = 1 < 2.5; }");
    CHECK(real.m_program);
}

TEST_CASE("An op nothing serves is a warning", "[checker]")
{
    Checked result = check("op lonely(); process C { send lonely(); }");
    CHECK(result.m_program);
    CHECK(contains(result.m_messages, "WARN_OP_NOT_SERVED"));
}
