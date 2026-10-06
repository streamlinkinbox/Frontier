//============================================================================================================================================
//                                                               PROFILES.JS
//============================================================================================================================================
// Cross-section presets and bridge catalogues. All dimensions in metres.

import { ROAD_SURFACES, DEFAULT_SURFACE } from './Surfaces.js?v=11';

export const ROAD_PRESETS = {
  street: { label: 'Street', roadWidth: 8.0, pavementLeft: 2.0, pavementRight: 2.0, curbHeight: 0.18, curbWidth: 0.22, lanes: 2 },
  avenue: { label: 'Avenue', roadWidth: 14.0, pavementLeft: 3.2, pavementRight: 3.2, curbHeight: 0.2, curbWidth: 0.28, lanes: 4 },
  alley: { label: 'Alley', roadWidth: 5.0, pavementLeft: 1.0, pavementRight: 1.0, curbHeight: 0.12, curbWidth: 0.16, lanes: 1 },
  narrow: { label: 'Narrow lane', roadWidth: 6.0, pavementLeft: 1.4, pavementRight: 1.4, curbHeight: 0.15, curbWidth: 0.18, lanes: 2 },
  highway: { label: 'Highway', roadWidth: 20.0, pavementLeft: 1.2, pavementRight: 1.2, curbHeight: 0.26, curbWidth: 0.35, lanes: 6 },
  // A slip road is one running lane between two hard strips, never a two-way street narrowed down.
  slip: { label: 'Slip road', roadWidth: 5.5, pavementLeft: 1.0, pavementRight: 1.6, curbHeight: 0.14, curbWidth: 0.25, lanes: 1 },
  // Unsealed presets. No kerb to speak of, a loose shoulder instead of a footway and a camber steep enough to shed
  // water, because an unsealed road with a 2 % crown turns into a river.
  gravel: { label: 'Gravel road', roadWidth: 6.4, pavementLeft: 1.1, pavementRight: 1.1, curbHeight: 0.02, curbWidth: 0.1, lanes: 2, surface: 'gravel' },
  track: { label: 'Farm track', roadWidth: 3.6, pavementLeft: 0.7, pavementRight: 0.7, curbHeight: 0.0, curbWidth: 0.06, lanes: 1, surface: 'track' },
  forest: { label: 'Forest road', roadWidth: 5.0, pavementLeft: 1.4, pavementRight: 1.4, curbHeight: 0.0, curbWidth: 0.08, lanes: 1, surface: 'dirt' },
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
  // Surface: the corridor's own choice wins, otherwise the preset's, otherwise asphalt.
  const surface = ROAD_SURFACES[corridor.surface] ? corridor.surface : (preset.surface || DEFAULT_SURFACE);
  const spec = ROAD_SURFACES[surface];
  const p = {
    label: preset.label,
    roadWidth: num(o.roadWidth, preset.roadWidth),
    pavementLeft: num(o.pavementLeft, preset.pavementLeft),
    pavementRight: num(o.pavementRight, preset.pavementRight),
    curbHeight: num(o.curbHeight, preset.curbHeight),
    curbWidth: num(o.curbWidth, preset.curbWidth),
    lanes: num(o.lanes, preset.lanes),
    surface,
    crown: num(o.crown, spec.crown ?? 0.02), // camber: centre is this fraction of the half width above the gutter
    // An unsealed road gets a matching loose shoulder unless the document says otherwise.
    paving: corridor.paving || spec.shoulder || PAVING_DEFAULTS.paving,
    pavingScale: num(corridor.pavingScale, PAVING_DEFAULTS.pavingScale),
  };
  p.roadHalf = p.roadWidth * 0.5;
  p.leftTotalHalf = p.roadHalf + p.curbWidth + p.pavementLeft;
  p.rightTotalHalf = p.roadHalf + p.curbWidth + p.pavementRight;
  p.crownRise = p.roadHalf * p.crown;
  p.sealed = spec.sealed;
  // Lane lines need a sealed surface to be painted on. A document can still force them back on per corridor.
  p.markings = typeof o.markings === 'boolean' ? o.markings : spec.sealed;
  return p;
}

const num = (v, fallback) => (typeof v === 'number' && Number.isFinite(v) ? v : fallback);
