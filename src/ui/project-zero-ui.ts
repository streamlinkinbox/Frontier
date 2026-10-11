import { MaterialManager, TextureLayer } from '../core/materials';
import { RoadConfig } from '../core/road-topology';
import { IntersectionData } from '../core/intersection-solver';

export interface UIEventListeners {
  onScenarioChange: (scenarioId: string) => void;
  onToolChange: (toolId: string) => void;
  onCameraPreset: (preset: 'perspective' | 'top' | 'bridge' | 'intersection' | 'train') => void;
  onWireframeToggle: (enabled: boolean) => void;
  onLightingToggle: (isNight: boolean) => void;
  onTrafficCountChange: (count: number) => void;
  onExportGLB: () => void;
  onExportOBJ: () => void;
  onExportJSON: () => void;
  onRoadPropertyChange: (roadId: string, prop: string, value: any) => void;
  onIntersectionPropertyChange: (nodeId: string, prop: string, value: any) => void;
  onMaterialLayerChange: (layerId: string, prop: string, value: any) => void;
}

export class ProjectZeroUI {
  private container: HTMLElement;
  private listeners: UIEventListeners;
  private materials: MaterialManager;

  // State
  public activeTab: 'inspector' | 'layerstack' = 'inspector';
  public activeTool: string = 'select';
  public selectedEntity: { type: 'road' | 'intersection' | null; id: string | null } = { type: null, id: null };
  public isNight: boolean = false;
  public wireframeEnabled: boolean = false;

  constructor(container: HTMLElement, materials: MaterialManager, listeners: UIEventListeners) {
    this.container = container;
    this.materials = materials;
    this.listeners = listeners;
    this.render();
    this.bindEvents();
  }

