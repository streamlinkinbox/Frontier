/* ============================================================
   Frontier Engine — Outliner + Inspector shell
   Two panels. No chrome. Every entity owns its own cards.
   ============================================================ */

const UI = {
  sel: 'sun',
  q: '',
  folders: FOLDERS.reduce((a, f) => (a[f.id] = true, a), {}),
  hover: null
};

/* dependency hooks (cleared on every inspector rebuild) */
let DEPS = [];
let _syncing = false;
function registerDep(fn) { DEPS.push(fn); return fn; }
window.__refreshDependents = (id) => {
  if (_syncing) return;
  _syncing = true;
  DEPS.forEach(f => { try { f(id); } catch (e) { /* one bad card must not kill the rest */ } });
  _syncing = false;
};
window.__refreshTransform = () => { if (window.__paintTransform) window.__paintTransform(); };

/* ============================================================
   OUTLINER
   ============================================================ */
function renderOutliner() {
  const root = document.getElementById('outliner');
  root.innerHTML = '';

  /* --- header --- */
  const hd = h('div', 'pn__hd');
  hd.innerHTML =
    '<span class="pn__ico">' + icon('layers', 15) + '</span>' +
    '<h2>Outliner</h2>' +
    '<span class="pn__cnt">' + ENTITIES.length + '</span>';
  root.appendChild(hd);

  /* --- search --- */
  const sb = h('div', 'search');
  sb.innerHTML = icon('search', 14) + '<input type="text" placeholder="Filter scene…" spellcheck="false"><kbd>⌘F</kbd>';
  const input = sb.querySelector('input');
  input.value = UI.q;
  input.addEventListener('input', () => { UI.q = input.value.toLowerCase(); paintTree(); });
  root.appendChild(sb);

  const tree = h('div', 'tree');
  root.appendChild(tree);

  function paintTree() {
    tree.innerHTML = '';
    let shown = 0;
    FOLDERS.forEach(F => {
      const items = ENTITIES.filter(e => e.folder === F.id).filter(e =>
        !UI.q || e.name.toLowerCase().includes(UI.q) || e.label.toLowerCase().includes(UI.q));
      if (!items.length && UI.q) return;
      shown += items.length;

      const node = h('div', 'fnode');
      const head = h('button', 'fnode__hd' + (UI.q ? ' is-open' : ''));
      head.type = 'button';
      head.innerHTML =
        '<span class="chev">' + icon('chevron', 12) + '</span>' +
        icon(UI.q || UI.folders[F.id] ? 'folderOpen' : 'folder', 14) +
        '<span class="fnode__n">' + F.name + '</span>' +
        '<span class="fnode__c">' + items.length + '</span>';
      head.addEventListener('click', () => {
        if (UI.q) return;
        UI.folders[F.id] = !UI.folders[F.id];
        head.classList.toggle('is-open', UI.folders[F.id]);
        node.querySelector('.fnode__bd').hidden = !UI.folders[F.id];
        click(UI.folders[F.id] ? 1500 : 800, 0.02, 0.007);
      });
      if (UI.folders[F.id] || UI.q) head.classList.add('is-open');
      node.appendChild(head);

      const bd = h('div', 'fnode__bd');
      bd.hidden = !(UI.folders[F.id] || UI.q);
      items.forEach(e => bd.appendChild(entityRow(e)));
      node.appendChild(bd);
      tree.appendChild(node);
    });
    if (!shown) tree.appendChild(h('div', 'tree__empty', 'No entity matches “' + UI.q + '”'));
  }

  function entityRow(e) {
    const row = h('div', 'erow' + (UI.sel === e.id ? ' is-sel' : '') + (e.visible ? '' : ' is-hidden'));
    row.setAttribute('role', 'treeitem');
    row.tabIndex = 0;
    row.style.setProperty('--a', e.accent);
    row.innerHTML =
      '<span class="erow__ic" style="--a:' + e.accent + '">' + icon(e.icon, 14) + '</span>' +
      '<span class="erow__tx"><b>' + e.name + '</b><i>' + e.label + '</i></span>' +
      '<span class="erow__mb" title="' + e.mobility + '">' + e.mobility[0].toUpperCase() + '</span>' +
      '<button class="erow__b" data-a="vis" title="Toggle visibility">' + icon(e.visible ? 'eye' : 'eyeOff', 13) + '</button>' +
      '<button class="erow__b" data-a="lock" title="Toggle lock">' + icon(e.locked ? 'lock' : 'unlock', 13) + '</button>';

    const select = () => {
      if (UI.sel === e.id) return;
      UI.sel = e.id; thunk();
      renderOutliner(); renderInspector();
    };
    row.addEventListener('click', select);
    row.addEventListener('keydown', ev => { if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); select(); } });

    row.querySelectorAll('.erow__b').forEach(b => {
      b.addEventListener('click', ev => {
        ev.stopPropagation();
        const a = b.dataset.a;
        if (a === 'vis') { e.visible = !e.visible; b.innerHTML = icon(e.visible ? 'eye' : 'eyeOff', 13); row.classList.toggle('is-hidden', !e.visible); }
        else { e.locked = !e.locked; b.innerHTML = icon(e.locked ? 'lock' : 'unlock', 13); }
        click(a === 'vis' ? (e.visible ? 1800 : 700) : (e.locked ? 900 : 1600), 0.025, 0.01);
        if (UI.sel === e.id && window.__paintInspectorHead) window.__paintInspectorHead();
      });
    });
    return row;
  }

  paintTree();
}

