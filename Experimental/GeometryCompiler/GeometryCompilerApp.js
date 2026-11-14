/*============================================================================================================================================
                                                          GEOMETRYCOMPILERAPP.JS
============================================================================================================================================*/
// Standalone WebGPU Geometry Compiler workspace controller following Project Zero Editor UI conventions.

import {
  OPTICAL_CATEGORIES,
  SPECIMEN_CATALOG,
  constructSpecimen,
  parseObjText,
  parseStlBytes,
  compileSparseSdfVolume,
  evictMip0ForBudget,
  extractSdfSliceRgba,
  extractDualContourSurface,
  compileClusterLodDag,
  bakeTransparentSpecimenIcon,
  serializeVolumeFvol,
} from "/Experimental/GeometryCompiler/Engine/CompilerSolvers.js";
import { GeometryCompilerViewport } from "/Experimental/GeometryCompiler/Engine/WebGpuViewport.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    EXPORT & FORMATTING
//------------------------------------------------------------------------------------------------------------------------

function rgbaToDataUrl(rgba, width, height) {
  const c = document.createElement("canvas");
  c.width = width;
  c.height = height;
  const ctx = c.getContext("2d");
  const img = ctx.createImageData(width, height);
  img.data.set(rgba);
  ctx.putImageData(img, 0, 0);
  return c.toDataURL("image/png");
}