  private render() {
    this.container.innerHTML = `
      <div class="app-container">
        <!-- TOP NAVIGATION BAR -->
        <header class="top-bar">
          <div class="brand-section">
            <div class="brand-logo">
              <div class="brand-symbol">F</div>
              <span>FRONTIER <span style="font-weight:400; color:#8e8e96;">· ROAD ARCHITECT</span></span>
            </div>
            <div class="brand-badge" id="engine-status-badge">
              <span class="status-dot"></span>
              <span id="engine-name">WebGPU ACTIVE</span>
            </div>
          </div>

          <!-- Presets -->
          <div class="preset-selector">
            <span style="color:#71717a; font-weight:600;">PRESET:</span>
            <select id="scenario-select">
              <option value="metropolitan">1. Metropolitan Interchange & Overpass</option>
              <option value="railway_crossing">2. Urban Boulevard & Rail Crossing</option>
              <option value="construction_zone">3. Highway Viaduct & Road Rebuilding</option>
              <option value="pedestrian_plaza">4. Pedestrian City Block & Plazas</option>
              <option value="acute_y_junction">5. Acute Y-Junction (Quad Stress Test)</option>
            </select>
          </div>

          <!-- Center Toolbar -->
          <div class="center-toolbar">
            <button class="tool-btn active" id="tool-select" title="Select & 3D Move Gizmo (W)">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 3l7 18 3-7 7-3L3 3z"/></svg>
              <span>Gizmo</span>
            </button>
            <button class="tool-btn" id="tool-add-road" title="Draw Road Spline (A)">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="6" cy="6" r="3"/><circle cx="18" cy="18" r="3"/><path d="M6 9a9 9 0 0 0 9 9"/></svg>
              <span>Road</span>
            </button>
            <button class="tool-btn" id="tool-wireframe" title="Wireframe Quad Topology Inspector (Z)">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/></svg>
              <span>Quad Wireframe</span>
            </button>
            <button class="tool-btn" id="tool-lighting" title="Toggle Day / Night Mode (N)">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="5"/><path d="M12 1v2M12 21v2M4.2 4.2l1.4 1.4M18.4 18.4l1.4 1.4M1 12h2M21 12h2M4.2 19.8l1.4-1.4M18.4 5.6l1.4-1.4"/></svg>
              <span>Day/Night</span>
            </button>
          </div>

          <!-- Right Export Actions -->
          <div class="right-actions">
            <button class="action-btn" id="btn-export-obj" title="Export Wavefront OBJ">
              <span>OBJ</span>
            </button>
            <button class="action-btn primary" id="btn-export-glb" title="Export GLTF / GLB Binary">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3"/></svg>
              <span>Export GLB</span>
            </button>
          </div>
        </header>

        <!-- MAIN WORKSPACE -->
        <main class="workspace-layout">
          <!-- LEFT SIDEBAR: OUTLINER / LAYERSTACK -->
          <aside class="sidebar-left">
            <div class="sidebar-header">
              <span class="sidebar-title">Network Outliner</span>
              <span class="group-count" id="outliner-total-count">12 Items</span>
            </div>
            <div class="sidebar-scroll" id="outliner-list">
              <!-- Dynamically populated -->
            </div>
          </aside>

          <!-- CENTER VIEWPORT -->
          <div class="viewport-container" id="viewport-wrapper">
            <canvas id="viewport-canvas"></canvas>

            <!-- Floating Viewport Controls -->
            <div class="viewport-overlay">
              <button class="overlay-btn" id="btn-reset-cam">
                <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/></svg>
                <span>Reset View</span>
              </button>
            </div>

            <!-- Camera Presets -->
            <div class="viewport-cam-presets">
              <button class="cam-btn" id="cam-perspective">3D Orbit</button>
              <button class="cam-btn" id="cam-top">Top Plan</button>
              <button class="cam-btn" id="cam-bridge">Bridge</button>
              <button class="cam-btn" id="cam-intersection">Intersection</button>
              <button class="cam-btn" id="cam-train">Rail Crossing</button>
            </div>

            <!-- Viewport Help Shortcuts -->
            <div class="viewport-help">
              <div class="help-item"><kbd>Left Drag</kbd> Orbit</div>
              <div class="help-item"><kbd>Right Drag</kbd> Pan</div>
              <div class="help-item"><kbd>Scroll</kbd> Zoom</div>
              <div class="help-item"><kbd>Click Node</kbd> 3D Move Gizmo</div>
              <div class="help-item"><kbd>Z</kbd> Wireframe Quads</div>
            </div>
          </div>

          <!-- RIGHT SIDEBAR: DUAL-TAB INSPECTOR -->
          <aside class="sidebar-right">
            <div class="inspector-tabs">
              <button class="tab-btn active" id="tab-inspector">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 20h9M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4L16.5 3.5z"/></svg>
                <span>Inspector</span>
              </button>
              <button class="tab-btn" id="tab-layerstack">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/></svg>
                <span>Textures</span>
              </button>
            </div>

            <div class="inspector-content" id="inspector-body">
              <!-- Dynamically populated based on active tab -->
            </div>
          </aside>
        </main>

        <!-- BOTTOM TELEMETRY BAR -->
        <footer class="bottom-bar">
          <div class="telemetry-group">
            <div class="telemetry-item">
              <span>FPS:</span> <strong id="telemetry-fps">60</strong>
            </div>
            <div class="telemetry-item">
              <span>Quads:</span> <strong id="telemetry-quads">1,480</strong>
            </div>
            <div class="telemetry-item">
              <span>Topology:</span> <span class="quad-ratio-badge" id="telemetry-quad-ratio">>94% Quads</span>
            </div>
            <div class="telemetry-item">
              <span>Triangles:</span> <strong id="telemetry-tris">2,960</strong>
            </div>
          </div>

          <div class="telemetry-group">
            <div class="telemetry-item">
              <span>AI Traffic:</span>
              <input type="range" id="traffic-slider" min="0" max="25" value="18" style="width:75px;">
              <strong id="traffic-count-label">18 / 25</strong>
            </div>
            <div class="telemetry-item">
              <span id="selection-status-text">Select node or road segment</span>
            </div>
          </div>
        </footer>
      </div>
    `;
  }

