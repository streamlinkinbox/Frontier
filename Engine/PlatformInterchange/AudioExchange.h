//============================================================================================================================================
//                                                        AUDIOEXCHANGE.H
//============================================================================================================================================
// 🧩 The C-ABI boundary to the platform's audio device (miniaudio: WASAPI / PulseAudio / ALSA / Core Audio, null when asked).
//    Owns the device, its realtime thread, and the one rule that keeps audio glitch-free: the realtime callback touches
//    nothing but the attached SignalIntegrator and a handful of relaxed atomics. The main thread talks to it through
//    Advance(Δτ) exactly like every other host in Slate (Notifications.Advance, ControlCentre.AdvanceLocomotion …).
//
//    Threads
//        main       Open / Attach / Advance / Close — Advance publishes device events (start, stop, reroute, loss) as
//                   AudioMetrics counters and reopens after a device loss with a 0.5 s back-off.
//        realtime   miniaudio's data callback → Render() on the attached integrator, in fixed RenderSliceFrames slices
//                   regardless of the period WASAPI actually granted, so an offline RenderToWave pass produces the same
//                   samples the device plays (bit-identity between the dyno's WAV and the speaker).
//
//    Demand (rpm, throttle …) does not pass through here: the integrator owns its own RelayQueue<PowertrainRecord> (see
//    CrankClickIntegrator now, AcousticIntegrator from row A2). This exchange is content-agnostic — anything that fills
//    interleaved float frames can be attached.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct ma_device;      // miniaudio, opaque here — only MiniaudioTranslation.cpp and AudioExchange.cpp see the header
struct ma_context;

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   SIGNAL INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

// Anything that can fill interleaved float frames on the realtime thread. Render must be lock-free and allocation-free.
//    Output is interleaved, ChannelCount floats per frame, nominal full scale ±1 (the device clips after the callback).
class SignalIntegrator
{
public:
    virtual ~SignalIntegrator() noexcept = default;

    // Called once from Open/Attach on the main thread before the first Render; size internal rings here, never in Render.
    virtual void Prepare(uint32_t SampleRate, uint32_t ChannelCount) noexcept = 0;

    // Realtime: write exactly FrameCount × ChannelCount floats into Output (already zeroed).
    virtual void Render(float* Output, uint32_t FrameCount) noexcept = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   AUDIO CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

enum class AudioDriverCategory : uint32_t
{
    Platform = 0,     // miniaudio's default driver order for the host OS (WASAPI on Windows)
    Null     = 1      // no hardware: a clocked thread that discards output — sandbox proofs, CI, headless renders
};

struct AudioConfiguration
{
    uint32_t             SampleRate        = 48000u;                         // [Hz]     requested; QueryMetrics reports the granted rate
    uint32_t             ChannelCount      = 2u;                             // [-]      interleaved output channels
    uint32_t             PeriodFrames      = 256u;                           // [frames] requested device period (WASAPI shared mode may grant 480/512)
    uint32_t             PeriodCount       = 3u;                             // [-]      device ring depth
    uint32_t             RenderSliceFrames = 64u;                            // [frames] fixed slice handed to the integrator (1.33 ms @ 48 kHz)
    AudioDriverCategory  Driver            = AudioDriverCategory::Platform;
    bool                 LowLatency        = true;                           // [-]      ma_performance_profile_low_latency vs conservative
    float                MasterGain        = 1.0f;                           // [-]      linear, applied after the integrator, before the device clip

