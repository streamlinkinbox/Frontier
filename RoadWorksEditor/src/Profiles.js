//============================================================================================================================================
//                                                               PROFILES.JS
//============================================================================================================================================
// Cross-section presets and bridge catalogues. All dimensions in metres.

export const ROAD_PRESETS = {
  street: { label: 'Street', roadWidth: 8.0, pavementLeft: 2.0, pavementRight: 2.0, curbHeight: 0.18, curbWidth: 0.22, lanes: 2 },
  avenue: { label: 'Avenue', roadWidth: 14.0, pavementLeft: 3.2, pavementRight: 3.2, curbHeight: 0.2, curbWidth: 0.28, lanes: 4 },
  alley: { label: 'Alley', roadWidth: 5.0, pavementLeft: 1.0, pavementRight: 1.0, curbHeight: 0.12, curbWidth: 0.16, lanes: 1 },
  narrow: { label: 'Narrow lane', roadWidth: 6.0, pavementLeft: 1.4, pavementRight: 1.4, curbHeight: 0.15, curbWidth: 0.18, lanes: 2 },
  highway: { label: 'Highway', roadWidth: 20.0, pavementLeft: 1.2, pavementRight: 1.2, curbHeight: 0.26, curbWidth: 0.35, lanes: 6 },
};

export const BRIDGE_TYPES = {
  beam: { label: 'Beam / girder', needsTowers: false },
  box: { label: 'Box girder', needsTowers: false },
  slab: { label: 'Solid slab', needsTowers: false },
  cantilever: { label: 'Haunched cantilever', needsTowers: false },
  arch: { label: 'Deck arch', needsTowers: false },
  tiedarch: { label: 'Tied (bowstring) arch', needsTowers: false },
  masonry: { label: 'Masonry viaduct', needsTowers: false },
  truss: { label: 'Warren truss (under)', needsTowers: false },
  throughtruss: { label: 'Pratt through truss', needsTowers: false },
  suspension: { label: 'Suspension', needsTowers: true },
  cablestay: { label: 'Cable-stayed', needsTowers: true },
};

export const PIER_TYPES = {
  wall: 'Wall pier',
  column: 'Twin column',
  hammerhead: 'Hammerhead',
  vpier: 'V-pier',
  none: 'None (abutments only)',
};

export const RAILING_TYPES = {
  parapet: 'Concrete parapet',
  steel: 'Steel rail',
  jersey: 'Jersey barrier',
  none: 'None',
};

// Paving is a per-corridor choice rather than a preset field: the same street profile can be laid in brick in the
// old town and cast concrete on the bypass.
export const PAVING_DEFAULTS = { paving: 'concrete', pavingScale: 1.0 };

export function resolveProfile(corridor) {
  const preset = ROAD_PRESETS[corridor.preset] || ROAD_PRESETS.street;
  const o = corridor.overrides || {};
  const p = {
    label: preset.label,
    roadWidth: num(o.roadWidth, preset.roadWidth),
    pavementLeft: num(o.pavementLeft, preset.pavementLeft),
    pavementRight: num(o.pavementRight, preset.pavementRight),
    curbHeight: num(o.curbHeight, preset.curbHeight),
    curbWidth: num(o.curbWidth, preset.curbWidth),
    lanes: num(o.lanes, preset.lanes),
    crown: num(o.crown, 0.02), // camber: centre is this fraction of the half width above the gutter
    paving: corridor.paving || PAVING_DEFAULTS.paving,
    pavingScale: num(corridor.pavingScale, PAVING_DEFAULTS.pavingScale),
  };
  p.roadHalf = p.roadWidth * 0.5;
  p.leftTotalHalf = p.roadHalf + p.curbWidth + p.pavementLeft;
  p.rightTotalHalf = p.roadHalf + p.curbWidth + p.pavementRight;
  p.crownRise = p.roadHalf * p.crown;
  return p;
}

const num = (v, fallback) => (typeof v === 'number' && Number.isFinite(v) ? v : fallback);
