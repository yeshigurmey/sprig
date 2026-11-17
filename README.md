# Sprig

A compiler and virtual machine for a small statically typed language, written from scratch in C++17. The compiler and VM use nothing beyond the standard library.

Sprig source code goes through a hand written lexer and parser, a type checker, an optimizer, and a bytecode generator. The result runs on a stack based virtual machine, the same execution model the JVM and CPython use.

```
fn fib(n: int) -> int {
    if n < 2 {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

print(fib(30));
```

```
$ ./build/sprig demo.sprig
832040
```

## How it works

```
 source text
     |
     v
 +--------+   tokens   +--------+   syntax tree   +--------------+
 | Lexer  | ---------> | Parser | --------------> | Type checker |
 +--------+            +--------+                 +--------------+
                                                         |
                                           typed tree    |
                                                         v
 +----+    bytecode    +----------------+          +-----------+
 | VM | <------------- | Code generator | <------- | Optimizer |
 +----+                +----------------+          +-----------+
```

| Stage | File | What it does |
|---|---|---|
| Lexer | `src/lexer.cpp` | Splits text into tokens and tracks line and column for error messages |
| Parser | `src/parser.cpp` | Recursive descent with precedence climbing, builds the syntax tree |
| Type checker | `src/sema.cpp` | Resolves names and scopes, checks types, assigns each variable a stack slot |
| Optimizer | `src/optimizer.cpp` | Constant propagation, constant folding, dead code elimination |
| Code generator | `src/codegen.cpp` | Emits bytecode, patches jump targets, computes each function's stack needs |
| VM | `src/vm.cpp` | Executes bytecode with a value stack and call frames |

## Build and run

You need a C++17 compiler (GCC or Clang) and CMake 3.16 or newer. GoogleTest is downloaded automatically the first time you build.

```
cmake -S . -B build
cmake --build build
./build/sprig examples/fib.sprig
```

Run the tests:

```
ctest --test-dir build --output-on-failure
```

Build with AddressSanitizer and UndefinedBehaviorSanitizer:

```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSPRIG_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Developed and tested on Linux with GCC 13 and Clang 18.

## Command line

```
usage: sprig [options] <file.sprig>

  -O0              turn the optimizer off
  -O1              turn the optimizer on (default)
  --dump-tokens    print the tokens the lexer produced, then exit
  --dump-ast       print the syntax tree (after optimizing, unless -O0), then exit
  --dump-bytecode  print the compiled bytecode, then exit
  --stats          after running, print optimizer and VM counters to stderr
  --max-instructions N
                   stop with an error after N instructions (guards against infinite loops)
```

Every stage can be inspected, which is the fastest way to see what the compiler is doing.

## Watching the optimizer work

`examples/constants.sprig` contains this function:

```
fn seconds_in(days: int) -> int {
    let seconds_per_minute = 60;
    let minutes_per_hour = 60;
    let hours_per_day = 24;
    return days * seconds_per_minute * minutes_per_hour * hours_per_day;
}
```

The tree with the optimizer off (`sprig -O0 --dump-ast`, with line breaks added here for readability):

```
(fn seconds_in (days:int) int (block
    (let seconds_per_minute 60)
    (let minutes_per_hour 60)
    (let hours_per_day 24)
    (return (* (* (* days seconds_per_minute) minutes_per_hour) hours_per_day))))
```

The tree with the optimizer on:

```
(fn seconds_in (days:int) int (block (return (* days 86400))))
```

Three variables and three multiplications became one multiplication. The compiled bytecode for that function (`sprig --dump-bytecode`):

```
== seconds_in (params: 1, locals: 4, max stack: 2) ==
0000  line   8  Load        slot 0
0003  line   8  Const       0  (= 86400)
0006  line   8  Mul
0007  line   8  Ret
0008  line   8  Trap
```

`Trap` is a guard that never runs. The type checker has already proven that every path through this function returns, and the trap turns any bug in that proof into a clear error instead of the VM reading past the end of the code.

## Error messages

Type errors are all reported in one run, each with the source line and a caret under the exact spot.

```
demo.sprig:5:18: error: argument 2 of 'area' must be int, but this is a bool
    let ok = area(3, true);
                     ^
demo.sprig:6:7: error: 'total' is not declared
    print(total + 1);
          ^
