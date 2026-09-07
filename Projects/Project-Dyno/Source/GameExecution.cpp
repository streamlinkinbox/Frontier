//============================================================================================================================================
//                                                       GAMEEXECUTION.CPP
//============================================================================================================================================
// 🧩 Project-Dyno — the windowless dyno cell. No Vulkan, no GLFW, no ImGui: a console main loop that drives the audio
//    transport exactly the way Project-Zero's loop will after the merge (Pull.Advance → generator demand → Audio.Advance).
//
//    Usage
//        Project-Dyno                                          live: default device, LaFerrari, "sweep" pull, loops until Enter / Ctrl-C
//        Project-Dyno --car Porsche918Spyder --pull pull       live: one of Porsche918Spyder | FerrariLaFerrari | NissanGtrNismo | KoenigseggAgeraR | DodgeDemon, or a path to a .toml
//        Project-Dyno --pull overrun                           pulls: idle | sweep | pull | steady | blip | overrun | limiter
//        Project-Dyno --free                                   live: free rev — Space (hold) = throttle, Enter = stop (FreeRevPowertrain, as the editor)
//        Project-Dyno --listener cockpit                       listener preset: trackside (default) | chase | cockpit
//        Project-Dyno --pure                                   pure-tone mode (voice only: no noise, transients, post chain) — the order-diagram proof mode
//        Project-Dyno --seed 12345                             xorshift32 seed (default 0x5EED1234 — two runs of the same arguments are identical)
//        Project-Dyno --null                                   null driver (no hardware): clocked, silent — CI / sandbox
//        Project-Dyno --render out.wav --pull pull             offline: same voice, same slicing, written to WAV; no device
//        Project-Dyno --seconds 8                              run / render length in seconds (0 = until Enter live, = pull length for --render)
//        Project-Dyno --float                                  write IEEE float32 instead of PCM16 (bit-identity proofs)
//        Project-Dyno --slice 128                              render slice in frames (1 … 256; default 64 — the output does not depend on it, the proofs check that)
//        Project-Dyno --save out.toml --car …                  write the structure the binary loaded (AcousticStructure::Save) and exit — the editor's Export twin
//        Project-Dyno --clicks [--cylinders 8] [--sweep]       the row-A1 click train instead of the voice (transport self-test)
//
//    Row A2: AcousticIntegrator + the car archive (EngineContent/AudioArchives/<Car>/<Car>.toml) replace the click train;
//    the [Audio] line every 2 s carries the voice's firing / pop / clip counters, the rpm it is playing and the boost.

#include "../../../Engine/DeviceExchange/DiagnosticMetrics.h"
#include "../../../Engine/PlatformInterchange/AcousticIntegrator.h"
#include "../../../Engine/PlatformInterchange/AcousticStructure.h"
#include "../../../Engine/PlatformInterchange/AudioExchange.h"
#include "../../../Engine/PlatformInterchange/WaveCodec.h"
#include "CrankClickIntegrator.h"
#include "DynoSequence.h"
#include "FreeRevPowertrain.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <conio.h>
#include <windows.h>
#else
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONSOLE KEYS
//------------------------------------------------------------------------------------------------------------------------

// Enter on stdin ends a live run; a detached reader keeps the main loop free of blocking I/O. Ctrl-C still works as usual.
std::atomic<bool> StopRequested { false };

void WatchStandardInput()
{
    std::thread([]
    {
        std::string Line;
        if (std::getline(std::cin, Line) || std::cin.eof()) StopRequested.store(true, std::memory_order_relaxed);
    }).detach();
}

// --free: the throttle is the Space bar, held. Windows reads the key state directly (no console mode games); elsewhere the
//    loop reads stdin bytes without echo — a run of spaces holds the pedal, silence for 0.15 s lifts it.
class ThrottleKey
{
public:
    void Open() noexcept
    {
#if !defined(_WIN32)
        if (!isatty(0) || tcgetattr(0, &Saved) != 0) return;
        termios Raw = Saved;
        Raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON);
        Raw.c_cc[VMIN] = 0; Raw.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &Raw) == 0) RawMode = true;
