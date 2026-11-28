import * as THREE from 'three';
import { UnrealObserverCamera } from '../camera/unrealCamera';
import { VehicleRigidBody } from '../physics/vehiclePhysics';
import { VEHICLE_DESIGNS, VehicleDesignId } from '../vehicle/vehicleConfigs';
import { VineEcosystem } from '../vines/vineSystem';
import { BIOME_SPECS, BiomeId, TrailCorridorWorld } from '../world/trailCorridor';

export type ExploreMode = 'UNREAL_CAMERA' | 'PEACEFUL_DRIVE';

export interface HudCallbacks {
  onSelectBiome: (biome: BiomeId) => void;
  onCycleNextBiome: () => void;
  onJumpCourseZ: (targetZ: number, label: string) => void;
  onJumpForwardDelta: (deltaMeters: number) => void;
  onSummonVehicleHere: () => void;
  onToggleExploreMode: () => ExploreMode;
  onSelectVehicle: (id: VehicleDesignId) => void;
  onToggleAudio: () => boolean;
  onChangeFlySpeed: (speedMps: number) => void;
}

export class ExpeditionHud {
  private readonly _tmpDeckVel = new THREE.Vector3();
  private portalFlashEl!: HTMLElement;
  private biomeSubEl!: HTMLElement;
  private coordsEl!: HTMLElement;
  private surfaceEl!: HTMLElement;
  private flySpeedEl!: HTMLElement;
  private portalDistEl!: HTMLElement;

  private modeBtn!: HTMLButtonElement;
  private audioBtn!: HTMLButtonElement;
  private labBtn!: HTMLButtonElement;
  private labDrawer!: HTMLElement;
  private bannerEl!: HTMLElement;
  private controlsStripEl!: HTMLElement;
  private flySpeedSlider!: HTMLInputElement;
  private flySpeedSliderVal!: HTMLElement;

  private levelChipEls = new Map<BiomeId, HTMLButtonElement>();
  private vehicleChipEls = new Map<VehicleDesignId, HTMLButtonElement>();

  private bannerTimeout = 0;
  private isLabOpen = false;

