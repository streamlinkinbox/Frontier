//============================================================================================================================================
//                                                       AUDIOEXCHANGE.CPP
//============================================================================================================================================
// 🧩 miniaudio device transport (see AudioExchange.h). The only TU besides MiniaudioTranslation.cpp that includes miniaudio.h.
//
//    Callback anatomy (realtime thread):
//        ① mark InCallback, load the attached integrator (acquire)
//        ② render in RenderSliceFrames slices into a stack scratch (no heap), scale by MasterGain, track |peak|
//        ③ copy into the device's interleaved f32 output; leave clipping to miniaudio (noClip = false)
//        ④ stamp wall time → LastMicros / PeakMicros / OverloadCount, clear InCallback
//    Everything it touches is either stack, the integrator, or a relaxed atomic. No locks, no allocation, no logging.

#include "AudioExchange.h"

#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

namespace Frontier {

namespace {

constexpr uint32_t ScratchFrames   = 256u;   // [frames] largest RenderSliceFrames the stack scratch accepts
constexpr uint32_t ScratchChannels = 8u;     // [-]      largest channel count the stack scratch accepts
constexpr float    ReopenDelay     = 0.5f;   // [s]      back-off between reopen attempts after a device loss

float AtomicMax(std::atomic<float>& Slot, float Candidate) noexcept
{
    float Seen = Slot.load(std::memory_order_relaxed);
    while (Candidate > Seen && !Slot.compare_exchange_weak(Seen, Candidate, std::memory_order_relaxed)) { }
    return Seen > Candidate ? Seen : Candidate;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

AudioExchange::AudioExchange() noexcept = default;

AudioExchange::~AudioExchange() noexcept
{
    Close();
}

bool AudioExchange::Open(const AudioConfiguration& Requested, std::string* Error) noexcept
{
    if (IsOpen()) Close();

    Configuration = Requested;
    if (Configuration.SampleRate == 0u)        { if (Error) *Error = "AudioExchange::Open: sample rate must be non-zero"; return false; }
    if (Configuration.ChannelCount == 0u || Configuration.ChannelCount > ScratchChannels)
                                               { if (Error) *Error = "AudioExchange::Open: channel count must be 1 … 8"; return false; }
    if (Configuration.RenderSliceFrames == 0u || Configuration.RenderSliceFrames > ScratchFrames)
                                               { if (Error) *Error = "AudioExchange::Open: render block must be 1 … 256 frames"; return false; }
    if (Configuration.PeriodFrames == 0u)      Configuration.PeriodFrames = 256u;

    MasterGain.store(Configuration.MasterGain, std::memory_order_relaxed);
    Metrics = AudioMetrics{};
    CallbackCount.store(0u); FramesRendered.store(0u); LastFrames.store(0u);
    LastMicros.store(0.0f);  PeakMicros.store(0.0f);   OutputPeak.store(0.0f);
    OverloadCount.store(0u); ClipCount.store(0u);      StopSignals.store(0u); RerouteSignals.store(0u);
    StopsFolded = ReroutesFolded = 0u;
    ReopenBackoff = 0.0f;
    LossPending   = false;

    return BringUp(Error);
}

bool AudioExchange::BringUp(std::string* Error) noexcept
{
    Context = new (std::nothrow) ma_context;
    Device  = new (std::nothrow) ma_device;
    if (!Context || !Device)
    {
        delete Context; delete Device; Context = nullptr; Device = nullptr;
        if (Error) *Error = "AudioExchange::Open: out of memory";
        return false;
    }

    ma_context_config ContextConfig = ma_context_config_init();
    ContextConfig.threadPriority    = ma_thread_priority_realtime;

    ma_result Result;
    if (Configuration.Driver == AudioDriverCategory::Null)
    {
        const ma_backend Backends[] = { ma_backend_null };
        Result = ma_context_init(Backends, 1u, &ContextConfig, Context);
    }
    else
    {
        Result = ma_context_init(nullptr, 0u, &ContextConfig, Context);
    }
    if (Result != MA_SUCCESS)
    {
        delete Context; delete Device; Context = nullptr; Device = nullptr;
        if (Error) *Error = std::string("AudioExchange::Open: ma_context_init failed: ") + ma_result_description(Result);
        return false;
    }

    ma_device_config DeviceConfig       = ma_device_config_init(ma_device_type_playback);
    DeviceConfig.playback.format        = ma_format_f32;
    DeviceConfig.playback.channels      = Configuration.ChannelCount;
    DeviceConfig.sampleRate             = Configuration.SampleRate;
    DeviceConfig.periodSizeInFrames     = Configuration.PeriodFrames;
    DeviceConfig.periods                = Configuration.PeriodCount;
    DeviceConfig.performanceProfile     = Configuration.LowLatency ? ma_performance_profile_low_latency : ma_performance_profile_conservative;
    DeviceConfig.dataCallback           = &AudioExchange::PlaybackThunk;
    DeviceConfig.notificationCallback   = reinterpret_cast<ma_device_notification_proc>(&AudioExchange::NotificationThunk);
    DeviceConfig.pUserData              = this;
    DeviceConfig.noPreSilencedOutputBuffer = MA_TRUE;   // ServeCallback writes every sample itself
    DeviceConfig.noClip                 = MA_FALSE;      // the device clips after us; ClipCount records that it had to
    DeviceConfig.noDisableDenormals     = MA_FALSE;      // FTZ/DAZ on the realtime thread — waveguide tails never denormal
    DeviceConfig.noFixedSizedCallback   = MA_FALSE;      // hold the period size so LastCallbackFrames is meaningful

    Result = ma_device_init(Context, &DeviceConfig, Device);
    if (Result != MA_SUCCESS)
    {
        ma_context_uninit(Context);
        delete Context; delete Device; Context = nullptr; Device = nullptr;
        if (Error) *Error = std::string("AudioExchange::Open: ma_device_init failed: ") + ma_result_description(Result);
        return false;
    }

    Metrics.GrantedSampleRate   = Device->sampleRate;
    Metrics.GrantedPeriodFrames = Device->playback.internalPeriodSizeInFrames;
    Metrics.DriverName         = ma_get_backend_name(Context->backend);
    Metrics.DeviceName          = Device->playback.name;

    Result = ma_device_start(Device);
    if (Result != MA_SUCCESS)
    {
        TearDown();
        if (Error) *Error = std::string("AudioExchange::Open: ma_device_start failed: ") + ma_result_description(Result);
        return false;
    }
    return true;
}

void AudioExchange::TearDown() noexcept
{
    if (Device)
    {
        ma_device_uninit(Device);      // stops the thread and joins it — no callback can be in flight after this returns
        delete Device;
        Device = nullptr;
    }
    if (Context)
    {
        ma_context_uninit(Context);
        delete Context;
        Context = nullptr;
    }
}

void AudioExchange::Close() noexcept
{
    TearDown();
    Attached.store(nullptr, std::memory_order_release);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ATTACH / GAIN
//------------------------------------------------------------------------------------------------------------------------

void AudioExchange::Attach(SignalIntegrator* Integrator) noexcept
{
    if (Integrator)
    {
        // Prepare on the main thread with the granted rate (a shared-mode WASAPI mix rate can differ from the request).
        const uint32_t Rate = Metrics.GrantedSampleRate ? Metrics.GrantedSampleRate : Configuration.SampleRate;
        Integrator->Prepare(Rate, Configuration.ChannelCount);
    }
    Attached.store(Integrator, std::memory_order_release);

    // Whoever was attached before may still be inside Render on the realtime thread: wait for that callback to leave
    //    before returning, so the caller may destroy the old integrator immediately afterwards.
    while (InCallback.load(std::memory_order_acquire) != 0u) std::this_thread::yield();
}

void AudioExchange::AssignMasterGain(float Gain) noexcept
{
    Configuration.MasterGain = std::clamp(Gain, 0.0f, 4.0f);
    MasterGain.store(Configuration.MasterGain, std::memory_order_relaxed);
}

void AudioExchange::ResetPeaks() noexcept
{
    PeakMicros.store(0.0f, std::memory_order_relaxed);
    Metrics.PeakCallbackMicros = 0.0f;
    Metrics.PeakLoad           = 0.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   REALTIME
//------------------------------------------------------------------------------------------------------------------------

void AudioExchange::PlaybackThunk(ma_device* Device, void* Output, const void* /*Input*/, uint32_t FrameCount)
{
    static_cast<AudioExchange*>(Device->pUserData)->ServeCallback(static_cast<float*>(Output), FrameCount);
}

void AudioExchange::NotificationThunk(const void* Opaque)
{
    const ma_device_notification* Notification = static_cast<const ma_device_notification*>(Opaque);
    AudioExchange* Self = static_cast<AudioExchange*>(Notification->pDevice->pUserData);
    switch (Notification->type)
    {
        case ma_device_notification_type_stopped:            Self->StopSignals.fetch_add(1u, std::memory_order_relaxed);    break;
        case ma_device_notification_type_rerouted:           Self->RerouteSignals.fetch_add(1u, std::memory_order_relaxed); break;
        case ma_device_notification_type_interruption_began: Self->StopSignals.fetch_add(1u, std::memory_order_relaxed);    break;
        default: break;
    }
}

void AudioExchange::ServeCallback(float* Output, uint32_t FrameCount) noexcept
{
    using Clock = std::chrono::steady_clock;
    const auto Entered = Clock::now();

    InCallback.store(1u, std::memory_order_release);
    SignalIntegrator* Integrator = Attached.load(std::memory_order_acquire);

    const uint32_t Channels = Configuration.ChannelCount;
    const uint32_t Slice    = Configuration.RenderSliceFrames;
    const float    Gain     = MasterGain.load(std::memory_order_relaxed);

    float Peak = 0.0f;
    if (!Integrator)
    {
        std::memset(Output, 0, size_t(FrameCount) * Channels * sizeof(float));
    }
    else
    {
        alignas(32) float Scratch[ScratchFrames * ScratchChannels];
        uint32_t Done = 0u;
        while (Done < FrameCount)
        {
            const uint32_t Frames  = std::min(Slice, FrameCount - Done);
            const size_t   Samples = size_t(Frames) * Channels;
            std::memset(Scratch, 0, Samples * sizeof(float));
            Integrator->Render(Scratch, Frames);
            float* Dst = Output + size_t(Done) * Channels;
            for (size_t I = 0; I < Samples; ++I)
            {
                const float S = Scratch[I] * Gain;
                Dst[I] = S;
                const float A = std::fabs(S);
                if (A > Peak) Peak = A;
            }
            Done += Frames;
        }
    }

    InCallback.store(0u, std::memory_order_release);

    const float Micros = std::chrono::duration<float, std::micro>(Clock::now() - Entered).count();
    const uint32_t Rate = Metrics.GrantedSampleRate ? Metrics.GrantedSampleRate : Configuration.SampleRate;
    const float Budget  = 1.0e6f * float(FrameCount) / float(Rate);

    CallbackCount.fetch_add(1u, std::memory_order_relaxed);
    FramesRendered.fetch_add(FrameCount, std::memory_order_relaxed);
    LastFrames.store(FrameCount, std::memory_order_relaxed);
    LastMicros.store(Micros, std::memory_order_relaxed);
    AtomicMax(PeakMicros, Micros);
    OutputPeak.store(Peak, std::memory_order_relaxed);
    if (Micros > Budget) OverloadCount.fetch_add(1u, std::memory_order_relaxed);
    if (Peak > 1.0f)     ClipCount.fetch_add(1u, std::memory_order_relaxed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   MAIN-THREAD ADVANCE
//------------------------------------------------------------------------------------------------------------------------

void AudioExchange::Advance(float Δτ) noexcept
{
    if (!IsOpen() && !LossPending) return;

    // Fold realtime counters
    Metrics.CallbackCount      = CallbackCount.load(std::memory_order_relaxed);
    Metrics.FramesRendered     = FramesRendered.load(std::memory_order_relaxed);
    Metrics.LastCallbackFrames = LastFrames.load(std::memory_order_relaxed);
    Metrics.LastCallbackMicros = LastMicros.load(std::memory_order_relaxed);
    Metrics.PeakCallbackMicros = PeakMicros.load(std::memory_order_relaxed);
    Metrics.OutputPeak         = OutputPeak.load(std::memory_order_relaxed);
    Metrics.OverloadCount      = OverloadCount.load(std::memory_order_relaxed);
    Metrics.ClipCount          = ClipCount.load(std::memory_order_relaxed);
    const uint32_t Rate   = Metrics.GrantedSampleRate ? Metrics.GrantedSampleRate : Configuration.SampleRate;
    const float    Budget = Metrics.LastCallbackFrames ? 1.0e6f * float(Metrics.LastCallbackFrames) / float(Rate) : 0.0f;
    const float    PeakBudget = Metrics.GrantedPeriodFrames ? 1.0e6f * float(Metrics.GrantedPeriodFrames) / float(Rate) : Budget;
    Metrics.CallbackLoad = Budget     > 0.0f ? Metrics.LastCallbackMicros / Budget     : 0.0f;
    Metrics.PeakLoad     = PeakBudget > 0.0f ? Metrics.PeakCallbackMicros / PeakBudget : 0.0f;

    // Device events
    const uint32_t Stops    = StopSignals.load(std::memory_order_relaxed);
    const uint32_t Reroutes = RerouteSignals.load(std::memory_order_relaxed);
    if (Reroutes != ReroutesFolded) { Metrics.RerouteCount += Reroutes - ReroutesFolded; ReroutesFolded = Reroutes; }
    if (Stops != StopsFolded)
    {
        Metrics.StopCount += Stops - StopsFolded;
        StopsFolded = Stops;
        // A stop we did not ask for (Close never reaches here with a live device) is a loss: schedule a reopen.
        if (IsOpen() && ma_device_get_state(Device) != ma_device_state_started)
        {
            SignalIntegrator* Integrator = Attached.load(std::memory_order_acquire);
            TearDown();
            Attached.store(Integrator, std::memory_order_release);   // keep the integrator for the reopen
            LossPending   = true;
            ReopenBackoff = ReopenDelay;
        }
    }

    // Reopen with back-off
    if (LossPending)
    {
        ReopenBackoff -= Δτ;
        if (ReopenBackoff <= 0.0f)
        {
            std::string Error;
            if (BringUp(&Error))
            {
                LossPending = false;
                Metrics.ReopenCount += 1u;
                if (SignalIntegrator* Integrator = Attached.load(std::memory_order_acquire))
                    Integrator->Prepare(Metrics.GrantedSampleRate, Configuration.ChannelCount);
            }
            else
            {
                ReopenBackoff = ReopenDelay;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   OFFLINE RENDER
//------------------------------------------------------------------------------------------------------------------------

void AudioExchange::RenderOffline(SignalIntegrator& Integrator, uint32_t SampleRate, uint32_t ChannelCount, uint32_t RenderSliceFrames,
                                  double Seconds, float MasterGain, std::vector<float>& Output) noexcept
{
    const uint32_t Stride = std::clamp(RenderSliceFrames, 1u, ScratchFrames);
    const uint64_t Frames = uint64_t(std::llround(Seconds * double(SampleRate)));
    Output.assign(size_t(Frames) * ChannelCount, 0.0f);

    Integrator.Prepare(SampleRate, ChannelCount);

    uint64_t Done = 0u;
    while (Done < Frames)
    {
        const uint32_t Slice = uint32_t(std::min<uint64_t>(Stride, Frames - Done));
        float* Dst = Output.data() + size_t(Done) * ChannelCount;
        Integrator.Render(Dst, Slice);   // Output was zero-filled by assign — same contract as the realtime scratch
        if (MasterGain != 1.0f)
            for (size_t I = 0, N = size_t(Slice) * ChannelCount; I < N; ++I) Dst[I] *= MasterGain;
        Done += Slice;
    }
}

} // namespace Frontier
