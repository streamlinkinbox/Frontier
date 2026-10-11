//============================================================================================================================================
//                                                  SCENEMATERIALRESOLVE.SLANG
//============================================================================================================================================
// 📦 The hit → ShadingRecord decode, shared by every path that shades a ray hit.
//
//    WHY THIS FILE EXISTS.  It was ReSTIRViewport.slang's private §R4b. The surfel passes could not reach it, so
//    SurfelIrradianceUpdate.slang shaded its bounce hits with `vec3 hitN = -wi;` — a normal invented from the ray
//    direction — and no material decode at all. Every bounce in the GI field was therefore achromatic and
//    geometry-blind: no base colour, no metal, no coat, no transmission, and a cosine term that depended on which
//    way the ray happened to point rather than on the surface it struck. A material cannot bleed its colour into
//    a room if the bounce never reads the material.
//
//    UnifiedMaterialEvaluation.h already makes this argument for the CPU side, and it applies verbatim here:
//    transport, visibility and sampling are legitimately different between the raster, surfel and ReSTIR paths.
//    Which BSDF you evaluate once a ray knows what it hit is not.
//
//    THE CONTRACT.  This file declares no bindings of its own; the includer must already have declared, with the
//    layout qualifiers its own pass uses:
//
//        GpuTriangle      Triangles[]        ─ flat triangle soup, indexed by instance.FlatTriangleOffset + primitive
//        GpuVertex        Vertices[]         ─ shared vertex records (normal / tangent / texcoord)
//        uint             Indices[]          ─ shared index buffer
//        GpuMaterialSlab  MaterialSlabs[]    ─ the 304 B authored slabs
//        sampler2D        Textures[]         ─ the bindless table (constants-only passes may bind an empty table)
//
//    and must have included MaterialEvaluation.slang (for ShadingRecord, the kChannel* table and ReflectanceConsumes)
//    and declared `const uint kMaterialTextureNone`. That is the same include-after-your-buffers discipline
//    TraversalCWBVH.slang already uses, for the same reason: one body of arithmetic, many binding sets.

//------------------------------------------------------------------------------------------------------------------------
//                                          R4b: MATERIAL RESOLVE (slab + textures → ShadingRecord)
//------------------------------------------------------------------------------------------------------------------------
// Barycentrics are rebuilt from the hit position against the flat triangle (one convention for the raster-fed primary
//    hit and the CWBVH bounce hit). Vertex normals / tangents / UVs come from the shared VertexRecord buffer (binding 11/12)
//    through the instance's index range — the same data the R2 raster consumes, so nothing is uploaded twice.
// Texture LOD: ray cones (Akenine-Möller et al. 2019/2021). Cone width w = t · pixelSpread (primary) + t₂ · pixelSpread
//    (bounce, ⚠️ no curvature term yet — flat-surface spread only); λ = ½ log2(texW·texH · uvArea / worldArea) + log2(w / |n·d|).
// ⚠️ Texture channel selection is not part of MaterialSlabRecord; the shader assumes the glTF packing the codec emits:
//    metalness = B and roughness = G of the metallicRoughness image, occlusion = R, opacity = A of base colour, others RGB.

vec3 Barycentric(vec3 p, vec3 a, vec3 b, vec3 c)
{
    vec3 e0 = b - a, e1 = c - a, e2 = p - a;
    float d00 = dot(e0, e0), d01 = dot(e0, e1), d11 = dot(e1, e1), d20 = dot(e2, e0), d21 = dot(e2, e1);
    float den = d00 * d11 - d01 * d01;
    if (abs(den) < 1e-20) return vec3(1.0, 0.0, 0.0);
    float v = (d11 * d20 - d01 * d21) / den;
    float w = (d00 * d21 - d01 * d20) / den;
    return vec3(1.0 - v - w, v, w);
}

// Texture slot of channel C (0-15) of a slab, 0xFFFF = none — mirrors MaterialSlabTexture in SceneRecords.slang.
uint SlabTexture(GpuMaterialSlab S, uint C)
{
    uint Word = C < 8u ? S.TextureSlots0[C / 2u] : S.TextureSlots1[(C - 8u) / 2u];
    return (Word >> ((C & 1u) * 16u)) & 0xFFFFu;
}
// M3: kChannel* / kReflectance* / ReflectanceConsumes live in MaterialEvaluation.slang now (single source of truth,
// CPU-tested 1:1); included above.
uint MaterialSelection(GpuMaterial mat) { return (mat.Slabs.z >> 8u) & 0xFu; }

