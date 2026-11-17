// Stage 5: abstract syntax tree -> bytecode.
#pragma once

#include "ast.h"
#include "bytecode.h"

namespace sprig {

// Translates a type checked program into bytecode for the VM.
// Must only be called on a program that passed analyze() with no errors.
// Throws SyntaxError if a function is too large for the 16-bit jump operands.
Module generate(const Program& program);

}  // namespace sprig
