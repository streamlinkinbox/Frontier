/*==========================================================================================================================================
                                                   HEADLESS PROOF FOR THE PORTABLE ENGINE
                                     ------------------------------------------------------------------
                                     Ring store, categories, panel semantics, descriptors, interchange.
==========================================================================================================================================*/

// 🧩 Headless proof for the portable engine: ring store, categories, panel semantics, descriptors, interchange.

#include "Applications/Editor/CommandLine.h"
#include "Applications/Editor/ProjectBrowserHost.h"
#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ProjectContent.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/ProjectDescriptor.h"

#include <ProjectInterchange.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{

int Failures = 0;
int Passes   = 0;

void Check(bool Condition, const char* Name)
{
    if (Condition)
    {
        ++Passes;
        std::printf("  pass  %s\n", Name);
    }
    else
    {
        ++Failures;
        std::printf("  FAIL  %s\n", Name);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RING STORE
//------------------------------------------------------------------------------------------------------------------------

void CheckRingStore()
{
    std::printf("[ring store]\n");
    Frontier::LogRingStore Store;

    std::array<Frontier::LogEntry, 8> Out{};
    uint64_t                          Cursor = 0;
    uint64_t                          Skipped = 0;
    Check(Store.ReadSince(0, Out.data(), 8, Cursor, Skipped) == 0, "empty store reads zero");

    const uint64_t First = Store.Write(3, Frontier::LogSeverity::Warning, Frontier::LogVerbosity::Log,
                                       "first message", "Validation.cpp");
    Check(First == 1, "first sequence is one");
    uint32_t Count = Store.ReadSince(0, Out.data(), 8, Cursor, Skipped);
    Check(Count == 1 && Cursor == 1 && Skipped == 0, "single entry round trip");
    Check(std::strcmp(Out[0].Message, "first message") == 0 &&
          std::strcmp(Out[0].Source, "Validation.cpp") == 0 &&
          Out[0].Severity == Frontier::LogSeverity::Warning, "entry fields survive the ring");

    // Wrap: write ten past capacity; a stale cursor must skip forward with the loss accounted.
    for (uint32_t Index = 0; Index < Frontier::LogRingStore::Capacity + 10 - 1; ++Index)
    {
        Store.Write(1, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "filler", "Validation.cpp");
    }
    Check(Store.QueryWritten() == Frontier::LogRingStore::Capacity + 10, "total written counts wraps");

    uint64_t StaleSkipped = 0;
    uint64_t NewCursor    = 0;
    Count = Store.ReadSince(0, Out.data(), 8, NewCursor, StaleSkipped);
    Check(StaleSkipped == 10 && Count == 8, "stale cursor reports exactly the overwritten losses");

    uint64_t Delivered = Count;
    Cursor             = NewCursor;
    for (;;)
    {
        uint64_t RoundSkipped = 0;
        const uint32_t Got = Store.ReadSince(Cursor, Out.data(), 8, Cursor, RoundSkipped);
        Delivered += Got;
        if (Got == 0)
        {
            break;
        }
    }
    Check(Delivered == Frontier::LogRingStore::Capacity, "full retained window readable after wrap");

    // Contention: four producers, entries certified untorn and delivered at most once.
    Frontier::LogRingStore Busy;
    constexpr uint32_t     Producers      = 4;
    constexpr uint32_t     PerProducer    = 20000;
    std::vector<std::thread> Threads;
    for (uint32_t Producer = 0; Producer < Producers; ++Producer)
    {
        Threads.emplace_back([&Busy, Producer]() {
            char Text[64] = {};
            for (uint32_t Index = 0; Index < PerProducer; ++Index)
            {
                std::snprintf(Text, sizeof(Text), "P%u#%u#%u", Producer, Index, Producer * 1000003u + Index);
                Busy.Write(2, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, Text, "Threads.cpp");
            }
        });
    }
    for (std::thread& Thread : Threads)
    {
        Thread.join();
    }

    std::vector<char> Seen(static_cast<size_t>(Producers) * PerProducer + 1, 0);
    std::array<Frontier::LogEntry, 1024> Chunk{};
    uint64_t BusyCursor    = 0;
    uint64_t BusyDelivered = 0;
    uint64_t BusySkipped   = 0;
    bool     Untorn        = true;
    bool     Unique         = true;
    for (;;)
    {
        uint64_t       RoundSkipped = 0;
        const uint32_t Got = Busy.ReadSince(BusyCursor, Chunk.data(), static_cast<uint32_t>(Chunk.size()),
                                            BusyCursor, RoundSkipped);
        BusySkipped += RoundSkipped;
        for (uint32_t Index = 0; Index < Got; ++Index)
        {
            unsigned Producer = 0;
            unsigned Written  = 0;
            unsigned Checksum = 0;
            if (std::sscanf(Chunk[Index].Message, "P%u#%u#%u", &Producer, &Written, &Checksum) != 3 ||
                Checksum != Producer * 1000003u + Written)
            {
                Untorn = false;
                continue;
            }
            const size_t Slot = Chunk[Index].Sequence;
            if (Slot >= Seen.size() || Seen[Slot] != 0)
            {
                Unique = false;
            }
            else
            {
                Seen[Slot] = 1;
            }
        }
        BusyDelivered += Got;
        if (Got == 0)
        {
            break;
        }
    }
    Check(Untorn, "no torn entries under contention");
    Check(Unique, "each sequence delivered at most once");
    Check(BusyDelivered + BusySkipped == static_cast<uint64_t>(Producers) * PerProducer,
          "delivered + skipped accounts for every write");

    // Concurrent drain: a reader emptying the ring while producers are still writing must never observe a
    // torn entry or a duplicate, and the final accounting must still be exact.
    {
        Frontier::LogRingStore Live;
        std::atomic<bool>      ProducersDone{false};
        std::atomic<uint64_t>  LiveDelivered{0};
        std::atomic<uint64_t>  LiveSkipped{0};
        std::atomic<bool>      LiveUntorn{true};
        std::vector<char>      LiveSeen(static_cast<size_t>(Producers) * PerProducer + 1, 0);

        std::thread Reader([&]() {
            std::array<Frontier::LogEntry, 512> Drain{};
            uint64_t                            Cursor = 0;
            while (!ProducersDone.load(std::memory_order_acquire))
            {
                uint64_t       Skipped = 0;
                const uint32_t Got = Live.ReadSince(Cursor, Drain.data(),
                                                    static_cast<uint32_t>(Drain.size()), Cursor, Skipped);
                LiveSkipped.fetch_add(Skipped, std::memory_order_relaxed);
                for (uint32_t Index = 0; Index < Got; ++Index)
                {
                    unsigned Producer = 0;
                    unsigned Written  = 0;
                    unsigned Checksum = 0;
                    if (std::sscanf(Drain[Index].Message, "P%u#%u#%u", &Producer, &Written, &Checksum) != 3 ||
                        Checksum != Producer * 1000003u + Written)
                    {
                        LiveUntorn.store(false, std::memory_order_relaxed);
                    }
                    const size_t Slot = Drain[Index].Sequence;
                    if (Slot < LiveSeen.size())
                    {
                        if (LiveSeen[Slot] != 0)
                        {
                            LiveUntorn.store(false, std::memory_order_relaxed);
                        }
                        LiveSeen[Slot] = 1;
                    }
                }
                LiveDelivered.fetch_add(Got, std::memory_order_relaxed);
                if (Got == 0)
                {
                    std::this_thread::yield();
                }
            }
            // Final drain after the producers retire.
            for (;;)
            {
                uint64_t       Skipped = 0;
                const uint32_t Got = Live.ReadSince(Cursor, Drain.data(),
                                                    static_cast<uint32_t>(Drain.size()), Cursor, Skipped);
                LiveSkipped.fetch_add(Skipped, std::memory_order_relaxed);
                LiveDelivered.fetch_add(Got, std::memory_order_relaxed);
                for (uint32_t Index = 0; Index < Got; ++Index)
                {
                    unsigned Producer = 0;
                    unsigned Written  = 0;
                    unsigned Checksum = 0;
                    if (std::sscanf(Drain[Index].Message, "P%u#%u#%u", &Producer, &Written, &Checksum) != 3 ||
                        Checksum != Producer * 1000003u + Written)
                    {
                        LiveUntorn.store(false, std::memory_order_relaxed);
                    }
                }
                if (Got == 0)
                {
                    break;
                }
            }
        });

        std::vector<std::thread> LiveThreads;
        for (uint32_t Producer = 0; Producer < Producers; ++Producer)
        {
            LiveThreads.emplace_back([&Live, Producer]() {
                char Text[64] = {};
                for (uint32_t Index = 0; Index < PerProducer; ++Index)
                {
                    std::snprintf(Text, sizeof(Text), "P%u#%u#%u", Producer, Index, Producer * 1000003u + Index);
                    Live.Write(2, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, Text, "Threads.cpp");
                }
            });
        }
        for (std::thread& Thread : LiveThreads)
        {
            Thread.join();
        }
        ProducersDone.store(true, std::memory_order_release);
        Reader.join();

        Check(LiveUntorn.load(), "no torn or duplicated entries while draining live");
        Check(LiveDelivered.load() + LiveSkipped.load() == static_cast<uint64_t>(Producers) * PerProducer,
              "live drain accounts for every write");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

void CheckCategories()
{
    std::printf("[categories]\n");
    Frontier::LogCategoryRegistry& Registry = Frontier::LogCategories();

    const Frontier::LogCategory* Render = Registry.QueryByName("Render");
    Check(Render != nullptr && Render->Token == 1 && !Render->UserCreated, "engine channel Render seeded at token 1");
    Check(Registry.QueryCount() >= 9, "nine engine channels seeded");

    const Frontier::LogCategory First  = Frontier::RegisterLogCategory("Foliage", Frontier::LogColour::Pink);
    const Frontier::LogCategory Second = Frontier::RegisterLogCategory("Foliage", Frontier::LogColour::Blue);
    Check(First.Token != 0 && First.UserCreated, "user category registered");
    Check(Second.Token == First.Token && Second.Colour == Frontier::LogColour::Blue,
          "re-registration keeps the token and updates the colour");
    Check(Frontier::RegisterLogCategory("", Frontier::LogColour::Pink).Token == 0, "empty name rejected");

    const Frontier::LogColourSpectrum Blue = Frontier::ResolveLogColour(Frontier::LogColour::Blue);
    Check(Blue.Red > 0.38f && Blue.Red < 0.40f && Blue.Green > 0.65f && Blue.Blue > 0.99f,
          "blue palette resolves to the prototype #63a8ff");
    const Frontier::LogColourSpectrum Pink = Frontier::ResolveLogColour(Frontier::LogColour::Pink);
    Check(Pink.Red > 0.99f && Pink.Green > 0.47f && Pink.Green < 0.48f && Pink.Blue > 0.84f,
          "pink palette resolves to the prototype #ff7ad9");

    const std::optional<Frontier::LogColour> Parsed = Frontier::ParseLogColour("purple");
    Check(Parsed.has_value() && *Parsed == Frontier::LogColour::Purple, "palette names parse case-insensitively");
    Check(!Frontier::ParseLogColour("chartreuse").has_value(), "unknown palette names rejected");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PANEL
//------------------------------------------------------------------------------------------------------------------------

void CheckPanel()
{
    std::printf("[panel]\n");
    Frontier::LogRingStore        Store;
    Frontier::LogStreamPanel      Panel;
    Frontier::LogCategoryRegistry& Registry = Frontier::LogCategories();
    const uint32_t Foliage = Registry.QueryByName("Foliage")->Token;
    const uint32_t Render  = 1;

    // Repeat collapsing: three identical inside the window, one after it.
    const uint64_t T0 = 1700000000000000ull;
    Store.WriteAt(Foliage, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "leaf fall", "Tree.cpp", T0);
    Store.WriteAt(Foliage, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "leaf fall", "Tree.cpp", T0 + 1000000);
    Store.WriteAt(Foliage, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "leaf fall", "Tree.cpp", T0 + 2000000);
    Store.WriteAt(Foliage, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "leaf fall", "Tree.cpp", T0 + 9000000);
    Store.WriteAt(Render, Frontier::LogSeverity::Warning, Frontier::LogVerbosity::Log, "pipeline stall", "Pipeline.cpp", T0 + 9500000);
    Store.WriteAt(Render, Frontier::LogSeverity::Error, Frontier::LogVerbosity::Display, "device lost", "Device.cpp", T0 + 9600000);
    Store.WriteAt(Render, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Verbose, "chatty detail", "Device.cpp", T0 + 9700000);

    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryTotalCount() == 7, "panel consumed every entry");
    Check(Panel.QueryVisible().size() == 4, "collapse folds the first three and the ceiling hides verbose");
    Check(!Panel.QueryVisible().empty() && Panel.QueryVisible().front().RepeatCount == 3, "repeat badge counts three");
    Check(Panel.QuerySeverityCount(Frontier::LogSeverity::Warning) == 1 &&
          Panel.QuerySeverityCount(Frontier::LogSeverity::Error) == 1, "severity counters track the stream");
    Check(Panel.QueryWarningsLastMinute() == 1 && Panel.QueryErrorsLastMinute() == 1,
          "trailing-minute delta windows count the fresh warning and error");
    Check(Panel.QueryCategoryCount(Foliage) == 4, "category counters track the stream");

    Panel.ConfigureCeiling(Frontier::LogCeiling::Verbose);
    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryVisible().size() == 5, "verbose ceiling reveals the chatty entry");

    Panel.ConfigureQuery("LEAF");
    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryVisible().size() == 2, "query matches messages case-insensitively");

    Panel.ConfigureQuery("foliage");
    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryVisible().size() == 2, "query matches category names");

    Panel.ConfigureQuery("");
    Panel.ConfigureCollapse(false);
    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryVisible().size() == 7, "disabling collapse unfolds the retained window");

    Panel.ConfigureSeverity(Frontier::LogSeverity::Info, false);
    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryVisible().size() == 2, "severity chip off hides every info row");
    Panel.ConfigureAllSeverities();

    // Pause freezes the view, not the stream.
    Panel.ConfigurePaused(true);
    const uint64_t BeforePause = Panel.QueryTotalCount();
    Store.WriteAt(Render, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "while paused", "X.cpp", T0 + 10000000);
    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryTotalCount() == BeforePause, "pause holds the cursor");
    Panel.ConfigurePaused(false);
    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryTotalCount() == BeforePause + 1, "resume catches up");

    // Follow break + new-below accounting.
    Panel.NotifyScrolledUp();
    Check(!Panel.QueryFollow(), "scrolling up breaks follow");
    Store.WriteAt(Render, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "below one", "X.cpp", T0 + 11000000);
    Store.WriteAt(Render, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "below two", "X.cpp", T0 + 11100000);
    Panel.Synchronize(Store, Registry, 0.1);
    Check(Panel.QueryNewBelow() == 2, "new rows counted while scrolled up");
    Panel.JumpToLatest();
    Check(Panel.QueryFollow() && Panel.QueryNewBelow() == 0, "jump to latest restores follow");

    // Selection toggles.
    Panel.SelectVisible(0);
    Check(Panel.QuerySelectedIndex() == 0, "row expands on select");
    Panel.SelectVisible(0);
    Check(Panel.QuerySelectedIndex() == -1, "row collapses on re-select");

    // Clear resets the view but never the store.
    const uint64_t Written = Store.QueryWritten();
    Panel.ClearView();
    Check(Panel.QueryVisible().empty() && Panel.QueryTotalCount() == 0, "clear empties the view");
    Check(Store.QueryWritten() == Written, "clear leaves the ring untouched");

    // Clock formatting, both modes.
    const Frontier::LogClockText Wall = Frontier::FormatLogClock(1700000000123456ull, 0, false);
    Check(std::strcmp(Wall.Millis, ".123") == 0, "wall clock renders fractional digits");
    const Frontier::LogClockText Session = Frontier::FormatLogClock(3661234000ull, 1000000ull, true);
    Check(std::strcmp(Session.Main, "01:01:00") == 0 && std::strcmp(Session.Millis, ".234") == 0,
          "session clock renders elapsed time");

    // The no-SDK presentation links and runs without pretending to draw.
    Panel.ConstructInterface();
    Check(true, "presentation stub links and returns");
}

