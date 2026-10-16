export interface ColorOption {
  id: string;
  name: string;
  hex: string;
  metallic: boolean;
}

export interface WheelOption {
  id: string;
  name: string;
  size: string;
  spokes: number;
  rim: string;
  machined?: boolean;
}

export interface ModeOption {
  id: string;
  name: string;
  accent: string;
  desc: string;
  power: string;
}

export interface EnvOption {
  id: string;
  name: string;
  preset:
    | "studio"
    | "city"
    | "night"
    | "sunset"
    | "warehouse"
    | "dawn"
    | "park";
  bg: string;
}

export const COLORS: ColorOption[] = [
  { id: "silver", name: "Nebula Silver", hex: "#c2c8d0", metallic: true },
  { id: "black", name: "Obsidian Black", hex: "#17191d", metallic: true },
  { id: "red", name: "Command Red", hex: "#8a1a1d", metallic: true },
  { id: "blue", name: "Tactical Blue", hex: "#1c3f73", metallic: true },
  { id: "sand", name: "Moab Sand", hex: "#b2946a", metallic: false },
  { id: "green", name: "Recon Green", hex: "#33442f", metallic: false },
  { id: "white", name: "Arctic White", hex: "#e7ebef", metallic: true },
  { id: "copper", name: "Copper Bronze", hex: "#8c5a2e", metallic: true },
];

export const WHEELS: WheelOption[] = [
  { id: "trail", name: "Trail Beadlock", size: '22"', spokes: 6, rim: "#2b2e33" },
  { id: "sport", name: "Machined Sport", size: '24"', spokes: 5, rim: "#c9ced4", machined: true },
  { id: "blackout", name: "Blackout Mesh", size: '22"', spokes: 10, rim: "#0e0f11" },
  { id: "bronze", name: "Bronze Forged", size: '24"', spokes: 7, rim: "#9a6a2f", machined: true },
];

export const MODES: ModeOption[] = [
  { id: "normal", name: "Normal", accent: "#38bdf8", desc: "Balanced road comfort", power: "625 hp" },
  { id: "terrain", name: "Terrain", accent: "#f59e0b", desc: "Off-road crawl & traction", power: "830 lb-ft" },
  { id: "sport", name: "Sport", accent: "#ef4444", desc: "Sharpened throttle & response", power: "0-60 3.0s" },
  { id: "watts", name: "Watts to Freedom", accent: "#a855f7", desc: "Max launch control", power: "1000 hp" },
];

export const ENVS: EnvOption[] = [
  { id: "studio", name: "Studio", preset: "studio", bg: "from-[#1b1e24] via-[#0c0e12] to-[#030405]" },
  { id: "night", name: "Night City", preset: "night", bg: "from-[#0b1426] via-[#070b16] to-[#010204]" },
  { id: "desert", name: "Desert Dusk", preset: "sunset", bg: "from-[#2b1a10] via-[#160b06] to-[#040202]" },
  { id: "showroom", name: "Showroom", preset: "warehouse", bg: "from-[#1a1c20] via-[#0e1013] to-[#040506]" },
];
