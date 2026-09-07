//============================================================================================================================================
//                                                       ACOUSTICSTRUCTURE.H
//============================================================================================================================================
// 🧩 The acoustic structure of one vehicle: the 111 tunable numbers in 8 sections that the AudioEditor's inspector edits, the
//    car archives (EngineContent/AudioArchives/<Car>/<Car>.toml) carry, and AcousticIntegrator plays. One field sheet
//    (AcousticStructure::Fields) drives every consumer — defaults, TOML Load with per-field fallback, TOML Save with the
//    unit comments, Describe for an inspector — so the loader, the writer, the editor and the voice can never disagree on a
//    field. The sheet is the C++ twin of `ACOUSTIC_SCHEMA` in Tools/AudioEditor/index.html; the row count is asserted at
//    compile time and Scratchpad/AcousticStructureTest checks every default and the byte-for-byte Save against the page.
//
//    Section names and keys are the TOML interface and stay snake_case there; the C++ members are the same words in
//    PascalCase (pitch_hz → Voice.PitchHz). Unknown keys are counted (Load reports them), missing keys take the default,
//    so a rev-3 archive loads in a rev-2 build and vice versa. Trivially copyable on purpose: AcousticIntegrator relays a
//    new structure to the realtime thread through a RelayQueue<AcousticStructure> (no locks, no allocation).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   FIELD SHEET
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t AcousticTextCapacity     = 96u;    // [bytes] display strings (name, engine line) incl. terminator
constexpr uint32_t AcousticRealListCapacity = 32u;    // [-]     longest list a field can hold (firing order of a 16-cylinder, harmonics)
constexpr uint32_t AcousticFieldCount       = 124u;   // [-]     rows in the sheet = ACOUSTIC_SCHEMA.length in the editor

enum class AcousticFieldCategory : uint32_t
{
    Real     = 0,     // double
    Whole    = 1,     // int32_t (cylinder_count)
    Toggle   = 2,     // bool
    Text     = 3,     // char[AcousticTextCapacity]
    RealList = 4      // AcousticRealList
};

// A fixed-capacity list of reals (integers in the TOML for firing_order / bank_of_cylinder, reals for harmonics).
struct AcousticRealList
{
    uint32_t Count = 0u;                             // [-] entries in use
    double   Entry[AcousticRealListCapacity] = {};   // [-]

    [[nodiscard]] double operator[](uint32_t I) const noexcept { return I < Count ? Entry[I] : 0.0; }
};

// One row of the sheet: where the field lives, what it defaults to, what the inspector shows.
struct AcousticFieldNote
{
    const char*           Section;       // TOML section, e.g. "exhaust"
    const char*           Key;           // TOML key, e.g. "valve_open_rpm"
    AcousticFieldCategory Category;
    size_t                Offset;        // [bytes] offsetof the member inside AcousticStructure
    double                DefaultReal;   // Real / Whole / Toggle default (Toggle: 0 or 1)
    const char*           DefaultText;   // Text default
    const char*           Unit;          // "" or "-" = dimensionless, else e.g. "rpm"
    const char*           Note;          // the schema comment, written after the value on Save
    double                Minimum;       // inspector range (Ranged only)
    double                Maximum;
    double                Step;
    bool                  Ranged;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ACOUSTIC STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

struct AcousticStructure
{
    struct VehicleSheet
    {
        char              Name[AcousticTextCapacity] = "Generic V8";                            // [-]      display name
        char              Engine[AcousticTextCapacity] = "4.0 L 90° V8";                        // [-]      one-line engine description
        int32_t           CylinderCount = 8;                                                    // [-]      number of cylinders
        AcousticRealList  FiringOrder = { 8u, { 1.0, 8.0, 3.0, 6.0, 4.0, 5.0, 2.0, 7.0 } };     // [-]      cylinder numbers in firing sequence
        AcousticRealList  BankOfCylinder = { 8u, { 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0 } };  // [-]      bank per cylinder number (0 = left pipe, 1 = right pipe)
        bool              EvenFiring = true;                                                    // [-]      on = firings at even 720/N (split pins, inline, flat-plane, 90° V8)
        double            BankAngleDeg = 90.0;                                                  // [°]      V angle between the banks
        double            IdleRpm = 900.0;                                                      // [rpm]    
        double            RedlineRpm = 8000.0;                                                  // [rpm]    fuel cut
        double            PeakPowerRpm = 7500.0;                                                // [rpm]    
        double            PeakTorqueRpm = 5500.0;                                               // [rpm]    
        double            PeakTorqueNm = 500.0;                                                 // [N·m]    
        double            InertiaKgm2 = 0.4;                                                    // [kg·m²]  effective rotating inertia seen by the free-rev powertrain
        double            FrictionNm = 25.0;                                                    // [N·m]    friction torque at zero rpm
        double            FrictionPerKrpmNm = 6.0;                                              // [N·m]    friction torque increase per 1000 rpm
    } Vehicle;

