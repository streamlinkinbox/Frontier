// Does the ReSTIR kernel's sky render the same clean clouds the raster marches?
//
// The kernel cannot be dispatched here, but its cloud path CAN be re-transcribed back into C++ term by term
// and rendered through the production pack (CelestialSequence::PackSkyRecord). A transcription slip — a wrong
// drift, a mismatched step, a dropped clamp — shows as a numeric divergence from the raster, which is why this
// is worth doing. It is also the regression guard for the streak the wind fix cured: the altitude-varying drift
// accumulating over time-of-day shredded the slab into horizontal streaks (see WindField::AdvectDrift), so the
// proof renders morning AND morning-plus-two-cloud-hours and demands both streak-free — the old form shreds
// progressively with the clock, the uniform form stays coherent at every hour.
//
// What the twin is: everything below the "Transcribed BACK" banner mirrors Engine/Shaders/SkyRecords.slang and
// reads ONLY the packed SkyConstantRecord, exactly as the kernel does. The air under the clouds comes from
// AtmosphereModel, already parity-proved against the kernel's SkyRadiance (SkyKernelParityProof); stars, the
// twilight wash, moons and the sun disc are skipped — other proofs own them — leaving the cloud composition
// (Radiance * Transmittance + Scatter) under test here. The frame around the twin — camera, aureole, ground,
// colour — replicates the streak lab's Morning setup so the committed render stays comparable with it.
#include "DisplayPresentation/AtmosphereModel.h"
#include "DisplayPresentation/CelestialTier.h"
#include "DisplayPresentation/CelestialSolver.h"
#include "DisplayPresentation/ColourTransfer.h"
#include "DisplayPresentation/FidelityClassifier.h"
#include "DisplayPresentation/SkyConstantRecord.h"
#include "DisplayPresentation/VolumetricMedia.h"
#include "DisplayPresentation/WindField.h"
#include "Projects/Project-Zero/Source/CelestialSequence.h"
#include "PngWriteShim.h"

#include <cmath>
#include <cstdio>

using namespace Frontier;
using namespace Frontier::ProjectZero;

