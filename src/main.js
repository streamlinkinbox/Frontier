import {
  BRIDGE_NETWORKS,
  installBridgeNetworks,
  auditBridgeNetworks,
} from "./bridge-networks.js";
import { bootStage } from "./boot-status.js";
import {
  installTransportSamples,
  repairSampleCity,
  checkedProject,
} from "./transport-samples.js";
import {
  addRailExit,
  railCrossings,
  corridorPoints,
  crossingIssues,
} from "./transport-geometry.js";
import { addSampleCity, mapBounds, CITY_ID } from "./sample-city.js";
import { modern, streetLayout, LANDSCAPES } from "./landscape-layout.js";
import {
  landscapedProject,
  upgradeStarterRoundabout,
  applyLandscape,
  canInstallLandscapeExamples,
  installLandscapeExamples,
} from "./starter.js";
import { LANDSCAPE_REFERENCES } from "./landscape-references.js";
import {
  STREET_DEFAULTS,
  STREET_PRESETS,
  streetSettings,
  generateStreetscape,
  auditStreetscape,
  exportStreetscapeOBJ,
} from "./streetscape.js";
import "./style.css";
import "./theme.css";
import {
  createIcons,
  MousePointer2,
  Spline,
  Move,
  Mountain,
  Undo2,
  Redo2,
  ChevronDown,
  ChevronRight,
  Plus,
  Minus,
  Search,
  X,
  ArrowUpRight,
  Check,
  Layers,
  Box,
  LayoutGrid,
  Columns2,
  Magnet,
  Scan,
  Map as MapIcon,
  Route,
  Trees,
  TrainFront,
  Waves,
  Building2,
  Settings2,
  Download,
  FolderOpen,
  Save,
  Trash2,
  CircleHelp,
  Maximize,
  Eye,
  EyeOff,
  CheckCheck,
  Command,
  TriangleAlert,
  ArrowRight,
  RotateCcw,
  RotateCw,
  PanelLeftClose,
  GitBranch,
  Link,
  Circle,
  Keyboard,
  Upload,
  Sun,
  SlidersHorizontal,
  Cable,
  Navigation,
  CornerDownLeft,
  Menu,
  Pencil,
  Copy,
  FileJson,
  Package,
  Grip,
  ShieldCheck,
} from "lucide";
import { World } from "./world.js";
import { isWaterChannel } from "./drainage.js";
import {
  PRESETS,
  presetById,
  seedProject,
  nearestSnap,
  addRoad,
  addRoundabout,
  cleanup,
  dist,
  roadWidth,
  validateProject,
  upgradePedestrianSpace,
  moveNode,
} from "./model.js";
import { generateRoadMesh, auditMesh, exportOBJ, roadY } from "./geometry.js";
const icons = {
  MousePointer2,
  Spline,
  Move,
  Mountain,
  Undo2,
  Redo2,
  ChevronDown,
  ChevronRight,
  Plus,
  Minus,
  Search,
  X,
  ArrowUpRight,
  Check,
  Layers,
  Box,
  LayoutGrid,
  Columns2,
  Magnet,
  Scan,
  Map: MapIcon,
  Route,
  Trees,
  TrainFront,
  Waves,
  Building2,
  Settings2,
  Download,
  FolderOpen,
  Save,
  Trash2,
  CircleHelp,
  Maximize,
  Eye,
  EyeOff,
  CheckCheck,
  Command,
  TriangleAlert,
  ArrowRight,
  RotateCcw,
  RotateCw,
  PanelLeftClose,
  GitBranch,
  Link,
  Circle,
  Keyboard,
  Upload,
  Sun,
  SlidersHorizontal,
  Cable,
  Navigation,
  CornerDownLeft,
  Menu,
  Pencil,
  Copy,
  FileJson,
  Package,
  Grip,
  ShieldCheck,
};
const icon = (name, cls = "") => `<i data-lucide="${name}" class="${cls}"></i>`;
const $ = (s) => document.querySelector(s),
  $$ = (s) => [...document.querySelectorAll(s)];
