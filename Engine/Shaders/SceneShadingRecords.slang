//============================================================================================================================================
//                                                   SCENESHADINGRECORDS.SLANG
//============================================================================================================================================
// 📦 The device-side scene records every shading pass reads: triangle soup, material header, authored slab, instance
//    and vertex. Declarations only — no bindings, no layout qualifiers, so each pass binds them at whatever slots its
//    own descriptor set uses.
//
//    These were private to ReSTIRViewport.slang, which is why a second pass that wanted to shade a ray hit had no
//    vocabulary to do it with and invented one. Include this, bind the buffers, then include SceneMaterialResolve.slang.
//
// 📝 Field maps live in Shaders/SceneRecords.slang (the raster set's view of the same bytes) and in
//    ContentInterchange/MaterialIndex.h. Nothing here may be reordered without changing both.

struct GpuTriangle          // TriangleIndex (SwapchainExchange.h), R4a layout
{
    vec4 v0;            // xyz = vertex α, w = materialIndex (floatBitsToUint)
    vec4 v1;            // xyz = vertex β, w = γ.u
    vec4 v2;            // xyz = vertex γ, w = γ.v
    vec4 uv;            // xy = α.uv, zw = β.uv
};

struct GpuMaterial          // MaterialRecord (ContentInterchange/MaterialIndex.h, 64 B) — R4a header; mirrors SceneRecords.slang
{
    vec4  Albedo;           // xyz = flattened base colour, w = specular roughness
    vec4  Emissive;         // xyz = emission radiance [nit], w = metalness
    uvec4 Slabs;            // x = SlabOffset, y = SlabCount, z = Flags, w = Complexity
    uvec2 Textures;         // x = base colour slot, y = normal slot (0xFFFFFFFF = none)
    float AlphaCutoff;
    float NormalScale;
};

struct GpuMaterialSlab      // MaterialSlabRecord (304 B) — 19 vec4; field map in SceneRecords.slang. Read by R4b shading.
{
    vec4  Payload[15];
    uvec4 TextureSlots0;
    uvec4 TextureSlots1;
    uvec4 Runtime;
    vec4  Slate2;           // M2: x = anisotropy_rotation [rad], y = direct_f0_weight (M6, 0), zw = automotive profile / sweep
};

struct GpuInstance          // InstanceRecord (SceneStructure.h / SceneRecords.slang) — only MaterialIndex is read here
{
    mat4 World;
    mat4 PreviousWorld;
    uint VertexOffset;
    uint FirstIndex;
    uint TriangleCount;
    uint MaterialIndex;
    uint ClusterOffset;
    uint ClusterCount;
    uint Flags;
    uint FlatTriangleOffset;
};

struct GpuLuminaire         // LuminaireRecord (SceneStructure.h): emissive triangle + alias entry
{
    uint  TriangleSlot;     // index into Triangles[]
    uint  InstanceIndex;
    uint  PrimitiveIndex;
    uint  AliasSlot;
    float Threshold;
    float Area;
    float Probability;
    float Pad;
};

struct GpuVertex            // VertexRecord (SceneRecords.slang, 64 B) — R4b: interpolated normals / tangents / UVs
{
    vec4 Position;
    vec4 Normal;            // xyz object space
    vec4 Tangent;           // xyz object space, w = bitangent sign
    vec4 Texcoord;          // xy
};
