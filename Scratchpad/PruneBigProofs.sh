#!/usr/bin/env bash
# Session-branch proof policy: this branch carries every proof PNG up to 1 MB. Larger proofs (the 2560x1600 script
# renders, 1-6 MB each) stay on the source branch SultanAladin/Frontier- arena/01a0b490-frontier, because a full
# transplant of them is ~200 MB and every full suite run regenerates them. Run this after the suite, before committing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../Editor/EditorTools/ParametricSketcher" && pwd)"
cd "$(git rev-parse --show-toplevel)"

Pruned=0
while IFS= read -r Path; do
    [ -n "$Path" ] || continue
    git rm --cached -q "$Path" 2>/dev/null || true
    rm -f "$Path"
    Pruned=$((Pruned + 1))
done < <(find "$ROOT/Proofs" -type f -size +1000k -printf '%P\n' | sed "s|^|${ROOT#$PWD/}/Proofs/|")

echo "── pruned $Pruned proof(s) over 1 MB; $(find "$ROOT/Proofs" -type f | wc -l) proofs remain, $(du -sh "$ROOT/Proofs" | cut -f1)"
