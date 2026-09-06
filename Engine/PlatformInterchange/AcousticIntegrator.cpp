//============================================================================================================================================
//                                                     ACOUSTICINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 The vehicle voice, ported statement for statement from the AudioEditor's `AcousticIntegrator` (Tools/AudioEditor/index.html,
//    <script id="dsp">). Read it next to the JavaScript: the numbered stages of Render() are the JavaScript's, the local
//    names are the JavaScript's in PascalCase, and every expression keeps its association order — the two are held to
//    ±1 × 10⁻⁶ per sample over a whole dyno pull (Scratchpad/AcousticIdentityTest), which one re-associated product
//    inside a feedback path would break.
//
//    Randomness order per sample (the contract, enforced by the identity proof):
//        events      FIRE: gauss (amplitude) · uniform (misfire) · uniform (walk step, pure mode too) · gauss (timing jitter, only
//                    when timing_jitter_deg > 0 and not pure)     CLATTER: uniform · gauss     INTAKE: gauss (only while the intake
//                    voice is on)
//        voices      per active cylinder: signed (crackle, if gain > 0) · signed (howl, if gain > 0)
//        intake      signed (breath) once per sample while the intake voice is on
//        transients  uniform (backfire chance) · anti-lag: uniform (count) then per pop uniform · uniform · blow-off: none ·
//                    overrun: uniform (rate test) then uniform ×4 per pop · TransientSlots::Advance: signed per sounding slot
//        turbo       signed (rush noise) while enabled     mechanical: signed (clatter noise) while clatter_level > 0
//
//    Rev 2 (voice.crank_pulse off) and rev 3 (on) share one loop; the archives decide. Units: [Hz] [rpm] [°] [s] [bar] [-].

#include "AcousticIntegrator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONSTANTS (the JavaScript's)
//------------------------------------------------------------------------------------------------------------------------

constexpr uint8_t  EventFire    = 0u;
constexpr uint8_t  EventClatter = 1u;
constexpr uint8_t  EventIntake  = 2u;
constexpr double   IntakeEventDeg     = 360.0;     // [°] intake event: TDC overlap, 360° after the firing TDC
constexpr double   ExhaustKernelDeg   = 300.0;     // [°] exhaust kernel span after EVO
constexpr double   ExhaustOpenDeg     = 250.0;     // [°] exhaust valve open duration
constexpr double   ValveRampDeg       = 40.0;      // [°] open-area ramps at each end of a valve event
constexpr double   IntakeKernelDeg    = 260.0;     // [°] intake kernel span after the TDC overlap
constexpr double   IntakeEarOffsetM   = 0.3;       // [m] the intake voice reaches the right channel this much later
constexpr double   IntakeOpenDeg      = 240.0;     // [°] intake valve open duration from TDC
constexpr double   KernelSmoothDeg    = 3.0;       // [°] raised-cosine smoothing width
constexpr double   CylinderSpread[8]  = { 1.0, -0.73, -0.54, 0.55, -0.46, 0.18, 0.4, -0.4 };   // zero-mean imbalance pattern along the firing order
constexpr uint32_t RebuildSamples     = 128u;      // [samples] per-slice constants refresh cadence
constexpr double   RpmReference       = 8000.0;    // [rpm] RevSim's rpmFactor = rpm / 8000
constexpr double   WalkReversion      = 0.02;      // [-] firing-time walk returns to the crank at 2 % per firing
constexpr double   SpeedOfSound       = 343.0;     // [m/s]
constexpr double   Pi                 = 3.141592653589793;   // JavaScript Math.PI (MSVC has no M_PI without _USE_MATH_DEFINES)

constexpr ListenerWeights ListenerSheet[3] =
{
    { 1.0,  1.0, 1.0, 1.0, 1.0, 16000.0 },   // trackside
    { 1.0,  0.6, 0.5, 1.0, 0.5,  7000.0 },   // chase
    { 0.45, 1.2, 1.2, 0.6, 0.3,  3500.0 },   // cockpit
};
constexpr const char* ListenerNames[3] = { "trackside", "chase", "cockpit" };

[[nodiscard]] inline double Fmod1(double X) noexcept { return std::fmod(X, 1.0); }   // JavaScript % on non-negative operands

