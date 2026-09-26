#include "Engine/DeviceExchange/PipelineCacheFile.h"
#include <cassert>
#include <iostream>
#include <iterator>
using namespace Frontier::PipelineDiagnostics;
std::string Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
    assert(argc==2);const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    assert(!Parse(nullptr).DisableOptimization && !Parse("").IgnoreInput);
    assert(Parse("default").Recognized && std::string(Parse("default").File)=="ShaderCache.bin");
    assert(Parse("no-opt").DisableOptimization && !Parse("no-opt").IgnoreInput);
    assert(Parse("empty-cache").IgnoreInput && !Parse("empty-cache").DisableOptimization);
    assert(std::string(Parse("no-opt").File)!=Parse("default").File);
    assert(std::string(Parse("empty-cache").File)!=Parse("default").File);
    assert(!Parse("typo").Recognized && !Parse("typo").DisableOptimization);
    assert(Fingerprint("",0)==14695981039346656037ull);
    assert(Fingerprint("hello",5)==0xa430d84680aabd0bull);
    assert(Fingerprint("one",3)!=Fingerprint("two",3)); // equal-size files do not imply equal content
    auto file=root/"ShaderCache.bin";std::string error;
    assert(Replace(file,"old",3,error));assert(Read(file)=="old");
    assert(Replace(file,"new cache",9,error));assert(Read(file)=="new cache");
    auto blocked=root/"directory";std::filesystem::create_directories(blocked);
    std::ofstream(blocked/"preserve")<<"keep";
    assert(!Replace(blocked,"invalid",7,error));assert(Read(blocked/"preserve")=="keep");
    assert(!Replace(root/"missing-parent/file","bad",3,error));assert(Read(file)=="new cache");
    for(auto& entry:std::filesystem::directory_iterator(root))assert(entry.path().extension()!=".tmp");
    std::cout<<"PASS: mode isolation/defaults, deterministic fingerprints, equal-size differences, successful replacement, failed replacement preserves destination, temp cleanup. CPU only.\n";
}
