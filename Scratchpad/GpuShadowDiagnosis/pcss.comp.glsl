#version 460
layout(local_size_x = 16, local_size_y = 16) in;
#include "Shaders/ShadowSample.slang"
layout(set = 0, binding = 0, r32f) uniform writeonly image2D LitImage;
layout(push_constant) uniform Push { vec2 PlaneOrigin; float PlaneScale; float PlaneY; };
void main(){
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz = imageSize(LitImage);
    if (px.x >= sz.x || px.y >= sz.y) return;
    // A flat receiver plane under the light; sample the real ShadowLitFraction across it.
    vec2 uv = (vec2(px) + 0.5) / vec2(sz);
    vec3 world = vec3(PlaneOrigin.x + (uv.x - 0.5) * PlaneScale,
                      PlaneY,
                      PlaneOrigin.y + (uv.y - 0.5) * PlaneScale);
    vec3 nrm = vec3(0.0, 1.0, 0.0);
    float lit = ShadowLitFraction(world, nrm, 1.0, 0u);
    imageStore(LitImage, px, vec4(lit, 0, 0, 1));
}
