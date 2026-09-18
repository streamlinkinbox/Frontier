#!/usr/bin/env bash
# -Explode: write every payload a project embeds back out as its own file, and leave sibling references behind.
#
#    usage: bash Tools/Scripts/ExplodeProject.sh <file.projectspace> [out-dir]
#
# The other half of PackProject.sh (see its note on why these are wrappers and not reimplementations): the exploded
#    container is written INTO the payload directory, because a sibling reference means "next to the file that names it" —
#    a container written anywhere else points at a directory that is not there.
#
# Exit codes: 0 = every input exploded; 1 = one refused; 2 = no input.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

if [ "$#" -eq 0 ]; then
    echo "usage: bash Tools/Scripts/ExplodeProject.sh <file.projectspace> [out-dir]"
    exit 2
fi

File="$1"
FileDirectory="$(dirname "$File")"
Out="${2:-$FileDirectory/Content}"

if [ ! -f "$File" ]; then echo "[ExplodeProject] no such file: $File"; exit 1; fi
mkdir -p "$Out" || exit 1

Tool="Build/SpaceTool"
if [ ! -x "$Tool" ]; then
    # Shared with PackProject.sh: build once, use twice.
    bash Tools/Scripts/PackProject.sh --build-only || exit 1
fi
if [ ! -x "$Tool" ]; then echo "[ExplodeProject] $Tool is not built — run Tools/Scripts/PackProject.sh once"; exit 1; fi

"$Tool" -Explode="$File" -Out="$Out" || exit 1
echo "[ExplodeProject] $File → $Out"