namespace {
// The control flags live only in SkyRecords.slang (kCloudLayerFlag et al.); the twin mirrors their values.
constexpr uint32_t kTwinLayerFlag = 1u << 0u, kTwinLocalCloudFlag = 1u << 1u, kTwinLocalFogFlag = 1u << 2u;
constexpr uint32_t kTwinFollowWindFlag = 1u << 3u, kTwinBoxWindFlag = 1u << 4u, kTwinFogWindFlag = 1u << 5u;
constexpr float kTwinExtinction = 0.01f;
constexpr float kTwinMaxDistance = 200000.0f;

// ── Transcribed BACK from Engine/Shaders/SkyRecords.slang, not from VolumetricMedia ─────────────────────────
float TwinSmoothstep(float E0, float E1, float V)
{
    float T = (V-E0)/(E1-E0);
    T = T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
    return T*T*(3.0f-2.0f*T);
}
float TwinHash(const float P[3])
{
    float H = std::sin(P[0]*127.1f + P[1]*311.7f + P[2]*74.7f) * 43758.5453f;
    return H - std::floor(H);
}
float TwinNoise(const float P[3])
{
    float I[3] = { std::floor(P[0]), std::floor(P[1]), std::floor(P[2]) };
    float F[3] = { P[0]-I[0], P[1]-I[1], P[2]-I[2] };
    float U[3];
    for (int C = 0; C < 3; ++C) U[C] = F[C]*F[C]*(3.0f-2.0f*F[C]);
    float C000[3]={I[0],I[1],I[2]}, C100[3]={I[0]+1,I[1],I[2]};
    float C010[3]={I[0],I[1]+1,I[2]}, C110[3]={I[0]+1,I[1]+1,I[2]};
    float C001[3]={I[0],I[1],I[2]+1}, C101[3]={I[0]+1,I[1],I[2]+1};
    float C011[3]={I[0],I[1]+1,I[2]+1}, C111[3]={I[0]+1,I[1]+1,I[2]+1};
    float X00=TwinHash(C000)+(TwinHash(C100)-TwinHash(C000))*U[0];
    float X10=TwinHash(C010)+(TwinHash(C110)-TwinHash(C010))*U[0];
    float X01=TwinHash(C001)+(TwinHash(C101)-TwinHash(C001))*U[0];
    float X11=TwinHash(C011)+(TwinHash(C111)-TwinHash(C011))*U[0];
    float Y0=X00+(X10-X00)*U[1], Y1=X01+(X11-X01)*U[1];
    return (Y0+(Y1-Y0)*U[2])*2.0f-1.0f;
}
float TwinHeightProfile(const SkyConstantRecord& K, uint32_t Type, float Normalised)
{
    float H = Normalised < 0.0f ? 0.0f : (Normalised > 1.0f ? 1.0f : Normalised);
    if (Type == 0u) return TwinSmoothstep(0.0f, 0.08f, H)*(1.0f-TwinSmoothstep(0.75f, 1.0f, H));
    if (Type == 1u) return TwinSmoothstep(0.0f, 0.12f, H)*(1.0f-TwinSmoothstep(0.50f, 0.95f, H));
    if (Type == 2u) return TwinSmoothstep(0.0f, 0.07f, H)*(1.0f-TwinSmoothstep(0.35f, 1.0f, H))*1.15f;
    if (Type == 3u) return TwinSmoothstep(0.0f, 0.05f, H)*(1.0f-TwinSmoothstep(0.85f, 1.0f, H))
                        * (1.0f+(1.6f-1.0f)*(TwinSmoothstep(0.7f, 1.0f, H)*K.CloudShape[2]));
    if (Type == 4u) return TwinSmoothstep(0.0f, 0.20f, H)*(1.0f-TwinSmoothstep(0.55f, 0.9f, H))*0.75f;
    return TwinSmoothstep(0.0f, 0.25f, H)*(1.0f-TwinSmoothstep(0.4f, 0.85f, H))*0.45f;
}
void TwinWindAt(const SkyConstantRecord& K, float Altitude, float Out[3])
{
    float Km = std::fmax(Altitude, 0.0f) * 0.001f;
    float Speed = K.CloudWind[0] * (1.0f + K.CloudWind[2] * Km);
    float Bearing = (K.CloudWind[1] + K.CloudWind[3] * Km) * 3.14159265358979323846f / 180.0f;
    Out[0] = std::sin(Bearing)*Speed; Out[1] = std::cos(Bearing)*Speed; Out[2] = 0.0f;
}
void TwinDriftAt(const SkyConstantRecord& K, float Altitude, float RefAltitude, float Cell,
                 float ArtFactor, float Out[2])
{
    float Step[3], Reference[3];
    TwinWindAt(K, Altitude, Step);
    TwinWindAt(K, RefAltitude, Reference);
    const float kShearMemory = 120.0f;
    for (int C = 0; C < 2; ++C)
    {
        float Shear = (Step[C]-Reference[C]) * (kShearMemory*ArtFactor);
        const float Limit = 2.0f*Cell;
        Shear = Shear < -Limit ? -Limit : (Shear > Limit ? Limit : Shear);
        Out[C] = Reference[C]*(K.CloudAlbedo[3]*ArtFactor) + Shear;
    }
}
float TwinCloudDensityAt(const SkyConstantRecord& K, const float Position[3])
{
    float Ceiling = std::fmax(K.CloudShape[1], 0.0f);
    float Base = K.CloudLayer[0] < 0.0f ? 0.0f : (K.CloudLayer[0] > Ceiling ? Ceiling : K.CloudLayer[0]);
    float TopEnd = K.CloudLayer[0]+K.CloudLayer[1];
    float Top = TopEnd < 0.0f ? 0.0f : (TopEnd > Ceiling ? Ceiling : TopEnd);
    if (Position[2] < Base || Position[2] > Top || Top <= Base+1.0f) return 0.0f;
    float H = (Position[2]-Base)/(Top-Base);
    float Profile = TwinHeightProfile(K, K.Control[3], H);
    if (Profile <= 0.0f) return 0.0f;
    float Drift[3] = { 0.0f, 0.0f, 0.0f };
    if ((K.Control[2] & kTwinFollowWindFlag) != 0u)
    {
        float Advected[2];
        TwinDriftAt(K, Position[2], 0.5f*(Base+Top), std::fmax(K.CloudShape[0]*900.0f, 1.0f), 0.8f, Advected);
        Drift[0] = Advected[0]; Drift[1] = Advected[1];
    }
    float Inverse = 1.0f/std::fmax(K.CloudShape[0]*900.0f, 1.0f);
    float S[3] = { (Position[0]+Drift[0])*Inverse, (Position[1]+Drift[1])*Inverse, Position[2]*Inverse };
    float Q1[3] = { S[0]*2.02f+3.1f, S[1]*2.02f+1.7f, S[2]*2.02f+9.2f };
    float Q2[3] = { S[0]*4.10f+7.7f, S[1]*4.10f+2.2f, S[2]*4.10f+1.1f };
    float Shape = TwinNoise(S)*0.5f + TwinNoise(Q1)*0.25f + TwinNoise(Q2)*0.125f;
    Shape /= 0.875f;
    Shape = Shape*0.5f+0.5f;
    float Coverage = K.CloudLayer[2] < 0.0f ? 0.0f : (K.CloudLayer[2] > 1.0f ? 1.0f : K.CloudLayer[2]);
    float Threshold = 1.0f-Coverage;
    float Raw = (Shape-Threshold)/std::fmax(1.0f-Threshold, 1e-3f);
    Raw = Raw < 0.0f ? 0.0f : (Raw > 1.0f ? 1.0f : Raw);
    return TwinSmoothstep(0.0f, 1.0f, Raw)*Profile*std::fmax(K.CloudLayer[3], 0.0f);
}
float TwinLocalDensityAt(const SkyConstantRecord& K, const float Position[3], bool Fog)
{
    if (Fog)
    {
        if ((K.Control[2] & kTwinLocalFogFlag) == 0u) return 0.0f;
    }
    else if ((K.Control[2] & kTwinLocalCloudFlag) == 0u) return 0.0f;
    const float* Centre = Fog ? K.LocalFogCentre : K.LocalCloudCentre;
    const float* HalfSize = Fog ? K.LocalFogHalfSize : K.LocalCloudHalfSize;
    const float* Params = Fog ? K.LocalFogParams : K.LocalCloudParams;
    float Local[3] = { (Position[0]-Centre[0])/std::fmax(HalfSize[0], 1e-3f),
                       (Position[1]-Centre[1])/std::fmax(HalfSize[1], 1e-3f),
                       (Position[2]-Centre[2])/std::fmax(HalfSize[2], 1e-3f) };
    float R = std::sqrt(Local[0]*Local[0]+Local[1]*Local[1]+Local[2]*Local[2]);
    if (R >= 1.0f) return 0.0f;
    float Mask = 1.0f-TwinSmoothstep(0.55f, 1.0f, R);
    if (Mask <= 0.0f) return 0.0f;
    uint32_t WindFlag = Fog ? kTwinFogWindFlag : kTwinBoxWindFlag;
    float Drift[3] = { 0.0f, 0.0f, 0.0f };
    if ((K.Control[2] & WindFlag) != 0u)
    {
        float Advected[2];
        TwinDriftAt(K, Position[2], Centre[2], std::fmax(Params[2], 1.0f), 0.6f, Advected);
        Drift[0] = Advected[0]; Drift[1] = Advected[1];
    }
    float Inverse = 1.0f/std::fmax(Params[2], 1.0f);
    float S[3] = { (Position[0]+Drift[0])*Inverse, (Position[1]+Drift[1])*Inverse,
                   (Position[2]+Drift[2])*Inverse };
    float Q1[3] = { S[0]*2.02f+3.1f, S[1]*2.02f+1.7f, S[2]*2.02f+9.2f };
    float Shape = TwinNoise(S)*0.5f + TwinNoise(Q1)*0.25f;
    Shape /= 0.75f;
    Shape = Shape*0.5f+0.5f;
    float Coverage = Params[1] < 0.0f ? 0.0f : (Params[1] > 1.0f ? 1.0f : Params[1]);
    float Threshold = 1.0f-Coverage;
    float Raw = (Shape-Threshold)/std::fmax(1.0f-Threshold, 1e-3f);
    Raw = Raw < 0.0f ? 0.0f : (Raw > 1.0f ? 1.0f : Raw);
    return TwinSmoothstep(0.0f, 1.0f, Raw)*Mask*std::fmax(Params[0], 0.0f);
}
bool TwinSlabInterval(const SkyConstantRecord& K, const float Origin[3], const float Direction[3],
                      float Maximum, float& Near, float& Far)
{
    float Ceiling = std::fmax(K.CloudShape[1], 0.0f);
    float Base = K.CloudLayer[0] < 0.0f ? 0.0f : (K.CloudLayer[0] > Ceiling ? Ceiling : K.CloudLayer[0]);
    float TopEnd = K.CloudLayer[0]+K.CloudLayer[1];
    float Top = TopEnd < 0.0f ? 0.0f : (TopEnd > Ceiling ? Ceiling : TopEnd);
    if (Top <= Base+1.0f) return false;
    if (std::fabs(Direction[2]) < 1e-6f)
    {
        if (Origin[2] < Base || Origin[2] > Top) return false;
        Near = 0.0f; Far = Maximum; return Far > Near;
    }
    float T0 = (Base-Origin[2])/Direction[2];
    float T1 = (Top-Origin[2])/Direction[2];
    if (T0 > T1) { float Swap = T0; T0 = T1; T1 = Swap; }
    Near = std::fmax(T0, 0.0f); Far = std::fmin(T1, Maximum);
    return Far > Near;
}
bool TwinBoxInterval(const float Origin[3], const float Direction[3], const float Centre[3],
                     const float Half[3], float Maximum, float& Near, float& Far)
{
    float Low = -1e30f, High = 1e30f;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (std::fabs(Direction[Axis]) < 1e-7f)
        {
            if (std::fabs(Origin[Axis]-Centre[Axis]) > Half[Axis]) return false;
            continue;
        }
        float Inv = 1.0f/Direction[Axis];
        float T0 = (-Half[Axis]-(Origin[Axis]-Centre[Axis]))*Inv;
        float T1 = (Half[Axis]-(Origin[Axis]-Centre[Axis]))*Inv;
        if (T0 > T1) { float Swap = T0; T0 = T1; T1 = Swap; }
        Low = std::fmax(Low, T0); High = std::fmin(High, T1);
    }
    if (Low > High || High < 0.0f) return false;
    Near = std::fmax(Low, 0.0f); Far = std::fmin(High, Maximum);
    return Far > Near;
}
float TwinShadowMedium(const SkyConstantRecord& K, const float Origin[3], const float Direction[3],
                       uint32_t Medium, uint32_t Taps)
{
    float StepSize = Medium == 0u ? 4000.0f/std::fmax(float(K.CloudControl[0] ? K.CloudControl[0] : 1u), 1.0f)
                   : std::fmax((Medium == 1u ? K.LocalCloudParams[2] : K.LocalFogParams[2])*0.5f, 1.0f);
    uint32_t Count = Taps ? Taps : 1u;
    float Depth = 0.0f;
    for (uint32_t I = 1u; I <= Count; ++I)
    {
        float Distance = StepSize*float(I)*0.5f;
        float Q[3] = { Origin[0]+Direction[0]*Distance, Origin[1]+Direction[1]*Distance,
                       Origin[2]+Direction[2]*Distance };
        float D = Medium == 0u ? TwinCloudDensityAt(K, Q) : TwinLocalDensityAt(K, Q, Medium == 2u);
        Depth += D*StepSize*0.5f*kTwinExtinction;
        if (Depth > 4.0f) break;
    }
    return std::exp(-Depth);
}
float TwinSunTransmittance(const SkyConstantRecord& K, const float Origin[3], const float Direction[3])
{
    uint32_t Taps = K.CloudControl[2] ? K.CloudControl[2] : 1u;
    float Result = 1.0f;
    if ((K.Control[2] & kTwinLayerFlag) != 0u) Result *= TwinShadowMedium(K, Origin, Direction, 0u, Taps);
    if ((K.Control[2] & kTwinLocalCloudFlag) != 0u) Result *= TwinShadowMedium(K, Origin, Direction, 1u, Taps);
    if ((K.Control[2] & kTwinLocalFogFlag) != 0u) Result *= TwinShadowMedium(K, Origin, Direction, 2u, Taps);
    return Result;
}
float TwinPhase(float Mu, float G)
{
    float G2 = G*G;
    float Den = std::pow(std::fmax(1.0f+G2-2.0f*G*Mu, 1e-6f), 1.5f);
    return (1.0f-G2)/std::fmax(4.0f*3.14159265358979323846f*Den, 1e-9f);
}
void TwinMarchMedium(const SkyConstantRecord& K, const float Origin[3], const float Direction[3],
                     float Near, float Far, uint32_t Medium, const float SunDirection[3],
                     const float SunRadiance[3], const float Ambient[3],
                     float OutScatter[3], float& OutT)
{
    OutScatter[0]=OutScatter[1]=OutScatter[2]=0.0f; OutT = 1.0f;
    float Span = Far-Near;
    if (Span <= 0.0f) return;
    uint32_t Budget = Medium == 0u ? (K.CloudControl[0] ? K.CloudControl[0] : 1u)
                                   : (K.CloudControl[1] ? K.CloudControl[1] : 1u);
    float Feature = Medium == 0u ? std::fmax(K.CloudShape[0]*900.0f, 1.0f)
                  : std::fmax((Medium == 1u ? K.LocalCloudParams[2] : K.LocalFogParams[2])*0.5f, 1.0f);
    float DesiredStep = Medium == 0u ? 4000.0f/float(Budget) : Feature;
    uint32_t Count = (uint32_t)std::fmax(std::ceil(Span/DesiredStep), 1.0f);
    uint32_t Cap = Medium == 0u ? (Budget*4u > 4u ? Budget*4u : 4u) : Budget;
    if (Count > Cap) Count = Cap;
    float Step = Span/float(Count);
    float G = Medium == 0u ? K.CloudShape[3]
            : (Medium == 1u ? K.LocalCloudParams[3] : K.LocalFogParams[3]);
    float Mu = Direction[0]*SunDirection[0]+Direction[1]*SunDirection[1]+Direction[2]*SunDirection[2];
    float Phase = TwinPhase(Mu, G);
    float Albedo[3];
    if (Medium == 0u) { Albedo[0]=K.CloudAlbedo[0]; Albedo[1]=K.CloudAlbedo[1]; Albedo[2]=K.CloudAlbedo[2]; }
    else if (Medium == 1u) { Albedo[0]=0.92f; Albedo[1]=0.94f; Albedo[2]=0.97f; }
    else { Albedo[0]=Albedo[1]=Albedo[2]=0.88f; }
    float T = 1.0f;
    float S[3] = { 0.0f, 0.0f, 0.0f };
    for (uint32_t I = 0u; I < Count; ++I)
    {
        float Tm = Near + (float(I)+0.5f)*Step;
        float P[3] = { Origin[0]+Direction[0]*Tm, Origin[1]+Direction[1]*Tm, Origin[2]+Direction[2]*Tm };
        float Density = Medium == 0u ? TwinCloudDensityAt(K, P) : TwinLocalDensityAt(K, P, Medium == 2u);
        if (Density <= 1e-5f) continue;
        float SunT = TwinSunTransmittance(K, P, SunDirection);
        float Extinction = Density*Step*kTwinExtinction;
        float SegmentT = std::exp(-Extinction);
        float Incoming[3] = { SunRadiance[0]*SunT*Phase+Ambient[0],
                              SunRadiance[1]*SunT*Phase+Ambient[1],
                              SunRadiance[2]*SunT*Phase+Ambient[2] };
        for (int C = 0; C < 3; ++C)
            S[C] += Incoming[C]*Albedo[C]*(1.0f-SegmentT)*T;
        T *= SegmentT;
        if (T < 0.005f) break;
    }
    OutScatter[0]=S[0]; OutScatter[1]=S[1]; OutScatter[2]=S[2]; OutT = T;
}
void TwinCloudAlong(const SkyConstantRecord& K, const float Origin[3], const float Direction[3],
                    float Maximum, const float SunDirection[3], const float SunRadiance[3],
                    const float Ambient[3], float OutScatter[3], float& OutT)
{
    OutScatter[0]=OutScatter[1]=OutScatter[2]=0.0f; OutT = 1.0f;
    float Near, Far;
    if ((K.Control[2] & kTwinLayerFlag) != 0u &&
        TwinSlabInterval(K, Origin, Direction, Maximum, Near, Far))
    {
        float S[3]; float T;
        TwinMarchMedium(K, Origin, Direction, Near, Far, 0u, SunDirection, SunRadiance, Ambient, S, T);
        for (int C = 0; C < 3; ++C) OutScatter[C] += S[C]*OutT;
        OutT *= T;
    }
    if ((K.Control[2] & kTwinLocalCloudFlag) != 0u &&
        TwinBoxInterval(Origin, Direction, K.LocalCloudCentre, K.LocalCloudHalfSize, Maximum, Near, Far))
    {
        float S[3]; float T;
        TwinMarchMedium(K, Origin, Direction, Near, Far, 1u, SunDirection, SunRadiance, Ambient, S, T);
        for (int C = 0; C < 3; ++C) OutScatter[C] += S[C]*OutT;
        OutT *= T;
    }
    if ((K.Control[2] & kTwinLocalFogFlag) != 0u &&
        TwinBoxInterval(Origin, Direction, K.LocalFogCentre, K.LocalFogHalfSize, Maximum, Near, Far))
    {
        float S[3]; float T;
        TwinMarchMedium(K, Origin, Direction, Near, Far, 2u, SunDirection, SunRadiance, Ambient, S, T);
        for (int C = 0; C < 3; ++C) OutScatter[C] += S[C]*OutT;
        OutT *= T;
    }
}
// ── end of the SkyRecords.slang transcription ────────────────────────────────────────────────────────────────

