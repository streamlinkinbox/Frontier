//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/SoftwareRaster.cpp — CPU rasteriser executing the .slang bodies through SlangMirror
//============================================================================================================================================

#include "SoftwareRaster.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Shaders/SlangMirror.h"

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHADER BODIES (the .slang files, compiled as C++)
//------------------------------------------------------------------------------------------------------------------------
// 📝 The .slang sources are included verbatim. `#include "ShadingRecords.slang"` inside them resolves to the same file,
//    whose struct declarations are valid C++ once the mirror types are in scope. `out` / `uint` / `bool` need spelling.

namespace Frontier::SlangMirror
{
#define out
// Slang swizzles are properties; the mirror exposes them as accessors. No shader identifier is otherwise named xy/xyz/zw.
#define xy xy()
#define xyz xyz()
#define zw zw()
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
using uint = std::uint32_t;
inline float log(float A) { return std::log(A); }
#include "Shaders/ShadingRecords.slang"
#include "Shaders/GridProjection.slang"
#include "Shaders/SurfaceRaster.slang"
#include "Shaders/LineRaster.slang"
#include "Shaders/PointRaster.slang"
#undef out
#pragma GCC diagnostic pop
#undef xy
#undef xyz
#undef zw
#undef out
} // namespace Frontier::SlangMirror

namespace Frontier
{

namespace SM = SlangMirror;

//------------------------------------------------------------------------------------------------------------------------
//                                                  DETAIL
//------------------------------------------------------------------------------------------------------------------------

struct SoftwareRaster::Detail
{
    uint32_t              W = 0, H = 0;
    std::vector<float>    Colour;                                                       // [-] RGBA float, straight alpha over opaque clear
    std::vector<float>    DepthPlane;                                                   // [-] 0..1
    std::vector<uint32_t> PickPlane;                                                    // [-]
    SM::ViewRecord        View{};
    ViewRecord            ViewSource{};
    bool                  Overlay = false;
    Tally                 Count{};

    // A vertex after the vertex stage: clip position + the varyings the fragment stage needs.
    struct ClipVertex
    {
        SM::float4 Clip;
        float      Varying[8];
    };

    template<typename FragmentShade>
    void Triangle(ClipVertex V0, ClipVertex V1, ClipVertex V2, int VaryingCount, uint32_t PickIdentity, bool CullNone, FragmentShade&& Shade) noexcept;

    template<typename FragmentShade>
    void RasterClipped(const ClipVertex& V0, const ClipVertex& V1, const ClipVertex& V2, int VaryingCount, uint32_t PickIdentity, bool CullNone, FragmentShade&& Shade) noexcept;

