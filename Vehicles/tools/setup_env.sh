#!/bin/bash
# One-shot environment for the vehicle pipeline (idempotent, ~2 min cold):
#   ~/.venv       python with numpy/scipy/matplotlib   (fit_curves.py, loft_shell.py, render_arc.py)
#   ~/.bpyenv     headless Blender (pip bpy) + stub display libs   (features.py)
#   ~/.solidarc   SolidArc console built from SultanAladin/Frontier- (sparse clone), binary at ~/.solidarc/build/SolidArc
set -e
H=$HOME; T=$(cd "$(dirname "$0")" && pwd)
[ -x $H/.venv/bin/python ] || { python3 -m venv $H/.venv; $H/.venv/bin/pip install -q numpy scipy matplotlib; }
[ -x $H/.bpyenv/bin/python ] && [ -d $H/.bpystubs ] || bash $T/setup_bpy.sh
if [ ! -x $H/.solidarc/build/SolidArc ]; then
  mkdir -p $H/.solidarc/build
  [ -d $H/.solidarc/src/.git ] || { git clone -q --filter=blob:none --sparse --depth 1 https://github.com/SultanAladin/Frontier-.git $H/.solidarc/src
    (cd $H/.solidarc/src && git sparse-checkout set Editor/AuthoringTools/Modelling/SolidArc); }
  cd $H/.solidarc/src/Editor/AuthoringTools/Modelling/SolidArc
  SRCS="Kernel/CurveSpecification.cpp Kernel/SurfaceSpecification.cpp Kernel/TopologySpecification.cpp Kernel/SkinSolver.cpp Kernel/IntersectionSolver.cpp Kernel/FairPatchSolver.cpp Kernel/ProfileSolver.cpp Kernel/ConstraintSolver.cpp Kernel/ConstraintGraph.cpp Kernel/MirrorSolver.cpp Kernel/BlendSolver.cpp Presentation/SoftwareRaster.cpp Presentation/ScenePresentation.cpp Interaction/CameraProjection.cpp Interaction/SnapResolution.cpp Interaction/InputEvent.cpp Interaction/HotkeyChart.cpp Interaction/ToolSession.cpp Interaction/TransformGizmo.cpp Document/SceneDocument.cpp Document/FigureRecipe.cpp Document/UndoSequence.cpp Console/CommandCodec.cpp Console/ConsoleHost.cpp Console/ConsoleInteraction.cpp Console/ConsoleSelection.cpp Console/SolidArcConsole.cpp"
  for f in $SRCS; do g++ -std=c++20 -O2 -w -I. -IPresentation -DSOLIDARC_PROOF_FOLDER="\"$H/.solidarc/build/Proofs\"" -c $f -o $H/.solidarc/build/$(echo $f | tr / _).o & done; wait
  g++ -O2 $H/.solidarc/build/*.o -o $H/.solidarc/build/SolidArc -lpthread
fi
echo "env OK: $($H/.venv/bin/python -c 'import scipy;print("scipy",scipy.__version__)') · $(LD_LIBRARY_PATH=$H/.bpystubs $H/.bpyenv/bin/python -c 'import bpy;print("bpy",bpy.app.version_string)' 2>/dev/null) · $($H/.solidarc/build/SolidArc --help | head -1)"
