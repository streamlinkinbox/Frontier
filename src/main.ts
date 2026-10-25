// Frontier Fluid demo entry point.
import { Demo } from './demo/Demo';

const canvas = document.getElementById('gl') as HTMLCanvasElement;
const loading = document.getElementById('loading') as HTMLElement;
const fatal = document.getElementById('fatal') as HTMLElement;
const fatalMsg = document.getElementById('fatal-msg') as HTMLElement;

Demo.create(canvas)
  .then((demo) => {
    loading.style.opacity = '0';
    setTimeout(() => loading.remove(), 450);
    demo.start();
  })
  .catch((err: unknown) => {
    console.error(err);
    loading.remove();
    fatal.style.display = 'flex';
    if (err instanceof Error && err.message) {
      fatalMsg.textContent = `${err.message} Frontier Fluid needs WebGPU — try a recent Chrome/Edge (or Safari 18+ / Firefox 141+) with hardware acceleration enabled.`;
    }
  });