    [[nodiscard]] bool operator==(const AudioConfiguration&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   AUDIO METRICS
//------------------------------------------------------------------------------------------------------------------------

// Snapshot the main thread reads through QueryMetrics(); the realtime side only bumps relaxed atomics.
struct AudioMetrics
{
    uint64_t CallbackCount        = 0u;      // [-]   data callbacks served since Open
    uint64_t FramesRendered       = 0u;      // [-]   frames handed to the device since Open
    uint32_t GrantedSampleRate    = 0u;      // [Hz]  what the device actually runs at
    uint32_t GrantedPeriodFrames  = 0u;      // [frames] what the device actually asks for per callback
    uint32_t LastCallbackFrames   = 0u;      // [frames] size of the most recent callback
    float    LastCallbackMicros   = 0.0f;    // [µs]  wall time spent inside the most recent callback
    float    PeakCallbackMicros   = 0.0f;    // [µs]  worst callback since Open (or since ResetPeaks)
    float    CallbackLoad         = 0.0f;    // [-]   LastCallbackMicros ÷ callback period (1.0 = the deadline)
    float    PeakLoad             = 0.0f;    // [-]   worst load since Open
    float    OutputPeak           = 0.0f;    // [-]   |sample| peak of the most recent callback, after MasterGain
    uint32_t OverloadCount        = 0u;      // [-]   callbacks whose load exceeded 1.0 (a glitch on most hosts)
    uint32_t ClipCount            = 0u;      // [-]   callbacks where |sample| exceeded 1.0 before the device clip
    uint32_t StopCount            = 0u;      // [-]   device stopped notifications (loss, interruption)
    uint32_t RerouteCount         = 0u;      // [-]   default device changed under us
    uint32_t ReopenCount          = 0u;      // [-]   successful reopens after a loss
    std::string DriverName;                 // "WASAPI", "PulseAudio", "Null" …
    std::string DeviceName;                  // playback endpoint friendly name
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   AUDIO EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class AudioExchange
{
public:
    AudioExchange() noexcept;
    ~AudioExchange() noexcept;

    AudioExchange(const AudioExchange&)            = delete;
    AudioExchange& operator=(const AudioExchange&) = delete;

    // Brings the device up and starts the realtime thread (silence until an integrator is attached). False + reason on refusal;
    //    the exchange is then inert and Advance/Close are safe no-ops.
    bool Open(const AudioConfiguration& Configuration, std::string* Error = nullptr) noexcept;
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept { return Device != nullptr; }

    // Attaches (or detaches with nullptr) the integrator the realtime thread calls. Prepare() is invoked here, on the main
    //    thread, before the pointer is published. Detaching waits for any in-flight callback to leave the old integrator.
    void Attach(SignalIntegrator* Integrator) noexcept;
    [[nodiscard]] SignalIntegrator* QueryIntegrator() const noexcept { return Attached.load(std::memory_order_acquire); }

    // Main-thread housekeeping: folds realtime counters into Metrics, handles device loss / reroute (reopen with back-off).
    void Advance(float Δτ) noexcept;

    void AssignMasterGain(float Gain) noexcept;
    [[nodiscard]] float QueryMasterGain() const noexcept { return MasterGain.load(std::memory_order_relaxed); }

    [[nodiscard]] const AudioMetrics&       QueryMetrics()       const noexcept { return Metrics; }
    [[nodiscard]] const AudioConfiguration& QueryConfiguration() const noexcept { return Configuration; }
    void ResetPeaks() noexcept;

    // Offline path: drives Integrator through the same fixed RenderSliceFrames slices the realtime thread uses, for
    //    Seconds of audio, into Output (interleaved, resized). Deterministic; no device needed. Used by the dyno's --render
    //    and by every Scratchpad proof. Static because it needs no device — the exchange only supplies the slicing rule.
    static void RenderOffline(SignalIntegrator& Integrator, uint32_t SampleRate, uint32_t ChannelCount, uint32_t RenderSliceFrames,
                              double Seconds, float MasterGain, std::vector<float>& Output) noexcept;

private:
    // Realtime side
    static void PlaybackThunk(ma_device* Device, void* Output, const void* Input, uint32_t FrameCount);
    static void NotificationThunk(const void* Notification);
    void        ServeCallback(float* Output, uint32_t FrameCount) noexcept;

    // Main side
    bool BringUp(std::string* Error) noexcept;
    void TearDown() noexcept;

    AudioConfiguration          Configuration;
    ma_context*                 Context     = nullptr;
    ma_device*                  Device      = nullptr;
    AudioMetrics                Metrics;

    std::atomic<SignalIntegrator*> Attached   { nullptr };
    std::atomic<uint32_t>        InCallback { 0u };          // [-] 1 while ServeCallback runs (Attach spins on it)
    std::atomic<float>           MasterGain { 1.0f };

    // Realtime → main counters (relaxed; Advance folds them)
    std::atomic<uint64_t>       CallbackCount     { 0u };
    std::atomic<uint64_t>       FramesRendered    { 0u };
    std::atomic<uint32_t>       LastFrames        { 0u };
    std::atomic<float>          LastMicros        { 0.0f };
    std::atomic<float>          PeakMicros        { 0.0f };
    std::atomic<float>          OutputPeak        { 0.0f };
    std::atomic<uint32_t>       OverloadCount     { 0u };
    std::atomic<uint32_t>       ClipCount         { 0u };
    std::atomic<uint32_t>       StopSignals       { 0u };
    std::atomic<uint32_t>       RerouteSignals    { 0u };
    uint32_t                    StopsFolded       = 0u;
    uint32_t                    ReroutesFolded    = 0u;
    float                       ReopenBackoff     = 0.0f;     // [s] remaining before the next reopen attempt
    bool                        LossPending       = false;
};

} // namespace Frontier
