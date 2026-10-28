// Native LFRender optical image. Cached independently of live cabin/depth visibility.
//! include "FrameDescriptor.wgsl"
//! include "LensFlare.wgsl"
@group(0) @binding(1) var ImageTarget:texture_storage_2d<rgba16float,write>;
@compute @workgroup_size(8,8)
fn IntegrateFlareImage(@builtin(global_invocation_id) ID:vec3u){
 let Size=textureDimensions(ImageTarget);if(any(ID.xy>=Size)){return;}
 let Pixel=(vec2f(ID.xy)+0.5)/vec2f(Size)*Frame.ScreenExtent;
 textureStore(ImageTarget,vec2i(ID.xy),vec4f(ResolveLensFlare(Pixel),1.0));
}
