#!/usr/bin/env bash
# Shader gate — every entry of CMakeLists' SHADER_TABLE lowered to SPIR-V, because a shader that does not compile is a
#    shader nobody has run. This exists because two defects sat undetected in Engine/Shaders/ for a whole milestone:
#      · ReSTIRViewport.slang used `flat` as an identifier, and `flat` is a GLSL keyword (an interpolation qualifier) —
#        the glslc fallback path in CMakeLists.txt cannot lower that file, even though the Slang path can;
#      · the GI pool's history snapshot declared `vec3 histUv = res.SelectedUv;` and then built a vec4 from it, which is
#        five components out of two — an error in both toolchains, in a commit nobody had lowered yet.
#    Compiling is not running, but it is the difference between "reviewed" and "well-formed".
#
#    The table is READ FROM CMakeLists.txt (the same list the build uses), never duplicated here — a shader added to the
#    build is compiled by this gate automatically, and one renamed in the build cannot leave a stale entry behind.
#
#    usage: bash Tools/Build/CheckShaders.sh        → PASS (or SKIPPED when no compiler is installed, exit 0)
#           bash Tools/Build/CheckShaders.sh -v     → also print each compiler command line
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

Verbose=0
[ "${1:-}" = "-v" ] && Verbose=1

ShaderRoot="Engine/Shaders"
Table="$(sed -n '/^set(SHADER_TABLE/,/^    )/p' CMakeLists.txt | grep -oE '"[^"]+\.slang\|[a-z]+\|[^"]+\.spv"' | tr -d '"')"
if [ -z "$Table" ]; then
    echo "[Shaders] RED — could not read SHADER_TABLE from CMakeLists.txt (has the table been renamed?)"
    exit 1
fi
Entries=$(printf '%s\n' "$Table" | wc -l)

# ─── pick a compiler ─────────────────────────────────────────────────────────────────────────────────────────────────
Sdk="${VULKAN_SDK:-}"
Slangc=""; Glslc=""; Glslang=""
for Dir in "$Sdk/bin" /usr/bin /usr/local/bin; do
    [ -z "$Slangc"  ] && [ -x "$Dir/slangc"          ] && Slangc="$Dir/slangc"
    [ -z "$Glslc"   ] && [ -x "$Dir/glslc"           ] && Glslc="$Dir/glslc"
    [ -z "$Glslang" ] && [ -x "$Dir/glslangValidator" ] && Glslang="$Dir/glslangValidator"
    [ -z "$Glslang" ] && [ -x "$Dir/glslang"          ] && Glslang="$Dir/glslang"
done
for Bin in slangc glslc glslangValidator glslang; do
    case "$Bin" in
        slangc)  [ -z "$Slangc"  ] && command -v "$Bin" >/dev/null 2>&1 && Slangc="$(command -v "$Bin")" ;;
        glslc)   [ -z "$Glslc"   ] && command -v "$Bin" >/dev/null 2>&1 && Glslc="$(command -v "$Bin")" ;;
        *)       [ -z "$Glslang" ] && command -v "$Bin" >/dev/null 2>&1 && Glslang="$(command -v "$Bin")" ;;
    esac
done

if [ -n "$Slangc" ]; then       Compiler="$Slangc";  Flavor=slangc
elif [ -n "$Glslc" ]; then      Compiler="$Glslc";   Flavor=glslc
elif [ -n "$Glslang" ]; then    Compiler="$Glslang"; Flavor=glslang
else
    echo "[Shaders] SKIPPED — no slangc, glslc or glslangValidator on PATH (and no \$VULKAN_SDK/bin)"
    echo "    Install the Vulkan SDK, or run this gate on the machine that lowers the shaders."
    exit 0
fi
echo "[Shaders] compiling $Entries shader(s) with $Flavor ($Compiler)"

# ─── stage + lower ───────────────────────────────────────────────────────────────────────────────────────────────────
# glslc and glslang require a recognised extension for the stage, so the source is staged under a .glsl name in a temp
#    directory; the .slang includes still resolve through -IEngine/Shaders.
# SHADER_OUT (D9b): when set, the lowered .spv files are KEPT in that directory instead of being verified in a temp one
#    that is thrown away. This gate's job is "do they lower"; the runner's is "run them", and it needs the artifacts — so
#    one lowering recipe serves both rather than a second copy of the tool flags living in a shell script.
if [ -n "${SHADER_OUT:-}" ]; then
    Stage="$SHADER_OUT"
    mkdir -p "$Stage" || { echo "[Shaders] RED — cannot create SHADER_OUT=$SHADER_OUT"; exit 1; }
else
    Stage="$(mktemp -d /tmp/CheckShaders.XXXXXX)"
    trap 'rm -rf "$Stage"' EXIT
fi
Failures=0

while IFS='|' read -r Src StageName Out; do
    [ -z "${Src:-}" ] && continue
    Source="$ShaderRoot/$Src"
    Staged="$Stage/$Out.glsl"
    cp "$Source" "$Staged" || { echo "  FAIL  $Src (cannot read)"; Failures=$((Failures + 1)); continue; }
    case "$Flavor" in
        slangc)
            Command=( "$Compiler" "$Source" "-DFRONTIER_SHADER_TOOLCHAIN=1" "-IEngine" "-I$ShaderRoot"
                      -target spirv -profile glsl_450 -stage "$StageName" -entry main -o "$Stage/$Out" ) ;;
        glslc)
            Command=( "$Compiler" "-DFRONTIER_SHADER_TOOLCHAIN=1" "-IEngine" "-I$ShaderRoot"
                      --target-env=vulkan1.2 "-fshader-stage=$StageName" -o "$Stage/$Out" "$Staged" ) ;;
        glslang)
            case "$StageName" in
                compute) StageFlag=comp ;;
                vertex)  StageFlag=vert ;;
                fragment) StageFlag=frag ;;
                *)       StageFlag="$StageName" ;;
            esac
            Command=( "$Compiler" -V --target-env vulkan1.2 -S "$StageFlag" -DFRONTIER_SHADER_TOOLCHAIN=1
                      -IEngine "-I$ShaderRoot" -o "$Stage/$Out" "$Staged" ) ;;
    esac
    [ "$Verbose" = "1" ] && printf '  $ %s\n' "${Command[*]}"
    if Output=$("${Command[@]}" 2>&1); then
        printf '  PASS  %-28s → %s (%s B)\n' "$Src" "$Out" "$(stat -c%s "$Stage/$Out" 2>/dev/null || echo '?')"
    else
        printf '  FAIL  %-28s\n' "$Src"
        printf '%s\n' "$Output" | grep -E "error|ERROR" | head -5 | sed 's/^/        /'
        Failures=$((Failures + 1))
    fi
done <<< "$Table"

if [ "$Failures" -ne 0 ]; then
    echo "[Shaders] RED — $Failures of $Entries shader(s) failed to lower with $Flavor"
    exit 1
fi
echo "[Shaders] GREEN — $Entries/$Entries shaders lowered to SPIR-V with $Flavor"
