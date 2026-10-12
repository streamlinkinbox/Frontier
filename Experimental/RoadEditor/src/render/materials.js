// Material library keyed by the material names emitted by the road builders.
import * as THREE from 'three';

export function buildMaterials(T) {
  const std = (o) => new THREE.MeshStandardMaterial(o);
  const tex = (name, extra = {}) => {
    const t = T[name];
    return std({ map: t.map, normalMap: t.normal || null, normalScale: new THREE.Vector2(0.6, 0.6), roughness: 0.92, metalness: 0, ...extra });
  };
  const M = {
    asphalt: tex('asphalt', { roughness: 0.88 }),
    asphaltOld: tex('asphaltOld', { roughness: 0.95 }),
    busRed: tex('busRed', { roughness: 0.85 }),
    bikeGreen: tex('bikeGreen', { roughness: 0.85 }),
    concrete: tex('concrete', { roughness: 0.9 }),
    sidewalk: tex('sidewalk', { roughness: 0.9 }),
    curb: tex('curb', { roughness: 0.82, color: 0xe6e3dc }),
    pavers: tex('pavers', { roughness: 0.9 }),
    cobble: tex('cobble', { roughness: 0.8, normalScale: new THREE.Vector2(1.2, 1.2) }),
    gravel: tex('gravel', { roughness: 1 }),
    dirt: tex('dirt', { roughness: 1 }),
    grass: tex('grass', { roughness: 1 }),
    ballast: tex('ballast', { roughness: 1, normalScale: new THREE.Vector2(1.4, 1.4) }),
    tactile: std({ map: T.tactile.map, roughness: 0.7 }),
    barrier: tex('concrete', { color: 0xd8d4cc, roughness: 0.85 }),
    girder: std({ color: 0x8c949c, roughness: 0.55, metalness: 0.4 }),
    steel: std({ color: 0x9ea3a8, roughness: 0.35, metalness: 0.85 }),
    railGroove: std({ color: 0x3a3836, roughness: 0.7, metalness: 0.4 }),
    crossPanel: tex('concrete', { color: 0x8e8a83, roughness: 0.95 }),
    fence: std({ map: T.fence.map, transparent: false, alphaTest: 0.5, side: THREE.DoubleSide, roughness: 0.5, metalness: 0.6 }),
    markWhite: std({ color: 0xefeee8, roughness: 0.6, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -4 }),
    markYellow: std({ color: 0xe0ad1c, roughness: 0.6, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -4 }),
    decal: std({ map: T.decals, alphaTest: 0.45, transparent: false, roughness: 0.6, polygonOffset: true, polygonOffsetFactor: -3, polygonOffsetUnits: -6 }),
    tunnel: tex('concrete', { color: 0x9a978f, side: THREE.DoubleSide, roughness: 0.95 }),
    platform: tex('sidewalk', { color: 0xc9c4ba }),
    platformEdge: std({ color: 0xd9b51e, roughness: 0.7 }),
    water: std({ color: 0x1d4a5a, roughness: 0.08, metalness: 0.1, normalMap: T.waterNormal, normalScale: new THREE.Vector2(0.35, 0.35), transparent: true, opacity: 0.88 }),
    riverbed: tex('dirt', { color: 0x5b5a4a }),
    quay: tex('quay', { roughness: 0.95 }),
    quayTop: tex('concrete', { color: 0xbab3a6 }),
  };
  // fallback for unknown names
  M.__fallback = std({ color: 0xff00ff });
  return M;
}

// Prop part materials
export function buildPropMaterials(T) {
  const std = (o) => new THREE.MeshStandardMaterial(o);
  return {
    vcol: std({ vertexColors: true, roughness: 0.6, metalness: 0.25 }),
    leaf: std({ vertexColors: true, roughness: 0.9, metalness: 0, flatShading: true }),
    sign: std({ map: T.signs, roughness: 0.45, side: THREE.DoubleSide, alphaTest: 0.5 }),
    glass: std({ color: 0xa8c8d8, roughness: 0.05, metalness: 0.2, transparent: true, opacity: 0.32, depthWrite: false }),
    glow: std({ color: 0xfff1d0, emissive: 0xffe2a8, emissiveIntensity: 0.4, roughness: 0.4 }),
    sigRed: std({ color: 0x400808, emissive: 0xff2010, emissiveIntensity: 0.25 }),
    sigAmber: std({ color: 0x402a08, emissive: 0xffa000, emissiveIntensity: 0.15 }),
    sigGreen: std({ color: 0x08401a, emissive: 0x20ff70, emissiveIntensity: 1.6 }),
  };
}
