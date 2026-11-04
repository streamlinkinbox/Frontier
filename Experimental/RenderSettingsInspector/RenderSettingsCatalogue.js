/*==========================================================================================================================================
                                                       RENDERSETTINGSCATALOGUE.JS
==========================================================================================================================================*/
// 🧩 Every setting of the one Render settings component: levelspace key, the C++ field it drives, range, demo default and the
//    conditions under which it applies; plus the demo's render and trace extents, a ray count and a labelled cost estimate.
'use strict';

const RenderSettingsCatalogue = (() =>
{
    //------------------------------------------------------------------------------------------------------------------------
    //                                                   GRADES AND VIEWS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 IlluminationConstants.cpp ResolveQualityGrade, field for field (the demo's ?quality=low|medium|high).
    const QualityGrades = [
        { Name: 'Low',    ClipResolution: 64,  FinestVoxel: 0.25,  CardExtent: 1024, ShadowExtent: 1024, Steps: 64, RenderScale: 0.6,
          TileBudget: 384,  PixelLimit: 1280 * 720 },
        { Name: 'Medium', ClipResolution: 96,  FinestVoxel: 0.16,  CardExtent: 2048, ShadowExtent: 2048, Steps: 80, RenderScale: 0.85,
          TileBudget: 1024, PixelLimit: 1600 * 900 },
        { Name: 'High',   ClipResolution: 128, FinestVoxel: 0.125, CardExtent: 2048, ShadowExtent: 2048, Steps: 96, RenderScale: 1.0,
          TileBudget: 2048, PixelLimit: 1920 * 1080 }
    ];

    // 📝 The demo's InspectionModes (InspectorPages.js); index = RefreshToggles::Inspection = levelspace inspection_mode. Missing:
    //    the C++ port does not write the view (none since InspectionMarch was ported). NeedsVirtualized: without virtualized geometry
    //    the view shows the demo's orange "no virtualized geometry" checker (ClusterDebugFallback) or an empty visibility buffer.
    const InspectionViews = [
        { Index: 0,  Group: 'Lighting', Name: 'Final image',                    Short: 'Final' },
        { Index: 1,  Group: 'Lighting', Name: 'Direct sun only',                Short: 'Direct sun' },
        { Index: 2,  Group: 'Lighting', Name: 'Indirect diffuse (GI)',          Short: 'Indirect' },
        { Index: 3,  Group: 'Lighting', Name: 'Indirect irradiance (denoised)', Short: 'Denoised GI' },
        { Index: 4,  Group: 'Lighting', Name: 'ReSTIR irradiance (raw)',        Short: 'Raw ReSTIR' },
        { Index: 5,  Group: 'Lighting', Name: 'Glossy reflections',             Short: 'Reflections' },
        { Index: 6,  Group: 'Scene',    Name: 'BVH / distance field',           Short: 'BVH / SDF' },
        { Index: 7,  Group: 'Scene',    Name: 'Surface cards (texel space)',    Short: 'Cards' },
        { Index: 8,  Group: 'Scene',    Name: 'Normals',                        Short: 'Normals' },
        { Index: 9,  Group: 'Scene',    Name: 'Albedo',                         Short: 'Albedo' },
        { Index: 10, Group: 'Clusters', Name: 'Visibility buffer IDs',          Short: 'Cluster IDs', NeedsVirtualized: true },
        { Index: 11, Group: 'Clusters', Name: 'Deferred resolve',               Short: 'Resolve', NeedsVirtualized: true },
        { Index: 12, Group: 'Clusters', Name: 'Hi-Z pyramid',                   Short: 'Hi-Z' },
        { Index: 13, Group: 'Clusters', Name: 'Glass layers (front / back)',    Short: 'Glass layers' },
        { Index: 14, Group: 'Clusters', Name: 'LOD levels (near to far)',       Short: 'LOD levels' }
    ];

    const DistanceViews = [['Normals', 1], ['Step heat', 2], ['NEE / cache', 3]];

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       DEFAULTS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The demo's defaults (RefreshToggles, IlluminationSettings, VarianceSettings ×2: wavelet levels = WaveletIterations + 1) with Default.levelspace's own values
    //    (quality 2, exposure 0.12 EV); the measured refinements off, as the shipped level has them.
    const DemoDefaults = {
        Renderer: 'Rtx', QualityTier: 2, RenderScale: 1.0, TraceScale: 2, InitialCandidates: 2,
        GlobalIllumination: true, MaxBounces: 3, MultiBounceNee: true, RussianRoulette: true,
        TemporalReuse: true, SpatialReuse: true, VisibilityReuse: true, RadianceRefresh: true,
        SpatialCount: 5, SpatialRadius: 20, TemporalTallyLimit: 20,
        IndirectScale: 1.0, SkyScale: 1.0, IncandescenceScale: 1.0, CardRadiosity: true, CardTileBudget: 0,
        Reflection: true, RayTracedReflections: true, ReflectionBounces: 2, ReflectionScale: 1.0, ReflectionRoughness: 0.8,
        MirrorFilter: false, MirrorTrace: false,
        DirectShadow: true, RayTracedShadows: true, FireflyFilter: true, SolarDisc: false, StratifiedSun: false, PenumbraFilter: false,
        VarianceFilter: true, Antialias: true,
        DiffuseWaveletLevels: 4, DiffuseTallyLimit: 24, DiffuseMinimumWeight: 0.08, DiffuseColourSigma: 4.0,
        GlossyWaveletLevels: 3, GlossyTallyLimit: 12, GlossyMinimumWeight: 0.12, GlossyColourSigma: 3.0,
        AutoExposure: true, ExposureCompensation: 0.12,
        VirtualizedGeometry: false, VirtualizedStreamSlots: 0, ClusteredGeometry: true, ClusterTriangles: 128, ClusterBounds: false,
        FreezeDistance: false, LiveRefresh: true,
        Inspection: 0, DistanceInspection: 1, HudVisible: false, InsetEnabled: false, SliceHeight: 1.0,
        TargetRate: 60
    };

    // 📝 SolarDisc, StratifiedSun, PenumbraFilter, MirrorFilter: Documentation/Plans/Ongoing/RayTracedLighting.md's recommendation.
    const MeasuredRecommendation = { SolarDisc: true, StratifiedSun: true, PenumbraFilter: true, MirrorFilter: true };

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     AVAILABILITY
    //------------------------------------------------------------------------------------------------------------------------

    const Available = { Enabled: true, Reason: '' };
    const Unavailable = Reason => ({ Enabled: false, Reason });

    // 📝 Whether a setting applies under the others, and why not: the inspector greys a row out instead of hiding it.
    function ResolveAvailability(S, Key)
    {
        const Analytic = S.Renderer === 'Analytic';
        const Rtx      = S.Renderer === 'Rtx';
        const LightingKeys = ['GlobalIllumination', 'MaxBounces', 'MultiBounceNee', 'RussianRoulette', 'TemporalReuse', 'SpatialReuse',
                              'VisibilityReuse', 'RadianceRefresh', 'SpatialCount', 'SpatialRadius', 'TemporalTallyLimit', 'IndirectScale',
                              'SkyScale', 'IncandescenceScale', 'CardRadiosity', 'CardTileBudget', 'InitialCandidates', 'TraceScale',
                              'Reflection', 'RayTracedReflections', 'ReflectionBounces', 'ReflectionScale', 'ReflectionRoughness', 'MirrorFilter',
                              'MirrorTrace', 'RayTracedShadows', 'FireflyFilter', 'SolarDisc', 'StratifiedSun', 'PenumbraFilter', 'VarianceFilter',
                              'DiffuseWaveletLevels', 'DiffuseTallyLimit', 'DiffuseMinimumWeight', 'DiffuseColourSigma', 'GlossyWaveletLevels',
                              'GlossyTallyLimit', 'GlossyMinimumWeight', 'GlossyColourSigma', 'FreezeDistance'];
        if (Analytic && LightingKeys.includes(Key)) return Unavailable('analytic renderer');
        const RtxKeys = ['MaxBounces', 'InitialCandidates', 'MultiBounceNee', 'RussianRoulette', 'VisibilityReuse', 'RayTracedShadows',
                         'RayTracedReflections', 'ReflectionBounces', 'SolarDisc', 'StratifiedSun', 'PenumbraFilter', 'MirrorFilter', 'MirrorTrace',
                         'FireflyFilter'];
        if (!Rtx && RtxKeys.includes(Key)) return Unavailable('RTX only');
        const IndirectKeys = ['MaxBounces', 'InitialCandidates', 'MultiBounceNee', 'RussianRoulette', 'TemporalReuse', 'SpatialReuse',
                              'VisibilityReuse', 'RadianceRefresh', 'SpatialCount', 'SpatialRadius', 'TemporalTallyLimit', 'IndirectScale'];
        if (!S.GlobalIllumination && IndirectKeys.includes(Key)) return Unavailable('indirect GI off');
        if ((Key === 'SpatialCount' || Key === 'SpatialRadius') && !S.SpatialReuse) return Unavailable('spatial reuse off');
        if (Key === 'TemporalTallyLimit' && !S.TemporalReuse) return Unavailable('temporal reuse off');
        if (Key === 'CardTileBudget' && !S.CardRadiosity) return Unavailable('card radiosity off');
        const ReflectionKeys = ['RayTracedReflections', 'ReflectionBounces', 'ReflectionScale', 'ReflectionRoughness', 'MirrorFilter', 'MirrorTrace'];
        if (!S.Reflection && ReflectionKeys.includes(Key)) return Unavailable('reflections off');
        if ((Key === 'ReflectionBounces' || Key === 'MirrorFilter' || Key === 'MirrorTrace') && !S.RayTracedReflections)
            return Unavailable('needs RTX reflection rays');
        if (Key === 'MirrorTrace' && !S.MirrorFilter) return Unavailable('needs the mirror filter');
        const ShadowKeys = ['RayTracedShadows', 'SolarDisc', 'StratifiedSun', 'PenumbraFilter'];
        if (!S.DirectShadow && ShadowKeys.includes(Key)) return Unavailable('sun shadows off');
        if ((Key === 'StratifiedSun' || Key === 'PenumbraFilter') && !S.RayTracedShadows) return Unavailable('needs ray-traced shadows');
        if (Key.startsWith('Diffuse') || Key.startsWith('Glossy'))
            if (!S.VarianceFilter) return Unavailable('denoiser off');
        if (Key === 'VirtualizedStreamSlots' && !S.VirtualizedGeometry) return Unavailable('virtualized geometry off');
        if ((Key === 'ClusterTriangles' || Key === 'ClusterBounds') && !S.ClusteredGeometry) return Unavailable('cluster culling off');
        if (Key === 'DistanceInspection' && S.Inspection !== 6) return Unavailable('BVH / distance field view only');
        if (Key === 'SliceHeight' && !S.InsetEnabled) return Unavailable('slice inset off');
        return Available;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    EXTENTS AND RAYS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 IlluminationHost.js ReconstructScreenTargets: the viewport's device pixels × the render scale, pulled under the grade's
    //    pixel ceiling, then the trace extent ⌈render ÷ TraceScale⌉ that ReSTIR GI and the reflections resolve at.
    function ResolveExtents(S, ViewportWidth, ViewportHeight, PixelRatio)
    {
        const Grade = QualityGrades[S.QualityTier];
        let Ratio = Math.min(PixelRatio || 1, 2) * S.RenderScale;
        const Pixels = ViewportWidth * ViewportHeight * Ratio * Ratio;
        if (Pixels > Grade.PixelLimit) Ratio *= Math.sqrt(Grade.PixelLimit / Pixels);
        const Render = [Math.max(64, Math.floor(ViewportWidth * Ratio)), Math.max(64, Math.floor(ViewportHeight * Ratio))];
        const Trace  = [Math.ceil(Render[0] / S.TraceScale), Math.ceil(Render[1] / S.TraceScale)];
        const Screen = [Math.round(ViewportWidth * Math.min(PixelRatio || 1, 2)), Math.round(ViewportHeight * Math.min(PixelRatio || 1, 2))];
        return { Screen, Render, Trace, Grade, Capped: Pixels > Grade.PixelLimit };
    }

    // 📝 Rays one refresh may trace at most, from the settings alone (every path to its last bounce, every pixel reflective):
    //    ① sun visibility per render pixel, ② M paths × bounces per trace pixel with an NEE ray per vertex, ③ the visibility
    //    re-trace of reused samples, ④ reflection paths per trace pixel, ⑤ the mirror trace per render pixel.
    function CountRays(S, Extents)
    {
        if (S.Renderer !== 'Rtx') return { Sun: 0, Paths: 0, Visibility: 0, Reflection: 0, Mirror: 0, Total: 0 };
        const RenderPixels = Extents.Render[0] * Extents.Render[1];
        const TracePixels  = Extents.Trace[0] * Extents.Trace[1];
        const Vertex       = S.MultiBounceNee ? 2 : 1;
        const Sun          = S.DirectShadow && S.RayTracedShadows ? RenderPixels : 0;
        const Paths        = S.GlobalIllumination ? TracePixels * S.InitialCandidates * S.MaxBounces * Vertex : 0;
        const Visibility   = S.GlobalIllumination && S.VisibilityReuse ? TracePixels : 0;
        const Reflective   = S.Reflection && S.RayTracedReflections;
        const Reflection   = Reflective ? TracePixels * S.ReflectionBounces * 2 : 0;
        const Mirror       = Reflective && S.MirrorFilter && S.MirrorTrace ? RenderPixels : 0;
        return { Sun, Paths, Visibility, Reflection, Mirror, Total: Sun + Paths + Visibility + Reflection + Mirror };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     COST ESTIMATE
    //------------------------------------------------------------------------------------------------------------------------

    // 🚧 A prototype model, not a measurement: each submission's milliseconds scale with the pixels and rays it touches, around a
    //    nominal 1920 × 1080 refresh. The C++ inspector replaces it with the measured submission times (DeviceExchange
    //    SubmissionTiming), so these bars only show how the settings move the work.
    function EstimateSubmissions(S, Extents)
    {
        const RenderShare = (Extents.Render[0] * Extents.Render[1]) / (1920 * 1080);
        const TraceShare  = (Extents.Trace[0] * Extents.Trace[1]) / (960 * 540);
        const ScreenShare = (Extents.Screen[0] * Extents.Screen[1]) / (1920 * 1080);
        const Grade = Extents.Grade;
        const Submissions = [];
        const Push = (Name, Group, Milliseconds) => { if (Milliseconds > 0.005) Submissions.push({ Name, Group, Milliseconds }); };
        Push('Visibility raster', 'Geometry', 0.9 * RenderShare * (S.VirtualizedGeometry ? 0.62 : 1.0));
        if (S.Renderer === 'Analytic')
        {
            Push('Analytic illumination', 'Lighting', 0.55 * RenderShare);
        }
        else
        {
            const Rtx = S.Renderer === 'Rtx';
            Push('Direct light', 'Lighting', 0.75 * RenderShare);
            if (S.DirectShadow)
                Push(Rtx && S.RayTracedShadows ? 'Ray-traced sun shadows' : 'Shadow cascades', 'Shadows',
                     Rtx && S.RayTracedShadows ? 1.05 * RenderShare : 0.45 * Math.pow(Grade.ShadowExtent / 2048, 2) + 0.1);
            if (Rtx && S.DirectShadow && S.RayTracedShadows && S.PenumbraFilter) Push('Penumbra filter', 'Shadows', 0.32 * RenderShare);
            Push('Distance field update', 'Geometry', S.FreezeDistance ? 0.02 : 0.35 * Math.pow(Grade.ClipResolution / 128, 3));
            if (S.CardRadiosity)
                Push('Surface cache relight', 'Lighting', 0.22 + 0.55 * (S.CardTileBudget || Grade.TileBudget) / 2048);
            if (S.GlobalIllumination)
            {
                const Depth = (0.45 + 0.55 * S.MaxBounces / 3) * (S.MultiBounceNee ? 1.0 : 0.72) * (S.RussianRoulette ? 0.82 : 1.0);
                Push(Rtx ? 'ReSTIR initial paths' : 'Distance-field gather', 'Global illumination',
                     (Rtx ? 2.1 * S.InitialCandidates * Depth : 1.7 * (Grade.Steps / 96)) * TraceShare);
                if (S.TemporalReuse) Push('Temporal reuse', 'Global illumination', 0.24 * TraceShare);
                if (S.SpatialReuse) Push('Spatial reuse', 'Global illumination', 0.07 * S.SpatialCount * TraceShare * (0.8 + S.SpatialRadius / 100));
                if (Rtx && S.VisibilityReuse) Push('Visibility validation', 'Global illumination', 0.42 * TraceShare);
            }
            if (S.Reflection)
            {
                const Traced = Rtx && S.RayTracedReflections;
                Push(Traced ? 'Reflection paths' : 'Distance-field reflections', 'Reflections',
                     (Traced ? 0.7 * S.ReflectionBounces : 0.65) * TraceShare * (0.35 + 0.65 * (S.ReflectionRoughness - 0.15) / 0.85));
                if (Traced && S.MirrorFilter) Push('Mirror filter', 'Reflections', 0.12 * RenderShare);
                if (Traced && S.MirrorFilter && S.MirrorTrace) Push('Mirror trace', 'Reflections', 1.6 * RenderShare);
            }
            if (S.VarianceFilter)
                Push('Variance filters', 'Denoising', (0.09 * S.DiffuseWaveletLevels + 0.09 * S.GlossyWaveletLevels + 0.12) * TraceShare * 1.6);
            Push('Recombination', 'Image', 0.2 * RenderShare);
        }
        Push('Environment overlay', 'Image', 0.95 * ScreenShare);
        if (S.Antialias) Push('Temporal antialiasing', 'Image', 0.16 * ScreenShare);
        if (S.AutoExposure) Push('Exposure', 'Image', 0.05);
        const Total = Submissions.reduce((Sum, Submission) => Sum + Submission.Milliseconds, 0);
        return { Submissions, Total };
    }

    const GroupColours = {
        'Geometry': '#8c9bb0', 'Lighting': '#e3c27e', 'Shadows': '#c9a36a', 'Global illumination': '#9fb4d6',
        'Reflections': '#a99ad8', 'Denoising': '#7fc1b0', 'Image': '#9a9a9a'
    };

    //------------------------------------------------------------------------------------------------------------------------
    //                                                   LEVELSPACE RECORD
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The one record that replaces rec_diagnostics, rec_clusters, rec_pipeline, rec_sdf and rec_cards. Keys already read by
    //    SceneWorld.cpp keep their names; the rest are proposed for the C++ step.
    const LevelspaceKeys = [
        ['hardware_ray_query', S => S.Renderer === 'Rtx'], ['analytic_illumination', S => S.Renderer === 'Analytic'],
        ['quality_tier', S => S.QualityTier], ['render_scale', S => S.RenderScale], ['trace_scale', S => S.TraceScale],
        ['initial_candidates', S => S.InitialCandidates],
        ['global_illumination', S => S.GlobalIllumination], ['max_bounces', S => S.MaxBounces], ['multi_bounce_nee', S => S.MultiBounceNee],
        ['russian_roulette', S => S.RussianRoulette], ['temporal_reuse', S => S.TemporalReuse], ['spatial_reuse', S => S.SpatialReuse],
        ['visibility_reuse', S => S.VisibilityReuse], ['radiance_refresh', S => S.RadianceRefresh], ['spatial_count', S => S.SpatialCount],
        ['spatial_radius', S => S.SpatialRadius], ['temporal_tally_limit', S => S.TemporalTallyLimit], ['indirect_scale', S => S.IndirectScale],
        ['sky_scale', S => S.SkyScale], ['incandescence_scale', S => S.IncandescenceScale], ['card_radiosity', S => S.CardRadiosity],
        ['card_tile_budget', S => S.CardTileBudget],
        ['reflections', S => S.Reflection], ['ray_traced_reflections', S => S.RayTracedReflections], ['reflection_bounces', S => S.ReflectionBounces],
        ['reflection_scale', S => S.ReflectionScale], ['reflection_roughness', S => S.ReflectionRoughness], ['mirror_filter', S => S.MirrorFilter],
        ['mirror_trace', S => S.MirrorTrace],
        ['direct_shadow', S => S.DirectShadow], ['ray_traced_shadows', S => S.RayTracedShadows], ['firefly_filter', S => S.FireflyFilter],
        ['solar_disc', S => S.SolarDisc], ['stratified_sun', S => S.StratifiedSun], ['penumbra_filter', S => S.PenumbraFilter],
        ['variance_filter', S => S.VarianceFilter], ['temporal_aa', S => S.Antialias],
        ['diffuse_wavelet_levels', S => S.DiffuseWaveletLevels], ['diffuse_tally_limit', S => S.DiffuseTallyLimit],
        ['diffuse_minimum_weight', S => S.DiffuseMinimumWeight], ['diffuse_colour_sigma', S => S.DiffuseColourSigma],
        ['glossy_wavelet_levels', S => S.GlossyWaveletLevels], ['glossy_tally_limit', S => S.GlossyTallyLimit],
        ['glossy_minimum_weight', S => S.GlossyMinimumWeight], ['glossy_colour_sigma', S => S.GlossyColourSigma],
        ['auto_exposure', S => S.AutoExposure], ['exposure_ev', S => S.ExposureCompensation],
        ['virtualized_geometry', S => S.VirtualizedGeometry], ['virtualized_stream_slots', S => S.VirtualizedStreamSlots],
        ['clustered_geometry', S => S.ClusteredGeometry], ['max_cluster_triangles', S => S.ClusterTriangles],
        ['cluster_bounds_overlay', S => S.ClusterBounds], ['freeze_distance', S => S.FreezeDistance], ['live_refresh', S => S.LiveRefresh],
        ['inspection_mode', S => S.Inspection], ['distance_inspection', S => S.DistanceInspection], ['readout_overlay', S => S.HudVisible],
        ['slice_inset', S => S.InsetEnabled], ['slice_height_m', S => S.SliceHeight]
    ];
    const ExistingKeys = new Set(['hardware_ray_query', 'analytic_illumination', 'quality_tier', 'global_illumination', 'temporal_aa',
                                  'exposure_ev', 'inspection_mode', 'virtualized_geometry', 'virtualized_stream_slots', 'solar_disc',
                                  'stratified_sun', 'penumbra_filter', 'mirror_filter', 'mirror_trace']);

    function EncodeLevelspace(S)
    {
        const Lines = ['[[records]]', 'identity = "rec_render"', 'type = "RenderPipeline"', 'name = "Render settings"', '[records.configuration]',
                       'enabled = true'];
        for (const [Key, Read] of LevelspaceKeys)
        {
            const Setting = Read(S);
            const Text = typeof Setting === 'boolean' ? String(Setting) : Number.isInteger(Setting) ? String(Setting) : Number(Setting).toFixed(2);
            Lines.push(`${Key} = ${Text}`);
        }
        return Lines;
    }

    return {
        QualityGrades, InspectionViews, DistanceViews, DemoDefaults, MeasuredRecommendation, ResolveAvailability, ResolveExtents,
        CountRays, EstimateSubmissions, GroupColours, EncodeLevelspace, ExistingKeys
    };
})();
