/** Canonical Japanese roof styles (with Chinese / Korean equivalents). */
export type RoofStyle = 'kirizuma' | 'yosemune' | 'irimoya' | 'hogyo';
export type TileSystem = 'hongawara' | 'sangawara' | 'modern';
export type Region = 'japan' | 'china' | 'korea';
/** Ridge-end ornament: demon tile (JP), chiwen beast (CN), or none. */
export type Ornament = 'none' | 'onigawara' | 'chiwen';

export type LampDesign =
  | 'chochin-tube' | 'chochin-round' | 'kaku-chochin'
  | 'andon' | 'kiriko' | 'bonbori'
  | 'gongdeng' | 'zoumadeng' | 'chorong' | 'akari'
  | 'toro' | 'kasuga-toro' | 'yukimi-toro' | 'oribe-toro' | 'oki-toro'
  | 'rankei-toro' | 'pagoda-toro' | 'concrete-bollard'
  | 'disc-lantern' | 'melon-lantern' | 'barrel-lantern' | 'gourd-lantern'
  | 'hex-palace' | 'diamond-gongdeng' | 'roof-chochin';
/** Hanging = cords under the eaves · Standing = paper/wood floor row · Stone = garden lanterns */
export type LampMount = 'hanging' | 'standing' | 'stone';

/** Entrance: raised-floor platform with steps and/or an access ramp. */
export type EntryType = 'none' | 'steps' | 'ramp' | 'both';
export type StairMaterial = 'stone' | 'wood';

/** Traditional East Asian wooden signs: grand plaques, roof billboards, wall signs, garden posts. */
export type EavesPlaqueStyle = 'palace_gold' | 'natural_cedar' | 'vermilion';
export type EavesPlaqueText = 'taihedian' | 'tianxia' | 'fenghuang' | 'chashitsu' | 'daxiongbaodian';
export type RoofSignType = 'none' | 'ridge' | 'gable';
export type WallSignType = 'none' | 'bracket' | 'plank' | 'both';
export type GroundSignType = 'none' | 'all' | 'torii' | 'roofed_post' | 'bamboo_frame' | 'a_frame';

export interface LampGroup {
  enabled: boolean;
  design: LampDesign;
  mount: LampMount;
  /** lantern count in the row (1–8) */
  count: number;
  /** scale multiplier */
  size: number;
  paperColor: string;
  frameColor: string;
  /** emissive glow 0–3 */
  glow: number;
  /** characters drawn on the paper */
  text: string;
  /** hanging tassels (fángsuì 房穗) + top ring */
  tassels: boolean;
}

export const DEFAULT_LAMP_GROUP: LampGroup = {
  enabled: false,
  design: 'chochin-tube',
  mount: 'hanging',
  count: 3,
  size: 1,
  paperColor: '#c33b2a',
  frameColor: '#3a2c22',
  glow: 1.2,
  text: '祭',
  tassels: true,
};

export interface LampMeta {
  name: string;
  sub: string;
  cat: 'paper' | 'stone';
}

export const LAMP_META: Record<LampDesign, LampMeta> = {
  'chochin-tube': { name: 'Tube chōchin', sub: '筒提灯', cat: 'paper' },
  'chochin-round': { name: 'Round chōchin', sub: '丸提灯', cat: 'paper' },
  'kaku-chochin': { name: 'Square chōchin', sub: '角提灯', cat: 'paper' },
  andon: { name: 'Andon', sub: '行灯', cat: 'paper' },
  kiriko: { name: 'Tall kiriko', sub: '切子', cat: 'paper' },
  bonbori: { name: 'Bonbori', sub: '雪洞', cat: 'paper' },
  gongdeng: { name: 'Palace lantern', sub: '宫灯', cat: 'paper' },
  zoumadeng: { name: 'Carousel lantern', sub: '走马灯', cat: 'paper' },
  chorong: { name: 'Cheongsachorong', sub: '청사초롱', cat: 'paper' },
  akari: { name: 'Paper globe', sub: '明かり', cat: 'paper' },
  toro: { name: 'Stone tōrō', sub: '石灯籠', cat: 'stone' },
  'kasuga-toro': { name: 'Kasuga tōrō', sub: '春日灯籠', cat: 'stone' },
  'yukimi-toro': { name: 'Yukimi tōrō', sub: '雪見灯籠', cat: 'stone' },
  'oribe-toro': { name: 'Oribe tōrō', sub: '織部灯籠', cat: 'stone' },
  'oki-toro': { name: 'Oki tōrō', sub: '置き灯籠', cat: 'stone' },
  'rankei-toro': { name: 'Hanging tōrō', sub: '釣灯籠', cat: 'stone' },
  'pagoda-toro': { name: 'Pagoda lantern', sub: '塔灯籠', cat: 'stone' },
  'concrete-bollard': { name: 'Concrete bollard', sub: '現代', cat: 'stone' },
  'disc-lantern': { name: 'Flat disc', sub: '扁灯笼', cat: 'paper' },
  'melon-lantern': { name: 'Melon lantern', sub: '瓜灯', cat: 'paper' },
  'barrel-lantern': { name: 'Barrel lantern', sub: '桶灯', cat: 'paper' },
  'gourd-lantern': { name: 'Gourd lantern', sub: '葫芦灯', cat: 'paper' },
  'hex-palace': { name: 'Hex palace', sub: '六角宫灯', cat: 'paper' },
  'diamond-gongdeng': { name: 'Diamond lantern', sub: '菱形灯', cat: 'paper' },
  'roof-chochin': { name: 'Roof-top chōchin', sub: '屋根提灯', cat: 'paper' },
};

