import { CanvasTexture, RepeatWrapping, SRGBColorSpace } from 'three';

function canvas(size: number, height = size) {
  const c = document.createElement('canvas');
  c.width = size;
  c.height = height;
  return c;
}

/** Packed dirt with gravel speckle and tyre-worn variation. */
export function makeDirtTexture(): CanvasTexture {
  const size = 256;
  const c = canvas(size);
  const ctx = c.getContext('2d')!;
  ctx.fillStyle = '#6b5638';
  ctx.fillRect(0, 0, size, size);

  // large tonal patches
  for (let i = 0; i < 220; i++) {
    const x = Math.random() * size;
    const y = Math.random() * size;
    const r = 6 + Math.random() * 34;
    const shade = Math.random();
    ctx.fillStyle =
      shade > 0.5
        ? `rgba(58,44,28,${0.06 + Math.random() * 0.12})`
        : `rgba(140,116,80,${0.05 + Math.random() * 0.1})`;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
  }

  // gravel
  for (let i = 0; i < 2600; i++) {
    const x = Math.random() * size;
    const y = Math.random() * size;
    const r = 0.4 + Math.random() * 1.7;
    const v = Math.random();
    ctx.fillStyle = v > 0.6 ? `rgba(180,170,150,${0.25 + Math.random() * 0.4})` : `rgba(40,30,18,${0.2 + Math.random() * 0.4})`;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
  }

  // fine grain
  const img = ctx.getImageData(0, 0, size, size);
  for (let i = 0; i < img.data.length; i += 4) {
    const n = (Math.random() - 0.5) * 26;
    img.data[i] = Math.max(0, Math.min(255, img.data[i] + n));
    img.data[i + 1] = Math.max(0, Math.min(255, img.data[i + 1] + n));
    img.data[i + 2] = Math.max(0, Math.min(255, img.data[i + 2] + n));
  }
  ctx.putImageData(img, 0, 0);

  const tex = new CanvasTexture(c);
  tex.wrapS = tex.wrapT = RepeatWrapping;
  tex.colorSpace = SRGBColorSpace;
  tex.anisotropy = 8;
  return tex;
}

/** Alpha-tested leaf card for ferns and canopy foliage. */
export function makeLeafTexture(): CanvasTexture {
  const size = 128;
  const c = canvas(size);
  const ctx = c.getContext('2d')!;
  ctx.clearRect(0, 0, size, size);

  const drawLeaf = (cx: number, cy: number, len: number, wid: number, ang: number, hue: number) => {
    ctx.save();
    ctx.translate(cx, cy);
    ctx.rotate(ang);
    const g = ctx.createLinearGradient(0, -len * 0.5, 0, len * 0.5);
    g.addColorStop(0, `hsl(${hue}, 42%, 26%)`);
    g.addColorStop(0.55, `hsl(${hue}, 46%, 34%)`);
    g.addColorStop(1, `hsl(${hue}, 40%, 20%)`);
    ctx.fillStyle = g;
    ctx.beginPath();
    ctx.moveTo(0, -len * 0.5);
    ctx.quadraticCurveTo(wid, 0, 0, len * 0.5);
    ctx.quadraticCurveTo(-wid, 0, 0, -len * 0.5);
    ctx.fill();
    ctx.strokeStyle = `hsla(${hue - 8}, 40%, 16%, 0.8)`;
    ctx.lineWidth = 1.1;
    ctx.beginPath();
    ctx.moveTo(0, -len * 0.5);
    ctx.lineTo(0, len * 0.5);
    ctx.stroke();
    ctx.restore();
  };

  for (let i = 0; i < 9; i++) {
    const hue = 92 + Math.random() * 34;
    drawLeaf(
      size * 0.5 + (Math.random() - 0.5) * size * 0.3,
      size * 0.5 + (Math.random() - 0.5) * size * 0.3,
      52 + Math.random() * 40,
      10 + Math.random() * 10,
      (Math.random() - 0.5) * Math.PI,
      hue,
    );
  }

  const tex = new CanvasTexture(c);
  tex.colorSpace = SRGBColorSpace;
  return tex;
}

/** Vertical bark striations for tree trunks and vine stems. */
export function makeBarkTexture(): CanvasTexture {
  const w = 128;
  const h = 256;
  const c = canvas(w, h);
  const ctx = c.getContext('2d')!;
  ctx.fillStyle = '#3b2e21';
  ctx.fillRect(0, 0, w, h);
  for (let i = 0; i < 90; i++) {
    const x = Math.random() * w;
    const wid = 1 + Math.random() * 5;
    const v = Math.random();
    ctx.fillStyle =
      v > 0.5 ? `rgba(24,18,12,${0.1 + Math.random() * 0.3})` : `rgba(96,78,56,${0.08 + Math.random() * 0.22})`;
    ctx.fillRect(x, 0, wid, h);
  }
  const tex = new CanvasTexture(c);
  tex.wrapS = tex.wrapT = RepeatWrapping;
  tex.colorSpace = SRGBColorSpace;
  return tex;
}

/** Soft radial sprite for dust, spores and leaf shards. */
export function makeSparkTexture(): CanvasTexture {
  const size = 64;
  const c = canvas(size);
  const ctx = c.getContext('2d')!;
  const g = ctx.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
  g.addColorStop(0, 'rgba(255,255,255,1)');
  g.addColorStop(0.35, 'rgba(255,255,255,0.65)');
  g.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, size, size);
  const tex = new CanvasTexture(c);
  tex.colorSpace = SRGBColorSpace;
  return tex;
}
