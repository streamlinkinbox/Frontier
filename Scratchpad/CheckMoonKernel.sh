#!/usr/bin/env bash
#============================================================================================================================================
# 📦 Scratchpad/CheckMoonKernel.sh — the moons reach both pipelines, and say the same thing on each
#============================================================================================================================================
# The Celestial moons ship on two paths that must agree: the CPU raster draws textured discs through EvaluateMoons
#    (MoonConstantRecord.h), and the ray-tracing kernel draws them through MoonAlong (MoonRecords.slang, binding
#    22) plus a MoonAmbient direct fill. The roster resolves once, in CelestialSequence, and both consumers read it.
#
# Six ways this rots, all guarded:
#    ① the shader drifts from the C++ model, so the GI-on and GI-off nights stop matching,
#    ② the std140 block and its C++ mirror disagree, which is a wrong picture rather than a compile error,
#    ③ a second copy of the atlas appears (a filename or a size restated outside MoonConstantRecord.h),
#    ④ the sequence packs the kernel different moons than the raster's (PackMoonRecord must mirror ApplyTo),
#    ⑤ the host stops writing the descriptor — the layout, the pool, the write or the per-frame push,
#    ⑥ the moons stop being opt-in, and every existing proof's night sheet shifts under them.
set -u
cd "$(dirname "$0")/.."
Fail=0
Report() { if [ "$1" = "0" ]; then printf '  %-66s PASS\n' "$2"; else printf '  %-66s FAIL\n' "$2"; Fail=1; fi; }

# Vendored headers, following CheckCelestialSky: stb_image.h for the JPEG decode, vulkan.h for the raster headers.
#    Absent headers SKIP the compiled proofs (the CheckShaderCompile precedent: never red for a missing toolchain)
#    while every structural check below still runs.
Stb="${STB:-/tmp/stbsrc}"; Vkh="${VKH:-/tmp/vkh/include}"
HaveHeaders=1
[ -f "$Stb/stb_image.h" ] || HaveHeaders=0
[ -f "$Vkh/vulkan/vulkan.h" ] || HaveHeaders=0

if [ "$HaveHeaders" = "1" ]; then
echo "[MoonKernel] the sequence packs the raster's moons for the kernel"
Pack="$(mktemp -u /tmp/MoonPackProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -ffunction-sections -fdata-sections -Wall -Wextra -I . -I Engine -I "$Stb" -I "$Vkh" \
     -o "$Pack" Scratchpad/MoonPackProof.cpp Projects/Project-Zero/Source/CelestialSequence.cpp \
     Engine/DisplayPresentation/CelestialSolver.cpp Engine/GeometricRaster/StarCatalogueIndex.cpp \
     Engine/ContentInterchange/TextureIndex.cpp \
     -Wl,--gc-sections 2>/tmp/MoonPack.build; then
    echo "  PACK PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/MoonPack.build | head -16; exit 1
fi
"$Pack" || Fail=1
rm -f "$Pack"

echo
echo "[MoonKernel] the shader matches the model and the moons reach the raster"
Render="$(mktemp -u /tmp/MoonRenderProof.XXXXXX)"
if ! g++ -std=c++20 -O2 -msse4.2 -I . -I Engine -I Scratchpad -I "$Stb" -I "$Vkh" -o "$Render" \
     Scratchpad/MoonRenderProof.cpp \
     Engine/GeometricRaster/VisibilityRaster.cpp \
     Engine/GeometricRaster/SceneStructure.cpp \
     Engine/GeometricRaster/GeometryStructure.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     Engine/DisplayPresentation/CelestialSolver.cpp \
     Engine/GeometricRaster/StarCatalogueIndex.cpp \
     Projects/Project-Zero/Source/CelestialSequence.cpp \
     Engine/ContentInterchange/TextureIndex.cpp \
     Engine/DeviceExchange/OrientationClassifier.cpp \
     2>/tmp/MoonRender.build; then
    echo "  RENDER PROOF FAILED TO BUILD"; sed 's/^/    /' /tmp/MoonRender.build | head -20; exit 1
fi
"$Render" || Fail=1
rm -f "$Render"
else
echo "[MoonKernel] compiled proofs SKIPPED — vendored headers absent"
echo "  (need \$STB/stb_image.h and \$VKH/vulkan/vulkan.h; e.g. STB=/tmp/stbsrc VKH=/tmp/vkh/include)"
echo "  fetch with: git clone --depth 1 --filter=blob:none --sparse https://github.com/nothings/stb /tmp/stbsrc &&"
echo "    (cd /tmp/stbsrc && git sparse-checkout set stb_image.h); and KhronosGroup/Vulkan-Headers (include/) to /tmp/vkh"
fi