// effective open-area window: raised-cosine ramps of ValveRampDeg, plateau between
[[nodiscard]] double Lift(double K, double OpenDeg) noexcept
{
    if (K <= 0.0 || K >= OpenDeg) return 0.0;
    const double Ramp = std::min(ValveRampDeg, 0.5 * OpenDeg);
    if (K < Ramp) return 0.5 - 0.5 * std::cos(Pi * K / Ramp);
    if (K > OpenDeg - Ramp) return 0.5 - 0.5 * std::cos(Pi * (OpenDeg - K) / Ramp);
    return 1.0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   LISTENER PRESETS
//------------------------------------------------------------------------------------------------------------------------

const ListenerWeights& QueryListenerWeights(ListenerPreset Preset) noexcept
{
    const uint32_t I = uint32_t(Preset);
    return ListenerSheet[I < 3u ? I : 0u];
}

const char* QueryListenerName(ListenerPreset Preset) noexcept
{
    const uint32_t I = uint32_t(Preset);
    return ListenerNames[I < 3u ? I : 0u];
}

bool ParseListenerPreset(const char* Name, ListenerPreset& Out) noexcept
{
    if (!Name) return false;
    for (uint32_t I = 0u; I < 3u; ++I)
        if (std::strcmp(Name, ListenerNames[I]) == 0) { Out = ListenerPreset(I); return true; }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONSTRUCTION AND PREPARE
//------------------------------------------------------------------------------------------------------------------------

AcousticIntegrator::AcousticIntegrator(const AcousticStructure& Initial, uint32_t Seed) noexcept
    : Pending(Initial), StructureRelay(Initial), Demand(PowertrainRecord{}), Readout(AcousticReadout{}), Meters(AcousticMeterRecord{}),
      SeedRequest(Seed), Listener(ListenerSheet[0])
{
    Structure = Initial;
}

void AcousticIntegrator::AssignStructure(const AcousticStructure& S) noexcept
{
    Pending = S;
    StructureRelay.Publish(S);
}

void AcousticIntegrator::Prepare(uint32_t SampleRate, uint32_t ChannelCount) noexcept
{
    // the JavaScript constructor: rate, lags, sheets, zeroed voices, then assignStructure
    Rate = SampleRate == 0u ? 48000u : SampleRate; Channels = ChannelCount == 0u ? 2u : ChannelCount;
    Fs = double(Rate); Dt = 1.0 / Fs;
    Rng.Reseed(SeedRequest);
    DemandRpm = 900.0; DemandThrottle = 0.0; DemandLoad = 0.0; DemandBoost = 0.0;
    RpmS = 900.0; ThrottleS = 0.0; LoadS = 0.0;
    RpmLag = LagCoefficient(Fs, 0.004); ThrottleLag = LagCoefficient(Fs, 0.010); LoadLag = LagCoefficient(Fs, 0.005);
    MaxRpmStep = 50000.0 * Dt;
    Theta = 0.0; CycleIndex = 0.0; SampleIndex = 0u; FiringWalkDeg = 0.0;
    for (uint32_t C = 0u; C <= AcousticMaxCylinders; ++C)
    {
        VoiceActive[C] = 0u; VoiceT[C] = 0.0; VoiceDeg[C] = 0.0; VoiceAmp[C] = 0.0; RingHz[C] = 0.0;
        IntakeActive[C] = 0u; IntakeDeg[C] = 0.0; IntakeAmp[C] = 0.0;
        BankOfCylinder[C] = 0u; FiringAngle[C] = 0.0; SpreadOf[C] = 0.0;
    }
    for (uint32_t I = 0u; I < AcousticSheetSize; ++I)
    {
        const double Phase = double(I) / double(AcousticSheetSize); double V = 0.0;
        if (Phase < 0.05) V = Phase / 0.05;                                   // rapid spike
        else if (Phase < 0.3) V = 1.0 - 1.2 * (Phase - 0.05) / 0.25;           // blowdown to −0.2
        else if (Phase < 0.6) V = -0.2 * (1.0 - (Phase - 0.3) / 0.3);          // rarefaction recovery to 0
        PulseSheet[I] = V; BodySheet[I] = 0.0;
    }
    for (uint32_t I = 0u; I < AcousticKernelSize; ++I) { ExhaustKernelHard[I] = 0.0; ExhaustKernelSoft[I] = 0.0; IntakeKernel[I] = 0.0; }
    ExhaustKernelScale = double(AcousticKernelSize) / ExhaustKernelDeg; IntakeKernelScale = double(AcousticKernelSize) / IntakeKernelDeg;
    for (double& A : HarmonicAmps) A = 0.0;
    for (uint32_t H = 0u; H < AcousticHarmonicCount; ++H)
        for (uint32_t I = 0u; I < AcousticSheetSize; ++I)
        {
            const double Phase = double(I) / double(AcousticSheetSize);
            SineSheet[H][I] = std::sin(TwoPi * double(H + 2u) * Phase);   // the same expression the JavaScript evaluates inside rebuildSheet → the same double
        }
    ThumpHz = 65.0; DecayRate = 15.0; NoiseRate = 40.0; CrackleGain = 0.1; RingGain = 0.1;
    RaspGain = 0.0; HowlGain = 0.0; CamDepth = 0.0; VoiceDrive = 1.0;
    CrankPulse = false; IntakeGain = 0.0; BreathGain = 0.0; IntakeHz = 220.0; IntakeLag = LagCoefficient(Fs / double(RebuildSamples), 0.08);
    IntakeRes[0] = BiquadSection{}; IntakeRes[1] = BiquadSection{}; IntakeMouth = BiquadSection{}; IntakeDirect = 0.0;
    for (double& E : IntakePath) E = 0.0;
    IntakePathHead = 0u; IntakePathDelayL = 0u; IntakePathDelayR = 0u;
    for (uint32_t B = 0u; B < 2u; ++B) { BankLow[B] = BiquadSection{}; BankHigh[B] = BiquadSection{}; }
    PanSplit = false;
    ClatterEnv[0] = ClatterEnv[1] = 0.0; ClatterImpulse[0] = ClatterImpulse[1] = 0.0;
    ClatterDecay = std::exp(-Dt / 0.0012);
    ClatterBP1 = BiquadSection{}; ClatterBP2 = BiquadSection{}; ClatterRing = BiquadSection{};
    WhinePhase = 0.0; MotorPhase = 0.0; TurboPhase = 0.0;
    Spool = 0.0; Boost = 0.0; RushBP = BiquadSection{}; RushHz = 2000.0;
    Transients.Prepare(Fs);
    LiftArmedBackfire = false; LiftArmedTurbo = false;
    OverrunActive = false; OverrunEnv = 0.0; PopBoost = 0.0; PopBoostDecay = std::exp(-Dt / 0.06);
    for (uint32_t Ch = 0u; Ch < 2u; ++Ch)
    {
        FormantA[Ch] = BiquadSection{}; FormantB[Ch] = BiquadSection{};
        Silencer[Ch][0] = BiquadSection{}; Silencer[Ch][1] = BiquadSection{};
        Comb[Ch].Clear(); ValveHP[Ch] = BiquadSection{}; ListenerLP[Ch] = OnePole{};
    }
    SilencerHz = 24000.0; SilencerBypass = true; ValveGain = 1.0;
    ValveHz = 10.0; ValveLag = LagCoefficient(Fs / double(RebuildSamples), 0.2);
    PureTone = PureRequest.load(std::memory_order_relaxed);
    FiringCount = 0u; PopCount = 0u; ClippedCount = 0u;
    ListenerApplied = ListenerRequest.load(std::memory_order_relaxed);
    ApplyListener(ListenerPreset(ListenerApplied));
    for (double& M : Meter) M = 0.0;
    MeterFrames = 0u;
    MeterInterval = MeterIntervalRequest.load(std::memory_order_relaxed);
    Scope.Count = 0u; ScopeFill = 0u; ScopeCapturing = false; ScopeCycleSamples = 0u;
    ScopeReady.store(false, std::memory_order_relaxed);
    SignatureValid = false; CylinderCount = 0u; EventCount = 0u;
    // the newest structure wins (the relay may hold one newer than Pending if a publish raced Prepare — same thread, so no)
    AcousticStructure Newest = Pending;
    StructureRelay.Take(Newest);
    ApplyStructure(Newest);
    Readout.Place(AcousticReadout{});
    Meters.Place(AcousticMeterRecord{});
    Prepared = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LISTENER / GEOMETRY / KERNELS
//------------------------------------------------------------------------------------------------------------------------

void AcousticIntegrator::ApplyListener(ListenerPreset Preset) noexcept
{
    Listener = QueryListenerWeights(Preset);
    ListenerLP[0].SetCutoff(Fs, Listener.CutoffHz); ListenerLP[1].SetCutoff(Fs, Listener.CutoffHz);
}

// Firing angles from the geometry (the JavaScript static firingAngles): even → j · 720/N along the order; shared straight pins
//    on a V (even_firing off, banks two equal halves) → each bank at 720/(N/2), bank 1 trailing by the V angle.
void AcousticIntegrator::FiringAngles(uint32_t N, const int32_t* Order, const uint8_t* Bank, bool EvenFiring, double BankAngleDeg, double* Angle) noexcept
{
    for (uint32_t C = 0u; C <= N; ++C) Angle[C] = 0.0;
    uint32_t Bank0 = 0u; for (uint32_t C = 1u; C <= N; ++C) if (!Bank[C]) ++Bank0;
    const bool Shared = !EvenFiring && N >= 2u && (N % 2u) == 0u && Bank0 == N / 2u;
    int32_t PerBank[2] = { 0, 0 };
    const double BankStep = 720.0 / double(std::max(1u, N / 2u));
    for (uint32_t J = 0u; J < N; ++J)
    {
        const uint32_t Cyl = uint32_t(ClampReal(double(Order[J]), 1.0, double(N)));
        if (!Shared) { Angle[Cyl] = double(J) * 720.0 / double(N); continue; }
        const uint8_t B = Bank[Cyl];
        Angle[Cyl] = double(PerBank[B]) * BankStep + (B ? BankAngleDeg : 0.0);
        ++PerBank[B];
    }
}

double AcousticIntegrator::FinishKernel(double* Kernel, double SpanDeg) noexcept
{
    const uint32_t Size = AcousticKernelSize;
    const double PerDeg = double(Size) / SpanDeg;
    double* Discharge = KernelScratchA;
    for (uint32_t I = 0u; I < Size; ++I) Discharge[I] = Kernel[I];
    for (uint32_t I = 0u; I < Size; ++I)
    {
        const double A = I > 0u ? Discharge[I - 1u] : 0.0, B = I + 1u < Size ? Discharge[I + 1u] : 0.0;
        Kernel[I] = 0.5 * (B - A) * PerDeg;
    }
    const int32_t Half = std::max(1, int32_t(RoundHalfUp(0.5 * KernelSmoothDeg * PerDeg)));
    double Win[64] = {}; double WSum = 0.0;   // Half ≤ 11 for the fixed spans (3° × 2048/260 / 2 ≈ 11.8 → 12): 25 taps
    for (int32_t J = -Half; J <= Half; ++J) { const double W = 0.5 + 0.5 * std::cos(Pi * double(J) / double(Half + 1)); Win[J + Half] = W; WSum += W; }
    double* Src = KernelScratchB;
    for (uint32_t I = 0u; I < Size; ++I) Src[I] = Kernel[I];
    double Peak = 1.0e-9;
    for (uint32_t I = 0u; I < Size; ++I)
    {
        double Acc = 0.0;
        for (int32_t J = -Half; J <= Half; ++J) { const int32_t Idx = int32_t(I) + J; Acc += Win[J + Half] * (Idx < 0 || Idx >= int32_t(Size) ? 0.0 : Src[Idx]); }
        Kernel[I] = Acc / WSum; if (std::fabs(Kernel[I]) > Peak) Peak = std::fabs(Kernel[I]);
    }
    return Peak;
}

void AcousticIntegrator::BuildKernels() noexcept
{
    const AcousticStructure::VoiceSheet& Vo = Structure.Voice;
    const double Evo = Structure.Combustion.EvoDeg;
    const double Expo = std::max(0.25, Vo.PulseSharpness);
    auto Build = [&](double* Kernel, double RiseDeg, double BlowdownDeg, double BlowdownLevel) -> double
    {
        for (uint32_t I = 0u; I < AcousticKernelSize; ++I)
        {
            const double K = double(I) / double(AcousticKernelSize) * ExhaustKernelDeg, A = Evo + K;   // K: degrees after EVO, A: after the firing TDC
            const double Blow = BlowdownLevel * (K < RiseDeg ? 0.5 - 0.5 * std::cos(Pi * std::pow(K / RiseDeg, Expo)) : std::exp(-2.0 * (K - RiseDeg) / BlowdownDeg));
            const double Vel = A < 180.0 ? 0.0 : std::sin(Pi * (A - 180.0) / 180.0);
            Kernel[I] = Blow + Lift(K, ExhaustOpenDeg) * Vo.PulseTailLevel * (Vel >= 0.0 ? Vel : Vel * Vo.PulseUndershoot);
        }
        return FinishKernel(Kernel, ExhaustKernelDeg);
    };
    const double Peak = Build(ExhaustKernelHard, Vo.PulseRiseDeg, Vo.PulseBlowdownDeg, 1.0);
    Build(ExhaustKernelSoft, Vo.PulseRiseDeg * Vo.PulseSoftStretch, Vo.PulseBlowdownDeg * 0.5 * (1.0 + Vo.PulseSoftStretch), Vo.PulseSoftLevel);
    for (uint32_t I = 0u; I < AcousticKernelSize; ++I) { ExhaustKernelHard[I] /= Peak; ExhaustKernelSoft[I] /= Peak; }
    double* Ik = IntakeKernel;
    for (uint32_t I = 0u; I < AcousticKernelSize; ++I)
    {
        const double K = double(I) / double(AcousticKernelSize) * IntakeKernelDeg;   // degrees after the TDC overlap
        Ik[I] = -Lift(K, IntakeOpenDeg) * std::sin(Pi * K / 180.0);
    }
    const double IPeak = FinishKernel(Ik, IntakeKernelDeg);
    for (uint32_t I = 0u; I < AcousticKernelSize; ++I) Ik[I] /= IPeak;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   APPLY STRUCTURE (assignStructure)
//------------------------------------------------------------------------------------------------------------------------

void AcousticIntegrator::ApplyStructure(const AcousticStructure& S) noexcept
{
    Structure = S;
    const AcousticStructure::VehicleSheet& V = S.Vehicle;
    const uint32_t N = uint32_t(ClampReal(RoundHalfUp(double(V.CylinderCount)), 1.0, double(AcousticMaxCylinders)));
    const bool CountChanged = EventCount == 0u || CylinderCount != N;
    CylinderCount = N;
    int32_t Order[AcousticMaxCylinders] = {};
    const bool OrderGiven = V.FiringOrder.Count == N;
    for (uint32_t J = 0u; J < N; ++J) Order[J] = OrderGiven ? int32_t(RoundHalfUp(V.FiringOrder.Entry[J])) : int32_t(J + 1u);
    for (uint32_t C = 0u; C <= AcousticMaxCylinders; ++C) BankOfCylinder[C] = 0u;
    for (uint32_t C = 1u; C <= N; ++C)
    {
        // (v.bank_of_cylinder[c − 1] | 0) ? 1 : 0 — a missing entry reads as 0
        const double Entry = C - 1u < V.BankOfCylinder.Count ? V.BankOfCylinder.Entry[C - 1u] : 0.0;
        BankOfCylinder[C] = (int32_t(Entry) != 0) ? 1u : 0u;
        RingHz[C] = S.Voice.RingHz + double((C - 1u) % 3u) * 1000.0;
    }
    FiringAngles(N, Order, BankOfCylinder, V.EvenFiring, V.BankAngleDeg, FiringAngle);
    EvenFiring = V.EvenFiring;
    for (uint32_t C = 0u; C <= AcousticMaxCylinders; ++C) SpreadOf[C] = 0.0;
    for (uint32_t J = 0u; J < N; ++J)
    {
        const uint32_t Cyl = uint32_t(ClampReal(double(Order[J]), 1.0, double(N)));
        const uint32_t Place = (N >= 2u && (N % 2u) == 0u) ? J % (N / 2u) : J;
        SpreadOf[Cyl] = CylinderSpread[Place % 8u];
    }
    // signature: the firing angles + evo_deg (the JavaScript joins them into a string; equality is the same test)
    bool SameSignature = SignatureValid && SignatureEvo == S.Combustion.EvoDeg;
    for (uint32_t C = 0u; C <= N && SameSignature; ++C) SameSignature = SignatureAngle[C] == FiringAngle[C];
    const bool RebuildEvents = CountChanged || !SameSignature;
    for (uint32_t C = 0u; C <= AcousticMaxCylinders; ++C) SignatureAngle[C] = FiringAngle[C];
    SignatureEvo = S.Combustion.EvoDeg; SignatureValid = true;
    if (RebuildEvents)
    {
        EventCount = N * AcousticEventsPerCylinder;
        const double CycleStart = std::floor(Theta / 720.0) * 720.0;
        const uint8_t Types[4]   = { EventFire, EventClatter, EventClatter, EventIntake };
        const double  Offsets[4] = { S.Combustion.EvoDeg, 375.0, 590.0, IntakeEventDeg };
        for (uint32_t C = 1u; C <= N; ++C)
        {
            const uint32_t I = (C - 1u) * AcousticEventsPerCylinder;
            for (uint32_t K = 0u; K < AcousticEventsPerCylinder; ++K)
            {
                EvType[I + K] = Types[K]; EvCyl[I + K] = uint8_t(C); EvBank[I + K] = BankOfCylinder[C];
                double A = CycleStart + FiringAngle[C] + Offsets[K];
                while (A < Theta) A += 720.0;
                EvAnchor[I + K] = A; EvNext[I + K] = A; EvJitter[I + K] = 0.0;
            }
        }
        FiringWalkDeg = 0.0;
        if (CountChanged) for (uint32_t C = 0u; C <= AcousticMaxCylinders; ++C) { VoiceActive[C] = 0u; IntakeActive[C] = 0u; }
    }
    else
    {
        for (uint32_t C = 1u; C <= N; ++C) for (uint32_t K = 0u; K < AcousticEventsPerCylinder; ++K) EvBank[(C - 1u) * AcousticEventsPerCylinder + K] = BankOfCylinder[C];
    }
    IdleRpm = V.IdleRpm; RedlineRpm = V.RedlineRpm;
    FiringInterval = 720.0 / double(N);
    CrankPulse = S.Voice.CrankPulse;
    BuildKernels();
    ClatterBP1.Bandpass(Fs, S.Mechanical.ClatterHz, 1.5);
    ClatterBP2.Bandpass(Fs, S.Mechanical.ClatterHz * 1.8, 2.0);
    ClatterRing.Bandpass(Fs, S.Mechanical.ClatterHz * 1.3, 12.0);
    PopDecay = std::exp(-Dt / std::max(0.05, S.Combustion.PopDecayS));
    SpoolUp = LagCoefficient(Fs, S.Turbo.SpoolUpS); SpoolDown = LagCoefficient(Fs, S.Turbo.SpoolDownS);
    const AcousticStructure::ExhaustSheet& Ex = S.Exhaust;
    Comb[0].Configure(Fs, Ex.CombMs, Ex.CombFeedback, Ex.CombMix); Comb[1].Configure(Fs, Ex.CombMs, Ex.CombFeedback, Ex.CombMix);
    FormantA[0].Bandpass(Fs, Ex.FormantAHz, Ex.FormantAQ); FormantA[1].Bandpass(Fs, Ex.FormantAHz, Ex.FormantAQ);
    FormantB[0].Bandpass(Fs, Ex.FormantBHz, Ex.FormantBQ); FormantB[1].Bandpass(Fs, Ex.FormantBHz, Ex.FormantBQ);
    const bool Open = RpmS > Ex.ValveOpenRpm || LoadS > Ex.ValveOpenLoad;
    ValveHz = Open ? Ex.ValveOpenHpHz : Ex.ValveHpHz;
    ValveHP[0].Highpass(Fs, ValveHz, 1.12); ValveHP[1].Highpass(Fs, ValveHz, 1.12);
    SilencerHz = Open ? Ex.SilencerOpenHz : Ex.SilencerHz; ValveGain = Open ? Ex.ValveOpenGain : 1.0;
    PanSplit = Ex.PanSplitHz > 0.0;
    if (PanSplit) for (uint32_t B = 0u; B < 2u; ++B) { BankLow[B].Lowpass(Fs, Ex.PanSplitHz, 0.5); BankHigh[B].Highpass(Fs, Ex.PanSplitHz, 0.5); }   // Linkwitz-Riley 2nd order
    IntakeHz = IntakeTarget(RpmS);
    RebuildSheet(RpmS, LoadS);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PER-SLICE CONSTANTS (rebuildSheet)
//------------------------------------------------------------------------------------------------------------------------

double AcousticIntegrator::IntakeTarget(double Rpm) const noexcept
{
    const AcousticStructure::IntakeSheet& It = Structure.Intake;
    if (Rpm < It.StepRpm1) return It.RunnerHz;
    if (Rpm >= It.StepRpm2) return It.RunnerHz * It.MinRatio;
    const double Span = std::max(1.0, It.StepRpm2 - It.StepRpm1), U = (Rpm - It.StepRpm1) / Span;
    return It.RunnerHz * (It.ShortRatio + (0.75 * It.ShortRatio - It.ShortRatio) * U);
}

void AcousticIntegrator::RebuildSheet(double Rpm, double Load) noexcept
{
    const AcousticStructure& S = Structure;
    const AcousticStructure::VoiceSheet& Vo = S.Voice;
    const double RpmFactor = Rpm / RpmReference;
    ThumpHz = Vo.PitchHz + RpmFactor * Vo.PitchRiseHz;
    DecayRate = Vo.DecayPerS + Rpm * 0.001 * Vo.DecayPerKrpm;
    NoiseRate = 40.0 + Load * 60.0;
    CrackleGain = Vo.CrackleLevel + Vo.CrackleLoadLevel * Load;
    RingGain = (Vo.RingLevel + Vo.RingLoadLevel * Load) * std::max(0.0, 1.0 - Rpm / std::max(1.0, Vo.RingFadeRpm));
    RaspGain = Vo.Resonance > 1.0 ? std::max(0.0, RpmFactor - 0.2) * Load * (Vo.Resonance - 1.0) * Vo.RaspLevel : 0.0;
    HowlGain = (Vo.HowlLevel > 0.0 && Load > 0.2) ? std::max(0.0, (Rpm - Vo.HowlRpm) / std::max(1.0, Vo.HowlSpanRpm)) * Load * Vo.HowlLevel : 0.0;
    CamDepth = Vo.CamDepth;
    VoiceDrive = (1.0 + 0.8 * Load) * S.Exhaust.Drive;
    if (!CrankPulse)
    {
        const double Peak = 1.0 + RpmFactor * (8.0 + Vo.Resonance * 4.0);
        const AcousticRealList& List = Vo.Harmonics;
        for (uint32_t H = 0u; H < AcousticHarmonicCount; ++H)
        {
            const double Number = double(H + 1u);
            const double RestAmp = H < List.Count ? List.Entry[H] : 0.5 / std::pow(Number, 1.2);
            const double RpmBoost = std::max(0.0, 1.0 - std::fabs(Number - Peak) / 4.0);
            const double LoadBoost = Load * (Number / double(AcousticHarmonicCount)) * 2.0;
            HarmonicAmps[H] = RestAmp * (1.0 + RpmBoost * 1.5 + LoadBoost);
        }
        for (uint32_t I = 0u; I < AcousticSheetSize; ++I)
        {
            double V = 0.0;
            for (uint32_t H = 0u; H < AcousticHarmonicCount; ++H) V += SineSheet[H][I] * HarmonicAmps[H];   // same products, same order as the JavaScript's inline sin
            BodySheet[I] = V * 0.15;
        }
    }
    // exhaust valve high-pass, silencer low-pass and bypass gain follow rpm with a 0.2 s lag
    const AcousticStructure::ExhaustSheet& Ex = S.Exhaust;
    const bool Open = Rpm > Ex.ValveOpenRpm || Load > Ex.ValveOpenLoad;
    const double Target = Open ? Ex.ValveOpenHpHz : Ex.ValveHpHz;
    ValveHz += ValveLag * (Target - ValveHz);
    ValveHP[0].Highpass(Fs, ValveHz, 1.12); ValveHP[1].Highpass(Fs, ValveHz, 1.12);
    SilencerHz += ValveLag * ((Open ? Ex.SilencerOpenHz : Ex.SilencerHz) - SilencerHz);
    SilencerBypass = SilencerHz >= Fs * 0.45;
    if (!SilencerBypass) for (uint32_t Ch = 0u; Ch < 2u; ++Ch) { Silencer[Ch][0].Lowpass(Fs, SilencerHz, 0.5412); Silencer[Ch][1].Lowpass(Fs, SilencerHz, 1.3066); }
    ValveGain += ValveLag * ((Open ? Ex.ValveOpenGain : 1.0) - ValveGain);
    // rev 3 intake voice: tract frequency steps (0.08 s lag), level ∝ throttle · rpm, breath ∝ throttle · rpm
    const AcousticStructure::IntakeSheet& It = S.Intake;
    if (CrankPulse && It.Level > 0.0)
    {
        IntakeHz += IntakeLag * (IntakeTarget(Rpm) - IntakeHz);
        IntakeRes[0].Bandpass(Fs, IntakeHz, It.Q); IntakeRes[1].Bandpass(Fs, 3.0 * IntakeHz, It.Q); IntakeMouth.Highpass(Fs, 2.0 * IntakeHz, 0.707);
        IntakeGain = It.Level * (0.05 + 0.95 * Load) * ClampReal(Rpm / std::max(1.0, RedlineRpm), 0.0, 1.0); IntakeDirect = It.Direct;
        IntakePathDelayL = It.PathM > 0.0 ? uint32_t(ClampReal(RoundHalfUp(It.PathM / SpeedOfSound * Fs), 1.0, double(AcousticIntakePathSize - 1u))) : 0u;
        IntakePathDelayR = It.PathM > 0.0 ? uint32_t(ClampReal(RoundHalfUp((It.PathM + IntakeEarOffsetM) / SpeedOfSound * Fs), 1.0, double(AcousticIntakePathSize - 1u))) : 0u;
        BreathGain = It.Level * It.Breath * Load * ClampReal(Rpm / std::max(1.0, RedlineRpm), 0.0, 1.0);
    }
    else { IntakeGain = 0.0; BreathGain = 0.0; }
    // turbo air-rush band follows the spool
    if (S.Turbo.Enabled) { RushHz = S.Turbo.RushHz + Spool * S.Turbo.RushSpanHz; RushBP.Bandpass(Fs, RushHz, S.Turbo.RushQ); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   EVENTS (fireEvent)
//------------------------------------------------------------------------------------------------------------------------

void AcousticIntegrator::FireEvent(uint32_t Index, double Fraction, double Load, double DTheta) noexcept
{
    const AcousticStructure& S = Structure;
    const AcousticStructure::CombustionSheet& C = S.Combustion;
    const uint8_t Bank = EvBank[Index], Type = EvType[Index], Cyl = EvCyl[Index];
    const double Spread = 1.0 + C.IdleJitterBoost * (1.0 - Load);
    double JitterNext = 0.0;
    if (Type == EventFire)
    {
        double A = 1.0 + C.JitterAmp * Spread * Rng.Gauss(); if (A < 0.2) A = 0.2;
        if (C.CylinderSpreadPct > 0.0) A *= 1.0 + 0.01 * C.CylinderSpreadPct * Spread * SpreadOf[Cyl];   // imbalance grows toward idle like the jitter
        const double Misfire = Rng.Uniform() < C.MisfireProbability * (1.0 + 3.0 * (1.0 - Load)) ? 0.15 : 1.0;
        const double LiftLevel = S.Voice.LiftLevel + (1.0 - S.Voice.LiftLevel) * Load;
        VoiceActive[Cyl] = 1u; VoiceT[Cyl] = (1.0 - Fraction) * Dt; VoiceDeg[Cyl] = (1.0 - Fraction) * DTheta; VoiceAmp[Cyl] = A * Misfire * LiftLevel;
        ++FiringCount;
        // shared firing-time walk (rev 2); rev 3 sets jitter_pct 0 and uses timing_jitter_deg — a fresh draw per firing, never cumulative
        const double Interval = FiringInterval, Limit = 0.3 * Interval;
        const double Step = PureTone ? 0.0 : (Rng.Uniform() - 0.5) * C.JitterPct * 0.01 * Spread * Interval;
        FiringWalkDeg = ClampReal(FiringWalkDeg * (1.0 - WalkReversion) + Step, -Limit, Limit);
        EvAnchor[Index] += 720.0;
        EvJitter[Index] = (C.TimingJitterDeg > 0.0 && !PureTone) ? C.TimingJitterDeg * Rng.Gauss() : 0.0;
        for (uint32_t E = 0u; E < EventCount; ++E) if (EvType[E] == EventFire) EvNext[E] = EvAnchor[E] + FiringWalkDeg + EvJitter[E];
        return;
    }
    else if (Type == EventClatter)
    {
        const double Amp = S.Mechanical.ClatterLevel * (0.6 + 0.8 * Rng.Uniform());
        ClatterEnv[Bank] += Amp; ClatterImpulse[Bank] += 0.5 * Amp;
        JitterNext = 0.3 * Rng.Gauss();
    }
    else if (IntakeGain > 0.0)
    {
        // rev 3 intake voice: the suction pulse of this cylinder; cycle variance shared with the exhaust (jitter_amp)
        double A = 1.0 + 0.5 * C.JitterAmp * Spread * Rng.Gauss(); if (A < 0.3) A = 0.3;
        IntakeActive[Cyl] = 1u; IntakeDeg[Cyl] = (1.0 - Fraction) * DTheta; IntakeAmp[Cyl] = A;
    }
    EvAnchor[Index] += 720.0;
    EvNext[Index] = EvAnchor[Index] + JitterNext;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RENDER
//------------------------------------------------------------------------------------------------------------------------

void AcousticIntegrator::Integrate(float* Output, double* OutLeft, double* OutRight, uint32_t FrameCount) noexcept
{
    if (!Prepared) return;
    // slice entry: newest structure, listener, pure mode, demand (the worklet's message handler runs between process calls)
    {
        AcousticStructure Incoming;
        if (StructureRelay.Take(Incoming)) ApplyStructure(Incoming);
        const uint32_t WantListener = ListenerRequest.load(std::memory_order_relaxed);
        if (WantListener != ListenerApplied) { ListenerApplied = WantListener; ApplyListener(ListenerPreset(WantListener)); }
        PureTone = PureRequest.load(std::memory_order_relaxed);
        MeterInterval = MeterIntervalRequest.load(std::memory_order_relaxed);
        PowertrainRecord Record;
        if (Demand.Take(Record))
        {
            DemandRpm = ClampReal(Record.Rpm, 0.0, 20000.0); DemandThrottle = ClampReal(Record.Throttle, 0.0, 1.0);
            DemandLoad = ClampReal(Record.Load, 0.0, 1.0); DemandBoost = Record.Boost;
        }
    }

    const AcousticStructure& S = Structure;
    const AcousticStructure::CombustionSheet& C  = S.Combustion;
    const AcousticStructure::VoiceSheet&      Vo = S.Voice;
    const AcousticStructure::MixSheet&        M  = S.Mix;
    const AcousticStructure::TurboSheet&      Tb = S.Turbo;
    const AcousticStructure::MechanicalSheet& Me = S.Mechanical;
    const AcousticStructure::ExhaustSheet&    Ex = S.Exhaust;
    const double LocalFs = Fs, LocalDt = Dt;
    const bool Pure = PureTone;
    const double Idle = IdleRpm, Redline = RedlineRpm, InvRedline = 1.0 / std::max(1.0, Redline);
    const uint32_t N = CylinderCount;
    const uint8_t* Bank = BankOfCylinder;
    const double ThumpLevel = Vo.ThumpLevel, BodyLevel = Vo.BodyLevel, CamOrder = Vo.CamOrder;
    const double Width = ClampReal(Ex.BankPan * Listener.Width, 0.0, 1.0);
    const double CSame = 0.5 + 0.5 * Width, COther = 0.5 - 0.5 * Width;        // linear pan law
    const double ExGain = M.Exhaust * Listener.Exhaust, IndGain = M.Induction * Listener.Induction;
    const double MechGain = M.Mechanical * Listener.Mechanical, TransGain = M.Transients * Listener.Transients;
    const double OutGain = M.OutputGain;
    const bool Turbo = Tb.Enabled && !Pure;
    const bool BypassLP = Listener.CutoffHz >= LocalFs * 0.45;
    const double WhineStep = Me.WhineOrder / 60.0 * LocalDt;
    const double FA = Ex.FormantALevel, FB = Ex.FormantBLevel;
    const bool Formants = !Pure && (FA > 0.0 || FB > 0.0);
    const bool SplitPan = PanSplit;
    const double KScale = ExhaustKernelScale, IScale = IntakeKernelScale;
    const uint32_t Stride = Channels;

    for (uint32_t K = 0u; K < FrameCount; ++K)
    {
        // 1. demand smoothing (per sample → slice-size invariant)
        const double Delta = DemandRpm - RpmS;
        RpmS += RpmLag * Delta > MaxRpmStep ? MaxRpmStep : (RpmLag * Delta < -MaxRpmStep ? -MaxRpmStep : RpmLag * Delta);
        ThrottleS += ThrottleLag * (DemandThrottle - ThrottleS);
        LoadS += LoadLag * (DemandLoad - LoadS);
        const double Rpm = RpmS, Throttle = ThrottleS;
        const bool FuelOn = LoadS > 0.02;
        const double Load = FuelOn ? Throttle : 0.0;
        const double RpmNorm = ClampReal((Rpm - Idle) / std::max(1.0, Redline - Idle), 0.0, 1.0);

        // 2. per-slice constants, sample-counted
        if ((SampleIndex % RebuildSamples) == 0u) RebuildSheet(Rpm, Load);
        const double LocalThumpHz = ThumpHz, LocalDecayRate = DecayRate, LocalNoiseRate = NoiseRate;
        const double LocalCrackle = Pure ? 0.0 : CrackleGain, LocalRing = Pure ? 0.0 : RingGain, LocalRasp = Pure ? 0.0 : RaspGain, LocalHowl = Pure ? 0.0 : HowlGain;
        const double LocalIntakeGain = Pure ? 0.0 : IntakeGain;

        // 3. crank clock + cylinder events at exact sub-sample times
        const double ThetaPrev = Theta;
        const double DTheta = 6.0 * Rpm * LocalDt;
        Theta = ThetaPrev + DTheta;
        const double Cycle = std::floor(Theta / 720.0);
        bool CycleStarted = false;
        if (Cycle != CycleIndex) { CycleIndex = Cycle; CycleStarted = true; }
        for (uint32_t E = 0u; E < EventCount; ++E)
        {
            while (Theta >= EvNext[E])
            {
                const double Fraction = DTheta > 0.0 ? ClampReal((EvNext[E] - ThetaPrev) / DTheta, 0.0, 0.999999) : 0.0;
                FireEvent(E, Fraction, Load, DTheta);
            }
        }
        const double CamPhase = Fmod1(Theta / 720.0);
        const double CamMod = (1.0 - CamDepth) + CamDepth * std::sin(CamOrder * TwoPi * CamPhase);

        // 4. per-cylinder voices
        double SumL = 0.0, SumR = 0.0, PulseSum = 0.0, HowlSum = 0.0, Bank0 = 0.0, Bank1 = 0.0;
        for (uint32_t Cyl = 1u; Cyl <= N; ++Cyl)
        {
            if (!VoiceActive[Cyl]) continue;
            const double T = VoiceT[Cyl];
            const double Amp = VoiceAmp[Cyl];
            double Pulse, Body = 0.0, Env;
            if (CrankPulse)
            {
                const double D = VoiceDeg[Cyl];
                if (D >= ExhaustKernelDeg) { VoiceActive[Cyl] = 0u; continue; }
                const double Pos = D * KScale; const uint32_t I0 = uint32_t(Pos); const double Fr = Pos - double(I0); const uint32_t I1 = I0 + 1u < AcousticKernelSize ? I0 + 1u : I0;
                const double Hard = ExhaustKernelHard[I0] + (ExhaustKernelHard[I1] - ExhaustKernelHard[I0]) * Fr, Soft = ExhaustKernelSoft[I0] + (ExhaustKernelSoft[I1] - ExhaustKernelSoft[I0]) * Fr;
                Pulse = (Soft + (Hard - Soft) * Load) * ThumpLevel * Amp;
                Env = std::exp(-T * LocalDecayRate);
                VoiceDeg[Cyl] = D + DTheta;
            }
            else
            {
                Env = std::exp(-T * LocalDecayRate);
                if (Env < 0.001) { VoiceActive[Cyl] = 0u; continue; }
                const double Phase = Fmod1(LocalThumpHz * T);
                const double Pos = Phase * double(AcousticSheetSize); const uint32_t I0 = uint32_t(Pos); const double Fr = Pos - double(I0); const uint32_t I1 = (I0 + 1u) & (AcousticSheetSize - 1u);
                Pulse = (PulseSheet[I0] + (PulseSheet[I1] - PulseSheet[I0]) * Fr) * Env * ThumpLevel * Amp;
                Body = (BodySheet[I0] + (BodySheet[I1] - BodySheet[I0]) * Fr) * Env * BodyLevel * Amp;
            }
            double Extra = 0.0;
            if (LocalCrackle > 0.0 || LocalRing > 0.0)
            {
                const double NoiseEnv = std::exp(-T * LocalNoiseRate);
                if (LocalCrackle > 0.0) Extra += Rng.Signed() * NoiseEnv * LocalCrackle;
                if (LocalRing > 0.0) Extra += std::sin(TwoPi * RingHz[Cyl] * T) * NoiseEnv * LocalRing;
            }
            if (LocalRasp > 0.0) Extra += std::sin(TwoPi * LocalThumpHz * 3.0 * T) * std::sin(TwoPi * LocalThumpHz * 0.5 * T) * Env * LocalRasp;
            if (LocalHowl > 0.0)
            {
                const double Howl = (std::sin(TwoPi * LocalThumpHz * T) * 0.6 + std::sin(TwoPi * LocalThumpHz * 2.0 * T) * 0.4 + Rng.Signed() * 0.2) * Env * LocalHowl;
                Extra += Howl; HowlSum += Howl;
            }
            const double Sample = (Pulse + Body + Extra) * CamMod;
            PulseSum += Pulse;
            if (SplitPan) { if (Bank[Cyl]) Bank1 += Sample; else Bank0 += Sample; }
            else if (Bank[Cyl]) { SumL += Sample * COther; SumR += Sample * CSame; }
            else { SumL += Sample * CSame; SumR += Sample * COther; }
            VoiceT[Cyl] = T + LocalDt;
        }
        if (SplitPan)
        {
            const double Low0 = BankLow[0].Advance(Bank0), Low1 = BankLow[1].Advance(Bank1), High0 = -BankHigh[0].Advance(Bank0), High1 = -BankHigh[1].Advance(Bank1), Shared = 0.5 * (Low0 + Low1);
            SumL = Shared + High0 * CSame + High1 * COther; SumR = Shared + High0 * COther + High1 * CSame;
        }

        // 4b. rev 3 intake voice: suction pulses (crank degrees) + breath noise through the runner resonator (f, 3f) → induction bus
        double IntakeOut = 0.0, IntakeOutR = 0.0;
        bool IntakeSplit = false;   // true when the right channel carries its own delayed tap (the JS `intakeOutR === intakeOut` test)
        if (LocalIntakeGain > 0.0)
        {
            double Raw = 0.0;
            for (uint32_t Cyl = 1u; Cyl <= N; ++Cyl)
            {
                if (!IntakeActive[Cyl]) continue;
                const double D = IntakeDeg[Cyl];
                if (D >= IntakeKernelDeg) { IntakeActive[Cyl] = 0u; continue; }
                const double Pos = D * IScale; const uint32_t I0 = uint32_t(Pos); const double Fr = Pos - double(I0); const uint32_t I1 = I0 + 1u < AcousticKernelSize ? I0 + 1u : I0;
                Raw += (IntakeKernel[I0] + (IntakeKernel[I1] - IntakeKernel[I0]) * Fr) * IntakeAmp[Cyl];
                IntakeDeg[Cyl] = D + DTheta;
            }
            Raw = Raw * LocalIntakeGain + Rng.Signed() * BreathGain;
            IntakeOut = IntakeRes[0].Advance(Raw) + 0.5 * IntakeRes[1].Advance(Raw) + IntakeDirect * IntakeMouth.Advance(Raw);
            if (IntakePathDelayL > 0u)
            {
                const uint32_t Head = IntakePathHead, Mask = AcousticIntakePathSize - 1u;
                IntakePath[Head] = IntakeOut;
                IntakeOut = IntakePath[(Head - IntakePathDelayL) & Mask]; IntakeOutR = IntakePath[(Head - IntakePathDelayR) & Mask];
                IntakePathHead = (Head + 1u) & Mask;
                IntakeSplit = IntakeOutR != IntakeOut;
            }
            else IntakeOutR = IntakeOut;
        }

        // 5. throttle-edge transients: backfire thud, anti-lag pops / blow-off (turbo), overrun crackle
        double Trans = 0.0;
        if (!Pure)
        {
            if (Throttle > 0.6) LiftArmedBackfire = true;
            if (Throttle > 0.7) LiftArmedTurbo = true;
            if (Throttle < 0.1)
            {
                if (LiftArmedBackfire)
                {
                    LiftArmedBackfire = false;
                    if (Rpm > Redline * 0.6 && C.BackfireLevel > 0.0 && Rng.Uniform() < C.BackfireProbability)
                        Transients.Spawn(TransientCategory::Backfire, 0.0, 0.2, 100.0, 40.0, C.BackfireLevel, 0.005, 0.5 * C.BackfireLevel, 2000.0);
                }
                if (LiftArmedTurbo)
                {
                    LiftArmedTurbo = false;
                    if (Turbo && Rpm > 3000.0)
                    {
                        if (Tb.AntilagLevel > 0.0 && Rpm > Tb.AntilagRpm)
                        {
                            const uint32_t Pops = 3u + uint32_t(std::floor(Rng.Uniform() * 3.0)); double At = 0.0;
                            for (uint32_t P = 0u; P < Pops; ++P)
                            {
                                const double F0 = 80.0 + Rng.Uniform() * 40.0;   // drawn before the spawn, as the JS argument list evaluates it
                                Transients.Spawn(TransientCategory::Antilag, At, 0.1, F0, 30.0, Tb.AntilagLevel, 0.005, 0.0, 3000.0);
                                At += 0.05 + Rng.Uniform() * 0.03;
                            }
                            PopCount += Pops;
                        }
                        else if (Tb.BlowoffLevel > 0.0) Transients.Spawn(TransientCategory::Blowoff, 0.0, 0.8, 3000.0, 1000.0, Tb.BlowoffLevel, 0.02, 0.0, 20000.0);
                    }
                }
            }
            const bool Closed = Throttle < 0.05, Fast = Rpm > Idle * 1.4;
            if (Closed && Fast) { if (!OverrunActive) { OverrunActive = true; OverrunEnv = 1.0; } }
            else { OverrunActive = false; OverrunEnv = 0.0; }
            OverrunEnv *= PopDecay;
            PopBoost *= PopBoostDecay;
            if (OverrunEnv > 0.001 && C.PopLevel > 0.0)
            {
                const double Rate = C.PopRateHz * OverrunEnv * (0.3 + 0.7 * RpmNorm) * (1.0 + 4.0 * PopBoost);
                if (Rng.Uniform() < Rate * LocalDt)
                {
                    const double U = Rng.Uniform();
                    // the JS argument list draws duration then frequency, left to right
                    const double Dur = 0.05 + 0.07 * Rng.Uniform();
                    const double F0 = 90.0 + 60.0 * Rng.Uniform();
                    Transients.Spawn(TransientCategory::Pop, 0.0, Dur, F0, 45.0, C.PopLevel * (0.3 + 1.5 * U * U) * (0.7 + 0.3 * OverrunEnv), 0.003, 0.35 * C.PopLevel, 2500.0);
                    PopBoost = 1.0; ++PopCount;
                }
            }
            Trans = Transients.Advance(Rng);
        }

        // 6. turbo: spool follows rpm / redline · load; whine + air rush
        double TurboOut = 0.0;
        if (Turbo)
        {
            const double Target = ClampReal(Rpm * InvRedline, 0.0, 1.0) * Load;
            Spool += (Target > Spool ? SpoolUp : SpoolDown) * (Target - Spool);
            Boost = Tb.BoostMaxBar * Spool;
            const double Hz = Tb.WhineHz + Spool * Tb.WhineSpanHz;
            TurboPhase += Hz * LocalDt; if (TurboPhase >= 1.0) TurboPhase -= 1.0;
            TurboOut = std::sin(TwoPi * TurboPhase) * Tb.WhineLevel * Spool + RushBP.Advance(Rng.Signed()) * Tb.RushLevel * Spool;
        }
        else { Spool = 0.0; Boost = 0.0; }

        // 7. mechanical: valve clatter (crank-locked), gear whine, hybrid motor
        double Mech = 0.0;
        if (!Pure)
        {
            if (Me.ClatterLevel > 0.0)
            {
                ClatterEnv[0] *= ClatterDecay; ClatterEnv[1] *= ClatterDecay;
                const double ClatterNoise = (ClatterEnv[0] + ClatterEnv[1]) * Rng.Signed();
                Mech = ClatterBP1.Advance(ClatterNoise) + 0.6 * ClatterBP2.Advance(ClatterNoise) + 0.5 * ClatterRing.Advance(ClatterImpulse[0] + ClatterImpulse[1]);
                ClatterImpulse[0] = 0.0; ClatterImpulse[1] = 0.0;
            }
            if (Me.WhineLevel > 0.0 && Me.WhineOrder > 0.0)
            {
                WhinePhase += WhineStep * Rpm; if (WhinePhase >= 1.0) WhinePhase -= 1.0;
                const double Hz = Me.WhineOrder * Rpm / 60.0, Fade = ClampReal((0.4 * LocalFs - Hz) / (0.05 * LocalFs), 0.0, 1.0);
                Mech += Me.WhineLevel * Rpm * InvRedline * Fade * std::sin(TwoPi * WhinePhase);
            }
            if (Me.MotorLevel > 0.0)
            {
                MotorPhase += (Me.MotorHz + Me.MotorLoadHz * Load) * LocalDt; if (MotorPhase >= 1.0) MotorPhase -= 1.0;
                Mech += Me.MotorLevel * Load * std::sin(TwoPi * MotorPhase);
            }
        }

        // 8. mix: per-channel tanh → [formants, silencer, bypass gain] → buses → output gain → comb → valve high-pass → ±1 clip → listener low-pass
        double L, R;
        if (Pure) { L = SumL * ExGain * OutGain; R = SumR * ExGain * OutGain; }
        else
        {
            const double Drive = VoiceDrive;
            double VL = std::tanh(SumL * Drive) * ExGain, VR = std::tanh(SumR * Drive) * ExGain;
            if (Formants)
            {
                if (FA > 0.0) { VL += FA * FormantA[0].Advance(VL); VR += FA * FormantA[1].Advance(VR); }
                if (FB > 0.0) { VL += FB * FormantB[0].Advance(VL); VR += FB * FormantB[1].Advance(VR); }
            }
            if (!SilencerBypass) { VL = Silencer[0][1].Advance(Silencer[0][0].Advance(VL)); VR = Silencer[1][1].Advance(Silencer[1][0].Advance(VR)); }
            if (ValveGain != 1.0) { VL *= ValveGain; VR *= ValveGain; }
            const double Centre = (TurboOut + IntakeOut) * IndGain + Mech * MechGain + Trans * TransGain;
            L = (VL + Centre) * OutGain;
            R = (VR + (!IntakeSplit ? Centre : (TurboOut + IntakeOutR) * IndGain + Mech * MechGain + Trans * TransGain)) * OutGain;
            L = Comb[0].Advance(L); R = Comb[1].Advance(R);
            L = ValveHP[0].Advance(L); R = ValveHP[1].Advance(R);
            if (L > 1.0) { L = 1.0; ++ClippedCount; } else if (L < -1.0) { L = -1.0; ++ClippedCount; }
            if (R > 1.0) R = 1.0; else if (R < -1.0) R = -1.0;
            if (!BypassLP) { L = ListenerLP[0].Advance(L); R = ListenerLP[1].Advance(R); }
            Meter[2] += VL * VL; Meter[3] += VR * VR;
        }
        if (Output) { Output[K * Stride] = float(L); if (Stride > 1u) Output[K * Stride + 1u] = float(R); }
        else { OutLeft[K] = L; if (OutRight) OutRight[K] = R; }

        // 9. meters + crank-locked scope capture
        Meter[0] += SumL * SumL; Meter[1] += SumR * SumR; Meter[4] += HowlSum * HowlSum; Meter[5] += Mech * Mech;
        Meter[6] += Trans * Trans; Meter[7] += TurboOut * TurboOut; Meter[8] += L * L; Meter[9] += R * R; Meter[11] += IntakeOut * IntakeOut;
        if (ScopeCapturing)
        {
            Scope.Head[ScopeFill] = float(PulseSum); Scope.Output[ScopeFill++] = float(L);
            if (ScopeFill >= ScopeCycleSamples || ScopeFill >= AcousticScopeCapacity) { ScopeCapturing = false; Scope.Count = ScopeFill; ScopeReady.store(true, std::memory_order_release); }
        }
        else if (CycleStarted && ScopeArmed.load(std::memory_order_relaxed) && !ScopeReady.load(std::memory_order_relaxed))
        {
            ScopeCycleSamples = uint32_t(std::min(double(AcousticScopeCapacity), std::ceil(720.0 / std::max(1.0e-6, DTheta))));
            ScopeFill = 0u; ScopeCapturing = true; Scope.Head[ScopeFill] = float(PulseSum); Scope.Output[ScopeFill++] = float(L);
        }
        ++SampleIndex;
        ++MeterFrames;
        if (MeterFrames >= MeterInterval)
        {
            AcousticMeterRecord Rec; Rec.Frames = MeterFrames;
            for (uint32_t I = 0u; I < AcousticMeterCount; ++I) { Rec.Level[I] = std::sqrt(Meter[I] / double(MeterFrames)); Meter[I] = 0.0; }
            MeterFrames = 0u;
            Meters.Publish(Rec);
        }
    }
    PublishSliceEnd(FrameCount);
}

void AcousticIntegrator::PublishSliceEnd(uint32_t) noexcept
{
    AcousticReadout R;
    R.Rpm = RpmS; R.Throttle = ThrottleS; R.Load = LoadS; R.Boost = Boost; R.Spool = Spool;
    R.ValveHz = ValveHz; R.SilencerHz = SilencerHz; R.IntakeHz = IntakeHz; R.ValveGain = ValveGain; R.ThumpHz = ThumpHz;
    R.Theta = Theta; R.Firings = FiringCount; R.SampleIndex = SampleIndex;
    R.Pops = PopCount; R.Clipped = ClippedCount; R.Dropped = Transients.QueryDropped(); R.Spawned = Transients.QuerySpawned();
    R.Cylinders = CylinderCount; R.CrankPulse = CrankPulse; R.FuelOn = LoadS > 0.02;
    Readout.Publish(R);
}

bool AcousticIntegrator::TakeScope(AcousticScopeCapture& Out) noexcept
{
    if (!ScopeReady.load(std::memory_order_acquire)) return false;
    // the realtime side stopped writing Scope when it raised ScopeReady and will not start again until ScopeArmed is set anew
    Out.Count = Scope.Count;
    std::memcpy(Out.Output, Scope.Output, sizeof(float) * Scope.Count);
    std::memcpy(Out.Head, Scope.Head, sizeof(float) * Scope.Count);
    ScopeArmed.store(false, std::memory_order_relaxed);
    ScopeReady.store(false, std::memory_order_release);
    return true;
}

} // namespace Frontier
