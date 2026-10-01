import { MAX_EMITTERS } from './fluid-solver.js?v=smooth-lod-20261001';

const PARAM_FLOATS = 24 + MAX_EMITTERS * 12;
const PARAM_BYTES = PARAM_FLOATS * Float32Array.BYTES_PER_ELEMENT;
const MANIFEST_URL = './assets/baked-plume.json?v=baked-plume-cage-20261001';

function assertManifest(manifest) {
  const [nx, ny, nz] = manifest.dimensions || [];
  if (!Number.isInteger(nx) || !Number.isInteger(ny) || !Number.isInteger(nz)) {
    throw new Error('The baked plume manifest has invalid dimensions.');
  }
  if (manifest.format !== 'rgba32float') {
    throw new Error(`Unsupported baked plume format: ${manifest.format || 'unknown'}.`);
  }
  if (!Array.isArray(manifest.origin) || manifest.origin.length !== 3 || !Number.isFinite(manifest.cellSize)) {
    throw new Error('The baked plume manifest has invalid world bounds.');
  }
  if (!Number.isInteger(manifest.frames) || manifest.frames < 1 || !Number.isFinite(manifest.duration)) {
    throw new Error('The baked plume manifest has invalid frame timing.');
  }
  return { nx, ny, nz };
}

function makeUniformData(manifest, dimensions) {
  const [nx, ny, nz] = dimensions;
  const floats = new Float32Array(PARAM_FLOATS);
  const ints = new Int32Array(floats.buffer);
  ints.set([nx, ny, nz, 1], 0);
  floats.set([...manifest.origin, manifest.cellSize], 4);
  floats.set([1 / 60, 0, 0, 0], 8);
  floats.set([0, 0, 0, 0], 12);
  floats.set([0, 0, 0, 1.0], 16);
  floats.set([1, 0, 0, 0], 20);

  // The source record is metadata for the renderer's contact glow only. It is
  // not an emitter and is never consumed by a compute pass in baked mode.
  floats.set([0, 0.25, 0, 0], 24);
  floats.set([1.15, 1.0, 9999, 1], 28);
  floats.set([0, 0, 8.0, 1.0], 32);
  return floats;
}

function frameAtTime(time, frameCount, duration) {
  const wrapped = ((time % duration) + duration) % duration;
  return Math.min(frameCount - 1, Math.floor(wrapped / (duration / frameCount)));
}

/**
 * Load an offline-baked gas field. The browser only fetches bytes, uploads
 * read-only storage buffers, and lets the render shader sample them. There is
 * deliberately no solver, timestep, or compute dispatch in this module.
 */
export async function loadBakedPlume(device, manifestUrl = MANIFEST_URL) {
  const manifestResponse = await fetch(manifestUrl, { cache: 'force-cache' });
  if (!manifestResponse.ok) throw new Error(`Could not read baked plume manifest (${manifestResponse.status}).`);
  const manifest = await manifestResponse.json();
  const dimensions = assertManifest(manifest);
  const cellsPerFrame = dimensions[0] * dimensions[1] * dimensions[2];
  const bytesPerFrame = cellsPerFrame * 16;
  const expectedBytes = bytesPerFrame * manifest.frames;
  const dataUrl = new URL(manifest.data, new URL(manifestUrl, document.baseURI));
  const dataResponse = await fetch(dataUrl, { cache: 'force-cache' });
  if (!dataResponse.ok) throw new Error(`Could not read baked plume volume (${dataResponse.status}).`);
  const data = await dataResponse.arrayBuffer();
  if (data.byteLength !== expectedBytes) {
    throw new Error(`Baked plume volume is ${data.byteLength} bytes; expected ${expectedBytes}.`);
  }

  const uniformData = makeUniformData(manifest, dimensions);
  const uniformBuffer = device.createBuffer({
    size: PARAM_BYTES,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    label: 'baked-plume-read-only-parameters',
  });
  device.queue.writeBuffer(uniformBuffer, 0, uniformData);

  const gasBuffers = [];
  const frameBytes = new Uint8Array(data);
  for (let frame = 0; frame < manifest.frames; frame++) {
    const gasBuffer = device.createBuffer({
      size: bytesPerFrame,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
      label: `baked-plume-frame-${String(frame + 1).padStart(2, '0')}`,
    });
    device.queue.writeBuffer(gasBuffer, 0, frameBytes.subarray(frame * bytesPerFrame, (frame + 1) * bytesPerFrame));
    gasBuffers.push(gasBuffer);
  }

  const grid = {
    nx: dimensions[0],
    ny: dimensions[1],
    nz: dimensions[2],
    cellSize: manifest.cellSize,
    origin: [...manifest.origin],
    cells: cellsPerFrame,
    width: dimensions[0] * manifest.cellSize,
    height: dimensions[1] * manifest.cellSize,
    depth: dimensions[2] * manifest.cellSize,
    maxSources: 1,
  };
  const resourceBytes = bytesPerFrame * gasBuffers.length + PARAM_BYTES;
  const baked = {
    kind: 'baked',
    id: manifest.id,
    label: manifest.label,
    mode: manifest.mode,
    manifest,
    grid,
    bounds: {
      min: [...manifest.bounds.min],
      max: [...manifest.bounds.max],
    },
    contentBounds: manifest.contentBounds
      ? { min: [...manifest.contentBounds.min], max: [...manifest.contentBounds.max] }
      : null,
    frameCount: manifest.frames,
    duration: manifest.duration,
    uniformBuffer,
    gasBuffers,
    resourceBytes,
    frameIndex: -1,
    getFrame(time) {
      const nextIndex = frameAtTime(time, this.frameCount, this.duration);
      if (nextIndex === this.frameIndex) return this.frame;
      this.frameIndex = nextIndex;
      this.frame = {
        kind: 'baked-frame',
        label: `${this.label} · frame ${nextIndex + 1}/${this.frameCount}`,
        grid: this.grid,
        bounds: this.bounds,
        contentBounds: this.contentBounds,
        uniformBuffer: this.uniformBuffer,
        gasBuffer: this.gasBuffers[nextIndex],
        resourceBytes: this.resourceBytes,
        frameIndex: nextIndex,
        frameCount: this.frameCount,
      };
      return this.frame;
    },
    destroy() {
      for (const buffer of this.gasBuffers) buffer.destroy();
      this.uniformBuffer.destroy();
    },
  };
  baked.getFrame(0);
  return baked;
}

export { MANIFEST_URL };
