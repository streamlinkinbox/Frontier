import * as THREE from 'three';
import { clamp, fbm2D, lerp, mulberry32, ridge2D, smoothstep } from '../math/noise';

export interface MaterialTextureSet {
  map: THREE.DataTexture | THREE.CanvasTexture;
  normalMap: THREE.DataTexture;
  roughnessMap?: THREE.DataTexture;
}

export interface ProceduralTextureLibrary {
  terrainDetail: MaterialTextureSet;
  rock: MaterialTextureSet;
  bark: MaterialTextureSet;
  vineStem: MaterialTextureSet;
  tireTread: MaterialTextureSet;
  fernAtlas: THREE.CanvasTexture;
  broadleafAtlas: THREE.CanvasTexture;
  skyEnvMap: THREE.DataTexture;
  particleSoft: THREE.CanvasTexture;
}

function heightToNormalTexture(
  heights: Float32Array,
  size: number,
  strength: number
): THREE.DataTexture {
  const data = new Uint8Array(size * size * 4);
  const mask = size - 1;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const hL = heights[y * size + ((x - 1) & mask)];
      const hR = heights[y * size + ((x + 1) & mask)];
      const hD = heights[((y - 1) & mask) * size + x];
      const hU = heights[((y + 1) & mask) * size + x];

      const dx = (hL - hR) * strength;
      const dy = (hD - hU) * strength;
      const dz = 1.0;
      const invLen = 1.0 / Math.sqrt(dx * dx + dy * dy + dz * dz);

      const idx = (y * size + x) * 4;
      data[idx] = Math.round((dx * invLen * 0.5 + 0.5) * 255);
      data[idx + 1] = Math.round((dy * invLen * 0.5 + 0.5) * 255);
      data[idx + 2] = Math.round((dz * invLen * 0.5 + 0.5) * 255);
      data[idx + 3] = 255;
    }
  }
  const tex = new THREE.DataTexture(data, size, size, THREE.RGBAFormat);
  tex.wrapS = THREE.RepeatWrapping;
  tex.wrapT = THREE.RepeatWrapping;
  tex.colorSpace = THREE.NoColorSpace;
  tex.needsUpdate = true;
  tex.generateMipmaps = true;
  tex.minFilter = THREE.LinearMipmapLinearFilter;
  tex.magFilter = THREE.LinearFilter;
  return tex;
}

function createTerrainDetailSet(): MaterialTextureSet {
  const size = 256;
  const albedo = new Uint8Array(size * size * 4);
  const rough = new Uint8Array(size * size * 4);
  const heights = new Float32Array(size * size);
  const rng = mulberry32(104729);

  // Tileable domain sampling using periodic frequencies
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const u = (x / size) * 8;
      const v = (y / size) * 8;
      // Seamless blend via periodic wrap approx + fine grain
      const pebbles = ridge2D(u * 3.1, v * 3.1, 3);
      const soil = fbm2D(u * 1.8, v * 1.8, 4) * 0.5 + 0.5;
      const grit = (rng() - 0.5) * 0.14;
      const h = clamp(soil * 0.65 + pebbles * 0.35 + grit, 0, 1);
      heights[y * size + x] = h;

      const idx = (y * size + x) * 4;
      // Rich neutral micro-albedo multiplier (around 0.72..1.18 centered so vertex/shader colors shine cleanly)
      const tone = clamp(0.76 + h * 0.38 + (pebbles - 0.5) * 0.16, 0.45, 1.0);
      albedo[idx] = Math.round(tone * 248);
      albedo[idx + 1] = Math.round(tone * 242);
      albedo[idx + 2] = Math.round(tone * 230);
      albedo[idx + 3] = 255;

      const rVal = Math.round(clamp(0.92 - pebbles * 0.22, 0.35, 0.98) * 255);
      rough[idx] = rVal;
      rough[idx + 1] = rVal;
      rough[idx + 2] = rVal;
      rough[idx + 3] = 255;
    }
  }

  const map = new THREE.DataTexture(albedo, size, size, THREE.RGBAFormat);
  map.wrapS = THREE.RepeatWrapping;
  map.wrapT = THREE.RepeatWrapping;
  map.colorSpace = THREE.SRGBColorSpace;
  map.anisotropy = 8;
  map.generateMipmaps = true;
  map.minFilter = THREE.LinearMipmapLinearFilter;
  map.needsUpdate = true;

  const roughnessMap = new THREE.DataTexture(rough, size, size, THREE.RGBAFormat);
  roughnessMap.wrapS = THREE.RepeatWrapping;
  roughnessMap.wrapT = THREE.RepeatWrapping;
  roughnessMap.generateMipmaps = true;
  roughnessMap.minFilter = THREE.LinearMipmapLinearFilter;
  roughnessMap.needsUpdate = true;

  const normalMap = heightToNormalTexture(heights, size, 3.2);
  normalMap.anisotropy = 8;

  return { map, normalMap, roughnessMap };
}

