// Phase A row A2 proof: the C++ voice against the editor's JavaScript, sample for sample.
//    [1] AcousticStructure: the three archives load with 0 unknown keys and Serialise reproduces every file byte for byte;
//        every sheet default equals the member initialiser (a structure built from "" equals AcousticStructure{})
//    [2] Identity: for every Scratchpad/Reference/<car>_<pull>*.f64 the JavaScript dumped (node Scratchpad/AcousticEditorRender.js
//        --dump …), AcousticIntegrator renders the same car / pull / seed / slicing in double and the two agree to ±1 × 10⁻⁶ per
//        sample (reported: max |Δ|, first offending sample), and the counters (firings, pops, clipped, dropped) are equal
//    [3] Slice invariance: 1 / 37 / 64 / 256-frame slices with held demand are byte-identical over 2 s
//    [4] Realtime hygiene: Render of a 64-frame slice for the LaFerrari on the pull, 20 000 slices, no allocation is observable
//        (the class holds no heap state; the check is the per-slice wall time — reported, not gated: the sandbox is not the GTX box)
// Build (from repo root):
//    g++ -std=c++20 -O2 -Wall -Wextra -I. -IEngine -IExternalPackages/tomlpp/include Scratchpad/AcousticIdentityTest.cpp
//        Engine/PlatformInterchange/AcousticStructure.cpp Engine/PlatformInterchange/AcousticIntegrator.cpp
//        Projects/Project-Dyno/Source/DynoSequence.cpp -o /tmp/ait && /tmp/ait | tee Scratchpad/AcousticIdentityTest.log
//    (one line; Scratchpad/CheckAcousticIdentity.sh does this plus the dumps)

#include "../Engine/PlatformInterchange/AcousticIntegrator.h"
#include "../Engine/PlatformInterchange/AcousticStructure.h"
#include "../Projects/Project-Dyno/Source/DynoSequence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Frontier;

namespace {

int Passed = 0, Failed = 0;
void Check(bool Ok, const std::string& Text) { (Ok ? Passed : Failed)++; std::printf("  %s  %s\n", Ok ? "PASS" : "FAIL", Text.c_str()); }

std::string ReadText(const std::string& Path)
{
    std::ifstream F(Path, std::ios::binary); std::ostringstream S; S << F.rdbuf(); return S.str();
}

// the dump's .txt: one `key number-or-word` per line, numbers with 17 significant digits
struct Echo
{
    std::vector<std::pair<std::string, std::string>> Lines;
    [[nodiscard]] bool empty() const { return Lines.empty(); }
    [[nodiscard]] bool count(const char* Key) const { for (auto& L : Lines) if (L.first == Key) return true; return false; }
    [[nodiscard]] const std::string& at(const char* Key) const { static const std::string None; for (auto& L : Lines) if (L.first == Key) return L.second; return None; }
    [[nodiscard]] double Number(const char* Key) const { return std::strtod(at(Key).c_str(), nullptr); }
};

Echo ReadEcho(const std::string& Path)
{
    Echo Out;
    std::istringstream S(ReadText(Path)); std::string Line;
    while (std::getline(S, Line)) { const size_t Sp = Line.find(' '); if (Sp != std::string::npos) Out.Lines.emplace_back(Line.substr(0, Sp), Line.substr(Sp + 1)); }
    return Out;
}

const char* CarKeys[5] = { "Porsche918Spyder", "FerrariLaFerrari", "NissanGtrNismo", "KoenigseggAgeraR", "DodgeDemon" };

// Renders like the dump: pull.advance(slice/rate) → scripted record → AssignDemand → render, per slice. RenderDouble gives the
//    samples before the float cast, so the comparison is double against double (the transport's float path is the same loop —
//    [3] renders it too and checks the cast is the only difference).
struct RenderResult
{
    std::vector<double> Left, Right;
    std::vector<float>  Interleaved;   // the transport's float output of a second, identically driven integrator
    AcousticReadout     Readout;
};

RenderResult RenderPull(const AcousticStructure& S, const std::string& Pull, double Seconds, uint32_t Slice, uint32_t Seed, bool Pure, const std::string& Listener)
{
    ListenerPreset Preset = ListenerPreset::Trackside; (void)ParseListenerPreset(Listener.c_str(), Preset);
    AcousticIntegrator Ig(S, Seed), Twin(S, Seed);
    for (AcousticIntegrator* I : { &Ig, &Twin }) { I->AssignListener(Preset); I->AssignPureTone(Pure); I->Prepare(48000u, 2u); }
    DynoSequence Sequence; Sequence.Select(Pull, S.Vehicle.RedlineRpm, S.Vehicle.IdleRpm);
    const uint64_t Total = uint64_t(std::floor(Seconds * 48000.0));
    RenderResult R; R.Left.assign(size_t(Total), 0.0); R.Right.assign(size_t(Total), 0.0); R.Interleaved.assign(size_t(Total) * 2u, 0.0f);
    uint64_t Done = 0u; double Time = 0.0;
    while (Done < Total)
    {
        const uint32_t N = uint32_t(std::min<uint64_t>(Slice, Total - Done)); const double Dt = double(N) / 48000.0;
        Sequence.Advance(Dt);
        const PowertrainRecord Rec = DynoSequence::ScriptedRecord(Sequence.QueryRpm(), Sequence.QueryThrottle(), S.Vehicle, Time);
        Ig.AssignDemand(Rec); Twin.AssignDemand(Rec);
        Ig.RenderDouble(R.Left.data() + Done, R.Right.data() + Done, N);
        Twin.Render(R.Interleaved.data() + size_t(Done) * 2u, N);
        Done += N; Time += Dt;
    }
    R.Readout = Ig.QueryReadout();
    return R;
}

} // namespace

