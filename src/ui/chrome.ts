// ---------------------------------------------------------------------------
// Frontier UI / viewport-side chrome — tool rail, brush bar, main toolbar,
// HUD stats (styled after the reference screenshots; sculpt tools are inert
// this milestone by design).
// ---------------------------------------------------------------------------

export interface ChromeActions {
  onNew(): void;
  onSave(): void;
  onOpen(): void;
  onUndo(): void;
  onRedo(): void;
  onWireframe(): void;
  onTextured(): void;
  onSnapshot(): void;
  onAutoCook(): void;
  wireframe: boolean;
  textured: boolean;
  autoCook: boolean;
  setTitle(t: string, dirty: boolean): void;
}

const TOOLS = [
  { icon: '✎', color: '#6fb7ff', tip: 'Sculpt — ships next milestone' },
  { icon: '◇', color: '#ff7b7b', tip: 'Erase — ships next milestone' },
  { icon: '💧', color: '#69d97c', tip: 'Hydraulic brush — ships next milestone' },
  { icon: '⛰', color: '#e8c15a', tip: 'Stamp — ships next milestone' },
  { icon: '✎', color: '#c77dff', tip: 'Crease pen — ships next milestone' },
  { icon: '⧉', color: '#6fb7ff', tip: 'Clone — ships next milestone' },
  { icon: '✥', color: '#e8c15a', tip: 'Grab — ships next milestone' },
  { icon: '◌', color: '#9ad1ff', tip: 'Mask — ships next milestone' },
  { icon: '✳', color: '#ff8f8f', tip: 'Scatter — ships next milestone' },
  { icon: '⛨', color: '#7ddf8a', tip: 'Protect — ships next milestone' },
];

export function buildChrome(side: HTMLElement, actions: ChromeActions): { stats: HTMLElement } {
  side.innerHTML = `
    <div id="tool-rail">
      ${TOOLS.map((t, i2) => `${i2 === 4 || i2 === 7 ? '<div class="sep"></div>' : ''}<button class="inert" data-tip="${t.tip}" style="color:${t.color}">${t.icon}</button>`).join('')}
    </div>
    <div id="main-toolbar" class="pill">
      <button data-act="new" title="New graph">🗎</button>
      <button data-act="open" title="Open graph (.json)">📂</button>
      <button data-act="save" title="Save graph (.json)">💾</button>
      <button data-act="undo" title="Undo (Ctrl+Z)">↩</button>
      <button data-act="redo" title="Redo (Ctrl+Shift+Z)">↪</button>
      <span class="div"></span>
      <button data-act="wire" title="Wireframe">⬢</button>
      <button data-act="tex" title="Splatmap shading">🖵</button>
      <button data-act="snap" title="Snapshot PNG">📷</button>
      <button data-act="auto" title="Auto-cook on edit">⚙</button>
      <span class="title" id="doc-title">Untitled</span>
      <button data-act="clear" class="danger" title="Clear selection / close">✕</button>
    </div>
    <div id="hud-stats"></div>
    <div id="brush-bar" class="disabled">
      <div class="bb-group"><span class="bb-slider" style="--v:24%"></span><span>24</span></div>
      <div class="bb-group">Opacity <span class="bb-slider" style="--v:85%"></span><span>85%</span></div>
      <div class="bb-group">Hardness <span class="bb-slider" style="--v:70%"></span><span>70%</span></div>
      <div class="bb-group">Flow <span class="bb-slider" style="--v:100%"></span><span>100%</span></div>
      <select disabled><option>Normal</option></select>
      <span class="bb-x">✕</span>
    </div>
  `;

  const toolbar = side.querySelector('#main-toolbar')!;
  const syncToggles = () => {
    toolbar.querySelector('[data-act="wire"]')!.classList.toggle('on', actions.wireframe);
    toolbar.querySelector('[data-act="tex"]')!.classList.toggle('on', actions.textured);
    toolbar.querySelector('[data-act="auto"]')!.classList.toggle('on', actions.autoCook);
  };
  syncToggles();

  toolbar.addEventListener('click', (e) => {
    const btn = (e.target as HTMLElement).closest('button') as HTMLElement | null;
    if (!btn) return;
    switch (btn.dataset.act) {
      case 'new': actions.onNew(); break;
      case 'open': actions.onOpen(); break;
      case 'save': actions.onSave(); break;
      case 'undo': actions.onUndo(); break;
      case 'redo': actions.onRedo(); break;
      case 'wire': actions.onWireframe(); syncToggles(); break;
      case 'tex': actions.onTextured(); syncToggles(); break;
      case 'snap': actions.onSnapshot(); break;
      case 'auto': actions.onAutoCook(); syncToggles(); break;
      case 'clear': actions.onNew(); break;
    }
  });

  return { stats: side.querySelector('#hud-stats')! };
}

let toastTimer: number | undefined;
export function toast(msg: string, warn = false, ms = 4200): void {
  let el = document.querySelector('#toast') as HTMLElement | null;
  if (!el) {
    el = document.createElement('div');
    el.id = 'toast';
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.classList.toggle('warn', warn);
  el.style.display = 'block';
  window.clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => { el!.style.display = 'none'; }, ms);
}