    struct CombustionSheet
    {
        double            EvoDeg = 130.0;                                                       // [°]      voice starts this many degrees after the firing TDC (exhaust valve opening)
        double            JitterPct = 5.0;                                                      // [%]      firing time walks by a uniform ±half of this share of the interval per firing, …
        double            IdleJitterBoost = 1.0;                                                // [-]      jitter grows by this factor toward zero load
        double            JitterAmp = 0.05;                                                     // [-]      cycle-to-cycle amplitude deviation (1 σ)
        double            TimingJitterDeg = 0.0;                                                // [°]      per-firing timing jitter (1 σ, crank degrees), not cumulative
        double            CylinderSpreadPct = 0.0;                                              // [%]      fixed cylinder-to-cylinder amplitude imbalance at full load (peak, % of the pulse
        double            MisfireProbability = 0.0;                                             // [-]      per firing at full load
        double            BackfireLevel = 2.0;                                                  // [-]      lift-off backfire thud (throttle > 0.6 → < 0.1 above 60 % redline)
        double            BackfireProbability = 0.2;                                            // [-]      chance of the thud per lift
        double            PopLevel = 0.6;                                                       // [-]      overrun crackle amplitude
        double            PopRateHz = 25.0;                                                     // [Hz]     overrun crackle rate at redline right after lift
        double            PopDecayS = 1.2;                                                      // [s]      overrun crackle fades with this time constant
    } Combustion;

