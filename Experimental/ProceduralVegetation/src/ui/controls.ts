/**
 * Small imperative UI toolkit (no framework): section cards, tick-track
 * sliders with an editable mono value, per-level numeric cells with
 * drag-to-scrub, pill selects, switches, steppers and inline icons.
 */

export type Listener = () => void;

export function el<K extends keyof HTMLElementTagNameMap>(tag: K, cls?: string, text?: string): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

// -----------------------------------------------------------------------------
// Icons (stroke glyphs, 24 × 24 viewBox)
// -----------------------------------------------------------------------------

const ICONS: Record<string, string> = {
  caret: 'M6 9l6 6 6-6',
  search: 'M11 4a7 7 0 1 1 0 14 7 7 0 0 1 0-14zM21 21l-4.3-4.3',
  leaf: 'M5 20C5 11 10 5 20 4c-1 10-6 16-15 16zM5 20c3-6 7-10 11-12',
  wind: 'M4 8h10a2.5 2.5 0 1 0-2.5-2.5M4 12h14a2.5 2.5 0 1 1-2.5 2.5M4 16h7a2 2 0 1 1-2 2',
  grid: 'M4 4h16v16H4zM4 10h16M4 16h16M10 4v16M16 4v16',
  quad: 'M4 4h16v16H4zM12 4v16M4 12h16',
  frame: 'M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5',
  camera: 'M4 8h3l2-3h6l2 3h3v11H4zM12 16.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7z',
  download: 'M12 4v11M7 10l5 5 5-5M4 19h16',
  dice: 'M4 7a3 3 0 0 1 3-3h10a3 3 0 0 1 3 3v10a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3zM8.5 8.5h.01M15.5 8.5h.01M12 12h.01M8.5 15.5h.01M15.5 15.5h.01',
  reset: 'M3.5 12a8.5 8.5 0 1 0 2.8-6.3M3.5 4v5h5',
  check: 'M5 12l5 5L20 7',
  x: 'M6 6l12 12M18 6L6 18',
  plus: 'M12 5v14M5 12h14',
  place: 'M12 5a7 7 0 1 1 0 14 7 7 0 0 1 0-14zM12 2v4M12 18v4M2 12h4M18 12h4',
  scatter: 'M6 8h.01M14 6h.01M18 14h.01M8 17h.01M13 13h.01',
  trash: 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v6M14 11v6',
  tree: 'M12 3a6 6 0 0 1 6 6c0 3-2 5-4 5.5V21h-4v-6.5C8 14 6 12 6 9a6 6 0 0 1 6-6z',
  conifer: 'M12 3l6 9h-3l4 6H5l4-6H6zM11 18h2v3h-2z',
  palm: 'M12 21c0-8 1-12 4-15M12 8C9 5 6 5 3 7c3 0 6 1 9 3M12 8c2-4 5-5 9-4-3 1-6 2-9 4M12 10c-2 1-4 3-5 6 2-2 4-3 5-3',
  acacia: 'M12 21v-7M12 14c-6 0-9-2-9-4 3-1 6-1 9-1s6 0 9 1c0 2-3 4-9 4z',
  yucca: 'M10 21h4V6a2 2 0 0 0-4 0zM6 8v4a4 4 0 0 0 4 4M18 6v6a4 4 0 0 1-4 4',
  mountain: 'M3 20l6-11 4 6 3-4 5 9z',
  eye: 'M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12zM12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z',
  sun: 'M12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4',
  turntable: 'M12 4a8 8 0 1 1-7.5 5M4 4v5h5',
  layers: 'M12 3l9 5-9 5-9-5zM3 13l9 5 9-5M3 17l9 5 9-5',
  ground: 'M3 17h18M6 17c0-3 2-5 6-5s6 2 6 5M9 8h.01M15 6h.01',
  grass: 'M4 21c1-6 3-10 6-13M9 21c0-7 2-12 4-16M13 21c1-6 3-11 6-14M17 21c0-5 1-8 3-10M7 21c-1-4-2-7-4-9',
  wheat: 'M12 22V9M12 9c-3 0-5-2-5-5 3 0 5 2 5 5zM12 9c3 0 5-2 5-5-3 0-5 2-5 5zM12 14c-3 0-5-2-5-5 3 0 5 2 5 5zM12 14c3 0 5-2 5-5-3 0-5 2-5 5zM12 19c-3 0-5-2-5-5 3 0 5 2 5 5zM12 19c3 0 5-2 5-5-3 0-5 2-5 5z',
  tussock: 'M12 21v-9M12 12C9 9 6 8 3 9c2 3 5 4 9 3zM12 12c3-3 6-4 9-3-2 3-5 4-9 3zM12 15c-3-1-6 0-8 3 3 0 6-1 8-3zM12 15c3-1 6 0 8 3-3 0-6-1-8-3z',
};

