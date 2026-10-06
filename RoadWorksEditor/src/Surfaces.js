//============================================================================================================================================
//                                                              SURFACES.JS
//============================================================================================================================================
// Carriageway surfaces. Until now every road was asphalt; a corridor now picks a running surface the same way it
// picks its paving, and the carriageway is split into one mesh group per surface (`road#gravel`) so each gets its
// own texture in a single draw call.
//
// Unsealed surfaces are not just a different colour. A gravel road or a two-track has no kerb worth the name, no
// painted lines, a much steeper camber to shed water, and a loose shoulder rather than a footway — so the surface
// carries those rules with it and the profile resolver applies them.

export const ROAD_SURFACES = {
  asphalt: { label: 'Asphalt', sealed: true, crown: 0.02 },
  chipseal: { label: 'Chip seal', sealed: true, crown: 0.025 },
  concrete: { label: 'Concrete slab', sealed: true, crown: 0.02 },
  setts: { label: 'Stone setts', sealed: true, crown: 0.03 },
  gravel: { label: 'Gravel', sealed: false, crown: 0.045, shoulder: 'gravelShoulder' },
  dirt: { label: 'Graded dirt', sealed: false, crown: 0.05, shoulder: 'dirtShoulder' },
  track: { label: 'Two-track trail', sealed: false, crown: 0.055, shoulder: 'dirtShoulder' },
};

export const DEFAULT_SURFACE = 'asphalt';

export function surfaceOf(profile) {
  return ROAD_SURFACES[profile?.surface] || ROAD_SURFACES[DEFAULT_SURFACE];
}

export function isSealed(profile) {
  return surfaceOf(profile).sealed;
}

// Carriageway group name. Asphalt keeps the bare `road` name so existing documents, exports and the OBJ group
// layout are unchanged.
export function surfaceGroup(profile) {
  const key = profile?.surface || DEFAULT_SURFACE;
  return key === DEFAULT_SURFACE ? 'road' : `road#${key}`;
}
