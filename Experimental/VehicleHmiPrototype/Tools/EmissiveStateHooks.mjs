//============================================================================================================================================
//                                                        EMISSIVESTATEHOOKS.MJS
//============================================================================================================================================
// 🧩 Prints whether the emissive light structure was really live in a headless run:  EMISSIVESTATE {json}. The executed ON / OFF test reads it, so a toggle that silently did nothing
//    (both runs identical because neither ran the structure) cannot pass for a comparison.
export default {
    BeforeFrame() {},
    async Report(Host)
    {
        const Emissive = Host.Emissive;
        console.log("EMISSIVESTATE " + JSON.stringify({ Active: !!Host.EmissiveActive, Setting: !!Host.Settings.EmissiveLights, RayTracing: !!Host.Settings.RayTracing, Uploaded: !!Host.EmissiveUploaded,
            Instances: Emissive ? Emissive.Count : 0, Triangles: Emissive ? Emissive.TriangleCount : 0, TruncatedCells: Emissive ? Emissive.Statistics.TruncatedCells : 0, Preset: Host.Settings.LightingPreset }));
    }
};
