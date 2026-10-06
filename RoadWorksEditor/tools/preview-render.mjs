// Headless preview renderer — rasterises a generated network to a PNG so the geometry can be reviewed without a
// browser (CI screenshots, quick regression eyeballing).
//   node tools/preview-render.mjs [out.png] [top|iso]
import { writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { buildNetwork } from '../src/Network.js';
import { BRIDGE_DEFAULTS } from '../src/BridgeMesh.js';

const WIDTH = 1280;
const HEIGHT = 760;

const COLOURS = {
  road: [52, 56, 63],
  curb: [158, 163, 170],
  pavement: [107, 112, 119],
  markings: [230, 226, 214],
  deck: [139, 142, 147],
  structure: [89, 97, 110],
  piers: [124, 127, 133],
  railing: [167, 173, 181],
  barrier: [154, 157, 161],
  cables: [198, 202, 208],
  earth: [107, 100, 80],
  roadbed: [138, 141, 146],
  signFace: [190, 70, 66],
  signPost: [154, 160, 168],
};

// `pavement#brick@1.00` → `pavement`
const baseName = (n) => n.split('#')[0];

const mk = (name, pts, extra = {}) => ({
  id: name,
  name,
  preset: 'street',
  overrides: {},
  family: 'road',
  capMode: 'flat',
  closed: false,
  tension: 0,
  radiusBias: 0,
  visible: true,
  points: pts.map(([x, y, z = 0]) => ({ x, y, z })),
  ...extra,
});

export const DEMO = [
  mk('Harbour Avenue', [[-150, 0], [-60, 0], [0, 0], [70, 6], [150, 24]], { preset: 'avenue', paving: 'flagstone', guardrail: { type: 'pedestrian', when: 'fill', fillTrigger: 2.5, height: 1.1 } }),
  mk('Mill Street', [[0, -176], [0, -120], [0, -40], [0, 0], [0, 55], [10, 120]], { paving: 'concrete' }),
  mk('Quay Lane', [[-150, -70], [-80, -58], [-20, -40], [0, -40], [60, -52], [130, -46]], { preset: 'narrow', paving: 'brick' }),
  mk('Dock Alley', [[-80, -58], [-78, 0]], { preset: 'alley', paving: 'cobble' }),
  mk('Harbour Expressway', [[-170, -176], [-60, -176], [60, -176], [170, -170]], { preset: 'highway', paving: 'asphaltWalk', guardrail: { type: 'thrie', when: 'always', offset: 0.45 } }),
  mk('Quarry Ramp', [[0, 55], [45, 62, 1.8], [95, 70, 4.4], [150, 74, 6.0]], { paving: 'granite', guardrail: { type: 'wbeam', when: 'fill', fillTrigger: 1.2 } }),
  mk('Estuary Viaduct', [[-130, 95, 11], [-60, 86, 11], [10, 92, 11], [80, 104, 11], [150, 96, 11]], {
    preset: 'highway',
    family: 'bridge',
    bridge: { ...BRIDGE_DEFAULTS, type: 'cablestay', pierType: 'hammerhead', pierSpacing: 42, railing: 'jersey', towerHeight: 26, deckThickness: 1.1 },
  }),
  mk('Mill Street Overpass', [[10, 120], [16, 150, 4], [20, 190, 11], [20, 230, 11]], {
    family: 'bridge',
    bridge: { ...BRIDGE_DEFAULTS, type: 'arch', pierType: 'column', pierSpacing: 30, railing: 'parapet', archRise: 7 },
  }),
  // The bridge gallery: one span of every superstructure family, laid out in a grid north of the network.
  ...[
    ['Beam Viaduct', 'beam', 'street', { pierSpacing: 30 }],
    ['Box Girder Span', 'box', 'avenue', { pierSpacing: 38, girderDepth: 1.6 }],
    ['Slab Crossing', 'slab', 'street', { pierSpacing: 26 }],
    ['Cantilever Reach', 'cantilever', 'avenue', { pierSpacing: 46, girderDepth: 1.4, pierType: 'wall' }],
    ['Deck Arch', 'arch', 'street', { archRise: 7 }],
    ['Bowstring Arch', 'tiedarch', 'street', { archRise: 8, cableCount: 9 }],
    ['Stone Viaduct', 'masonry', 'narrow', { pierSpacing: 22, pierWidth: 2.0 }],
    ['Warren Truss', 'truss', 'street', { trussHeight: 4.6 }],
    ['Pratt Through Truss', 'throughtruss', 'street', { trussHeight: 5.2 }],
    ['Suspension Span', 'suspension', 'avenue', { towerHeight: 24, cableCount: 10 }],
    ['Cable-stay Span', 'cablestay', 'avenue', { towerHeight: 26, railing: 'jersey' }],
  ].map(([name, type, preset, overrides], i) => {
    const x = -248 + (i % 4) * 164;
    const y = 300 + Math.floor(i / 4) * 82;
    return mk(name, [[x, y, 12], [x + 104, y, 12]], {
      preset,
      family: 'bridge',
      bridge: { ...BRIDGE_DEFAULTS, type, pierType: 'column', railing: 'parapet', ...overrides },
    });
  }),
];

export function render(groups, { view = 'iso', width = WIDTH, height = HEIGHT, bg = [36, 40, 49] } = {}) {
  const yaw = view === 'top' ? 0 : view === 'side' ? 0 : -0.62;
  const pitch = view === 'top' ? Math.PI / 2 : view === 'side' ? 0.02 : 0.62;
  const cy = Math.cos(yaw);
  const sy = Math.sin(yaw);
  const cp = Math.cos(pitch);
  const sp = Math.sin(pitch);

  const project = (x, y, z) => {
    const rx = x * cy - y * sy;
    const ry = x * sy + y * cy;
    // Z-up camera: screen right = rx, screen up = ry*sin(pitch) + z*cos(pitch), depth grows away from the eye.
    return { x: rx, y: ry * sp + z * cp, depth: ry * cp - z * sp };
  };

  // bounds
  let minX = Infinity;
  let maxX = -Infinity;
  let minY = Infinity;
  let maxY = -Infinity;
  for (const spec of Object.values(groups)) {
    for (let i = 0; i < spec.positions.length; i += 3) {
      const p = project(spec.positions[i], spec.positions[i + 1], spec.positions[i + 2]);
      minX = Math.min(minX, p.x);
      maxX = Math.max(maxX, p.x);
      minY = Math.min(minY, p.y);
      maxY = Math.max(maxY, p.y);
    }
  }
  const pad = 24;
  const scale = Math.min((width - pad * 2) / (maxX - minX), (height - pad * 2) / (maxY - minY));
  const ox = (width - (maxX + minX) * scale) * 0.5;
  const oy = (height - (maxY + minY) * scale) * 0.5;

  const rgb = new Uint8Array(width * height * 3);
  for (let i = 0; i < width * height; i++) {
    rgb[i * 3] = bg[0];
    rgb[i * 3 + 1] = bg[1];
    rgb[i * 3 + 2] = bg[2];
  }
  const depthBuf = new Float32Array(width * height).fill(Infinity);

  const light = [0.4, -0.35, 0.85];
  const ll = Math.hypot(...light);

  for (const [name, spec] of Object.entries(groups)) {
    const base = COLOURS[baseName(name)] || [140, 140, 140];
    const pos = spec.positions;
    const nor = spec.normals;
    for (let t = 0; t < spec.indices.length; t += 3) {
      const ia = spec.indices[t];
      const ib = spec.indices[t + 1];
      const ic = spec.indices[t + 2];
      const pa = project(pos[ia * 3], pos[ia * 3 + 1], pos[ia * 3 + 2]);
      const pb = project(pos[ib * 3], pos[ib * 3 + 1], pos[ib * 3 + 2]);
      const pc = project(pos[ic * 3], pos[ic * 3 + 1], pos[ic * 3 + 2]);

      let nx = 0;
      let ny = 0;
      let nz = 1;
      if (nor && nor.length) {
        nx = (nor[ia * 3] + nor[ib * 3] + nor[ic * 3]) / 3;
        ny = (nor[ia * 3 + 1] + nor[ib * 3 + 1] + nor[ic * 3 + 1]) / 3;
        nz = (nor[ia * 3 + 2] + nor[ib * 3 + 2] + nor[ic * 3 + 2]) / 3;
      }
      const nl = Math.hypot(nx, ny, nz) || 1;
      const lambert = Math.abs((nx * light[0] + ny * light[1] + nz * light[2]) / (nl * ll));
      const shade = 0.3 + 0.75 * lambert;

      const ax = pa.x * scale + ox;
      const ay = height - (pa.y * scale + oy);
      const bx = pb.x * scale + ox;
      const by = height - (pb.y * scale + oy);
      const cx2 = pc.x * scale + ox;
      const cy2 = height - (pc.y * scale + oy);

      const x0 = Math.max(0, Math.floor(Math.min(ax, bx, cx2)));
      const x1 = Math.min(width - 1, Math.ceil(Math.max(ax, bx, cx2)));
      const y0 = Math.max(0, Math.floor(Math.min(ay, by, cy2)));
      const y1 = Math.min(height - 1, Math.ceil(Math.max(ay, by, cy2)));
      const area = (bx - ax) * (cy2 - ay) - (by - ay) * (cx2 - ax);
      if (Math.abs(area) < 1e-9) continue;

      for (let py = y0; py <= y1; py++) {
        for (let px = x0; px <= x1; px++) {
          const sx = px + 0.5;
          const sy2 = py + 0.5;
          const w0 = ((bx - ax) * (sy2 - ay) - (by - ay) * (sx - ax)) / area;
          const w1 = ((cx2 - bx) * (sy2 - by) - (cy2 - by) * (sx - bx)) / area;
          const w2 = 1 - w0 - w1;
          if (w0 < 0 || w1 < 0 || w2 < 0) continue;
          const depth = w1 * pa.depth + w2 * pb.depth + w0 * pc.depth;
          const o = py * width + px;
          if (depth >= depthBuf[o]) continue;
          depthBuf[o] = depth;
          rgb[o * 3] = Math.min(255, base[0] * shade);
          rgb[o * 3 + 1] = Math.min(255, base[1] * shade);
          rgb[o * 3 + 2] = Math.min(255, base[2] * shade);
        }
      }
    }
  }
  return { rgb, width, height };
}

export function encodePng({ rgb, width, height }) {
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (width * 3 + 1)] = 0;
    Buffer.from(rgb.buffer, y * width * 3, width * 3).copy(raw, y * (width * 3 + 1) + 1);
  }
  const chunks = [Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])];
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  chunks.push(chunk('IHDR', ihdr));
  chunks.push(chunk('IDAT', deflateSync(raw, { level: 6 })));
  chunks.push(chunk('IEND', Buffer.alloc(0)));
  return Buffer.concat(chunks);
}

function chunk(type, data) {
  const out = Buffer.alloc(data.length + 12);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  data.copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)) >>> 0, 8 + data.length);
  return out;
}

const CRC_TABLE = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c;
  }
  return t;
})();

function crc32(buf) {
  let c = -1;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return c ^ -1;
}

if (process.argv[1] && process.argv[1].endsWith('preview-render.mjs')) {
  const out = process.argv[2] || 'preview.png';
  const view = process.argv[3] || 'iso';
  const net = buildNetwork(DEMO, { markings: true });
  const image = render(net.groups, { view });
  writeFileSync(out, encodePng(image));
  console.log(`${out} · ${view} · ${net.stats.triangles} triangles · ${net.stats.junctions} junctions`);
}
