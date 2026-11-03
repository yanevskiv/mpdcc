// Tests that translate, build and run small MPD programs.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

TEST_CASE("write prints values, and a hook prints its process and its call", "[programs]")
{
    const char *source = R"mpd(
process X {
    write("sum", 2 + 3, true, 'c', 2.5);
    work(1, "two");
    THINK;
}
)mpd";
    Run result = run("hooks", source, "--delay=0");
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {
        "sum 5 true 'c' 2.5",
        "X: work(1, two)",
        "X: THINK",
    };
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("Arithmetic as in C: / and % truncate, and ints become reals", "[programs]")
{
    const char *source = R"mpd(
process X {
    int a = -7;
    real r = 1;
    r += 0.5;
    write(a / 2, a % 2, 7 / 2, 7 % 3, r * 2, -(a - 3));
    char c = 'a';
    write(c < 'b', !(1 == 1) || 2 != 3);
}
)mpd";
    Run result = run("arithmetic", source);
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"-3 -1 3 1 3.0 10", "true true"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("Control: if and else, while, for with st, arrays and blocks", "[programs]")
{
    const char *source = R"mpd(
const int N = 5;
process X {
    int a[N] = 1;
    for [i = 1 to N - 1] a[i] = a[i - 1] * 2;
    for [i = 0 to N - 1 st i % 2 == 0] write(a[i]);
    int k = 0;
    while (k < 3) { k++; }
    if (k == 3) write("three"); else write("not three");
}
)mpd";
    Run result = run("control", source);
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"1", "4", "16", "three"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("A call of a proc runs it and returns its result and its var and res formals",
          "[programs]")
{
    const char *source = R"mpd(
module Math
    op divide(int a, int b, res int q, var int r) returns bool;
    op square(int x) returns int;
body
    proc divide(a, b, q, r) returns ok {
        ok = b != 0;
        if (ok) { q = a / b; r = a % b + r; }
    }
    proc square(x) returns y { y = x * x; }
end Math;

process X {
    int q, r = 100;
    bool ok;
    call Math.divide(17, 5, q, r);
    write(q, r, Math.square(Math.square(3)));
}
)mpd";
    Run result = run("rpc", source);
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"3 102 81"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("Rendezvous: a guard may use the call's arguments", "[programs]")
{
    const char *source = R"mpd(
op put(int x);
op get(int x, res int y);

process Server {
    int n = 0;
    while (true) {
        in put(x) -> n += x;
        [] get(x, y) and n >= x -> y = n; n -= x;
        ni
    }
}

process Taker {
    int y;
    call get(10, y);
    write("got", y);
}

process Giver {
    for [i = 1 to 4] { call put(i); write("put", i); }
}
)mpd";
    Run result = run("guard_arguments", source);
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"got 10", "put 1", "put 2", "put 3", "put 4"};
    CHECK(sorted_lines(result.m_output) == expected);
}

TEST_CASE("by serves the smallest priority first; without by, the oldest call", "[programs]")
{
    const char *source = R"mpd(
op job(int length);
op other(int n);
op go();

process Server {
    receive go();
    for [k = 1 to 3] { in job(length) by length -> write("job", length); ni }
    for [k = 1 to 3] { in other(n) -> write("other", n); ni }
}

process Client {
    send job(5); send job(2); send job(9);
    send other(5); send other(2); send other(9);
    send go();
}
)mpd";
    Run result = run("priority", source);
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"job 2",   "job 5",   "job 9",
                                         "other 5", "other 2", "other 9"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("Across arms, in serves the oldest acceptable invocation", "[programs]")
{
    const char *source = R"mpd(
op a(int n);
op b(int n);
op go();

process Server {
    receive go();
    for [k = 1 to 4] {
        in a(n) -> write("a", n);
        [] b(n) -> write("b", n);
        ni
    }
}

process Client {
    send b(1); send a(2); send b(3); send a(4);
    send go();
}
)mpd";
    Run result = run("oldest", source);
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"b 1", "a 2", "b 3", "a 4"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("?op counts the invocations waiting", "[programs]")
{
    const char *source = R"mpd(
op a(); op go();
process Server {
    receive go();
    write(?a);
    in a() -> write(?a); ni
}
process Client { send a(); send a(); send a(); send go(); }
)mpd";
    Run result = run("pending", source);
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"3", "2"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("send doesn't wait, and receive takes the messages in order", "[programs]")
{
    const char *source = R"mpd(
op box(int n, char c);
process Sender {
    for [i = 1 to 3] send box(i, 'x');
    write("sent");
}
process Receiver {
    nap(50);
    int n; char c;
    for [i = 1 to 3] { receive box(n, c); write(n, c); }
}
)mpd";
    Run result = run("messages", source);
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"sent", "1 'x'", "2 'x'", "3 'x'"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("send to a proc starts a new process for each invocation", "[programs]")
{
    const char *source = R"mpd(
op job(int n);
sem finished = 0;
proc job(n) { work(n); V(finished); }
process Boss {
    send job(1); send job(2);
    P(finished); P(finished);
}
)mpd";
    Run result = run("spawn", source, "--delay=0");
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"job#1: work(1)", "job#2: work(2)"};
    CHECK(sorted_lines(result.m_output) == expected);
}