constexpr float kPi = 3.14159265358979323846f;

int Failures = 0;
void Expect(bool Ok, const char* What)
{
    if (!Ok) ++Failures;
    std::printf("  %-66s %s\n", What, Ok ? "PASS" : "FAIL");
}

// Morning, facing the sun: the streak-exposing frame. Copied from the streak lab's solver.
float SolveHourForElevation(float Elevation, float FromHour, float ToHour)
{
    CelestialObservation Probe{};
    Probe.Year = 2026; Probe.Month = 9; Probe.Day = 10;
    Probe.UtcOffset = 2.0f; Probe.Latitude = -26.19f; Probe.Longitude = 28.32f;
    float Hour = FromHour, Best = 1e9f;
    for (float H = FromHour; H <= ToHour; H += 0.01f)
    {
        Probe.LocalHours = H;
        const float Residual = std::fabs(CelestialSolver::Solve(Probe).Sun.Elevation - Elevation);
        if (Residual < Best) { Best = Residual; Hour = H; }
    }
    return Hour;
}

// The streak gauge: high-pass along elevation vs azimuth over the sky band. A coherent field leaves a small
//    elevation residual; the shredded slab decorrelates every row and the residual dominates.
void StreakGauge(const unsigned char* Rgb, uint32_t W, uint32_t H, float& Dy, float& Dx)
{
    const int Y0 = 20, Y1 = (int)H-60;
    auto Lum = [&](int X, int Y) -> float {
        const unsigned char* P = Rgb + ((size_t)Y*W+X)*3u;
        return (0.2126f*P[0]+0.7152f*P[1]+0.0722f*P[2])/255.0f;
    };
    constexpr int R = 8;
    double Sy = 0.0, Sx = 0.0; long N = 0;
    for (int Y = Y0+R+1; Y < Y1-R-1; ++Y) for (int X = R+1; X < (int)W-R-1; ++X)
    {
        float TrendY = 0.0f, TrendX = 0.0f;
        for (int K = -R; K <= R; ++K) { TrendY += Lum(X, Y+K); TrendX += Lum(X+K, Y); }
        TrendY /= float(2*R+1); TrendX /= float(2*R+1);
        float ResY = Lum(X, Y)-TrendY, ResY1 = Lum(X, Y+1)-TrendY;
        float ResX = Lum(X, Y)-TrendX, ResX1 = Lum(X+1, Y)-TrendX;
        // NOTE: the neighbour residual reuses the centre trend — deliberately, so the gauge reads the field's
        //    own roughness rather than the trend's slope; the trend is smooth over one pixel either way.
        Sy += std::fabs(ResY1-ResY); Sx += std::fabs(ResX1-ResX); ++N;
    }
    Dy = float(Sy/double(N)); Dx = float(Sx/double(N));
}
} // namespace

