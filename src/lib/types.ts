/** Canonical Japanese roof styles (with Chinese / Korean equivalents). */
export type RoofStyle = 'kirizuma' | 'yosemune' | 'irimoya' | 'hogyo';
export type TileSystem = 'hongawara' | 'sangawara' | 'modern';
export type Region = 'japan' | 'china' | 'korea';

export interface RoofParams {
  style: RoofStyle;
  region: Region;
  seed: number;
  /** Building footprint in meters (walls). Ridge auto-aligns to the long axis. */
  width: number;
  depth: number;
  wallHeight: number;
  /** Roof pitch as rise/run, e.g. 0.45 ≈ traditional 4.5-sun gradient. */
  pitch: number;
  /** Concave slope curvature (sorimashi), 0 = straight, ~0.45 = deep temple curve. */
  sori: number;
  /** Eave corner upturn in meters (反り / cheoma / jiaoqiao). */
  cornerLift: number;
  /** Hip-rafter curl in meters (sori of the hip rafter off the straight line). */
  hipSori: number;
  /** Horizontal eave overhang in meters. */
  overhang: number;
  /** Irimoya only: share of the rise occupied by the upper gable roof. */
  gableFraction: number;
  tile: TileSystem;
  roofColor: string;
  ridgeColor: string;
  wallColor: string;
  woodColor: string;
  /** 0 = matte unglazed, 1 = glossy glazed tile. */
  tileGlaze: number;
  showRafters: boolean;
  showStructure: boolean;
  showWalls: boolean;
  onigawara: boolean;
  finial: boolean;
  eaveCaps: boolean;
  /** Cusped gable bargeboards (karahafu-style ogee curve) on gable styles. */
  karahafu: boolean;
  rafterSpacing: number;
}

export const DEFAULT_PARAMS: RoofParams = {
  style: 'irimoya',
  region: 'japan',
  seed: 7,
  width: 6.4,
  depth: 5.2,
  wallHeight: 2.6,
  pitch: 0.52,
  sori: 0.24,
  cornerLift: 0.16,
  hipSori: 0.1,
  overhang: 1.0,
  gableFraction: 0.45,
  tile: 'hongawara',
  roofColor: '#3a4048',
  ridgeColor: '#2c3138',
  wallColor: '#e8e0d0',
  woodColor: '#7a5c42',
  tileGlaze: 0.35,
  showRafters: true,
  showStructure: true,
  showWalls: true,
  onigawara: true,
  finial: true,
  eaveCaps: true,
  karahafu: false,
  rafterSpacing: 0.455,
};

export interface Preset {
  id: string;
  label: string;
  sub: string;
  patch: Partial<RoofParams>;
}

export const PRESETS: Preset[] = [
  {
    id: 'minka',
    label: 'Minka Farmhouse',
    sub: '日本 · kirizuma + hongawara',
    patch: {
      style: 'kirizuma', region: 'japan', tile: 'hongawara',
      pitch: 0.45, sori: 0.16, cornerLift: 0.09, hipSori: 0.06, overhang: 0.9,
      roofColor: '#6e7278', ridgeColor: '#54575c', tileGlaze: 0.15,
      wallColor: '#e8e0d0', woodColor: '#6e5138', onigawara: false, karahafu: false,
    },
  },
  {
    id: 'temple',
    label: 'Temple Hall',
    sub: '日本 · irimoya + onigawara',
    patch: {
      style: 'irimoya', region: 'japan', tile: 'hongawara', gableFraction: 0.45,
      pitch: 0.55, sori: 0.3, cornerLift: 0.2, hipSori: 0.12, overhang: 1.25,
      roofColor: '#2e3238', ridgeColor: '#23262b', tileGlaze: 0.55,
      wallColor: '#efe6d4', woodColor: '#8a3d2b', onigawara: true, karahafu: false,
    },
  },
  {
    id: 'palace',
    label: 'Palace Hall · Xieshan',
    sub: '中国 · 歇山 + yellow glaze',
    patch: {
      style: 'irimoya', region: 'china', tile: 'hongawara', gableFraction: 0.4,
      pitch: 0.5, sori: 0.34, cornerLift: 0.3, hipSori: 0.16, overhang: 1.35,
      roofColor: '#c7982f', ridgeColor: '#a87c22', tileGlaze: 0.7,
      wallColor: '#b03a2e', woodColor: '#5e3b26', onigawara: true, karahafu: false,
    },
  },
  {
    id: 'hanok',
    label: 'Hanok · Ujin-gak',
    sub: '한국 · hip roof, gentle curve',
    patch: {
      style: 'yosemune', region: 'korea', tile: 'hongawara',
      pitch: 0.5, sori: 0.2, cornerLift: 0.12, hipSori: 0.09, overhang: 1.1,
      roofColor: '#3d444c', ridgeColor: '#31373e', tileGlaze: 0.25,
      wallColor: '#ece4d2', woodColor: '#7a5c42', onigawara: false, karahafu: false,
    },
  },
  {
    id: 'modern',
    label: 'Modern Machiya',
    sub: '和モダン · flat seam metal',
    patch: {
      style: 'kirizuma', region: 'japan', tile: 'modern',
      pitch: 0.35, sori: 0.04, cornerLift: 0.02, hipSori: 0.02, overhang: 0.6,
      roofColor: '#2b2e33', ridgeColor: '#1f2125', tileGlaze: 0.5,
      wallColor: '#ddd8cc', woodColor: '#4a4038', onigawara: false, karahafu: false,
    },
  },
];

/** Regional curve defaults applied when the region changes (style/tiles kept). */
export const REGION_CURVES: Record<Region, Partial<RoofParams>> = {
  japan: { sori: 0.24, cornerLift: 0.16, hipSori: 0.1, overhang: 1.0 },
  china: { sori: 0.34, cornerLift: 0.3, hipSori: 0.16, overhang: 1.3 },
  korea: { sori: 0.2, cornerLift: 0.12, hipSori: 0.09, overhang: 1.1 },
};

export const STYLE_META: Record<RoofStyle, { kanji: string; name: string; sub: string }> = {
  kirizuma: { kanji: '切妻', name: 'Kirizuma', sub: 'Gabled · 切妻造' },
  yosemune: { kanji: '寄棟', name: 'Yosemune', sub: 'Hipped · 寄棟造' },
  irimoya: { kanji: '入母屋', name: 'Irimoya', sub: 'Hip-and-gable · 入母屋造' },
  hogyo: { kanji: '宝形', name: 'Hōgyō', sub: 'Pyramidal · 宝形造' },
};