Kernel=Engine/Shaders/ReSTIRViewport.slang
Moon=Engine/Shaders/MoonRecords.slang
Sky=Engine/Shaders/SkyRecords.slang
Post=Engine/Shaders/PostRecords.slang
Code="$(sed 's;//.*;;' "$Kernel")"
MoonCode="$(sed 's;//.*;;' "$Moon")"
SkyCode="$(sed 's;//.*;;' "$Sky")"
PostCode="$(sed 's;//.*;;' "$Post")"

echo
echo "[MoonKernel] the discs and the moonlight are wired into the kernel"
printf '%s' "$SkyCode" | grep -q 'Radiance += MoonAlong(Direction, Transmittance);'
Report $? "the sky entry point adds the moon discs"
printf '%s' "$Code" | grep -q 'accumulatedRadiance += albedo \* frame.Occlusion \* MoonAmbient();'
Report $? "the direct path adds the moonlight fill"
Entries=$(printf '%s' "$MoonCode" | grep -c 'vec3 MoonAlong(vec3 Direction, vec3 Transmittance)')
[ "$Entries" = "1" ]
Report $? "MoonRecords exposes exactly one MoonAlong ($Entries found)"

echo
echo "[MoonKernel] the block is declared where it was reserved"
printf '%s' "$MoonCode" | grep -q 'layout(std140, binding = 22) uniform MoonConstants'
Report $? "the moon uniform sits at binding 22"
# 23-24 are the post seam's: star tables (storage) and the post record (UBO). Both are written at bring-up,
#    so neither is the declared-but-unwritten hole this assert used to forbid.
printf '%s' "$PostCode" | grep -q 'layout(std430, binding = 23) readonly buffer StarTable'
Report $? "the star tables sit at binding 23"
printf '%s' "$PostCode" | grep -q 'layout(std140, binding = 24) uniform PostConstants'
Report $? "the post record sits at binding 24"
printf '%s' "$Code" | grep -q 'binding = 25) uniform sampler2D Textures'
Report $? "the bindless table is still last, as a variable-count binding must be"

echo
echo "[MoonKernel] the atlas lives in one place"
# A restated filename or size is the roster and the atlas starting to drift.
AtlasFiles=$(grep -rln --include=*.cpp --include=*.h -e 'luna_2k.jpg' Engine Projects Scratchpad | sort | tr '\n' ' ')
[ "$AtlasFiles" = "Engine/DisplayPresentation/MoonConstantRecord.h " ]
Report $? "no filename is restated outside the atlas ($AtlasFiles)"

echo
echo "[MoonKernel] the C++ mirror matches the shader's std140 layout"
grep -q 'static_assert(sizeof(MoonConstantRecord) == 288u' Engine/DisplayPresentation/MoonConstantRecord.h
Report $? "the record is pinned at 288 bytes"
Offsets=$(grep -c 'static_assert(offsetof(MoonConstantRecord' Engine/DisplayPresentation/MoonConstantRecord.h)
[ "$Offsets" -ge 5 ]
Report $? "every member's offset is asserted ($Offsets of them)"

echo
echo "[MoonKernel] the host writes binding 22 as a uniform buffer"
X=Engine/DeviceExchange/SwapchainExchange.cpp
H=Engine/DeviceExchange/SwapchainExchange.h
PackSite=Projects/Project-Zero/Source/CelestialSequence.cpp
grep -q '(B == 21u || B == 22u || B == 24u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER' "$X"
Report $? "binding 22 is laid out as a uniform buffer"
grep -q 'WriteUniform(22u, MoonInfo);' "$X"
Report $? "binding 22 is actually written"
grep -q 'kMoonRecordBytes = 288u' "$X"
Report $? "the host allocation agrees with the mirror's 288 bytes"
MoonLine=$(grep -n '"BringMoonRecord"' "$X" | head -1 | cut -d: -f1); MoonLine=${MoonLine:-0}
SetLine=$(grep -n '"BringDescriptorSet"' "$X" | head -1 | cut -d: -f1); SetLine=${SetLine:-0}
[ "$MoonLine" -gt 0 ] && [ "$MoonLine" -lt "$SetLine" ]
Report $? "the moon buffer is brought up before the set that writes it"

