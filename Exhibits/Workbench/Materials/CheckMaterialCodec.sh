#!/usr/bin/env bash
# M6 gate — the codec gap-fill proof. Compiles MaterialCodecProof.cpp against the real interchange TU
#    (MaterialCodec.cpp) plus the project's interchange headers (cgltf / ufbx / fast_obj) and runs the per-extension
#    round-trip fixtures (glTF core + 12 KHR extensions + extras graph, FBX Standard Surface, OBJ/.mtl).
#    Header resolution: $REPO/ExternalPackages (submodule layout), then $MATERIAL_CODEC_EXT, then ~/.cache/m6
#    (the documented fallback for submodule-less clones — same directory layout).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Ext=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_CODEC_EXT:-}" "$HOME/.cache/m6"; do
    [ -n "$candidate" ] && [ -f "$candidate/cgltf/cgltf.h" ] && [ -f "$candidate/ufbx/ufbx.h" ] \
        && [ -f "$candidate/fast_obj/fast_obj.h" ] && Ext="$candidate" && break
done
if [ -z "$Ext" ]; then
    echo "[MaterialCodec] RED — interchange headers not found (tried ExternalPackages/, \$MATERIAL_CODEC_EXT, ~/.cache/m6)"
    exit 1
fi
echo "[MaterialCodec] headers: $Ext"

Bin="$(mktemp -u /tmp/MaterialCodec.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I Exhibits/Workbench/Materials -I Engine/ContentInterchange \
     -I "$Ext/cgltf" -I "$Ext/ufbx" -I "$Ext/fast_obj" \
     Exhibits/Workbench/Materials/MaterialCodecProof.cpp \
     Engine/ContentInterchange/MaterialCodec.cpp \
     -o "$Bin" 2>/tmp/MaterialCodec.build; then
    echo "[MaterialCodec] COMPILE FAILED"; sed 's/^/    /' /tmp/MaterialCodec.build | head -40; exit 1
fi
if [ -s /tmp/MaterialCodec.build ]; then echo "[MaterialCodec] warnings:"; sed 's/^/    /' /tmp/MaterialCodec.build | head -20; fi
if ! "$Bin" 2>&1 | tee /tmp/MaterialCodec.log | grep -q "MATERIAL CODEC: PASS"; then
    echo "[MaterialCodec] RED"; grep "FAIL" /tmp/MaterialCodec.log | sed 's/^/    /' | head -30; rm -f "$Bin"; exit 1
fi
grep -c "^ok " /tmp/MaterialCodec.log | xargs echo "[MaterialCodec] GREEN — checks passed:"
rm -f "$Bin"
