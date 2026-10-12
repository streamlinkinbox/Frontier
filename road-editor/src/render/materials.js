// Material keys used by the bucket builders. Colours are base colours; per-vertex tints
// (per-road look, wear, patches) multiply on top.
export const MATERIALS = {
  asphalt: { color: 0x33363b, roughness: 0.93 },
  asphalt_red: { color: 0x9a2e2e, roughness: 0.85 },
  bike: { color: 0x2f8a52, roughness: 0.85 },
  concrete: { color: 0xc4c2ba, roughness: 0.85 },
  pavers: { color: 0xa39585, roughness: 0.9 },
  grass: { color: 0x4f8a36, roughness: 1 },
  gravel: { color: 0x9a8b70, roughness: 1 },
  dirt: { color: 0x6e5536, roughness: 1 },
  tram: { color: 0x7a6b60, roughness: 0.9 },
  ballast: { color: 0x57524a, roughness: 1 },
  paint_white: { color: 0xf2f2ef, roughness: 0.6 },
  paint_yellow: { color: 0xf0c020, roughness: 0.6 },
  paint_red: { color: 0xd43b3b, roughness: 0.6 },
  paint_green: { color: 0x3ea86a, roughness: 0.6 },
  steel: { color: 0xa7adb3, roughness: 0.35, metalness: 0.8 },
  wood: { color: 0x6b4a2b, roughness: 0.95 },
  bridge: { color: 0xb8b5ac, roughness: 0.8 },
  bridge_steel: { color: 0x7b8fa3, roughness: 0.4, metalness: 0.6 },
  cable: { color: 0xdedede, roughness: 0.3, metalness: 0.7 },
  tunnel: { color: 0x3f4d5e, roughness: 0.7 },
  rubber: { color: 0x161616, roughness: 0.95 },
  bollard: { color: 0xf1f1f1, roughness: 0.5 },
  red: { color: 0xc62828, roughness: 0.5 },
  lamp_pole: { color: 0x2d3136, roughness: 0.5, metalness: 0.4 },
  lamp_glow: { color: 0xfff1c4, roughness: 0.3, emissive: 0xffe9a0, emissiveIntensity: 1.2 },
  lamp_red: { color: 0xff3b30, roughness: 0.3, emissive: 0xff3b30, emissiveIntensity: 1.5 },
  ground: { color: 0xffffff, roughness: 1 },
  sand: { color: 0xc8b48a, roughness: 1 },
};

export const MATERIAL_KEYS = Object.keys(MATERIALS);
