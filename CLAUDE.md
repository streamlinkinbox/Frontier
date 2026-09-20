# 🔒 Locked agent rules — read first, edit never

**STOP. This file and everything under `RULES/` is LOCKED.**

You (the coding agent) must NOT edit, rewrite, shorten, "improve", reorganise, translate,
summarise, move, or delete this file or `RULES/`. These rules were written by the user.
Deleting or modifying them is the single most harmful thing you can do in this repository.

This is **enforced, not requested**:

- `.claude/settings.json` — `permissions.deny` rejects Edit/Write tool calls on every locked path.
- `.claude/hooks/guard_locked_rules.py` — PreToolUse hook; hard-blocks Edit/Write/MultiEdit **and**
  Bash workarounds (`rm`, `mv`, `sed -i`, `>`, `git commit --no-verify`, `core.hooksPath`, …).
- `.githooks/pre-commit` — rejects any commit that touches a locked path.

If one of your tool calls gets blocked, that is the protection working. Do **not** try to route
around it (no bash redirection, no `--no-verify`, no copy-then-replace, no editing the guard,
no writing a lookalike file). Tell the user what you wanted to change and let them decide.
**Only the user may change locked files** (their editor works normally; they bypass with
`git commit --no-verify`).

---

## The three locked rules (full text: [`RULES/00-LOCKED-RULES.md`](RULES/00-LOCKED-RULES.md))

1. **C++ standard: C++20** — every configuration, every target. `/std:c++20` (MSVC) or
   `-std=c++20` (GCC/Clang). Never downgraded, never "temporarily" older.
2. **Use templated functions** — a shared algorithm is written **once**, as a template; per-type
   copy-paste forks are defects. Per-subject code is the differences; the walk is written once.
3. **Naming convention** — every name is constructed from its physical or mathematical mechanism.
   `RULES/SKILL-Naming.md` (verbatim from the Slate project) is the full authority: PascalCase,
   `<Subject><Role>` modules with a closed role-suffix list, banned words, no shorthand.

**Before your first line of C++ in any session, read:**
`RULES/00-LOCKED-RULES.md` and `RULES/SKILL-Naming.md`.

Everything outside these rule files (build system, project layout, task planning) is normal
project work and may be discussed and changed freely — the lock covers only the rule files above.
