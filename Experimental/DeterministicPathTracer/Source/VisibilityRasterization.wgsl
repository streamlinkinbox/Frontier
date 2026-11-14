/*====================================================================================================================================
                                                  VISIBILITYRASTERIZATION.WGSL
====================================================================================================================================*/
// 🧩 Rasterizes the vehicle triangles into a triangle-index target so camera rays can skip the bounds-tree descent

// 📝 QuadratureSequence.wgsl is prepended to this source. The projection is offset so that each pixel centre lands
//    exactly where this sample's traced camera ray crosses the image plane (shared subpixel pair 0). The integrator
//    re-intersects the stored triangle with that ray, so raster precision only decides which triangle is tried.

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct RasterUniforms
{
    CameraOrigin      : vec4f,   // xyz [m]
    CameraForward     : vec4f,   // xyz unit forward
    CameraRight       : vec4f,   // xyz right × tan(fov/2) × aspect
    CameraUp          : vec4f,   // xyz up × tan(fov/2)
    Sampling          : vec4u,   // width, height, sample index, first instance record [vec4u]
}

struct RasterVarying
{
    @builtin(position)              Position   : vec4f,
    @location(0) @interpolate(flat) Identifier : u32,
}

@group(0) @binding(0) var<uniform>       Raster            : RasterUniforms;
@group(0) @binding(1) var<storage, read> TrianglePositions : array<vec4f>;   // per triangle: corner, edge A, edge B (object space)
@group(0) @binding(2) var<storage, read> BoundsEntries     : array<vec4u>;   // instance records from Raster.Sampling.w (8 vec4u each)

const NearPlane : f32 = 0.02;    // [m]
const FarPlane  : f32 = 500.0;   // [m]


//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

@vertex
fn ProjectTriangleCorner(@builtin(vertex_index) VertexIndex: u32, @builtin(instance_index) Instance: u32) -> RasterVarying
{
    let TriangleIndex = VertexIndex / 3u;
    let CornerIndex   = VertexIndex % 3u;
    var Local         = TrianglePositions[TriangleIndex * 3u].xyz;
    if (CornerIndex > 0u) { Local += TrianglePositions[TriangleIndex * 3u + CornerIndex].xyz; }
    // 📝 Object → world rows 3–5 of the instance record (rigid bay transform).
    let Record   = Raster.Sampling.w + Instance * 8u;
    let RowX     = bitcast<vec4f>(BoundsEntries[Record + 3u]);
    let RowY     = bitcast<vec4f>(BoundsEntries[Record + 4u]);
    let RowZ     = bitcast<vec4f>(BoundsEntries[Record + 5u]);
    let Position = vec3f(dot(RowX.xyz, Local) + RowX.w, dot(RowY.xyz, Local) + RowY.w, dot(RowZ.xyz, Local) + RowZ.w);

    // 📝 Ray direction = Forward + NdcX·Right − NdcY·Up, so a point Offset = c·Forward + a·Right + b·Up has
    //    NdcX = a/c and image-up = b/c. The subpixel term moves pixel centres onto this sample's ray.
    let Offset   = Position - Raster.CameraOrigin.xyz;
    let Depth    = dot(Offset, Raster.CameraForward.xyz);
    let Across   = dot(Offset, Raster.CameraRight.xyz) / dot(Raster.CameraRight.xyz, Raster.CameraRight.xyz);
    let Upward   = dot(Offset, Raster.CameraUp.xyz) / dot(Raster.CameraUp.xyz, Raster.CameraUp.xyz);
    let Subpixel = ScrambledQuadraturePair(Raster.Sampling.z, 0u, 0u, vec2u(0u));
    let Shift    = vec2f((0.5 - Subpixel.x) * 2.0 / f32(Raster.Sampling.x), (Subpixel.y - 0.5) * 2.0 / f32(Raster.Sampling.y));

    var Varying : RasterVarying;
    Varying.Position   = vec4f(Across + Shift.x * Depth, Upward + Shift.y * Depth, (Depth - NearPlane) * FarPlane / (FarPlane - NearPlane), Depth);
    Varying.Identifier = ((Instance << 20u) | TriangleIndex) + 1u;
    return Varying;
}

@fragment
fn WriteTriangleIdentifier(Varying: RasterVarying) -> @location(0) vec4u
{
    return vec4u(Varying.Identifier, 0u, 0u, 0u);
}
