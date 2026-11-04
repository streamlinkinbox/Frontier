/*==========================================================================================================================================
                                                           BAKINGWORKSPACE.JS
==========================================================================================================================================*/
/* 🧩 Controller for the Standalone HTML Baking Workspace:
      2 Separate Left Tabs (Grid-Based Mesh Maps | Dual High/Low Scene Outliner) + 16K/Custom Per-Map Inspector. */

(function () {
  "use strict";

  const Core = window.FrontierBakingCore;
  const CATALOG = Core.MESH_MAP_CATALOG;
  const SCHEMAS = Core.MAP_PROPERTY_SCHEMAS;

  const CATEGORY_ORDER = [
    "Volume & Distance",
    "Normals",
    "Surface",
    "Coordinates & IDs",
    "Masks",
    "Color",
  ];

  const PRESET_2D_SIZES = [512, 1024, 2048, 4096, 8192, 16384];
  const MODE_3D_CYCLE = [
    ["combo", "Iso + Cut"],
    ["triplane", "Triplane"],
    ["stack", "Stack"],
  ];

  const $ = (id) => document.getElementById(id);

  function buildInitialPerMapProps() {
    const out = {};
    for (const entry of CATALOG) {
      if (entry.isVolume) continue;
      const schema = SCHEMAS[entry.id] || [];
      const bag = {};
      for (const field of schema) {
        bag[field.key] = field.default;
      }
      out[entry.id] = bag;
    }
    return out;
  }

  const state = {
    leftTab: "maps", // "maps" | "outliner"

    studyShape: "Torus",
    asset: null,
    meshAudit: null,
    matchMode: "byName",

    searchQuery: "",
    collapsedCategories: new Set(),

    selectedMapId: "volumeSdf",
    previewTarget: "volumeSdf", // "blank" | mapId
    checkedMaps: new Set(["volumeSdf", "normalTangent", "ao", "curvature"]),

    // Global 2D resolution (supports presets up to 16384 + Custom W x H)
    res2dMode: "preset", // "preset" | "custom"
    globalWidth: 2048,
    globalHeight: 2048,
    lockAspect: true,
    outputFormat: "png8",
    paddingPx: 8,

    // Per-map 2D resolution overrides: mapId -> { mode: "preset"|"custom", width, height }
    mapResOverrides: {},

    // Per-map domain properties: mapId -> { ... }
    perMapProps: buildInitialPerMapProps(),

    // Global Cage / Ray envelope
    frontalDistance: 0.05,
    rearDistance: 0.05,

    // Volume SDF resolution & settings (supports presets + Custom Nx x Ny x Nz)
    sdfResMode: "preset", // "preset" | "custom"
    sdfDims: [64, 64, 64],
    sdfSignMode: "auto",
    sdfPadding: 0.14,
    sdfNarrowBand: 0,

    // SDF Interactive Preview State
    sdfLayout: "split",
    sdfAxis: "z",
    sdfSliceNorm: 0.5,
    sdfPalette: "bands",
    sdfMode3dIndex: 0,
    sdfYaw: -0.68,
    sdfPitch: 0.44,
    sdfZoom: 1.0,

    // 2D Preview State
    channelMode: "rgb",
    zoomActual: false,
    uvOverlay: false,

    // Baked outputs
    bakedSdf: null,
    baked2d: {},
    isBaking: false,
  };

  let toastTimer = null;

  function showToast(msg) {
    const el = $("ToastBanner");
    el.textContent = msg;
    el.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
      el.hidden = true;
    }, 2600);
  }

  function getCatalogEntry(mapId) {
    return CATALOG.find((m) => m.id === mapId) || CATALOG[0];
  }

  function formatCompactTris(n) {
    if (n >= 1000000) return `${(n / 1000000).toFixed(1)}M`;
    if (n >= 1000) return `${(n / 1000).toFixed(1)}k`;
    return `${n}`;
  }

  function formatDimPair(w, h) {
    if (w === h) {
      if (w === 16384) return "16K";
      if (w === 8192) return "8K";
      if (w === 4096) return "4K";
      if (w === 2048) return "2K";
      if (w === 1024) return "1K";
      return `${w}px`;
    }
    return `${w}×${h}`;
  }

  function formatSdfDims(dims) {
    const [nx, ny, nz] = dims;
    return nx === ny && ny === nz ? `${nx}³` : `${nx}×${ny}×${nz}`;
  }

  function formatBytes(bytes) {
    if (bytes >= 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    return `${Math.max(1, Math.round(bytes / 1024))} KB`;
  }

  function getEffective2dDims(mapId) {
    const ov = state.mapResOverrides[mapId];
    if (ov) return { width: ov.width, height: ov.height, mode: ov.mode, pinned: true };
    return {
      width: state.globalWidth,
      height: state.globalHeight,
      mode: state.res2dMode,
      pinned: false,
    };
  }

  function bytesPerPixelForFormat(fmt) {
    if (fmt === "exr32") return 16;
    if (fmt === "png16") return 8;
    return 4;
  }

  function getAxisDim(dims, axis) {
    if (axis === "x") return dims[0];
    if (axis === "y") return dims[1];
    return dims[2];
  }

  // ----------------------------------------------------------------------------------------------------------------------------------------
  // Left Panel Tab Switching: [ Mesh Maps | Outliner ]
  // ----------------------------------------------------------------------------------------------------------------------------------------

  function renderLeftTabs() {
    const isMaps = state.leftTab === "maps";
    $("LeftTabMapsBtn").setAttribute("aria-selected", isMaps ? "true" : "false");
    $("LeftTabOutlinerBtn").setAttribute("aria-selected", !isMaps ? "true" : "false");
    $("MeshMapsPane").hidden = !isMaps;
    $("SceneOutlinerPane").hidden = isMaps;

    $("TabMapsCountLabel").textContent = String(CATALOG.length);
    if (state.asset) {
      const lowActive = state.asset.lowParts.filter((p) => p.enabled !== false).length;
      const highActive = state.asset.highParts.filter((p) => p.enabled !== false).length;
      $("TabOutlinerCountLabel").textContent = `${lowActive} · ${highActive}`;
    }
  }

  // ----------------------------------------------------------------------------------------------------------------------------------------
  // Study & Dedicated Dual Scene Outliner Management
  // ----------------------------------------------------------------------------------------------------------------------------------------

  function loadStudyShape(shapeKey, autoBakeSdf) {
    state.studyShape = shapeKey;
    state.asset = Core.createStudyAsset(shapeKey);
    state.baked2d = {};
    state.bakedSdf = null;
    refreshOutlinerAndAudit();

    if (autoBakeSdf) {
      runVolumeSdfBake();
    } else {
      renderAll();
    }
  }

  function refreshOutlinerAndAudit() {
    try {
      const targetMesh = state.asset.high || state.asset.low;
      state.meshAudit = Core.auditTriangleMesh(targetMesh);
    } catch (_) {
      state.meshAudit = null;
    }
    renderLeftTabs();
    renderDualOutliner();
  }

  function renderDualOutliner() {
    const renderPartTree = (parts, containerEl, totalLabelEl, role) => {
      containerEl.innerHTML = "";
      let activeTris = 0;
      const enabledCount = parts.filter((p) => p.enabled !== false).length;

      for (const part of parts) {
        const triCount = part.triangles ?? Math.round(part.indices.length / 3);
        const vertCount = Math.round(part.positions.length / 3);
        const isEnabled = part.enabled !== false;
        if (isEnabled) activeTris += triCount;

        const row = document.createElement("div");
        row.className = "outliner-node";
        row.dataset.enabled = isEnabled ? "true" : "false";
        row.setAttribute("role", "listitem");

        const bar = document.createElement("span");
        bar.className = "node-accent-bar";

        const copy = document.createElement("div");
        copy.className = "node-copy";
        copy.innerHTML = `
          <span class="node-name" title="${part.name}">${part.name}</span>
          <span class="node-sub">${triCount.toLocaleString()} tris · ${vertCount.toLocaleString()} verts</span>
        `;

        const visBtn = document.createElement("button");
        visBtn.type = "button";
        visBtn.className = "node-vis-btn";
        visBtn.setAttribute("aria-pressed", isEnabled ? "true" : "false");
        visBtn.title = isEnabled ? "Included in bake (click to mute)" : "Muted (click to include)";
        visBtn.innerHTML = '<svg class="icon" style="width:12px;height:12px;"><use href="#i-check" /></svg>';
        visBtn.addEventListener("click", () => {
          if (isEnabled && enabledCount <= 1) {
            showToast(`Keep at least 1 ${role} sub-mesh active`);
            return;
          }
          part.enabled = !isEnabled;
          refreshOutlinerAndAudit();
          renderInspector();
        });

        row.appendChild(bar);
        row.appendChild(copy);
        row.appendChild(visBtn);

        if (parts.length > 1) {
          const delBtn = document.createElement("button");
          delBtn.type = "button";
          delBtn.className = "node-del-btn";
          delBtn.textContent = "×";
          delBtn.title = `Remove ${part.name}`;
          delBtn.addEventListener("click", () => {
            const idx = parts.indexOf(part);
            if (idx >= 0) parts.splice(idx, 1);
            if (!parts.some((p) => p.enabled !== false) && parts[0]) {
              parts[0].enabled = true;
            }
            refreshOutlinerAndAudit();
            renderInspector();
          });
          row.appendChild(delBtn);
        }

        containerEl.appendChild(row);
      }

      if (role === "Low Poly") {
        totalLabelEl.textContent = `${formatCompactTris(activeTris)} tris · ${enabledCount}/${parts.length} active · UV0`;
      } else {
        const manifoldTag = state.meshAudit && state.meshAudit.closedManifold ? "Watertight" : "Open shell";
        totalLabelEl.textContent = `${formatCompactTris(activeTris)} tris · ${enabledCount}/${parts.length} active · ${manifoldTag}`;
      }
    };

    renderPartTree(state.asset.lowParts, $("LowOutlinerTree"), $("LowTotalTrisLabel"), "Low Poly");
    renderPartTree(state.asset.highParts, $("HighOutlinerTree"), $("HighTotalTrisLabel"), "High Poly");
  }

  async function importMeshFilesIntoOutliner(fileList, role) {
    if (!fileList || fileList.length === 0) return;
    let importedParts = [];

    for (const file of fileList) {
      const lower = file.name.toLowerCase();
      try {
        if (lower.endsWith(".obj")) {
          const text = await file.text();
          const parts = Core.parseObjToParts(text, file.name.replace(/\.[^.]+$/, ""));
          importedParts = importedParts.concat(parts);
        } else if (lower.endsWith(".stl")) {
          const buf = await file.arrayBuffer();
          const parts = Core.parseStlToParts(buf, file.name.replace(/\.[^.]+$/, ""));
          importedParts = importedParts.concat(parts);
        } else {
          showToast(`Unsupported format: ${file.name}`);
        }
      } catch (err) {
        showToast(`Failed reading ${file.name}: ${err.message}`);
      }
    }

    if (importedParts.length === 0) return;

    if (role === "low") {
      state.asset.lowParts = importedParts;
    } else {
      state.asset.highParts = importedParts;
    }
    state.asset.label = fileList[0].name.replace(/\.[^.]+$/, "");
    state.baked2d = {};
    state.bakedSdf = null;

    refreshOutlinerAndAudit();
    renderAll();
    showToast(
      `Imported ${importedParts.length} ${role === "low" ? "Low Poly" : "High Poly"} sub-mesh${importedParts.length > 1 ? "es" : ""}`
    );
  }

  // ----------------------------------------------------------------------------------------------------------------------------------------
  // TAB 1: Grid-Based Mesh Maps Grouped into Category Cards
  // ----------------------------------------------------------------------------------------------------------------------------------------

  function renderViewportPreviewDropdown() {
    const sel = $("ViewportPreviewSelect");
    const prevValue = state.previewTarget;
    sel.innerHTML = "";

    const blankOpt = document.createElement("option");
    blankOpt.value = "blank";
    blankOpt.textContent = "Blank Stage";
    sel.appendChild(blankOpt);

    for (const catName of CATEGORY_ORDER) {
      const entries = CATALOG.filter((m) => m.group === catName);
      if (!entries.length) continue;
      const og = document.createElement("optgroup");
      og.label = catName;
      for (const m of entries) {
        const isBaked = m.isVolume ? Boolean(state.bakedSdf) : Boolean(state.baked2d[m.id]);
        const opt = document.createElement("option");
        opt.value = m.id;
        opt.textContent = isBaked ? `● ${m.label}` : m.label;
        og.appendChild(opt);
      }
      sel.appendChild(og);
    }

    sel.value = prevValue;
  }

  function renderUnifiedMapGrid() {
    const container = $("UnifiedMapList");
    container.innerHTML = "";
    const q = state.searchQuery.trim().toLowerCase();

    const bakedCount = (state.bakedSdf ? 1 : 0) + Object.keys(state.baked2d).length;
    $("BakedCountLabel").textContent = String(bakedCount);

    for (const categoryName of CATEGORY_ORDER) {
      const items = CATALOG.filter((entry) => {
        if (entry.group !== categoryName) return false;
        if (!q) return true;
        return (
          entry.label.toLowerCase().includes(q) ||
          entry.group.toLowerCase().includes(q) ||
          (entry.short && entry.short.toLowerCase().includes(q))
        );
      });

      if (items.length === 0) continue;

      const isCollapsed = !q && state.collapsedCategories.has(categoryName);
      const bakedInCat = items.filter((m) => (m.isVolume ? Boolean(state.bakedSdf) : Boolean(state.baked2d[m.id]))).length;

      const card = document.createElement("section");
      card.className = "category-card";
      card.dataset.collapsed = isCollapsed ? "true" : "false";

      const headBtn = document.createElement("button");
      headBtn.type = "button";
      headBtn.className = "category-card-head";
      headBtn.innerHTML = `
        <span>${categoryName}</span>
        <span class="category-card-meta">
          <span>${bakedInCat}/${items.length}</span>
          <svg class="icon" style="width:12px;height:12px;transform:rotate(${isCollapsed ? "-90deg" : "0deg"});"><use href="#i-chevron" /></svg>
        </span>
      `;
      headBtn.addEventListener("click", () => {
        if (state.collapsedCategories.has(categoryName)) {
          state.collapsedCategories.delete(categoryName);
        } else {
          state.collapsedCategories.add(categoryName);
        }
        renderUnifiedMapGrid();
      });
      card.appendChild(headBtn);

      if (!isCollapsed) {
        const gridWrap = document.createElement("div");
        gridWrap.className = "category-grid";

        for (const entry of items) {
          const isSelected = state.selectedMapId === entry.id;
          const isQueued = state.checkedMaps.has(entry.id);
          const isBaked = entry.isVolume ? Boolean(state.bakedSdf) : Boolean(state.baked2d[entry.id]);

          const tile = document.createElement("div");
          tile.className = "map-grid-tile";
          tile.setAttribute("role", "option");
          tile.setAttribute("aria-selected", isSelected ? "true" : "false");
          tile.dataset.selected = isSelected ? "true" : "false";

          // Visual Preview Swatch Box
          const previewBox = document.createElement("div");
          previewBox.className = `tile-preview-box sw-${entry.swatch}`;

          if (entry.isVolume && state.bakedSdf) {
            const thumb = document.createElement("canvas");
            const maxS = getAxisDim(state.bakedSdf.dims, state.sdfAxis) - 1;
            Core.renderSdfSliceToCanvas(
              thumb,
              state.bakedSdf,
              state.sdfAxis,
              Math.round(state.sdfSliceNorm * maxS),
              state.sdfPalette,
              { thumbnail: true }
            );
            previewBox.appendChild(thumb);
          } else if (!entry.isVolume && state.baked2d[entry.id]) {
            const thumb = document.createElement("canvas");
            thumb.width = 128;
            thumb.height = 128;
            thumb.getContext("2d").drawImage(state.baked2d[entry.id].canvas, 0, 0, 128, 128);
            previewBox.appendChild(thumb);
          }

          // Resolution / Baked Badge Pill inside bottom-left of swatch
          let badgeLabel = "";
          if (entry.isVolume) {
            badgeLabel = state.bakedSdf ? formatSdfDims(state.bakedSdf.dims) : formatSdfDims(state.sdfDims);
          } else {
            const eff = getEffective2dDims(entry.id);
            const bakedObj = state.baked2d[entry.id];
            badgeLabel = bakedObj
              ? formatDimPair(bakedObj.width, bakedObj.height)
              : `${formatDimPair(eff.width, eff.height)}${eff.pinned ? " · pin" : ""}`;
          }

          const resBadge = document.createElement("span");
          resBadge.className = "tile-baked-pill";
          resBadge.innerHTML = `${isBaked ? '<span class="baked-dot"></span>' : ""}<span>${badgeLabel}</span>`;
          previewBox.appendChild(resBadge);

          // Queue Check Toggle inside top-right of swatch
          const queueBtn = document.createElement("button");
          queueBtn.type = "button";
          queueBtn.className = "tile-queue-btn";
          queueBtn.setAttribute("aria-checked", isQueued ? "true" : "false");
          queueBtn.title = "Include in batch queue";
          queueBtn.innerHTML = '<svg class="icon" style="width:11px;height:11px;"><use href="#i-check" /></svg>';
          queueBtn.addEventListener("click", (ev) => {
            ev.stopPropagation();
            if (state.checkedMaps.has(entry.id)) {
              state.checkedMaps.delete(entry.id);
            } else {
              state.checkedMaps.add(entry.id);
            }
            renderUnifiedMapGrid();
            updateBakeActionButtons();
          });
          previewBox.appendChild(queueBtn);

          // Clean Tile Footer (Title + Short subtitle / bake time)
          const meta = document.createElement("div");
          meta.className = "tile-meta";
          let subLine = entry.short || entry.group;
          if (entry.isVolume && state.bakedSdf) {
            subLine = `Float32 · ${state.bakedSdf.elapsedMs} ms`;
          } else if (!entry.isVolume && state.baked2d[entry.id]) {
            subLine = `Baked · ${state.baked2d[entry.id].elapsedMs} ms`;
          }
          meta.innerHTML = `
            <span class="tile-title">${entry.label}</span>
            <span class="tile-sub">${subLine}</span>
          `;

          tile.appendChild(previewBox);
          tile.appendChild(meta);

          tile.addEventListener("click", () => {
            state.selectedMapId = entry.id;
            state.previewTarget = entry.id;
            renderAll();
          });

          gridWrap.appendChild(tile);
        }

        card.appendChild(gridWrap);
      }

      container.appendChild(card);
    }
  }

  // ----------------------------------------------------------------------------------------------------------------------------------------
  // Inspector: Up to 16K + Custom Dimensions + Dynamic Map-Specific Properties
  // ----------------------------------------------------------------------------------------------------------------------------------------

  function renderDynamicMapProperties(mapEntry) {
    const card = $("DynamicMapPropsCard");
    if (mapEntry.isVolume) {
      card.hidden = true;
      return;
    }

    card.hidden = false;
    $("DynamicMapPropsTitle").textContent = `${mapEntry.label} Properties`;
    $("DynamicMapGroupTag").textContent = mapEntry.group;

    const body = $("DynamicMapPropsBody");
    body.innerHTML = "";
    const schema = SCHEMAS[mapEntry.id] || [];
    const propBag = state.perMapProps[mapEntry.id] || (state.perMapProps[mapEntry.id] = {});

    for (const field of schema) {
      const currentVal = propBag[field.key] !== undefined ? propBag[field.key] : field.default;

      if (field.type === "select") {
        const row = document.createElement("div");
        row.className = "field-row";
        const label = document.createElement("span");
        label.textContent = field.label;

        const select = document.createElement("select");
        for (const [val, text] of field.options) {
          const opt = document.createElement("option");
          opt.value = val;
          opt.textContent = text;
          if (String(val) === String(currentVal)) opt.selected = true;
          select.appendChild(opt);
        }
        select.addEventListener("change", () => {
          propBag[field.key] = select.value;
        });

        row.appendChild(label);
        row.appendChild(select);
        body.appendChild(row);
      } else if (field.type === "range") {
        const wrap = document.createElement("div");
        wrap.className = "slider-block";

        const head = document.createElement("div");
        head.className = "slider-head";
        const label = document.createElement("span");
        label.textContent = field.label;
        const out = document.createElement("output");
        const decimals = field.step < 0.05 ? 2 : field.step < 1 ? 2 : 0;
        const suffix = field.unit ? ` ${field.unit}` : "";
        out.textContent = `${Number(currentVal).toFixed(decimals)}${suffix}`;
        head.appendChild(label);
        head.appendChild(out);

        const input = document.createElement("input");
        input.type = "range";
        input.min = String(field.min);
        input.max = String(field.max);
        input.step = String(field.step);
        input.value = String(currentVal);
        input.addEventListener("input", () => {
          const v = Number(input.value);
          propBag[field.key] = v;
          out.textContent = `${v.toFixed(decimals)}${suffix}`;
        });

        wrap.appendChild(head);
        wrap.appendChild(input);
        body.appendChild(wrap);
      } else if (field.type === "toggle") {
        const row = document.createElement("label");
        row.className = "field-row";
        row.style.cursor = "pointer";

        const span = document.createElement("span");
        span.textContent = field.label;
        const chk = document.createElement("input");
        chk.type = "checkbox";
        chk.checked = Boolean(currentVal);
        chk.style.accentColor = "var(--text)";
        chk.addEventListener("change", () => {
          propBag[field.key] = chk.checked;
        });

        row.appendChild(span);
        row.appendChild(chk);
        body.appendChild(row);
      }
    }
  }

  function renderInspector() {
    const entry = getCatalogEntry(state.selectedMapId);
    const isSdf = Boolean(entry.isVolume);

    $("InspectorMapName").textContent = entry.label;
    $("Res2dControls").hidden = isSdf;
    $("ResSdfControls").hidden = !isSdf;
    $("SdfInspectorSection").hidden = !isSdf;
    $("CageDisclosure").hidden = isSdf;

    if (isSdf) {
      const [nx, ny, nz] = state.sdfDims;
      const isCubic = nx === ny && ny === nz;
      $("HeroMetricPrimary").textContent = isCubic ? `${nx}³` : `${nx}×${ny}×${nz}`;
      $("HeroMetricUnit").textContent = "voxels";

      const voxBytes = nx * ny * nz * 4;
      $("HeroMetricSub").textContent = state.bakedSdf
        ? `Baked in ${state.bakedSdf.elapsedMs} ms · ${formatBytes(voxBytes)} Float32`
        : `Float32 grid · ${formatBytes(voxBytes)}`;

      for (const btn of $("ResSdfPillRow").querySelectorAll("button")) {
        const tag = btn.dataset.ressdf;
        const active =
          tag === "custom"
            ? state.sdfResMode === "custom"
            : state.sdfResMode === "preset" && isCubic && Number(tag) === nx;
        btn.setAttribute("aria-pressed", active ? "true" : "false");
      }

      $("CustomSdfDimBox").hidden = state.sdfResMode !== "custom";
      $("CustomSdfXInput").value = String(nx);
      $("CustomSdfYInput").value = String(ny);
      $("CustomSdfZInput").value = String(nz);

      $("MeshAuditStatusLabel").textContent = state.meshAudit && state.meshAudit.closedManifold ? "Watertight" : "Open shell";
      $("SdfPaddingVal").textContent = `${Math.round(state.sdfPadding * 100)}%`;
      $("SdfBandVal").textContent = state.sdfNarrowBand === 0 ? "Full" : `${state.sdfNarrowBand.toFixed(2)}`;

      renderDynamicMapProperties(entry);
      renderSdfTriptych();
    } else {
      const eff = getEffective2dDims(entry.id);
      const isSquarePreset = eff.mode === "preset" && eff.width === eff.height && PRESET_2D_SIZES.includes(eff.width);

      $("HeroMetricPrimary").textContent = String(eff.width);
      $("HeroMetricUnit").textContent = `× ${eff.height}`;

      const rawBytes = eff.width * eff.height * bytesPerPixelForFormat(state.outputFormat);
      const bakedObj = state.baked2d[entry.id];
      $("HeroMetricSub").textContent = bakedObj
        ? `Baked ${bakedObj.width}×${bakedObj.height} · ${formatBytes(rawBytes)}`
        : `${eff.mode === "custom" ? "Custom" : formatDimPair(eff.width, eff.height)} · ${formatBytes(rawBytes)}`;

      for (const btn of $("Res2dPillRow").querySelectorAll("button")) {
        const tag = btn.dataset.res2d;
        const active =
          tag === "custom"
            ? !isSquarePreset
            : isSquarePreset && Number(tag) === eff.width;
        btn.setAttribute("aria-pressed", active ? "true" : "false");
      }

      $("Custom2dDimBox").hidden = isSquarePreset;
      $("CustomWidthInput").value = String(eff.width);
      $("CustomHeightInput").value = String(eff.height);
      $("LockAspectBtn").setAttribute("aria-pressed", state.lockAspect ? "true" : "false");
      $("PinMapResCheckbox").checked = eff.pinned;
      $("OutputFormatSelect").value = state.outputFormat;
      $("DilationValLabel").textContent = `${state.paddingPx} px`;

      renderDynamicMapProperties(entry);
    }

    updateBakeActionButtons();
  }

  function apply2dResolutionChange(width, height, mode) {
    const w = Math.max(64, Math.min(16384, Math.round(Number(width) || 2048)));
    const h = Math.max(64, Math.min(16384, Math.round(Number(height) || 2048)));

    if (state.mapResOverrides[state.selectedMapId]) {
      state.mapResOverrides[state.selectedMapId] = { width: w, height: h, mode };
    } else {
      state.globalWidth = w;
      state.globalHeight = h;
      state.res2dMode = mode;
    }
    renderUnifiedMapGrid();
    renderInspector();
  }

  function updateBakeActionButtons() {
    const entry = getCatalogEntry(state.selectedMapId);
    $("BakeSelectedBtn").textContent = `Bake ${entry.label}`;
    $("BakeSelectedBtn").disabled = state.isBaking;

    const n = state.checkedMaps.size;
    $("BakeCheckedBtn").textContent = `Batch (${n})`;
    $("BakeCheckedBtn").disabled = state.isBaking || n === 0;
  }

  // ----------------------------------------------------------------------------------------------------------------------------------------
  // Center Stage Rendering (Blank | Volume SDF 3D+2D | 2D Map)
  // ----------------------------------------------------------------------------------------------------------------------------------------

  function renderCurrentSdf3d() {
    if (!state.bakedSdf) return;
    const maxIdx = getAxisDim(state.bakedSdf.dims, state.sdfAxis) - 1;
    const sliceIndex = Math.round(state.sdfSliceNorm * maxIdx);
    const mode3d = MODE_3D_CYCLE[state.sdfMode3dIndex][0];
    Core.renderSdf3dPreview($("Sdf3dCanvas"), state.bakedSdf, {
      yaw: state.sdfYaw,
      pitch: state.sdfPitch,
      zoom: state.sdfZoom,
      mode3d,
      displayMode: state.sdfPalette,
      axis: state.sdfAxis,
      sliceIndex,
    });
  }

  function renderStage() {
    renderViewportPreviewDropdown();

    const target = state.previewTarget;
    const isBlank = target === "blank";
    const isSdf = target === "volumeSdf";
    const is2d = !isBlank && !isSdf;

    $("BlankViewportStage").hidden = !isBlank;
    $("SdfViewportStage").hidden = !isSdf;
    $("Map2dViewportStage").hidden = !is2d;
    $("FloatingStageDock").hidden = isBlank;
    $("ChannelPillWrap").hidden = isBlank;

    $("ChannelSelect").hidden = !is2d;
    $("SdfPaletteSelect").hidden = !isSdf;

    $("SdfLayoutGroup").hidden = !isSdf;
    $("SdfDockDivider1").hidden = !isSdf;
    $("SdfAxisGroup").hidden = !isSdf;
    $("SdfSliceSliderWrap").hidden = !isSdf;
    $("SdfCutGroup").hidden = !isSdf;
    $("Map2dDockInfo").hidden = !is2d;

    if (isBlank) {
      $("StageTitle").textContent = "Blank Stage";
      $("StageProbeReadout").textContent = `${state.asset.label}`;
      $("ExportBtnLabel").textContent = "Export";
      $("ExportActiveBtn").disabled = true;
      return;
    }

    if (isSdf) {
      $("StageTitle").textContent = "Volume SDF";
      $("ExportBtnLabel").textContent = "Export .fvol";
      $("ExportActiveBtn").disabled = !state.bakedSdf;

      const stageEl = $("SdfViewportStage");
      stageEl.dataset.layout = state.sdfLayout === "split" ? "split" : "single";
      $("Sdf3dPane").hidden = state.sdfLayout === "2d";
      $("Sdf2dPane").hidden = state.sdfLayout === "3d";

      for (const b of $("SdfLayoutGroup").querySelectorAll("button")) {
        b.setAttribute("aria-pressed", b.dataset.sdfLayout === state.sdfLayout ? "true" : "false");
      }
      for (const b of $("SdfAxisGroup").querySelectorAll("button")) {
        b.setAttribute("aria-pressed", b.dataset.sdfAxis === state.sdfAxis ? "true" : "false");
      }
      $("SdfMode3dToggle").textContent = MODE_3D_CYCLE[state.sdfMode3dIndex][1];

      if (state.bakedSdf) {
        const maxIdx = getAxisDim(state.bakedSdf.dims, state.sdfAxis) - 1;
        const idx = Math.round(state.sdfSliceNorm * maxIdx);
        $("Sdf3dMetaLabel").textContent = formatSdfDims(state.bakedSdf.dims);
        $("Sdf2dMetaLabel").textContent = `${state.sdfAxis.toUpperCase()} · ${Math.round(state.sdfSliceNorm * 100)}%`;
        $("SdfSliceValueLabel").textContent = `${idx} / ${maxIdx}`;

        if (state.sdfLayout !== "2d") {
          renderCurrentSdf3d();
        }
        if (state.sdfLayout !== "3d") {
          Core.renderSdfSliceToCanvas($("Sdf2dCanvas"), state.bakedSdf, state.sdfAxis, idx, state.sdfPalette);
        }
      }
      return;
    }

    const entry = getCatalogEntry(target);
    $("StageTitle").textContent = entry.label;
    $("ExportBtnLabel").textContent = "Export .png";
    const baked = state.baked2d[target];
    $("ExportActiveBtn").disabled = !baked;

    if (!baked) {
      $("Map2dFrameWrap").hidden = true;
      $("Map2dUnbakedPrompt").hidden = false;
      const eff = getEffective2dDims(target);
      $("UnbakedTitle").textContent = `${entry.label} (${formatDimPair(eff.width, eff.height)}) is not baked yet`;
      $("EmptyStateBakeBtn").textContent = `Bake ${entry.label}`;
      $("StageProbeReadout").textContent = "Ready";
    } else {
      $("Map2dFrameWrap").hidden = false;
      $("Map2dUnbakedPrompt").hidden = true;
      const cv = $("Map2dCanvas");
      Core.render2dMapWithChannel(cv, baked, state.asset.low, {
        channel: state.channelMode,
        uvOverlay: state.uvOverlay,
      });
      cv.style.maxWidth = state.zoomActual ? "none" : "min(100%, 540px)";
      cv.style.maxHeight = state.zoomActual ? "none" : "calc(100vh - 255px)";
      $("StageProbeReadout").textContent = `${baked.width} × ${baked.height} · ${state.channelMode.toUpperCase()}`;
    }
  }

  function renderSdfTriptych() {
    if (!state.bakedSdf) return;
    const axes = [
      ["x", "ThumbAxisX"],
      ["y", "ThumbAxisY"],
      ["z", "ThumbAxisZ"],
    ];
    for (const [ax, canvasId] of axes) {
      const c = $(canvasId);
      const maxIdx = getAxisDim(state.bakedSdf.dims, ax) - 1;
      const norm = ax === state.sdfAxis ? state.sdfSliceNorm : 0.5;
      Core.renderSdfSliceToCanvas(c, state.bakedSdf, ax, Math.round(norm * maxIdx), state.sdfPalette, { thumbnail: true });
    }
    for (const tile of $("TriptychGrid").querySelectorAll(".triptych-tile")) {
      tile.setAttribute("aria-pressed", tile.dataset.cutAxis === state.sdfAxis ? "true" : "false");
    }
  }

  function renderAll() {
    renderLeftTabs();
    renderDualOutliner();
    renderUnifiedMapGrid();
    renderInspector();
    renderStage();
  }

  // ----------------------------------------------------------------------------------------------------------------------------------------
  // Baking Execution (Volume SDF + 27 2D Maps with Custom W x H & Map-Specific Properties)
  // ----------------------------------------------------------------------------------------------------------------------------------------

  function setProgress(visible, label, frac) {
    const wrap = $("BakeProgressWrap");
    wrap.hidden = !visible;
    if (visible) {
      $("BakeProgressLabel").textContent = label;
      $("BakeProgressBar").value = Math.max(0, Math.min(1, frac || 0));
    }
  }

  async function runVolumeSdfBake() {
    state.isBaking = true;
    updateBakeActionButtons();
    const [nx, ny, nz] = state.sdfDims;
    setProgress(true, `Baking Volume SDF (${formatSdfDims(state.sdfDims)})…`, 0.15);

    try {
      const targetMesh = state.asset.high || state.asset.low;
      state.bakedSdf = await Core.bakeVolumeSdf(
        targetMesh,
        {
          nx,
          ny,
          nz,
          padding: state.sdfPadding,
          signMode: state.sdfSignMode,
          narrowBand: state.sdfNarrowBand,
        },
        (frac) => setProgress(true, `Baking Volume SDF (${formatSdfDims(state.sdfDims)})…`, frac)
      );
      state.previewTarget = "volumeSdf";
      $("StageProbeReadout").textContent = `min ${state.bakedSdf.minDistance.toFixed(3)} · max +${state.bakedSdf.maxDistance.toFixed(3)}`;
      showToast(`Volume SDF baked (${formatSdfDims(state.bakedSdf.dims)})`);
    } catch (err) {
      showToast(`SDF error: ${err.message}`);
    } finally {
      state.isBaking = false;
      setProgress(false);
      renderAll();
    }
  }

  async function bakeSingle2dAsync(mapId) {
    const eff = getEffective2dDims(mapId);
    const props = state.perMapProps[mapId] || {};
    state.baked2d[mapId] = await Core.bakeMeshMap2D(mapId, state.asset, {
      width: eff.width,
      height: eff.height,
      format: state.outputFormat,
      paddingPx: state.paddingPx,
      matchMode: state.matchMode,
      frontalDistance: state.frontalDistance,
      rearDistance: state.rearDistance,
      ...props,
    });
  }

  async function runSingleMapBake(mapId) {
    if (mapId === "volumeSdf") {
      await runVolumeSdfBake();
      return;
    }
    const entry = getCatalogEntry(mapId);
    const eff = getEffective2dDims(mapId);
    state.isBaking = true;
    updateBakeActionButtons();
    setProgress(true, `Baking ${entry.label} (${formatDimPair(eff.width, eff.height)})…`, 0.35);

    try {
      await new Promise((r) => requestAnimationFrame(r));
      await bakeSingle2dAsync(mapId);
      state.previewTarget = mapId;
      showToast(`Baked ${entry.label} (${eff.width} × ${eff.height})`);
    } catch (err) {
      showToast(`Bake error: ${err.message}`);
    } finally {
      state.isBaking = false;
      setProgress(false);
      renderAll();
    }
  }

  async function runBatchBake() {
    const queue = CATALOG.map((m) => m.id).filter((id) => state.checkedMaps.has(id));
    if (!queue.length) return;

    state.isBaking = true;
    updateBakeActionButtons();

    try {
      for (let idx = 0; idx < queue.length; idx++) {
        const id = queue[idx];
        const entry = getCatalogEntry(id);
        setProgress(true, `${entry.label} (${idx + 1}/${queue.length})`, idx / queue.length);
        await new Promise((r) => requestAnimationFrame(r));

        if (id === "volumeSdf") {
          const [nx, ny, nz] = state.sdfDims;
          state.bakedSdf = await Core.bakeVolumeSdf(state.asset.high || state.asset.low, {
            nx,
            ny,
            nz,
            padding: state.sdfPadding,
            signMode: state.sdfSignMode,
            narrowBand: state.sdfNarrowBand,
          });
        } else {
          await bakeSingle2dAsync(id);
        }
      }
      if (!queue.includes(state.previewTarget)) {
        state.previewTarget = queue[0];
      }
      showToast(`Baked ${queue.length} maps`);
    } catch (err) {
      showToast(`Batch error: ${err.message}`);
    } finally {
      state.isBaking = false;
      setProgress(false);
      renderAll();
    }
  }

  // ----------------------------------------------------------------------------------------------------------------------------------------
  // Export (.fvol binary Float32 SDF & .png 2D maps)
  // ----------------------------------------------------------------------------------------------------------------------------------------

  function exportActiveOutput() {
    if (state.previewTarget === "volumeSdf" && state.bakedSdf) {
      const vol = state.bakedSdf;
      const headerBytes = 36;
      const buf = new ArrayBuffer(headerBytes + vol.distances.byteLength);
      const dv = new DataView(buf);
      dv.setUint8(0, 70); dv.setUint8(1, 86); dv.setUint8(2, 79); dv.setUint8(3, 76);
      dv.setUint32(4, 1, true);
      dv.setUint32(8, vol.dims[0], true);
      dv.setUint32(12, vol.dims[1], true);
      dv.setUint32(16, vol.dims[2], true);
      dv.setFloat32(20, vol.bounds.min[0], true);
      dv.setFloat32(24, vol.bounds.min[1], true);
      dv.setFloat32(28, vol.bounds.min[2], true);
      dv.setFloat32(32, vol.bounds.step[0], true);
      new Float32Array(buf, headerBytes).set(vol.distances);

      const blob = new Blob([buf], { type: "application/octet-stream" });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = `${state.studyShape.toLowerCase()}_sdf_${vol.dims.join("x")}.fvol`;
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      showToast(`Exported ${a.download}`);
      return;
    }

    const baked = state.baked2d[state.previewTarget];
    if (baked) {
      baked.canvas.toBlob((blob) => {
        if (!blob) return;
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = `${state.studyShape.toLowerCase()}_${state.previewTarget}_${baked.width}x${baked.height}.png`;
        a.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
        showToast(`Exported ${a.download}`);
      }, "image/png");
    }
  }

  // ----------------------------------------------------------------------------------------------------------------------------------------
  // Interactive Events (Left Tabs, Orbit, Probe, Dual Outliner Drop, Custom Resolution)
  // ----------------------------------------------------------------------------------------------------------------------------------------

  function bindEvents() {
    // 2 Separate Left-Panel Tabs: Mesh Maps | Scene Outliner
    $("LeftTabMapsBtn").addEventListener("click", () => {
      state.leftTab = "maps";
      renderLeftTabs();
    });

    $("LeftTabOutlinerBtn").addEventListener("click", () => {
      state.leftTab = "outliner";
      renderLeftTabs();
    });

    // Dual Outliner Controls
    $("MatchModeSelect").addEventListener("change", (ev) => {
      state.matchMode = ev.target.value;
    });

    $("ResetStudyMeshesBtn").addEventListener("click", () => {
      loadStudyShape(state.studyShape, true);
      showToast(`Reset ${state.studyShape} Low & High meshes`);
    });

    $("AddLowMeshBtn").addEventListener("click", () => $("LowMeshFileInput").click());
    $("AddHighMeshBtn").addEventListener("click", () => $("HighMeshFileInput").click());

    $("LowMeshFileInput").addEventListener("change", (ev) => {
      importMeshFilesIntoOutliner(ev.target.files, "low");
      ev.target.value = "";
    });
    $("HighMeshFileInput").addEventListener("change", (ev) => {
      importMeshFilesIntoOutliner(ev.target.files, "high");
      ev.target.value = "";
    });

    const wireDropZone = (colEl, role) => {
      colEl.addEventListener("dragover", (ev) => {
        ev.preventDefault();
        colEl.classList.add("drag-over");
      });
      colEl.addEventListener("dragleave", () => colEl.classList.remove("drag-over"));
      colEl.addEventListener("drop", (ev) => {
        ev.preventDefault();
        colEl.classList.remove("drag-over");
        if (ev.dataTransfer && ev.dataTransfer.files.length) {
          importMeshFilesIntoOutliner(ev.dataTransfer.files, role);
        }
      });
    };
    wireDropZone($("LowDropCol"), "low");
    wireDropZone($("HighDropCol"), "high");

    $("MapSearchInput").addEventListener("input", (ev) => {
      state.searchQuery = ev.target.value || "";
      renderUnifiedMapGrid();
    });

    $("StudyShapeSelect").addEventListener("change", (ev) => {
      loadStudyShape(ev.target.value, true);
    });

    $("ViewportPreviewSelect").addEventListener("change", (ev) => {
      const val = ev.target.value;
      state.previewTarget = val;
      if (val !== "blank") state.selectedMapId = val;
      renderAll();
    });

    $("ChannelSelect").addEventListener("change", (ev) => {
      state.channelMode = ev.target.value;
      renderStage();
    });

    $("SdfPaletteSelect").addEventListener("change", (ev) => {
      state.sdfPalette = ev.target.value;
      renderStage();
      renderSdfTriptych();
      renderUnifiedMapGrid();
    });

    for (const btn of $("SdfLayoutGroup").querySelectorAll("button")) {
      btn.addEventListener("click", () => {
        state.sdfLayout = btn.dataset.sdfLayout;
        renderStage();
      });
    }
    for (const btn of $("SdfAxisGroup").querySelectorAll("button")) {
      btn.addEventListener("click", () => {
        state.sdfAxis = btn.dataset.sdfAxis;
        renderStage();
        renderSdfTriptych();
      });
    }
    $("SdfSliceSlider").addEventListener("input", (ev) => {
      state.sdfSliceNorm = Number(ev.target.value) / 100;
      renderStage();
      renderSdfTriptych();
    });
    $("SdfMode3dToggle").addEventListener("click", () => {
      state.sdfMode3dIndex = (state.sdfMode3dIndex + 1) % MODE_3D_CYCLE.length;
      renderStage();
    });

    $("ZoomFitBtn").addEventListener("click", () => {
      state.zoomActual = false;
      $("ZoomFitBtn").setAttribute("aria-pressed", "true");
      $("ZoomActualBtn").setAttribute("aria-pressed", "false");
      renderStage();
    });
    $("ZoomActualBtn").addEventListener("click", () => {
      state.zoomActual = true;
      $("ZoomFitBtn").setAttribute("aria-pressed", "false");
      $("ZoomActualBtn").setAttribute("aria-pressed", "true");
      renderStage();
    });
    $("UvOverlayBtn").addEventListener("click", () => {
      state.uvOverlay = !state.uvOverlay;
      $("UvOverlayBtn").setAttribute("aria-pressed", state.uvOverlay ? "true" : "false");
      renderStage();
    });

    $("PreviewSelectedBtn").addEventListener("click", () => {
      state.previewTarget = state.selectedMapId;
      renderStage();
    });

    // Inspector: 2D Resolution Pills (512 .. 16K + Custom)
    for (const btn of $("Res2dPillRow").querySelectorAll("button")) {
      btn.addEventListener("click", () => {
        const tag = btn.dataset.res2d;
        if (tag === "custom") {
          const eff = getEffective2dDims(state.selectedMapId);
          apply2dResolutionChange(eff.width, eff.height, "custom");
        } else {
          const px = Number(tag);
          apply2dResolutionChange(px, px, "preset");
        }
      });
    }

    $("LockAspectBtn").addEventListener("click", () => {
      state.lockAspect = !state.lockAspect;
      if (state.lockAspect) {
        const eff = getEffective2dDims(state.selectedMapId);
        apply2dResolutionChange(eff.width, eff.width, "custom");
      } else {
        renderInspector();
      }
    });

    $("CustomWidthInput").addEventListener("change", (ev) => {
      const w = Math.max(64, Math.min(16384, Math.round(Number(ev.target.value) || 2048)));
      const eff = getEffective2dDims(state.selectedMapId);
      const h = state.lockAspect ? w : eff.height;
      apply2dResolutionChange(w, h, "custom");
    });

    $("CustomHeightInput").addEventListener("change", (ev) => {
      const h = Math.max(64, Math.min(16384, Math.round(Number(ev.target.value) || 2048)));
      const eff = getEffective2dDims(state.selectedMapId);
      const w = state.lockAspect ? h : eff.width;
      apply2dResolutionChange(w, h, "custom");
    });

    $("PinMapResCheckbox").addEventListener("change", (ev) => {
      if (ev.target.checked) {
        state.mapResOverrides[state.selectedMapId] = {
          width: state.globalWidth,
          height: state.globalHeight,
          mode: state.res2dMode,
        };
      } else {
        delete state.mapResOverrides[state.selectedMapId];
      }
      renderUnifiedMapGrid();
      renderInspector();
    });

    $("OutputFormatSelect").addEventListener("change", (ev) => {
      state.outputFormat = ev.target.value;
      renderInspector();
    });

    $("DilationSlider").addEventListener("input", (ev) => {
      state.paddingPx = Number(ev.target.value);
      $("DilationValLabel").textContent = `${state.paddingPx} px`;
    });

    // Inspector: 3D Volume SDF Resolution Pills (32³ .. 128³ + Custom Nx×Ny×Nz)
    for (const btn of $("ResSdfPillRow").querySelectorAll("button")) {
      btn.addEventListener("click", () => {
        const tag = btn.dataset.ressdf;
        if (tag === "custom") {
          state.sdfResMode = "custom";
        } else {
          const n = Number(tag);
          state.sdfResMode = "preset";
          state.sdfDims = [n, n, n];
        }
        renderUnifiedMapGrid();
        renderInspector();
      });
    }

    const onCustomSdfAxisChange = () => {
      const nx = Math.max(8, Math.min(256, Math.round(Number($("CustomSdfXInput").value) || 64)));
      const ny = Math.max(8, Math.min(256, Math.round(Number($("CustomSdfYInput").value) || 64)));
      const nz = Math.max(8, Math.min(256, Math.round(Number($("CustomSdfZInput").value) || 64)));
      state.sdfResMode = "custom";
      state.sdfDims = [nx, ny, nz];
      renderUnifiedMapGrid();
      renderInspector();
    };
    $("CustomSdfXInput").addEventListener("change", onCustomSdfAxisChange);
    $("CustomSdfYInput").addEventListener("change", onCustomSdfAxisChange);
    $("CustomSdfZInput").addEventListener("change", onCustomSdfAxisChange);

    // Volume SDF Properties
    $("SdfSignModeSelect").addEventListener("change", (ev) => {
      state.sdfSignMode = ev.target.value;
    });
    $("SdfPaddingSlider").addEventListener("input", (ev) => {
      state.sdfPadding = Number(ev.target.value) / 100;
      $("SdfPaddingVal").textContent = `${ev.target.value}%`;
    });
    $("SdfBandSlider").addEventListener("input", (ev) => {
      const vox = Number(ev.target.value);
      state.sdfNarrowBand = vox === 0 ? 0 : vox * 0.03;
      $("SdfBandVal").textContent = vox === 0 ? "Full" : `${vox} vox`;
    });

    for (const tile of $("TriptychGrid").querySelectorAll(".triptych-tile")) {
      tile.addEventListener("click", () => {
        state.sdfAxis = tile.dataset.cutAxis;
        state.previewTarget = "volumeSdf";
        renderStage();
        renderSdfTriptych();
      });
    }

    // Cage Disclosure
    $("FrontDistSlider").addEventListener("input", (ev) => {
      state.frontalDistance = Number(ev.target.value);
      $("FrontDistVal").textContent = state.frontalDistance.toFixed(3);
    });
    $("RearDistSlider").addEventListener("input", (ev) => {
      state.rearDistance = Number(ev.target.value);
      $("RearDistVal").textContent = state.rearDistance.toFixed(3);
    });

    // Bake & Export Actions
    $("BakeSelectedBtn").addEventListener("click", () => runSingleMapBake(state.selectedMapId));
    $("EmptyStateBakeBtn").addEventListener("click", () => runSingleMapBake(state.previewTarget));
    $("BakeCheckedBtn").addEventListener("click", runBatchBake);
    $("ExportActiveBtn").addEventListener("click", exportActiveOutput);

    // 3D SDF Orbit & Zoom
    const c3d = $("Sdf3dCanvas");
    let dragging = false;
    let lx = 0, ly = 0;
    c3d.addEventListener("pointerdown", (ev) => {
      dragging = true;
      lx = ev.clientX;
      ly = ev.clientY;
      c3d.setPointerCapture(ev.pointerId);
    });
    c3d.addEventListener("pointermove", (ev) => {
      if (!dragging) return;
      state.sdfYaw += (ev.clientX - lx) * 0.011;
      state.sdfPitch = Math.max(-1.35, Math.min(1.35, state.sdfPitch + (ev.clientY - ly) * 0.011));
      lx = ev.clientX;
      ly = ev.clientY;
      renderCurrentSdf3d();
    });
    c3d.addEventListener("pointerup", () => { dragging = false; });
    c3d.addEventListener("wheel", (ev) => {
      ev.preventDefault();
      state.sdfZoom = Math.max(0.55, Math.min(1.85, state.sdfZoom * (ev.deltaY > 0 ? 0.93 : 1.07)));
      renderCurrentSdf3d();
    }, { passive: false });

    // 2D SDF Slice Probe
    const c2d = $("Sdf2dCanvas");
    c2d.addEventListener("pointermove", (ev) => {
      if (!state.bakedSdf) return;
      const r = c2d.getBoundingClientRect();
      const nx = Math.max(0, Math.min(0.999, (ev.clientX - r.left) / r.width));
      const ny = Math.max(0, Math.min(0.999, (ev.clientY - r.top) / r.height));
      const maxIdx = getAxisDim(state.bakedSdf.dims, state.sdfAxis) - 1;
      const probe = Core.probeSdfSlice(state.bakedSdf, state.sdfAxis, Math.round(state.sdfSliceNorm * maxIdx), nx, ny);
      if (!probe) return;
      const d = probe.distance;
      const dv = d / state.bakedSdf.bounds.step[0];
      $("StageProbeReadout").textContent = `[${probe.voxel.join(",")}]  d=${d >= 0 ? "+" : ""}${d.toFixed(4)} (${dv >= 0 ? "+" : ""}${dv.toFixed(2)} vox)`;
    });

    // 2D Map Pixel Probe
    const m2d = $("Map2dCanvas");
    m2d.addEventListener("pointermove", (ev) => {
      const baked = state.baked2d[state.previewTarget];
      if (!baked) return;
      const r = m2d.getBoundingClientRect();
      const bx = Math.max(0, Math.min(baked.bufferWidth - 1, Math.floor(((ev.clientX - r.left) / r.width) * baked.bufferWidth)));
      const by = Math.max(0, Math.min(baked.bufferHeight - 1, Math.floor(((ev.clientY - r.top) / r.height) * baked.bufferHeight)));
      const p = (by * baked.bufferWidth + bx) * 4;
      const tx = Math.round((bx / Math.max(1, baked.bufferWidth - 1)) * (baked.width - 1));
      const ty = Math.round((by / Math.max(1, baked.bufferHeight - 1)) * (baked.height - 1));
      $("StageProbeReadout").textContent = `(${tx}, ${ty}) · RGB ${baked.rgba[p]}, ${baked.rgba[p + 1]}, ${baked.rgba[p + 2]}`;
    });
  }

  function init() {
    bindEvents();
    loadStudyShape("Torus", true);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
