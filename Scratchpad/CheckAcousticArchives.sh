#!/usr/bin/env bash
# Phase A acoustics: the editor and the car archives are browser/JS assets with nothing to link, so what is
#    checked here is what a bad merge or a hand edit actually breaks — the schema agreeing with the data.
#
# The field-count check is the one that matters. The C++ port (References/AcousticPhaseA-CppPortPlan.md) sizes its
#    field sheet from the schema; a sheet shorter than the archives would silently load the missing fields as
#    defaults and never report it. That was already wrong once: the plan said 71, the schema and archives carry 77.
#
# The DSP itself cannot be judged here — it needs a browser AudioContext:
#    node Scratchpad/AcousticEditorServe.js 8080
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

Failed=0

if command -v node >/dev/null 2>&1; then
    for Module in Scratchpad/AcousticEditor*.js Scratchpad/AcousticLaFerrariReference.js; do
        [ -f "$Module" ] || continue
        node --check "$Module" >/dev/null 2>&1 || { echo "  PARSE FAILED: $Module"; Failed=1; }
    done
    echo "  $(ls Scratchpad/Acoustic*.js 2>/dev/null | wc -l) acoustic JS modules parse"
else
    echo "  node absent - JS parse check skipped"
fi

python3 - <<'PY' || Failed=1
import glob, re, sys
try:
    import tomllib
except ImportError:
    print("  tomllib absent - archive check skipped"); sys.exit(0)

Editor = open('Tools/AudioEditor/index.html', encoding='utf-8', errors='replace').read()
Match  = re.search(r'ACOUSTIC_SCHEMA\s*=\s*\[(.*?)\n\s*\];', Editor, re.S)
if not Match:
    print("  SCHEMA NOT FOUND in Tools/AudioEditor/index.html"); sys.exit(1)
Schema = {f"{s}.{k}" for s, k in re.findall(r"\[\s*'([a-zA-Z]+)'\s*,\s*'([A-Za-z0-9_]+)'", Match.group(1))}
print(f"  editor schema: {len(Schema)} fields")

Archives = sorted(glob.glob('EngineContent/AudioArchives/*/*.toml'))
if not Archives:
    print("  NO CAR ARCHIVES FOUND"); sys.exit(1)

Bad = 0
for Path in Archives:
    try:
        Document = tomllib.loads(open(Path, 'rb').read().decode())
    except Exception as Error:
        print(f"  PARSE FAILED {Path}: {Error}"); Bad = 1; continue
    Keys = set()
    def Walk(Node, Prefix=''):
        for Key, Value in Node.items():
            if isinstance(Value, dict): Walk(Value, Prefix + Key + '.')
            else: Keys.add(Prefix + Key)
    Walk(Document)
    Missing = Schema - Keys
    Extra   = Keys - Schema
    Name = Path.split('/')[-1]
    if Missing or Extra:
        print(f"  {Name}: {len(Missing)} missing, {len(Extra)} unknown vs the editor schema"); Bad = 1
    else:
        print(f"  {Name:28s} {len(Keys)} fields, matches the schema exactly")
sys.exit(Bad)
PY
[ $? -eq 0 ] || Failed=1

[ $Failed -eq 0 ] && echo "[Acoustic] OK"
exit $Failed