export function icon(name: string, cls = 'i'): SVGSVGElement {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('class', cls);
  const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
  path.setAttribute('d', ICONS[name] ?? ICONS.x);
  svg.append(path);
  return svg;
}

// -----------------------------------------------------------------------------
// Buttons
// -----------------------------------------------------------------------------

export interface ButtonOpts {
  icon?: string;
  kbd?: string;
  title?: string;
  cls?: string;
}

export function button(label: string, onClick: () => void, opts: ButtonOpts = {}): HTMLButtonElement {
  const b = el('button', 'abtn' + (opts.cls ? ' ' + opts.cls : ''));
  if (opts.icon) b.append(icon(opts.icon));
  if (label) b.append(el('span', '', label));
  if (opts.kbd) b.append(el('kbd', '', opts.kbd));
  if (opts.title) b.title = opts.title;
  b.addEventListener('click', onClick);
  return b;
}

/** Square icon-only header button. */
export function iconButton(name: string, title: string, onClick: () => void): HTMLButtonElement {
  const b = el('button', 'hbtn');
  b.title = title;
  b.append(icon(name));
  b.addEventListener('click', onClick);
  return b;
}

// -----------------------------------------------------------------------------
// Section card
// -----------------------------------------------------------------------------

export function section(title: string, parent: HTMLElement, opts: { collapsed?: boolean; hint?: string } = {}): HTMLElement {
  const s = el('div', 'sec' + (opts.collapsed ? ' closed' : ''));
  const h = el('div', 'sec-head');
  h.append(el('span', 'ttl', title));
  if (opts.hint) h.append(el('span', 'hint', opts.hint));
  h.append(icon('caret', 'i car'));
  const body = el('div', 'sec-body');
  s.append(h, body);
  h.addEventListener('click', () => s.classList.toggle('closed'));
  parent.append(s);
  return body;
}

// -----------------------------------------------------------------------------
// Slider (tick track + editable mono value)
// -----------------------------------------------------------------------------

export interface SliderOpts {
  min: number;
  max: number;
  step?: number;
  title?: string;
  unit?: string;
  format?: (v: number) => string;
}

export function slider(
  parent: HTMLElement,
  label: string,
  get: () => number,
  set: (v: number) => void,
  opts: SliderOpts,
  onChange: Listener,
): { refresh: () => void } {
  const ctl = el('div', 'ctl');
  const top = el('div', 'ctl-top');
  const lab = el('span', 'lab', label);
  if (opts.title) lab.title = opts.title;
  const valWrap = el('span', 'v');
  const num = el('input', 'val');
  num.type = 'text';
  num.spellcheck = false;
  valWrap.append(num);
  if (opts.unit) valWrap.append(el('small', '', opts.unit));
  top.append(lab, valWrap);

  const bar = el('div', 'trk');
  bar.tabIndex = 0;
  bar.setAttribute('role', 'slider');
  bar.setAttribute('aria-label', label);
  const ticks = el('div', 'ticks');
  const fill = el('div', 'fill');
  const thumb = el('div', 'thumb');
  bar.append(ticks, fill, thumb);
  ctl.append(top, bar);

  const step = opts.step ?? (opts.max - opts.min) / 200;
  const fmt = opts.format ?? ((v: number) => formatNumber(v, opts.step));
  const snap = (v: number): number => {
    const s = opts.min + Math.round((v - opts.min) / step) * step;
    return roundTo(Math.min(opts.max, Math.max(opts.min, s)), step);
  };
  const refresh = (): void => {
    const v = get();
    const p = Math.min(1, Math.max(0, (v - opts.min) / (opts.max - opts.min)));
    bar.style.setProperty('--p', `${(p * 100).toFixed(3)}%`);
    bar.setAttribute('aria-valuenow', String(v));
    num.value = fmt(v);
  };
  const apply = (v: number): void => {
    set(snap(v));
    refresh();
    onChange();
  };
  const fromClient = (clientX: number): void => {
    const r = bar.getBoundingClientRect();
    const p = Math.min(1, Math.max(0, (clientX - r.left) / Math.max(1, r.width)));
    apply(opts.min + p * (opts.max - opts.min));
  };
  bar.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    bar.setPointerCapture(e.pointerId);
    bar.classList.add('drag');
    bar.focus({ preventScroll: true });
    fromClient(e.clientX);
  });
  bar.addEventListener('pointermove', (e) => {
    if (bar.classList.contains('drag')) fromClient(e.clientX);
  });
  const end = (e: PointerEvent): void => {
    if (!bar.classList.contains('drag')) return;
    bar.classList.remove('drag');
    try {
      bar.releasePointerCapture(e.pointerId);
    } catch {
      /* ignore */
    }
  };
  bar.addEventListener('pointerup', end);
  bar.addEventListener('pointercancel', end);
  bar.addEventListener('keydown', (e) => {
    const mult = e.shiftKey ? 10 : 1;
    if (e.key === 'ArrowRight' || e.key === 'ArrowUp') apply(get() + step * mult);
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') apply(get() - step * mult);
    else if (e.key === 'Home') apply(opts.min);
    else if (e.key === 'End') apply(opts.max);
    else return;
    e.preventDefault();
  });

  const commit = (): void => {
    const v = parseFloat(num.value);
    if (!Number.isNaN(v)) {
      set(v);
      onChange();
    }
    refresh();
  };
  num.addEventListener('change', commit);
  num.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
  });
  attachScrub(num, get, (v) => set(v), step, onChange, refresh);

  parent.append(ctl);
  refresh();
  return { refresh };
}

