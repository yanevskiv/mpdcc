// The command-line options of mpdcc.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "options.h"

// Parse the arguments; an option's value may be joined (-oFILE) or split (-o FILE).
bool Options::parse(std::span<const char *const> args, std::string &error)
{
    for (size_t i = 0; i < args.size(); i++) {
        std::string_view arg = args[i];
        if (arg == "-h" || arg == "--help") {
            m_wants_help = true;
        } else if (arg == "--version") {
            m_wants_version = true;
        } else if (arg == "-fdump-tokens") {
            m_dumps_tokens = true;
        } else if (arg == "-fno-runtime") {
            m_writes_runtime = false;
        } else if (arg == "-w") {
            m_warnings_enabled = false;
        } else if (arg == "-Werror") {
            m_warnings_as_errors = true;
        } else if (arg == "-o" || arg == "-D") {
            if (i + 1 == args.size()) {
                error = "option '" + std::string(arg) + "' needs a value";
                return false;
            }
            std::string_view value = args[++i];
            if (arg == "-o") {
                m_output = value;
            } else if (!add_define(value, error)) {
                return false;
            }
        } else if (arg.starts_with("-o")) {
            m_output = arg.substr(2);
        } else if (arg.starts_with("-D")) {
            if (!add_define(arg.substr(2), error)) {
                return false;
            }
        } else if (arg.size() > 1 && arg[0] == '-') {
            error = "unknown option '" + std::string(arg) + "'";
            return false;
        } else if (!m_input.empty()) {
            error = "more than one input: '" + m_input + "' and '" + std::string(arg) + "'";
            return false;
        } else {
            m_input = arg;
        }
    }
    return true;
}

// Print what mpdcc does, its options, and how to build and run what it writes.
void Options::print_help(std::ostream &stream, std::string_view program)
{
    stream
        << "Usage: " << program << " [options] INPUT\n"
        << "\n"
        << "Translate INPUT, a program in MPD, the notation of Andrews' slides on RPC and\n"
        << "rendezvous, into Ada. Each process becomes an Ada task. mpdcc writes the main\n"
        << "procedure (NAME.adb) and the runtime it uses (mpd_runtime.ads and\n"
        << "mpd_runtime.adb) into the current directory; GNAT builds them into a program\n"
        << "that runs the processes.\n"
        << "\n"
        << "Example:\n"
        << "  mpdcc example.mpd              writes example.adb and the runtime\n"
        << "  gnatmake example.adb           builds ./example\n"
        << "  ./example --timeout=2          runs it for two seconds\n"
        << "\n"
        << "Options:\n"
        << "  -o FILE.adb      write the main procedure to FILE.adb (default: INPUT's name)\n"
        << "  -D NAME=VALUE    set the constant NAME, also over const NAME = 5 in INPUT\n"
        << "  -fno-runtime     write only the main procedure, not mpd_runtime.ad[sb]\n"
        << "  -fdump-tokens    print INPUT's tokens with their positions, and stop\n"
        << "  -w               don't report warnings\n"
        << "  -Werror          report warnings as errors\n"
        << "  -h, --help       show this help\n"
        << "  --version        show mpdcc's version\n"
        << "\n"
        << "The translated program takes:\n"
        << "  --trace          print every invocation and who serves it\n"
        << "  --quiet          don't print hooks, calls nothing defines like work()\n"
        << "  --delay=MS       pause each hook for up to MS milliseconds (default 10)\n"
        << "  --seed=N         seed the random choices, to repeat a run\n"
        << "  --timeout=S      stop after S seconds\n"
        << "\n"
        << "Exit status of mpdcc:\n"
        << "  Status 0         INPUT was translated\n"
        << "  Status 1         INPUT has errors\n"
        << "  Status 2         bad options\n"
        << "\n"
        << "Exit status of the translated program:\n"
        << "  Status 0         every process ended or waits in in or receive, or --timeout\n"
        << "  Status 1         a process failed, such as on an index out of range\n"
        << "  Status 2         bad options\n"
        << "  Status 3         deadlock, a process waits in call or P; it lists them all\n";
}

// Return the input.
const std::string &Options::input() const
{
    return m_input;
}

// Return the output.
const std::string &Options::output() const
{
    return m_output;
}

// Return the -D constants.
const std::vector<Define> &Options::defines() const
{
    return m_defines;
}

// Report --help.
bool Options::wants_help() const
{
    return m_wants_help;
}

// Report --version.
bool Options::wants_version() const
{
    return m_wants_version;
}

// Report -fdump-tokens.
bool Options::dumps_tokens() const
{
    return m_dumps_tokens;
}

// Report whether -fno-runtime was left out.
bool Options::writes_runtime() const
{
    return m_writes_runtime;
}

// Report whether -w was left out.
bool Options::warnings_enabled() const
{
    return m_warnings_enabled;
}

// Report -Werror.
bool Options::warnings_as_errors() const
{
    return m_warnings_as_errors;
}

// Split NAME=VALUE, requiring both halves.
bool Options::add_define(std::string_view text, std::string &error)
{
    size_t equals = text.find('=');
    if (equals == std::string_view::npos || equals == 0 || equals + 1 == text.size()) {
        error = "-D takes NAME=VALUE, not '" + std::string(text) + "'";
        return false;
    }
    Define define;
    define.m_name = text.substr(0, equals);
    define.m_value = text.substr(equals + 1);
    m_defines.push_back(define);
    return true;
}