TEST_CASE("Semaphores: P and V, wait and signal, and arrays of them", "[programs]")
{
    const char *source = R"mpd(
const int N = 4;
sem mutex = 1;
sem done[N] = 0;
process Worker[i = 0 to N - 1] {
    for [k = 1 to 3] { P(mutex); work(i); V(mutex); }
    signal(done[i]);
}
process Boss {
    for [i = 0 to N - 1] wait(done[i]);
    write("all done");
}
)mpd";
    Run result = run("semaphores", source, "--trace --delay=2");
    CHECK(result.m_status == 0);
    int inside = 0;
    int works = 0;
    for (const std::string &line : lines(result.m_output)) {
        if (contains(line, ": work(") && contains(line, " end")) {
            inside--;
        } else if (contains(line, ": work(")) {
            works++;
            CHECK(++inside == 1);
        }
    }
    CHECK(works == 12);
    CHECK(contains(result.m_output, "all done"));
}

TEST_CASE("--trace shows calls, sends, and who serves them", "[programs]")
{
    const char *source = R"mpd(
module M op a(int x); body process S { in a(x) -> skip; ni } end M;
process C { call M.a(4); }
)mpd";
    Run result = run("trace", source, "--trace");
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"C calls M.a(4)", "S serves M.a(4) from C"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("A deadlock in P or call stops with 3 and says who waits for what", "[programs]")
{
    const char *source = R"mpd(
sem a = 0, b = 0;
op never();
process X { P(a); }
process Y { call never(); }
)mpd";
    Run result = run("deadlock", source);
    CHECK(result.m_status == 3);
    CHECK(contains(result.m_error, "deadlock"));
    CHECK(contains(result.m_error, "X: P(a)"));
    CHECK(contains(result.m_error, "Y: call never"));
}

TEST_CASE("When the only processes left wait in in or receive, the program ends with 0",
          "[programs]")
{
    const char *source = R"mpd(
op a();
process Server { while (true) { in a() -> skip; ni } }
process Client { call a(); write("done"); }
)mpd";
    Run result = run("quiet_end", source);
    CHECK(result.m_status == 0);
    CHECK(result.m_output == "done\n");
}

TEST_CASE("--timeout stops a program that runs forever, with 0", "[programs]")
{
    const char *source = R"mpd(
process X { while (true) work(); }
)mpd";
    Run result = run("timeout", source, "--quiet --timeout=0.5");
    CHECK(result.m_status == 0);
    CHECK(contains(result.m_error, "--timeout"));
}

TEST_CASE("A process that fails, such as on an index out of range, stops with 1",
          "[programs]")
{
    const char *source = R"mpd(
process X { int a[3]; int i = 3; a[i] = 1; }
)mpd";
    Run result = run("failure", source);
    CHECK(result.m_status == 1);
    CHECK(contains(result.m_error, "mpd: X: CONSTRAINT_ERROR"));
}

TEST_CASE("Arrays of processes know their index, and -D sets their number", "[programs]")
{
    const char *source = R"mpd(
const int N = 2;
process P[i = 1 to N] { write("P", i); }
)mpd";
    Run result = run("defines", source, "", {{"N", "4"}});
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"P 1", "P 2", "P 3", "P 4"};
    CHECK(sorted_lines(result.m_output) == expected);
}
