//============================================================================================================================================
//                                                  PROJECTOPENINGSEQUENCE.CPP
//============================================================================================================================================
// 📦 Borderless ImGui project browser with a responsive loading card and console until the child renderer is ready.

#include "ProjectOpeningSequence.h"
#include "../ProjectInterchange/ProjectSpecification.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <shobjidl.h>
#include <imgui.h>
#include "ProjectOpeningPanel.h"
#include "ProjectPreviewCodec.h"
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace Frontier
{
namespace
{
std::wstring Widen(const std::string& Text)
{
    const int Count = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, nullptr, 0);
    std::wstring Result(static_cast<size_t>(Count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, Result.data(), Count);
    if (!Result.empty()) Result.pop_back();
    return Result;
}

std::string Narrow(const std::filesystem::path& Path)
{
    const auto Text = Path.u8string();
    return std::string(reinterpret_cast<const char*>(Text.data()), Text.size());
}

std::wstring Quote(const std::wstring& Text)
{
    std::wstring Result = L"\"";
    size_t Slashes = 0u;
    for (const wchar_t Character : Text)
    {
        if (Character == L'\\') { ++Slashes; continue; }
        Result.append(Slashes * (Character == L'"' ? 2u : 1u), L'\\');
        Slashes = 0u;
        if (Character == L'"') Result += L'\\';
        Result += Character;
    }
    Result.append(Slashes * 2u, L'\\');
    return Result + L'"';
}

std::vector<std::filesystem::path> Scan(const std::filesystem::path& Root, bool Projects)
{
    std::vector<std::filesystem::path> Found;
    std::error_code Error;
    std::filesystem::recursive_directory_iterator Walk(Root, std::filesystem::directory_options::skip_permission_denied, Error), End;
    for (; !Error && Walk != End && Found.size() < 4096u; Walk.increment(Error))
    {
        if (Walk.depth() >= 5) Walk.disable_recursion_pending();
        if (!Walk->is_regular_file(Error)) continue;
        auto Extension = Walk->path().extension().string();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return char(std::tolower(C)); });
        if (Projects ? Extension == ".frontier" : (Extension == ".gltf" || Extension == ".glb")) Found.push_back(Walk->path());
    }
    std::sort(Found.begin(), Found.end());
    return Found;
}

bool Browse(HWND Window, bool Folder, std::filesystem::path& Result)
{
    IFileOpenDialog* Dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog)))) return false;
    FILEOPENDIALOGOPTIONS Flags = 0;
    Dialog->GetOptions(&Flags);
    Dialog->SetOptions(Flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (Folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    const COMDLG_FILTERSPEC Filters[] = {{ L"Scene (.gltf, .glb)", L"*.gltf;*.glb" }};
    if (!Folder) Dialog->SetFileTypes(1u, Filters);
    bool Chosen = false;
    if (SUCCEEDED(Dialog->Show(Window)))
    {
        IShellItem* Item = nullptr;
        if (SUCCEEDED(Dialog->GetResult(&Item)))
        {
            PWSTR Name = nullptr;
            if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &Name)))
            {
                Result = Name;
                CoTaskMemFree(Name);
                Chosen = true;
            }
            Item->Release();
        }
    }
    Dialog->Release();
    return Chosen;
}

LRESULT CALLBACK OpeningMessages(HWND Window, UINT Message, WPARAM Word, LPARAM Long)
{
    if (ImGui_ImplWin32_WndProcHandler(Window, Message, Word, Long)) return 1;
    if (Message == WM_NCHITTEST)
    {
        POINT Point{ static_cast<short>(LOWORD(Long)), static_cast<short>(HIWORD(Long)) };
        ScreenToClient(Window, &Point);
        RECT Area{}; GetClientRect(Window, &Area);
        if (Point.y < 44 && Point.x < Area.right - 60) return HTCAPTION;
    }
    if (Message == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(Window, Message, Word, Long);
}
}

