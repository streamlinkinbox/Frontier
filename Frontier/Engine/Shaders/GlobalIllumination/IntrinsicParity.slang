//============================================================================================================================================
//                                                         INTRINSICPARITY.SLANG
//============================================================================================================================================
// 🧩 WGSL builtin semantics on Slang/SPIR-V: the exact GLSL.std.450 instructions Tint emits, saturating casts, select order.

#pragma once

// 📝 Every helper here exists so a ported WGSL expression keeps its WGSL meaning bit for bit: select(f, t, c) argument
//    order, saturating float → integer conversion, round-half-to-even, and the pack/unpack instructions Tint emits.

static uint AtomicDiscard;   // [-] - sink for InterlockedExchange's original value (WGSL atomicStore has none)

//------------------------------------------------------------------------------------------------------------------------
//                                                   SELECT
//------------------------------------------------------------------------------------------------------------------------

// 📝 WGSL select(False, True, Condition); component-wise for vector conditions, whole-value for a scalar condition.
float SelectBranch(float False, float True, bool Condition) { return Condition ? True : False; }
uint SelectBranch(uint False, uint True, bool Condition) { return Condition ? True : False; }
int SelectBranch(int False, int True, bool Condition) { return Condition ? True : False; }
bool SelectBranch(bool False, bool True, bool Condition) { return Condition ? True : False; }
half SelectBranch(half False, half True, bool Condition) { return Condition ? True : False; }
float2 SelectBranch(float2 False, float2 True, bool2 Condition) { return float2(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y); }
float2 SelectBranch(float2 False, float2 True, bool Condition) { return Condition ? True : False; }
float3 SelectBranch(float3 False, float3 True, bool3 Condition) { return float3(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y, Condition.z ? True.z : False.z); }
float3 SelectBranch(float3 False, float3 True, bool Condition) { return Condition ? True : False; }
float4 SelectBranch(float4 False, float4 True, bool4 Condition) { return float4(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y, Condition.z ? True.z : False.z, Condition.w ? True.w : False.w); }
float4 SelectBranch(float4 False, float4 True, bool Condition) { return Condition ? True : False; }
uint2 SelectBranch(uint2 False, uint2 True, bool2 Condition) { return uint2(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y); }
uint2 SelectBranch(uint2 False, uint2 True, bool Condition) { return Condition ? True : False; }
uint3 SelectBranch(uint3 False, uint3 True, bool3 Condition) { return uint3(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y, Condition.z ? True.z : False.z); }
uint3 SelectBranch(uint3 False, uint3 True, bool Condition) { return Condition ? True : False; }
uint4 SelectBranch(uint4 False, uint4 True, bool4 Condition) { return uint4(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y, Condition.z ? True.z : False.z, Condition.w ? True.w : False.w); }
uint4 SelectBranch(uint4 False, uint4 True, bool Condition) { return Condition ? True : False; }
int2 SelectBranch(int2 False, int2 True, bool2 Condition) { return int2(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y); }
int2 SelectBranch(int2 False, int2 True, bool Condition) { return Condition ? True : False; }
int3 SelectBranch(int3 False, int3 True, bool3 Condition) { return int3(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y, Condition.z ? True.z : False.z); }
int3 SelectBranch(int3 False, int3 True, bool Condition) { return Condition ? True : False; }
int4 SelectBranch(int4 False, int4 True, bool4 Condition) { return int4(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y, Condition.z ? True.z : False.z, Condition.w ? True.w : False.w); }
int4 SelectBranch(int4 False, int4 True, bool Condition) { return Condition ? True : False; }
bool2 SelectBranch(bool2 False, bool2 True, bool2 Condition) { return bool2(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y); }
bool2 SelectBranch(bool2 False, bool2 True, bool Condition) { return Condition ? True : False; }
bool3 SelectBranch(bool3 False, bool3 True, bool3 Condition) { return bool3(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y, Condition.z ? True.z : False.z); }
bool3 SelectBranch(bool3 False, bool3 True, bool Condition) { return Condition ? True : False; }
bool4 SelectBranch(bool4 False, bool4 True, bool4 Condition) { return bool4(Condition.x ? True.x : False.x, Condition.y ? True.y : False.y, Condition.z ? True.z : False.z, Condition.w ? True.w : False.w); }
bool4 SelectBranch(bool4 False, bool4 True, bool Condition) { return Condition ? True : False; }

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONVERSIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 WGSL u32(f32)/i32(f32) saturate: values below the range clamp to its minimum, above it to the largest representable.
uint ToUnsigned(float Scalar) { return Scalar < 0.0 ? 0u : (Scalar < 4294967040.0 ? uint(Scalar) : 4294967295u); }
uint ToUnsigned(half Scalar) { return ToUnsigned(float(Scalar)); }
uint ToUnsigned(int Scalar) { return uint(Scalar); }
uint ToUnsigned(uint Scalar) { return Scalar; }
uint ToUnsigned(bool Scalar) { return Scalar ? 1u : 0u; }
int ToSigned(float Scalar) { return Scalar < -2147483648.0 ? int(-2147483648) : (Scalar < 2147483520.0 ? int(Scalar) : 2147483647); }
int ToSigned(int Scalar) { return Scalar; }
int ToSigned(uint Scalar) { return asint(Scalar); }
int ToSigned(bool Scalar) { return Scalar ? 1 : 0; }

