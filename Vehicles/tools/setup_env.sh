#!/bin/bash
# One-shot environment for the vehicle pipeline (idempotent, ~2 min cold):
#   ~/.venv       python with numpy/scipy/pillow       (reconstruct_liger.py, build_bmw_m4_g82.py, render_arc.py)
#   ~/.bpyenv     headless Blender (pip bpy) + stub display libs   (extract_liger_meshes.py, features.py)
#   ~/.solidarc   SolidArc console built from unassignedinbox/Slate (branch arena/01a0fd48-slate)
set -e
H=$HOME; T=$(cd "$(dirname "$0")" && pwd)
[ -x $H/.venv/bin/python ] || { python3 -m venv $H/.venv; $H/.venv/bin/pip install -q numpy scipy pillow matplotlib; }
[ -x $H/.bpyenv/bin/python ] && [ -d $H/.bpystubs ] || bash $T/setup_bpy.sh
if [ ! -x $H/.solidarc/build/SolidArc ]; then
  mkdir -p $H/.solidarc/build $H/.solidarc/build/Proofs
  [ -d $H/.solidarc/src/.git ] || { git clone -q --depth 1 --branch arena/01a0fd48-slate https://github.com/unassignedinbox/Slate.git $H/.solidarc/src; }
  cd $H/.solidarc/src/Frontier/Editor/AuthoringTools/Modelling/SolidArc
  SRCS="Kernel/CurveSpecification.cpp Kernel/SurfaceSpecification.cpp Kernel/TopologySpecification.cpp Kernel/FaceEditSolver.cpp Kernel/SkinSolver.cpp Kernel/IntersectionSolver.cpp Kernel/FairPatchSolver.cpp Kernel/ProfileSolver.cpp Kernel/ConstraintSolver.cpp Kernel/ConstraintGraph.cpp Kernel/MirrorSolver.cpp Kernel/BlendSolver.cpp Presentation/SoftwareRaster.cpp Presentation/ScenePresentation.cpp Interaction/CameraProjection.cpp Interaction/SnapResolution.cpp Interaction/InputEvent.cpp Interaction/HotkeyChart.cpp Interaction/ToolSession.cpp Interaction/TransformGizmo.cpp Document/SceneDocument.cpp Document/FigureRecipe.cpp Document/UndoSequence.cpp Console/CommandCodec.cpp Console/ConsoleHost.cpp Console/ConsoleInteraction.cpp Console/ConsoleSelection.cpp Console/SolidArcConsole.cpp"
  for f in $SRCS; do g++ -std=c++20 -O2 -w -I. -IPresentation -DSOLIDARC_PROOF_FOLDER="\"$H/.solidarc/build/Proofs\"" -c $f -o $H/.solidarc/build/$(echo $f | tr / _).o & done; wait
  g++ -O2 $H/.solidarc/build/*.o -o $H/.solidarc/build/SolidArc -lpthread
fi
echo "env OK: $($H/.venv/bin/python -c 'import scipy;print("scipy",scipy.__version__)') · $(LD_LIBRARY_PATH=$H/.bpystubs $H/.bpyenv/bin/python -c 'import bpy;print("bpy",bpy.app.version_string)' 2>/dev/null) · $($H/.solidarc/build/SolidArc --help | head -1)"
