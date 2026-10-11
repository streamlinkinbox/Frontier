// Screen-facing editor overlays (gizmos, handles, markers). Drawn without depth testing so
// editing affordances are always visible. Vertex layout: pos xyz + rgba (7 floats).

const LIFT = 0.25;

export class OverlayBuilder {
  constructor() { this.tris = []; this.lines = []; }
  _v(p, c) { return [p[0], p[1] + LIFT, p[2], c[0], c[1], c[2], c[3] ?? 1]; }
  tri(a, b, c, col) { this.tris.push(...this._v(a, col), ...this._v(b, col), ...this._v(c, col)); }
  quad(a, b, c, d, col) { this.tri(a, b, c, col); this.tri(a, c, d, col); }
  line(a, b, col) { this.lines.push(...this._v(a, col), ...this._v(b, col)); }
  // Thick segment between two ground points [x,z]
  band(a, b, w, col) {
    const dx = b[0] - a[0], dz = b[1] - a[1];
    const l = Math.hypot(dx, dz) || 1;
    const nx = (-dz / l) * w / 2, nz = (dx / l) * w / 2;
    const A = [a[0] + nx, 0, a[1] + nz], B = [b[0] + nx, 0, b[1] + nz];
    const C = [b[0] - nx, 0, b[1] - nz], D = [a[0] - nx, 0, a[1] - nz];
    this.quad(A, B, C, D, col);
  }
  arrowhead(tip, dir, size, col) {
    const l = Math.hypot(dir[0], dir[1]) || 1;
    const ux = dir[0] / l, uz = dir[1] / l;
    const px = -uz, pz = ux;
    const base = [tip[0] - ux * size, tip[1] - uz * size];
    this.tri([tip[0], 0, tip[1]], [base[0] + px * size * 0.5, 0, base[1] + pz * size * 0.5], [base[0] - px * size * 0.5, 0, base[1] - pz * size * 0.5], col);
  }
  square(cx, cz, s, col) {
    const h = s / 2;
    this.quad([cx - h, 0, cz - h], [cx + h, 0, cz - h], [cx + h, 0, cz + h], [cx - h, 0, cz + h], col);
  }
  ring(cx, cz, r, col, seg = 36) {
    for (let i = 0; i < seg; i++) {
      const a0 = (i / seg) * Math.PI * 2, a1 = ((i + 1) / seg) * Math.PI * 2;
      this.line([cx + Math.cos(a0) * r, 0, cz + Math.sin(a0) * r], [cx + Math.cos(a1) * r, 0, cz + Math.sin(a1) * r], col);
    }
  }
  build() {
    return { tris: new Float32Array(this.tris), lines: new Float32Array(this.lines) };
  }
}
