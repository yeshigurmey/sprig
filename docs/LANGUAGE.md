# The Sprig language

This is the complete definition of the language. If a program does something this page does not describe, that is a bug.

## A complete program

```
// Counts the primes below 50.
fn is_prime(n: int) -> bool {
    if n < 2 {
        return false;
    }
    let d = 2;
    while d * d <= n {
        if n % d == 0 {
            return false;
        }
        d = d + 1;
    }
    return true;
}

let count = 0;
let n = 0;
while n < 50 {
    if is_prime(n) {
        count = count + 1;
    }
    n = n + 1;
}
print(count);
```

A file is a mix of function definitions and ordinary statements. The statements outside functions run from top to bottom, like a script. Functions can be defined anywhere in the file, including after the code that calls them.

## Types

| Type | Values | Notes |
|---|---|---|
| `int` | whole numbers from -9223372036854775808 to 9223372036854775807 | 64-bit, wraps around on overflow |
| `bool` | `true`, `false` | Not interchangeable with `int`. `if 1 { }` is a type error |

A function with no `-> type` returns nothing, and its result cannot be used as a value.

## Variables

```
let x = 5;            // type inferred from the value: int
let ok: bool = true;  // type written out
x = x + 1;            // assignment, must keep the same type
```

* Every variable needs a starting value.
* A variable exists from its `let` to the end of the block it was declared in.
* An inner block may declare a variable with the same name as an outer one. The inner one hides the outer one until the block ends.
* Declaring the same name twice in one block is an error.
* There are no global variables. A function sees its parameters and its own locals, nothing else. Variables declared at the top level of the file belong to the top level code only.

## Operators

From loosest to tightest binding. Operators on the same row bind equally and group left to right.

| Operators | Operands | Result |
|---|---|---|
| `\|\|` | bool | bool |
| `&&` | bool | bool |
| `==` `!=` | two ints or two bools | bool |
| `<` `<=` `>` `>=` | int | bool |
| `+` `-` | int | int |
| `*` `/` `%` | int | int |
| unary `-` | int | int |
| unary `!` | bool | bool |

Rules worth knowing:

* `&&` and `||` short circuit. In `a && b`, `b` is only evaluated if `a` is true. In `a || b`, `b` is only evaluated if `a` is false.
* `/` rounds toward zero, so `-7 / 2` is `-3`. `%` gives the matching remainder, so `-7 % 2` is `-1`.
* Dividing by zero, or taking a remainder by zero, stops the program with a runtime error.
* Overflow wraps: `9223372036854775807 + 1` is `-9223372036854775808`.

## Statements

```
if condition { ... }
if condition { ... } else { ... }
if a { ... } else if b { ... } else { ... }

while condition { ... }

print(expression);     // prints an int or a bool, followed by a newline

return;                // leave a function that returns nothing, or end the program at top level
return expression;     // leave a function with a value

{ ... }                // a bare block, which opens a new scope
```

Conditions are not wrapped in parentheses, and the braces are always required.

## Functions

```
fn name(first: int, second: bool) -> int {
    ...
}
```

* Parameters always need a type. The return type is optional.
* Arguments are passed by value. Assigning to a parameter inside a function does not affect the caller.
* A function that declares a return type must return on every path. The compiler rejects `fn f(n: int) -> int { if n > 0 { return 1; } }` because the `if` might not be taken.
* Recursion and mutual recursion work. Recursion deeper than about 100,000 calls stops the program with a "stack overflow" runtime error instead of crashing.
* Functions cannot be nested inside other functions.

## Comments

`//` starts a comment that runs to the end of the line.

## Grammar

```
program    := (function | statement)* EOF

function   := "fn" IDENT "(" (param ("," param)*)? ")" ("->" type)? block
param      := IDENT ":" type
type       := "int" | "bool"

statement  := "let" IDENT (":" type)? "=" expression ";"
            | IDENT "=" expression ";"
            | "if" expression block ("else" (ifStatement | block))?
            | "while" expression block
            | "return" expression? ";"
            | "print" "(" expression ")" ";"
            | block
            | expression ";"
block      := "{" statement* "}"

expression := or
or         := and ("||" and)*
and        := equality ("&&" equality)*
equality   := comparison (("==" | "!=") comparison)*
comparison := term (("<" | "<=" | ">" | ">=") term)*
term       := factor (("+" | "-") factor)*
factor     := unary (("*" | "/" | "%") unary)*
unary      := ("-" | "!") unary | primary
primary    := INT | "true" | "false" | IDENT | IDENT "(" (expression ("," expression)*)? ")"
            | "(" expression ")"

INT        := [0-9]+
IDENT      := [A-Za-z_][A-Za-z0-9_]*     (except keywords)
keywords   := fn let if else while return print true false int bool
```

The parser implements the six binary levels (`or` down to `factor`) with one loop and a precedence table instead of six separate functions. See `Parser::binary` in `src/parser.cpp`.
