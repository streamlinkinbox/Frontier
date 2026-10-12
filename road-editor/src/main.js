import { Viewer } from './render/scene.js';
import { Editor } from './editor/editor.js';
import { UI } from './ui/ui.js';
import { generateCity } from './core/city.js';

const ui = new UI();
const viewer = new Viewer(document.getElementById('viewport'));
viewer.onFps = (v) => ui.fps(v);
const editor = new Editor({ viewer, ui });
ui.attach(editor);
window.__editor = editor; // handy for debugging in the console

// Cursor text in the status bar is updated from pointer picks; also keep the camera speed visible.
viewer.onSpeed = (v) => ui.toast(`Fly speed ${Math.round(v)} m/s`);

if (!editor.restore()) {
  editor.load(generateCity({ seed: 2026 }));
  viewer.focus(0, 0, 0, 900);
  ui.toast('Welcome: generated a sample city. Press R to draw, or Generate city for a new one.');
}
