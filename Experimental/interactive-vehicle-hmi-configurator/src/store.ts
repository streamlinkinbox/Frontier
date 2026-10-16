import { create } from "zustand";
import { COLORS, WHEELS, MODES, ENVS } from "./data/config";

export type Finish = "gloss" | "matte";
export type Tab = "paint" | "wheels" | "lights" | "drive" | "scene";

export interface DragMat {
  kind: "paint" | "wheel";
  id: string;
  name: string;
  hex: string;
  x: number;
  y: number;
  droppable: boolean;
}

export interface ApplyReq {
  kind: "paint" | "wheel";
  id: string;
  ndc: [number, number] | null;
  nonce: number;
}

interface ConfigState {
  colorId: string;
  finish: Finish;
  wheelId: string;
  headlights: boolean;
  highBeams: boolean;
  fogLights: boolean;
  lightBar: boolean;
  lightBarColor: string;
  underglow: boolean;
  underglowColor: string;
  modeId: string;
  envId: string;
  scan: boolean;
  autoRotate: boolean;
  tab: Tab;
  // drag & drop materials
  dragMat: DragMat | null;
  applyReq: ApplyReq | null;
  // climate
  temp: number;
  fan: number;
  climateAuto: boolean;
  sync: boolean;
  set: (p: Partial<ConfigState>) => void;
}

export const useConfig = create<ConfigState>((set) => ({
  colorId: COLORS[0].id,
  finish: "gloss",
  wheelId: WHEELS[0].id,
  headlights: true,
  highBeams: false,
  fogLights: false,
  lightBar: true,
  lightBarColor: "#eaf4ff",
  underglow: false,
  underglowColor: "#38bdf8",
  modeId: MODES[0].id,
  envId: ENVS[0].id,
  scan: false,
  autoRotate: false,
  tab: "paint",
  dragMat: null,
  applyReq: null,
  temp: 21,
  fan: 4,
  climateAuto: true,
  sync: true,
  set: (p) => set(p),
}));

// selector helpers
export const getColor = (id: string) => COLORS.find((c) => c.id === id) ?? COLORS[0];
export const getWheel = (id: string) => WHEELS.find((w) => w.id === id) ?? WHEELS[0];
export const getMode = (id: string) => MODES.find((m) => m.id === id) ?? MODES[0];
export const getEnv = (id: string) => ENVS.find((e) => e.id === id) ?? ENVS[0];
