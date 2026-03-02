# .ros Language — Syntax Reference

---

## Comments

```ros
// This is a line comment
/// This is a doc-comment (parsed by docs_gen.py)
```

There are no block comments.

---

## Literals

| Kind | Example | Notes |
|------|---------|-------|
| Integer (decimal) | `42`, `0`, `-1` | `i32` by default |
| Integer (hex) | `0xFF`, `0x1F` | Prefix `0x` |
| Boolean | `true`, `false` | `bool` type |
| String | `"hello\nworld"` | Escape: `\n \t \\ \"` |

---

## Identifiers & Keywords

Keywords: `let`, `mut`, `fn`, `return`, `if`, `else`, `while`, `for`, `in`,
`break`, `continue`, `import`, `true`, `false`

Type names: `i32`, `u32`, `bool`, `str`

Identifiers: start with a letter or `_`, followed by letters, digits, `_`.

---

## Variables

```ros
let x: i32 = 42           // immutable binding
let mut counter: i32 = 0  // mutable binding
```

`let` bindings without `mut` cannot be assigned after declaration.

### Compound Assignment (mutable only)

```ros
counter += 1
counter -= 5
counter *= 2
counter /= 3
counter %= 7
```

---

## Types

| Type | Size | Description |
|------|------|-------------|
| `i32` | 4 B | Signed 32-bit integer (default numeric type) |
| `u32` | 4 B | Unsigned 32-bit integer |
| `bool` | 1 B | `true` / `false` |
| `str` | ptr | Pointer to a null-terminated string literal |

See [types.md](types.md) for casting and full type rules.

---

## Operators

### Arithmetic

| Op | Description |
|----|-------------|
| `+` | Addition |
| `-` | Subtraction / unary negation |
| `*` | Multiplication |
| `/` | Integer division (truncates toward zero) |
| `%` | Modulo (remainder, same sign as dividend) |

### Bitwise

| Op | Description |
|----|-------------|
| `&` | Bitwise AND |
| `\|` | Bitwise OR |
| `^` | Bitwise XOR |
| `~` | Bitwise NOT (unary) |
| `<<` | Left shift |
| `>>` | Right shift (arithmetic) |

### Comparison

| Op | Description |
|----|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |

### Logical

| Op | Description |
|----|-------------|
| `&&` | Logical AND (short-circuit) |
| `\|\|` | Logical OR (short-circuit) |
| `!` | Logical NOT (unary) |

### Precedence (high → low)

| Level | Operators |
|-------|-----------|
| 7 | unary `-`, `!`, `~` |
| 6 | `*`, `/`, `%` |
| 5 | `+`, `-` |
| 4 | `<<`, `>>` |
| 3 | `&`, `^`, `\|` |
| 2 | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| 1 | `&&`, `\|\|` |

Parentheses override any precedence level.

---

## Control Flow

### if / else if / else

```ros
if x > 10 {
    println("big")
} else if x > 0 {
    println("small")
} else {
    println("zero or negative")
}
```

Braces are **required**. The condition does not need parentheses.

### while

```ros
let mut i: i32 = 0
while i < 10 {
    print(i)
    i += 1
}
```

### for (range)

```ros
for i in 0..10 {     // i = 0, 1, ..., 9  (exclusive upper bound)
    print(i)
}
for i in 1..6 {      // i = 1, 2, 3, 4, 5
    println(i)
}
```

### break / continue

```ros
while true {
    if done == 1 { break }
    if skip == 1 { continue }
    process()
}
```

---

## Functions

```ros
fn greet(name: str) {
    print("Hello, ")
    println(name)
}

fn add(a: i32, b: i32) -> i32 {
    return a + b
}

fn factorial(n: i32) -> i32 {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
```

- Return type is optional (void functions omit `-> type`)
- `return` exits the function with a value
- Functions must be declared before they are called (or in any order — the compiler does a forward-declaration pass)
- Recursive calls are fully supported

---

## Imports

```ros
import gui     // loads /lib/ros/gui.ros
import math    // loads /lib/ros/math.ros
import io      // loads /lib/ros/io.ros
```

Import declaration must appear at the top of the file.
After importing, all functions defined in that library are available without a namespace prefix.

See [imports.md](imports.md) and [stdlib.md](stdlib.md) for details.

---

## Built-in Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `print(x)` | Print integer or string literal (no newline) |
| `println` | `println(x)` | Print integer or string literal + newline |
| `getarg` | `getarg(n: i32) -> i32` | Return the n-th command-line argument as a string handle (0 = program name) |

---

## Entry Point

If a `main()` function is defined, it is called automatically as the program entry point.
If no `main()` is defined, top-level statements execute sequentially.

---

## Program Structure

```ros
// 1. Imports (optional, must be first)
import gui

// 2. Helper functions (any order)
fn helper(x: i32) -> i32 {
    return x * 2
}

// 3. main() or top-level statements
fn main() {
    let result: i32 = helper(21)
    println(result)
}
```

---

## Full Grammar (EBNF)

```
program     = import* (fn_decl | statement)* ;

import      = "import" IDENT ;

fn_decl     = "fn" IDENT "(" param_list ")" ["->" type] block ;
param_list  = (param ("," param)*)? ;
param       = IDENT ":" type ;

block       = "{" statement* "}" ;

statement   = let_stmt
            | assign_stmt
            | compound_stmt
            | return_stmt
            | if_stmt
            | while_stmt
            | for_stmt
            | break_stmt
            | continue_stmt
            | expr_stmt ;

let_stmt    = "let" ["mut"] IDENT ":" type "=" expr ;
assign_stmt = IDENT "=" expr ;
compound_stmt = IDENT ("+=" | "-=" | "*=" | "/=" | "%=") expr ;
return_stmt = "return" expr? ;
if_stmt     = "if" expr block ("else" "if" expr block)* ["else" block] ;
while_stmt  = "while" expr block ;
for_stmt    = "for" IDENT "in" expr ".." expr block ;
break_stmt  = "break" ;
continue_stmt = "continue" ;
expr_stmt   = expr ;

expr        = logical_or ;
logical_or  = logical_and (("||") logical_and)* ;
logical_and = comparison (("&&") comparison)* ;
comparison  = bitwise (("==" | "!=" | "<" | ">" | "<=" | ">=") bitwise)* ;
bitwise     = shift (("&" | "|" | "^") shift)* ;
shift       = additive (("<<" | ">>") additive)* ;
additive    = multiplicative (("+" | "-") multiplicative)* ;
multiplicative = unary (("*" | "/" | "%") unary)* ;
unary       = ("-" | "!" | "~") unary | primary ;
primary     = INT_LIT
            | STR_LIT
            | "true" | "false"
            | IDENT "(" arg_list ")"    (* function call *)
            | IDENT
            | "(" expr ")" ;

arg_list    = (expr ("," expr)*)? ;

type        = "i32" | "u32" | "bool" | "str" ;
```
