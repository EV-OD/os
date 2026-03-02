# .ros Language — Functions

---

## Declaration

```ros
fn name(param1: type1, param2: type2) -> return_type {
    // body
    return value
}
```

- `fn` keyword introduces a function.
- Parameter types are **required**.
- `-> return_type` is omitted for void functions.
- The body must be a block `{ }`.

---

## Void Functions

```ros
fn greet(name: str) {
    print("Hello, ")
    println(name)
}
```

A void function returns implicitly at the closing brace, or explicitly with a bare `return`:

```ros
fn maybe_print(x: i32) {
    if x == 0 { return }
    println(x)
}
```

---

## Value-Returning Functions

```ros
fn square(n: i32) -> i32 {
    return n * n
}

fn max(a: i32, b: i32) -> i32 {
    if a > b { return a }
    return b
}
```

The `return` statement exits the function immediately with the given value.

---

## Calling Functions

```ros
let result: i32 = square(7)       // 49
let bigger: i32 = max(10, 42)     // 42
greet("RandomOS")
```

Arguments are passed by value. For `i32`/`u32`/`bool`, this means a copy of the integer.
For `str`, the pointer value is copied (the string data is not duplicated).

---

## Recursion

Functions can call themselves:

```ros
fn factorial(n: i32) -> i32 {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

fn fib(n: i32) -> i32 {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
```

The call stack lives at `0xBFFFF000` (growing downward). Stack depth is limited by the
4 KB page mapped there — deep recursion will page-fault and terminate the process.

---

## Nested Calls

Function calls can be used anywhere an expression is expected:

```ros
let x: i32 = add(fib(5), factorial(3))   // 8 + 6 = 14
println(max(square(3), square(4)))        // 16
```

---

## Forward Declarations

Functions can be called before they are defined in the source text — the compiler
performs a forward-declaration pass before code generation.

```ros
fn main() {
    println(helper())    // ok — helper defined below
}

fn helper() -> i32 {
    return 42
}
```

---

## Parameters and Scope

- Parameters are immutable by default (same as `let`).
- Variables declared inside a function body are local to that block.
- There are no global mutable variables in Phase 1 — all mutable state must be passed as parameters or stored in a text-buffer handle (`tbuf_*`).

---

## Calling Convention (generated code)

The compiler generates **cdecl** code for all function calls:
- Arguments pushed right-to-left onto the stack before `call`
- Return value in `EAX`
- Caller cleans the stack (`add esp, N`) after the call
- `EBX`, `ESI`, `EDI` are callee-saved

This matches the kernel's C calling convention so `.ros` functions can in principle
be called from C and vice versa when the pointer types align.
