/**
 * Material database.
 *
 * Values are real engineering numbers (SI) wherever possible, because the
 * fracture solvers are driven by physics (Griffith energy balance, Rayleigh
 * wave speed, Weibull flaw statistics), not by art-directed magic constants.
 *
 *   E    Young's modulus                       [Pa]
 *   nu   Poisson's ratio                       [-]
 *   rho  density                               [kg/m^3]
 *   Gc   critical strain-energy release rate   [J/m^2]   (fracture energy)
 *   sigT tensile strength                      [Pa]
 *   m    Weibull modulus (flaw-size scatter)   [-]
 *
 * Derived at load: Rayleigh wave speed cR ~ 0.92 * sqrt(G/rho), which sets the
 * terminal crack speed. Dynamic cracks in brittle solids run at 0.4-0.6 cR and
 * micro-branch above ~0.4 cR (Fineberg/Sharon; Sundaram & Tippur for glass).
 */

export type MaterialId =
  | 'annealed-glass'
  | 'tempered-glass'
  | 'granite'
  | 'spruce'
  | 'abs-plastic'
  | 'acrylic'
  | 'concrete'
  | 'brick';

export interface Material {
  id: MaterialId;
  label: string;
  E: number;
  nu: number;
  rho: number;
  Gc: number;          // fracture energy, J/m^2
  sigT: number;        // tensile strength, Pa
  m: number;           // Weibull modulus
  /** Stored residual strain energy per unit volume [J/m^3] (thermal tempering). */
  residual: number;
  /** Fracture-energy anisotropy: cost multiplier across the "grain" axis. */
  anisotropy: number;
  /** Grain / bedding axis in object space (unit), only used if anisotropy != 1. */
  grain: [number, number, number];
  /** 0 = perfectly brittle, 1 = fully ductile (crack blunting, plastic hinge). */
  ductility: number;
  /** Fracture-surface roughness: 0 = mirror (glass), 1 = very rough (concrete). */
  roughness: number;
  /** Mean spacing of intrinsic flaws [m]; controls smallest natural fragment. */
  flawSpacing: number;
  /** Base albedo + render style hints. */
  color: [number, number, number];
  style: 'glass' | 'opaque' | 'plastic';
  specular: number;
  /** Restitution / friction for the rigid-body pass. */
  restitution: number;
  friction: number;
}

const M = (m: Material) => m;

