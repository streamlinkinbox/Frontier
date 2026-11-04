//==========================================================================================================================================
//                                                   PLANETARY RECORDS SLANG
//==========================================================================================================================================
// Multi-realm float4 record decoders, spherical planet geometry, phase functions, and non-tiled 3D procedural synthesis.

static const float π      = 3.14159265358979323846f;
static const float kTwoπ  = 6.28318530717958647692f;
static const float kInv4π = 0.07957747154594766788f;

static const uint kRealmStride       = 19u;
static const uint kSunStride         = 6u;
static const uint kMoonStride        = 7u;
static const uint kPlanetStride      = 8u;
static const uint kCloudStride       = 8u;
static const uint kFogBoxStride      = 6u;
static const uint kLuminaireStride   = 3u;
static const uint kLightningStride   = 3u;
static const uint kFacetStride       = 3u;
static const uint kSphereStride      = 2u;
static const uint kGlassStride       = 3u;
static const uint kPortalStride      = 9u;
static const uint kFlareSourceStride = 4u;
static const uint kCustomStarStride  = 2u;

struct PushDispatchHeader
{
    uint4  Counts; // x = Width, y = Height, z = TargetIndex (RealmId or FrameIndex), w = Flags
    float4 Tuning;
};

float3 SafeNormalize(float3 Vector)
{
    float LenSq = dot(Vector, Vector);
    return (LenSq > 1.0e-20f) ? (Vector * rsqrt(LenSq)) : float3(0.0f, 1.0f, 0.0f);
}

bool RaySphereBoth(float3 Origin, float3 Direction, float Radius, out float OutNear, out float OutFar)
{
    float HalfB = dot(Origin, Direction);
    float TermC = dot(Origin, Origin) - Radius * Radius;
    float Disc  = HalfB * HalfB - TermC;
    if (Disc < 0.0f)
    {
        OutNear = 0.0f;
        OutFar  = 0.0f;
        return false;
    }
    float Root = sqrt(Disc);
    OutNear    = -HalfB - Root;
    OutFar     = -HalfB + Root;
    return true;
}

float DistanceToSphereExit(float3 Origin, float3 Direction, float Radius)
{
    float NearT, FarT;
    if (!RaySphereBoth(Origin, Direction, Radius, NearT, FarT) || FarT <= 0.0f)
    {
        return 0.0f;
    }
    return FarT;
}

bool HitsPlanetGround(float3 Origin, float3 Direction, float GroundRadiusKm, out float OutGroundT)
{
    float NearT, FarT;
    OutGroundT = 0.0f;
    if (!RaySphereBoth(Origin, Direction, GroundRadiusKm, NearT, FarT))
    {
        return false;
    }
    if (NearT > 1.0e-4f)
    {
        OutGroundT = NearT;
        return true;
    }
    return false;
}

float RayleighPhaseValue(float cosθ)
{
    return (3.0f / (16.0f * π)) * (1.0f + cosθ * cosθ);
}

float CornetteShanksPhaseValue(float cosθ, float 𝑔)
{
    float 𝑔Sq   = 𝑔 * 𝑔;
    float Numer = 3.0f * (1.0f - 𝑔Sq) * (1.0f + cosθ * cosθ);
    float Denom = (8.0f * π) * (2.0f + 𝑔Sq) * pow(max(1.0f + 𝑔Sq - 2.0f * 𝑔 * cosθ, 1.0e-4f), 1.5f);
    return Numer / Denom;
}

float HenyeyGreensteinValue(float cosθ, float 𝑔)
{
    float 𝑔Sq   = 𝑔 * 𝑔;
    float Denom = max(1.0f + 𝑔Sq - 2.0f * 𝑔 * cosθ, 1.0e-4f);
    return (1.0f - 𝑔Sq) / (4.0f * π * Denom * sqrt(Denom));
}

// 📝 The droplets' sharp forward lobe (their diffraction peak): a cloud between the observer and the sun glows along its thin
//    parts — the silver lining. VolumetricClouds blends it into the single-scattering phase by the record's silver-lining weight.
static const float kSilverLiningG = 0.9f;

// 📝 The solar disc seen through a cloud. Its direct beam leaves the cloud as that forward glow, so the sharp disc fades as the
//    cloud's transmittance to the fourth power: hidden behind any cloud thicker than τ ≈ 1.5 instead of shining through at 1 %.
//    The cloud composite and the flare's visibility share it, so the flare dims exactly as the disc does.
float ResolveSolarDiscTransmittance(float CloudTransmittance)
{
    float Transmittance = saturate(CloudTransmittance);
    float Squared       = Transmittance * Transmittance;
    return Squared * Squared;
}

//------------------------------------------------------------------------------------------------------------------------
//                MAGNITUDE-INVARIANT UINT32 BIT-MIX HASH, C2 QUINTIC FADE & ROTATED 3D OCTAVES
//------------------------------------------------------------------------------------------------------------------------

uint HashBitMix32(uint Word)
{
    Word ^= Word >> 16u;
    Word *= 0x7FEB352Du;
    Word ^= Word >> 15u;
    Word *= 0x846CA68Bu;
    Word ^= Word >> 16u;
    return Word;
}

