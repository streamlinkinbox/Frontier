#!/bin/bash
#============================================================================================================================================
#                                                       MUTATEPLUMECOMPOSITE.SH
#============================================================================================================================================
# 🧩 Mutation check of PlumeCompositeValidation: each mutation edits a COPY of Frontier/Extensions/Plume/PlumeComposite.cpp (the repository is
#    untouched), compiles it with the flags of the validation's own compile, links it with the validation's object and runs it. A mutant the validation
#    does not stop (a failed assertion, or a run that crashes) SURVIVED: the README says which survive and why. The unmutated copy runs first as the control.
#
#        Exhibits/FluidPlumeEditor/Code/MutatePlumeComposite.sh <build folder>
#
#    The build folder is a configured and built one: ninja -t commands gives the flags, libFrontierGlobalIllumination.a the vector functions.
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BUILD=$(cd "${1:?usage: MutatePlumeComposite.sh <build folder>}" && pwd)
M=${PLUME_MUTATION_SCRATCH:-$ROOT/_ClaudeScratch/mut-composite}
mkdir -p "$M"
COMPILE=$(cd "$BUILD" && ninja -t commands PlumeCompositeValidation | grep 'PlumeCompositeValidation.cpp.o' | head -1)
FLAGS=$(printf '%s' "$COMPILE" | sed -e 's/^[^ ]* //' -e 's/ -MD .*$//')
GI="$BUILD/libFrontierGlobalIllumination.a"
[ -f "$GI" ] || { echo "not built: $GI" >&2; exit 2; }
cd "$M" || exit 2
eval "c++ $FLAGS -c $ROOT/Exhibits/FluidPlumeEditor/Code/PlumeCompositeValidation.cpp -o validation.o" || { echo "the validation did not compile" >&2; exit 2; }

SURVIVORS=0
CAUGHT=0
run() { # name sed-expression ("" for the control)
  local Name=$1 Expr=$2
  cp "$ROOT/Frontier/Extensions/Plume/PlumeComposite.cpp" "PlumeComposite.$Name.cpp"
  if [ -n "$Expr" ]; then
    sed -i "$Expr" "PlumeComposite.$Name.cpp"
    if cmp -s "PlumeComposite.$Name.cpp" "$ROOT/Frontier/Extensions/Plume/PlumeComposite.cpp"; then echo "MUTATION $Name: NO CHANGE (pattern missing)"; SURVIVORS=$((SURVIVORS + 1)); return; fi
  fi
  if ! eval "c++ $FLAGS -c PlumeComposite.$Name.cpp -o composite.$Name.o" 2> "compile.$Name.log" || ! c++ validation.o "composite.$Name.o" "$GI" -o "validation.$Name" 2>> "compile.$Name.log"; then
    echo "MUTATION $Name: DID NOT COMPILE (see $M/compile.$Name.log)"; SURVIVORS=$((SURVIVORS + 1)); return
  fi
  "./validation.$Name" > "out.$Name" 2>&1
  local Code=$? Failed
  Failed=$(grep -c '^FAIL' "out.$Name")
  if [ -z "$Expr" ]; then echo "CONTROL (unmutated copy): exit $Code, $(tail -1 "out.$Name")"; return; fi
  if [ "$Code" -eq 0 ]; then echo "MUTATION $Name: SURVIVED"; SURVIVORS=$((SURVIVORS + 1))
  elif [ "$Failed" -gt 0 ]; then echo "MUTATION $Name: caught ($Failed assertions failed)"; CAUGHT=$((CAUGHT + 1))
  else echo "MUTATION $Name: caught (the run ended with exit code $Code and no verdict: a crash)"; CAUGHT=$((CAUGHT + 1)); fi
}

run Control ""
run KeyPositionX      's/{ Camera.Position\[0\], Camera.Position\[1\], Camera.Position\[2\],/{ 0.0, Camera.Position[1], Camera.Position[2],/'
run KeyPositionY      's/{ Camera.Position\[0\], Camera.Position\[1\], Camera.Position\[2\],/{ Camera.Position[0], 0.0, Camera.Position[2],/'
run KeyPositionZ      's/{ Camera.Position\[0\], Camera.Position\[1\], Camera.Position\[2\],/{ Camera.Position[0], Camera.Position[1], 0.0,/'
run KeyForwardX       's/Camera.Forward\[0\], Camera.Forward\[1\], Camera.Forward\[2\],/0.0, Camera.Forward[1], Camera.Forward[2],/'
run KeyForwardY       's/Camera.Forward\[0\], Camera.Forward\[1\], Camera.Forward\[2\],/Camera.Forward[0], 0.0, Camera.Forward[2],/'
run KeyForwardZ       's/Camera.Forward\[0\], Camera.Forward\[1\], Camera.Forward\[2\],/Camera.Forward[0], Camera.Forward[1], 0.0,/'
run KeyFieldOfView    's/Camera.VerticalFieldOfView, Camera.Aspect }/0.0, Camera.Aspect }/'
run KeyAspect         's/Camera.VerticalFieldOfView, Camera.Aspect }/Camera.VerticalFieldOfView, 0.0 }/'
run KeyPictureSize    's/ \&\& Backdrop.Picture == Picture) return;/) return;/'
run NeverKept         's/if (Backdrop.Resolved \&\& Backdrop.Key == Key/if (false \&\& Backdrop.Key == Key/'
run AlphaClamp        's/if (AlphaChannel) Held = std::clamp(Held, 0.0f, 1.0f);//'
run NonFinite         's/std::isfinite(Decoded) ? Decoded :/true ? Decoded :/'
run TexelRowStride    's/\* Trace\[0\] + std::min(X \/ Scale/* Trace[1] + std::min(X \/ Scale/'
run TexelScale        's/std::min(Y \/ Scale, Trace\[1\] - 1u)/std::min(Y \/ (Scale + 1), Trace[1] - 1u)/'
run ExposureValue     's/Channels\[Channel\] \* PlumeExposure/Channels[Channel] * 0.5f/'
run SrgbExponent      's/1.0 \/ 2.4/1.0 \/ 2.2/'
run OpaqueAlpha       's/Destination\[3\] = 255;/Destination[3] = 254;/'
run GroundChecker     's/DarkCell ? 0.16f : 0.21f/DarkCell ? 0.16f : 0.16f/'
run SkyBlue           's/0.88f + (0.55f - 0.88f) \* HorizonBlend/0.20f + (0.20f - 0.20f) * HorizonBlend/'
echo "$CAUGHT mutants caught, $SURVIVORS survived or did not run"
[ "$SURVIVORS" -eq 0 ]
