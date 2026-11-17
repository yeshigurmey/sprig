// Glue that runs the compiler stages in order. Both the command line tool and
// the tests go through here, so they exercise exactly the same pipeline.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ast.h"
#include "bytecode.h"
#include "diagnostics.h"
#include "optimizer.h"

namespace sprig {

struct CompileOptions {
    bool optimize = true;
};

struct CompileResult {
    // Empty when compilation succeeded. If it is not empty, `module` is unusable.
    std::vector<Diagnostic> diagnostics;

    std::unique_ptr<Program> ast;  // the tree after type checking (and optimizing, if enabled)
    Module module;
    OptStats optStats;

    bool ok() const { return diagnostics.empty(); }
};

// source text -> lexer -> parser -> type checker -> optimizer -> code generator
CompileResult compile(const std::string& source, const CompileOptions& options = {});

// Formats an error the way gcc and clang do, with the offending line and a
// caret under the exact column:
//
//     demo.sprig:3:9: error: 'y' is not declared
//         print(y);
//               ^
std::string formatDiagnostic(const std::string& filename, const std::string& source, const Diagnostic& diagnostic);

}  // namespace sprig
