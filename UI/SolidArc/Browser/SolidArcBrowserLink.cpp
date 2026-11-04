/*====================================================================================================================================
                                                      SOLIDARCBROWSERLINK.CPP
====================================================================================================================================*/
// 🧩 Connects the Frontier project browser to SolidArc: native .solidarc previews and the workspace launch

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Browser/SolidArcBrowserLink.h"

#include "UI/SolidArc/Browser/SolidArcBrowserPreview.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace Frontier::SolidArc
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string Utf8(const std::filesystem::path& Path)
{
    const std::u8string Encoded = Path.u8string();
    return std::string(Encoded.begin(), Encoded.end());
}

#if defined(_WIN32)
std::wstring QuoteArgument(const std::wstring& Argument)
{
    // 📝 CommandLineToArgvW rules: backslashes double only before a quote or the closing quote.
    std::wstring Quoted = L"\"";
    size_t       Slashes = 0;
    for (const wchar_t Character : Argument)
    {
        if (Character == L'\\') { ++Slashes; continue; }
        if (Character == L'"') Quoted.append(Slashes * 2 + 1, L'\\');
        else Quoted.append(Slashes, L'\\');
        Slashes = 0;
        Quoted += Character;
    }
    Quoted.append(Slashes * 2, L'\\');
    Quoted += L'"';
    return Quoted;
}

std::wstring Widen(const std::string& Text)
{
    const int Length = MultiByteToWideChar(CP_UTF8, 0, Text.data(), int(Text.size()), nullptr, 0);
    std::wstring Wide(size_t(Length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, Text.data(), int(Text.size()), Wide.data(), Length);
    return Wide;
}
#endif

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path LocateSolidArcWorkspace()
{
#if defined(_WIN32)
    wchar_t Executable[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, Executable, MAX_PATH)) return {};
    return std::filesystem::path(Executable).parent_path() / L"SolidArcWorkspace.exe";
#elif defined(__APPLE__)
    char     Executable[4096] = {};
    uint32_t Capacity         = sizeof(Executable);
    if (_NSGetExecutablePath(Executable, &Capacity) != 0) return {};
    return std::filesystem::path(Executable).parent_path() / "SolidArcWorkspace";
#else
    std::error_code Failure;
    const auto      Executable = std::filesystem::read_symlink("/proc/self/exe", Failure);
    if (Failure) return {};
    return Executable.parent_path() / "SolidArcWorkspace";
#endif
}