const escape = (s) =>
  String(s).replace(
    /[&<>"']/g,
    (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        c
      ],
  );
const refreshIcons = () =>
  createIcons({ icons, attrs: { "stroke-width": 1.6 } });
await bootStage("Reading your saved project…", 1);
let project,
  welcomeRoundabout = false;
try {
  const saved = localStorage.getItem("frontier-project-v1");
  project = saved ? validateProject(JSON.parse(saved)) : landscapedProject();
  welcomeRoundabout = !saved;
} catch {
  project = landscapedProject();
  welcomeRoundabout = true;
}
upgradePedestrianSpace(project);
await bootStage("Checking the original district…", 2);
const upgradedStarter = upgradeStarterRoundabout(project);
if (upgradedStarter) {
  try {
    localStorage.setItem(
      "frontier-before-landscape-v1",
      JSON.stringify(project),
    );
    project = upgradedStarter;
    welcomeRoundabout = true;
  } catch {
    /* Keep the original if a backup cannot be saved. */
  }
}
project.corridors ||= seedProject().corridors;
let cityPrepared = null,
  cityError = "";
await bootStage("Preparing Eastbank and the built samples…", 3);
if (project.sampleCityVersion !== 1) {
  try {
    localStorage.setItem(
      "frontier-before-second-city-v1",
      JSON.stringify(project),
    );
    cityPrepared = addSampleCity(project);
  } catch (error) {
    cityError = error.message;
  }
}

let transportBefore = null,
  transportNotice = "";
await bootStage(
  "Checking junctions, parking, bridges and railway crossings…",
  4,
);
if (project.sampleCityVersion === 1 && project.transportVersion !== 1) {
  try {
    const snapshot = JSON.stringify(project);
    if (!localStorage.getItem("frontier-before-transport-v1"))
      localStorage.setItem("frontier-before-transport-v1", snapshot);
    let needsRepair = false;
    if (!cityPrepared) {
      try {
        checkedProject(project);
      } catch {
        needsRepair = true;
      }
    }
    const prepared = needsRepair
      ? repairSampleCity(project)
      : installTransportSamples(project);
    cityPrepared = prepared;
    transportBefore = snapshot;
    transportNotice = needsRepair
      ? "Damaged generated Eastbank rebuilt; other roads retained. Undo is available. Download the full pre-repair backup."
      : "Added parking, two bridge routes, rail departures and level crossings. Previous map backed up; Undo is available.";
  } catch (error) {
    cityError = error.message;
    transportNotice =
      "Transport update was not applied: " +
      error.message +
      " Use Repair Eastbank or export your map for a targeted repair.";
  }
}
let networksAdded = false;
if (project.transportVersion === 1 && project.bridgeNetworkVersion !== 1) {
  try {
    await bootStage("Connecting three elevated bridge networks…", 4);
    const snapshot = JSON.stringify(project);
    if (!localStorage.getItem("frontier-before-bridge-networks-v1"))
      localStorage.setItem("frontier-before-bridge-networks-v1", snapshot);
    cityPrepared = installBridgeNetworks(project);
    networksAdded = cityPrepared.added;
    transportBefore ||= snapshot;
    transportNotice +=
      " Three connected bridge networks added. Previous map backed up; Undo is available.";
  } catch (error) {
    transportNotice += " Bridge networks were not added: " + error.message;
  }
}
const state = {
  selected:
    project.edges.find((e) => e.district === CITY_ID && e.tramVehicle)?.id ||
    (welcomeRoundabout
      ? project.edges.find((e) => e.roundaboutRole === "ring")?.id
      : null) ||
    project.edges.find((e) => e.name === "Willow Lane")?.id ||
    project.edges[0]?.id,
  preset: "local",
  tool: "select",
  mode: "3d",
  category: "All",
  libraryTab: "roads",
  rightTab: "properties",
  snap: true,
  drawStart: null,
  roundaboutSettings: { radius: 28, entries: 4, rotation: 0, clockwise: true },
  filter: "",
  history: transportBefore ? [transportBefore] : [],
  future: [],
  actions: [],
  dirty: false,
  coords: { x: 0, z: 0 },
  terrainAmount: 1.5,
};
let world, planWorld, saveTimer, toastTimer, modalFocus;
function roadThumb(p, large = false) {
  if (modern(p)) {
    const l = streetLayout(p),
      x = (u) => 8 + ((u + l.width / 2) / l.width) * 68,
      colors = {
        traffic: "#626965",
        parking: "#858779",
        doorzone: "#dddcc9",
        cycle: "#b16b61",
        buffer: "#8ca676",
        median: "#7f9c68",
        tram: "#97b080",
      };
    return `<svg viewBox="0 0 84 58" class="road-thumb" aria-hidden="true"><rect width="84" height="58" fill="#d2d3c6"/>${l.bands.map((b) => `<rect x="${x(b.lo)}" width="${((b.hi - b.lo) / l.width) * 68}" height="58" fill="${colors[b.kind]}"/>`).join("")}${l.mode === "tram" ? [-2.3675, -0.9325, 0.9325, 2.3675].map((u) => `<path d="M${x(u)} 0v58" stroke="#515958" stroke-width=".65"/>`).join("") : l.mode === "garden" ? [12, 29, 46].map((y) => `<circle cx="42" cy="${y}" r="3.5" fill="#54744d"/>`).join("") : ""}</svg>`;
  }
  if (p.id === "roundabout")
    return `<svg viewBox="0 0 84 58" class="road-thumb" aria-hidden="true"><rect width="84" height="58" fill="#ced4bf"/><path d="M42 0v58M0 29h84" stroke="#eff0de" stroke-width="13"/><path d="M42 0v58M0 29h84" stroke="#66706c" stroke-width="9"/><circle cx="42" cy="29" r="19" fill="#66706c" stroke="#eeeddf" stroke-width="3"/><circle cx="42" cy="29" r="10" fill="#a5b491" stroke="#e8e6d5" stroke-width="2"/><path d="M54 21l1 6 4-3M30 37l-1-6-4 3" fill="none" stroke="#fff6d9" stroke-width="1.6"/></svg>`;
  const lane = p.lanes,
    w = large ? 220 : 84,
    h = large ? 70 : 58,
    roadW = p.id === "alley" ? 28 : p.lanes === 4 ? 49 : 36,
    x = (w - roadW) / 2;
  let marks = "";
  if (p.id !== "trail" && p.id !== "alley") {
    for (let i = 1; i < lane; i++)
      marks += `<path d="M${x + (i * roadW) / lane} 0v${h}" stroke="${p.id === "country" ? "#e6d4a0" : "#ececde"}" stroke-width="1" stroke-dasharray="6 6"/>`;
    marks += `<path d="M${x + 2} 0v${h} M${x + roadW - 2} 0v${h}" stroke="#e9e7d6" stroke-width=".7"/>`;
  }
  return `<svg viewBox="0 0 ${w} ${h}" class="road-thumb ${large ? "large" : ""}" aria-hidden="true"><rect width="${w}" height="${h}" fill="${p.id === "trail" ? "#c7c7a7" : "#ced4bf"}"/><path d="M${x - 3} 0v${h} M${x + roadW + 3} 0v${h}" stroke="${p.sidewalk ? "#efeee1" : "#b7b49c"}" stroke-width="5"/><rect x="${x}" width="${roadW}" height="${h}" fill="${p.id === "trail" ? "#b29c79" : "#66706c"}"/>${marks}${p.bike ? `<path d="M${x + 5} 0v${h} M${x + roadW - 5} 0v${h}" stroke="#8bb5a0" stroke-width="5"/>` : ""}<circle cx="${x - 12}" cy="15" r="6" fill="#9aaa8b"/><circle cx="${x + roadW + 12}" cy="43" r="7" fill="#a7b697"/></svg>`;
}
$("#app").innerHTML = `
<header class="app-header"><a href="#" class="brand" aria-label="Frontier home"><span class="brand-symbol"><b></b><b></b><b></b></span>frontier<span class="brand-dot">®</span></a><span class="header-divider"></span><span class="workspace-name">Road & World Studio <span class="early-access">BETA</span></span><nav class="header-nav"><button id="project-menu">Project ${icon("chevron-down")}</button><button id="help-menu">Help ${icon("chevron-down")}</button></nav><div class="header-end"><button id="theme-toggle" class="theme-toggle" aria-label="Toggle light or dark interface">${icon("sun")} <span>Light UI</span></button><span class="local-badge"><span></span> Local workspace</span><button class="avatar" id="about" aria-label="About this workspace">JD</button></div></header>
<div class="project-bar"><div class="project-breadcrumb">${icon("folder-open")}<span>My projects</span>${icon("chevron-right")}<button id="project-title">${escape(project.name)} ${icon("chevron-down")}</button><span class="saved-indicator" id="saved-label">${icon("check-check")} All changes saved</span></div><div class="project-actions"><div class="history-controls"><button id="undo" class="icon-btn" title="Undo (Ctrl Z)">${icon("undo-2")}</button><button id="redo" class="icon-btn" title="Redo (Ctrl Shift Z)">${icon("redo-2")}</button></div><button id="save" class="button quiet">${icon("save")} Save project</button><button id="export" class="button primary">${icon("download")} Export ${icon("chevron-down")}</button></div></div>
<main class="workspace"><aside class="library"><div class="panel-heading"><h2>Asset library</h2><span class="small-label">${PRESETS.length + 6} ASSETS</span></div><div class="library-tabs"><button data-library="roads" class="active">${icon("route")} Roads</button><button data-library="structures">${icon("cable")} Structures</button></div><div class="search-wrap">${icon("search")}<input id="library-search" placeholder="Search assets..." aria-label="Search assets"/><kbd>/</kbd></div><div class="category-list" id="category-list"></div><div class="library-section-title"><span id="library-label">ROAD PRESETS</span><button class="icon-btn tiny" id="library-info" title="About road presets">${icon("sliders-horizontal")}</button></div><div id="preset-list" class="preset-list"></div><div class="library-bottom"><button class="terrain-card" id="terrain-card"><span class="terrain-illustration">${icon("mountain")}</span><span><strong>Shape the landscape</strong><small>Terrain, water & elevation</small></span>${icon("arrow-up-right")}</button><div class="library-footer">${icon("box")} Parametric. Connected. Yours.</div></div></aside>
<section class="editor-area"><div class="viewport-toolbar"><div class="view-switch"><button data-view="2d">${icon("layout-grid")} 2D plan</button><button data-view="3d" class="active">${icon("box")} 3D world</button><button data-view="split">${icon("columns-2")} Split view</button></div><div class="viewport-options"><button id="mobile-inspector" class="toolbar-button mobile-inspector" title="Toggle properties">${icon("settings-2")}</button><button id="snap" class="toolbar-button enabled" title="Snap to nodes and road segments (G)">${icon("magnet")}<span>Snap</span><span class="keycap">G</span></button><span class="toolbar-divider"></span><button id="overlays" class="toolbar-button">${icon("layers")}<span>Overlays</span>${icon("chevron-down")}</button></div></div>
<div class="street-tour"><span>SAMPLE CITIES</span><button id="show-eastbank">${icon("building-2")} Eastbank · 2nd city</button><button id="show-westhaven">Westhaven</button><label class="sample-picker-label">Find a sample<select id="city-sample" aria-label="Find a built sample">${sampleOptions()}</select></label><button data-tour="roundabout">${icon("rotate-cw")} Roundabout</button><button data-tour="garden">${icon("trees")} Garden</button><button data-tour="cycle">${icon("route")} Cycle</button><button data-tour="tram">${icon("train-front")} Tram</button><button data-tour="parking">Parking</button><button id="tour-networks">Bridge networks · 3</button><button id="tour-bridge">Single bridges</button><button id="tour-crossing">Rail crossing</button><button id="rail-exit-tool">Rail exits ↗</button><button id="repair-eastbank">Repair Eastbank</button><button id="transport-backup">Backup ↓</button><button id="landscape-references">References ${icon("arrow-up-right")}</button></div>${transportNotice ? `<div class="transport-notice" role="status">${escape(transportNotice)}</div>` : ""}<div class="viewports"><div id="world-view" class="world-view"><div class="scene-topline"><div class="scene-title"><span class="live-dot"></span> ${project.sampleCityVersion === 1 ? "WESTHAVEN + EASTBANK <span>/</span> TWO-CITY MAP" : escape(project.name)}</div><div class="render-mode"><span id="perspective-label">Perspective</span><button id="camera-navigation" title="Switch between Unreal-style fly and orbit navigation">Fly camera</button><select id="camera-speed" aria-label="Fly camera movement speed"><option value="3">3 m/s</option><option value="10">10 m/s</option><option value="25" selected>25 m/s</option><option value="80">80 m/s</option></select></div></div><div class="scene-labels" id="scene-labels"></div><div class="tool-palette"><button data-tool="select" class="active" title="Select road (V)">${icon("mouse-pointer-2")}</button><button data-tool="draw" title="Draw road (R)">${icon("spline")}</button><button data-tool="bridge" title="Draw bridge (B)">${icon("cable")}</button><span></span><button data-tool="terrain" title="Terrain brush (T)">${icon("mountain")}</button><button data-tool="pan" title="Pan (H)">${icon("move")}</button><span></span><button data-tool="delete" title="Delete road (D)">${icon("trash-2")}</button></div><div class="compass"><span>N</span><div class="compass-diamond">${icon("navigation")}</div><small>Z</small></div><div class="viewport-hint" id="viewport-hint"><span class="hint-icon">${icon("mouse-pointer-2")}</span><span>Select a road · drag its endpoints to reshape</span></div><div class="mini-map" id="mini-map"><div class="mini-map-title">${icon("map")} REGION OVERVIEW<button id="minimap-toggle" title="Open split view">${icon("maximize")}</button></div><svg id="mini-map-svg" viewBox="-290 -235 580 470"></svg><div class="mini-map-foot"><span class="live-dot"></span> Westhaven County <span>1 : 2,500</span></div></div><div class="scene-bottom"><div class="scene-scale"><span></span>50 m</div><div class="navigation-hint"><kbd>RMB + WASD</kbd> Fly <span>·</span><kbd>Q / E</kbd> Height <span>·</span><kbd>Shift</kbd> Fast</div><div class="zoom-controls"><button id="zoom-out" title="Zoom out">${icon("minus")}</button><button id="zoom-level" title="Reset view">100%</button><button id="zoom-in" title="Zoom in">${icon("plus")}</button><span></span><button id="fit-view" title="Fit project to view (F)">${icon("scan")}</button></div></div><div class="drawing-card hidden" id="drawing-card"></div></div><div id="plan-view" class="world-view plan-view hidden"><div class="plan-title">${icon("layout-grid")} LINKED 2D PLAN<span>Live sync</span></div></div></div>
<div class="editor-infobar"><div><span class="green-dot"></span><span id="network-state">Network connected</span><span class="info-separator"></span><span id="road-count"></span><span class="info-separator"></span><span id="intersection-count"></span></div><button id="topology-footer">${icon("git-branch")} Quad surface ${icon("arrow-up-right")}</button></div></section>
<aside class="inspector"><div class="inspector-tabs"><button data-inspector="properties" class="active">Properties</button><button data-inspector="layers">Layers</button><button data-inspector="history">History</button></div><div id="inspector-content"></div><div class="inspector-bottom"><span class="connection-light"></span> 2D & 3D always in sync ${icon("link")}</div></aside></main>
<footer class="status-bar"><span><span class="status-logo">f</span> FRONTIER STUDIO <span class="status-version">v0.1</span></span><div><span id="cursor-position">X 0.00 <i>Y 0.00</i> Z 0.00</span><span class="status-divider"></span><span>Units: meters</span><span class="status-divider"></span><button id="shortcuts">${icon("keyboard")} Shortcuts <kbd>?</kbd></button></div></footer>
<div id="toast" role="status"></div><div id="popover" class="popover hidden"></div><div id="modal-root"></div><input type="file" id="import-file" accept="application/json,.json" hidden/>`;

function setTheme(theme, persist = true) {
  document.documentElement.dataset.theme = theme;
  const dark = theme === "dark",
    button = $("#theme-toggle");
  if (button) {
    button.setAttribute("aria-pressed", String(dark));
    button.setAttribute(
      "aria-label",
      dark ? "Switch to light interface" : "Switch to dark interface",
    );
    button.querySelector("span").textContent = dark ? "Light UI" : "Dark UI";
  }
  if (persist)
    try {
      localStorage.setItem("frontier-theme-v1", theme);
    } catch {}
  for (const view of [world, planWorld])
    if (view) {
      view.scene.background.set(dark ? "#1c1c1c" : "#e7eae4");
      view.needsRender = true;
    }
}
function renderLibrary() {
  const structure = state.libraryTab === "structures";
  $("#category-list").innerHTML = (
    structure
      ? ["All", "Bridges", "Rail"]
      : ["All", "Urban", "Rural", "Off-road"]
  )
    .map(
      (c) =>
        `<button data-category="${c}" class="${state.category === c ? "active" : ""}">${c}</button>`,
    )
    .join("");
  $("#library-label").textContent = structure
    ? "STRUCTURE PRESETS"
    : "ROAD PRESETS";
  if (structure) {
    const assets = [
      {
        id: "bridge-local",
        name: "Local road bridge",
        sub: "2 lanes · concrete deck",
        icon: "cable",
        category: "Bridges",
      },
      {
        id: "bridge-avenue",
        name: "Avenue bridge",
        sub: "4 lanes · cycle lanes",
        icon: "cable",
        category: "Bridges",
      },
      {
        id: "bridge-country",
        name: "Rural crossing",
        sub: "2 lanes · safety barriers",
        icon: "cable",
        category: "Bridges",
      },
      {
        id: "rail",
        name: "Railway track",
        sub: "Dual track · ground level",
        icon: "train-front",
        category: "Rail",
      },
      {
        id: "monorail",
        name: "Monorail viaduct",
        sub: "Single beam · elevated",
        icon: "route",
        category: "Rail",
      },
      {
        id: "ramps",
        name: "Curbs, ramps & drainage",
        sub: "9 reference-inspired streetscapes",
        icon: "git-branch",
        category: "Bridges",
      },
    ];
    $("#preset-list").innerHTML =
      assets
        .filter(
          (a) =>
            (state.category === "All" || state.category === a.category) &&
            a.name.toLowerCase().includes(state.filter),
        )
        .map(
          (a) =>
            `<button class="structure-card" data-structure="${a.id}"><span class="structure-thumb">${icon(a.icon)}</span><strong>${a.name}</strong><small>${a.sub}</small>${icon("plus")}</button>`,
        )
        .join("") || '<div class="empty-results">No matching assets.</div>';
  } else {
    const presets = PRESETS.filter(
      (p) =>
        (state.category === "All" ||
          (state.category === "Urban" &&
            ["Residential", "Urban", "Highway"].includes(p.type)) ||
          (state.category === "Rural" && p.type === "Rural") ||
          p.type === state.category) &&
        p.name.toLowerCase().includes(state.filter),
    );
    $("#preset-list").innerHTML =
      presets
        .map(
          (p) =>
            `<button class="preset-card ${state.preset === p.id ? "active" : ""}" data-preset="${p.id}"><div class="preset-image">${roadThumb(p)}<span class="preset-draw">${icon("plus")}</span></div><div class="preset-info"><strong>${p.name}</strong><span>${p.lanes} ${p.lanes === 1 ? "lane" : "lanes"}<b>·</b>${roadWidth(p).toFixed(1)} m</span><small>${p.type}</small></div>${state.preset === p.id ? `<span class="preset-selected">${icon("check")}</span>` : ""}</button>`,
        )
        .join("") ||
      '<div class="empty-results">No matching roads.<br>Try a different search.</div>';
  }
  refreshIcons();
}
function toggleRow(label, key, value, sub = "") {
  return `<label class="toggle-row"><span>${label}${sub ? `<small>${sub}</small>` : ""}</span><input type="checkbox" data-property="${key}" ${value ? "checked" : ""}/><span class="toggle"></span></label>`;
}
function crossSection(e) {
  if (modern(e)) {
    const l = streetLayout(e),
      x = (u) => 12 + ((u + l.width / 2) / l.width) * 220,
      colors = {
        traffic: "#626965",
        parking: "#858779",
        doorzone: "#dddcc9",
        cycle: "#b16b61",
        buffer: "#8ca676",
        median: "#7f9c68",
        tram: "#97b080",
      };
    return `<svg class="cross-section" viewBox="0 0 244 76"><rect x="5" y="23" width="234" height="23" fill="#c8c8be"/>${l.bands.map((b) => `<rect x="${x(b.lo)}" y="27" height="19" width="${((b.hi - b.lo) / l.width) * 220}" fill="${colors[b.kind]}"/>`).join("")}${l.mode === "tram" ? [-2.3675, -0.9325, 0.9325, 2.3675].map((u) => `<path d="M${x(u)} 27v19" stroke="#343c39" stroke-width="1"/>`).join("") : ""}<text x="122" y="63" text-anchor="middle" fill="currentColor" font-size="9">${l.width.toFixed(1)} m · ${l.median ? l.median + " m reservation" : "2 m cycle tracks"}</text></svg>`;
  }
  const width = roadWidth(e),
    lanes = e.lanes;
  let lines = "";
  const start = 25,
    end = 217,
    len = end - start;
  for (let i = 0; i < lanes; i++) {
    const x = start + (len * (i + 0.5)) / lanes;
    lines += `<path d="M${x} 40v-9m-3 3 3-3 3 3" transform="rotate(${e.networkFlow === "forward" || i < lanes / 2 ? 0 : 180} ${x} 35.5)" fill="none" stroke="#eaeadd" stroke-width="1.3"/>`;
    if (i > 0)
      lines += `<path d="M${start + (len * i) / lanes} 24v20" stroke="#d3c594" stroke-dasharray="3 3"/>`;
  }
  return `<svg class="cross-section" viewBox="0 0 244 76"><path d="M11 44h222v7H11z" fill="#d3d2c7"/><path d="M24 23h194v23H24z" fill="#626c66"/><path d="M12 20h12v26H12zm206 0h14v26h-14z" fill="#babfad"/>${lines}<path d="M24 61h194m-194-3v6m194-6v6" stroke="#b7b9b0"/><rect x="91" y="54" width="62" height="15" fill="#f2f4ee"/><text x="122" y="65" text-anchor="middle" fill="#7a8176" font-size="9" font-family="Inter, sans-serif">${width.toFixed(1)} m roadway</text></svg>`;
}
function selectedRoad() {
  return project.edges.find((e) => e.id === state.selected);
}
function renderInspector() {
  const content = $("#inspector-content");
  $$("[data-inspector]").forEach((b) =>
    b.classList.toggle("active", b.dataset.inspector === state.rightTab),
  );
  if (state.rightTab === "layers") {
    content.innerHTML = `<div class="inspector-section"><div class="eyebrow">SCENE COLLECTION</div><h3>World layers</h3><p class="muted-copy">Control visibility in both views. Hidden layers remain in your project.</p>${[
      ["roads", "Road network", "route"],
      ["markings", "Road markings", "git-branch"],
      ["buildings", "Building massing", "building-2"],
      ["vegetation", "Trees & landscape", "trees"],
      ["water", "River & lake", "waves"],
      ["rail", "Rail, tram & monorail", "train-front"],
    ]
      .map(
        ([id, label, i]) =>
          `<div class="layer-row">${icon(i)}<span>${label}</span><button data-layer="${id}" title="Toggle ${label}" class="icon-btn ${world.layers[id].visible ? "" : "muted"}">${icon(world.layers[id].visible ? "eye" : "eye-off")}</button></div>`,
      )
      .join(
        "",
      )}</div><div class="inspector-section"><div class="eyebrow">DISPLAY</div>${toggleRow("Quad wireframe", "wireframe", world.options.wireframe)}<p class="muted-copy">Shows the exported quad edges, not the renderer’s internal triangles.</p></div>`;
    refreshIcons();
    return;
  }
  if (state.rightTab === "history") {
    content.innerHTML = `<div class="inspector-section"><div class="eyebrow">THIS SESSION</div><h3>Project history</h3><p class="muted-copy">Undo and redo your road and terrain edits.</p><div class="history-list">${
      [...state.actions]
        .reverse()
        .slice(0, 30)
        .map(
          (a, i) =>
            `<div>${icon(i === 0 ? "circle" : "check")}<span>${escape(a)}</span><small>${i === 0 ? "Latest" : ""}</small></div>`,
        )
        .join("") ||
      `<div>${icon("folder-open")}<span>Opened ${escape(project.name)}</span></div>`
    }</div></div>`;
    refreshIcons();
    return;
  }
  if (["rail", "monorail"].includes(state.tool)) {
    content.innerHTML = `<div class="selection-heading"><span class="selection-kind">RAIL CORRIDOR TOOL</span><h2>${state.tool === "rail" ? "Railway track" : "Monorail viaduct"}</h2><p>Click to draw a connected alignment.</p></div><div class="inspector-section"><div class="eyebrow">STRUCTURE PROFILE</div><div class="property-stat"><span>System</span><strong>${state.tool === "rail" ? "Dual track" : "Single beam"}</strong></div><div class="property-stat"><span>Ground clearance</span><strong>${state.tool === "rail" ? "At grade" : "8 m"}</strong></div><p class="muted-copy">Place successive points in either view. Esc finishes the alignment. Undo removes the last section. Corridors are saved in project JSON, but are not included in the road-surface OBJ.</p><p class="muted-copy">Rail is a layout preview: turnouts, crossing signals, track junctions and production topology are not generated.</p></div>`;
    refreshIcons();
    return;
  }
  if (state.tool === "terrain") {
    content.innerHTML = `<div class="selection-heading"><span class="selection-kind">TERRAIN TOOL</span><h2>Shape the landscape</h2><p>Road elevations follow the terrain.</p></div><div class="inspector-section"><div class="eyebrow">SCULPT BRUSH</div><label class="field-label">Brush operation</label><div class="segmented"><button data-brush="raise" class="${state.terrainAmount > 0 ? "active" : ""}">Raise</button><button data-brush="lower" class="${state.terrainAmount < 0 ? "active" : ""}">Lower</button></div><div class="property-stat"><span>Radius</span><strong>24 m</strong></div><div class="property-stat"><span>Strength per click</span><strong>1.5 m</strong></div><p class="muted-copy">Click the landscape to sculpt. Hold Shift to invert the brush. Both views and road meshes rebuild together.</p></div><div class="inspector-section"><div class="eyebrow">WATER SYSTEM</div><p class="muted-copy">Willow River and Quarry Lake are included in this project. Waterway editing is not available in this version.</p><button class="button full" id="show-water">${icon("waves")} Toggle water visibility</button></div>`;
    refreshIcons();
    return;
  }
  const e = selectedRoad();
  if (!e) {
    content.innerHTML = `<div class="empty-inspector">${icon("mouse-pointer-2")}<h3>A world of possibilities.</h3><p>Select a road in either view to inspect its profile, lanes and connections.</p><button class="button" id="start-draw">${icon("spline")} Draw your first road</button></div>`;
    refreshIcons();
    return;
  }
  const a = project.nodes.find((n) => n.id === e.a),
    b = project.nodes.find((n) => n.id === e.b),
    audit = auditMesh(world.result, project),
    roadQuads = world.result.mesh.groups.filter((id) => id === e.id).length,
    degrees = [a, b].map(
      (n) => project.edges.filter((r) => r.a === n.id || r.b === n.id).length,
    );
  const junctionName = (d) =>
    d === 1
      ? "Open end"
      : d === 2
        ? "Continuous"
        : d === 3
          ? "3-way junction"
          : `${d}-way junction`;
  content.innerHTML = `<div class="selection-heading"><div class="selection-kind"><span>ROAD SEGMENT</span><button class="icon-btn tiny" id="focus-road" title="Focus selected road">${icon("scan")}</button></div><div class="selected-name"><input id="road-name" aria-label="Road name" value="${escape(e.name)}"/>${icon("pencil")}</div><div class="road-meta"><span class="road-type-dot"></span>${escape(presetById(e.preset).name)}<span class="id-tag">${escape(e.id.toUpperCase())}</span></div></div>
 <div class="inspector-section road-profile"><div class="section-heading"><span>ROAD PROFILE</span><span class="parametric-tag">Parametric</span></div><div class="profile-preview">${crossSection(e)}</div><div class="property-stat"><span>Segment length</span><strong>${dist(a, b).toFixed(1)} <small>m</small></strong></div><div class="field-row"><label>Traffic lanes</label><div class="stepper"><button data-step="-1" aria-label="Remove lane" ${e.lanes <= (modern(e) ? 2 : 1) ? "disabled" : ""}>${icon("minus")}</button><span>${e.lanes}</span><button data-step="1" aria-label="Add lane" ${e.lanes >= 6 ? "disabled" : ""}>${icon("plus")}</button></div></div><div class="field-row"><label for="lane-width">Lane width</label><div class="number-field"><input id="lane-width" type="number" min="2.5" max="${e.roundaboutRole === "ring" ? 6 : 4.5}" step="0.1" value="${e.laneWidth.toFixed(1)}"/><span>m</span></div></div><div class="field-row"><label for="road-speed">Design speed</label><div class="number-field"><input id="road-speed" type="number" min="10" max="130" step="5" value="${e.speed}"/><span>km/h</span></div></div></div>
 <div class="inspector-section"><div class="section-heading"><span>SURFACE & DETAILS</span>${icon("settings-2")}</div>${toggleRow("Sidewalks", "sidewalk", e.sidewalk > 0, e.sidewalk ? `${e.sidewalk.toFixed(1)} m · both sides` : "")}${toggleRow("Protected bike lanes", "bike", e.bike)}${toggleRow("Road markings", "markings", e.markings)}${toggleRow("Bridge structure", "bridge", e.structure === "bridge")}</div>
 ${
   !e.roundabout
     ? `<section class="inspector-section"><div class="eyebrow">LANDSCAPE & MOBILITY</div><label class="field-label" for="landscape-layout">Street cross-section</label><select id="landscape-layout" class="landscape-select">${Object.entries(
         LANDSCAPES,
       )
         .map(
           ([key, v]) =>
             `<option value="${key}" ${(e.landscape ?? "standard") === key ? "selected" : ""}>${v.name}</option>`,
         )
         .join(
           "",
         )}</select><p class="muted-copy">${e.landscape === "parking" ? "2.2 m parallel bays · 0.9 m clear door buffer · 2 m cycle tracks. Bays stop 10 m before the trimmed junction mouth. Parked cars are static previews." : modern(e) ? `${streetLayout(e).median ? streetLayout(e).median + " m central reservation · " : ""}${e.bike ? "2 m cycle tracks + 1.2 m planted buffers per side. " : ""}Crossing paths stay clear. Trees and rails occupy reserved space, not traffic lanes.` : "Choose a researched landscape profile. Applying a profile changes the road width and is checked against adjoining corners."}</p>${e.landscape === "tram" ? '<p class="muted-copy">Twin-track alignment preview. Straight-through junctions connect. Use Rail exits to route an open end off the road on a smooth curve. Track switches and vehicle simulation are not generated.</p>' : ""}<button id="landscape-references-inspector" class="street-preset-link">Research & reference images ${icon("arrow-up-right")}</button></section>`
     : ""
 }
 ${e.roundabout ? `<section class="inspector-section"><div class="eyebrow">ROUNDABOUT ASSEMBLY</div><div class="property-stat"><span>Circulation</span><strong>${e.circulation}</strong></div><p class="muted-copy">Filleted ring, grass center and welded entry junctions. Connect regular roads to the outer endpoints. Ring segments remain editable; deleting a ring segment removes its whole assembly.</p><button class="button full" id="frame-roundabout">Frame roundabout</button></section>` : ""}
 ${e.bridgeNetwork ? `<section class="inspector-section"><div class="eyebrow">CONNECTED BRIDGE NETWORK</div><p class="muted-copy">${escape(BRIDGE_NETWORKS.find((n) => n.id === e.bridgeNetwork)?.description || "Shared ground-level approach road.")}</p><p class="muted-copy">${e.structure === "bridge" ? `Deck heights: ${roadY(project, e, 0).toFixed(2)} → ${roadY(project, e, 1).toFixed(2)} m (world datum). ${e.networkFlow === "forward" ? "One-way circulation." : "Two-way carriageway."}` : "Ground-level connection."} Fixed deck elevations are retained when dragging nodes. Edits that exceed the ramp-grade or crossing-clearance limits are rejected.</p><button class="button" data-network="${e.bridgeNetwork}">Frame interchange</button></section>` : ""}
 ${streetscapePanel(e)}
 <div class="inspector-section junction-section"><div class="section-heading"><span>CONNECTIONS</span><span class="auto-tag">${icon("git-branch")} Auto-merge</span></div><div class="connection-row"><span class="node-dot"></span><span>Start node</span><strong>${junctionName(degrees[0])}</strong></div><div class="connection-row"><span class="node-dot"></span><span>End node</span><strong>${junctionName(degrees[1])}</strong></div><div class="connection-note">${icon("link")} Merged connected junctions. Grade-separated crossings remain independent.</div></div>
 <button id="topology-card" class="topology-card ${audit.issues.length ? "warning" : ""}"><span class="topology-icon">${icon(audit.issues.length ? "triangle-alert" : "shield-check")}</span><span><strong>${audit.issues.length ? "Review road topology" : "Road surface checks passed"}</strong><small>${roadQuads} segment quads · ${audit.degenerate} degenerate faces</small></span>${icon("arrow-up-right")}</button><div class="inspector-actions"><button id="inspect-wireframe">${icon("git-branch")} Inspect quad flow</button><button id="delete-selected" title="Delete selected segment">${icon("trash-2")}</button></div>`;
  refreshIcons();
}
function streetscapePanel(e) {
  if (!e.sidewalk)
    return `<section class="inspector-section"><div class="eyebrow">CONNECTED ROAD EDGES</div><p class="muted-copy">${e.bridgeNetwork ? "0.9 m concrete safety shoulders and continuous barriers follow the elevated deck. These are not pedestrian footways." : "0.9 m soft shoulders follow the junction curves."} Adjoining paved corners return into the shoulder with a graded termination instead of a gap.</p><button id="street-audit" class="street-preset-link">Inspect edge connections ${icon("arrow-up-right")}</button></section>`;
  const cfg = streetSettings(e),
    ramps = world.streetscape?.ramps.filter((r) => r.road === e.id) || [],
    steep = ramps.some((r) => r.grade > 1 / 12 + 0.0001);
  const select = (key, label, choices) =>
    `<div class="field-row"><label for="street-${key}">${label}</label><select id="street-${key}" data-street="${key}">${choices.map(([value, label]) => `<option value="${value}" ${cfg[key] === value ? "selected" : ""}>${label}</option>`).join("")}</select></div>`;
  return `<section class="inspector-section streetscape-panel"><div class="section-heading"><span>CURBS, RAMPS & DRAINAGE</span><button id="street-presets-icon" title="Reference-inspired streetscape presets">${icon("layout-grid")}</button></div><button class="button full street-inspect" id="inspect-curb">${icon("navigation")} Inspect at street level ${icon("arrow-up-right")}</button><button class="street-preset-link" id="inspect-drain">Inspect drainage ${icon("arrow-up-right")}</button><br/><button class="street-preset-link" id="street-presets">9 reference designs ${icon("chevron-right")}</button>
 ${select("curbStyle", "Curb profile", [
   ["granite", "Granite blocks"],
   ["concrete", "Beveled concrete"],
   ["rolled", "Rolled edge"],
   ["brick-roll", "Brick rolled curb"],
   ["spoon", "Rounded spoon curb"],
   ["dished", "Deep dished curb"],
 ])}
 ${select("rampStyle", "Crossing ramps", [
   ["flared", "Flared drop"],
   ["blended", "Blended corner"],
   ["grooved", "Grooved drop"],
   ["none", "No ramps"],
 ])}
 ${select("paving", "Paving", [
   ["slab", "Concrete slabs"],
   ["brick", "Brick paving"],
 ])}
 ${select("tactile", "Tactile warning", [
   ["yellow", "Safety yellow"],
   ["stone", "Stone grey"],
   ["red", "Terracotta"],
   ["none", "None"],
 ])}
 ${select("drainage", "Drainage", [
   ["channel", "Linear grating"],
   ["inlet", "Open curb inlet"],
   ["brick-channel", "Brick channel + grille"],
   ["weep-channel", "Spoon channel + weeps"],
   ["slot-channel", "Dished channel + slot"],
   ["none", "None"],
 ])}
 <div class="field-row"><label for="street-width">Sidewalk width</label><div class="number-field"><input id="street-width" type="number" min="0.6" max="6" step="0.1" value="${e.sidewalk.toFixed(1)}"/><span>m</span></div></div><div class="field-row"><label for="street-height">Curb height</label><div class="number-field"><input id="street-height" type="number" min="0.08" max="0.22" step="0.01" value="${cfg.curbHeight.toFixed(2)}"/><span>m</span></div></div>
 <div class="street-note">Continuous corner returns · ramped paving-to-shoulder ends</div><p class="street-note">Formed curb profiles select their matching water channel. Inlet throats use a vertical concrete curb. Drain collectors require a clear approach over 13 m long.</p><div class="ramp-metrics"><span>${ramps.length} lowered ramps</span><span>${ramps.length ? (100 * ramps[0].grade).toFixed(1) + "% grade" : "At junctions"}</span></div>${steep ? '<p class="street-warning">Ramp exceeds 1:12. Widen the sidewalk for a gentler transition.</p><button id="gentle-ramp" class="street-preset-link">Widen to ' + Math.max(2.4, Math.ceil(((cfg.curbHeight - 0.006) * 12 + 0.45) * 10) / 10).toFixed(1) + " m " + icon("arrow-right") + "</button>" : '<p class="street-note">' + (ramps.length ? "6 mm lip · tactile domes · real sloped surface" : "No crossing ramp fits this approach.") + "</p>"}
 <button id="street-audit" class="street-preset-link">Inspect streetscape quads ${icon("arrow-up-right")}</button></section>`;
}
function landscapeReferences() {
  modal(
    "Complete streets, by design.",
    "Research, your reference images, and what this generator implements.",
    `${canInstallLandscapeExamples(project) && !["garden", "cycle", "tram"].every((mode) => project.edges.some((e) => e.landscape === mode)) ? '<div class="landscape-callout"><strong>Bring the examples into this saved map</strong><p>Apply the three profiles to the original Harbor Avenue blocks. Existing connections and other streets are kept. You can undo this change.</p><button class="button primary" id="install-landscape-examples">Add 3 street examples</button></div>' : ""}<div class="landscape-reference-grid">${LANDSCAPE_REFERENCES.map((r) => `<article><img src="/references/landscape/${r.image}" alt="${escape(r.title)}"/><div><small>${escape(r.credit)}</small><h3>${escape(r.title)}</h3><p>${escape(r.note)}</p><a href="${r.url}" target="_blank" rel="noopener noreferrer">Read source / guidance ↗</a>${r.mode ? `<div class="landscape-card-actions"><button class="button" data-tour="${r.mode}">Find example</button><button class="button" data-landscape-apply="${r.mode}" ${!selectedRoad() || selectedRoad()?.roundabout ? "disabled" : ""}>Apply to selected road</button></div>` : ""}</div></article>`).join("")}</div><p class="street-note">Parking research: <a href="https://nacto.org/publication/urban-bikeway-design-guide/designing-bikeways-for-all-ages-and-abilities/protected-bike-lanes/designing-protected-bike-lanes/" target="_blank" rel="noopener noreferrer">NACTO protected bike lanes</a> informs the 0.9 m door buffer. <a href="https://mutcd.fhwa.dot.gov/pdfs/11th_Edition/part8.pdf" target="_blank" rel="noopener noreferrer">MUTCD Part 8</a> informs railway stop-line setbacks. Our bays, bridge dimensions and static signals are illustrative, not a local design approval.</p><p class="street-note">These are reference images, not generated screenshots or a claim of engineering compliance. Layouts need local accessibility, drainage, sight-line, vehicle swept-path and railway review. Image rights remain with their original owners.</p>`,
  );
}
function sampleOptions() {
  const corridors = (project.corridors || []).filter(
      (c) =>
        c.id.startsWith("eastbank-") ||
        c.district === CITY_ID ||
        project.edges.some(
          (e) => e.id === c.sourceEdge && e.district === CITY_ID,
        ),
    ),
    bridges = project.edges.filter(
      (e) => e.structure === "bridge" && !e.bridgeNetwork,
    ),
    networks = BRIDGE_NETWORKS.filter((n) =>
      project.edges.some((e) => e.bridgeNetwork === n.id),
    );
  const roads = PRESETS.filter((p) =>
      project.edges.some((e) => e.district === CITY_ID && e.preset === p.id),
    ),
    profiles = STREET_PRESETS.filter((p) =>
      project.edges.some(
        (e) => e.district === CITY_ID && e.curbSample === p.id,
      ),
    );
  return `<option value="">${roads.length + profiles.length + corridors.length + bridges.length + networks.length} built samples…</option><optgroup label="Connected bridge networks">${networks.map((n) => `<option value="network:${n.id}">${n.name}</option>`).join("")}</optgroup><optgroup label="Eastbank road types">${roads.map((p) => `<option value="road:${p.id}">${escape(p.name)}</option>`).join("")}</optgroup><optgroup label="Eastbank curb & drainage">${profiles.map((p) => `<option value="curb:${p.id}">${escape(p.name)}</option>`).join("")}</optgroup><optgroup label="Rail corridors">${corridors.map((c) => `<option value="rail:${escape(c.id)}">${escape(c.name || (c.kind === "rail" ? "Ground railway" : "Elevated monorail"))}</option>`).join("")}</optgroup><optgroup label="Bridges">${bridges.map((e) => `<option value="id:${escape(e.id)}">${escape(e.name)}</option>`).join("")}</optgroup>`;
}
function showEastbank() {
  closeModal();
  setTool("select");
  if (
    project.sampleCityVersion !== 1 &&
    !edit("Added Eastbank sample city", () => {
      addSampleCity(project);
      installTransportSamples(project);
      installBridgeNetworks(project);
    })
  )
    return;
  world.setOptions({
    roads: true,
    rail: true,
    vegetation: true,
    buildings: true,
    markings: true,
  });
  planWorld?.setOptions(world.options);
  world.frameCity();
  planWorld?.frameCity();
}
function focusBuiltSample(value) {
  if (!value) return;
  const picker = $("#city-sample");
  if (picker && [...picker.options].some((o) => o.value === value))
    picker.value = value;
  closeModal();
  setTool("select");
  world.setOptions({
    roads: true,
    rail: true,
    vegetation: true,
    buildings: true,
    markings: true,
  });
  planWorld?.setOptions(world.options);
  const cut = value.indexOf(":"),
    kind = value.slice(0, cut),
    id = value.slice(cut + 1);
  if (kind === "network") {
    const edges = project.edges.filter((e) => e.bridgeNetwork === id),
      nodes = new Set(edges.flatMap((e) => [e.a, e.b]));
    if (!edges.length) {
      toast("This bridge network is not installed in the current map.");
      return;
    }
    selectRoad(edges.find((e) => e.structure === "bridge")?.id || edges[0].id);
    const origin = project.bridgeNetworks?.find((n) => n.id === id)?.origin;
    const points = origin
      ? [
          { x: origin.x - 230, z: origin.z - 240 },
          { x: origin.x + 230, z: origin.z + 240 },
        ]
      : project.nodes.filter((n) => nodes.has(n.id));
    world.frameArea(points, 25);
    planWorld?.frameArea(
      project.nodes.filter((n) => nodes.has(n.id)),
      35,
    );
    toast(
      BRIDGE_NETWORKS.find((n) => n.id === id)?.description || "Bridge network",
    );
    return;
  }
  if (kind === "rail") {
    const c = project.corridors?.find((c) => c.id === id);
    if (c) {
      selectRoad(c.sourceEdge || null);
      world.frameArea(corridorPoints(project, c));
      planWorld?.frameArea(corridorPoints(project, c));
    }
    return;
  }
  const e =
    kind === "id"
      ? project.edges.find((e) => e.id === id)
      : project.edges.find(
          (e) =>
            e.district === CITY_ID &&
            (kind === "curb" ? e.curbSample === id : e.preset === id),
        );
  if (!e) {
    toast("This sample has been removed from this project.");
    return;
  }
  selectRoad(e.id);
  if (e.roundabout) {
    world.frameRoundabout(e.roundabout);
    planWorld?.frameRoundabout(e.roundabout);
  } else if (kind === "curb") {
    setView("3d");
    world.inspectCurb(e.id, isWaterChannel(e));
  } else {
    if (e.structure === "bridge") {
      const points = [e.a, e.b].map((id) =>
        project.nodes.find((n) => n.id === id),
      );
      world.frameArea(points, 110);
      planWorld?.frameArea(points, 80);
    } else {
      world.focus(e.id);
      planWorld?.focus(e.id);
    }
  }
}
function tourStreet(kind) {
  closeModal();
  setTool("select");
  $("#city-sample").value = `road:${kind}`;
  if (kind === "roundabout") {
    let e =
      project.edges.find(
        (e) => e.district === CITY_ID && e.roundaboutRole === "ring",
      ) || project.edges.find((e) => e.roundaboutRole === "ring");
    if (!e) {
      if (
        !edit("Added starter roundabout", () => {
          addRoundabout(project, { x: -230, z: 100 }, { radius: 28 });
          project.landscapeStarterVersion = 1;
        })
      )
        return;
      e = project.edges.find((e) => e.roundaboutRole === "ring");
    }
    selectRoad(e.id);
    world.frameRoundabout(e.roundabout);
    planWorld?.frameRoundabout(e.roundabout);
    return;
  }
  const e =
    project.edges.find(
      (e) =>
        e.district === CITY_ID &&
        e.landscape === kind &&
        (kind !== "tram" || e.tramVehicle),
    ) || project.edges.find((e) => e.landscape === kind);
  if (!e) {
    landscapeReferences();
    toast(
      "Use Add 3 street examples if available, or select a regular road and apply this profile.",
    );
    return;
  }
  selectRoad(e.id);
  world.focus(e.id);
  planWorld?.focus(e.id);
}

function roundaboutDialog() {
  const cfg = state.roundaboutSettings;
  modal(
    "A connected roundabout.",
    "Place a circulating ring with welded entries, not an overlapping circular decal.",
    `<div class="roundabout-config"><div class="roundabout-diagram">${roadThumb(presetById("roundabout"))}<strong>Single circulating lane</strong><span>Rolled island curb · pedestrian edge</span></div><div>
    <div class="field-row"><label for="roundabout-radius">Ring centerline radius</label><input id="roundabout-radius" type="number" min="24" max="50" step="1" value="${cfg.radius}"/></div>
    <div class="field-row"><label for="roundabout-entries">Entry roads</label><select id="roundabout-entries"><option value="4" ${cfg.entries === 4 ? "selected" : ""}>Four entries</option><option value="3" ${cfg.entries === 3 ? "selected" : ""}>Three entries</option></select></div>
    <div class="field-row"><label for="roundabout-rotation">Rotation (degrees)</label><input id="roundabout-rotation" type="number" min="-360" max="360" step="15" value="${cfg.rotation}"/></div>
    <div class="field-row"><label for="roundabout-circulation">Traffic circulation</label><select id="roundabout-circulation"><option value="clockwise" ${cfg.clockwise ? "selected" : ""}>Clockwise</option><option value="counterclockwise" ${!cfg.clockwise ? "selected" : ""}>Counterclockwise</option></select></div>
   </div></div><p class="street-note">Click clear ground in 2D or 3D. Entry roads extend 32 m beyond the ring; connect their free endpoints with the road tool. Existing roads inside the ring are protected from being covered. Placement is one undo step. Dimensions are set before placement; afterwards this is an editable road graph, not a locked parametric object.</p>`,
    `<button class="button primary" id="activate-roundabout">${icon("plus")} Place roundabout</button>`,
  );
}
function streetscapePresets() {
  if (!selectedRoad()) {
    toast("Select a road first to apply a streetscape.");
    return;
  }
  modal(
    "Details that make a street.",
    "Nine designs, including three formed water channels with real collector openings. Applied to the selected road.",
    `<div class="street-reference-grid">${STREET_PRESETS.map((p) => `<button data-street-preset="${p.id}"><img src="/references/${p.id}.${p.id === "slot-channel" ? "svg" : "jpg"}" alt="Reference: ${p.name}"/><div><strong>${p.name}</strong><small>${p.note}</small><span>Apply design ${icon("arrow-up-right")}</span></div></button>`).join("")}</div><p class="street-note">Photographs are your design references; the slot-channel card is a design diagram. These are not screenshots of the generated geometry. Presets set a 2.4 m sidewalk. Channels are modeled drainage details, not a simulated pipe network; verify landing clearance, crossfall and local accessibility rules for production.</p>`,
  );
}
function streetAudit() {
  const report = auditStreetscape(world.streetscape);
  modal(
    "Street geometry, not stacked blocks.",
    "Curbs and ramps share a continuous surface. Grate slots and inlet mouths are open geometry.",
    `<div class="audit-stats"><div><strong>${report.quads.toLocaleString()}</strong><span>Base quad faces</span></div><div><strong>${report.ramps}</strong><span>Lowered ramps</span></div><div><strong>${report.drains}</strong><span>Drain structures</span></div></div><div class="audit-checks"><div>${icon(report.ok ? "shield-check" : "triangle-alert")}<span>${report.ok ? "Pavement geometry checks passed" : "Pavement geometry needs repair"}</span></div><div>${icon("check")}<span>Degenerate 3D faces</span><strong>${report.degenerate}</strong></div><div>${icon("check")}<span>Duplicate faces</span><strong>${report.duplicates}</strong></div>${[
      ["Missing junction edge returns", report.missingCorners],
      ["Paving/shoulder mouth seam errors", report.seamErrors],
      ["Steep edge terminations (review)", report.steepTransitions],
      ["Inverted surface faces", report.invertedFaces],
      ["Folded surface faces", report.foldedFaces],
      ["Paving overlap pairs", report.overlapPairs],
      ["Insufficient-radius corners", report.clearanceFailures],
    ]
      .map(
        ([label, count]) =>
          `<div>${icon(count ? "triangle-alert" : "check")}<span>${label}</span><strong>${count}</strong></div>`,
      )
      .join(
        "",
      )}<div>${icon("triangle-alert")}<span>Ramps steeper than 1:12</span><strong>${report.steepRamps}</strong></div></div>${
      report.issues.length
        ? `<div class="street-warning">${report.issues
            .slice(0, 6)
            .map((i) =>
              escape(`${i.type}: ${i.owner || i.owners?.join(" / ")}`),
            )
            .join("<br>")}</div>`
        : ""
    }<div class="scope-note"><strong>Scope of validation</strong><p>Road ribbons, curb ribbons, drain plates and repeated tactile domes retain quad faces. Streetscape ribbons are open surfaces; attached detail parts are separate shells. This is not a watertight solid or an accessibility certification. Corner radii reserve space for the full pavement width. Each corner has one material owner. Mixed paving styles change at a crossing ramp where available, otherwise at a clean mouth joint. Paving-to-shoulder returns taper down instead of disappearing. Checks cover missing returns, mouth seams, folded/inverted surfaces and positive-area intersections between edge panels; shared edges are allowed. Edits that introduce these failures are rejected. Checks are not a solid-mesh or accessibility certification.</p><p>Street export includes the selected segment and its adjoining corner pieces, plus tactile domes. Road-aligned surface UVs are included. Material textures are not packed into the OBJ.</p></div>`,
    `<button class="button" id="audit-wireframe">Show quad edges</button><button class="button primary" id="download-street">Export selected streetscape</button>`,
  );
}
function downloadStreet() {
  const e = selectedRoad();
  if (!e) {
    toast("Select a road first.");
    return;
  }
  const result = {
    ...world.result,
    roads: world.result.roads.filter((r) => r.e.id === e.id),
    junctions: world.result.junctions.filter((j) =>
      j.arms.some((a) => a.e.id === e.id),
    ),
  };
  const street = generateStreetscape(project, result);
  const check = auditStreetscape(street);
  if (!check.ok) {
    toast(
      "Export blocked: resolve the pavement geometry errors first. Open the streetscape report.",
    );
    return;
  }
  download(
    "frontier-streetscape.obj",
    exportStreetscapeOBJ(street),
    "text/plain",
  );
  toast("Selected curb, ramp and drainage quad mesh exported");
}
function updateNavigationUI() {
  if (!world) return;
  const fly = world.fly?.enabled;
  $("#zoom-level").textContent = fly
    ? `${world.fly.speed.toFixed(0)} m/s`
    : `${Math.round(world.camera.zoom * 100)}%`;
  $("#camera-navigation").textContent =
    world.navigation === "fly" ? "Fly camera" : "Orbit camera";
  $("#camera-navigation").disabled = state.mode === "2d";
  $("#camera-speed").disabled = !fly;
  $("#camera-speed").value = String(
    [3, 10, 25, 80].reduce((a, b) =>
      Math.abs(b - world.fly.speed) < Math.abs(a - world.fly.speed) ? b : a,
    ),
  );
  $(".navigation-hint").innerHTML = fly
    ? "<kbd>RMB + WASD</kbd> Fly <span>·</span><kbd>Q / E</kbd> Height <span>·</span><kbd>Shift</kbd> Fast"
    : "<kbd>RMB</kbd> Orbit <span>·</span><kbd>MMB</kbd> Pan";
}
function renderMinimap() {
  const map = $("#mini-map-svg"),
    nodes = new Map(project.nodes.map((n) => [n.id, n])),
    bounds = mapBounds(project);
  map.setAttribute(
    "viewBox",
    `${bounds.minX} ${bounds.minZ} ${bounds.width} ${bounds.height}`,
  );
  let river = "";
  for (let z = bounds.minZ; z <= bounds.maxZ; z += 10) {
    const x = -119 + 15 * Math.sin(z / 62) + 9 * Math.sin(z / 120);
    river += `${z === bounds.minZ ? "M" : "L"}${x},${z}`;
  }
  map.innerHTML = `<rect x="${bounds.minX}" y="${bounds.minZ}" width="${bounds.width}" height="${bounds.height}" fill="#d5ddc9"/><path d="${river}" fill="none" stroke="#a7c6bd" stroke-width="23"/>${project.edges
    .map((e) => {
      const a = nodes.get(e.a),
        b = nodes.get(e.b);
      return `<path d="M${a.x},${a.z}L${b.x},${b.z}" stroke="${e.id === state.selected ? "#e89a69" : e.structure === "bridge" ? "#bfa78c" : "#90998a"}" stroke-width="${roadWidth(e)}" fill="none"/>`;
    })
    .join(
      "",
    )}<rect x="-63" y="-139" width="238" height="276" rx="3" fill="none" stroke="#f9fcf3" stroke-width="5"/>${project.sampleCityVersion === 1 ? `<text x="${project.cityOrigin.x + 240}" y="${project.cityOrigin.z - 210}" text-anchor="middle" font-size="24" fill="#53654b">EASTBANK</text>` : ""}`;
}
function updateStats() {
  const junctions = world.result.junctions.filter((j) => j.degree >= 3).length;
  $("#road-count").textContent = `${project.edges.length} road segments`;
  $("#intersection-count").textContent = `${junctions} junctions`;
  const audit = auditMesh(world.result, project);
  $("#network-state").textContent =
    audit.issues.length || !world.pavementCheck.ok
      ? "Topology review needed"
      : "Topology checks passed";
  $("#network-state").previousElementSibling.classList.toggle(
    "orange",
    audit.issues.length > 0 || !world.pavementCheck.ok,
  );
  $("#undo").disabled = !state.history.length;
  $("#redo").disabled = !state.future.length;
}
function rebuild(prepared = null) {
  const picker = $("#city-sample"),
    value = picker?.value;
  if (picker) {
    picker.innerHTML = sampleOptions();
    if ([...picker.options].some((o) => o.value === value))
      picker.value = value;
  }
  world.rebuild(project, prepared);
  world.select(state.selected);
  if (planWorld) {
    planWorld.rebuild(project, prepared);
    planWorld.select(state.selected);
  }
  renderInspector();
  renderMinimap();
  updateStats();
}
function save() {
  clearTimeout(saveTimer);
  try {
    localStorage.setItem("frontier-project-v1", JSON.stringify(project));
    state.dirty = false;
    $("#saved-label").innerHTML = `${icon("check-check")} All changes saved`;
    refreshIcons();
    return true;
  } catch {
    $("#saved-label").textContent = "Storage full — download JSON";
    return false;
  }
}
function edit(label, fn) {
  const snapshot = JSON.stringify(project),
    selected = state.selected,
    previous = world.pavementCheck;
  try {
    fn();
    cleanup(project);
    for (const c of project.corridors || [])
      if (c.sourceEdge && !project.edges.some((e) => e.id === c.sourceEdge)) {
        delete c.sourceEdge;
        delete c.sourceNode;
      }
    validateProject(project);
    const networkCheck = auditBridgeNetworks(project);
    if (!networkCheck.ok) throw Error(networkCheck.issues[0]);
    const previousRailIssues = crossingIssues(JSON.parse(snapshot)),
      railIssues = crossingIssues(project).filter(
        (x) => !previousRailIssues.includes(x),
      );
    if (railIssues.length) throw Error(railIssues[0]);
    const result = generateRoadMesh(project),
      streetscape = generateStreetscape(project, result),
      check = auditStreetscape(streetscape),
      roadCheck = auditMesh(result, project);
    const faults = (c) =>
      c
        ? c.foldedPanels +
          c.invertedPanels +
          c.collapsedPanels +
          c.overlapPairs +
          c.clearanceFailures +
          c.degenerate +
          c.duplicates +
          c.invertedFaces +
          c.foldedFaces +
          (c.missingCorners || 0) +
          (c.seamErrors || 0)
        : 0;
    if (
      faults(check) > faults(previous) ||
      (faults(check) === faults(previous) &&
        ((check.overlapArea ?? 0) > (previous?.overlapArea ?? 0) + 1e-5 ||
          (check.clearanceDeficit ?? 0) >
            (previous?.clearanceDeficit ?? 0) + 1e-5)) ||
      (!check.ok && previous?.ok) ||
      roadCheck.issues.length ||
      roadCheck.degenerate ||
      roadCheck.duplicate ||
      roadCheck.nonmanifold ||
      roadCheck.crossings ||
      roadCheck.seamErrors ||
      roadCheck.nonConvex ||
      roadCheck.selfIntersections
    ) {
      const issue = check.issues[0];
      const where =
        issue?.owner || issue?.owners?.join(" / ") || "tight junction";
      throw Error(
        `Edit rejected: insufficient pavement clearance or overlap at ${where}. Spread the road nodes apart or reduce the sidewalk width.`,
      );
    }
    state.history.push(snapshot);
    if (state.history.length > 40) state.history.shift();
    state.future = [];
    state.actions.push(label);
    state.dirty = true;
    $("#saved-label").textContent = "Saving changes…";
    rebuild({ result, streetscape, streetAudit: check });
    saveTimer = setTimeout(save, 500);
    return true;
  } catch (error) {
    project = JSON.parse(snapshot);
    state.selected = selected;
    rebuild();
    toast(error.message);
    return false;
  }
}
function undo() {
  if (!state.history.length) return;
  state.future.push(JSON.stringify(project));
  project = JSON.parse(state.history.pop());
  state.actions.push("Undid last change");
  rebuild();
  save();
  toast("Change undone");
}
function redo() {
  if (!state.future.length) return;
  state.history.push(JSON.stringify(project));
  project = JSON.parse(state.future.pop());
  state.actions.push("Redid last change");
  rebuild();
  save();
  toast("Change restored");
}
function changeRoad(key, value, label) {
  const e = selectedRoad();
  if (!e) return;
  edit(label || `Updated ${key}`, () => {
    e[key] = value;
    if (
      key === "curbStyle" &&
      ["brick-roll", "spoon", "dished"].includes(value)
    )
      e.drainage = {
        "brick-roll": "brick-channel",
        spoon: "weep-channel",
        dished: "slot-channel",
      }[value];
    if (
      key === "drainage" &&
      value === "inlet" &&
      ["brick-roll", "spoon", "dished"].includes(e.curbStyle)
    )
      e.curbStyle = "concrete";
  });
}
function selectRoad(id) {
  if (state.tool === "delete" && id) {
    state.selected = id;
    deleteRoad();
    return;
  }
  state.selected = id;
  world.select(id);
  planWorld?.select(id);
  renderInspector();
  renderMinimap();
}
function deleteRoad(confirmAssembly = false) {
  if (selectedRoad()?.roundaboutRole === "ring" && !confirmAssembly) {
    modal(
      "Remove this roundabout?",
      "This removes the circulating ring and its tagged entry roads.",
      '<p class="muted-copy">Other connected roads are kept. You can restore the complete assembly with Undo.</p>',
      '<button class="button" id="close-modal">Keep editing</button><button class="button primary" id="confirm-delete-roundabout">Remove roundabout</button>',
    );
    return;
  }
  if (!state.selected) return;
  const name = selectedRoad()?.name || "road";
  const accepted = edit(`Deleted ${name}`, () => {
    const selected = selectedRoad();
    project.edges = project.edges.filter((e) =>
      selected?.roundaboutRole === "ring"
        ? e.roundabout !== selected.roundabout
        : e.id !== state.selected,
    );
    state.selected = null;
  });
  if (accepted)
    toast(
      confirmAssembly
        ? "Roundabout removed. Undo to restore."
        : "Road segment deleted. Undo to restore.",
    );
}
function nodeMoved(id, point) {
  const target = nearestSnap(
    {
      ...project,
      nodes: project.nodes.filter((n) => n.id !== id),
      edges: project.edges.filter((e) => e.a !== id && e.b !== id),
    },
    point,
    state.snap,
  );
  let changed = false;
  const accepted = edit("Moved road node", () => {
    const result = moveNode(project, id, target);
    changed = result.ok;
    if (!result.ok)
      throw Error(
        "Move rejected: overlapping roads or a short junction stub. Move the node farther from adjacent intersections.",
      );
    if (result.ok) state.selected = result.ids.at(-1) || state.selected;
  });
  if (!accepted) return;
  toast(
    changed
      ? "Node moved. Connected roads and junctions rebuilt."
      : "Move rejected: would create overlapping or very short roads.",
  );
}
function groundClick(pt, event) {
  if (state.tool === "roundabout") {
    let feature;
    const center = state.snap
      ? { x: Math.round(pt.x / 2) * 2, z: Math.round(pt.z / 2) * 2 }
      : pt;
    const accepted = edit("Placed roundabout", () => {
      feature = addRoundabout(project, center, state.roundaboutSettings);
      state.selected = feature.ids[0];
    });
    if (accepted) {
      setTool("select");
      world.frameRoundabout(feature.id);
      planWorld?.frameRoundabout(feature.id);
      toast(
        "Roundabout placed · connect roads to its entry endpoints · Undo removes the assembly.",
      );
    }
    return;
  }
  if (state.tool === "terrain") {
    edit(state.terrainAmount > 0 ? "Raised terrain" : "Lowered terrain", () =>
      project.brushes.push({
        ...pt,
        radius: 24,
        amount: state.terrainAmount * (event.shiftKey ? -1 : 1),
      }),
    );
    return;
  }
  if (!["draw", "bridge", "rail", "monorail"].includes(state.tool)) return;
  const point = nearestSnap(project, pt, state.snap);
  if (!state.drawStart) {
    state.drawStart = point;
    updateDrawing();
    return;
  }
  if (dist(state.drawStart, point) < 4) {
    toast("Roads must be at least 4 meters long.");
    return;
  }
  if (["rail", "monorail"].includes(state.tool)) {
    edit("Drew rail corridor", () => {
      project.corridors.push({
        id: `c${project.nextId++}`,
        kind: state.tool,
        elevation: state.tool === "rail" ? 0.4 : 8,
        points: [{ ...state.drawStart }, { ...point }],
      });
    });
    state.drawStart = point;
    updateDrawing();
    toast("Rail section added · click to continue, Esc to finish");
    return;
  }
  let ids = [];
  const accepted = edit(
    `Drew ${state.tool === "bridge" ? "bridge" : "road"}`,
    () => {
      ids = addRoad(project, state.drawStart, point, state.preset, {
        name: state.tool === "bridge" ? "New bridge" : "New road",
        structure: state.tool === "bridge" ? "bridge" : "surface",
      });
      state.selected = ids.at(-1) || state.selected;
    },
  );
  if (!accepted) return;
  if (!ids.length) {
    toast("Overlapping or duplicate segment rejected.");
    return;
  }
  state.drawStart = point;
  world.preview([point]);
  planWorld?.preview([point]);
  updateDrawing();
  toast(
    `${state.tool === "bridge" ? "Bridge" : "Road"} added · click to continue, Esc to finish`,
  );
}
function hover(pt) {
  state.coords = pt;
  $("#cursor-position").innerHTML =
    `X ${pt.x.toFixed(2)} <i>Y ${(pt.y || 0).toFixed(2)}</i> Z ${pt.z.toFixed(2)}`;
  if (state.tool === "roundabout") {
    const settings = state.roundaboutSettings,
      center = state.snap
        ? { x: Math.round(pt.x / 2) * 2, z: Math.round(pt.z / 2) * 2 }
        : pt,
      points = [];
    for (let i = 0; i <= 72; i++) {
      const a = (settings.rotation * Math.PI) / 180 + (i * Math.PI) / 36,
        n = {
          x: center.x + settings.radius * Math.cos(a),
          z: center.z + settings.radius * Math.sin(a),
        };
      points.push(n);
      if (i < 72 && i % (72 / settings.entries) === 0)
        points.push(
          {
            x: center.x + (settings.radius + 32) * Math.cos(a),
            z: center.z + (settings.radius + 32) * Math.sin(a),
          },
          n,
        );
    }
    world.preview(points, false);
    planWorld?.preview(points, false);
    return;
  }
  if (state.drawStart) {
    const snapped = nearestSnap(project, pt, state.snap);
    world.preview([state.drawStart, snapped]);
    planWorld?.preview([state.drawStart, snapped]);
    const length = $("#draft-length");
    if (length)
      length.textContent = `${dist(state.drawStart, snapped).toFixed(1)} m`;
  }
}
function setTool(tool) {
  state.tool = tool;
  state.drawStart = null;
  world.setTool(tool);
  planWorld?.setTool(tool);
  world.preview([]);
  planWorld?.preview([]);
  $$("[data-tool]").forEach((b) =>
    b.classList.toggle("active", b.dataset.tool === tool),
  );
  $("#viewport-hint").innerHTML =
    `<span class="hint-icon">${icon({ select: "mouse-pointer-2", roundabout: "rotate-cw", draw: "spline", bridge: "cable", terrain: "mountain", pan: "move", delete: "trash-2", rail: "train-front", monorail: "route" }[tool])}</span><span>${{ select: "Select a road · drag its endpoints to reshape", roundabout: "Click clear ground to place the ring and entry roads · Esc cancels", draw: "Click to place a start node. Snap connects roads automatically.", bridge: "Click to place bridge abutments across a waterway.", terrain: "Click to raise terrain · Shift + click to lower", pan: "Drag to move around your world", delete: "Click a road segment to remove it · Ctrl Z to undo", rail: "Click to place railway points · Esc to finish", monorail: "Click to place monorail points · Esc to finish" }[tool]}</span>`;
  updateDrawing();
  renderInspector();
  refreshIcons();
}
function updateDrawing() {
  const card = $("#drawing-card");
  card.classList.toggle(
    "hidden",
    !["draw", "bridge", "rail", "monorail", "roundabout"].includes(state.tool),
  );
  if (state.tool === "roundabout") {
    card.innerHTML = `<div><span class="live-dot"></span>ROUNDABOUT TOOL<button id="finish-draw" aria-label="Cancel roundabout">${icon("x")}</button></div><strong>${state.roundaboutSettings.entries}-entry circulating junction</strong><p>${state.roundaboutSettings.radius} m ring radius · 32 m entry roads</p><small>Click clear space · <kbd>Esc</kbd> Cancel</small>`;
    refreshIcons();
    return;
  }
  card.innerHTML = `<div><span class="live-dot"></span>${state.tool === "bridge" ? "BRIDGE TOOL" : ["rail", "monorail"].includes(state.tool) ? "RAIL CORRIDOR" : "ROAD TOOL"}<button id="finish-draw" aria-label="Finish drawing">${icon("x")}</button></div><strong>${["rail", "monorail"].includes(state.tool) ? (state.tool === "rail" ? "Railway track" : "Monorail viaduct") : escape(presetById(state.preset).name)}</strong><p>${state.drawStart ? "Place the next node" : "Choose a starting point"}<span id="draft-length">${state.drawStart ? "0.0 m" : ""}</span></p><small><kbd>Esc</kbd> Finish <span>·</span> ${state.snap ? "Snapping on" : "Free placement"}</small>`;
  refreshIcons();
}
function setView(mode) {
  state.mode = mode;
  $$("[data-view]").forEach((b) =>
    b.classList.toggle("active", b.dataset.view === mode),
  );
  $("#plan-view").classList.toggle("hidden", mode !== "split");
  $(".viewports").classList.toggle("is-split", mode === "split");
  $("#mini-map").classList.toggle("hidden", mode === "split");
  world.setMode(mode === "2d" ? "2d" : "3d");
  $("#perspective-label").textContent =
    mode === "2d" ? "Orthographic" : "Perspective";
  if (mode === "split" && !planWorld) {
    planWorld = new World($("#plan-view"), {
      onSelect: selectRoad,
      onGround: groundClick,
      onHover: hover,
      onNodeMove: nodeMoved,
    });
    planWorld.rebuild(project);
    planWorld.setMode("2d");
    planWorld.setTool(state.tool);
    planWorld.select(state.selected);
    planWorld.setOptions(world.options);
  }
  world.resize();
  planWorld?.resize();
  updateNavigationUI();
}
function toast(message) {
  clearTimeout(toastTimer);
  $("#toast").innerHTML = `${icon("check")}<span>${escape(message)}</span>`;
  $("#toast").classList.add("visible");
  refreshIcons();
  toastTimer = setTimeout(() => $("#toast").classList.remove("visible"), 3500);
}
function download(name, contents, type) {
  const blob = new Blob([contents], { type }),
    url = URL.createObjectURL(blob),
    a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function modal(title, subtitle, body, footer = "") {
  if (!$("#modal-root").children.length) modalFocus = document.activeElement;
  closePopover();
  $("#modal-root").innerHTML =
    `<div class="modal-backdrop"><section class="modal" role="dialog" aria-modal="true" aria-labelledby="modal-title"><div class="modal-heading"><div><div class="eyebrow">FRONTIER STUDIO</div><h2 id="modal-title">${title}</h2><p>${subtitle}</p></div><button id="close-modal" class="icon-btn" aria-label="Close dialog">${icon("x")}</button></div><div class="modal-body">${body}</div>${footer ? `<div class="modal-footer">${footer}</div>` : ""}</section></div>`;
  refreshIcons();
  $("#close-modal").focus();
}
function closeModal() {
  if (!$("#modal-root").children.length) return;
  $("#modal-root").innerHTML = "";
  modalFocus?.focus();
}
function topologyModal() {
  const a = auditMesh(world.result, project);
  modal(
    "Built to connect.",
    "Road-surface topology report · current project",
    `<div class="audit-banner ${a.issues.length ? "warning" : ""}">${icon(a.issues.length ? "triangle-alert" : "shield-check")}<div><strong>${a.issues.length ? "Some geometry needs review" : "All implemented checks passed"}</strong><span>Every exported road-surface face has four vertices.</span></div></div><div class="audit-stats"><div><strong>${a.quads.toLocaleString()}</strong><span>Quad faces</span></div><div><strong>${a.vertices.toLocaleString()}</strong><span>Welded vertices</span></div><div><strong>${world.result.junctions.filter((j) => j.degree >= 3).length}</strong><span>Merged junctions</span></div></div><div class="audit-checks">${[
      ["Degenerate faces", a.degenerate],
      ["Duplicate faces", a.duplicate],
      ["Non-manifold edges", a.nonmanifold],
      ["Unjoined centerline crossings", a.crossings],
      ["Unwelded junction seams", a.seamErrors],
      ["Non-convex quad faces", a.nonConvex],
      ["Self-intersecting junction boundaries", a.selfIntersections],
      ["Short-segment warnings", a.short],
    ]
      .map(
        ([label, n]) =>
          `<div>${icon(n ? "triangle-alert" : "check")}<span>${label}</span><strong>${n}</strong></div>`,
      )
      .join(
        "",
      )}</div><div class="scope-note"><strong>What this report covers</strong><p>Road strips and junction patches share vertices. Junctions are triangulated internally, then subdivided into quads; extraordinary vertices remain. The WebGL renderer triangulates quads for display.</p><p>This is not an AAA asset certification. Surface collision, extreme-angle clearance, quad aspect ratio, UVs, LODs and sidewalk topology require further production validation. Roads are open surfaces, with ${a.boundaries.toLocaleString()} boundary edges. Buildings, markings and bridge details are preview meshes, not part of the quad OBJ.</p></div>`,
    `<button class="button" id="audit-street">Pavement report</button><button class="button" id="audit-wireframe">${icon("git-branch")} Show quad wireframe</button><button class="button primary" id="audit-export">Export road mesh ${icon("arrow-right")}</button>`,
  );
}
function exportModal() {
  const a = auditMesh(world.result, project);
  modal(
    "Take your world with you.",
    "Export the editable project or the generated road surface.",
    `<div class="export-option"><span class="export-icon">${icon("file-json")}</span><div><strong>Frontier project</strong><p>Editable road graph, rail alignments, properties and terrain edits.</p><span>.JSON · project format v1</span></div><button class="button" id="download-json">Download ${icon("download")}</button></div><div class="export-option"><span class="export-icon">${icon("box")}</span><div><strong>Quad road & landscape surface</strong><p>${a.quads.toLocaleString()} quad faces · meters · Y-up · welded junctions.</p><span>.OBJ · road surface only</span></div><button class="button" id="download-obj">Download ${icon("download")}</button></div><div class="export-option"><span class="export-icon">${icon("route")}</span><div><strong>Selected streetscape</strong><p>Curbs, ramps, tactile domes and drainage. Quad geometry with aligned surface UVs.</p><span>.OBJ · selected road + adjoining corners</span></div><button class="button" id="download-street">Download ${icon("download")}</button></div><div class="scope-note"><strong>A focused geometry export</strong><p>The road OBJ contains the partitioned road/junction surfaces, including cycle tracks, planted reservation surfaces, beveled edges and crossing cut-throughs. Curbs, ramps and drains have a separate selected-streetscape quad export. Terrain, buildings, rail, markings and bridge supports remain viewport previews. Surface UV coordinates are included; material textures, collision meshes and engine-specific LODs are not exported.</p>${a.issues.length ? `<p class="warning-text">${a.issues.length} topology warning(s). Review the topology report before production use.</p>` : ""}</div>`,
  );
}
function helpModal() {
  modal(
    "A small road. A bigger world.",
    "Your quick guide to Frontier Road & World Studio.",
    `<div class="help-grid"><div>${icon("spline")}<h3>Draw connected roads</h3><p>Choose a preset, then click to place road nodes. Same-level crossings split and merge automatically. Press Esc to finish.</p></div><div>${icon("columns-2")}<h3>Two views. One graph.</h3><p>Work in plan, inspect in 3D, or keep both open. Select any segment to change its width, lane count and features.</p></div><div>${icon("mountain")}<h3>Follow the terrain</h3><p>Raise or lower ground with the terrain brush. Surface roads follow the terrain; bridges interpolate between their abutments.</p></div><div>${icon("shield-check")}<h3>Inspect before export</h3><p>Turn on quad wireframe and run the topology report. Export editable JSON or a road-surface OBJ.</p></div></div><div class="scope-note"><strong>Prototype scope, not a finished AAA toolchain</strong><p>Includes urban, country and unpaved roads; lane lines, arrows, crosswalks and cycle symbols; bridge decks; contextual rail and monorail; terrain, a river and a lake. Track-switch generation, direct rail-node editing, waterway alignment editing, a complete traffic-signing library, accessibility certification, complex sidewalk collision cleanup and engine-ready asset processing are still future work.</p></div>`,
  );
}
function shortcutsModal() {
  modal(
    "A quicker way around.",
    "Keep your hands on the creative work.",
    `<div class="shortcut-list">${[
      ["Select road", "V"],
      ["Draw road", "R"],
      ["Draw bridge", "B"],
      ["Terrain brush", "T"],
      ["Pan the view", "H"],
      ["Delete tool", "D"],
      ["Toggle snapping", "G"],
      ["Fit project", "F"],
      ["Finish drawing / close dialog", "Esc"],
      ["Undo", "Ctrl / ⌘ Z"],
      ["Redo", "Ctrl / ⌘ Shift Z"],
      ["Save project", "Ctrl / ⌘ S"],
      ["Fly / look (3D fly mode)", "Hold RMB + WASD"],
      ["Fly up / down", "RMB + E / Q"],
      ["Faster / speed adjustment", "Shift / RMB + scroll"],
      ["Orbit (orbit mode)", "Right drag"],
      ["Pan / zoom", "Middle drag / scroll"],
    ]
      .map(([a, b]) => `<div><span>${a}</span><kbd>${b}</kbd></div>`)
      .join("")}</div>`,
  );
}
function closePopover() {
  $("#popover").classList.add("hidden");
}
function popover(anchor, html) {
  const r = anchor.getBoundingClientRect(),
    p = $("#popover");
  p.innerHTML = html;
  p.classList.remove("hidden");
  p.style.top = `${r.bottom + 8}px`;
  p.style.left = `${Math.min(r.left, window.innerWidth - 250)}px`;
  refreshIcons();
}
function overlaysMenu(anchor) {
  popover(
    anchor,
    `<div class="popover-title">VIEWPORT OVERLAYS</div><button data-overlay="wireframe">${icon("git-branch")}Quad wireframe ${world.options.wireframe ? icon("check") : ""}</button><button data-overlay="markings">${icon("route")}Road markings ${world.options.markings ? icon("check") : ""}</button><button data-overlay="buildings">${icon("building-2")}Building massing ${world.options.buildings ? icon("check") : ""}</button><button data-overlay="vegetation">${icon("trees")}Trees & landscape ${world.options.vegetation ? icon("check") : ""}</button><button data-overlay="rail">${icon("train-front")}Rail corridors ${world.options.rail ? icon("check") : ""}</button><button data-overlay="water">${icon("waves")}Water ${world.options.water ? icon("check") : ""}</button><div class="popover-divider"></div><button id="overlay-topology">${icon("shield-check")}Topology report</button>`,
  );
}
function updateOptions(key) {
  const options = { [key]: !world.options[key] };
  world.setOptions(options);
  planWorld?.setOptions(options);
  if (state.rightTab === "layers") renderInspector();
}

// A single event dispatcher keeps controls functional after panel rerenders.
document.addEventListener("click", (event) => {
  const button = event.target.closest("button,a.brand");
  if (!button) {
    if (!event.target.closest("#popover")) closePopover();
    if (event.target.classList.contains("modal-backdrop")) closeModal();
    return;
  }
  const id = button.id,
    d = button.dataset;
  if (
    !button.closest("#popover") &&
    !["overlays", "project-menu", "help-menu", "project-title"].includes(id)
  )
    closePopover();
  if (d.streetPreset) {
    const preset = STREET_PRESETS.find((p) => p.id === d.streetPreset),
      e = selectedRoad();
    if (e && preset) {
      const accepted = edit("Applied streetscape preset", () => {
        for (const key of Object.keys(STREET_DEFAULTS))
          e[key] = preset[key] ?? STREET_DEFAULTS[key];
        e.sidewalk = 2.4;
      });
      if (!accepted) return;
      closeModal();
      setView("3d");
      world.inspectCurb(e.id, isWaterChannel(e));
      updateNavigationUI();
      toast(`${preset.name} applied`);
    }
    return;
  }
  if (d.view) {
    setView(d.view);
    return;
  }
  if (d.tool) {
    setTool(d.tool);
    return;
  }
  if (d.preset) {
    if (d.preset === "roundabout") {
      roundaboutDialog();
      return;
    }
    state.preset = d.preset;
    renderLibrary();
    setTool("draw");
    return;
  }

  if (button.dataset.network) {
    focusBuiltSample(`network:${button.dataset.network}`);
    return;
  }
  if (button.dataset.networkAll) {
    const id = button.dataset.networkAll;
    focusBuiltSample(`network:${id}`);
    const used = new Set(
      project.edges
        .filter((e) => e.bridgeNetwork === id)
        .flatMap((e) => [e.a, e.b]),
    );
    world.frameArea(
      project.nodes.filter((n) => used.has(n.id)),
      30,
    );
    planWorld?.frameArea(
      project.nodes.filter((n) => used.has(n.id)),
      30,
    );
    return;
  }
  if (button.id === "install-bridge-networks") {
    try {
      if (!localStorage.getItem("frontier-before-bridge-networks-v1"))
        localStorage.setItem(
          "frontier-before-bridge-networks-v1",
          JSON.stringify(project),
        );
      if (
        edit("Added three connected bridge networks", () =>
          installBridgeNetworks(project),
        )
      )
        focusBuiltSample("network:loop");
    } catch (error) {
      toast(`Bridge networks not added: ${error.message}`);
    }
    return;
  }
  if (button.id === "tour-networks") {
    modal(
      "Three connected bridge networks",
      "Compact elevated distributors, not cloverleaf interchanges.",
      `${project.bridgeNetworkVersion !== 1 ? '<p><button id="install-bridge-networks" class="button primary">Back up & add three networks</button></p>' : ""}<div class="network-cards">${BRIDGE_NETWORKS.map((n) => `<article><h3>${n.name}</h3><p>${n.description}</p><button class="button primary" data-network="${n.id}">View interchange ↗</button><button class="button" data-network-all="${n.id}">Fit all connections</button></article>`).join("")}</div><p class="street-note">Editable connected roads, continuous deck elevations, concrete barriers and piers. The roundabout is a raised interpretation of the underpass reference. These are geometric prototypes, not structural or traffic engineering approvals.</p><div class="network-references"><img src="/references/bridge-networks/loop.jpg" alt="User-supplied elevated loop reference"/><img src="/references/bridge-networks/roundabout.jpg" alt="User-supplied roundabout over a through-road reference"/></div>`,
    );
    return;
  }
  if (button.id === "tour-bridge") {
    const e =
      project.edges.find((e) => e.sampleKey === "bridge:arch") ||
      project.edges.find((e) => e.structure === "bridge");
    if (e) focusBuiltSample("id:" + e.id);
    return;
  }
  if (button.id === "tour-crossing") {
    $("#city-sample").value = "";
    const crossings = railCrossings(project),
      c =
        crossings.find(
          (c) =>
            project.edges.find((e) => e.id === c.roadId)?.sampleKey ===
            "crossing",
        ) || crossings[0];
    if (c) {
      selectRoad(c.roadId);
      world.frameArea(
        [
          { x: c.point.x - 15, z: c.point.z - 15 },
          { x: c.point.x + 15, z: c.point.z + 15 },
        ],
        8,
      );
      planWorld?.frameArea([c.point], 40);
    } else toast("No ground-level railway crossings in this map.");
    return;
  }
  if (button.dataset.networkBackup) {
    const keys = [
      "frontier-before-bridge-networks-v1",
      "frontier-before-eastbank-repair-v1",
      "frontier-before-transport-v1",
    ];
    const key = keys[Number(button.dataset.networkBackup)],
      saved = key && localStorage.getItem(key);
    if (saved) download(`${key}.json`, saved, "application/json");
    return;
  }
  if (button.id === "transport-backup") {
    const backups = [
      ["frontier-before-bridge-networks-v1", "Before bridge networks"],
      ["frontier-before-eastbank-repair-v1", "Before Eastbank repair"],
      ["frontier-before-transport-v1", "Before parking / rail update"],
    ];
    modal(
      "Saved-map backups",
      "Download before clearing browser storage.",
      backups
        .map(
          ([key, label], i) =>
            `<p><button class="button" data-network-backup="${i}" ${localStorage.getItem(key) ? "" : "disabled"}>${label} ↓</button></p>`,
        )
        .join(""),
    );
    return;
  }
  if (button.id === "repair-eastbank") {
    modal(
      "Repair generated Eastbank",
      "Backed up, audited, undoable.",
      `<p>Rebuild only the tagged Eastbank sample roads and sample rail corridors at their original location. Edits to those generated roads will be replaced. Westhaven, terrain and untagged custom roads are retained.</p><p>A full JSON backup is saved first. If the rebuilt district conflicts with your custom roads, the operation is rejected instead of saving broken geometry.</p><button class="button primary" id="confirm-eastbank-repair">Back up & rebuild Eastbank</button>`,
    );
    return;
  }
  if (button.id === "confirm-eastbank-repair") {
    try {
      localStorage.setItem(
        "frontier-before-eastbank-repair-v1",
        JSON.stringify(project),
      );
    } catch {
      toast("Could not save a backup; repair cancelled.");
      return;
    }
    if (edit("Repaired generated Eastbank", () => repairSampleCity(project))) {
      closeModal();
      showEastbank();
      toast(
        "Eastbank rebuilt and audited. Undo or download Backup to recover the previous map.",
      );
    }
    return;
  }
  if (button.id === "rail-exit-tool") {
    const roads = project.edges.filter(
      (e) =>
        e.landscape === "tram" &&
        [e.a, e.b].some(
          (id) =>
            project.edges.filter((x) => x.a === id || x.b === id).length === 1,
        ),
    );
    modal(
      "Turn tracks off the street",
      "A smooth, independently routed rail or monorail departure.",
      roads.length
        ? `<label>Open-ended tram street<select id="exit-road" class="landscape-select">${roads.map((e) => `<option value="${escape(e.id)}">${escape(e.name)}</option>`).join("")}</select></label><label>System<select id="exit-kind" class="landscape-select"><option value="rail">Ground railway · twin tracks</option><option value="monorail">Elevated monorail · portal supports</option></select></label><label>Turn<select id="exit-side" class="landscape-select"><option value="-1">Left from the open end</option><option value="1">Right from the open end</option></select></label><label>Radius (m)<input id="exit-radius" type="number" value="80" min="30" max="200"/></label><p>Creates or replaces this street's selected rail exit. The first tangent and railway gauge match the tram street; the route follows that endpoint when moved. Level crossings currently require unkerbed roads and an angle of at least 60°. Gates are static previews, not working signalling.</p><button class="button primary" id="apply-rail-exit">Create / replace departure</button>`
        : "<p>Draw a Green tram boulevard with an open end, then open this tool again.</p>",
    );
    return;
  }
  if (button.id === "apply-rail-exit") {
    const edgeId = $("#exit-road").value,
      kind = $("#exit-kind").value,
      side = Number($("#exit-side").value),
      radius = Number($("#exit-radius").value);
    let c;
    if (
      edit("Created curved rail departure", () => {
        project.corridors = project.corridors.filter(
          (c) => !(c.sourceEdge === edgeId && c.kind === kind),
        );
        c = addRailExit(project, edgeId, { kind, side, radius });
      })
    ) {
      closeModal();
      focusBuiltSample("rail:" + c.id);
    }
    return;
  }
  if (d.tour) {
    tourStreet(d.tour);
    return;
  }
  if (d.landscapeApply) {
    const e = selectedRoad();
    if (
      e &&
      edit("Applied landscape profile", () =>
        applyLandscape(e, d.landscapeApply),
      )
    ) {
      closeModal();
      world.focus(e.id);
    }
    return;
  }
  if (d.library) {
    state.libraryTab = d.library;
    state.category = "All";
    $$("[data-library]").forEach((b) =>
      b.classList.toggle("active", b.dataset.library === d.library),
    );
    renderLibrary();
    return;
  }
  if (d.category) {
    state.category = d.category;
    renderLibrary();
    return;
  }
  if (d.inspector) {
    state.rightTab = d.inspector;
    renderInspector();
    return;
  }
  if (d.step) {
    const e = selectedRoad();
    changeRoad(
      "lanes",
      Math.max(
        modern(e) ? 2 : 1,
        Math.min(6, e.lanes + Number(d.step) * (modern(e) ? 2 : 1)),
      ),
      "Changed lane count",
    );
    return;
  }
  if (d.structure) {
    if (d.structure.startsWith("bridge-")) {
      state.preset = d.structure.slice(7);
      setTool("bridge");
    } else if (["rail", "monorail"].includes(d.structure)) {
      world.setOptions({ rail: true });
      planWorld?.setOptions({ rail: true });
      setTool(d.structure);
    } else {
      streetscapePresets();
    }
    return;
  }
  if (d.layer) {
    const visible = !world.layers[d.layer].visible;
    world.layers[d.layer].visible = visible;
    world.needsRender = true;
    if (planWorld) {
      planWorld.layers[d.layer].visible = visible;
      planWorld.needsRender = true;
    }
    world.options[d.layer] = visible;
    if (planWorld) planWorld.options[d.layer] = visible;
    renderInspector();
    return;
  }
  if (d.overlay) {
    updateOptions(d.overlay);
    overlaysMenu($("#overlays"));
    return;
  }
  if (d.brush) {
    state.terrainAmount = d.brush === "raise" ? 1.5 : -1.5;
    renderInspector();
    return;
  }
  const actions = {
    "theme-toggle": () =>
      setTheme(
        document.documentElement.dataset.theme === "dark" ? "light" : "dark",
      ),
    "camera-navigation": () => {
      world.setNavigation(world.navigation === "fly" ? "orbit" : "fly");
      updateNavigationUI();
    },
    "inspect-drain": () => {
      if (!world.streetscape.drains.some((d) => d.road === state.selected)) {
        toast(
          "This segment has no drain: choose drainage on a longer surface road.",
        );
        return;
      }
      setView("3d");
      world.inspectCurb(state.selected, true);
      updateNavigationUI();
    },
    "inspect-curb": () => {
      setView("3d");
      world.inspectCurb(state.selected);
      updateNavigationUI();
    },
    "confirm-delete-roundabout": () => {
      closeModal();
      deleteRoad(true);
    },
    "frame-roundabout": () => {
      const e = selectedRoad();
      if (e?.roundabout) {
        world.frameRoundabout(e.roundabout);
        planWorld?.frameRoundabout(e.roundabout);
      }
    },
    "install-landscape-examples": () => {
      const backup = JSON.stringify(project);
      if (
        edit("Installed Harbor landscape examples", () =>
          installLandscapeExamples(project),
        )
      ) {
        try {
          localStorage.setItem("frontier-before-landscape-profiles-v1", backup);
        } catch {
          /* Undo is still available. */
        }
        tourStreet("garden");
        toast(
          "Three landscape examples added. Undo restores the previous profiles.",
        );
      }
    },
    "show-eastbank": showEastbank,
    "show-westhaven": () => {
      setTool("select");
      const points = world.result.roads
        .filter((r) => r.e.district !== CITY_ID)
        .flatMap((r) => [r.a, r.b]);
      world.frameArea(points);
      planWorld?.frameArea(points);
    },
    "landscape-references": landscapeReferences,
    "landscape-references-inspector": landscapeReferences,
    "activate-roundabout": () => {
      const cfg = {
        radius: Number($("#roundabout-radius").value),
        entries: Number($("#roundabout-entries").value),
        rotation: Number($("#roundabout-rotation").value),
        clockwise: $("#roundabout-circulation").value === "clockwise",
      };
      if (
        !Number.isFinite(cfg.radius) ||
        cfg.radius < 24 ||
        cfg.radius > 50 ||
        !Number.isFinite(cfg.rotation) ||
        Math.abs(cfg.rotation) > 360
      ) {
        toast("Use a 24–50 m radius and a rotation between −360° and 360°.");
        return;
      }
      state.roundaboutSettings = cfg;
      closeModal();
      setTool("roundabout");
    },
    "street-presets": streetscapePresets,
    "street-presets-icon": streetscapePresets,
    "street-audit": streetAudit,
    "audit-street": streetAudit,
    "download-street": downloadStreet,
    "gentle-ramp": () =>
      changeRoad(
        "sidewalk",
        Math.max(
          2.4,
          Math.ceil(
            ((streetSettings(selectedRoad()).curbHeight - 0.006) * 12 + 0.45) *
              10,
          ) / 10,
        ),
        "Widened ramp sidewalk",
      ),
    "mobile-inspector": () => $(".inspector").classList.toggle("mobile-open"),
    undo: undo,
    redo: redo,
    save: () =>
      toast(
        save()
          ? "Project saved in this browser."
          : "Browser storage full. Export the project JSON.",
      ),
    export: exportModal,
    "close-modal": closeModal,
    about: helpModal,
    shortcuts: shortcutsModal,
    "library-info": () =>
      toast(
        "Presets are parametric. Select one, then click in the viewport to draw.",
      ),
    "start-draw": () => setTool("draw"),
    "terrain-card": () => {
      state.rightTab = "properties";
      setTool("terrain");
    },
    "finish-draw": () => setTool("select"),
    snap: () => {
      state.snap = !state.snap;
      button.classList.toggle("enabled", state.snap);
      updateDrawing();
      toast(
        state.snap
          ? "Snap to nodes and segments enabled"
          : "Free placement enabled",
      );
    },
    "zoom-in": () => world.zoom(1.2),
    "zoom-out": () => world.zoom(1 / 1.2),
    "zoom-level": () => world.home(),
    "fit-view": () => world.home(),
    "minimap-toggle": () => setView("split"),
    "focus-road": () => {
      world.focus(state.selected);
      planWorld?.focus(state.selected);
    },
    "delete-selected": deleteRoad,
    "topology-card": topologyModal,
    "topology-footer": topologyModal,
    "overlay-topology": topologyModal,
    "inspect-wireframe": () => {
      updateOptions("wireframe");
      toast(
        world.options.wireframe
          ? "Quad wireframe enabled"
          : "Quad wireframe hidden",
      );
    },
    "audit-wireframe": () => {
      world.setOptions({ wireframe: true });
      planWorld?.setOptions({ wireframe: true });
      closeModal();
    },
    "audit-export": exportModal,
    "show-water": () => updateOptions("water"),
    "download-json": () => {
      download(
        "westhaven.frontier.json",
        JSON.stringify(project, null, 2),
        "application/json",
      );
      toast("Editable project exported");
    },
    "download-obj": () => {
      download(
        "westhaven-roads.obj",
        exportOBJ(world.result, project),
        "text/plain",
      );
      toast("Quad road-surface OBJ exported");
    },
    overlays: () => overlaysMenu(button),
    "project-menu": () => projectMenu(button),
    "project-title": () => projectMenu(button),
    "help-menu": () =>
      popover(
        button,
        `<button id="quick-guide">${icon("circle-help")}Quick guide & scope</button><button id="keyboard-guide">${icon("keyboard")}Keyboard shortcuts</button><button id="help-topology">${icon("shield-check")}Topology report</button>`,
      ),
    "quick-guide": helpModal,
    "keyboard-guide": shortcutsModal,
    "help-topology": topologyModal,
    "import-project": () => $("#import-file").click(),
    "export-project": exportModal,
    "rename-project": () =>
      modal(
        "Name your world.",
        "Choose a project name.",
        `<label class="field-label" for="new-project-name">Project name</label><input class="text-input" id="new-project-name" maxlength="60" value="${escape(project.name)}"/>`,
        `<button class="button primary" id="confirm-rename">Save name</button>`,
      ),
    "confirm-rename": () => {
      const name = $("#new-project-name").value.trim();
      if (!name) return;
      edit("Renamed project", () => (project.name = name));
      $("#project-title").innerHTML = `${escape(name)} ${icon("chevron-down")}`;
      closeModal();
      save();
      refreshIcons();
    },
    "reset-project": () =>
      modal(
        "Start fresh in Westhaven?",
        "Your current project will be replaced.",
        `<p class="muted-copy">Export a JSON backup first if you want to keep your edits. You can undo this reset during the current session.</p>`,
        `<button class="button" id="close-modal">Keep editing</button><button class="button primary" id="confirm-reset">Reset demo project</button>`,
      ),
    "confirm-reset": () => {
      edit("Reset demo project", () => {
        project = landscapedProject();
        addSampleCity(project);
        installTransportSamples(project);
        installBridgeNetworks(project);
        state.selected = project.edges.find(
          (e) => e.name === "Willow Lane",
        )?.id;
      });
      closeModal();
      save();
      showEastbank();
      $("#project-title").innerHTML =
        `${escape(project.name)} ${icon("chevron-down")}`;
      refreshIcons();
      toast("Westhaven demo restored");
    },
  };
  actions[id]?.();
  if (button.classList.contains("brand")) {
    event.preventDefault();
    world.home();
  }
});
function projectMenu(anchor) {
  popover(
    anchor,
    `<div class="popover-title">${escape(project.name)}</div><button id="rename-project">${icon("pencil")}Rename project</button><button id="import-project">${icon("upload")}Open project JSON</button><button id="export-project">${icon("download")}Export project</button><div class="popover-divider"></div><button id="reset-project">${icon("rotate-ccw")}Reset demo project</button>`,
  );
}
document.addEventListener("change", (event) => {
  const el = event.target,
    key = el.dataset.property;
  if (el.id === "city-sample") {
    focusBuiltSample(el.value);
    return;
  }
  if (el.id === "landscape-layout") {
    const e = selectedRoad();
    if (e) edit("Changed landscape profile", () => applyLandscape(e, el.value));
    return;
  }
  if (el.dataset.street) {
    changeRoad(el.dataset.street, el.value, "Updated streetscape");
    return;
  }
  if (el.id === "camera-speed") {
    world.fly.setSpeed(Number(el.value));
    updateNavigationUI();
    return;
  }
  if (el.id === "street-width" || el.id === "street-height") {
    const isWidth = el.id === "street-width",
      v = Number(el.value);
    if (
      !Number.isFinite(v) ||
      v < (isWidth ? 0.6 : 0.08) ||
      v > (isWidth ? 6 : 0.22)
    ) {
      renderInspector();
      toast("Value is outside the supported range.");
      return;
    }
    changeRoad(
      isWidth ? "sidewalk" : "curbHeight",
      v,
      "Updated curb dimensions",
    );
    return;
  }
  if (key) {
    if (key === "wireframe") {
      updateOptions("wireframe");
      return;
    }
    if (key === "sidewalk")
      changeRoad("sidewalk", el.checked ? 1.8 : 0, "Changed sidewalks");
    else if (key === "bridge")
      changeRoad(
        "structure",
        el.checked ? "bridge" : "surface",
        "Changed bridge structure",
      );
    else changeRoad(key, el.checked);
    return;
  }
  if (el.id === "lane-width") {
    const value = Number(el.value),
      max = selectedRoad()?.roundaboutRole === "ring" ? 6 : 4.5;
    if (!Number.isFinite(value) || value < 2.5 || value > max) {
      renderInspector();
      toast(`Lane width must be 2.5–${max} m.`);
      return;
    }
    changeRoad("laneWidth", value, "Changed lane width");
  }
  if (el.id === "road-speed") {
    const value = Number(el.value);
    if (!Number.isFinite(value) || value < 10 || value > 130) {
      renderInspector();
      toast("Design speed must be 10–130 km/h.");
      return;
    }
    changeRoad("speed", value, "Changed design speed");
  }
  if (el.id === "road-name") {
    const value = el.value.trim().slice(0, 60);
    if (value) changeRoad("name", value, "Renamed road");
    else renderInspector();
  }
});
$("#library-search").addEventListener("input", (e) => {
  state.filter = e.target.value.toLowerCase();
  renderLibrary();
});
$("#import-file").addEventListener("change", async (e) => {
  const file = e.target.files[0];
  if (!file) return;
  try {
    if (file.size > 10e6)
      throw Error("Project file must be smaller than 10 MB.");
    const data = validateProject(JSON.parse(await file.text()));
    const accepted = edit("Imported project", () => {
      project = data;
      project.corridors ||= [];
      state.selected = project.edges[0]?.id;
    });
    if (!accepted) {
      e.target.value = "";
      return;
    }
    save();
    $("#project-title").innerHTML =
      `${escape(project.name)} ${icon("chevron-down")}`;
    refreshIcons();
    toast("Project opened successfully");
  } catch (error) {
    toast(`Import failed: ${error.message}`);
  }
  e.target.value = "";
});
document.addEventListener("keydown", (e) => {
  if (world?.fly?.active) return;
  if (e.key === "Tab" && $("#modal-root").children.length) {
    const items = $$(
        "#modal-root button:not(:disabled), #modal-root input, #modal-root select",
      ),
      first = items[0],
      last = items.at(-1);
    if (e.shiftKey && document.activeElement === first) {
      e.preventDefault();
      last?.focus();
    } else if (!e.shiftKey && document.activeElement === last) {
      e.preventDefault();
      first?.focus();
    }
    return;
  }
  if (e.key === "Escape") {
    closeModal();
    closePopover();
    setTool("select");
    return;
  }
  if (e.target.closest("input,textarea,select")) return;
  if (e.ctrlKey || e.metaKey) {
    if (e.key.toLowerCase() === "z") {
      e.preventDefault();
      e.shiftKey ? redo() : undo();
    }
    if (e.key.toLowerCase() === "s") {
      e.preventDefault();
      save();
      toast("Project saved");
    }
    return;
  }
  if ($("#modal-root").children.length) return;
  const shortcuts = {
    v: "select",
    r: "draw",
    b: "bridge",
    t: "terrain",
    h: "pan",
    d: "delete",
  };
  if (shortcuts[e.key.toLowerCase()]) setTool(shortcuts[e.key.toLowerCase()]);
  if (e.key === "g") $("#snap").click();
  if (e.key === "f") world.home();
  if (e.key === "?") shortcutsModal();
  if (e.key === "/") {
    e.preventDefault();
    $("#library-search").focus();
  }
  if (e.key === "Delete" || e.key === "Backspace") {
    e.preventDefault();
    deleteRoad();
  }
});

