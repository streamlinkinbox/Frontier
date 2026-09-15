#!/usr/bin/env bash
# ------------------------------------------------------------------------------------------------------------------
#  CheckShaderCompile.sh — lower every compute shader to SPIR-V and fail on any that will not compile.
# ------------------------------------------------------------------------------------------------------------------
# Until now nothing in the repository could tell whether a shader edit was even syntactically valid: the CMake build
#    only compiles shaders when it finds glslc, and a machine without the Vulkan SDK silently falls back to whatever
#    .spv files happen to be checked in. That makes a typo in a .slang file invisible until someone with a GPU runs
#    the application — which, in this sandbox, is nobody.
#
# glslang is used through its WebAssembly build, fetched from npm on demand rather than vendored: it is ~1 MB of
#    binary that has no business living in Git, and npm gives us a pinned, reproducible version. If the fetch fails
#    (no network) the script SKIPS rather than fails, so it never turns an offline machine red for the wrong reason.
#
# Only the compute shaders are checked. The rasteriser's .slang files are lowered by slangc on Windows and use Slang
#    syntax that glslang cannot parse; claiming to check them and silently not doing so would be worse than not
#    listing them at all.
set -uo pipefail

Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
Cache="${TMPDIR:-/tmp}/frontier-glslang"
Version="0.0.15"

Pass=0; Fail=0
printf '\n=== Shader compilation (GLSL -> SPIR-V, glslang %s) ===\n\n' "$Version"

# ── Fetch the compiler once and reuse it ────────────────────────────────────────────────────────────────────────
if [[ ! -f "$Cache/package/dist/node-devel/glslang.js" ]]; then
    mkdir -p "$Cache"
    if ! ( cd "$Cache" && npm pack "@webgpu/glslang@$Version" >/dev/null 2>&1 \
           && tar xzf "webgpu-glslang-$Version.tgz" ); then
        printf '  SKIP  glslang could not be fetched (offline?) — shader syntax NOT verified\n\n'
        exit 0
    fi
fi

if ! command -v node >/dev/null 2>&1; then
    printf '  SKIP  node is not installed — shader syntax NOT verified\n\n'
    exit 0
fi

# ── The driver: compile one file, print the first few diagnostics on failure ────────────────────────────────────
cat > "$Cache/Compile.js" <<'JAVASCRIPT'
const FileSystem = require('fs');
const NodePath   = require('path');
const Path       = process.argv[2];
const Cache      = process.argv[3];
const Roots      = process.argv.slice(4);          // mirrors the -I list CMake passes to glslc

// The WebAssembly build of glslang has no filesystem, so it cannot service #include itself. Rather than skip every
//    shader that has one — which is most of the interesting ones — includes are resolved here against the same
//    roots CMake uses, recursively, with a guard so a cycle cannot hang the check. Line directives are deliberately
//    NOT emitted: glslang's WASM entry point reports flat line numbers, and a #line would make them lie.
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
    // CMake defines this for both glslc and slangc, so the check must too or the sources take a different branch.
    Source = Source.replace(/^#version .*$/m, '$&\n#define FRONTIER_SHADER_TOOLCHAIN 1');
    // The third argument asks glslang for its own diagnostics rather than a bare throw.
    try   { process.stdout.write(String(Glslang.compileGLSL(Source, 'compute', true).length)); }
    catch (Error) { process.stderr.write(String(Error)); process.exit(1); }
});
JAVASCRIPT

for Shader in "$Root"/Engine/Shaders/*.slang; do
    Name="$(basename "$Shader")"
    # Compute shaders declare a workgroup; anything else is a raster stage glslang cannot parse as compute.
    grep -q 'local_size_x' "$Shader" || continue

    if Words="$(node "$Cache/Compile.js" "$Shader" "$Cache" "$Root/Engine" "$Root/Engine/Shaders" 2>"$Cache/Error.txt")"; then
        printf '  PASS  %-28s %s SPIR-V words\n' "$Name" "$Words"
        Pass=$((Pass + 1))
    else
        printf '  FAIL  %-28s\n' "$Name"
        sed 's/^/          /' "$Cache/Error.txt" | head -8
        Fail=$((Fail + 1))
    fi
done

printf '\n  %d passed, %d failed\n\n' "$Pass" "$Fail"
[[ "$Fail" -eq 0 ]]