export const STONE_DESIGNS: LampDesign[] = [
  'toro', 'kasuga-toro', 'yukimi-toro', 'oribe-toro', 'oki-toro',
  'rankei-toro', 'pagoda-toro', 'concrete-bollard',
];

/** Stone designs that may genuinely hang (tsuri-dōrō hang from eaves). */
export const HANGABLE_STONE: LampDesign[] = ['rankei-toro'];

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
  /** Flying-eave wing sweep at the corners in meters (photos: 0.4–0.9). */
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
  /** Painted trim color (brackets etc.) */
  trimColor: string;
  /** 0 = matte unglazed, 1 = glossy glazed tile. */
  tileGlaze: number;
  showRafters: boolean;
  showStructure: boolean;
  showWalls: boolean;
  ornament: Ornament;
  /** Small wenshou beasts marching down the hip ridges. */
  hipBeasts: boolean;
  beastCount: number;
  /** Bracket sets (dougong-style) carrying the eaves. */
  dougong: boolean;
  /** Guardian beasts perched on the four wing corners. */
  cornerBeasts: boolean;
  /** Wind bells (fūrin) swaying under the four wing corners. */
  windBells: boolean;
  finial: boolean;
  eaveCaps: boolean;
  /** Cusped gable bargeboards (karahafu-style ogee curve) on gable styles. */
  karahafu: boolean;
  rafterSpacing: number;
  entryType: EntryType;
  /** Raised-floor platform height in meters. */
  floorHeight: number;
  /** Stair flight width in meters. */
  stairWidth: number;
  stairMaterial: StairMaterial;
  /** Handrails on steps + ramp. */
  entryRails: boolean;
  /** Ramp slope as 1:N run:rise (12 = barrier-free, 6 = steep). */
  rampSlope: number;
  /** Real point lights inside lantern groups (off = emissive only). */
  lampLights: boolean;
  lamps: LampGroup[];
  /** Traditional wooden signs (kanban, bian'e, tatefuda, torii). */
  signsEnabled: boolean;
  /** Front eaves / lintel grand plaque (bian'e / gaku). */
  showEavesPlaque: boolean;
  eavesPlaqueText: EavesPlaqueText;
  eavesPlaqueStyle: EavesPlaqueStyle;
  /** Custom characters for the eaves plaque (overrides preset if non-empty). */
  customEavesText: string;
  /** Roof-mounted sign: ridge billboard (yagura-kanban) or gable plaque. */
  roofSign: RoofSignType;
  /** Side-wall signs: wall-bracket projecting sign ("厠" restroom) and/or flat plank. */
  wallSigns: WallSignType;
  /** Freestanding ground / garden signs in front: torii gate, roofed tatefuda, bamboo framed, A-frame. */
  groundSigns: GroundSignType;
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
  trimColor: '#1f6f5e',
  tileGlaze: 0.35,
  showRafters: true,
  showStructure: true,
  showWalls: true,
  ornament: 'onigawara',
  hipBeasts: false,
  beastCount: 5,
  dougong: false,
  cornerBeasts: false,
  windBells: false,
  finial: true,
  eaveCaps: true,
  karahafu: false,
  rafterSpacing: 0.455,
  entryType: 'steps',
  floorHeight: 0.45,
  stairWidth: 1.5,
  stairMaterial: 'stone',
  entryRails: true,
  rampSlope: 8,
  lampLights: true,
  lamps: [
    { ...DEFAULT_LAMP_GROUP, enabled: true },
    { ...DEFAULT_LAMP_GROUP, design: 'toro', mount: 'stone', count: 2, size: 1.1, text: '' },
    { ...DEFAULT_LAMP_GROUP, design: 'andon', mount: 'hanging', count: 2, paperColor: '#f2e4c8', text: '酒' },
  ],
  signsEnabled: true,
  showEavesPlaque: true,
  eavesPlaqueText: 'taihedian',
  eavesPlaqueStyle: 'palace_gold',
  customEavesText: '',
  roofSign: 'ridge',
  wallSigns: 'both',
  groundSigns: 'all',
};