await bootStage("Building 3D geometry and checking connections…", 5);
world = new World($("#world-view"), {
  onSelect: selectRoad,
  onGround: groundClick,
  onHover: hover,
  onNodeMove: nodeMoved,
  onChange: () => {
    updateNavigationUI();
  },
});
world.rebuild(project, cityPrepared);
cityPrepared = null;
world.select(state.selected);
if (networksAdded) {
  focusBuiltSample("network:loop");
} else if (project.sampleCityVersion === 1) {
  world.frameArea(
    project.nodes.filter(
      (n) =>
        n.x >= project.cityOrigin.x &&
        n.x <= project.cityOrigin.x + 480 &&
        Math.abs(n.z - project.cityOrigin.z) < 200,
    ),
    30,
  );
} else if (welcomeRoundabout) {
  const e = project.edges.find((e) => e.roundaboutRole === "ring");
  if (e) world.frameRoundabout(e.roundabout);
}
const labels = [
  ...(project.sampleCityVersion === 1
    ? [
        {
          name: "EASTBANK CITY",
          sub: "TRAM + LANDSCAPE DISTRICT",
          x: project.cityOrigin.x + 240,
          z: project.cityOrigin.z - 40,
          cls: "town-label",
        },
      ]
    : []),
  { name: "WESTHAVEN", sub: "TOWN CENTER", x: 66, z: -30, cls: "town-label" },
  { name: "Willow River", x: -116, z: 110, cls: "water-label" },
  { name: "OLD MILL FARMLANDS", x: -230, z: 100, cls: "region-label" },
  { name: "NORTHWOOD", x: -210, z: -185, cls: "region-label" },
  { name: "EASTFIELD", x: 231, z: -108, cls: "region-label" },
];
$("#scene-labels").innerHTML = labels
  .map(
    (l) =>
      `<div class="map-label ${l.cls}"><span>${l.name}</span>${l.sub ? `<small>${l.sub}</small>` : ""}</div>`,
  )
  .join("");
