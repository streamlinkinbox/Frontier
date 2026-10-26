// Meshes the SDF cliff chunks off the main thread. Results stream back in batches.
import { buildChunkGeometry, makeChunkContext } from './sdf-chunks.js';

self.onmessage = (event) => {
  const { id, jobs, meta, v } = event.data;
  try {
    const ctx = makeChunkContext(v);
    let batch = [], transfer = [];
    const flush = (done) => {
      self.postMessage({ id, type: 'batch', chunks: batch, done }, transfer);
      batch = []; transfer = [];
    };
    for (let n = 0; n < jobs.length; n++) {
      const job = jobs[n];
      const g = buildChunkGeometry(job, meta, v, ctx);
      g.ci = job.ci; g.cj = job.cj;
      batch.push(g);
      transfer.push(g.positions.buffer, g.normals.buffer, g.aux.buffer, g.aux2.buffer, g.index.buffer);
      if (batch.length >= 24 || n === jobs.length - 1) flush(n === jobs.length - 1);
    }
    if (jobs.length === 0) self.postMessage({ id, type: 'batch', chunks: [], done: true });
  } catch (error) {
    self.postMessage({ id, type: 'error', message: String((error && error.stack) || error) });
  }
};