//------------------------------------------------------------------------------------------------------------------------
//                                                   ROUNDING & ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

float RoundEven(float Scalar) { return spirv_asm { result:$$float = OpExtInst glsl450 RoundEven $Scalar }; }
float2 RoundEven(float2 Scalar) { return spirv_asm { result:$$float2 = OpExtInst glsl450 RoundEven $Scalar }; }
float3 RoundEven(float3 Scalar) { return spirv_asm { result:$$float3 = OpExtInst glsl450 RoundEven $Scalar }; }
float4 RoundEven(float4 Scalar) { return spirv_asm { result:$$float4 = OpExtInst glsl450 RoundEven $Scalar }; }
float FloatSign(float Scalar) { return spirv_asm { result:$$float = OpExtInst glsl450 FSign $Scalar }; }
float2 FloatSign(float2 Scalar) { return spirv_asm { result:$$float2 = OpExtInst glsl450 FSign $Scalar }; }
float3 FloatSign(float3 Scalar) { return spirv_asm { result:$$float3 = OpExtInst glsl450 FSign $Scalar }; }
float4 FloatSign(float4 Scalar) { return spirv_asm { result:$$float4 = OpExtInst glsl450 FSign $Scalar }; }
int FloatSign(int Scalar) { return spirv_asm { result:$$int = OpExtInst glsl450 SSign $Scalar }; }
int2 FloatSign(int2 Scalar) { return spirv_asm { result:$$int2 = OpExtInst glsl450 SSign $Scalar }; }
int3 FloatSign(int3 Scalar) { return spirv_asm { result:$$int3 = OpExtInst glsl450 SSign $Scalar }; }
int4 FloatSign(int4 Scalar) { return spirv_asm { result:$$int4 = OpExtInst glsl450 SSign $Scalar }; }
float FusedMultiplyAdd(float A, float B, float C) { return spirv_asm { result:$$float = OpExtInst glsl450 Fma $A $B $C }; }
float2 FusedMultiplyAdd(float2 A, float2 B, float2 C) { return spirv_asm { result:$$float2 = OpExtInst glsl450 Fma $A $B $C }; }
float3 FusedMultiplyAdd(float3 A, float3 B, float3 C) { return spirv_asm { result:$$float3 = OpExtInst glsl450 Fma $A $B $C }; }
float4 FusedMultiplyAdd(float4 A, float4 B, float4 C) { return spirv_asm { result:$$float4 = OpExtInst glsl450 Fma $A $B $C }; }
float QuantizeToHalf(float Scalar) { return spirv_asm { result:$$float = OpQuantizeToF16 $Scalar }; }
float2 QuantizeToHalf(float2 Scalar) { return spirv_asm { result:$$float2 = OpQuantizeToF16 $Scalar }; }
float3 QuantizeToHalf(float3 Scalar) { return spirv_asm { result:$$float3 = OpQuantizeToF16 $Scalar }; }
float4 QuantizeToHalf(float4 Scalar) { return spirv_asm { result:$$float4 = OpQuantizeToF16 $Scalar }; }

//------------------------------------------------------------------------------------------------------------------------
//                                                   PACKING
//------------------------------------------------------------------------------------------------------------------------

