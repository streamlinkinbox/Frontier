//============================================================================================================================================
//                                                         BACKENDCLIENT.CPP
//============================================================================================================================================
// Sync premium lookup against the Charge backend. Windows uses WinHTTP (linked explicitly in
// both build paths); other platforms report unqueried so the manual toggle stays in charge.

#include "BackendClient.h"
#include "LocalConfiguration.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace Networking
{
namespace
{
char BackendIdentity[64]{};

struct BackendConfig
{
    char Url[256]{};
    char Key[129]{};
    bool Present = false;
};

bool PrintableNoSpace(const char* Begin, const char* End, std::size_t Max) noexcept
{
    const auto Length = static_cast<std::size_t>(End - Begin);
    if (Length == 0 || Length >= Max)
        return false;
    for (const char* P = Begin; P != End; ++P)
    {
        const unsigned char C = static_cast<unsigned char>(*P);
        if (C <= 32 || C >= 127)
            return false;
    }
    return true;
}

bool ParseBackendIni(const char* Text, std::size_t Length, BackendConfig& Out) noexcept
{
    BackendConfig Candidate;
    unsigned Seen = 0;
    bool Valid = true;
    const char* Cursor = Text;
    const char* Stop = Text + Length;
    while (Cursor < Stop && Valid)
    {
        const char* End = Cursor;
        while (End < Stop && *End != '\n')
            ++End;
        const char* LineEnd = End;
        if (LineEnd > Cursor && LineEnd[-1] == '\r')
            --LineEnd;
        if (LineEnd > Cursor && *Cursor != '#')
        {
            const char* Equal = Cursor;
            while (Equal < LineEnd && *Equal != '=')
                ++Equal;
            if (Equal == LineEnd)
            {
                Valid = false;
                break;
            }
            auto KeyIs = [&](const char* Word) {
                const std::size_t N = std::strlen(Word);
                return static_cast<std::size_t>(Equal - Cursor) == N &&
                    std::strncmp(Cursor, Word, N) == 0;
            };
            const char* V = Equal + 1;
            unsigned Bit = 0;
            if (KeyIs("version"))
            {
                Bit = 1;
                if (static_cast<std::size_t>(LineEnd - V) != 1 || *V != '1')
                    Valid = false;
            }
            else if (KeyIs("backend_url"))
            {
                Bit = 2;
                if (!PrintableNoSpace(V, LineEnd, sizeof(Candidate.Url)))
                    Valid = false;
                else
                {
                    const auto N = static_cast<std::size_t>(LineEnd - V);
                    std::memcpy(Candidate.Url, V, N);
                    Candidate.Url[N] = 0;
                }
            }
            else if (KeyIs("backend_key"))
            {
                Bit = 4;
                if (!PrintableNoSpace(V, LineEnd, sizeof(Candidate.Key)))
                    Valid = false;
                else
                {
                    const auto N = static_cast<std::size_t>(LineEnd - V);
                    std::memcpy(Candidate.Key, V, N);
                    Candidate.Key[N] = 0;
                }
            }
            else
                Valid = false;
            if (Seen & Bit)
                Valid = false;
            Seen |= Bit;
        }
        Cursor = (End < Stop) ? End + 1 : Stop;
    }
    if (Valid && Seen == 7)
    {
        Out = Candidate;
        Out.Present = true;
    }
    volatile char* Wipe = Candidate.Key;
    for (std::size_t I = 0; I < sizeof(Candidate.Key); ++I)
        Wipe[I] = 0;
    return Out.Present;
}

bool LoadBackendConfig(BackendConfig& Out) noexcept
{
    Out = BackendConfig{};
    const auto Directory = ExecutableDirectory();
    const std::filesystem::path Files[2] = {
        Directory.parent_path() / "Charge.backend.ini", Directory / "Charge.backend.ini"};
    for (const auto& File : Files)
    {
        std::error_code Error;
        const auto Bytes = std::filesystem::file_size(File, Error);
        if (Error)
            continue;
        if (Bytes == 0 || Bytes > 2048)
            return false;
        std::ifstream Input(File, std::ios::binary);
        if (!Input)
            return false;
        char Contents[2048]{};
        Input.read(Contents, static_cast<std::streamsize>(Bytes));
        const auto Got = static_cast<std::size_t>(Input.gcount());
        const bool Parsed = Got == static_cast<std::size_t>(Bytes) &&
            ParseBackendIni(Contents, Got, Out);
        volatile char* Wipe = Contents;
        for (std::size_t I = 0; I < sizeof(Contents); ++I)
            Wipe[I] = 0;
        return Parsed;
    }
    return false;
}

bool ValidEpicId(const char* Id) noexcept
{
    if (!Id || !*Id)
        return false;
    std::size_t Length = 0;
    for (const char* P = Id; *P; ++P)
    {
        const unsigned char C = static_cast<unsigned char>(*P);
        const bool Alnum = (C >= '0' && C <= '9') || (C >= 'a' && C <= 'z') ||
            (C >= 'A' && C <= 'Z');
        if (!Alnum)
            return false;
        if (++Length >= sizeof(BackendIdentity))
            return false;
    }
    return true;
}

struct SplitUrl
{
    char Host[128]{};
    unsigned Port = 0;
    bool Secure = false;
    char Prefix[128]{};
};

bool SplitBackendUrl(const char* Url, SplitUrl& Out) noexcept
{
    Out = SplitUrl{};
    const char* Rest = nullptr;
    if (std::strncmp(Url, "http://", 7) == 0)
        Rest = Url + 7;
    else if (std::strncmp(Url, "https://", 8) == 0)
    {
        Rest = Url + 8;
        Out.Secure = true;
    }
    else
        return false;
    const char* HostEnd = Rest;
    while (*HostEnd && *HostEnd != ':' && *HostEnd != '/')
    {
        const unsigned char C = static_cast<unsigned char>(*HostEnd);
        const bool Ok = (C >= '0' && C <= '9') || (C >= 'a' && C <= 'z') ||
            (C >= 'A' && C <= 'Z') || C == '.' || C == '-';
        if (!Ok)
            return false;
        ++HostEnd;
    }
    const auto HostLength = static_cast<std::size_t>(HostEnd - Rest);
    if (HostLength == 0 || HostLength >= sizeof(Out.Host))
        return false;
    std::memcpy(Out.Host, Rest, HostLength);
    Out.Port = Out.Secure ? 443u : 80u;
    if (*HostEnd == ':')
    {
        const char* Digits = HostEnd + 1;
        unsigned Port = 0;
        unsigned Count = 0;
        while (*Digits >= '0' && *Digits <= '9' && Count < 5)
        {
            Port = Port * 10u + static_cast<unsigned>(*Digits - '0');
            ++Digits;
            ++Count;
        }
        if (Count == 0 || Port == 0 || Port > 65535)
            return false;
        Out.Port = Port;
        HostEnd = Digits;
    }
    if (*HostEnd == 0)
        return true;
    if (*HostEnd != '/')
        return false;
    const std::size_t PrefixLength = std::strlen(HostEnd);
    if (PrefixLength >= sizeof(Out.Prefix))
        return false;
    for (const char* P = HostEnd; *P; ++P)
    {
        const unsigned char C = static_cast<unsigned char>(*P);
        if (C <= 32 || C >= 127 || C == '?' || C == '#' || C == '\\')
            return false;
    }
    std::memcpy(Out.Prefix, HostEnd, PrefixLength + 1);
    return true;
}

// Only the Windows branch below asks a backend anything, so the verdict parser is dead weight
//    elsewhere and -Wunused-function says so.
#if defined(_WIN32)
bool ScanVerdict(const char* Body, bool& Premium, int& Coins) noexcept
{
    const char* P = std::strstr(Body, "\"premium\"");
    if (!P)
        return false;
    P = std::strchr(P + 9, ':');
    if (!P)
        return false;
    ++P;
    while (*P == ' ' || *P == '\t')
        ++P;
    if (std::strncmp(P, "true", 4) == 0)
        Premium = true;
    else if (std::strncmp(P, "false", 5) == 0)
        Premium = false;
    else
        return false;
    Coins = 0;
    const char* C = std::strstr(Body, "\"autocoin\"");
    if (C)
    {
        C = std::strchr(C + 10, ':');
        if (C)
        {
            const long Value = std::strtol(C + 1, nullptr, 10);
            if (Value > 0)
                Coins = Value > 1000000000L ? 1000000000 : static_cast<int>(Value);
        }
    }
    return true;
}
#endif

#if defined(_WIN32)
struct WinHttpHandle
{
    HINTERNET Raw = nullptr;
    ~WinHttpHandle() noexcept
    {
        if (Raw)
            WinHttpCloseHandle(Raw);
    }
    WinHttpHandle() noexcept = default;
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
};

bool WidenAscii(const char* In, wchar_t* Out, std::size_t Cap) noexcept
{
    std::size_t I = 0;
    for (; In[I] && I + 1 < Cap; ++I)
    {
        const unsigned char C = static_cast<unsigned char>(In[I]);
        if (C >= 127)
            return false;
        Out[I] = static_cast<wchar_t>(C);
    }
    if (In[I] != 0)
        return false;
    Out[I] = 0;
    return true;
}

// GET <prefix>/v1/premium?epic_account_id=<id> with X-Charge-Key. Short timeouts;
// transport start must never hang on a dead backend.
bool WinHttpGetPremium(const SplitUrl& Url, const char* Key, const char* EpicId,
    char* Body, std::size_t BodyCap, unsigned& Status) noexcept
{
    Status = 0;
    char Path[512]{};
    const int Written = std::snprintf(Path, sizeof(Path),
        "%s/v1/premium?epic_account_id=%s", Url.Prefix, EpicId);
    if (Written <= 0 || static_cast<std::size_t>(Written) >= sizeof(Path))
        return false;
    wchar_t Host[128]{}, RequestPath[512]{};
    if (!WidenAscii(Url.Host, Host, sizeof(Host) / sizeof(Host[0])))
        return false;
    if (!WidenAscii(Path, RequestPath, sizeof(RequestPath) / sizeof(RequestPath[0])))
        return false;
    char Header[256]{};
    if (std::snprintf(Header, sizeof(Header), "X-Charge-Key: %s", Key) >=
        static_cast<int>(sizeof(Header)))
        return false;
    wchar_t HeaderWide[256]{};
    if (!WidenAscii(Header, HeaderWide, sizeof(HeaderWide) / sizeof(HeaderWide[0])))
        return false;
    WinHttpHandle Session, Connect, Request;
    Session.Raw = WinHttpOpen(L"FrontierCharge/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!Session.Raw)
        return false;
    WinHttpSetTimeouts(Session.Raw, 3000, 3000, 3000, 3000);
    Connect.Raw = WinHttpConnect(Session.Raw, Host,
        static_cast<INTERNET_PORT>(Url.Port), 0);
    if (!Connect.Raw)
        return false;
    Request.Raw = WinHttpOpenRequest(Connect.Raw, L"GET", RequestPath, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        Url.Secure ? WINHTTP_FLAG_SECURE : 0);
    if (!Request.Raw)
        return false;
    if (!WinHttpAddRequestHeaders(Request.Raw, HeaderWide,
            static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD))
        return false;
    if (!WinHttpSendRequest(Request.Raw, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        return false;
    if (!WinHttpReceiveResponse(Request.Raw, nullptr))
        return false;
    DWORD Code = 0;
    DWORD CodeLength = sizeof(Code);
    if (!WinHttpQueryHeaders(Request.Raw,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &Code, &CodeLength,
            WINHTTP_NO_HEADER_INDEX))
        return false;
    Status = Code;
    std::size_t Used = 0;
    for (;;)
    {
        DWORD Available = 0;
        if (!WinHttpQueryDataAvailable(Request.Raw, &Available))
            return false;
        if (Available == 0)
            break;
        if (Used + Available + 1 > BodyCap)
            return false;
        DWORD Read = 0;
        if (!WinHttpReadData(Request.Raw, Body + Used, Available, &Read))
            return false;
        Used += Read;
        if (Read == 0)
            break;
    }
    Body[Used] = 0;
    volatile char* Wipe = Header;
    for (std::size_t I = 0; I < sizeof(Header); ++I)
        Wipe[I] = 0;
    return true;
}
#endif
} // namespace

void SetBackendIdentity(const char* EpicAccountId) noexcept
{
    BackendIdentity[0] = 0;
    if (!EpicAccountId)
        return;
    std::snprintf(BackendIdentity, sizeof(BackendIdentity), "%s", EpicAccountId);
    if (!ValidEpicId(BackendIdentity))
        BackendIdentity[0] = 0;
}

BackendVerdict QueryBackendPremium(DiagnosticReception Emit) noexcept
{
    BackendVerdict Verdict;
    if (BackendIdentity[0] == 0)
        return Verdict;
    BackendConfig Config;
    if (!LoadBackendConfig(Config) || !Config.Present)
    {
        if (Emit)
            Emit("backend=unconfigured");
        return Verdict;
    }
    SplitUrl Url;
    if (!SplitBackendUrl(Config.Url, Url))
    {
        if (Emit)
            Emit("backend=bad_url");
        return Verdict;
    }
#if !defined(_WIN32)
    (void)Config;
    if (Emit)
        Emit("backend=unsupported_platform");
    return Verdict;
#else
    char Body[4096]{};
    unsigned Status = 0;
    if (!WinHttpGetPremium(Url, Config.Key, BackendIdentity, Body, sizeof(Body),
            Status))
    {
        if (Emit)
            Emit("backend=unreachable");
        return Verdict;
    }
    if (Status == 401 || Status == 403)
    {
        if (Emit)
            Emit("backend=denied");
        return Verdict;
    }
    if (Status != 200)
    {
        if (Emit)
            Emit("backend=bad_status");
        return Verdict;
    }
    bool Premium = false;
    int Coins = 0;
    if (!ScanVerdict(Body, Premium, Coins))
    {
        if (Emit)
            Emit("backend=bad_response");
        return Verdict;
    }
    Verdict.Queried = true;
    Verdict.Premium = Premium;
    Verdict.Coins = Coins;
    if (Emit)
    {
        char Line[256]{};
        char Host[64]{};
        std::snprintf(Host, sizeof(Host), "%s", Url.Host);
        std::snprintf(Line, sizeof(Line), "backend=ok host=%s premium=%d coins=%d",
            Host, Premium ? 1 : 0, Coins);
        Emit(Line);
    }
    volatile char* Wipe = Config.Key;
    for (std::size_t I = 0; I < sizeof(Config.Key); ++I)
        Wipe[I] = 0;
    return Verdict;
#endif
}
} // namespace Networking