function triggerDownload(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function sliderFillStyle(val, min, max) {
  const pct = Math.max(0, Math.min(100, ((val - min) / Math.max(1e-6, max - min)) * 100));
  return `--fill:${pct.toFixed(1)}%`;
}

function cornerAlphaMax(icon) {
  const { rgba, width, height } = icon;
  const corners = [
    0,
    (width - 1) * 4,
    ((height - 1) * width) * 4,
    ((height - 1) * width + (width - 1)) * 4,
  ];
  return Math.max(...corners.map((i) => rgba[i + 3]));
}

function resolveAutoLodIndex(distance) {
  if (distance < 2.15) return 0;
  if (distance < 3.05) return 1;
  if (distance < 4.15) return 2;
  return 3;
}

const METHOD_LABELS = {
  retopo: "Retopo + Shrinkwrap (Compare)",
  naniteQem: "Nanite-Style QEM (Compare)",
  hybrid: "Hybrid · Production Default",
};

const HYBRID_SW_LABELS = {
  adaptive: "Auto (LOD 2–3)",
  allLods: "All LODs",
  strictLow: "Strict 2–3",
  off: "Off",
};

const SDF_VIZ_LABELS = [
  "Iso + AO",
  "3D Cutaway",
  "Step Heat",
  "Sparse Bricks",
];

//------------------------------------------------------------------------------------------------------------------------
//                                                    APPLICATION STATE
//------------------------------------------------------------------------------------------------------------------------

const state = {
  specimens: [],
  activeSpecimenId: "GlassVessel",
  openTabs: ["GlassVessel", "UtahTeapot", "ChamferBox"],
  searchQuery: "",
  categoryFilter: "All",
  viewMode: "quadCompare", // quadCompare | surface | clusterLod | sdfRaymarch | sdfSlice | dualContour | iconStudio
  wireframe: true,
  showGuides: true,
  autoOrbit: true,
  shelfOpen: false,
  leftWidth: 288,
  rightWidth: 356,

  // 4-Way Multi-LOD & Retopology Guide Policy (Hybrid is default production path; Retopo & Nanite QEM are comparison-only)
  opticalCategory: "DielectricGlass",
  activeMethod: "hybrid", // "retopo" | "naniteQem" | "hybrid" (production default)
  lodSelection: "2", // "0" | "1" | "2" | "3" | "auto"
  activeLodIndex: 2,
  maxTrisPerCluster: 128,
  minWallThicknessMm: 45,
  maxNormalDeviationDeg: 14,
  lockDualWalls: true,

  // Retopo Guide, Seam & Probabilistic QEM Controls
  useEdgeGuides: true,
  creaseAngleDeg: 32,
  lockUvSeams: true,
  lockBoundaries: true,
  featureGuideWeight: 0.75,
  shrinkwrapPasses: 2,
  probQemSigma: 0.035,
  naniteShrinkwrap: false,
  hybridShrinkwrapMode: "adaptive", // "adaptive" | "allLods" | "strictLow" | "off"
  regionalAdaptive: true,
  coarseRetopoSeed: true,

  // Sparse 8x8x8 SDF Baker + 3D Raymarch Visualizer + 2D Slice Scrubber (up to 128^3!)
  sdfDim: 32, // 16 | 32 | 64 | 96 | 128
  vramBudgetKb: 2048,
  sdfSliceAxis: 0, // 0=Z, 1=Y, 2=X
  sdfSliceLevel: 0, // 0=Mip0+Mip1, 1=Mip1
  sdfSliceDepth: 0.5,
  sdfVizMode: 0, // 0=Iso+AO, 1=3D Cutaway, 2=Step Heat, 3=Sparse Bricks
  sdfIsoOffsetMm: 0, // -40..+40 mm
  sdfCutFlip: false,
  sdfAutoSweep: false,
  sdfSweepDir: 1,

  // Interactive Orbit Camera
  camera: {
    yaw: 0.68,
    pitch: 0.34,
    distance: 2.45,
    target: [0, 0, 0.55],
  },
};

let compiled = null;
let viewport = null;
let lastCompileMs = 0;

//------------------------------------------------------------------------------------------------------------------------
//                                              SPECIMEN CATALOG & COMPILER
//------------------------------------------------------------------------------------------------------------------------

function bootstrapSpecimens() {
  state.specimens = SPECIMEN_CATALOG.map((entry) => {
    const geometry = constructSpecimen(entry.id);
    const icon = bakeTransparentSpecimenIcon(geometry, {
      resolutionPx: 128,
      supersample: 2,
      opticalCategory: entry.defaultOptical,
    });
    return {
      id: entry.id,
      title: entry.title,
      subtitle: entry.subtitle,
      category: entry.defaultOptical === "DielectricGlass" ? "Dielectric" : "Solid",
      opticalCategory: entry.defaultOptical,
      custom: false,
      geometry,
      iconDataUrl: rgbaToDataUrl(icon.rgba, icon.width, icon.height),
      iconMeta: { ...icon, cornerAlpha: cornerAlphaMax(icon) },
    };
  });
}

function getActiveSpecimen() {
  return state.specimens.find((s) => s.id === state.activeSpecimenId) || state.specimens[0];
}

function syncActiveLodIndex() {
  state.activeLodIndex = state.lodSelection === "auto"
    ? resolveAutoLodIndex(state.camera.distance)
    : Number(state.lodSelection);
}

function getMethodLod(clusterDag, method, lodIdx) {
  if (method === "retopo") return clusterDag.retopoLods[lodIdx] || clusterDag.originalLod;
  if (method === "naniteQem") return clusterDag.naniteQemLods[lodIdx] || clusterDag.originalLod;
  return clusterDag.hybridLods[lodIdx] || clusterDag.originalLod;
}

function buildLodPolicy() {
  return {
    opticalCategory: state.opticalCategory,
    activeMethod: state.activeMethod,
    maxTrisPerCluster: state.maxTrisPerCluster,
    minWallThicknessMeters: state.minWallThicknessMm * 0.001,
    maxNormalDeviationDeg: state.maxNormalDeviationDeg,
    lockDualWalls: state.lockDualWalls,
    useEdgeGuides: state.useEdgeGuides,
    creaseAngleDeg: state.creaseAngleDeg,
    lockUvSeams: state.lockUvSeams,
    lockBoundaries: state.lockBoundaries,
    featureGuideWeight: state.featureGuideWeight,
    shrinkwrapPasses: state.shrinkwrapPasses,
    probQemSigma: state.probQemSigma,
    naniteShrinkwrap: state.naniteShrinkwrap,
    hybridShrinkwrapMode: state.hybridShrinkwrapMode,
    regionalAdaptive: state.regionalAdaptive,
    coarseRetopoSeed: state.coarseRetopoSeed,
  };
}

function compileActiveSpecimen(syncOpticalFromAsset = false) {
  const t0 = performance.now();
  const spec = getActiveSpecimen();
  if (!spec) return;
  if (syncOpticalFromAsset) {
    state.opticalCategory = spec.opticalCategory;
    state.lockDualWalls = spec.opticalCategory === "DielectricGlass" || spec.opticalCategory === "ThinFoliage";
    state.camera.target = [...spec.geometry.boundsCenter];
  }
  syncActiveLodIndex();

  const geometry = spec.geometry;
  const clusterDag = compileClusterLodDag(geometry, buildLodPolicy());

  const sdfVolume = compileSparseSdfVolume(geometry, {
    gridDim: state.sdfDim,
  });
  const eviction = evictMip0ForBudget(sdfVolume, state.vramBudgetKb * 1024);

  const sdfSliceSize = 256;
  const useMip1 = state.sdfSliceLevel === 1 || sdfVolume.mip0Evicted;
  const sdfSliceRgba = extractSdfSliceRgba(sdfVolume, state.sdfSliceAxis, state.sdfSliceDepth, sdfSliceSize, useMip1);

  const dualContour = extractDualContourSurface(sdfVolume, `${spec.title} Dual-Contour`);

  const icon = bakeTransparentSpecimenIcon(geometry, {
    resolutionPx: 128,
    supersample: 2,
    opticalCategory: state.opticalCategory,
  });
  const iconMeta = { ...icon, cornerAlpha: cornerAlphaMax(icon) };
  spec.iconMeta = iconMeta;
  spec.iconDataUrl = rgbaToDataUrl(icon.rgba, icon.width, icon.height);

  compiled = {
    geometry,
    clusterDag,
    sdfVolume,
    eviction,
    sdfSliceRgba,
    sdfSliceSize,
    dualContour,
    icon: iconMeta,
    iconDataUrl: spec.iconDataUrl,
  };

  if (viewport) {
    viewport.uploadCompiledAsset(compiled);
  }
  lastCompileMs = performance.now() - t0;
}

function recompileClusterLodOnly() {
  if (!compiled) { compileActiveSpecimen(false); return; }
  const t0 = performance.now();
  syncActiveLodIndex();
  compiled.clusterDag = compileClusterLodDag(compiled.geometry, buildLodPolicy());
  if (viewport) viewport.uploadCompiledAsset(compiled);
  lastCompileMs = performance.now() - t0;
}

function recompileSdfVolumeOnly() {
  if (!compiled) { compileActiveSpecimen(false); return; }
  const t0 = performance.now();
  const spec = getActiveSpecimen();
  const sdfVolume = compileSparseSdfVolume(compiled.geometry, { gridDim: state.sdfDim });
  const eviction = evictMip0ForBudget(sdfVolume, state.vramBudgetKb * 1024);
  const useMip1 = state.sdfSliceLevel === 1 || sdfVolume.mip0Evicted;
  compiled.sdfVolume = sdfVolume;
  compiled.eviction = eviction;
  compiled.sdfSliceRgba = extractSdfSliceRgba(sdfVolume, state.sdfSliceAxis, state.sdfSliceDepth, compiled.sdfSliceSize, useMip1);
  compiled.dualContour = extractDualContourSurface(sdfVolume, `${spec.title} Dual-Contour`);
  if (viewport) viewport.uploadCompiledAsset(compiled);
  lastCompileMs = performance.now() - t0;
}

function refreshSdfSliceOnly() {
  if (!compiled?.sdfVolume) return;
  if (state.viewMode === "sdfSlice") {
    const useMip1 = state.sdfSliceLevel === 1 || compiled.sdfVolume.mip0Evicted;
    compiled.sdfSliceRgba = extractSdfSliceRgba(
      compiled.sdfVolume,
      state.sdfSliceAxis,
      state.sdfSliceDepth,
      compiled.sdfSliceSize,
      useMip1,
    );
  }
  const pctText = `${Math.round(state.sdfSliceDepth * 100)}%`;
  const axisNames = ["Z", "Y", "X"];
  const v = compiled.sdfVolume;
  const ax = state.sdfSliceAxis;
  const minVal = v.boundsMin[ax === 0 ? 2 : ax === 1 ? 1 : 0];
  const maxVal = v.boundsMax[ax === 0 ? 2 : ax === 1 ? 1 : 0];
  const meters = (minVal + state.sdfSliceDepth * (maxVal - minVal)).toFixed(2);

  const hudOut = document.getElementById("HudSliceOut");
  if (hudOut) hudOut.textContent = `${axisNames[ax]} ${pctText} (${meters}m)`;
  const hudPill = document.getElementById("HudSliceTitle");
  if (hudPill) hudPill.textContent = `Sparse 8×8×8 SDF · ${v.mip0Evicted ? "Mip1 (Mip0 Evicted)" : `Mip${state.sdfSliceLevel}`} · ${axisNames[ax]}=${pctText}`;
  const hudRng = document.getElementById("HudSliceRange");
  if (hudRng) {
    hudRng.value = String(state.sdfSliceDepth);
    hudRng.style.setProperty("--fill", `${((state.sdfSliceDepth - 0.02) / 0.96 * 100).toFixed(1)}%`);
  }
  const inspRng = document.getElementById("RngSliceDepth");
  if (inspRng) {
    inspRng.value = String(state.sdfSliceDepth);
    inspRng.style.setProperty("--fill", `${((state.sdfSliceDepth - 0.02) / 0.96 * 100).toFixed(1)}%`);
  }
  const inspOut = document.getElementById("OutSliceDepth");
  if (inspOut) inspOut.textContent = pctText;
}

function selectSpecimen(id) {
  state.activeSpecimenId = id;
  if (!state.openTabs.includes(id)) {
    state.openTabs.push(id);
  }
  compileActiveSpecimen(true);
  renderUi();
}

async function importGeometryFile(file) {
  if (!file) return;
  const lower = file.name.toLowerCase();
  let geometry;
  if (lower.endsWith(".stl")) {
    const buf = await file.arrayBuffer();
    geometry = parseStlBytes(buf, file.name);
  } else {
    const text = await file.text();
    geometry = parseObjText(text, file.name);
  }
  const id = `Custom_${Date.now()}`;
  const icon = bakeTransparentSpecimenIcon(geometry, { resolutionPx: 128, supersample: 2, opticalCategory: "OpaqueSolid" });
  const item = {
    id,
    title: file.name.replace(/\.(obj|stl)$/i, ""),
    subtitle: `Imported ${lower.endsWith(".stl") ? "STL" : "OBJ"} asset`,
    category: "Custom",
    opticalCategory: "OpaqueSolid",
    custom: true,
    geometry,
    iconDataUrl: rgbaToDataUrl(icon.rgba, icon.width, icon.height),
    iconMeta: { ...icon, cornerAlpha: cornerAlphaMax(icon) },
  };
  state.specimens.unshift(item);
  selectSpecimen(id);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    UI SHELL RENDERING
//------------------------------------------------------------------------------------------------------------------------

function buildShellHtml() {
  return `
  <div class="workspace" id="WorkspaceRoot" style="--left:${state.leftWidth}px;--right:${state.rightWidth}px">
    <!-- LEFT DOCK: SPECIMENS & COMPILER QUEUE -->
    <aside class="dock left">
      <div class="tab-strip">
        <button class="document-tab active" type="button">Specimens</button>
        <button class="document-tab" type="button" id="BtnOpenShelfTab">Shelf</button>
      </div>
      <div class="divider left" id="DividerLeft"></div>

      <div class="outliner-heading">
        <div>
          <h1>COMPILER</h1>
          <small>WEBGPU · 4-WAY LOD · SDF</small>
        </div>
        <button type="button" id="BtnImportLeft">+ .OBJ / .STL</button>
      </div>

      <div class="stats" id="LeftStats"></div>

      <div class="outliner-search">
        <label>
          <span>⌕</span>
          <input type="text" id="SearchInput" placeholder="Filter geometry specimens..." value="${state.searchQuery}" />
        </label>
      </div>

      <div class="filter-chips" id="FilterChips"></div>
      <div class="outliner-rows" id="OutlinerRows"></div>
      <div class="pipeline-checklist" id="PipelineChecklist"></div>
      <div class="outliner-footer" id="OutlinerFooter"></div>
    </aside>

    <!-- CENTER DOCK: WEBGPU 4-WAY VIEWPORT & DOCUMENT TABS -->
    <main class="dock center">
      <div class="tab-strip" id="CenterTabStrip"></div>

      <div class="viewport-toolbar">
        <div class="viewport-heading-row">
          <div class="viewport-identity" id="ViewportIdentity"></div>
          <div class="viewport-modes" id="ViewportModes"></div>
        </div>
        <div class="viewport-action-row" id="ViewportActions"></div>
      </div>

      <div class="viewport-stage" id="ViewportStage">
        <canvas id="WebGpuCanvas"></canvas>
        <canvas id="OverlayCanvas"></canvas>
        <div class="hud-overlay" id="ViewportHud"></div>
        <div class="drop-hint" id="DropHint">Drop .OBJ or .STL mesh to compare 4-Way LODs, Sparse SDF &amp; 128px Icon</div>
      </div>

      <div class="viewport-footer" id="ViewportFooter"></div>
    </main>

    <!-- RIGHT DOCK: PROJECT ZERO INSPECTOR CARDS -->
    <aside class="dock right">
      <div class="tab-strip">
        <button class="document-tab active" type="button">Inspector</button>
      </div>
      <div class="divider right" id="DividerRight"></div>

      <div class="inspector-scroll" id="InspectorScroll"></div>
      <div class="inspector-footer" id="InspectorFooter"></div>
    </aside>
  </div>

  <!-- PROJECT ZERO BOTTOM CONTENT SHELF DRAWER -->
  <section class="shelf-drawer" id="ShelfDrawer">
    <div class="shelf-header">
      <strong>Auto-Baked Transparent 128×128 Asset Shelf (Alpha=0 Background)</strong>
      <div style="display:flex;gap:8px;align-items:center">
        <button type="button" id="BtnShelfImport">+ Import .OBJ / .STL</button>
        <button type="button" id="BtnCloseShelf">Close</button>
      </div>
    </div>
    <div class="shelf-grid" id="ShelfGrid"></div>
  </section>
  `;
}

function renderViewportHud() {
  if (!compiled) return;
  const { clusterDag, sdfVolume, dualContour, icon } = compiled;
  const lodIdx = state.activeLodIndex;
  const origLod = clusterDag.originalLod;
  const rLod = clusterDag.retopoLods[lodIdx];
  const qLod = clusterDag.naniteQemLods[lodIdx];
  const hLod = clusterDag.hybridLods[lodIdx];
  const activeLod = getMethodLod(clusterDag, state.activeMethod, lodIdx);
  const axisNames = ["Z", "Y", "X"];

  // Lowest symmetric Hausdorff error among the 3 LOD solvers (preferring 0 breaches)
  const candidates = [
    { key: "retopo", lod: rLod },
    { key: "naniteQem", lod: qLod },
    { key: "hybrid", lod: hLod },
  ].sort((a, b) => (a.lod.wallCollapseCount - b.lod.wallCollapseCount) || (a.lod.meanErrorMm - b.lod.meanErrorMm));
  const bestKey = lodIdx > 0 ? candidates[0].key : null;

  const hudEl = document.getElementById("ViewportHud");

  if (state.viewMode === "quadCompare") {
    const badgeMeta = (lod, extra) => `
      <span class="qb-meta">
        <b>${lod.triangleCount.toLocaleString()}</b> tris (-${lod.reductionPercent}%) ·
        err <b class="${lod.meanErrorMm <= 10 ? "good" : "warn"}">${lod.meanErrorMm}mm</b> ·
        slv <b class="${lod.sliverPercent <= 10 ? "good" : "warn"}">${lod.sliverPercent}%</b> ·
        b=<b class="${lod.wallCollapseCount === 0 ? "good" : "bad"}">${lod.wallCollapseCount}</b>${extra}
      </span>
    `;
    hudEl.innerHTML = `
      <div class="quad-grid-hud">
        <div class="quad-cell">
          <div class="quad-badge reference">
            <span class="qb-title"><span class="dot"></span>1. Original (LOD 0 Reference)</span>
            <span class="qb-meta"><b>${origLod.triangleCount.toLocaleString()}</b> tris · guides <b class="warn">${origLod.guideEdgeCount}</b> (crease ${origLod.creaseEdgeCount}, seam ${origLod.seamEdgeCount})</span>
          </div>
        </div>
        <div class="quad-cell">
          <button type="button" class="quad-badge ${state.activeMethod === "retopo" ? "selected" : ""}" data-pick-method="retopo">
            <span class="qb-title"><span class="dot ${state.activeMethod === "retopo" ? "amber" : ""}"></span>2. Retopo + Shrinkwrap (LOD${lodIdx})${bestKey === "retopo" ? ' <span class="quad-winner-tag">Lowest Err</span>' : ""}</span>
            ${badgeMeta(rLod, ` · ${rLod.clusterCount} grp`)}
          </button>
        </div>
        <div class="quad-cell">
          <button type="button" class="quad-badge ${state.activeMethod === "naniteQem" ? "selected" : ""}" data-pick-method="naniteQem">
            <span class="qb-title"><span class="dot ${state.activeMethod === "naniteQem" ? "amber" : ""}"></span>3. Nanite-Style QEM${state.naniteShrinkwrap ? " + Int.SW" : ""} (LOD${lodIdx})${bestKey === "naniteQem" ? ' <span class="quad-winner-tag">Lowest Err</span>' : ""}</span>
            ${badgeMeta(qLod, ` · ${qLod.groupLockedVerts} lock`)}
          </button>
        </div>
        <div class="quad-cell">
          <button type="button" class="quad-badge ${state.activeMethod === "hybrid" ? "selected" : ""}" data-pick-method="hybrid">
            <span class="qb-title"><span class="dot ${state.activeMethod === "hybrid" ? "amber" : ""}"></span>4. Hybrid · ${hLod.retopoSeeded ? "Retopo→QEM" : "Prob-QEM"} + SW [${HYBRID_SW_LABELS[state.hybridShrinkwrapMode]}] (LOD${lodIdx})${bestKey === "hybrid" ? ' <span class="quad-winner-tag">Best</span>' : ""}</span>
            ${badgeMeta(hLod, ` · minA ${hLod.avgMinAngleDeg}°`)}
          </button>
        </div>
      </div>
      <div class="hud-row"></div>
      <div class="hud-row" style="justify-content:center;position:relative;z-index:4">
        <div class="hud-bar" style="margin:0 auto">
          <div class="hud-seg">
            <button type="button" data-lod="0" class="${state.lodSelection === "0" ? "active" : ""}">LOD 0 · 100%</button>
            <button type="button" data-lod="1" class="${state.lodSelection === "1" ? "active" : ""}">LOD 1 · 50%</button>
            <button type="button" data-lod="2" class="${state.lodSelection === "2" ? "active" : ""}">LOD 2 · 25%</button>
            <button type="button" data-lod="3" class="${state.lodSelection === "3" ? "active" : ""}">LOD 3 · 12%</button>
            <button type="button" data-lod="auto" class="${state.lodSelection === "auto" ? "active" : ""}">Auto (L${lodIdx})</button>
          </div>
          <div class="hud-seg">
            <button type="button" data-pick-method="retopo" class="${state.activeMethod === "retopo" ? "active" : ""}">Use Retopo</button>
            <button type="button" data-pick-method="naniteQem" class="${state.activeMethod === "naniteQem" ? "active" : ""}">Use Nanite QEM</button>
            <button type="button" data-pick-method="hybrid" class="${state.activeMethod === "hybrid" ? "active" : ""}">Use Hybrid</button>
          </div>
        </div>
      </div>
    `;
  } else {
    let hudTop = "";
    let hudBottom = "";

    if (state.viewMode === "sdfRaymarch") {
      const pctText = `${Math.round(state.sdfSliceDepth * 100)}%`;
      const ax = state.sdfSliceAxis;
      const minVal = sdfVolume.boundsMin[ax === 0 ? 2 : ax === 1 ? 1 : 0];
      const maxVal = sdfVolume.boundsMax[ax === 0 ? 2 : ax === 1 ? 1 : 0];
      const meters = (minVal + state.sdfSliceDepth * (maxVal - minVal)).toFixed(2);
      const mipLabel = sdfVolume.mip0Evicted
        ? "Mip1 (Mip0 Evicted)"
        : state.sdfSliceLevel === 1
          ? "Mip1 Coarse"
          : "Hierarchical Mip1 → 8³ Bricks";
      const engineLabel = viewport?.webGpuAvailable ? "WebGPU Raymarch" : "CPU Sparse Raymarch";
      hudTop = `
        <div class="hud-pill"><span class="dot amber"></span><span>3D Sparse SDF · ${sdfVolume.dim}³ · ${SDF_VIZ_LABELS[state.sdfVizMode]} · ${mipLabel}</span></div>
        <div class="hud-pill safe"><span class="dot"></span><span>${sdfVolume.allocatedBrickCount}/${sdfVolume.totalBricks} bricks (${sdfVolume.prunedPercent}% skipped) · ${engineLabel}</span></div>
      `;
      const cutawayControls = state.sdfVizMode === 1
        ? `
          <div class="hud-seg">
            <button type="button" data-hud-axis="0" class="${state.sdfSliceAxis === 0 ? "active" : ""}">Z</button>
            <button type="button" data-hud-axis="1" class="${state.sdfSliceAxis === 1 ? "active" : ""}">Y</button>
            <button type="button" data-hud-axis="2" class="${state.sdfSliceAxis === 2 ? "active" : ""}">X</button>
          </div>
          <input type="range" id="HudSliceRange" class="scrub-slider" style="width:130px;${sliderFillStyle(state.sdfSliceDepth, 0.02, 0.98)}" min="0.02" max="0.98" step="0.005" value="${state.sdfSliceDepth}" />
          <output id="HudSliceOut">${axisNames[ax]} ${pctText} (${meters}m)</output>
          <button type="button" id="BtnSdfCutFlip" style="min-height:24px;height:24px;padding:0 8px;font-size:10px" class="${state.sdfCutFlip ? "active-pill" : ""}">Flip</button>
          <button type="button" id="BtnSdfSweep" style="min-height:24px;height:24px;padding:0 9px;font-size:10px" class="${state.sdfAutoSweep ? "active-pill" : ""}">${state.sdfAutoSweep ? "❚❚" : "▶"}</button>
        `
        : `
          <div class="hud-seg">
            <button type="button" data-hud-mip="0" class="${state.sdfSliceLevel === 0 ? "active" : ""}">Mip0+1</button>
            <button type="button" data-hud-mip="1" class="${state.sdfSliceLevel === 1 ? "active" : ""}">Mip1</button>
          </div>
          <span style="font-size:10px;color:#98a0b0;padding:0 4px">Iso</span>
          <input type="range" id="HudIsoRange" class="scrub-slider" style="width:110px;${sliderFillStyle(state.sdfIsoOffsetMm, -40, 40)}" min="-40" max="40" step="1" value="${state.sdfIsoOffsetMm}" />
          <output id="HudIsoOut" style="min-width:44px">${state.sdfIsoOffsetMm >= 0 ? "+" : ""}${state.sdfIsoOffsetMm}mm</output>
        `;
      hudBottom = `
        <div class="hud-bar sdf-scrubber" id="SdfRayHudBar">
          <div class="hud-seg">
            <button type="button" data-sdf-viz="0" class="${state.sdfVizMode === 0 ? "active" : ""}">Iso + AO</button>
            <button type="button" data-sdf-viz="1" class="${state.sdfVizMode === 1 ? "active" : ""}">3D Cutaway</button>
            <button type="button" data-sdf-viz="2" class="${state.sdfVizMode === 2 ? "active" : ""}">Step Heat</button>
            <button type="button" data-sdf-viz="3" class="${state.sdfVizMode === 3 ? "active" : ""}">Sparse Bricks</button>
          </div>
          ${cutawayControls}
        </div>
      `;
    } else if (state.viewMode === "sdfSlice") {
      const pctText = `${Math.round(state.sdfSliceDepth * 100)}%`;
      hudTop = `
        <div class="hud-pill"><span class="dot amber"></span><span id="HudSliceTitle">Sparse 8×8×8 SDF · ${sdfVolume.mip0Evicted ? "Mip1 (Mip0 Evicted)" : `Mip${state.sdfSliceLevel}`} · ${axisNames[state.sdfSliceAxis]}=${pctText}</span></div>
        <div class="hud-pill safe"><span class="dot"></span><span>Conservative Mip1 ≤ Mip0 · ${sdfVolume.monotonicityVerified ? "VERIFIED" : "CHECK"}</span></div>
      `;
      const ax = state.sdfSliceAxis;
      const minVal = sdfVolume.boundsMin[ax === 0 ? 2 : ax === 1 ? 1 : 0];
      const maxVal = sdfVolume.boundsMax[ax === 0 ? 2 : ax === 1 ? 1 : 0];
      const meters = (minVal + state.sdfSliceDepth * (maxVal - minVal)).toFixed(2);
      hudBottom = `
        <div class="hud-bar sdf-scrubber" id="SdfScrubberBar">
          <div class="hud-seg">
            <button type="button" data-hud-axis="0" class="${state.sdfSliceAxis === 0 ? "active" : ""}">Z</button>
            <button type="button" data-hud-axis="1" class="${state.sdfSliceAxis === 1 ? "active" : ""}">Y</button>
            <button type="button" data-hud-axis="2" class="${state.sdfSliceAxis === 2 ? "active" : ""}">X</button>
          </div>
          <div class="hud-seg">
            <button type="button" data-hud-mip="0" class="${state.sdfSliceLevel === 0 ? "active" : ""}">Mip0</button>
            <button type="button" data-hud-mip="1" class="${state.sdfSliceLevel === 1 ? "active" : ""}">Mip1</button>
          </div>
          <input type="range" id="HudSliceRange" class="scrub-slider" min="0.02" max="0.98" step="0.005" value="${state.sdfSliceDepth}" style="${sliderFillStyle(state.sdfSliceDepth, 0.02, 0.98)}" />
          <output id="HudSliceOut">${axisNames[ax]} ${pctText} (${meters}m)</output>
          <button type="button" id="BtnSdfSweep" style="min-height:24px;height:24px;padding:0 10px;font-size:10px" class="${state.sdfAutoSweep ? "active-pill" : ""}">${state.sdfAutoSweep ? "❚❚ Pause" : "▶ Sweep"}</button>
        </div>
      `;
    } else if (state.viewMode === "iconStudio") {
      hudTop = `
        <div class="hud-pill safe"><span class="dot"></span><span>128×128 2×2 SSAA Transparent Asset Icon · Corner α=${icon.cornerAlpha}</span></div>
        <div class="hud-pill"><span>Silhouette ${icon.silhouettePercent}% · Transparent ${icon.transparentPercent}%</span></div>
      `;
    } else {
      hudTop = `
        <div class="hud-pill"><span class="dot"></span><span>${
          state.viewMode === "dualContour"
            ? `QEF Dual-Contour · ${dualContour.extractedTriangleCount} tris (${dualContour.qefSolvedCells} QEF cells)`
            : `${METHOD_LABELS[state.activeMethod]} LOD${lodIdx} · ${activeLod.triangleCount} tris (-${activeLod.reductionPercent}%) · ${activeLod.meanErrorMm}mm err · ${activeLod.clusterCount} clusters (≤${activeLod.maxClusterTriangles} tris)`
        }</span></div>
      `;
      if (state.viewMode === "clusterLod" || state.viewMode === "surface") {
        hudBottom = `
          <div class="hud-bar" style="margin:0 auto">
            <div class="hud-seg">
              <button type="button" data-lod="0" class="${state.lodSelection === "0" ? "active" : ""}">LOD 0</button>
              <button type="button" data-lod="1" class="${state.lodSelection === "1" ? "active" : ""}">LOD 1</button>
              <button type="button" data-lod="2" class="${state.lodSelection === "2" ? "active" : ""}">LOD 2</button>
              <button type="button" data-lod="3" class="${state.lodSelection === "3" ? "active" : ""}">LOD 3</button>
              <button type="button" data-lod="auto" class="${state.lodSelection === "auto" ? "active" : ""}">Auto (L${lodIdx})</button>
            </div>
            <div class="hud-seg">
              <button type="button" data-pick-method="retopo" class="${state.activeMethod === "retopo" ? "active" : ""}">Retopo</button>
              <button type="button" data-pick-method="naniteQem" class="${state.activeMethod === "naniteQem" ? "active" : ""}">Nanite QEM</button>
              <button type="button" data-pick-method="hybrid" class="${state.activeMethod === "hybrid" ? "active" : ""}">Hybrid</button>
            </div>
          </div>
        `;
      }
    }

    hudEl.innerHTML = `
      <div class="hud-row">${hudTop}</div>
      <div class="hud-row" style="justify-content:center">${hudBottom}</div>
    `;
  }

  // Stop pointer propagation on interactive HUD widgets so dragging/clicking never steals orbit
  document.querySelectorAll(".hud-bar, .hud-pill, .quad-badge").forEach((el) => {
    el.addEventListener("pointerdown", (e) => e.stopPropagation());
  });
  document.querySelectorAll("[data-lod]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.lodSelection = btn.dataset.lod;
      syncActiveLodIndex();
      renderUi();
    });
  });
  document.querySelectorAll("[data-pick-method]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.activeMethod = btn.dataset.pickMethod;
      renderUi();
    });
  });
  document.querySelectorAll("[data-hud-axis]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.sdfSliceAxis = Number(btn.dataset.hudAxis);
      refreshSdfSliceOnly();
      renderUi();
    });
  });
  document.querySelectorAll("[data-hud-mip]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.sdfSliceLevel = Number(btn.dataset.hudMip);
      refreshSdfSliceOnly();
      renderUi();
    });
  });
  document.getElementById("HudSliceRange")?.addEventListener("input", (e) => {
    state.sdfAutoSweep = false;
    state.sdfSliceDepth = Number(e.target.value);
    refreshSdfSliceOnly();
  });
  document.getElementById("BtnSdfSweep")?.addEventListener("click", () => {
    state.sdfAutoSweep = !state.sdfAutoSweep;
    renderViewportHud();
  });
  document.querySelectorAll("[data-sdf-viz]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.sdfVizMode = Number(btn.dataset.sdfViz);
      renderUi();
    });
  });
  document.getElementById("BtnSdfCutFlip")?.addEventListener("click", () => {
    state.sdfCutFlip = !state.sdfCutFlip;
    renderViewportHud();
  });
  document.getElementById("HudIsoRange")?.addEventListener("input", (e) => {
    state.sdfIsoOffsetMm = Number(e.target.value);
    const outEl = document.getElementById("HudIsoOut");
    if (outEl) outEl.textContent = `${state.sdfIsoOffsetMm >= 0 ? "+" : ""}${state.sdfIsoOffsetMm}mm`;
    const rng = document.getElementById("HudIsoRange");
    if (rng) rng.style.setProperty("--fill", `${((state.sdfIsoOffsetMm + 40) / 80 * 100).toFixed(1)}%`);
    const inspOut = document.getElementById("OutIsoOffset");
    if (inspOut) inspOut.textContent = `${state.sdfIsoOffsetMm >= 0 ? "+" : ""}${state.sdfIsoOffsetMm} mm`;
    const inspRng = document.getElementById("RngIsoOffset");
    if (inspRng) {
      inspRng.value = String(state.sdfIsoOffsetMm);
      inspRng.style.setProperty("--fill", `${((state.sdfIsoOffsetMm + 40) / 80 * 100).toFixed(1)}%`);
    }
  });
}

