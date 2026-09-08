import common from "./shaders/common.wgsl?raw";
import compute from "./shaders/compute.wgsl?raw";
import render from "./shaders/render.wgsl?raw";
import materials from "./shaders/materials.wgsl?raw";
export const computeShader = common + "\n" + compute;
export const renderShader =
  common +
  "\n" +
  materials +
  "\n" +
  render +
  `
@vertex fn vertexMain(@builtin(vertex_index) id: u32) -> @builtin(position) vec4f {
  let x = f32((id << 1u) & 2u); let y = f32(id & 2u);
  return vec4f(x*2.-1.,y*2.-1.,0.,1.);
}
@fragment fn fragmentMain(@builtin(position) position: vec4f) -> @location(0) vec4f {
  return renderPixel(position.xy);
}`;

// The raymarcher has one source of truth. Its explicitly typed WGSL subset is
// mechanically emitted as GLSL ES for the clearly labeled WebGL2 fallback.
function toGLSL(source: string): string {
  const type = (s: string) =>
    s
      .replace(/\bvec([234])f\b/g, "vec$1")
      .replace(/\bvec([234])i\b/g, "ivec$1")
      .replace(/\bvec([234])u\b/g, "uvec$1")
      .replace(/\bf32\b/g, "float")
      .replace(/\bi32\b/g, "int")
      .replace(/\bu32\b/g, "uint")
      .replace(/\binverseSqrt\b/g, "inversesqrt");
  let out = source.replace(
    /fn\s+(\w+)\s*\(([^)]*)\)\s*->\s*(\w+)\s*\{/g,
    (_, name, args, ret) =>
      `${ret} ${name}(${args
        .split(",")
        .filter(Boolean)
        .map((arg: string) => {
          const [n, t] = arg.split(":");
          return `${t.trim()} ${n.trim()}`;
        })
        .join(",")}) {`,
  );
  out = out.replace(
    /\b(let|var|const)\s+(\w+)\s*:\s*(\w+)\s*=/g,
    (_, kind, name, t) => `${kind === "const" ? "const " : ""}${t} ${name} =`,
  );
  out = out.replace(
    /textureSampleLevel\(field,linearSampler,([^;]+),0\.\)/g,
    "textureLod(field,$1,0.)",
  );
  out = out.replace(
    /textureLoad\(field,([^;]+),0\)/g,
    "texelFetch(field,$1,0)",
  );
  return type(out);
}
const glCommon = common.slice(common.indexOf("const WORLD_MIN"));
export const glFragment = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler3D;
layout(std140) uniform Params {
  vec4 eye;vec4 forward;vec4 right;vec4 up;vec4 viewport;vec4 brush;vec4 water;
  vec4 flags;vec4 dims;vec4 erosion;vec4 geology;vec4 sculpt;vec4 brushParams;vec4 pick;
  vec4 planeOrigin;vec4 planeNormal;vec4 strokeTangent;vec4 strokePrevious;vec4 processes;vec4 material;vec4 materialShape;vec4 materialOptics;vec4 baseColor;vec4 waterOptics;vec4 river;
  vec4 flow;vec4 flowArc[8];vec4 rockDetail;vec4 rockLayers;
} u;
uniform sampler3D field;
out vec4 fragColor;
float select(float a,float b,bool s){return s?b:a;}
vec3 select(vec3 a,vec3 b,bool s){return s?b:a;}
${toGLSL(glCommon + "\n" + materials + "\n" + render)}
void main(){fragColor=renderPixel(vec2(gl_FragCoord.x,u.viewport.y-gl_FragCoord.y));}
`;
export const glVertex = `#version 300 es
void main(){float x=float((gl_VertexID<<1)&2);float y=float(gl_VertexID&2);gl_Position=vec4(x*2.-1.,y*2.-1.,0.,1.);}`;

// Present the scaled raymarch target into a stable, display-sized canvas.
// Adaptive resolution must not reset the canvas bitmap between GPU frames.
export const presentShader = `
struct VertexOutput {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
};
@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(1) var imageSampler: sampler;
@vertex fn vertexMain(@builtin(vertex_index) id: u32) -> VertexOutput {
  let x = f32((id << 1u) & 2u); let y = f32(id & 2u);
  var output: VertexOutput;
  output.position = vec4f(x*2.-1.,y*2.-1.,0.,1.);
  output.uv = vec2f(x,1.-y);
  return output;
}
@fragment fn fragmentMain(input: VertexOutput) -> @location(0) vec4f {
  return textureSampleLevel(image,imageSampler,input.uv,0.);
}`;