function createRockTextureSet(): MaterialTextureSet {
  const size = 256;
  const albedo = new Uint8Array(size * size * 4);
  const heights = new Float32Array(size * size);

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const u = (x / size) * 6;
      const v = (y / size) * 6;
      const strata = fbm2D(u * 0.9, v * 3.4 + fbm2D(u, v, 2) * 0.8, 4) * 0.5 + 0.5;
      const cracks = ridge2D(u * 2.5, v * 2.5, 4);
      const mossMask = smoothstep(0.56, 0.78, fbm2D(u * 1.3 + 11.2, v * 1.3 - 4.7, 3) * 0.5 + 0.5);

      const h = clamp(strata * 0.55 + cracks * 0.45, 0, 1);
      heights[y * size + x] = h;

      // Basalt/limestone warm slate blended with tropical lichen/moss
      const rockR = lerp(58, 118, h);
      const rockG = lerp(59, 115, h);
      const rockB = lerp(54, 106, h);

      const mossR = 54;
      const mossG = 86;
      const mossB = 40;

      const idx = (y * size + x) * 4;
      albedo[idx] = Math.round(lerp(rockR, mossR, mossMask * 0.68));
      albedo[idx + 1] = Math.round(lerp(rockG, mossG, mossMask * 0.68));
      albedo[idx + 2] = Math.round(lerp(rockB, mossB, mossMask * 0.68));
      albedo[idx + 3] = 255;
    }
  }

  const map = new THREE.DataTexture(albedo, size, size, THREE.RGBAFormat);
  map.wrapS = THREE.RepeatWrapping;
  map.wrapT = THREE.RepeatWrapping;
  map.colorSpace = THREE.SRGBColorSpace;
  map.anisotropy = 8;
  map.generateMipmaps = true;
  map.minFilter = THREE.LinearMipmapLinearFilter;
  map.needsUpdate = true;

  const normalMap = heightToNormalTexture(heights, size, 4.4);
  return { map, normalMap };
}

function createBarkAndVineSet(isVine: boolean): MaterialTextureSet {
  const size = 256;
  const albedo = new Uint8Array(size * size * 4);
  const heights = new Float32Array(size * size);

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      // Twist u with v for braided liana fiber look on vines
      const twist = isVine ? (y / size) * 2.0 : 0.0;
      const u = ((x / size) + twist) * (isVine ? 10 : 8);
      const v = (y / size) * (isVine ? 4 : 3);

      const fiber = Math.sin(u * Math.PI * 2 + fbm2D(u * 0.8, v * 2.2, 3) * 2.2) * 0.5 + 0.5;
      const barkNoise = fbm2D(u * 2.1, v * 1.4, 4) * 0.5 + 0.5;
      const h = clamp(fiber * 0.62 + barkNoise * 0.38, 0, 1);
      heights[y * size + x] = h;

      const idx = (y * size + x) * 4;
      if (isVine) {
        // Living liana: deep chlorophyll olive-bark with warm woody ridges (modulated per-vine via vertex color!)
        albedo[idx] = Math.round(lerp(112, 215, h));
        albedo[idx + 1] = Math.round(lerp(126, 228, h));
        albedo[idx + 2] = Math.round(lerp(82, 165, h));
      } else {
        // Rainforest buttress tree bark: rich umber-grey with damp moss crevices
        albedo[idx] = Math.round(lerp(56, 118, h));
        albedo[idx + 1] = Math.round(lerp(52, 108, h));
        albedo[idx + 2] = Math.round(lerp(42, 86, h));
      }
      albedo[idx + 3] = 255;
    }
  }

  const map = new THREE.DataTexture(albedo, size, size, THREE.RGBAFormat);
  map.wrapS = THREE.RepeatWrapping;
  map.wrapT = THREE.RepeatWrapping;
  map.colorSpace = THREE.SRGBColorSpace;
  map.anisotropy = 8;
  map.generateMipmaps = true;
  map.minFilter = THREE.LinearMipmapLinearFilter;
  map.needsUpdate = true;

  const normalMap = heightToNormalTexture(heights, size, isVine ? 3.8 : 4.2);
  return { map, normalMap };
}

