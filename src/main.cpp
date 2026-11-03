// mpdcc, a compiler from MPD, the notation of Andrews' slides, to Ada.
// Copyright (C) 2026 Ivan Janevski
// SPDX-License-Identifier: GPL-3.0-or-later

#include "checker.h"
#include "diagnostics.h"
#include "emitter.h"
#include "lexer.h"
#include "options.h"
#include "parser.h"
#include "program.h"
#include "runtime.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Version of mpdcc.
#define MPDCC_VERSION "0.1.0"

// Runs one mpdcc invocation, from the MPD file to the Ada files.
class Driver
{
public:
    // Keep the command line.
    Driver(int argc, const char *const *argv);

    // Run the invocation.
    int run();

private:
    // Translate the input, already read.
    int translate(const std::string &source);

    // Write the Ada main procedure and, unless -fno-runtime, the runtime next to it.
    int emit(Program &program);

    // Read a whole file.
    static bool read_file(const std::string &path, std::string &text);

    // Write a file, leaving it untouched when it already holds the text.
    static bool write_file(const std::filesystem::path &path, std::string_view text);

    // Spell a unit's file name as an Ada name.
    static std::string ada_name(std::string_view unit);

    // Name the program was run as.
    std::string m_program;

    // Arguments after the program's name.
    std::vector<const char *> m_args;

    // Parsed options.
    Options m_options;
};

// Keep the program's name and its arguments.
Driver::Driver(int argc, const char *const *argv)
    : m_program(argc > 0 ? argv[0] : "mpdcc")
    , m_args(argv + (argc > 0 ? 1 : 0), argv + argc)
{}

// Parse the options, then read and translate the input.
int Driver::run()
{
    std::string error;
    if (!m_options.parse(m_args, error)) {
        std::cerr << "mpdcc: " << error << '\n';
        return 2;
    }
    if (m_options.wants_help()) {
        Options::print_help(std::cout, m_program);
        return 0;
    }
    if (m_options.wants_version()) {
        std::cout << "mpdcc " << MPDCC_VERSION << '\n';
        return 0;
    }
    if (m_options.input().empty()) {
        std::cerr << "mpdcc: no input; '" << m_program << " --help' shows how to use mpdcc\n";
        return 2;
    }
    std::string source;
    if (!read_file(m_options.input(), source)) {
        std::cerr << "mpdcc: can't read '" << m_options.input() << "'\n";
        return 1;
    }
    return translate(source);
}

// Run the stages, stopping after the first that reports errors.
int Driver::translate(const std::string &source)
{
    Diagnostics diagnostics(m_options.input(), source, std::cerr);
    diagnostics.set_warnings(m_options.warnings_enabled(), m_options.warnings_as_errors());
    Lexer tokenizer(source, diagnostics);
    std::vector<Token> tokens = tokenizer.tokenize();
    if (m_options.dumps_tokens()) {
        Lexer::dump(tokens, std::cout);
        return diagnostics.error_count() ? 1 : 0;
    }
    if (diagnostics.error_count()) {
        return 1;
    }
    Parser parser(tokens, diagnostics);
    std::unique_ptr<Ast> ast = parser.parse_program();
    if (!ast) {
        return 1;
    }
    Checker checker(diagnostics);
    std::unique_ptr<Program> program = checker.check(*ast, m_options.defines());
    if (!program) {
        return 1;
    }
    return emit(*program);
}

// Name the output after the input unless -o names it, then write the files.
int Driver::emit(Program &program)
{
    std::filesystem::path input(m_options.input());
    std::filesystem::path output(m_options.output());
    if (output.empty()) {
        output = AdaWriter::unit_from(input.stem().string()) + ".adb";
    }
    std::string unit = output.stem().string();
    if (output.extension() != ".adb" || !AdaWriter::valid_unit(unit)) {
        std::cerr << "mpdcc: the output must be a lower-case Ada unit name ending in .adb, "
                     "such as example.adb, not '"
                  << output.string() << "'\n";
        return 2;
    }
    AdaWriter writer(program, ada_name(unit), input.filename().string());
    if (!write_file(output, writer.write())) {
        std::cerr << "mpdcc: can't write '" << output.string() << "'\n";
        return 1;
    }
    if (!m_options.writes_runtime()) {
        return 0;
    }
    std::filesystem::path directory = output.parent_path();
    if (!write_file(directory / "mpd_runtime.ads", runtime_spec()) ||
        !write_file(directory / "mpd_runtime.adb", runtime_body())) {
        std::cerr << "mpdcc: can't write the runtime next to '" << output.string() << "'\n";
        return 1;
    }
    return 0;
}

// Read a whole file.
bool Driver::read_file(const std::string &path, std::string &text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

// Write a file unless it already holds the text, so gnatmake keeps its objects.
bool Driver::write_file(const std::filesystem::path &path, std::string_view text)
{
    std::string old;
    if (read_file(path.string(), old) && old == text) {
        return true;
    }
    std::ofstream file(path, std::ios::binary);
    file << text;
    return static_cast<bool>(file);
}

// Capitalize the first letter and each letter after an underscore.
std::string Driver::ada_name(std::string_view unit)
{
    std::string name(unit);
    for (size_t i = 0; i < name.size(); i++) {
        if (i == 0 || name[i - 1] == '_') {
            name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
        }
    }
    return name;
}

// Run the mpdcc executable.
int main(int argc, char **argv)
{
    Driver app(argc, argv);
    return app.run();
}
