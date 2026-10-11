/* ============================================================
   FRONTIER · ROAD EDITOR — procedural material library
   Every surface and every road marking is painted into a <canvas> at
   load time: colour, height-derived normals and a roughness plate. No
   binary assets, so the whole editor is text in the repository.
   ============================================================ */

import * as THREE from '../vendor/three.module.js';

const C = (w, h) => { const c = document.createElement('canvas'); c.width = w; c.height = h || w; return c; };
const rnd = (seed) => { let s = seed >>> 0 || 7; return () => { s ^= s << 13; s >>>= 0; s ^= s >> 17; s <<= 0; s ^= s << 5; s >>>= 0; return s / 4294967296; }; };

/** colour canvas + height canvas → {map, normalMap, roughnessMap} */
function package_(size, drawColor, drawHeight, opt = {}) {
  const cc = C(size), hc = C(size / (opt.halfHeight ? 2 : 1));
  const cx = cc.getContext('2d'), hx = hc.getContext('2d');
  cx.globalCompositeOperation = 'source-over';
  drawColor(cx, size, rnd(opt.seed ?? 1337));
  hx.fillStyle = opt.heightBase ?? '#808080';
  hx.fillRect(0, 0, hc.width, hc.height);
  drawHeight(hx, hc.width, rnd((opt.seed ?? 1337) + 99));
  const map = new THREE.CanvasTexture(cc);
  map.wrapS = map.wrapT = THREE.RepeatWrapping;
  map.colorSpace = THREE.SRGBColorSpace;
  map.anisotropy = opt.aniso ?? 8;
  const { normal, rough } = fromHeight(hc, opt.normalStrength ?? 1.4, opt.roughFrom ?? [.35, .95]);
  return { map, normalMap: normal, roughnessMap: rough, srcColor: cc, srcHeight: hc };
}
function fromHeight(hc, strength, roughRange) {
  const w = hc.width, h = hc.height;
  const src = hc.getContext('2d').getImageData(0, 0, w, h).data;
  const nc = C(w, h), rc = C(w, h);
  const nctx = nc.getContext('2d'), rctx = rc.getContext('2d');
  const nid = nctx.createImageData(w, h), rid = rctx.createImageData(w, h);
  const at = (x, y) => src[((y + h) % h * w + (x + w) % w) * 4];
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const dx = (at(x + 1, y) - at(x - 1, y)) / 255 * strength;
    const dy = (at(x, y + 1) - at(x, y - 1)) / 255 * strength;
    const l = Math.hypot(-dx, -dy, 1);
    const i = (y * w + x) * 4;
    nid.data[i] = ((-dx / l) * .5 + .5) * 255;
    nid.data[i + 1] = ((dy / l) * .5 + .5) * 255;
    nid.data[i + 2] = ((1 / l) * .5 + .5) * 255;
    nid.data[i + 3] = 255;
    const v = at(x, y) / 255, r = (roughRange[1] - roughRange[0]) * (1 - v) + roughRange[0];
    rid.data[i] = rid.data[i + 1] = rid.data[i + 2] = r * 255; rid.data[i + 3] = 255;
  }
  nctx.putImageData(nid, 0, 0); rctx.putImageData(rid, 0, 0);
  const mk = c => { const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.RepeatWrapping; return t; };
  return { normal: mk(nc), rough: mk(rc) };
}

/* ------------------------------- painters ------------------------------- */
const speckle = (x, size, n, colors, rMin = .4, rMax = 1.9, rand = Math.random) => {
  for (let i = 0; i < n; i++) {
    x.fillStyle = colors[(rand() * colors.length) | 0];
    const r = rMin + rand() * (rMax - rMin);
    x.beginPath(); x.arc(rand() * size, rand() * size, r, 0, 7); x.fill();
  }
};
const grain = (x, size, amount, rand = Math.random) => {
  const id = x.getImageData(0, 0, size, size), d = id.data;
  for (let i = 0; i < d.length; i += 4) {
    const n = (rand() - .5) * amount;
    d[i] += n; d[i + 1] += n; d[i + 2] += n;
  }
  x.putImageData(id, 0, 0);
};

