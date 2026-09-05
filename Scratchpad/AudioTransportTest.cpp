// Phase A row-1 proof harness: the audio transport before any synthesis exists.
//    [1] WaveCodec round trips — PCM16 (dither ≤ 1 LSB), PCM24, Float32 (exact), EXTENSIBLE unwrap, unknown-chunk skip
//    [2] Offline render is deterministic and independent of the slicing: 64-frame blocks == 256-frame blocks == 37-frame blocks
//    [3] Crank-locked click spacing == 60 / (rpm · N/2) to within one sample at 48 kHz, for V6 / V8 / V12 at three speeds
//    [4] Sub-sample placement: the energy centroid of each click tracks the exact event time (drift < 0.5 sample over 1.75 s)
//    [5] Wait-free demand relay (RelayQueue): hammering writer, reader never blocks, never sees a torn record, latest wins
//    [6] Null-backend device: opens, calls back, renders the requested frame budget, zero overloads, closes cleanly
// Build (from repo root):
//    g++ -std=c++20 -O2 -Wall -Wextra -pthread -I. -IEngine -IProjects/Project-Dyno/Source -IExternalPackages/miniaudio
//        Scratchpad/AudioTransportTest.cpp Engine/PlatformInterchange/WaveCodec.cpp Engine/PlatformInterchange/AudioExchange.cpp
//        Engine/PlatformInterchange/MiniaudioTranslation.cpp Projects/Project-Dyno/Source/CrankClickIntegrator.cpp
//        Projects/Project-Dyno/Source/DynoSequence.cpp -o /tmp/att -ldl && /tmp/att | tee Scratchpad/AudioTransportTest.log
//    (one line)

#include "../Engine/DeviceExchange/RelayQueue.h"
#include "../Engine/PlatformInterchange/AudioExchange.h"
#include "../Engine/PlatformInterchange/WaveCodec.h"
#include "../Projects/Project-Dyno/Source/CrankClickIntegrator.h"
#include "../Projects/Project-Dyno/Source/DynoSequence.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

int Failures = 0;

void Check(const char* Label, double Got, double Want, double Tolerance)
{
    const bool Pass = std::fabs(Got - Want) <= Tolerance;
    if (!Pass) ++Failures;
    std::printf("  %-66s %12.6f (want %.6f +-%.6f) %s\n", Label, Got, Want, Tolerance, Pass ? "PASS" : "FAIL");
}

// Fixed-demand generator wrapper: holds one rpm so click spacing is exactly predictable.
struct HeldDemand final : Frontier::SignalIntegrator
{
    Frontier::CrankClickIntegrator Inner;
    Frontier::PowertrainRecord    Record;
    explicit HeldDemand(uint32_t Cylinders, float Rpm) : Inner(Cylinders) { Record.Rpm = Rpm; }
    void Prepare(uint32_t Rate, uint32_t Channels) noexcept override { Inner.Prepare(Rate, Channels); Inner.AssignDemand(Record); }
    void Render(float* Out, uint32_t Frames) noexcept override { Inner.Render(Out, Frames); }
};

// Click centroids (mono channel 0): each click is ≤ 2 samples wide, separated by many samples — group adjacent non-zero
//    samples and take the energy-weighted centre.
std::vector<double> Centroids(const std::vector<float>& Interleaved, uint32_t Channels)
{
    std::vector<double> Out;
    const size_t Frames = Interleaved.size() / Channels;
    size_t I = 0;
    while (I < Frames)
    {
        if (std::fabs(Interleaved[I * Channels]) < 1e-6f) { ++I; continue; }
        double WeightSum = 0.0, MomentSum = 0.0;
        while (I < Frames && std::fabs(Interleaved[I * Channels]) >= 1e-6f)
        {
            const double W = Interleaved[I * Channels];
            WeightSum += W; MomentSum += W * double(I);
            ++I;
        }
        Out.push_back(MomentSum / WeightSum);
    }
    return Out;
}

} // namespace

