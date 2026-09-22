/**
 * FRONTIER GAME ENGINE - CORE STUDIO DASHBOARD
 * Complete interactive logic for:
 * - Celestial Sun Gizmo (SVG hemisphere projection, 2-way sync with Pitch/Yaw)
 * - 8 Unique Bespoke Tactile Cards (Image 1 & Image 2 design system)
 * - Diurnal 24-Hour Simulation Engine
 * - Scene Outliner (Hierarchy, Filter, Search, Multi-Entity Selection)
 * - Inspector Panel (Transform Scrubbers, Light Component, Cascaded Shadows, HLSL generation)
 */

(function () {
  'use strict';

  // ============================================================
  // AUDIO SYNTHESIS (Tactile studio UI clicks via Web Audio API)
  // ============================================================
  let audioCtx = null;
  function getAudioContext() {
    if (!audioCtx) {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      if (AudioContextClass) audioCtx = new AudioContextClass();
    }
    if (audioCtx && audioCtx.state === 'suspended') {
      audioCtx.resume();
    }
    return audioCtx;
  }

  function playTickSound(frequency = 1200, duration = 0.015, volume = 0.04) {
    try {
      const ctx = getAudioContext();
      if (!ctx) return;
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(frequency, ctx.currentTime);
      gain.gain.setValueAtTime(volume, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + duration);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start();
      osc.stop(ctx.currentTime + duration);
    } catch (e) {
      // Audio autoplay policy fallback
    }
  }

  // ============================================================
  // GLOBAL STATE
  // ============================================================
  const state = {
    selectedEntityId: 'sun',
    sun: {
      azimuth: 42.8,       // 0 to 360 deg
      elevation: 58.2,     // -10 to 90 deg
      lux: 120450,         // 0 to 150000 lx
      ev: 0.0,             // -4.0 to +4.0 EV
      kelvin: 5600,        // 1800 to 12000 K
      colorHex: '#FFF4E8',
      colorRGB: { r: 255, g: 244, b: 232 },
      angularDiameter: 0.533, // deg
      shadowBias: 0.005,
      csmSplits: [15, 50, 150, 600], // meters
      rayleigh: 0.033,
      mie: 0.042,
      turbidity: 0.042,
      godraysIntensity: 1.85,
      anisotropy: 0.75,
      raySamples: 64,
      timeOfDay: 13.704,   // 13:42:15 in hours (13 + 42/60 + 15/3600)
      isSimulating: false,
      simSpeed: 1,
      rtCost: 0.38,
      rtShadowsEnabled: true,
      rtDenoiserEnabled: true,
      exposureThresholdLux: 100000
    },
    historyRT: [
      0.45, 0.42, 0.48, 0.52, 0.39, 0.36, 0.44, 0.40,
      0.37, 0.41, 0.49, 0.55, 0.46, 0.42, 0.38, 0.35,
      0.39, 0.41, 0.43, 0.38, 0.36, 0.37, 0.39, 0.38
    ]
  };

  // ============================================================
  // COLOR TEMPERATURE (PLANCKIAN LOCUS BLACKBODY FORMULA)
  // Converts Kelvin to accurate RGB chromaticity
  // ============================================================
  function kelvinToRGB(kelvin) {
    const temp = kelvin / 100;
    let r, g, b;

    // Red
    if (temp <= 66) {
      r = 255;
    } else {
      r = temp - 60;
      r = 329.698727446 * Math.pow(r, -0.1332047592);
      if (r < 0) r = 0;
      if (r > 255) r = 255;
    }

    // Green
    if (temp <= 66) {
      g = temp;
      g = 99.4708025861 * Math.log(g) - 161.1195681661;
      if (g < 0) g = 0;
      if (g > 255) g = 255;
    } else {
      g = temp - 60;
      g = 288.1221695283 * Math.pow(g, -0.0755148492);
      if (g < 0) g = 0;
      if (g > 255) g = 255;
    }

    // Blue
    if (temp >= 66) {
      b = 255;
    } else if (temp <= 19) {
      b = 0;
    } else {
      b = temp - 10;
      b = 138.5177312231 * Math.log(b) - 305.0447927307;
      if (b < 0) b = 0;
      if (b > 255) b = 255;
    }

    return {
      r: Math.round(r),
      g: Math.round(g),
      b: Math.round(b)
    };
  }

  function rgbToHex(r, g, b) {
    return '#' + [r, g, b].map(x => {
      const hex = x.toString(16);
      return hex.length === 1 ? '0' + hex : hex;
    }).join('').toUpperCase();
  }

  function kelvinToCIE(kelvin) {
    // Kang et al. Planckian locus approximation
    const T = kelvin;
    let x, y;
    if (T <= 7000) {
      x = -4.6070e9 / Math.pow(T, 3) + 2.9678e6 / Math.pow(T, 2) + 0.09911e3 / T + 0.244063;
    } else {
      x = -2.0064e9 / Math.pow(T, 3) + 1.9018e6 / Math.pow(T, 2) + 0.24748e3 / T + 0.237040;
    }
    y = -3.000 * Math.pow(x, 2) + 2.870 * x - 0.275;
    return {
      x: Math.max(0.2, Math.min(0.6, x)).toFixed(3),
      y: Math.max(0.2, Math.min(0.6, y)).toFixed(3)
    };
  }

  // ============================================================
  // SUN VECTOR MATH
  // Direction vector [X, Y, Z] from Azimuth and Elevation
  // ============================================================
  function computeSunVector(azimuthDeg, elevationDeg) {
    const azRad = (azimuthDeg * Math.PI) / 180;
    const elRad = (elevationDeg * Math.PI) / 180;

    const x = Math.cos(elRad) * Math.sin(azRad);
    const y = Math.cos(elRad) * Math.cos(azRad);
    const z = Math.sin(elRad);

    return {
      x: Number(x.toFixed(3)),
      y: Number(y.toFixed(3)),
      z: Number(z.toFixed(3))
    };
  }

  // ============================================================
  // DOM ELEMENT REFERENCES
  // ============================================================
  const dom = {
    // Header telemetry
    hdrLuxVal: document.getElementById('hdr-lux-val'),
    hdrKelvinVal: document.getElementById('hdr-kelvin-val'),
    hdrRtVal: document.getElementById('hdr-rt-val'),

    // Hero banner
    heroAvatar: document.getElementById('hero-avatar'),
    heroTitle: document.getElementById('hero-title-text'),
    heroTypeBadge: document.getElementById('hero-type-badge'),
    heroSubtitle: document.getElementById('hero-subtitle-text'),
    btnSnapZenith: document.getElementById('btn-snap-zenith'),
    btnGoldenHour: document.getElementById('btn-golden-hour'),
    btnResetRig: document.getElementById('btn-reset-rig'),

    // Quick status cards
    statSolarState: document.getElementById('stat-solar-state'),
    statSolarSub: document.getElementById('stat-solar-sub'),
    statAngularSize: document.getElementById('stat-angular-size'),
    statGpuCost: document.getElementById('stat-gpu-cost'),

    // Card 1: Gizmo
    valAzMajor: document.getElementById('val-azimuth-major'),
    valAzFraction: document.getElementById('val-azimuth-fraction'),
    valElMajor: document.getElementById('val-elevation-major'),
    valElFraction: document.getElementById('val-elevation-fraction'),
    gizmoStage: document.getElementById('gizmo-stage'),
    gizmoSunDisc: document.getElementById('gizmo-sun-disc'),
    gizmoRayLine: document.getElementById('gizmo-ray-line'),
    vecX: document.getElementById('vec-x'),
    vecY: document.getElementById('vec-y'),
    vecZ: document.getElementById('vec-z'),

    // Card 2: Illuminance & Probes
    valLuxMajor: document.getElementById('val-lux-major'),
    valLuxFraction: document.getElementById('val-lux-fraction'),
    luxSlider: document.getElementById('slider-lux'),
    luxSliderLabel: document.getElementById('lux-slider-label'),
    evSlider: document.getElementById('slider-ev'),
    evSliderLabel: document.getElementById('ev-slider-label'),
    luxThresholdDot: document.getElementById('lux-threshold-dot'),
    probesWindow: document.getElementById('probes-window'),
    probeTooltip: document.getElementById('probe-tooltip'),

    // Card 3: Kelvin
    valKelvinMajor: document.getElementById('val-kelvin-major'),
    kelvinDesc: document.getElementById('kelvin-tag-desc'),
    kelvinSwatch: document.getElementById('kelvin-swatch'),
    kelvinHexLabel: document.getElementById('kelvin-hex-label'),
    cieXyReadout: document.getElementById('cie-xy-readout'),
    sliderKelvin: document.getElementById('slider-kelvin'),

    // Card 4: CSM
    csmBar: document.getElementById('csm-bar'),
    cas0Range: document.getElementById('cas-0-range'),
    cas1Range: document.getElementById('cas-1-range'),
    cas2Range: document.getElementById('cas-2-range'),
    cas3Range: document.getElementById('cas-3-range'),
    sliderAngularDia: document.getElementById('slider-angular-dia'),
    angularDiaVal: document.getElementById('angular-diameter-val'),
    sliderShadowBias: document.getElementById('slider-shadow-bias'),
    shadowBiasVal: document.getElementById('shadow-bias-val'),

    // Card 5: Atmosphere
    valAtmoMajor: document.getElementById('val-atmo-major'),
    valAtmoFraction: document.getElementById('val-atmo-fraction'),
    atmoCanvas: document.getElementById('atmo-canvas'),
    sliderRayleigh: document.getElementById('slider-rayleigh'),
    rayleighVal: document.getElementById('rayleigh-val'),
    sliderMie: document.getElementById('slider-mie'),
    mieVal: document.getElementById('mie-val'),

    // Card 6: Diurnal Timeline
    valTimeMajor: document.getElementById('val-time-major'),
    valTimeFraction: document.getElementById('val-time-fraction'),
    valTimeSeconds: document.getElementById('val-time-seconds'),
    timePhaseTag: document.getElementById('time-phase-tag'),
    diurnalBar: document.getElementById('diurnal-bar'),
    diurnalThumb: document.getElementById('diurnal-thumb'),
    thumbIcon: document.getElementById('thumb-celestial-icon'),
    btnToggleDiurnal: document.getElementById('btn-toggle-diurnal'),
    playIcon: document.getElementById('play-icon'),
    playText: document.getElementById('play-text'),

    // Card 7: Volumetrics
    valGodraysMajor: document.getElementById('val-godrays-major'),
    valGodraysFraction: document.getElementById('val-godrays-fraction'),
    volumetricCanvas: document.getElementById('volumetric-canvas'),
    sliderAnisotropy: document.getElementById('slider-anisotropy'),
    anisotropyVal: document.getElementById('anisotropy-val'),
    sliderRaySamples: document.getElementById('slider-ray-samples'),
    samplesVal: document.getElementById('samples-val'),

    // Card 8: Telemetry
    valRtMajor: document.getElementById('val-rt-major'),
    valRtFraction: document.getElementById('val-rt-fraction'),
    telemetryCanvas: document.getElementById('telemetry-canvas'),
    toggleRtShadows: document.getElementById('toggle-rt-shadows'),
    toggleRtDenoiser: document.getElementById('toggle-rt-denoiser'),

    // Inspector
    inspHeaderIcon: document.getElementById('insp-header-icon'),
    inspHeaderName: document.getElementById('insp-header-name'),
    transPosX: document.getElementById('trans-pos-x'),
    transPosY: document.getElementById('trans-pos-y'),
    transPosZ: document.getElementById('trans-pos-z'),
    transRotPitch: document.getElementById('trans-rot-pitch'),
    transRotYaw: document.getElementById('trans-rot-yaw'),
    transRotRoll: document.getElementById('trans-rot-roll'),
    inspLuxVal: document.getElementById('insp-lux-val'),
    inspLuxField: document.getElementById('insp-lux-field'),
    inspKelvinVal: document.getElementById('insp-kelvin-val'),
    inspColorPicker: document.getElementById('insp-color-picker'),
    inspHexField: document.getElementById('insp-hex-field'),
    inspSrcAngleVal: document.getElementById('insp-src-angle-val'),
    inspSliderAngle: document.getElementById('insp-slider-angle'),
    inspBtnNoon: document.getElementById('insp-btn-noon'),
    inspBtnCopyHlsl: document.getElementById('insp-btn-copy-hlsl'),
    inspBtnExportPreset: document.getElementById('insp-btn-export-preset'),

    // Outliner
    treeRoot: document.getElementById('tree-root'),
    outlinerSearch: document.getElementById('outliner-search'),
    outlinerFilters: document.getElementById('outliner-filters'),

    // Modals & Toasts
    codeModal: document.getElementById('code-modal'),
    modalTitle: document.getElementById('modal-title'),
    modalCode: document.getElementById('modal-code'),
    modalClose: document.getElementById('modal-close'),
    modalCopyBtn: document.getElementById('modal-copy-btn'),
    modalDismissBtn: document.getElementById('modal-dismiss-btn'),
    toastContainer: document.getElementById('toast-container'),

    // Header buttons
    btnExportJson: document.getElementById('btn-export-json'),
    btnHlslCode: document.getElementById('btn-hlsl-code')
  };

  // ============================================================
  // TOAST NOTIFICATIONS
  // ============================================================
  function showToast(message, icon = '✓') {
    const toast = document.createElement('div');
    toast.className = 'toast-msg';
    toast.innerHTML = `<span style="color:var(--accent-solar);">${icon}</span> <span>${message}</span>`;
    dom.toastContainer.appendChild(toast);
    setTimeout(() => {
      toast.style.opacity = '0';
      toast.style.transform = 'translateY(10px)';
      toast.style.transition = 'all 0.25s ease';
      setTimeout(() => toast.remove(), 250);
    }, 2800);
  }

  // ============================================================
  // UPDATE SUN GIZMO & CELESTIAL POSITION
  // ============================================================
  function updateSunGizmoDisplay() {
    const s = state.sun;
    const azRad = (s.azimuth * Math.PI) / 180;
    const elClamped = Math.max(-10, Math.min(90, s.elevation));
    const elNorm = Math.max(0, elClamped / 90);

    // Center pivot (190, 140) in SVG viewBox
    const cx = 190;
    const cy = 140;
    const rx = 145;
    const ry = 30; // base horizon radius

    // 3D projection onto celestial dome:
    // Radius shrinks as elevation climbs towards zenith
    const currentHorizonRadius = rx * Math.cos((elClamped * Math.PI) / 180);
    const sunX = cx + currentHorizonRadius * Math.sin(azRad);
    const sunY = cy - (elNorm * 110) + (Math.cos(azRad) * ry * (1 - elNorm * 0.7));

    // Update Sun Disc translation in SVG
    if (dom.gizmoSunDisc) {
      dom.gizmoSunDisc.setAttribute('transform', `translate(${sunX.toFixed(1)}, ${sunY.toFixed(1)})`);
    }

    // Update Ray line
    if (dom.gizmoRayLine) {
      dom.gizmoRayLine.setAttribute('x1', cx);
      dom.gizmoRayLine.setAttribute('y1', cy);
      dom.gizmoRayLine.setAttribute('x2', sunX.toFixed(1));
      dom.gizmoRayLine.setAttribute('y2', sunY.toFixed(1));
    }

    // Hero Metric format: split major and fraction like Image 2 "113.1 mg/dl"
    const azParts = s.azimuth.toFixed(1).split('.');
    const elParts = s.elevation.toFixed(1).split('.');

    if (dom.valAzMajor) dom.valAzMajor.textContent = azParts[0] + '.';
    if (dom.valAzFraction) dom.valAzFraction.textContent = azParts[1] + '°';
    if (dom.valElMajor) dom.valElMajor.textContent = elParts[0] + '.';
    if (dom.valElFraction) dom.valElFraction.textContent = elParts[1] + '°';

    // Vector readout
    const vec = computeSunVector(s.azimuth, s.elevation);
    if (dom.vecX) dom.vecX.textContent = `X: ${vec.x >= 0 ? '+' : ''}${vec.x.toFixed(3)}`;
    if (dom.vecY) dom.vecY.textContent = `Y: ${vec.y >= 0 ? '+' : ''}${vec.y.toFixed(3)}`;
    if (dom.vecZ) dom.vecZ.textContent = `Z: ${vec.z >= 0 ? '+' : ''}${vec.z.toFixed(3)}`;

    // Update Quick Status Cards
    if (dom.statSolarSub) {
      dom.statSolarSub.textContent = `Elevation: ${s.elevation >= 0 ? '+' : ''}${s.elevation.toFixed(1)}°`;
    }
    if (dom.statSolarState) {
      if (s.elevation > 70) {
        dom.statSolarState.textContent = 'ZENITH';
        dom.statSolarState.style.color = '#ffffff';
      } else if (s.elevation > 15) {
        dom.statSolarState.textContent = 'DAYLIGHT';
        dom.statSolarState.style.color = '#ffffff';
      } else if (s.elevation > 0) {
        dom.statSolarState.textContent = 'GOLDEN';
        dom.statSolarState.style.color = '#fbbf24';
      } else {
        dom.statSolarState.textContent = 'NIGHT';
        dom.statSolarState.style.color = '#818cf8';
      }
    }

    // Synchronize Inspector Transform Rotation (Pitch = -Elevation, Yaw = Azimuth)
    if (dom.transRotPitch && document.activeElement !== dom.transRotPitch) {
      dom.transRotPitch.value = (-s.elevation).toFixed(1);
    }
    if (dom.transRotYaw && document.activeElement !== dom.transRotYaw) {
      dom.transRotYaw.value = (s.azimuth).toFixed(1);
    }
  }

  // ============================================================
  // UPDATE ILLUMINANCE & SENSOR PROBES (CARD 2)
  // ============================================================
  function updateIlluminanceDisplay() {
    const s = state.sun;
    const luxFormatted = Math.round(s.lux).toLocaleString('en-US');
    const parts = luxFormatted.split(',');

    if (parts.length > 1) {
      if (dom.valLuxMajor) dom.valLuxMajor.textContent = parts[0] + ',';
      if (dom.valLuxFraction) dom.valLuxFraction.textContent = parts.slice(1).join(',');
    } else {
      if (dom.valLuxMajor) dom.valLuxMajor.textContent = parts[0];
      if (dom.valLuxFraction) dom.valLuxFraction.textContent = '';
    }

    if (dom.luxSlider) dom.luxSlider.value = Math.round(s.lux);
    if (dom.luxSliderLabel) dom.luxSliderLabel.textContent = `${Math.round(s.lux).toLocaleString()} lx`;
    if (dom.evSliderLabel) dom.evSliderLabel.textContent = `${s.ev >= 0 ? '+' : ''}${s.ev.toFixed(2)} EV`;

    // Header tag
    if (dom.hdrLuxVal) dom.hdrLuxVal.textContent = (s.lux / 1000).toFixed(1) + 'k';

    // Inspector sync
    if (dom.inspLuxVal) dom.inspLuxVal.textContent = `${Math.round(s.lux).toLocaleString()} lx`;
    if (dom.inspLuxField && document.activeElement !== dom.inspLuxField) {
      dom.inspLuxField.value = Math.round(s.lux);
    }

    // Probes readout modulation based on direct sun intensity
    const probeDots = dom.probesWindow ? dom.probesWindow.querySelectorAll('.sensor-dot') : [];
    probeDots.forEach((dot, index) => {
      const factor = [0.98, 0.70, 0.87, 0.12, 1.09, 0.81][index] || 0.8;
      const probeLux = Math.round(s.lux * factor);
      dot.setAttribute('data-lux', `${probeLux.toLocaleString()} lx`);
    });
  }

  // ============================================================
  // UPDATE COLOR TEMPERATURE & BLACKBODY SPECTRUM (CARD 3)
  // ============================================================
  function updateKelvinDisplay() {
    const s = state.sun;
    const rgb = kelvinToRGB(s.kelvin);
    const hex = rgbToHex(rgb.r, rgb.g, rgb.b);
    const cie = kelvinToCIE(s.kelvin);

    s.colorHex = hex;
    s.colorRGB = rgb;

    if (dom.valKelvinMajor) dom.valKelvinMajor.textContent = Math.round(s.kelvin).toLocaleString();
    if (dom.sliderKelvin) dom.sliderKelvin.value = s.kelvin;

    // Header tag
    if (dom.hdrKelvinVal) dom.hdrKelvinVal.textContent = Math.round(s.kelvin).toLocaleString();

    // Swatch & Bloom glow
    if (dom.kelvinSwatch) {
      dom.kelvinSwatch.style.backgroundColor = hex;
      dom.kelvinSwatch.style.boxShadow = `0 0 32px ${hex}88`;
    }
    if (dom.kelvinHexLabel) {
      dom.kelvinHexLabel.textContent = hex;
      // Invert text color if swatch is very dark
      const lum = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
      dom.kelvinHexLabel.style.color = lum > 140 ? '#0b0d11' : '#ffffff';
    }

    if (dom.cieXyReadout) {
      dom.cieXyReadout.textContent = `x: ${cie.x}  |  y: ${cie.y}`;
    }

    // Description tag
    if (dom.kelvinDesc) {
      if (s.kelvin < 2500) dom.kelvinDesc.textContent = 'Candlelight';
      else if (s.kelvin < 3500) dom.kelvinDesc.textContent = 'Sunset Warm';
      else if (s.kelvin < 5000) dom.kelvinDesc.textContent = 'Golden Sun';
      else if (s.kelvin < 6200) dom.kelvinDesc.textContent = 'Daylight (D55)';
      else if (s.kelvin < 7500) dom.kelvinDesc.textContent = 'Overcast (D65)';
      else dom.kelvinDesc.textContent = 'Deep Blue Sky';
    }

    // Inspector sync
    if (dom.inspKelvinVal) dom.inspKelvinVal.textContent = `${Math.round(s.kelvin).toLocaleString()} K`;
    if (dom.inspColorPicker) dom.inspColorPicker.value = hex;
    if (dom.inspHexField && document.activeElement !== dom.inspHexField) dom.inspHexField.value = hex;
  }

  // ============================================================
  // UPDATE CASCADED SHADOW MAPS (CARD 4)
  // ============================================================
  function updateCsmDisplay() {
    const s = state.sun;
    const splits = s.csmSplits;

    if (dom.cas0Range) dom.cas0Range.textContent = `0 - ${splits[0]}m`;
    if (dom.cas1Range) dom.cas1Range.textContent = `${splits[0]} - ${splits[1]}m`;
    if (dom.cas2Range) dom.cas2Range.textContent = `${splits[1]} - ${splits[2]}m`;
    if (dom.cas3Range) dom.cas3Range.textContent = `${splits[2]} - ${splits[3]}m`;

    if (dom.sliderAngularDia) dom.sliderAngularDia.value = s.angularDiameter;
    if (dom.angularDiaVal) dom.angularDiaVal.textContent = `${s.angularDiameter.toFixed(3)}°`;
    if (dom.statAngularSize) dom.statAngularSize.textContent = `${s.angularDiameter.toFixed(3)}°`;

    if (dom.sliderShadowBias) dom.sliderShadowBias.value = s.shadowBias;
    if (dom.shadowBiasVal) dom.shadowBiasVal.textContent = `${s.shadowBias.toFixed(3)} / ${(s.shadowBias * 100).toFixed(1)}`;
  }

  // ============================================================
  // UPDATE ATMOSPHERIC SCATTERING (CARD 5)
  // Canvas particle drift and altitude gradient
  // ============================================================
  let atmoParticles = [];
  function initAtmoParticles(count = 35) {
    atmoParticles = [];
    for (let i = 0; i < count; i++) {
      atmoParticles.push({
        x: Math.random(),
        y: Math.random(),
        radius: Math.random() * 2.0 + 0.8,
        speedX: (Math.random() - 0.5) * 0.0008,
        speedY: (Math.random() - 0.5) * 0.0005,
        alpha: Math.random() * 0.6 + 0.2
      });
    }
  }

  function renderAtmoCanvas() {
    if (!dom.atmoCanvas) return;
    const ctx = dom.atmoCanvas.getContext('2d');
    const w = dom.atmoCanvas.width = dom.atmoCanvas.clientWidth || 320;
    const h = dom.atmoCanvas.height = dom.atmoCanvas.clientHeight || 120;

    // Background gradient: altitude slices
    const grad = ctx.createLinearGradient(0, 0, 0, h);
    grad.addColorStop(0, '#040508');
    grad.addColorStop(0.35, '#0d152a');
    grad.addColorStop(0.7, '#1e293b');
    grad.addColorStop(1, '#0f172a');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, w, h);

    // Sun altitude beam line
    const elNorm = Math.max(0, state.sun.elevation / 90);
    const beamY = h * (1 - elNorm * 0.85);

    const beamGrad = ctx.createRadialGradient(w * 0.65, beamY, 5, w * 0.65, beamY, w * 0.7);
    beamGrad.addColorStop(0, `${state.sun.colorHex}66`);
    beamGrad.addColorStop(0.5, 'rgba(99, 102, 241, 0.12)');
    beamGrad.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = beamGrad;
    ctx.fillRect(0, 0, w, h);

    // Draw and animate particles
    for (let p of atmoParticles) {
      p.x += p.speedX;
      p.y += p.speedY;
      if (p.x < 0) p.x = 1;
      if (p.x > 1) p.x = 0;
      if (p.y < 0) p.y = 1;
      if (p.y > 1) p.y = 0;

      ctx.beginPath();
      ctx.arc(p.x * w, p.y * h, p.radius, 0, Math.PI * 2);
      ctx.fillStyle = `rgba(255, 255, 255, ${p.alpha})`;
      ctx.fill();
    }
  }

  // ============================================================
  // UPDATE DIURNAL TIMELINE (CARD 6)
  // Scrubber, solar time formatting, time-of-day calculations
  // ============================================================
  function updateTimelineDisplay() {
    const s = state.sun;
    const totalHours = s.timeOfDay;
    const hours = Math.floor(totalHours);
    const totalMinutes = (totalHours - hours) * 60;
    const minutes = Math.floor(totalMinutes);
    const seconds = Math.floor((totalMinutes - minutes) * 60);

    const pad = (n) => String(n).padStart(2, '0');

    if (dom.valTimeMajor) dom.valTimeMajor.textContent = `${pad(hours)}:`;
    if (dom.valTimeFraction) dom.valTimeFraction.textContent = `${pad(minutes)}`;
    if (dom.valTimeSeconds) dom.valTimeSeconds.textContent = `:${pad(seconds)}`;

    // Scrubber thumb position: 0h -> 0%, 24h -> 100%
    const pct = Math.max(0, Math.min(100, (totalHours / 24) * 100));
    if (dom.diurnalThumb) {
      dom.diurnalThumb.style.left = `${pct.toFixed(2)}%`;
    }

    // Celestial Icon & Phase Tag
    const isDay = totalHours >= 5.5 && totalHours <= 18.5;
    if (dom.thumbIcon) dom.thumbIcon.textContent = isDay ? '☀️' : '🌙';

    if (dom.timePhaseTag) {
      if (totalHours >= 5.0 && totalHours < 8.0) {
        dom.timePhaseTag.textContent = 'Golden Dawn';
        dom.timePhaseTag.style.color = '#fbbf24';
      } else if (totalHours >= 8.0 && totalHours < 12.0) {
        dom.timePhaseTag.textContent = 'Morning Sun';
        dom.timePhaseTag.style.color = '#38bdf8';
      } else if (totalHours >= 12.0 && totalHours < 14.5) {
        dom.timePhaseTag.textContent = 'Solar Noon';
        dom.timePhaseTag.style.color = '#ffffff';
      } else if (totalHours >= 14.5 && totalHours < 18.0) {
        dom.timePhaseTag.textContent = 'Afternoon Phase';
        dom.timePhaseTag.style.color = '#60a5fa';
      } else if (totalHours >= 18.0 && totalHours < 20.0) {
        dom.timePhaseTag.textContent = 'Dusk / Twilight';
        dom.timePhaseTag.style.color = '#f43f5e';
      } else {
        dom.timePhaseTag.textContent = 'Midnight Phase';
        dom.timePhaseTag.style.color = '#818cf8';
      }
    }
  }

  // Calculate astronomical sun azimuth and elevation from time of day (0.0 to 24.0)
  function applyTimeOfDayToSun(tHours) {
    const s = state.sun;
    s.timeOfDay = ((tHours % 24) + 24) % 24;

    // Solar noon is at 12:00.
    // Hour angle H = (t - 12) * 15 deg
    const hourAngleDeg = (s.timeOfDay - 12) * 15;
    const hRad = (hourAngleDeg * Math.PI) / 180;

    // Approximate latitude 35 deg North
    const latRad = (35 * Math.PI) / 180;
    // Declination 15 deg (summer daylight)
    const decRad = (15 * Math.PI) / 180;

    // Solar elevation
    const sinEl = Math.sin(latRad) * Math.sin(decRad) + Math.cos(latRad) * Math.cos(decRad) * Math.cos(hRad);
    const elevation = (Math.asin(sinEl) * 180) / Math.PI;

    // Solar azimuth
    const cosAz = (Math.sin(decRad) - Math.sin(latRad) * sinEl) / (Math.cos(latRad) * Math.cos(Math.asin(sinEl)));
    let azimuth = (Math.acos(Math.max(-1, Math.min(1, cosAz))) * 180) / Math.PI;
    if (hourAngleDeg > 0) azimuth = 360 - azimuth;

    s.elevation = Number(elevation.toFixed(1));
    s.azimuth = Number(azimuth.toFixed(1));

    // Calculate realistic Lux and Kelvin based on elevation angle:
    if (s.elevation > 0) {
      // Direct sunlight attenuates with air mass
      const sinElevClamped = Math.max(0.02, Math.sin((s.elevation * Math.PI) / 180));
      s.lux = Math.round(135000 * Math.pow(sinElevClamped, 0.7));

      // Color temperature warms dramatically near horizon
      if (s.elevation < 10) {
        s.kelvin = Math.round(2000 + (s.elevation / 10) * 1800);
      } else if (s.elevation < 30) {
        s.kelvin = Math.round(3800 + ((s.elevation - 10) / 20) * 1400);
      } else {
        s.kelvin = Math.round(5200 + ((s.elevation - 30) / 60) * 700);
      }
    } else {
      // Night skylight
      s.lux = 50; // faint moon / starlight
      s.kelvin = 9500; // deep indigo night sky
    }

    updateSunGizmoDisplay();
    updateIlluminanceDisplay();
    updateKelvinDisplay();
    updateTimelineDisplay();
  }

  // ============================================================
  // UPDATE VOLUMETRIC CREPUSCULAR GOD RAYS (CARD 7)
  // Dynamic Canvas rendering light shafts responding to sun vector
  // ============================================================
  function renderVolumetricCanvas() {
    if (!dom.volumetricCanvas) return;
    const ctx = dom.volumetricCanvas.getContext('2d');
    const w = dom.volumetricCanvas.width = dom.volumetricCanvas.clientWidth || 320;
    const h = dom.volumetricCanvas.height = dom.volumetricCanvas.clientHeight || 120;

    ctx.fillStyle = '#0a0c12';
    ctx.fillRect(0, 0, w, h);

    const s = state.sun;
    const azRad = (s.azimuth * Math.PI) / 180;
    const elNorm = Math.max(0, s.elevation / 90);

    // Sun source position
    const srcX = w * (0.5 + Math.sin(azRad) * 0.4);
    const srcY = h * (0.15 + (1 - elNorm) * 0.7);

    // Draw radiating god-ray shafts
    const rayCount = Math.min(24, s.raySamples / 3);
    ctx.save();
    ctx.globalCompositeOperation = 'screen';

    for (let i = 0; i < rayCount; i++) {
      const angle = (i / rayCount) * Math.PI * 2 + (Date.now() * 0.0003);
      const rayLen = w * (0.6 + 0.3 * Math.sin(i * 1.7));
      const endX = srcX + Math.cos(angle) * rayLen;
      const endY = srcY + Math.sin(angle) * rayLen;

      const rayGrad = ctx.createLinearGradient(srcX, srcY, endX, endY);
      rayGrad.addColorStop(0, `${s.colorHex}55`);
      rayGrad.addColorStop(0.3, `${s.colorHex}22`);
      rayGrad.addColorStop(1, 'rgba(0,0,0,0)');

      ctx.beginPath();
      ctx.moveTo(srcX, srcY);
      ctx.lineTo(endX + 8, endY);
      ctx.lineTo(endX - 8, endY);
      ctx.closePath();
      ctx.fillStyle = rayGrad;
      ctx.fill();
    }

    // Center bright core
    const coreGrad = ctx.createRadialGradient(srcX, srcY, 2, srcX, srcY, 32);
    coreGrad.addColorStop(0, '#ffffff');
    coreGrad.addColorStop(0.4, s.colorHex);
    coreGrad.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = coreGrad;
    ctx.beginPath();
    ctx.arc(srcX, srcY, 32, 0, Math.PI * 2);
    ctx.fill();

    ctx.restore();
  }

  // ============================================================
  // UPDATE RAYTRACING TELEMETRY & FRAME BUDGET (CARD 8)
  // Stepped area histogram & polyline chart directly styled after Image 1
  // ============================================================
  function renderTelemetryCanvas() {
    if (!dom.telemetryCanvas) return;
    const ctx = dom.telemetryCanvas.getContext('2d');
    const w = dom.telemetryCanvas.width = dom.telemetryCanvas.clientWidth || 320;
    const h = dom.telemetryCanvas.height = dom.telemetryCanvas.clientHeight || 120;

    ctx.clearRect(0, 0, w, h);

    const history = state.historyRT;
    const targetBudget = 0.60; // ms
    const maxVal = 0.85; // ms chart ceiling

    const padLeft = 14;
    const padRight = 14;
    const padTop = 18;
    const padBottom = 22;
    const chartW = w - padLeft - padRight;
    const chartH = h - padTop - padBottom;

    // Target Dotted Line at 0.60 ms (Matching Image 1 target threshold)
    const targetY = padTop + chartH * (1 - (targetBudget / maxVal));
    ctx.save();
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.25)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(padLeft, targetY);
    ctx.lineTo(padLeft + chartW, targetY);
    ctx.stroke();
    ctx.restore();

    // Stepped Histogram Area Bars (Matching Image 1 dark stepped blocks)
    const stepW = chartW / history.length;
    for (let i = 0; i < history.length; i++) {
      const val = history[i];
      const barH = chartH * (val / maxVal);
      const x = padLeft + i * stepW;
      const y = padTop + chartH - barH;

      ctx.fillStyle = (i % 3 === 0) ? 'rgba(255, 255, 255, 0.07)' : 'rgba(255, 255, 255, 0.03)';
      ctx.fillRect(x, y, stepW - 1, barH);
    }

    // Continuous Telemetry Polyline
    ctx.beginPath();
    for (let i = 0; i < history.length; i++) {
      const val = history[i];
      const x = padLeft + (i + 0.5) * stepW;
      const y = padTop + chartH * (1 - (val / maxVal));
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.75)';
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // Data Nodes (White dots with halo; active one in amber)
    for (let i = 0; i < history.length; i++) {
      if (i % 2 !== 0 && i !== history.length - 1) continue;
      const val = history[i];
      const x = padLeft + (i + 0.5) * stepW;
      const y = padTop + chartH * (1 - (val / maxVal));

      const isCurrent = (i === history.length - 1);

      ctx.beginPath();
      ctx.arc(x, y, isCurrent ? 4 : 2, 0, Math.PI * 2);
      ctx.fillStyle = isCurrent ? '#f59e0b' : '#ffffff';
      ctx.fill();

      if (isCurrent) {
        ctx.beginPath();
        ctx.arc(x, y, 7, 0, Math.PI * 2);
        ctx.strokeStyle = 'rgba(245, 158, 11, 0.4)';
        ctx.lineWidth = 1.5;
        ctx.stroke();
      }
    }

    // X-Axis Frame Time Labels (F-60, F-30, Live)
    ctx.fillStyle = 'rgba(255, 255, 255, 0.35)';
    ctx.font = '9px "JetBrains Mono", monospace';
    ctx.fillText('F-60', padLeft, h - 6);
    ctx.fillText('F-30', padLeft + chartW * 0.45, h - 6);
    ctx.fillText('LIVE', padLeft + chartW - 24, h - 6);
  }

  // ============================================================
  // GIZMO INTERACTION (MOUSE DRAG / TRACKING ON SVG STAGE)
  // ============================================================
  function setupGizmoInteraction() {
    let isDragging = false;

    function handlePointer(e) {
      if (!dom.gizmoStage) return;
      const rect = dom.gizmoStage.getBoundingClientRect();
      const pointerX = e.clientX - rect.left;
      const pointerY = e.clientY - rect.top;

      // Map pointer to SVG viewBox coordinates (0..380, 0..180)
      const svgX = (pointerX / rect.width) * 380;
      const svgY = (pointerY / rect.height) * 180;

      const cx = 190;
      const cy = 140;

      const dx = svgX - cx;
      const dy = cy - svgY;

      // Azimuth from dx, dy
      let az = (Math.atan2(dx, dy * 2.5) * 180) / Math.PI;
      if (az < 0) az += 360;

      // Elevation from distance to center
      const dist = Math.sqrt(dx * dx + (dy * dy * 1.5));
      let el = Math.max(-5, Math.min(90, 90 - (dist / 145) * 90));

      state.sun.azimuth = Number(az.toFixed(1));
      state.sun.elevation = Number(el.toFixed(1));

      // Calculate approximate time of day from this new solar position
      // At az=180, time=12:00. At az=90, time=06:00. At az=270, time=18:00.
      let estHours = 12 + ((state.sun.azimuth - 180) / 15);
      if (estHours < 0) estHours += 24;
      if (estHours > 24) estHours -= 24;
      state.sun.timeOfDay = Number(estHours.toFixed(3));

      // Sync lux and kelvin smoothly
      if (state.sun.elevation > 0) {
        const sinEl = Math.max(0.05, Math.sin((state.sun.elevation * Math.PI) / 180));
        state.sun.lux = Math.round(130000 * Math.pow(sinEl, 0.7));
        state.sun.kelvin = Math.round(2800 + sinEl * 3200);
      } else {
        state.sun.lux = 50;
        state.sun.kelvin = 9500;
      }

      updateSunGizmoDisplay();
      updateIlluminanceDisplay();
      updateKelvinDisplay();
      updateTimelineDisplay();
      renderVolumetricCanvas();
      playTickSound(800 + state.sun.elevation * 10, 0.01, 0.02);
    }

    if (dom.gizmoStage) {
      dom.gizmoStage.addEventListener('mousedown', (e) => {
        isDragging = true;
        handlePointer(e);
      });

      window.addEventListener('mousemove', (e) => {
        if (!isDragging) return;
        handlePointer(e);
      });

      window.addEventListener('mouseup', () => {
        if (isDragging) isDragging = false;
      });

      // Touch support
      dom.gizmoStage.addEventListener('touchstart', (e) => {
        isDragging = true;
        if (e.touches.length > 0) handlePointer(e.touches[0]);
      }, { passive: true });

      window.addEventListener('touchmove', (e) => {
        if (!isDragging || e.touches.length === 0) return;
        handlePointer(e.touches[0]);
      }, { passive: true });

      window.addEventListener('touchend', () => {
        isDragging = false;
      });
    }

    // Quick cardinal preset buttons
    const presetBtns = dom.gizmoStage?.parentElement?.parentElement?.querySelectorAll('.mini-pill-btn');
    presetBtns?.forEach(btn => {
      btn.addEventListener('click', () => {
        presetBtns.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        playTickSound(1400, 0.02, 0.06);

        const preset = btn.getAttribute('data-preset');
        if (preset === 'noon') {
          state.sun.azimuth = 180.0;
          state.sun.elevation = 85.0;
          state.sun.timeOfDay = 12.0;
        } else if (preset === 'golden') {
          state.sun.azimuth = 250.0;
          state.sun.elevation = 14.0;
          state.sun.timeOfDay = 17.2;
        } else if (preset === 'sunrise') {
          state.sun.azimuth = 78.0;
          state.sun.elevation = 2.0;
          state.sun.timeOfDay = 6.1;
        } else if (preset === 'dusk') {
          state.sun.azimuth = 285.0;
          state.sun.elevation = -2.0;
          state.sun.timeOfDay = 19.4;
        }

        applyTimeOfDayToSun(state.sun.timeOfDay);
        showToast(`Sun preset: ${btn.textContent}`);
      });
    });
  }

  // ============================================================
  // CARD 2: PROBE DOTS & THRESHOLD DRAGGING
  // ============================================================
  function setupProbesInteraction() {
    let isDraggingThreshold = false;

    if (dom.luxThresholdDot && dom.probesWindow) {
      dom.luxThresholdDot.addEventListener('mousedown', (e) => {
        isDraggingThreshold = true;
        e.stopPropagation();
      });

      window.addEventListener('mousemove', (e) => {
        if (!isDraggingThreshold || !dom.probesWindow) return;
        const rect = dom.probesWindow.getBoundingClientRect();
        let pct = ((e.clientX - rect.left) / rect.width) * 100;
        pct = Math.max(10, Math.min(90, pct));
        dom.luxThresholdDot.style.left = `${pct.toFixed(1)}%`;

        // Modulate exposure threshold
        state.sun.exposureThresholdLux = Math.round(20000 + (pct / 100) * 120000);
        if (dom.probeTooltip) {
          dom.probeTooltip.textContent = `Auto-Exposure Target Threshold: ${state.sun.exposureThresholdLux.toLocaleString()} lx`;
        }
        playTickSound(1100, 0.01, 0.015);
      });

      window.addEventListener('mouseup', () => {
        if (isDraggingThreshold) isDraggingThreshold = false;
      });
    }

    // Hoverable Probe Dots
    const probeDots = dom.probesWindow ? dom.probesWindow.querySelectorAll('.sensor-dot') : [];
    probeDots.forEach(dot => {
      dot.addEventListener('mouseenter', () => {
        const name = dot.getAttribute('data-name');
        const lux = dot.getAttribute('data-lux');
        if (dom.probeTooltip) {
          dom.probeTooltip.innerHTML = `<strong style="color:#ffffff;">${name}:</strong> ${lux} · Calibrated Ground Sensor`;
        }
        playTickSound(1600, 0.015, 0.03);
      });

      dot.addEventListener('click', () => {
        const name = dot.getAttribute('data-name');
        showToast(`Selected Probe: ${name}`, '◎');
      });
    });

    // Direct Lux Slider
    if (dom.luxSlider) {
      dom.luxSlider.addEventListener('input', (e) => {
        state.sun.lux = Number(e.target.value);
        updateIlluminanceDisplay();
        playTickSound(900 + state.sun.lux * 0.005, 0.01, 0.015);
      });
    }

    // EV Slider
    if (dom.evSlider) {
      dom.evSlider.addEventListener('input', (e) => {
        state.sun.ev = Number(e.target.value);
        updateIlluminanceDisplay();
        playTickSound(1200, 0.01, 0.02);
      });
    }
  }

  // ============================================================
  // CARD 3: KELVIN SLIDER & PRESETS
  // ============================================================
  function setupKelvinInteraction() {
    if (dom.sliderKelvin) {
      dom.sliderKelvin.addEventListener('input', (e) => {
        state.sun.kelvin = Number(e.target.value);
        updateKelvinDisplay();
        playTickSound(s => 700 + state.sun.kelvin * 0.1, 0.01, 0.02);
      });
    }

    const kelvinBtns = dom.sliderKelvin?.parentElement?.querySelectorAll('.mini-pill-btn');
    kelvinBtns?.forEach(btn => {
      btn.addEventListener('click', () => {
        kelvinBtns.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        const k = Number(btn.getAttribute('data-kelvin'));
        state.sun.kelvin = k;
        updateKelvinDisplay();
        playTickSound(1500, 0.02, 0.05);
        showToast(`Color temperature: ${k.toLocaleString()}K`);
      });
    });
  }

  // ============================================================
  // CARD 4: CSM PARTITION SPLITS & ANGULAR SIZE
  // ============================================================
  function setupCsmInteraction() {
    if (dom.sliderAngularDia) {
      dom.sliderAngularDia.addEventListener('input', (e) => {
        state.sun.angularDiameter = Number(e.target.value);
        updateCsmDisplay();
        playTickSound(1000 + state.sun.angularDiameter * 200, 0.01, 0.02);
      });
    }

    if (dom.sliderShadowBias) {
      dom.sliderShadowBias.addEventListener('input', (e) => {
        state.sun.shadowBias = Number(e.target.value);
        updateCsmDisplay();
      });
    }

    // Split handles drag inside CSM bar
    const handles = dom.csmBar?.querySelectorAll('.cascade-split-handle');
    handles?.forEach(handle => {
      let isDragging = false;
      const handleIdx = Number(handle.getAttribute('data-handle'));

      handle.addEventListener('mousedown', (e) => {
        isDragging = true;
        e.stopPropagation();
      });

      window.addEventListener('mousemove', (e) => {
        if (!isDragging || !dom.csmBar) return;
        const rect = dom.csmBar.getBoundingClientRect();
        let pct = (e.clientX - rect.left) / rect.width;
        pct = Math.max(0.05, Math.min(0.95, pct));

        // Adjust split distance in meters
        if (handleIdx === 0) {
          state.sun.csmSplits[0] = Math.round(pct * 60);
        } else if (handleIdx === 1) {
          state.sun.csmSplits[1] = Math.round(pct * 120);
        } else if (handleIdx === 2) {
          state.sun.csmSplits[2] = Math.round(pct * 300);
        }
        updateCsmDisplay();
        playTickSound(1300, 0.01, 0.015);
      });

      window.addEventListener('mouseup', () => {
        if (isDragging) isDragging = false;
      });
    });
  }

  // ============================================================
  // CARD 5: ATMOSPHERE CONTROLS
  // ============================================================
  function setupAtmoInteraction() {
    if (dom.sliderRayleigh) {
      dom.sliderRayleigh.addEventListener('input', (e) => {
        state.sun.rayleigh = Number(e.target.value);
        if (dom.rayleighVal) dom.rayleighVal.textContent = state.sun.rayleigh.toFixed(3);
        playTickSound(1200, 0.01, 0.02);
      });
    }

    if (dom.sliderMie) {
      dom.sliderMie.addEventListener('input', (e) => {
        state.sun.mie = Number(e.target.value);
        state.sun.turbidity = state.sun.mie;
        if (dom.mieVal) dom.mieVal.textContent = state.sun.mie.toFixed(3);

        const parts = state.sun.turbidity.toFixed(3).split('.');
        if (dom.valAtmoMajor) dom.valAtmoMajor.textContent = parts[0] + '.';
        if (dom.valAtmoFraction) dom.valAtmoFraction.textContent = parts[1];
        playTickSound(1300, 0.01, 0.02);
      });
    }
  }

  // ============================================================
  // CARD 6: DIURNAL TIMELINE & REAL-TIME SIMULATION
  // ============================================================
  function setupTimelineInteraction() {
    let isDraggingTimeline = false;

    function handleBarClick(e) {
      if (!dom.diurnalBar) return;
      const rect = dom.diurnalBar.getBoundingClientRect();
      let pct = (e.clientX - rect.left) / rect.width;
      pct = Math.max(0, Math.min(1, pct));
      const hours = pct * 24;
      applyTimeOfDayToSun(hours);
      playTickSound(1100, 0.01, 0.02);
    }

    if (dom.diurnalBar) {
      dom.diurnalBar.addEventListener('mousedown', (e) => {
        isDraggingTimeline = true;
        handleBarClick(e);
      });

      window.addEventListener('mousemove', (e) => {
        if (!isDraggingTimeline) return;
        handleBarClick(e);
      });

      window.addEventListener('mouseup', () => {
        if (isDraggingTimeline) isDraggingTimeline = false;
      });
    }

    // Play / Pause Simulation Button
    if (dom.btnToggleDiurnal) {
      dom.btnToggleDiurnal.addEventListener('click', () => {
        state.sun.isSimulating = !state.sun.isSimulating;
        playTickSound(1500, 0.02, 0.05);

        if (state.sun.isSimulating) {
          dom.playIcon.textContent = '⏸';
          dom.playText.textContent = 'Pause Sim';
          dom.btnToggleDiurnal.style.background = 'var(--accent-solar)';
          dom.btnToggleDiurnal.style.color = '#0b0d11';
          showToast('Diurnal solar simulation running', '☀️');
        } else {
          dom.playIcon.textContent = '▶';
          dom.playText.textContent = 'Simulate Cycle';
          dom.btnToggleDiurnal.style.background = '#ffffff';
          dom.btnToggleDiurnal.style.color = '#0b0d11';
          showToast('Simulation paused', '⏸');
        }
      });
    }

    // Speed chips (1x, 10x, 60x, 300x)
    const speedChips = dom.diurnalBar?.parentElement?.querySelectorAll('.speed-chips-row .mini-pill-btn');
    speedChips?.forEach(chip => {
      chip.addEventListener('click', () => {
        speedChips.forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        state.sun.simSpeed = Number(chip.getAttribute('data-speed'));
        playTickSound(1600, 0.02, 0.04);
        showToast(`Simulation Speed: ${chip.textContent}`);
      });
    });
  }

  // ============================================================
  // CARD 7: VOLUMETRICS CONTROLS
  // ============================================================
  function setupVolumetricsInteraction() {
    if (dom.sliderAnisotropy) {
      dom.sliderAnisotropy.addEventListener('input', (e) => {
        state.sun.anisotropy = Number(e.target.value);
        if (dom.anisotropyVal) dom.anisotropyVal.textContent = `${state.sun.anisotropy >= 0 ? '+' : ''}${state.sun.anisotropy.toFixed(2)}`;
        renderVolumetricCanvas();
      });
    }

    if (dom.sliderRaySamples) {
      dom.sliderRaySamples.addEventListener('input', (e) => {
        state.sun.raySamples = Number(e.target.value);
        if (dom.samplesVal) dom.samplesVal.textContent = `${state.sun.raySamples} Samples`;
        renderVolumetricCanvas();
      });
    }
  }

  // ============================================================
  // CARD 8: TELEMETRY CONTROLS
  // ============================================================
  function setupTelemetryInteraction() {
    if (dom.toggleRtShadows) {
      dom.toggleRtShadows.addEventListener('change', (e) => {
        state.sun.rtShadowsEnabled = e.target.checked;
        showToast(state.sun.rtShadowsEnabled ? 'Inline RT Shadows: ON' : 'Inline RT Shadows: OFF');
        playTickSound(1400, 0.02, 0.04);
      });
    }
    if (dom.toggleRtDenoiser) {
      dom.toggleRtDenoiser.addEventListener('change', (e) => {
        state.sun.rtDenoiserEnabled = e.target.checked;
        showToast(state.sun.rtDenoiserEnabled ? 'SVGF Denoiser: ACTIVE' : 'Denoiser BYPASSED');
        playTickSound(1400, 0.02, 0.04);
      });
    }
  }

  // ============================================================
  // HERO BANNER ACTIONS (SNAP ZENITH, GOLDEN HOUR, RESET)
  // ============================================================
  function setupHeroBannerActions() {
    if (dom.btnSnapZenith) {
      dom.btnSnapZenith.addEventListener('click', () => {
        applyTimeOfDayToSun(12.0);
        showToast('Snapped Sun to High Noon Zenith (12:00)');
        playTickSound(1600, 0.03, 0.06);
      });
    }

    if (dom.btnGoldenHour) {
      dom.btnGoldenHour.addEventListener('click', () => {
        applyTimeOfDayToSun(17.3);
        showToast('Configured Cinematic Golden Hour (17:18)');
        playTickSound(1600, 0.03, 0.06);
      });
    }

    if (dom.btnResetRig) {
      dom.btnResetRig.addEventListener('click', () => {
        applyTimeOfDayToSun(13.704);
        state.sun.lux = 120450;
        state.sun.kelvin = 5600;
        state.sun.angularDiameter = 0.533;
        state.sun.csmSplits = [15, 50, 150, 600];
        updateIlluminanceDisplay();
        updateKelvinDisplay();
        updateCsmDisplay();
        showToast('Reset Sun Light Rig to Default Parameters');
        playTickSound(1100, 0.03, 0.06);
      });
    }
  }

  // ============================================================
  // RIGHT PANEL: INSPECTOR TWO-WAY SYNCHRONIZATION
  // Transform drag-scrubbing, numeric inputs, accordion toggles
  // ============================================================
  function setupInspectorInteraction() {
    // Accordion fold/unfold
    const accordionHeaders = document.querySelectorAll('.accordion-header');
    accordionHeaders.forEach(header => {
      header.addEventListener('click', () => {
        const section = header.closest('.inspector-accordion-section');
        section.classList.toggle('collapsed');
        playTickSound(1200, 0.015, 0.03);
      });
    });

    // Mobility selector
    const mobBtns = document.querySelectorAll('#insp-mobility .mobility-btn');
    mobBtns.forEach(btn => {
      btn.addEventListener('click', () => {
        mobBtns.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        showToast(`Mobility set to: ${btn.textContent.toUpperCase()}`);
        playTickSound(1400, 0.02, 0.04);
      });
    });

    // Pitch & Yaw numeric fields 2-way sync
    if (dom.transRotPitch) {
      dom.transRotPitch.addEventListener('input', (e) => {
        const p = Number(e.target.value);
        state.sun.elevation = Math.max(-10, Math.min(90, -p));
        updateSunGizmoDisplay();
      });
    }

    if (dom.transRotYaw) {
      dom.transRotYaw.addEventListener('input', (e) => {
        const y = Number(e.target.value);
        state.sun.azimuth = ((y % 360) + 360) % 360;
        updateSunGizmoDisplay();
      });
    }

    // Drag-scrubbing on 'P' (Pitch) and 'Y' (Yaw) label badges!
    const pitchLabel = dom.transRotPitch?.parentElement?.querySelector('.xyz-label');
    const yawLabel = dom.transRotYaw?.parentElement?.querySelector('.xyz-label');

    function makeDraggableLabel(labelEl, isPitch) {
      if (!labelEl) return;
      let isDragging = false;
      let startX = 0;
      let startVal = 0;

      labelEl.addEventListener('mousedown', (e) => {
        isDragging = true;
        startX = e.clientX;
        startVal = isPitch ? -state.sun.elevation : state.sun.azimuth;
        document.body.style.cursor = 'ew-resize';
      });

      window.addEventListener('mousemove', (e) => {
        if (!isDragging) return;
        const delta = (e.clientX - startX) * 0.4;
        const newVal = startVal + delta;
        if (isPitch) {
          state.sun.elevation = Math.max(-10, Math.min(90, -newVal));
        } else {
          state.sun.azimuth = ((newVal % 360) + 360) % 360;
        }
        updateSunGizmoDisplay();
        playTickSound(1000, 0.01, 0.01);
      });

      window.addEventListener('mouseup', () => {
        if (isDragging) {
          isDragging = false;
          document.body.style.cursor = 'default';
        }
      });
    }

    makeDraggableLabel(pitchLabel, true);
    makeDraggableLabel(yawLabel, false);

    // Inspector Lux Field
    if (dom.inspLuxField) {
      dom.inspLuxField.addEventListener('input', (e) => {
        state.sun.lux = Math.max(0, Number(e.target.value));
        updateIlluminanceDisplay();
      });
    }

    // Inspector Color Picker
    if (dom.inspColorPicker) {
      dom.inspColorPicker.addEventListener('input', (e) => {
        const hex = e.target.value;
        state.sun.colorHex = hex;
        if (dom.inspHexField) dom.inspHexField.value = hex;
        if (dom.kelvinSwatch) dom.kelvinSwatch.style.backgroundColor = hex;
        if (dom.kelvinHexLabel) dom.kelvinHexLabel.textContent = hex;
      });
    }

    // Inspector Sun Source Angle slider
    if (dom.inspSliderAngle) {
      dom.inspSliderAngle.addEventListener('input', (e) => {
        state.sun.angularDiameter = Number(e.target.value);
        updateCsmDisplay();
      });
    }

    // Inspector Action Buttons
    if (dom.inspBtnNoon) {
      dom.inspBtnNoon.addEventListener('click', () => {
        applyTimeOfDayToSun(12.0);
        showToast('Aligned Sun to Solar High Noon');
      });
    }

    if (dom.inspBtnCopyHlsl) {
      dom.inspBtnCopyHlsl.addEventListener('click', () => {
        openHlslModal();
      });
    }

    if (dom.inspBtnExportPreset) {
      dom.inspBtnExportPreset.addEventListener('click', () => {
        openJsonModal();
      });
    }
  }

  // ============================================================
  // LEFT PANEL: OUTLINER (SEARCH, FILTERS, HIERARCHY SELECTION)
  // ============================================================
  function setupOutlinerInteraction() {
    // Search input
    if (dom.outlinerSearch) {
      dom.outlinerSearch.addEventListener('input', (e) => {
        const query = e.target.value.toLowerCase().trim();
        const items = dom.treeRoot?.querySelectorAll('.tree-item');
        items?.forEach(item => {
          const name = item.querySelector('.entity-name')?.textContent.toLowerCase() || '';
          if (name.includes(query)) {
            item.style.display = 'flex';
          } else {
            item.style.display = 'none';
          }
        });
      });

      // Keyboard shortcut ⌘F / Ctrl+F focuses search
      window.addEventListener('keydown', (e) => {
        if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'f') {
          e.preventDefault();
          dom.outlinerSearch.focus();
        }
      });
    }

    // Category filter chips
    const filterChips = dom.outlinerFilters?.querySelectorAll('.filter-chip');
    filterChips?.forEach(chip => {
      chip.addEventListener('click', () => {
        filterChips.forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        const filter = chip.getAttribute('data-filter');
        playTickSound(1300, 0.015, 0.03);

        const items = dom.treeRoot?.querySelectorAll('.tree-item');
        items?.forEach(item => {
          const type = item.getAttribute('data-type');
          if (filter === 'all' || type === filter) {
            item.style.display = 'flex';
          } else {
            item.style.display = 'none';
          }
        });
      });
    });

    // Folder collapse / expand
    const folderHeaders = dom.treeRoot?.querySelectorAll('.tree-folder-header');
    folderHeaders?.forEach(header => {
      header.addEventListener('click', () => {
        const folder = header.closest('.tree-folder');
        folder.classList.toggle('collapsed');
        playTickSound(1100, 0.01, 0.02);
      });
    });

    // Entity Selection
    const treeItems = dom.treeRoot?.querySelectorAll('.tree-item');
    treeItems?.forEach(item => {
      item.addEventListener('click', (e) => {
        if (e.target.closest('.tree-item-actions')) return; // ignore toggles

        treeItems.forEach(i => i.classList.remove('selected'));
        item.classList.add('selected');

        const entityId = item.getAttribute('data-id');
        const entityName = item.querySelector('.entity-name')?.textContent || '';
        const entityIcon = item.querySelector('.entity-icon')?.textContent || '☀️';

        selectEntity(entityId, entityName, entityIcon);
        playTickSound(1500, 0.02, 0.04);
      });
    });
  }

  // ============================================================
  // MULTI-ENTITY SELECTION ENGINE
  // When user selects Sun: full Sun Gizmo and 8 Sun cards.
  // When user selects Camera, Campfire, Atmosphere: adapts gracefully!
  // ============================================================
  function selectEntity(id, name, icon) {
    state.selectedEntityId = id;

    // Update Banner
    if (dom.heroAvatar) dom.heroAvatar.textContent = icon;
    if (dom.heroTitle) dom.heroTitle.textContent = name;
    if (dom.inspHeaderIcon) dom.inspHeaderIcon.textContent = icon;
    if (dom.inspHeaderName) dom.inspHeaderName.textContent = name;

    const cardsContainer = document.getElementById('cards-container');
    const heroSubtitle = dom.heroSubtitle;

    if (id === 'sun') {
      if (dom.heroTypeBadge) dom.heroTypeBadge.textContent = 'Primary Directional';
      if (heroSubtitle) heroSubtitle.textContent = 'Atmosphere coupled · Cascaded Shadow Maps (4 splits) · Raytraced Penumbra';

      // Restore all 8 Sun cards
      const allCards = cardsContainer?.children;
      if (allCards) {
        for (let card of allCards) {
          card.style.display = 'flex';
        }
      }
      showToast('Selected Directional Light (Sun) - Gizmo Active');
    } else if (id === 'atmo') {
      if (dom.heroTypeBadge) dom.heroTypeBadge.textContent = 'Sky Atmosphere';
      if (heroSubtitle) heroSubtitle.textContent = 'Precomputed Atmospheric Transmittance · Rayleigh & Mie Phase Scattering';
      showToast('Selected Sky Atmosphere & Fog');
    } else if (id === 'camera-hero') {
      if (dom.heroTypeBadge) dom.heroTypeBadge.textContent = 'Cinematic Camera';
      if (heroSubtitle) heroSubtitle.textContent = '35mm Full Frame Sensor · f/2.8 Prime · ACES Color Pipeline';
      showToast('Selected Cinematic Hero Camera');
    } else if (id === 'campfire') {
      if (dom.heroTypeBadge) dom.heroTypeBadge.textContent = 'Point Light (Omni)';
      if (heroSubtitle) heroSubtitle.textContent = '1,800K Warm Flame · Inverse Square Law · Cube Map Shadows';
      showToast('Selected Point Light (Campfire)');
    } else {
      if (dom.heroTypeBadge) dom.heroTypeBadge.textContent = 'Engine Component';
      if (heroSubtitle) heroSubtitle.textContent = 'Active Scene Object';
      showToast(`Selected ${name}`);
    }
  }

  // ============================================================
  // TOP NAVIGATION PILLS (STUDIO, PHOTOMETRICS, SHADOWS, ETC.)
  // ============================================================
  function setupNavigationPills() {
    const navPills = document.querySelectorAll('#nav-mode-selector .nav-pill');
    navPills.forEach(pill => {
      pill.addEventListener('click', () => {
        navPills.forEach(p => p.classList.remove('active'));
        pill.classList.add('active');
        const mode = pill.getAttribute('data-mode');
        playTickSound(1400, 0.02, 0.04);

        const cardGizmo = document.getElementById('card-gizmo');
        const cardLux = document.getElementById('card-illuminance');
        const cardKelvin = document.getElementById('card-kelvin');
        const cardCsm = document.getElementById('card-csm');
        const cardAtmo = document.getElementById('card-atmosphere');
        const cardTimeline = document.getElementById('card-timeline');
        const cardVol = document.getElementById('card-volumetric');
        const cardTel = document.getElementById('card-telemetry');

        const allCards = [cardGizmo, cardLux, cardKelvin, cardCsm, cardAtmo, cardTimeline, cardVol, cardTel];

        // Filter cards view based on mode
        if (mode === 'studio') {
          allCards.forEach(c => c && (c.style.display = 'flex'));
          showToast('Dashboard View: Sun & Sky Full Studio');
        } else if (mode === 'photometrics') {
          allCards.forEach(c => c && (c.style.display = 'none'));
          if (cardLux) cardLux.style.display = 'flex';
          if (cardKelvin) cardKelvin.style.display = 'flex';
          if (cardGizmo) cardGizmo.style.display = 'flex';
          showToast('Filtered View: Photometrics & Color');
        } else if (mode === 'shadows') {
          allCards.forEach(c => c && (c.style.display = 'none'));
          if (cardCsm) cardCsm.style.display = 'flex';
          if (cardTel) cardTel.style.display = 'flex';
          if (cardGizmo) cardGizmo.style.display = 'flex';
          showToast('Filtered View: Cascaded Shadow Maps');
        } else if (mode === 'atmosphere') {
          allCards.forEach(c => c && (c.style.display = 'none'));
          if (cardAtmo) cardAtmo.style.display = 'flex';
          if (cardVol) cardVol.style.display = 'flex';
          if (cardTimeline) cardTimeline.style.display = 'flex';
          showToast('Filtered View: Atmospheric Scattering');
        } else if (mode === 'raytracing') {
          allCards.forEach(c => c && (c.style.display = 'none'));
          if (cardTel) cardTel.style.display = 'flex';
          if (cardCsm) cardCsm.style.display = 'flex';
          showToast('Filtered View: Raytracing Telemetry');
        } else if (mode === 'presets') {
          openJsonModal();
        }
      });
    });
  }

  // ============================================================
  // CODE & JSON EXPORT MODALS
  // ============================================================
  function openHlslModal() {
    const s = state.sun;
    const vec = computeSunVector(s.azimuth, s.elevation);

    const code = `// =======================================================
// FRONTIER GAME ENGINE - DIRECTIONAL LIGHT CONSTANT BUFFER
// Generated for DX12 / Vulkan DXR HLSL Shader Pipeline
// =======================================================

cbuffer DirectionalLightCB : register(b1)
{
    float3  g_SunDirection;          // [${vec.x.toFixed(4)}, ${vec.y.toFixed(4)}, ${vec.z.toFixed(4)}]
    float   g_SunIntensityLux;       // ${s.lux.toFixed(1)} Lux
    float3  g_SunColorLinear;        // [${(s.colorRGB.r/255).toFixed(4)}, ${(s.colorRGB.g/255).toFixed(4)}, ${(s.colorRGB.b/255).toFixed(4)}]
    float   g_SunAngularDiameter;    // ${s.angularDiameter.toFixed(4)} rad
    
    // Cascaded Shadow Maps (CSM 4-Split Partitions)
    float4  g_CascadeSplitDistances; // [${s.csmSplits[0]}.0, ${s.csmSplits[1]}.0, ${s.csmSplits[2]}.0, ${s.csmSplits[3]}.0]
    float   g_ShadowDepthBias;       // ${s.shadowBias.toFixed(4)}
    float   g_ShadowSlopeBias;       // 0.5000
    uint    g_ShadowMapResolution;   // 4096
    uint    g_RaytracingSamples;     // ${s.raySamples}
    
    // Atmospheric & Crepuscular Phase
    float   g_RayleighScattering;    // ${s.rayleigh.toFixed(4)}
    float   g_MieScattering;         // ${s.mie.toFixed(4)}
    float   g_VolumetricScattering;  // ${s.godraysIntensity.toFixed(4)}
    float   g_PhaseAnisotropyG;      // ${s.anisotropy.toFixed(4)}
};`;

    if (dom.modalTitle) dom.modalTitle.textContent = 'HLSL / C++ Constant Buffer (DirectionalLightCB)';
    if (dom.modalCode) dom.modalCode.textContent = code;
    if (dom.codeModal) dom.codeModal.classList.add('active');
  }

  function openJsonModal() {
    const s = state.sun;
    const vec = computeSunVector(s.azimuth, s.elevation);

    const json = {
      engine: 'Frontier Engine',
      version: '4.8.2 LTS',
      entity: 'Directional_Light_Sun',
      photometrics: {
        illuminanceLux: s.lux,
        colorTemperatureKelvin: s.kelvin,
        colorHex: s.colorHex,
        colorLinearRGB: [
          Number((s.colorRGB.r / 255).toFixed(4)),
          Number((s.colorRGB.g / 255).toFixed(4)),
          Number((s.colorRGB.b / 255).toFixed(4))
        ],
        exposureBiasEV: s.ev
      },
      celestialVector: {
        azimuthDeg: s.azimuth,
        elevationDeg: s.elevation,
        directionXYZ: vec,
        solarTime: s.timeOfDay
      },
      cascadedShadows: {
        resolution: 4096,
        splitDistancesMeters: s.csmSplits,
        angularDiameterDeg: s.angularDiameter,
        depthBias: s.shadowBias,
        raytracingSoftPenumbra: s.rtShadowsEnabled
      },
      atmosphere: {
        rayleighCoeff: s.rayleigh,
        mieTurbidity: s.mie,
        volumetricScattering: s.godraysIntensity,
        henyeyGreensteinG: s.anisotropy
      }
    };

    if (dom.modalTitle) dom.modalTitle.textContent = 'Exported Environment Light Rig (JSON)';
    if (dom.modalCode) dom.modalCode.textContent = JSON.stringify(json, null, 2);
    if (dom.codeModal) dom.codeModal.classList.add('active');
  }

  function setupModals() {
    if (dom.modalClose) {
      dom.modalClose.addEventListener('click', () => {
        dom.codeModal.classList.remove('active');
      });
    }
    if (dom.modalDismissBtn) {
      dom.modalDismissBtn.addEventListener('click', () => {
        dom.codeModal.classList.remove('active');
      });
    }
    if (dom.codeModal) {
      dom.codeModal.addEventListener('click', (e) => {
        if (e.target === dom.codeModal) dom.codeModal.classList.remove('active');
      });
    }
    if (dom.modalCopyBtn) {
      dom.modalCopyBtn.addEventListener('click', () => {
        if (dom.modalCode) {
          navigator.clipboard.writeText(dom.modalCode.textContent).then(() => {
            showToast('Copied to Clipboard!');
          });
        }
      });
    }
    if (dom.btnHlslCode) {
      dom.btnHlslCode.addEventListener('click', openHlslModal);
    }
    if (dom.btnExportJson) {
      dom.btnExportJson.addEventListener('click', openJsonModal);
    }

    // Card expand ↗ icons
    const cardArrows = document.querySelectorAll('.card-action-arrow');
    cardArrows.forEach(arrow => {
      arrow.addEventListener('click', () => {
        const cardType = arrow.getAttribute('data-card');
        showToast(`Card telemetry inspected: ${cardType.toUpperCase()}`, '↗');
        playTickSound(1500, 0.02, 0.04);
      });
    });
  }

  // ============================================================
  // MAIN ANIMATION LOOP
  // Handles diurnal simulation & telemetry real-time ticker
  // ============================================================
  let lastFrameTime = performance.now();
  let telemetryTicker = 0;

  function engineLoop(currentTime) {
    const dt = (currentTime - lastFrameTime) / 1000;
    lastFrameTime = currentTime;

    // Diurnal Simulation
    if (state.sun.isSimulating) {
      // 1 real second = dt * simSpeed seconds of diurnal cycle
      // 1 hr = 3600s
      const hoursAdvance = (dt * state.sun.simSpeed * 0.25);
      state.sun.timeOfDay = (state.sun.timeOfDay + hoursAdvance) % 24;
      applyTimeOfDayToSun(state.sun.timeOfDay);
    }

    // Render Canvas Widgets
    renderAtmoCanvas();
    renderVolumetricCanvas();

    // Telemetry jitter & redraw
    telemetryTicker += dt;
    if (telemetryTicker > 0.4) {
      telemetryTicker = 0;
      // Add slight realistic jitter to shadow pass cost
      const jitter = (Math.random() - 0.5) * 0.04;
      const baseCost = state.sun.rtShadowsEnabled ? 0.38 : 0.21;
      const currentCost = Math.max(0.15, Number((baseCost + jitter).toFixed(2)));

      state.sun.rtCost = currentCost;
      state.historyRT.shift();
      state.historyRT.push(currentCost);

      if (dom.valRtMajor) dom.valRtMajor.textContent = currentCost.toFixed(2).split('.')[0] + '.';
      if (dom.valRtFraction) dom.valRtFraction.textContent = currentCost.toFixed(2).split('.')[1];
      if (dom.hdrRtVal) dom.hdrRtVal.textContent = currentCost.toFixed(2);
      if (dom.statGpuCost) dom.statGpuCost.innerHTML = `${currentCost.toFixed(2)}<span style="font-size:14px; font-weight:400; color:var(--text-muted); margin-left:3px;">ms</span>`;

      renderTelemetryCanvas();
    }

    requestAnimationFrame(engineLoop);
  }

  // ============================================================
  // INITIALIZATION
  // ============================================================
  function init() {
    initAtmoParticles(35);
    updateSunGizmoDisplay();
    updateIlluminanceDisplay();
    updateKelvinDisplay();
    updateCsmDisplay();
    updateTimelineDisplay();

    setupGizmoInteraction();
    setupProbesInteraction();
    setupKelvinInteraction();
    setupCsmInteraction();
    setupAtmoInteraction();
    setupTimelineInteraction();
    setupVolumetricsInteraction();
    setupTelemetryInteraction();
    setupHeroBannerActions();
    setupInspectorInteraction();
    setupOutlinerInteraction();
    setupNavigationPills();
    setupModals();

    renderAtmoCanvas();
    renderVolumetricCanvas();
    renderTelemetryCanvas();

    requestAnimationFrame(engineLoop);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }

})();
