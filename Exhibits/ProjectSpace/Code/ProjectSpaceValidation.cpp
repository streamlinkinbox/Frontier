/*==========================================================================================================================================
                                                        PROJECTSPACEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify embedded project defaults, no-sidecar scoped saves, refused writes, fallback, and actual native scope controls.

#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/IconPresentation.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition)
        throw std::runtime_error(Description);
}

std::string Read(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Input), {}};
}

void Capture(const std::filesystem::path &Path)
{
    constexpr int              Width = 1100, Height = 1000;
    std::vector<unsigned char> Pixels(Width * Height * 3, 24);
    const auto                *Commands = ImGui::GetDrawData();
    for (const auto *List : Commands->CmdLists)
        FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, Commands->DisplayPos,
                                           Commands->FramebufferScale);
    std::ofstream Output(Path, std::ios::binary);
    Output << "P6\n" << Width << ' ' << Height << "\n255\n";
    Output.write(reinterpret_cast<const char *>(Pixels.data()), Pixels.size());
    Require(Output.good(), "Native projectspace capture failed");
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    namespace fs = std::filesystem;
    try
    {
        Require(ArgumentCount == 3, "Expected engine content and disposable evidence directory");
        const fs::path Content  = fs::absolute(Arguments[1]);
        const fs::path Evidence = fs::absolute(Arguments[2]);
        fs::remove_all(Evidence);
        fs::create_directories(Evidence);
        const auto           Template      = Content / "Defaults/BlankProject/Blank.projectspace";
        const auto           TemplateBytes = Read(Template);
        const auto           Project       = Evidence / DecodeContentPath("工程 Δ") / "Sample.projectspace";
        const auto           Personal      = Evidence / "Personal.projectspace";
        std::string          Diagnostic;
        ProjectSpaceDocument Seed, Document;
        Require(ProjectSpaceCodec::Decode(Template, Seed, Diagnostic), Diagnostic.c_str());
        Require(Seed.AppearancePresent && Seed.InputPresent && Seed.NotificationsPresent, "Template profiles missing");
        Require(ProjectSpaceCodec::CreateBlank(Template, Project, "Unicode project Δ", Diagnostic), Diagnostic.c_str());
        Require(fs::is_directory(Project.parent_path() / "Content"), "Blank content root not created");
        Require(ProjectSpaceCodec::Decode(Project, Document, Diagnostic), Diagnostic.c_str());
        Require(Document.Sections["project"]["identity"] != Seed.Sections["project"]["identity"],
                "Template identity was cloned");
        Require(!ProjectSpaceCodec::CreateBlank(Template, Project, "Overwrite", Diagnostic),
                "Existing project overwritten");
        Document.Sections.insert("extension", toml::table{{"opaque", "retain me"}});
        Document.Sections["engine"].as_table()->insert("custom_engine", 42);
        Document.Sections["catalogue"].as_array()->push_back(
            toml::table{{"identity", "icon-a"}, {"path", "Content/thing.svg"}});
        Document.Appearance.RequestedFamily   = "Absent family preserved";
        Document.Appearance.Preferences.Theme = ThemeCategory::Nord;
        Require(ProjectSpaceCodec::Encode(Project, Document, Diagnostic), Diagnostic.c_str());
        const auto Original            = Read(Project);
        auto       Invalid             = Document;
        Invalid.Input.MouseSensitivity = 900;
        Require(!ProjectSpaceCodec::Encode(Project, Invalid, Diagnostic) && Read(Project) == Original,
                "Invalid input replaced project");
        Invalid = Document;
        Invalid.Sections.insert_or_assign("version", 99);
        Require(!ProjectSpaceCodec::Encode(Project, Invalid, Diagnostic), "Unknown project schema accepted");
        Invalid = Document;
        Invalid.Sections.insert_or_assign("content_roots", toml::array{"../escape"});
        Require(!ProjectSpaceCodec::Validate(Invalid, Diagnostic), "Root escape accepted");
        Invalid.Sections.insert_or_assign("content_roots", toml::array{"C:\\escape"});
        Require(!ProjectSpaceCodec::Validate(Invalid, Diagnostic), "Foreign absolute path accepted");
        Invalid                           = Document;
        Invalid.Notifications.HoldSeconds = 99;
        Require(!ProjectSpaceCodec::Validate(Invalid, Diagnostic), "Invalid notification profile accepted");
        Invalid = Document;
        Invalid.Sections["dependency_pins"].as_array()->push_back(toml::table{{"identity", "dep-a"},
                                                                              {"path", "Content/dep.geometry"},
                                                                              {"revision", "R1"},
                                                                              {"type", "geometry"},
                                                                              {"sha256", "not-a-digest"}});
        Require(!ProjectSpaceCodec::Validate(Invalid, Diagnostic), "Malformed dependency pin accepted");
        Invalid = Document;
        Invalid.Sections["engine"].as_table()->insert_or_assign("startup_level", "missing-level");
        Require(!ProjectSpaceCodec::Validate(Invalid, Diagnostic), "Unresolved startup-level reference accepted");
        const auto Broken = Evidence / "Broken.projectspace";
        std::ofstream(Broken) << "FSPC binary not text";
        const auto IdentityBefore = Document.Sections["project"]["identity"].value<std::string>();
        Require(!ProjectSpaceCodec::Decode(Broken, Document, Diagnostic) &&
                    Document.Sections["project"]["identity"].value<std::string>() == IdentityBefore,
                "Binary/failed decode replaced live document");
        std::ofstream(Broken, std::ios::trunc) << std::string(ProjectSpaceCodec::MaximumBytes + 1, ' ');
        Require(!ProjectSpaceCodec::Decode(Broken, Document, Diagnostic), "Unbounded project read");
        ImGui::CreateContext();
        auto &IO       = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1100, 1000);
        IO.DeltaTime   = 1.0f / 60.0f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        ActivationOwnership Ownership(new ControlCentrePanel(Content / "FontArchives", Personal, Project));
        auto               *Panel = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Project panel initialization failed");
        Require(Panel->QueryAppearance().QueryApplied().Theme == ThemeCategory::Nord &&
                    Panel->QueryDiagnostic().find("font unavailable") != std::string::npos,
                "Project theme or font fallback not applied");
        const auto Frame = [&]()
        {
            (void)Panel->Integrate(1.0 / 60.0);
            Panel->PrepareInterface();
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        };
        const auto Settle = [&]()
        {
            for (int Ordinal = 0; Ordinal < 100; ++Ordinal)
                Frame();
        };
        const auto Click = [&](PlaneExtent Extent)
        {
            Require(Extent.Width() > 0 && Extent.Height() > 0, "Missing scope hit area");
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) * 0.5f, (Extent.MinimumY + Extent.MaximumY) * 0.5f);
            IO.AddMouseButtonEvent(0, true);
            Frame();
            IO.AddMouseButtonEvent(0, false);
            Frame();
            if (Panel->QueryConfirmation().QueryOpen())
            {
                const auto Primary = Panel->QueryConfirmation().QueryPrimaryExtent();
                IO.AddMousePosEvent((Primary.MinimumX + Primary.MaximumX) / 2,
                                    (Primary.MinimumY + Primary.MaximumY) / 2);
                Frame();
                IO.AddMouseButtonEvent(0, true);
                Frame();
                IO.AddMouseButtonEvent(0, false);
                Frame();
            }
        };
        Panel->ActivatePage(AppearancePageCategory::Theme);
        Settle();
        Require(Panel->QueryPreferenceScope() == PreferenceScopeCategory::Project, "Project save scope not selected");
        auto Appearance         = Panel->QueryAppearance().QueryDraft();
        Appearance.CornerRadius = 24;
        Panel->QueryAppearance().ConfigureDraft(Appearance);
        Click(Panel->QueryApplyExtent());
        Frame();
        Require(!Panel->QueryAppearance().QueryDirty(), "Native project Apply did not save");
        Require(ProjectSpaceCodec::Decode(Project, Document, Diagnostic) &&
                    Document.Appearance.Preferences.CornerRadius == 24 &&
                    Document.Appearance.RequestedFamily == "Absent family preserved",
                "Saving fallback overwrote requested font");
        Require(Document.Sections["extension"]["opaque"].value<std::string>() == "retain me" &&
                    Document.Sections["catalogue"].as_array()->size() == 1 &&
                    Document.Sections["engine"]["custom_engine"].value<int>() == 42,
                "Preference save erased unrelated sections");
        Capture(Evidence / "ProjectAppearance.ppm");
        Panel->ActivateSettingsPage(ControlCentrePageCategory::Input);
        Settle();
        auto Input             = Panel->QueryInput().QueryDraft();
        Input.MouseSensitivity = 80;
        Panel->QueryInput().ConfigureDraft(Input);
        Click(Panel->QueryApplyExtent());
        Frame();
        Require(ProjectSpaceCodec::Decode(Project, Document, Diagnostic) && Document.Input.MouseSensitivity == 80 &&
                    Document.Appearance.Preferences.CornerRadius == 24,
                "Project Input save erased appearance or failed");
        Capture(Evidence / "ProjectInput.ppm");
        Panel->ActivateSettingsPage(ControlCentrePageCategory::Notifications);
        Settle();
        auto Notifications        = Panel->QueryNotifications().QueryDraft();
        Notifications.HoldSeconds = 7;
        Panel->QueryNotifications().ConfigureDraft(Notifications);
        Click(Panel->QueryApplyExtent());
        Frame();
        Require(ProjectSpaceCodec::Decode(Project, Document, Diagnostic) && Document.Notifications.HoldSeconds == 7,
                "Notification settings not persisted");
        Capture(Evidence / "ProjectNotifications.ppm");
        const auto ProjectBytes = Read(Project);
        Click(Panel->QueryScopeExtent(0));
        Require(Panel->QueryPreferenceScope() == PreferenceScopeCategory::Personal,
                "Native Personal scope selector failed");
        Input.MouseSensitivity = 20;
        Panel->QueryInput().ConfigureDraft(Input);
        Require(Panel->FinalizeInputPreferences() && Read(Project) == ProjectBytes, "Personal save modified project");
        Require(ProjectSpaceCodec::Decode(Personal, Document, Diagnostic) && Document.InputPresent &&
                    !Document.AppearancePresent && !Document.NotificationsPresent,
                "Personal override copied unrelated project preferences");
        Require(Panel->ReloadProfiles() && Panel->QueryInput().QueryApplied().MouseSensitivity == 20,
                "Personal override reload failed");
        Require(Panel->ConfigurePersonalOverrides(false) && Panel->QueryInput().QueryApplied().MouseSensitivity == 80,
                "Disabling personal overrides did not restore project input");
        Require(Panel->ConfigurePersonalOverrides(true) && Panel->QueryInput().QueryApplied().MouseSensitivity == 20,
                "Enabling personal overrides failed");
        fs::remove(Personal);
        Require(Panel->ReloadProfiles() && !Panel->QueryPersonalOverrides() &&
                    Panel->QueryInput().QueryApplied().MouseSensitivity == 80,
                "Removed personal container retained stale override policy or preferences");
        Click(Panel->QueryScopeExtent(1));
        Input.MouseSensitivity = 90;
        Panel->QueryInput().ConfigureDraft(Input);
        Require(!Panel->ReloadProfiles(), "Reload silently discarded pending drafts");
        fs::create_directory(fs::path(EncodeContentPath(Project) + ".tmp"));
        Require(!Panel->FinalizeInputPreferences() && Panel->QueryInput().QueryDirty() && Read(Project) == ProjectBytes,
                "Failed replacement changed applied preferences or the project");
        fs::remove(fs::path(EncodeContentPath(Project) + ".tmp"));
        Panel->QueryInput().ResetDraft();
        for (const auto &Entry : fs::recursive_directory_iterator(Evidence))
            Require(Entry.path().extension() != ".toml", "Standalone TOML sidecar created");
        Require(Read(Template) == TemplateBytes, "Engine template was modified");
        Ownership.reset();
        // A valid project without profiles falls back to engine defaults, not sidecar files.
        Require(ProjectSpaceCodec::Decode(Project, Document, Diagnostic), Diagnostic.c_str());
        Document.Sections.insert_or_assign("workspace", toml::table{});
        Document.AppearancePresent = Document.InputPresent = Document.NotificationsPresent = false;
        Require(ProjectSpaceCodec::Encode(Project, Document, Diagnostic), Diagnostic.c_str());
        ActivationOwnership Fallback(new ControlCentrePanel(Content / "FontArchives", {}, Project));
        auto               *FallbackPanel = static_cast<ControlCentrePanel *>(Fallback.get());
        Require(FallbackPanel->Initialize() && FallbackPanel->Activate() &&
                    FallbackPanel->QueryAppearance().QueryApplied().Theme == Seed.Appearance.Preferences.Theme &&
                    FallbackPanel->QueryInput().QueryApplied() == Seed.Input,
                "Missing profiles did not use engine defaults");
        Fallback.reset();
        ImGui::DestroyContext();
        ImGui::CreateContext();
        auto &EmergencyIO       = ImGui::GetIO();
        EmergencyIO.IniFilename = nullptr;
        EmergencyIO.DisplaySize = ImVec2(1100, 1000);
        EmergencyIO.DeltaTime   = 1.0f / 60.0f;
        EmergencyIO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        ActivationOwnership Emergency(new ControlCentrePanel(Evidence / "UnavailableFonts", {}, Project));
        auto               *EmergencyPanel = static_cast<ControlCentrePanel *>(Emergency.get());
        Require(EmergencyPanel->Initialize() && EmergencyPanel->Activate() &&
                    EmergencyPanel->QueryTypefaces().QueryFamilyCount() == 0,
                "Missing font payloads prevented emergency UI startup");
        EmergencyPanel->ActivatePage(AppearancePageCategory::Theme);
        for (int Ordinal = 0; Ordinal < 100; ++Ordinal)
        {
            (void)EmergencyPanel->Integrate(1.0 / 60.0);
            EmergencyPanel->PrepareInterface();
            ImGui::NewFrame();
            EmergencyPanel->ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        }
        Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Emergency face produced no native UI geometry");
        Capture(Evidence / "EmergencyFont.ppm");
        Emergency.reset();
        ImGui::DestroyContext();
        std::cout
            << "PASS: single projectspace; fresh identity; Unicode; no overwrite; bounded/versioned reads; root paths; "
               "font/default fallback; native Project/Personal saves; notification persistence; extension/catalogue "
               "retention; "
               "override precedence; drafts; failed-write preservation; no TOML sidecars; four native captures\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
