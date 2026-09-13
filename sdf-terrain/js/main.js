// Frontier SDF Terrain Studio — entry point.
import { Studio } from './ui.js';

window.addEventListener('DOMContentLoaded', () => {
  const studio = new Studio();
  window.studio = studio; // handy for console tinkering
  studio.boot().catch((e) => {
    console.error(e);
    const log = document.getElementById('log');
    if (log) log.textContent = 'Boot failed: ' + (e.message || e);
  });
  window.addEventListener('resize', () => studio.editor.resize());
});