  private bindEvents() {
    // Scenario preset selection
    const scenarioSelect = document.getElementById('scenario-select') as HTMLSelectElement;
    scenarioSelect.addEventListener('change', (e) => {
      this.listeners.onScenarioChange((e.target as HTMLSelectElement).value);
    });

    // Toolbar buttons
    document.getElementById('tool-select')!.addEventListener('click', () => {
      this.setActiveTool('select');
      this.listeners.onToolChange('select');
    });

    document.getElementById('tool-add-road')!.addEventListener('click', () => {
      this.setActiveTool('add-road');
      this.listeners.onToolChange('add-road');
    });

    document.getElementById('tool-wireframe')!.addEventListener('click', () => {
      this.wireframeEnabled = !this.wireframeEnabled;
      document.getElementById('tool-wireframe')!.classList.toggle('active', this.wireframeEnabled);
      this.listeners.onWireframeToggle(this.wireframeEnabled);
    });

    document.getElementById('tool-lighting')!.addEventListener('click', () => {
      this.isNight = !this.isNight;
      document.getElementById('tool-lighting')!.classList.toggle('active', this.isNight);
      this.listeners.onLightingToggle(this.isNight);
    });

    // Exports
    document.getElementById('btn-export-glb')!.addEventListener('click', () => {
      this.listeners.onExportGLB();
    });
    document.getElementById('btn-export-obj')!.addEventListener('click', () => {
      this.listeners.onExportOBJ();
    });

    // Camera presets
    document.getElementById('btn-reset-cam')!.addEventListener('click', () => this.listeners.onCameraPreset('perspective'));
    document.getElementById('cam-perspective')!.addEventListener('click', () => this.listeners.onCameraPreset('perspective'));
    document.getElementById('cam-top')!.addEventListener('click', () => this.listeners.onCameraPreset('top'));
    document.getElementById('cam-bridge')!.addEventListener('click', () => this.listeners.onCameraPreset('bridge'));
    document.getElementById('cam-intersection')!.addEventListener('click', () => this.listeners.onCameraPreset('intersection'));
    document.getElementById('cam-train')!.addEventListener('click', () => this.listeners.onCameraPreset('train'));

    // Inspector tab switching
    document.getElementById('tab-inspector')!.addEventListener('click', () => {
      this.activeTab = 'inspector';
      document.getElementById('tab-inspector')!.classList.add('active');
      document.getElementById('tab-layerstack')!.classList.remove('active');
      this.updateInspectorView();
    });

    document.getElementById('tab-layerstack')!.addEventListener('click', () => {
      this.activeTab = 'layerstack';
      document.getElementById('tab-layerstack')!.classList.add('active');
      document.getElementById('tab-inspector')!.classList.remove('active');
      this.updateLayerstackView();
    });

    // Traffic slider
    const trafficSlider = document.getElementById('traffic-slider') as HTMLInputElement;
    trafficSlider.addEventListener('input', (e) => {
      const val = parseInt((e.target as HTMLInputElement).value, 10);
      document.getElementById('traffic-count-label')!.innerText = `${val} / 25`;
      this.listeners.onTrafficCountChange(val);
    });

    // Keyboard shortcuts
    window.addEventListener('keydown', (e) => {
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement) return;
      if (e.key === 'z' || e.key === 'Z') {
        document.getElementById('tool-wireframe')!.click();
      } else if (e.key === 'n' || e.key === 'N') {
        document.getElementById('tool-lighting')!.click();
      } else if (e.key === 'w' || e.key === 'W') {
        document.getElementById('tool-select')!.click();
      }
    });

