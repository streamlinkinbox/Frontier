#include "GpuSurfaceExtractor.h"
#include "MarchingCubesTables.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
namespace Frontier::ProjectFluid {
namespace {void Check(VkResult r,const char* where){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(where)+" VkResult="+std::to_string(r));}}
struct GpuSurfaceExtractor::Implementation {
    struct Buffer {VkBuffer Handle{};VkDeviceMemory Memory{};VkDeviceSize Size{},Allocated{};void* Map{};};
    VkPhysicalDevice Physical;VkDevice Device;VkQueue Queue;uint32_t Family;VkPhysicalDeviceProperties Properties{};uint32_t TimestampBits{};
    std::array<Buffer,8> Buffers{};Buffer Upload,Download;
    VkDescriptorSetLayout SetLayout{};VkDescriptorPool DescriptorPool{};VkDescriptorSet Set{};
    VkPipelineLayout Layout{};VkPipeline Pipeline{};VkCommandPool Pool{};VkCommandBuffer Command{};VkFence Fence{};VkQueryPool Queries{};
    SurfaceGpuParameters Parameters;uint64_t Bytes{};bool Completed=false;
    Implementation(VkPhysicalDevice p,VkDevice d,VkQueue q,uint32_t f):Physical(p),Device(d),Queue(q),Family(f){}
    ~Implementation(){if(Device){vkDeviceWaitIdle(Device);for(auto& b:Buffers)Destroy(b);Destroy(Upload);Destroy(Download);
        if(Queries)vkDestroyQueryPool(Device,Queries,nullptr);if(Fence)vkDestroyFence(Device,Fence,nullptr);if(Pool)vkDestroyCommandPool(Device,Pool,nullptr);
        if(Pipeline)vkDestroyPipeline(Device,Pipeline,nullptr);if(Layout)vkDestroyPipelineLayout(Device,Layout,nullptr);if(DescriptorPool)vkDestroyDescriptorPool(Device,DescriptorPool,nullptr);if(SetLayout)vkDestroyDescriptorSetLayout(Device,SetLayout,nullptr);}}
    void Destroy(Buffer& b){if(b.Map)vkUnmapMemory(Device,b.Memory);if(b.Handle)vkDestroyBuffer(Device,b.Handle,nullptr);if(b.Memory){vkFreeMemory(Device,b.Memory,nullptr);Bytes-=b.Allocated;}b={};}
    void Make(Buffer& b,VkDeviceSize size,VkBufferUsageFlags usage,bool host){
        b.Size=size;VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=usage;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;Check(vkCreateBuffer(Device,&info,nullptr,&b.Handle),"create fluid buffer");
        VkMemoryRequirements req;vkGetBufferMemoryRequirements(Device,b.Handle,&req);VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(Physical,&memory);
        const auto flags=host?(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT):VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
        if(type==UINT32_MAX)throw std::runtime_error("No suitable fluid buffer memory type");
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;Check(vkAllocateMemory(Device,&alloc,nullptr,&b.Memory),"allocate fluid memory");b.Allocated=req.size;Bytes+=req.size;
        Check(vkBindBufferMemory(Device,b.Handle,b.Memory,0),"bind fluid memory");if(host)Check(vkMapMemory(Device,b.Memory,0,size,0,&b.Map),"map fluid memory");
    }
    void Init(const char* shader){
        vkGetPhysicalDeviceProperties(Physical,&Properties);const auto& limits=Properties.limits;
        if(limits.maxPerStageDescriptorStorageBuffers<8||limits.maxDescriptorSetStorageBuffers<8||limits.maxComputeWorkGroupInvocations<64||limits.maxComputeWorkGroupSize[0]<64||limits.maxPushConstantsSize<64)throw std::runtime_error("GPU lacks required fluid compute limits");
        uint32_t count=0;vkGetPhysicalDeviceQueueFamilyProperties(Physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(Physical,&count,families.data());
        if(Family>=count||!(families[Family].queueFlags&VK_QUEUE_COMPUTE_BIT))throw std::runtime_error("Fluid requires a compute queue");TimestampBits=families[Family].timestampValidBits;
        const auto n=Parameters.Grid[0]*Parameters.Grid[1]*Parameters.Grid[2];
        const std::array<VkDeviceSize,8> sizes={PbfFluid::MaxParticles*sizeof(SurfaceGpuKernel),4096, PbfFluid::MaxParticles*128u*sizeof(uint32_t),n*sizeof(float),4096*sizeof(int32_t),n*3*sizeof(SurfaceGpuVertex),Parameters.Config[0]*sizeof(uint32_t),8};
        VkDeviceSize total=0;for(uint32_t i=0;i<8;++i){if(sizes[i]>limits.maxStorageBufferRange)throw std::runtime_error("Fluid buffer exceeds maxStorageBufferRange");Make(Buffers[i],sizes[i],VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|(i==5?VK_BUFFER_USAGE_VERTEX_BUFFER_BIT:0)|(i==6?VK_BUFFER_USAGE_INDEX_BUFFER_BIT:0),false);total+=sizes[i];}
        Make(Upload,sizes[0]+sizes[1]+sizes[2]+sizes[4]+8,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);
        // Readback storage is lazy: normal updates copy only the 8-byte status.
        Make(Download,8,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);
        std::array<VkDescriptorSetLayoutBinding,8> bindings{};for(uint32_t i=0;i<8;++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=8;sl.pBindings=bindings.data();Check(vkCreateDescriptorSetLayout(Device,&sl,nullptr,&SetLayout),"fluid descriptor layout");
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=1;dp.pPoolSizes=&poolSize;Check(vkCreateDescriptorPool(Device,&dp,nullptr,&DescriptorPool),"fluid descriptor pool");
        VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=DescriptorPool;sa.descriptorSetCount=1;sa.pSetLayouts=&SetLayout;Check(vkAllocateDescriptorSets(Device,&sa,&Set),"fluid descriptors");
        std::array<VkDescriptorBufferInfo,8> bi{};std::array<VkWriteDescriptorSet,8> writes{};for(uint32_t i=0;i<8;++i){bi[i]={Buffers[i].Handle,0,sizes[i]};writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=Set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&bi[i];}vkUpdateDescriptorSets(Device,8,writes.data(),0,nullptr);
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Parameters)};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&SetLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;Check(vkCreatePipelineLayout(Device,&pl,nullptr,&Layout),"fluid pipeline layout");
        std::ifstream file(shader,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error(std::string("Missing shader: ")+shader);const auto bytes=file.tellg();if(bytes<=0||bytes%4)throw std::runtime_error("Invalid fluid SPIR-V");std::vector<uint32_t> code(size_t(bytes)/4);file.seekg(0);if(!file.read(reinterpret_cast<char*>(code.data()),bytes))throw std::runtime_error("Cannot read fluid SPIR-V");
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=size_t(bytes);sm.pCode=code.data();VkShaderModule module{};Check(vkCreateShaderModule(Device,&sm,nullptr,&module),"fluid shader");
        VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pc.layout=Layout;pc.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pc.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pc.stage.module=module;pc.stage.pName="main";auto result=vkCreateComputePipelines(Device,VK_NULL_HANDLE,1,&pc,nullptr,&Pipeline);vkDestroyShaderModule(Device,module,nullptr);Check(result,"fluid pipeline");
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=Family;cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;Check(vkCreateCommandPool(Device,&cp,nullptr,&Pool),"fluid command pool");VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=Pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;Check(vkAllocateCommandBuffers(Device,&ca,&Command),"fluid command buffer");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};Check(vkCreateFence(Device,&fc,nullptr,&Fence),"fluid fence");if(TimestampBits){VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qp.queryType=VK_QUERY_TYPE_TIMESTAMP;qp.queryCount=4;Check(vkCreateQueryPool(Device,&qp,nullptr,&Queries),"fluid timestamps");}
    }
    void Begin(){Check(vkResetFences(Device,1,&Fence),"reset fluid fence");Check(vkResetCommandBuffer(Command,0),"reset fluid command");VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;Check(vkBeginCommandBuffer(Command,&b),"begin fluid command");}
    void Barrier(VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags a,VkAccessFlags b){VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};m.srcAccessMask=a;m.dstAccessMask=b;vkCmdPipelineBarrier(Command,src,dst,0,1,&m,0,nullptr,0,nullptr);}
    void Submit(){Check(vkEndCommandBuffer(Command),"end fluid command");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&Command;Check(vkQueueSubmit(Queue,1,&submit,Fence),"submit fluid");Check(vkWaitForFences(Device,1,&Fence,VK_TRUE,UINT64_MAX),"wait fluid");}
};
GpuSurfaceExtractor::GpuSurfaceExtractor(VkPhysicalDevice p,VkDevice d,VkQueue q,uint32_t family,const char* shader):P(std::make_unique<Implementation>(p,d,q,family)){P->Init(shader);}
GpuSurfaceExtractor::~GpuSurfaceExtractor()=default;
VkBuffer GpuSurfaceExtractor::VertexBuffer()const{return P->Buffers[5].Handle;}
VkBuffer GpuSurfaceExtractor::IndexBuffer()const{return P->Buffers[6].Handle;}
uint64_t GpuSurfaceExtractor::AllocatedBytes()const{return P->Bytes;}
GpuSurfaceTiming GpuSurfaceExtractor::Update(const SurfaceGpuInput& input){
    auto& p=*P;auto params=input.Parameters;const SurfaceGpuParameters defaults;
    if(params.Grid[0]!=defaults.Grid[0]||params.Grid[1]!=defaults.Grid[1]||params.Grid[2]!=defaults.Grid[2]||params.Config[0]>defaults.Config[0]||params.Config[2]!=8)throw std::runtime_error("Unsupported fluid grid/capacity");
    const auto brickCount=((params.Grid[0]+7)/8)*((params.Grid[1]+7)/8)*((params.Grid[2]+7)/8);
    if(input.Kernels.size()>PbfFluid::MaxParticles||params.Grid[3]!=input.Kernels.size()||input.Offsets.size()!=brickCount+1||input.Offsets.front()!=0||input.Offsets.back()!=input.Ids.size()||!std::is_sorted(input.Offsets.begin(),input.Offsets.end()))throw std::runtime_error("Invalid fluid candidate layout");
    for(auto id:input.Ids)if(id>=input.Kernels.size())throw std::runtime_error("Invalid fluid candidate index");
    p.Parameters=params;p.Completed=false;p.Begin();VkDeviceSize cursor=0;
    auto copy=[&](int slot,const void* data,size_t bytes){if(bytes>p.Buffers[slot].Size||cursor+bytes>p.Upload.Size)throw std::runtime_error("Fluid upload capacity exceeded");if(bytes){std::memcpy(static_cast<char*>(p.Upload.Map)+cursor,data,bytes);VkBufferCopy c{cursor,0,bytes};vkCmdCopyBuffer(p.Command,p.Upload.Handle,p.Buffers[slot].Handle,1,&c);cursor+=bytes;}};
    copy(0,input.Kernels.data(),input.Kernels.size()*sizeof(SurfaceGpuKernel));copy(1,input.Offsets.data(),input.Offsets.size()*4);copy(2,input.Ids.data(),input.Ids.size()*4);
    std::array<int32_t,4096> table;std::copy(MarchingCubesTables::Tri.begin(),MarchingCubesTables::Tri.end(),table.begin());copy(4,table.data(),sizeof(table));const uint32_t zero[2]={};copy(7,zero,8);
    p.Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    if(p.Queries){vkCmdResetQueryPool(p.Command,p.Queries,0,4);vkCmdWriteTimestamp(p.Command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,p.Queries,0);}
    vkCmdBindPipeline(p.Command,VK_PIPELINE_BIND_POINT_COMPUTE,p.Pipeline);vkCmdBindDescriptorSets(p.Command,VK_PIPELINE_BIND_POINT_COMPUTE,p.Layout,0,1,&p.Set,0,nullptr);
    const uint32_t n=params.Grid[0]*params.Grid[1]*params.Grid[2],cells=(params.Grid[0]-1)*(params.Grid[1]-1)*(params.Grid[2]-1);
    for(uint32_t stage=0;stage<3;++stage){params.Config[1]=stage;vkCmdPushConstants(p.Command,p.Layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),&params);const auto groups=((stage==0?n:stage==1?n*3:cells)+63)/64;if(groups>p.Properties.limits.maxComputeWorkGroupCount[0])throw std::runtime_error("Fluid dispatch limit exceeded");vkCmdDispatch(p.Command,groups,1,1);
        p.Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);if(p.Queries)vkCmdWriteTimestamp(p.Command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,p.Queries,stage+1);}
    p.Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkBufferCopy status{0,0,8};vkCmdCopyBuffer(p.Command,p.Buffers[7].Handle,p.Download.Handle,1,&status);p.Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
    auto start=std::chrono::steady_clock::now();p.Submit();GpuSurfaceTiming timing;timing.HostWaitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();const auto* result=static_cast<const uint32_t*>(p.Download.Map);timing.Indices=result[0];timing.Overflow=result[1];
    if(p.Queries){uint64_t ticks[4]{};Check(vkGetQueryPoolResults(p.Device,p.Queries,0,4,sizeof(ticks),ticks,8,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"fluid timestamps");const uint64_t mask=p.TimestampBits==64?UINT64_MAX:((uint64_t(1)<<p.TimestampBits)-1);auto delta=[&](int a,int b){return double((ticks[b]-ticks[a])&mask)*p.Properties.limits.timestampPeriod/1e6;};timing.FieldMs=delta(0,1);timing.EdgesMs=delta(1,2);timing.TrianglesMs=delta(2,3);timing.Timestamps=true;}
    p.Completed=true;return timing;
}
SurfaceGpuOutput GpuSurfaceExtractor::Readback(){auto& p=*P;if(!p.Completed)throw std::runtime_error("No completed fluid mesh");
    const auto required=p.Buffers[3].Size+p.Buffers[5].Size+p.Buffers[6].Size+8;if(p.Download.Size<required){p.Destroy(p.Download);p.Make(p.Download,required,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);}
    p.Begin();p.Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkDeviceSize cursor=0;
    for(int slot:{7,3,5,6}){VkBufferCopy c{0,cursor,p.Buffers[slot].Size};vkCmdCopyBuffer(p.Command,p.Buffers[slot].Handle,p.Download.Handle,1,&c);cursor+=c.size;}p.Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);p.Submit();
    SurfaceGpuOutput out;const auto* bytes=static_cast<const char*>(p.Download.Map);std::memcpy(&out.RequestedIndices,bytes,4);std::memcpy(&out.Overflow,bytes+4,4);cursor=8;
    out.Field.resize(p.Buffers[3].Size/4);std::memcpy(out.Field.data(),bytes+cursor,p.Buffers[3].Size);cursor+=p.Buffers[3].Size;
    out.Vertices.resize(p.Buffers[5].Size/sizeof(SurfaceGpuVertex));std::memcpy(out.Vertices.data(),bytes+cursor,p.Buffers[5].Size);cursor+=p.Buffers[5].Size;
    out.Indices.resize(std::min(out.RequestedIndices,p.Parameters.Config[0]));std::memcpy(out.Indices.data(),bytes+cursor,out.Indices.size()*4);return out;
}
}