bool LaunchSolidArcWorkspace(const std::filesystem::path& Workspace, const std::vector<std::string>& Arguments,
                             const std::filesystem::path& Document, std::string& Refusal)
{
    std::error_code Failure;
    if (Workspace.empty() || !std::filesystem::is_regular_file(Workspace, Failure))
    {
        Refusal = "SolidArcWorkspace was not found next to this application (" + Utf8(Workspace) + ").";
        return false;
    }
    if (!std::filesystem::is_regular_file(Document, Failure))
    {
        Refusal = "The document no longer exists: " + Utf8(Document);
        return false;
    }

    // 💡 The workspace may chdir to its own folder at start-up (package layout), so a relative document path would dangle.
    const std::filesystem::path Absolute = std::filesystem::absolute(Document, Failure);
    const std::filesystem::path Opened   = Failure ? Document : Absolute.lexically_normal();
#if defined(_WIN32)
    std::wstring CommandLine = QuoteArgument(Workspace.wstring());
    for (const std::string& Argument : Arguments) CommandLine += L" " + QuoteArgument(Widen(Argument));
    CommandLine += L" --open " + QuoteArgument(Opened.wstring());
    STARTUPINFOW        Startup{};
    PROCESS_INFORMATION Started{};
    Startup.cb = sizeof(Startup);
    if (!CreateProcessW(Workspace.c_str(), CommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup, &Started))
    {
        Refusal = "SolidArcWorkspace could not be started (Windows error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(Started.hThread);
    CloseHandle(Started.hProcess);
    return true;
#else
    std::vector<std::string> Words = { Utf8(Workspace) };
    Words.insert(Words.end(), Arguments.begin(), Arguments.end());
    Words.push_back("--open");
    Words.push_back(Utf8(Opened));
    std::vector<char*> Vector;
    for (std::string& Word : Words) Vector.push_back(Word.data());
    Vector.push_back(nullptr);

    // 💡 A close-on-exec pipe reports an exec failure (the child writes errno) and closes silently on success.
    int Channel[2] = { -1, -1 };
    if (pipe(Channel) != 0)
    {
        Refusal = std::string("SolidArcWorkspace could not be started (pipe: ") + std::strerror(errno) + ").";
        return false;
    }
    fcntl(Channel[1], F_SETFD, FD_CLOEXEC);
    const pid_t Child = fork();
    if (Child < 0)
    {
        close(Channel[0]);
        close(Channel[1]);
        Refusal = std::string("SolidArcWorkspace could not be started (fork: ") + std::strerror(errno) + ").";
        return false;
    }
    if (Child == 0)
    {
        close(Channel[0]);
        setsid();                                                                            // Detached from the browser's session
        execv(Vector[0], Vector.data());
        const int Error = errno;
        (void)!write(Channel[1], &Error, sizeof(Error));
        _exit(127);
    }
    close(Channel[1]);
    int        Error = 0;
    const auto Read  = read(Channel[0], &Error, sizeof(Error));
    close(Channel[0]);
    if (Read == ssize_t(sizeof(Error)))
    {
        waitpid(Child, nullptr, 0);
        Refusal = std::string("SolidArcWorkspace could not be started (exec: ") + std::strerror(Error) + ").";
        return false;
    }
    return true;
#endif
}

BrowserNativeDocument DescribeSolidArcBrowserDocument(const std::filesystem::path& Path)
{
    const SolidArcBrowserPreview Preview = ComposeSolidArcBrowserPreview(Path);
    BrowserNativeDocument        Native;
    Native.Refusal = Preview.Refusal;
    if (!Preview.Refusal.empty()) return Native;
    const auto Count = [](size_t Amount, const char* Single, const char* Several) { return std::to_string(Amount) + " " + (Amount == 1 ? Single : Several); };
    char       Timing[32] = {};
    std::snprintf(Timing, sizeof(Timing), "%.0f ms", Preview.Milliseconds);
    Native.Geometry    = Preview.Triangles;
    Native.GeometryKey = Preview.Key;
    Native.Summary     = Count(Preview.BodyCount, "body", "bodies") + " · " + Count(Preview.FaceCount, "face", "faces") + " · " +
                         Count(Preview.StepCount, "step", "steps");
    Native.Description = "SolidArc document '" + Preview.Title + "', replayed by GeometryProcessing and tessellated into " +
                         std::to_string(Preview.TriangleCount) + " triangles in " + Timing + ". Open it in SolidArc to edit the feature tree.";
    if (Preview.BodyCount == 0)
        Native.Description = "SolidArc model '" + Preview.Title + "' has no bodies yet. Open it in SolidArc to start modelling.";
    return Native;
}

bool CreateSolidArcBrowserDocument(const std::filesystem::path& Path, const std::string& Title, std::string& Refusal)
{
    std::error_code Failure;
    if (Path.empty() || std::filesystem::exists(Path, Failure))
    {
        Refusal = Path.empty() ? "Choose where to save the new SolidArc model." : "A file already exists at " + Utf8(Path) + "; choose another name.";
        return false;
    }
    const SolidArcDocument Document = CreateSolidArcDocument(Title.empty() ? Utf8(Path.stem()) : Title);
    if (!SaveSolidArcDocument(Document, Utf8(Path)))
    {
        Refusal = "The SolidArc model could not be written to " + Utf8(Path) + ".";
        return false;
    }
    return true;
}

void ConnectSolidArcBrowser(ProjectBrowserPanel& Browser, SolidArcBrowserLinkSettings Settings)
{
    Browser.QueryDocumentGeometry = [](const std::filesystem::path& Path)
    {
        BrowserNativeDocument Native;
        if (!QueryBrowserSolidArcExtension(Utf8(Path.extension()))) return Native;
        return DescribeSolidArcBrowserDocument(Path);
    };
    Browser.CreateWorkspaceDocument = [](const std::filesystem::path& Path, const std::string& Title, std::string& Refusal)
    {
        return CreateSolidArcBrowserDocument(Path, Title, Refusal);
    };
    Browser.LaunchWorkspaceDocument = [Settings = std::move(Settings)](const std::filesystem::path& Document, std::string& Refusal)
    {
        if (Settings.Launch) return Settings.Launch(Document, Refusal);
        return LaunchSolidArcWorkspace(Settings.Workspace, Settings.Arguments, Document, Refusal);
    };
}

} // namespace Frontier::SolidArc
