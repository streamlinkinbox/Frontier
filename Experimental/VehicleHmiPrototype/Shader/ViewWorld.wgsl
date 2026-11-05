//============================================================================================================================================
//                                                              VIEWWORLD.WGSL
//============================================================================================================================================
// 🧩 What one view sees of the world that another does not: occupants it hides, the scan's fade and the paint ripple.
//! include "FrameDescriptor.wgsl"

//------------------------------------------------------------------------------------------------------------------------
//                                        VIEW-DEPENDENT WORLD: HIDING, THE SCAN, THE PAINT RIPPLE
//------------------------------------------------------------------------------------------------------------------------

// 📝 An occupant this view leaves out (the Garage preview shows one car; the studio's other cars are hidden from it). Raster
//    clips its vertices away and rays skip its BLAS at the TLAS leaf, so it casts and receives nothing in this view.
fn OccupantHidden(Occupant: u32) -> bool
{
    return Occupant < 128u && ((Frame.HiddenOccupants[Occupant >> 5u] >> (Occupant & 31u)) & 1u) != 0u;
}

// 📝 The scan fades only the occupants the view names, and only while it has some amount.
fn OccupantScanned(Occupant: u32) -> bool
{
    return Frame.ScanState.x > 0.0 && Occupant < 128u && ((Frame.ScannedOccupants[Occupant >> 5u] >> (Occupant & 31u)) & 1u) != 0u;
}

// 📝 The reference leaves its lamps untouched by the scan ("small emissive lamp (unpatched, stays solid)"): the car's head and tail lamps
//    keep their solid shading and have no ghost.
fn ScanSparesFamily(Family: u32) -> bool
{
    return Family == HeadlightFamily || Family == TaillightFamily;
}

// 📐 The scan's solid fade, Truck.tsx makeScannable() verbatim: sd = P.x − uScanX is the distance past the travelling front
//    (positive where it has already passed), sf = smoothstep(−0.9, −0.05, sd) and the surface keeps 1 − sf · uHoloAmt · 0.965
//    of its opacity. The plane carries the front in world space, so the sweep follows the car wherever it stands.
fn ResolveScanPassed(Position: vec3f) -> f32
{
    return dot(Position, Frame.ScanPlane.xyz) + Frame.ScanPlane.w;
}

fn ResolveScanOpacity(Position: vec3f) -> f32
{
    return 1.0 - smoothstep(-0.9, -0.05, ResolveScanPassed(Position)) * Frame.ScanState.x * 0.965;
}

// 📐 The paint ripple, Truck.tsx makeScannable() verbatim: an expanding sphere about RippleCentre turns the body coat's colour,
//    roughness and metalness into the next finish, m = 1 − smoothstep(R − 0.55, R, |P − centre|). Applied wherever a
//    material is fetched for a body-coat surface, so the raster shading, the traced hits and the reflections all see it.
fn ApplyPaintRipple(Material: MaterialRecord, Family: u32, Position: vec3f) -> MaterialRecord
{
    var Painted = Material;
    if (Family == BodyCoatFamily && Frame.RippleCentre.w > 0.0)
    {
        let Radius = Frame.RippleCentre.w;
        let Reach  = 1.0 - smoothstep(Radius - Frame.RippleFinish.y, Radius, distance(Position, Frame.RippleCentre.xyz));
        Painted.Albedo    = mix(Material.Albedo, Frame.RippleNext.rgb, Reach);
        Painted.Roughness = mix(Material.Roughness, Frame.RippleNext.w, Reach);
        Painted.Metalness = mix(Material.Metalness, Frame.RippleFinish.x, Reach);
    }
    return Painted;
}
