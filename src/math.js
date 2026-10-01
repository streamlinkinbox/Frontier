const EPSILON = 1e-6;

export function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function normalize3(v) {
  const length = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / length, v[1] / length, v[2] / length];
}

function cross3(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

export function cameraFrame(camera) {
  const distance = camera.distance;
  const cosPitch = Math.cos(camera.pitch);
  const eye = [
    camera.target[0] + Math.sin(camera.yaw) * cosPitch * distance,
    camera.target[1] + Math.sin(camera.pitch) * distance,
    camera.target[2] + Math.cos(camera.yaw) * cosPitch * distance,
  ];
  const forward = normalize3([
    camera.target[0] - eye[0],
    camera.target[1] - eye[1],
    camera.target[2] - eye[2],
  ]);
  const right = normalize3(cross3(forward, [0, 1, 0]));
  const up = normalize3(cross3(right, forward));
  return { eye, forward, right, up };
}

export function cameraUniformData(camera, width, height, time, lod) {
  const { eye, forward, right, up } = cameraFrame(camera);
  const data = new Float32Array(24);
  data.set([eye[0], eye[1], eye[2], 1], 0);
  data.set([forward[0], forward[1], forward[2], 0], 4);
  data.set([right[0], right[1], right[2], 0], 8);
  data.set([up[0], up[1], up[2], 0], 12);
  data.set([width, height, time, Math.tan(camera.fov * 0.5)], 16);
  data.set([lod.step, lod.maxSamples, lod.level, lod.censorPixels ?? 0], 20);
  return data;
}

export function rayFromScreen(camera, x, y, width, height) {
  const { eye, forward, right, up } = cameraFrame(camera);
  const aspect = width / Math.max(height, 1);
  const tangent = Math.tan(camera.fov * 0.5);
  const ndcX = (x / Math.max(width, 1)) * 2 - 1;
  const ndcY = 1 - (y / Math.max(height, 1)) * 2;
  return {
    eye,
    direction: normalize3([
      forward[0] + right[0] * ndcX * aspect * tangent + up[0] * ndcY * tangent,
      forward[1] + right[1] * ndcX * aspect * tangent + up[1] * ndcY * tangent,
      forward[2] + right[2] * ndcX * aspect * tangent + up[2] * ndcY * tangent,
    ]),
  };
}

export function intersectGround(ray) {
  if (Math.abs(ray.direction[1]) < EPSILON) return null;
  const t = -ray.eye[1] / ray.direction[1];
  if (t <= 0) return null;
  return [
    ray.eye[0] + ray.direction[0] * t,
    0,
    ray.eye[2] + ray.direction[2] * t,
  ];
}
