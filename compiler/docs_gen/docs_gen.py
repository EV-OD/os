#!/usr/bin/env python3
"""
docs_gen.py — RandomOS Language Documentation Generator
========================================================

Parses one or more .ros source files, extracts doc-comment blocks (triple-slash
`///` lines immediately above `fn` declarations), and emits a Markdown API
reference document.

Usage
-----
    # Single file
    python3 docs_gen.py /path/to/file.ros

    # Multiple files
    python3 docs_gen.py gui.ros math.ros io.ros

    # All stdlib files written to /lib/ros/ (requires a mounted image or host copy)
    python3 docs_gen.py --all-stdlib

    # Specify a stdlib root directory
    python3 docs_gen.py --stdlib-dir ./extracted/lib/ros

    # Redirect output to a file
    python3 docs_gen.py gui.ros > docs/language/gen/gui_api.md

    # Generate index page for all stdlib modules
    python3 docs_gen.py --index --stdlib-dir ./extracted/lib/ros > docs/language/gen/index.md

Options
-------
    --all-stdlib          Process gui.ros, math.ros, io.ros from ./compiler/stdlib/
    --stdlib-dir <dir>    Override stdlib source directory (default: compiler/stdlib)
    --out-dir <dir>       Write one .md file per module instead of stdout
    --title <text>        Override the top-level heading
    --no-source           Omit the source snippet in each function entry
    -h / --help           Show this help message
"""

import os
import sys
import re
import argparse
import textwrap
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Default location of stdlib .ros sources relative to the workspace root
# (these are the embedded strings from shell.c, optionally extracted here)
# ---------------------------------------------------------------------------
DEFAULT_STDLIB_DIR = Path(__file__).parent.parent / "stdlib"
STDLIB_MODULES = ["gui", "math", "io"]


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

class Param:
    def __init__(self, name: str, typ: str):
        self.name = name
        self.typ = typ

    def __repr__(self):
        return f"{self.name}: {self.typ}"


class FnDoc:
    def __init__(self):
        self.name: str = ""
        self.params: list[Param] = []
        self.return_type: Optional[str] = None
        self.doc_lines: list[str] = []
        self.source_line: int = 0

    @property
    def signature(self) -> str:
        params_str = ", ".join(repr(p) for p in self.params)
        ret = f" -> {self.return_type}" if self.return_type else ""
        return f"fn {self.name}({params_str}){ret}"

    @property
    def doc(self) -> str:
        return " ".join(self.doc_lines).strip()


class ModuleDoc:
    def __init__(self, name: str, path: str):
        self.name = name
        self.path = path
        self.module_doc: list[str] = []   # top-of-file //! comments
        self.functions: list[FnDoc] = []


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

_PARAM_RE = re.compile(
    r"(\w+)\s*:\s*(i32|u32|bool|str|\*\w+)"
)
_FN_RE = re.compile(
    r"^\s*fn\s+(\w+)\s*\(([^)]*)\)\s*(?:->\s*(\w+|\*\w+))?\s*\{"
)
_DOC_COMMENT_RE = re.compile(r"^\s*///\s?(.*)")
_MODULE_DOC_RE  = re.compile(r"^\s*//!\s?(.*)")
_LINE_COMMENT_RE = re.compile(r"^\s*//[^/!]")


