#!/usr/bin/env python3
#============================================================================================================================================
#                                                 STAGEATROUSDENOISE.PY
#============================================================================================================================================
# 🧩 The four mechanical substitutions that turn Engine/Shaders/AtrousDenoise.slang into compilable C++, each one asserted
#    against the shader text so a reformatted shader fails loudly instead of being silently mis-ported:
#
#      ① drop the two prologue lines  — `#version 460` and `layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;`
#      ② the B₃ kernel's GLSL array constructor → a brace initialiser, the five literals carried over verbatim
#      ③ `layout(push_constant) uniform DenoiseConstants` → `struct DenoiseConstants`
#      ④ the block's closing `};` → `} DenoiseParameters;`, so the fields are an instance the mirror can write
#
#    Writes into $DO_STAGE: AtrousDenoise.cpu.1.h (through the push-constant block), AtrousDenoise.cpu.2.h (the rest),
#    and transform.manifest (what was dropped/rewritten, printed by the callers). The M9 proof re-derives all four from
#    the shader text itself (§C0) and compares the staged halves byte for byte — so this script's assertions and the
#    proof's re-derivation are two independent checks of the same transform.
#
#    Callers: CheckMaterialDenoise.sh (the gate) and RunDenoiseExhibit.sh (the exhibit sheets).
import os
import re
import sys

stage = os.environ.get("DO_STAGE")
if not stage:
    sys.stderr.write("DO_STAGE is not set\n")
    sys.exit(2)

source = "Engine/Shaders/AtrousDenoise.slang"
lines = open(source, encoding="utf-8").read().split("\n")

prologue = [i for i, l in enumerate(lines) if l.startswith("#") or l.startswith("layout(local_size")]
assert len(prologue) == 2, f"expected 2 prologue lines (#version + the workgroup size), found {len(prologue)}"
assert lines[prologue[0]].startswith("#version") and lines[prologue[1]].startswith("layout(local_size"), "unexpected prologue"
array_lines = [i for i, l in enumerate(lines) if "float[5](" in l]
assert len(array_lines) == 1, f"expected 1 GLSL array constructor, found {len(array_lines)}"
closers = [i for i, l in enumerate(lines) if l.strip() == "};"]
push = [i for i, l in enumerate(lines) if "push_constant" in l]
assert len(closers) == 1 and len(push) == 1 and closers[0] > push[0], "push-constant block not found as a single '};'"
assert lines[push[0]] == "layout(push_constant) uniform DenoiseConstants", f"unexpected push-constant opener: {lines[push[0]]!r}"
split = closers[0]
array = array_lines[0]

values = re.findall(r"-?\d+\.?\d*", lines[array].split("float[5](")[1])
assert len(values) == 5, f"array constructor has {len(values)} literals"
replacement = "    const float Weights[5] = float[5](" + ", ".join(values) + ");"

drop = set(prologue)
body = [l for i, l in enumerate(lines) if i not in drop]
# Re-index after the prologue removal, then rewrite the array line and close the push block with its instance.
shift = sum(1 for i in prologue if i < array)
shift_push = sum(1 for i in prologue if i < push[0])
shift_close = sum(1 for i in prologue if i < split)
body[array - shift] = "    const float Weights[5] = { " + ", ".join(v + "f" for v in values) + " };"
body[push[0] - shift_push] = "struct DenoiseConstants"
body[split - shift_close] = body[split - shift_close].replace("};", "} DenoiseParameters;")

open(os.path.join(stage, "AtrousDenoise.cpu.1.h"), "w", encoding="utf-8").write("\n".join(body[:split - shift_close + 1]) + "\n")
open(os.path.join(stage, "AtrousDenoise.cpu.2.h"), "w", encoding="utf-8").write("\n".join(body[split - shift_close + 1:]))
open(os.path.join(stage, "transform.manifest"), "w", encoding="utf-8").write(
    f"source {source}\n"
    f"dropped {lines[prologue[0]]}\n"
    f"dropped {lines[prologue[1]]}\n"
    f"rewrote {lines[array].strip()}\n"
    f"        -> {body[array - shift].strip()}\n"
    f"rewrote {lines[push[0]].strip()}\n"
    f"        -> {body[push[0] - shift_push].strip()}\n"
    f"split after {body[split - shift_close].strip()}\n")
print("[StageAtrousDenoise] staged " + stage)
