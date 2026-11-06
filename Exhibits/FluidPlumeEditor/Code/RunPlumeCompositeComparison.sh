#!/bin/bash
#============================================================================================================================================
#                                                       RUNPLUMECOMPOSITECOMPARISON.SH
#============================================================================================================================================
# 🧩 The plume picture's composite before its backdrop was kept, against the one in Frontier/Extensions/Plume/PlumeComposite.cpp: the bytes of every
#    picture compared, then the time of each (PlumeCompositeComparison.cpp). The earlier composite is cut out of an earlier commit's PlumeExtension.cpp
#    (from `constexpr FluidDynamics::Vector3 StageLight` to the end of its anonymous namespace) and compiled as it was; the repository is untouched, and
#    everything is built in <build folder>/plume-composite-comparison at -O2 with the flags of PlumeCompositeValidation's own compile.
#
#        Exhibits/FluidPlumeEditor/Code/RunPlumeCompositeComparison.sh <build folder> [earlier commit]
#
#    The build folder is a configured one (ninja -t commands gives the flags). The earlier commit is 2f66b41, the last before the composite moved into
#    PlumeComposite.cpp. The exit code is 0 when no picture differs.
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BUILD=$(cd "${1:?usage: RunPlumeCompositeComparison.sh <build folder> [earlier commit]}" && pwd)
EARLIER=${2:-2f66b41}
WORK="$BUILD/plume-composite-comparison"
mkdir -p "$WORK"

git -C "$ROOT" show "$EARLIER:Frontier/Extensions/Plume/PlumeExtension.cpp" > "$WORK/PlumeExtension.earlier.cpp"
python3 - "$WORK/PlumeExtension.earlier.cpp" "$WORK/OldPlumeComposite.inc" "$EARLIER" <<'PY'
import sys
lines = open(sys.argv[1], encoding='utf-8').read().split('\n')
start = next(i for i, line in enumerate(lines) if line.startswith('constexpr FluidDynamics::Vector3 StageLight'))
end = next(i for i, line in enumerate(lines) if i > start and line.startswith('} // namespace'))
open(sys.argv[2], 'w', encoding='utf-8').write('\n'.join(lines[start:end]) + '\n')
print('the earlier composite: lines %d to %d of PlumeExtension.cpp at %s' % (start + 1, end, sys.argv[3]))
PY

COMPILE=$(cd "$BUILD" && ninja -t commands PlumeCompositeValidation | grep 'PlumeCompositeValidation.cpp.o' | head -1)
FLAGS=$(printf '%s' "$COMPILE" | sed -e 's/^[^ ]* //' -e 's/ -MD .*$//' -e 's/ -g / /')
cd "$WORK"
eval "c++ -O2 $FLAGS -I$WORK \
  $ROOT/Exhibits/FluidPlumeEditor/Code/PlumeCompositeComparison.cpp \
  $ROOT/Frontier/Extensions/Plume/PlumeComposite.cpp \
  $ROOT/Frontier/Engine/Rendering/GlobalIllumination/TransformProjection.cpp \
  -o PlumeCompositeComparison"
./PlumeCompositeComparison