    struct VoiceSheet
    {
        bool              CrankPulse = false;                                                   // [-]      rev 3: each firing plays one blowdown kernel defined over crank degrees (length ∝ …
        double            PulseRiseDeg = 12.0;                                                  // [°]      crank_pulse: blowdown rise from exhaust valve opening to the discharge peak
        double            PulseBlowdownDeg = 45.0;                                              // [°]      crank_pulse: blowdown decay length (1/e² point)
        double            PulseTailLevel = 0.25;                                                // [-]      crank_pulse: displacement-stroke discharge (piston velocity BDC → TDC) re the …
        double            PulseUndershoot = 0.3;                                                // [-]      crank_pulse: share of the reverse piston velocity after TDC (valve still open …
        double            PulseSharpness = 1.0;                                                 // [-]      crank_pulse: rise curvature
        double            PulseSoftLevel = 0.25;                                                // [-]      crank_pulse: blowdown at zero load re full load (the voice crossfades hard ↔ soft …
        double            PulseSoftStretch = 1.5;                                               // [-]      crank_pulse: zero-load rise is this many times longer (decay ½ (1 + stretch) ×)
        double            PitchHz = 65.0;                                                       // [Hz]     rev-2 voice: pulse repetition inside one firing at 0 rpm (the "thump" pitch)
        double            PitchRiseHz = 15.0;                                                   // [Hz]     added to the pitch at 8000 rpm
        double            DecayPerS = 15.0;                                                     // [1/s]    voice envelope decay at 0 rpm
        double            DecayPerKrpm = 3.0;                                                   // [1/s]    added decay per 1000 rpm
        double            ThumpLevel = 0.8;                                                     // [-]      pressure pulse amplitude (rev 2: the asymmetric thump sheet
        double            BodyLevel = 0.6;                                                      // [-]      rev-2 voice: harmonic body (24 harmonics of the pitch)
        AcousticRealList  Harmonics = { 4u, { 1.0, 0.8, 0.5, 0.3 } };                           // [-]      weights of harmonics 1 … n
        double            Resonance = 0.5;                                                      // [-]      formant climb with rpm
        double            CrackleLevel = 0.1;                                                   // [-]      per-firing noise burst at zero load
        double            CrackleLoadLevel = 0.4;                                               // [-]      added at full load
        double            RingLevel = 0.1;                                                      // [-]      valve ring at zero load (5 / 6 / 7 kHz by cylinder)
        double            RingLoadLevel = 0.2;                                                  // [-]      added at full load
        double            RingHz = 5000.0;                                                      // [Hz]     ring of cylinder 1
        double            RingFadeRpm = 4000.0;                                                 // [rpm]    ring is gone above this speed
        double            RaspLevel = 1.5;                                                      // [-]      AM rasp (3 × pitch carrier, ½ × pitch modulator)
        double            HowlLevel = 0.0;                                                      // [-]      intake howl above howl_rpm
        double            HowlRpm = 4000.0;                                                     // [rpm]    
        double            HowlSpanRpm = 5000.0;                                                 // [rpm]    howl reaches full level this far above howl_rpm
        double            CamOrder = 4.0;                                                       // [-]      amplitude modulation order relative to the cam (rpm / 120 Hz)
        double            CamDepth = 0.0;                                                       // [-]      modulation depth (0 for flat-plane cranks)
        double            LiftLevel = 1.0;                                                      // [-]      voice amplitude at zero load (fuel cut / overrun)
    } Voice;

    struct IntakeSheet
    {
        double            Level = 0.0;                                                          // [-]      rev 3 intake voice on the induction bus: suction pulses at intake valve opening …
        double            Direct = 0.3;                                                         // [-]      share of the raw suction train radiated straight out of the trumpet mouth …
        double            PathM = 0.0;                                                          // [m]      extra path of the intake voice to the listener re the tailpipes (trumpets under the …
        double            RunnerHz = 220.0;                                                     // [Hz]     quarter-wave frequency of the long tract (c / 4L)
        double            ShortRatio = 2.0;                                                     // [-]      tract length is cut to 1/ratio at step_rpm_1 (LaFerrari: halved → ×2), then eases …
        double            MinRatio = 2.6;                                                       // [-]      frequency ratio above step_rpm_2 (minimum tract length)
        double            StepRpm1 = 6000.0;                                                    // [rpm]    first tract step
        double            StepRpm2 = 8000.0;                                                    // [rpm]    second tract step (minimum length above it)
        double            Q = 9.0;                                                              // [-]      runner resonator Q (ring length of the trumpet)
        double            Breath = 0.2;                                                         // [-]      aspiration noise through the same resonator, ∝ throttle · rpm / redline, re level
    } Intake;

