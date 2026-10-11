#include "GpuSurfaceExtractor.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <stdexcept>
namespace Frontier::ProjectFluid {
namespace {
using Clock=std::chrono::steady_clock;
double Elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void Check(VkResult result,const char* name){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(name)+" VkResult="+std::to_string(result));}
struct Device {
 VkInstance Instance{};VkDevice Handle{};VkPhysicalDevice Physical{};VkQueue Queue{};uint32_t Family{};VkPhysicalDeviceProperties Properties{};
 ~Device(){if(Handle){vkDeviceWaitIdle(Handle);vkDestroyDevice(Handle,nullptr);}if(Instance)vkDestroyInstance(Instance,nullptr);}
 void Init(uint32_t selection){
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Project-Zero Fluid GPU Test";app.apiVersion=VK_API_VERSION_1_2;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;Check(vkCreateInstance(&ci,nullptr,&Instance),"create instance");
  uint32_t count=0;Check(vkEnumeratePhysicalDevices(Instance,&count,nullptr),"enumerate GPUs");std::vector<VkPhysicalDevice> devices(count);Check(vkEnumeratePhysicalDevices(Instance,&count,devices.data()),"enumerate GPUs");
  for(uint32_t i=0;i<count;++i){VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(devices[i],&properties);std::cerr<<"[FluidTest] available_device["<<i<<"]="<<properties.deviceName<<'\n';}
  if(selection>=count)throw std::runtime_error("Requested fluid GPU index is unavailable; no silent CPU fallback");Physical=devices[selection];vkGetPhysicalDeviceProperties(Physical,&Properties);
  if(Properties.apiVersion<VK_API_VERSION_1_2)throw std::runtime_error("Fluid test requires Vulkan 1.2");
  uint32_t queues=0;vkGetPhysicalDeviceQueueFamilyProperties(Physical,&queues,nullptr);std::vector<VkQueueFamilyProperties> families(queues);vkGetPhysicalDeviceQueueFamilyProperties(Physical,&queues,families.data());Family=UINT32_MAX;
  for(uint32_t i=0;i<queues;++i)if(families[i].queueCount&&(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){Family=i;break;}
  if(Family==UINT32_MAX)throw std::runtime_error("No fluid compute queue");float priority=1;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=Family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;Check(vkCreateDevice(Physical,&di,nullptr,&Handle),"create fluid device");vkGetDeviceQueue(Handle,Family,0,&Queue);
 }
};
std::vector<std::array<uint32_t,3>> Faces(const SurfaceGpuOutput& mesh){std::vector<std::array<uint32_t,3>> out;for(size_t i=0;i<mesh.Indices.size();i+=3)out.push_back({mesh.Indices[i],mesh.Indices[i+1],mesh.Indices[i+2]});std::sort(out.begin(),out.end());return out;}
}
int RunFluidGpuTest(int argc,char** argv){
 std::ofstream summary;
 try{
  bool cpu=false,verify=false;uint32_t steps=120,gpu=0;std::string shader="Engine/Shaders/FluidExtract.spv";
  auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::filesystem::path folder=std::filesystem::path("Build/FluidTests")/std::to_string(stamp);
  for(int i=1;i<argc;++i){const std::string a=argv[i];if(a=="--fluid-cpu-test")cpu=true;else if(a=="--fluid-verify")verify=true;
   else if(a=="--fluid-steps"||a=="--fluid-device"||a=="--fluid-output"||a=="--fluid-shader"){
    if(i+1>=argc)throw std::runtime_error("Missing value for "+a);const std::string value=argv[++i];
    if(a=="--fluid-output")folder=value;else if(a=="--fluid-shader")shader=value;else{size_t used=0;const auto n=std::stoul(value,&used);if(used!=value.size()||n>36000)throw std::runtime_error("Invalid fluid numeric option");if(a=="--fluid-steps")steps=uint32_t(n);else gpu=uint32_t(n);}}
  }
  if(steps==0)throw std::runtime_error("Fluid steps must be positive");
  std::filesystem::create_directories(folder);std::ofstream csv(folder/"performance.csv");summary.open(folder/"summary.txt");if(!csv||!summary)throw std::runtime_error("Cannot create fluid logs");
  auto log=[&](const std::string& s){std::cerr<<"[FluidTest] "<<s<<'\n';summary<<s<<'\n';summary.flush();};
  log(std::string("mode=")+(cpu?"CPU GPU-layout mirror":"Vulkan GPU extraction")+"; CPU solver/PCA; raw mesh, no mesh smoothing; NOT live Project-Zero scene water");
  Device device;std::unique_ptr<GpuSurfaceExtractor> extractor;
  if(!cpu){const auto initStart=Clock::now();device.Init(gpu);log(std::string("device=")+device.Properties.deviceName+" type="+std::to_string(device.Properties.deviceType)+" driver="+std::to_string(device.Properties.driverVersion));
   if(!std::filesystem::exists(shader)){const auto alternative=std::filesystem::path(argv[0]).parent_path()/shader;if(std::filesystem::exists(alternative))shader=alternative.string();}
   extractor=std::make_unique<GpuSurfaceExtractor>(device.Physical,device.Handle,device.Queue,device.Family,shader.c_str());log("allocated_buffer_bytes="+std::to_string(extractor->AllocatedBytes())+" gpu_setup_wall_ms="+std::to_string(Elapsed(initStart)));}
  csv<<"step,warmup,particles,solver_ms,pca_ms,prepare_ms,extract_wall_ms,gpu_field_ms,gpu_edges_ms,gpu_triangles_ms,submit_wait_ms,index_count,overflow,total_wall_ms\n";csv<<std::fixed<<std::setprecision(6);
  PbfFluid fluid;SurfaceReconstruction pca;SurfaceGpuOutput output;SurfaceGpuInput input;std::vector<double> totals;uint32_t count=0;
  for(uint32_t step=0;step<steps;++step){const auto start=Clock::now();auto t=start;fluid.Step(1.f/60);const double solver=Elapsed(t);t=Clock::now();pca.Update(fluid.Positions());const double reconstruction=Elapsed(t);t=Clock::now();input=PrepareGpuSurface(pca.Kernels());const double prepare=Elapsed(t);t=Clock::now();GpuSurfaceTiming timing;
   if(cpu){output=MirrorGpuSurface(input);timing.Indices=output.RequestedIndices;timing.Overflow=output.Overflow;}else timing=extractor->Update(input);
   const double extract=Elapsed(t),total=Elapsed(start);count=timing.Indices;if(step>=5)totals.push_back(total);
   csv<<step<<','<<(step<5)<<','<<fluid.Positions().size()<<','<<solver<<','<<reconstruction<<','<<prepare<<','<<extract<<',';
   if(timing.Timestamps)csv<<timing.FieldMs<<','<<timing.EdgesMs<<','<<timing.TrianglesMs;else csv<<"NA,NA,NA";
   csv<<','<<timing.HostWaitMs<<','<<count<<','<<timing.Overflow<<','<<total<<'\n';
   if(timing.Overflow){csv.flush();throw std::runtime_error("GPU index overflow: truncated mesh rejected");}
   if(step%30==0||step+1==steps){csv.flush();log("step="+std::to_string(step+1)+" total_ms="+std::to_string(total)+" triangles="+std::to_string(count/3));}
  }
  if(!cpu)output=extractor->Readback();
  if(verify&&!cpu){const auto mirror=MirrorGpuSurface(input);double maximum=0;for(size_t i=0;i<output.Field.size();++i){const double e=std::abs(double(output.Field[i])-mirror.Field[i]);if(!std::isfinite(output.Field[i])||e>2e-4*std::max(1.f,std::abs(mirror.Field[i])))throw std::runtime_error("CPU/GPU scalar field parity FAILED at sample "+std::to_string(i)+" gpu="+std::to_string(output.Field[i])+" cpu="+std::to_string(mirror.Field[i]));maximum=std::max(maximum,e);}
   if(Faces(output)!=Faces(mirror))throw std::runtime_error("CPU/GPU triangle-edge topology parity FAILED: gpu_indices="+std::to_string(output.Indices.size())+" cpu_indices="+std::to_string(mirror.Indices.size())+" (including isovalue rounding)");
   for(auto id:output.Indices){if(id>=output.Vertices.size())throw std::runtime_error("GPU vertex index out of range");const auto&a=output.Vertices[id];const auto&b=mirror.Vertices[id];const auto delta=Vec3{a.Position.x-b.Position.x,a.Position.y-b.Position.y,a.Position.z-b.Position.z};const auto dn=Vec3{a.Normal.x-b.Normal.x,a.Normal.y-b.Normal.y,a.Normal.z-b.Normal.z};if(a.Position.w!=1||!std::isfinite(Length(delta))||Length(delta)>2e-4f||!std::isfinite(Length(dn))||Length(dn)>.002f)throw std::runtime_error("CPU/GPU vertex/normal parity FAILED");}
   log("CPU/GPU final-state parity PASS; max_field_error="+std::to_string(maximum));}
  SaveGpuSurfaceObj(output,(folder/"last-frame.obj").string().c_str());if(!csv)throw std::runtime_error("Fluid CSV write failed");
  if(!totals.empty()){std::sort(totals.begin(),totals.end());log("post_warmup_total_median_ms="+std::to_string(totals[totals.size()/2])+" p95_ms="+std::to_string(totals[std::min(totals.size()-1,size_t(std::ceil(totals.size()*.95))-1)]));}
  log("Readback/verification/export excluded from frame timings. Logs: "+folder.string());return 0;
 }catch(const std::exception& e){if(summary)summary<<"FAILED: "<<e.what()<<'\n';std::cerr<<"[FluidTest] FAILED: "<<e.what()<<'\n';return 1;}
}
}
