// ---------------------------------------------------------------------------
// Central tuning knobs for the mine. Tweak these and reload for a new feel.
// ---------------------------------------------------------------------------
export const CFG = {
  MAZE_W: 15,          // maze cells across
  MAZE_H: 15,          // maze cells deep
  TILE: 9,             // metres per grid tile (tunnel width)
  WALL_H: 5.4,         // tunnel height
  LOOP_CHANCE: 0.16,   // extra criss-cross connections punched through walls

  RAIL_LINES: 3,       // independent minecart rail lines threading the maze
  CART_SPEED: 8.2,     // m/s top speed of a rolling cart
  CART_HIT_RADIUS: 2.5,

  CHECKPOINTS: 6,
  LAMP_POOL: 6,        // how many real dynamic lights are live at once

  CAR: {
    ACCEL: 17,
    BRAKE: 28,
    REVERSE_ACCEL: 11,
    MAX_SPEED: 21,     // ~76 km/h — feels fast underground
    MAX_REVERSE: 8,
    ROLL_DRAG: 1.15,
  },
  HEALTH: 3,
};

export const GW = CFG.MAZE_W * 2 + 1;   // tile-grid width  (cells + walls)
export const GH = CFG.MAZE_H * 2 + 1;   // tile-grid depth

export const tidx = (x, z) => z * GW + x;

// tile index -> world metres (x/z planes, y is up)
export const tileToWorldX = (x) => (x - (GW - 1) / 2) * CFG.TILE;
export const tileToWorldZ = (z) => (z - (GH - 1) / 2) * CFG.TILE;
export const worldToTileX = (wx) => Math.round(wx / CFG.TILE + (GW - 1) / 2);
export const worldToTileZ = (wz) => Math.round(wz / CFG.TILE + (GH - 1) / 2);
