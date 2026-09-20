#!/usr/bin/env python3
"""PreToolUse guard — hard-blocks agent attempts to modify the locked rule files.

Locked paths (relative to the project root):
    CLAUDE.md
    RULES/**
    .claude/settings.json
    .claude/hooks/**
    .githooks/**

Claude Code pipes hook JSON to stdin, e.g.
    {"tool_name": "Edit", "tool_input": {"file_path": "CLAUDE.md", ...}}
    {"tool_name": "Bash", "tool_input": {"command": "rm CLAUDE.md", ...}}

Exit codes: 0 = allow, 2 = BLOCK (stderr is fed back to the agent as the reason).

These files belong to the user. Agents read them; they never write them. If a rule seems
wrong or blocking, the agent must tell the user instead of touching the files.
"""

import json
import os
import re
import sys

PROJECT_ROOT = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()

LOCKED_FILES = ("claude.md",)
LOCKED_PREFIXES = ("rules/", ".claude/settings.json", ".claude/hooks/", ".githooks/")

# A Bash command mentioning a locked path is blocked if it also does any of these.
WRITE_VERBS = re.compile(
    r"(?:^|[\s;&|(])(?:rm|rmdir|mv|cp|dd|truncate|touch|chmod|chown|chattr|tee|patch|"
    r"shred|ln|mkfifo|install|rename|split|csplit|python(?:[0-9.]+)?|perl|node|ruby)(?:\s|$)",
    re.IGNORECASE,
)
SED_INPLACE = re.compile(r"\bsed\b[^|;&]*\s(-i\b|--in-place)", re.IGNORECASE)
GIT_WRITE = re.compile(
    r"\bgit\s+(?:rm|mv|checkout|restore|clean|apply|am)(?:\s|$)"
    r"|\bgit\s+update-index\b",
    re.IGNORECASE,
)
HOOKS_REPOINT = re.compile(r"\bhooksPath\b", re.IGNORECASE)
NO_VERIFY = re.compile(r"\bgit\s+commit\b[^|;&]*(?:--no-verify|\s-n\s|\s-n$)", re.IGNORECASE)
# `>` / `>>` at the start of a token (not `2>`, not `->`, not `>=`), or attached as in `x>file`.
REDIRECTION = re.compile(
    r"(?:^|[\s;&|(])>{1,2}(?![=&])|[^0-9\s>&|;(](?<![-=])>{1,2}(?!>)",
    re.IGNORECASE,
)


def normalize(path):
    """Return the project-relative, lowercased path, or None if outside the project."""
    path = path.strip().strip('"').strip("'")
    path = path.replace("\\", "/")
    if not os.path.isabs(path):
        candidate = os.path.normpath(os.path.join(PROJECT_ROOT, path))
    else:
        candidate = os.path.normpath(path)
    try:
        rel = os.path.relpath(candidate, PROJECT_ROOT)
    except ValueError:  # different drive on Windows
        return None
    rel = rel.replace("\\", "/")
    while rel.startswith("./"):
        rel = rel[2:]
    if rel.startswith("../"):
        return None  # outside the project; not our concern here
    return rel.lower()


def is_locked(rel):
    if not rel:
        return False
    if rel in LOCKED_FILES:
        return True
    return any(rel == pref or rel.startswith(pref) for pref in LOCKED_PREFIXES)


BLOCK_MSG = (
    "\U0001F512 LOCKED RULES: {path} is user-owned and must not be modified, moved, "
    "recreated or deleted by an agent.\n"
    "You may READ it freely. If you believe a rule is wrong, missing, or blocking correct "
    "work: STOP, quote the rule, and ask the user. Only the user edits locked files.\n"
    "Do not attempt a workaround (no bash redirection, no --no-verify, no lookalike file, "
    "no editing the guard)."
)


def check_file_tool(tool_name, tool_input):
    for key in ("file_path", "notebook_path", "path"):
        target = tool_input.get(key)
        if target:
            rel = normalize(target)
            if is_locked(rel):
                sys.stderr.write(BLOCK_MSG.format(path=target))
                return 2
    return 0


def check_bash(tool_input):
    command = (tool_input.get("command") or "")
    lowered = command.lower()
    # Re-pointing the git hooks or bypassing them is blocked even when no locked
    # path is spelled out — both exist only to defeat the rules guard.
    if HOOKS_REPOINT.search(command):
        sys.stderr.write(
            "\U0001F512 LOCKED RULES: reconfiguring core.hooksPath would disable the "
            "user's rules guard. Ask the user instead."
        )
        return 2
    if NO_VERIFY.search(command):
        sys.stderr.write(
            "\U0001F512 LOCKED RULES: --no-verify bypasses the user's rules guard. "
            "If a hook is blocking your commit, the commit touches locked files or is "
            "otherwise invalid \u2014 fix the content or ask the user. The user runs "
            "--no-verify themselves when they intend it."
        )
        return 2
    mentions = ("claude.md" in lowered) or any(pref in lowered for pref in LOCKED_PREFIXES)
    if not mentions:
        return 0
    if (
        WRITE_VERBS.search(command)
        or SED_INPLACE.search(command)
        or GIT_WRITE.search(command)
        or REDIRECTION.search(command)
    ):
        sys.stderr.write(BLOCK_MSG.format(path="a locked rule file (mentioned in this command)"))
        return 2
    return 0


def main():
    try:
        payload = json.load(sys.stdin)
    except Exception:
        return 0  # never break the session on malformed input
    tool_name = payload.get("tool_name", "")
    tool_input = payload.get("tool_input") or {}
    if tool_name in ("Edit", "Write", "MultiEdit", "NotebookEdit"):
        return check_file_tool(tool_name, tool_input)
    if tool_name == "Bash":
        return check_bash(tool_input)
    return 0


if __name__ == "__main__":
    sys.exit(main())
