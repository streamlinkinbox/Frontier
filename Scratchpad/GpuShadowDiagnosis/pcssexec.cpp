// Render a shadow map with the real ShadowRaster, then run the real ShadowSample PCSS over a receiver plane.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <fstream>
#include <algorithm>
static int gErr=0,gWarn=0;
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCB(VkDebugUtilsMessageSeverityFlagBitsEXT s,
  VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* d,void*){
  if(s&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){gErr++;std::printf("  [VALIDATION-ERROR] %s\n",d->pMessage);}
  else if(s&VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT){gWarn++;std::printf("  [VALIDATION-WARN] %s\n",d->pMessage);}
  return VK_FALSE;}
static std::vector<uint32_t> Load(const char*p){std::ifstream f(p,std::ios::binary|std::ios::ate);
  if(!f){std::printf("missing %s\n",p);return{};}size_t n=(size_t)f.tellg();f.seekg(0);
  std::vector<uint32_t>v(n/4);f.read((char*)v.data(),n);return v;}
static VkPhysicalDeviceMemoryProperties gMem;
static uint32_t MemType(uint32_t bits,VkMemoryPropertyFlags w){
  for(uint32_t i=0;i<gMem.memoryTypeCount;i++)if((bits&(1u<<i))&&((gMem.memoryTypes[i].propertyFlags&w)==w))return i;return ~0u;}
struct Buf{VkBuffer b{};VkDeviceMemory m{};void* map{};};
static Buf MakeBuf(VkDevice D,VkDeviceSize sz,VkBufferUsageFlags u){
  Buf r;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=sz;ci.usage=u;
  vkCreateBuffer(D,&ci,nullptr,&r.b);VkMemoryRequirements mr;vkGetBufferMemoryRequirements(D,r.b,&mr);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=mr.size;
  ai.memoryTypeIndex=MemType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vkAllocateMemory(D,&ai,nullptr,&r.m);vkBindBufferMemory(D,r.b,r.m,0);vkMapMemory(D,r.m,0,sz,0,&r.map);return r;}
struct M4{float m[16];};
static M4 BuildLightClip(const float T[3],const float C[3],float HalfDeg,float N,float F){
  constexpr float kPi=3.14159265358979323846f;
  float Dx=C[0]-T[0],Dy=C[1]-T[1],Dz=C[2]-T[2];float Dl=std::sqrt(Dx*Dx+Dy*Dy+Dz*Dz);
  if(Dl<=0){Dx=0;Dy=0;Dz=-1;}else{Dx/=Dl;Dy/=Dl;Dz/=Dl;}
  float Ux=0,Uy=0,Uz=1; if(Dx*Dx+Dy*Dy<0.01f){Ux=0;Uy=1;Uz=0;}
  float Rx=Dy*Uz-Dz*Uy,Ry=Dz*Ux-Dx*Uz,Rz=Dx*Uy-Dy*Ux;
  float Rl=std::sqrt(Rx*Rx+Ry*Ry+Rz*Rz); if(Rl>0){Rx/=Rl;Ry/=Rl;Rz/=Rl;}
  float Vx=Ry*Dz-Rz*Dy,Vy=Rz*Dx-Rx*Dz,Vz=Rx*Dy-Ry*Dx;
  float Fo=1.0f/std::tan(HalfDeg*kPi/180.0f),Rg=F/(F-N);
  const float R0[4]={Fo*Rx,Fo*Ry,Fo*Rz,-Fo*(Rx*T[0]+Ry*T[1]+Rz*T[2])};
  const float R1[4]={-Fo*Vx,-Fo*Vy,-Fo*Vz,Fo*(Vx*T[0]+Vy*T[1]+Vz*T[2])};
  const float R2[4]={Rg*Dx,Rg*Dy,Rg*Dz,-Rg*(Dx*T[0]+Dy*T[1]+Dz*T[2])-Rg*N};
  const float R3[4]={Dx,Dy,Dz,-(Dx*T[0]+Dy*T[1]+Dz*T[2])};
  M4 M{};for(int c=0;c<4;c++){M.m[c*4+0]=R0[c];M.m[c*4+1]=R1[c];M.m[c*4+2]=R2[c];M.m[c*4+3]=R3[c];}return M;}

