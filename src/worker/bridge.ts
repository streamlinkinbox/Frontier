// ---------------------------------------------------------------------------
// Frontier worker / main-thread bridge
// Owns the cook worker, serialises cook requests (latest wins), surfaces
// progress and delivers transferable results.
// ---------------------------------------------------------------------------

import { Graph } from '../graph/types';
import { CookResult } from './eval';
import CookWorker from './cookWorker?worker';

export interface CookCallbacks {
  onProgress: (frac: number, stage: string) => void;
  onDone: (result: CookResult) => void;
  onError: (message: string) => void;
}

export class Cooker {
  private worker: Worker | null = null;
  private jobId = 0;
  private pending: Graph | null = null;
  private busy = false;
  private cbs: CookCallbacks;

  constructor(cbs: CookCallbacks) {
    this.cbs = cbs;
    this.spawn();
  }

  private spawn() {
    this.worker = new CookWorker();
    this.worker.onmessage = (e: MessageEvent) => {
      const msg = e.data as { type: string; jobId: number; frac?: number; stage?: string; result?: CookResult; message?: string };
      if (msg.jobId !== this.jobId) return;
      if (msg.type === 'progress') this.cbs.onProgress(msg.frac ?? 0, msg.stage ?? '');
      else if (msg.type === 'done') {
        this.busy = false;
        this.cbs.onDone(msg.result!);
        this.flush();
      } else if (msg.type === 'error') {
        this.busy = false;
        this.cbs.onError(msg.message ?? 'cook failed');
        this.flush();
      } else if (msg.type === 'cancelled') {
        this.busy = false;
        this.flush();
      }
    };
    this.worker.onerror = (e) => {
      this.busy = false;
      this.cbs.onError(e.message ?? 'worker crashed');
      this.flush();
    };
  }

  private flush() {
    if (this.pending && !this.busy) {
      const g = this.pending;
      this.pending = null;
      this.dispatch(g);
    }
  }

  cook(graph: Graph): void {
    if (this.busy) {
      this.pending = graph;   // latest request wins
      return;
    }
    this.dispatch(graph);
  }

  private dispatch(graph: Graph) {
    this.busy = true;
    this.jobId++;
    this.worker?.postMessage({ type: 'cook', graph, jobId: this.jobId });
  }

  dispose() {
    this.worker?.terminate();
    this.worker = null;
  }
}
