import { CFG, GW, GH, tidx } from './config.js';
import { mulberry32 } from './util.js';

// ---------------------------------------------------------------------------
// Maze: generated over a cell grid (MAZE_W x MAZE_H). The tile grid is
// (2W+1) x (2H+1): odd tiles are tunnel floors (cells), even tiles are rock.
// Extra loop openings criss-cross the perfect maze so there are multiple
// racing lines to choose from.
// ---------------------------------------------------------------------------
export function generateMaze(seed) {
  const rng = mulberry32(seed);
  const W = CFG.MAZE_W, H = CFG.MAZE_H;
  const open = new Uint8Array(GW * GH);
  const visited = new Uint8Array(W * H);

  // iterative randomized DFS (recursive backtracker without the recursion)
  const stack = [[0, 0]];
  visited[0] = 1;
  open[tidx(1, 1)] = 1;
  while (stack.length) {
    const [cx, cz] = stack[stack.length - 1];
    const opts = [];
    if (cx > 0     && !visited[cx - 1 + cz * W]) opts.push([-1, 0]);
    if (cx < W - 1 && !visited[cx + 1 + cz * W]) opts.push([1, 0]);
    if (cz > 0     && !visited[cx + (cz - 1) * W]) opts.push([0, -1]);
    if (cz < H - 1 && !visited[cx + (cz + 1) * W]) opts.push([0, 1]);
    if (!opts.length) { stack.pop(); continue; }
    const [dx, dz] = opts[(rng() * opts.length) | 0];
    const nx = cx + dx, nz = cz + dz;
    open[tidx(2 * cx + 1 + dx, 2 * cz + 1 + dz)] = 1; // knock the wall
    open[tidx(2 * nx + 1, 2 * nz + 1)] = 1;           // open the new cell
    visited[nx + nz * W] = 1;
    stack.push([nx, nz]);
  }

  // punch extra loops -> criss-crossing, non-tree maze (multiple routes)
  for (let cx = 0; cx < W; cx++) {
    for (let cz = 0; cz < H; cz++) {
      if (cx < W - 1 && rng() < CFG.LOOP_CHANCE) open[tidx(2 * cx + 2, 2 * cz + 1)] = 1;
      if (cz < H - 1 && rng() < CFG.LOOP_CHANCE) open[tidx(2 * cx + 1, 2 * cz + 2)] = 1;
    }
  }

  const isOpen = (x, z) => x >= 0 && z >= 0 && x < GW && z < GH && open[tidx(x, z)] === 1;
  const maze = { open, isOpen, W, H, rng, seed };
  return maze;
}

// BFS shortest path across cells (movement allowed only through open walls)
export function bfsPath(maze, from, to) {
  const { W, H, isOpen } = maze;
  const prev = new Int32Array(W * H).fill(-1);
  const start = from[0] + from[1] * W;
  prev[start] = start;
  const q = [from];
  for (let head = 0; head < q.length; head++) {
    const [cx, cz] = q[head];
    if (cx === to[0] && cz === to[1]) break;
    for (const [dx, dz] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const nx = cx + dx, nz = cz + dz;
      if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
      if (prev[nx + nz * W] !== -1) continue;
      if (!isOpen(2 * cx + 1 + dx, 2 * cz + 1 + dz)) continue;
      prev[nx + nz * W] = cx + cz * W;
      q.push([nx, nz]);
    }
  }
  const end = to[0] + to[1] * W;
  if (prev[end] === -1) return [];
  const path = [];
  let cur = end;
  while (cur !== start) { path.push([cur % W, (cur / W) | 0]); cur = prev[cur]; }
  path.push(from);
  return path.reverse();
}

// ---------------------------------------------------------------------------
// Rail lines: long BFS corridors that thread the maze. Mine carts shuttle
// back and forth along them like traffic. Lines avoid the start/finish cells
// and try not to overlap each other.
// ---------------------------------------------------------------------------
export function pickRailLines(maze, count, excludeCells) {
  const { W, H, isOpen, rng } = maze;
  const key = (x, z) => x + ',' + z;
  const excluded = new Set(excludeCells.map(([x, z]) => key(x, z)));
  const usedTiles = new Set();
  const lines = [];

  for (let li = 0; li < count; li++) {
    let minLen = 12, maxOverlap = 0.3, chosen = null;
    for (let att = 0; att < 300 && !chosen; att++) {
      if (att === 120) { minLen = 9; maxOverlap = 0.55; }
      if (att === 240) { maxOverlap = 1.01; }
      const sc = [(rng() * W) | 0, (rng() * H) | 0];
      if (excluded.has(key(sc[0], sc[1]))) continue;

      // randomized BFS tree from sc
      const dist = new Int16Array(W * H).fill(-1);
      const prev = new Int32Array(W * H).fill(-1);
      dist[sc[0] + sc[1] * W] = 0;
      const q = [sc];
      for (let head = 0; head < q.length; head++) {
        const [cx, cz] = q[head];
        const dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
        for (let k = dirs.length - 1; k > 0; k--) {
          const j = (rng() * (k + 1)) | 0;
          [dirs[k], dirs[j]] = [dirs[j], dirs[k]];
        }
        for (const [dx, dz] of dirs) {
          const nx = cx + dx, nz = cz + dz;
          if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
          if (dist[nx + nz * W] !== -1) continue;
          if (!isOpen(2 * cx + 1 + dx, 2 * cz + 1 + dz)) continue;
          dist[nx + nz * W] = dist[cx + cz * W] + 1;
          prev[nx + nz * W] = cx + cz * W;
          q.push([nx, nz]);
        }
      }
      // farthest reachable cell
      let far = sc, fd = 0;
      for (let c = 0; c < W * H; c++) {
        if (dist[c] > fd && !excluded.has(key(c % W, (c / W) | 0))) { fd = dist[c]; far = [c % W, (c / W) | 0]; }
      }
      if (fd < minLen || far === sc) continue;

      // reconstruct cell path
      const cells = [];
      let cur = far[0] + far[1] * W;
      while (cur !== sc[0] + sc[1] * W) { cells.unshift([cur % W, (cur / W) | 0]); cur = prev[cur]; }
      cells.unshift(sc);

      // tile keys for overlap bookkeeping (cell tiles + passage tiles)
      const tiles = [];
      for (let k = 0; k < cells.length; k++) {
        const [a, b] = cells[k];
        tiles.push(key(2 * a + 1, 2 * b + 1));
        if (k > 0) { const [pa, pb] = cells[k - 1]; tiles.push(key(pa + a + 1, pb + b + 1)); }
      }
      const overlap = tiles.filter((t) => usedTiles.has(t)).length / tiles.length;
      if (overlap > maxOverlap) continue;
      chosen = { cells, tiles };
    }
    if (chosen) {
      chosen.tiles.forEach((t) => usedTiles.add(t));
      lines.push(chosen.cells);
    }
  }
  return lines;
}