2 error(s)
```

Runtime errors report the line that failed:

```
demo.sprig:6: runtime error: division by zero
```

## The language

Sprig has two types, `int` (64-bit, wraps on overflow) and `bool`. It has variables with type inference, `if` / `else`, `while`, functions with recursion, and short circuit `&&` and `||`. The full grammar and rules are in [docs/LANGUAGE.md](docs/LANGUAGE.md).

## Design decisions

**Static types, so the VM needs no type tags.** Because the type checker proves what every value is before the program runs, the VM stores each value as a bare 64-bit integer and never checks types while running. `print` compiles to either `PrintInt` or `PrintBool`, chosen at compile time.

**Variables are resolved to stack slots at compile time.** The type checker gives every variable a numbered slot in its function's frame. At run time, reading a variable is a single array index, with no name lookups.

**One copy of the arithmetic rules.** The optimizer (which computes things at compile time) and the VM (which computes them at run time) both call the same functions in `src/arith.h`. An optimized program therefore cannot disagree with an unoptimized one about what `a / b` means.

**No undefined behaviour in arithmetic.** Signed overflow is undefined in C++, so wrapping is done on unsigned values. `INT64_MIN / -1`, which crashes the process on x86 if computed directly, is handled explicitly. The test suite runs under UndefinedBehaviorSanitizer to keep this honest.

**Stack overflow is checked once per call, not once per push.** The code generator tracks how deep the stack can get inside each function and records the maximum. The VM checks that bound when a function is called, so pushing a value is a plain write. Debug builds assert the bound on every instruction, which means the tests would catch a miscount.

**The optimizer refuses rewrites that could change behaviour.** `x * 0` is not replaced with `0`, because `x` might be a function call that prints. `1 / 0` is not folded, because it has to fail at run time on the right line. A variable is only treated as a constant if nothing in the function ever assigns to it.

## Testing

119 tests. The GitHub Actions workflow in `.github/workflows/ci.yml` builds and runs them on every push with GCC and Clang, including a Debug build under AddressSanitizer and UndefinedBehaviorSanitizer.

| Kind | Where | What it checks |
|---|---|---|
| Unit tests | `tests/lexer_test.cpp`, `parser_test.cpp`, `sema_test.cpp`, `optimizer_test.cpp` | Each stage on its own, including its error cases |
| End to end tests | `tests/vm_test.cpp` | Source in, printed output out |
| Differential tests | `tests/vm_test.cpp`, `tests/examples_test.cpp` | Every program runs with the optimizer off and on, and both runs must produce identical output |
| Golden tests | `examples/*.expected` | Each example program must produce its recorded output |
| Command line tests | `CMakeLists.txt` | Exit behaviour and messages of the `sprig` binary itself |

The differential tests are the important ones for the optimizer. They catch any rewrite that changes what a program does, including cases nobody wrote a specific test for.

## Benchmarks

`python3 bench/bench.py build/sprig` runs each program in `bench/` with the optimizer off and on.

| Program | Instructions -O0 | Instructions -O1 | Fewer instructions | Time -O0 | Time -O1 | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| collatz | 471,175,527 | 471,175,525 | 0.0% | 725 ms | 725 ms | 1.00x |
| constants | 87,000,022 | 57,000,014 | 34.5% | 113 ms | 73 ms | 1.56x |
| fib | 70,491,550 | 70,491,550 | 0.0% | 122 ms | 123 ms | 1.00x |
| primes | 218,799,767 | 218,799,765 | 0.0% | 290 ms | 285 ms | 1.02x |

Measured with a GCC 13 Release build on a Linux x86-64 machine. Times will differ on other hardware. Instruction counts are exact and will not.

What the numbers say:

* The optimizer pays off on code full of named constants and spelled out arithmetic (`constants`: 34.5% fewer instructions, about 1.5x faster). That is the case its three passes target.
* It does nothing for `fib`, `primes` and `collatz`, where every value depends on a loop counter or a function argument. Making those faster needs different techniques, listed below.
* The interpreter itself executes roughly 550 to 800 million bytecode instructions per second on this machine.

## Project layout

```
src/            the compiler and VM (about 2,900 lines)
tests/          GoogleTest suites (about 900 lines)
examples/       sample programs with their expected output
bench/          benchmark programs and the script that measures them
docs/           language reference and a guided walkthrough of the code
.github/        continuous integration workflow
```

## Limitations and what could come next

Sprig is deliberately small. Things it does not have, roughly in the order they would be worth adding:

* **Faster instruction dispatch.** Fusing common pairs such as "compare, then jump" into single instructions would cut the instruction count of every loop, which the current optimizer cannot do.
* **Function inlining.** Would let constant propagation see through calls such as `seconds_in(7)`.
* **More types.** Strings, arrays and floating point numbers.
* **Global variables.** Functions currently see only their parameters and their own locals.
* **Slot reuse.** A variable removed by the optimizer still has a stack slot reserved for it.
* **Error recovery in the parser.** It stops at the first syntax error, while the type checker reports all type errors.