export const EAVES_PLAQUE_TEXT_META: Record<EavesPlaqueText, { kanji: string; name: string; meaning: string }> = {
  taihedian: { kanji: '太和殿', name: 'Taihedian', meaning: 'Hall of Supreme Harmony (Forbidden City)' },
  tianxia: { kanji: '天下第一', name: 'Tianxia Diyi', meaning: 'Number One Under Heaven' },
  fenghuang: { kanji: '鳳凰堂', name: 'Hōō-dō', meaning: 'Phoenix Hall (Byōdō-in Temple)' },
  chashitsu: { kanji: '喫茶去', name: 'Kissa-ko', meaning: 'Have a Cup of Tea (Zen Teahouse)' },
  daxiongbaodian: { kanji: '大雄寶殿', name: 'Daxiong Baodian', meaning: 'Great Hero Treasure Hall (Temple)' },
};

export const EAVES_PLAQUE_STYLE_META: Record<EavesPlaqueStyle, { name: string; sub: string }> = {
  palace_gold: { name: 'Palace Gold', sub: '黒漆金箔' },
  natural_cedar: { name: 'Natural Cedar', sub: '天然杉木' },
  vermilion: { name: 'Vermilion Red', sub: '朱漆金字' },
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
      wallColor: '#e8e0d0', woodColor: '#6e5138', ornament: 'none', karahafu: false,
      dougong: false, hipBeasts: false, cornerBeasts: false, windBells: false,
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
      wallColor: '#efe6d4', woodColor: '#8a3d2b', ornament: 'onigawara', karahafu: false,
      dougong: true, hipBeasts: false, cornerBeasts: false, windBells: true,
    },
  },
  {
    id: 'palace',
    label: 'Palace Hall · Xieshan',
    sub: '中国 · 歇山 + orange glaze',
    patch: {
      style: 'irimoya', region: 'china', tile: 'hongawara', gableFraction: 0.4,
      pitch: 0.5, sori: 0.34, cornerLift: 0.5, hipSori: 0.16, overhang: 1.35,
      roofColor: '#d2792b', ridgeColor: '#a85a1c', tileGlaze: 0.7,
      wallColor: '#b03a2e', woodColor: '#5e3b26', trimColor: '#1f6f70',
      ornament: 'chiwen', hipBeasts: true, beastCount: 5, dougong: true, karahafu: false,
      cornerBeasts: true, windBells: true,
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
      wallColor: '#ece4d2', woodColor: '#7a5c42', ornament: 'none', karahafu: false,
      dougong: false, hipBeasts: false, cornerBeasts: false, windBells: false,
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
      wallColor: '#ddd8cc', woodColor: '#4a4038', ornament: 'none', karahafu: false,
      dougong: false, hipBeasts: false, cornerBeasts: false, windBells: false,
    },
  },
];

/** Regional curve defaults applied when the region changes (style/tiles kept). */
export const REGION_CURVES: Record<Region, Partial<RoofParams>> = {
  japan: { sori: 0.24, cornerLift: 0.16, hipSori: 0.1, overhang: 1.0 },
  china: { sori: 0.34, cornerLift: 0.45, hipSori: 0.16, overhang: 1.3 },
  korea: { sori: 0.2, cornerLift: 0.12, hipSori: 0.09, overhang: 1.1 },
};

export const STYLE_META: Record<RoofStyle, { kanji: string; name: string; sub: string }> = {
  kirizuma: { kanji: '切妻', name: 'Kirizuma', sub: 'Gabled · 切妻造' },
  yosemune: { kanji: '寄棟', name: 'Yosemune', sub: 'Hipped · 寄棟造' },
  irimoya: { kanji: '入母屋', name: 'Irimoya', sub: 'Hip-and-gable · 入母屋造' },
  hogyo: { kanji: '宝形', name: 'Hōgyō', sub: 'Pyramidal · 宝形造' },
};
