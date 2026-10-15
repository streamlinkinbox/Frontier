import { buildWorldDistanceBvh, buildWorldGeometry, scaleWorldEmitterGeometry } from './scene.mjs';

self.addEventListener('message', event => {
  const { requestId, centerX, centerZ, emitterSize = 1.35 } = event.data;
  try {
    const world = buildWorldGeometry(centerX, centerZ);
    scaleWorldEmitterGeometry(world, emitterSize);
    const worldDistanceBvh = buildWorldDistanceBvh(world.vertices);
    self.postMessage(
      { requestId, world, worldDistanceBvh },
      [
        world.vertices.buffer,
        world.surfaceCards.buffer,
        worldDistanceBvh.triangleBuffer,
        worldDistanceBvh.nodeBuffer,
      ],
    );
  } catch (error) {
    self.postMessage({ requestId, error: error instanceof Error ? error.message : String(error) });
  }
});
