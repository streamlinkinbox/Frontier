// Physical material parameters. Gc (critical strain energy release rate,
// J/m^2) is the quantity that actually decides how finely something shatters:
// fragment area is bought with impact energy at a price of 2*Gc per m^2.
// Values are real-world orders of magnitude.
import { Vector3 } from 'three';

export type FractureMode = 'plate-brittle' | 'bulk-brittle' | 'ductile' | 'fibrous';

export interface MaterialDef {
  id: string;
  label: string;
  mode: FractureMode;
  density: number;        // kg/m^3
  Gc: number;             // J/m^2  (glass ~7, concrete ~120, wood‖ ~300, ABS ~5000)
  weibull: number;        // scatter of local toughness (flaw population)
  /** residual stored elastic energy (tempered glass!) in J/m^3 */
  storedEnergy: number;
  hertzCone: number;      // 0 = none, else half angle in radians
  coneEnergy: number;     // fraction of impact energy spent punching the cone
  anisoDir?: Vector3;     // grain / bedding direction, object space
  anisoRatio: number;     // Gc across-grain / along-grain
  beddingWeak: number;    // 0..1, how much weaker bedding planes are
  ringBias: number;       // 0..1 tendency to form concentric (ring) cracks
  jitter: number;         // crack-path wander, radians
  minFragment: number;    // m^3, below this a crack is arrested
  surface: { amp: number; freq: number; octaves: number; subdiv: number; stretch?: Vector3 };
  restitution: number;
  friction: number;
  dustPerJoule: number;
  color: number;
  interiorColor: number;
  roughness: number;
  metalness: number;
  transmission?: number;
  ior?: number;
  sound?: 'glass' | 'wood' | 'stone' | 'plastic';
}

const M = (m: MaterialDef) => m;

export const MATERIALS: Record<string, MaterialDef> = {
  glass: M({
    id: 'glass', label: 'Annealed glass pane', mode: 'plate-brittle',
    density: 2500, Gc: 7, weibull: 0.55, storedEnergy: 0,
    hertzCone: 1.15, coneEnergy: 0.16,
    anisoRatio: 1, beddingWeak: 0, ringBias: 0.42, jitter: 0.20,
    minFragment: 2.5e-7,
    surface: { amp: 0.0016, freq: 26, octaves: 3, subdiv: 1 },
    restitution: 0.12, friction: 0.28, dustPerJoule: 0.7,
    color: 0xcfe6ea, interiorColor: 0xbcd8de, roughness: 0.03, metalness: 0,
    transmission: 0.95, ior: 1.52, sound: 'glass',
  }),
  tempered: M({
    id: 'tempered', label: 'Tempered glass (dicing)', mode: 'plate-brittle',
    density: 2500, Gc: 7, weibull: 0.25, storedEnergy: 46000,
    hertzCone: 0, coneEnergy: 0,
    anisoRatio: 1, beddingWeak: 0, ringBias: 0.5, jitter: 0.75,
    minFragment: 9e-8,
    surface: { amp: 0.0008, freq: 40, octaves: 2, subdiv: 1 },
    restitution: 0.1, friction: 0.35, dustPerJoule: 1.4,
    color: 0xcfe6ea, interiorColor: 0xc8e2e8, roughness: 0.06, metalness: 0,
    transmission: 0.9, ior: 1.52, sound: 'glass',
  }),
  wood: M({
    id: 'wood', label: 'Pine plank (grain)', mode: 'fibrous',
    density: 520, Gc: 320, weibull: 0.7, storedEnergy: 0,
    hertzCone: 0, coneEnergy: 0,
    anisoDir: new Vector3(1, 0, 0), anisoRatio: 11, beddingWeak: 0, ringBias: 0.05, jitter: 0.12,
    minFragment: 5e-6,
    surface: { amp: 0.012, freq: 22, octaves: 4, subdiv: 2, stretch: new Vector3(0.12, 1.6, 1.6) },
    restitution: 0.2, friction: 0.55, dustPerJoule: 0.25,
    color: 0xb5854a, interiorColor: 0xd8b57d, roughness: 0.75, metalness: 0, sound: 'wood',
  }),
  concrete: M({
    id: 'concrete', label: 'Reinforced concrete', mode: 'bulk-brittle',
    density: 2350, Gc: 140, weibull: 0.9, storedEnergy: 0,
    hertzCone: 0.95, coneEnergy: 0.3,
    anisoRatio: 1, beddingWeak: 0, ringBias: 0.22, jitter: 0.33,
    minFragment: 8e-6,
    surface: { amp: 0.016, freq: 34, octaves: 4, subdiv: 2 },
    restitution: 0.12, friction: 0.8, dustPerJoule: 2.2,
    color: 0x9d9d97, interiorColor: 0x8d8b84, roughness: 0.95, metalness: 0, sound: 'stone',
  }),
  rock: M({
    id: 'rock', label: 'Granite boulder (bedded)', mode: 'bulk-brittle',
    density: 2700, Gc: 95, weibull: 0.8, storedEnergy: 0,
    hertzCone: 1.0, coneEnergy: 0.26,
    anisoDir: new Vector3(0, 1, 0), anisoRatio: 1, beddingWeak: 0.55, ringBias: 0.18, jitter: 0.28,
    minFragment: 1.2e-5,
    surface: { amp: 0.022, freq: 18, octaves: 5, subdiv: 2 },
    restitution: 0.1, friction: 0.9, dustPerJoule: 1.6,
    color: 0x7c7b78, interiorColor: 0x93897f, roughness: 0.92, metalness: 0.02, sound: 'stone',
  }),
  plastic: M({
    id: 'plastic', label: 'ABS crate (ductile)', mode: 'ductile',
    density: 1050, Gc: 460, weibull: 0.5, storedEnergy: 0,
    hertzCone: 0, coneEnergy: 0,
    anisoRatio: 1, beddingWeak: 0, ringBias: 0.1, jitter: 0.5,
    minFragment: 4e-5,
    surface: { amp: 0.02, freq: 9, octaves: 2, subdiv: 2 },
    restitution: 0.35, friction: 0.45, dustPerJoule: 0.05,
    color: 0xd8552f, interiorColor: 0xf0b9a4, roughness: 0.42, metalness: 0, sound: 'plastic',
  }),
};