/* ============================================================
   INSPECTOR
   ============================================================ */
function renderInspector() {
  clearLoops();
  DEPS = [];
  const root = document.getElementById('inspector');
  root.innerHTML = '';
  const e = ent(UI.sel);

  /* --- head --- */
  const hd = h('div', 'pn__hd pn__hd--ent');
  root.appendChild(hd);

  /* --- breadcrumb --- */
  const bc = h('div', 'bc');
  bc.innerHTML = '<span>/Game/Maps/Highland</span><i>' + icon('chevron', 10) + '</i><span>PersistentLevel</span><i>' + icon('chevron', 10) + '</i><b>' + e.id.charAt(0).toUpperCase() + e.id.slice(1) + '</b>';
  root.appendChild(bc);

  /* --- body --- */
  const body = h('div', 'insp');
  root.appendChild(body);

  /* --- transform --- */
  const tf = h('section', 'sect');
  tf.innerHTML = '<div class="sect__hd">' + icon('move', 13) + '<h3>Transform</h3><span class="sect__tag">' + e.mobility + '</span></div>';
  const tfBody = h('div', 'sect__bd');
  tf.appendChild(tfBody);
  body.appendChild(tf);

  const groups = [
    ['Location', 'loc', 'uu', 0],
    ['Rotation', 'rot', '°', 1],
    ['Scale 3D', 'scale', '×', 2]
  ];
  const fields = {};
  groups.forEach(([label, key, unit, dp]) => {
    const row = h('div', 'tfrow');
    row.innerHTML = '<span class="tfrow__l">' + label + '</span>';
    const cells = h('div', 'tfrow__c');
    ['X', 'Y', 'Z'].forEach((ax, i) => {
      const f = h('div', 'nf nf--' + ax.toLowerCase());
      f.innerHTML = '<span class="nf__ax">' + ax + '</span><span class="nf__v"></span>';
      const vEl = f.querySelector('.nf__v');
      const stepFor = dp === 2 ? 0.005 : dp === 1 ? 0.25 : 1;
      let last = 0, crossed = 0;
      bindDrag(f, {
        onStart(p) { last = p.px; crossed = 0; f.classList.add('is-scrub'); },
        onMove(p) {
          const d = p.px - last; last = p.px;
          const before = e.transform[key][i];
          e.transform[key][i] = round(before + d * stepFor, dp);
          if (e.transform[key][i] !== before) {
            vEl.textContent = fmt(e.transform[key][i], dp);
            crossed += Math.abs(d);
            if (crossed > 7) { crossed = 0; tick(); }
          }
        },
        onEnd() { f.classList.remove('is-scrub'); }
      });
      f.title = 'Drag horizontally to scrub ' + label + ' ' + ax;
      cells.appendChild(f);
      fields[key] = fields[key] || [];
      fields[key].push(vEl);
    });
    const u = h('span', 'tfrow__u', unit);
    row.append(cells, u);
    tfBody.appendChild(row);
  });

  window.__paintTransform = () => {
    groups.forEach(([, key, , dp]) => e.transform[key].forEach((v, i) => {
      if (fields[key] && fields[key][i]) fields[key][i].textContent = fmt(v, dp);
    }));
  };
  window.__paintTransform();

  /* --- cards --- */
  const cw = h('section', 'sect sect--cards');
  cw.innerHTML = '<div class="sect__hd">' + icon('sliders', 13) + '<h3>' + e.label + ' — Controls</h3><span class="sect__tag">' + e.cards.length + ' cards</span></div>';
  const grid = h('div', 'cards');
  cw.appendChild(grid);
  body.appendChild(cw);
  e.cards.forEach(c => buildCard(e, c, grid));

  /* one reconcile pass so sibling cards agree after boot-time writes */
  window.__refreshDependents(e.id);

  window.__paintInspectorHead = paintInspectorHead;
  paintInspectorHead();
  root.scrollTop = 0;

  function paintInspectorHead() {
    hd.innerHTML =
      '<span class="ehd__ic" style="--a:' + e.accent + '">' + icon(e.icon, 19) + '</span>' +
      '<span class="ehd__tx"><b>' + e.name + '</b><i>' + e.label + '</i></span>' +
      '<span class="ehd__badge">' + (e.visible ? 'VISIBLE' : 'HIDDEN') + '</span>' +
      '<button class="ib" data-a="vis" title="Toggle visibility">' + icon(e.visible ? 'eye' : 'eyeOff', 15) + '</button>' +
      '<button class="ib" data-a="lock" title="Toggle lock">' + icon(e.locked ? 'lock' : 'unlock', 15) + '</button>';
    hd.querySelectorAll('.ib').forEach(b => {
      b.addEventListener('click', () => {
        const a = b.dataset.a;
        if (a === 'vis') e.visible = !e.visible; else e.locked = !e.locked;
        click(1500, 0.025, 0.01);
        renderOutliner(); paintInspectorHead();
      });
    });
  }
}

function round(v, dp) { const m = Math.pow(10, dp); return Math.round(v * m) / m; }

/* ============================================================
   BOOT
   ============================================================ */
document.addEventListener('keydown', (e) => {
  if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'f') {
    e.preventDefault();
    const i = document.querySelector('#outliner .search input');
    i && i.focus();
  }
  if (e.key === 'Escape') {
    const i = document.querySelector('#outliner .search input');
    if (i && document.activeElement === i) { i.value = ''; UI.q = ''; renderOutliner(); }
  }
});

renderOutliner();
renderInspector();
