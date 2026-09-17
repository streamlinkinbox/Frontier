import * as THREE from 'three';

function canvasTex(size: number, draw: (ctx: CanvasRenderingContext2D, s: number) => void): THREE.CanvasTexture {
  const c = document.createElement('canvas');
  c.width = c.height = size;
  const ctx = c.getContext('2d')!;
  draw(ctx, size);
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

let wood: THREE.CanvasTexture | null = null;
let plaster: THREE.CanvasTexture | null = null;

/** Subtle vertical wood grain, multiplied by the wood color. */
export function woodTexture(): THREE.CanvasTexture {
  if (wood) return wood;
  wood = canvasTex(256, (ctx, s) => {
    ctx.fillStyle = '#c8c8c8';
    ctx.fillRect(0, 0, s, s);
    for (let i = 0; i < 90; i++) {
      const x = Math.random() * s;
      const w = 1 + Math.random() * 3;
      const shade = 150 + Math.floor(Math.random() * 70);
      ctx.fillStyle = `rgba(${shade},${shade},${shade},0.35)`;
      ctx.fillRect(x, 0, w, s);
    }
    for (let i = 0; i < 26; i++) {
      // grain streaks
      ctx.strokeStyle = `rgba(90,90,90,${0.12 + Math.random() * 0.15})`;
      ctx.lineWidth = 0.8;
      const x = Math.random() * s;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.bezierCurveTo(x + 6, s * 0.3, x - 6, s * 0.6, x + 3, s);
      ctx.stroke();
    }
  });
  return wood;
}

/** Fine stucco noise for plaster walls, multiplied by the wall color. */
export function plasterTexture(): THREE.CanvasTexture {
  if (plaster) return plaster;
  plaster = canvasTex(256, (ctx, s) => {
    ctx.fillStyle = '#f2f2f2';
    ctx.fillRect(0, 0, s, s);
    const img = ctx.getImageData(0, 0, s, s);
    for (let i = 0; i < img.data.length; i += 4) {
      const n = 228 + Math.floor(Math.random() * 27);
      img.data[i] = img.data[i + 1] = img.data[i + 2] = n;
    }
    ctx.putImageData(img, 0, 0);
  });
  return plaster;
}
