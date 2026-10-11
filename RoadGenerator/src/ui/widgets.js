// Tiny DOM toolkit + inline icon set (lucide-style strokes) + inspector widgets.

const ICON_PATHS = {
  pointer: '<path d="M4 4l7.07 17 2.51-7.39L21 11.07z"/>',
  move: '<path d="M5 9l-3 3 3 3"/><path d="M9 5l3-3 3 3"/><path d="M15 19l-3 3-3-3"/><path d="M19 9l3 3-3 3"/><path d="M2 12h20"/><path d="M12 2v20"/>',
  road: '<path d="M4 20 10 4"/><path d="M14 4l6 16"/><path d="M12 8v2"/><path d="M12 13v2"/><path d="M12 18v2"/>',
  rail: '<path d="M6 3v18"/><path d="M18 3v18"/><path d="M6 8h12"/><path d="M6 13h12"/><path d="M6 18h12"/>',
  node: '<circle cx="12" cy="12" r="3"/><path d="M12 2v7"/><path d="M12 15v7"/><path d="M2 12h7"/><path d="M15 12h7"/>',
  frame: '<path d="M8 3H5a2 2 0 0 0-2 2v3"/><path d="M21 8V5a2 2 0 0 0-2-2h-3"/><path d="M3 16v3a2 2 0 0 0 2 2h3"/><path d="M16 21h3a2 2 0 0 0 2-2v-3"/>',
  layers: '<path d="m12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83Z"/><path d="m22 17.65-9.17 4.16a2 2 0 0 1-1.66 0L2 17.65"/><path d="m22 12.65-9.17 4.16a2 2 0 0 1-1.66 0L2 12.65"/>',
  eye: '<path d="M2.06 12.35a1 1 0 0 1 0-.7 10.75 10.75 0 0 1 19.88 0 1 1 0 0 1 0 .7 10.75 10.75 0 0 1-19.88 0"/><circle cx="12" cy="12" r="3"/>',
  eyeOff: '<path d="M10.73 5.08A10.43 10.43 0 0 1 12 5c7 0 10 7 10 7a13.16 13.16 0 0 1-1.67 2.68"/><path d="M6.61 6.61A13.5 13.5 0 0 0 2 12s3 7 10 7a9.74 9.74 0 0 0 5.39-1.61"/><line x1="2" x2="22" y1="2" y2="22"/>',
  search: '<circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>',
  settings: '<path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="m4.93 4.93 1.41 1.41"/><path d="m17.66 17.66 1.41 1.41"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="m6.34 17.66-1.41 1.41"/><path d="m19.07 4.93-1.41 1.41"/>',
  car: '<path d="M19 17h2c.6 0 1-.4 1-1v-3c0-.9-.7-1.7-1.5-1.9C18.7 10.6 16 10 16 10s-1.3-1.4-2.2-2.3c-.5-.4-1.1-.7-1.8-.7H5c-.6 0-1.1.4-1.4.9l-1.4 2.9A3.7 3.7 0 0 0 2 12v4c0 .6.4 1 1 1h2"/><circle cx="7" cy="17" r="2"/><path d="M9 17h6"/><circle cx="17" cy="17" r="2"/>',
  train: '<rect width="16" height="16" x="4" y="3" rx="2"/><path d="M4 11h16"/><path d="M12 3v8"/><path d="m8 19-2 3"/><path d="m18 22-2-3"/><path d="M8 15h.01"/><path d="M16 15h.01"/>',
  bridge: '<path d="M2 20h20"/><path d="M4 20V9a8 8 0 0 1 16 0v11"/><path d="M4 13h16"/><path d="M8 20v-7"/><path d="M16 20v-7"/><path d="M12 20v-7"/>',
  paint: '<path d="M18.37 2.63 14 7l-1.59-1.59a2 2 0 0 0-2.82 0L8 7l9 9 1.59-1.59a2 2 0 0 0 0-2.82L17 10l4.37-4.37a2.12 2.12 0 1 0-3-3Z"/><path d="M9 8c-2 3-4 3.5-7 4l8 10c2-1 6-5 6-7"/><path d="M14.5 17.5 4.5 15"/>',
  lamp: '<path d="M8 2h8l4 10H4L8 2Z"/><path d="M12 12v6"/><path d="M8 22v-2c0-1.1.9-2 2-2h4a2 2 0 0 1 2 2v2H8Z"/>',
  cone: '<path d="m7 21 3.5-17h3L17 21"/><path d="M4 21h16"/><path d="M8.6 13h6.8"/><path d="M9.6 8h4.8"/>',
  grid: '<rect width="18" height="18" x="3" y="3" rx="2"/><path d="M3 9h18"/><path d="M3 15h18"/><path d="M9 3v18"/><path d="M15 3v18"/>',
  wand: '<path d="M15 4V2"/><path d="M15 16v-2"/><path d="M8 9h2"/><path d="M20 9h2"/><path d="M17.8 11.8 19 13"/><path d="M15 9h.01"/><path d="M17.8 6.2 19 5"/><path d="m3 21 9-9"/><path d="M12.2 6.2 11 5"/>',
  download: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" x2="12" y1="15" y2="3"/>',
  upload: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" x2="12" y1="3" y2="15"/>',
  trash: '<path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/>',
  play: '<polygon points="6 3 20 12 6 21 6 3"/>',
  pause: '<rect x="14" y="4" width="4" height="16" rx="1"/><rect x="6" y="4" width="4" height="16" rx="1"/>',
  ruler: '<path d="M21.3 15.3a2.4 2.4 0 0 1 0 3.4l-2.6 2.6a2.4 2.4 0 0 1-3.4 0L2.7 8.7a2.41 2.41 0 0 1 0-3.4l2.6-2.6a2.41 2.41 0 0 1 3.4 0Z"/><path d="m14.5 12.5 2-2"/><path d="m11.5 9.5 2-2"/><path d="m8.5 6.5 2-2"/><path d="m17.5 15.5 2-2"/>',
  xray: '<path d="M12 3v18"/><path d="M3 12h18"/><circle cx="12" cy="12" r="9" stroke-dasharray="3 3"/>',
  texture: '<rect width="18" height="18" x="3" y="3" rx="2"/><path d="m3 15 5-5 5 5"/><path d="m13 11 3-3 5 5"/><circle cx="8.5" cy="8.5" r="1.5"/>',
  info: '<circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/>',
  split: '<path d="M16 3h5v5"/><path d="M8 3H3v5"/><path d="M12 22v-8.3a4 4 0 0 0-1.172-2.872L3 3"/><path d="m15 9 6-6"/>',
  reverse: '<path d="m17 2 4 4-4 4"/><path d="M3 11v-1a4 4 0 0 1 4-4h14"/><path d="m7 22-4-4 4-4"/><path d="M21 13v1a4 4 0 0 1-4 4H3"/>',
  undo: '<path d="M3 7v6h6"/><path d="M21 17a9 9 0 0 0-9-9 9 9 0 0 0-6 2.3L3 13"/>',
  redo: '<path d="M21 7v6h-6"/><path d="M3 17a9 9 0 0 1 9-9 9 9 0 0 1 6 2.3l3 2.7"/>',
  tree: '<path d="M12 22v-7"/><path d="M7 15h10l-5-9z"/><path d="M9 9h6l-3-6z"/>',
  ground: '<path d="M2 20h20"/><path d="M4 20c2-6 6-9 8-9s6 3 8 9"/>',
  parking: '<rect width="18" height="18" x="3" y="3" rx="2"/><path d="M9 17V7h4a3 3 0 0 1 0 6H9"/>',
  marking: '<path d="M4 20 10 4"/><path d="M14 4l6 16"/><path d="M12 6v3"/><path d="M12 11v3"/><path d="M12 16v3"/>',
};
export function icon(name, size = 16) {
  const p = ICON_PATHS[name] || ICON_PATHS.info;
  return `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${p}</svg>`;
}

