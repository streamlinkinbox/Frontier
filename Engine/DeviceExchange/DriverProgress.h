#pragma once
// Startup-only observer. The Vulkan call stays on its original thread. This worker
// only samples process memory and prints a heartbeat; it never calls Vulkan.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace Frontier {
class DriverProgress {
    using Clock = std::chrono::steady_clock;
    std::string Label;
    unsigned Id;
    Clock::time_point Start = Clock::now();
    std::mutex Mutex;
    std::condition_variable Wake;
    bool Stop = false;
    std::thread Worker;
    inline static std::atomic<unsigned> NextId{0};
    inline static std::mutex OutputMutex;

    void Print(const char* state, const std::string& detail = {}) const {
        uint64_t resident=0, peak=0, pid=0;
        int64_t commit=-1;
        bool available=false;
#ifdef _WIN32
        pid=GetCurrentProcessId();
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        using Query=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
        auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
        if(query&&query(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory),sizeof(memory))){
            resident=memory.WorkingSetSize;peak=memory.PeakWorkingSetSize;commit=int64_t(memory.PrivateUsage);available=true;
        }
#else
        pid=static_cast<uint64_t>(getpid());
        std::ifstream status("/proc/self/status");std::string line;
        while(std::getline(status,line)){
            if(line.rfind("VmRSS:",0)==0){resident=std::stoull(line.substr(6))*1024;available=true;}
            else if(line.rfind("VmHWM:",0)==0)peak=std::stoull(line.substr(6))*1024;
        }
#endif
        std::ostringstream out;
        out<<"[GPU startup] "<<state<<" #"<<Id<<" "<<Label<<" elapsed="<<std::fixed<<std::setprecision(2)
           <<std::chrono::duration<double>(Clock::now()-Start).count()<<"s pid="<<pid;
        if(available)out<<" rss="<<resident/1048576.0<<"MiB peak_rss="<<peak/1048576.0<<"MiB";
        else out<<" rss=N/A peak_rss=N/A";
        if(commit>=0)out<<" private_commit="<<commit/1048576.0<<"MiB";
        else out<<" private_commit=N/A";
        out<<" "<<detail<<'\n';
        std::lock_guard lock(OutputMutex);std::cerr<<out.str()<<std::flush;
    }
    void Join() {
        {std::lock_guard lock(Mutex);Stop=true;}
        Wake.notify_all();
        if(Worker.joinable())Worker.join();
    }
public:
    explicit DriverProgress(std::string label, std::chrono::milliseconds interval=std::chrono::seconds(5))
        : Label(std::move(label)), Id(++NextId) {
        Print("BEGIN");
        if(interval<=std::chrono::milliseconds::zero())interval=std::chrono::seconds(5);
        try {
            Worker=std::thread([this,interval]{
                std::unique_lock lock(Mutex);
                while(!Wake.wait_for(lock,interval,[this]{return Stop;})) {
                    lock.unlock();
                    try { Print("WAIT", "driver call has not returned; percent/ETA unavailable (not proof of compiler progress)"); }
                    catch(...) { /* Diagnostics must not terminate a running driver call. */ }
                    lock.lock();
                }
            });
        } catch(const std::exception&) {
            Print("NOTICE", "heartbeat thread unavailable; begin/end logging remains active");
        }
    }
    DriverProgress(const DriverProgress&)=delete;
    DriverProgress& operator=(const DriverProgress&)=delete;
    ~DriverProgress(){Join();}
    void Finish(int result) {
        Join();
        Print(result==0?"DONE":"RESULT", "VkResult="+std::to_string(result));
    }
    template<class Function>
    static auto Call(const std::string& label, Function&& function,
                     std::chrono::milliseconds interval=std::chrono::seconds(5)) {
        DriverProgress progress(label,interval);
        auto result=function();
        progress.Finish(static_cast<int>(result));
        return result;
    }
};
}