uint PackSnormPair(float2 Pair) { return spirv_asm { result:$$uint = OpExtInst glsl450 PackSnorm2x16 $Pair }; }
float2 UnpackSnormPair(uint Packed) { return spirv_asm { result:$$float2 = OpExtInst glsl450 UnpackSnorm2x16 $Packed }; }
uint PackUnormPair(float2 Pair) { return spirv_asm { result:$$uint = OpExtInst glsl450 PackUnorm2x16 $Pair }; }
float2 UnpackUnormPair(uint Packed) { return spirv_asm { result:$$float2 = OpExtInst glsl450 UnpackUnorm2x16 $Packed }; }
uint PackHalfPair(float2 Pair) { return spirv_asm { result:$$uint = OpExtInst glsl450 PackHalf2x16 $Pair }; }
float2 UnpackHalfPair(uint Packed) { return spirv_asm { result:$$float2 = OpExtInst glsl450 UnpackHalf2x16 $Packed }; }
uint PackUnormQuad(float4 Quad) { return spirv_asm { result:$$uint = OpExtInst glsl450 PackUnorm4x8 $Quad }; }
float4 UnpackUnormQuad(uint Packed) { return spirv_asm { result:$$float4 = OpExtInst glsl450 UnpackUnorm4x8 $Packed }; }
uint PackSnormQuad(float4 Quad) { return spirv_asm { result:$$uint = OpExtInst glsl450 PackSnorm4x8 $Quad }; }
float4 UnpackSnormQuad(uint Packed) { return spirv_asm { result:$$float4 = OpExtInst glsl450 UnpackSnorm4x8 $Packed }; }

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOURCE EXTENTS
//------------------------------------------------------------------------------------------------------------------------

