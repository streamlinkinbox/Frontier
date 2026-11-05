#!/bin/bash
# 🧩 Does Tools/TlasSkip.test.mjs notice a broken skip? Each mutant is one exact edit of the skip's own code in Source/RayTracingSpace.js (the key's
#    bounds, centroid, occupant and BLAS root; the leaf count; the buffer the upload went to; what turning the skip off or forcing a build forgets). The
#    suite is run against the mutant; a mutant that leaves it green is a survivor and is reported as one.
#
#      Exhibits/VehicleHmiPrototype/MutateTlasSkip.sh                 (about 20 s; writes Exhibits/VehicleHmiPrototype/tlas-skip-mutations.txt)
#
#    The edits are made to a copy of Source/ and the suite in a temporary folder, never to the repository: a run that is stopped halfway leaves nothing
#    to restore. Needs node only.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="$ROOT/Experimental/VehicleHmiPrototype"
RESULT="${1:-$ROOT/Exhibits/VehicleHmiPrototype/tlas-skip-mutations.txt}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/Tools"
[ -d "$DEMO/node_modules" ] && ln -s "$DEMO/node_modules" "$WORK/node_modules"
cp "$DEMO/Tools/TlasSkip.test.mjs" "$WORK/Tools/"
cp "$DEMO/package.json" "$WORK/" 2>/dev/null || true

# name | the exact text it replaces | what stands there instead
python3 - "$DEMO/Source/RayTracingSpace.js" "$WORK" <<'PYTHON' > "$WORK/mutants.tsv"
import json, sys
Source = open(sys.argv[1]).read()
# 📝 The key compares nine numbers per leaf (six bounds, three centroid components), the occupant and its BLAS root; the ground leaf's bounds are read
#    from the scene on their own line. Each comparison is dropped, and each bound's is also made blind to NaN, one at a time.
Bounds = [("LowerBoundX", "Bounds[B] !== Min0"),     ("LowerBoundY", "Bounds[B + 1] !== Min1"), ("LowerBoundZ", "Bounds[B + 2] !== Min2"),
          ("UpperBoundX", "Bounds[B + 3] !== Max0"), ("UpperBoundY", "Bounds[B + 4] !== Max1"), ("UpperBoundZ", "Bounds[B + 5] !== Max2")]
Centroids = [("CentroidX", "Bounds[B + 6] !== Cen0"), ("CentroidY", "Bounds[B + 7] !== Cen1"), ("CentroidZ", "Bounds[B + 8] !== Cen2")]
Mutants = [(Name + "Dropped", Text + " ||", "false ||") for Name, Text in Bounds + Centroids]
for Name, Text in Bounds:
    Stored, Value = Text.split(" !== ")
    Mutants.append((Name + "BlindToNaN", Text + " ||", "Math.abs(" + Stored + " - " + Value + ") > 0 ||"))
for Name, Old in [("GroundLowerX", "Min0 = Box.Minimum[0];"), ("GroundLowerY", "Min1 = Box.Minimum[1];"), ("GroundLowerZ", "Min2 = Box.Minimum[2];"),
                  ("GroundUpperX", "Max0 = Box.Maximum[0];"), ("GroundUpperY", "Max1 = Box.Maximum[1];"), ("GroundUpperZ", "Max2 = Box.Maximum[2];")]:
    Mutants.append((Name + "Unread", Old, Old.split(" = ")[0] + " = 0;"))
Mutants += [
    ("OccupantIndex",  "Identity[I] !== Occ ||",                                 ""),
    ("BlasRoot",       "|| Identity[I + 1] !== Root))",                          "))"),
    ("GroundIdentity", "Occ  = GroundIdx;",                                      "Occ  = 0;"),
    ("LeafCount",      "let Same = this.TlasKeyCount === Count;",                "let Same = true;"),
    ("BufferIdentity", "this.TlasUploadedBuffer === this.NodeBuffer",            "true"),
    ("NeverUploaded",  "Unchanged && this.NodeBuffer !== null &&",               "Unchanged &&"),
    ("SkipOffForgets", "if (!SkipAllowed)\n            this.TlasKeyCount = -1;", ""),
    ("ForcedRebuild",  " && !this.TlasForceRebuild;",                            ";"),
    ("FailedBuildMark","this.TlasUploadedBuffer = null;   // set again only by an upload", "// set again only by an upload"),
    ("KeyNeverStored", "this.TlasKeyCount = Count;\n        return Same;",       "return Same;"),
    ("SkipAlways",     "const Unchanged   = SkipAllowed && this.TlasInputsMatch(Scene, ObjectCount, HasGround, GroundIdx);", "const Unchanged   = SkipAllowed;"),
]
for Name, Old, New in Mutants:
    if Source.count(Old) != 1:
        sys.exit(f"{Name}: the text it replaces occurs {Source.count(Old)} times, not once")
    open(f"{sys.argv[2]}/{Name}.js", "w").write(Source.replace(Old, New))
    print(Name)
PYTHON
[ -s "$WORK/mutants.tsv" ] || { echo "no mutants written"; exit 2; }

run_suite()   # $1: the RayTracingSpace.js to test
{
    rm -rf "$WORK/Source"
    cp -r "$DEMO/Source" "$WORK/Source"
    cp "$1" "$WORK/Source/RayTracingSpace.js"
    ( cd "$WORK/Tools" && node --test TlasSkip.test.mjs 2>&1 )
}

{
    echo "MutateTlasSkip.sh: one edit of the skip's key or its guards at a time, against Tools/TlasSkip.test.mjs"
    OUT="$(run_suite "$DEMO/Source/RayTracingSpace.js")"
    PASSES="$(echo "$OUT" | grep -E '^# pass ' | awk '{print $3}')"
    FAILS="$(echo "$OUT" | grep -E '^# fail ' | awk '{print $3}')"
    echo "unmutated: ${PASSES:-?} pass, ${FAILS:-?} fail"
    [ "${FAILS:-1}" = "0" ] || { echo "the unmutated suite is not green: nothing to compare"; exit 1; }
    Caught=0; Total=0; Survivors=""
    while read -r Name; do
        Total=$((Total + 1))
        OUT="$(run_suite "$WORK/$Name.js")"
        FAILED="$(echo "$OUT" | grep -E '^# fail ' | awk '{print $3}')"
        if [ "${FAILED:-0}" != "0" ]; then
            Caught=$((Caught + 1))
            FIRST="$(echo "$OUT" | grep -E '^not ok ' | head -1 | sed 's/^not ok [0-9]* - //')"
            printf 'caught    %-16s %s test(s) fail, the first: %s\n' "$Name" "$FAILED" "$FIRST"
        else
            Survivors="$Survivors $Name"
            printf 'SURVIVED  %-16s the suite stays green\n' "$Name"
        fi
    done < "$WORK/mutants.tsv"
    echo "caught $Caught of $Total${Survivors:+; survivors:$Survivors}"
} | tee "$RESULT"
