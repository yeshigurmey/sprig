# A tour of the code

This page walks through the compiler one stage at a time, in the order a program travels through it. Each section says what the stage receives, what it hands on, the one idea that makes it work, and why it was built that way. Every section ends with something to try, because the quickest way to understand a stage is to watch it run.

Build the project first (see the README), then follow along with `./build/sprig`.

## The big picture

A computer cannot run text. Something has to turn `print(1 + 2 * 3);` into steps a machine can follow. Doing that in one giant leap is hard, so compilers break the job into small translations, each one a little closer to the machine:

```
"1 + 2 * 3"                      text: a row of characters
[1] [+] [2] [*] [3]              tokens: a row of words
(+ 1 (* 2 3))                    tree: who belongs with whom
Const 1, Const 2, Const 3,       bytecode: one small step at a time
Mul, Add
7                                the answer
```

Each stage has exactly one job and trusts the stage before it. The parser never worries about what a character is, because the lexer handled that. The code generator never worries about type errors, because the type checker already rejected those programs. That separation is the reason each file in `src/` is short enough to read in one sitting.

`src/driver.cpp` is where the stages are chained together. Its `compile` function is about twenty lines, and it is a good first thing to read.

## Stage 1: the lexer

**Files:** `src/token.h`, `src/lexer.h`, `src/lexer.cpp`
**In:** the source file as one string. **Out:** a list of tokens.

A token is one meaningful unit: a number, a name, a keyword, an operator. The lexer walks the text from left to right with a single index (`pos_`), looks at the current character, and decides which kind of token starts there.

**The key idea is one character of lookahead.** When the lexer sees `=`, it cannot know yet whether this is `=` (assignment) or `==` (comparison). So it peeks at the next character without consuming it. That is all `match()` does. The same trick separates `-` from `->`, `<` from `<=`, and `/` from the start of a `//` comment.

**Why track line and column on every token?** Every error message the compiler ever prints needs a position, and the token is the only place that information exists. The parser copies it onto tree nodes, and the code generator copies the line into the bytecode so that even a runtime error can say which line failed.

**A detail worth understanding:** `Lexer::number` checks for overflow *before* multiplying, with `value > (max - digit) / 10`. Checking afterwards would be too late, because signed overflow in C++ is undefined behaviour: the compiler is allowed to assume it never happens, so a check written after the fact can be optimized away.

**Try it**

```
./build/sprig --dump-tokens examples/fib.sprig
```

Then put a `@` somewhere in a copy of the file and run it again to see the error and its position.

## Stage 2: the parser

**Files:** `src/ast.h`, `src/parser.h`, `src/parser.cpp`, `src/ast_printer.cpp`
**In:** tokens. **Out:** an abstract syntax tree (AST).

A flat list of tokens does not say that in `1 + 2 * 3` the multiplication happens first. A tree does:

```
      +
     / \
    1   *
       / \
      2   3
```

The rule for reading a tree is "compute the children, then apply the operator", so the shape of the tree *is* the order of operations.

**The key idea is recursive descent: one function per grammar rule.** The grammar in `docs/LANGUAGE.md` says an `if` statement is the word `if`, an expression, a block, and optionally `else` and more. `Parser::ifStatement` reads exactly that, in that order, calling `expression()` and `block()` for the parts. Since a block contains statements and a statement can be an `if`, the functions call each other in a loop, which is where "recursive" comes from.

**Operator precedence** is handled by `Parser::binary`, using a technique called precedence climbing. Each operator has a number (`*` is 6, `+` is 5). The function parses one operand and then keeps absorbing operators as long as they are at least as strong as a minimum it was given. When it meets `+` and then `*`, it calls itself with a higher minimum for the right hand side, so the `*` is grouped first. The comment above the function explains why the recursive call uses `precedence + 1`: that one detail is what makes `10 - 4 - 3` come out as 3 and not 9.

**Why write the parser by hand instead of using a generator like Bison?** A hand written parser can give specific error messages ("parameters need a type, like `n: int`"), is easy to step through in a debugger, and is what GCC and Clang themselves use.

**Why does each node have a `kind` tag instead of using virtual functions?** Later stages `switch` on the tag and cast to the concrete type. This keeps each pass (type checking, optimizing, code generation) together in one file as one readable `switch`. With virtual functions, the code for one pass would be scattered across every node class. LLVM and Clang make the same choice.

**Who owns the tree?** Each node holds its children in `std::unique_ptr`. That means a child has exactly one owner, and deleting the root automatically deletes everything below it. There is no `delete` anywhere in the project.

**Try it**

```
./build/sprig -O0 --dump-ast examples/gcd.sprig
```

The output uses parentheses to show nesting: `(+ 1 (* 2 3))` is the tree drawn above. The parser tests in `tests/parser_test.cpp` compare against exactly this text.

## Stage 3: the type checker

