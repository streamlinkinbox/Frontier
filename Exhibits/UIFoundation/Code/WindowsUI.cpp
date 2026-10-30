/*==========================================================================================================================================
                                                               WINDOWSUI.CPP
==========================================================================================================================================*/
// 🧩 Select the Frontier startup workspace while Engine owns the shared native application loop.

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/IconPresentation.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/EditorWorkspace.h"
#include "UI/Editor/ProjectBrowserPanel.h"
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <iostream>
#include <cstring>

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRONTIER APPLICATION
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

int RunWorkspace()
{
    std::filesystem::path ProjectPath;
    int                   ArgumentCount = 0;
    auto                **Arguments     = CommandLineToArgvW(GetCommandLineW(), &ArgumentCount);
    bool                  CreateProject = false;
    for (int Index = 1; Arguments && Index < ArgumentCount; ++Index)
    {
        const std::wstring Argument = Arguments[Index];
        if ((Argument == L"--project" || Argument == L"--create-project") && Index + 1 < ArgumentCount)
        {
            CreateProject = Argument == L"--create-project";
            ProjectPath   = Arguments[++Index];
        }
        else
        {
            std::cerr << "Usage: FrontierUI.exe [--project file.projectspace | --create-project file.projectspace]\n";
            LocalFree(Arguments);
            return 2;
        }
    }
    if (Arguments)
        LocalFree(Arguments);
    if (CreateProject)
    {
        const auto Template =
            Frontier::ResolveContentDirectory("EngineContent/Defaults") / "BlankProject/Blank.projectspace";
        std::string Diagnostic;
        if (!Frontier::ProjectSpaceCodec::CreateBlank(Template, ProjectPath,
                                                      Frontier::EncodeContentPath(ProjectPath.stem()), Diagnostic))
        {
            std::cerr << "Project creation refused: " << Diagnostic << '\n';
            return 1;
        }
    }
    if (!ProjectPath.empty())
    {
        Frontier::ProjectSpaceDocument Candidate;
        std::string Diagnostic;
        if (!Frontier::ProjectSpaceCodec::Decode(ProjectPath, Candidate, Diagnostic))
        {
            std::cerr << "Project opening refused: " << Diagnostic << '\n';
            return 1;
        }
    }
    Frontier::ActivationOwnership Ownership;
    Frontier::ControlCentrePanel *Panel = nullptr;
    std::unique_ptr<Frontier::ProjectBrowserPanel> Browser;
    std::unique_ptr<Frontier::EditorWorkspace> Workspace;
    std::filesystem::path Preferences;
    GLFWwindow *Window = nullptr;
    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = ProjectPath.empty() ? "Frontier - Project Browser" : "Frontier - Editor Workspace";
    Callbacks.Initialize = [&](GLFWwindow *CurrentWindow)
    {
        Window = CurrentWindow;
        Frontier::IconPresentation::Attach("EngineContent/Icons");
        const wchar_t *LocalDirectory = _wgetenv(L"LOCALAPPDATA");
        Preferences = LocalDirectory ? std::filesystem::path(LocalDirectory)/L"Frontier/Personal.projectspace" : std::filesystem::path{};
        Ownership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences, ProjectPath));
        Panel = static_cast<Frontier::ControlCentrePanel *>(Ownership.get());
        if (!Panel->Initialize() || !Panel->Activate()) throw std::runtime_error("UI initialization failed: "+Panel->QueryDiagnostic());
        Browser = std::make_unique<Frontier::ProjectBrowserPanel>([&](bool Save) { return ChooseProjectPath(Window, Save); });
        if (!ProjectPath.empty())
        {
            Workspace = std::make_unique<Frontier::EditorWorkspace>(*Panel);
            Frontier::ProjectSpaceDocument Document;
            std::string Diagnostic;
            if (Frontier::ProjectSpaceCodec::Decode(ProjectPath, Document, Diagnostic))
                Workspace->ConfigureProject({Document.Sections["project"]["name"].value_or(std::string("Untitled")),
                    Frontier::EncodeContentPath(ProjectPath)});
        }
    };
    Callbacks.ConfigurePlatform = [&]()
    {
        const auto RequestedProject = Browser->ConsumeLaunchPath();
        if (!RequestedProject.empty())
        {
            Workspace.reset();
            Ownership.reset();
            Ownership.reset(new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences, RequestedProject));
            Panel = static_cast<Frontier::ControlCentrePanel *>(Ownership.get());
            if (!Panel->Initialize() || !Panel->Activate()) throw std::runtime_error("Project activation failed: "+Panel->QueryDiagnostic());
            Workspace = std::make_unique<Frontier::EditorWorkspace>(*Panel);
            Frontier::ProjectSpaceDocument Document;
            std::string Diagnostic;
            if (Frontier::ProjectSpaceCodec::Decode(RequestedProject, Document, Diagnostic))
                Workspace->ConfigureProject({Document.Sections["project"]["name"].value_or(std::string("Untitled")),
                    Frontier::EncodeContentPath(RequestedProject)});
            glfwSetWindowTitle(Window, "Frontier - Editor Workspace");
        }
        if (Workspace) (void)Workspace->ConfigurePlatform();
    };
    Callbacks.PrepareInterface = [&]() { Panel->PrepareInterface(); };
    Callbacks.RecoverWindows = [&]() { if (Workspace) Workspace->QueryPanels().RecoverPlatformWindows(); };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        if (Workspace)
        {
            (void)Panel->Integrate(ImGui::GetIO().DeltaTime);
            Workspace->ConstructInterface();
            if (MainVisible) Panel->ConstructInterface();
        }
        else if (MainVisible)
        {
            Browser->ConfigurePalette(Frontier::ControlKit::Palette());
            Browser->ConstructInterface();
        }
    };
    Callbacks.AfterRender = [&]() { if (Workspace) Workspace->IntegrateLayoutPersistence(ImGui::GetIO().DeltaTime); };
    Callbacks.Finalize = [&]() { Workspace.reset(); Browser.reset(); Ownership.reset(); };
    return Frontier::RunWindowsApplication(Callbacks);
}

int main()
{
    try
    {
        return RunWorkspace();
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "Severe editor failure: " << Failure.what() << '\n';
        // 📝 Native fallback requires no working renderer. The reference full-screen presentation is a later milestone.
        wchar_t    Description[2048]{};
        const auto Length  = static_cast<int>(std::min<size_t>(std::strlen(Failure.what()), 2047));
        const auto Decoded = MultiByteToWideChar(CP_UTF8, 0, Failure.what(), Length, Description, 2047);
        MessageBoxW(nullptr, Decoded > 0 ? Description : L"An editor failure prevented normal operation.",
                    L"Frontier - Severe failure", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        return 1;
    }
    catch (...)
    {
        MessageBoxW(nullptr, L"An unknown editor failure prevented normal operation.", L"Frontier - Severe failure",
                    MB_OK | MB_ICONERROR | MB_TASKMODAL);
        return 1;
    }
}
