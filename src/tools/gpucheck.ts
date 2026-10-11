/**
 * Headless WebGPU check.
 *
 * Runs the real renderer (same WGSL, same pipeline, same upload path as the
 * browser) against a software Vulkan device, renders every scene to a PNG in
 * tools/.cache and reports compile/validation errors.
 *
 *   npm run verify          # wraps this with the SwiftShader environment
 *   node tools/run-ts.mjs src/tools/gpucheck.ts
 */
import { mkdirSync } from 'node:fs';
import { create, globals } from 'webgpu';
import { buildScene } from '../core/build';
import { MeshBuilder, MAT_ORDER } from '../core/mesh';
import { RoadRenderer, type FrameUniforms } from '../gpu/renderer';
import { OrbitCamera, SCENES } from '../app/scenes';
import { writePngFromRgba } from './raster';

// the package exposes a property bag of classes plus a factory for the
// navigator-style GPU entry point (see node_modules/webgpu/README.md)
Object.assign(globalThis, globals as Record<string, unknown>);
Object.defineProperty(globalThis, 'navigator', { value: { gpu: create([]) }, configurable: true, writable: true });

const OUT = 'tools/.cache';
mkdirSync(OUT, { recursive: true });

const WIDTH = 1280;
const HEIGHT = 800;

function groundMesh(radius: number): MeshBuilder {
  const b = new MeshBuilder();
  const mat = MAT_ORDER.indexOf('soil');
  const n = 44;
  const step = (radius * 2) / n;
  const grid: number[][] = [];
  for (let j = 0; j <= n; j++) {
    const row: number[] = [];
    for (let i = 0; i <= n; i++) {
      const x = -radius + i * step;
      const z = -radius + j * step;
      const y = -0.42 + Math.sin(x * 0.011) * 0.18 + Math.cos(z * 0.014) * 0.18;
      row.push(b.addVertex({ p: [x, y, z], n: [0, 1, 0], uv: [x, z], uv2: [x, z], mat, tint: [1, 1, 1, 1] }));
    }
    grid.push(row);
  }
  for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) b.addQuad(grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]);
  return b;
}