function createTireTreadSet(): MaterialTextureSet {
  const size = 256;
  const albedo = new Uint8Array(size * size * 4);
  const heights = new Float32Array(size * size);

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const u = x / size; // across tread width
      const v = (y / size) * 18; // around circumference

      // Aggressive mud-terrain directional chevron lugs
      const centerDist = Math.abs(u - 0.5) * 2;
      const phase = v + centerDist * 1.45 * (u > 0.5 ? 1 : -1);
      const lugWave = Math.sin(phase * Math.PI * 2);
      const channel = Math.abs(u - 0.5) < 0.06 || (Math.abs(u - 0.5) > 0.42 && Math.abs(u - 0.5) < 0.50);
      const lug = !channel && lugWave > -0.15 ? 1.0 : 0.15;

      const dust = fbm2D(u * 12, v * 2, 3) * 0.5 + 0.5;
      heights[y * size + x] = lug;

      const idx = (y * size + x) * 4;
      const base = lug > 0.5 ? 38 : 24;
      const mudTint = (1 - lug) * dust * 26;
      albedo[idx] = Math.round(base + mudTint * 1.25);
      albedo[idx + 1] = Math.round(base + mudTint * 0.95);
      albedo[idx + 2] = Math.round(base + mudTint * 0.65);
      albedo[idx + 3] = 255;
    }
  }

  const map = new THREE.DataTexture(albedo, size, size, THREE.RGBAFormat);
  map.wrapS = THREE.RepeatWrapping;
  map.wrapT = THREE.RepeatWrapping;
  map.colorSpace = THREE.SRGBColorSpace;
  map.generateMipmaps = true;
  map.minFilter = THREE.LinearMipmapLinearFilter;
  map.needsUpdate = true;

  const normalMap = heightToNormalTexture(heights, size, 5.0);
  return { map, normalMap };
}

