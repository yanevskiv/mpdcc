// Tests of the Ada writer.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

TEST_CASE("Ada names avoid reserved words, the runtime's names and clashes of case",
          "[emitter]")
{
    NameTable names;
    CHECK(names.make("in") == "in_2");
    CHECK(names.make("Self") == "Self_2");
    CHECK(names.make("buffer") == "buffer");
    CHECK(names.make("Buffer") == "Buffer_2");
    CHECK(names.make("a__b_") == "a_b");
}

TEST_CASE("Unit names come from file names, in lower case", "[emitter]")
{
    CHECK(AdaWriter::unit_from("Bounded-Buffer") == "bounded_buffer");
    CHECK(AdaWriter::unit_from("2pc") == "mpd_2pc");
    CHECK(AdaWriter::unit_from("select") == "select_main");
    CHECK(AdaWriter::valid_unit("time_server"));
    CHECK_FALSE(AdaWriter::valid_unit("Time"));
    CHECK_FALSE(AdaWriter::valid_unit("a__b"));
}

TEST_CASE("Literals and operators in Ada: reals, characters, rem, and then", "[emitter]")
{
    Translation result = translate(R"(
process C {
    real r = 2.5 + 1e20;
    char c = '\n';
    bool b = 7 % 3 == 1 && c != 'x';
}
)");
    INFO(result.m_messages);
    REQUIRE(result.m_ok);
    CHECK(contains(result.m_ada, "2.5"));
    CHECK(contains(result.m_ada, "1.0E+20"));
    CHECK(contains(result.m_ada, "Character'Val (10)"));
    CHECK(contains(result.m_ada, "(7 rem 3)"));
    CHECK(contains(result.m_ada, "and then"));
}