//------------------------------------------------------------------------------------------------------------------------
//                                             DESCRIPTORS + ENUMERATION
//------------------------------------------------------------------------------------------------------------------------

void CheckDescriptors(const std::filesystem::path& RepositoryRoot)
{
    std::printf("[descriptors]\n");
    const std::filesystem::path Projects = RepositoryRoot / "Frontier" / "Projects";

    Frontier::ProjectDescriptor Zero;
    std::string                 Diagnostic;
    const bool ZeroOk = Frontier::DecodeProjectDescriptor(
        Projects / "Project-Zero" / "ProjectZero.frontier", Zero, Diagnostic);
    Check(ZeroOk, "Project Zero descriptor decodes");
    if (!ZeroOk)
    {
        std::printf("        %s\n", Diagnostic.c_str());
    }
    Check(Zero.Name == "ProjectZero" && Zero.LogCategories.size() == 3, "Project Zero declares three log categories");
    Check(Zero.LogCategories.size() == 3 && Zero.LogCategories[0].Name == "Foliage" &&
          Zero.LogCategories[0].Colour == "pink", "Foliage declared pink, like the approved prototype");
    Check(Zero.CodeInterchangeNumber == 3 && Zero.InterfaceFingerprint == 0xb6725f24d0869173ull,
          "interchange number and fingerprint decode");

    const Frontier::ProjectBrowserEnumeration Listing = Frontier::EnumerateProjects(Projects);
    bool SawNetworking = false;
    bool SawVehicle    = false;
    bool SawZero       = false;
    for (const Frontier::ProjectBrowserEntry& Entry : Listing.Entries)
    {
        SawNetworking |= Entry.Descriptor.Name == "ProjectNetworking";
        SawVehicle    |= Entry.Descriptor.Name == "ProjectVehicleHmiDemo";
        SawZero       |= Entry.Descriptor.Name == "ProjectZero";
    }
    Check(SawZero && SawNetworking && SawVehicle, "browser enumerates every repository project");
    Check(Listing.Diagnostics.empty(), "every repository descriptor decodes cleanly");
    for (const std::string& Line : Listing.Diagnostics)
    {
        std::printf("        %s\n", Line.c_str());
    }

    Frontier::ProjectDescriptor Broken;
    Check(!Frontier::DecodeProjectDescriptor(Projects / "Nope" / "Nope.frontier", Broken, Diagnostic),
          "missing descriptor fails with a diagnostic");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONTENT DIRECTORY
//------------------------------------------------------------------------------------------------------------------------

void CheckContentDirectory()
{
    std::printf("[content directory]\n");
    std::error_code             Error;
    const std::filesystem::path Scratch = std::filesystem::temp_directory_path(Error) / "FrontierLogStreamProof";
    std::filesystem::remove_all(Scratch, Error);

    const std::filesystem::path ExeDir       = Scratch / "Bin";
    const std::filesystem::path ProjectContent = Scratch / "Project" / "Content";
    const std::filesystem::path Staged         = ExeDir / "EngineContent" / "Fonts";
    std::filesystem::create_directories(ProjectContent / "Fonts", Error);
    std::filesystem::create_directories(Staged, Error);
    { std::ofstream(ProjectContent / "Fonts" / "Shared.ttf") << "project"; }
    { std::ofstream(Staged / "Shared.ttf") << "engine"; }
    { std::ofstream(Staged / "EngineOnly.ttf") << "engine"; }

    Frontier::ConfigureContentRoots(ExeDir, ProjectContent);
    Check(Frontier::ResolveProjectContentFile("Fonts/Shared.ttf") == ProjectContent / "Fonts" / "Shared.ttf",
          "project content shadows shared content");
    Check(Frontier::ResolveProjectContentFile("Fonts/EngineOnly.ttf") == Staged / "EngineOnly.ttf",
          "shared content resolves when the project has none");
    Check(Frontier::ResolveProjectContentFile("Fonts/Missing.ttf").empty(), "missing content resolves empty, never faked");

    std::filesystem::remove_all(Scratch, Error);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     COMMAND LINE
//------------------------------------------------------------------------------------------------------------------------

void CheckCommandLine()
{
    std::printf("[command line]\n");
    {
        const char* Args[] = {"Frontier"};
        Check(Frontier::DecodeCommandLine(1, Args).Mode == Frontier::LaunchMode::Browser,
              "no arguments opens the browser");
    }
    {
        const char* Args[] = {"Frontier", "--project", "X.frontier"};
        const Frontier::LaunchRequest Request = Frontier::DecodeCommandLine(3, Args);
        Check(Request.Mode == Frontier::LaunchMode::Project &&
              Request.ProjectDescriptor == std::filesystem::path("X.frontier"), "--project opens directly");
    }
    {
        const char* Args[] = {"Frontier", "X.frontier", "--headless"};
        Check(Frontier::DecodeCommandLine(3, Args).Mode == Frontier::LaunchMode::Headless,
              "bare descriptor plus --headless runs headless");
    }
    {
        const char* Args[] = {"Frontier", "--headless"};
        Check(Frontier::DecodeCommandLine(2, Args).Mode == Frontier::LaunchMode::Invalid,
              "--headless without a project is refused");
    }
    {
        const char* Args[] = {"Frontier", "--bogus"};
        Check(Frontier::DecodeCommandLine(2, Args).Mode == Frontier::LaunchMode::Invalid,
              "unknown arguments are refused");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERCHANGE
//------------------------------------------------------------------------------------------------------------------------

struct CapturedDiagnostics
{
    std::vector<std::string> Subjects;
    std::vector<std::string> Texts;
};

void FRONTIER_CODE_IMAGE_CALL CaptureDiagnostic(const FrontierProjectDiagnostic* Diagnostic, void* Reception)
{
    auto* Captured = static_cast<CapturedDiagnostics*>(Reception);
    if (Diagnostic == nullptr || Captured == nullptr)
    {
        return;
    }
    Captured->Subjects.emplace_back(Diagnostic->SubjectName != nullptr ? Diagnostic->SubjectName : "");
    Captured->Texts.emplace_back(Diagnostic->Explanation != nullptr ? Diagnostic->Explanation : "");
}

void CheckInterchange()
{
    std::printf("[project interchange]\n");
    FrontierProjectInterchange Delivery{};
    Delivery.StructureSize = sizeof(Delivery);
    std::array<char, 256> Refusal{};

    Check(!ConstructProjectInterchange(99, FRONTIER_INTERFACE_FINGERPRINT, &Delivery,
                                       Refusal.data(), Refusal.size()) && Refusal[0] != '\0',
          "wrong interchange number refused with an explanation");
    Check(!ConstructProjectInterchange(FRONTIER_CODE_INTERCHANGE_NUMBER, 0xdeadbeefull, &Delivery,
                                       Refusal.data(), Refusal.size()) && Refusal[0] != '\0',
          "wrong fingerprint refused with an explanation");

    Check(ConstructProjectInterchange(FRONTIER_CODE_INTERCHANGE_NUMBER, FRONTIER_INTERFACE_FINGERPRINT,
                                       &Delivery, Refusal.data(), Refusal.size()) &&
          Delivery.ConstructProject != nullptr && Delivery.AdvanceProject != nullptr &&
          Delivery.RetireProject != nullptr, "v3 negotiation fills the lifecycle table");

    CapturedDiagnostics          Captured;
    FrontierHostInterchange      Host{};
    Host.StructureSize    = sizeof(Host);
    Host.ReceiveDiagnostic = &CaptureDiagnostic;
    Host.ProjectReception = &Captured;

    Check(Delivery.ConstructProject(&Host, nullptr), "project constructs against the host");
    Check(Captured.Subjects.size() == 1 && Captured.Subjects[0] == "Project-Zero",
          "construction announces itself on the project subject");

    Delivery.AdvanceProject();
    Check(Captured.Subjects.size() >= 2, "first advance emits immediately");
    Delivery.RetireProject();
    Check(!Captured.Texts.empty() && Captured.Texts.back().find("retired") != std::string::npos,
          "retirement announces itself");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  BUILD CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

void CheckBuildConfiguration()
{
    std::printf("[build configuration]\n");
    std::printf("        editor %d · development %d · shipping %d · editor machinery %d\n",
                Frontier::BuildConfiguration::EditorEnabled ? 1 : 0,
                Frontier::BuildConfiguration::DevelopmentEnabled ? 1 : 0,
                Frontier::BuildConfiguration::ShippingEnabled ? 1 : 0,
                Frontier::BuildConfiguration::EditorMachineryEnabled ? 1 : 0);
#if defined(FRONTIER_SHIPPING_PROFILE)
    static_assert(!Frontier::BuildConfiguration::EditorEnabled, "shipping strips the editor");
    static_assert(!Frontier::BuildConfiguration::EditorMachineryEnabled, "shipping strips editor machinery");
    static_assert(Frontier::BuildConfiguration::ShippingEnabled, "shipping flag set");
#endif
#if defined(FRONTIER_EDITOR_PROFILE)
    static_assert(Frontier::BuildConfiguration::EditorEnabled, "editor flag set");
    static_assert(Frontier::BuildConfiguration::EditorMachineryEnabled, "editor keeps its machinery");
#endif
    Check(Frontier::LogRank(Frontier::LogSeverity::Fatal, Frontier::LogVerbosity::Verbose) == 0 &&
          Frontier::LogRank(Frontier::LogSeverity::Info, Frontier::LogVerbosity::Verbose) == 5,
          "severity ranks match the prototype table");
    Check(Frontier::LogPassesCeiling(Frontier::LogSeverity::Error, Frontier::LogVerbosity::Log,
                                     Frontier::LogCeiling::Warning), "ceiling admits severities above it");
    Check(!Frontier::LogPassesCeiling(Frontier::LogSeverity::Info, Frontier::LogVerbosity::Verbose,
                                      Frontier::LogCeiling::Log), "ceiling hides verbose chatter by default");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    const std::filesystem::path RepositoryRoot = ArgumentCount > 1 ? Arguments[1]
                                                                   : std::filesystem::current_path();

    CheckRingStore();
    CheckCategories();
    CheckPanel();
    CheckDescriptors(RepositoryRoot);
    CheckContentDirectory();
    CheckCommandLine();
    CheckInterchange();
    CheckBuildConfiguration();

    std::printf("\nLog Stream validation: %d passed, %d failed\n", Passes, Failures);
    return Failures == 0 ? 0 : 1;
}
