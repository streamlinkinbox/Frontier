import { extractMesh, buildGLB } from "./mesh";
self.onmessage = (event) => {
  try {
    const mesh = extractMesh(event.data.data, event.data.size, (progress) =>
      self.postMessage({ progress }),
    );
    const buffer = buildGLB(mesh);
    self.postMessage(
      { buffer, triangles: mesh.indices.length / 3 },
      { transfer: [buffer] },
    );
  } catch (error) {
    self.postMessage({
      error: error instanceof Error ? error.message : String(error),
    });
  }
};