async function main(): Promise<void> {
  const gpu = (globalThis as { navigator: { gpu: GPU } }).navigator.gpu;
  if (!gpu) throw new Error('navigator.gpu missing — is the SwiftShader environment set?');
  const adapter = await gpu.requestAdapter();
  if (!adapter) throw new Error('requestAdapter() returned null (no Vulkan ICD?)');
  const device = await adapter.requestDevice();
  const errors: string[] = [];
  device.addEventListener?.('uncapturederror', (e: Event) => {
    const ev = e as unknown as { error?: { message?: string } };
    errors.push(ev.error?.message ?? 'uncaptured GPU error');
  });

  const info = (adapter as unknown as { info?: { vendor?: string; architecture?: string; description?: string } }).info ?? {};
  console.log(`adapter: ${info.vendor ?? '?'} ${info.architecture ?? ''} ${info.description ?? ''}`.trim());
  const limits = device.limits;
  console.log(`limits: maxTexture2D ${limits.maxTextureDimension2D} maxBindGroups ${limits.maxBindGroups}`);

  const format: GPUTextureFormat = 'rgba8unorm';
  const renderer = await RoadRenderer.create(device, format, 'depth24plus');
  let failures = 0;

  for (const preset of SCENES) {
    const doc = preset.build();
    const t0 = Date.now();
    const result = buildScene(doc, 'high');
    const buildMs = Date.now() - t0;
    renderer.clearBatches();
    renderer.upload(result.builder);
    renderer.upload(groundMesh(1200));

    const target = device.createTexture({
      size: [WIDTH, HEIGHT],
      format,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    const camera = new OrbitCamera();
    // frame the network from a low three-quarter view
    let minX = Infinity;
    let maxX = -Infinity;
    let minZ = Infinity;
    let maxZ = -Infinity;
    for (const c of result.corridors) {
      for (const p of [c.path.positionAt(0), c.path.positionAt(c.length / 2), c.path.positionAt(c.length)]) {
        minX = Math.min(minX, p[0]);
        maxX = Math.max(maxX, p[0]);
        minZ = Math.min(minZ, p[2]);
        maxZ = Math.max(maxZ, p[2]);
      }
    }
    const radius = Number.isFinite(minX) ? Math.max(45, Math.hypot(maxX - minX, maxZ - minZ) / 2) : 120;
    camera.target = [(minX + maxX) / 2, 0, (minZ + maxZ) / 2];
    camera.distance = radius * 1.35;
    camera.pitch = 0.42;
    camera.yaw = 0.9;

    const uniforms: FrameUniforms = {
      viewProj: camera.viewProj(WIDTH / HEIGHT),
      cameraPos: camera.eye(),
      sunDir: [0.42, 0.78, 0.46, 1.35],
      skyTop: [0.1, 0.2, 0.5],
      skyBottom: [0.38, 0.5, 0.66],
      ground: [0.016, 0.015, 0.013],
      params: [0, 0, 0, 0],
      fog: [0.4, 0.52, 0.68, 1 / 1600],
    };
    const encoder = device.createCommandEncoder();
    const wire = renderer.uploadWire(result.builder, quadEdges(result.builder));
    renderer.render(encoder, target.createView(), WIDTH, HEIGHT, uniforms);
    if (preset.id === 'cross') renderer.renderWire(encoder, target.createView(), WIDTH, HEIGHT, wire);
    device.queue.submit([encoder.finish()]);

    const bytesPerRow = Math.ceil((WIDTH * 4) / 256) * 256;
    const readback = device.createBuffer({ size: bytesPerRow * HEIGHT, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const enc3 = device.createCommandEncoder();
    enc3.copyTextureToBuffer({ texture: target }, { buffer: readback, bytesPerRow, rowsPerImage: HEIGHT }, { width: WIDTH, height: HEIGHT });
    device.queue.submit([enc3.finish()]);
    await readback.mapAsync(GPUMapMode.READ);
    const raw = new Uint8Array(readback.getMappedRange().slice(0));
    readback.unmap();
    const tight = new Uint8Array(WIDTH * HEIGHT * 4);
    for (let y = 0; y < HEIGHT; y++) tight.set(raw.subarray(y * bytesPerRow, y * bytesPerRow + WIDTH * 4), y * WIDTH * 4);
    const path = `${OUT}/gpu-${preset.id}.png`;
    writePngFromRgba(path, WIDTH, HEIGHT, tight);

    // sanity: the frame must not be a single flat colour
    const seen = new Set<number>();
    for (let i = 0; i < tight.length; i += 4 * 977) seen.add((tight[i] << 16) | (tight[i + 1] << 8) | tight[i + 2]);
    const distinct = seen.size;
    const ok = distinct > 6 && result.builder.triCount > 0;
    if (!ok) failures++;
    console.log(
      `${preset.id.padEnd(12)} quads=${String(result.stats.quads).padStart(7)} tris=${String(result.stats.triangles).padStart(7)} junctions=${String(result.stats.junctions).padStart(2)} build=${String(buildMs).padStart(4)}ms distinctColours=${distinct} ${ok ? 'ok' : 'FLAT/EMPTY'} -> ${path}`,
    );
    target.destroy();
    readback.destroy();
  }

  if (errors.length) {
    failures++;
    console.error('GPU errors:');
    for (const e of errors) console.error('  ' + e);
  }
  if (failures) throw new Error(`${failures} render check(s) failed`);
  console.log('render check OK');
}

function quadEdges(builder: MeshBuilder): Uint32Array {
  const out: number[] = [];
  const q = builder.quads;
  for (let i = 0; i + 3 < q.length; i += 4) out.push(q[i], q[i + 1], q[i + 1], q[i + 2], q[i + 2], q[i + 3], q[i + 3], q[i]);
  return new Uint32Array(out);
}

main().catch((err) => {
  console.error(err instanceof Error ? err.message : err);
  throw err;
});
