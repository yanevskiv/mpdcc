// Tests that build and run every program in examples/.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

#include <map>
#include <regex>

// Check, from --trace output, that no two hooks starting with hook run at once.
static std::string exclusion_problem(const std::string &output, const std::string &hook)
{
    int inside = 0;
    int runs = 0;
    for (const std::string &line : lines(output)) {
        if (!contains(line, ": " + hook)) {
            continue;
        }
        if (contains(line, " end")) {
            inside--;
        } else {
            runs++;
            if (++inside > 1) {
                return "overlap: " + line;
            }
        }
    }
    return runs ? "" : "no " + hook + " ran";
}

// The values written as "fetched N", in order.
static std::vector<int> fetched(const std::string &output)
{
    std::vector<int> values;
    for (const std::string &line : lines(output)) {
        if (line.rfind("fetched ", 0) == 0) {
            values.push_back(std::stoi(line.substr(8)));
        }
    }
    return values;
}

// The numbers 1 to n.
static std::vector<int> one_to(int n)
{
    std::vector<int> values;
    for (int i = 1; i <= n; i++) {
        values.push_back(i);
    }
    return values;
}

TEST_CASE("examples/bounded_buffer_rpc.mpd", "[examples]")
{
    Run result = run("bounded_buffer_rpc", example("bounded_buffer_rpc"));
    CHECK(result.m_status == 0);
    CHECK(fetched(result.m_output) == one_to(20));
}

TEST_CASE("examples/bounded_buffer.mpd", "[examples]")
{
    Run result = run("bounded_buffer", example("bounded_buffer"));
    CHECK(result.m_status == 0);
    CHECK(fetched(result.m_output) == one_to(20));
}

TEST_CASE("examples/time_server.mpd", "[examples]")
{
    Run result = run("time_server", example("time_server"));
    CHECK(result.m_status == 0);
    std::regex woke(R"(^sleeper (\d) woke at (\d+)$)");
    int count = 0;
    for (const std::string &line : lines(result.m_output)) {
        std::smatch match;
        REQUIRE(std::regex_match(line, match, woke));
        CHECK(std::stoi(match[2]) >= 5 * std::stoi(match[1]));
        count++;
    }
    CHECK(count == 3);
}

TEST_CASE("examples/philosophers.mpd", "[examples]")
{
    Run result = run("philosophers", example("philosophers"), "--trace");
    CHECK(result.m_status == 0);
    std::regex eat(R"(^Phil\((\d)\): eat\(\d\)( end)?$)");
    bool eating[5] = {};
    int meals[5] = {};
    for (const std::string &line : lines(result.m_output)) {
        std::smatch match;
        if (!std::regex_match(line, match, eat)) {
            continue;
        }
        int p = std::stoi(match[1]);
        if (match[2].matched) {
            eating[p] = false;
            continue;
        }
        INFO(line);
        CHECK_FALSE(eating[(p + 4) % 5]);
        CHECK_FALSE(eating[(p + 1) % 5]);
        eating[p] = true;
        meals[p]++;
    }
    for (int p = 0; p < 5; p++) {
        CHECK(meals[p] == 3);
    }
}

TEST_CASE("examples/sjn.mpd", "[examples]")
{
    Run result = run("sjn", example("sjn"), "--trace");
    CHECK(result.m_status == 0);
    CHECK(exclusion_problem(result.m_output, "work(") == "");
}

TEST_CASE("examples/readers_writers.mpd", "[examples]")
{
    Run result = run("readers_writers", example("readers_writers"), "--trace");
    CHECK(result.m_status == 0);
    int readers = 0;
    int writers = 0;
    int reads = 0;
    for (const std::string &line : lines(result.m_output)) {
        bool end = contains(line, " end");
        if (contains(line, ": reading(")) {
            readers += end ? -1 : 1;
            reads += end ? 0 : 1;
            CHECK(writers == 0);
        } else if (contains(line, ": writing(")) {
            writers += end ? -1 : 1;
            CHECK(writers <= 1);
            CHECK(readers == 0);
        }
    }
    CHECK(reads == 12);
}

TEST_CASE("examples/sleeping_barber.mpd", "[examples]")
{
    Run result = run("sleeping_barber", example("sleeping_barber"), "--delay=5");
    CHECK(result.m_status == 0);
    std::map<std::string, int> cut;
    int visits = 0;
    for (const std::string &line : lines(result.m_output)) {
        std::smatch match;
        if (std::regex_match(line, match, std::regex(R"(^Barber: cut_hair\((\d)\)$)"))) {
            cut[match[1]]++;
        } else if (std::regex_match(line, match, std::regex(R"(^Customer\((\d)\): pay\(\d\)$)"))) {
            INFO(line);
            CHECK(cut[match[1]]-- > 0);
            visits++;
        } else if (contains(line, ": leave(")) {
            visits++;
        }
    }
    CHECK(visits == 15);
}

TEST_CASE("examples/smokers.mpd", "[examples]")
{
    Run result = run("smokers", example("smokers"), "--delay=0");
    CHECK(result.m_status == 0);
    CHECK(lines(result.m_output).size() == 6);
}

TEST_CASE("examples/merge.mpd", "[examples]")
{
    Run result = run("merge", example("merge"));
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"2", "3", "4", "6", "6", "8", "9", "10", "12", "15"};
    CHECK(lines(result.m_output) == expected);
}

TEST_CASE("examples/fork.mpd", "[examples]")
{
    Run result = run("fork", example("fork"));
    CHECK(result.m_status == 0);
    std::vector<std::string> expected = {"all jobs finished",      "job 1 squared is 1",
                                         "job 2 squared is 4",     "job 3 squared is 9",
                                         "job 4 squared is 16"};
    CHECK(sorted_lines(result.m_output) == expected);
    CHECK(lines(result.m_output).back() == "all jobs finished");
}