const PAINTERS = {
  asphalt: {
    size: 512, colorScale: 1,
    color: (x, s, R) => {
      x.fillStyle = '#4b4e52'; x.fillRect(0, 0, s, s);
      for (let i = 0; i < 9000; i++) { const g = 40 + R() * 120; x.fillStyle = `rgba(${g},${g + 2},${g + 6},${.25 + R() * .5})`; x.fillRect(R() * s, R() * s, .6 + R() * 2.4, .6 + R() * 2.4); }
      speckle(x, s, 2600, ['#8e8f92', '#6f7175', '#3a3c3f', '#a3a4a6'], .5, 2.1, R);
      // tar seams, patch repairs, saw cuts
      x.strokeStyle = 'rgba(24,24,26,.75)'; x.lineWidth = 3;
      for (let i = 0; i < 3; i++) { x.beginPath(); let px = R() * s, py = R() * s; x.moveTo(px, py); for (let k = 0; k < 8; k++) { px += (R() - .5) * 90; py += (R() - .5) * 90; x.lineTo(px, py); } x.stroke(); }
      x.strokeStyle = 'rgba(255,255,255,.05)'; x.lineWidth = 1.2;
      for (let i = 0; i < 2; i++) { x.beginPath(); const y0 = R() * s; x.moveTo(0, y0); x.lineTo(s, y0 + (R() - .5) * 6); x.stroke(); }
      x.fillStyle = 'rgba(30,30,32,.4)'; x.beginPath(); x.ellipse(R() * s, R() * s, 40 + R() * 50, 26 + R() * 40, R() * 3, 0, 7); x.fill();
      grain(x, s, 26, R);
    },
    height: (x, s, R) => {
      for (let i = 0; i < 12000; i++) { const g = 90 + R() * 90; x.fillStyle = `rgba(${g},${g},${g},.55)`; x.fillRect(R() * s, R() * s, 1 + R() * 3, 1 + R() * 3); }
      x.strokeStyle = 'rgba(20,20,20,.85)'; x.lineWidth = 5;
      for (let i = 0; i < 3; i++) { x.beginPath(); let px = R() * s, py = R() * s; x.moveTo(px, py); for (let k = 0; k < 8; k++) { px += (R() - .5) * 90; py += (R() - .5) * 90; x.lineTo(px, py); } x.stroke(); }
    }
  },
  concrete: {
    size: 512,
    color: (x, s, R) => {
      x.fillStyle = '#a3a29c'; x.fillRect(0, 0, s, s);
      for (let i = 0; i < 5200; i++) { const g = 150 + R() * 60; x.fillStyle = `rgba(${g},${g},${g - 4},${.18 + R() * .3})`; x.beginPath(); x.arc(R() * s, R() * s, R() * 2.4, 0, 7); x.fill(); }
      // saw-cut joints on a 4 m panel grid
      x.strokeStyle = 'rgba(70,70,68,.75)'; x.lineWidth = 3.4;
      for (const p of [0, s / 2]) { x.beginPath(); x.moveTo(p, 0); x.lineTo(p, s); x.moveTo(0, p); x.lineTo(s, p); x.stroke(); }
      x.fillStyle = 'rgba(80,80,78,.25)';
      for (let i = 0; i < 24; i++) x.fillRect(R() * s, R() * s, 20 + R() * 90, 4 + R() * 12);
      grain(x, s, 16, R);
    },
    height: (x, s, R) => {
      x.strokeStyle = 'rgba(10,10,10,.95)'; x.lineWidth = 6;
      for (const p of [0, s / 2]) { x.beginPath(); x.moveTo(p, 0); x.lineTo(p, s); x.moveTo(0, p); x.lineTo(s, p); x.stroke(); }
      for (let i = 0; i < 900; i++) { x.fillStyle = `rgba(${100 + R() * 60},${100 + R() * 60},${100 + R() * 60},.5)`; x.beginPath(); x.arc(R() * s, R() * s, R() * 2, 0, 7); x.fill(); }
    }
  },
  brick: {
    size: 512,
    color: (x, s, R) => {
      x.fillStyle = '#8d8880'; x.fillRect(0, 0, s, s);
      const bw = s / 8, bh = s / 16;
      for (let ry = 0; ry < 16; ry++) for (let rx = -1; rx < 9; rx++) {
        const off = (ry % 2) * bw / 2;
        const v = R();
        x.fillStyle = `rgb(${148 + v * 68},${62 + v * 44},${44 + v * 34})`;
        x.fillRect(rx * bw + off + 1.6, ry * bh + 1.6, bw - 3.2, bh - 3.2);
        x.fillStyle = `rgba(255,240,220,${R() * .12})`; x.fillRect(rx * bw + off + 1.6, ry * bh + 1.6, bw - 3.2, bh - 3.2);
      }
      grain(x, s, 22, R);
    },
    height: (x, s, R) => {
      x.fillStyle = '#2a2a2a'; x.fillRect(0, 0, s, s);
      const bw = s / 8, bh = s / 16;
      for (let ry = 0; ry < 16; ry++) for (let rx = -1; rx < 9; rx++) {
        const off = (ry % 2) * bw / 2;
        x.fillStyle = `rgb(${170 + R() * 60},${170 + R() * 60},${170 + R() * 60})`;
        x.fillRect(rx * bw + off + 2.4, ry * bh + 2.4, bw - 4.8, bh - 4.8);
      }
    }
  },
  cobble: {
    size: 512,
    color: (x, s, R) => {
      x.fillStyle = '#6b665f'; x.fillRect(0, 0, s, s);
      const n = 11, cw = s / n;
      for (let ry = 0; ry < n; ry++) for (let rx = 0; rx < n; rx++) {
        const v = R();
        x.save();
        x.translate(rx * cw + cw / 2, ry * cw + cw / 2);
        x.rotate((R() - .5) * .22);
        x.fillStyle = `rgb(${108 + v * 88},${106 + v * 82},${100 + v * 74})`;
        roundRect(x, -cw / 2 + 2.6, -cw / 2 + 2.6, cw - 5.2, cw - 5.2, 4.5); x.fill();
        x.fillStyle = `rgba(255,255,255,${R() * .1})`; roundRect(x, -cw / 2 + 3, -cw / 2 + 3, cw - 6, cw / 3, 3); x.fill();
        x.restore();
      }
      grain(x, s, 26, R);
    },
    height: (x, s, R) => {
      x.fillStyle = '#1c1c1c'; x.fillRect(0, 0, s, s);
      const n = 11, cw = s / n;
      for (let ry = 0; ry < n; ry++) for (let rx = 0; rx < n; rx++) {
        x.save(); x.translate(rx * cw + cw / 2, ry * cw + cw / 2); x.rotate((R() - .5) * .22);
        x.fillStyle = `rgb(${180 + R() * 70},${180 + R() * 70},${180 + R() * 70})`;
        roundRect(x, -cw / 2 + 3.4, -cw / 2 + 3.4, cw - 6.8, cw - 6.8, 5); x.fill(); x.restore();
      }
    }
  },
  paver: {
    size: 512,
    color: (x, s, R) => {
      x.fillStyle = '#7d7a74'; x.fillRect(0, 0, s, s);
      const cols = 10, rows = 20, cw = s / cols, ch = s / rows;
      for (let ry = 0; ry < rows; ry++) for (let rx = -1; rx <= cols; rx++) {
        const off = (ry % 2) * cw / 2, v = R();
        x.fillStyle = `rgb(${158 + v * 54},${152 + v * 50},${140 + v * 46})`;
        x.fillRect(rx * cw + off + 1.5, ry * ch + 1.2, cw - 3, ch - 2.4);
        if (R() > .82) { x.fillStyle = `rgba(${90 + R() * 60},${70 + R() * 40},${60 + R() * 40},.5)`; x.fillRect(rx * cw + off + 1.5, ry * ch + 1.2, cw - 3, ch - 2.4); }
      }
      grain(x, s, 18, R);
    },
    height: (x, s, R) => {
      x.fillStyle = '#242424'; x.fillRect(0, 0, s, s);
      const cols = 10, rows = 20, cw = s / cols, ch = s / rows;
      for (let ry = 0; ry < rows; ry++) for (let rx = -1; rx <= cols; rx++) {
        const off = (ry % 2) * cw / 2;
        x.fillStyle = `rgb(${150 + R() * 70},${150 + R() * 70},${150 + R() * 70})`;
        x.fillRect(rx * cw + off + 2.2, ry * ch + 1.8, cw - 4.4, ch - 3.6);
      }
    }
  },
  granite: {
    size: 256,
    color: (x, s, R) => {
      x.fillStyle = '#9d9c98'; x.fillRect(0, 0, s, s);
      speckle(x, s, 5200, ['#d8d7d2', '#6e6d6a', '#b9b8b4', '#8b8a86', '#f0efe9'], .6, 2.6, R);
      grain(x, s, 30, R);
    },
    height: (x, s, R) => { for (let i = 0; i < 3000; i++) { const g = 110 + R() * 80; x.fillStyle = `rgba(${g},${g},${g},.5)`; x.beginPath(); x.arc(R() * s, R() * s, R() * 2, 0, 7); x.fill(); } }
  },
  gravel: {
    size: 256,
    color: (x, s, R) => {
      x.fillStyle = '#6e685e'; x.fillRect(0, 0, s, s);
      for (let i = 0; i < 2600; i++) {
        const v = R(); const g = 90 + v * 130;
        x.fillStyle = `rgb(${g + 8},${g + 2},${g - 10})`;
        x.beginPath(); x.ellipse(R() * s, R() * s, 1.6 + v * 4.6, 1.4 + v * 3.6, R() * 3, 0, 7); x.fill();
      }
      grain(x, s, 34, R);
    },
    height: (x, s, R) => { for (let i = 0; i < 2600; i++) { const v = R(); x.fillStyle = `rgb(${120 + v * 120},${120 + v * 120},${120 + v * 120})`; x.beginPath(); x.ellipse(R() * s, R() * s, 1.8 + v * 4.4, 1.6 + v * 3.4, R() * 3, 0, 7); x.fill(); } }
  },
  grass: {
    size: 256,
    color: (x, s, R) => {
      x.fillStyle = '#3f5a2c'; x.fillRect(0, 0, s, s);
      for (let i = 0; i < 7000; i++) {
        const v = R();
        x.strokeStyle = `rgb(${40 + v * 70},${72 + v * 92},${28 + v * 46})`;
        x.lineWidth = 1 + v;
        const px = R() * s, py = R() * s;
        x.beginPath(); x.moveTo(px, py); x.lineTo(px + (R() - .5) * 4, py - 3 - R() * 6); x.stroke();
      }
      grain(x, s, 20, R);
    },
    height: (x, s, R) => { for (let i = 0; i < 4000; i++) { const g = 100 + R() * 110; x.strokeStyle = `rgba(${g},${g},${g},.6)`; x.lineWidth = 1.4; const px = R() * s, py = R() * s; x.beginPath(); x.moveTo(px, py); x.lineTo(px + (R() - .5) * 4, py - 5 - R() * 6); x.stroke(); } }
  },
  metal: {
    size: 256, half: true,
    color: (x, s, R) => {
      x.fillStyle = '#8b8f95'; x.fillRect(0, 0, s, s);
      for (let i = 0; i < 1400; i++) { const g = 120 + R() * 90; x.strokeStyle = `rgba(${g},${g},${g + 4},.25)`; x.lineWidth = .8 + R() * 1.6; const y = R() * s; x.beginPath(); x.moveTo(0, y); x.lineTo(s, y + (R() - .5) * 3); x.stroke(); }
      for (let i = 0; i < 60; i++) { x.fillStyle = `rgba(${90 + R() * 40},${70 + R() * 30},${50 + R() * 30},${R() * .18})`; x.beginPath(); x.arc(R() * s, R() * s, 4 + R() * 22, 0, 7); x.fill(); }
    },
    height: (x, s, R) => { for (let i = 0; i < 700; i++) { const g = 120 + R() * 40; x.strokeStyle = `rgba(${g},${g},${g},.3)`; x.lineWidth = 1; const y = R() * s; x.beginPath(); x.moveTo(0, y); x.lineTo(s, y); x.stroke(); } }
  },
  iron: {
    size: 256, half: true,
    color: (x, s, R) => {
      x.fillStyle = '#3a3b3d'; x.fillRect(0, 0, s, s);
      x.strokeStyle = 'rgba(150,150,150,.35)'; x.lineWidth = 3;
      for (let r = 12; r < s / 2; r += 16) { x.beginPath(); x.arc(s / 2, s / 2, r, 0, 7); x.stroke(); }
      speckle(x, s, 1400, ['#55575a', '#2a2b2d'], .6, 2.4, R);
    },
    height: (x, s, R) => { x.strokeStyle = 'rgba(20,20,20,.8)'; x.lineWidth = 6; for (let r = 12; r < s / 2; r += 16) { x.beginPath(); x.arc(s / 2, s / 2, r, 0, 7); x.stroke(); } }
  },
  tile: {
    size: 256,
    color: (x, s, R) => {
      x.fillStyle = '#dcdcd4'; x.fillRect(0, 0, s, s);
      const tw = s / 8, th = s / 16;
      for (let ry = 0; ry < 16; ry++) for (let rx = -1; rx <= 8; rx++) {
        const off = (ry % 2) * tw / 2;
        x.fillStyle = `rgb(${222 + R() * 24},${220 + R() * 24},${208 + R() * 26})`;
        x.fillRect(rx * tw + off + 1.2, ry * th + 1.2, tw - 2.4, th - 2.4);
      }
    },
    height: (x, s, R) => { x.fillStyle = '#1e1e1e'; x.fillRect(0, 0, s, s); const tw = s / 8, th = s / 16; for (let ry = 0; ry < 16; ry++) for (let rx = -1; rx <= 8; rx++) { const off = (ry % 2) * tw / 2; x.fillStyle = `rgb(${220 + R() * 30},${220 + R() * 30},${220 + R() * 30})`; x.fillRect(rx * tw + off + 2, ry * th + 2, tw - 4, th - 4); } }
  },
  timber: {
    size: 256,
    color: (x, s, R) => {
      x.fillStyle = '#6a4a2d'; x.fillRect(0, 0, s, s);
      const pw = s / 6;
      for (let i = 0; i < 6; i++) {
        const v = R();
        x.fillStyle = `rgb(${96 + v * 50},${64 + v * 36},${38 + v * 24})`;
        x.fillRect(i * pw + 1.5, 0, pw - 3, s);
        for (let k = 0; k < 40; k++) { x.strokeStyle = `rgba(${60 + R() * 40},${40 + R() * 30},${24 + R() * 20},.35)`; x.lineWidth = 1; const yy = R() * s; x.beginPath(); x.moveTo(i * pw + 2, yy); x.bezierCurveTo(i * pw + pw * .3, yy + 12, i * pw + pw * .6, yy - 12, i * pw + pw - 2, yy); x.stroke(); }
      }
    },
    height: (x, s, R) => { x.fillStyle = '#202020'; x.fillRect(0, 0, s, s); const pw = s / 6; for (let i = 0; i < 6; i++) { x.fillStyle = '#c8c8c8'; x.fillRect(i * pw + 2.5, 0, pw - 5, s); } }
  },
  paint: {   // thermoplastic / road mark paint — tinted per-vertex
    size: 256, half: true,
    color: (x, s, R) => {
      x.fillStyle = '#ffffff'; x.fillRect(0, 0, s, s);
      for (let i = 0; i < 1800; i++) { const g = 200 + R() * 55; x.fillStyle = `rgba(${g},${g},${g},${R() * .5})`; x.fillRect(R() * s, R() * s, R() * 4, R() * 4); }
      x.globalAlpha = .16;
      for (let i = 0; i < 26; i++) { x.fillStyle = '#6f6f6f'; x.beginPath(); x.ellipse(R() * s, R() * s, 6 + R() * 26, 3 + R() * 10, R() * 3, 0, 7); x.fill(); }
      x.globalAlpha = 1;
    },
    height: (x, s, R) => { for (let i = 0; i < 2000; i++) { const g = 150 + R() * 100; x.fillStyle = `rgba(${g},${g},${g},.5)`; x.fillRect(R() * s, R() * s, R() * 4, R() * 4); } }
  },
  water: {
    size: 256,
    color: (x, s, R) => { x.fillStyle = '#5d7f8c'; x.fillRect(0, 0, s, s); for (let i = 0; i < 260; i++) { x.strokeStyle = `rgba(210,230,240,${R() * .16})`; x.lineWidth = 1 + R() * 2.4; const y = R() * s; x.beginPath(); x.moveTo(0, y); for (let k = 0; k < 8; k++) x.lineTo((k + 1) * s / 8, y + (R() - .5) * 8); x.stroke(); } },
    height: (x, s, R) => { for (let i = 0; i < 420; i++) { const g = 90 + R() * 150; x.strokeStyle = `rgba(${g},${g},${g},.6)`; x.lineWidth = 2 + R() * 5; const y = R() * s; x.beginPath(); x.moveTo(0, y); for (let k = 0; k < 8; k++) x.lineTo((k + 1) * s / 8, y + (R() - .5) * 14); x.stroke(); } },
    normalStrength: 2.4
  },
  mesh: {
    size: 128,
    color: (x, s, R) => { x.clearRect(0, 0, s, s); x.strokeStyle = 'rgba(180,184,190,.9)'; x.lineWidth = 3; for (let i = -s; i < s * 2; i += 12) { x.beginPath(); x.moveTo(i, 0); x.lineTo(i + s, s); x.beginPath(); x.moveTo(i, s); x.lineTo(i + s, 0); x.stroke(); } },
    height: null, alpha: 1, skipNormal: true
  },
  bark: {
    size: 128,
    color: (x, s, R) => { x.fillStyle = '#4a3a2a'; x.fillRect(0, 0, s, s); for (let i = 0; i < 400; i++) { const v = R(); x.strokeStyle = `rgba(${50 + v * 70},${38 + v * 54},${26 + v * 36},.75)`; x.lineWidth = 1 + R() * 3.4; const px = R() * s; x.beginPath(); x.moveTo(px, 0); x.bezierCurveTo(px + (R() - .5) * 10, s / 3, px + (R() - .5) * 10, s * .66, px + (R() - .5) * 8, s); x.stroke(); } },
    height: (x, s, R) => { for (let i = 0; i < 300; i++) { const g = R() * 255; x.strokeStyle = `rgba(${g > 128 ? 240 : 20},${g > 128 ? 240 : 20},${g > 128 ? 240 : 20},.5)`; x.lineWidth = 2 + R() * 4; const px = R() * s; x.beginPath(); x.moveTo(px, 0); x.lineTo(px + (R() - .5) * 8, s); x.stroke(); } }
  },
  facade: {
    size: 256,
    color: (x, s, R) => {
      x.fillStyle = '#4b4d52'; x.fillRect(0, 0, s, s);
      const cols = 8, rows = 8, cw = s / cols, ch = s / rows;
      for (let ry = 0; ry < rows; ry++) for (let rx = 0; rx < cols; rx++) {
        const lit = R();
        x.fillStyle = lit > .78 ? `rgba(${180 + R() * 70},${160 + R() * 60},${110 + R() * 60},1)` : `rgba(${26 + R() * 26},${30 + R() * 28},${38 + R() * 30},1)`;
        x.fillRect(rx * cw + 3, ry * ch + 4, cw - 6, ch - 9);
        x.fillStyle = 'rgba(120,120,124,.5)'; x.fillRect(rx * cw, ry * ch + ch - 5, cw, 2.5);
      }
      grain(x, s, 12, R);
    },
    height: (x, s, R) => { x.fillStyle = '#909090'; x.fillRect(0, 0, s, s); const cols = 8, rows = 8, cw = s / cols, ch = s / rows; for (let ry = 0; ry < rows; ry++) for (let rx = 0; rx < cols; rx++) { x.fillStyle = '#2a2a2a'; x.fillRect(rx * cw + 3, ry * ch + 4, cw - 6, ch - 9); } }
  },
  tactile: {
    size: 128,
    color: (x, s, R) => { x.fillStyle = '#e0c33a'; x.fillRect(0, 0, s, s); const n = 6, cw = s / n; for (let ry = 0; ry < n; ry++) for (let rx = 0; rx < n; rx++) { x.fillStyle = '#f2d94f'; x.beginPath(); x.arc(rx * cw + cw / 2, ry * cw + cw / 2, cw * .3, 0, 7); x.fill(); } },
    height: (x, s, R) => { x.fillStyle = '#303030'; x.fillRect(0, 0, s, s); const n = 6, cw = s / n; for (let ry = 0; ry < n; ry++) for (let rx = 0; rx < n; rx++) { const gr = x.createRadialGradient(rx * cw + cw / 2, ry * cw + cw / 2, 1, rx * cw + cw / 2, ry * cw + cw / 2, cw * .32); gr.addColorStop(0, '#ffffff'); gr.addColorStop(1, '#404040'); x.fillStyle = gr; x.beginPath(); x.arc(rx * cw + cw / 2, ry * cw + cw / 2, cw * .32, 0, 7); x.fill(); } },
    normalStrength: 2.6
  },
  mural: {
    size: 256,
    color: (x, s, R) => {
      const g = x.createLinearGradient(0, 0, s, s); g.addColorStop(0, '#1d4f8f'); g.addColorStop(.5, '#d1543f'); g.addColorStop(1, '#e9c46a');
      x.fillStyle = g; x.fillRect(0, 0, s, s);
      for (let i = 0; i < 26; i++) { x.fillStyle = `rgba(255,255,255,${R() * .3})`; x.beginPath(); x.arc(R() * s, R() * s, 6 + R() * 40, 0, 7); x.fill(); }
    },
    height: null, skipNormal: true
  }
};
function roundRect(x, a, b, w, h, r) { x.beginPath(); x.moveTo(a + r, b); x.arcTo(a + w, b, a + w, b + h, r); x.arcTo(a + w, b + h, a, b + h, r); x.arcTo(a, b + h, a, b, r); x.arcTo(a, b, a + w, b, r); x.closePath(); }

