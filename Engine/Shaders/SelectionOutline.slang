//============================================================================================================================================
//                                                      SELECTIONOUTLINE.SLANG
//============================================================================================================================================
// 🧩 Draws the editor's selection outline: the TRUE SILHOUETTE of the picked instances, in green, one compute
//    dispatch over the presentation image.
//
//    How the silhouette is found (OutlineRecords.slang holds the shared logic):
//      · VisibilityRaster already wrote every pixel's packed (instance, primitive) id into the R32_UINT
//        visibility image — the same rasterisation the frame was shaded from.
//      · A pixel is INSIDE the selection when its instance bits name a picked instance.
//      · A pixel is ON THE OUTLINE when it is NOT inside but a covered pixel sits within the stroke radius.
//    The boundary of the covered set IS the object's screen-space silhouette — concave edges, holes and
//    overlaps included — because it is read from the id of every pixel, not from any proxy shape. There is no
//    box and no sphere anywhere in this: an L-shaped object outlines as an L.
//
//    Runs after the kernel and the denoiser have finished the presentation image and before the spatial
//    interface composites over it, so the stroke sits on the lit scene and under the UI.

#version 460
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// The id image the visibility raster filled this frame (R32_UINT, point sampled — ids never interpolate).
layout(binding = 0) uniform usampler2D VisibilityImage;

// The finished presentation image the stroke lands on.
layout(rgba32f, binding = 1) uniform image2D PresentationImage;

layout(push_constant) uniform OutlineConstants
{
    uint RenderWidth;            // [px] the rendered sub-rectangle (render scale can leave the image larger)
    uint RenderHeight;           // [px]
    uint StrokeRadius;           // [px] outline thickness measured outward from the silhouette
    uint PickedCount;            // [cnt] live entries in PickedInstances
    uint PickedInstances[16];    // [idx] instance ordinals the editor has picked (kOutlinePickCapacity wide)
};

#include "Shaders/OutlineRecords.slang"

void main()
{
    ivec2 Pixel = ivec2(gl_GlobalInvocationID.xy);
    if (Pixel.x >= int(RenderWidth) || Pixel.y >= int(RenderHeight) || PickedCount == 0u)
        return;

    // Inside pixels keep their shading — the outline is a rim outside the silhouette, never a tint over the object.
    uint Centre = texelFetch(VisibilityImage, Pixel, 0).x;
    if (OutlineCovers(Centre, PickedCount, PickedInstances))
        return;

    int Radius = int(StrokeRadius);
    for (int Dy = -Radius; Dy <= Radius; ++Dy)
    {
        for (int Dx = -Radius; Dx <= Radius; ++Dx)
        {
            if (!OutlineWithinBrush(Dx, Dy, Radius))
                continue;
            ivec2 Tap = Pixel + ivec2(Dx, Dy);
            if (Tap.x < 0 || Tap.y < 0 || Tap.x >= int(RenderWidth) || Tap.y >= int(RenderHeight))
                continue;
            if (OutlineCovers(texelFetch(VisibilityImage, Tap, 0).x, PickedCount, PickedInstances))
            {
                imageStore(PresentationImage, Pixel, vec4(kOutlineTint, 1.0));
                return;
            }
        }
    }
}