echo
echo "[MoonKernel] the moons survive a resize and reach the frame"
grep -q 'Vulkan->MoonBuffer) *vkDestroyBuffer' "$X"
Report $? "the moon buffer is destroyed at retire"
! sed -n '/void SwapchainExchange::RetireSwapchain/,/^}/p' "$X" | grep -q 'MoonBuffer'
Report $? "a resize does not unbind the moons"
# One production pack site, so the kernel cannot be handed two rosters from two callers.
PackSites=$(grep -rn 'PackMoonConstants(' Engine Projects --include=*.cpp | wc -l)
[ "$PackSites" = "1" ]
Report $? "PackMoonConstants has exactly one production caller ($PackSites found)"
# The structural backstop to the runtime proof above: a linked Luna reads the solved frame, not a cache.
sed -n '/void ResolveMoonDrawList/,/^}/p' "$PackSite" | grep -q 'E.Direction\[0\] = Solved.Moon.Direction\[0\]'
Report $? "the resolver reads the solved lunar direction"
# And the project registers the files, assigns the atlas, and pushes the record every frame.
grep -q 'RegisterPath(MoonPath' Projects/Project-Zero/Source/GameExecution.cpp
Report $? "the project registers the atlas before decode"
grep -q 'Celestial.AssignMoonAtlas(' Projects/Project-Zero/Source/GameExecution.cpp
Report $? "the project assigns the atlas after decode"
grep -q 'Celestial.PackMoonRecord()' Projects/Project-Zero/Source/GameExecution.cpp
Report $? "the project packs the record each frame"
grep -q 'Surface.RefreshMoons(' Projects/Project-Zero/Source/GameExecution.cpp
Report $? "the project pushes it at the swapchain"

echo
echo "[MoonKernel] the moons stay off unless asked"
grep -q 'const MoonDrawList\* Moons = nullptr;' Engine/GeometricRaster/VisibilityRaster.h
Report $? "CelestialSettings defaults to no moons"

echo
echo "[MoonKernel] the compiled kernel agrees with the host layout"
# The kernel is compiled to real SPIR-V (glslang's WASM build, fetched on demand exactly as CheckShaderCompile
#    does) and binding 22 must come back a UNIFORM_BUFFER with 23/24 carrying the post seam (stars storage, post
#    UBO) — the shader side of the host checks above. CheckSkyKernel pins the whole table; this pins the moon
#    row plus the post rows, so either gate runs standalone.
MoonCache="${TMPDIR:-/tmp}/frontier-glslang"
MoonVersion="0.0.15"
if ! command -v node >/dev/null 2>&1; then
    echo "  SKIP  node is not installed — SPIR-V cross-check NOT run"
elif [[ ! -f "$MoonCache/package/dist/node-devel/glslang.js" ]] && \
     ! ( mkdir -p "$MoonCache" && cd "$MoonCache" && npm pack "@webgpu/glslang@$MoonVersion" >/dev/null 2>&1 \
         && tar xzf "webgpu-glslang-$MoonVersion.tgz" ); then
    echo "  SKIP  glslang could not be fetched (offline?) — SPIR-V cross-check NOT run"
else
    cat > "$MoonCache/EmitMoon.js" <<'JAVASCRIPT'