struct SurfaceFrame         // everything the kernel needs at a hit besides the ShadingRecord
{
    vec3  Normal;           // shading normal (interpolated + normal map), facing the viewer side
    vec3  Tangent;          // orthonormal frame axes for the local BSDF space
    vec3  Bitangent;
    vec3  Geometric;        // face normal facing the viewer side (ray offsets, shadow rays)
    vec3  Emission;         // slab emission × texture [nit]
    float Opacity;          // geometry_opacity × texture alpha
    float Occlusion;        // baked AO — multiplies the ambient fills (debug floor, moonlight), never the direct light
    float LodBias;          // ½ log2(uvArea / worldArea) + log2(coneWidth / |n·d|): add ½ log2(texW·texH) per texture
    bool  ThinWalled;       // K4: SlabFlags bit0 (P14.z) — foil (thin-wall compound) vs solid (tracer walks the medium)
};

vec3 ToLocal(SurfaceFrame F, vec3 w)   { return vec3(dot(w, F.Tangent), dot(w, F.Bitangent), dot(w, F.Normal)); }
vec3 FromLocal(SurfaceFrame F, vec3 w) { return F.Tangent * w.x + F.Bitangent * w.y + F.Normal * w.z; }

vec4 SampleChannel(GpuMaterialSlab S, uint channel, vec2 uv, float lodBias, vec4 fallback)
{
    #ifdef FRONTIER_CONSTANT_MATERIALS
    return fallback;
    #else
    uint slot = SlabTexture(S, channel);
    #ifdef FRONTIER_SDF_MATERIALS
    // 📝 A single guard covers both the unbound sentinel and slots outside the resident descriptor table.
    if (slot >= uint(PC.CameraEye.w)) return fallback;
    #else
    if (slot == 0xFFFFu) return fallback;
    #endif
    ivec2 size = textureSize(Textures[nonuniformEXT(slot)], 0);
    float lod = lodBias + 0.5 * log2(float(size.x * size.y));
    return textureLod(Textures[nonuniformEXT(slot)], uv, lod);
    #endif
}

// M1: the selection gate — skips the texture op entirely when the channel is dead (explicit LOD keeps this legal
// in non-uniform control flow). `live` is per material (constant weight / selection), never per texel.
vec4 SampleChannelGated(GpuMaterialSlab S, uint channel, vec2 uv, float lodBias, vec4 fallback, bool live)
{
    if (!live) return fallback;
    return SampleChannel(S, channel, uv, lodBias, fallback);
}

