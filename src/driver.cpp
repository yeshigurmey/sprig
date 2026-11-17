#include "driver.h"

#include <sstream>

#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"

namespace sprig {

CompileResult compile(const std::string& source, const CompileOptions& options) {
    CompileResult result;
    try {
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        result.ast = parser.parseProgram();

        result.diagnostics = analyze(*result.ast);
        // The optimizer and code generator assume a valid program, so stop
        // here if the type checker found anything.
        if (!result.diagnostics.empty()) return result;

        if (options.optimize) result.optStats = optimize(*result.ast);
        result.module = generate(*result.ast);
    } catch (const SyntaxError& error) {
        result.diagnostics.push_back(Diagnostic{error.loc, error.what()});
    }
    return result;
}

std::string formatDiagnostic(const std::string& filename, const std::string& source, const Diagnostic& diagnostic) {
    std::ostringstream out;
    out << filename << ':' << diagnostic.loc.line << ':' << diagnostic.loc.col << ": error: " << diagnostic.message
        << '\n';

    // Find the text of the line the error is on.
    std::size_t start = 0;
    for (int line = 1; line < diagnostic.loc.line; ++line) {
        const std::size_t newline = source.find('\n', start);
        if (newline == std::string::npos) return out.str();
        start = newline + 1;
    }
    std::size_t end = source.find('\n', start);
    if (end == std::string::npos) end = source.size();
    std::string text = source.substr(start, end - start);
    if (!text.empty() && text.back() == '\r') text.pop_back();

    // Keep tabs as tabs in the padding so the caret lines up in a terminal.
    std::string padding;
    for (int col = 1; col < diagnostic.loc.col; ++col) {
        const std::size_t index = static_cast<std::size_t>(col - 1);
        padding += (index < text.size() && text[index] == '\t') ? '\t' : ' ';
    }
    out << "    " << text << '\n' << "    " << padding << "^\n";
    return out.str();
}

}  // namespace sprig