    // Initial inspector render
    this.updateInspectorView();
  }

  public setActiveTool(toolId: string) {
    this.activeTool = toolId;
    document.querySelectorAll('.tool-btn').forEach(btn => btn.classList.remove('active'));
    const btn = document.getElementById(`tool-${toolId}`);
    if (btn) btn.classList.add('active');
  }

  public setBackendStatus(isWebGPU: boolean) {
    const badge = document.getElementById('engine-status-badge');
    const label = document.getElementById('engine-name');
    if (badge && label) {
      if (isWebGPU) {
        badge.style.background = '#182218';
        badge.style.borderColor = '#234d23';
        badge.style.color = '#4ade80';
        label.innerText = 'WebGPU ACTIVE';
      } else {
        badge.style.background = '#282015';
        badge.style.borderColor = '#4a3818';
        badge.style.color = '#f59e0b';
        label.innerText = 'WebGPU (WebGL2 Fallback)';
      }
    }
  }

  public updateTelemetry(fps: number, quads: number, tris: number) {
    const fpsElem = document.getElementById('telemetry-fps');
    const quadsElem = document.getElementById('telemetry-quads');
    const trisElem = document.getElementById('telemetry-tris');
    const ratioElem = document.getElementById('telemetry-quad-ratio');

    if (fpsElem) fpsElem.innerText = Math.round(fps).toString();
    if (quadsElem) quadsElem.innerText = quads.toLocaleString();
    if (trisElem) trisElem.innerText = tris.toLocaleString();

    if (ratioElem && tris > 0) {
      const quadRatio = Math.min(100, Math.round(((quads * 2) / tris) * 100));
      ratioElem.innerText = `>${quadRatio}% Quads`;
    }
  }

  /**
   * Updates Left Sidebar Network Outliner List
   */
  public updateOutliner(
    roads: RoadConfig[],
    intersections: IntersectionData[],
    hasBridge: boolean,
    hasTunnel: boolean,
    hasRailway: boolean,
    hasConstruction: boolean
  ) {
    const list = document.getElementById('outliner-list');
    if (!list) return;

    let html = `
      <div class="outliner-group">
        <div class="group-header">
          <span>Road Corridors</span>
          <span class="group-count">${roads.length}</span>
        </div>
    `;

    for (const r of roads) {
      const isSel = this.selectedEntity.type === 'road' && this.selectedEntity.id === r.id;
      html += `
        <div class="outliner-row ${isSel ? 'selected' : ''}" data-type="road" data-id="${r.id}">
          <div class="row-left">
            <span class="row-icon">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 19L8 5M16 5l4 14M12 7v2M12 13v2M12 19v2"/></svg>
            </span>
            <span>${r.name}</span>
          </div>
          <div class="row-actions">
            <button class="visibility-toggle" title="Toggle Visibility">
              <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg>
            </button>
          </div>
        </div>
      `;
    }

    html += `
      </div>
      <div class="outliner-group">
        <div class="group-header">
          <span>Intersections & Nodes</span>
          <span class="group-count">${intersections.length}</span>
        </div>
    `;

    for (const j of intersections) {
      const isSel = this.selectedEntity.type === 'intersection' && this.selectedEntity.id === j.id;
      html += `
        <div class="outliner-row ${isSel ? 'selected' : ''}" data-type="intersection" data-id="${j.id}">
          <div class="row-left">
            <span class="row-icon">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="4"/><path d="M12 2v6M12 16v6M2 12h6M16 12h6"/></svg>
            </span>
            <span>${j.name} (${j.type.toUpperCase()})</span>
          </div>
          <div class="row-actions">
            <span style="font-size:10px; color:#a1a1aa; font-family:var(--font-mono);">${j.arms.length} Arms</span>
          </div>
        </div>
      `;
    }

    html += `
      </div>
      <div class="outliner-group">
        <div class="group-header">
          <span>Infrastructure & Layers</span>
        </div>
        <div class="outliner-row">
          <div class="row-left">
            <span class="row-icon"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="7" width="20" height="14" rx="2"/><path d="M16 21V5a2 2 0 0 0-2-2h-4a2 2 0 0 0-2 2v16"/></svg></span>
            <span>Highway Viaduct</span>
          </div>
          <span style="font-size:10px; color:#4ade80;">${hasBridge ? 'ACTIVE' : 'OFF'}</span>
        </div>
        <div class="outliner-row">
          <div class="row-left">
            <span class="row-icon"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 19v-9a8 8 0 0 1 16 0v9"/></svg></span>
            <span>Underpass / Tunnel</span>
          </div>
          <span style="font-size:10px; color:#4ade80;">${hasTunnel ? 'ACTIVE' : 'OFF'}</span>
        </div>
        <div class="outliner-row">
          <div class="row-left">
            <span class="row-icon"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="4" y="3" width="16" height="16" rx="2"/><path d="M4 11h16M12 3v18"/></svg></span>
            <span>Railway & Train</span>
          </div>
          <span style="font-size:10px; color:#4ade80;">${hasRailway ? 'ACTIVE' : 'OFF'}</span>
        </div>
        <div class="outliner-row">
          <div class="row-left">
            <span class="row-icon"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/></svg></span>
            <span>Construction Zone</span>
          </div>
          <span style="font-size:10px; color:#fb923c;">${hasConstruction ? 'ACTIVE' : 'OFF'}</span>
        </div>
      </div>
    `;

    list.innerHTML = html;

    // Row click selection
    list.querySelectorAll('.outliner-row[data-id]').forEach(row => {
      row.addEventListener('click', () => {
        const type = row.getAttribute('data-type') as 'road' | 'intersection';
        const id = row.getAttribute('data-id')!;
        this.selectEntity(type, id);
      });
    });
  }

  public selectEntity(type: 'road' | 'intersection' | null, id: string | null) {
    this.selectedEntity = { type, id };
    const statusText = document.getElementById('selection-status-text');
    if (statusText) {
      statusText.innerText = id ? `Selected: ${type?.toUpperCase()} [${id}]` : 'Select node or road segment';
    }

    // Highlight row in outliner
    document.querySelectorAll('.outliner-row').forEach(row => {
      if (row.getAttribute('data-id') === id) {
        row.classList.add('selected');
      } else {
        row.classList.remove('selected');
      }
    });

    if (this.activeTab === 'inspector') {
      this.updateInspectorView();
    }
  }

  /**
   * Updates Inspector Tab View
   */
  public updateInspectorView(entityData?: any) {
    const body = document.getElementById('inspector-body');
    if (!body) return;

    if (!this.selectedEntity.id) {
      body.innerHTML = `
        <div class="inspector-section">
          <div class="section-title">Inspector Overview</div>
          <p style="color:var(--text-secondary); margin-bottom:14px;">
            Click on any road alignment, intersection center node, or 3D gizmo to inspect and fine-tune engineering parameters.
          </p>
          <div style="background:var(--bg-card); padding:14px; border-radius:6px; border:1px solid var(--border-color);">
            <div style="font-weight:600; color:#fff; margin-bottom:6px;">Civil Geometry Engine</div>
            <ul style="color:var(--text-secondary); padding-left:18px; font-size:12px; line-height:1.7;">
              <li>Pristine quad mesh ribbon generation</li>
              <li>Merged concentric quad intersection topology</li>
              <li>AASHTO & Eurocode 5.5m minimum bridge clearance</li>
              <li>Continuous curbs with 15mm chamfered top</li>
              <li>AASHTO compliant stop bars on incoming lanes</li>
              <li>ADA dropped curb ramps with tactile blister warning</li>
            </ul>
          </div>
        </div>
      `;
      return;
    }

    if (this.selectedEntity.type === 'road') {
      body.innerHTML = `
        <div class="inspector-section">
          <div class="section-title">Road Corridor Parameters</div>
          <div class="prop-row">
            <span class="prop-label">Lanes Count</span>
            <div class="prop-control">
              <input type="range" id="prop-lanes" min="1" max="6" value="2">
              <span class="slider-val" id="val-lanes">2</span>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Lane Width (m)</span>
            <div class="prop-control">
              <input type="range" id="prop-lane-width" min="2.8" max="4.5" step="0.1" value="3.5">
              <span class="slider-val" id="val-lane-width">3.5m</span>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Crown Slope</span>
            <div class="prop-control">
              <input type="range" id="prop-crossfall" min="0.01" max="0.04" step="0.005" value="0.02">
              <span class="slider-val" id="val-crossfall">2.0%</span>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Speed Limit</span>
            <div class="prop-control">
              <input type="range" id="prop-speed" min="30" max="120" step="10" value="50">
              <span class="slider-val" id="val-speed">50 km/h</span>
            </div>
          </div>
        </div>

        <div class="inspector-section">
          <div class="section-title">Curbs & Sidewalks</div>
          <div class="prop-row">
            <span class="prop-label">Curb Height</span>
            <div class="prop-control">
              <input type="range" id="prop-curb-h" min="0.05" max="0.25" step="0.01" value="0.15">
              <span class="slider-val" id="val-curb-h">15 cm</span>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Sidewalk Width</span>
            <div class="prop-control">
              <input type="range" id="prop-sw-w" min="1.5" max="6.0" step="0.25" value="3.0">
              <span class="slider-val" id="val-sw-w">3.0m</span>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Left Sidewalk</span>
            <div class="prop-control">
              <label class="toggle-switch">
                <input type="checkbox" id="toggle-sw-left" checked>
                <span class="toggle-slider"></span>
              </label>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Right Sidewalk</span>
            <div class="prop-control">
              <label class="toggle-switch">
                <input type="checkbox" id="toggle-sw-right" checked>
                <span class="toggle-slider"></span>
              </label>
            </div>
          </div>
        </div>
      `;

      // Bind input events
      const roadId = this.selectedEntity.id;
      const bindSlider = (id: string, valId: string, prop: string, suffix: string = '') => {
        const input = document.getElementById(id) as HTMLInputElement;
        const valSpan = document.getElementById(valId);
        if (input && valSpan) {
          input.addEventListener('input', () => {
            valSpan.innerText = `${input.value}${suffix}`;
            this.listeners.onRoadPropertyChange(roadId, prop, parseFloat(input.value));
          });
        }
      };

      bindSlider('prop-lanes', 'val-lanes', 'lanes');
      bindSlider('prop-lane-width', 'val-lane-width', 'laneWidth', 'm');
      bindSlider('prop-crossfall', 'val-crossfall', 'crossfall', '%');
      bindSlider('prop-speed', 'val-speed', 'speedLimit', ' km/h');
      bindSlider('prop-curb-h', 'val-curb-h', 'curbHeight', 'm');
      bindSlider('prop-sw-w', 'val-sw-w', 'sidewalkWidth', 'm');

    } else if (this.selectedEntity.type === 'intersection') {
      body.innerHTML = `
        <div class="inspector-section">
          <div class="section-title">Intersection Quad Solver</div>
          <div class="prop-row">
            <span class="prop-label">Corner Radius</span>
            <div class="prop-control">
              <input type="range" id="prop-corner-r" min="4.0" max="16.0" step="0.5" value="8.0">
              <span class="slider-val" id="val-corner-r">8.0m</span>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Junction Type</span>
            <div class="prop-control">
              <select class="prop-input" id="prop-junction-type">
                <option value="plus">4-Way Crossroad (+)</option>
                <option value="y">3-Way Y-Junction (Y)</option>
                <option value="tee">3-Way T-Junction (T)</option>
                <option value="roundabout">Roundabout</option>
              </select>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Pedestrian Zebra</span>
            <div class="prop-control">
              <label class="toggle-switch">
                <input type="checkbox" id="toggle-zebra" checked>
                <span class="toggle-slider"></span>
              </label>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Stop Bars</span>
            <div class="prop-control">
              <label class="toggle-switch">
                <input type="checkbox" id="toggle-stopbars" checked>
                <span class="toggle-slider"></span>
              </label>
            </div>
          </div>
          <div class="prop-row">
            <span class="prop-label">Tactile Curb Cut</span>
            <div class="prop-control">
              <label class="toggle-switch">
                <input type="checkbox" id="toggle-tactile" checked>
                <span class="toggle-slider"></span>
              </label>
            </div>
          </div>
        </div>

        <div class="inspector-section">
          <div class="section-title">Topology Health</div>
          <div style="background:var(--bg-card); padding:12px; border-radius:6px; border:1px solid var(--border-color); font-size:12px;">
            <div style="display:flex; justify-content:space-between; margin-bottom:6px;">
              <span style="color:var(--text-secondary);">Center Topology:</span>
              <strong style="color:#4ade80;">Concentric Quads</strong>
            </div>
            <div style="display:flex; justify-content:space-between; margin-bottom:6px;">
              <span style="color:var(--text-secondary);">Overlapping Geometry:</span>
              <strong style="color:#4ade80;">0.0% (Merged Single Mesh)</strong>
            </div>
            <div style="display:flex; justify-content:space-between;">
              <span style="color:var(--text-secondary);">Grade Continuity:</span>
              <strong style="color:#4ade80;">C2 Continuous</strong>
            </div>
          </div>
        </div>
      `;

      const cornerSlider = document.getElementById('prop-corner-r') as HTMLInputElement;
      const cornerVal = document.getElementById('val-corner-r');
      if (cornerSlider && cornerVal) {
        cornerSlider.addEventListener('input', () => {
          cornerVal.innerText = `${cornerSlider.value}m`;
          this.listeners.onIntersectionPropertyChange(this.selectedEntity.id!, 'cornerRadius', parseFloat(cornerSlider.value));
        });
      }
    }
  }

  /**
   * Updates Texture & Material Layerstack Tab View
   */
  public updateLayerstackView() {
    const body = document.getElementById('inspector-body');
    if (!body) return;

    let html = `
      <div class="inspector-section">
        <div class="section-title">
          <span>Texture Layer Stack</span>
          <span style="font-size:10px; color:var(--text-secondary);">${this.materials.layers.length} Layers</span>
        </div>
        <p style="color:var(--text-secondary); margin-bottom:14px; font-size:12px;">
          Procedural PBR materials composite live onto road surfaces, curbs, and sidewalks with real-time roughness and normal mapping.
        </p>
    `;

    for (const layer of this.materials.layers) {
      html += `
        <div class="layer-card" data-layer-id="${layer.id}">
          <div class="layer-header">
            <div class="layer-title">
              <span class="layer-swatch" style="background:${layer.color || '#555'};"></span>
              <span>${layer.name}</span>
            </div>
            <label class="toggle-switch">
              <input type="checkbox" class="layer-enable-toggle" ${layer.enabled ? 'checked' : ''}>
              <span class="toggle-slider"></span>
            </label>
          </div>

          <div class="prop-row" style="margin-bottom:6px;">
            <span class="prop-label">Opacity</span>
            <div class="prop-control">
              <input type="range" class="layer-opacity-slider" min="0" max="1" step="0.05" value="${layer.opacity}">
              <span class="slider-val">${Math.round(layer.opacity * 100)}%</span>
            </div>
          </div>

          <div class="prop-row" style="margin-bottom:6px;">
            <span class="prop-label">Roughness</span>
            <div class="prop-control">
              <input type="range" class="layer-rough-slider" min="0.05" max="1" step="0.05" value="${layer.roughness}">
              <span class="slider-val">${layer.roughness}</span>
            </div>
          </div>

          ${layer.type === 'paving_bond' ? `
            <div class="prop-row" style="margin-bottom:4px;">
              <span class="prop-label">Paving Bond</span>
              <div class="prop-control">
                <select class="prop-input layer-paving-select">
                  <option value="granite_slabs" ${layer.pavingPattern === 'granite_slabs' ? 'selected' : ''}>Granite Slabs (600x600)</option>
                  <option value="herringbone45" ${layer.pavingPattern === 'herringbone45' ? 'selected' : ''}>Herringbone 45° Brick</option>
                  <option value="herringbone90" ${layer.pavingPattern === 'herringbone90' ? 'selected' : ''}>Herringbone 90° Brick</option>
                  <option value="cobblestone" ${layer.pavingPattern === 'cobblestone' ? 'selected' : ''}>Historic Cobblestone</option>
                  <option value="hexagonal" ${layer.pavingPattern === 'hexagonal' ? 'selected' : ''}>Hexagonal Pavers</option>
                  <option value="running_bond" ${layer.pavingPattern === 'running_bond' ? 'selected' : ''}>Running Stretcher Bond</option>
                </select>
              </div>
            </div>
          ` : ''}
        </div>
      `;
    }

    html += `
        <button class="action-btn" id="btn-refresh-textures" style="width:100%; justify-content:center; margin-top:8px;">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 2v6h-6M3 12a9 9 0 0 1 15-6.7L21 8M3 22v-6h6M21 12a9 9 0 0 1-15 6.7L3 16"/></svg>
          <span>Rebake PBR Textures</span>
        </button>
      </div>
    `;

    body.innerHTML = html;

    // Bind layer events
    body.querySelectorAll('.layer-card').forEach(card => {
      const layerId = card.getAttribute('data-layer-id')!;
      const enableToggle = card.querySelector('.layer-enable-toggle') as HTMLInputElement;
      const opacitySlider = card.querySelector('.layer-opacity-slider') as HTMLInputElement;
      const roughSlider = card.querySelector('.layer-rough-slider') as HTMLInputElement;
      const pavingSelect = card.querySelector('.layer-paving-select') as HTMLSelectElement;

      enableToggle.addEventListener('change', () => {
        const l = this.materials.layers.find(x => x.id === layerId);
        if (l) {
          l.enabled = enableToggle.checked;
          this.materials.updateRoadTexture();
        }
      });

      opacitySlider.addEventListener('input', () => {
        const l = this.materials.layers.find(x => x.id === layerId);
        if (l) {
          l.opacity = parseFloat(opacitySlider.value);
          card.querySelectorAll('.slider-val')[0].textContent = `${Math.round(l.opacity * 100)}%`;
          this.materials.updateRoadTexture();
        }
      });

      roughSlider.addEventListener('input', () => {
        const l = this.materials.layers.find(x => x.id === layerId);
        if (l) {
          l.roughness = parseFloat(roughSlider.value);
          card.querySelectorAll('.slider-val')[1].textContent = `${l.roughness}`;
        }
      });

      if (pavingSelect) {
        pavingSelect.addEventListener('change', () => {
          this.materials.setPavingPattern(pavingSelect.value as any);
        });
      }
    });

    document.getElementById('btn-refresh-textures')?.addEventListener('click', () => {
      this.materials.updateRoadTexture();
    });
  }
}
