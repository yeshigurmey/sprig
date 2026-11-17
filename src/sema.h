// Stage 3: semantic analysis ("sema"). Checks that a syntactically valid
// program also makes sense: names exist, types match, functions return.
#pragma once

#include <vector>

#include "ast.h"
#include "diagnostics.h"

namespace sprig {

// Checks the program and annotates the tree in place:
//   * every expression gets its `type`
//   * every variable use and declaration gets its stack `slot`
//   * every call gets the index of the function it calls
//   * every function gets `numLocals`
//
// Returns all the problems found. An empty result means the program is valid
// and the later stages can trust the annotations without rechecking anything.
std::vector<Diagnostic> analyze(Program& program);

}  // namespace sprig