/* ------------------------------- glyph decals ------------------------------- */
const GLYPH = {
  cycle: (x, s) => {
    x.strokeStyle = '#fff'; x.fillStyle = '#fff'; x.lineWidth = s * .055; x.lineCap = 'round';
    const r = s * .19, cy = s * .62;
    x.beginPath(); x.arc(s * .27, cy, r, 0, 7); x.stroke();
    x.beginPath(); x.arc(s * .73, cy, r, 0, 7); x.stroke();
    x.beginPath(); x.moveTo(s * .27, cy); x.lineTo(s * .45, s * .38); x.lineTo(s * .62, cy); x.stroke();
    x.beginPath(); x.moveTo(s * .45, s * .38); x.lineTo(s * .58, s * .3); x.stroke();
    x.beginPath(); x.moveTo(s * .58, s * .3); x.lineTo(s * .64, s * .3); x.stroke();
    x.beginPath(); x.moveTo(s * .27, cy); x.lineTo(s * .4, s * .32); x.lineTo(s * .55, s * .34); x.stroke();
    x.beginPath(); x.arc(s * .5, s * .19, s * .055, 0, 7); x.fill();
  },
  'cycle-arrows': (x, s) => {
    x.fillStyle = '#fff';
    for (const off of [.18, .5]) { x.beginPath(); x.moveTo(s * .2, s * (off + .22)); x.lineTo(s * .5, s * off); x.lineTo(s * .8, s * (off + .22)); x.lineTo(s * .8, s * (off + .3)); x.lineTo(s * .5, s * (off + .08)); x.lineTo(s * .2, s * (off + .3)); x.closePath(); x.fill(); }
  },
  bus: (x, s) => {
    x.fillStyle = '#fff';
    roundRect(x, s * .22, s * .2, s * .56, s * .58, s * .08); x.fill();
    x.fillStyle = 'rgba(0,0,0,.75)';
    x.fillRect(s * .27, s * .27, s * .46, s * .17);
    x.fillStyle = '#fff';
    x.beginPath(); x.arc(s * .34, s * .8, s * .07, 0, 7); x.arc(s * .66, s * .8, s * .07, 0, 7); x.fill();
  },
  'word-bus': (x, s, h) => { x.fillStyle = '#fff'; x.font = `900 ${h * .62}px Inter, system-ui, sans-serif`; x.textAlign = 'center'; x.textBaseline = 'middle'; x.fillText('BUS', s / 2, h / 2); },
  'word-slow': (x, s, h) => { x.fillStyle = '#fff'; x.font = `900 ${h * .6}px Inter, system-ui, sans-serif`; x.textAlign = 'center'; x.textBaseline = 'middle'; x.fillText('SLOW', s / 2, h / 2); },
  arrow: (x, s) => {
    x.fillStyle = '#fff';
    x.beginPath(); x.moveTo(s * .44, s * .82); x.lineTo(s * .44, s * .46); x.lineTo(s * .3, s * .46); x.lineTo(s * .5, s * .16); x.lineTo(s * .7, s * .46); x.lineTo(s * .56, s * .46); x.lineTo(s * .56, s * .82); x.closePath(); x.fill();
  },
  'arrow-turn': (x, s) => {
    x.fillStyle = '#fff';
    x.beginPath(); x.moveTo(s * .44, s * .84); x.lineTo(s * .44, s * .4); x.bezierCurveTo(s * .44, s * .3, s * .5, s * .24, s * .6, s * .24); x.lineTo(s * .66, s * .24); x.lineTo(s * .66, s * .12); x.lineTo(s * .84, s * .3); x.lineTo(s * .66, s * .48); x.lineTo(s * .66, s * .36); x.lineTo(s * .6, s * .36); x.quadraticCurveTo(s * .56, s * .36, s * .56, s * .42); x.lineTo(s * .56, s * .84); x.closePath(); x.fill();
  },
  train: (x, s) => {
    x.fillStyle = '#fff'; x.strokeStyle = '#fff'; x.lineWidth = s * .05;
    roundRect(x, s * .3, s * .18, s * .4, s * .5, s * .1); x.stroke();
    x.beginPath(); x.moveTo(s * .36, s * .34); x.lineTo(s * .64, s * .34); x.stroke();
    x.beginPath(); x.arc(s * .42, s * .74, s * .05, 0, 7); x.arc(s * .58, s * .74, s * .05, 0, 7); x.fill();
    x.font = `900 ${s * .16}px Inter, sans-serif`; x.textAlign = 'center'; x.fillText('RAIL', s * .5, s * .95);
  },
  'keep-clear': (x, s) => {
    x.strokeStyle = 'rgba(255,255,255,.85)'; x.lineWidth = s * .012;
    for (let i = -2; i < 12; i++) { x.beginPath(); x.moveTo(i * s / 8, 0); x.lineTo(i * s / 8 + s, s); x.stroke(); }
  },
  'give-way': (x, s) => { x.fillStyle = '#fff'; x.beginPath(); x.moveTo(s * .1, s * .2); x.lineTo(s * .9, s * .2); x.lineTo(s * .5, s * .85); x.closePath(); x.fill(); },
  hatch: (x, s) => { x.strokeStyle = 'rgba(255,255,255,.9)'; x.lineWidth = s * .03; for (let i = 0; i < 8; i++) { x.beginPath(); x.moveTo(i * s / 6, 0); x.lineTo(i * s / 6 + s, s); x.stroke(); } },
  speed30: (x, s) => { x.strokeStyle = '#fff'; x.lineWidth = s * .05; x.beginPath(); x.arc(s / 2, s / 2, s * .42, 0, 7); x.stroke(); x.fillStyle = '#fff'; x.font = `800 ${s * .38}px Inter, sans-serif`; x.textAlign = 'center'; x.textBaseline = 'middle'; x.fillText('30', s / 2, s / 2 + s * .02); }
};

