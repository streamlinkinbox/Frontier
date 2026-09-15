// Minimal glslc: resolve #include manually, then hand GLSL to slang's bundled glslang.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <dlfcn.h>
typedef void (*glslang_OutputFunc)(void const* data, size_t size, void* userData);
struct Req10 {
    char const* sourcePath; void const* inputBegin; void const* inputEnd;
    glslang_OutputFunc diagnosticFunc; void* diagnosticUserData;
    glslang_OutputFunc outputFunc; void* outputUserData;
    int slangStage; unsigned action; unsigned optimizationLevel; unsigned debugInfoType;
};
static std::string gOut, gDiag;
static void outFn(void const* d,size_t n,void*){ gOut.append((const char*)d,n); }
static void diagFn(void const* d,size_t n,void*){ gDiag.append((const char*)d,n); }
// Slang stage enum: 0 none,1 vertex,2 hull,3 domain,4 geometry,5 fragment,6 compute
static int stageOf(const std::string& s){
    if(s=="vertex")return 1; if(s=="fragment")return 5; if(s=="compute")return 6;
    if(s=="geometry")return 4; return 0;
}
static bool readAll(const std::string& p,std::string& out){
    std::ifstream f(p,std::ios::binary); if(!f) return false;
    std::ostringstream ss; ss<<f.rdbuf(); out=ss.str(); return true;
}
// Recursively splice #include "..." against a list of roots.
static bool expand(const std::string& path,const std::vector<std::string>& roots,std::string& out,int depth,std::string& err){
    if(depth>32){ err="include depth exceeded"; return false; }
    std::string src; if(!readAll(path,src)){ err="cannot open "+path; return false; }
    std::istringstream in(src); std::string line;
    while(std::getline(in,line)){
        std::string t=line; size_t a=t.find_first_not_of(" \t");
        if(a!=std::string::npos && t.compare(a,8,"#include")==0){
            size_t q1=t.find('"',a), q2=(q1==std::string::npos?q1:t.find('"',q1+1));
            if(q1!=std::string::npos && q2!=std::string::npos){
                std::string rel=t.substr(q1+1,q2-q1-1); bool found=false;
                for(const auto& r:roots){ std::string cand=r+"/"+rel;
                    std::ifstream probe(cand); if(probe){ if(!expand(cand,roots,out,depth+1,err)) return false; found=true; break; } }
                if(!found){ err="include not found: "+rel; return false; }
                continue;
            }
        }
        // glslc-specific pragma that plain glslang rejects
        if(t.find("GL_GOOGLE_include_directive")!=std::string::npos) continue;
        out+=line; out+="\n";
    }
    return true;
}
int main(int argc,char** argv){
    if(argc<4){ std::printf("usage: myglslc <file> <stage> <out.spv> [roots...]\n"); return 1; }
    std::string file=argv[1], stage=argv[2], out=argv[3];
    std::vector<std::string> roots; for(int i=4;i<argc;i++) roots.push_back(argv[i]);
    std::string src, err;
    if(!expand(file,roots,src,0,err)){ std::printf("PREPROCESS FAILED: %s\n",err.c_str()); return 2; }
    void* lib=dlopen("/tmp/spy/slangpy/libslang-glslang-2026.12.so",RTLD_NOW);
    if(!lib){ std::printf("dlopen failed: %s\n",dlerror()); return 3; }
    auto fn=(int(*)(Req10*))dlsym(lib,"glslang_compile");
    if(!fn){ std::printf("no glslang_compile\n"); return 4; }
    Req10 r{}; r.sourcePath=file.c_str();
    r.inputBegin=src.data(); r.inputEnd=src.data()+src.size();
    r.outputFunc=outFn; r.diagnosticFunc=diagFn;
    r.slangStage=stageOf(stage); r.action=0; r.optimizationLevel=0; r.debugInfoType=0;
    int rc=fn(&r);
    if(!gDiag.empty()) std::printf("%s\n",gDiag.c_str());
    if(rc!=0 || gOut.empty()){ std::printf("COMPILE FAILED (rc=%d)\n",rc); return 5; }
    FILE* f=fopen(out.c_str(),"wb"); fwrite(gOut.data(),1,gOut.size(),f); fclose(f);
    std::printf("OK %s [%s] -> %s (%zu bytes)\n",file.c_str(),stage.c_str(),out.c_str(),gOut.size());
    return 0;
}
