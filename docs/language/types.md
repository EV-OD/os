# .ros Language — Type System

---

## Primitive Types

| Type | Size | Range / Values | Notes |
|------|------|----------------|-------|
| `i32` | 4 bytes | −2 147 483 648 … 2 147 483 647 | Default integer type |
| `u32` | 4 bytes | 0 … 4 294 967 295 | Unsigned (no negative literals) |
| `bool` | 1 byte | `true`, `false` | Stored as 0/1 |
| `str` | 4 bytes (ptr) | Pointer to null-terminated data | String literals only; no mutation |

---

## Variable Declaration

```ros
let x: i32 = 42          // immutable — x cannot be reassigned
let mut n: i32 = 0        // mutable   — n can be reassigned or compound-assigned
let flag: bool = true
let msg: str = "hello"
```

Type annotations are **required** on `let` declarations.

---

## Mutability

| Declared as | Can reassign? | Can compound-assign? |
|-------------|--------------|----------------------|
| `let` | ✗ | ✗ |
| `let mut` | ✓ | ✓ |

Attempting to assign to an immutable binding is a compile-time error.

---

## Integer Literals

| Format | Example | Parsed as |
|--------|---------|-----------|
| Decimal | `42`, `0`, `1000` | `i32` |
| Hexadecimal | `0xFF`, `0x1B` | `i32` |
| Negative | `-1`, `-100` | Parsed as unary negation of a positive literal |

---

## Boolean Values

`true` and `false` are keywords. Booleans are used as conditions in `if` and `while`:

```ros
let done: bool = false
while !done {
    // ...
}
```

Any `i32` can be used as a boolean condition: `0` is false, non-zero is true.

---

## String Type

`str` is a pointer to a null-terminated character array. String values are **string literals only** — there is no dynamic string type in phase 1.

```ros
let greeting: str = "Hello, RandomOS!\n"
print(greeting)
```

`str` values cannot be modified. They live in the data section of the `.rox` binary.

Escape sequences inside string literals:

| Sequence | Character |
|----------|-----------|
| `\n` | Newline (0x0A) |
| `\t` | Tab (0x09) |
| `\\` | Backslash |
| `\"` | Double quote |

---

## Type Rules

- Every expression has a statically known type.
- Arithmetic and bitwise operators require both operands to be `i32` or `u32`.
- Comparison operators accept numeric types and produce an `i32` (0 or 1).
- Logical operators (`&&`, `||`, `!`) accept any type and treat 0 as false.
- Function arguments and return values must match the declared types exactly.
- No implicit coercion between `i32` and `u32` — use an explicit cast expression.

---

## Casting (Phase 1)

Explicit numeric casts use the `as` keyword (currently treated as a `mov` without sign/zero extension logic — the upper bits are truncated for narrowing casts):

```ros
let big: i32 = 256
let small: u32 = big as u32
```

> Full cast semantics (sign-extension, zero-extension) are refined in later phases.

---

## Future Types (Planned)

| Type | Phase | Notes |
|------|-------|-------|
| `i8`, `i16` | 3 | Narrow signed integers |
| `u8`, `u16` | 3 | Narrow unsigned integers |
| `*T` | 3 | Raw pointer to type `T` |
| `T[N]` | 3 | Fixed-size array |
| `struct` | 3 | Value-type aggregate |
| Type inference | 4 | `let x = 42` infers `i32` |
