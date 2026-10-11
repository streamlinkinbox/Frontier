#!/usr/bin/env python3
"""Enforce Frontier file-directive widths on engine-bound sources.

Replaces marker lines (so authors never hand-count characters):
  //@@HDR: <title>  -> 142-char header rule + `// <title>` line
  //@@BAN: <title>  -> 122-char banner rule, centered title, 122-char rule

Usage: FormatDirectives.py [--check] <file>...
  --check: audit only (exit 1 if any rule line is off-width).
"""

import sys

HDR = "//" + "=" * 140
BAN = "//" + "-" * 120


def expand(lines):
    out = []
    for ln in lines:
        s = ln.rstrip("\n")
        if s.startswith("//@@HDR:"):
            out.append(HDR + "\n")
            out.append("// " + s[len("//@@HDR:"):].strip() + "\n")
        elif s.startswith("//@@BAN:"):
            title = s[len("//@@BAN:"):].strip()
            out.append(BAN + "\n")
            out.append("//" + title.center(120) + "\n")
            out.append(BAN + "\n")
        else:
            out.append(ln)
    return out


def audit(path, lines):
    bad = []
    for i, ln in enumerate(lines, 1):
        s = ln.rstrip("\n")
        if s.startswith("//===") or s.startswith("//---"):
            want = 142 if s.startswith("//===") else 122
            if len(s) != want:
                bad.append("%s:%d width %d want %d" % (path, i, len(s), want))
    return bad


def main(args):
    check = "--check" in args
    paths = [a for a in args if not a.startswith("--")]
    fail = []
    for p in paths:
        with open(p, encoding="utf-8") as f:
            lines = f.readlines()
        if not check:
            lines = expand(lines)
            with open(p, "w", encoding="utf-8") as f:
                f.writelines(lines)
        fail.extend(audit(p, lines))
    for b in fail:
        print("directive:", b)
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