def parse_ros_file(path: str | Path) -> ModuleDoc:
    """Parse a .ros file and return a ModuleDoc with all documented functions."""
    path = Path(path)
    name = path.stem
    mod = ModuleDoc(name=name, path=str(path))

    try:
        source = path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        print(f"[docs_gen] warning: file not found: {path}", file=sys.stderr)
        return mod

    lines = source.splitlines()
    pending_doc: list[str] = []

    for lineno, line in enumerate(lines, start=1):
        # Module-level doc comment (//!)
        m = _MODULE_DOC_RE.match(line)
        if m:
            mod.module_doc.append(m.group(1).strip())
            pending_doc = []
            continue

        # Doc comment (///)
        m = _DOC_COMMENT_RE.match(line)
        if m:
            pending_doc.append(m.group(1).strip())
            continue

        # Function declaration
        m = _FN_RE.match(line)
        if m:
            fn = FnDoc()
            fn.name = m.group(1)
            fn.return_type = m.group(3) if m.group(3) else None
            fn.source_line = lineno
            fn.doc_lines = pending_doc[:]

            # Parse parameters
            raw_params = m.group(2).strip()
            if raw_params:
                for pm in _PARAM_RE.finditer(raw_params):
                    fn.params.append(Param(pm.group(1), pm.group(2)))

            mod.functions.append(fn)
            pending_doc = []
            continue

        # Any non-comment, non-blank line resets the pending doc buffer
        stripped = line.strip()
        if stripped and not _LINE_COMMENT_RE.match(line):
            pending_doc = []

    return mod


# ---------------------------------------------------------------------------
# Markdown renderer
# ---------------------------------------------------------------------------

def render_module(mod: ModuleDoc, include_source: bool = True) -> str:
    lines: list[str] = []

    # Title
    lines.append(f"# {mod.name}.ros — API Reference")
    lines.append("")
    lines.append(f"> Generated by `docs_gen.py` from `{mod.path}`")
    lines.append("")

    # Module doc
    if mod.module_doc:
        lines.append(" ".join(mod.module_doc))
        lines.append("")

    if not mod.functions:
        lines.append("_No documented functions found._")
        return "\n".join(lines)

    # Summary table
    lines.append("## Function Index")
    lines.append("")
    lines.append("| Function | Returns | Description |")
    lines.append("|----------|---------|-------------|")
    for fn in mod.functions:
        ret = f"`{fn.return_type}`" if fn.return_type else "void"
        desc = fn.doc.split(".")[0][:80] if fn.doc else "—"
        lines.append(f"| [`{fn.name}`](#{fn.name.lower()}) | {ret} | {desc} |")
    lines.append("")

    # Per-function reference
    lines.append("---")
    lines.append("")
    lines.append("## Reference")
    lines.append("")

    for fn in mod.functions:
        lines.append(f"### `{fn.name}`")
        lines.append("")
        lines.append("```ros")
        lines.append(fn.signature)
        lines.append("```")
        lines.append("")

        # Parameters table
        if fn.params:
            lines.append("**Parameters:**")
            lines.append("")
            lines.append("| Name | Type | Description |")
            lines.append("|------|------|-------------|")
            # Try to extract per-param docs from the function doc comment
            # Recognise lines like "name — description" or "name: description"
            param_docs: dict[str, str] = {}
            for doc_line in fn.doc_lines:
                for pm in fn.params:
                    pattern = re.compile(
                        rf"\b{re.escape(pm.name)}\s*[:\-–—]\s*(.+)", re.IGNORECASE
                    )
                    hit = pattern.search(doc_line)
                    if hit:
                        param_docs[pm.name] = hit.group(1).strip()
            for pm in fn.params:
                desc = param_docs.get(pm.name, "")
                lines.append(f"| `{pm.name}` | `{pm.typ}` | {desc} |")
            lines.append("")

        # Return value
        if fn.return_type:
            lines.append(f"**Returns:** `{fn.return_type}`")
            lines.append("")

        # Doc text
        if fn.doc:
            # Strip any per-param lines already shown, keep the rest
            main_doc = []
            for doc_line in fn.doc_lines:
                is_param_line = any(
                    re.search(rf"\b{re.escape(p.name)}\s*[:\-–—]", doc_line)
                    for p in fn.params
                )
                if not is_param_line:
                    main_doc.append(doc_line)
            if main_doc:
                lines.append(" ".join(main_doc))
                lines.append("")

        if include_source:
            lines.append(
                f"_Defined at line {fn.source_line} in `{Path(mod.path).name}`_"
            )
            lines.append("")

        lines.append("---")
        lines.append("")

    return "\n".join(lines)


