// Minimal forward renderer for the demo: HDR scene target + linear-depth
// target (so the water shader can measure true absorption path length),
// refraction copy, half-res planar-reflection target, ACES tonemap blit.
// Pipelines for sky live here; terrain/props/water have their own modules.

import skyWGSL from './shaders/sky.wgsl?raw';
import tonemapWGSL from './shaders/tonemap.wgsl?raw';
import frameWGSL from './shaders/frame.wgsl?raw';
import type { Mat4, Vec3 } from './math';

export interface FrameData {
  viewProj: Mat4;
  view: Mat4;
  proj: Mat4;
  invProj: Mat4;
  camPos: Vec3;
  time: number;
  sunDir: Vec3;
  sunI: number;
  sunColor: Vec3;
  clipLevel: number;
  clipEnabled: boolean;
  fogColor: Vec3;
  fogDensity: number;
  waterLevel: number;
  screenSize: [number, number];
}

export const COLOR_FORMAT: GPUTextureFormat = 'rgba16float';
export const DEPTH_FORMAT: GPUTextureFormat = 'depth24plus';
export function loadRenderWGSL(src: string): string {
  return src.includes('//@@FRAME@@') ? src.replace('//@@FRAME@@', frameWGSL) : src;
}
export function makeRenderModule(device: GPUDevice, src: string, label: string): GPUShaderModule {
  const mod = device.createShaderModule({ code: loadRenderWGSL(src), label });
  mod.getCompilationInfo().then((info) => {
    for (const m of info.messages) {
      if (m.type === 'error') console.error(`[render] shader ${label} (${m.lineNum}:${m.linePos}) ${m.message}`);
    }
  }).catch(() => undefined);
  return mod;
}

const FRAME_BYTES = 336;

export class Renderer {
  readonly device: GPUDevice;
  readonly canvas: HTMLCanvasElement;
  readonly context: GPUCanvasContext;
  readonly canvasFormat: GPUTextureFormat;
  width = 1;
  height = 1;

  sceneColor!: GPUTexture;
  linDepth!: GPUTexture;
  depth!: GPUTexture;
  refractTex!: GPUTexture;
  reflColor!: GPUTexture;
  reflLinDepth!: GPUTexture;
  reflDepth!: GPUTexture;
  reflWidth = 1;
  reflHeight = 1;

  readonly frameLayout: GPUBindGroupLayout;
  readonly frameStride: number;
  private frameBuf: GPUBuffer;
  readonly frameBindGroup: GPUBindGroup;
  private skyPipeline: GPURenderPipeline;
  private tonemapPipeline: GPURenderPipeline;
  private tonemapBG: GPUBindGroup | null = null;

  constructor(device: GPUDevice, canvas: HTMLCanvasElement) {
    this.device = device;
    this.canvas = canvas;
    const ctx = canvas.getContext('webgpu');
    if (!ctx) throw new Error('Could not get WebGPU canvas context');
    this.context = ctx;
    this.canvasFormat = navigator.gpu.getPreferredCanvasFormat();
    ctx.configure({ device, format: this.canvasFormat, alphaMode: 'opaque' });

    this.frameLayout = device.createBindGroupLayout({
      label: 'frame',
      entries: [{
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: FRAME_BYTES },
      }],
    });
    const align = device.limits.minUniformBufferOffsetAlignment;
    this.frameStride = Math.ceil(FRAME_BYTES / align) * align;
    this.frameBuf = device.createBuffer({
      size: this.frameStride * 2,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      label: 'frame-ubo',
    });
    this.frameBindGroup = device.createBindGroup({
      layout: this.frameLayout,
      entries: [{ binding: 0, resource: { buffer: this.frameBuf, size: FRAME_BYTES } }],
    });

    const skyMod = makeRenderModule(device, skyWGSL, 'sky');
    this.skyPipeline = device.createRenderPipeline({
      label: 'sky',
      layout: device.createPipelineLayout({ bindGroupLayouts: [this.frameLayout] }),
      vertex: { module: skyMod, entryPoint: 'vsMain' },
      fragment: {
        module: skyMod, entryPoint: 'fsMain',
        targets: [{ format: COLOR_FORMAT }, { format: COLOR_FORMAT }],
      },
      primitive: { topology: 'triangle-list' },
      depthStencil: { format: DEPTH_FORMAT, depthWriteEnabled: false, depthCompare: 'always' },
    });

