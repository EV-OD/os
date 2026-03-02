# Docs Generator — README

`docs_gen.py` parses `.ros` source files, extracts **doc-comment blocks** (`///` 
lines immediately above `fn` declarations), and emits a Markdown API reference.

---

## Install / Requirements

- Python 3.10+
- No third-party packages required

---

## Doc-Comment Format

Place `///` comments directly above a `fn` declaration:

```ros
/// Returns the absolute value of x.
/// x — the input integer
fn abs(x: i32) -> i32 {
    if x < 0 { return 0 - x }
    return x
}
```

Module-level documentation uses `//!` at the top of the file:

```ros
//! math.ros — RandomOS integer math utilities.
//! Import with: import math
```

---

## Usage

```bash
# Single file → stdout
python3 compiler/docs_gen/docs_gen.py gui.ros

# Multiple files → stdout (one block per file)
python3 compiler/docs_gen/docs_gen.py gui.ros math.ros io.ros

# All stdlib modules (looks in compiler/stdlib/)
python3 compiler/docs_gen/docs_gen.py --all-stdlib

# Custom stdlib directory
python3 compiler/docs_gen/docs_gen.py --stdlib-dir ./extracted_ros

# Write one .md per file to an output directory
python3 compiler/docs_gen/docs_gen.py --all-stdlib --out-dir docs/language/gen

# Combined index page
python3 compiler/docs_gen/docs_gen.py --all-stdlib --index > docs/language/gen/stdlib_index.md

# Omit source line references
python3 compiler/docs_gen/docs_gen.py gui.ros --no-source
```

---

## Extracting Stdlib Sources

The stdlib `.ros` files (`gui.ros`, `math.ros`, `io.ros`) are embedded as C string
literals in `c_files/src/shell.c`. To use the docs generator on them, extract them
from a running OS instance, or copy them out of the `LIB_GUI_ROS`, `LIB_MATH_ROS`,
and `LIB_IO_ROS` constants in `shell.c` into `compiler/stdlib/`.

A helper script is provided:

```bash
python3 compiler/docs_gen/extract_stdlib.py c_files/src/shell.c compiler/stdlib
```

After extraction, run:

```bash
python3 compiler/docs_gen/docs_gen.py --all-stdlib --out-dir docs/language/gen
```

---

## Output Structure

Each module produces:

1. **Title** — `# <name>.ros — API Reference`
2. **Function index table** — name, return type, one-line description
3. **Per-function entries** — full signature, parameters table, return type, full doc text, source line

---

## Integration with Build System

Add a CMake custom target to regenerate docs on every build:

```cmake
find_program(PYTHON3 python3)
if(PYTHON3)
    add_custom_target(docs
        COMMAND ${PYTHON3}
            ${CMAKE_SOURCE_DIR}/compiler/docs_gen/docs_gen.py
            --all-stdlib
            --stdlib-dir ${CMAKE_SOURCE_DIR}/compiler/stdlib
            --out-dir    ${CMAKE_SOURCE_DIR}/docs/language/gen
        COMMENT "Generating .ros language docs"
    )
endif()
```

Then run: `ninja docs` (or `make docs`).