const FileSystem = require('fs');
const NodePath   = require('path');
const Path       = process.argv[2];
const Cache      = process.argv[3];
const Out        = process.argv[4];
const Roots      = process.argv.slice(5);
function Resolve(File, Seen)
{
    return FileSystem.readFileSync(File, 'utf8').split('\n').map((Line) => {
        const Match = Line.match(/^\s*#\s*include\s+[\"<]([^\">]+)[\">]/);
        if (!Match) return Line;
        for (const Root of Roots)
        {
            const Candidate = NodePath.join(Root, Match[1]);
            if (!FileSystem.existsSync(Candidate)) continue;
            if (Seen.has(Candidate)) return '// (already included) ' + Match[1];
            Seen.add(Candidate);
            return Resolve(Candidate, Seen);
        }
        throw new Error('unresolved #include "' + Match[1] + '" in ' + File);
    }).join('\n');
}
require(Cache + '/package/dist/node-devel/glslang.js')().then((Glslang) => {
    let Source;
    try   { Source = Resolve(Path, new Set()); }
    catch (Error) { process.stderr.write(String(Error)); process.exit(1); }
    Source = Source.replace(/^#version .*$/m, '$&\n#define FRONTIER_SHADER_TOOLCHAIN 1');
    try {
        const Words = Glslang.compileGLSL(Source, 'compute', true);
        const Bytes = Buffer.allocUnsafe(Words.length * 4);
        for (let I = 0; I < Words.length; ++I) Bytes.writeUInt32LE(Words[I] >>> 0, I * 4);
        FileSystem.writeFileSync(Out, Bytes);
    } catch (Error) { process.stderr.write(String(Error)); process.exit(1); }
});
JAVASCRIPT
    Spv="$(mktemp -u /tmp/MoonKernelLayout.XXXXXX.spv)"
    if ! node "$MoonCache/EmitMoon.js" "$Kernel" "$MoonCache" "$Spv" Engine Engine/Shaders 2>/tmp/MoonKernelSpv.build; then
        echo "  KERNEL FAILED TO LOWER"; sed 's/^/    /' /tmp/MoonKernelSpv.build | head -8; Fail=1
    else
        Actual="$(python3 - "$Spv" <<'PY'
import struct, sys
data = open(sys.argv[1],'rb').read()
words = struct.unpack('<%dI' % (len(data)//4), data)
assert words[0] == 0x07230203, "bad magic"
i = 5
decor, vartype, varclass, ptrtype, imginfo, elem, block, bufblock = {}, {}, {}, {}, {}, {}, set(), set()
while i < len(words):
    w0 = words[i]; ln = w0 >> 16; op = w0 & 0xFFFF
    ops = words[i+1:i+ln]
    if op == 71 and len(ops) >= 3 and ops[1] == 33: decor.setdefault(ops[0], {})[33] = ops[2]
    elif op == 71 and len(ops) >= 2 and ops[1] == 2: block.add(ops[0])
    elif op == 71 and len(ops) >= 2 and ops[1] == 3: bufblock.add(ops[0])
    elif op == 59 and len(ops) >= 3: vartype[ops[1]] = ops[0]; varclass[ops[1]] = ops[2]
    elif op == 32 and len(ops) >= 3: ptrtype[ops[0]] = (ops[1], ops[2])
    elif op == 25 and len(ops) >= 8: imginfo[ops[0]] = ('image', ops[6])
    elif op == 27 and len(ops) >= 2: imginfo[ops[0]] = ('sampled', None)
    elif op in (28, 29) and len(ops) >= 2: elem[ops[0]] = ops[1]
    i += ln if ln else 1
out = []
for vid, d in decor.items():
    if 33 not in d or vid not in vartype: continue
    t = ptrtype.get(vartype[vid], (None, None))[1]
    while t in elem: t = elem[t]
    cls = varclass[vid]
    if t in imginfo:
        k, s = imginfo[t]
        kind = 'COMBINED_IMAGE_SAMPLER' if k == 'sampled' else ('STORAGE_IMAGE' if s == 2 else 'UNKNOWN-IMAGE')
    elif t in bufblock: kind = 'STORAGE_BUFFER'
    elif cls == 2 and t in block: kind = 'UNIFORM_BUFFER'
    elif cls == 12 and t in block: kind = 'STORAGE_BUFFER'
    else: kind = 'UNKNOWN'
    out.append(f"{d[33]}:{kind}")
print(' '.join(sorted(out, key=lambda s: int(s.split(':')[0]))))
PY
)"
        rm -f "$Spv"
        case " $Actual " in
            *" 22:UNIFORM_BUFFER "*) Report 0 "binding 22 lowers as a uniform buffer";;
            *) Report 1 "binding 22 lowers as a uniform buffer"; printf '    table: %s\n' "$Actual";;
        esac
        case " $Actual " in
            *" 23:STORAGE_BUFFER "*) Report 0 "binding 23 lowers as the star-table storage buffer";;
            *) Report 1 "binding 23 lowers as the star-table storage buffer"; printf '    table: %s\n' "$Actual";;
        esac
        case " $Actual " in
            *" 24:UNIFORM_BUFFER "*) Report 0 "binding 24 lowers as the post uniform buffer";;
            *) Report 1 "binding 24 lowers as the post uniform buffer"; printf '    table: %s\n' "$Actual";;
        esac
    fi
fi

echo
if [ "$Fail" != "0" ]; then echo "[MoonKernel] FAILED"; exit 1; fi
echo "[MoonKernel] OK"
