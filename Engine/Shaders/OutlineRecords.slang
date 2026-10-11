//============================================================================================================================================
// 📦 Engine/Shaders/OutlineRecords.slang — Selection-silhouette vocabulary shared by the GPU pass and the CPU proof
//============================================================================================================================================
// 🧩 The selection outline is an EDGE DETECT ON THE VISIBILITY INSTANCE ID, not a shape drawn around the object.
//    VisibilityRaster writes every pixel's packed (instance, primitive) id into the R32_UINT visibility image
//    (SceneRecords.slang: Packed = (Instance & 0x3FFFF) << 14 | (Primitive & 0x3FFF), invalid = 0xFFFFFFFF).
//    A pixel belongs to the selection when its INSTANCE bits name a picked instance; the outline is exactly the
//    set of pixels that do NOT belong but sit within the brush radius of one that does. That boundary IS the
//    object's true silhouette at the rendered resolution — concavities, holes and all — because it is read off
//    the same rasterisation the frame was shaded from. No box, no sphere, no proxy.
//
//    These functions are pure so Exhibits/Workbench/Editor/EditorSelectionProof.cpp compiles this exact text as
//    C++ (SlangCpuShim.h, FRONTIER_CPU_PORT) and proves the boundary on the CPU: one source of truth.

#ifndef FRONTIER_OUTLINE_RECORDS
#define FRONTIER_OUTLINE_RECORDS

// The outline's green — the same green as the gizmo's Y axis (0x12d40a), so selection reads as one language.
const vec3 kOutlineTint = vec3(0.0706, 0.8314, 0.0392);

const uint kOutlineInvalidVisibility = 0xFFFFFFFFu;   // VisibilityRaster's clear: no geometry under this pixel
const uint kOutlinePickCapacity      = 16u;           // kMaxEditorPicked — the outliner's multi-pick width

// SceneRecords.slang packing, inverted: the instance ordinal occupies the top 18 bits above the 14 primitive bits.
uint OutlineInstanceOfPacked(uint Packed)
{
    return Packed >> 14u;
}

// True when the packed visibility id under a pixel names one of the picked instances.
bool OutlineCovers(uint Packed, uint PickedCount, uint PickedInstances[16])
{
    if (Packed == kOutlineInvalidVisibility)
        return false;
    uint Instance = OutlineInstanceOfPacked(Packed);
    for (uint I = 0u; I < PickedCount && I < kOutlinePickCapacity; ++I)
        if (PickedInstances[I] == Instance)
            return true;
    return false;
}

// The brush: a tap at offset (Dx, Dy) contributes when it sits inside the round radius. Round rather than square
//    so the outline reads as an even stroke on diagonals, the way an artist's outline would.
bool OutlineWithinBrush(int Dx, int Dy, int Radius)
{
    return Dx * Dx + Dy * Dy <= Radius * Radius;
}

#endif
