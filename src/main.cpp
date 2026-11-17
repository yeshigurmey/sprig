// The `sprig` command line tool.
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "driver.h"
#include "lexer.h"
#include "vm.h"

namespace {

const char* const kUsage =
    "usage: sprig [options] <file.sprig>\n"
    "\n"
    "options:\n"
    "  -O0              turn the optimizer off\n"
    "  -O1              turn the optimizer on (default)\n"
    "  --dump-tokens    print the tokens the lexer produced, then exit\n"
    "  --dump-ast       print the syntax tree (after optimizing, unless -O0), then exit\n"
    "  --dump-bytecode  print the compiled bytecode, then exit\n"
    "  --stats          after running, print optimizer and VM counters to stderr\n"
    "  --max-instructions N\n"
    "                   stop with an error after N instructions (guards against infinite loops)\n"
    "  -h, --help       show this message\n";

// Exit codes, so scripts can tell the failure kinds apart.
enum ExitCode { kOk = 0, kCompileError = 1, kRuntimeError = 2, kUsageError = 64 };

}  // namespace

int main(int argc, char** argv) {
    sprig::CompileOptions options;
    bool dumpTokens = false;
    bool dumpAst = false;
    bool dumpBytecode = false;
    bool showStats = false;
    std::uint64_t maxInstructions = 0;  // 0 means no limit
    std::string path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-O0") {
            options.optimize = false;
        } else if (arg == "-O1") {
            options.optimize = true;
        } else if (arg == "--dump-tokens") {
            dumpTokens = true;
        } else if (arg == "--dump-ast") {
            dumpAst = true;
        } else if (arg == "--dump-bytecode") {
            dumpBytecode = true;
        } else if (arg == "--stats") {
            showStats = true;
        } else if (arg == "--max-instructions") {
            if (i + 1 >= argc) {
                std::cerr << "sprig: --max-instructions needs a number\n";
                return kUsageError;
            }
            try {
                maxInstructions = std::stoull(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "sprig: --max-instructions needs a number, got '" << argv[i] << "'\n";
                return kUsageError;
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            return kOk;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "sprig: unknown option '" << arg << "'\n\n" << kUsage;
            return kUsageError;
        } else if (!path.empty()) {
            std::cerr << "sprig: only one input file is supported\n\n" << kUsage;
            return kUsageError;
        } else {
            path = arg;
        }
    }

    if (path.empty()) {
        std::cerr << kUsage;
        return kUsageError;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "sprig: cannot open '" << path << "'\n";
        return kUsageError;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();

    if (dumpTokens) {
        try {
            for (const sprig::Token& tok : sprig::Lexer(source).tokenize()) {
                std::cout << tok.loc.line << ':' << tok.loc.col << '\t' << sprig::tokenKindName(tok.kind);
                if (tok.kind == sprig::TokenKind::Int || tok.kind == sprig::TokenKind::Ident) {
                    std::cout << '\t' << tok.text;
                }
                std::cout << '\n';
            }
        } catch (const sprig::SyntaxError& error) {
            std::cerr << sprig::formatDiagnostic(path, source, sprig::Diagnostic{error.loc, error.what()});
            return kCompileError;
        }
        return kOk;
    }

    const sprig::CompileResult result = sprig::compile(source, options);
    if (!result.ok()) {
        for (const sprig::Diagnostic& diagnostic : result.diagnostics) {
            std::cerr << sprig::formatDiagnostic(path, source, diagnostic);
        }
        std::cerr << result.diagnostics.size() << " error(s)\n";
        return kCompileError;
    }

    if (dumpAst) {
        std::cout << sprig::printProgram(*result.ast);
        return kOk;
    }
    if (dumpBytecode) {
        std::cout << sprig::disassemble(result.module);
        return kOk;
    }

    sprig::VM vm(result.module, std::cout);
    if (maxInstructions != 0) vm.setInstructionLimit(maxInstructions);
    const auto started = std::chrono::steady_clock::now();
    int exitCode = kOk;
    try {
        vm.run();
    } catch (const sprig::RuntimeError& error) {
        std::cout.flush();
        std::cerr << path << ':' << error.line << ": runtime error: " << error.what() << '\n';
        exitCode = kRuntimeError;
    }
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - started;
    std::cout.flush();

    if (showStats) {
        std::size_t codeBytes = 0;
        for (const sprig::Function& fn : result.module.functions) codeBytes += fn.code.size();

        // One "name: value" pair per line so scripts (see bench/bench.py) can parse it.
        std::cerr << "optimizer: " << (options.optimize ? "on" : "off") << '\n'
                  << "constants propagated: " << result.optStats.constantsPropagated << '\n'
                  << "expressions folded: " << result.optStats.expressionsFolded << '\n'
                  << "statements removed: " << result.optStats.statementsRemoved << '\n'
                  << "bytecode bytes: " << codeBytes << '\n'
                  << "instructions executed: " << vm.stats().instructions << '\n'
                  << "calls: " << vm.stats().calls << '\n'
                  << "max call depth: " << vm.stats().maxFrames << '\n'
                  << "run time ms: " << elapsed.count() << '\n';
    }
    return exitCode;
}
