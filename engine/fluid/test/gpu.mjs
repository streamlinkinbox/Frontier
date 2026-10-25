// Node WebGPU bootstrap for tests (Dawn via the `webgpu` package).
import { create, globals } from 'webgpu';

const keepAlive = [];
export async function getDevice({ verbose = true } = {}) {
  Object.assign(globalThis, globals);
  const gpu = create([]);
  const adapter = await gpu.requestAdapter();
  // dawn.node: the GPU instance and adapter must outlive the device, otherwise the GC can free the
  // native instance while the device is in use (use-after-free -> mutex/futex aborts).
  keepAlive.push(gpu, adapter);
  if (!adapter) throw new Error('No WebGPU adapter (run tests through test/launch.mjs)');
  // default limits on purpose: the fluid module must run on any WebGPU device
  const device = await adapter.requestDevice();
  const errors = [];
  device.addEventListener('uncapturederror', (e) => { errors.push(e.error.message); if (verbose) console.error('[webgpu error]', e.error.message); });
  device.errors = errors;
  keepAlive.push(device);
  if (verbose) console.log('[gpu]', adapter.info.vendor, adapter.info.architecture, adapter.info.description);
  return { device, adapter, gpu };
}

export async function flush(device) { await device.queue.onSubmittedWorkDone(); }
