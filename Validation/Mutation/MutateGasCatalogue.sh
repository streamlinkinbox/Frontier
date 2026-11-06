#!/bin/bash
#============================================================================================================================================
#                                                       MUTATEGASCATALOGUE.SH
#============================================================================================================================================
# 🧩 Mutation check of PyroVolumeValidation's comparison of the native presets with the prototype's own arithmetic. Each mutation edits the GENERATOR
#    (Tools/Build/GenerateGasPresets.py), regenerates the three headers from it, rebuilds the validation and runs it; a mutant it does not stop (no
#    assertion fails) SURVIVED. The generator and the three headers are saved first and put back after every mutant and on exit (a signal included),
#    and the last step regenerates them from the real generator and runs the control again, so the working tree ends as it began.
#
#        Validation/Mutation/MutateGasCatalogue.sh <build folder>
#
#    The build folder is a configured one with ninja; the run needs the repository root as its working directory (it reads Experimental/Fluid) and, for the
#    solver half, FRONTIER_VULKAN_LIB (the half that does not need a device is the one these mutants are aimed at).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$(cd "${1:?usage: MutateGasCatalogue.sh <build folder>}" && pwd)
export PATH="$HOME/.local/bin:$PATH"
cd "$ROOT" || exit 2
FILES=(Tools/Build/GenerateGasPresets.py Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h Frontier/Engine/VolumetricDynamics/GasSceneFields.inl Frontier/Engine/Interface/FluidEditor/FluidEditorControls.h)
SAVED=$(mktemp -d)
for File in "${FILES[@]}"; do mkdir -p "$SAVED/$(dirname "$File")"; cp "$File" "$SAVED/$File"; done
restore() { for File in "${FILES[@]}"; do cp "$SAVED/$File" "$ROOT/$File"; done; }
trap restore EXIT INT TERM

regenerate() { for Generator in GenerateGasPresets GenerateGasSceneCodec GenerateFluidEditorControls; do python3 "Tools/Build/$Generator.py" > /dev/null || return 1; done; }
validate() { ninja -C "$BUILD" PyroVolumeValidation > "$SAVED/build.log" 2>&1 || return 1; "$BUILD/PyroVolumeValidation" > "$SAVED/out.log" 2>&1; return 0; }

SURVIVORS=0
CAUGHT=0
run() { # name sed-expression ("" for the control)
  local Name=$1 Expr=$2
  restore
  if [ -n "$Expr" ]; then
    sed -i "$Expr" Tools/Build/GenerateGasPresets.py
    if cmp -s Tools/Build/GenerateGasPresets.py "$SAVED/Tools/Build/GenerateGasPresets.py"; then echo "MUTATION $Name: NO CHANGE (pattern missing)"; SURVIVORS=$((SURVIVORS + 1)); return; fi
    if ! regenerate; then echo "MUTATION $Name: the generator refused ($(python3 Tools/Build/GenerateGasPresets.py 2>&1 | tail -1))"; CAUGHT=$((CAUGHT + 1)); return; fi
  fi
  if ! validate; then echo "MUTATION $Name: DID NOT COMPILE (see $SAVED/build.log)"; SURVIVORS=$((SURVIVORS + 1)); return; fi
  local Failed
  Failed=$(grep -c '^FAIL' "$SAVED/out.log")
  if [ -z "$Expr" ]; then echo "CONTROL (the generator as committed): $(tail -1 "$SAVED/out.log")"; return; fi
  if [ "$Failed" -gt 0 ]; then echo "MUTATION $Name: caught ($Failed assertions failed: $(grep '^FAIL' "$SAVED/out.log" | cut -c1-96 | head -3 | tr '\n' '|'))"; CAUGHT=$((CAUGHT + 1))
  else echo "MUTATION $Name: SURVIVED"; SURVIVORS=$((SURVIVORS + 1)); fi
}

run Control ""
# 📝 The defaults taken from DEFAULT_PARAMS alone, as the c7egoist transcription did: every preset opens on another grid, quality and overlay.
run DefaultsFromDefaultParameters "s/^    for Key, Reading in ReadInitialOverrides(Specification.read_text(encoding='utf-8')).items():/    for Key, Reading in {}.items():/"
# 📝 voxelQuantization back among the counts: the Voxel fire preset's 0.88 becomes 0, and the reader refuses a fraction.
run VoxelQuantizationACount "s/    'SliceAxis', 'ObstacleType', 'RenderChannel', 'ColorPalette',\$/    'SliceAxis', 'ObstacleType', 'RenderChannel', 'ColorPalette', 'VoxelQuantization',/; s/^        if Declared\[Key\] == 'int32_t' and Fractional:/        if False:/"
# 📝 One of the seven overrides lost: the grid of the editor's opening settings.
run InitialGridLost "s/^        Defaults\[Key\] = Reading\$/        Defaults[Key] = Reading if Key != 'gridResolution' else Defaults[Key]/"
restore
regenerate && for Generator in GenerateGasPresets GenerateGasSceneCodec GenerateFluidEditorControls; do python3 "Tools/Build/$Generator.py" --check > /dev/null || { echo "the headers do not match their generators after the restore"; exit 1; }; done
run Control ""
echo "$CAUGHT mutants stopped, $SURVIVORS survived"
[ "$SURVIVORS" -eq 0 ]
