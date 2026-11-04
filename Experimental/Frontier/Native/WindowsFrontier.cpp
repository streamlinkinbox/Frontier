/*==========================================================================================================================================
                                                           WINDOWSFRONTIER.CPP
==========================================================================================================================================*/
// 🧩 Run the native Project Browser; opening a project activates the editor workspace for that project.

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#ifndef FRONTIER_BUILD_COMMIT
#define FRONTIER_BUILD_COMMIT "local"
#endif
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Interface/VulkanPresentation.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Render/BrowserPreviewRendering.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/EditorWorkspace.h"
#include "UI/Editor/ProjectBrowserPanel.h"
#include "EverestWorkspace.h"
#include "EverestSceneViewport.h"
#ifdef FRONTIER_SOLIDARC_ENABLED
#include "UI/SolidArc/Browser/SolidArcBrowserLink.h"
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <objbase.h>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <map>
#include <string>
#include <utility>
#include <vector>

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECT BROWSER APPLICATION
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path ChooseProjectPath(GLFWwindow *Window, bool Save)
{
    wchar_t       Filename[32768] = {};
    OPENFILENAMEW Selection{};
    Selection.lStructSize  = sizeof(Selection);
    Selection.hwndOwner    = glfwGetWin32Window(Window);
    Selection.lpstrFilter  = L"Frontier projects (*.projectspace)\0*.projectspace\0\0";
    Selection.lpstrFile    = Filename;
    Selection.nMaxFile    = 32768;
    Selection.lpstrDefExt = L"projectspace";
    Selection.Flags       = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                            (Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const bool Accepted = Save ? GetSaveFileNameW(&Selection) != FALSE : GetOpenFileNameW(&Selection) != FALSE;
    return Accepted ? std::filesystem::path(Filename) : std::filesystem::path{};
}

std::vector<std::filesystem::path> ChooseImportFiles(GLFWwindow *Window)
{
    // 📎 Multi-select import picker; the prototype slices payloads to 200 files downstream.
    std::vector<wchar_t> Filenames(32768, L'\0');
    OPENFILENAMEW Selection{};
    Selection.lStructSize  = sizeof(Selection);
    Selection.hwndOwner    = glfwGetWin32Window(Window);
    // 📝 Every format the browser attaches (ClassifyBrowserImport); .solidarc models were missing, so they could not be picked.
    Selection.lpstrFilter  = L"Supported files (*.projectspace;*.solidarc;*.png;*.jpg;*.jpeg;*.webp;*.md)\0"
                             L"*.projectspace;*.solidarc;*.png;*.jpg;*.jpeg;*.webp;*.md\0"
                             L"Game projects (*.projectspace)\0*.projectspace\0"
                             L"SolidArc models (*.solidarc)\0*.solidarc\0"
                             L"Images and Markdown (*.png;*.jpg;*.jpeg;*.webp;*.md)\0*.png;*.jpg;*.jpeg;*.webp;*.md\0"
                             L"All files (*.*)\0*.*\0\0";
    Selection.lpstrFile    = Filenames.data();
    Selection.nMaxFile     = DWORD(Filenames.size());
    Selection.Flags        = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT |
                             OFN_EXPLORER;
    if (GetOpenFileNameW(&Selection) == FALSE) return {};
    std::vector<std::filesystem::path> Paths;
    const wchar_t *Cursor = Filenames.data();
    const std::wstring Head(Cursor);
    Cursor += Head.size() + 1;
    if (*Cursor == L'\0') Paths.emplace_back(Head); // 📎 Single selection returns the full path.
    else
        while (*Cursor != L'\0')
        {
            Paths.emplace_back(std::filesystem::path(Head) / Cursor);
            Cursor += wcslen(Cursor) + 1;
        }
    return Paths;
}

std::filesystem::path ChooseImportFolder(GLFWwindow *Window)
{
    wchar_t Display[MAX_PATH] = {};
    BROWSEINFOW Selection{};
    Selection.hwndOwner = glfwGetWin32Window(Window);
    Selection.pszDisplayName = Display;
    Selection.lpszTitle = L"Select a folder to import supported files from";
    Selection.ulFlags = BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    if (const PIDLIST_ABSOLUTE Chosen = SHBrowseForFolderW(&Selection))
    {
        wchar_t Folder[MAX_PATH] = {};
        const bool Resolved = SHGetPathFromIDListW(Chosen, Folder) != FALSE;
        CoTaskMemFree(Chosen);
        if (Resolved) return std::filesystem::path(Folder);
    }
    return std::filesystem::path{};
}

// 📝 New dialog › Browse: a .solidarc to create (no overwrite prompt: the browser never replaces a file) or a library folder.
std::filesystem::path ChooseCreationPath(GLFWwindow *Window, Frontier::BrowserCreation Creation)
{
    if (Creation == Frontier::BrowserCreation::GameProject) return ChooseProjectPath(Window, true);
    if (Creation == Frontier::BrowserCreation::SolidArcDocument)
    {
        wchar_t       Filename[32768] = {};
        OPENFILENAMEW Selection{};
        Selection.lStructSize = sizeof(Selection);
        Selection.hwndOwner   = glfwGetWin32Window(Window);
        Selection.lpstrFilter = L"SolidArc models (*.solidarc)\0*.solidarc\0\0";
        Selection.lpstrFile   = Filename;
        Selection.nMaxFile    = 32768;
        Selection.lpstrDefExt = L"solidarc";
        Selection.lpstrTitle  = L"Save the new SolidArc model as";
        Selection.Flags       = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        return GetSaveFileNameW(&Selection) != FALSE ? std::filesystem::path(Filename) : std::filesystem::path{};
    }
    wchar_t Display[MAX_PATH] = {};
    BROWSEINFOW Selection{};
    Selection.hwndOwner = glfwGetWin32Window(Window);
    Selection.pszDisplayName = Display;
    Selection.lpszTitle = L"Choose or create the texture library folder";
    Selection.ulFlags = BIF_NEWDIALOGSTYLE | BIF_RETURNONLYFSDIRS; // 📝 "Make New Folder" stays available.
    if (const PIDLIST_ABSOLUTE Chosen = SHBrowseForFolderW(&Selection))
    {
        wchar_t Folder[MAX_PATH] = {};
        const bool Resolved = SHGetPathFromIDListW(Chosen, Folder) != FALSE;
        CoTaskMemFree(Chosen);
        if (Resolved) return std::filesystem::path(Folder);
    }
    return std::filesystem::path{};
}

// 📝 CommandLineToArgvW quoting: backslashes double only before a quote, so "C:\" survives as one argument.
std::wstring QuoteCommandWord(const std::wstring &Word)
{
    std::wstring Quoted = L"\"";
    size_t Backslashes = 0;
    for (const wchar_t Letter : Word)
    {
        if (Letter == L'\\') { ++Backslashes; continue; }
        Quoted.append(Letter == L'"' ? Backslashes * 2 + 1 : Backslashes, L'\\');
        Backslashes = 0;
        Quoted.push_back(Letter);
    }
    Quoted.append(Backslashes * 2, L'\\');
    return Quoted + L"\"";
}

// 📝 TextureWorkspace.exe beside Frontier.exe, started on one material library folder (`--library <folder>`). The working
//    directory is inherited, so it resolves EngineContent exactly like the browser.
bool LaunchTextureWorkspace(const std::filesystem::path &Folder, std::string &Refusal)
{
    wchar_t Executable[MAX_PATH] = {};
    const DWORD Length = GetModuleFileNameW(nullptr, Executable, MAX_PATH);
    const std::filesystem::path Workspace = Length && Length < MAX_PATH
        ? std::filesystem::path(Executable).parent_path() / L"TextureWorkspace.exe" : std::filesystem::path{};
    std::error_code Failure;
    if (Workspace.empty() || !std::filesystem::is_regular_file(Workspace, Failure))
    {
        Refusal = "TextureWorkspace.exe was not found next to Frontier.exe.";
        return false;
    }
    std::wstring CommandLine = QuoteCommandWord(Workspace.wstring()) + L" --library " + QuoteCommandWord(Folder.wstring());
    STARTUPINFOW Startup{};
    PROCESS_INFORMATION Started{};
    Startup.cb = sizeof(Startup);
    if (!CreateProcessW(Workspace.c_str(), CommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup,
            &Started))
    {
        Refusal = "TextureWorkspace.exe could not be started (Windows error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(Started.hThread);
    CloseHandle(Started.hProcess);
    Frontier::DiagnosticJournal::Append("TextureWorkspace", "library=" + Frontier::DiagnosticJournal::EncodePath(Folder));
    return true;
}

// 📝 `Frontier.exe <project>.projectspace` (or a project dropped on Frontier.exe) opens that project instead of the browser.
std::filesystem::path ResolveLaunchProject()
{
    std::filesystem::path Requested;
    int Count = 0;
    LPWSTR *Words = CommandLineToArgvW(GetCommandLineW(), &Count);
    try
    {
        if (Words && Count > 1) Requested = std::filesystem::absolute(std::filesystem::path(Words[1]));
    }
    catch (...) { Requested.clear(); }
    if (Words) LocalFree(Words);
    return Requested;
}

int main()
{
    Frontier::DiagnosticJournal::Initialize("Frontier");
    Frontier::DiagnosticJournal::Append("Startup", "Frontier Project Browser with editor workspace activation");
    Frontier::ActivationOwnership AppearanceOwnership;
    Frontier::ControlCentrePanel *Appearance = nullptr;
    std::unique_ptr<Frontier::ProjectBrowserPanel> Browser;
    std::unique_ptr<Frontier::EditorWorkspace> Workspace;
    std::unique_ptr<Everest::EverestWorkspace> EverestSpace;
    std::unique_ptr<Frontier::BrowserPreviewRendering> PreviewRendering;
    std::vector<Frontier::BrowserCatalogueEntry> PreviewCatalogue;
    std::map<VkImageView, VkDescriptorSet> PreviewDescriptors;
    VkImageView LastLiveView = VK_NULL_HANDLE;
    bool PreviewFailed = false;
    // 📝 Everest's live Scene Viewport borrows the presentation device; its presented image is sampled in GENERAL layout.
    Frontier::VulkanPresentation *Presentation = nullptr;
    std::unique_ptr<Everest::EverestSceneViewport> SceneView;
    VkImageView SceneViewImage = VK_NULL_HANDLE;
    VkDescriptorSet SceneViewDescriptor = VK_NULL_HANDLE;
    int SceneViewJournalEntries = 0; // 📝 the first statuses and every new target reach the diagnostic file, capped
    const auto RetireSceneView = [&]()
    {
        SceneView.reset(); // 📝 Waits for the device before releasing anything the interface could still sample.
        if (SceneViewDescriptor) ImGui_ImplVulkan_RemoveTexture(SceneViewDescriptor);
        SceneViewDescriptor = VK_NULL_HANDLE;
        SceneViewImage = VK_NULL_HANDLE;
    };
    const auto ResolveBesideExecutable = [](const std::filesystem::path &Relative)
    {
        if (std::filesystem::exists(Relative)) return Relative;
        wchar_t Executable[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, Executable, MAX_PATH)) return std::filesystem::path(Executable).parent_path() / Relative;
        return Relative;
    };
    // 📝 Stored browser lambdas capture these by reference across frames; InitializeVulkan-scope
    // closures dangle after setup returns and AV on first touch (frame-2 tile resolve).
    const auto Describe = [&](VkImageView View) -> VkDescriptorSet
    {
        if (!View) return VK_NULL_HANDLE;
        const auto Cached = PreviewDescriptors.find(View);
        if (Cached != PreviewDescriptors.end()) return Cached->second;
        // 📝 Pooled target extents keep their images alive until Finalize, so lazily retained
        // descriptors stay valid without ever freeing a set a previous frame still samples.
        const auto Descriptor = ImGui_ImplVulkan_AddTexture(View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        PreviewDescriptors.emplace(View, Descriptor);
        return Descriptor;
    };
    const auto NoteFailure = [&](const char *Stage, const std::exception &Failure)
    {
        if (PreviewFailed) return;
        PreviewFailed = true; // 📝 Device loss fails every frame; journal the first refusal only.
        Frontier::DiagnosticJournal::Append("BrowserPreviewRefused", std::string(Stage) + ": " + Failure.what());
    };
    GLFWwindow *Window = nullptr;
    std::filesystem::path Preferences;
    bool ReturnRequested = false;
    std::filesystem::path LaunchProject = ResolveLaunchProject();
#ifdef FRONTIER_SOLIDARC_ENABLED
    // 📝 Open by file type: a .solidarc given to Frontier.exe (a drop on its icon, or Frontier picked in "Open with") belongs to
    //    SolidArc, so it is handed to SolidArcWorkspace.exe and Frontier does not open a window of its own.
    if (Frontier::QueryBrowserSolidArcExtension(Frontier::DiagnosticJournal::EncodePath(LaunchProject.extension())))
    {
        std::string Refusal;
        if (Frontier::SolidArc::LaunchSolidArcWorkspace(Frontier::SolidArc::LocateSolidArcWorkspace(), {}, LaunchProject, Refusal))
            return 0;
        Frontier::DiagnosticJournal::Append("SolidArcRefused", Refusal);
        std::wstring Message(size_t(MultiByteToWideChar(CP_UTF8, 0, Refusal.data(), int(Refusal.size()), nullptr, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, Refusal.data(), int(Refusal.size()), Message.data(), int(Message.size()));
        MessageBoxW(nullptr, Message.c_str(), L"Frontier", MB_OK | MB_ICONERROR);
        return 1;
    }
#endif
    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = "Frontier - Project Browser [" FRONTIER_BUILD_COMMIT "]";
    Callbacks.SceneRendering = true;
    Callbacks.Initialize = [&](GLFWwindow *Current)
    {
        Window = Current;
        Frontier::IconPresentation::Attach("EngineContent/Icons");
        const auto *Local = _wgetenv(L"LOCALAPPDATA");
        Preferences = Local ? std::filesystem::path(Local) / "Frontier/Personal.projectspace" : std::filesystem::path{};
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        if (!Appearance->Initialize() || !Appearance->Activate())
            throw std::runtime_error("Frontier appearance initialization failed: " + Appearance->QueryDiagnostic());
        Browser = std::make_unique<Frontier::ProjectBrowserPanel>([&](bool Save) { return ChooseProjectPath(Window, Save); });
        PreviewCatalogue = Frontier::ConstructBrowserCatalogue();
        Browser->PickImportFiles = [&]() { return ChooseImportFiles(Window); };
        Browser->PickImportFolder = [&]() { return ChooseImportFolder(Window); };
        Browser->PickCreationPath = [&](Frontier::BrowserCreation Creation) { return ChooseCreationPath(Window, Creation); };
        Browser->LaunchTextureWorkspace = LaunchTextureWorkspace;
        std::filesystem::path EverestProjectPath = "Projects/Everest/Everest.projectspace";
        if (!std::filesystem::exists(EverestProjectPath))
        {
            wchar_t Executable[MAX_PATH] = {};
            if (GetModuleFileNameW(nullptr, Executable, MAX_PATH))
                EverestProjectPath = std::filesystem::path(Executable).parent_path() / EverestProjectPath;
        }
        if (std::filesystem::exists(EverestProjectPath))
            Browser->AttachPaths({EverestProjectPath}, false);
#ifdef FRONTIER_SOLIDARC_ENABLED
        // 📝 SolidArc: .solidarc previews come from the SolidArc library; "Open in SolidArc" starts SolidArcWorkspace.exe.
        Frontier::SolidArc::ConnectSolidArcBrowser(*Browser, {Frontier::SolidArc::LocateSolidArcWorkspace()});
#endif
        Frontier::DiagnosticJournal::Append("Startup", "Project Browser initialized");
    };
    Callbacks.InitializeVulkan = [&](Frontier::VulkanPresentation &Display)
    {
        Presentation = &Display;
        std::filesystem::path ShaderDirectory = "EngineContent/ShaderArchives/Surface";
        if (!std::filesystem::exists(ShaderDirectory))
        {
            wchar_t Executable[MAX_PATH] = {};
            if (GetModuleFileNameW(nullptr, Executable, MAX_PATH))
                ShaderDirectory = std::filesystem::path(Executable).parent_path() / ShaderDirectory;
        }
        PreviewRendering = std::make_unique<Frontier::BrowserPreviewRendering>();
        PreviewRendering->Initialize(Display.QueryRasterAccess(), ShaderDirectory);
        Browser->QueryThumbnailImage = [&](const std::string &Key)
        {
            Frontier::BrowserPreviewImage Image;
            try
            {
                const auto ViewCut = Key.rfind(':');
                const auto RevisionCut = ViewCut == std::string::npos ? std::string::npos : Key.rfind(':', ViewCut - 1);
                if (ViewCut == std::string::npos || RevisionCut == std::string::npos) return Image;
                const std::string Token = Key.substr(0, RevisionCut);
                const size_t Revision = std::stoul(Key.substr(RevisionCut + 1, ViewCut - RevisionCut - 1));
                const size_t View = std::stoul(Key.substr(ViewCut + 1));
                const auto Entry = std::find_if(PreviewCatalogue.begin(), PreviewCatalogue.end(),
                    [&](const auto &Candidate) { return Candidate.Token == Token; });
                if (Entry == PreviewCatalogue.end()) return Image;
                const auto Configuration = Frontier::ConfigureBrowserPreview(*Entry, View, Revision);
                PreviewRendering->RequestThumbnail(Key, Configuration);
                if (!PreviewRendering->QueryThumbnail(Key).Ready) return Image;
                static int TracedReady = 0;
                if (TracedReady < 4)
                    Frontier::DiagnosticJournal::Append("BrowserTileReady", "key=" + Key);
                Image.Texture = (uint64_t)Describe(PreviewRendering->QueryThumbnailView(Key));
                if (TracedReady < 4)
                {
                    Frontier::DiagnosticJournal::Append("BrowserTileDescribed", "key=" + Key);
                    ++TracedReady;
                }
            }
            catch (const std::exception &Failure) { NoteFailure("QueryThumbnailImage", Failure); }
            return Image;
        };
        Browser->QueryConfiguredThumbnail = [&](const std::string &Key, const Frontier::BrowserPreviewConfiguration &Configuration)
        {
            Frontier::BrowserPreviewImage Image;
            try
            {
                PreviewRendering->RequestThumbnail(Key, Configuration);
                if (!PreviewRendering->QueryThumbnail(Key).Ready) return Image;
                Image.Texture = (uint64_t)Describe(PreviewRendering->QueryThumbnailView(Key));
            }
            catch (const std::exception &Failure) { NoteFailure("QueryConfiguredThumbnail", Failure); }
            return Image;
        };
        Browser->QueryLiveImage = [&]()
        {
            Frontier::BrowserPreviewImage Image;
            try
            {
                // Hold the last completed frame while the live target re-renders: the queue
                // samples after the browser submit, so the current view always holds fresh pixels
                // and only a recreated target (or the never-rendered first frames) reports null.
                if (PreviewRendering->QueryLiveReady()) LastLiveView = PreviewRendering->QueryLiveView();
                const auto Current = PreviewRendering->QueryLiveView();
                if (Current && Current == LastLiveView)
                    Image.Texture = (uint64_t)Describe(Current);
            }
            catch (const std::exception &Failure) { NoteFailure("QueryLiveImage", Failure); }
            return Image;
        };
        Browser->ConfigureLivePreview = [&](const Frontier::BrowserPreviewConfiguration &Configuration,
                                            const Frontier::BrowserPreviewCamera &Camera, int Channel, int Width,
                                            int Height)
        {
            try
            {
                PreviewRendering->ConfigureLive(Configuration, Camera, Channel, Width, Height);
            }
            catch (const std::exception &Failure) { NoteFailure("ConfigureLivePreview", Failure); }
        };
        Browser->CaptureLiveThumbnail = [&](const std::string &Key, const Frontier::BrowserPreviewConfiguration &Configuration,
                                           const Frontier::BrowserPreviewCamera &Camera, int Channel)
        {
            try
            {
                PreviewRendering->CaptureThumbnail(Key, Configuration, Camera, Channel);
            }
            catch (const std::exception &Failure) { NoteFailure("CaptureLiveThumbnail", Failure); }
        };
        Browser->QueryImportedImage = [&](const std::string &Token, const std::vector<uint8_t> &ImageBytes)
        {
            Frontier::BrowserPreviewImage Image;
            try
            {
                PreviewRendering->RequestImportImage(Token, ImageBytes);
                const auto Import = PreviewRendering->QueryImportImage(Token);
                if (!Import.Ready && !Import.Diagnostic.empty()) Image.Failed = true;
                if (!Import.Ready) return Image;
                Image.Texture = (uint64_t)Describe(PreviewRendering->QueryImportView(Token));
                Image.Width = float(Import.Width);
                Image.Height = float(Import.Height);
            }
            catch (const std::exception &Failure) { NoteFailure("QueryImportedImage", Failure); }
            return Image;
        };
        Display.ConfigureRendering(
            [&](VkCommandBuffer Commands, uint64_t Completed, uint64_t Serial)
            {
                try
                {
                    PreviewRendering->ConstructCommands(Commands, Completed, Serial);
                }
                catch (const std::exception &Failure) { NoteFailure("ConstructCommands", Failure); }
            },
            [&]() { PreviewRendering->ConfirmSubmission(); });
        Frontier::DiagnosticJournal::Append("Startup", "Browser preview rendering initialized");
    };
    const auto ActivateProject = [&](const std::filesystem::path &Requested)
    {
        Frontier::ProjectSpaceDocument Document;
        std::string Diagnostic;
        if (!Frontier::ProjectSpaceCodec::Decode(Requested, Document, Diagnostic))
            throw std::runtime_error("Project activation failed: " + Diagnostic);
        Workspace.reset();
        RetireSceneView();
        EverestSpace.reset();
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences, Requested));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        if (!Appearance->Initialize() || !Appearance->Activate())
            throw std::runtime_error("Project activation failed: " + Appearance->QueryDiagnostic());
        Frontier::EditorProjectSummary Summary;
        Summary.Name = Document.Sections["project"]["name"].value_or(std::string("Project"));
        Summary.Location = Frontier::DiagnosticJournal::EncodePath(Requested);
        Summary.Identity = Document.Sections["project"]["identity"].value_or(std::string{});
        if (const auto *Levels = Document.Sections["levels"].as_array()) Summary.Levels = Levels->size();
        if (const auto *Roots = Document.Sections["content_roots"].as_array()) Summary.Roots = Roots->size();
        if (const auto *Catalogue = Document.Sections["catalogue"].as_array()) Summary.Catalogue = Catalogue->size();
        Summary.StartupLevel = Document.Sections["engine"]["startup_level"].value_or(std::string{});
        const std::string WindowLabel = Summary.Name;
        if (Summary.Identity == "project-everest-v1" || Summary.Name == "Everest" || Summary.Name == "Project Everest")
        {
            EverestSpace = std::make_unique<Everest::EverestWorkspace>(*Appearance, Requested.parent_path());
            EverestSpace->EvaluateReturnToBrowser = [&]() { ReturnRequested = true; };
            // 📝 Unreal-style camera drags: the pointer is hidden and held in the window that shows the viewport, with raw
            //    motion where the platform offers it; GLFW returns it to where the drag began. Only windows ImGui still owns
            //    are touched (a docked-out viewport window may have closed).
            EverestSpace->ConstrainPointer = [](void *PlatformWindow, bool ConstraintEnabled)
            {
                bool Owned = false;
                for (const ImGuiViewport *Viewport : ImGui::GetPlatformIO().Viewports)
                    Owned = Owned || Viewport->PlatformHandle == PlatformWindow;
                if (!Owned) return;
                GLFWwindow *Target = static_cast<GLFWwindow *>(PlatformWindow);
                if (glfwRawMouseMotionSupported())
                    glfwSetInputMode(Target, GLFW_RAW_MOUSE_MOTION, ConstraintEnabled ? GLFW_TRUE : GLFW_FALSE);
                glfwSetInputMode(Target, GLFW_CURSOR, ConstraintEnabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            };
            EverestSpace->PresentSceneViewport = [&](const Everest::SceneViewportRequest &Request)
            {
                Everest::SceneViewportImage Image;
                try
                {
                    if (!SceneView)
                    {
                        const Frontier::VulkanDeviceAccess Device = Presentation ? Presentation->QueryDeviceAccess() : Frontier::VulkanDeviceAccess{};
                        SceneView = std::make_unique<Everest::EverestSceneViewport>(
                            EverestSpace->QueryProjectRoot(), ResolveBesideExecutable("EngineContent/ShaderArchives"), &Device);
                    }
                    const Everest::SceneViewportRefresh Outcome =
                        SceneView->Render(Request, EverestSpace->QuerySceneWorld(), EverestSpace->QueryElapsedSeconds());
                    Image.Status  = Outcome.Status;
                    Image.Readout = Outcome.Readout;
                    if ((Outcome.View != SceneViewImage || SceneViewJournalEntries < 12) && SceneViewJournalEntries < 32)
                    {
                        Frontier::DiagnosticJournal::Append("SceneViewport", (Outcome.View ? "image " : "no image ") + Outcome.Status);
                        ++SceneViewJournalEntries;
                    }
                    if (Outcome.View != SceneViewImage)
                    {
                        // 📝 Render() retired the queue before replacing its targets: the previous set is no longer sampled.
                        if (SceneViewDescriptor) ImGui_ImplVulkan_RemoveTexture(SceneViewDescriptor);
                        SceneViewDescriptor = Outcome.View ? ImGui_ImplVulkan_AddTexture(Outcome.View, VK_IMAGE_LAYOUT_GENERAL) : VK_NULL_HANDLE;
                        SceneViewImage = Outcome.View;
                    }
                    Image.Texture = (uint64_t)SceneViewDescriptor;
                }
                catch (const std::exception &Failure)
                {
                    Image.Status = std::string("Scene viewport failed: ") + Failure.what();
                    Frontier::DiagnosticJournal::Append("SceneViewportRefused", Failure.what());
                }
                return Image;
            };
        }
        else
        {
            Workspace = std::make_unique<Frontier::EditorWorkspace>(*Appearance);
            Workspace->ConfigureProject(std::move(Summary));
            Workspace->EvaluateReturnToBrowser = [&]() { ReturnRequested = true; };
        }
        glfwSetWindowTitle(Window, ("Frontier - " + WindowLabel).c_str());
        Frontier::DiagnosticJournal::Append("ProjectSelection", Frontier::DiagnosticJournal::EncodePath(Requested));
    };
    Callbacks.PrepareInterface = [&]() { Appearance->PrepareInterface(); };
    Callbacks.RecoverWindows = [&]()
    {
        if (EverestSpace) EverestSpace->QueryDockspace().RecoverPlatformWindows();
        if (Workspace) Workspace->QueryPanels().RecoverPlatformWindows();
    };
    Callbacks.ConfigurePlatform = [&]()
    {
        auto Requested = Browser->ConsumeLaunchPath();
        if (Requested.empty() && !LaunchProject.empty())
        {
            Requested = std::exchange(LaunchProject, std::filesystem::path{});
            Frontier::DiagnosticJournal::Append("ProjectSelection", "Command line: " + Frontier::DiagnosticJournal::EncodePath(Requested));
            if (!std::filesystem::is_regular_file(Requested))
            {
                Frontier::DiagnosticJournal::Append("ProjectSelection", "Command-line project not found; staying in the Project Browser");
                Requested.clear();
            }
        }
        if (!Requested.empty()) ActivateProject(Requested);
        if (ReturnRequested && (Workspace || EverestSpace))
        {
            ReturnRequested = false;
            Workspace.reset();
            RetireSceneView();
            EverestSpace.reset();
            AppearanceOwnership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences));
            Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
            if (!Appearance->Initialize() || !Appearance->Activate())
                throw std::runtime_error("Project Browser reactivation failed: " + Appearance->QueryDiagnostic());
            Browser->ResetSelection();
            glfwSetWindowTitle(Window, Callbacks.Title.c_str());
            Frontier::DiagnosticJournal::Append("ProjectSelection", "Returned to the Project Browser");
        }
        if (EverestSpace) (void)EverestSpace->ConfigurePlatform();
        if (Workspace) (void)Workspace->ConfigurePlatform();
    };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        if (EverestSpace)
        {
            (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
            EverestSpace->ConstructInterface(ImGui::GetIO().DeltaTime);
            if (MainVisible) Appearance->ConstructInterface();
        }
        else if (Workspace)
        {
            (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
            Workspace->ConstructInterface();
            if (MainVisible) Appearance->ConstructInterface();
        }
        else if (MainVisible)
        {
            Browser->ConfigurePalette(Frontier::ControlKit::Palette());
            Browser->ConstructInterface();
        }
    };
    Callbacks.AfterRender = [&]()
    {
        if (Workspace) Workspace->IntegrateLayoutPersistence(ImGui::GetIO().DeltaTime);
    };
    Callbacks.Finalize = [&]()
    {
        // 📝 The shared application loop has retired all GPU work before this callback.
        for (const auto &Entry : PreviewDescriptors)
            if (Entry.second) ImGui_ImplVulkan_RemoveTexture(Entry.second);
        PreviewDescriptors.clear();
        if (PreviewRendering) PreviewRendering->Finalize();
        PreviewRendering.reset();
        RetireSceneView();
        EverestSpace.reset();
        Workspace.reset();
        Browser.reset();
        AppearanceOwnership.reset();
        Frontier::DiagnosticJournal::Append("Shutdown", "Frontier application resources reclaimed");
    };
    return Frontier::RunWindowsApplication(Callbacks);
}