int RunProjectBrowser(const char* ProofImage)
{
    const HRESULT Apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t ImageName[32768]{};
    GetModuleFileNameW(nullptr, ImageName, static_cast<DWORD>(std::size(ImageName)));
    const std::filesystem::path Image(ImageName);
    auto Root = Image.parent_path();
    for (auto Candidate = Root; !Candidate.empty(); Candidate = Candidate.parent_path())
    {
        if (std::filesystem::is_directory(Candidate / "Projects")) { Root = Candidate / "Projects"; break; }
        if (std::filesystem::is_directory(Candidate / "Frontier/Projects")) { Root = Candidate / "Frontier/Projects"; break; }
        if (Candidate == Candidate.parent_path()) break;
    }
    WNDCLASSW Class{};
    Class.lpfnWndProc = OpeningMessages;
    Class.hInstance = GetModuleHandleW(nullptr);
    Class.lpszClassName = L"FrontierProjectBrowser";
    Class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&Class);
    RECT Desktop{}; SystemParametersInfoW(SPI_GETWORKAREA, 0u, &Desktop, 0u);
    HWND Window = CreateWindowExW(WS_EX_APPWINDOW, Class.lpszClassName, L"Frontier — Open project", WS_POPUP,
        (Desktop.left + Desktop.right - 840) / 2, (Desktop.top + Desktop.bottom - 640) / 2, 840, 640,
        nullptr, nullptr, Class.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC Swap{};
    Swap.BufferCount = 2; Swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; Swap.OutputWindow = Window;
    Swap.SampleDesc.Count = 1; Swap.Windowed = TRUE; Swap.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* Commands = nullptr;
    IDXGISwapChain* Chain = nullptr;
    HRESULT Created = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0u, nullptr, 0u,
        D3D11_SDK_VERSION, &Swap, &Chain, &Device, nullptr, &Commands);
    if (FAILED(Created)) Created = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0u, nullptr, 0u,
        D3D11_SDK_VERSION, &Swap, &Chain, &Device, nullptr, &Commands);
    ID3D11Texture2D* Back = nullptr;
    ID3D11RenderTargetView* Target = nullptr;
    if (!Window || FAILED(Created) || FAILED(Chain->GetBuffer(0, IID_PPV_ARGS(&Back))) ||
        FAILED(Device->CreateRenderTargetView(Back, nullptr, &Target)))
    {
        MessageBoxW(Window, L"The project browser display could not start. The console contains startup diagnostics.", L"Frontier", MB_OK | MB_ICONERROR);
        if (Back) Back->Release(); if (Target) Target->Release(); if (Chain) Chain->Release();
        if (Commands) Commands->Release(); if (Device) Device->Release(); if (Window) DestroyWindow(Window);
        if (SUCCEEDED(Apartment)) CoUninitialize();
        return 1;
    }
    Back->Release();
    ImGui::CreateContext();
    auto& Io = ImGui::GetIO(); Io.IniFilename = nullptr;
    const auto Font = Root.parent_path() / "EngineContent/Fonts/SunReference/DMSans-Regular.ttf";
    if (std::filesystem::exists(Font)) Io.Fonts->AddFontFromFileTTF(Narrow(Font).c_str(), 16.0f);
    ApplyProjectOpeningTheme();
    ImGui_ImplWin32_Init(Window); ImGui_ImplDX11_Init(Device, Commands);
    ShowWindow(Window, SW_SHOW); UpdateWindow(Window);
    ProjectOpeningSpecification Opening;
    auto& Directory = Opening.Directory;
    auto& Projects = Opening.Projects;
    auto& Scenes = Opening.Scenes;
    auto& SelectedProject = Opening.SelectedProject;
    auto& SelectedScene = Opening.SelectedScene;
    auto& Refusal = Opening.Refusal;
    auto& Quality = Opening.Quality;
    auto& Rendering = Opening.Rendering;
    auto& Scale = Opening.Scale;
    std::snprintf(Directory, sizeof(Directory), "%s", Narrow(Root).c_str());
    Projects = Scan(Root, true);
    ProjectSpecification Specification;
    bool Running = true, Launched = false;
    int Result = 0, ProofFrames = 0;
    if (!Projects.empty()) { SelectedProject = Projects.front(); Opening.SelectionChanged = true; }
    double OpeningStarted = 0.0;
    PROCESS_INFORMATION Child{};
    HANDLE Ready = nullptr;
    std::map<std::filesystem::path, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> PreviewTextures;
    bool RefreshPreviews = false;
    while (Running)
    {
        MSG Message{};
        while (PeekMessageW(&Message, nullptr, 0u, 0u, PM_REMOVE))
        {
            if (Message.message == WM_QUIT) Running = false;
            TranslateMessage(&Message); DispatchMessageW(&Message);
        }
        if (!Running) break;
        if (Child.hProcess)
        {
            if (WaitForSingleObject(Ready, 0) == WAIT_OBJECT_0) { Launched = true; break; }
            if (WaitForSingleObject(Child.hProcess, 0) == WAIT_OBJECT_0)
            {
                DWORD Code = 0; GetExitCodeProcess(Child.hProcess, &Code);
                Refusal = "Project startup stopped (exit " + std::to_string(Code) + "). See the console, then correct the scene or project and retry.";
                CloseHandle(Child.hProcess); CloseHandle(Child.hThread); CloseHandle(Ready);
                Child = {}; Ready = nullptr;
            }
        }
        if (RefreshPreviews)
        {
            // 📝 Reclaim before recording, never while a draw list still references the previous images.
            PreviewTextures.clear();
            Opening.Previews.clear();
            RefreshPreviews = false;
        }
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        Opening.Loading = Child.hProcess != nullptr;
        Opening.LoadingSeconds = ImGui::GetTime() - OpeningStarted;
        RecordProjectOpeningPanel(Opening);
        if (Opening.Close) Running = false;
        if (Opening.BrowseDirectory)
        {
            Opening.BrowseDirectory = false;
            std::filesystem::path Choice;
            if (Browse(Window, true, Choice))
            {
                std::snprintf(Directory, sizeof(Directory), "%s", Narrow(Choice).c_str());
                Opening.Scan = true;
            }
        }
        if (Opening.Scan)
        {
            Projects = Scan(std::filesystem::u8path(Directory), true);
            Opening.Scan = false;
            RefreshPreviews = true;
            if (std::find(Projects.begin(), Projects.end(), SelectedProject) == Projects.end())
                SelectedProject = Projects.empty() ? std::filesystem::path{} : Projects.front();
            if (SelectedProject.empty())
            {
                SelectedScene.clear();
                Scenes.clear();
                Refusal.clear();
            }
            else Opening.SelectionChanged = true;
        }
        if (Opening.SelectionChanged)
        {
            Opening.SelectionChanged = false;
            SelectedScene.clear(); Scenes.clear();
            if (DecodeProjectSpecification(SelectedProject, Specification, Refusal))
            {
                Scenes = Scan(Specification.ContentLocation, false);
                SelectedScene = Specification.OpeningSceneLocation;
                if (std::find(Scenes.begin(), Scenes.end(), SelectedScene) == Scenes.end()) Scenes.insert(Scenes.begin(), SelectedScene);
            }
        }
        if (Opening.BrowseScene) { Browse(Window, false, SelectedScene); Opening.BrowseScene = false; }
        if (Opening.Open)
        {
            Opening.Open = false;

                ProjectSpecification Validated;
                if (DecodeProjectSpecification(SelectedProject, Validated, Refusal))
                {
                    if (!std::filesystem::is_regular_file(SelectedScene)) Refusal = "The selected scene is missing. Generate the project content or choose an existing scene.";
                    else if (!std::filesystem::is_regular_file(Validated.CodeImageLocation)) Refusal = "The project's DLL is missing. Build or select a complete project package.";
                    else
                    {
                        const auto Event = L"Local\\FrontierOpening-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
                        Ready = CreateEventW(nullptr, TRUE, FALSE, Event.c_str());
                        const wchar_t* Scales[] = {L"0.5", L"0.75", L"1.0"};
                        std::wstring Command = Quote(Image.wstring()) + L" " + Quote(SelectedProject.wstring()) + L" --scene " + Quote(SelectedScene.wstring())
                            + L" --quality " + std::to_wstring(Quality) + L" --render-preset " + std::to_wstring(Rendering)
                            + L" --render-scale " + Scales[Scale] + L" --launcher-event " + Quote(Event);
                        STARTUPINFOW Startup{}; Startup.cb = sizeof(Startup);
                        if (!Ready || !CreateProcessW(Image.c_str(), Command.data(), nullptr, nullptr, FALSE, 0, nullptr, Image.parent_path().c_str(), &Startup, &Child))
                        {
                            Refusal = "Could not start Frontier (Windows error " + std::to_string(GetLastError()) + ").";
                            if (Ready) CloseHandle(Ready); Ready = nullptr;
                        }
                        else { Refusal.clear(); OpeningStarted = ImGui::GetTime(); }
                    }
                }

        }
        unsigned Decoded = 0;
        for (const auto& Project : Opening.PreviewRequests)
        {
            if (Opening.Previews.contains(Project)) continue;
            if (Decoded++ == 2) break;
            if (Opening.Previews.size() >= 32)
            {
                const auto Evicted = std::find_if(Opening.Previews.begin(), Opening.Previews.end(), [&](const auto& Entry)
                {
                    return std::find(Opening.PreviewRequests.begin(), Opening.PreviewRequests.end(), Entry.first) == Opening.PreviewRequests.end();
                });
                if (Evicted == Opening.Previews.end()) break;
                PreviewTextures.erase(Evicted->first);
                Opening.Previews.erase(Evicted);
            }
            auto& Preview = Opening.Previews[Project];
            if (!FindProjectPreview(Project, Preview.Source, Preview.Refusal)) continue;
            ProjectPreviewPixels Pixels;
            if (!DecodeProjectPreview(Preview.Source, Pixels))
            {
                Preview.Refusal = Pixels.Refusal;
                continue;
            }
            D3D11_TEXTURE2D_DESC Description{};
            Description.Width = Pixels.Width;
            Description.Height = Pixels.Height;
            Description.MipLevels = 1;
            Description.ArraySize = 1;
            Description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            Description.SampleDesc.Count = 1;
            Description.Usage = D3D11_USAGE_IMMUTABLE;
            Description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA Content{Pixels.Pixels.data(), Pixels.Width * 4u, 0};
            Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> View;
            if (FAILED(Device->CreateTexture2D(&Description, &Content, Texture.GetAddressOf())) ||
                FAILED(Device->CreateShaderResourceView(Texture.Get(), nullptr, View.GetAddressOf())))
            {
                Preview.Refusal = "The display could not upload this preview image.";
                continue;
            }
            Preview.Texture = ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(View.Get())));
            Preview.Width = static_cast<int>(Pixels.Width);
            Preview.Height = static_cast<int>(Pixels.Height);
            PreviewTextures.emplace(Project, std::move(View));
        }
        ImGui::Render();
        const float Clear[] = {0,0,0,1};
        Commands->OMSetRenderTargets(1, &Target, nullptr); Commands->ClearRenderTargetView(Target, Clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (ProofImage && ++ProofFrames == 6)
        {
            // Read the actual DX11 render target, including the real backend's font/textures and clipping.
            ID3D11Texture2D* Source = nullptr;
            ID3D11Texture2D* Readback = nullptr;
            D3D11_TEXTURE2D_DESC Description{};
            D3D11_MAPPED_SUBRESOURCE Mapped{};
            Result = 1;
            if (SUCCEEDED(Chain->GetBuffer(0, IID_PPV_ARGS(&Source))))
            {
                Source->GetDesc(&Description);
                Description.Usage = D3D11_USAGE_STAGING; Description.BindFlags = 0;
                Description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; Description.MiscFlags = 0;
                if (SUCCEEDED(Device->CreateTexture2D(&Description, nullptr, &Readback)))
                {
                    Commands->CopyResource(Readback, Source);
                    if (SUCCEEDED(Commands->Map(Readback, 0, D3D11_MAP_READ, 0, &Mapped)))
                    {
                        std::ofstream File(ProofImage, std::ios::binary);
                        File << "P6\n" << Description.Width << " " << Description.Height << "\n255\n";
                        for (UINT Y = 0; Y < Description.Height; ++Y)
                            for (UINT X = 0; X < Description.Width; ++X)
                                File.write(static_cast<const char*>(Mapped.pData) + Y * Mapped.RowPitch + X * 4u, 3);
                        if (File.good()) Result = 0;
                        Commands->Unmap(Readback, 0);
                    }
                    Readback->Release();
                }
                Source->Release();
            }
            Running = false;
        }
        Chain->Present(1, 0);
    }
    DWORD ConsoleProcesses[8]{};
    if (Launched && GetConsoleProcessList(ConsoleProcesses, 8u) <= 2u) ShowWindow(GetConsoleWindow(), SW_HIDE);
    if (Child.hProcess)
    {
        if (!Launched) { TerminateProcess(Child.hProcess, ERROR_CANCELLED); WaitForSingleObject(Child.hProcess, 1000); }
        CloseHandle(Child.hProcess); CloseHandle(Child.hThread);
    }
    if (Ready) CloseHandle(Ready);
    PreviewTextures.clear();
    Opening.Previews.clear();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    Target->Release(); Chain->Release(); Commands->Release(); Device->Release();
    DestroyWindow(Window); UnregisterClassW(Class.lpszClassName, Class.hInstance);
    if (SUCCEEDED(Apartment)) CoUninitialize();
    return Result;
}

void CompleteProjectOpening(int ArgumentCount, char** Arguments) noexcept
{
    for (int Index = 1; Index + 1 < ArgumentCount; ++Index)
        if (std::strcmp(Arguments[Index], "--launcher-event") == 0)
        {
            const auto Name = Widen(Arguments[Index + 1]);
            HANDLE Event = OpenEventW(EVENT_MODIFY_STATE, FALSE, Name.c_str());
            if (Event) { SetEvent(Event); CloseHandle(Event); }
            return;
        }
}
}
#else
namespace Frontier
{
int RunProjectBrowser(const char* ProofImage)
{
    std::fputs("The native project browser currently requires Windows. Use Frontier ProjectName.frontier --scene scene.gltf.\n", stderr);
    return 64;
}
void CompleteProjectOpening(int, char**) noexcept {}
}
#endif
