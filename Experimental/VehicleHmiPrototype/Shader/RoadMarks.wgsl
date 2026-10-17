// Shared road-deposit filtering for raster guides AND triangle path hits.
fn SampleRoadMark(World: vec2f, PixelFootprint: f32, Rim: bool) -> f32
{
    let Extent = vec2f(TyreMarkExtentX, TyreMarkExtentY);
    let Texels = vec2f(textureDimensions(TyreMarks));
    let Coordinates = (World - vec2f(TyreMarkOriginX, TyreMarkOriginY)) / Extent;
    if (any(Coordinates <= vec2f(0.0)) || any(Coordinates >= vec2f(1.0)))
    {
        return 0.0;
    }
    let Position = Coordinates * Texels - 0.5;
    let Corner = vec2i(floor(Position));
    let Fraction = Position - floor(Position);
    var Deposit = 0.0;
    var Lateral = 0.0;
    var LateralWeight = 0.0;
    for (var Tap = 0u; Tap < 4u; Tap++)
    {
        let Offset = vec2i(i32(Tap & 1u), i32(Tap >> 1u));
        let Texel = clamp(Corner + Offset, vec2i(0), vec2i(Texels) - vec2i(1));
        let Weight = select(1.0 - Fraction.x, Fraction.x, Offset.x == 1) * select(1.0 - Fraction.y, Fraction.y, Offset.y == 1);
        let Channels = textureLoad(TyreMarks, Texel, 0);
        let Value = select(Channels.xy, Channels.zw, Rim);
        Deposit += Value.x * Weight;
        if (Value.y > 0.5 / 255.0)
        {
            Lateral += ((Value.y * 255.0 - 1.0) / 254.0 * 1.5 - 0.25) * Weight;
            LateralWeight += Weight;
        }
    }
    if (LateralWeight <= 1.0e-4 || Deposit <= 0.0)
    {
        return 0.0;
    }
    Lateral /= LateralWeight;
    // 📝 Edges: a pixel's footprint in lateral units, so the edge is one pixel wide at any distance.
    let Soft = max(PixelFootprint / select(TyreMarkPatchWidth, 0.06, Rim), 0.02);
    let Inside = smoothstep(-Soft, Soft, Lateral) * smoothstep(-Soft, Soft, 1.0 - Lateral);
    var Grooves = 0.0;
    for (var Groove = 0u; Groove < 4u; Groove++)
    {
        let Centre = 0.2 + 0.2 * f32(Groove);
        Grooves = max(Grooves, 1.0 - smoothstep(0.022, 0.022 + Soft, abs(Lateral - Centre)));
    }
    let Normalised = Deposit / max(LateralWeight, 0.25);
    return clamp(Normalised, 0.0, 1.0) * Inside * (1.0 - select(0.55 * Grooves, 0.0, Rim));
}
