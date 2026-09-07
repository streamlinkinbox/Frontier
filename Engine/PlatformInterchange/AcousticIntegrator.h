//============================================================================================================================================
//                                                      ACOUSTICINTEGRATOR.H
//============================================================================================================================================
// 🧩 The vehicle voice: a SignalIntegrator that turns a PowertrainRecord stream into stereo engine sound from an
//    AcousticStructure. It is the one-for-one C++ twin of `AcousticIntegrator` in the AudioEditor's shared DSP text
//    (Tools/AudioEditor/index.html, <script id="dsp">): the same crank clock, the same per-cylinder voices (rev-2 thump +
//    body sheets, rev-3 crank-degree kernels + intake voice), the same transients, turbo, mechanical layer and post chain,
//    in the same statement order, in double, with the same xorshift32 draws — so the same TOML, the same pull and the same
//    seed give the same samples here, in the page and in the worklet (Scratchpad/AcousticIdentityTest: max |Δ| ≤ 1 × 10⁻⁶).
//
//    Threads
//        main       AssignStructure / AssignDemand / AssignListener / AssignPureTone / ArmScope publish through wait-free
//                   relays or relaxed atomics; TakeMeters / TakeScope / QueryReadout read what the realtime side published.
//                   Prepare runs here too (AudioExchange::Attach, RenderOffline) before the first Render.
//        realtime   Render: takes the newest structure and demand at slice entry (the worklet does the same between
//                   process calls), integrates per sample, publishes the readout and, on cadence, the meters at slice end.
//                   No locks, no allocation, no I/O. Slice length is irrelevant to the output (per-sample smoothing,
//                   rebuilds counted in samples since Prepare) — 1 / 37 / 64 / 256-frame slices are byte-identical.
//
//    Determinism: Prepare reseeds the generator (AssignSeed, default 0x5EED1234) and zeroes every state, so two renders of
//    the same demand sequence are identical — the A/B contract the editor, the dyno's --render and the proofs rely on.
//    A game that wants variety seeds from its clock before Attach.

#pragma once

#include "../DeviceExchange/RelayQueue.h"
#include "AcousticStructure.h"
#include "AudioExchange.h"
#include "PowertrainRecord.h"
#include "SignalSections.h"
#include "TransientSlots.h"

#include <atomic>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   SIZES AND PRESETS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t AcousticMaxCylinders     = 16u;      // [-]        voice slots (cylinder numbers 1 … 16)
constexpr uint32_t AcousticKernelSize       = 2048u;    // [samples]  rev-3 crank-degree kernels (exhaust hard / soft, intake)
constexpr uint32_t AcousticSheetSize        = 512u;     // [samples]  rev-2 pulse / body sheets (RevSim: 512-point lookups)
constexpr uint32_t AcousticHarmonicCount    = 24u;      // [-]        body sheet harmonics
constexpr uint32_t AcousticChargerHarmonics = 8u;       // [-]        supercharger rotor-pulsation harmonics (charger.whine_harmonics, row A2½)
constexpr uint32_t AcousticEventsPerCylinder = 4u;      // [-]        firing + two valve-seating clatters + intake opening
constexpr uint32_t AcousticMeterCount       = 12u;      // [-]        0/1 voice L/R · 2/3 exhaust bus L/R · 4 howl · 5 mechanical · 6 transients · 7 turbo · 8/9 out L/R · 10 supercharger · 11 intake
constexpr uint32_t AcousticScopeCapacity    = 16384u;   // [samples]  one crank-locked 720° cycle of output + head pulses
constexpr uint32_t AcousticIntakePathSize   = 1024u;    // [samples]  intake path ring (≈ 7.3 m at 48 kHz)
constexpr uint32_t AcousticTransientCount   = 32u;      // [-]        one-shot slots
constexpr uint32_t AcousticCombCapacity     = 2048u;    // [samples]  feedback comb ring
constexpr uint32_t AcousticDefaultSeed      = 0x5EED1234u;

