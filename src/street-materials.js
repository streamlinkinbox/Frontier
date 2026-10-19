import * as THREE from "three";
const cache = new Map();
export const STREET_COLORS = {
  shoulder: 0xb7a17d,
  brickcourse: 0xad7160,
  broomconcrete: 0xbec2be,
  granite: 0xb8b6ac,
  curbstone: 0xc6c5ba,
  paving: 0xc3c5c1,
  brick: 0xb8917e,
  gutter: 0xbfc2bd,
  joint: 0x686e66,
  drainmetal: 0x414c4c,
  drainvoid: 0x202a28,
  ironcover: 0x716258,
  tactile_yellow: 0xd5b042,
  tactile_stone: 0xa8ada3,
  tactile_red: 0xb68477,
};
export function surfaceTexture(kind) {
  if (cache.has(kind)) return cache.get(kind);
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = 512;
  const ctx = canvas.getContext("2d"),
    data = ctx.createImageData(512, 512);
  let seed = 42;
  const random = () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 4294967296;
  };
  for (let i = 0; i < data.data.length; i += 4) {
    const base = kind === "asphalt" ? 150 : 224,
      noise =
        (random() - 0.5) *
        (kind === "granite" ? 65 : kind === "asphalt" ? 95 : 24);
    for (let k = 0; k < 3; k++) data.data[i + k] = base + noise;
    data.data[i + 3] = 255;
  }
  ctx.putImageData(data, 0, 0);
  if (kind === "paving") {
    ctx.strokeStyle = "#9c9f98";
    ctx.lineWidth = 2;
    for (let y = 0; y < 512; y += 256) {
      ctx.strokeRect(-0.5, y, 256, 256);
      ctx.strokeRect(256, y, 256, 256);
    }
  }
  if (kind === "brick") {
    ctx.strokeStyle = "#81867f";
    ctx.lineWidth = 3;
    for (let row = 0; row < 16; row++) {
      const shift = row % 2 ? 32 : 0;
      for (let x = -64; x < 512; x += 64) {
        ctx.fillStyle = `rgba(100,70,55,${random() * 0.13})`;
        ctx.fillRect(x + shift, row * 32, 64, 32);
        ctx.strokeRect(x + shift, row * 32, 64, 32);
      }
    }
  }
  if (kind === "brickcourse") {
    ctx.strokeStyle = "#bcb7a7";
    ctx.lineWidth = 2.2;
    for (let y = 0; y < 512; y += 128)
      for (let x = 0; x < 512; x += 51.2) {
        ctx.fillStyle = `rgba(95,40,22,${random() * 0.14})`;
        ctx.fillRect(x, y, 51.2, 128);
        ctx.strokeRect(x, y, 51.2, 128);
      }
  }
  if (kind === "broomconcrete") {
    ctx.strokeStyle = "rgba(95,105,100,.16)";
    ctx.lineWidth = 0.7;
    for (let y = 0; y < 512; y += 2) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(512, y);
      ctx.stroke();
    }
  }
  if (kind === "ironcover") {
    ctx.strokeStyle = "#98928a";
    ctx.lineWidth = 10;
    for (let x = 0; x < 512; x += 48)
      for (let y = 0; y < 512; y += 48) {
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x + 16, y + 18);
        ctx.lineTo(x + 36, y + 18);
        ctx.stroke();
      }
  }
  const tex = new THREE.CanvasTexture(canvas);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 4;
  cache.set(kind, tex);
  return tex;
}
export function streetMaterial(name) {
  const mat = new THREE.MeshStandardMaterial({
    color: STREET_COLORS[name],
    roughness: name === "drainmetal" ? 0.66 : 0.94,
    metalness: ["drainmetal", "ironcover"].includes(name) ? 0.6 : 0,
    side: THREE.DoubleSide,
  });
  if (
    [
      "granite",
      "curbstone",
      "paving",
      "brick",
      "gutter",
      "ironcover",
      "brickcourse",
      "broomconcrete",
      "shoulder",
    ].includes(name)
  ) {
    const kind = ["curbstone", "gutter"].includes(name) ? "concrete" : name;
    mat.map = surfaceTexture(kind);
    mat.bumpMap = mat.map;
    mat.bumpScale = kind === "granite" ? 0.012 : 0.004;
  }
  return mat;
}
