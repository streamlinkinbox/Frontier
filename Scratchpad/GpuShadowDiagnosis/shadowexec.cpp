// Execute the real ShadowRaster pipeline: render a known occluder into a shadow map and read the depth back.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <fstream>
static int gErr=0,gWarn=0;
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCB(VkDebugUtilsMessageSeverityFlagBitsEXT s,
  VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* d,void*){
  if(s&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){gErr++;std::printf("    [VALIDATION-ERROR] %s\n",d->pMessage);}
  else if(s&VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT){gWarn++;std::printf("    [VALIDATION-WARN] %s\n",d->pMessage);}
  return VK_FALSE;}
static std::vector<uint32_t> Load(const char*p){std::ifstream f(p,std::ios::binary|std::ios::ate);
  if(!f)return{};size_t n=(size_t)f.tellg();f.seekg(0);std::vector<uint32_t>v(n/4);f.read((char*)v.data(),n);return v;}
static VkPhysicalDeviceMemoryProperties gMem;
static uint32_t MemType(uint32_t bits,VkMemoryPropertyFlags want){
  for(uint32_t i=0;i<gMem.memoryTypeCount;i++) if((bits&(1u<<i))&&((gMem.memoryTypes[i].propertyFlags&want)==want))return i;
  return ~0u;}
struct Buf{VkBuffer b=VK_NULL_HANDLE;VkDeviceMemory m=VK_NULL_HANDLE;void* map=nullptr;};
static Buf MakeBuf(VkDevice D,VkDeviceSize sz,VkBufferUsageFlags u){
  Buf r;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=sz;ci.usage=u;
  ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;vkCreateBuffer(D,&ci,nullptr,&r.b);
  VkMemoryRequirements mr;vkGetBufferMemoryRequirements(D,r.b,&mr);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=mr.size;
  ai.memoryTypeIndex=MemType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vkAllocateMemory(D,&ai,nullptr,&r.m);vkBindBufferMemory(D,r.b,r.m,0);vkMapMemory(D,r.m,0,sz,0,&r.map);return r;}
struct M4{float m[16];};
// Column-major, matching the engine's Matrix4x4 convention as uploaded.
// BuildLightClip transcribed verbatim from Engine/DeviceExchange/VisibilityExchange.cpp:1288 —
// the harness must test the ENGINE's matrix, not a hand-rolled one.
static M4 BuildLightClip(const float Tap[3],const float Centre[3],float HalfAngleDegrees,float Near,float Far){
  constexpr float kPi=3.14159265358979323846f;
  float Dx=Centre[0]-Tap[0],Dy=Centre[1]-Tap[1],Dz=Centre[2]-Tap[2];
  const float Dl=std::sqrt(Dx*Dx+Dy*Dy+Dz*Dz);
  if(Dl<=0.0f){Dx=0;Dy=0;Dz=-1;} else {Dx/=Dl;Dy/=Dl;Dz/=Dl;}
  float Ux=0,Uy=0,Uz=1;
  if(Dx*Dx+Dy*Dy<0.01f){Ux=0;Uy=1;Uz=0;}
  float Rx=Dy*Uz-Dz*Uy,Ry=Dz*Ux-Dx*Uz,Rz=Dx*Uy-Dy*Ux;
  const float Rl=std::sqrt(Rx*Rx+Ry*Ry+Rz*Rz);
  if(Rl>0){Rx/=Rl;Ry/=Rl;Rz/=Rl;}
  const float Vx=Ry*Dz-Rz*Dy,Vy=Rz*Dx-Rx*Dz,Vz=Rx*Dy-Ry*Dx;
  const float F=1.0f/std::tan(HalfAngleDegrees*kPi/180.0f);
  const float Rg=Far/(Far-Near);
  const float R0[4]={F*Rx,F*Ry,F*Rz,-F*(Rx*Tap[0]+Ry*Tap[1]+Rz*Tap[2])};
  const float R1[4]={-F*Vx,-F*Vy,-F*Vz,F*(Vx*Tap[0]+Vy*Tap[1]+Vz*Tap[2])};
  const float R2[4]={Rg*Dx,Rg*Dy,Rg*Dz,-Rg*(Dx*Tap[0]+Dy*Tap[1]+Dz*Tap[2])-Rg*Near};
  const float R3[4]={Dx,Dy,Dz,-(Dx*Tap[0]+Dy*Tap[1]+Dz*Tap[2])};
  M4 M{};
  for(int C=0;C<4;C++){M.m[C*4+0]=R0[C];M.m[C*4+1]=R1[C];M.m[C*4+2]=R2[C];M.m[C*4+3]=R3[C];}
  return M;}