    void Blend(uint32_t X, uint32_t Y, SM::float4 Source) noexcept
    {
        float* Dst = &Colour[(static_cast<size_t>(Y) * W + X) * 4];
        float A = SM::saturate(Source.w);
        Dst[0] = Dst[0] * (1 - A) + Source.x * A;
        Dst[1] = Dst[1] * (1 - A) + Source.y * A;
        Dst[2] = Dst[2] * (1 - A) + Source.z * A;
        Dst[3] = 1.0f;
    }
};

static SM::ViewRecord MirrorView(const ViewRecord& V) noexcept
{
    SM::ViewRecord R;
    std::memcpy(R.ViewClip.m, V.ViewClip, sizeof R.ViewClip.m);
    std::memcpy(R.ClipView.m, V.ClipView, sizeof R.ClipView.m);
    R.EyePosition  = { V.EyePosition[0], V.EyePosition[1], V.EyePosition[2], V.EyePosition[3] };
    R.Viewport     = { V.Viewport[0], V.Viewport[1], V.Viewport[2], V.Viewport[3] };
    R.GridStyle    = { V.GridStyle[0], V.GridStyle[1], V.GridStyle[2], V.GridStyle[3] };
    R.Illumination = { V.Illumination[0], V.Illumination[1], V.Illumination[2], V.Illumination[3] };
    return R;
}

static SM::DrawRecord MirrorDraw(const DrawRecord& D) noexcept
{
    SM::DrawRecord R;
    std::memcpy(R.ModelWorld.m, D.ModelWorld, sizeof R.ModelWorld.m);
    R.Tint      = { D.Tint[0], D.Tint[1], D.Tint[2], D.Tint[3] };
    R.Selection = { D.Highlight, 0.0f, D.LineWidth, D.PointSize };
    return R;
}

// Sutherland–Hodgman against w > ε (near) only; the guard band handles the rest via clamping in screen space.
template<typename FragmentShade>
void SoftwareRaster::Detail::Triangle(ClipVertex V0, ClipVertex V1, ClipVertex V2, int VaryingCount, uint32_t PickIdentity, bool CullNone, FragmentShade&& Shade) noexcept
{
    const float Near = 1e-5f;
    ClipVertex In[3] = { V0, V1, V2 };
    ClipVertex Out[4];
    int OutCount = 0;
    for (int I = 0; I < 3; ++I)
    {
        const ClipVertex& A = In[I];
        const ClipVertex& B = In[(I + 1) % 3];
        bool AIn = A.Clip.w > Near, BIn = B.Clip.w > Near;
        if (AIn) Out[OutCount++] = A;
        if (AIn != BIn)
        {
            float T = (Near - A.Clip.w) / (B.Clip.w - A.Clip.w);
            ClipVertex M;
            M.Clip = SM::lerp(A.Clip, B.Clip, T);
            for (int K = 0; K < VaryingCount; ++K) M.Varying[K] = A.Varying[K] + (B.Varying[K] - A.Varying[K]) * T;
            Out[OutCount++] = M;
        }
    }
    if (OutCount < 3) return;
    RasterClipped(Out[0], Out[1], Out[2], VaryingCount, PickIdentity, CullNone, Shade);
    if (OutCount == 4) RasterClipped(Out[0], Out[2], Out[3], VaryingCount, PickIdentity, CullNone, Shade);
}

template<typename FragmentShade>
void SoftwareRaster::Detail::RasterClipped(const ClipVertex& V0, const ClipVertex& V1, const ClipVertex& V2, int VaryingCount, uint32_t PickIdentity, bool CullNone, FragmentShade&& Shade) noexcept
{
    // Perspective divide → screen. Vulkan: y down in clip, so screen y = (ndc.y * 0.5 + 0.5) * H directly.
    float InvW[3] = { 1.0f / V0.Clip.w, 1.0f / V1.Clip.w, 1.0f / V2.Clip.w };
    float SX[3] = { (V0.Clip.x * InvW[0] * 0.5f + 0.5f) * W, (V1.Clip.x * InvW[1] * 0.5f + 0.5f) * W, (V2.Clip.x * InvW[2] * 0.5f + 0.5f) * W };
    float SY[3] = { (V0.Clip.y * InvW[0] * 0.5f + 0.5f) * H, (V1.Clip.y * InvW[1] * 0.5f + 0.5f) * H, (V2.Clip.y * InvW[2] * 0.5f + 0.5f) * H };
    float SZ[3] = { V0.Clip.z * InvW[0], V1.Clip.z * InvW[1], V2.Clip.z * InvW[2] };

    float Area = (SX[1] - SX[0]) * (SY[2] - SY[0]) - (SX[2] - SX[0]) * (SY[1] - SY[0]);
    if (std::fabs(Area) < 1e-12f) return;
    // With Y down on screen, a CCW triangle in world/NDC (Y up) has NEGATIVE screen area. Front = Area < 0.
    bool FrontFacing = Area < 0.0f;
    if (!FrontFacing) ++Count.BackFacing;
    if (!CullNone && !FrontFacing && false) return;                                     // no culling: back faces are tinted, not dropped
    ++Count.Triangles;

    int MinX = std::max(0, static_cast<int>(std::floor(std::min({ SX[0], SX[1], SX[2] }))));
    int MaxX = std::min(static_cast<int>(W) - 1, static_cast<int>(std::ceil(std::max({ SX[0], SX[1], SX[2] }))));
    int MinY = std::max(0, static_cast<int>(std::floor(std::min({ SY[0], SY[1], SY[2] }))));
    int MaxY = std::min(static_cast<int>(H) - 1, static_cast<int>(std::ceil(std::max({ SY[0], SY[1], SY[2] }))));
    if (MinX > MaxX || MinY > MaxY) return;

    float InvArea = 1.0f / Area;
    const ClipVertex* V[3] = { &V0, &V1, &V2 };
    for (int Y = MinY; Y <= MaxY; ++Y)
    {
        float PY = Y + 0.5f;
        for (int X = MinX; X <= MaxX; ++X)
        {
            float PX = X + 0.5f;
            float E0 = ((SX[1] - PX) * (SY[2] - PY) - (SX[2] - PX) * (SY[1] - PY)) * InvArea;
            float E1 = ((SX[2] - PX) * (SY[0] - PY) - (SX[0] - PX) * (SY[2] - PY)) * InvArea;
            float E2 = 1.0f - E0 - E1;
            // Top-left rule approximated by a half-open test on the normalised barycentrics.
            if (E0 < 0 || E1 < 0 || E2 < 0) continue;
            if (E0 == 0 && Area > 0) continue;
            float Z = E0 * SZ[0] + E1 * SZ[1] + E2 * SZ[2];
            if (Z < 0.0f || Z > 1.0f) continue;
            size_t Index = static_cast<size_t>(Y) * W + X;
            ++Count.Fragments;
            if (!Overlay && Z > DepthPlane[Index]) { ++Count.DepthRejected; continue; }
            // Perspective-correct varyings.
            float Wt[3] = { E0 * InvW[0], E1 * InvW[1], E2 * InvW[2] };
            float Norm = 1.0f / (Wt[0] + Wt[1] + Wt[2]);
            float Varying[8];
            for (int K = 0; K < VaryingCount; ++K) Varying[K] = (V[0]->Varying[K] * Wt[0] + V[1]->Varying[K] * Wt[1] + V[2]->Varying[K] * Wt[2]) * Norm;
            SM::float4 Fragment = Shade(Varying, FrontFacing);
            if (Fragment.w <= 0.002f) continue;
            Blend(X, Y, Fragment);
            if (!Overlay && Fragment.w > 0.5f) DepthPlane[Index] = Z;
            if (PickIdentity != 0 && Fragment.w > 0.5f) PickPlane[Index] = PickIdentity;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

SoftwareRaster::SoftwareRaster(uint32_t Width, uint32_t Height) noexcept : Self(std::make_unique<Detail>()) { Resize(Width, Height); }
SoftwareRaster::~SoftwareRaster() = default;

void SoftwareRaster::Resize(uint32_t Width, uint32_t Height) noexcept
{
    Self->W = Width; Self->H = Height;
    Self->Colour.assign(static_cast<size_t>(Width) * Height * 4, 0.0f);
    Self->DepthPlane.assign(static_cast<size_t>(Width) * Height, 1.0f);
    Self->PickPlane.assign(static_cast<size_t>(Width) * Height, 0u);
}

uint32_t SoftwareRaster::Width() const noexcept { return Self->W; }
uint32_t SoftwareRaster::Height() const noexcept { return Self->H; }

void SoftwareRaster::BeginTarget(const float ClearColour[4]) noexcept
{
    for (size_t I = 0; I < static_cast<size_t>(Self->W) * Self->H; ++I)
    {
        Self->Colour[I * 4 + 0] = ClearColour[0]; Self->Colour[I * 4 + 1] = ClearColour[1];
        Self->Colour[I * 4 + 2] = ClearColour[2]; Self->Colour[I * 4 + 3] = 1.0f;
        Self->DepthPlane[I] = 1.0f; Self->PickPlane[I] = 0u;
    }
    Self->Overlay = false;
    Self->Count = {};
}

void SoftwareRaster::BindView(const ViewRecord& View) noexcept
{
    Self->ViewSource = View;
    Self->View = MirrorView(View);
}

void SoftwareRaster::BeginOverlay() noexcept { Self->Overlay = true; }
void SoftwareRaster::EndTarget() noexcept { Self->Overlay = false; }

//------------------------------------------------------------------------------------------------------------------------
//                                                  GRID (analytic, per pixel, 4-tap supersample)
//------------------------------------------------------------------------------------------------------------------------

void SoftwareRaster::DrawGrid() noexcept
{
    const ViewRecord& V = Self->ViewSource;
    Mat4 ClipView; for (int I = 0; I < 16; ++I) ClipView.M[I] = V.ClipView[I];
    bool Perspective = V.EyePosition[3] > 0.5f;
    Vec3 Eye{ V.EyePosition[0], V.EyePosition[1], V.EyePosition[2] };
    const float Taps[4][2] = { { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
    for (uint32_t Y = 0; Y < Self->H; ++Y)
        for (uint32_t X = 0; X < Self->W; ++X)
        {
            SM::float4 Sum{ 0, 0, 0, 0 };
            float DepthMin = 1.0f;
            for (const float* Tap : Taps)
            {
                double NdcX = ((X + Tap[0]) / Self->W) * 2.0 - 1.0;
                double NdcY = ((Y + Tap[1]) / Self->H) * 2.0 - 1.0;
                Vec3 NearP = ClipView.TransformPoint({ NdcX, NdcY, 0.0 });
                Vec3 FarP  = ClipView.TransformPoint({ NdcX, NdcY, 1.0 });
                Vec3 Origin = Perspective ? Eye : NearP;
                Vec3 Direction = (FarP - NearP).Normalised();
                SM::GridSample G = SM::GridShade(Self->View, SM::float3(float(Origin.X), float(Origin.Y), float(Origin.Z)),
                                                 SM::float3(float(Direction.X), float(Direction.Y), float(Direction.Z)), V.PixelAngle, V.PixelWorld);
                Sum = Sum + SM::float4(G.Colour.xyz() * G.Colour.w, G.Colour.w);
                DepthMin = std::min(DepthMin, G.Depth);
            }
            if (Sum.w <= 0.002f) continue;
            SM::float4 Averaged{ Sum.x / Sum.w, Sum.y / Sum.w, Sum.z / Sum.w, Sum.w * 0.25f };
            size_t Index = static_cast<size_t>(Y) * Self->W + X;
            if (DepthMin > Self->DepthPlane[Index]) continue;
            Self->Blend(X, Y, Averaged);
            ++Self->Count.Fragments;
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SURFACES
//------------------------------------------------------------------------------------------------------------------------

void SoftwareRaster::DrawSurface(const SurfaceStream& Stream, const DrawRecord& Draw) noexcept
{
    SM::DrawRecord D = MirrorDraw(Draw);
    const SM::ViewRecord& V = Self->View;
    uint32_t N = Stream.VertexCount();
    std::vector<Detail::ClipVertex> Clip(N);
    for (uint32_t I = 0; I < N; ++I)
    {
        SM::float3 P{ Stream.Positions[I * 3], Stream.Positions[I * 3 + 1], Stream.Positions[I * 3 + 2] };
        SM::float3 Nm{ Stream.Normals[I * 3], Stream.Normals[I * 3 + 1], Stream.Normals[I * 3 + 2] };
        SM::float2 UV{ Stream.Parameters.empty() ? 0.0f : Stream.Parameters[I * 2], Stream.Parameters.empty() ? 0.0f : Stream.Parameters[I * 2 + 1] };
        SM::float4 C;
        SM::SurfaceVarying Vy = SM::SurfaceVertex(V, D, P, Nm, UV); C = Vy.Clip;
        Clip[I].Clip = C;
        Clip[I].Varying[0] = Vy.WorldPosition.x; Clip[I].Varying[1] = Vy.WorldPosition.y; Clip[I].Varying[2] = Vy.WorldPosition.z;
        Clip[I].Varying[3] = Vy.WorldNormal.x;   Clip[I].Varying[4] = Vy.WorldNormal.y;   Clip[I].Varying[5] = Vy.WorldNormal.z;
        Clip[I].Varying[6] = Vy.Parameter.x;     Clip[I].Varying[7] = Vy.Parameter.y;
    }
    auto Shade = [&](const float* Vy, bool Front)
    {
        SM::SurfaceVarying In;
        In.WorldPosition = { Vy[0], Vy[1], Vy[2] };
        In.WorldNormal   = { Vy[3], Vy[4], Vy[5] };
        In.Parameter     = { Vy[6], Vy[7] };
        return SM::SurfaceFragment(V, D, In, Front);
    };
    for (size_t T = 0; T + 2 < Stream.Triangles.size(); T += 3)
        Self->Triangle(Clip[Stream.Triangles[T]], Clip[Stream.Triangles[T + 1]], Clip[Stream.Triangles[T + 2]], 8, Draw.PickIdentity, true, Shade);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SEGMENTS
//------------------------------------------------------------------------------------------------------------------------

void SoftwareRaster::DrawSegments(const SegmentStream& Stream, const DrawRecord& Draw) noexcept
{
    SM::DrawRecord D = MirrorDraw(Draw);
    const SM::ViewRecord& V = Self->View;
    auto Shade = [&](const float* Vy, bool)
    {
        SM::LineVarying In; In.Across = Vy[0]; In.Along = Vy[1]; In.SegmentPixels = Vy[2];
        return SM::LineFragment(V, D, In, Draw.Dashed);
    };
    for (uint32_t S = 0; S < Stream.SegmentCount(); ++S)
    {
        const float* E = &Stream.Endpoints[S * 6];
        SM::float3 A{ E[0], E[1], E[2] }, B{ E[3], E[4], E[5] };
        Detail::ClipVertex Q[4];
        for (uint32_t Corner = 0; Corner < 4; ++Corner)
        {
            SM::LineVarying Vy = SM::LineVertex(V, D, A, B, Corner);
            Q[Corner].Clip = Vy.Clip;
            Q[Corner].Varying[0] = Vy.Across; Q[Corner].Varying[1] = Vy.Along; Q[Corner].Varying[2] = Vy.SegmentPixels;
        }
        if (Q[0].Clip.w <= 0 && Q[2].Clip.w <= 0) continue;
        ++Self->Count.Segments;
        Self->Triangle(Q[0], Q[1], Q[3], 3, Draw.PickIdentity, true, Shade);
        Self->Triangle(Q[0], Q[3], Q[2], 3, Draw.PickIdentity, true, Shade);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  POINTS
//------------------------------------------------------------------------------------------------------------------------

void SoftwareRaster::DrawPoints(const PointStream& Stream, const DrawRecord& Draw) noexcept
{
    SM::DrawRecord D = MirrorDraw(Draw);
    const SM::ViewRecord& V = Self->View;
    auto Shade = [&](const float* Vy, bool)
    {
        SM::PointVarying In; In.Local = { Vy[0], Vy[1] }; In.Glyph = Vy[2];
        return SM::PointFragment(V, D, In);
    };
    for (uint32_t P = 0; P < Stream.PointCount(); ++P)
    {
        const float* E = &Stream.Points[P * 4];
        SM::float3 Position{ E[0], E[1], E[2] };
        Detail::ClipVertex Q[4];
        for (uint32_t Corner = 0; Corner < 4; ++Corner)
        {
            SM::PointVarying Vy = SM::PointVertex(V, D, Position, E[3], Corner);
            Q[Corner].Clip = Vy.Clip;
            Q[Corner].Varying[0] = Vy.Local.x; Q[Corner].Varying[1] = Vy.Local.y; Q[Corner].Varying[2] = Vy.Glyph;
        }
        if (Q[0].Clip.w <= 0) continue;
        ++Self->Count.Points;
        Self->Triangle(Q[0], Q[1], Q[3], 3, Draw.PickIdentity, true, Shade);
        Self->Triangle(Q[0], Q[3], Q[2], 3, Draw.PickIdentity, true, Shade);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  READBACK
//------------------------------------------------------------------------------------------------------------------------

RasterImage SoftwareRaster::Readback() const noexcept
{
    RasterImage Image;
    Image.Width = Self->W; Image.Height = Self->H;
    Image.Pixels.resize(static_cast<size_t>(Self->W) * Self->H * 4);
    for (size_t I = 0; I < Image.Pixels.size(); ++I)
    {
        float C = Self->Colour[I];
        // sRGB-ish gamma for the proof PNGs so shading reads like a real viewport.
        if (I % 4 != 3) C = std::pow(SM::saturate(C), 1.0f / 2.2f);
        Image.Pixels[I] = static_cast<uint8_t>(std::lround(SM::saturate(C) * 255.0f));
    }
    return Image;
}

uint32_t SoftwareRaster::Pick(uint32_t X, uint32_t Y) const noexcept
{
    if (X >= Self->W || Y >= Self->H) return 0;
    return Self->PickPlane[static_cast<size_t>(Y) * Self->W + X];
}

float SoftwareRaster::Depth(uint32_t X, uint32_t Y) const noexcept
{
    if (X >= Self->W || Y >= Self->H) return 1.0f;
    return Self->DepthPlane[static_cast<size_t>(Y) * Self->W + X];
}

RasterExchange::Tally SoftwareRaster::QueryTally() const noexcept { return Self->Count; }

//------------------------------------------------------------------------------------------------------------------------
//                                                  PNG (stored deflate + CRC32 + Adler32)
//------------------------------------------------------------------------------------------------------------------------

namespace
{

uint32_t Crc32(const uint8_t* Bytes, size_t Length, uint32_t Seed = 0xFFFFFFFFu) noexcept
{
    static uint32_t Lookup[256];
    static bool Ready = false;
    if (!Ready)
    {
        for (uint32_t N = 0; N < 256; ++N)
        {
            uint32_t C = N;
            for (int K = 0; K < 8; ++K) C = (C & 1) ? 0xEDB88320u ^ (C >> 1) : C >> 1;
            Lookup[N] = C;
        }
        Ready = true;
    }
    uint32_t C = Seed;
    for (size_t I = 0; I < Length; ++I) C = Lookup[(C ^ Bytes[I]) & 0xFF] ^ (C >> 8);
    return C;
}

void PutBigEndian(std::vector<uint8_t>& Out, uint32_t Value) noexcept
{
    Out.push_back(uint8_t(Value >> 24)); Out.push_back(uint8_t(Value >> 16)); Out.push_back(uint8_t(Value >> 8)); Out.push_back(uint8_t(Value));
}

void PutChunk(std::vector<uint8_t>& Out, const char* Type, const std::vector<uint8_t>& Payload) noexcept
{
    PutBigEndian(Out, static_cast<uint32_t>(Payload.size()));
    size_t Start = Out.size();
    Out.insert(Out.end(), Type, Type + 4);
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    PutBigEndian(Out, Crc32(&Out[Start], Out.size() - Start) ^ 0xFFFFFFFFu);
}

// Bit writer + fixed-Huffman deflate (RFC 1951 block type 1) with a hash-chain LZ77 matcher. Enough to keep proof
//    PNGs at a fraction of raw size without pulling in zlib.
struct BitSink
{
    std::vector<uint8_t> Bytes;
    uint32_t Accumulator = 0;
    int      Count = 0;
    void Put(uint32_t Value, int Width) noexcept                                        // LSB-first
    {
        Accumulator |= Value << Count; Count += Width;
        while (Count >= 8) { Bytes.push_back(uint8_t(Accumulator & 0xFF)); Accumulator >>= 8; Count -= 8; }
    }
    void PutReversed(uint32_t Code, int Width) noexcept                                 // Huffman codes are MSB-first
    {
        uint32_t R = 0;
        for (int I = 0; I < Width; ++I) R |= ((Code >> I) & 1u) << (Width - 1 - I);
        Put(R, Width);
    }
    void Flush() noexcept { if (Count > 0) { Bytes.push_back(uint8_t(Accumulator & 0xFF)); Accumulator = 0; Count = 0; } }
};

void PutLiteral(BitSink& Sink, uint32_t Symbol) noexcept
{
    if (Symbol < 144)      Sink.PutReversed(0x30 + Symbol, 8);
    else if (Symbol < 256) Sink.PutReversed(0x190 + Symbol - 144, 9);
    else if (Symbol < 280) Sink.PutReversed(Symbol - 256, 7);
    else                   Sink.PutReversed(0xC0 + Symbol - 280, 8);
}

void PutMatch(BitSink& Sink, uint32_t Length, uint32_t Distance) noexcept
{
    static const uint16_t LengthBase[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const uint8_t  LengthExtra[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const uint16_t DistanceBase[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const uint8_t  DistanceExtra[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    int L = 28; while (LengthBase[L] > Length) --L;
    PutLiteral(Sink, 257 + L);
    if (LengthExtra[L]) Sink.Put(Length - LengthBase[L], LengthExtra[L]);
    int D = 29; while (DistanceBase[D] > Distance) --D;
    Sink.PutReversed(static_cast<uint32_t>(D), 5);
    if (DistanceExtra[D]) Sink.Put(Distance - DistanceBase[D], DistanceExtra[D]);
}

std::vector<uint8_t> DeflateFixed(const std::vector<uint8_t>& Raw) noexcept
{
    constexpr uint32_t HashBits = 15, HashSize = 1u << HashBits, WindowSize = 32768, MaxChain = 32;
    std::vector<int32_t> Head(HashSize, -1), Previous(Raw.size(), -1);
    auto HashAt = [&](size_t I) { return ((uint32_t(Raw[I]) << 10) ^ (uint32_t(Raw[I + 1]) << 5) ^ Raw[I + 2]) & (HashSize - 1); };

    BitSink Sink;
    Sink.Bytes.push_back(0x78); Sink.Bytes.push_back(0x01);                             // zlib header
    Sink.Put(1, 1); Sink.Put(1, 2);                                                     // final block, fixed Huffman
    size_t I = 0;
    while (I < Raw.size())
    {
        uint32_t BestLength = 0, BestDistance = 0;
        if (I + 3 <= Raw.size())
        {
            uint32_t H = HashAt(I);
            int32_t Candidate = Head[H];
            for (uint32_t Chain = 0; Candidate >= 0 && Chain < MaxChain; ++Chain, Candidate = Previous[Candidate])
            {
                size_t Distance = I - static_cast<size_t>(Candidate);
                if (Distance > WindowSize) break;
                uint32_t Limit = static_cast<uint32_t>(std::min<size_t>(258, Raw.size() - I));
                uint32_t Length = 0;
                while (Length < Limit && Raw[Candidate + Length] == Raw[I + Length]) ++Length;
                if (Length > BestLength) { BestLength = Length; BestDistance = static_cast<uint32_t>(Distance); if (Length == Limit) break; }
            }
            Previous[I] = Head[H]; Head[H] = static_cast<int32_t>(I);
        }
        if (BestLength >= 3)
        {
            PutMatch(Sink, BestLength, BestDistance);
            for (uint32_t K = 1; K < BestLength && I + K + 3 <= Raw.size(); ++K) { uint32_t H = HashAt(I + K); Previous[I + K] = Head[H]; Head[H] = static_cast<int32_t>(I + K); }
            I += BestLength;
        }
        else { PutLiteral(Sink, Raw[I]); ++I; }
    }
    PutLiteral(Sink, 256);                                                              // end of block
    Sink.Flush();
    uint32_t A = 1, B = 0;
    for (uint8_t Byte : Raw) { A = (A + Byte) % 65521; B = (B + A) % 65521; }
    PutBigEndian(Sink.Bytes, (B << 16) | A);
    return Sink.Bytes;
}

} // namespace

bool WritePng(const std::string& Path, const RasterImage& Image) noexcept
{
    std::vector<uint8_t> Raw;
    Raw.reserve((static_cast<size_t>(Image.Width) * 4 + 1) * Image.Height);
    for (uint32_t Y = 0; Y < Image.Height; ++Y)
    {
        Raw.push_back(2);                                                               // filter: Up (flat backdrops → zeros)
        const uint8_t* Row = &Image.Pixels[static_cast<size_t>(Y) * Image.Width * 4];
        const uint8_t* Above = Y ? Row - static_cast<size_t>(Image.Width) * 4 : nullptr;
        for (size_t I = 0; I < static_cast<size_t>(Image.Width) * 4; ++I) Raw.push_back(uint8_t(Row[I] - (Above ? Above[I] : 0)));
    }
    std::vector<uint8_t> Z = DeflateFixed(Raw);

    std::vector<uint8_t> Out{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> Header;
    PutBigEndian(Header, Image.Width); PutBigEndian(Header, Image.Height);
    Header.insert(Header.end(), { 8, 6, 0, 0, 0 });                                     // 8-bit RGBA
    PutChunk(Out, "IHDR", Header);
    PutChunk(Out, "IDAT", Z);
    PutChunk(Out, "IEND", {});

    if (FILE* F = std::fopen(Path.c_str(), "wb"))
    {
        size_t Written = std::fwrite(Out.data(), 1, Out.size(), F);
        std::fclose(F);
        return Written == Out.size();
    }
    return false;
}

} // namespace Frontier
