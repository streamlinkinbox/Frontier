// Exercises the SAME presentation helper as DisplayPresent, without scene/inset resources.
export async function CloudPresentProbe(Shaders,E,Frame){
 const Source='CloudPresentationProbe.wgsl';Shaders.Sources.set(Source,`//! include "FrameDescriptor.wgsl"
 @group(0) @binding(5) var FlareSampler:sampler;
 //! include "CloudPresent.wgsl"
 @vertex fn VS(@builtin(vertex_index) I:u32)->@builtin(position) vec4f{return vec4f(vec2f(f32((I<<1u)&2u),f32(I&2u))*2.0-1.0,0,1);}
 @fragment fn FS(@builtin(position) P:vec4f)->@location(0) vec4f{return vec4f(AddClouds(P.xy,vec3f(1)),1);}`);
 const D=E.Device,Layout=D.createBindGroupLayout({entries:[{binding:0,visibility:GPUShaderStage.FRAGMENT,buffer:{type:'uniform'}},{binding:5,visibility:GPUShaderStage.FRAGMENT,sampler:{type:'filtering'}},...[6,7,9].map(binding=>({binding,visibility:GPUShaderStage.FRAGMENT,texture:{sampleType:'float'}})),{binding:8,visibility:GPUShaderStage.FRAGMENT,texture:{sampleType:'depth'}},{binding:10,visibility:GPUShaderStage.FRAGMENT,buffer:{type:'uniform'}}]});
 const Program=await Shaders.ConstructRasterProgram(Source,{VertexEntry:'VS',FragmentEntry:'FS',Targets:[{format:'rgba16float'}]},[Layout]);
 const Sampler=D.createSampler({magFilter:'linear',minFilter:'linear'});
 return {Record(En,C,Depth,Mask,Output,Timestamp){const Group=D.createBindGroup({layout:Layout,entries:[{binding:0,resource:{buffer:Frame}},{binding:5,resource:Sampler},{binding:6,resource:C.ColourView},{binding:7,resource:C.GuideView},{binding:8,resource:Depth},{binding:9,resource:Mask},{binding:10,resource:{buffer:C.Params}}]});const P=En.beginRenderPass({label:'Cloud reconstruction probe',timestampWrites:Timestamp,colorAttachments:[{view:Output,loadOp:'clear',storeOp:'store',clearValue:[0,0,0,1]}]});P.setPipeline(Program);P.setBindGroup(0,Group);P.draw(3);P.end();}};
}