    struct ExhaustSheet
    {
        double            Drive = 1.0;                                                          // [-]      multiplies the per-channel tanh drive (1 + 0.8 · load)
        double            BankPan = 0.9;                                                        // [-]      0 = both banks centred … 1 = hard left/right
        double            PanSplitHz = 0.0;                                                     // [Hz]     below this the two banks merge in the image (tailpipes a metre apart are one emitter …
        double            FormantAHz = 180.0;                                                   // [Hz]     fixed exhaust resonator A (collector / silencer ridge that stays put while the pitch …
        double            FormantAQ = 8.0;                                                      // [-]      
        double            FormantALevel = 0.0;                                                  // [-]      added to the exhaust bus after the tanh
        double            FormantBHz = 215.0;                                                   // [Hz]     fixed exhaust resonator B
        double            FormantBQ = 8.0;                                                      // [-]      
        double            FormantBLevel = 0.0;                                                  // [-]      
        double            SilencerHz = 24000.0;                                                 // [Hz]     4th-order low-pass on the exhaust bus while the bypass valve is shut (cat + silencer …
        double            SilencerOpenHz = 24000.0;                                             // [Hz]     the same low-pass above valve_open_rpm (bypass open)
        double            ValveOpenGain = 1.0;                                                  // [-]      exhaust bus gain above valve_open_rpm (the "opens up" step
        double            CombMix = 1.0;                                                        // [-]      share of the feedback comb output added to the bus (RevSim 1
        double            CombMs = 2.667;                                                       // [ms]     feedback comb delay (pipe colour)
        double            CombFeedback = 0.3;                                                   // [-]      
        double            ValveHpHz = 10.0;                                                     // [Hz]     high-pass below valve_open_rpm (exhaust valve shut)
        double            ValveOpenHpHz = 10.0;                                                 // [Hz]     high-pass above valve_open_rpm
        double            ValveOpenRpm = 5000.0;                                                // [rpm]    
        double            ValveOpenLoad = 2.0;                                                  // [-]      the bypass valve also opens above this load (above 1 = rpm only)
    } Exhaust;

    struct MechanicalSheet
    {
        double            ClatterLevel = 0.0;                                                   // [-]      valve seating clatter (crank-locked)
        double            ClatterHz = 4500.0;                                                   // [Hz]     clatter resonance
        double            WhineOrder = 0.0;                                                     // [-]      gear whine order relative to crank speed (0 = none)
        double            WhineLevel = 0.0;                                                     // [-]      at redline
        double            MotorLevel = 0.0;                                                     // [-]      hybrid drive motor tone, scales with load (0 = none)
        double            MotorHz = 200.0;                                                      // [Hz]     motor tone at zero load
        double            MotorLoadHz = 600.0;                                                  // [Hz]     added at full load
    } Mechanical;

    struct TurboSheet
    {
        bool              Enabled = false;                                                      // [-]      forced-induction layer
        double            SpoolUpS = 0.2;                                                       // [s]      spool follows rpm / redline · load with this lag
        double            SpoolDownS = 0.2;                                                     // [s]      
        double            WhineHz = 2500.0;                                                     // [Hz]     compressor whine at zero spool
        double            WhineSpanHz = 4000.0;                                                 // [Hz]     added at full spool
        double            WhineLevel = 0.4;                                                     // [-]      at full spool
        double            RushHz = 2000.0;                                                      // [Hz]     air-rush band-pass centre at zero spool
        double            RushSpanHz = 6000.0;                                                  // [Hz]     added at full spool
        double            RushQ = 1.5;                                                          // [-]      
        double            RushLevel = 0.4;                                                      // [-]      at full spool
        double            BoostMaxBar = 1.0;                                                    // [bar]    readout only
        double            BlowoffLevel = 0.8;                                                   // [-]      blow-off flutter on lift above 3000 rpm
        double            AntilagLevel = 3.0;                                                   // [-]      anti-lag pops instead of the blow-off above antilag_rpm (0 = off)
        double            AntilagRpm = 4000.0;                                                  // [rpm]    
        double            FlutterHz = 14.0;                                                     // [Hz]     blow-off flutter rate (14 = RevSim, the Agera's wastegate chatter ≈ 25)
        double            ThumpLevel = 0.0;                                                     // [-]      low sine thud under the blow-off on lift (the Agera's "whump"); 0 = off
        double            ThumpHz = 60.0;                                                       // [Hz]     thud pitch
    } Turbo;

