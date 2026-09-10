// Execute the real shadow pipelines on a software Vulkan device, with validation layers on.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>

static int gErrors=0, gWarnings=0;
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCB(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* d, void*)
{
    const char* tag = (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "VALIDATION-ERROR" : "VALIDATION-WARN";
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) gErrors++;
    else if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) gWarnings++;
    else return VK_FALSE;
    std::printf("    [%s] %s\n", tag, d->pMessage ? d->pMessage : "");
    return VK_FALSE;
}
static std::vector<uint32_t> Load(const char* p){
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if(!f){ std::printf("    cannot open %s\n",p); return {}; }
    size_t n=(size_t)f.tellg(); f.seekg(0);
    std::vector<uint32_t> v(n/4); f.read((char*)v.data(),n); return v;
}
// The C++ side, copied verbatim from VisibilityExchange.cpp:1329.
struct ShadowConstantRecord {
    float LightClip[4][16]; float TapOrigin[4][4]; float TapRadiance[4][4];
    float TapNormal[4][4]; float Geometry[4]; uint32_t Control[4];
};

int main(){
    std::printf("\n=== Frontier GPU shadow pipeline diagnosis (SwiftShader) ===\n\n");

    // ---- layout check against the shader's std140 expectation
    std::printf("[1] std140 shadow constant block\n");
    std::printf("    sizeof(ShadowConstantRecord) = %zu B\n", sizeof(ShadowConstantRecord));
    size_t expect = 4*64 + 3*4*16 + 32;
    std::printf("    shader block expects        = %zu B  -> %s\n", expect,
        sizeof(ShadowConstantRecord)==expect?"MATCH":"MISMATCH");
    std::printf("    offsets: LightClip %zu  TapOrigin %zu  TapRadiance %zu  TapNormal %zu  Geometry %zu  Control %zu\n",
        offsetof(ShadowConstantRecord,LightClip), offsetof(ShadowConstantRecord,TapOrigin),
        offsetof(ShadowConstantRecord,TapRadiance), offsetof(ShadowConstantRecord,TapNormal),
        offsetof(ShadowConstantRecord,Geometry), offsetof(ShadowConstantRecord,Control));

    // ---- instance with validation
    VkApplicationInfo A{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    A.pApplicationName="shadowdiag"; A.apiVersion=VK_API_VERSION_1_2;
    const char* layers[]={"VK_LAYER_KHRONOS_validation"};
    const char* exts[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    uint32_t lc=0; vkEnumerateInstanceLayerProperties(&lc,nullptr);
    std::vector<VkLayerProperties> lp(lc); vkEnumerateInstanceLayerProperties(&lc,lp.data());
    bool haveVal=false; for(auto&l:lp) if(!std::strcmp(l.layerName,layers[0])) haveVal=true;
    std::printf("\n[2] instance\n    validation layer available: %s\n", haveVal?"yes":"NO (running without)");
    VkInstanceCreateInfo IC{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; IC.pApplicationInfo=&A;
    if(haveVal){ IC.enabledLayerCount=1; IC.ppEnabledLayerNames=layers;
                 IC.enabledExtensionCount=1; IC.ppEnabledExtensionNames=exts; }
    VkInstance I;
    if(vkCreateInstance(&IC,nullptr,&I)!=VK_SUCCESS){ std::printf("    instance FAILED\n"); return 1; }
    VkDebugUtilsMessengerEXT msg=VK_NULL_HANDLE;
    if(haveVal){
        auto mk=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(I,"vkCreateDebugUtilsMessengerEXT");
        if(mk){ VkDebugUtilsMessengerCreateInfoEXT M{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            M.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            M.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            M.pfnUserCallback=DebugCB; mk(I,&M,nullptr,&msg); }
    }
    uint32_t n=1; VkPhysicalDevice P; vkEnumeratePhysicalDevices(I,&n,&P);
    VkPhysicalDeviceProperties Pp; vkGetPhysicalDeviceProperties(P,&Pp);
    std::printf("    device: %s\n", Pp.deviceName);
    std::printf("    maxPerStageDescriptorStorageImages=%u  maxBoundDescriptorSets=%u\n",
        Pp.limits.maxPerStageDescriptorStorageImages, Pp.limits.maxBoundDescriptorSets);

    float q=1.f; VkDeviceQueueCreateInfo QC{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    QC.queueCount=1; QC.pQueuePriorities=&q; QC.queueFamilyIndex=0;
    VkDeviceCreateInfo DC{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; DC.queueCreateInfoCount=1; DC.pQueueCreateInfos=&QC;
    VkDevice D; if(vkCreateDevice(P,&DC,nullptr,&D)!=VK_SUCCESS){ std::printf("    device FAILED\n"); return 1; }

    // ---- shader modules
    std::printf("\n[3] shader modules from the repo's own SPIR-V\n");
    struct S { const char* path; const char* name; VkShaderStageFlagBits stage; VkShaderModule mod; };
    S mods[]={
        {"/tmp/spv/ShadowResolve.slang.spv","ShadowResolve",VK_SHADER_STAGE_COMPUTE_BIT,VK_NULL_HANDLE},
        {"/tmp/spv/ShadowRaster.vert.slang.spv","ShadowRaster.vert",VK_SHADER_STAGE_VERTEX_BIT,VK_NULL_HANDLE},
        {"/tmp/spv/ShadowRaster.frag.slang.spv","ShadowRaster.frag",VK_SHADER_STAGE_FRAGMENT_BIT,VK_NULL_HANDLE},
    };
    for(auto& s:mods){
        auto code=Load(s.path); if(code.empty()){ std::printf("    %-20s NO SPIR-V\n",s.name); continue; }
        VkShaderModuleCreateInfo MC{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        MC.codeSize=code.size()*4; MC.pCode=code.data();
        VkResult r=vkCreateShaderModule(D,&MC,nullptr,&s.mod);
        std::printf("    %-20s %s (%zu words)\n", s.name, r==VK_SUCCESS?"module OK":"MODULE FAILED", code.size());
    }

    // ---- descriptor set layouts matching the shader declarations
    std::printf("\n[4] descriptor layouts + compute pipeline (ShadowResolve)\n");
    VkDescriptorSetLayoutBinding set0[]={
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {10,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {11,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {12,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
    };
    VkDescriptorSetLayoutBinding set1[]={
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
    };
    VkDescriptorSetLayoutCreateInfo L0{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    L0.bindingCount=5; L0.pBindings=set0;
    VkDescriptorSetLayoutCreateInfo L1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    L1.bindingCount=2; L1.pBindings=set1;
    VkDescriptorSetLayout dsl[2];
    vkCreateDescriptorSetLayout(D,&L0,nullptr,&dsl[0]);
    vkCreateDescriptorSetLayout(D,&L1,nullptr,&dsl[1]);
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,16};
    VkPipelineLayoutCreateInfo PL{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    PL.setLayoutCount=2; PL.pSetLayouts=dsl; PL.pushConstantRangeCount=1; PL.pPushConstantRanges=&pcr;
    VkPipelineLayout lay;
    std::printf("    pipeline layout: %s\n", vkCreatePipelineLayout(D,&PL,nullptr,&lay)==VK_SUCCESS?"OK":"FAILED");

    VkPipeline computePipe=VK_NULL_HANDLE;
    if(mods[0].mod){
        VkComputePipelineCreateInfo CP{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        CP.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        CP.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; CP.stage.module=mods[0].mod; CP.stage.pName="main";
        CP.layout=lay;
        VkResult r=vkCreateComputePipelines(D,VK_NULL_HANDLE,1,&CP,nullptr,&computePipe);
        std::printf("    ShadowResolve compute pipeline: %s\n", r==VK_SUCCESS?"CREATED":"FAILED");
    }

    // ---- graphics pipeline for the shadow map raster (depth-only)
    std::printf("\n[5] graphics pipeline (ShadowRaster, depth-only)\n");
    VkAttachmentDescription depth{};
    depth.format=VK_FORMAT_D32_SFLOAT; depth.samples=VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; depth.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; depth.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference dref{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.pDepthStencilAttachment=&dref;
    VkRenderPassCreateInfo RP{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    RP.attachmentCount=1; RP.pAttachments=&depth; RP.subpassCount=1; RP.pSubpasses=&sub;
    VkRenderPass rp;
    std::printf("    render pass: %s\n", vkCreateRenderPass(D,&RP,nullptr,&rp)==VK_SUCCESS?"OK":"FAILED");

    if(mods[1].mod && mods[2].mod){
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage=VK_SHADER_STAGE_VERTEX_BIT; st[0].module=mods[1].mod; st[0].pName="main";
        st[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module=mods[2].mod; st[1].pName="main";
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{0,0,512,512,0,1}; VkRect2D sc{{0,0},{512,512}};
        VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vs.viewportCount=1; vs.pViewports=&vp; vs.scissorCount=1; vs.pScissors=&sc;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE;
        rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth=1.f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable=VK_TRUE; ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkGraphicsPipelineCreateInfo GP{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        GP.stageCount=2; GP.pStages=st; GP.pVertexInputState=&vi; GP.pInputAssemblyState=&ia;
        GP.pViewportState=&vs; GP.pRasterizationState=&rs; GP.pMultisampleState=&ms;
        GP.pDepthStencilState=&ds; GP.pColorBlendState=&cb; GP.layout=lay; GP.renderPass=rp; GP.subpass=0;
        VkPipeline gpipe;
        VkResult r=vkCreateGraphicsPipelines(D,VK_NULL_HANDLE,1,&GP,nullptr,&gpipe);
        std::printf("    ShadowRaster graphics pipeline: %s\n", r==VK_SUCCESS?"CREATED":"FAILED");
    }

    std::printf("\n=== validation summary: %d error(s), %d warning(s) ===\n\n", gErrors, gWarnings);
    return gErrors?1:0;
}
