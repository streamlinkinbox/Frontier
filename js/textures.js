import * as THREE from 'three';

// Procedural PBR textures (seeded, tileable): fossil bone, lab floor,
// contact shadow. No external image assets.

function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function canvasTex(c, srgb) {
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 8;
  return t;
}

// draw fn at 9 wrapped offsets for seamless tiling
function wrapped(S, fn) {
  for (const ox of [-S, 0, S])
    for (const oy of [-S, 0, S]) fn(ox, oy);
}

export function makeBoneTextures() {
  const S = 512;
  const rnd = mulberry32(1337);

  // ---- albedo ----
  const c = document.createElement('canvas');
  c.width = c.height = S;
  const x = c.getContext('2d');
  x.fillStyle = '#d9c69e';
  x.fillRect(0, 0, S, S);
  // large soft mottling
  for (let i = 0; i < 90; i++) {
    const r = 30 + rnd() * 90;
    const px = rnd() * S, py = rnd() * S;
    const col = rnd() < 0.5 ? '138,110,72' : '236,222,190';
    const a = 0.08 + rnd() * 0.1;
    wrapped(S, (ox, oy) => {
      const g = x.createRadialGradient(px + ox, py + oy, 0, px + ox, py + oy, r);
      g.addColorStop(0, `rgba(${col},${a.toFixed(3)})`);
      g.addColorStop(1, `rgba(${col},0)`);
      x.fillStyle = g;
      x.beginPath();
      x.arc(px + ox, py + oy, r, 0, 7);
      x.fill();
    });
  }
  // speckle
  for (let i = 0; i < 2600; i++) {
    const px = rnd() * S, py = rnd() * S;
    const r = 0.5 + rnd() * 1.8;
    const col = rnd() < 0.55 ? '110,88,58' : '245,236,212';
    const a = 0.05 + rnd() * 0.1;
    wrapped(S, (ox, oy) => {
      x.fillStyle = `rgba(${col},${a.toFixed(3)})`;
      x.beginPath();
      x.arc(px + ox, py + oy, r, 0, 7);
      x.fill();
    });
  }
  // cracks (shared paths, also drawn into bump)
  const cracks = [];
  for (let i = 0; i < 16; i++) {
    let px = rnd() * S, py = rnd() * S, a = rnd() * Math.PI * 2;
    const pts = [[px, py]];
    const segs = 4 + ((rnd() * 6) | 0);
    for (let k = 0; k < segs; k++) {
      a += (rnd() - 0.5) * 1.1;
      px += Math.cos(a) * (8 + rnd() * 20);
      py += Math.sin(a) * (8 + rnd() * 20);
      pts.push([px, py]);
    }
    cracks.push(pts);
  }
  const strokeCracks = (ctx, col, w) => {
    ctx.strokeStyle = col;
    ctx.lineWidth = w;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    for (const pts of cracks) {
      wrapped(S, (ox, oy) => {
        ctx.beginPath();
        pts.forEach(([px, py], k) => (k ? ctx.lineTo(px + ox, py + oy) : ctx.moveTo(px + ox, py + oy)));
        ctx.stroke();
      });
    }
  };
  strokeCracks(x, 'rgba(88,68,42,0.4)', 1.5);

  // ---- bump ----
  const bc = document.createElement('canvas');
  bc.width = bc.height = S;
  const bx = bc.getContext('2d');
  bx.fillStyle = '#808080';
  bx.fillRect(0, 0, S, S);
  const rnd2 = mulberry32(777);
  for (let i = 0; i < 1800; i++) {
    const px = rnd2() * S, py = rnd2() * S;
    const r = 0.5 + rnd2() * 2.2;
    const v = rnd2() < 0.5 ? 96 : 168;
    wrapped(S, (ox, oy) => {
      bx.fillStyle = `rgba(${v},${v},${v},0.25)`;
      bx.beginPath();
      bx.arc(px + ox, py + oy, r, 0, 7);
      bx.fill();
    });
  }
  strokeCracks(bx, 'rgba(35,35,35,0.85)', 2);

  // ---- roughness ----
  const rc = document.createElement('canvas');
  rc.width = rc.height = 256;
  const rx = rc.getContext('2d');
  rx.fillStyle = '#c2c2c2';
  rx.fillRect(0, 0, 256, 256);
  const rnd3 = mulberry32(4242);
  for (let i = 0; i < 60; i++) {
    const r = 15 + rnd3() * 50;
    const px = rnd3() * 256, py = rnd3() * 256;
    const v = 165 + ((rnd3() * 60) | 0);
    wrapped(256, (ox, oy) => {
      const g = rx.createRadialGradient(px + ox, py + oy, 0, px + ox, py + oy, r);
      g.addColorStop(0, `rgba(${v},${v},${v},0.5)`);
      g.addColorStop(1, `rgba(${v},${v},${v},0)`);
      rx.fillStyle = g;
      rx.beginPath();
      rx.arc(px + ox, py + oy, r, 0, 7);
      rx.fill();
    });
  }

  return {
    map: canvasTex(c, true),
    bumpMap: canvasTex(bc, false),
    roughnessMap: canvasTex(rc, false),
  };
}

export function makeGroundTextures() {
  const S = 512;
  const rnd = mulberry32(9001);
  const c = document.createElement('canvas');
  c.width = c.height = S;
  const x = c.getContext('2d');
  x.fillStyle = '#141a24';
  x.fillRect(0, 0, S, S);
  for (let i = 0; i < 70; i++) {
    const r = 25 + rnd() * 80;
    const px = rnd() * S, py = rnd() * S;
    const col = rnd() < 0.5 ? '8,11,17' : '30,38,52';
    const a = 0.15 + rnd() * 0.2;
    wrapped(S, (ox, oy) => {
      const g = x.createRadialGradient(px + ox, py + oy, 0, px + ox, py + oy, r);
      g.addColorStop(0, `rgba(${col},${a.toFixed(3)})`);
      g.addColorStop(1, `rgba(${col},0)`);
      x.fillStyle = g;
      x.beginPath();
      x.arc(px + ox, py + oy, r, 0, 7);
      x.fill();
    });
  }
  for (let i = 0; i < 3200; i++) {
    const px = rnd() * S, py = rnd() * S;
    const v = 18 + ((rnd() * 40) | 0);
    wrapped(S, (ox, oy) => {
      x.fillStyle = `rgba(${v},${v + 4},${v + 10},0.5)`;
      x.fillRect(px + ox, py + oy, 1.5, 1.5);
    });
  }
  const map = canvasTex(c, true);
  map.repeat.set(10, 10);
  return { map };
}

export function makeContactShadow() {
  const S = 256;
  const c = document.createElement('canvas');
  c.width = c.height = S;
  const x = c.getContext('2d');
  const g = x.createRadialGradient(S / 2, S / 2, 8, S / 2, S / 2, S / 2);
  g.addColorStop(0, 'rgba(0,0,0,0.85)');
  g.addColorStop(0.55, 'rgba(0,0,0,0.45)');
  g.addColorStop(1, 'rgba(0,0,0,0)');
  x.fillStyle = g;
  x.fillRect(0, 0, S, S);
  return canvasTex(c, false);
}