enum class ListenerPreset : uint32_t
{
    Trackside = 0,   // exhaust 1.0 · induction 1.0 · mechanical 1.0 · transients 1.0 · width 1.0 · no low-pass
    Chase     = 1,   // exhaust 1.0 · induction 0.6 · mechanical 0.5 · transients 1.0 · width 0.5 · 7 kHz
    Cockpit   = 2    // exhaust 0.45 · induction 1.2 · mechanical 1.2 · transients 0.6 · width 0.3 · 3.5 kHz
};

struct ListenerWeights
{
    double Exhaust;      // [-]
    double Induction;    // [-]
    double Mechanical;   // [-]
    double Transients;   // [-]
    double Width;        // [-]  multiplies exhaust.bank_pan
    double CutoffHz;     // [Hz] listener low-pass; ≥ 0.45 × Fs = bypassed
};

[[nodiscard]] const ListenerWeights& QueryListenerWeights(ListenerPreset Preset) noexcept;
[[nodiscard]] const char*            QueryListenerName(ListenerPreset Preset) noexcept;
[[nodiscard]] bool                   ParseListenerPreset(const char* Name, ListenerPreset& Out) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                   REALTIME → MAIN RECORDS
//------------------------------------------------------------------------------------------------------------------------

// Published at the end of every Render slice (latest wins).
struct AcousticReadout
{
    double   Rpm          = 0.0;    // [rpm] smoothed crank speed the voice is playing
    double   Throttle     = 0.0;    // [-]
    double   Load         = 0.0;    // [-]   smoothed demand load (fuel on when > 0.02)
    double   Boost        = 0.0;    // [bar] turbo layer
    double   Spool        = 0.0;    // [-]
    double   ValveHz      = 0.0;    // [Hz]  exhaust valve high-pass corner (lagged)
    double   SilencerHz   = 0.0;    // [Hz]  silencer low-pass corner (lagged)
    double   IntakeHz     = 0.0;    // [Hz]  intake tract frequency (lagged)
    double   ValveGain    = 1.0;    // [-]
    double   ThumpHz      = 0.0;    // [Hz]  rev-2 thump pitch
    double   Theta        = 0.0;    // [°]   crank angle since Prepare
    uint64_t Firings      = 0u;     // [-]
    uint64_t SampleIndex  = 0u;     // [-]   samples rendered since Prepare
    uint32_t Pops         = 0u;     // [-]
    uint32_t Clipped      = 0u;     // [-]   left-channel samples that hit ±1
    uint32_t Dropped      = 0u;     // [-]   transient spawns that found no free slot
    uint32_t Spawned      = 0u;     // [-]
    uint32_t Cylinders    = 0u;     // [-]
    bool     CrankPulse   = false;  // [-]   rev-3 voice active
    bool     FuelOn       = false;  // [-]
};

// RMS of each meter bus over the frames since the previous record (published every MeterInterval samples).
struct AcousticMeterRecord
{
    double   Level[AcousticMeterCount] = {};   // [-]  RMS
    uint64_t Frames = 0u;                      // [-]  frames the RMS covers
};

