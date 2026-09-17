import * as THREE from 'three';
import { RoofParams } from './types';
import { woodTexture, plasterTexture } from './textures';

export interface RoofMaterials {
  tile: THREE.MeshStandardMaterial;
  ridge: THREE.MeshStandardMaterial;
  underlay: THREE.MeshStandardMaterial;
  underside: THREE.MeshStandardMaterial;
  wood: THREE.MeshStandardMaterial;
  woodDark: THREE.MeshStandardMaterial;
  plaster: THREE.MeshStandardMaterial;
  mortar: THREE.MeshStandardMaterial;
  stone: THREE.MeshStandardMaterial;
  flashing: THREE.MeshStandardMaterial;
  all: THREE.MeshStandardMaterial[];
}

let mats: RoofMaterials | null = null;

export function getMaterials(): RoofMaterials {
  if (mats) return mats;
  const tile = new THREE.MeshStandardMaterial({ color: '#3a4048', roughness: 0.6, metalness: 0.08, side: THREE.DoubleSide });
  const ridge = new THREE.MeshStandardMaterial({ color: '#2c3138', roughness: 0.55, metalness: 0.08, side: THREE.DoubleSide });
  const underlay = new THREE.MeshStandardMaterial({ color: '#26211c', roughness: 0.95 });
  const underside = new THREE.MeshStandardMaterial({ color: '#4a3a2c', roughness: 0.9, side: THREE.BackSide });
  const wood = new THREE.MeshStandardMaterial({ color: '#7a5c42', roughness: 0.8, map: woodTexture() });
  const woodDark = new THREE.MeshStandardMaterial({ color: '#4a3828', roughness: 0.85, map: woodTexture() });
  const plaster = new THREE.MeshStandardMaterial({ color: '#e8e0d0', roughness: 0.95, map: plasterTexture(), side: THREE.DoubleSide });
  const mortar = new THREE.MeshStandardMaterial({ color: '#b3aa98', roughness: 0.95 });
  const stone = new THREE.MeshStandardMaterial({ color: '#8d9094', roughness: 0.9 });
  const flashing = new THREE.MeshStandardMaterial({ color: '#2c3138', roughness: 0.45, metalness: 0.55, side: THREE.DoubleSide });
  const all = [tile, ridge, underlay, underside, wood, woodDark, plaster, mortar, stone, flashing];
  mats = { tile, ridge, underlay, underside, wood, woodDark, plaster, mortar, stone, flashing, all };
  return mats;
}

/** Push user colors into the shared materials (no re-allocation on slider drag). */
export function applyParamsToMaterials(p: RoofParams): void {
  const m = getMaterials();
  m.tile.color.set(p.roofColor);
  m.tile.roughness = 0.85 - p.tileGlaze * 0.65;
  m.tile.metalness = p.tile === 'modern' ? 0.55 : 0.08;
  m.ridge.color.set(p.ridgeColor);
  m.ridge.roughness = 0.8 - p.tileGlaze * 0.55;
  m.ridge.metalness = p.tile === 'modern' ? 0.55 : 0.08;
  m.flashing.color.set(p.ridgeColor);
  m.plaster.color.set(p.wallColor);
  m.wood.color.set(p.woodColor);
  m.woodDark.color.set(p.woodColor).multiplyScalar(0.62);
  if (p.tile === 'modern') {
    m.tile.side = THREE.FrontSide;
    m.tile.roughness = Math.max(0.35, m.tile.roughness);
  } else {
    m.tile.side = THREE.DoubleSide;
  }
  for (const mat of m.all) mat.needsUpdate = false;
}