    const tmMod = makeRenderModule(device, tonemapWGSL, 'tonemap');
    this.tonemapPipeline = device.createRenderPipeline({
      label: 'tonemap',
      layout: 'auto',
      vertex: { module: tmMod, entryPoint: 'vsMain' },
      fragment: { module: tmMod, entryPoint: 'fsMain', targets: [{ format: this.canvasFormat }] },
      primitive: { topology: 'triangle-list' },
    });
  }

  get frameOffsetMain(): number { return 0; }
  get frameOffsetRefl(): number { return this.frameStride; }

  resize(width: number, height: number): void {
    width = Math.max(1, Math.floor(width));
    height = Math.max(1, Math.floor(height));
    if (width === this.width && height === this.height && this.sceneColor) return;
    this.width = width;
    this.height = height;
    this.canvas.width = width;
    this.canvas.height = height;
    const d = this.device;
    for (const t of [this.sceneColor, this.linDepth, this.depth, this.refractTex, this.reflColor, this.reflLinDepth, this.reflDepth]) {
      t?.destroy();
    }
    const colorUsage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC;
    this.sceneColor = d.createTexture({ size: [width, height], format: COLOR_FORMAT, usage: colorUsage, label: 'sceneColor' });
    this.linDepth = d.createTexture({ size: [width, height], format: COLOR_FORMAT, usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING, label: 'linDepth' });
    this.depth = d.createTexture({ size: [width, height], format: DEPTH_FORMAT, usage: GPUTextureUsage.RENDER_ATTACHMENT, label: 'depth' });
    this.refractTex = d.createTexture({ size: [width, height], format: COLOR_FORMAT, usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST, label: 'refraction' });
    this.reflWidth = Math.max(1, width >> 1);
    this.reflHeight = Math.max(1, height >> 1);
    this.reflColor = d.createTexture({ size: [this.reflWidth, this.reflHeight], format: COLOR_FORMAT, usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING, label: 'reflColor' });
    this.reflLinDepth = d.createTexture({ size: [this.reflWidth, this.reflHeight], format: COLOR_FORMAT, usage: GPUTextureUsage.RENDER_ATTACHMENT, label: 'reflLinDepth' });
    this.reflDepth = d.createTexture({ size: [this.reflWidth, this.reflHeight], format: DEPTH_FORMAT, usage: GPUTextureUsage.RENDER_ATTACHMENT, label: 'reflDepth' });
    this.tonemapBG = null;
  }

  writeFrame(slot: 0 | 1, f: FrameData): void {
    const a = new Float32Array(FRAME_BYTES / 4);
    a.set(f.viewProj, 0);
    a.set(f.view, 16);
    a.set(f.proj, 32);
    a.set(f.invProj, 48);
    a.set(f.camPos, 64);
    a[67] = f.time;
    a.set(f.sunDir, 68);
    a[71] = f.sunI;
    a.set(f.sunColor, 72);
    a[75] = f.clipLevel;
    a.set(f.fogColor, 76);
    a[79] = f.clipEnabled ? 1 : 0;
    a.set(f.screenSize, 80);
    a[82] = f.fogDensity;
    a[83] = f.waterLevel;
    this.device.queue.writeBuffer(this.frameBuf, slot * this.frameStride, a);
  }

  /** Opaque-scene render pass descriptor (caller adds draws; call drawSky first inside). */
  opaquePassDesc(clear = true): GPURenderPassDescriptor {
    return {
      label: 'opaque',
      colorAttachments: [
        { view: this.sceneColor.createView(), loadOp: clear ? 'clear' : 'load', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } },
        { view: this.linDepth.createView(), loadOp: clear ? 'clear' : 'load', storeOp: 'store', clearValue: { r: 900, g: 0, b: 0, a: 0 } },
      ],
      depthStencilAttachment: {
        view: this.depth.createView(),
        depthLoadOp: clear ? 'clear' : 'load', depthStoreOp: 'store', depthClearValue: 1,
      },
    };
  }

  reflectionPassDesc(): GPURenderPassDescriptor {
    return {
      label: 'reflection',
      colorAttachments: [
        { view: this.reflColor.createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } },
        { view: this.reflLinDepth.createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 900, g: 0, b: 0, a: 0 } },
      ],
      depthStencilAttachment: {
        view: this.reflDepth.createView(),
        depthLoadOp: 'clear', depthStoreOp: 'store', depthClearValue: 1,
      },
    };
  }

  /** Transparent (water + particles) pass: loads existing color/depth. */
  transparentPassDesc(): GPURenderPassDescriptor {
    return {
      label: 'transparent',
      colorAttachments: [{ view: this.sceneColor.createView(), loadOp: 'load', storeOp: 'store' }],
      depthStencilAttachment: { view: this.depth.createView(), depthLoadOp: 'load', depthStoreOp: 'store' },
    };
  }

  drawSky(pass: GPURenderPassEncoder, frameOffset: number): void {
    pass.setPipeline(this.skyPipeline);
    pass.setBindGroup(0, this.frameBindGroup, [frameOffset]);
    pass.draw(3);
  }

  copyForRefraction(encoder: GPUCommandEncoder): void {
    encoder.copyTextureToTexture({ texture: this.sceneColor }, { texture: this.refractTex }, [this.width, this.height]);
  }

  present(encoder: GPUCommandEncoder): void {
    if (!this.tonemapBG) {
      this.tonemapBG = this.device.createBindGroup({
        layout: this.tonemapPipeline.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: this.sceneColor.createView() }],
      });
    }
    const pass = encoder.beginRenderPass({
      label: 'present',
      colorAttachments: [{
        view: this.context.getCurrentTexture().createView(),
        loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 },
      }],
    });
    pass.setPipeline(this.tonemapPipeline);
    pass.setBindGroup(0, this.tonemapBG);
    pass.draw(3);
    pass.end();
  }
}