**Files:** `src/sema.h`, `src/sema.cpp`
**In:** the tree. **Out:** the same tree with extra information filled in, plus a list of errors.

("Sema" is short for semantic analysis, the traditional name for this stage.)

The parser only checks grammar. `print(1 + true);` is grammatically fine and still nonsense. This stage walks the tree and checks meaning: does every name exist, do the types fit, does every function return what it promised.

**The key idea is the scope stack.** `scopes_` is a list of tables, one per block that is currently open. Entering `{` pushes a new empty table, leaving `}` pops it. Looking up a name searches from the newest table backwards. That one structure gives the three behaviours people expect: an inner variable hides an outer one, a variable disappears when its block ends, and two variables with the same name in the same block clash.

**It runs in two passes.** The first pass only records every function's name and signature. The second pass checks the bodies. Without the first pass, a function could not call one defined further down the file, and two functions could never call each other.

**It also prepares the next stages.** While checking, it writes four things onto the tree:

* the type of every expression
* a numbered stack slot for every variable, so the VM never deals with names
* the index of the function each call refers to
* the number of slots each function needs

**Why does it keep going after an error?** Fixing one error per compile is slow, so it collects them all. The risk is an avalanche: one undeclared variable makes the expression around it invalid, which makes the statement around that invalid. The special type `Type::Error` prevents this. Anything that touches an `Error` stays quiet, so one mistake produces one message. `Sema.OneMistakeProducesOneError` in the tests pins that down.

**Try it**

Make a file with several mistakes and see them all reported at once:

```
let x = 1;
print(y);
x = true;
if x { }
```

## Stage 4: the optimizer

**Files:** `src/optimizer.h`, `src/optimizer.cpp`, `src/arith.h`
**In:** a valid, typed tree. **Out:** a smaller tree that behaves identically.

Three passes work together, and each one creates work for the next:

1. **Constant propagation.** If a variable is given a known value and never assigned again, every read of it is replaced by that value. `let n = 10; print(n * 2);` becomes `print(10 * 2);`.
2. **Constant folding.** An operation whose inputs are all known is computed now. `10 * 2` becomes `20`.
3. **Dead code elimination.** Code that can no longer run is deleted: the branch of an `if` whose condition folded to `false`, anything after a `return`, a `let` whose variable has been replaced everywhere.

**The key idea is that an optimization is only allowed if nobody can tell it happened.** Most of the code in this file is not the rewrites themselves. It is the checks that decide when a rewrite is safe:

* A variable is only propagated if no assignment to it exists *anywhere* in the function. The check is for the whole function, not just "earlier in the file", because of loops: in `while ... { print(i); i = i + 1; }` the assignment is written after the `print` but runs before the next one.
* `1 / 0` is never folded. The language says this is a runtime error, so the error has to survive to run time.
* `x * 0` is not replaced by `0`, because `x` could be a function call that prints something. But `false && x` *is* replaced by `false`, because `&&` would have skipped `x` anyway.
* `(x * 60) * 24` is regrouped to `x * 1440` only because Sprig integers wrap on overflow, which keeps multiplication associative. In a language where overflow is an error, this rewrite would be wrong.

**Why does `src/arith.h` exist?** The optimizer computes `a / b` at compile time and the VM computes it at run time. If those were two separately written pieces of code, they could disagree in some corner case, and a program would behave differently depending on whether it was optimized. Both call the same function, so they cannot.

**Try it**

```
./build/sprig -O0 --dump-ast examples/constants.sprig
./build/sprig     --dump-ast examples/constants.sprig
./build/sprig -O0 --stats examples/constants.sprig
./build/sprig     --stats examples/constants.sprig
```

Compare the trees, then compare "instructions executed" between the two `--stats` runs.

## Stage 5: the code generator

**Files:** `src/bytecode.h`, `src/bytecode.cpp`, `src/codegen.h`, `src/codegen.cpp`
**In:** the tree. **Out:** bytecode, a flat list of tiny instructions.

The VM is a **stack machine**. Instructions do not say where their inputs are. They take them from the top of a stack and put the result back. To generate code for a tree, visit the children first and the operator last:

```
1 + 2 * 3     Const 1     stack: 1
              Const 2     stack: 1 2
              Const 3     stack: 1 2 3
              Mul         stack: 1 6
              Add         stack: 7
```

That is the entire algorithm for expressions, and it is why `genExpr` is short. A stack machine was chosen over a register machine for exactly this reason: generating code needs no decisions about where to keep values.

**The key idea for control flow is backpatching.** There are no `if` or `while` instructions, only jumps. A loop looks like this:

```
let i = 0;            0000  Const 0
while i < 3 {         0003  Store slot 0
    i = i + 1;        0006  Load slot 0         <- loop start
}                     0009  Const 3
                      0012  Lt
                      0013  JumpIfFalse -> 0029
                      0016  Load slot 0
                      0019  Const 1
                      0022  Add
                      0023  Store slot 0
                      0026  Jump -> 0006
                      0029  Halt                <- loop end
```