/** glyph textures are alpha masks — white paint is tinted by material colour */
export function buildGlyphs(size = 256) {
  const out = {};
  for (const [name, draw] of Object.entries(GLYPH)) {
    const wide = name.startsWith('word');
    const c = C(size, wide ? size / 3 : size);
    const x = c.getContext('2d');
    x.clearRect(0, 0, c.width, c.height);
    x.save();
    x.scale(size / c.width, size / c.height);
    draw(x, size, size);
    x.restore();
    const t = new THREE.CanvasTexture(c);
    t.colorSpace = THREE.SRGBColorSpace;
    t.anisotropy = 8;
    out[name] = t;
  }
  return out;
}

/* ------------------------------- surfaces → material set ------------------------------- */
export function buildMaterials(scaleBias = 1) {
  const out = {};
  for (const [key, painter] of Object.entries(PAINTERS)) {
    const size = painter.size || 256;
    const opt = { seed: 4000 + key.length * 97, normalStrength: painter.normalStrength ?? 1.35, halfHeight: painter.half, alphaTest: painter.alpha };
    const cc = C(size), hc = C(size);
    const cx = cc.getContext('2d'), hx = hc.getContext('2d');
    painter.color(cx, size, rnd(opt.seed));
    let maps = {};
    if (painter.height) {
      hx.fillStyle = '#808080'; hx.fillRect(0, 0, size, size);
      painter.height(hx, size, rnd(opt.seed + 7));
      maps = fromHeight(hc, opt.normalStrength, painter.roughFrom || [.42, .95]);
    } else if (painter.skipNormal && painter.height === null) {
      maps = {};
    }
    const map = new THREE.CanvasTexture(cc);
    map.wrapS = map.wrapT = THREE.RepeatWrapping;
    map.colorSpace = THREE.SRGBColorSpace;
    map.anisotropy = 8;
    out[key] = { map, normalMap: maps.normal || null, roughnessMap: maps.rough || null, size, alpha: !!painter.alpha };
  }
  return out;
}

