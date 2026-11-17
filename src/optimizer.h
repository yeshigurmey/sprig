// Stage 4 (optional, on by default): rewrite the tree into a cheaper one that
// behaves exactly the same.
#pragma once

#include "ast.h"

namespace sprig {

// Counts of what the optimizer did, shown by --stats.
struct OptStats {
    int constantsPropagated = 0;  // variable reads replaced by their known value
    int expressionsFolded = 0;    // operations computed at compile time
    int statementsRemoved = 0;    // statements proven to never matter
};

// Runs three classic optimizations over a type checked program:
//   1. constant propagation   let n = 10; ... n * 2      ->   ... 10 * 2
//   2. constant folding       10 * 2                     ->   20
//   3. dead code elimination  if false { ... }           ->   (nothing)
//
// Must only be called on a program that passed analyze() with no errors.
OptStats optimize(Program& program);

}  // namespace sprig
