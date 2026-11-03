// Tests of the parser.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

// Parse a source, keeping the messages.
static std::unique_ptr<Ast> parse(const std::string &source, std::string *messages = nullptr)
{
    std::ostringstream stream;
    Diagnostics diagnostics("test.mpd", source, stream);
    std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
    std::unique_ptr<Ast> ast = Parser(tokens, diagnostics).parse_program();
    if (messages) {
        *messages = stream.str();
    }
    return ast;
}

TEST_CASE("A module's ops, with modes, unnamed formals and returns", "[parser]")
{
    auto ast = parse(R"(
type elem = int;
Module BoundedBuffer
    op deposit(elem);
    op fetch(result elem item);
    op size() returns int;
Body
End BoundedBuffer;
)");
    REQUIRE(ast);
    REQUIRE(ast->m_modules.size() == 2);
    const Module &module = ast->m_modules[1];
    CHECK(module.m_name == "BoundedBuffer");
    REQUIRE(module.m_ops.size() == 3);
    CHECK(module.m_ops[0].m_formals[0].m_type.m_alias == "elem");
    CHECK(module.m_ops[0].m_formals[0].m_name.empty());
    CHECK(module.m_ops[1].m_formals[0].m_mode == "res");
    CHECK(module.m_ops[1].m_formals[0].m_name == "item");
    CHECK(module.m_ops[2].m_has_returns);
    CHECK(module.m_ops[2].m_returns.m_base == BaseType::Int);
}

TEST_CASE("Process NAME(...) is a proc, as on the slides, and process NAME[...] an array",
          "[parser]")
{
    auto ast = parse(R"(
module M
    op deposit(int);
body
    Process deposit(item) { skip; }
    proc other(a, b) returns c { skip; }
    process Worker[i = 1 to 4] { skip; }
end M;
)");
    REQUIRE(ast);
    const Module &module = ast->m_modules[1];
    REQUIRE(module.m_procs.size() == 2);
    CHECK(module.m_procs[0].m_name == "deposit");
    CHECK(module.m_procs[0].m_formals == std::vector<std::string>{"item"});
    CHECK(module.m_procs[1].m_returns == "c");
    REQUIRE(module.m_processes.size() == 1);
    CHECK(module.m_processes[0].m_index == "i");
}

TEST_CASE("An in statement's arms: formals, returns, and, by and their statements", "[parser]")
{
    auto ast = parse(R"(
process S {
    in deposit(item) and count < n -> buf[rear] = item; count++;
    [] fetch() returns item and count > 0 by count -> item = 1;
    [] M.tick() -> skip;
    ni
}
)");
    REQUIRE(ast);
    const Statement &in = *ast->m_modules[0].m_processes[0].m_body[0];
    REQUIRE(in.m_kind == StatementKind::In);
    REQUIRE(in.m_arms.size() == 3);
    CHECK(in.m_arms[0].m_formals == std::vector<std::string>{"item"});
    CHECK(in.m_arms[0].m_guard);
    CHECK(in.m_arms[0].m_body.size() == 2);
    CHECK(in.m_arms[1].m_returns == "item");
    CHECK(in.m_arms[1].m_by);
    CHECK(in.m_arms[2].m_module == "M");
}

TEST_CASE("Statements: declarations, assignments, for with st, call, send and receive",
          "[parser]")
{
    auto ast = parse(R"(
process S {
    int a[3] = 0, b = 2;
    a[1] += b;
    for [j = 0 to 2 st j != 1] write(j);
    call M.go(1);
    send other(2);
    receive got(b);
    work(b);
    THINK;
}
)");
    REQUIRE(ast);
    const auto &body = ast->m_modules[0].m_processes[0].m_body;
    REQUIRE(body.size() == 8);
    CHECK(body[0]->m_declarators.size() == 2);
    CHECK(body[0]->m_declarators[0].m_size);
    CHECK(body[1]->m_operator == "+=");
    CHECK(body[2]->m_condition);
    CHECK(body[3]->m_kind == StatementKind::Call);
    CHECK(body[3]->m_invocation->m_module == "M");
    CHECK(body[4]->m_kind == StatementKind::Send);
    CHECK(body[5]->m_kind == StatementKind::Receive);
    CHECK(body[6]->m_kind == StatementKind::NameCall);
    CHECK(body[7]->m_kind == StatementKind::Hook);
}

TEST_CASE("C's precedence: * before +, comparisons before &&, && before ||", "[parser]")
{
    auto ast = parse("const bool b = 1 + 2 * 3 == 7 && true || false;");
    REQUIRE(ast);
    const Expression &top = *ast->m_constants[0].m_value;
    CHECK(top.m_text == "||");
    CHECK(top.m_operands[0]->m_text == "&&");
    CHECK(top.m_operands[0]->m_operands[0]->m_text == "==");
    CHECK(top.m_operands[0]->m_operands[0]->m_operands[0]->m_operands[1]->m_text == "*");
}

TEST_CASE("Parse errors: no ni, end with another name, a bad statement", "[parser]")
{
    std::string messages;
    CHECK_FALSE(parse("process S { in a() -> skip; }", &messages));
    CHECK(contains(messages, "ERR_PARSE"));
    CHECK_FALSE(parse("module M body end N;", &messages));
    CHECK(contains(messages, "ERR_PARSE_END_NAME"));
    CHECK_FALSE(parse("process S { 3; }", &messages));
    CHECK(contains(messages, "ERR_PARSE_STATEMENT"));
    CHECK_FALSE(parse("module M op a(); body", &messages));
    CHECK(contains(messages, "ERR_PARSE_NO_END"));
}
