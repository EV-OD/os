# .ros Language — Control Flow

---

## if / else if / else

```ros
if condition {
    // executed when condition is non-zero (true)
} else if other_condition {
    // executed when first is false and this is non-zero
} else {
    // executed when all conditions are false
}
```

- Condition does **not** need parentheses.
- Braces `{ }` are **required** — no single-line shorthand.
- `else if` chains can be arbitrarily long.
- The `else` branch is optional.

**Example:**
```ros
fn classify(n: i32) -> str {
    if n < 0 {
        return "negative"
    } else if n == 0 {
        return "zero"
    } else {
        return "positive"
    }
}
```

---

## while

```ros
while condition {
    // body
}
```

Executes the body repeatedly as long as `condition` is non-zero.
The condition is evaluated **before** each iteration.

**Example:**
```ros
let mut i: i32 = 1
while i <= 10 {
    print(i)
    i += 1
}
```

---

## for (range iteration)

```ros
for variable in low..high {
    // body — variable goes from low to high-1 (exclusive upper bound)
}
```

- `low` and `high` are `i32` expressions evaluated once before the loop.
- The loop variable is implicitly declared as `i32`; it cannot be reassigned inside the body.
- Equivalent to `let mut variable = low; while variable < high { ...; variable += 1 }`.

**Example:**
```ros
// Print 1 through 5
for i in 1..6 {
    println(i)
}

// Sum 0..99
let mut total: i32 = 0
for k in 0..100 {
    total += k
}
println(total)   // 4950
```

---

## break

Immediately exits the enclosing `while` or `for` loop.

```ros
let mut i: i32 = 0
while true {
    if i >= 5 { break }
    print(i)
    i += 1
}
```

---

## continue

Skips the rest of the current iteration and proceeds to the next.

```ros
// Print only even numbers 0–9
let mut k: i32 = 0
while k < 10 {
    k += 1
    if k % 2 != 0 { continue }
    print(k)
}
```

---

## Nested Loops

`break` and `continue` affect only the **innermost** enclosing loop.

```ros
for i in 0..5 {
    for j in 0..5 {
        if j == 3 { break }    // breaks inner loop only
        print(j)
    }
}
```

---

## Boolean Conditions

Any `i32` expression is valid as a condition: `0` = false, non-zero = true.
`bool` values (`true`/`false`) are stored as 1/0 and work the same way.

```ros
let found: bool = false
while !found {
    // search...
}
```
