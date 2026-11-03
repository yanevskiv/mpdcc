// The command-line options of mpdcc.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPDCC_OPTIONS_H
#define MPDCC_OPTIONS_H

#include "program.h"

#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Parses the options of one mpdcc invocation.
class Options
{
public:
    // Parse the arguments after the program's name.
    bool parse(std::span<const char *const> args, std::string &error);

    // Print the options mpdcc takes.
    static void print_help(std::ostream &stream, std::string_view program);

    // Return the MPD file to translate.
    const std::string &input() const;

    // Return the Ada file to write.
    const std::string &output() const;

    // Return the constants given with -D.
    const std::vector<Define> &defines() const;

    // Report whether help was requested.
    bool wants_help() const;

    // Report whether the version was requested.
    bool wants_version() const;

    // Report whether the tokens should be printed instead of translated.
    bool dumps_tokens() const;

    // Report whether the Ada runtime should be written next to the output.
    bool writes_runtime() const;

    // Report whether warnings are reported.
    bool warnings_enabled() const;

    // Report whether warnings are errors.
    bool warnings_as_errors() const;

private:
    // Record one -D value.
    bool add_define(std::string_view text, std::string &error);

    // MPD file to translate.
    std::string m_input;

    // Ada file to write, empty for the default.
    std::string m_output;

    // Constants given with -D.
    std::vector<Define> m_defines;

    // Whether help was requested.
    bool m_wants_help = false;

    // Whether the version was requested.
    bool m_wants_version = false;

    // Whether the tokens are printed.
    bool m_dumps_tokens = false;

    // Whether the Ada runtime is written.
    bool m_writes_runtime = true;

    // Whether warnings are reported.
    bool m_warnings_enabled = true;

    // Whether warnings are errors.
    bool m_warnings_as_errors = false;
};

#endif