export function el(tag, attrs = {}, children = []) {
  const e = document.createElement(tag);
  for (const k in attrs) {
    const v = attrs[k];
    if (k === "class") e.className = v; else if (k === "html") e.innerHTML = v; else if (k === "text") e.textContent = v;
    else if (k.startsWith("on") && typeof v === "function") e.addEventListener(k.slice(2), v);
    else if (k === "style" && typeof v === "object") Object.assign(e.style, v);
    else if (v !== null && v !== undefined && v !== false) e.setAttribute(k, v === true ? "" : v);
  }
  for (const c of [].concat(children)) if (c !== null && c !== undefined && c !== false) e.append(c.nodeType ? c : document.createTextNode(String(c)));
  return e;
}
export const fmt = (v, d = 1) => (Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(d));

/** Slider field: label, range input, value readout. onChange(value, final) */
export function slider(label, value, min, max, step, onChange, opts = {}) {
  const val = el("span", { class: "val", text: opts.format ? opts.format(value) : fmt(value, step < 1 ? (step < 0.1 ? 2 : 1) : 0) + (opts.unit || "") });
  const input = el("input", { type: "range", class: "slider", min, max, step, value });
  const fire = (final) => { const v = parseFloat(input.value); val.textContent = opts.format ? opts.format(v) : fmt(v, step < 1 ? (step < 0.1 ? 2 : 1) : 0) + (opts.unit || ""); onChange(v, final); };
  input.addEventListener("input", () => fire(false));
  input.addEventListener("change", () => fire(true));
  return el("div", { class: "fieldblock" }, [el("div", { class: "field" }, [el("label", { text: label }), val]), input]);
}
export function toggle(label, value, onChange) {
  const b = el("button", { class: "toggle" + (value ? " on" : ""), onclick: () => { value = !value; b.classList.toggle("on", value); onChange(value); } });
  return el("div", { class: "field" }, [el("label", { text: label }), b]);
}
export function select(label, value, options, onChange) {
  const s = el("select", { class: "sel", onchange: () => onChange(s.value) }, options.map((o) => { const [v, t] = Array.isArray(o) ? o : [o, o]; return el("option", { value: v, text: t, selected: v === value }); }));
  return el("div", { class: "field" }, [el("label", { text: label }), s]);
}
export function number(label, value, step, onChange, opts = {}) {
  const i = el("input", { type: "number", class: "num", step, value: typeof value === "number" ? +value.toFixed(3) : value, min: opts.min, max: opts.max });
  i.addEventListener("change", () => onChange(parseFloat(i.value)));
  return el("div", { class: "field" }, [el("label", { text: label }), i]);
}
export function text(label, value, onChange) {
  const i = el("input", { type: "text", class: "txt", value });
  i.addEventListener("change", () => onChange(i.value));
  return el("div", { class: "field" }, [el("label", { text: label }), i]);
}
export function color(label, rgb, onChange) {
  const hex = "#" + rgb.map((c) => Math.round(Math.max(0, Math.min(1, c)) * 255).toString(16).padStart(2, "0")).join("");
  const i = el("input", { type: "color", class: "swatch", value: hex });
  i.addEventListener("input", () => onChange([1, 3, 5].map((k) => parseInt(i.value.slice(k, k + 2), 16) / 255)));
  return el("div", { class: "field" }, [el("label", { text: label }), i]);
}
export function button(label, onClick, cls = "btn") { return el("button", { class: cls, onclick: onClick, html: label }); }
export function card(title, iconName, children, wide = false) { return el("div", { class: "card" + (wide ? " wide" : "") }, [el("h4", { html: (iconName ? icon(iconName, 13) : "") + `<span>${title}</span>` }), ...children]); }
export function metric(value, unit, caption) { return el("div", {}, [el("div", { class: "metric", html: `${value}<small>${unit || ""}</small>` }), caption ? el("div", { class: "cap", text: caption }) : null]); }
export function chips(options, value, onChange) {
  const wrap = el("div", { class: "chips" });
  for (const o of options) { const [v, t] = Array.isArray(o) ? o : [o, o]; wrap.append(el("button", { class: "chip" + (v === value ? " on" : ""), text: t, onclick: () => { value = v; [...wrap.children].forEach((c) => c.classList.toggle("on", c.textContent === t)); onChange(v); } })); }
  return wrap;
}
export function objHead(eyebrow, title, iconName, onRename) {
  const t = onRename ? el("input", { type: "text", value: title, onchange: (e) => onRename(e.target.value) }) : title;
  return el("div", { class: "obj-head" }, [el("div", { class: "obj-tile", html: icon(iconName, 24) }), el("div", { style: { minWidth: 0 } }, [el("div", { class: "obj-eyebrow", text: eyebrow }), el("div", { class: "obj-title" }, [t])])]);
}
export function download(name, data, type = "application/octet-stream") {
  const blob = data instanceof Blob ? data : new Blob([data], { type });
  const a = el("a", { href: URL.createObjectURL(blob), download: name }); document.body.append(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 500);
}
export function pickFile(accept, cb) { const i = el("input", { type: "file", accept }); i.onchange = () => { const f = i.files[0]; if (!f) return; const r = new FileReader(); r.onload = () => cb(r.result, f.name); r.readAsText(f); }; i.click(); }