int main()
{
    std::printf("AcousticIdentityTest — C++ voice vs the AudioEditor's JavaScript\n");
    const std::string Root = std::filesystem::exists("EngineContent") ? "." : "..";

    // [1] structure: archives round-trip, defaults agree
    std::printf("\n[1] AcousticStructure\n");
    {
        AcousticStructure Empty; std::string Err;
        Check(AcousticStructure::Deserialise("", Empty, &Err), "empty TOML deserialises (defaults)");
        bool DefaultsAgree = true;
        for (uint32_t I = 0u; I < AcousticFieldCount; ++I)
        {
            const AcousticFieldNote& F = AcousticStructure::Fields[I];
            if (F.Category == AcousticFieldCategory::Text) { if (std::strcmp(Empty.QueryText(I), F.DefaultText) != 0) DefaultsAgree = false; }
            else if (F.Category != AcousticFieldCategory::RealList) { if (Empty.QueryReal(I) != F.DefaultReal) { DefaultsAgree = false; std::printf("    default mismatch %s.%s\n", F.Section, F.Key); } }
        }
        Check(DefaultsAgree, "every Real / Whole / Toggle / Text default in the sheet equals the member initialiser");
        Check(AcousticStructure::FindField("exhaust", "valve_open_rpm") < AcousticFieldCount && AcousticStructure::FindField("exhaust", "nope") == AcousticFieldCount, "FindField finds exhaust.valve_open_rpm and rejects exhaust.nope");
        for (const char* Key : CarKeys)
        {
            const std::string Path = Root + "/EngineContent/AudioArchives/" + Key + "/" + Key + ".toml";
            const std::string Text = ReadText(Path);
            AcousticStructure S; uint32_t Unknown = 99u;
            const bool Loaded = AcousticStructure::Deserialise(Text, S, &Err, &Unknown);
            std::vector<std::string> Heads; std::istringstream Lines(Text); std::string Line;
            while (std::getline(Lines, Line) && !Line.empty() && Line[0] == '#') Heads.push_back(Line.size() > 2 ? Line.substr(2) : "");
            std::vector<const char*> Hp; for (auto& H : Heads) Hp.push_back(H.c_str());
            const std::string Again = AcousticStructure::Serialise(S, Hp.data(), uint32_t(Hp.size()));
            Check(Loaded && Unknown == 0u, std::string(Key) + ": loads, 0 unknown keys, " + std::to_string(S.Vehicle.CylinderCount) + " cylinders, crank_pulse " + (S.Voice.CrankPulse ? "on" : "off"));
            Check(Again == Text, std::string(Key) + ": Serialise reproduces the archive byte for byte (" + std::to_string(Text.size()) + " bytes)");
        }
        // unknown keys are counted, not fatal
        AcousticStructure S2; uint32_t Unknown = 0u;
        AcousticStructure::Deserialise("[vehicle]\nidle_rpm = 1234\nbogus = 1\n[nonsense]\nx = 2\n", S2, &Err, &Unknown);
        Check(S2.Vehicle.IdleRpm == 1234.0 && Unknown == 2u, "unknown key + unknown section counted (2), known key applied");
    }

    // [2] identity against every reference dump
    std::printf("\n[2] Identity vs Scratchpad/Reference/*.f64 (JavaScript dumps)\n");
    {
        std::vector<std::filesystem::path> Dumps;
        const std::filesystem::path RefDir = Root + "/Scratchpad/Reference";
        if (std::filesystem::exists(RefDir))
            for (auto& E : std::filesystem::directory_iterator(RefDir)) if (E.path().extension() == ".f64") Dumps.push_back(E.path());
        std::sort(Dumps.begin(), Dumps.end());
        Check(!Dumps.empty(), std::to_string(Dumps.size()) + " reference dumps found (node Scratchpad/AcousticEditorRender.js --dump all <pull> …)");
        double WorstAll = 0.0;
        for (const auto& Dump : Dumps)
        {
            const std::string Stem = Dump.stem().string();
            const Echo Dumped = ReadEcho((RefDir / (Stem + ".txt")).string());
            if (Dumped.empty()) { Check(false, Stem + ": no .txt echo beside the .f64"); continue; }
            const std::string Car = Dumped.at("car"), Pull = Dumped.at("pull"), Listener = Dumped.count("listener") ? Dumped.at("listener") : "trackside";
            const double Seconds = Dumped.Number("seconds");
            const uint32_t Slice = uint32_t(Dumped.Number("slice")), Seed = uint32_t(Dumped.Number("seed"));
            const bool Pure = Dumped.Number("pure") != 0.0;
            AcousticStructure S; std::string Err;
            const std::string TomlPath = (RefDir / (Stem + ".toml")).string();
            if (!AcousticStructure::Load(TomlPath, S, &Err)) { Check(false, Stem + ": " + Err); continue; }
            const auto T0 = std::chrono::steady_clock::now();
            const RenderResult R = RenderPull(S, Pull, Seconds, Slice, Seed, Pure, Listener);
            const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
            std::ifstream F(Dump, std::ios::binary); std::vector<double> Ref(R.Interleaved.size());
            F.read(reinterpret_cast<char*>(Ref.data()), std::streamsize(Ref.size() * sizeof(double)));
            const bool SizeOk = size_t(F.gcount()) == Ref.size() * sizeof(double);
            double Worst = 0.0; size_t WorstAt = 0u; double Peak = 0.0, WorstCast = 0.0; size_t Exact = 0u;
            for (size_t I = 0; I < Ref.size(); ++I)
            {
                const double Ours = (I & 1u) ? R.Right[I / 2u] : R.Left[I / 2u];
                const double D = std::fabs(Ours - Ref[I]);
                if (D > Worst) { Worst = D; WorstAt = I; }
                if (D == 0.0) ++Exact;
                Peak = std::max(Peak, std::fabs(Ref[I]));
                WorstCast = std::max(WorstCast, std::fabs(double(R.Interleaved[I]) - Ours));
            }
            WorstAll = std::max(WorstAll, Worst);
            char Line[512];
            std::snprintf(Line, sizeof(Line), "%s: %s %s %.1f s slice %u seed 0x%08X%s%s — max |Δ| %.3e at frame %zu ch %zu, %.4f %% of samples bit-exact (peak %.3f, %.0f ms = %.1f× realtime)",
                          Stem.c_str(), Car.c_str(), Pull.c_str(), Seconds, Slice, Seed, Pure ? " pure" : "", Listener != "trackside" ? (" " + Listener).c_str() : "",
                          Worst, WorstAt / 2u, WorstAt % 2u, 100.0 * double(Exact) / double(std::max<size_t>(1u, Ref.size())), Peak, Ms, Seconds * 1000.0 / std::max(1.0, Ms));
            Check(SizeOk && Worst <= 1.0e-6, Line);
            std::snprintf(Line, sizeof(Line), "%s: the transport's float output differs from the double path by ≤ %.3e (float32 rounding only, bar 1.2e-7)", Stem.c_str(), WorstCast);
            Check(WorstCast <= 1.2e-7, Line);
            const uint64_t Firings = uint64_t(Dumped.Number("firings"));
            const uint32_t Pops = uint32_t(Dumped.Number("pops")), Clipped = uint32_t(Dumped.Number("clipped")), Dropped = uint32_t(Dumped.Number("dropped"));
            std::snprintf(Line, sizeof(Line), "%s: counters firings %llu / pops %u / clipped %u / dropped %u equal the JavaScript (%llu / %u / %u / %u)", Stem.c_str(),
                          (unsigned long long)R.Readout.Firings, R.Readout.Pops, R.Readout.Clipped, R.Readout.Dropped, (unsigned long long)Firings, Pops, Clipped, Dropped);
            Check(R.Readout.Firings == Firings && R.Readout.Pops == Pops && R.Readout.Clipped == Clipped && R.Readout.Dropped == Dropped, Line);
            const double RpmS = Dumped.Number("rpmS"), Theta = Dumped.Number("theta");
            std::snprintf(Line, sizeof(Line), "%s: final smoothed rpm %.6f / crank angle %.6f° equal the JavaScript (%.6f / %.6f°)", Stem.c_str(), R.Readout.Rpm, R.Readout.Theta, RpmS, Theta);
            Check(std::fabs(R.Readout.Rpm - RpmS) <= 1.0e-9 * std::max(1.0, RpmS) && std::fabs(R.Readout.Theta - Theta) <= 1.0e-9 * std::max(1.0, Theta), Line);
        }
        std::printf("  worst |Δ| over all dumps: %.3e (bar 1e-6; float32 rounding of |x| ≤ 1 is ≤ 6e-8)\n", WorstAll);
    }

    // [3] slice invariance with held demand
    std::printf("\n[3] Slice invariance\n");
    {
        AcousticStructure S; std::string Err;
        AcousticStructure::Load(Root + "/EngineContent/AudioArchives/FerrariLaFerrari/FerrariLaFerrari.toml", S, &Err);
        const uint32_t Slices[4] = { 1u, 37u, 64u, 256u };
        std::vector<float> Renders[4];
        for (uint32_t K = 0u; K < 4u; ++K)
        {
            AcousticIntegrator Ig(S); Ig.Prepare(48000u, 2u);
            PowertrainRecord Rec; Rec.Rpm = 5200.0; Rec.Throttle = 0.8; Rec.Load = 0.8; Ig.AssignDemand(Rec);
            const uint32_t Total = 96000u; Renders[K].assign(Total * 2u, 0.0f);
            for (uint32_t Done = 0u; Done < Total;) { const uint32_t N = std::min(Slices[K], Total - Done); Ig.Render(Renders[K].data() + Done * 2u, N); Done += N; }
        }
        bool Same = true;
        for (uint32_t K = 1u; K < 4u && Same; ++K) Same = std::memcmp(Renders[0].data(), Renders[K].data(), Renders[0].size() * sizeof(float)) == 0;
        Check(Same, "LaFerrari held 5200 rpm / 0.8: 1 / 37 / 64 / 256-frame slices byte-identical over 2 s");
        // structure hot-swap mid-render does not touch the crank: theta continues, events keep phase
        AcousticIntegrator Ig(S); Ig.Prepare(48000u, 2u);
        PowertrainRecord Rec; Rec.Rpm = 3000.0; Rec.Throttle = 0.5; Rec.Load = 0.5; Ig.AssignDemand(Rec);
        std::vector<float> Buf(64u * 2u);
        for (int I = 0; I < 100; ++I) Ig.Render(Buf.data(), 64u);
        const double ThetaBefore = Ig.QueryReadout().Theta;
        AcousticStructure S2 = S; S2.Exhaust.FormantALevel = 0.9; Ig.AssignStructure(S2);
        Ig.Render(Buf.data(), 64u);
        const double ThetaAfter = Ig.QueryReadout().Theta;
        Check(std::fabs((ThetaAfter - ThetaBefore) - 6.0 * 3000.0 / 48000.0 * 64.0) < 1.0e-6, "AssignStructure mid-render: crank angle advances exactly one slice (no reset, no jump)");
    }

    // [4] cost
    std::printf("\n[4] Realtime cost (informative)\n");
    {
        AcousticStructure S; std::string Err;
        AcousticStructure::Load(Root + "/EngineContent/AudioArchives/FerrariLaFerrari/FerrariLaFerrari.toml", S, &Err);
        AcousticIntegrator Ig(S); Ig.Prepare(48000u, 2u);
        DynoSequence Sequence; Sequence.Select("pull", S.Vehicle.RedlineRpm, S.Vehicle.IdleRpm);
        std::vector<float> Buf(64u * 2u);
        double WorstUs = 0.0, TotalUs = 0.0; const int Slices = 20000; double Time = 0.0;
        for (int I = 0; I < Slices; ++I)
        {
            Sequence.Advance(64.0 / 48000.0); Ig.AssignDemand(DynoSequence::ScriptedRecord(Sequence.QueryRpm(), Sequence.QueryThrottle(), S.Vehicle, Time)); Time += 64.0 / 48000.0;
            const auto T0 = std::chrono::steady_clock::now();
            Ig.Render(Buf.data(), 64u);
            const double Us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - T0).count();
            WorstUs = std::max(WorstUs, Us); TotalUs += Us;
        }
        char Line[256];
        std::snprintf(Line, sizeof(Line), "LaFerrari pull, 20 000 × 64-frame slices: mean %.1f µs / slice (budget 1333 µs), worst %.1f µs → mean load %.2f %%", TotalUs / Slices, WorstUs, 100.0 * (TotalUs / Slices) / 1333.3);
        Check(TotalUs / Slices < 1333.3, Line);
    }

    std::printf("\n%s — %d pass, %d fail\n", Failed == 0 ? "ALL PASS" : "FAILURES", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