uint ArrayCount<T>(StructuredBuffer<T> Span) { uint Count; uint Stride; Span.GetDimensions(Count, Stride); return Count; }
uint ArrayCount<T>(RWStructuredBuffer<T> Span) { uint Count; uint Stride; Span.GetDimensions(Count, Stride); return Count; }
uint2 TextureExtent(Texture2D<float4> Texture) { uint Width; uint Height; uint Levels; Texture.GetDimensions(0, Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2D<float4> Texture, int Level) { uint Width; uint Height; uint Levels; Texture.GetDimensions(uint(Level), Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2D<float4> Texture, uint Level) { uint Width; uint Height; uint Levels; Texture.GetDimensions(Level, Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2D<float> Texture) { uint Width; uint Height; uint Levels; Texture.GetDimensions(0, Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2D<float> Texture, int Level) { uint Width; uint Height; uint Levels; Texture.GetDimensions(uint(Level), Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2D<float> Texture, uint Level) { uint Width; uint Height; uint Levels; Texture.GetDimensions(Level, Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2D<uint4> Texture) { uint Width; uint Height; uint Levels; Texture.GetDimensions(0, Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2D<uint4> Texture, int Level) { uint Width; uint Height; uint Levels; Texture.GetDimensions(uint(Level), Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2D<uint4> Texture, uint Level) { uint Width; uint Height; uint Levels; Texture.GetDimensions(Level, Width, Height, Levels); return uint2(Width, Height); }
uint2 TextureExtent(Texture2DArray<float4> Texture) { uint Width; uint Height; uint Layers; uint Levels; Texture.GetDimensions(0, Width, Height, Layers, Levels); return uint2(Width, Height); }
uint TextureLayers(Texture2DArray<float4> Texture) { uint Width; uint Height; uint Layers; uint Levels; Texture.GetDimensions(0, Width, Height, Layers, Levels); return Layers; }
uint2 TextureExtent(Texture2DArray<float> Texture) { uint Width; uint Height; uint Layers; uint Levels; Texture.GetDimensions(0, Width, Height, Layers, Levels); return uint2(Width, Height); }
uint TextureLayers(Texture2DArray<float> Texture) { uint Width; uint Height; uint Layers; uint Levels; Texture.GetDimensions(0, Width, Height, Layers, Levels); return Layers; }
uint3 TextureExtent(Texture3D<float4> Texture) { uint Width; uint Height; uint Depth; uint Levels; Texture.GetDimensions(0, Width, Height, Depth, Levels); return uint3(Width, Height, Depth); }
uint2 TextureExtent(RWTexture2D<float4> Texture) { uint Width; uint Height; Texture.GetDimensions(Width, Height); return uint2(Width, Height); }
uint3 TextureExtent(RWTexture3D<float4> Texture) { uint Width; uint Height; uint Depth; Texture.GetDimensions(Width, Height, Depth); return uint3(Width, Height, Depth); }

//------------------------------------------------------------------------------------------------------------------------
//                                                   CLAMPED TEXEL LOADS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Dawn compiles every WGSL textureLoad through Tint's robustness transform: the level is clamped to the mip range and
//    the coordinates (and layer) to the extent of that level. LoadTexel reproduces it so edge taps read the edge texel.
float4 LoadTexel(Texture2D<float4> Texture, int2 Coordinates, int Level)
{
    uint Width; uint Height; uint Levels;
    Texture.GetDimensions(0, Width, Height, Levels);
    let ClampedLevel = clamp(Level, 0, int(Levels) - 1);
    Texture.GetDimensions(uint(ClampedLevel), Width, Height, Levels);
    return Texture.Load(int3(clamp(Coordinates, int2(0), int2(int(Width), int(Height)) - 1), ClampedLevel));
}
float4 LoadTexel(Texture2D<float4> Texture, uint2 Coordinates, int Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), Level); }
float4 LoadTexel(Texture2D<float4> Texture, int2 Coordinates, uint Level) { return LoadTexel(Texture, Coordinates, int(min(Level, 2147483647u))); }
float4 LoadTexel(Texture2D<float4> Texture, uint2 Coordinates, uint Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), int(min(Level, 2147483647u))); }
float LoadTexel(Texture2D<float> Texture, int2 Coordinates, int Level)
{
    uint Width; uint Height; uint Levels;
    Texture.GetDimensions(0, Width, Height, Levels);
    let ClampedLevel = clamp(Level, 0, int(Levels) - 1);
    Texture.GetDimensions(uint(ClampedLevel), Width, Height, Levels);
    return Texture.Load(int3(clamp(Coordinates, int2(0), int2(int(Width), int(Height)) - 1), ClampedLevel));
}
float LoadTexel(Texture2D<float> Texture, uint2 Coordinates, int Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), Level); }
float LoadTexel(Texture2D<float> Texture, int2 Coordinates, uint Level) { return LoadTexel(Texture, Coordinates, int(min(Level, 2147483647u))); }
float LoadTexel(Texture2D<float> Texture, uint2 Coordinates, uint Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), int(min(Level, 2147483647u))); }
uint4 LoadTexel(Texture2D<uint4> Texture, int2 Coordinates, int Level)
{
    uint Width; uint Height; uint Levels;
    Texture.GetDimensions(0, Width, Height, Levels);
    let ClampedLevel = clamp(Level, 0, int(Levels) - 1);
    Texture.GetDimensions(uint(ClampedLevel), Width, Height, Levels);
    return Texture.Load(int3(clamp(Coordinates, int2(0), int2(int(Width), int(Height)) - 1), ClampedLevel));
}
uint4 LoadTexel(Texture2D<uint4> Texture, uint2 Coordinates, int Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), Level); }
uint4 LoadTexel(Texture2D<uint4> Texture, int2 Coordinates, uint Level) { return LoadTexel(Texture, Coordinates, int(min(Level, 2147483647u))); }
uint4 LoadTexel(Texture2D<uint4> Texture, uint2 Coordinates, uint Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), int(min(Level, 2147483647u))); }
float4 LoadTexel(Texture3D<float4> Texture, int3 Coordinates, int Level)
{
    uint Width; uint Height; uint Depth; uint Levels;
    Texture.GetDimensions(0, Width, Height, Depth, Levels);
    let ClampedLevel = clamp(Level, 0, int(Levels) - 1);
    Texture.GetDimensions(uint(ClampedLevel), Width, Height, Depth, Levels);
    return Texture.Load(int4(clamp(Coordinates, int3(0), int3(int(Width), int(Height), int(Depth)) - 1), ClampedLevel));
}
float4 LoadTexel(Texture3D<float4> Texture, uint3 Coordinates, int Level) { return LoadTexel(Texture, int3(min(Coordinates, uint3(2147483647u))), Level); }
float4 LoadTexel(Texture3D<float4> Texture, int3 Coordinates, uint Level) { return LoadTexel(Texture, Coordinates, int(min(Level, 2147483647u))); }
float4 LoadTexel(Texture3D<float4> Texture, uint3 Coordinates, uint Level) { return LoadTexel(Texture, int3(min(Coordinates, uint3(2147483647u))), int(min(Level, 2147483647u))); }
float4 LoadTexel(Texture2DArray<float4> Texture, int2 Coordinates, int Layer, int Level)
{
    uint Width; uint Height; uint Layers; uint Levels;
    Texture.GetDimensions(0, Width, Height, Layers, Levels);
    let ClampedLevel = clamp(Level, 0, int(Levels) - 1);
    Texture.GetDimensions(uint(ClampedLevel), Width, Height, Layers, Levels);
    let ClampedLayer = clamp(Layer, 0, int(Layers) - 1);
    return Texture.Load(int4(clamp(Coordinates, int2(0), int2(int(Width), int(Height)) - 1), ClampedLayer, ClampedLevel));
}
float4 LoadTexel(Texture2DArray<float4> Texture, uint2 Coordinates, int Layer, int Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), Layer, Level); }
float4 LoadTexel(Texture2DArray<float4> Texture, int2 Coordinates, uint Layer, int Level) { return LoadTexel(Texture, Coordinates, int(min(Layer, 2147483647u)), Level); }
float4 LoadTexel(Texture2DArray<float4> Texture, uint2 Coordinates, uint Layer, int Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), int(min(Layer, 2147483647u)), Level); }
float4 LoadTexel(Texture2DArray<float4> Texture, int2 Coordinates, int Layer, uint Level) { return LoadTexel(Texture, Coordinates, Layer, int(min(Level, 2147483647u))); }
float4 LoadTexel(Texture2DArray<float4> Texture, uint2 Coordinates, uint Layer, uint Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), int(min(Layer, 2147483647u)), int(min(Level, 2147483647u))); }
float4 LoadTexel(Texture2DArray<float4> Texture, uint2 Coordinates, int Layer, uint Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), Layer, int(min(Level, 2147483647u))); }
float4 LoadTexel(Texture2DArray<float4> Texture, int2 Coordinates, uint Layer, uint Level) { return LoadTexel(Texture, Coordinates, int(min(Layer, 2147483647u)), int(min(Level, 2147483647u))); }
float LoadTexel(Texture2DArray<float> Texture, int2 Coordinates, int Layer, int Level)
{
    uint Width; uint Height; uint Layers; uint Levels;
    Texture.GetDimensions(0, Width, Height, Layers, Levels);
    let ClampedLevel = clamp(Level, 0, int(Levels) - 1);
    Texture.GetDimensions(uint(ClampedLevel), Width, Height, Layers, Levels);
    let ClampedLayer = clamp(Layer, 0, int(Layers) - 1);
    return Texture.Load(int4(clamp(Coordinates, int2(0), int2(int(Width), int(Height)) - 1), ClampedLayer, ClampedLevel));
}
float LoadTexel(Texture2DArray<float> Texture, uint2 Coordinates, int Layer, int Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), Layer, Level); }
float LoadTexel(Texture2DArray<float> Texture, int2 Coordinates, uint Layer, int Level) { return LoadTexel(Texture, Coordinates, int(min(Layer, 2147483647u)), Level); }
float LoadTexel(Texture2DArray<float> Texture, uint2 Coordinates, uint Layer, int Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), int(min(Layer, 2147483647u)), Level); }
float LoadTexel(Texture2DArray<float> Texture, int2 Coordinates, int Layer, uint Level) { return LoadTexel(Texture, Coordinates, Layer, int(min(Level, 2147483647u))); }
float LoadTexel(Texture2DArray<float> Texture, uint2 Coordinates, uint Layer, uint Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), int(min(Layer, 2147483647u)), int(min(Level, 2147483647u))); }
float LoadTexel(Texture2DArray<float> Texture, uint2 Coordinates, int Layer, uint Level) { return LoadTexel(Texture, int2(min(Coordinates, uint2(2147483647u))), Layer, int(min(Level, 2147483647u))); }
float LoadTexel(Texture2DArray<float> Texture, int2 Coordinates, uint Layer, uint Level) { return LoadTexel(Texture, Coordinates, int(min(Layer, 2147483647u)), int(min(Level, 2147483647u))); }