def render_index(modules: list[ModuleDoc]) -> str:
    """Render a combined index page listing all modules and their functions."""
    lines: list[str] = []
    lines.append("# RandomOS Standard Library — Full Index")
    lines.append("")
    lines.append("> Generated by `docs_gen.py`")
    lines.append("")

    for mod in modules:
        lines.append(f"## `import {mod.name}` — `{mod.path}`")
        lines.append("")
        if mod.module_doc:
            lines.append(" ".join(mod.module_doc))
            lines.append("")
        if mod.functions:
            lines.append("| Function | Signature | Description |")
            lines.append("|----------|-----------|-------------|")
            for fn in mod.functions:
                sig = fn.signature.replace("|", "\\|")
                desc = fn.doc.split(".")[0][:70] if fn.doc else "—"
                lines.append(f"| `{fn.name}` | `{sig}` | {desc} |")
        else:
            lines.append("_No documented functions._")
        lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate Markdown docs from .ros source files",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""\
            Examples:
              python3 docs_gen.py gui.ros math.ros
              python3 docs_gen.py --all-stdlib
              python3 docs_gen.py --stdlib-dir ./ros_sources --out-dir ./gen
        """),
    )

    parser.add_argument(
        "files",
        nargs="*",
        metavar="FILE",
        help=".ros source files to process",
    )
    parser.add_argument(
        "--all-stdlib",
        action="store_true",
        help=f"Process all stdlib modules from stdlib dir",
    )
    parser.add_argument(
        "--stdlib-dir",
        metavar="DIR",
        default=str(DEFAULT_STDLIB_DIR),
        help=f"Directory containing stdlib .ros files (default: {DEFAULT_STDLIB_DIR})",
    )
    parser.add_argument(
        "--out-dir",
        metavar="DIR",
        help="Write one <module>.md per input file instead of printing to stdout",
    )
    parser.add_argument(
        "--index",
        action="store_true",
        help="Emit a combined index page instead of per-module pages",
    )
    parser.add_argument(
        "--title",
        metavar="TEXT",
        help="Override the top-level heading",
    )
    parser.add_argument(
        "--no-source",
        action="store_true",
        help="Omit source line references from output",
    )

    args = parser.parse_args()

    # Collect files to process
    ros_files: list[Path] = []

    if args.all_stdlib:
        stdlib_dir = Path(args.stdlib_dir)
        for name in STDLIB_MODULES:
            p = stdlib_dir / f"{name}.ros"
            if p.exists():
                ros_files.append(p)
            else:
                print(
                    f"[docs_gen] warning: stdlib file not found: {p}",
                    file=sys.stderr,
                )
        if not ros_files:
            print(
                f"[docs_gen] error: no stdlib files found in {args.stdlib_dir}\n"
                f"  Hint: extract them with:  rosc --dump-stdlib {args.stdlib_dir}",
                file=sys.stderr,
            )
            sys.exit(1)

    for f in args.files:
        ros_files.append(Path(f))

    if not ros_files:
        parser.print_help()
        sys.exit(0)

    # Parse all files
    modules = [parse_ros_file(f) for f in ros_files]

    include_source = not args.no_source

    # Output
    if args.index:
        output = render_index(modules)
        if args.out_dir:
            out = Path(args.out_dir) / "index.md"
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(output, encoding="utf-8")
            print(f"[docs_gen] wrote {out}", file=sys.stderr)
        else:
            print(output)
        return

    if args.out_dir:
        out_dir = Path(args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        for mod in modules:
            text = render_module(mod, include_source=include_source)
            out = out_dir / f"{mod.name}.md"
            out.write_text(text, encoding="utf-8")
            print(f"[docs_gen] wrote {out}", file=sys.stderr)
    else:
        # Single stdout stream — emit all modules separated by a horizontal rule
        parts = []
        for mod in modules:
            parts.append(render_module(mod, include_source=include_source))
        print("\n\n---\n\n".join(parts))


if __name__ == "__main__":
    main()
