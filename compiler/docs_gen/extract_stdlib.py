#!/usr/bin/env python3
"""
extract_stdlib.py — Extract embedded stdlib .ros sources from shell.c
======================================================================

Reads the LIB_GUI_ROS, LIB_MATH_ROS, and LIB_IO_ROS C string constants
from c_files/src/shell.c and writes them as proper .ros files so that
docs_gen.py can process them.

Usage:
    python3 compiler/docs_gen/extract_stdlib.py [shell_c] [out_dir]

    shell_c : path to c_files/src/shell.c  (default: c_files/src/shell.c)
    out_dir : output directory              (default: compiler/stdlib)
"""

import re
import sys
from pathlib import Path

WORKSPACE = Path(__file__).parent.parent.parent   # project root

CONSTANTS = {
    "LIB_GUI_ROS":  "gui.ros",
    "LIB_MATH_ROS": "math.ros",
    "LIB_IO_ROS":   "io.ros",
}


def unescape_c_string(s: str) -> str:
    """Convert C escape sequences in a string literal to actual characters."""
    result = []
    i = 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            esc = s[i + 1]
            if   esc == "n":  result.append("\n")
            elif esc == "t":  result.append("\t")
            elif esc == "\\":  result.append("\\")
            elif esc == '"':  result.append('"')
            elif esc == "r":  result.append("\r")
            elif esc == "0":  result.append("\0")
            else:
                result.append("\\")
                result.append(esc)
            i += 2
        else:
            result.append(s[i])
            i += 1
    return "".join(result)


def extract_c_string_literal(source: str, var_name: str) -> str | None:
    """
    Find a C variable definition like:
        static const char NAME[] = "..." "..." ... ;
    and concatenate all the string fragments.
    """
    # Find 'static const char NAME[] =' or 'const char NAME[] ='
    pattern = re.compile(
        rf"(?:static\s+)?const\s+char\s+{re.escape(var_name)}\s*\[\s*\]\s*=\s*",
        re.MULTILINE,
    )
    m = pattern.search(source)
    if not m:
        return None

    start = m.end()
    # From 'start' collect all "..." fragments until we see ';'
    fragment_re = re.compile(r'"((?:[^"\\]|\\.)*)"', re.DOTALL)
    result_parts: list[str] = []
    pos = start
    while pos < len(source):
        # skip whitespace and newlines
        while pos < len(source) and source[pos] in " \t\r\n":
            pos += 1
        if pos >= len(source) or source[pos] == ";":
            break
        fm = fragment_re.match(source, pos)
        if fm:
            result_parts.append(unescape_c_string(fm.group(1)))
            pos = fm.end()
        else:
            # Unexpected character — stop
            break

    return "".join(result_parts) if result_parts else None


def main():
    shell_c = Path(sys.argv[1]) if len(sys.argv) > 1 else WORKSPACE / "c_files" / "src" / "shell.c"
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else WORKSPACE / "compiler" / "stdlib"

    if not shell_c.exists():
        print(f"error: {shell_c} not found", file=sys.stderr)
        sys.exit(1)

    out_dir.mkdir(parents=True, exist_ok=True)
    source = shell_c.read_text(encoding="utf-8", errors="replace")

    any_written = False
    for var_name, filename in CONSTANTS.items():
        content = extract_c_string_literal(source, var_name)
        if content is None:
            print(f"[extract_stdlib] warning: {var_name} not found in {shell_c}", file=sys.stderr)
            continue
        out_path = out_dir / filename
        out_path.write_text(content, encoding="utf-8")
        lines = content.count("\n")
        print(f"[extract_stdlib] wrote {out_path}  ({lines} lines)")
        any_written = True

    if not any_written:
        print("[extract_stdlib] no stdlib constants extracted — check variable names", file=sys.stderr)
        sys.exit(1)

    print(f"\nDone. Run docs_gen.py to generate Markdown:")
    print(f"  python3 compiler/docs_gen/docs_gen.py --stdlib-dir {out_dir} --all-stdlib --out-dir docs/language/gen")


if __name__ == "__main__":
    main()