// -----------------------------------------------------------------------------
// Select / switch / stepper rows
// -----------------------------------------------------------------------------

export function select<T extends string | number>(
  parent: HTMLElement,
  label: string,
  options: { value: T; label: string }[],
  get: () => T,
  set: (v: T) => void,
  onChange: Listener,
): { refresh: () => void } {
  const row = el('div', 'ctl rowctl');
  const lab = el('span', 'lab', label);
  const sel = el('select', 'dd');
  for (const o of options) {
    const op = el('option', '', o.label);
    op.value = String(o.value);
    sel.append(op);
  }
  const refresh = (): void => {
    sel.value = String(get());
  };
  sel.addEventListener('change', () => {
    const raw = sel.value;
    const sample = options[0].value;
    set((typeof sample === 'number' ? Number(raw) : raw) as T);
    onChange();
  });
  row.append(lab, sel);
  parent.append(row);
  refresh();
  return { refresh };
}

export function check(parent: HTMLElement, label: string, get: () => boolean, set: (v: boolean) => void, onChange: Listener): { refresh: () => void } {
  const row = el('div', 'ctl rowctl');
  const lab = el('span', 'lab', label);
  const sw = el('button', 'sw');
  sw.setAttribute('role', 'switch');
  const refresh = (): void => {
    const on = get();
    sw.classList.toggle('on', on);
    sw.setAttribute('aria-checked', String(on));
  };
  sw.addEventListener('click', () => {
    set(!get());
    refresh();
    onChange();
  });
  row.append(lab, sw);
  parent.append(row);
  refresh();
  return { refresh };
}

export interface StepperOpts {
  step?: number;
  min?: number;
  max?: number;
  integer?: boolean;
  title?: string;
}

/** Label + [−  value  +] stepper (drag the value to scrub). */
export function stepper(
  parent: HTMLElement,
  label: string,
  get: () => number,
  set: (v: number) => void,
  opts: StepperOpts,
  onChange: Listener,
): { refresh: () => void; element: HTMLElement } {
  const row = el('div', 'ctl rowctl');
  const lab = el('span', 'lab', label);
  if (opts.title) lab.title = opts.title;
  const stp = el('div', 'stp');
  const dec = el('button', 'b', '−');
  const num = el('input', 'n');
  num.type = 'text';
  num.spellcheck = false;
  const inc = el('button', 'b', '+');
  stp.append(dec, num, inc);
  const step = opts.step ?? 1;
  const clampV = (v: number): number => {
    if (opts.integer) v = Math.round(v);
    if (opts.min !== undefined) v = Math.max(opts.min, v);
    if (opts.max !== undefined) v = Math.min(opts.max, v);
    return v;
  };
  const refresh = (): void => {
    num.value = formatNumber(get(), step);
  };
  const apply = (v: number): void => {
    set(clampV(v));
    refresh();
    onChange();
  };
  dec.addEventListener('click', () => apply(get() - step));
  inc.addEventListener('click', () => apply(get() + step));
  num.addEventListener('change', () => {
    const v = parseFloat(num.value);
    if (!Number.isNaN(v)) apply(v);
    else refresh();
  });
  num.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
  });
  attachScrub(num, get, (v) => set(clampV(v)), step, onChange, refresh);
  row.append(lab, stp);
  parent.append(row);
  refresh();
  return { refresh, element: row };
}

