/**
 * Generation runs off the main thread so the UI stays responsive while a
 * 300k-face oak is being built.
 */

import { TreeParams } from '../tree/params';
import { generateTree } from '../tree/generate';
import { toOBJ, toGLB } from '../tree/export';
import { LeafMesh } from '../tree/mesh';
import { sampleStem, stemRadiusAt } from '../tree/skeleton';
import type { ObstacleKind } from '../env/environment';

export interface GenerateRequest {
  type: 'generate';
  id: number;
  params: TreeParams;
}
export interface ExportRequest {
  type: 'export';
  id: number;
  params: TreeParams;
  format: 'obj' | 'glb';
  includeLeaves: boolean;
}
export type WorkerRequest = GenerateRequest | ExportRequest;

export interface GenerateResponse {
  type: 'generated';
  id: number;
  buffers: {
    position: Float32Array;
    normal: Float32Array;
    uv: Float32Array;
    wind: Float32Array;
    pivot: Float32Array;
    level: Float32Array;
    junction: Float32Array;
    index: Uint32Array;
    edgeIndex: Uint32Array;
  };
  leaves: {
    positions: Float32Array;
    normals: Float32Array;
    uvs: Float32Array;
    wind: Float32Array;
    pivots: Float32Array;
    indices: Uint32Array;
    count: number;
  };
  obstacles: { kind: ObstacleKind; positions: Float32Array; normals: Float32Array; indices: Uint32Array }[];
  /** How far the plant continues below the ground (metres). */
  groundDepth: number;
  /** Grass mesher statistics (grasses only). */
  grass: import('../plant/grassMesher').GrassStats | null;
  report: ReturnType<typeof generateTree>['report'];
  stats: ReturnType<typeof generateTree>['stats'];
  timings: ReturnType<typeof generateTree>['timings'];
  summary: ReturnType<typeof generateTree>['summary'];
  /** A few junction locations (for inspection tooling). */
  samples: { level: number; kind: string; pos: [number, number, number]; dir: [number, number, number]; parentDir: [number, number, number]; radius: number; parentRadius: number }[];
}
export interface ExportResponse {
  type: 'exported';
  id: number;
  format: 'obj' | 'glb';
  data: ArrayBuffer;
}
export interface ErrorResponse {
  type: 'error';
  id: number;
  message: string;
}
export type WorkerResponse = GenerateResponse | ExportResponse | ErrorResponse;

const ctx = self as unknown as Worker;

ctx.onmessage = (ev: MessageEvent<WorkerRequest>) => {
  const msg = ev.data;
  try {
    if (msg.type === 'generate') {
      const r = generateTree(msg.params);
      const leaves = packLeaves(r.leaves);
      const samples: GenerateResponse['samples'] = [];
      for (const st of r.skeleton?.stems ?? []) {
        if (st.dropped || !st.parent || samples.length > 400) continue;
        if (st.attach !== 'side' && st.attach !== 'fork') continue;
        const n = st.nodes[0];
        const ps = sampleStem(st.parent, st.attach === 'side' ? st.attachS : st.parent.length);
        const pr = stemRadiusAt(st.parent, st.attach === 'side' ? st.attachS : st.parent.length, msg.params.mesh, msg.params.botany);
        samples.push({
          level: st.level,
          kind: st.attach,
          pos: [n.pos.x, n.pos.y, n.pos.z],
          dir: [n.dir.x, n.dir.y, n.dir.z],
          parentDir: [ps.dir.x, ps.dir.y, ps.dir.z],
          radius: st.logicalRadius,
          parentRadius: pr,
        });
      }
      const res: GenerateResponse = {
        type: 'generated',
        id: msg.id,
        buffers: r.buffers,
        leaves,
        obstacles: r.obstacles,
        groundDepth: r.groundDepth,
        grass: r.grass ?? null,
        report: r.report,
        stats: r.stats,
        timings: r.timings,
        summary: r.summary,
        samples,
      };
      const transfer: ArrayBuffer[] = [
        r.buffers.position.buffer,
        r.buffers.normal.buffer,
        r.buffers.uv.buffer,
        r.buffers.wind.buffer,
        r.buffers.pivot.buffer,
        r.buffers.level.buffer,
        r.buffers.junction.buffer,
        r.buffers.index.buffer,
        r.buffers.edgeIndex.buffer,
        leaves.positions.buffer,
        leaves.normals.buffer,
        leaves.uvs.buffer,
        leaves.wind.buffer,
        leaves.pivots.buffer,
        leaves.indices.buffer,
      ] as ArrayBuffer[];
      for (const o of r.obstacles) transfer.push(o.positions.buffer as ArrayBuffer, o.normals.buffer as ArrayBuffer, o.indices.buffer as ArrayBuffer);
      ctx.postMessage(res, transfer);
    } else if (msg.type === 'export') {
      const r = generateTree(msg.params, { validate: false, obstacleMeshes: false });
      const leaves = msg.includeLeaves ? r.leaves : null;
      let data: ArrayBuffer;
      if (msg.format === 'obj') {
        const enc = new TextEncoder();
        data = enc.encode(toOBJ(r.mesh, leaves, msg.params.name.replace(/\s+/g, '_'))).buffer as ArrayBuffer;
      } else {
        data = toGLB(r.mesh, leaves, msg.params.name);
      }
      const res: ExportResponse = { type: 'exported', id: msg.id, format: msg.format, data };
      ctx.postMessage(res, [data]);
    }
  } catch (e) {
    const err: ErrorResponse = { type: 'error', id: msg.id, message: e instanceof Error ? e.message : String(e) };
    ctx.postMessage(err);
  }
};

function packLeaves(l: LeafMesh): GenerateResponse['leaves'] {
  return {
    positions: new Float32Array(l.positions),
    normals: new Float32Array(l.normals),
    uvs: new Float32Array(l.uvs),
    wind: new Float32Array(l.wind),
    pivots: new Float32Array(l.pivots),
    indices: new Uint32Array(l.indices),
    count: l.count,
  };
}