#endif
    }
    void Close() noexcept
    {
#if !defined(_WIN32)
        if (RawMode) { tcsetattr(0, TCSANOW, &Saved); RawMode = false; }
#endif
    }
    // 0 … 1 pedal for this frame; sets Quit on Enter / Escape / q.
    double Poll(double Δτ, bool& Quit) noexcept
    {
#if defined(_WIN32)
        (void)Δτ;
        if (_kbhit()) { const int C = _getch(); if (C == '\r' || C == 27 || C == 'q') Quit = true; }
        return (GetAsyncKeyState(VK_SPACE) & 0x8000) ? 1.0 : 0.0;
#else
        if (!RawMode) { return 0.0; }
        char Bytes[64]; bool Pressed = false;
        pollfd Fd { 0, POLLIN, 0 };
        while (poll(&Fd, 1, 0) > 0 && (Fd.revents & POLLIN))
        {
            const ssize_t N = read(0, Bytes, sizeof(Bytes));
            if (N <= 0) break;
            for (ssize_t I = 0; I < N; ++I) { if (Bytes[I] == ' ') Pressed = true; else if (Bytes[I] == '\n' || Bytes[I] == 27 || Bytes[I] == 'q') Quit = true; }
        }
        SinceKey = Pressed ? 0.0 : SinceKey + Δτ;
        return SinceKey < 0.15 ? 1.0 : 0.0;
#endif
    }

private:
    bool   RawMode  = false;
    double SinceKey = 1.0;   // [s]
#if !defined(_WIN32)
    termios Saved {};
#endif
};

const char* Argument(int argc, char** argv, const char* Name, const char* Fallback = nullptr)
{
    for (int I = 1; I + 1 < argc; ++I)
        if (std::strcmp(argv[I], Name) == 0) return argv[I + 1];
    return Fallback;
}

bool Switch(int argc, char** argv, const char* Name)
{
    for (int I = 1; I < argc; ++I)
        if (std::strcmp(argv[I], Name) == 0) return true;
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CAR ARCHIVE
//------------------------------------------------------------------------------------------------------------------------

// --car takes a key (folder name under EngineContent/AudioArchives) or a path. The archive folder is looked for next to the
//    working directory and up to four levels above it (Build/Output/<OS>/<Config>/Binary is five deep from the repository root).
std::string LocateArchive(const std::string& Car)
{
    namespace fs = std::filesystem;
    if (Car.size() > 5u && Car.compare(Car.size() - 5u, 5u, ".toml") == 0) return Car;
    fs::path Folder = fs::current_path();
    for (int Up = 0; Up < 6; ++Up)
    {
        const fs::path Candidate = Folder / "EngineContent" / "AudioArchives" / Car / (Car + ".toml");
        std::error_code Ec;
        if (fs::exists(Candidate, Ec)) return Candidate.string();
        if (!Folder.has_parent_path() || Folder.parent_path() == Folder) break;
        Folder = Folder.parent_path();
    }
    return "EngineContent/AudioArchives/" + Car + "/" + Car + ".toml";
}

void ReportMetrics(Frontier::DiagnosticMetrics& Logger, const Frontier::AudioMetrics& M, const Frontier::AcousticReadout* Voice)
{
    char Line[768];
    int N = std::snprintf(Line, sizeof(Line),
                          "%s | %s | %u Hz | period %u frames | callbacks %llu | frames %llu | last %.1f us (%.2f load) | peak %.1f us (%.2f load) | overloads %u | clips %u | stops %u | reroutes %u | reopens %u",
                          M.DriverName.c_str(), M.DeviceName.c_str(), M.GrantedSampleRate, M.GrantedPeriodFrames,
                          static_cast<unsigned long long>(M.CallbackCount), static_cast<unsigned long long>(M.FramesRendered),
                          M.LastCallbackMicros, M.CallbackLoad, M.PeakCallbackMicros, M.PeakLoad,
                          M.OverloadCount, M.ClipCount, M.StopCount, M.RerouteCount, M.ReopenCount);
    if (Voice && N > 0 && N < int(sizeof(Line)))
        std::snprintf(Line + N, sizeof(Line) - size_t(N), " | voice %.0f rpm thr %.2f load %.2f%s | firings %llu | pops %u | clipped %u | dropped %u | boost %.2f bar",
                      Voice->Rpm, Voice->Throttle, Voice->Load, Voice->FuelOn ? "" : " (fuel cut)", static_cast<unsigned long long>(Voice->Firings),
                      Voice->Pops, Voice->Clipped, Voice->Dropped, Voice->Boost);
    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Audio", Line);
}

} // namespace

