// Verifies the R10 timestamp scheme on a real device: availability handling, the subtraction,
// and the case that motivated it — a frame where one stage does not run at all.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
static VkPhysicalDeviceMemoryProperties gMem;
static uint32_t MemType(uint32_t b,VkMemoryPropertyFlags w){
  for(uint32_t i=0;i<gMem.memoryTypeCount;i++) if((b&(1u<<i))&&((gMem.memoryTypes[i].propertyFlags&w)==w))return i; return ~0u;}
static std::vector<uint32_t> Load(const char*p){std::ifstream f(p,std::ios::binary|std::ios::ate);
  if(!f)return{};size_t n=(size_t)f.tellg();f.seekg(0);std::vector<uint32_t>v(n/4);f.read((char*)v.data(),n);return v;}
static constexpr uint32_t kTimestampCount = 16u;
int main(){
  VkApplicationInfo A{VK_STRUCTURE_TYPE_APPLICATION_INFO};A.apiVersion=VK_API_VERSION_1_2;
  VkInstanceCreateInfo IC{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};IC.pApplicationInfo=&A;
  VkInstance I;vkCreateInstance(&IC,nullptr,&I);
  uint32_t n=1;VkPhysicalDevice P;vkEnumeratePhysicalDevices(I,&n,&P);
  VkPhysicalDeviceProperties Pp;vkGetPhysicalDeviceProperties(P,&Pp);
  vkGetPhysicalDeviceMemoryProperties(P,&gMem);
  float q=1;VkDeviceQueueCreateInfo QC{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};QC.queueCount=1;QC.pQueuePriorities=&q;
  VkDeviceCreateInfo DC{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};DC.queueCreateInfoCount=1;DC.pQueueCreateInfos=&QC;
  VkDevice D;vkCreateDevice(P,&DC,nullptr,&D);VkQueue Q;vkGetDeviceQueue(D,0,0,&Q);
  VkQueryPoolCreateInfo QP{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  QP.queryType=VK_QUERY_TYPE_TIMESTAMP;QP.queryCount=kTimestampCount;
  VkQueryPool Pool;vkCreateQueryPool(D,&QP,nullptr,&Pool);
  auto code=Load("/tmp/loop.spv");
  VkShaderModuleCreateInfo SM{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};SM.codeSize=code.size()*4;SM.pCode=code.data();
  VkShaderModule M;vkCreateShaderModule(D,&SM,nullptr,&M);
  VkPipelineLayoutCreateInfo PL{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  VkPipelineLayout L;vkCreatePipelineLayout(D,&PL,nullptr,&L);
  VkComputePipelineCreateInfo CP{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  CP.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;CP.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
  CP.stage.module=M;CP.stage.pName="main";CP.layout=L;
  VkPipeline Pi;vkCreateComputePipelines(D,VK_NULL_HANDLE,1,&CP,nullptr,&Pi);
  VkCommandPoolCreateInfo CPo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  CPo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VkCommandPool Cp;vkCreateCommandPool(D,&CPo,nullptr,&Cp);
  VkCommandBufferAllocateInfo AI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  AI.commandPool=Cp;AI.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;AI.commandBufferCount=1;
  VkCommandBuffer B;vkAllocateCommandBuffers(D,&AI,&B);

  // Two frames: one WITH the "shadow" stage (12/13 written), one WITHOUT — the case that used to
  // report stale/garbage values and be subtracted from the kernel figure.
  for(int mode=0;mode<2;mode++){
    VkCommandBufferBeginInfo BI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(B,&BI);
    vkCmdResetQueryPool(B,Pool,0,kTimestampCount);
    vkCmdWriteTimestamp(B,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,Pool,10);   // trailing span start
    if(mode==0){ // shadow stage present
      vkCmdWriteTimestamp(B,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,Pool,12);
      vkCmdBindPipeline(B,VK_PIPELINE_BIND_POINT_COMPUTE,Pi);
      vkCmdDispatch(B,3000,1,1);
      vkCmdWriteTimestamp(B,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,Pool,13);
    } else {     // GI on: ReSTIR runs instead, 12/13 never written
      vkCmdWriteTimestamp(B,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,Pool,14);
      vkCmdBindPipeline(B,VK_PIPELINE_BIND_POINT_COMPUTE,Pi);
      vkCmdDispatch(B,3000,1,1);
      vkCmdWriteTimestamp(B,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,Pool,15);
    }
    vkCmdBindPipeline(B,VK_PIPELINE_BIND_POINT_COMPUTE,Pi);
    vkCmdDispatch(B,1500,1,1);   // stands in for denoise + luminance
    vkCmdWriteTimestamp(B,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,Pool,11); // trailing span end
    vkEndCommandBuffer(B);
    VkSubmitInfo S{VK_STRUCTURE_TYPE_SUBMIT_INFO};S.commandBufferCount=1;S.pCommandBuffers=&B;
    vkQueueSubmit(Q,1,&S,VK_NULL_HANDLE);vkQueueWaitIdle(Q);

    // Exactly the engine's read path.
    uint64_t St[kTimestampCount*2]{};
    VkResult r=vkGetQueryPoolResults(D,Pool,0,kTimestampCount,sizeof(St),St,sizeof(uint64_t)*2,
                  VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    auto Have=[&](uint32_t i){return St[i*2+1]!=0u;};
    auto Val =[&](uint32_t i){return St[i*2];};
    auto Ms=[&](uint32_t a,uint32_t b){
      if(!Have(a)||!Have(b)||Val(b)<=Val(a)) return 0.0f;
      return (float)((double)(Val(b)-Val(a))*Pp.limits.timestampPeriod*1e-6);};
    const float Shadow=Ms(12,13), Restir=Ms(14,15), Trailing=Ms(10,11);
    const float Kernel = Restir>0.0f?Restir:(Trailing>Shadow?Trailing-Shadow:0.0f);
    const float Owned  = Restir>0.0f?Restir:Shadow;
    const float Post   = Trailing>Owned?Trailing-Owned:0.0f;
    std::printf("\n%s (getResults rc=%d)\n", mode==0?"FRAME A - GI OFF: shadow stage runs, ReSTIR does not"
                                                    :"FRAME B - GI ON: ReSTIR runs, shadow stage does not", r);
    std::printf("  availability: q12 %d q13 %d q14 %d q15 %d\n",(int)Have(12),(int)Have(13),(int)Have(14),(int)Have(15));
    std::printf("  trailing(10-11) %.3f ms | shadow %.3f | restir %.3f | => kernel %.3f  post %.3f\n",
       Trailing,Shadow,Restir,Kernel,Post);
    std::printf("  check: shadow+restir+post = %.3f vs trailing %.3f  (delta %.3f)\n",
       Shadow+Restir+Post,Trailing,(Shadow+Restir+Post)-Trailing);
  }
  std::printf("\n");
  return 0;}