function renderUi() {
  const spec = getActiveSpecimen();
  if (!spec || !compiled) return;
  syncActiveLodIndex();

  const { geometry, clusterDag, sdfVolume, dualContour, icon } = compiled;
  const lodIdx = state.activeLodIndex;
  const rLod = clusterDag.retopoLods[lodIdx];
  const qLod = clusterDag.naniteQemLods[lodIdx];
  const hLod = clusterDag.hybridLods[lodIdx];
  const activeLod = getMethodLod(clusterDag, state.activeMethod, lodIdx);
  const residentBytes = sdfVolume.mip0Bytes + sdfVolume.mip1Bytes;

  // 1. Left Stats
  document.getElementById("LeftStats").innerHTML = `
    <div class="stat-tile">
      <span class="status-disc">✓</span>
      <small>SPECIMENS</small>
      <strong>${state.specimens.length}</strong>
    </div>
    <div class="stat-tile">
      <span class="status-disc ${activeLod.wallCollapseCount === 0 ? "" : "warn"}">${activeLod.wallCollapseCount === 0 ? "✓" : "!"}</span>
      <small>LOCKED GUIDES</small>
      <strong>${clusterDag.originalLod.guideEdgeCount}</strong>
    </div>
  `;

  // 2. Category Filter Chips
  const cats = ["All", "Dielectric", "Solid", "Custom"];
  document.getElementById("FilterChips").innerHTML = cats
    .map((c) => `<button type="button" data-cat="${c}" class="${state.categoryFilter === c ? "active" : ""}">${c}</button>`)
    .join("");

  // 3. Outliner Rows
  const q = state.searchQuery.trim().toLowerCase();
  const visible = state.specimens.filter((item) => {
    if (state.categoryFilter !== "All" && item.category !== state.categoryFilter) return false;
    if (q && !item.title.toLowerCase().includes(q) && !item.opticalCategory.toLowerCase().includes(q)) return false;
    return true;
  });
  document.getElementById("OutlinerRows").innerHTML = `
    <div class="outliner-section-label">Built-In &amp; Imported Assets (${visible.length})</div>
    ${visible.map((item) => `
      <div class="outliner-row ${item.id === state.activeSpecimenId ? "selected" : ""}" data-spec-id="${item.id}">
        <img class="specimen-thumb" src="${item.iconDataUrl}" alt="${item.title}" />
        <div class="row-info">
          <span class="row-name">${item.title}</span>
          <span class="row-sub">${item.geometry.triangleCount.toLocaleString()} tris · ${item.opticalCategory}</span>
        </div>
        <span class="row-badge ${item.opticalCategory === "DielectricGlass" ? "glass" : ""}">${item.opticalCategory === "DielectricGlass" ? "GLASS" : item.category.toUpperCase()}</span>
      </div>
    `).join("")}
  `;

  // 4. Pipeline Stage Checklist (4 Stages)
  document.getElementById("PipelineChecklist").innerHTML = `
    <div class="stage-item"><span class="dot"></span><span>1. 4-Way LOD Solvers</span><b>${activeLod.triangleCount} tris (${activeLod.meanErrorMm}mm)</b></div>
    <div class="stage-item"><span class="dot"></span><span>2. Sparse 8×8×8 SDF + Mip1</span><b>${sdfVolume.mip0Evicted ? "Mip1 resident" : `${sdfVolume.allocatedBrickCount}/${sdfVolume.totalBricks} bricks`}</b></div>
    <div class="stage-item"><span class="dot"></span><span>3. QEF Dual-Contour</span><b>${dualContour.extractedTriangleCount} tris</b></div>
    <div class="stage-item"><span class="dot"></span><span>4. 128px Transparent Icon</span><b>α0=${icon.cornerAlpha}</b></div>
  `;

  // 5. Left Footer
  document.getElementById("OutlinerFooter").innerHTML = `
    <span><small>BACKEND</small><b>${viewport?.webGpuAvailable ? "WEBGPU" : "FALLBACK"}</b></span>
    <span><small>COMPILE</small><b>${lastCompileMs.toFixed(0)} ms</b></span>
    <span><small>LOD${lodIdx} TRIS</small><b>${activeLod.triangleCount.toLocaleString()}</b></span>
  `;

  // 6. Center Document Tabs
  const tabsHtml = state.openTabs.map((tid) => {
    const item = state.specimens.find((s) => s.id === tid);
    if (!item) return "";
    return `<button type="button" class="document-tab ${tid === state.activeSpecimenId ? "active" : ""}" data-tab-id="${tid}">${item.title}</button>`;
  }).join("");
  document.getElementById("CenterTabStrip").innerHTML = `${tabsHtml}<button type="button" class="tab-add" id="BtnTabAdd" title="Import .OBJ or .STL">+</button>`;

  // 7. Viewport Header & Modes
  document.getElementById("ViewportIdentity").innerHTML = `
    <img class="specimen-thumb" src="${spec.iconDataUrl}" alt="${spec.title}" />
    <div>
      <strong>${spec.title}</strong>
      <span>LOD0 ${geometry.triangleCount.toLocaleString()} → ${METHOD_LABELS[state.activeMethod]} LOD${lodIdx} ${activeLod.triangleCount.toLocaleString()} tris</span>
    </div>
    <span class="viewport-preview-badge ${viewport?.webGpuAvailable ? "emerald" : ""}">${viewport?.adapterLabel || "WebGPU"}</span>
  `;

  const modes = [
    ["quadCompare", "4-Way Compare"],
    ["surface", "Single Surface"],
    ["clusterLod", "Cluster LOD"],
    ["sdfRaymarch", "SDF 3D"],
    ["sdfSlice", "SDF Slice"],
    ["dualContour", "Dual-Contour"],
    ["iconStudio", "Icon 128px"],
  ];
  document.getElementById("ViewportModes").innerHTML = modes
    .map(([id, label]) => `<button type="button" data-mode="${id}" class="${state.viewMode === id ? "active" : ""}">${label}</button>`)
    .join("");

  document.getElementById("ViewportActions").innerHTML = `
    <button type="button" class="viewport-construct" id="BtnRecompile">Compile All</button>
    <button type="button" id="BtnWireframe" class="${state.wireframe ? "active-pill" : ""}">Wireframe</button>
    <button type="button" id="BtnGuides" class="${state.showGuides ? "active-pill" : ""}">Guides (${clusterDag.originalLod.guideEdgeCount})</button>
    <button type="button" id="BtnAutoOrbit" class="${state.autoOrbit ? "active-pill" : ""}">Auto-Orbit</button>
    <button type="button" id="BtnResetCam">Reset</button>
    <button type="button" id="BtnExportFvol" style="margin-left:auto">.FVOL</button>
    <button type="button" id="BtnExportIcon">Icon .PNG</button>
  `;

  // 8. Floating Viewport HUD + 4-Way Badges / SDF Scrubber
  renderViewportHud();

  // 9. Viewport Footer with Project Zero Content Shelf Notch
  document.getElementById("ViewportFooter").innerHTML = `
    <span>LOD${lodIdx} ERR <b>Retopo ${rLod.meanErrorMm}mm · Nanite ${qLod.meanErrorMm}mm · Hybrid ${hLod.meanErrorMm}mm (${hLod.sliverPercent}% slv)</b></span>
    <button type="button" class="drawer-notch-btn" id="BtnToggleShelf">${state.shelfOpen ? "▼ Hide Asset Shelf" : `▲ Content Shelf (${state.specimens.length})`}</button>
    <span>SDF VRAM <b class="ok">${formatBytes(residentBytes)} / ${state.vramBudgetKb} KB</b></span>
  `;

  // 10. Right Inspector Cards
  const opticalOptions = OPTICAL_CATEGORIES.map(
    (o) => `<option value="${o.id}" ${state.opticalCategory === o.id ? "selected" : ""}>${o.label} (IOR ${o.ior.toFixed(2)})</option>`,
  ).join("");
  const axisNames = ["Z", "Y", "X"];
  const activeChain = state.activeMethod === "retopo"
    ? clusterDag.retopoLods
    : state.activeMethod === "naniteQem"
      ? clusterDag.naniteQemLods
      : clusterDag.hybridLods;
  const lodChainSummary = activeChain.map((l, i) => `L${i}:${l.triangleCount}`).join(" · ");

  document.getElementById("InspectorScroll").innerHTML = `
    <div class="inspector-subject">
      <img class="specimen-thumb" src="${spec.iconDataUrl}" alt="${spec.title}" />
      <div>
        <h2>${spec.title}</h2>
        <p>${spec.subtitle}</p>
      </div>
    </div>

    <!-- CARD 1: 4-WAY LOD COMPARISON, PROBABILISTIC QEM & RETOPO GUIDES -->
    <section class="workspace-card">
      <h3><i></i>Hybrid Production Solver &amp; Compare<small>LOD${lodIdx}</small></h3>

      <div class="field">
        <div class="field-head"><span>Active Viewport Solver</span><output>${METHOD_LABELS[state.activeMethod]}</output></div>
        <div class="seg-row">
          <button type="button" data-insp-method="hybrid" class="${state.activeMethod === "hybrid" ? "active" : ""}">Hybrid ★</button>
          <button type="button" data-insp-method="retopo" class="${state.activeMethod === "retopo" ? "active" : ""}" title="Comparison reference only">Retopo (Cmp)</button>
          <button type="button" data-insp-method="naniteQem" class="${state.activeMethod === "naniteQem" ? "active" : ""}" title="Comparison reference only">Nanite (Cmp)</button>
        </div>
      </div>

      <div class="readout"><span>Retopo + Shrinkwrap</span><span>${rLod.triangleCount}t · ${rLod.meanErrorMm}mm · ${rLod.sliverPercent}% slv · ${rLod.wallCollapseCount}b</span></div>
      <div class="readout"><span>Nanite-Style QEM${state.naniteShrinkwrap ? " + Int.SW" : ""}</span><span>${qLod.triangleCount}t · ${qLod.meanErrorMm}mm · ${qLod.sliverPercent}% slv · ${qLod.wallCollapseCount}b</span></div>
      <div class="readout"><span>Hybrid · ${state.regionalAdaptive ? "Regional" : "Uniform"}</span><span style="color:#4be374">${hLod.triangleCount}t · ${hLod.meanErrorMm}mm · ${hLod.sliverPercent}% slv · ${hLod.wallCollapseCount}b</span></div>
      ${hLod.regionMix ? `<div class="readout"><span>Local Region Mix</span><span>${hLod.regionMix.sharpPct}% QEM · ${hLod.regionMix.midPct}% Blend · ${hLod.regionMix.flatPct}% Retopo</span></div>` : ""}

      <div class="field" style="margin-top:10px">
        <div class="field-head"><span>Optical Material Category</span><output>${state.opticalCategory}</output></div>
        <select id="SelOptical">${opticalOptions}</select>
      </div>

      <div class="switch-row">
        <span>Regional Auto-Blend (QEM / Retopo)</span>
        <button type="button" class="toggle ${state.regionalAdaptive ? "on" : ""}" id="TogRegionalAdaptive"><i></i></button>
      </div>
      <div class="switch-row">
        <span>Retopo-Seeded Coarse LOD 3</span>
        <button type="button" class="toggle ${state.coarseRetopoSeed ? "on" : ""}" id="TogCoarseRetopoSeed"><i></i></button>
      </div>

      <div class="field" style="margin-top:8px">
        <div class="field-head"><span>Hybrid Shrinkwrap Policy</span><output>${HYBRID_SW_LABELS[state.hybridShrinkwrapMode]}</output></div>
        <div class="seg-row">
          <button type="button" data-hybrid-sw="adaptive" class="${state.hybridShrinkwrapMode === "adaptive" ? "active" : ""}" title="Light overshoot guard at LOD1; full error-guarded Shrinkwrap at LOD2-3">Auto 2–3</button>
          <button type="button" data-hybrid-sw="allLods" class="${state.hybridShrinkwrapMode === "allLods" ? "active" : ""}" title="Error-guarded Shrinkwrap across LOD1, LOD2, LOD3">All LODs</button>
          <button type="button" data-hybrid-sw="strictLow" class="${state.hybridShrinkwrapMode === "strictLow" ? "active" : ""}" title="100% hard-snapped vertices onto LOD0 at LOD2-3">Strict 2–3</button>
          <button type="button" data-hybrid-sw="off" class="${state.hybridShrinkwrapMode === "off" ? "active" : ""}" title="Pure Probabilistic QEM + Quality Flips (no Shrinkwrap)">Off</button>
        </div>
      </div>

      <div class="switch-row">
        <span>Nanite Interior Shrinkwrap (Seam-Safe)</span>
        <button type="button" class="toggle ${state.naniteShrinkwrap ? "on" : ""}" id="TogNaniteSw"><i></i></button>
      </div>
      <div class="field">
        <div class="field-head"><span>Probabilistic QEM Variance (σ)</span><output>${state.probQemSigma.toFixed(3)}</output></div>
        <input type="range" id="RngProbSigma" min="0.005" max="0.12" step="0.005" value="${state.probQemSigma}" style="${sliderFillStyle(state.probQemSigma, 0.005, 0.12)}" />
      </div>

      <div class="switch-row">
        <span>Hard Edge Guides (Crease Lock)</span>
        <button type="button" class="toggle ${state.useEdgeGuides ? "on" : ""}" id="TogEdgeGuides"><i></i></button>
      </div>
      <div class="field">
        <div class="field-head"><span>Crease Guide Dihedral Angle</span><output>${state.creaseAngleDeg}°</output></div>
        <input type="range" id="RngCreaseDeg" min="15" max="75" step="1" value="${state.creaseAngleDeg}" style="${sliderFillStyle(state.creaseAngleDeg, 15, 75)}" />
      </div>

      <div class="switch-row">
        <span>Lock UV Seam Edges (Never Collapse)</span>
        <button type="button" class="toggle ${state.lockUvSeams ? "on" : ""}" id="TogUvSeams"><i></i></button>
      </div>
      <div class="switch-row">
        <span>Lock Open Boundary Edges</span>
        <button type="button" class="toggle ${state.lockBoundaries ? "on" : ""}" id="TogBoundaries"><i></i></button>
      </div>
      <div class="switch-row">
        <span>Lock Dual Dielectric Thin Walls</span>
        <button type="button" class="toggle ${state.lockDualWalls ? "on" : ""}" id="TogDualWalls"><i></i></button>
      </div>
      <div class="switch-row">
        <span>Show Guide Overlay (Amber)</span>
        <button type="button" class="toggle ${state.showGuides ? "on" : ""}" id="TogShowGuides"><i></i></button>
      </div>

      <div class="field">
        <div class="field-head"><span>Feature Guide Weight</span><output>${Math.round(state.featureGuideWeight * 100)}%</output></div>
        <input type="range" id="RngGuideWeight" min="0.2" max="1.0" step="0.05" value="${state.featureGuideWeight}" style="${sliderFillStyle(state.featureGuideWeight, 0.2, 1.0)}" />
      </div>

      <div class="field">
        <div class="field-head"><span>Shrinkwrap Reprojection Passes</span><output>${state.shrinkwrapPasses} passes</output></div>
        <input type="range" id="RngShrinkPasses" min="1" max="4" step="1" value="${state.shrinkwrapPasses}" style="${sliderFillStyle(state.shrinkwrapPasses, 1, 4)}" />
      </div>

      <div class="field">
        <div class="field-head"><span>Max Triangles / Cluster</span><output>${state.maxTrisPerCluster}</output></div>
        <input type="range" id="RngMaxTris" min="32" max="128" step="16" value="${state.maxTrisPerCluster}" style="${sliderFillStyle(state.maxTrisPerCluster, 32, 128)}" />
      </div>

      <div class="field">
        <div class="field-head"><span>Min Wall Thickness Guard</span><output>${state.minWallThicknessMm} mm</output></div>
        <input type="range" id="RngWallMm" min="10" max="120" step="5" value="${state.minWallThicknessMm}" style="${sliderFillStyle(state.minWallThicknessMm, 10, 120)}" />
      </div>

      <div class="readout"><span>Detected Guides (Crease/Seam)</span><span>${clusterDag.originalLod.guideEdgeCount} (${clusterDag.originalLod.creaseEdgeCount}c / ${clusterDag.originalLod.seamEdgeCount}s)</span></div>
      <div class="readout"><span>Active LOD Chain</span><span>${lodChainSummary}</span></div>
      <div class="readout"><span>Clusters (LOD${lodIdx}) / Nanite Lock</span><span>${activeLod.clusterCount} grp · ${qLod.groupLockedVerts} vtx</span></div>
    </section>

    <!-- CARD 2: SPARSE 8x8x8 SDF BAKER, 3D RAYMARCH VISUALIZER & QEF -->
    <section class="workspace-card">
      <h3><i class="warm"></i>Sparse 8×8×8 SDF Baker &amp; 3D Viz<small>UP TO 128³</small></h3>

      <div class="field">
        <div class="field-head"><span>Volume Voxel Resolution</span><output>${state.sdfDim}³ (${sdfVolume.bricksPerAxis}³ bricks)</output></div>
        <div class="seg-row">
          <button type="button" data-dim="16" class="${state.sdfDim === 16 ? "active" : ""}">16³</button>
          <button type="button" data-dim="32" class="${state.sdfDim === 32 ? "active" : ""}">32³</button>
          <button type="button" data-dim="64" class="${state.sdfDim === 64 ? "active" : ""}">64³</button>
          <button type="button" data-dim="96" class="${state.sdfDim === 96 ? "active" : ""}">96³</button>
          <button type="button" data-dim="128" class="${state.sdfDim === 128 ? "active" : ""}">128³</button>
        </div>
      </div>

      <div class="field">
        <div class="field-head"><span>3D Raymarch Visualizer Mode</span><output>${SDF_VIZ_LABELS[state.sdfVizMode]}</output></div>
        <div class="seg-row">
          <button type="button" data-insp-sdf-viz="0" class="${state.sdfVizMode === 0 ? "active" : ""}">Iso+AO</button>
          <button type="button" data-insp-sdf-viz="1" class="${state.sdfVizMode === 1 ? "active" : ""}">Cutaway</button>
          <button type="button" data-insp-sdf-viz="2" class="${state.sdfVizMode === 2 ? "active" : ""}">Heat</button>
          <button type="button" data-insp-sdf-viz="3" class="${state.sdfVizMode === 3 ? "active" : ""}">Bricks</button>
        </div>
      </div>

      <div class="field">
        <div class="field-head"><span>Iso-Surface Offset</span><output id="OutIsoOffset">${state.sdfIsoOffsetMm >= 0 ? "+" : ""}${state.sdfIsoOffsetMm} mm</output></div>
        <input type="range" id="RngIsoOffset" min="-40" max="40" step="1" value="${state.sdfIsoOffsetMm}" style="${sliderFillStyle(state.sdfIsoOffsetMm, -40, 40)}" />
      </div>

      <div class="field">
        <div class="field-head"><span>VRAM Budget Cap (Evicts Mip0)</span><output>${formatBytes(state.vramBudgetKb * 1024)}</output></div>
        <input type="range" id="RngVram" min="16" max="4096" step="16" value="${state.vramBudgetKb}" style="${sliderFillStyle(state.vramBudgetKb, 16, 4096)}" />
      </div>

      <div class="field">
        <div class="field-head"><span>Slice / Cutaway Mip &amp; Axis</span><output>Mip${state.sdfSliceLevel} · ${axisNames[state.sdfSliceAxis]}</output></div>
        <div class="seg-row" style="margin-bottom:6px">
          <button type="button" data-miplvl="0" class="${state.sdfSliceLevel === 0 ? "active" : ""}">Mip0 (8³)</button>
          <button type="button" data-miplvl="1" class="${state.sdfSliceLevel === 1 ? "active" : ""}">Mip1 (4³ Min)</button>
        </div>
        <div class="seg-row">
          <button type="button" data-axis="0" class="${state.sdfSliceAxis === 0 ? "active" : ""}">Z-Plane</button>
          <button type="button" data-axis="1" class="${state.sdfSliceAxis === 1 ? "active" : ""}">Y-Plane</button>
          <button type="button" data-axis="2" class="${state.sdfSliceAxis === 2 ? "active" : ""}">X-Plane</button>
        </div>
      </div>

      <div class="field">
        <div class="field-head"><span>Slice / Cutaway Depth</span><output id="OutSliceDepth">${Math.round(state.sdfSliceDepth * 100)}%</output></div>
        <input type="range" id="RngSliceDepth" min="0.02" max="0.98" step="0.005" value="${state.sdfSliceDepth}" style="${sliderFillStyle(state.sdfSliceDepth, 0.02, 0.98)}" />
      </div>

      <div class="readout"><span>Narrow-Band Bricks / Pruned</span><span>${sdfVolume.allocatedBrickCount}/${sdfVolume.totalBricks} (${sdfVolume.prunedPercent}% saved)</span></div>
      <div class="readout"><span>Sparse vs Dense Footprint</span><span>${formatBytes(sdfVolume.mip0Bytes + sdfVolume.mip1Bytes)} vs ${formatBytes(sdfVolume.denseBytes)}</span></div>
      <div class="readout"><span>Mip0 Eviction Status</span><span style="color:${sdfVolume.mip0Evicted ? "#ffb454" : "#4be374"}">${sdfVolume.mip0Evicted ? "EVICTED → Mip1 Active" : "Resident (Mip0+Mip1)"}</span></div>
      <div class="readout"><span>Conservative Mip1 ≤ Mip0</span><span style="color:#4be374">${sdfVolume.monotonicityVerified ? "VERIFIED (PASS)" : "FAILED"}</span></div>
      <div class="readout"><span>QEF Dual-Contour Output</span><span>${dualContour.extractedTriangleCount.toLocaleString()} tris · ${dualContour.qefSolvedCells.toLocaleString()} QEF</span></div>
    </section>

    <!-- CARD 3: TRANSPARENT 3D ICON & EXPORT -->
    <section class="workspace-card">
      <h3><i></i>Transparent 128px Icon &amp; Export<small>.PNG / .FVOL</small></h3>

      <div class="icon-preview-box">
        <img class="specimen-thumb" src="${compiled.iconDataUrl}" alt="128px Transparent Icon" />
        <div style="flex:1;min-width:0">
          <div class="readout"><span>Resolution</span><span>${icon.width}×${icon.height} RGBA</span></div>
          <div class="readout"><span>Corner Alpha</span><span style="color:#4be374">${icon.cornerAlpha} (Transparent)</span></div>
          <div class="readout"><span>Silhouette / Alpha=0</span><span>${icon.silhouettePercent}% / ${icon.transparentPercent}%</span></div>
        </div>
      </div>

      <div class="card-btn-row">
        <button type="button" id="BtnCardPng">Icon .PNG</button>
        <button type="button" id="BtnCardFvol">.FVOL</button>
        <button type="button" id="BtnCardJson">Report .JSON</button>
      </div>
    </section>
  `;

  document.getElementById("InspectorFooter").innerHTML = `
    <span>frontier.fvol.v1 READY</span>
    <span>PROJECT ZERO UI SHELL</span>
  `;

  // 11. Bottom Shelf Drawer
  const shelf = document.getElementById("ShelfDrawer");
  shelf.classList.toggle("open", state.shelfOpen);
  document.getElementById("ShelfGrid").innerHTML = state.specimens.map((item) => `
    <div class="shelf-card ${item.id === state.activeSpecimenId ? "active" : ""}" data-shelf-id="${item.id}">
      <img class="specimen-thumb" src="${item.iconDataUrl}" alt="${item.title}" />
      <span>${item.title}</span>
      <small>${item.geometry.triangleCount} tris · α0=${item.iconMeta.cornerAlpha}</small>
    </div>
  `).join("");

  bindDynamicHandlers();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                INTERACTION & EVENT WIRING
//------------------------------------------------------------------------------------------------------------------------

function exportIconPng() {
  if (!compiled?.iconDataUrl) return;
  fetch(compiled.iconDataUrl)
    .then((r) => r.blob())
    .then((b) => triggerDownload(b, `${getActiveSpecimen().id}_128px_icon.png`));
}

function exportFvol() {
  if (!compiled) return;
  const spec = getActiveSpecimen();
  const payload = serializeVolumeFvol(compiled.sdfVolume, spec.title);
  triggerDownload(new Blob([payload], { type: "application/json" }), `${spec.id}.fvol`);
}

function exportJsonReport() {
  if (!compiled) return;
  const spec = getActiveSpecimen();
  const report = {
    assetTitle: spec.title,
    opticalCategory: state.opticalCategory,
    activeMethod: state.activeMethod,
    productionSolver: "hybrid",
    comparisonOnlySolvers: ["retopo", "naniteQem"],
    guidePolicy: {
      useEdgeGuides: state.useEdgeGuides,
      creaseAngleDeg: state.creaseAngleDeg,
      lockUvSeams: state.lockUvSeams,
      lockBoundaries: state.lockBoundaries,
      lockDualWalls: state.lockDualWalls,
      featureGuideWeight: state.featureGuideWeight,
      shrinkwrapPasses: state.shrinkwrapPasses,
      probQemSigma: state.probQemSigma,
      naniteShrinkwrap: state.naniteShrinkwrap,
      hybridShrinkwrapMode: state.hybridShrinkwrapMode,
      regionalAdaptive: state.regionalAdaptive,
      coarseRetopoSeed: state.coarseRetopoSeed,
    },
    geometry: { vertexCount: compiled.geometry.vertexCount, triangleCount: compiled.geometry.triangleCount },
    clusterDag: compiled.clusterDag.report,
    sdfVolume: {
      dim: compiled.sdfVolume.dim,
      mip1Dim: compiled.sdfVolume.mip1Dim,
      allocatedBrickCount: compiled.sdfVolume.allocatedBrickCount,
      totalBricks: compiled.sdfVolume.totalBricks,
      prunedPercent: compiled.sdfVolume.prunedPercent,
      mip0Evicted: compiled.sdfVolume.mip0Evicted,
      monotonicityVerified: compiled.sdfVolume.monotonicityVerified,
    },
    dualContour: {
      qefSolvedCells: compiled.dualContour.qefSolvedCells,
      extractedTriangleCount: compiled.dualContour.extractedTriangleCount,
    },
    icon: {
      width: compiled.icon.width,
      height: compiled.icon.height,
      cornerAlpha: compiled.icon.cornerAlpha,
      silhouettePercent: compiled.icon.silhouettePercent,
      transparentPercent: compiled.icon.transparentPercent,
    },
  };
  triggerDownload(new Blob([JSON.stringify(report, null, 2)], { type: "application/json" }), `${spec.id}_compiler_report.json`);
}

function bindDynamicHandlers() {
  const fileInput = document.getElementById("HiddenFileInput");
  const openImport = () => fileInput.click();

  document.getElementById("BtnTabAdd")?.addEventListener("click", openImport);
  document.getElementById("BtnToggleShelf")?.addEventListener("click", () => {
    state.shelfOpen = !state.shelfOpen;
    renderUi();
  });

  document.querySelectorAll("[data-cat]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.categoryFilter = btn.dataset.cat;
      renderUi();
    });
  });

  document.querySelectorAll("[data-spec-id]").forEach((row) => {
    row.addEventListener("click", () => selectSpecimen(row.dataset.specId));
  });
  document.querySelectorAll("[data-tab-id]").forEach((tab) => {
    tab.addEventListener("click", () => selectSpecimen(tab.dataset.tabId));
  });
  document.querySelectorAll("[data-shelf-id]").forEach((card) => {
    card.addEventListener("click", () => selectSpecimen(card.dataset.shelfId));
  });

  document.querySelectorAll("[data-mode]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.viewMode = btn.dataset.mode;
      if (state.viewMode === "sdfSlice") refreshSdfSliceOnly();
      renderUi();
    });
  });

  document.getElementById("BtnRecompile")?.addEventListener("click", () => {
    compileActiveSpecimen(false);
    renderUi();
  });
  document.getElementById("BtnWireframe")?.addEventListener("click", () => {
    state.wireframe = !state.wireframe;
    renderUi();
  });
  document.getElementById("BtnGuides")?.addEventListener("click", () => {
    state.showGuides = !state.showGuides;
    renderUi();
  });
  document.getElementById("BtnAutoOrbit")?.addEventListener("click", () => {
    state.autoOrbit = !state.autoOrbit;
    renderUi();
  });
  document.getElementById("BtnResetCam")?.addEventListener("click", () => {
    const spec = getActiveSpecimen();
    state.camera.yaw = 0.68;
    state.camera.pitch = 0.34;
    state.camera.distance = 2.45;
    if (spec) state.camera.target = [...spec.geometry.boundsCenter];
    if (state.lodSelection === "auto") {
      syncActiveLodIndex();
      renderUi();
    }
  });

  document.getElementById("BtnExportFvol")?.addEventListener("click", exportFvol);
  document.getElementById("BtnCardFvol")?.addEventListener("click", exportFvol);
  document.getElementById("BtnExportIcon")?.addEventListener("click", exportIconPng);
  document.getElementById("BtnCardPng")?.addEventListener("click", exportIconPng);
  document.getElementById("BtnCardJson")?.addEventListener("click", exportJsonReport);

  document.querySelectorAll("[data-insp-method]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.activeMethod = btn.dataset.inspMethod;
      renderUi();
    });
  });

  document.getElementById("SelOptical")?.addEventListener("change", (e) => {
    state.opticalCategory = e.target.value;
    compileActiveSpecimen(false);
    renderUi();
  });

  document.getElementById("TogRegionalAdaptive")?.addEventListener("click", () => {
    state.regionalAdaptive = !state.regionalAdaptive;
    recompileClusterLodOnly();
    renderUi();
  });

  document.getElementById("TogCoarseRetopoSeed")?.addEventListener("click", () => {
    state.coarseRetopoSeed = !state.coarseRetopoSeed;
    recompileClusterLodOnly();
    renderUi();
  });

  document.querySelectorAll("[data-hybrid-sw]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.hybridShrinkwrapMode = btn.dataset.hybridSw;
      recompileClusterLodOnly();
      renderUi();
    });
  });

  document.getElementById("TogNaniteSw")?.addEventListener("click", () => {
    state.naniteShrinkwrap = !state.naniteShrinkwrap;
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("RngProbSigma")?.addEventListener("input", (e) => {
    state.probQemSigma = Number(e.target.value);
    recompileClusterLodOnly();
    renderUi();
  });

  document.getElementById("TogEdgeGuides")?.addEventListener("click", () => {
    state.useEdgeGuides = !state.useEdgeGuides;
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("RngCreaseDeg")?.addEventListener("input", (e) => {
    state.creaseAngleDeg = Number(e.target.value);
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("TogUvSeams")?.addEventListener("click", () => {
    state.lockUvSeams = !state.lockUvSeams;
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("TogBoundaries")?.addEventListener("click", () => {
    state.lockBoundaries = !state.lockBoundaries;
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("TogDualWalls")?.addEventListener("click", () => {
    state.lockDualWalls = !state.lockDualWalls;
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("TogShowGuides")?.addEventListener("click", () => {
    state.showGuides = !state.showGuides;
    renderUi();
  });
  document.getElementById("RngGuideWeight")?.addEventListener("input", (e) => {
    state.featureGuideWeight = Number(e.target.value);
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("RngShrinkPasses")?.addEventListener("input", (e) => {
    state.shrinkwrapPasses = Number(e.target.value);
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("RngMaxTris")?.addEventListener("input", (e) => {
    state.maxTrisPerCluster = Number(e.target.value);
    recompileClusterLodOnly();
    renderUi();
  });
  document.getElementById("RngWallMm")?.addEventListener("input", (e) => {
    state.minWallThicknessMm = Number(e.target.value);
    recompileClusterLodOnly();
    renderUi();
  });

  document.querySelectorAll("[data-dim]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.sdfDim = Number(btn.dataset.dim);
      recompileSdfVolumeOnly();
      renderUi();
    });
  });
  document.getElementById("RngVram")?.addEventListener("input", (e) => {
    state.vramBudgetKb = Number(e.target.value);
    recompileSdfVolumeOnly();
    renderUi();
  });

  document.querySelectorAll("[data-insp-sdf-viz]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.sdfVizMode = Number(btn.dataset.inspSdfViz);
      state.viewMode = "sdfRaymarch";
      renderUi();
    });
  });
  document.getElementById("RngIsoOffset")?.addEventListener("input", (e) => {
    state.sdfIsoOffsetMm = Number(e.target.value);
    if (state.viewMode !== "sdfRaymarch") {
      state.viewMode = "sdfRaymarch";
      renderUi();
      return;
    }
    const outEl = document.getElementById("OutIsoOffset");
    if (outEl) outEl.textContent = `${state.sdfIsoOffsetMm >= 0 ? "+" : ""}${state.sdfIsoOffsetMm} mm`;
    const rng = document.getElementById("RngIsoOffset");
    if (rng) rng.style.setProperty("--fill", `${((state.sdfIsoOffsetMm + 40) / 80 * 100).toFixed(1)}%`);
    const hudOut = document.getElementById("HudIsoOut");
    if (hudOut) hudOut.textContent = `${state.sdfIsoOffsetMm >= 0 ? "+" : ""}${state.sdfIsoOffsetMm}mm`;
    const hudRng = document.getElementById("HudIsoRange");
    if (hudRng) {
      hudRng.value = String(state.sdfIsoOffsetMm);
      hudRng.style.setProperty("--fill", `${((state.sdfIsoOffsetMm + 40) / 80 * 100).toFixed(1)}%`);
    }
  });

  document.querySelectorAll("[data-miplvl]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.sdfSliceLevel = Number(btn.dataset.miplvl);
      if (state.viewMode !== "sdfRaymarch") state.viewMode = "sdfSlice";
      refreshSdfSliceOnly();
      renderUi();
    });
  });
  document.querySelectorAll("[data-axis]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.sdfSliceAxis = Number(btn.dataset.axis);
      if (state.viewMode === "sdfRaymarch") {
        state.sdfVizMode = 1; // Switch directly to 3D Cutaway along chosen axis!
      } else {
        state.viewMode = "sdfSlice";
      }
      refreshSdfSliceOnly();
      renderUi();
    });
  });
  document.getElementById("RngSliceDepth")?.addEventListener("input", (e) => {
    state.sdfAutoSweep = false;
    state.sdfSliceDepth = Number(e.target.value);
    if (state.viewMode === "sdfRaymarch") {
      if (state.sdfVizMode !== 1) {
        state.sdfVizMode = 1;
        renderUi();
      }
      refreshSdfSliceOnly();
    } else if (state.viewMode !== "sdfSlice") {
      state.viewMode = "sdfSlice";
      refreshSdfSliceOnly();
      renderUi();
    } else {
      refreshSdfSliceOnly();
    }
  });
}

function wirePersistentControls() {
  const search = document.getElementById("SearchInput");
  search.addEventListener("input", (e) => {
    state.searchQuery = e.target.value;
    renderUi();
  });

  const fileInput = document.getElementById("HiddenFileInput");
  const openImport = () => fileInput.click();
  document.getElementById("BtnImportLeft")?.addEventListener("click", openImport);
  document.getElementById("BtnShelfImport")?.addEventListener("click", openImport);
  document.getElementById("BtnOpenShelfTab")?.addEventListener("click", () => {
    state.shelfOpen = !state.shelfOpen;
    renderUi();
  });
  document.getElementById("BtnCloseShelf")?.addEventListener("click", () => {
    state.shelfOpen = false;
    renderUi();
  });
  fileInput.addEventListener("change", (e) => {
    const file = e.target.files?.[0];
    if (file) importGeometryFile(file);
    fileInput.value = "";
  });

  // Resizable Project Zero dock dividers
  const ws = document.getElementById("WorkspaceRoot");
  const attachResize = (el, side) => {
    el.addEventListener("pointerdown", (ev) => {
      ev.preventDefault();
      const startX = ev.clientX;
      const startW = side === "left" ? state.leftWidth : state.rightWidth;
      const onMove = (m) => {
        const dx = m.clientX - startX;
        const next = Math.max(220, Math.min(480, side === "left" ? startW + dx : startW - dx));
        if (side === "left") state.leftWidth = next;
        else state.rightWidth = next;
        ws.style.setProperty(`--${side}`, `${next}px`);
      };
      const onUp = () => {
        window.removeEventListener("pointermove", onMove);
        window.removeEventListener("pointerup", onUp);
      };
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });
  };
  attachResize(document.getElementById("DividerLeft"), "left");
  attachResize(document.getElementById("DividerRight"), "right");

  // Orbit & Zoom & Drag-Drop on Viewport Stage
  const stage = document.getElementById("ViewportStage");
  const dropHint = document.getElementById("DropHint");
  let dragging = false;
  let lastX = 0, lastY = 0;

  stage.addEventListener("pointerdown", (e) => {
    dragging = true;
    lastX = e.clientX;
    lastY = e.clientY;
    state.autoOrbit = false;
    stage.setPointerCapture(e.pointerId);
  });
  stage.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    const dx = e.clientX - lastX;
    const dy = e.clientY - lastY;
    lastX = e.clientX;
    lastY = e.clientY;
    state.camera.yaw -= dx * 0.008;
    state.camera.pitch = Math.max(-1.42, Math.min(1.42, state.camera.pitch + dy * 0.008));
  });
  stage.addEventListener("pointerup", () => { dragging = false; });
  stage.addEventListener("wheel", (e) => {
    e.preventDefault();
    if (state.viewMode === "sdfSlice") {
      state.sdfAutoSweep = false;
      state.sdfSliceDepth = Math.max(0.02, Math.min(0.98, state.sdfSliceDepth + Math.sign(e.deltaY) * 0.02));
      refreshSdfSliceOnly();
      return;
    }
    const prevLod = state.activeLodIndex;
    state.camera.distance = Math.max(1.05, Math.min(6.5, state.camera.distance * (1 + e.deltaY * 0.001)));
    if (state.lodSelection === "auto") {
      syncActiveLodIndex();
      if (state.activeLodIndex !== prevLod) renderUi();
    }
  }, { passive: false });

  stage.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropHint.classList.add("visible");
  });
  stage.addEventListener("dragleave", () => dropHint.classList.remove("visible"));
  stage.addEventListener("drop", (e) => {
    e.preventDefault();
    dropHint.classList.remove("visible");
    const file = e.dataTransfer?.files?.[0];
    if (file) importGeometryFile(file);
  });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BOOTSTRAP & LOOP