world.labelsCallback = (w) => {
  const m = w.camera.matrixWorld.elements;
  const compass = $(".compass-diamond svg");
  if (compass)
    compass.style.transform = `rotate(${-45 - (Math.atan2(-m[8], m[10]) * 180) / Math.PI}deg)`;
  const elements = $$("#scene-labels .map-label");
  labels.forEach((l, i) => {
    const pt = w.project(l.x, 3, l.z);
    const visible =
      pt.visible &&
      pt.x > 60 &&
      pt.x < w.container.clientWidth - 60 &&
      pt.y > 80 &&
      pt.y < w.container.clientHeight - 85;
    elements[i].style.transform =
      `translate(${pt.x}px,${pt.y}px) translate(-50%,-50%)`;
    elements[i].style.display = visible ? "" : "none";
  });
};
await bootStage("Opening the editor…", 6);
setTheme(document.documentElement.dataset.theme || "dark", false);
renderLibrary();
renderInspector();
renderMinimap();
updateStats();
updateNavigationUI();
refreshIcons();
save();
if (cityError)
  toast(`Eastbank could not be added; your map is unchanged. ${cityError}`);
window.addEventListener("beforeunload", () => {
  if (state.dirty) save();
});
// Test/debug entry point: no separate state is used by the UI.
window.frontier = {
  get project() {
    return project;
  },
  get world() {
    return world;
  },
  get state() {
    return state;
  },
  audit: () => auditMesh(world.result, project),
  setView,
  selectRoad,
};
