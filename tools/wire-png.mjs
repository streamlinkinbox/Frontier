// Minimal top-down wireframe renderer → PNG (no dependencies). Used to eyeball junction topology.
import { deflateSync } from "node:zlib";
import { writeFileSync } from "node:fs";

function crc32(buf) {
  let c, crc = 0xffffffff;
  for (let n = 0; n < buf.length; n++) { c = (crc ^ buf[n]) & 0xff; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crc = (crc >>> 8) ^ c; }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
export function encodePNG(width, height, rgba) {
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y++) { raw[y * (width * 4 + 1)] = 0; rgba.copy(raw, y * (width * 4 + 1) + 1, y * width * 4, (y + 1) * width * 4); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}

export class Canvas {
  constructor(w, h, bg = [16, 16, 16]) { this.w = w; this.h = h; this.buf = Buffer.alloc(w * h * 4); for (let i = 0; i < w * h; i++) { this.buf[i * 4] = bg[0]; this.buf[i * 4 + 1] = bg[1]; this.buf[i * 4 + 2] = bg[2]; this.buf[i * 4 + 3] = 255; } }
  px(x, y, c, a = 1) {
    x |= 0; y |= 0; if (x < 0 || y < 0 || x >= this.w || y >= this.h) return;
    const i = (y * this.w + x) * 4;
    this.buf[i] = this.buf[i] * (1 - a) + c[0] * a; this.buf[i + 1] = this.buf[i + 1] * (1 - a) + c[1] * a; this.buf[i + 2] = this.buf[i + 2] * (1 - a) + c[2] * a;
  }
  line(x0, y0, x1, y1, c, a = 1) {
    const dx = Math.abs(x1 - x0), dy = Math.abs(y1 - y0), n = Math.max(1, Math.ceil(Math.max(dx, dy)));
    for (let i = 0; i <= n; i++) this.px(x0 + ((x1 - x0) * i) / n, y0 + ((y1 - y0) * i) / n, c, a);
  }
  fillTri(a, b, c, col, alpha = 1) {
    const minX = Math.max(0, Math.floor(Math.min(a[0], b[0], c[0]))), maxX = Math.min(this.w - 1, Math.ceil(Math.max(a[0], b[0], c[0])));
    const minY = Math.max(0, Math.floor(Math.min(a[1], b[1], c[1]))), maxY = Math.min(this.h - 1, Math.ceil(Math.max(a[1], b[1], c[1])));
    const e = (p, q, r) => (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
    const area = e(a, b, c); if (Math.abs(area) < 1e-9) return;
    for (let y = minY; y <= maxY; y++) for (let x = minX; x <= maxX; x++) {
      const p = [x + 0.5, y + 0.5];
      const w0 = e(b, c, p) / area, w1 = e(c, a, p) / area, w2 = e(a, b, p) / area;
      if (w0 >= 0 && w1 >= 0 && w2 >= 0) this.px(x, y, col, alpha);
    }
  }
  save(path) { writeFileSync(path, encodePNG(this.w, this.h, this.buf)); }
}

const MAT_COLORS = { asphalt: [52, 52, 56], asphaltBike: [120, 50, 45], curb: [150, 150, 145], paving: [120, 110, 95], concrete: [130, 128, 120], grass: [60, 90, 45], soil: [80, 60, 40], gravel: [110, 105, 95], paint: [220, 220, 210], paintYellow: [220, 180, 60], metal: [120, 125, 130], metalPainted: [90, 95, 100], steelGalv: [150, 155, 160], rubber: [30, 30, 30] };

/** Render a MeshBuilder (or list of them) top-down into a PNG. */
export function renderPlan(builders, path, opts = {}) {
  const list = Array.isArray(builders) ? builders : [builders];
  let min = [Infinity, Infinity], max = [-Infinity, -Infinity];
  for (const mb of list) for (let i = 0; i < mb.vertexCount; i++) { const x = mb.positions[i * 3], z = mb.positions[i * 3 + 2]; if (x < min[0]) min[0] = x; if (z < min[1]) min[1] = z; if (x > max[0]) max[0] = x; if (z > max[1]) max[1] = z; }
  if (opts.center && opts.extent) { min = [opts.center[0] - opts.extent, opts.center[1] - opts.extent]; max = [opts.center[0] + opts.extent, opts.center[1] + opts.extent]; }
  const W = opts.width || 1400, pad = 20;
  const scale = (W - 2 * pad) / Math.max(max[0] - min[0], max[1] - min[1]);
  const H = Math.ceil((max[1] - min[1]) * scale + 2 * pad);
  const cv = new Canvas(W, H);
  const map = (p) => [pad + (p[0] - min[0]) * scale, pad + (p[2] - min[1]) * scale];
  for (const mb of list) {
    if (opts.fill !== false) for (const f of mb.faces) {
      const col = MAT_COLORS[mb.materials[f.m]] || [100, 100, 100];
      const pts = f.i.map((v) => map(mb.position(v)));
      for (let k = 1; k + 1 < pts.length; k++) cv.fillTri(pts[0], pts[k], pts[k + 1], col);
    }
    if (opts.wire !== false) for (const f of mb.faces) {
      const pts = f.i.map((v) => map(mb.position(v)));
      const col = f.i.length === 4 ? [110, 190, 255] : [255, 90, 90];
      for (let k = 0; k < pts.length; k++) { const a = pts[k], b = pts[(k + 1) % pts.length]; cv.line(a[0], a[1], b[0], b[1], col, f.i.length === 4 ? 0.55 : 1); }
    }
  }
  cv.save(path);
  return { width: W, height: H, scale };
}