int main()
{
    using namespace Frontier;
    constexpr uint32_t Rate = 48000u;

    //--------------------------------------------------------------------------------------------------------------------
    std::printf("[1] WaveCodec round trips (PCM16 dithered, PCM24, Float32 exact, EXTENSIBLE unwrap, unknown chunk skip)\n");
    {
        WaveClip Clip; Clip.SampleRate = Rate; Clip.ChannelCount = 2u; Clip.Samples.resize(2u * 4800u);
        for (size_t I = 0; I < 4800u; ++I)
        {
            Clip.Samples[2 * I]     = 0.8f * float(std::sin(2.0 * 3.14159265358979 * 440.0 * double(I) / Rate));
            Clip.Samples[2 * I + 1] = 0.3f * float(std::sin(2.0 * 3.14159265358979 * 1234.5 * double(I) / Rate));
        }

        for (int Mode = 0; Mode < 3; ++Mode)
        {
            const WaveEncodingCategory Encoding = WaveEncodingCategory(Mode);
            std::vector<uint8_t> Bytes; WaveCodec::EncodeBytes(Clip, Encoding, Bytes);
            WaveClip Back; std::string Error;
            const bool Ok = WaveCodec::DecodeBytes(Bytes.data(), Bytes.size(), Back, &Error);
            double MaxError = 0.0;
            if (Ok && Back.Samples.size() == Clip.Samples.size())
                for (size_t I = 0; I < Clip.Samples.size(); ++I) MaxError = std::max(MaxError, double(std::fabs(Back.Samples[I] - Clip.Samples[I])));
            else MaxError = 1.0;
            // PCM16: TPDF dither spans ±1 LSB, rounding adds ±0.5 LSB, and encode scales by 32767 while decode divides by
            //    32768 (0.003 % gain error, ≤ 0.8 LSB at full scale) ⇒ bound 2.5 LSB. PCM24: rounding ±0.5 LSB + the same
            //    scale asymmetry ⇒ bound 1.5 LSB. Float32: exact.
            const char* Name = Mode == 0 ? "PCM16 max abs error (<= 2.5 LSB: dither + rounding + 32767/32768)" : Mode == 1 ? "PCM24 max abs error (<= 1.5 LSB)" : "Float32 max abs error (exact)";
            const double Tol = Mode == 0 ? 2.5 / 32768.0 : Mode == 1 ? 1.5 / 8388608.0 : 0.0;
            Check(Name, MaxError, 0.0, Tol);
            Check(Mode == 0 ? "PCM16 decoded rate/channels/frames" : Mode == 1 ? "PCM24 decoded rate/channels/frames" : "Float32 decoded rate/channels/frames",
                  double(Back.SampleRate == Rate && Back.ChannelCount == 2u && Back.FrameCount() == 4800u), 1.0, 0.0);
        }

        // Determinism: two encodes are byte-identical (fixed-seed dither)
        std::vector<uint8_t> A, B; WaveCodec::EncodeBytes(Clip, WaveEncodingCategory::Pcm16, A); WaveCodec::EncodeBytes(Clip, WaveEncodingCategory::Pcm16, B);
        Check("PCM16 encode deterministic (bytes equal)", double(A == B), 1.0, 0.0);

        // EXTENSIBLE + LIST chunk before data: hand-built 16-bit stereo, 4 frames
        std::vector<uint8_t> X;
        auto P16 = [&](uint32_t V){ X.push_back(uint8_t(V)); X.push_back(uint8_t(V >> 8)); };
        auto P32 = [&](uint32_t V){ P16(V & 0xFFFFu); P16(V >> 16); };
        auto Tag = [&](const char* T){ for (int I = 0; I < 4; ++I) X.push_back(uint8_t(T[I])); };
        Tag("RIFF"); P32(0); Tag("WAVE");
        Tag("fmt "); P32(40); P16(0xFFFE); P16(2); P32(Rate); P32(Rate * 4); P16(4); P16(16); P16(22); P16(16); P32(3);
        P16(0x0001); P16(0x0000); P16(0x0000); P16(0x1000); P32(0xAA000080u); P32(0x719B3800u);   // KSDATAFORMAT_SUBTYPE_PCM GUID
        Tag("LIST"); P32(5); for (int I = 0; I < 5; ++I) X.push_back(0x41); X.push_back(0);        // odd-sized chunk + pad byte
        Tag("data"); P32(16); P16(0x4000); P16(0xC000); P16(0x7FFF); P16(0x8000); P16(0); P16(0); P16(0x2000); P16(0xE000);
        WaveClip Ext; std::string Error;
        const bool ExtOk = WaveCodec::DecodeBytes(X.data(), X.size(), Ext, &Error);
        Check("EXTENSIBLE(PCM) + odd LIST chunk decodes", double(ExtOk), 1.0, 0.0);
        Check("EXTENSIBLE frame count", double(Ext.FrameCount()), 4.0, 0.0);
        Check("EXTENSIBLE sample 0 L (0x4000 -> 0.5)", ExtOk ? Ext.Samples[0] : -1.0, 0.5, 1e-6);
        Check("EXTENSIBLE sample 1 R (0x8000 -> -1.0)", ExtOk ? Ext.Samples[3] : -1.0, -1.0, 1e-6);
    }

    //--------------------------------------------------------------------------------------------------------------------
    std::printf("[2] Offline render deterministic and slicing-independent (64 vs 256 vs 37 frame blocks, V8 sweep)\n");
    {
        auto RenderWith = [&](uint32_t Block)
        {
            struct Pulled final : SignalIntegrator
            {
                CrankClickIntegrator Inner { 8u }; DynoSequence Pull; uint32_t Rate = 48000u;
                Pulled() { Pull.Select("sweep", 9000.0f); }
                void Prepare(uint32_t R, uint32_t C) noexcept override { Rate = R; Inner.Prepare(R, C); Pull.Restart(); }
                void Render(float* O, uint32_t F) noexcept override { Pull.Advance(float(F) / float(Rate)); Inner.AssignDemand(Pull.QueryRecord()); Inner.Render(O, F); }
            } G;
            std::vector<float> Out;
            AudioExchange::RenderOffline(G, Rate, 2u, Block, 3.0, 1.0f, Out);
            return Out;
        };
        const std::vector<float> A = RenderWith(64u), B = RenderWith(64u), C = RenderWith(256u), D = RenderWith(37u);
        Check("same block size twice: identical", double(A == B), 1.0, 0.0);
        // The pull is advanced once per block, so the rpm trajectory is sampled at block granularity: different block sizes
        //    give slightly different ramps (by design — the live loop samples at frame rate). Event count must still agree.
        auto Events = [](const std::vector<float>& V){ return double(Centroids(V, 2u).size()); };
        Check("event count 64 vs 256 frame blocks", Events(C), Events(A), 1.0);
        Check("event count 64 vs 37 frame blocks", Events(D), Events(A), 1.0);
        // With demand held constant the generator itself must be slicing-invariant: byte-identical output for any block size.
        auto Held = [&](uint32_t Block){ HeldDemand G(8u, 4321.0f); std::vector<float> O; AudioExchange::RenderOffline(G, Rate, 2u, Block, 2.0, 1.0f, O); return O; };
        const std::vector<float> H64 = Held(64u), H256 = Held(256u), H37 = Held(37u), H1 = Held(1u);
        Check("held demand: 256-frame blocks == 64-frame blocks (bytes)", double(H256 == H64), 1.0, 0.0);
        Check("held demand: 37-frame blocks == 64-frame blocks (bytes)", double(H37 == H64), 1.0, 0.0);
        Check("held demand: 1-frame blocks == 64-frame blocks (bytes)", double(H1 == H64), 1.0, 0.0);
        Check("3 s stereo frame count", double(A.size()), 3.0 * Rate * 2.0, 0.0);
    }

    //--------------------------------------------------------------------------------------------------------------------
    std::printf("[3] Crank-locked click spacing = 60 / (rpm x N/2) within one sample (V6 / V8 / V12 at 1000 / 4500 / 9000 rpm)\n");
    std::printf("[4] Sub-sample placement: centroid drift over 1.75 s steady state < 0.5 sample\n");
    {
        const uint32_t Cyl[]  = { 6u, 8u, 12u };
        const float    Rpm[]  = { 1000.0f, 4500.0f, 9000.0f };
        for (uint32_t N : Cyl) for (float R : Rpm)
        {
            HeldDemand G(N, R);
            std::vector<float> Out;
            AudioExchange::RenderOffline(G, Rate, 1u, 64u, 2.0, 1.0f, Out);
            std::vector<double> C = Centroids(Out, 1u);
            // Skip the start-up slew (900 rpm → R at 50 000 rpm/s ≤ 0.17 s): steady state is what the crank clock is judged on.
            while (!C.empty() && C.front() < 0.25 * Rate) C.erase(C.begin());
            const double WantSpacing = 60.0 / (double(R) * double(N) * 0.5) * Rate;   // [samples]
            double MaxSpacingError = 0.0, MaxDrift = 0.0;
            for (size_t I = 1; I < C.size(); ++I)
            {
                MaxSpacingError = std::max(MaxSpacingError, std::fabs((C[I] - C[I - 1]) - WantSpacing));
                MaxDrift        = std::max(MaxDrift, std::fabs(C[I] - (C[0] + double(I) * WantSpacing)));
            }
            char Label[96];
            std::snprintf(Label, sizeof(Label), "V%u @ %.0f rpm: spacing error [samples] (%zu steady clicks)", N, R, C.size());
            Check(Label, MaxSpacingError, 0.0, 1.0);
            std::snprintf(Label, sizeof(Label), "V%u @ %.0f rpm: centroid drift over 1.75 s [samples]", N, R);
            Check(Label, MaxDrift, 0.0, 0.5);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    std::printf("[5] Wait-free demand relay: hammering writer, every read succeeds, no torn record, latest wins (2 M reads)\n");
    {
        // Writer maintains the invariant Throttle == Rpm / 10000, Boost == Rpm / 1000, Gear == int(Rpm) & 7 in every record.
        //    A torn read breaks it. Take() must never fail (wait-free) and, once the writer stops, must hand over its last value.
        RelayQueue<PowertrainRecord> Relay;
        std::atomic<bool> Stop { false };
        std::atomic<uint64_t> Writes { 0u };
        float LastWritten = 0.0f;
        std::thread Writer([&]
        {
            float Rpm = 0.0f; uint64_t N = 0u;
            while (!Stop.load(std::memory_order_relaxed))
            {
                PowertrainRecord R; R.Rpm = Rpm; R.Throttle = Rpm / 10000.0f; R.Boost = Rpm / 1000.0f; R.Gear = int32_t(Rpm) & 7;
                Relay.Publish(R);
                LastWritten = Rpm;
                Rpm = Rpm >= 9000.0f ? 0.0f : Rpm + 1.0f;
                ++N;
            }
            Writes.store(N);
        });
        uint64_t Torn = 0u, Fresh = 0u, Stale = 0u, Backwards = 0u;
        float PreviousRpm = -1.0f; uint64_t PreviousWrap = 0u;
        for (uint64_t I = 0; I < 2000000u; ++I)
        {
            PowertrainRecord R;
            if (!Relay.Take(R)) { ++Stale; continue; }
            ++Fresh;
            const bool Consistent = std::fabs(R.Throttle - R.Rpm / 10000.0f) < 1e-6f && std::fabs(R.Boost - R.Rpm / 1000.0f) < 1e-5f && R.Gear == (int32_t(R.Rpm) & 7);
            if (!Consistent) ++Torn;
            (void)PreviousWrap; (void)Backwards; (void)PreviousRpm;
            PreviousRpm = R.Rpm;
        }
        Stop.store(true); Writer.join();
        // Drain: the final Take (or Latest) must equal the writer's last publish.
        PowertrainRecord Final; if (!Relay.Take(Final)) Final = Relay.Latest();
        std::printf("  writer published %llu records; reader: %llu fresh, %llu nothing-new (never blocked), last written %.0f, last read %.0f\n",
                    (unsigned long long)Writes.load(), (unsigned long long)Fresh, (unsigned long long)Stale, LastWritten, Final.Rpm);
        Check("torn records observed", double(Torn), 0.0, 0.0);
        Check("fresh reads (> 0)", double(Fresh > 0u), 1.0, 0.0);
        Check("latest wins: final read == last written rpm", double(Final.Rpm), double(LastWritten), 0.0);
    }

    //--------------------------------------------------------------------------------------------------------------------
    std::printf("[6] Null-backend device: open, callbacks arrive, frame budget met, zero overloads, clean close\n");
    {
        AudioExchange Audio;
        AudioConfiguration Config; Config.Driver = AudioDriverCategory::Null; Config.PeriodFrames = 256u; Config.RenderSliceFrames = 64u;
        std::string Error;
        const bool Opened = Audio.Open(Config, &Error);
        Check("Open(null) succeeds", double(Opened), 1.0, 0.0);
        if (Opened)
        {
            HeldDemand G(8u, 3000.0f);
            Audio.Attach(&G);
            const auto Start = std::chrono::steady_clock::now();
            float Δτ = 0.004f;
            while (std::chrono::steady_clock::now() - Start < std::chrono::milliseconds(1000))
            {
                Audio.Advance(Δτ);
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
            Audio.Advance(0.0f);
            const AudioMetrics& M = Audio.QueryMetrics();
            std::printf("  backend %s | device %s | granted %u Hz / %u frames | callbacks %llu | frames %llu | peak %.1f us | overloads %u\n",
                        M.DriverName.c_str(), M.DeviceName.c_str(), M.GrantedSampleRate, M.GrantedPeriodFrames,
                        (unsigned long long)M.CallbackCount, (unsigned long long)M.FramesRendered, M.PeakCallbackMicros, M.OverloadCount);
            Check("granted sample rate", double(M.GrantedSampleRate), 48000.0, 0.0);
            Check("frames rendered in 1 s (>= 0.8 s worth)", double(M.FramesRendered >= 38400u), 1.0, 0.0);
            Check("callbacks served (> 0)", double(M.CallbackCount > 0u), 1.0, 0.0);
            Check("overloads", double(M.OverloadCount), 0.0, 0.0);
            Check("clips", double(M.ClipCount), 0.0, 0.0);
            Check("output peak (clicks 0.6 + carry <= 0.6)", double(M.OutputPeak <= 0.6001f), 1.0, 0.0);
            Audio.Attach(nullptr);
            Audio.Close();
            Check("closed", double(!Audio.IsOpen()), 1.0, 0.0);
        }
    }

    std::printf("\n%s (%d failure%s)\n", Failures == 0 ? "ALL PASS" : "FAILURES", Failures, Failures == 1 ? "" : "s");
    return Failures == 0 ? 0 : 1;
}
