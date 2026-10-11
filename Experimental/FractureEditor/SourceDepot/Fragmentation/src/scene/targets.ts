import { Vector3 } from 'three';
import { box, Piece, roughBlank } from '../core/convex';
import { rng } from '../core/math';
import { MATERIALS, type MaterialDef } from '../fracture/materials';

export interface Target {
  piece: Piece;
  mat: MaterialDef;
  position: Vector3;
  /** held in a frame / wall — does not fall before it breaks */
  static: boolean;
  /** cosmetic supports */
  supports: { size: [number, number, number]; pos: [number, number, number] }[];
  /** rebar lines in object space (concrete) */
  rebar: { a: Vector3; b: Vector3 }[];
  hint: string;
}

export function makeTarget(id: string): Target {
  const mat = MATERIALS[id];
  const r = rng(7);
  switch (id) {
    case 'glass':
    case 'tempered': {
      const w = 1.7, h = 1.15, t = 0.014;
      return {
        piece: box(w, h, t), mat, position: new Vector3(0, 1.25, 0), static: true,
        supports: [
          { size: [w + 0.1, 0.06, 0.06], pos: [0, h / 2 + 0.03, 0] },
          { size: [w + 0.1, 0.06, 0.06], pos: [0, -h / 2 - 0.03, 0] },
          { size: [0.06, h + 0.16, 0.06], pos: [-w / 2 - 0.03, 0, 0] },
          { size: [0.06, h + 0.16, 0.06], pos: [w / 2 + 0.03, 0, 0] }],
        rebar: [],
        hint: id === 'glass'
          ? 'Annealed float glass: radial star from the contact, concentric Wallner rings outward, big intact slabs at the frame.'
          : 'Toughened glass: 46 kJ/m³ of frozen-in tensile energy. One puncture and it dices itself — the impact barely matters.',
      };
    }
    case 'wood': {
      const w = 1.6, h = 0.26, t = 0.05;
      return {
        piece: box(w, h, t), mat: { ...mat, anisoDir: new Vector3(1, 0, 0) },
        position: new Vector3(0, 0.75, 0), static: false,
        supports: [
          { size: [0.18, 0.7, 0.3], pos: [-w / 2 + 0.06, -0.48, 0] },
          { size: [0.18, 0.7, 0.3], pos: [w / 2 - 0.06, -0.48, 0] }],
        rebar: [],
        hint: 'Pine, grain along X. Splitting along the fibres costs ~11× less energy than cutting them, so it throws long splinters and hinges instead of chips.',
      };
    }
    case 'concrete': {
      const w = 1.9, h = 1.5, t = 0.22;
      const rebar: { a: Vector3; b: Vector3 }[] = [];
      for (let i = -2; i <= 2; i++) rebar.push({ a: new Vector3(-w / 2, i * 0.3, 0), b: new Vector3(w / 2, i * 0.3, 0) });
      for (let i = -3; i <= 3; i++) rebar.push({ a: new Vector3(i * 0.28, -h / 2, 0), b: new Vector3(i * 0.28, h / 2, 0) });
      return {
        piece: box(w, h, t), mat, position: new Vector3(0, 0.9, 0), static: true,
        supports: [{ size: [w + 0.3, 0.12, t + 0.2], pos: [0, -h / 2 - 0.06, 0] }],
        rebar,
        hint: 'Reinforced concrete: a compressive crushing cone punches through, radial shear cracks run out, and the rebar cage keeps the slab hanging together.',
      };
    }
    case 'rock': {
      return {
        piece: roughBlank(0.6, 13, r, 0.85), mat, position: new Vector3(0, 0.62, 0), static: false,
        supports: [], rebar: [],
        hint: 'Granite with horizontal bedding: a Hertzian cone spalls out under the hit, then the block splits preferentially along its weak bedding planes.',
      };
    }
    default: {
      return {
        piece: box(0.72, 0.72, 0.72), mat, position: new Vector3(0, 0.45, 0), static: false,
        supports: [], rebar: [],
        hint: 'ABS: G_c is ~700× glass. It cannot pay for many new surfaces, so it necks, whitens and tears into a few large curved flaps.',
      };
    }
  }
}
