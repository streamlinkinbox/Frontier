// ---------------------------------------------------------------------------
// Frontier graph / types
// ---------------------------------------------------------------------------

import { ResolutionRequirement, ResolutionContext } from '../core/resolution';

export type PortType = 'world' | 'height' | 'sdf' | 'void' | 'mask' | 'layer' | 'splat';

export const PORT_COLORS: Record<PortType, string> = {
  world: '#8d939c',
  height: '#5ec26a',
  sdf: '#9d7bff',
  void: '#ff6b81',
  mask: '#ffa64d',
  layer: '#ff7ad9',
  splat: '#4dd7e8',
};

export interface PortDef {
  name: string;
  type: PortType;
  optional?: boolean;
}

export type ParamValue = number | string | boolean | [number, number, number];

export interface ParamDef {
  key: string;
  label: string;
  type: 'float' | 'int' | 'bool' | 'select' | 'color';
  min?: number;
  max?: number;
  step?: number;
  default: ParamValue;
  options?: { value: string; label: string }[];
  unit?: string;
}

export interface NodeDef {
  type: string;
  category: 'Generators' | 'SDF' | 'Erosion' | 'Texturing' | 'Output';
  label: string;
  subtitle: string;
  icon: string;              // emoji/glyph drawn in the node header circle
  inputs: PortDef[];
  outputs: PortDef[];
  params: ParamDef[];
  /** metres of smallest feature this node needs; null = resolution-agnostic */
  resRequirement?: (params: Record<string, ParamValue>, ctx: ResolutionContext) => ResolutionRequirement | null;
}

export interface GraphNode {
  id: string;
  type: string;
  x: number;
  y: number;
  params: Record<string, ParamValue>;
  muted?: boolean;
  collapsed?: boolean;
  locked?: boolean;
  hidden?: boolean;
  rename?: string;
}

export interface GraphEdge {
  id: string;
  from: string;
  fromPort: string;
  to: string;
  toPort: string;
}

export interface GraphDomain {
  size: [number, number, number];
  res: number;
  seed: number;
}

export interface Graph {
  nodes: GraphNode[];
  edges: GraphEdge[];
  domain: GraphDomain;
}