    struct ChargerSheet                                                                          // row A2½: belt-driven positive-displacement supercharger
    {
        bool              Enabled = false;                                                      // [-]      supercharger layer: rotor pulsation whine, boost ∝ rpm · load, bypass valve shut-throttle
        double            DriveRatio = 2.36;                                                    // [-]      rotor speed re crank speed (pulley ratio; Hellcat / Demon IHI 2.36)
        double            Lobes = 3.0;                                                          // [-]      lobes on the driven (male) rotor: pocket-passing order = lobes × drive_ratio re crank
        double            WhineLevel = 0.3;                                                     // [-]      pulsation whine at redline, full load; scales with rpm / redline
        AcousticRealList  WhineHarmonics = { 4u, { 1.0, 0.7, 0.5, 0.35 } };                     // [-]      weights of the pulsation harmonics 1 … n (up to 8)
        double            WhineDrive = 1.0;                                                     // [-]      tanh shaping of the summed harmonics (> 1 = gritty)
        double            BypassLevel = 0.15;                                                   // [-]      share of the whine left with the throttle shut (bypass valve open)
        double            BoostMaxBar = 1.0;                                                    // [bar]    readout only: boost = boost_max · rpm / redline · load (no spool)
        double            BarkLevel = 0.0;                                                      // [-]      intake bark on a fast throttle stab (bypass valve snapping shut); 0 = off
        double            BarkHz = 600.0;                                                       // [Hz]     bark band centre
    } Charger;

    struct MixSheet
    {
        double            Exhaust = 1.0;                                                        // [-]      bus levels after the tanh (listener presets multiply these)
        double            Induction = 1.0;                                                      // [-]      turbo whine + rush · rev 3 intake voice
        double            Mechanical = 1.0;                                                     // [-]      clatter · gear whine · motor
        double            Transients = 1.0;                                                     // [-]      backfire · pops · anti-lag · blow-off
        double            OutputGain = 1.5;                                                     // [-]      before the comb and the ±1 clip (RevSim master 1.5)
    } Mix;

    // The sheet, one row per field in schema order (Fields[0] is vehicle.name, Fields[AcousticFieldCount − 1] is mix.output_gain).
    static const AcousticFieldNote Fields[AcousticFieldCount];

    // Text file in → structure. Missing keys keep their defaults; unknown sections / keys are counted into *Unknown when
    //    asked for (the caller decides whether that is a warning). False only when the text is not TOML at all.
    static bool Deserialise(std::string_view Toml, AcousticStructure& Out, std::string* Error = nullptr, uint32_t* Unknown = nullptr) noexcept;

    // Structure → text, byte-identical to the editor's serialiseToml for the same structure and header lines
    //    (Header lines are written as "# line", an empty line as "#"; the archives use four).
    [[nodiscard]] static std::string Serialise(const AcousticStructure& S, const char* const* HeaderLines = nullptr, uint32_t HeaderCount = 0u) noexcept;

    // File wrappers.
    static bool Load(std::string_view Path, AcousticStructure& Out, std::string* Error = nullptr, uint32_t* Unknown = nullptr) noexcept;
    static bool Save(std::string_view Path, const AcousticStructure& S, const char* const* HeaderLines = nullptr, uint32_t HeaderCount = 0u, std::string* Error = nullptr) noexcept;

    // Field access through the sheet (what an inspector uses). Real/Whole/Toggle read and write as double; Text and
    //    RealList through their own accessors. Index ≥ AcousticFieldCount → 0 / no-op.
    [[nodiscard]] double            QueryReal(uint32_t Field) const noexcept;
    void                            AssignReal(uint32_t Field, double Value) noexcept;
    [[nodiscard]] const char*       QueryText(uint32_t Field) const noexcept;
    [[nodiscard]] const AcousticRealList* QueryList(uint32_t Field) const noexcept;

    // Index of "section.key" in the sheet, or AcousticFieldCount when absent.
    [[nodiscard]] static uint32_t FindField(std::string_view Section, std::string_view Key) noexcept;
};

static_assert(sizeof(AcousticStructure) < 4096u, "AcousticStructure is relayed by value to the realtime thread — keep it small");

} // namespace Frontier