int main()
{
    std::printf("[SkyCloudKernel] the kernel's cloud path, re-transcribed and rendered\n");

    FidelityClassifier Classifier;
    const FidelityCriteria Criteria = Classifier.ConstructCriteria(FidelityCategory::StandardFidelity);
    const CelestialBudget Budget = CelestialTier::BudgetFor(Criteria);

    CelestialSequence Sky;
    Sky.Budget = Budget;
    Sky.Prepare();
    for (uint32_t E = 0u; E < kCelestialEntityCount; ++E) Sky.Shown[E] = true;
    Sky.Observation.Year = 2026; Sky.Observation.Month = 9; Sky.Observation.Day = 10;
    Sky.Observation.LocalHours = SolveHourForElevation(10.0f, 6.0f, 10.0f);
    Sky.Observation.UtcOffset = 2.0f; Sky.Observation.Latitude = -26.19f; Sky.Observation.Longitude = 28.32f;
    const float TickOrigin[3] = { 0.0f, 0.0f, 2.0f };
    Sky.Tick(0.0f, TickOrigin, 0.0f);

    AtmosphereMedium Medium = Sky.Medium;
    AtmosphereLight Light = Sky.Light;
    for (int C = 0; C < 3; ++C) Light.Direction[C] = Sky.Frame().Sun.Direction[C];
    Light.Intensity *= Sky.SkyBrightness;
    for (int C = 0; C < 3; ++C) Light.Colour[C] *= Sky.SkyTint[C];
    CloudLayerSettings Cloud = Sky.Cloud;
    WindSettings Wind = Sky.Wind;
    const VolumetricBudget& VB = Budget.Volumetrics;
    const float SunEl = std::asin(std::fmax(-1.0f, std::fmin(1.0f, Light.Direction[2]))) * 180.0f / kPi;
    const float DayF = (SunEl <= -12.0f) ? 0.0f : (SunEl >= 0.0f) ? 1.0f
        : (SunEl + 12.0f) / 12.0f * ((SunEl + 12.0f) / 12.0f) * (3.0f - 2.0f * (SunEl + 12.0f) / 12.0f);
    float CloudSunRad[3] = { Light.Colour[0] * Light.Intensity * DayF,
                             Light.Colour[1] * Light.Intensity * DayF,
                             Light.Colour[2] * Light.Intensity * DayF };

    // Morning camera: face the sun's horizontal azimuth, level. Half-fov 55deg like the showcase.
    float F[3] = { 0.0f, 1.0f, 0.0f };
    {
        const float Hx = Light.Direction[0], Hy = Light.Direction[1];
        const float Hl = std::sqrt(Hx * Hx + Hy * Hy);
        if (Hl > 1e-6f) { F[0] = Hx / Hl; F[1] = Hy / Hl; F[2] = 0.0f; }
    }
    float R[3] = { F[1], -F[0], 0.0f };
    {
        const float Rl = std::sqrt(R[0]*R[0] + R[1]*R[1]);
        R[0] /= Rl; R[1] /= Rl;
    }
    float U[3] = { R[1]*F[2] - R[2]*F[1], R[2]*F[0] - R[0]*F[2], R[0]*F[1] - R[1]*F[0] };
    const float Eye[3] = { 0.0f, -6.0f, 1.7f };
    constexpr uint32_t kW = 480u, kH = 270u;
    const float TanHalf = std::tan(55.0f * kPi / 180.0f);
    const float Aspect = (float)kW / (float)kH;
    ColourTransfer Colour{};

    // 1 ─ the drift bound, both twins: the altitude-varying part of the offset can never span more than the
    //    clamp's width across the slab, whatever the sliders say (deterministic — no field instance involved).
    {
        bool CpuOk = true, KernelOk = true;
        const float Cases[3][2] = { { 0.0f, 0.0f }, { 1.0f, 31.0f }, { 2.0f, 60.0f } };
        for (const auto& Case : Cases)
        {
            WindSettings Probe = Wind;
            Probe.Shear = Case[0]; Probe.Veer = Case[1];
            float Cell = std::fmax(Cloud.Scale*900.0f, 1.0f);
            float Mid = Cloud.Base + 0.5f*Cloud.Thickness;
            float Ref[2]; WindField::AdvectDrift(Probe, Mid, Mid, 25128.0f, Cell, 0.8f, Ref);
            // The kernel twin reads the record's own packed wind; steer the pack through the live wind so both
            //    twins face the same sliders.
            WindSettings SavedWind = Sky.Wind;
            Sky.Wind = Probe;
            SkyConstantRecord ProbeK = Sky.PackSkyRecord();
            Sky.Wind = SavedWind;
            for (float A = Cloud.Base-200.0f; A <= Cloud.Base+Cloud.Thickness+200.0f; A += 50.0f)
            {
                float D[2]; WindField::AdvectDrift(Probe, A, Mid, 25128.0f, Cell, 0.8f, D);
                float Span = std::fmax(std::fabs(D[0]-Ref[0]), std::fabs(D[1]-Ref[1]));
                if (Span > 4.0f*Cell + 1.0f) CpuOk = false;
                float G[2], GRef[2];
                TwinDriftAt(ProbeK, A, Mid, Cell, 0.8f, G);
                TwinDriftAt(ProbeK, Mid, Mid, Cell, 0.8f, GRef);
                float KernelSpan = std::fmax(std::fabs(G[0]-GRef[0]), std::fabs(G[1]-GRef[1]));
                if (KernelSpan > 4.0f*Cell + 1.0f) KernelOk = false;
            }
        }
        Expect(CpuOk, "CPU drift: shear/veer span <= 4 cells at any slider extreme");
        Expect(KernelOk, "kernel drift: shear/veer span <= 4 cells at any slider extreme");
    }

    // 2 ─ render the morning frame through the kernel twin, and through the raster for the parity check. The
    //    per-pixel frame is the lab's (atmosphere + aureole + ground + colour); only the cloud march swaps.
    static unsigned char TwinRgb[(size_t)kW*kH*3u], RasterRgb[(size_t)kW*kH*3u];
    auto Render = [&](const SkyConstantRecord& K, unsigned char* Rgb, bool Twin) {
        float CloudTime = Sky.Observation.LocalHours * 3600.0f;
        for (uint32_t Py = 0; Py < kH; ++Py) for (uint32_t Px = 0; Px < kW; ++Px)
        {
            const float Sx = (2.0f * ((Px + 0.5f) / (float)kW) - 1.0f) * TanHalf * Aspect;
            const float Sy = (1.0f - 2.0f * ((Py + 0.5f) / (float)kH)) * TanHalf;
            float Dir[3] = { F[0] + R[0]*Sx + U[0]*Sy, F[1] + R[1]*Sx + U[1]*Sy, F[2] + R[2]*Sx + U[2]*Sy };
            const float Dl = std::sqrt(Dir[0]*Dir[0] + Dir[1]*Dir[1] + Dir[2]*Dir[2]);
            Dir[0] /= Dl; Dir[1] /= Dl; Dir[2] /= Dl;
            const AtmosphereSample S = AtmosphereModel::Integrate(Medium, Light, 2.0f, Dir,
                Budget.AtmosphereSamples, Budget.AtmosphereLightSamples);
            float Out[3] = { S.Radiance[0], S.Radiance[1], S.Radiance[2] };
            if (!S.HitGround)
            {
                const float Ad = Dir[0]*Light.Direction[0] + Dir[1]*Light.Direction[1] + Dir[2]*Light.Direction[2];
                const float Ang = std::acos(std::fmax(-1.0f, std::fmin(1.0f, Ad)));
                const float Lum = Out[0]*0.2126f + Out[1]*0.7152f + Out[2]*0.0722f;
                const float Gate = std::exp(-Ang / kAureoleSigma);
                const float Tgt = Lum <= kAureoleKnee ? Lum : kAureoleKnee + (Lum - kAureoleKnee) * kAureoleSlope;
                const float Mix = Lum > 0.0f ? (Lum + (Tgt - Lum) * Gate) / Lum : 1.0f;
                Out[0] *= Mix; Out[1] *= Mix; Out[2] *= Mix;
            }
            float CloudMaxDist = 1e30f;
            if (S.HitGround)
            {
                const float Centre[3] = { 0.0f, 0.0f, Medium.PlanetRadius + 2.0f };
                float N = 0.0f, Fa = 0.0f;
                if (AtmosphereModel::IntersectSphere(Centre, Dir, Medium.PlanetRadius, N, Fa) && N > 0.0f)
                {
                    CloudMaxDist = N;
                    const float Hit[3] = { Centre[0] + Dir[0]*N, Centre[1] + Dir[1]*N, Centre[2] + Dir[2]*N };
                    const float Hl = std::sqrt(Hit[0]*Hit[0] + Hit[1]*Hit[1] + Hit[2]*Hit[2]);
                    if (Hl > 0.0f)
                    {
                        const float Nl[3] = { Hit[0]/Hl, Hit[1]/Hl, Hit[2]/Hl };
                        const float NdotL = Nl[0]*Light.Direction[0] + Nl[1]*Light.Direction[1] + Nl[2]*Light.Direction[2];
                        const float Lit = NdotL > 0.0f ? NdotL : 0.0f;
                        for (int C = 0; C < 3; ++C)
                            Out[C] += Sky.GroundAlbedo[C] * Lit * Light.Intensity * 0.05f * S.Transmittance[C];
                    }
                }
            }
            if (Twin)
            {
                // The twin reads the record exactly as the kernel does — packed sun, packed clock, packed
                //    budgets — so the pack itself is under test alongside the transcription.
                float SunRad[3] = { K.SunRadiance[0]*DayF, K.SunRadiance[1]*DayF, K.SunRadiance[2]*DayF };
                float Maximum = CloudMaxDist > kTwinMaxDistance ? kTwinMaxDistance : CloudMaxDist;
                float Scatter[3]; float T = 1.0f;
                TwinCloudAlong(K, Eye, Dir, Maximum, K.SunDirection, SunRad, Out, Scatter, T);
                for (int C = 0; C < 3; ++C) Out[C] = Out[C]*T + Scatter[C];
            }
            else
            {
                const VolumetricSample V = VolumetricMedia::March(Cloud, Sky.LocalCloud, Sky.LocalFog, Wind, VB,
                    Eye, Dir, CloudMaxDist, Light.Direction, CloudSunRad, Out, CloudTime);
                for (int C = 0; C < 3; ++C) Out[C] = Out[C] * V.Transmittance + V.Scatter[C];
            }
            unsigned char Enc[3];
            ColourPipeline::ApplyToByte(Colour, Out, Enc);
            Rgb[((size_t)Py * kW + Px) * 3u + 0u] = Enc[0];
            Rgb[((size_t)Py * kW + Px) * 3u + 1u] = Enc[1];
            Rgb[((size_t)Py * kW + Px) * 3u + 2u] = Enc[2];
        }
    };

    SkyConstantRecord K = Sky.PackSkyRecord();
    Render(K, TwinRgb, true);
    Render(K, RasterRgb, false);
    PngWriteShim::WritePng("Diagnostics/SkyCloudKernel_Morning.png", kW, kH, 3, TwinRgb, (int)kW * 3);

    float Dy = 0.0f, Dx = 0.0f, MeanTwin = 0.0f, Diff = 0.0f;
    {
        StreakGauge(TwinRgb, kW, kH, Dy, Dx);
        double Sum = 0.0, D = 0.0; long N = 0;
        for (uint32_t I = 0; I < kW*kH; ++I)
            for (int C = 0; C < 3; ++C)
            {
                Sum += TwinRgb[I*3u+C]/255.0;
                D += std::fabs(int(TwinRgb[I*3u+C])-int(RasterRgb[I*3u+C]))/255.0; ++N;
            }
        MeanTwin = float(Sum/double(N)); Diff = float(D/double(N));
        std::printf("  morning: streakDy=%.4f streakDx=%.4f ratio=%.2f mean=%.3f twinVsRaster=%.4f\n",
                    Dy, Dx, Dy/std::fmax(Dx, 1e-6f), MeanTwin, Diff);
    }
    Expect(Dy < 0.06f, "morning kernel render: elevation residual stays coherent");
    Expect(Dy < 1.6f*Dx + 0.01f, "morning kernel render: no directional streak signature");
    Expect(MeanTwin > 0.15f && MeanTwin < 0.85f, "morning kernel render: mean luminance sane");
    Expect(Diff < 0.05f, "kernel twin matches the raster march (dual-path parity)");

    // 3 ─ two cloud-hours later the OLD drift had piled 58 cells of shear across the slab; the uniform drift
    //    must render the same coherent field. Only the clock advances — the sun stays put.
    {
        double SavedHours = Sky.Observation.LocalHours;
        Sky.Observation.LocalHours = SavedHours + 2.0;
        SkyConstantRecord Late = Sky.PackSkyRecord();
        Render(Late, TwinRgb, true);
        Sky.Observation.LocalHours = SavedHours;
        float LateDy = 0.0f, LateDx = 0.0f;
        StreakGauge(TwinRgb, kW, kH, LateDy, LateDx);
        double Sum = 0.0; long N = 0;
        for (uint32_t I = 0; I < kW*kH; ++I)
            for (int C = 0; C < 3; ++C) { Sum += TwinRgb[I*3u+C]/255.0; ++N; }
        float LateMean = float(Sum/double(N));
        std::printf("  +2h cloud clock: streakDy=%.4f streakDx=%.4f ratio=%.2f mean=%.3f\n",
                    LateDy, LateDx, LateDy/std::fmax(LateDx, 1e-6f), LateMean);
        Expect(LateDy < 0.06f, "+2h kernel render: elevation residual stays coherent");
        Expect(LateDy < 1.6f*LateDx + 0.01f, "+2h kernel render: no directional streak signature");
        Expect(LateMean > 0.15f && LateMean < 0.85f, "+2h kernel render: mean luminance sane");
    }

    if (Failures == 0) std::printf("[SkyCloudKernel] OK\n");
    else std::printf("[SkyCloudKernel] FAILED (%d)\n", Failures);
    return Failures == 0 ? 0 : 1;
}
