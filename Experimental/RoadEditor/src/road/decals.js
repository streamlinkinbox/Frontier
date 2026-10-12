// Road-marking decal atlas layout (4×4 cells). Painted procedurally in textures.js;
// builders only need the UV rectangles and physical sizes (metres: width across, length along).
export const ATLAS_GRID = 4;
export const DECALS = {
  bus:      { cell: 0,  w: 2.2, l: 2.6 },
  only:     { cell: 1,  w: 2.2, l: 2.6 },
  bike:     { cell: 2,  w: 1.3, l: 2.1 },
  arrowS:   { cell: 3,  w: 0.9, l: 5.0 },
  arrowL:   { cell: 4,  w: 1.5, l: 5.0 },
  arrowR:   { cell: 5,  w: 1.5, l: 5.0 },
  arrowSL:  { cell: 6,  w: 1.6, l: 5.0 },
  arrowSR:  { cell: 7,  w: 1.6, l: 5.0 },
  stop:     { cell: 8,  w: 2.6, l: 2.9 },
  rxr:      { cell: 9,  w: 3.2, l: 6.5 },
  slow:     { cell: 10, w: 2.4, l: 2.8 },
  manhole:  { cell: 11, w: 0.8, l: 0.8 },
  drain:    { cell: 12, w: 0.45, l: 0.95 },
  tram:     { cell: 13, w: 2.2, l: 2.6 },
  keep:     { cell: 14, w: 2.4, l: 2.4 },
  busStop:  { cell: 15, w: 2.4, l: 3.0 },
};
export function decalUV(name) {
  const d = DECALS[name], g = ATLAS_GRID;
  const cx = d.cell % g, cy = Math.floor(d.cell / g);
  const pad = 0.004;
  // v grows "up" the texture = forward along travel direction
  return [cx / g + pad, 1 - (cy + 1) / g + pad, (cx + 1) / g - pad, 1 - cy / g - pad];
}