export const MATERIALS: Record<MaterialId, Material> = {
  'annealed-glass': M({
    id: 'annealed-glass', label: 'Annealed glass (float)',
    E: 70e9, nu: 0.22, rho: 2500, Gc: 8, sigT: 45e6, m: 5,
    residual: 0, anisotropy: 1, grain: [0, 1, 0], ductility: 0,
    roughness: 0.04, flawSpacing: 0.05,
    color: [0.62, 0.74, 0.72], style: 'glass', specular: 1.0,
    restitution: 0.18, friction: 0.35,
  }),
  'tempered-glass': M({
    id: 'tempered-glass', label: 'Tempered glass (toughened)',
    E: 70e9, nu: 0.22, rho: 2500, Gc: 8, sigT: 170e6, m: 12,
    // Thermal tempering locks ~100 MPa surface compression in: the stored
    // elastic energy U = sigma^2/2E is released on failure and pays for a huge
    // amount of new surface -> "dicing" into thousands of ~cm cubes.
    residual: 7.0e4, anisotropy: 1, grain: [0, 1, 0], ductility: 0,
    roughness: 0.06, flawSpacing: 0.05,
    color: [0.62, 0.74, 0.72], style: 'glass', specular: 1.0,
    restitution: 0.22, friction: 0.4,
  }),
  granite: M({
    id: 'granite', label: 'Granite / rock',
    E: 55e9, nu: 0.25, rho: 2650, Gc: 70, sigT: 12e6, m: 9,
    residual: 0, anisotropy: 1.25, grain: [0, 1, 0], ductility: 0.02,
    roughness: 0.95, flawSpacing: 0.02,
    color: [0.42, 0.40, 0.38], style: 'opaque', specular: 0.35,
    restitution: 0.12, friction: 0.75,
  }),
  spruce: M({
    id: 'spruce', label: 'Spruce (wood, orthotropic)',
    E: 11e9, nu: 0.35, rho: 450, Gc: 300, sigT: 80e6, m: 6,
    residual: 0,
    // Splitting along the grain (RL/TL) costs ~300 J/m^2; breaking fibres
    // across the grain costs ~10x more. That ratio is why wood splinters.
    anisotropy: 10, grain: [1, 0, 0], ductility: 0.25,
    roughness: 0.8, flawSpacing: 0.015,
    color: [0.58, 0.40, 0.22], style: 'opaque', specular: 0.25,
    restitution: 0.25, friction: 0.6,
  }),
  'abs-plastic': M({
    id: 'abs-plastic', label: 'ABS plastic (ductile)',
    E: 2.2e9, nu: 0.35, rho: 1050, Gc: 5000, sigT: 42e6, m: 20,
    residual: 0, anisotropy: 1, grain: [0, 1, 0], ductility: 0.85,
    roughness: 0.45, flawSpacing: 0.06,
    color: [0.80, 0.30, 0.16], style: 'plastic', specular: 0.6,
    restitution: 0.35, friction: 0.5,
  }),
  acrylic: M({
    id: 'acrylic', label: 'Acrylic / PMMA (brittle plastic)',
    E: 3.2e9, nu: 0.37, rho: 1180, Gc: 400, sigT: 70e6, m: 14,
    residual: 0, anisotropy: 1, grain: [0, 1, 0], ductility: 0.25,
    roughness: 0.3, flawSpacing: 0.03,
    color: [0.85, 0.86, 0.9], style: 'plastic', specular: 0.8,
    restitution: 0.3, friction: 0.45,
  }),
  concrete: M({
    id: 'concrete', label: 'Concrete',
    E: 30e9, nu: 0.2, rho: 2400, Gc: 120, sigT: 3e6, m: 8,
    residual: 0, anisotropy: 1.1, grain: [0, 1, 0], ductility: 0.08,
    roughness: 1.0, flawSpacing: 0.025,
    color: [0.60, 0.59, 0.56], style: 'opaque', specular: 0.2,
    restitution: 0.1, friction: 0.8,
  }),
  brick: M({
    id: 'brick', label: 'Brick masonry',
    E: 16e9, nu: 0.2, rho: 1900, Gc: 60, sigT: 2e6, m: 7,
    residual: 0, anisotropy: 1, grain: [0, 1, 0], ductility: 0.05,
    roughness: 0.9, flawSpacing: 0.02,
    color: [0.52, 0.26, 0.19], style: 'opaque', specular: 0.18,
    restitution: 0.1, friction: 0.85,
  }),
};

/** Shear modulus. */
export const shearModulus = (m: Material) => m.E / (2 * (1 + m.nu));

/** Shear wave speed [m/s]. */
export const shearWaveSpeed = (m: Material) => Math.sqrt(shearModulus(m) / m.rho);

/**
 * Rayleigh (surface) wave speed [m/s] — the theoretical limiting crack speed.
 * Approximation cR/cs = (0.862 + 1.14 nu)/(1 + nu).
 */
export function rayleighSpeed(m: Material): number {
  return shearWaveSpeed(m) * (0.862 + 1.14 * m.nu) / (1 + m.nu);
}

/**
 * Griffith critical stress for a flaw of size a:  sigma_c = sqrt(E Gc / (pi a)).
 */
export function griffithStress(m: Material, a: number): number {
  return Math.sqrt((m.E * m.Gc) / (Math.PI * Math.max(a, 1e-6)));
}

/** Critical flaw size implied by the quoted tensile strength. */
export function criticalFlaw(m: Material): number {
  return (m.E * m.Gc) / (Math.PI * m.sigT * m.sigT);
}

/**
 * Micro-branching threshold: cracks become unstable and shed side branches
 * above ~0.4 cR in 3D brittle solids (Fineberg et al.).
 */
export const branchSpeed = (m: Material) => 0.4 * rayleighSpeed(m);
/** Practical terminal speed: glass tops out around 0.5-0.6 cR. */
export const terminalSpeed = (m: Material) => 0.6 * rayleighSpeed(m);
