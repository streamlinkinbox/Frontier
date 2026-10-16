/**
 * Asynchronous GPU -> CPU readback ring.
 *
 * WebGPU has no blocking readback: a buffer can only be read after the queue has finished
 * the copy and `mapAsync` resolves. A small ring of staging buffers lets the host keep
 * submitting frames while earlier results are in flight. A slot that is still mapped is
 * skipped rather than waited on, so the render loop never stalls for telemetry.
 */
export class ReadbackRing {
  /**
   * @param {GPUDevice} device
   * @param {number} size bytes per slot
   * @param {number} slots ring length (3 covers the usual 2-frame GPU queue depth)
   */
  constructor(device, size, slots = 3) {
    this.device = device;
    this.size = size;
    this.slots = [];
    for (let i = 0; i < slots; i++) {
      this.slots.push({
        buffer: device.createBuffer({
          size,
          usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
          label: `readback-${i}`,
        }),
        state: 'free', // free -> copied -> mapping -> ready -> free
        stamp: null,
        submittedAt: 0,
      });
    }
    this.completed = [];
    this.errors = 0;
  }

  /** Reserve a free slot for a copy in the current frame, or -1 if all are in flight. */
  acquire(stamp) {
    const k = this.slots.findIndex((s) => s.state === 'free');
    if (k < 0) return -1;
    this.slots[k].state = 'copied';
    this.slots[k].stamp = stamp;
    this.slots[k].submittedAt = performance.now();
    return k;
  }

  /** Call after queue.submit for every slot that received a copy this frame. */
  kick() {
    for (const slot of this.slots) {
      if (slot.state !== 'copied') continue;
      slot.state = 'mapping';
      slot.buffer
        .mapAsync(GPUMapMode.READ)
        .then(() => {
          slot.state = 'ready';
        })
        .catch(() => {
          // device loss or unmapped buffer: drop the slot and carry on
          this.errors++;
          slot.state = 'free';
        });
    }
  }

  /** Collect every slot that finished mapping since the last call. */
  poll() {
    const out = [];
    for (const slot of this.slots) {
      if (slot.state !== 'ready') continue;
      const data = slot.buffer.getMappedRange().slice(0);
      slot.buffer.unmap();
      out.push({ data, stamp: slot.stamp, latencyMs: performance.now() - slot.submittedAt });
      slot.state = 'free';
    }
    return out;
  }

  inFlight() {
    return this.slots.filter((s) => s.state !== 'free').length;
  }
}
