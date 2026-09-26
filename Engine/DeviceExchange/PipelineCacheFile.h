#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Frontier::PipelineDiagnostics {
struct Options {
    bool DisableOptimization=false;
    bool IgnoreInput=false;
    const char* File="ShaderCache.bin";
    bool Recognized=true;
};
inline Options Parse(const char* value) {
    if(!value || !*value || std::strcmp(value,"default")==0)return {};
    if(std::strcmp(value,"no-opt")==0)return {true,false,"ShaderCache.no-opt.bin",true};
    if(std::strcmp(value,"empty-cache")==0)return {false,true,"ShaderCache.isolated.bin",true};
    return {false,false,"ShaderCache.bin",false};
}
// Diagnostic fingerprint, not authentication or a replacement for Vulkan cache compatibility checks.
inline uint64_t Fingerprint(const void* bytes,size_t size) {
    auto data=static_cast<const unsigned char*>(bytes);uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<size;++i){hash^=data[i];hash*=1099511628211ull;}return hash;
}
// Keep the last good file until the new one has been completely written and closed.
// No delete-then-rename gap. Rename failure leaves the existing file untouched.
inline bool Replace(const std::filesystem::path& path,const void* data,size_t size,std::string& error) {
    static std::atomic<uint64_t> serial{0};
    auto temp=path;
    temp += "."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"."+std::to_string(++serial)+".tmp";
    std::error_code ec;
    try {
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        if(!out){error="cannot open temporary cache file";return false;}
        out.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));out.close();
        if(!out){error="cache write/close failed";std::filesystem::remove(temp,ec);return false;}
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            error="cache replacement failed, Windows error "+std::to_string(GetLastError());
            std::filesystem::remove(temp,ec);return false;
        }
#else
        std::filesystem::rename(temp,path,ec);
        if(ec){error=ec.message();std::filesystem::remove(temp,ec);return false;}
#endif
        return true;
    } catch(const std::exception& e) {
        error=e.what();std::filesystem::remove(temp,ec);return false;
    }
}
}