function createFernFrondCanvas(): THREE.CanvasTexture {
  const canvas = document.createElement('canvas');
  canvas.width = 512;
  canvas.height = 512;
  const ctx = canvas.getContext('2d')!;
  ctx.clearRect(0, 0, 512, 512);

  // Central woody-green rachis (stem) from bottom (256, 505) to tip (256, 18)
  ctx.lineCap = 'round';
  const stemGrad = ctx.createLinearGradient(256, 505, 256, 18);
  stemGrad.addColorStop(0, '#354a21');
  stemGrad.addColorStop(0.5, '#4f732d');
  stemGrad.addColorStop(1, '#7ba644');
  ctx.strokeStyle = stemGrad;
  ctx.lineWidth = 7;
  ctx.beginPath();
  ctx.moveTo(256, 508);
  ctx.quadraticCurveTo(252, 260, 256, 16);
  ctx.stroke();

  // Draw 24 pairs of natural pinnate leaflets along the rachis
  const pairs = 26;
  for (let i = 0; i < pairs; i++) {
    const t = i / (pairs - 1); // 0 at base, 1 at tip
    const y = lerp(478, 26, t);
    const envelope = Math.sin(Math.pow(t, 0.72) * Math.PI * 0.94) * (1 - t * 0.18);
    const len = 18 + envelope * 195;
    const width = 7 + envelope * 11.5;

    for (const side of [-1, 1]) {
      const yOffset = side > 0 ? -4 * (1 - t) : 4 * (1 - t);
      const startX = 256;
      const startY = y + yOffset;
      const angle = side * lerp(1.18, 0.48, t); // sweep forward near tip
      const endX = startX + Math.sin(angle) * len;
      const endY = startY - Math.cos(angle) * len * 0.42;
      const ctrlX = (startX + endX) * 0.5 + side * 12 * envelope;
      const ctrlY = (startY + endY) * 0.5 - 10 * envelope;

      const leafGrad = ctx.createLinearGradient(startX, startY, endX, endY);
      leafGrad.addColorStop(0, '#2e521d');
      leafGrad.addColorStop(0.55, '#4b7e2c');
      leafGrad.addColorStop(1, '#79a83c');

      ctx.fillStyle = leafGrad;
      ctx.beginPath();
      ctx.moveTo(startX, startY - width * 0.48);
      ctx.quadraticCurveTo(ctrlX, ctrlY - width * 0.55, endX, endY);
      ctx.quadraticCurveTo(ctrlX, ctrlY + width * 0.52, startX, startY + width * 0.48);
      ctx.closePath();
      ctx.fill();

      // Subtle mid-vein highlight
      ctx.strokeStyle = 'rgba(156, 198, 92, 0.45)';
      ctx.lineWidth = 1.3;
      ctx.beginPath();
      ctx.moveTo(startX, startY);
      ctx.quadraticCurveTo(ctrlX, ctrlY, endX, endY);
      ctx.stroke();
    }
  }

  const tex = new THREE.CanvasTexture(canvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  tex.needsUpdate = true;
  return tex;
}

function createBroadleafCanvas(): THREE.CanvasTexture {
  // Tropical Monstera / Elephant-Ear split broadleaf
  const canvas = document.createElement('canvas');
  canvas.width = 512;
  canvas.height = 512;
  const ctx = canvas.getContext('2d')!;
  ctx.clearRect(0, 0, 512, 512);

  ctx.save();
  const grad = ctx.createRadialGradient(256, 300, 30, 256, 230, 250);
  grad.addColorStop(0, '#2c5420');
  grad.addColorStop(0.65, '#3d722a');
  grad.addColorStop(1, '#5d9338');

  // Heart-shaped tropical blade
  ctx.fillStyle = grad;
  ctx.beginPath();
  ctx.moveTo(256, 445);
  ctx.bezierCurveTo(185, 485, 62, 390, 74, 245);
  ctx.bezierCurveTo(85, 120, 185, 48, 256, 18);
  ctx.bezierCurveTo(327, 48, 427, 120, 438, 245);
  ctx.bezierCurveTo(450, 390, 327, 485, 256, 445);
  ctx.closePath();
  ctx.fill();

  // Carve authentic monstera fenestrations (natural splits) using destination-out
  ctx.globalCompositeOperation = 'destination-out';
  const cuts = [
    [95, 190, 185, 215, 13],
    [76, 275, 192, 290, 15],
    [104, 355, 198, 352, 12],
    [417, 195, 327, 218, 13],
    [436, 280, 320, 292, 15],
    [408, 358, 314, 354, 12],
  ];
  ctx.lineCap = 'round';
  for (const [x0, y0, x1, y1, w] of cuts) {
    ctx.lineWidth = w;
    ctx.beginPath();
    ctx.moveTo(x0, y0);
    ctx.lineTo(x1, y1);
    ctx.stroke();
  }

  ctx.globalCompositeOperation = 'source-over';
  // Petiole & primary lateral veins
  ctx.strokeStyle = '#6ea142';
  ctx.lineWidth = 6;
  ctx.beginPath();
  ctx.moveTo(256, 508);
  ctx.lineTo(256, 28);
  ctx.stroke();

  ctx.lineWidth = 2.6;
  ctx.strokeStyle = 'rgba(142, 188, 86, 0.55)';
  for (let i = 0; i < 6; i++) {
    const y = 115 + i * 52;
    const span = 125 - Math.abs(i - 2.3) * 26;
    ctx.beginPath();
    ctx.moveTo(256, y + 18);
    ctx.quadraticCurveTo(256 - span * 0.5, y, 256 - span, y - 22);
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(256, y + 18);
    ctx.quadraticCurveTo(256 + span * 0.5, y, 256 + span, y - 22);
    ctx.stroke();
  }
  ctx.restore();

  const tex = new THREE.CanvasTexture(canvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  tex.needsUpdate = true;
  return tex;
}

function createSkyEquirectEnv(): THREE.DataTexture {
  // Equirectangular humid jungle canopy + golden afternoon sun reflection map for PBR metallics/wet mud
  const w = 128;
  const h = 64;
  const data = new Uint8Array(w * h * 4);

  for (let y = 0; y < h; y++) {
    const v = y / (h - 1); // 0 bottom, 1 top in DataTexture
    const elev = (v - 0.5) * Math.PI; // -pi/2 nadir to +pi/2 zenith
    for (let x = 0; x < w; x++) {
      const u = x / w;
      const azim = u * Math.PI * 2;

      // Sun direction roughly ahead-left high
      const sx = Math.cos(elev) * Math.cos(azim);
      const sy = Math.sin(elev);
      const sz = Math.cos(elev) * Math.sin(azim);
      const sunDot = clamp(sx * -0.42 + sy * 0.72 + sz * -0.55, 0, 1);
      const sunGlow = Math.pow(sunDot, 16) * 0.9 + Math.pow(sunDot, 3) * 0.32;

      let r: number, g: number, b: number;
      if (elev >= 0) {
        const t = Math.pow(Math.sin(elev), 0.65);
        // Humid golden mist horizon -> tropical canopy sky zenith
        r = lerp(212, 92, t) + sunGlow * 95;
        g = lerp(206, 138, t) + sunGlow * 76;
        b = lerp(176, 168, t) + sunGlow * 40;
      } else {
        const t = clamp(-Math.sin(elev) * 2.2, 0, 1);
        // Warm earth bounce & canopy reflection
        r = lerp(138, 48, t);
        g = lerp(128, 52, t);
        b = lerp(96, 36, t);
      }

      const idx = (y * w + x) * 4;
      data[idx] = clamp(Math.round(r), 0, 255);
      data[idx + 1] = clamp(Math.round(g), 0, 255);
      data[idx + 2] = clamp(Math.round(b), 0, 255);
      data[idx + 3] = 255;
    }
  }

  const tex = new THREE.DataTexture(data, w, h, THREE.RGBAFormat);
  tex.mapping = THREE.EquirectangularReflectionMapping;
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.needsUpdate = true;
  return tex;
}

function createSoftParticleCanvas(): THREE.CanvasTexture {
  const canvas = document.createElement('canvas');
  canvas.width = 128;
  canvas.height = 128;
  const ctx = canvas.getContext('2d')!;
  const grad = ctx.createRadialGradient(64, 64, 4, 64, 64, 60);
  grad.addColorStop(0, 'rgba(255,255,255,0.95)');
  grad.addColorStop(0.42, 'rgba(245,240,228,0.58)');
  grad.addColorStop(1, 'rgba(245,240,228,0.0)');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, 128, 128);

  const tex = new THREE.CanvasTexture(canvas);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.needsUpdate = true;
  return tex;
}

export function createProceduralTextureLibrary(): ProceduralTextureLibrary {
  return {
    terrainDetail: createTerrainDetailSet(),
    rock: createRockTextureSet(),
    bark: createBarkAndVineSet(false),
    vineStem: createBarkAndVineSet(true),
    tireTread: createTireTreadSet(),
    fernAtlas: createFernFrondCanvas(),
    broadleafAtlas: createBroadleafCanvas(),
    skyEnvMap: createSkyEquirectEnv(),
    particleSoft: createSoftParticleCanvas(),
  };
}