uint HashLattice3D(int3 Cell, uint Seed)
{
    uint WordX = uint(Cell.x) * 1597334677u + Seed;
    uint WordY = uint(Cell.y) * 3812015801u + (Seed ^ 0x9E3779B9u);
    uint WordZ = uint(Cell.z) * 2654435761u;
    return HashBitMix32(WordX ^ WordY ^ WordZ);
}

float3 QuinticFade3(float3 Frac)
{
    return Frac * Frac * Frac * (Frac * (Frac * 6.0f - 15.0f) + 10.0f);
}

// Orthonormal 3D octave rotation matrix so consecutive octave lattice planes never align or tile.
float3 RotateOctave3D(float3 Point)
{
    return float3(
         0.00f * Point.x + 0.80f * Point.y + 0.60f * Point.z,
        -0.80f * Point.x + 0.36f * Point.y - 0.48f * Point.z,
        -0.60f * Point.x - 0.48f * Point.y + 0.64f * Point.z);
}

float ValueNoise3D(float3 Point, uint Seed)
{
    float3 Floored = floor(Point);
    int3   Cell0   = int3(Floored);
    float3 Fade    = QuinticFade3(Point - Floored);

    float N000 = float(HashLattice3D(Cell0 + int3(0, 0, 0), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N100 = float(HashLattice3D(Cell0 + int3(1, 0, 0), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N010 = float(HashLattice3D(Cell0 + int3(0, 1, 0), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N110 = float(HashLattice3D(Cell0 + int3(1, 1, 0), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N001 = float(HashLattice3D(Cell0 + int3(0, 0, 1), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N101 = float(HashLattice3D(Cell0 + int3(1, 0, 1), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N011 = float(HashLattice3D(Cell0 + int3(0, 1, 1), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N111 = float(HashLattice3D(Cell0 + int3(1, 1, 1), Seed) & 0xFFFFFFu) / float(0x1000000u);

    float Nx00 = lerp(N000, N100, Fade.x);
    float Nx10 = lerp(N010, N110, Fade.x);
    float Nx01 = lerp(N001, N101, Fade.x);
    float Nx11 = lerp(N011, N111, Fade.x);
    return lerp(lerp(Nx00, Nx10, Fade.y), lerp(Nx01, Nx11, Fade.y), Fade.z);
}

float FbmRotated3D(float3 Point, int Octaves, uint Seed)
{
    float  Sum       = 0.0f;
    float  Amplitude = 0.5f;
    float  WeightSum = 0.0f;
    float3 CurPoint  = Point;
    for (int Octave = 0; Octave < Octaves; ++Octave)
    {
        Sum       += Amplitude * ValueNoise3D(CurPoint, Seed + uint(Octave) * 131u);
        WeightSum += Amplitude;
        CurPoint   = RotateOctave3D(CurPoint) * 2.03f + float3(11.3f, 7.7f, 5.1f);
        Amplitude *= 0.5f;
    }
    return (WeightSum > 0.0f) ? (Sum / WeightSum) : 0.0f;
}

float WorleyInverted3D(float3 Point, uint Seed)
{
    int3  Cell  = int3(floor(Point));
    float BestD = 4.0f;
    for (int Dz = -1; Dz <= 1; ++Dz)
    {
        for (int Dy = -1; Dy <= 1; ++Dy)
        {
            for (int Dx = -1; Dx <= 1; ++Dx)
            {
                int3 Neighbor = Cell + int3(Dx, Dy, Dz);
                uint H0       = HashLattice3D(Neighbor, Seed);
                uint H1       = HashBitMix32(H0);
                uint H2       = HashBitMix32(H1);
                float3 Feature = float3(Neighbor) + float3(
                    float(H0 & 0xFFFFu) / 65535.0f,
                    float(H1 & 0xFFFFu) / 65535.0f,
                    float(H2 & 0xFFFFu) / 65535.0f);
                float3 Offset = Feature - Point;
                BestD         = min(BestD, dot(Offset, Offset));
            }
        }
    }
    return saturate(1.0f - sqrt(BestD));
}

//------------------------------------------------------------------------------------------------------------------------
//                                         ENSEMBLE RECORD DECODING HELPERS
//------------------------------------------------------------------------------------------------------------------------

uint RealmSlotOffset(StructuredBuffer<float4> Ensemble, uint RealmId)
{
    uint TableOffset = uint(Ensemble[5].x + 0.5f);
    return TableOffset + RealmId * kRealmStride;
}

uint SunSlotOffset(StructuredBuffer<float4> Ensemble, uint GlobalSunIdx)
{
    uint TableOffset = uint(Ensemble[5].y + 0.5f);
    return TableOffset + GlobalSunIdx * kSunStride;
}

float3 CameraRayDirection(StructuredBuffer<float4> Ensemble, float2 ScreenUv)
{
    float  TanHalfY    = Ensemble[1].w;
    float3 CameraRight = Ensemble[2].xyz;
    float  AspectRatio = Ensemble[2].w;
    float3 CameraUp    = Ensemble[3].xyz;
    float3 CameraFwd   = Ensemble[4].xyz;

    float NdcX = (ScreenUv.x * 2.0f - 1.0f) * AspectRatio * TanHalfY;
    float NdcY = (1.0f - ScreenUv.y * 2.0f) * TanHalfY;
    return SafeNormalize(CameraFwd + CameraRight * NdcX + CameraUp * NdcY);
}
