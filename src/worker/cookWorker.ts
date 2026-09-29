// ---------------------------------------------------------------------------
// Frontier worker / cook worker entry
// ---------------------------------------------------------------------------

import { cookGraph, CookResult } from './eval';
import { Graph } from '../graph/types';

let cancelled = false;

self.onmessage = (e: MessageEvent) => {
  const msg = e.data as { type: string; graph?: Graph; jobId?: number };
  if (msg.type === 'cancel') {
    cancelled = true;
    return;
  }
  if (msg.type !== 'cook' || !msg.graph) return;
  cancelled = false;
  const jobId = msg.jobId ?? 0;
  const graph = msg.graph;

  const post = (f: number, stage: string) => {
    (self as unknown as Worker).postMessage({ type: 'progress', jobId, frac: f, stage });
  };

  try {
    const result: CookResult | null = cookGraph(graph, post, () => cancelled);
    if (cancelled || !result) {
      (self as unknown as Worker).postMessage({ type: 'cancelled', jobId });
      return;
    }
    const transfer: Transferable[] = [];
    if (result.mesh) {
      transfer.push(result.mesh.positions.buffer, result.mesh.normals.buffer, result.mesh.uvs.buffer, result.mesh.indices.buffer);
    }
    if (result.splat) for (const t of result.splat.textures) transfer.push(t.buffer);
    (self as unknown as Worker).postMessage({ type: 'done', jobId, result }, transfer);
  } catch (err) {
    (self as unknown as Worker).postMessage({ type: 'error', jobId, message: (err as Error)?.message ?? String(err), stack: (err as Error)?.stack });
  }
};