// -----------------------------------------------------------------------------
// Per-level tables
// -----------------------------------------------------------------------------

export function levelsHeader(parent: HTMLElement, labels = ['Trunk', 'Limbs', 'Branches', 'Twigs']): void {
  const h = el('div', 'lv-head');
  h.append(el('span'));
  for (const l of labels) h.append(el('span', '', l));
  parent.append(h);
}

export interface LevelRowOpts {
  step?: number;
  min?: number;
  max?: number;
  title?: string;
  /** Number of enabled columns (levels). */
  enabled?: () => number;
  integer?: boolean;
}

export function levelRow(
  parent: HTMLElement,
  label: string,
  get: () => number[],
  set: (i: number, v: number) => void,
  opts: LevelRowOpts,
  onChange: Listener,
): { refresh: () => void } {
  const row = el('div', 'lv-row');
  const lab = el('span', 'lab', label);
  if (opts.title) lab.title = opts.title;
  row.append(lab);
  const inputs: HTMLInputElement[] = [];
  const step = opts.step ?? 0.01;
  const clampV = (v: number): number => {
    if (opts.integer) v = Math.round(v);
    if (opts.min !== undefined) v = Math.max(opts.min, v);
    if (opts.max !== undefined) v = Math.min(opts.max, v);
    return v;
  };
  for (let i = 0; i < 4; i++) {
    const num = el('input', 'cell');
    num.type = 'text';
    num.spellcheck = false;
    const commit = (): void => {
      const v = parseFloat(num.value);
      if (!Number.isNaN(v)) {
        set(i, clampV(v));
        onChange();
      }
      refresh();
    };
    num.addEventListener('change', commit);
    num.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
    });
    attachScrub(num, () => get()[i], (v) => set(i, clampV(v)), step, onChange, () => refresh());
    inputs.push(num);
    row.append(num);
  }
  const refresh = (): void => {
    const vals = get();
    const n = opts.enabled ? opts.enabled() : 4;
    inputs.forEach((inp, i) => {
      inp.value = formatNumber(vals[i], step);
      inp.disabled = i >= n;
    });
  };
  parent.append(row);
  refresh();
  return { refresh };
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

/** Drag horizontally on a numeric field to scrub its value (DCC convention). */
function attachScrub(
  input: HTMLInputElement,
  get: () => number,
  set: (v: number) => void,
  step: number,
  onChange: Listener,
  refresh: () => void,
): void {
  let startX = 0;
  let startV = 0;
  let dragging = false;
  let moved = false;
  input.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    startX = e.clientX;
    startV = get();
    dragging = true;
    moved = false;
    input.setPointerCapture(e.pointerId);
  });
  input.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const dx = e.clientX - startX;
    if (!moved && Math.abs(dx) < 3) return;
    if (!moved) {
      moved = true;
      input.classList.add('dragging');
      input.blur();
    }
    const mult = e.shiftKey ? 10 : e.altKey ? 0.1 : 1;
    const v = startV + dx * step * mult * 0.5;
    set(roundTo(v, step * (mult < 1 ? 0.1 : 1)));
    refresh();
    onChange();
  });
  const end = (e: PointerEvent): void => {
    if (!dragging) return;
    dragging = false;
    input.classList.remove('dragging');
    try {
      input.releasePointerCapture(e.pointerId);
    } catch {
      /* ignore */
    }
    if (moved) e.preventDefault();
  };
  input.addEventListener('pointerup', end);
  input.addEventListener('pointercancel', end);
}

function roundTo(v: number, step: number): number {
  const d = Math.max(0, Math.min(6, Math.ceil(-Math.log10(step)) + 1));
  return parseFloat(v.toFixed(d));
}

export function formatNumber(v: number, step?: number): string {
  if (v === undefined || v === null || Number.isNaN(v)) return '–';
  if (Number.isInteger(v)) return String(v);
  const d = step ? Math.max(0, Math.min(4, Math.ceil(-Math.log10(step)))) : 2;
  return v.toFixed(d);
}

export function fmtInt(n: number): string {
  return n.toLocaleString('en-US');
}

/** 5,556 → "5.6k", 496,801 → "497k", 1,200,000 → "1.2M". */
export function fmtCompact(n: number): string {
  if (n < 10000) return n.toLocaleString('en-US');
  if (n < 100000) return `${(n / 1000).toFixed(1)}k`;
  if (n < 1000000) return `${Math.round(n / 1000)}k`;
  return `${(n / 1000000).toFixed(2)}M`;
}