// One crank-locked 720° cycle: Output = the left channel, Head = the summed cylinder pressure pulses (the scope's trigger trace).
struct AcousticScopeCapture
{
    uint32_t Count = 0u;                       // [samples]
    float    Output[AcousticScopeCapacity];
    float    Head[AcousticScopeCapacity];
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ACOUSTIC INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class AcousticIntegrator final : public SignalIntegrator
{
public:
    explicit AcousticIntegrator(const AcousticStructure& Initial = AcousticStructure{}, uint32_t Seed = AcousticDefaultSeed) noexcept;

    //──────────────────────────────────────────────────────────────────────────
    // Main thread
    //──────────────────────────────────────────────────────────────────────────

    // Publishes a new structure; the realtime side applies it at the next slice entry (the editor's inspector path).
    //    Before Prepare it simply becomes the structure Prepare starts from.
    void AssignStructure(const AcousticStructure& Structure) noexcept;

    // Publishes the latest demand (wait-free triple-slot relay, latest wins).
    void AssignDemand(const PowertrainRecord& Record) noexcept { Demand.Publish(Record); }

    void AssignListener(ListenerPreset Preset) noexcept   { ListenerRequest.store(uint32_t(Preset), std::memory_order_relaxed); }
    void AssignPureTone(bool Enabled) noexcept            { PureRequest.store(Enabled, std::memory_order_relaxed); }
    void AssignSeed(uint32_t Seed) noexcept               { SeedRequest = Seed; }                 // takes effect at the next Prepare
    void AssignMeterInterval(uint32_t Samples) noexcept   { MeterIntervalRequest.store(Samples == 0u ? 1u : Samples, std::memory_order_relaxed); }

    // Arms one crank-locked scope capture (starts at the next 720° boundary); TakeScope reads it and disarms.
    void ArmScope() noexcept                              { ScopeArmed.store(true, std::memory_order_release); }
    bool TakeScope(AcousticScopeCapture& Out) noexcept;

    // Newest meter record (true when one arrived since the last call) and the newest readout.
    bool TakeMeters(AcousticMeterRecord& Out) noexcept    { return Meters.Take(Out); }
    [[nodiscard]] AcousticReadout QueryReadout() noexcept { AcousticReadout R; if (!Readout.Take(R)) R = Readout.Latest(); return R; }

    [[nodiscard]] const AcousticStructure& QueryStructure() const noexcept { return Pending; }   // what the main thread last assigned
    [[nodiscard]] uint32_t                 QuerySampleRate() const noexcept { return Rate; }

    //──────────────────────────────────────────────────────────────────────────
    // SignalIntegrator
    //──────────────────────────────────────────────────────────────────────────

    // Sizes everything for SampleRate, zeroes all state, reseeds, applies the pending structure. Main thread, before Render.
    void Prepare(uint32_t SampleRate, uint32_t ChannelCount) noexcept override;

    // Realtime: FrameCount × ChannelCount interleaved floats (channel 0 = left, 1 = right, any further channels stay silent).
    void Render(float* Output, uint32_t FrameCount) noexcept override { Integrate(Output, nullptr, nullptr, FrameCount); }

    // The same samples before the float cast — separate L / R double extents (the editor's render(outL, outR, frames)).
    //    Proofs and the reference-dump comparison use it; the transport never does.
    void RenderDouble(double* Left, double* Right, uint32_t FrameCount) noexcept { Integrate(nullptr, Left, Right, FrameCount); }

private:
    //──────────────────────────────────────────────────────────────────────────
    // Realtime-side procedures (the JavaScript methods of the same names)
    //──────────────────────────────────────────────────────────────────────────

    void   ApplyStructure(const AcousticStructure& S) noexcept;           // assignStructure
    void   ApplyListener(ListenerPreset Preset) noexcept;                 // assignListener
    void   BuildKernels() noexcept;                                       // buildKernels
    double FinishKernel(double* Kernel, double SpanDeg) noexcept;         // finishKernel (uses the two scratch kernels below)
    double IntakeTarget(double Rpm) const noexcept;                       // intakeTarget
    void   RebuildSheet(double Rpm, double Load) noexcept;                // rebuildSheet
    void   FireEvent(uint32_t Index, double Fraction, double Load, double DTheta) noexcept;   // fireEvent
    void   PublishSliceEnd(uint32_t Frames) noexcept;
    void   Integrate(float* Interleaved, double* Left, double* Right, uint32_t FrameCount) noexcept;   // the sample loop behind both Render entry points

    static void FiringAngles(uint32_t N, const int32_t* Order, const uint8_t* BankOfCylinder, bool EvenFiring, double BankAngleDeg, double* Angle) noexcept;

    //──────────────────────────────────────────────────────────────────────────
    // Main ↔ realtime seams
    //──────────────────────────────────────────────────────────────────────────

    AcousticStructure                Pending;                    // main-thread copy of the last assigned structure
    RelayQueue<AcousticStructure>    StructureRelay;             // main → realtime
    RelayQueue<PowertrainRecord>     Demand;                     // main → realtime
    RelayQueue<AcousticReadout>      Readout;                    // realtime → main, every slice
    RelayQueue<AcousticMeterRecord>  Meters;                     // realtime → main, every MeterInterval samples
    std::atomic<uint32_t>            ListenerRequest { 0u };
    std::atomic<bool>                PureRequest     { false };
    std::atomic<uint32_t>            MeterIntervalRequest { 2048u };
    std::atomic<bool>                ScopeArmed      { true };   // the editor arms on start
    std::atomic<bool>                ScopeReady      { false };
    uint32_t                         SeedRequest;                // main thread; read by Prepare
    bool                             Prepared = false;

    //──────────────────────────────────────────────────────────────────────────
    // Realtime state — every member below is touched by Render only (and by Prepare before the first Render)
    //──────────────────────────────────────────────────────────────────────────

    uint32_t   Rate = 48000u;                       // [Hz]
    uint32_t   Channels = 2u;                       // [-]
    double     Fs = 48000.0, Dt = 1.0 / 48000.0;    // [Hz] [s]
    Xorshift   Rng;
    AcousticStructure Structure;                    // realtime copy

    // demand smoothing
    double DemandRpm = 900.0, DemandThrottle = 0.0, DemandLoad = 0.0, DemandBoost = 0.0;
    double RpmS = 900.0, ThrottleS = 0.0, LoadS = 0.0;
    double RpmLag = 1.0, ThrottleLag = 1.0, LoadLag = 1.0, MaxRpmStep = 1.0;

    // crank clock + events
    double   Theta = 0.0, CycleIndex = 0.0, FiringWalkDeg = 0.0;
    uint64_t SampleIndex = 0u;
    uint32_t CylinderCount = 0u, EventCount = 0u;
    bool     EvenFiring = true, CrankPulse = false, PanSplit = false;
    double   IdleRpm = 900.0, RedlineRpm = 8000.0, FiringInterval = 90.0;
    uint8_t  BankOfCylinder[AcousticMaxCylinders + 1u] = {};
    double   FiringAngle[AcousticMaxCylinders + 1u] = {};
    double   SpreadOf[AcousticMaxCylinders + 1u] = {};
    double   RingHz[AcousticMaxCylinders + 1u] = {};
    double   SignatureAngle[AcousticMaxCylinders + 1u] = {};   // the JS angleSignature: firing angles + evo_deg of the last event rebuild
    double   SignatureEvo = 0.0;
    bool     SignatureValid = false;
    uint8_t  EvType[AcousticMaxCylinders * AcousticEventsPerCylinder] = {};
    uint8_t  EvBank[AcousticMaxCylinders * AcousticEventsPerCylinder] = {};
    uint8_t  EvCyl[AcousticMaxCylinders * AcousticEventsPerCylinder] = {};
    double   EvAnchor[AcousticMaxCylinders * AcousticEventsPerCylinder] = {};
    double   EvNext[AcousticMaxCylinders * AcousticEventsPerCylinder] = {};
    double   EvJitter[AcousticMaxCylinders * AcousticEventsPerCylinder] = {};

    // per-cylinder voices
    uint8_t  VoiceActive[AcousticMaxCylinders + 1u] = {};
    double   VoiceT[AcousticMaxCylinders + 1u] = {}, VoiceDeg[AcousticMaxCylinders + 1u] = {}, VoiceAmp[AcousticMaxCylinders + 1u] = {};
    uint8_t  IntakeActive[AcousticMaxCylinders + 1u] = {};
    double   IntakeDeg[AcousticMaxCylinders + 1u] = {}, IntakeAmp[AcousticMaxCylinders + 1u] = {};
    double   PulseSheet[AcousticSheetSize] = {}, BodySheet[AcousticSheetSize] = {};
    double   ExhaustKernelHard[AcousticKernelSize] = {}, ExhaustKernelSoft[AcousticKernelSize] = {}, IntakeKernel[AcousticKernelSize] = {};
    double   KernelScratchA[AcousticKernelSize] = {}, KernelScratchB[AcousticKernelSize] = {};   // FinishKernel's discharge / source copies (no stack, no allocation)
    double   ExhaustKernelScale = 1.0, IntakeKernelScale = 1.0;
    double   HarmonicAmps[AcousticHarmonicCount] = {};
    double   SineSheet[AcousticHarmonicCount][AcousticSheetSize] = {};   // sin(2π (h + 2) i / 512) — the body sheet's sines, computed once in Prepare with the editor's expression

    // per-slice constants (rebuildSheet)
    double ThumpHz = 65.0, DecayRate = 15.0, NoiseRate = 40.0, CrackleGain = 0.1, RingGain = 0.1, RaspGain = 0.0, HowlGain = 0.0, CamDepth = 0.0, VoiceDrive = 1.0;
    double IntakeGain = 0.0, BreathGain = 0.0, IntakeHz = 220.0, IntakeLag = 1.0, IntakeDirect = 0.0;
    BiquadSection IntakeRes[2], IntakeMouth;
    double   IntakePath[AcousticIntakePathSize] = {};
    uint32_t IntakePathHead = 0u, IntakePathDelayL = 0u, IntakePathDelayR = 0u;
    BiquadSection BankLow[2], BankHigh[2];

    // mechanical / turbo / transients
    double ClatterEnv[2] = {}, ClatterImpulse[2] = {}, ClatterDecay = 0.0;
    BiquadSection ClatterBP1, ClatterBP2, ClatterRing;
    double WhinePhase = 0.0, MotorPhase = 0.0, TurboPhase = 0.0, Spool = 0.0, Boost = 0.0, RushHz = 2000.0;
    BiquadSection RushBP;
    double   ChargerPhase = 0.0, ChargerAmps[AcousticChargerHarmonics] = {}, BarkClock = 0.0;   // row A2½: supercharger pulsation + intake bark arming
    uint32_t ChargerCount = 0u;
    bool     BarkArmed = false;
    TransientSlots<AcousticTransientCount> Transients;
    bool   LiftArmedBackfire = false, LiftArmedTurbo = false, OverrunActive = false;
    double OverrunEnv = 0.0, PopBoost = 0.0, PopBoostDecay = 0.0, PopDecay = 0.0, SpoolUp = 1.0, SpoolDown = 1.0;

    // post chain
    BiquadSection FormantA[2], FormantB[2], Silencer[2][2], ValveHP[2];
    double SilencerHz = 24000.0, ValveGain = 1.0, ValveHz = 10.0, ValveLag = 1.0;
    bool   SilencerBypass = true, PureTone = false;
    CombLine<AcousticCombCapacity> Comb[2];
    OnePole ListenerLP[2];
    ListenerWeights Listener;
    uint32_t ListenerApplied = 0u;

    // counters, meters, scope
    uint64_t FiringCount = 0u;
    uint32_t PopCount = 0u, ClippedCount = 0u;
    double   Meter[AcousticMeterCount] = {};
    uint64_t MeterFrames = 0u;
    uint32_t MeterInterval = 2048u;
    AcousticScopeCapture Scope;
    uint32_t ScopeFill = 0u, ScopeCycleSamples = 0u;
    bool     ScopeCapturing = false;
};

} // namespace Frontier