int main(){
  std::printf("\n=== Executing the real ShadowRaster pipeline ===\n\n");
  VkApplicationInfo A{VK_STRUCTURE_TYPE_APPLICATION_INFO};A.apiVersion=VK_API_VERSION_1_2;
  const char* lay[]={"VK_LAYER_KHRONOS_validation"};const char* ext[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
  uint32_t lc=0;vkEnumerateInstanceLayerProperties(&lc,nullptr);std::vector<VkLayerProperties> lp(lc);
  vkEnumerateInstanceLayerProperties(&lc,lp.data());bool hv=false;
  for(auto&l:lp)if(!std::strcmp(l.layerName,lay[0]))hv=true;
  std::printf("validation layer: %s\n\n",hv?"ACTIVE":"absent");
  VkInstanceCreateInfo IC{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};IC.pApplicationInfo=&A;
  if(hv){IC.enabledLayerCount=1;IC.ppEnabledLayerNames=lay;IC.enabledExtensionCount=1;IC.ppEnabledExtensionNames=ext;}
  VkInstance I;vkCreateInstance(&IC,nullptr,&I);
  if(hv){auto mk=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(I,"vkCreateDebugUtilsMessengerEXT");
    if(mk){VkDebugUtilsMessengerCreateInfoEXT M{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      M.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
      M.messageType=0x7;M.pfnUserCallback=DebugCB;VkDebugUtilsMessengerEXT h;mk(I,&M,nullptr,&h);}}
  uint32_t n=1;VkPhysicalDevice P;vkEnumeratePhysicalDevices(I,&n,&P);
  vkGetPhysicalDeviceMemoryProperties(P,&gMem);
  float q=1;VkDeviceQueueCreateInfo QC{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  QC.queueCount=1;QC.pQueuePriorities=&q;QC.queueFamilyIndex=0;
  VkDeviceCreateInfo DC{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};DC.queueCreateInfoCount=1;DC.pQueueCreateInfos=&QC;
  VkDevice D;vkCreateDevice(P,&DC,nullptr,&D);VkQueue Q;vkGetDeviceQueue(D,0,0,&Q);
  const uint32_t Side=256;
  // depth image
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=VK_IMAGE_TYPE_2D;
  ii.format=VK_FORMAT_D32_SFLOAT;ii.extent={Side,Side,1};ii.mipLevels=1;ii.arrayLayers=1;
  ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;
  ii.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ii.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;VkImage img;vkCreateImage(D,&ii,nullptr,&img);
  VkMemoryRequirements mr;vkGetImageMemoryRequirements(D,img,&mr);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=mr.size;
  ai.memoryTypeIndex=MemType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory imem;vkAllocateMemory(D,&ai,nullptr,&imem);vkBindImageMemory(D,img,imem,0);
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=img;
  vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=VK_FORMAT_D32_SFLOAT;
  vi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};VkImageView iview;vkCreateImageView(D,&vi,nullptr,&iview);
  // render pass
  VkAttachmentDescription ad{};ad.format=VK_FORMAT_D32_SFLOAT;ad.samples=VK_SAMPLE_COUNT_1_BIT;
  ad.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;ad.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
  ad.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;ad.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
  ad.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;ad.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference ar{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sd{};sd.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sd.pDepthStencilAttachment=&ar;
  VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rpi.attachmentCount=1;rpi.pAttachments=&ad;
  rpi.subpassCount=1;rpi.pSubpasses=&sd;VkRenderPass rp;vkCreateRenderPass(D,&rpi,nullptr,&rp);
  VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fbi.renderPass=rp;
  fbi.attachmentCount=1;fbi.pAttachments=&iview;fbi.width=Side;fbi.height=Side;fbi.layers=1;
  VkFramebuffer fb;vkCreateFramebuffer(D,&fbi,nullptr,&fb);
  // scene buffers: one quad occluder at y=1, one instance, one cluster
  Buf vb=MakeBuf(D,64*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  Buf ib=MakeBuf(D,6*4,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  Buf inb=MakeBuf(D,160,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  Buf cb=MakeBuf(D,48,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  Buf ub=MakeBuf(D,480,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  float* V=(float*)vb.map;std::memset(V,0,64*4);
  const float qy=1.0f, h=0.5f;
  float pos[4][3]={{-h,qy,-h},{h,qy,-h},{h,qy,h},{-h,qy,h}};
  for(int i=0;i<4;i++){V[i*16+0]=pos[i][0];V[i*16+1]=pos[i][1];V[i*16+2]=pos[i][2];V[i*16+3]=1;}
  uint32_t* Idx=(uint32_t*)ib.map;uint32_t idx[6]={0,1,2,0,2,3};std::memcpy(Idx,idx,24);
  float* In=(float*)inb.map;std::memset(In,0,160);
  In[0]=1;In[5]=1;In[10]=1;In[15]=1;In[16]=1;In[21]=1;In[26]=1;In[31]=1;
  uint32_t* Iu=(uint32_t*)(In+32);Iu[0]=0;Iu[1]=0;Iu[2]=2;Iu[3]=0;Iu[4]=0;Iu[5]=1;Iu[6]=0;Iu[7]=0;
  float* C=(float*)cb.map;std::memset(C,0,48);C[3]=100.f;C[7]=1.0f;
  uint32_t* Cu=(uint32_t*)(C+8);Cu[0]=0;Cu[1]=0;Cu[2]=2;Cu[3]=0;
  // constants: light above, looking down at the quad
  std::memset(ub.map,0,480);float* U=(float*)ub.map;
  const float LightPos[3]={0,3,0},AimAt[3]={0,0,0};
  M4 clip=BuildLightClip(LightPos,AimAt,35.0f,0.1f,10.0f);
  std::memcpy(U,clip.m,64);
  U[64+0]=0;U[64+1]=3;U[64+2]=0;U[64+3]=0.5f;
  U[112+0]=256;U[112+1]=0.1f;U[112+2]=10.f;U[112+3]=0.005f;
  uint32_t* Uc=(uint32_t*)(U+116);Uc[0]=2;Uc[1]=3;Uc[2]=1;Uc[3]=0;
  // pipeline
  auto vcode=Load("/tmp/spv/ShadowRaster.vert.slang.spv");
  auto fcode=Load("/tmp/spv/ShadowRaster.frag.slang.spv");
  VkShaderModuleCreateInfo smv{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smv.codeSize=vcode.size()*4;smv.pCode=vcode.data();
  VkShaderModuleCreateInfo smf{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smf.codeSize=fcode.size()*4;smf.pCode=fcode.data();
  VkShaderModule vm,fm;vkCreateShaderModule(D,&smv,nullptr,&vm);vkCreateShaderModule(D,&smf,nullptr,&fm);
  VkDescriptorSetLayoutBinding b0[]={
    {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},
    {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},
    {3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}};
  VkDescriptorSetLayoutBinding b1[]={
    {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
  VkDescriptorSetLayoutCreateInfo l0{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};l0.bindingCount=3;l0.pBindings=b0;
  VkDescriptorSetLayoutCreateInfo l1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};l1.bindingCount=1;l1.pBindings=b1;
  VkDescriptorSetLayout dsl[2];std::printf("  dsl0 rc=%d\n",vkCreateDescriptorSetLayout(D,&l0,nullptr,&dsl[0]));std::printf("  dsl1 rc=%d\n",vkCreateDescriptorSetLayout(D,&l1,nullptr,&dsl[1]));
  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT,0,4};
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pli.setLayoutCount=2;pli.pSetLayouts=dsl;
  pli.pushConstantRangeCount=1;pli.pPushConstantRanges=&pcr;VkPipelineLayout pl;vkCreatePipelineLayout(D,&pli,nullptr,&pl);
  VkPipelineShaderStageCreateInfo st[2]{};
  st[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;st[0].stage=VK_SHADER_STAGE_VERTEX_BIT;st[0].module=vm;st[0].pName="main";
  st[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;st[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;st[1].module=fm;st[1].pName="main";
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
  gpi.layout=pl;gpi.renderPass=rp;VkPipeline gp;
  if(vkCreateGraphicsPipelines(D,VK_NULL_HANDLE,1,&gpi,nullptr,&gp)!=VK_SUCCESS){std::printf("pipeline FAILED\n");return 1;}
  // descriptors
  VkDescriptorPoolSize ps[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
  VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpi.maxSets=2;dpi.poolSizeCount=2;dpi.pPoolSizes=ps;
  VkDescriptorPool dp;std::printf("  createDescriptorPool rc=%d\n",vkCreateDescriptorPool(D,&dpi,nullptr,&dp));
  VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};dai.descriptorPool=dp;dai.descriptorSetCount=2;dai.pSetLayouts=dsl;
  VkDescriptorSet ds[2];std::printf("  allocateDescriptorSets rc=%d\n",vkAllocateDescriptorSets(D,&dai,ds));
  VkDescriptorBufferInfo bi1{inb.b,0,VK_WHOLE_SIZE},bi2{cb.b,0,VK_WHOLE_SIZE},bi3{vb.b,0,VK_WHOLE_SIZE},bi4{ub.b,0,VK_WHOLE_SIZE};
  VkWriteDescriptorSet w[4];
  std::memset(w,0,sizeof(w));
  for(int i=0;i<4;i++){w[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[i].descriptorCount=1;w[i].dstArrayElement=0;}
  w[0].dstSet=ds[0];w[0].dstBinding=1;w[0].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[0].pBufferInfo=&bi1;
  w[1].dstSet=ds[0];w[1].dstBinding=2;w[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[1].pBufferInfo=&bi2;
  w[2].dstSet=ds[0];w[2].dstBinding=3;w[2].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[2].pBufferInfo=&bi3;
  w[3].dstSet=ds[1];w[3].dstBinding=0;w[3].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w[3].pBufferInfo=&bi4;
  vkUpdateDescriptorSets(D,4,w,0,nullptr);
  // record + submit
  VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpi.queueFamilyIndex=0;
  VkCommandPool cp;vkCreateCommandPool(D,&cpi,nullptr,&cp);
  VkCommandBufferAllocateInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cbi.commandPool=cp;
  cbi.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cbi.commandBufferCount=1;VkCommandBuffer cmd;vkAllocateCommandBuffers(D,&cbi,&cmd);
  Buf rb=MakeBuf(D,(VkDeviceSize)Side*Side*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  VkCommandBufferBeginInfo bg{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkBeginCommandBuffer(cmd,&bg);
  VkClearValue cv{};cv.depthStencil={1.0f,0};
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rbi.renderPass=rp;rbi.framebuffer=fb;
  rbi.renderArea=sc;rbi.clearValueCount=1;rbi.pClearValues=&cv;
  vkCmdBeginRenderPass(cmd,&rbi,VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,gp);
  vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,0,1,&ds[0],0,nullptr);
  vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,1,1,&ds[1],0,nullptr);
  uint32_t tap=0;vkCmdPushConstants(cmd,pl,VK_SHADER_STAGE_VERTEX_BIT,0,4,&tap);
  vkCmdBindIndexBuffer(cmd,ib.b,0,VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd,6,1,0,0,0);
  vkCmdEndRenderPass(cmd);
  VkBufferImageCopy bic{};bic.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};bic.imageExtent={Side,Side,1};
  vkCmdCopyImageToBuffer(cmd,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rb.b,1,&bic);
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;
  vkQueueSubmit(Q,1,&si,VK_NULL_HANDLE);vkQueueWaitIdle(Q);
  // inspect
  float* dep=(float*)rb.map;uint32_t written=0;float mn=1e9,mx=-1e9;double sum=0;
  for(uint32_t i=0;i<Side*Side;i++){float d=dep[i];if(d<1.0f){written++;if(d<mn)mn=d;if(d>mx)mx=d;sum+=d;}}
  std::printf("  first 4 depths: %.6f %.6f %.6f %.6f\n",dep[0],dep[1],dep[Side*Side/2],dep[Side*Side-1]);
  std::printf("shadow map %ux%u\n",Side,Side);
  std::printf("  texels written by the occluder : %u / %u (%.1f%%)\n",written,Side*Side,100.0*written/(Side*Side));
  if(written){std::printf("  depth range                   : %.6f .. %.6f (mean %.6f)\n",mn,mx,sum/written);}
  else std::printf("  depth range                   : NOTHING RASTERISED\n");
  float expect=0; {float nz=0.1f,fz=10.f,dist=2.0f;expect=(fz/(fz-nz)*dist-(fz*nz)/(fz-nz))/dist;}
  std::printf("  expected depth for a quad 2 m from the light: %.6f\n",expect);
  std::printf("\n=== validation: %d error(s), %d warning(s) ===\n\n",gErr,gWarn);
  return 0;}