/** procedural equirect environment: sky gradient + sun blob + ground bounce */
export function buildEnvironment(sun = { azimuth: 135, elevation: 32, intensity: 1 }, timeOfDay = 16.5) {
  const w = 512, h = 256;
  const c = C(w, h), x = c.getContext('2d');
  const el = sun.elevation * Math.PI / 180;
  const horizonY = h * .5;
  const day = Math.max(0, Math.sin(el));
  const top = mix([.05, .07, .12], [.13, .3, .62], day);
  const mid = mix([.1, .11, .16], [.55, .7, .88], day);
  const low = mix([.07, .07, .09], [.86, .66, .46], Math.pow(day, .6));
  const g = x.createLinearGradient(0, 0, 0, horizonY);
  g.addColorStop(0, rgb(top)); g.addColorStop(.62, rgb(mid)); g.addColorStop(1, rgb(low));
  x.fillStyle = g; x.fillRect(0, 0, w, horizonY);
  const gr = x.createLinearGradient(0, horizonY, 0, h);
  gr.addColorStop(0, rgb(mix([.05, .05, .06], [.34, .33, .3], day)));
  gr.addColorStop(1, rgb(mix([.02, .02, .03], [.14, .14, .13], day)));
  x.fillStyle = gr; x.fillRect(0, horizonY, w, h - horizonY);
  const sx = ((sun.azimuth + 180) / 360) * w, sy = horizonY - Math.sin(el) * h * .48;
  const sg = x.createRadialGradient(sx, sy, 2, sx, sy, w * .16);
  sg.addColorStop(0, `rgba(255,${230 + day * 20 | 0},${190 + day * 40 | 0},${.9 * sun.intensity})`);
  sg.addColorStop(.25, `rgba(255,${200 * day + 40 | 0},${120 * day + 30 | 0},.32)`);
  sg.addColorStop(1, 'rgba(0,0,0,0)');
  x.fillStyle = sg; x.fillRect(0, 0, w, h);
  const t = new THREE.CanvasTexture(c);
  t.mapping = THREE.EquirectangularReflectionMapping;
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * clamp01(t));
const clamp01 = v => Math.max(0, Math.min(1, v));
const rgb = a => `rgb(${a[0] * 255 | 0},${a[1] * 255 | 0},${a[2] * 255 | 0})`;

/** soft circular shadow / glow sprite used for lamp pools */
export function buildRadial(color = 'rgba(255,240,200,.85)', size = 128) {
  const c = C(size), x = c.getContext('2d');
  const g = x.createRadialGradient(size / 2, size / 2, 1, size / 2, size / 2, size / 2);
  g.addColorStop(0, color); g.addColorStop(.45, 'rgba(255,238,200,.18)'); g.addColorStop(1, 'rgba(0,0,0,0)');
  x.fillStyle = g; x.fillRect(0, 0, size, size);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

export function disposeMaterialSet(set) {
  for (const k in set) { const m = set[k]; for (const p of ['map', 'normalMap', 'roughnessMap']) m[p]?.dispose?.(); }
}
