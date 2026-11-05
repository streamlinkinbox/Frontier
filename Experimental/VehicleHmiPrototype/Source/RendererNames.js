//============================================================================================================================================
//                                                           RENDERERNAMES.JS
//============================================================================================================================================
// 🧩 The two renderers and what they are called. There is NO hardware ray tracing in this app (WebGPU exposes none): the "BVH" renderer is a software path tracer, a two-level BVH
//    traversed in WGSL compute shaders on ordinary shader cores. The other renderer marches the scene's signed distance field ("SDF").
//
//    ?renderer=bvh | sdf          the names
//    ?renderer=rtx | gtx          the OLD names of the same two renderers, kept so existing links and notes keep working (rtx was always this software BVH tracer, never RT cores)

export const RendererAliases = Object.freeze({ bvh: "bvh", rtx: "bvh", sdf: "sdf", gtx: "sdf" });

// ➡️ "bvh" or "sdf" for whatever the query carried; anything unknown (or absent) is the default, the BVH path tracer.
export function ResolveRendererName(Text)
{
    return RendererAliases[String(Text ?? "").trim().toLowerCase()] ?? "bvh";
}

export const RendererLabels = Object.freeze({
    bvh: Object.freeze({ Short: "BVH", Long: "BVH · ReSTIR Path Tracer", Detail: "software BVH traversal in WGSL compute (no hardware ray tracing)" }),
    sdf: Object.freeze({ Short: "SDF", Long: "SDF · Distance-Field GI", Detail: "sphere tracing of the scene distance field" })
});