  constructor(
    container: HTMLElement,
    vines: VineEcosystem,
    callbacks: HudCallbacks
  ) {
    const flash = document.createElement('div');
    flash.className = 'portal-flash';
    container.appendChild(flash);
    this.portalFlashEl = flash;

    const levels: Array<{ id: BiomeId; key: string; name: string; sub: string }> = [
      {
        id: 'FOREST',
        key: '1',
        name: 'FOREST',
        sub: 'Giant Trees · Vines · Logs · Mudslides',
      },
      {
        id: 'SWAMP',
        key: '2',
        name: 'SWAMP',
        sub: 'Mangrove Stilt-Root Cathedrals · Bayou',
      },
      {
        id: 'DESERT',
        key: '3',
        name: 'DESERT VILLAGE',
        sub: 'Timber Shacks · Water Towers · Rolling Drums',
      },
      {
        id: 'DEADWOOD',
        key: '4',
        name: 'DEADWOOD',
        sub: 'Dead Forest · Live Erupting Road Roots',
      },
      {
        id: 'SINGULARITY',
        key: '5',
        name: 'SINGULARITY',
        sub: 'Steep Split Cliffs · Mars Storm · Lightning & Monoliths',
      },
      {
        id: 'GIANT_LOG',
        key: '6',
        name: 'GIANT LOG',
        sub: 'Web Network · Growing Mushrooms · Worms · Godrays',
      },
      {
        id: 'GLACIAL_ICE',
        key: '7',
        name: 'GLACIAL ICE',
        sub: 'Slippery Flat Ice · Traveling Cracks · Icicle Cave',
      },
      {
        id: 'CARNIVOROUS_BOG',
        key: '8',
        name: 'CARNIVOROUS BOG',
        sub: 'Sinking Lily Pads · Snapping Flytraps · [LMB] Pitcher Grapple',
      },
      {
        id: 'UNDERGROUND_MARKET',
        key: '9',
        name: 'UNDERGROUND MARKET',
        sub: 'Y-Split Aisles · Curves & Corners · Abandoned Shops & Neon',
      },
      {
        id: 'CANYON_BRIDGES',
        key: '0',
        name: 'CANYON BRIDGES',
        sub: '1→5→2→3 Sky-Islands · Varied Heights · Bridges & Jump Ramps',
      },
      {
        id: 'CLOCKWORK_FOUNDRY',
        key: '-',
        name: 'CLOCKWORK FOUNDRY',
        sub: 'Locking Gear Paths · 1-Car Rotating Gear Walls · Seesaws',
      },
      {
        id: 'CARGO_MEGASHIPS',
        key: '=',
        name: 'CARGO MEGASHIPS',
        sub: 'Wave-Rotated Decks · Sliding Cargo · Magnetic & Breaking Cranes',
      },
      {
        id: 'RUNAWAY_TRAINS',
        key: '\\',
        name: 'RUNAWAY TRAINS',
        sub: 'Parallel Banking Trains · Dynamited Trestles · Rolling Logs',
      },
    ];

    const levelChipsHtml = levels
      .map(
        (lv, idx) => `
      <button class="level-chip ${idx === 0 ? 'selected' : ''}" data-biome="${lv.id}">
        <span class="level-chip-name">[${lv.key}] ${lv.name}</span>
        <span class="level-chip-sub">${lv.sub}</span>
      </button>
    `
      )
      .join('');

    const vehChipsHtml = VEHICLE_DESIGNS.map(
      (d, idx) => `
      <button class="veh-chip ${idx === 0 ? 'selected' : ''}" data-veh="${d.id}">
        <span class="veh-chip-name">${d.name}</span>
        <span class="veh-chip-sub">${d.subtitle}</span>
      </button>
    `
    ).join('');

    const root = document.createElement('div');
    root.className = 'hud-layer';
    root.innerHTML = `
      <div class="hud-top">
        <div class="top-left-stack">
          <div class="expedition-stats">
            <div class="brand-tag">
              <span class="brand-title">FRONTIER · LEVEL OBSERVER</span>
              <span class="brand-sub" id="hud-biome-sub">PRIMEVAL TITAN FOREST · PASSIVE SANDBOX</span>
            </div>
            <div class="stat-cell">
              <span class="stat-label">COURSE POS</span>
              <span class="stat-val" id="hud-coords">0 m</span>
            </div>
            <div class="stat-cell">
              <span class="stat-label">ZONE / FEATURE</span>
              <span class="stat-val" id="hud-surface" style="color:var(--hud-amber)">FOREST CLEARING</span>
            </div>
            <div class="stat-cell">
              <span class="stat-label">FLY SPEED</span>
              <span class="stat-val" id="hud-fly-speed" style="color:var(--hud-cyan)">44 m/s</span>
            </div>
            <div class="stat-cell">
              <span class="stat-label">NEXT PASS</span>
              <span class="stat-val" id="hud-portal-dist">860 m</span>
            </div>
          </div>

          <div class="level-switcher-row">
            ${levelChipsHtml}
            <button class="hud-btn mode-btn" id="btn-next-level">[P] NEXT LEVEL</button>
          </div>

          <div class="landmark-row">
            <button class="jump-btn" data-jumpz="-8" data-label="COURSE START (0m)">START (0m)</button>
            <button class="jump-btn" data-jumpz="-175" data-label="SET-PIECE ZONE (-175m)">ZONE A (-175m)</button>
            <button class="jump-btn" data-jumpz="-380" data-label="MID-COURSE SET-PIECE (-380m)">MID-COURSE (-380m)</button>
            <button class="jump-btn" data-jumpz="-620" data-label="DEEP ZONE (-620m)">DEEP ZONE (-620m)</button>
            <button class="jump-btn" data-jumpz="-835" data-label="STONE PASS GATE (-835m)">PASS GATE (-835m)</button>
            <button class="jump-btn" id="btn-jump-fwd">[J] SKIP +200m AHEAD</button>
          </div>
        </div>

        <div class="top-right-stack">
          <div class="hud-actions">
            <button class="hud-btn mode-btn" id="btn-mode">[V] MODE: UNREAL FREE-CAM</button>
            <button class="hud-btn" id="btn-summon-car">[T] PLACE CAR HERE</button>
            <button class="hud-btn" id="btn-audio">[M] AUDIO: OFF</button>
            <button class="hud-btn" id="btn-lab">[TAB] SETTINGS</button>
          </div>

          <div class="vehicle-selector">
            ${vehChipsHtml}
          </div>
        </div>
      </div>

      <div class="hud-banner-wrap">
        <div class="tactical-callout" id="hud-banner">UNREAL WASD CAMERA READY · DRAG MOUSE TO LOOK</div>
      </div>

      <div class="lab-drawer" id="hud-lab-drawer">
        <div class="lab-title">
          <span>OBSERVER &amp; WORLD SETTINGS</span>
          <span style="color:var(--hud-muted)">PASSIVE</span>
        </div>

        <div class="lab-slider-row">
          <div class="lab-slider-head"><span>Unreal Camera Speed</span><span id="val-fly">44 m/s</span></div>
          <input type="range" id="rng-fly" min="6" max="240" step="2" value="44" />
        </div>
        <div class="lab-slider-row">
          <div class="lab-slider-head"><span>Ambient Vine Growth Speed</span><span id="val-grow">1.00x</span></div>
          <input type="range" id="rng-grow" min="0.2" max="2.5" step="0.05" value="1.0" />
        </div>
      </div>

      <div class="hud-bottom">
        <div class="controls-strip" id="hud-controls-strip">
          <span><kbd>RMB / LMB DRAG</kbd> Look</span>
          <span><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> Fly Course</span>
          <span><kbd>E</kbd>/<kbd>Q</kbd> Up / Down</span>
          <span><kbd>SHIFT</kbd> 3.2x Fast Fly</span>
          <span><kbd>SCROLL</kbd> Cam Speed</span>
          <span><kbd>1</kbd>-<kbd>\\</kbd> Switch Level</span>
          <span><kbd>T</kbd> Place Car Here</span>
          <span><kbd>V</kbd> Drive Course</span>
        </div>
      </div>
    `;

    container.appendChild(root);

    this.biomeSubEl = root.querySelector('#hud-biome-sub')!;
    this.coordsEl = root.querySelector('#hud-coords')!;
    this.surfaceEl = root.querySelector('#hud-surface')!;
    this.flySpeedEl = root.querySelector('#hud-fly-speed')!;
    this.portalDistEl = root.querySelector('#hud-portal-dist')!;

    this.modeBtn = root.querySelector('#btn-mode')!;
    this.audioBtn = root.querySelector('#btn-audio')!;
    this.labBtn = root.querySelector('#btn-lab')!;
    this.labDrawer = root.querySelector('#hud-lab-drawer')!;
    this.bannerEl = root.querySelector('#hud-banner')!;
    this.controlsStripEl = root.querySelector('#hud-controls-strip')!;
    this.flySpeedSlider = root.querySelector('#rng-fly') as HTMLInputElement;
    this.flySpeedSliderVal = root.querySelector('#val-fly')!;

    root.querySelectorAll<HTMLButtonElement>('.level-chip').forEach((btn) => {
      const biome = btn.dataset.biome as BiomeId;
      this.levelChipEls.set(biome, btn);
      btn.addEventListener('click', () => {
        callbacks.onSelectBiome(biome);
        btn.blur();
      });
    });

    root.querySelector('#btn-next-level')!.addEventListener('click', (e) => {
      callbacks.onCycleNextBiome();
      (e.currentTarget as HTMLButtonElement).blur();
    });

    root.querySelectorAll<HTMLButtonElement>('.jump-btn[data-jumpz]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const z = parseFloat(btn.dataset.jumpz || '-8');
        const label = btn.dataset.label || 'LANDMARK';
        callbacks.onJumpCourseZ(z, label);
        btn.blur();
      });
    });

    root.querySelector('#btn-jump-fwd')!.addEventListener('click', (e) => {
      callbacks.onJumpForwardDelta(200);
      (e.currentTarget as HTMLButtonElement).blur();
    });

    root.querySelector('#btn-summon-car')!.addEventListener('click', (e) => {
      callbacks.onSummonVehicleHere();
      (e.currentTarget as HTMLButtonElement).blur();
    });

    this.modeBtn.addEventListener('click', () => {
      const mode = callbacks.onToggleExploreMode();
      this.setExploreModeLabel(mode);
      this.modeBtn.blur();
    });

    this.audioBtn.addEventListener('click', () => {
      const muted = callbacks.onToggleAudio();
      this.setAudioLabel(muted);
      this.audioBtn.blur();
    });

    this.labBtn.addEventListener('click', () => {
      this.toggleLab();
      this.labBtn.blur();
    });

    root.querySelectorAll<HTMLButtonElement>('.veh-chip').forEach((btn) => {
      const id = btn.dataset.veh as VehicleDesignId;
      this.vehicleChipEls.set(id, btn);
      btn.addEventListener('click', () => {
        callbacks.onSelectVehicle(id);
        btn.blur();
      });
    });

    this.flySpeedSlider.addEventListener('input', () => {
      const v = parseFloat(this.flySpeedSlider.value);
      this.flySpeedSliderVal.textContent = `${Math.round(v)} m/s`;
      callbacks.onChangeFlySpeed(v);
    });

    const growInput = root.querySelector('#rng-grow') as HTMLInputElement;
    const growVal = root.querySelector('#val-grow')!;
    growInput.addEventListener('input', () => {
      const v = parseFloat(growInput.value);
      growVal.textContent = `${v.toFixed(2)}x`;
      vines.params.growthSpeedScale = v;
    });
  }

  public syncFlySpeedUI(speedMps: number): void {
    this.flySpeedSlider.value = `${Math.round(speedMps)}`;
    this.flySpeedSliderVal.textContent = `${Math.round(speedMps)} m/s`;
  }

  public flashPortalWarp(): void {
    this.portalFlashEl.classList.add('active');
    window.setTimeout(() => {
      this.portalFlashEl.classList.remove('active');
    }, 60);
  }

  public highlightSelectedBiome(biome: BiomeId): void {
    for (const [b, btn] of this.levelChipEls.entries()) {
      btn.classList.toggle('selected', b === biome);
    }
    const spec = BIOME_SPECS[biome];
    this.biomeSubEl.textContent = `${spec.title} · ${spec.subtitle}`;
  }

  public highlightSelectedVehicle(id: VehicleDesignId): void {
    for (const [vid, btn] of this.vehicleChipEls.entries()) {
      btn.classList.toggle('selected', vid === id);
    }
  }

  public setExploreModeLabel(mode: ExploreMode): void {
    if (mode === 'UNREAL_CAMERA') {
      this.modeBtn.textContent = '[V] MODE: UNREAL FREE-CAM';
      this.controlsStripEl.innerHTML = `
        <span><kbd>RMB / LMB DRAG</kbd> Look</span>
        <span><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> Fly Course</span>
        <span><kbd>E</kbd>/<kbd>Q</kbd> Up / Down</span>
        <span><kbd>SHIFT</kbd> 3.2x Fast Fly</span>
        <span><kbd>SCROLL</kbd> Cam Speed</span>
        <span><kbd>1</kbd>-<kbd>\\</kbd> Switch Level</span>
        <span><kbd>T</kbd> Place Car Here</span>
        <span><kbd>V</kbd> Drive Course</span>
      `;
    } else {
      this.modeBtn.textContent = '[V] MODE: DRIVE COURSE';
      this.controlsStripEl.innerHTML = `
        <span><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> Drive Course</span>
        <span><kbd>LMB HOLD</kbd> Pitcher Grapple</span>
        <span><kbd>SHIFT</kbd> Boost</span>
        <span><kbd>SPACE</kbd> Handbrake</span>
        <span><kbd>R</kbd> Reset Car</span>
        <span><kbd>1</kbd>-<kbd>\\</kbd> Switch Level</span>
        <span><kbd>V</kbd> Back to Unreal Camera</span>
      `;
    }
  }

  public setAudioLabel(muted: boolean): void {
    this.audioBtn.textContent = `[M] AUDIO: ${muted ? 'OFF' : 'ON'}`;
  }

  public toggleLab(): void {
    this.isLabOpen = !this.isLabOpen;
    this.labDrawer.classList.toggle('open', this.isLabOpen);
    this.labBtn.classList.toggle('active', this.isLabOpen);
  }

  public showCallout(text: string): void {
    this.bannerEl.textContent = text;
    this.bannerEl.classList.add('visible');
    window.clearTimeout(this.bannerTimeout);
    this.bannerTimeout = window.setTimeout(() => {
      this.bannerEl.classList.remove('visible');
    }, 2600);
  }

  public update(
    exploreMode: ExploreMode,
    unrealCam: UnrealObserverCamera,
    vehicle: VehicleRigidBody,
    world: TrailCorridorWorld
  ): void {
    const focusPos = exploreMode === 'UNREAL_CAMERA' ? unrealCam.position : vehicle.position;
    const distAlongCourse = Math.max(0, Math.round(-focusPos.z));
    this.coordsEl.textContent = `${distAlongCourse.toLocaleString()} m (Y:${Math.round(focusPos.y)}m)`;

    if (exploreMode === 'UNREAL_CAMERA') {
      this.flySpeedEl.textContent = `${Math.round(unrealCam.baseSpeedMps)} m/s`;
    } else {
      this.flySpeedEl.textContent = `${Math.round(vehicle.speedKmh)} km/h`;
    }

    const portalZ = world.activePortal ? world.activePortal.z : -860;
    const remPortal = Math.max(0, Math.round(focusPos.z - portalZ));
    this.portalDistEl.textContent = `${remPortal} m`;

    const spec = world.getTrailSpec(focusPos.z);
    const sample = world.sampleHeightRaw(focusPos.x, focusPos.z, focusPos.y);
    let zoneName =
      world.currentBiome === 'RUNAWAY_TRAINS'
        ? 'RUNAWAY ARMORED FREIGHT TRAIN'
        : world.currentBiome === 'CARGO_MEGASHIPS'
        ? 'PITCHING CARGO MEGASHIP DECK'
        : world.currentBiome === 'CLOCKWORK_FOUNDRY'
        ? 'BRASS COGWHEEL FOUNDRY'
        : world.currentBiome === 'CANYON_BRIDGES'
        ? 'MISTY CHASM PILLAR'
        : world.currentBiome === 'UNDERGROUND_MARKET'
        ? 'SUBTERRANEAN NEON CONCOURSE'
        : world.currentBiome === 'CARNIVOROUS_BOG'
        ? 'VICTORIA LILY-PAD LAGOON'
        : world.currentBiome === 'GLACIAL_ICE'
        ? 'SLIPPERY ICE FLATS'
        : world.currentBiome === 'GIANT_LOG'
        ? 'HOLLOW LOG ECOSYSTEM'
        : world.currentBiome === 'SINGULARITY'
        ? 'SINGULARITY CLIFF PLATEAU'
        : world.currentBiome === 'SWAMP'
        ? 'MANGROVE ROOT CATHEDRAL'
        : world.currentBiome === 'DESERT'
        ? 'BADLANDS SHACK VILLAGE'
        : world.currentBiome === 'DEADWOOD'
        ? 'ASHEN DEADWOOD ROAD'
        : 'GIANT FOREST';

    if (world.currentBiome === 'RUNAWAY_TRAINS') {
      if (sample.height < -10.0 && focusPos.y > 0.5) {
        zoneName = 'TRAIN-TO-TRAIN SIDEWAYS GORGE LEAP!';
      } else if (sample.height < -10.0) {
        zoneName = 'WHITEWATER TRESTLE GORGE ABYSS';
      } else if (sample.height > 5.4) {
        zoneName = `ARMORED BOXCAR ROOFTOP CATWALK (+${sample.height.toFixed(1)}m)`;
      } else {
        const dx = focusPos.x - spec.centerX;
        const trkLabel =
          dx < -8.0 ? 'WEST TRACK' : dx > 8.0 ? 'EAST TRACK' : 'CENTER MAINLINE';
        zoneName = `RUNAWAY CONSIST · ${trkLabel} (+${sample.height.toFixed(1)}m)`;
      }
    } else if (world.currentBiome === 'CARGO_MEGASHIPS') {
      if (world.cargoMegashipBuilder.activeCapturedCrane) {
        zoneName = 'ELECTROMAGNET CRANE HOISTING! [HOLD SHIFT BOOST TO SNAP FREE]';
      } else if (sample.height < -8.0 && focusPos.y > -4.0) {
        zoneName = 'BOW WAVE-JUMP / OVER-OCEAN AIRBORNE';
      } else if (sample.height < -8.0) {
        zoneName = 'STORM-TOSSED NORTH ATLANTIC SWELL';
      } else if (sample.height > 4.2) {
        zoneName = `40FT CONTAINER ROOFTOP HIGHWAY (+${sample.height.toFixed(1)}m)`;
      } else {
        const uShip = ((-focusPos.z % 112) + 112) % 112;
        zoneName =
          uShip > 82.0
            ? 'GANTRY CRANE CONTAINER DRAWBRIDGE'
            : `WAVE-ROTATING MEGASHIP DECK (${sample.height >= 0 ? '+' : ''}${sample.height.toFixed(1)}m)`;
      }
    } else if (world.currentBiome === 'CLOCKWORK_FOUNDRY') {
      const tmpVel = this._tmpDeckVel;
      const onMoving = world.sampleMovingDeckVelocity(focusPos.x, focusPos.z, tmpVel);
      if (sample.height < -12.0 && focusPos.y > -8.0) {
        zoneName = 'HIGH ARCH CATWALK / FOUNDRY JUMP RAMP';
      } else if (sample.height < -12.0) {
        zoneName = 'MOLTEN SLAG FOUNDRY CRUCIBLE';
      } else if (onMoving && Math.abs(tmpVel.y) > 0.05) {
        zoneName = `STEAM-PISTON ELEVATOR (${sample.height >= 0 ? '+' : ''}${sample.height.toFixed(1)}m)`;
      } else if (onMoving && tmpVel.lengthSq() > 0.02) {
        zoneName = `SPINNING BRASS COGWHEEL (${sample.height >= 0 ? '+' : ''}${sample.height.toFixed(1)}m)`;
      } else if (onMoving) {
        zoneName = 'GENEVA LOCKED GEAR / HUB CAPSTAN';
      } else {
        zoneName = 'ROTATING GEAR-WALL / SEESAW CAUSEWAY';
      }
    } else if (world.currentBiome === 'CANYON_BRIDGES') {
      if (sample.surface === 'ROPE_PLANK') {
        zoneName = 'SWAYING ROPE SUSPENSION BRIDGE';
      } else if (sample.height < -12.0 && focusPos.y > -8.0) {
        zoneName = focusPos.y > 2.5 ? 'HIGH TIMBER TRESTLE / STUNT RAMP' : 'CHASM AIRBORNE / TRESTLE SPAN';
      } else if (sample.height < -12.0) {
        zoneName = 'MISTY CANYON CHASM ABYSS';
      } else {
        zoneName = `SKY-ISLAND MESA (${sample.height >= 0 ? '+' : ''}${sample.height.toFixed(1)}m)`;
      }
    } else if (world.currentBiome === 'UNDERGROUND_MARKET') {
      const uMkt = ((Math.max(0, -focusPos.z) % 112) + 112) % 112;
      if (uMkt >= 58.0 && uMkt <= 67.0 && Math.abs(sample.signedOffset) < 7.5) {
        zoneName = '90° CROSS-CUT SHOP ALLEY';
      } else if (uMkt >= 24.0 && uMkt <= 37.0) {
        zoneName = 'Y-JUNCTION SPLITTING PROW';
      } else if (uMkt > 37.0 && uMkt <= 89.0) {
        zoneName =
          sample.signedOffset < 0
            ? 'LEFT NEON ELECTRONICS ARCADE'
            : 'RIGHT WET-MARKET FOOD ALLEY';
      } else {
        zoneName = 'CURVED SUBTERRANEAN BAZAAR';
      }
    } else if (world.currentBiome === 'CARNIVOROUS_BOG') {
      if (sample.surface === 'LILYPAD_WAX') {
        zoneName = 'BUOYANT VICTORIA LILY PAD';
      } else if (sample.waterDepth > 0.12) {
        zoneName = 'ACID-GREEN DIGESTIVE LAGOON';
      } else {
        zoneName = 'SPHAGNUM FLYTRAP ISLAND';
      }
    } else if (world.currentBiome === 'GLACIAL_ICE') {
      const chunkIdx = Math.floor(-focusPos.z / 112);
      const isCave = chunkIdx >= 1 && chunkIdx % 2 === 1;
      zoneName = isCave ? 'FALLING ICICLE CAVERN' : 'SLIPPERY ICE FLATS';
    } else if (world.currentBiome === 'GIANT_LOG') {
      if (sample.waterDepth > 0.04) {
        zoneName = 'AMBER SAP HOLLOW';
      } else if (Math.abs(sample.signedOffset) > 16.0) {
        zoneName = 'HEARTWOOD WALL BANK';
      } else {
        zoneName = 'COBWEB & WORM CAVERN';
      }
    } else if (world.currentBiome === 'SINGULARITY') {
      if (spec.splitFactor > 0.25) {
        zoneName = 'SPLIT CLIFF CHASM';
      } else if (spec.halfWidth < 11.5) {
        zoneName = 'RAZOR CLIFF SPINE';
      } else if (sample.waterDepth > 0.04) {
        zoneName = 'TURQUOISE BRINE BASIN';
      }
    } else if (world.currentBiome === 'DEADWOOD' && sample.mudiness > 0.35) {
      zoneName = 'ERUPTING ROOT HOLLOW';
    } else if (spec.mudslideFactor > 0.35 && world.currentBiome !== 'DEADWOOD') {
      zoneName =
        world.currentBiome === 'DESERT'
          ? 'CANYON ROCKSLIDE'
          : world.currentBiome === 'SWAMP'
          ? 'TIDAL PEAT SLIDE'
          : 'MUDSLIDE & BOULDERS';
    } else if (spec.splitFactor > 0.3 && world.currentBiome === 'FOREST') {
      zoneName = 'SPLIT ROAD / LOG';
    } else if (sample.waterDepth > 0.04) {
      zoneName =
        world.currentBiome === 'SWAMP'
          ? 'BLACKWATER ROOT BAYOU'
          : world.currentBiome === 'DEADWOOD'
          ? 'RUTTED MUD MIRE'
          : 'WATER LAGOON';
    }
    this.surfaceEl.textContent = zoneName;
  }
}
