// Heightfield pipeline worker: keeps the editor responsive while erosion runs.
import { generateTerrain } from './pipeline.js';

self.onmessage = (event) => {
  const { id, params } = event.data;
  try {
    const result = generateTerrain(params, (p) => self.postMessage({ id, type: 'progress', ...p }));
    const transfer = [result.height.buffer, result.hardness.buffer, result.deposit.buffer, result.flow.buffer, result.cavity.buffer, result.slope.buffer, result.river.buffer, result.waterLevel.buffer, result.lake.buffer, result.outcrop.buffer];
    self.postMessage({ id, type: 'done', result }, transfer);
  } catch (error) {
    self.postMessage({ id, type: 'error', message: String(error && error.stack || error) });
  }
};
