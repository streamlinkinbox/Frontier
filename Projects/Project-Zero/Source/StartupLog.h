#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
namespace Frontier::ProjectZero {
// Process-wide measurements, not exclusive attribution when workers overlap.
class StartupLog {
    using Clock=std::chrono::steady_clock;
    Clock::time_point Start=Clock::now();std::mutex Mutex;std::ofstream File;
public:
    static auto Now(){return Clock::now();}
    static double Elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
    StartupLog(){std::error_code ec;std::filesystem::create_directories("Build/Diagnostics",ec);
        const auto id=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto path="Build/Diagnostics/startup-"+std::to_string(id)+".csv";File.open(path);if(File){File<<"event,since_main_ms,phase_ms,rss_bytes,peak_rss_bytes,private_commit_bytes,payload_bytes\n";std::cerr<<"[Startup] CSV: "<<path<<'\n';}else std::cerr<<"[Startup] Cannot write CSV; console diagnostics remain active\n";
        Mark("main");}
    void Mark(const char* event,double phase=-1,uint64_t payload=0){std::lock_guard lock(Mutex);uint64_t rss=0,peak=0;int64_t commit=-1;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        using Query=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
        auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
        if(query&&query(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory),sizeof(memory))){rss=memory.WorkingSetSize;peak=memory.PeakWorkingSetSize;commit=int64_t(memory.PrivateUsage);}
#else
        std::ifstream status("/proc/self/status");std::string line;while(std::getline(status,line)){if(line.rfind("VmRSS:",0)==0)rss=std::stoull(line.substr(6))*1024;else if(line.rfind("VmHWM:",0)==0)peak=std::stoull(line.substr(6))*1024;}
#endif
        const double elapsed=Elapsed(Start);if(File){File<<event<<','<<elapsed<<','<<phase<<','<<rss<<','<<peak<<','<<commit<<','<<payload<<'\n';File.flush();}
        std::cerr<<"[Startup] "<<event<<" t="<<elapsed<<"ms phase="<<phase<<"ms rss="<<(rss/1048576.0)<<"MiB peak="<<(peak/1048576.0)<<"MiB private_bytes="<<commit<<" payload="<<payload<<"B\n";
    }
};
}