When the generator writes `JumpIfFalse` at 0013 it has a problem: the target is the end of the loop, and the body has not been generated yet, so nobody knows that address. The solution is to write a placeholder, generate the body, and then go back and fill in the real address. `emitJump` writes the placeholder and `patchJump` fills it in.

**`&&` and `||` are compiled as jumps, not as instructions.** An instruction would need both sides already computed, but the whole point of short circuiting is that the right side might never run.

**Tracking stack depth.** While emitting, the generator keeps a running count of how many values are on the stack and records the highest it reaches in `maxStack`. The VM uses that number to check for stack overflow once per call instead of on every push. This is the same approach the JVM takes with the `max_stack` field of a class file.

**Try it**

```
./build/sprig -O0 --dump-bytecode examples/fib.sprig
```

Read the listing for `fib` next to its source and match every line. Then find the two jumps in `examples/logic.sprig` that implement the first `&&`.

## Stage 6: the virtual machine

**Files:** `src/vm.h`, `src/vm.cpp`
**In:** bytecode. **Out:** the program's output.

The VM is a loop: read one instruction, do what it says, repeat. `VM::run` is one `switch` inside a `for (;;)`.

**The key idea is the call frame.** All values live in one big array. Each active function call owns a contiguous piece of it:

```
     base                     base + numLocals                sp
      |                              |                         |
      v                              v                         v
  ... | param 0 | param 1 | local 2 | temp | temp | temp | (free)
```

`Load slot 1` means "read the value at `base + 1`". That is why recursion works: each call to `fib` has its own `base`, so its own `n`.

**Calling a function copies nothing.** The caller pushes the arguments, so they are already sitting on top of the stack. The VM sets the new `base` to point at them, and they become the callee's first slots. Returning sets `sp` back to `base`, which discards the entire frame in one assignment, then pushes the result where the arguments used to be.

**Values have no type tags.** Many interpreters store a "this is an int" marker beside every value and check it on every operation. Sprig does not need to, because the type checker already proved every instruction gets the right kind of value. A `bool` is stored as 0 or 1 in the same 64-bit slot as an `int`.

**Things that could crash the host program are handled explicitly:**

* Division by zero is checked before dividing and reported with a line number.
* `INT64_MIN / -1` overflows and raises a hardware exception on x86. `arith::div` handles it.
* Runaway recursion hits a frame limit and becomes a "stack overflow" error instead of a segmentation fault.
* A function that somehow ends without returning hits a `Trap` instruction instead of running off the end of the code.

**Try it with a debugger**

Build with debug information and watch the stack change one instruction at a time:

```
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
gdb --args ./build-debug/sprig -O0 examples/gcd.sprig
```

Inside gdb, set a breakpoint on the line in `src/vm.cpp` that reads `const Op op = static_cast<Op>(code[ip++]);`, then:

```
(gdb) run
(gdb) print sprig::opName((sprig::Op)code[ip])     which instruction is next
(gdb) print sp                                     how many values are on the stack
(gdb) print stack[sp-1]                            the value on top
(gdb) continue                                     advance one instruction
```

On macOS use `lldb -- ./build-debug/sprig -O0 examples/gcd.sprig`. The commands are `b vm.cpp:<line>`, `r`, `p sp`, and `c`.

## How it is tested

**Files:** `tests/`, `examples/*.expected`, `.github/workflows/ci.yml`

Each stage has its own test file that feeds it small inputs and checks the result, including the error cases. Two techniques are worth pointing out because they find bugs that ordinary unit tests miss.

**Printing the tree as text.** The parser and optimizer tests do not inspect node objects one field at a time. They print the tree with `printProgram` and compare one string, for example `"(+ 1 (* 2 3))"`. A test for precedence is then a single readable line.

**Differential testing.** The helper `runBoth` in `tests/vm_test.cpp` runs every program twice, with the optimizer off and on, and fails if the outputs differ. This checks the property that actually matters ("optimizing never changes behaviour") across every test program, without anyone having to predict which optimization might be wrong.

**Sanitizers.** A C++ program can contain memory errors and undefined behaviour and still appear to work. Building with `-DSPRIG_SANITIZE=ON` makes the compiler insert checks that stop the program the moment either happens. The CI workflow runs the whole test suite this way on every push.

**An instruction limit in tests.** Every VM created by the tests gets a cap on how many instructions it may execute. A bug that causes an infinite loop then fails one test with a clear message instead of hanging the entire run.

**Try it**

Break something on purpose and see which tests notice. Two good ones:

* In `src/parser.cpp`, change `binary(precedence + 1)` to `binary(precedence)`.
* In `src/optimizer.cpp`, remove the `reassigned_.count(let.slot) == 0` condition.

Rebuild, run `ctest --test-dir build --output-on-failure`, read the failures, then undo the change.
