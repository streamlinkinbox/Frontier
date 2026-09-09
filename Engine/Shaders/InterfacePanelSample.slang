//============================================================================================================================================
// 📦 Engine/Shaders/InterfacePanelSample.slang — evaluate the composed panel at a point on its face (include only)
//============================================================================================================================================
// 🧩 The High tier of the light-contribution ladder. Low registers the panel as a proxy quad whose material carries
//    the panel's AVERAGE colour, so a reflection shows a flat glowing rectangle. This walks the actual figures at
//    the hit's plane coordinate, so the reflection shows the panel's layout — buttons, needle, readout — instead.
//
//    Pure functions over a figure buffer the caller binds. No bindings are declared here, deliberately: the raster
//    stage and the ray-tracing kernel bind the same data at different indices, and a header that hard-coded one
//    would be unusable by the other. This is the same reason InterfaceSignedDistance.slang stays binding-free, and
//    it is what the P0 plan meant by "the hit shader includes it unchanged".
//
//    Cost is the honest objection: every reflection ray that strikes the panel walks every figure. At the trial
//    panel's 14 figures that is nothing; at a full cluster it would not be. SampleInterfacePanel therefore takes a
//    budget and stops, and the caller decides what a reflection is worth. Low remains the default.

#ifndef FRONTIER_INTERFACE_PANEL_SAMPLE
#define FRONTIER_INTERFACE_PANEL_SAMPLE

// Requires, from the includer:
//    Shaders/InterfaceRecords.slang        the InterfaceInstanceFigure layout
//    Shaders/InterfaceSignedDistance.slang DistanceFigure / CoverageFromDistance
//    a buffer named InterfacePanelFigures[] of InterfaceInstanceFigure

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL SAMPLE
//------------------------------------------------------------------------------------------------------------------------

struct PanelSample
{
    vec3  Emission;     // [nit] radiance leaving the panel at this point
    float Coverage;     // [-]   0 = bare panel bed, 1 = fully covered by a figure
    uint  Walked;       // [cnt] figures examined — for telemetry and budget checks
};

// Unpack an RGBA8 tint, linearised. Radiance adds linearly; summing display-referred bytes would make a half-lit
//    panel visibly too bright, worst in the mid tones where most interface colour lives.
vec3 InterfaceTintToLinear(uint Packed)
{
    vec3 Encoded = vec3(float( Packed        & 0xFFu),
                        float((Packed >>  8) & 0xFFu),
                        float((Packed >> 16) & 0xFFu)) * (1.0 / 255.0);
    // sRGB → linear, branchless.
    vec3 Low  = Encoded * (1.0 / 12.92);
    vec3 High = pow((Encoded + 0.055) / 1.055, vec3(2.4));
    return mix(Low, High, step(vec3(0.04045), Encoded));
}

// Evaluate the panel at PanelUv, which is 0..1 across the proxy quad's face with the origin at its lower-left
//    corner — the UV the proxy's own vertices carry, so a caller can pass the interpolated texcoord directly.
//
// PanelHalfWidth / PanelHalfHeight are the proxy's half extents in metres, needed to turn the UV back into the
//    panel-local metres the figures are authored in.
//
// FigureCount is how many entries of InterfacePanelFigures[] belong to this panel; Budget caps the walk.
PanelSample SampleInterfacePanel(vec2 PanelUv, float PanelHalfWidth, float PanelHalfHeight,
                                 uint FigureCount, uint Budget, float PixelWidth)
{
    PanelSample Result;
    Result.Emission = vec3(0.0);
    Result.Coverage = 0.0;
    Result.Walked   = 0u;

    // UV → panel-local metres, centred. The panel's own plane is what every figure's world row is expressed in,
    //    and the proxy quad was built spanning exactly ±HalfExtent, so this is an exact inverse.
    vec2 Local = vec2((PanelUv.x - 0.5) * 2.0 * PanelHalfWidth,
                      (PanelUv.y - 0.5) * 2.0 * PanelHalfHeight);

    uint Limit = min(FigureCount, Budget);
    for (uint Index = 0u; Index < Limit; ++Index)
    {
        InterfaceInstanceFigure Figure = InterfacePanelFigures[Index];
        ++Result.Walked;

        // The figure's world rows place it in the panel plane. Translation is in .w, and the panel plane's own
        //    basis is the proxy's, so the figure's offset within the panel is the difference of translations
        //    projected onto the panel axes. The rows are orthonormal up to the panel scale, so the transpose is
        //    the inverse — the same argument InterfacePointerProjection makes on the CPU side.
        vec3 Right = vec3(Figure.RowX.x, Figure.RowY.x, Figure.RowZ.x);
        vec3 Up    = vec3(Figure.RowX.y, Figure.RowY.y, Figure.RowZ.y);
        float Scale = length(Right);
        if (Scale < 1e-9) continue;

        // Figure centre expressed in the panel's plane: project its translation onto the panel axes. Both share
        //    the panel's own frame, so this reduces to a dot with the normalised basis.
        vec3 Translation = vec3(Figure.RowX.w, Figure.RowY.w, Figure.RowZ.w);
        vec2 Centre = vec2(dot(Translation, Right / Scale), dot(Translation, Up / max(length(Up), 1e-9)));

        vec2 Point = (Local - Centre) / Scale;

        uint  Category = Figure.CategoryPalette >> 24;
        float Distance = DistanceFigure(Category, Point, Figure.HalfExtent / Scale,
                                        Figure.CornerRadius / Scale, Figure.ScalarAlpha, Figure.ScalarBeta);

        float Alpha = CoverageFromDistance(Distance, max(PixelWidth, 1e-6)) * clamp(Figure.Opacity, 0.0, 1.0);
        if (Alpha <= 0.0) continue;

        // Emission is the resolved tint scaled by the ⑦ emissive weight, so a figure that only reflects room
        //    light contributes nothing to what the panel RADIATES — the same rule the CPU averaging path uses.
        vec3 Tint = InterfaceTintToLinear(Figure.Tint);
        float Weight = clamp(Figure.EmissiveWeight, 0.0, 1.0);

        // Alpha-over, front-to-back is not available here (the buffer is submission order), so composite
        //    straightforwardly: later figures sit on top, which matches the raster's own back-to-front submission.
        Result.Emission = mix(Result.Emission, Tint * Weight, Alpha);
        Result.Coverage = mix(Result.Coverage, 1.0, Alpha);
    }
    return Result;
}

#endif // FRONTIER_INTERFACE_PANEL_SAMPLE