// Resolve the material at a hit. `viewDir` points from the hit towards the viewer / previous vertex; `coneWidth` is the
//    ray-cone footprint at the hit [m]. Fills the ShadingRecord from the first slab (Tier A: one resolved slab) or, when
//    the material has no slab, from the 64 B header (constant base colour / roughness / metalness).
ShadingRecord ResolveMaterial(GpuMaterial mat, GpuInstance instance, uint primitive, vec3 hitPos, vec3 viewDir, float coneWidth, out SurfaceFrame F)
{
    GpuTriangle tri = Triangles[instance.FlatTriangleOffset + primitive];
    #ifdef FRONTIER_SDF_MATERIALS
    uint GeometryStart=instance.FirstIndex+primitive*3u;
    tri.v0.xyz=(instance.World*vec4(Vertices[Indices[GeometryStart]+instance.VertexOffset].Position.xyz,1.0)).xyz;
    tri.v1.xyz=(instance.World*vec4(Vertices[Indices[GeometryStart+1u]+instance.VertexOffset].Position.xyz,1.0)).xyz;
    tri.v2.xyz=(instance.World*vec4(Vertices[Indices[GeometryStart+2u]+instance.VertexOffset].Position.xyz,1.0)).xyz;
    #endif
    vec3 bc = Barycentric(hitPos, tri.v0.xyz, tri.v1.xyz, tri.v2.xyz);
    vec3 e1 = tri.v1.xyz - tri.v0.xyz, e2 = tri.v2.xyz - tri.v0.xyz;
    vec3 faceCross = cross(e1, e2);
    float worldArea = 0.5 * length(faceCross);
    vec3 ng = worldArea > 0.0 ? faceCross / (2.0 * worldArea) : vec3(0.0, 0.0, 1.0);
    if (dot(ng, viewDir) < 0.0) ng = -ng;

    // Vertex attributes through the instance's index range (object space → world with the instance matrix).
    uint base = instance.FirstIndex + primitive * 3u;
    GpuVertex va = Vertices[Indices[base + 0u] + instance.VertexOffset];
    GpuVertex vb = Vertices[Indices[base + 1u] + instance.VertexOffset];
    GpuVertex vc = Vertices[Indices[base + 2u] + instance.VertexOffset];
    mat3 world = mat3(instance.World);   // ⚠️ normals use the linear part directly (rigid / uniform scale); non-uniform scale is not corrected
    vec2 uv  = bc.x * va.Texcoord.xy + bc.y * vb.Texcoord.xy + bc.z * vc.Texcoord.xy;
#ifdef FRONTIER_SDF_MATERIALS
    mat3 NormalProjection = abs(determinant(world)) > 1e-12 ? transpose(inverse(world)) : world;
    vec3 nI  = NormalProjection * (bc.x * va.Normal.xyz + bc.y * vb.Normal.xyz + bc.z * vc.Normal.xyz);
#else
    vec3 nI  = world * (bc.x * va.Normal.xyz + bc.y * vb.Normal.xyz + bc.z * vc.Normal.xyz);
#endif
    vec4 tI  = bc.x * va.Tangent + bc.y * vb.Tangent + bc.z * vc.Tangent;
    vec3 n   = dot(nI, nI) > 1e-12 ? normalize(nI) : ng;
    if (dot(n, ng) < 0.0) n = -n;                                   // interpolated normal follows the viewer-facing side
    vec3 t;
    if (dot(tI.xyz, tI.xyz) > 1e-12) t = world * tI.xyz;
    else                                                            // no authored tangents: derive from the UV parametrisation of the face
    {
        vec2 duv1 = vb.Texcoord.xy - va.Texcoord.xy, duv2 = vc.Texcoord.xy - va.Texcoord.xy;
        float det = duv1.x * duv2.y - duv2.x * duv1.y;
        t = abs(det) > 1e-12 ? (e1 * duv2.y - e2 * duv1.y) / det : (abs(n.z) < 0.999 ? cross(vec3(0.0, 0.0, 1.0), n) : vec3(1.0, 0.0, 0.0));
        tI.w = 1.0;
    }
    t = t - n * dot(n, t);
    t = dot(t, t) > 1e-12 ? normalize(t) : normalize(abs(n.z) < 0.999 ? cross(vec3(0.0, 0.0, 1.0), n) : vec3(1.0, 0.0, 0.0));
    float handed = tI.w < 0.0 ? -1.0 : 1.0;
    vec3 b = cross(n, t) * handed;

    // Ray-cone LOD bias (texture-size term added per fetch).
    vec2 duvA = vb.Texcoord.xy - va.Texcoord.xy, duvB = vc.Texcoord.xy - va.Texcoord.xy;
    float uvArea = 0.5 * abs(duvA.x * duvB.y - duvB.x * duvA.y);
    F.LodBias = (uvArea > 0.0 && worldArea > 0.0) ? 0.5 * log2(uvArea / worldArea) + log2(max(coneWidth, 1e-6) / max(abs(dot(ng, viewDir)), 1e-3)) : 0.0;

    ShadingRecord m;
    m.AutomotiveData=vec4(0.0);m.AutomotiveDensity=0.0;m.AutomotiveFootprint=0.0;
    m.BaseColor = mat.Albedo.xyz; m.Metalness = mat.Emissive.w; m.DiffuseRoughness = 0.0;
    m.SpecularWeight = 1.0; m.SpecularColor = vec3(1.0); m.SpecularRoughness = mat.Albedo.w; m.SpecularAnisotropy = 0.0; m.AnisotropyAngle = 0.0; m.SpecularIor = 1.5;
    m.ThinFilmWeight = 0.0; m.ThinFilmThickness = 0.5; m.ThinFilmIor = 1.4;
    m.HazinessWeight = 0.0; m.HazinessRoughness = 0.5;
    m.CoatWeight = 0.0; m.CoatColor = vec3(1.0); m.CoatRoughness = 0.0; m.CoatAnisotropy = 0.0; m.CoatIor = 1.6; m.CoatDarkening = 1.0;
    m.CoatTangent = vec3(1.0, 0.0, 0.0); m.CoatNormal = vec3(0.0, 0.0, 1.0);   // M2: identity coat frame
    m.FuzzWeight = 0.0; m.FuzzColor = vec3(1.0); m.FuzzRoughness = 0.5;
    m.Emission = mat.Emissive.xyz;
    m.Selection = MaterialSelection(mat);   // M3: per-selection shading path (covers the header path too)
    m.SssWeight = 0.0; m.SssColor = vec3(1.0); m.SssRadius = 0.0; m.SssRadiusScale = vec3(0.0); m.SssThickness = 0.0;   // M5: SSS off (branch-gated, never read at 0; slab resolve in the kernel milestone)
    m.TransmissionWeight = 0.0; m.TransmissionColor = vec3(1.0); m.TransmissionDepth = 0.0; m.TransmissionThickness = 0.0;   // K2: was UNSET (garbage TransmitMix → random T-branch). Opaque default; the slab resolve below reads P3.yzw/P4 (resident — no CPU change was needed)
    F.Emission = mat.Emissive.xyz; F.Opacity = 1.0; F.Occlusion = 1.0; F.ThinWalled = true;   // K4: header path (no slab) is always foil — TransmitMix = 0 there anyway, so the flag is inert

    if (mat.Slabs.y > 0u)
    {
        GpuMaterialSlab S = MaterialSlabs[mat.Slabs.x];   // Tier A: the FIRST slab (top-most after flatten; multi-slab stacks arrive with R5)
        uint selection = MaterialSelection(mat);   // M1: Sultan-18 §9 gate — unread channels are not sampled
        vec4 P0 = S.Payload[0], P1 = S.Payload[1], P2 = S.Payload[2], P3 = S.Payload[3];
        vec4 P4 = S.Payload[4], P6 = S.Payload[6], P7 = S.Payload[7];   // K2: P5 (transmission scatter/dispersion) is v1-out — surface BTDF only, never declared
        vec4 P8 = S.Payload[8], P9 = S.Payload[9], P10 = S.Payload[10], P11 = S.Payload[11], P12 = S.Payload[12], P13 = S.Payload[13];
        vec4 P14 = S.Payload[14];   // K4: .z = SlabFlags bit0 thin-walled (floats 56–59 = glint pair + flags + mask)
        float normalScale = uintBitsToFloat(S.Runtime.y), occlusionStrength = uintBitsToFloat(S.Runtime.z);

        vec4 baseTex = SampleChannel(S, kChannelBaseColor, uv, F.LodBias, vec4(1.0));   // never gated: Unlit reads it
        m.BaseColor          = P0.x * P0.yzw * baseTex.xyz;
        m.Metalness          = P1.x * SampleChannelGated(S, kChannelMetalness, uv, F.LodBias, vec4(1.0),
                               P1.x > 0.0 || ReflectanceConsumes(selection, kChannelMetalness)).z;   // glTF: B
        m.DiffuseRoughness   = P1.y;
        m.SpecularWeight     = P1.z;
        m.SpecularColor      = vec3(P1.w, P2.xy) * SampleChannelGated(S, kChannelSpecularColor, uv, F.LodBias, vec4(1.0),
                               P1.z > 0.0 || ReflectanceConsumes(selection, kChannelSpecularColor)).xyz;
        m.SpecularRoughness  = P2.z * SampleChannelGated(S, kChannelRoughness, uv, F.LodBias, vec4(1.0),
                               (P1.z > 0.0 && P2.z > 0.0) || ReflectanceConsumes(selection, kChannelRoughness)).y;   // glTF: G
        // M2: anisotropy — direction from RG (tangent space), strength scale from B (KHR_materials_anisotropy);
        // the slab rotation composes additively. Fallback = +tangent, strength 1 (constant path).
        vec4 anisoTex = SampleChannelGated(S, kChannelAnisotropy, uv, F.LodBias, vec4(1.0, 0.5, 1.0, 1.0),
                            P2.w != 0.0 || ReflectanceConsumes(selection, kChannelAnisotropy));
        m.SpecularAnisotropy = P2.w * anisoTex.z;
        vec2 anisoDir = anisoTex.xy * 2.0 - 1.0;
        m.AnisotropyAngle = dot(anisoDir, anisoDir) > 1e-12 ? atan(anisoDir.y, anisoDir.x) + S.Slate2.x : S.Slate2.x;
        m.SpecularIor        = P3.x;
        // K2: M4 slab resolve — the payload already carries the volume pair (resident, no CPU change); the kernel
        // just never read it. Weight × kChannelTransmission.x (KHR: R = transmissionFactor; unbound → ×1.0).
        // Thickness is NOT in the slab (OpenPBR §5 has none): foil Beer = 1 (documented limit); solid traverses
        // true geometry (K4) and treats 0 as the no-exit fallback.
        m.TransmissionWeight = P3.y * SampleChannelGated(S, kChannelTransmission, uv, F.LodBias, vec4(1.0),
                               P3.y > 0.0 || ReflectanceConsumes(selection, kChannelTransmission)).x;
        m.TransmissionColor  = vec3(P3.z, P3.w, P4.x);
        m.TransmissionDepth  = P4.y;
        // K2: M5 slab resolve — weight × kChannelSubsurface.x (no KHR standard; .x by the weight-texture
        // convention; unbound → ×1.0). RadiusScale .yzw; P8.x (scatter anisotropy g) has no ShadingRecord field —
        // M5 v1 wrap doesn't consume g (v2 dipole: CPU-side first, kernel field then). Thickness is tracer-side
        // (K5 sets it per hit; the 0 default = unset).
        m.SssWeight          = P6.x * SampleChannelGated(S, kChannelSubsurface, uv, F.LodBias, vec4(1.0),
                               P6.x > 0.0 || ReflectanceConsumes(selection, kChannelSubsurface)).x;
        m.SssColor           = vec3(P6.y, P6.z, P6.w);
        m.SssRadius          = P7.x;
        m.SssRadiusScale     = vec3(P7.y, P7.z, P7.w);
        m.CoatWeight         = P8.y * SampleChannelGated(S, kChannelCoat, uv, F.LodBias, vec4(1.0),
                               P8.y > 0.0 || ReflectanceConsumes(selection, kChannelCoat)).x;
        m.CoatColor          = vec3(P8.zw, P9.x);
        m.CoatRoughness      = P9.y;
        m.CoatAnisotropy     = P9.z;
        m.CoatIor            = P9.w;
        m.CoatDarkening      = P10.x;
        m.FuzzWeight         = P10.y;
        m.FuzzColor          = vec3(P10.zw, P11.x) * SampleChannelGated(S, kChannelFuzz, uv, F.LodBias, vec4(1.0),
                               P10.y > 0.0 || ReflectanceConsumes(selection, kChannelFuzz)).xyz;
        m.FuzzRoughness      = P11.y;
        m.Emission           = P11.z * vec3(P11.w, P12.xy) * SampleChannel(S, kChannelEmission, uv, F.LodBias, vec4(1.0)).xyz;   // never gated: short-circuit + cutout read it
        m.ThinFilmWeight     = P12.z * SampleChannelGated(S, kChannelThinFilm, uv, F.LodBias, vec4(1.0),
                               P12.z > 0.0 || ReflectanceConsumes(selection, kChannelThinFilm)).x;
        m.ThinFilmThickness  = P12.w;
        m.ThinFilmIor        = P13.x;
        m.AutomotiveData=vec4(S.Slate2.zw,uv*vec2(3.4557519,1.7278760));
        m.AutomotiveDensity=P14.x;
        m.AutomotiveFootprint=exp2(F.LodBias)*3.4557519;
        m.HazinessWeight     = P13.z;
        m.HazinessRoughness  = P13.w;
        F.ThinWalled = (floatBitsToUint(P14.z) & 1u) != 0u;   // K4: SlabFlags bit0 (exact bit-cast — the record is a memcpy mirror)
        F.Emission  = m.Emission;
        F.Opacity   = P13.y * SampleChannel(S, kChannelOpacity, uv, F.LodBias, vec4(1.0)).w;   // glTF: A of base colour
        F.Occlusion = mix(1.0, SampleChannelGated(S, kChannelOcclusion, uv, F.LodBias, vec4(1.0),
                               ReflectanceConsumes(selection, kChannelOcclusion)).x, occlusionStrength);

        uint normalSlot = SlabTexture(S, kChannelNormal);
        if (normalSlot != 0xFFFFu && ReflectanceConsumes(selection, kChannelNormal))
        {
            vec3 tn = SampleChannel(S, kChannelNormal, uv, F.LodBias, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
            tn.x *= normalScale; tn.y *= normalScale;
            vec3 nm = normalize(t * tn.x + b * tn.y + n * tn.z);
            if (dot(nm, ng) > 0.0) n = nm;                         // reject maps that push the normal below the surface
            t = normalize(t - n * dot(n, t)); b = cross(n, t) * handed;
        }

        // M6 glints — slate_glint_density / slate_glint_uv_scale (P14.xy), stored since R4a and READ from here
        //    on. The automotive precedent's exact rule: flakes are tiny metallic facets, so they are a bounded
        //    deterministic normal perturbation (the SHARED AutomotiveApplyTriCoatFlakeNormal — the same body
        //    the preview and the CPU port run), never a painted dot and never a light source. Density maps to
        //    the signal's coverage through a saturating 1 − exp(−D/4), so the library's 1 → 8 row spans sparse
        //    sparkle to dense metallic salt; uv_scale × 3 brackets the preview's own frequency (24) across the
        //    row's 4 → 12. Applied AFTER the normal map (flakes ride the mapped surface) and BEFORE the coat
        //    frame derives (the coat follows the base surface, flakes included — the preview's own order).
        if (P14.x > 0.0 && m.AutomotiveData.x == 0.0)
        {
            float glintCoverage = 1.0 - exp(-P14.x * 0.25);
            vec3 gn = AutomotiveApplyTriCoatFlakeNormal(n, hitPos, glintCoverage, P14.y * 3.0, 0.37);
            if (dot(gn, ng) > 0.0) n = gn;                         // the same below-surface rejection the map obeys
            t = normalize(t - n * dot(n, t)); b = cross(n, t) * handed;
        }

        // M2: independent coat frame (clearcoat_normal_texture), derived from the POST-normal-map base frame — the coat
        // follows the base surface plus its own perturbation — then expressed in local (base-frame) coords for the lobes.
        if (ReflectanceConsumes(selection, kChannelCoatNormal) && SlabTexture(S, kChannelCoatNormal) != 0xFFFFu)
        {
            vec3 cn = SampleChannel(S, kChannelCoatNormal, uv, F.LodBias, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
            vec3 nCw = normalize(t * cn.x + b * cn.y + n * cn.z);
            if (dot(nCw, ng) > 0.0)
            {
                vec3 tCw = t - nCw * dot(nCw, t);
                tCw = dot(tCw, tCw) > 1e-12 ? normalize(tCw)
                                            : normalize(abs(nCw.z) < 0.999 ? cross(vec3(0.0, 0.0, 1.0), nCw) : vec3(1.0, 0.0, 0.0));
                m.CoatTangent = vec3(dot(tCw, t), dot(tCw, b), dot(tCw, n));
                m.CoatNormal  = vec3(dot(nCw, t), dot(nCw, b), dot(nCw, n));
            }
        }
    }

    F.Normal = n; F.Tangent = t; F.Bitangent = b; F.Geometric = ng;
    return m;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           HIT LOOKUP AND HEMISPHERICAL RESPONSE
//------------------------------------------------------------------------------------------------------------------------

// Primitive index WITHIN its instance — what ResolveMaterial expects for the vertex / uv lookup.
uint LocalPrimitive(uint flatPrimitive, uint instance)
{
    return flatPrimitive - Instances[instance].FlatTriangleOffset;
}

// The material a flat triangle carries. TriangleIndex packs it in v0.w.
uint TriangleMaterial(uint flatPrimitive)
{
    return floatBitsToUint(Triangles[flatPrimitive].v0.w);
}

// ∫ f(wo, wi)·cos θ dωi — the fraction of a UNIFORM environment this material sends back to the viewer.
//
//    A path that has an irradiance but no incoming direction (a surfel field, a sky fill) needs this and nothing
//    else; outgoing radiance is E/π × this. The obvious shortcut — evaluate f once along the normal and multiply
//    by π — is correct only for a Lambert lobe. On a clearcoated or metallic slab f(N, N) sits on the specular
//    peak, so π·f is enormous and the material renders blown-out white. Clamping does not help: the peak is
//    orders of magnitude over unity, so the clamp just paints it white instead.
//
//    Cosine-weighted quadrature, 8 stratified directions: the pdf cos/π cancels the cosine, so the estimator is
//    (π/N)·Σ f. It cannot exceed the material's true hemispherical reflectance because it IS an estimate of it.
//    Identical arithmetic to ContentInterchange/UnifiedMaterialEvaluation.h::AmbientResponse, deliberately — the
//    CPU reference and this kernel must answer the same number for the same slab.
vec3 AmbientResponse(ShadingRecord m, ResolvedLayers L, vec3 wo)
{
    if (wo.z <= 0.0) return vec3(0.0);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 8; ++i)
    {
        float u1 = (float(i) + 0.5) / 8.0;
        float u2 = 0.618033988 * float(i);
        float r = sqrt(u1);
        float phi = 6.28318530718 * (u2 - floor(u2));
        vec3 wi = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
        sum += EvaluateBsdf(m, L, wo, wi);
    }
    return sum * (3.14159265358979 / 8.0);
}