int main(int argc,char**argv){
  const float LightSize = argc>1 ? (float)atof(argv[1]) : 0.5f;
  const uint32_t Side=256, Out=128;
  VkApplicationInfo A{VK_STRUCTURE_TYPE_APPLICATION_INFO};A.apiVersion=VK_API_VERSION_1_2;
  const char* lay[]={"VK_LAYER_KHRONOS_validation"};const char* ext[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
  uint32_t lc=0;vkEnumerateInstanceLayerProperties(&lc,nullptr);std::vector<VkLayerProperties>lp(lc);
  vkEnumerateInstanceLayerProperties(&lc,lp.data());bool hv=false;for(auto&l:lp)if(!std::strcmp(l.layerName,lay[0]))hv=true;
  VkInstanceCreateInfo IC{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};IC.pApplicationInfo=&A;
  if(hv){IC.enabledLayerCount=1;IC.ppEnabledLayerNames=lay;IC.enabledExtensionCount=1;IC.ppEnabledExtensionNames=ext;}
  VkInstance I;vkCreateInstance(&IC,nullptr,&I);
  if(hv){auto mk=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(I,"vkCreateDebugUtilsMessengerEXT");
    if(mk){VkDebugUtilsMessengerCreateInfoEXT M{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      M.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
      M.messageType=0x7;M.pfnUserCallback=DebugCB;VkDebugUtilsMessengerEXT h;mk(I,&M,nullptr,&h);}}
  uint32_t n=1;VkPhysicalDevice P;vkEnumeratePhysicalDevices(I,&n,&P);vkGetPhysicalDeviceMemoryProperties(P,&gMem);
  float q=1;VkDeviceQueueCreateInfo QC{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};QC.queueCount=1;QC.pQueuePriorities=&q;
  VkDeviceCreateInfo DC{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};DC.queueCreateInfoCount=1;DC.pQueueCreateInfos=&QC;
  VkDevice D;vkCreateDevice(P,&DC,nullptr,&D);VkQueue Q;vkGetDeviceQueue(D,0,0,&Q);
  // --- shadow map as a 2D ARRAY (what ShadowSample expects)
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=VK_IMAGE_TYPE_2D;
  ii.format=VK_FORMAT_D32_SFLOAT;ii.extent={Side,Side,1};ii.mipLevels=1;ii.arrayLayers=4;
  ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
  VkImage smap;vkCreateImage(D,&ii,nullptr,&smap);
  VkMemoryRequirements mr;vkGetImageMemoryRequirements(D,smap,&mr);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=mr.size;
  ai.memoryTypeIndex=MemType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory smem;vkAllocateMemory(D,&ai,nullptr,&smem);vkBindImageMemory(D,smap,smem,0);
  VkImageViewCreateInfo lv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};lv.image=smap;lv.viewType=VK_IMAGE_VIEW_TYPE_2D;
  lv.format=VK_FORMAT_D32_SFLOAT;lv.components={VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};lv.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
  VkImageView layer0;vkCreateImageView(D,&lv,nullptr,&layer0);
  VkImageViewCreateInfo av{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};av.image=smap;av.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  av.format=VK_FORMAT_D32_SFLOAT;av.components={VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};av.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,4};
  VkImageView arrview;vkCreateImageView(D,&av,nullptr,&arrview);
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=VK_FILTER_NEAREST;si.minFilter=VK_FILTER_NEAREST;
  si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  VkSampler samp;vkCreateSampler(D,&si,nullptr,&samp);
  // --- output image
  VkImageCreateInfo oi{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};oi.imageType=VK_IMAGE_TYPE_2D;oi.format=VK_FORMAT_R32_SFLOAT;
  oi.extent={Out,Out,1};oi.mipLevels=1;oi.arrayLayers=1;oi.samples=VK_SAMPLE_COUNT_1_BIT;
  oi.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImage oimg;vkCreateImage(D,&oi,nullptr,&oimg);
  vkGetImageMemoryRequirements(D,oimg,&mr);ai.allocationSize=mr.size;
  ai.memoryTypeIndex=MemType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory omem;vkAllocateMemory(D,&ai,nullptr,&omem);vkBindImageMemory(D,oimg,omem,0);
  VkImageViewCreateInfo ov{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ov.image=oimg;ov.viewType=VK_IMAGE_VIEW_TYPE_2D;
  ov.format=VK_FORMAT_R32_SFLOAT;ov.components={VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};ov.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  VkImageView oview;vkCreateImageView(D,&ov,nullptr,&oview);
  // --- scene: occluder quad at y=1
  Buf vb=MakeBuf(D,64*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT), ib=MakeBuf(D,24,VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
      inb=MakeBuf(D,160,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT), cbf=MakeBuf(D,48,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
      ubo=MakeBuf(D,480,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  float* V=(float*)vb.map;std::memset(V,0,256);
  float pos[4][3]={{-0.5f,1,-0.5f},{0.5f,1,-0.5f},{0.5f,1,0.5f},{-0.5f,1,0.5f}};
  for(int i=0;i<4;i++){V[i*16]=pos[i][0];V[i*16+1]=pos[i][1];V[i*16+2]=pos[i][2];V[i*16+3]=1;}
  uint32_t idx[6]={0,1,2,0,2,3};std::memcpy(ib.map,idx,24);
  float* In=(float*)inb.map;std::memset(In,0,160);In[0]=In[5]=In[10]=In[15]=1;In[16]=In[21]=In[26]=In[31]=1;
  uint32_t* Iu=(uint32_t*)(In+32);Iu[2]=2;Iu[5]=1;
  float* Cc=(float*)cbf.map;std::memset(Cc,0,48);Cc[3]=100;Cc[7]=1;
  uint32_t* Cu=(uint32_t*)(Cc+8);Cu[2]=2;
  std::memset(ubo.map,0,480);float* U=(float*)ubo.map;
  const float LP[3]={0,3,0},AT[3]={0,0,0};const float HalfDeg=35.0f, Near=0.1f, Far=10.0f;
  M4 clip=BuildLightClip(LP,AT,HalfDeg,Near,Far);std::memcpy(U,clip.m,64);
  U[64]=LP[0];U[65]=LP[1];U[66]=LP[2];U[67]=LightSize;             // TapOrigin.w = light size
  U[80]=1;U[81]=1;U[82]=1;U[83]=1;                                  // radiance
  U[96]=0;U[97]=-1;U[98]=0;
  U[99]=std::tan(HalfDeg*3.14159265f/180.0f);                       // TapNormal.w = tan(half) — the FIX
  U[112]=(float)Side;U[113]=Near;U[114]=Far;U[115]=0.005f;
  uint32_t* Uc=(uint32_t*)(U+116);Uc[0]=2;Uc[1]=5;Uc[2]=1;Uc[3]=0;  // PCSS, 5 taps, 1 live
  // --- shadow raster pass
  VkAttachmentDescription ad{};ad.format=VK_FORMAT_D32_SFLOAT;ad.samples=VK_SAMPLE_COUNT_1_BIT;
  ad.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;ad.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
  ad.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;ad.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference ar{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sd{};sd.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sd.pDepthStencilAttachment=&ar;
  VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rpi.attachmentCount=1;rpi.pAttachments=&ad;
  rpi.subpassCount=1;rpi.pSubpasses=&sd;VkRenderPass rp;vkCreateRenderPass(D,&rpi,nullptr,&rp);
  VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fbi.renderPass=rp;fbi.attachmentCount=1;
  fbi.pAttachments=&layer0;fbi.width=Side;fbi.height=Side;fbi.layers=1;VkFramebuffer fb;vkCreateFramebuffer(D,&fbi,nullptr,&fb);
  auto vc=Load("/tmp/spv/ShadowRaster.vert.slang.spv"),fc=Load("/tmp/spv/ShadowRaster.frag.slang.spv"),pc=Load("/tmp/spv/pcss.spv");
  if(vc.empty()||fc.empty()||pc.empty())return 1;
  VkShaderModule vm,fm,pm;
  VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  sm.codeSize=vc.size()*4;sm.pCode=vc.data();vkCreateShaderModule(D,&sm,nullptr,&vm);
  sm.codeSize=fc.size()*4;sm.pCode=fc.data();vkCreateShaderModule(D,&sm,nullptr,&fm);
  sm.codeSize=pc.size()*4;sm.pCode=pc.data();vkCreateShaderModule(D,&sm,nullptr,&pm);
  VkDescriptorSetLayoutBinding b0[]={{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},
    {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},
    {3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}};
  VkDescriptorSetLayoutBinding b1[]={{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
    {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
  VkDescriptorSetLayoutBinding bc[]={{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
  VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  VkDescriptorSetLayout d0,d1,dc;
  li.bindingCount=3;li.pBindings=b0;vkCreateDescriptorSetLayout(D,&li,nullptr,&d0);
  li.bindingCount=2;li.pBindings=b1;vkCreateDescriptorSetLayout(D,&li,nullptr,&d1);
  li.bindingCount=1;li.pBindings=bc;vkCreateDescriptorSetLayout(D,&li,nullptr,&dc);
  VkDescriptorSetLayout gl[2]={d0,d1};
  VkPushConstantRange gpc{VK_SHADER_STAGE_VERTEX_BIT,0,4};
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pli.setLayoutCount=2;pli.pSetLayouts=gl;
  pli.pushConstantRangeCount=1;pli.pPushConstantRanges=&gpc;VkPipelineLayout gplay;vkCreatePipelineLayout(D,&pli,nullptr,&gplay);
  VkDescriptorSetLayout cl[2]={dc,d1};
  VkPushConstantRange cpc{VK_SHADER_STAGE_COMPUTE_BIT,0,16};
  VkPipelineLayoutCreateInfo cli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};cli.setLayoutCount=2;cli.pSetLayouts=cl;
  cli.pushConstantRangeCount=1;cli.pPushConstantRanges=&cpc;VkPipelineLayout cplay;vkCreatePipelineLayout(D,&cli,nullptr,&cplay);
  VkPipelineShaderStageCreateInfo st[2]{};
  st[0].sType=st[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  st[0].stage=VK_SHADER_STAGE_VERTEX_BIT;st[0].module=vm;st[0].pName="main";
  st[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;st[1].module=fm;st[1].pName="main";
  VkPipelineVertexInputStateCreateInfo pvi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo pia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  pia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport vp{0,0,(float)Side,(float)Side,0,1};VkRect2D sc{{0,0},{Side,Side}};
  VkPipelineViewportStateCreateInfo pvs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  pvs.viewportCount=1;pvs.pViewports=&vp;pvs.scissorCount=1;pvs.pScissors=&sc;
  VkPipelineRasterizationStateCreateInfo prs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  prs.polygonMode=VK_POLYGON_MODE_FILL;prs.cullMode=VK_CULL_MODE_NONE;prs.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo pms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  pms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo pds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  pds.depthTestEnable=VK_TRUE;pds.depthWriteEnable=VK_TRUE;pds.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendStateCreateInfo pcb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpi.stageCount=2;gpi.pStages=st;gpi.pVertexInputState=&pvi;gpi.pInputAssemblyState=&pia;gpi.pViewportState=&pvs;
  gpi.pRasterizationState=&prs;gpi.pMultisampleState=&pms;gpi.pDepthStencilState=&pds;gpi.pColorBlendState=&pcb;
  gpi.layout=gplay;gpi.renderPass=rp;VkPipeline gp;
  if(vkCreateGraphicsPipelines(D,VK_NULL_HANDLE,1,&gpi,nullptr,&gp)!=VK_SUCCESS){std::printf("gfx pipeline FAILED\n");return 1;}
  VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cpi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
  cpi.stage.module=pm;cpi.stage.pName="main";cpi.layout=cplay;VkPipeline cp2;
  if(vkCreateComputePipelines(D,VK_NULL_HANDLE,1,&cpi,nullptr,&cp2)!=VK_SUCCESS){std::printf("compute pipeline FAILED\n");return 1;}
  VkDescriptorPoolSize ps[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
  VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpi.maxSets=3;dpi.poolSizeCount=4;dpi.pPoolSizes=ps;
  VkDescriptorPool dp;vkCreateDescriptorPool(D,&dpi,nullptr,&dp);
  VkDescriptorSetLayout all[3]={d0,d1,dc};VkDescriptorSet ds[3];
  VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};dai.descriptorPool=dp;
  dai.descriptorSetCount=3;dai.pSetLayouts=all;vkAllocateDescriptorSets(D,&dai,ds);
  VkDescriptorBufferInfo i1{inb.b,0,VK_WHOLE_SIZE},i2{cbf.b,0,VK_WHOLE_SIZE},i3{vb.b,0,VK_WHOLE_SIZE},i4{ubo.b,0,VK_WHOLE_SIZE};
  VkDescriptorImageInfo im1{samp,arrview,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo im2{VK_NULL_HANDLE,oview,VK_IMAGE_LAYOUT_GENERAL};
  VkWriteDescriptorSet w[6];std::memset(w,0,sizeof(w));
  for(int i=0;i<6;i++){w[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[i].descriptorCount=1;}
  w[0].dstSet=ds[0];w[0].dstBinding=1;w[0].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[0].pBufferInfo=&i1;
  w[1].dstSet=ds[0];w[1].dstBinding=2;w[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[1].pBufferInfo=&i2;
  w[2].dstSet=ds[0];w[2].dstBinding=3;w[2].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[2].pBufferInfo=&i3;
  w[3].dstSet=ds[1];w[3].dstBinding=0;w[3].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w[3].pBufferInfo=&i4;
  w[4].dstSet=ds[1];w[4].dstBinding=1;w[4].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w[4].pImageInfo=&im1;
  w[5].dstSet=ds[2];w[5].dstBinding=0;w[5].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w[5].pImageInfo=&im2;
  vkUpdateDescriptorSets(D,6,w,0,nullptr);
  VkCommandPoolCreateInfo cpo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool cpool;
  vkCreateCommandPool(D,&cpo,nullptr,&cpool);
  VkCommandBufferAllocateInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cbi.commandPool=cpool;
  cbi.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cbi.commandBufferCount=1;VkCommandBuffer cmd;vkAllocateCommandBuffers(D,&cbi,&cmd);
  Buf rb=MakeBuf(D,(VkDeviceSize)Out*Out*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  VkCommandBufferBeginInfo bg{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkBeginCommandBuffer(cmd,&bg);
  VkImageMemoryBarrier ob{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};ob.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
  ob.newLayout=VK_IMAGE_LAYOUT_GENERAL;ob.image=oimg;ob.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  ob.srcQueueFamilyIndex=ob.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;ob.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&ob);
  VkClearValue cv{};cv.depthStencil={1.f,0};
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rbi.renderPass=rp;rbi.framebuffer=fb;
  rbi.renderArea=sc;rbi.clearValueCount=1;rbi.pClearValues=&cv;
  vkCmdBeginRenderPass(cmd,&rbi,VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,gp);
  vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,gplay,0,1,&ds[0],0,nullptr);
  vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,gplay,1,1,&ds[1],0,nullptr);
  uint32_t tap=0;vkCmdPushConstants(cmd,gplay,VK_SHADER_STAGE_VERTEX_BIT,0,4,&tap);
  vkCmdBindIndexBuffer(cmd,ib.b,0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(cmd,6,1,0,0,0);
  vkCmdEndRenderPass(cmd);
  // The render pass's finalLayout hands the image to SHADER_READ_ONLY, but a layout transition is not
  // execution synchronisation: without this barrier the compute dispatch may sample the depth image while
  // the raster is still writing it. The engine's own RecordShadowFrame must do the equivalent.
  VkImageMemoryBarrier sb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  sb.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;sb.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  sb.image=smap;sb.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,4};
  sb.srcQueueFamilyIndex=sb.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  sb.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;sb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       0,0,nullptr,0,nullptr,1,&sb);
  vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,cp2);
  vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,cplay,0,1,&ds[2],0,nullptr);
  vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,cplay,1,1,&ds[1],0,nullptr);
  struct{float ox,oy,scale,y;}push{0,0,3.0f,0.0f};
  vkCmdPushConstants(cmd,cplay,VK_SHADER_STAGE_COMPUTE_BIT,0,16,&push);
  vkCmdDispatch(cmd,(Out+15)/16,(Out+15)/16,1);
  VkImageMemoryBarrier tb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};tb.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
  tb.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;tb.image=oimg;tb.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  tb.srcQueueFamilyIndex=tb.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;tb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
  tb.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&tb);
  VkBufferImageCopy bic{};bic.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};bic.imageExtent={Out,Out,1};
  vkCmdCopyImageToBuffer(cmd,oimg,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rb.b,1,&bic);
  vkEndCommandBuffer(cmd);
  VkSubmitInfo su{VK_STRUCTURE_TYPE_SUBMIT_INFO};su.commandBufferCount=1;su.pCommandBuffers=&cmd;
  vkQueueSubmit(Q,1,&su,VK_NULL_HANDLE);vkQueueWaitIdle(Q);
  float* lit=(float*)rb.map;uint32_t full=0,zero=0,part=0;
  for(uint32_t i=0;i<Out*Out;i++){float v=lit[i];if(v>=0.999f)full++;else if(v<=0.001f)zero++;else part++;}
  std::printf("light size %.2f m : umbra %5u  penumbra %5u  lit %5u   (penumbra %.1f%%)\n",
    LightSize,zero,part,full,100.0*part/(Out*Out));
  // centre row profile
  std::printf("  centre row: ");
  for(uint32_t x=0;x<Out;x+=8) std::printf("%.2f ",lit[(Out/2)*Out+x]);
  std::printf("\n");
  if(gErr||gWarn) std::printf("  validation: %d error(s) %d warning(s)\n",gErr,gWarn);
  if(argc>2){ // dump a PGM so the shadow can be looked at
    FILE* pf=fopen(argv[2],"wb"); fprintf(pf,"P5\n%u %u\n255\n",Out,Out);
    for(uint32_t i=0;i<Out*Out;i++){unsigned char c=(unsigned char)(std::min(1.0f,std::max(0.0f,lit[i]))*255.0f);fwrite(&c,1,1,pf);}
    fclose(pf); std::printf("  wrote %s\n",argv[2]); }
  return 0;}