//------------------------------------------------------------------------------------------------------------------------

async function main() {
  const root = document.getElementById("CompilerRoot");
  root.innerHTML = buildShellHtml();

  bootstrapSpecimens();
  viewport = new GeometryCompilerViewport(
    document.getElementById("WebGpuCanvas"),
    document.getElementById("OverlayCanvas"),
  );
  await viewport.initialize();

  compileActiveSpecimen(true);
  wirePersistentControls();
  renderUi();

  let prevTs = performance.now();
  const tick = (ts) => {
    const dt = Math.min(0.05, (ts - prevTs) * 0.001);
    prevTs = ts;
    if (state.autoOrbit && state.viewMode !== "sdfSlice" && state.viewMode !== "iconStudio") {
      state.camera.yaw += dt * 0.35;
    }
    if (state.sdfAutoSweep && (state.viewMode === "sdfSlice" || (state.viewMode === "sdfRaymarch" && state.sdfVizMode === 1))) {
      state.sdfSliceDepth += dt * 0.24 * state.sdfSweepDir;
      if (state.sdfSliceDepth >= 0.96) { state.sdfSliceDepth = 0.96; state.sdfSweepDir = -1; }
      if (state.sdfSliceDepth <= 0.04) { state.sdfSliceDepth = 0.04; state.sdfSweepDir = 1; }
      refreshSdfSliceOnly();
    }
    viewport.renderFrame(state, compiled);
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

main();
