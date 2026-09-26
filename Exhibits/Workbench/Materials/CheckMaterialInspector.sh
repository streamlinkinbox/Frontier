#!/usr/bin/env bash
# M7b gate — the material-inspector proof. Compiles MaterialInspectorProof.cpp against the real engine TUs
#    (MaterialInspector + MaterialIndex + ControlCentreHost + the inspector/UI cascade + imgui core + the CPU
#    shaderball exhibit as a SHADERBALL_PREVIEW_LIB TU) and walks nine archetype materials through selection, the 20
#    Sultan rows, fold attribution, the F-panel summary, and the editing loop (drafts, Apply/Discard, retention,
#    cutout, deterministic shaderball previews, the preview toggle, host commit flow, synthetic slider drag).
#    Header resolution: $REPO/ExternalPackages (submodule layout), then $MATERIAL_INSPECTOR_EXT, then ~/.cache/m7
#    (the documented fallback for submodule-less clones — same directory layout: imgui/, tomlplusplus/,
#    Vulkan-Headers/ as header-only deps; no GPU, no window, no Vulkan library).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.." || exit 1

Ext=""
for candidate in "$PWD/ExternalPackages" "${MATERIAL_INSPECTOR_EXT:-}" "$HOME/.cache/m7"; do
    [ -n "$candidate" ] && [ -f "$candidate/imgui/imgui.h" ] && [ -f "$candidate/tomlplusplus/include/toml++/toml.hpp" ] \
        && [ -f "$candidate/Vulkan-Headers/include/vulkan/vulkan.h" ] && Ext="$candidate" && break
done
if [ -z "$Ext" ]; then
    echo "[MaterialInspector] RED — UI headers not found (tried ExternalPackages/, \$MATERIAL_INSPECTOR_EXT, ~/.cache/m7)"
    exit 1
fi
echo "[MaterialInspector] headers: $Ext"

Bin="$(mktemp -u /tmp/MaterialInspector.XXXXXX)"
if ! g++ -std=c++20 -O2 -Wall -Wextra -ffunction-sections -fdata-sections -Wl,--gc-sections -DFRONTIER_CPU_PORT -DSHADERBALL_PREVIEW_LIB \
     -I Exhibits/Workbench/Materials -I Engine/DisplayPresentation -I Engine/ContentInterchange -I Engine/DeviceExchange \
     -I Engine/GeometricRaster -I Engine/Shaders -I Exhibits/Workbench/Editor \
     -I "$Ext/imgui" -I "$Ext/tomlplusplus/include" -I "$Ext/Vulkan-Headers/include" \
     Exhibits/Workbench/Materials/MaterialInspectorProof.cpp \
     Engine/DisplayPresentation/MaterialInspector.cpp \
     Engine/DisplayPresentation/ControlCentreHost.cpp \
     Engine/DisplayPresentation/AppearanceInspector.cpp \
     Engine/DisplayPresentation/ConfigurationInspector.cpp \
     Engine/DisplayPresentation/DialogueHost.cpp \
     Engine/DisplayPresentation/DiagnosticInspector.cpp \
     Engine/DisplayPresentation/ControlKit.cpp \
     Engine/DisplayPresentation/GlyphSpace.cpp \
     Engine/DisplayPresentation/PixelSpace.cpp \
     Engine/DisplayPresentation/ThemeStructure.cpp \
     Engine/DisplayPresentation/MotionIntegrator.cpp \
     Engine/DisplayPresentation/FidelityClassifier.cpp \
     Engine/DisplayPresentation/VectorCodec.cpp \
     Engine/DisplayPresentation/TypefaceRegistry.cpp \
     Engine/DisplayPresentation/FontCodec.cpp \
     Engine/DisplayPresentation/ConfigurationRegistry.cpp \
     Engine/DisplayPresentation/ShadingTableCodec.cpp \
     Engine/ContentInterchange/ShaderballPreview.cpp \
     Engine/ContentInterchange/MaterialIndex.cpp \
     Engine/DeviceExchange/InputExchange.cpp \
     Engine/DeviceExchange/VisibilityExchange.cpp \
     "$Ext/imgui/imgui.cpp" "$Ext/imgui/imgui_draw.cpp" "$Ext/imgui/imgui_tables.cpp" "$Ext/imgui/imgui_widgets.cpp" \
     -o "$Bin" 2>/tmp/MaterialInspector.build; then
    echo "[MaterialInspector] COMPILE FAILED"; sed 's/^/    /' /tmp/MaterialInspector.build | head -40; exit 1
fi
if [ -s /tmp/MaterialInspector.build ]; then echo "[MaterialInspector] warnings:"; sed 's/^/    /' /tmp/MaterialInspector.build | head -20; fi
if ! "$Bin" 2>&1 | tee /tmp/MaterialInspector.log | grep -q "MATERIAL INSPECTOR: PASS"; then
    echo "[MaterialInspector] RED"; grep "FAIL" /tmp/MaterialInspector.log | sed 's/^/    /' | head -30; rm -f "$Bin"; exit 1
fi
grep -c "^ok " /tmp/MaterialInspector.log | xargs echo "[MaterialInspector] GREEN — checks passed:"

# Project TU check (conditional): GameExecution.cpp — the ①h feed's home — must still parse. Needs the
#    interchange headers too, so it runs only when they resolve (same dir, the codec env var, or ~/.cache/m6).
M6=""
for candidate in "$Ext" "$PWD/ExternalPackages" "${MATERIAL_CODEC_EXT:-}" "$HOME/.cache/m6"; do
    [ -n "$candidate" ] && [ -f "$candidate/cgltf/cgltf.h" ] && [ -f "$candidate/ufbx/ufbx.h" ] \
        && [ -f "$candidate/fast_obj/fast_obj.h" ] && M6="$candidate" && break
done
if [ -n "$M6" ]; then
    if ! g++ -std=c++20 -O1 -fsyntax-only -I Engine -I Engine/DisplayPresentation -I Engine/ContentInterchange          -I Engine/DeviceExchange -I Engine/GeometricRaster -I Engine/Editor -I Projects/Project-Zero/Source          -I Projects/Project-Dyno/Source -I "$Ext/imgui" -I "$Ext/tomlplusplus/include" -I "$Ext/Vulkan-Headers/include"          -I "$M6/cgltf" -I "$M6/ufbx" -I "$M6/fast_obj" Projects/Project-Zero/Source/GameExecution.cpp          2>/tmp/MaterialInspector.game; then
        echo "[MaterialInspector] RED — GameExecution.cpp no longer parses:"; sed 's/^/    /' /tmp/MaterialInspector.game | head -20; rm -f "$Bin"; exit 1
    fi
    echo "[MaterialInspector] GameExecution.cpp parses"
else
    echo "[MaterialInspector] GameExecution.cpp check skipped (interchange headers not found)"
fi
rm -f "$Bin"
