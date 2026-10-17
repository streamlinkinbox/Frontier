// Browser WebGPU has no standard ray-query / ray-pipeline feature. Never infer RT-core
// access from an adapter's vendor string. Both backends below use the same WebGPU device.
export const RendererModes = Object.freeze({ RayTracing: "restir", DistanceField: "distance", Reference: "reference" });
export const PathPixelBytes = 144;
export const PathConfigurationBytes = 128;
export const PathTracingFeature = 8192;

export function ResolvePathSettings(Query, Grade = "high")
{
    const Integer = (Name, Fallback, Low, High) => {
        const Value = Number(Query.get(Name));
        return Query.has(Name) && Number.isFinite(Value) ? Math.max(Low, Math.min(High, Math.round(Value))) : Fallback;
    };
    const Renderer = Object.values(RendererModes).includes(Query.get("renderer")) ? Query.get("renderer") : RendererModes.RayTracing;
    return {
        Renderer,
        PathBounces: Integer("bounces", Grade === "low" ? 4 : 6, 2, 16),
        PathCandidates: Integer("candidates", 1, 1, 8),
        PathSeed: Integer("seed", 173, 0, 0xFFFFFFFF),
        PathRRStart: 3,
        PathHistoryWeight: 0.15,
        PathSpatialPasses: Grade === "low" ? 2 : 3,
        PathUniformProposal: 0.2,
        PathMemoryBudgetMiB: 384
    };
}

// Three 144-byte pixel buffers (temporal scratch + two *spatially persisted*
// histories), eight float32 HDR trace targets, and a FULL-RESOLUTION float16
// display target. Limit each binding and total RT target storage. The screen
// cost does not shrink with trace scale; adapter limits are not a VRAM budget.
export function ResolvePathTraceScale(Screen, Requested, Limits, BudgetMiB = 384)
{
    const BindingLimit = Math.min(Limits.maxStorageBufferBindingSize, Limits.maxBufferSize);
    const Budget = Math.max(32, BudgetMiB) * 1024 * 1024;
    const ScreenBytes = Screen[0] * Screen[1] * 8;
    const BytesPerTracePixel = PathPixelBytes * 3 + 16 * 8;
    let Scale = [1, 2, 4, 8].find((Candidate) => Candidate >= Requested) || 8;
    while (Scale < 8)
    {
        const Pixels = Math.ceil(Screen[0] / Scale) * Math.ceil(Screen[1] / Scale);
        if (Pixels * PathPixelBytes <= BindingLimit && Pixels * BytesPerTracePixel + ScreenBytes <= Budget)
            return Scale;
        Scale *= 2;
    }
    const Pixels = Math.ceil(Screen[0] / Scale) * Math.ceil(Screen[1] / Scale);
    if (Pixels * PathPixelBytes > BindingLimit || Pixels * BytesPerTracePixel + ScreenBytes > Budget)
        throw new Error("The ray-tracing targets exceed this adapter's buffer/memory budget. Reduce the render scale.");
    return Scale;
}

export function DescribeRayTracing(Exchange)
{
    const Fallback = Exchange.Adapter?.info?.isFallbackAdapter || Exchange.Adapter?.isFallbackAdapter;
    return `${Fallback ? "Software adapter · " : ""}WGSL compute triangle BVH · no browser RTX-core API`;
}
