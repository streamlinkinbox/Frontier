/* RoadForge — material specs: mesh material names → shader parameters, driven by the Surface layer stack. */
(function (RF) {
  'use strict';
  const PAVING_PATTERNS = [
    { id: 0, name: 'Stretcher bond 200×100' }, { id: 1, name: 'Herringbone 45°' }, { id: 2, name: 'Basketweave' }, { id: 3, name: 'Large slabs 600×600' },
    { id: 4, name: 'Hexagonal pavers' }, { id: 5, name: 'Flagstone (irregular)' }, { id: 6, name: 'Cobblestone setts' },
  ];
  const srgb = (hex) => { const n = parseInt(hex.replace('#', ''), 16); return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255].map((c) => Math.pow(c, 2.2)); };

  /** Build the material spec table from the resolved surface layer parameters. */
  function specs(L) {
    L = L || {};
    const asphaltBase = L.asphaltColor ? srgb(L.asphaltColor) : [0.085, 0.085, 0.088];
    const pavingBase = L.pavingColor ? srgb(L.pavingColor) : srgb('#9c9384');
    const curbBase = L.curbMaterial === 'granite' ? srgb('#8d8d8c') : srgb('#b7b3aa');
    const pipe = (p) => p;
    return {
      asphalt: { type: 'asphalt', color: asphaltBase, roughness: 0.9, scale: L.aggregateScale ?? 1, wear: L.wearEnabled === false ? 0 : (L.wear ?? 0.6), cracks: L.cracksEnabled === false ? 0 : (L.cracks ?? 0.25), patches: L.patchesEnabled === false ? 0 : (L.patches ?? 0.3), wetness: L.wetEnabled === false ? 0 : (L.wetness ?? 0), seed: L.seed ?? 1 },
      asphalt_new: { type: 'asphalt', color: [0.05, 0.05, 0.052], roughness: 0.92, scale: 1, wear: 0, cracks: 0, patches: 0 },
      concrete_road: { type: 'concrete_road', color: srgb('#9a9892'), roughness: 0.85 },
      concrete: { type: 'concrete', color: srgb('#9d9b95'), roughness: 0.8 },
      paving: { type: 'paving', color: pavingBase, roughness: 0.8, scale: L.pavingScale ?? 1, pattern: L.pavingPattern ?? 1 },
      paving_alt: { type: 'paving', color: srgb('#8a8378'), roughness: 0.8, scale: 1, pattern: 3 },
      curb: { type: 'curb', color: curbBase, roughness: 0.85, pattern: L.curbMaterial === 'granite' ? 1 : 0 },
      granite: { type: 'curb', color: srgb('#8d8d8c'), roughness: 0.5, pattern: 1 },
      paint_white: { type: 'paint', color: [0.86, 0.86, 0.84], roughness: 0.6, wear: L.paintWear ?? 0.35, pipeline: 'decal' },
      paint_yellow: { type: 'paint', color: srgb('#e3b63a'), roughness: 0.6, wear: L.paintWear ?? 0.35, pipeline: 'decal' },
      surface_red: { type: 'paint', color: srgb('#4a2320'), roughness: 0.88, wear: (L.paintWear ?? 0.35) * 0.25, pipeline: 'decal' },
      surface_green: { type: 'paint', color: srgb('#245a34'), roughness: 0.88, wear: (L.paintWear ?? 0.35) * 0.25, pipeline: 'decal' },
      grass: { type: 'grass', color: [0.5, 0.5, 0.5], roughness: 1 },
      ground: { type: 'ground', color: [0.5, 0.5, 0.5], roughness: 1, pipeline: 'ground' },
      hole: { type: 'flat', color: [0, 0, 0], pipeline: 'hole' },
      metal_galv: { type: 'metal_galv', color: srgb('#9ea2a6'), roughness: 0.45, metallic: 1, wear: L.rust ?? 0.15 },
      metal_dark: { type: 'metal_paint', color: srgb('#2b2d30'), roughness: 0.4, metallic: 0.4 },
      metal_green: { type: 'metal_paint', color: srgb('#2f4a3a'), roughness: 0.4, metallic: 0.3 },
      metal_white: { type: 'metal_paint', color: srgb('#d9d9d6'), roughness: 0.45, metallic: 0.2 },
      steel_painted: { type: 'metal_paint', color: srgb('#6b7076'), roughness: 0.5, metallic: 0.6, wear: 0.2 },
      cast_iron: { type: 'metal_paint', color: srgb('#1e1f22'), roughness: 0.6, metallic: 0.7, wear: 0.3 },
      carpaint: { type: 'carpaint', color: srgb('#a52a2a'), roughness: 0.25, metallic: 0.6 },
      glass: { type: 'glass', color: [0.02, 0.03, 0.04], roughness: 0.08, metallic: 0.9 },
      rubber: { type: 'rubber', color: [0.03, 0.03, 0.03], roughness: 0.8 },
      chrome: { type: 'metal_paint', color: srgb('#c9ccd0'), roughness: 0.15, metallic: 1 },
      emissive_warm: { type: 'emissive', color: srgb('#ffd9a0'), emissive: 6, wetness: 0.0 },
      emissive_white: { type: 'emissive', color: [1, 1, 1], emissive: 4 },
      signal_red: { type: 'emissive', color: [1, 0.08, 0.05], emissive: 3, wetness: 1 },
      signal_amber: { type: 'emissive', color: [1, 0.55, 0.05], emissive: 3, wetness: 1 },
      signal_green: { type: 'emissive', color: [0.1, 1, 0.3], emissive: 3, wetness: 1 },
      signal_off: { type: 'metal_paint', color: [0.05, 0.05, 0.05], roughness: 0.3, metallic: 0.2 },
      taillight: { type: 'emissive', color: [1, 0.1, 0.05], emissive: 1.5, wetness: 0.4 },
      headlight: { type: 'emissive', color: [1, 0.97, 0.9], emissive: 4, wetness: 0.1 },
      wood: { type: 'wood', color: srgb('#5a4330'), roughness: 0.85 },
      sleeper: { type: 'wood', color: srgb('#3a2d22'), roughness: 0.9 },
      ballast: { type: 'ballast', color: srgb('#8d8a84'), roughness: 0.95 },
      brick: { type: 'brick', color: srgb('#8a4a36'), roughness: 0.9 },
      barrier: { type: 'barrier', color: srgb('#a4a39e'), roughness: 0.8 },
      billboard: { type: 'billboard', color: [1, 1, 1], roughness: 0.5, emissive: 1.2, tile: 0 },
      plastic_orange: { type: 'plastic', color: srgb('#ff5a1f'), roughness: 0.45 },
      plastic_red: { type: 'plastic', color: srgb('#c8271c'), roughness: 0.45 },
      plastic_black: { type: 'plastic', color: srgb('#141414'), roughness: 0.55 },
      plastic_white: { type: 'plastic', color: srgb('#e9e9e6'), roughness: 0.45 },
      rail: { type: 'rail', color: [0.5, 0.5, 0.5], roughness: 0.4, metallic: 1 },
      tunnel_wall: { type: 'concrete', color: srgb('#b8b6ae'), roughness: 0.7 },
      tile_white: { type: 'paving', color: srgb('#dcdcd6'), roughness: 0.3, scale: 1, pattern: 0 },
      flat: { type: 'flat', color: [1, 1, 1], pipeline: 'overlay' },
      gizmo: { type: 'flat', color: [1, 1, 1], pipeline: 'overlay' },
      tactile: { type: 'paving', color: srgb('#c98b3a'), roughness: 0.75, scale: 1, pattern: 7 },
      sign_face: { type: 'billboard', color: [1, 1, 1], roughness: 0.4, emissive: 0.6, tile: 8 },
    };
  }
  RF.materials = { specs, srgb, PAVING_PATTERNS };
})(globalThis.RF || (globalThis.RF = {}));
