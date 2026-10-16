// Compute one conservative camera-frustum membership list per 32 px tile and
// logarithmic depth slice. Writes COUNT then up to ClusterMaxPerCell indices;
// every cell is overwritten each frame (including when all lights are off).
//! include "FrameDescriptor.wgsl"
//! include "LightRecords.wgsl"

@group(0) @binding(1) var<storage, read> Lights : array<LocalLight>;
@group(0) @binding(2) var<storage, read_write> Members : array<u32>;
@group(0) @binding(3) var<uniform> Cluster : ClusterSettings;

fn OverlapsCluster(Light: LocalLight, Tile: vec2u, Slice: u32) -> bool
{
    let Reach = Light.PositionReach.w;
    var Centre = Light.PositionReach.xyz;
    var Radius = Reach;
    if (Light.UpOuter.w > -0.999)
    {
        // A spotlight occupies only a truncated cone. The bounding sphere is
        // centred halfway along its axis; sqrt(1.25-cos θ) encloses the rim.
        let Tight = Reach * sqrt(max(0.25, 1.25 - Light.UpOuter.w));
        if (Tight < Reach)
        {
            Centre += Light.AxisInner.xyz * (0.5 * Reach);
            Radius = Tight;
        }
    }
    let View = (Frame.View * vec4f(Centre, 1.0)).xyz;
    let Depth = -View.z;
    let Low = Cluster.DepthRange.x * exp(f32(Slice) / Cluster.LogScale);
    let High = Cluster.DepthRange.x * exp(f32(Slice + 1u) / Cluster.LogScale);
    if (Depth + Radius < Low || Depth - Radius > High) { return false; }

    let Pixel0 = vec2f(Tile * ClusterTilePixels);
    let Pixel1 = min(Pixel0 + vec2f(f32(ClusterTilePixels)), Cluster.ScreenExtent);
    let X0 = (2.0 * Pixel0.x / Cluster.ScreenExtent.x - 1.0) * Cluster.Tangents.x;
    let X1 = (2.0 * Pixel1.x / Cluster.ScreenExtent.x - 1.0) * Cluster.Tangents.x;
    let Y1 = (1.0 - 2.0 * Pixel0.y / Cluster.ScreenExtent.y) * Cluster.Tangents.y;
    let Y0 = (1.0 - 2.0 * Pixel1.y / Cluster.ScreenExtent.y) * Cluster.Tangents.y;
    // Signed distances to four tile side planes. Their normals have length
    // sqrt(1+slope²); padding by that length*radius makes this a safe sphere
    // test even for sources behind the near plane or on a tile boundary.
    return X0 * Depth - View.x <= Radius * sqrt(1.0 + X0 * X0)
        && View.x - X1 * Depth <= Radius * sqrt(1.0 + X1 * X1)
        && Y0 * Depth - View.y <= Radius * sqrt(1.0 + Y0 * Y0)
        && View.y - Y1 * Depth <= Radius * sqrt(1.0 + Y1 * Y1);
}

@compute @workgroup_size(64)
fn BuildClusters(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Index = Invocation.x;
    let Tiles = Cluster.TileCount.x * Cluster.TileCount.y;
    if (Index >= Tiles * Cluster.SliceCount) { return; }
    let Slice = Index / Tiles;
    let Cell = Index % Tiles;
    let Tile = vec2u(Cell % Cluster.TileCount.x, Cell / Cluster.TileCount.x);
    let Base = Index * (ClusterMaxPerCell + 1u);
    var Count = 0u;
    for (var LightIndex = 0u; LightIndex < Cluster.LightCount; LightIndex++)
    {
        if (!OverlapsCluster(Lights[LightIndex], Tile, Slice)) { continue; }
        if (Count == ClusterMaxPerCell) { break; } // stable priority: car, fixtures, effects
        Members[Base + 1u + Count] = LightIndex;
        Count++;
    }
    Members[Base] = Count;
}