int main(int argc, char** argv)
{
    const char* RenderPath   = Argument(argc, argv, "--render");
    const char* SavePath     = Argument(argc, argv, "--save");
    const uint32_t SliceFrames = static_cast<uint32_t>(std::atoi(Argument(argc, argv, "--slice", "64")));
    const char* PullName     = Argument(argc, argv, "--pull", "sweep");
    const char* CarName      = Argument(argc, argv, "--car", "FerrariLaFerrari");
    const char* ListenerName = Argument(argc, argv, "--listener", "trackside");
    const uint32_t Cylinders = static_cast<uint32_t>(std::atoi(Argument(argc, argv, "--cylinders", "8")));
    const float    Seconds   = static_cast<float>(std::atof(Argument(argc, argv, "--seconds", "0")));
    const float    Redline   = static_cast<float>(std::atof(Argument(argc, argv, "--redline", "9000")));
    const uint32_t Seed      = static_cast<uint32_t>(std::strtoul(Argument(argc, argv, "--seed", "0x5EED1234"), nullptr, 0));
    const bool     UseNull   = Switch(argc, argv, "--null");
    const bool     UseSweep  = Switch(argc, argv, "--sweep");
    const bool     UseFloat  = Switch(argc, argv, "--float");
    const bool     UseClicks = Switch(argc, argv, "--clicks");
    const bool     UseFree   = Switch(argc, argv, "--free") && !RenderPath;
    const bool     UsePure   = Switch(argc, argv, "--pure");

    //──────────────────────────────────────────────────────────────────────────
    // Telemetry sink (same shape as Project-Zero's)
    //──────────────────────────────────────────────────────────────────────────
    Frontier::DiagnosticConfiguration DiagnosticConfig{};
    DiagnosticConfig.DestinationFolder          = "Diagnostics";
    DiagnosticConfig.OutputFileStem             = "ProjectDyno_TelemetryReport";
    DiagnosticConfig.FileExtension              = ".md";
    DiagnosticConfig.TimestampPrefixEnabled     = true;
    DiagnosticConfig.ConsoleEchoEnabled         = true;
    DiagnosticConfig.MarkdownTableFormatEnabled = true;

    Frontier::DiagnosticMetrics Logger(DiagnosticConfig);
    if (!Logger.InitializeSink())
        std::cerr << "[Project-Dyno] Telemetry sink could not be opened; continuing with console output only.\n";
    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Bootstrap", "Project-Dyno dyno cell starting.");

    //──────────────────────────────────────────────────────────────────────────
    // The car: AcousticStructure from the archive (or the click train behind --clicks)
    //──────────────────────────────────────────────────────────────────────────
    Frontier::AcousticStructure Structure;
    if (!UseClicks)
    {
        const std::string Path = LocateArchive(CarName);
        std::string Error; uint32_t Unknown = 0u;
        if (!Frontier::AcousticStructure::Load(Path, Structure, &Error, &Unknown))
        {
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Fatal, "Car", (std::string("cannot load '") + Path + "': " + Error).c_str());
            Logger.TerminateSink();
            return 1;
        }
        char Line[512];
        std::snprintf(Line, sizeof(Line), "%s — %s (%d cylinders, idle %.0f, redline %.0f rpm, %s voice%s) from %s%s", Structure.Vehicle.Name, Structure.Vehicle.Engine,
                      Structure.Vehicle.CylinderCount, Structure.Vehicle.IdleRpm, Structure.Vehicle.RedlineRpm, Structure.Voice.CrankPulse ? "rev-3 crank-pulse" : "rev-2",
                      Structure.Turbo.Enabled ? ", turbo" : "", Path.c_str(), Unknown ? " ⚠️ unknown keys ignored" : "");
        Logger.RecordMessage(Unknown ? Frontier::DiagnosticSeverity::Warning : Frontier::DiagnosticSeverity::Information, "Car", Line);
        if (Unknown) { std::snprintf(Line, sizeof(Line), "%u unknown key(s) in the archive — newer editor than binary? They were ignored.", Unknown); Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Car", Line); }
    }
    if (SavePath && !UseClicks)
    {
        const char* Header[4] = { nullptr, "saved by Project-Dyno --save (AcousticStructure::Save) — the editor's Export writes the same body", nullptr, "units: [Hz] [rpm] [m] [mm] [s] [K] [bar] [°] [-]" };
        const std::string Title = std::string("Frontier acoustic structure — ") + Structure.Vehicle.Name, LoadPath = std::string("load path: EngineContent/AudioArchives/") + CarName + "/" + CarName + ".toml";
        Header[0] = Title.c_str(); Header[2] = LoadPath.c_str();
        std::string Error;
        const bool Saved = Frontier::AcousticStructure::Save(SavePath, Structure, Header, 4u, &Error);
        Logger.RecordMessage(Saved ? Frontier::DiagnosticSeverity::Information : Frontier::DiagnosticSeverity::Fatal, "Car", Saved ? (std::string("saved ") + SavePath).c_str() : Error.c_str());
        Logger.TerminateSink();
        return Saved ? 0 : 1;
    }
    Frontier::ListenerPreset Listener = Frontier::ListenerPreset::Trackside;
    if (!Frontier::ParseListenerPreset(ListenerName, Listener))
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Car", (std::string("Unknown listener '") + ListenerName + "' — using trackside").c_str());

    //──────────────────────────────────────────────────────────────────────────
    // Powertrain stand-ins: the voice, the click train, the scripted pull, the free rev
    //──────────────────────────────────────────────────────────────────────────
    // The voice is ≈ 370 KB of kernels, sheets and scope — on the heap, not the 1 MB Windows main-thread stack. Built once here;
    //    the realtime side never allocates.
    std::unique_ptr<Frontier::AcousticIntegrator> VoiceOwner = std::make_unique<Frontier::AcousticIntegrator>(Structure, Seed);
    Frontier::AcousticIntegrator& Voice = *VoiceOwner;
    Voice.AssignListener(Listener);
    Voice.AssignPureTone(UsePure);
    Frontier::CrankClickIntegrator Clicks(Cylinders == 0u ? 8u : Cylinders);
    Clicks.AssignSweep(UseSweep);
    Frontier::SignalIntegrator& Powertrain = UseClicks ? static_cast<Frontier::SignalIntegrator&>(Clicks) : static_cast<Frontier::SignalIntegrator&>(Voice);

    const double IdleRpm    = UseClicks ? 900.0 : Structure.Vehicle.IdleRpm;
    const double RedlineRpm = UseClicks ? double(Redline) : Structure.Vehicle.RedlineRpm;
    Frontier::DynoSequence Pull;
    if (!Pull.Select(PullName, RedlineRpm, IdleRpm))
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Dyno", (std::string("Unknown pull '") + PullName + "' — using sweep").c_str());
    Frontier::FreeRevPowertrain FreeRev(Structure.Vehicle);
    {
        char Line[256];
        if (UseFree) std::snprintf(Line, sizeof(Line), "free rev (hold Space = throttle, Enter = stop), %s listener%s, seed 0x%08X", ListenerName, UsePure ? ", pure tone" : "", Seed);
        else if (UseClicks) std::snprintf(Line, sizeof(Line), "pull '%s' (%.1f s), click train, %u cylinders, redline %.0f rpm%s", std::string(Pull.QueryName()).c_str(), Pull.QueryDuration(), Cylinders == 0u ? 8u : Cylinders, Redline, UseSweep ? ", sine sweep on" : "");
        else std::snprintf(Line, sizeof(Line), "pull '%s' (%.1f s), %s listener%s, seed 0x%08X", std::string(Pull.QueryName()).c_str(), Pull.QueryDuration(), ListenerName, UsePure ? ", pure tone" : "", Seed);
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Dyno", Line);
    }

    // Demand for this frame: the scripted pull with the dyno's load rule, or the free rev. Both sides (voice / clicks) take the same record.
    double ScriptTime = 0.0;
    auto Demand = [&](double Δτ, double Throttle) -> Frontier::PowertrainRecord
    {
        if (UseFree) { FreeRev.Advance(Δτ, Throttle); return FreeRev.QueryRecord(); }
        Pull.Advance(Δτ);
        const Frontier::PowertrainRecord Record = UseClicks ? Pull.QueryRecord() : Frontier::DynoSequence::ScriptedRecord(Pull.QueryRpm(), Pull.QueryThrottle(), Structure.Vehicle, ScriptTime);
        ScriptTime += Δτ;
        return Record;
    };
    auto Publish = [&](const Frontier::PowertrainRecord& Record) { if (UseClicks) Clicks.AssignDemand(Record); else Voice.AssignDemand(Record); };

    Frontier::AudioConfiguration AudioConfig;
    AudioConfig.SampleRate        = 48000u;
    AudioConfig.ChannelCount      = 2u;
    AudioConfig.PeriodFrames      = 256u;
    AudioConfig.RenderSliceFrames = SliceFrames >= 1u && SliceFrames <= 256u ? SliceFrames : 64u;
    AudioConfig.Driver            = UseNull ? Frontier::AudioDriverCategory::Null : Frontier::AudioDriverCategory::Platform;

    //──────────────────────────────────────────────────────────────────────────
    // Offline render: identical voice + slicing, no device
    //──────────────────────────────────────────────────────────────────────────
    if (RenderPath)
    {
        // The pull is advanced per render slice at exactly the slice's duration — the same call sequence the JavaScript dump
        //    and the identity proof use, so the WAV is the reference render after float32 quantisation.
        struct PulledIntegrator final : Frontier::SignalIntegrator
        {
            Frontier::SignalIntegrator& Inner;
            decltype(Demand)&           NextDemand;
            decltype(Publish)&          Send;
            Frontier::DynoSequence&     Pull;
            uint32_t                    Rate = 48000u;
            PulledIntegrator(Frontier::SignalIntegrator& G, decltype(Demand)& D, decltype(Publish)& P, Frontier::DynoSequence& S) : Inner(G), NextDemand(D), Send(P), Pull(S) { }
            void Prepare(uint32_t SampleRate, uint32_t ChannelCount) noexcept override { Rate = SampleRate; Inner.Prepare(SampleRate, ChannelCount); Pull.Restart(); }
            void Render(float* Output, uint32_t FrameCount) noexcept override
            {
                Send(NextDemand(double(FrameCount) / double(Rate), 0.0));
                Inner.Render(Output, FrameCount);
            }
        } Pulled(Powertrain, Demand, Publish, Pull);

        Frontier::WaveClip Clip;
        Clip.SampleRate   = AudioConfig.SampleRate;
        Clip.ChannelCount = AudioConfig.ChannelCount;
        const double Length = Seconds > 0.0f ? Seconds : Pull.QueryDuration();
        Frontier::AudioExchange::RenderOffline(Pulled, Clip.SampleRate, Clip.ChannelCount, AudioConfig.RenderSliceFrames, Length, AudioConfig.MasterGain, Clip.Samples);

        std::string Error;
        const bool Written = Frontier::WaveCodec::Encode(RenderPath, Clip, UseFloat ? Frontier::WaveEncodingCategory::Float32 : Frontier::WaveEncodingCategory::Pcm16, &Error);
        char Line[512];
        if (Written)
        {
            if (UseClicks) std::snprintf(Line, sizeof(Line), "rendered %.2f s (%llu frames, %llu click events) to %s", Length, static_cast<unsigned long long>(Clip.FrameCount()), static_cast<unsigned long long>(Clicks.QueryEventCount()), RenderPath);
            else
            {
                const Frontier::AcousticReadout R = Voice.QueryReadout();
                std::snprintf(Line, sizeof(Line), "rendered %.2f s (%llu frames) to %s — firings %llu, pops %u, clipped %u, dropped %u", Length,
                              static_cast<unsigned long long>(Clip.FrameCount()), RenderPath, static_cast<unsigned long long>(R.Firings), R.Pops, R.Clipped, R.Dropped);
            }
        }
        else std::snprintf(Line, sizeof(Line), "render failed: %s", Error.c_str());
        Logger.RecordMessage(Written ? Frontier::DiagnosticSeverity::Information : Frontier::DiagnosticSeverity::Fatal, "Render", Line);
        Logger.TerminateSink();
        return Written ? 0 : 1;
    }

    //──────────────────────────────────────────────────────────────────────────
    // Live: device up, voice attached, main loop at ~240 Hz
    //──────────────────────────────────────────────────────────────────────────
    Frontier::AudioExchange Audio;
    {
        std::string Error;
        if (!Audio.Open(AudioConfig, &Error))
        {
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Fatal, "Audio", Error.c_str());
            Logger.TerminateSink();
            std::cerr << "\nProject-Dyno could not open an audio device (try --null or --render). Press Enter to close.\n";
            std::cin.get();
            return 1;
        }
    }
    Audio.Attach(&Powertrain);
    ReportMetrics(Logger, Audio.QueryMetrics(), nullptr);
    ThrottleKey Key;
    if (UseFree)
    {
        std::cout << "[Dyno] Free rev. Hold Space for throttle, Enter (or q) to stop.\n";
        Key.Open();
    }
    else if (Seconds <= 0.0f)
    {
        std::cout << "[Dyno] Running. Press Enter to stop.\n";
        WatchStandardInput();
    }

    using Clock    = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<float>;
    auto  PreviousTime = Clock::now();
    float Elapsed      = 0.0f;   // [s]
    float ReportTimer  = 0.0f;   // [s]
    double Throttle    = 0.0;    // [-] free-rev pedal

    while (!StopRequested.load(std::memory_order_relaxed))
    {
        const auto NowTime = Clock::now();
        float      Δτ      = std::chrono::duration_cast<Duration>(NowTime - PreviousTime).count();
        PreviousTime       = NowTime;
        if (Δτ > 0.1f) Δτ = 0.1f;   // same spiral-of-death clamp as Project-Zero's loop

        // ① Script (or pedal) → demand → transport housekeeping. This is the exact call sequence the game loop will carry.
        if (UseFree) { bool Quit = false; Throttle = Key.Poll(Δτ, Quit); if (Quit) break; }
        else if (Pull.Finished()) { Pull.Restart(); ScriptTime = 0.0; }
        Publish(Demand(Δτ, Throttle));
        Audio.Advance(Δτ);

        // ② Periodic health line (every 2 s) — what the F3 popup / AudioEditor device panel will show later
        Elapsed     += Δτ;
        ReportTimer += Δτ;
        if (ReportTimer >= 2.0f)
        {
            ReportTimer = 0.0f;
            const Frontier::AcousticReadout R = Voice.QueryReadout();
            ReportMetrics(Logger, Audio.QueryMetrics(), UseClicks ? nullptr : &R);
        }
        if (Seconds > 0.0f && Elapsed >= Seconds) break;

        std::this_thread::sleep_for(std::chrono::microseconds(4000));   // ~240 Hz main loop; the device thread is independent
    }
    Key.Close();

    //──────────────────────────────────────────────────────────────────────────
    // Shutdown
    //──────────────────────────────────────────────────────────────────────────
    Audio.Advance(0.0f);
    const Frontier::AcousticReadout FinalVoice = Voice.QueryReadout();
    ReportMetrics(Logger, Audio.QueryMetrics(), UseClicks ? nullptr : &FinalVoice);
    const Frontier::AudioMetrics Final = Audio.QueryMetrics();
    Audio.Attach(nullptr);
    Audio.Close();
    {
        char Line[320];
        const unsigned long long Events = UseClicks ? static_cast<unsigned long long>(Clicks.QueryEventCount()) : static_cast<unsigned long long>(FinalVoice.Firings);
        std::snprintf(Line, sizeof(Line), "%.1f s live, %llu firing events, %u overloads, %u clips (device) / %u clipped (voice), %u transients dropped — %s", Elapsed, Events,
                      Final.OverloadCount, Final.ClipCount, UseClicks ? 0u : FinalVoice.Clipped, UseClicks ? 0u : FinalVoice.Dropped,
                      (Final.OverloadCount == 0u && FinalVoice.Dropped == 0u) ? "PASS" : "CHECK");
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Shutdown", Line);
    }
    Logger.TerminateSink();
    return 0;
}
