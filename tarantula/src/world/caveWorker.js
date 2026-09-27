import { generateCave } from './caveGen.js';

self.onmessage = (e) => {
  const { seed, voxel } = e.data;
  const res = generateCave(seed, voxel, (p) => self.postMessage({ type: 'progress', p }));
  self.postMessage({ type: 'done', ...res }, [res.positions.buffer, res.normals.buffer, res.colors.buffer, res.index.buffer]);
};
