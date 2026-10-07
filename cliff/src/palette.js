/*
 * Rock palettes.  Each preset is a full "look": strata colours (the horizontal
 * sedimentary bands exposed by the benches), ground colours, sun + sky.
 *
 * Colours are authored as hex and converted to linear-working space by three's
 * Color, so they light correctly and export correctly.
 */
import * as THREE from '../vendor/three.module.js';

export const hex = (h) => new THREE.Color(h);

export const PRESETS = {
  limestone: {
    label: 'Limestone quarry',
    params: {
      pitRadius: 34, pitDepth: 24, benchHeight: 5.5, benchWidth: 5.0,
      roughness: 0.85, bulge: 0.9, edgeFray: 0.65, bandThickness: 2.6,
      strataContrast: 1.0, rockDensity: 1.0, rockScale: 1.0,
    },
    // pale -> dark, cycled; a couple of shale partings break the rhythm
    strata: ['#d3cdbc', '#bdb6a4', '#a89f8c', '#8d8677', '#736d62', '#5d5951'],
    shaleEvery: 5,
    rockTint: ['#c4beb0', '#b0a999'],
    ground: { grass: '#78804c', dirt: '#7c6b4d', rock: '#8d887e', dust: '#a39a84' },
    sun: { azimuth: 128, elevation: 33, color: '#ffeacb', intensity: 3.1 },
    sky: { top: '#3d6ea6', horizon: '#cfd6d8' },
    fog: '#c6ced1',
    fogDensity: 0.0026,
  },
  sandstone: {
    label: 'Sandstone mesa',
    params: {
      pitRadius: 30, pitDepth: 28, benchHeight: 6.5, benchWidth: 6.0,
      roughness: 1.0, bulge: 1.0, edgeFray: 0.8, bandThickness: 3.0,
      strataContrast: 1.15, rockDensity: 1.15, rockScale: 1.1,
    },
    strata: ['#e6c79c', '#cd9663', '#dbb98c', '#a4713f', '#c99a6b', '#7d4a2c'],
    shaleEvery: 6,
    rockTint: ['#cfa376', '#b0824f'],
    ground: { grass: '#8a8250', dirt: '#8f6b45', rock: '#9c7c5c', dust: '#b09070' },
    sun: { azimuth: 105, elevation: 42, color: '#ffd9a8', intensity: 3.2 },
    sky: { top: '#4a72a8', horizon: '#e3c9a8' },
    fog: '#dcc4a6',
    fogDensity: 0.0030,
  },
  granite: {
    label: 'Granite pit',
    params: {
      pitRadius: 32, pitDepth: 22, benchHeight: 5.0, benchWidth: 4.5,
      roughness: 1.15, bulge: 0.75, edgeFray: 0.5, bandThickness: 5.0,
      strataContrast: 0.7, rockDensity: 1.3, rockScale: 1.15,
    },
    strata: ['#aca8a2', '#83838b', '#b3aea6', '#6c6d74', '#918a83', '#56575f'],
    shaleEvery: 7,
    rockTint: ['#8f8b86', '#75736f'],
    ground: { grass: '#5f6b46', dirt: '#6d6154', rock: '#7f7c78', dust: '#8d8a85' },
    sun: { azimuth: 145, elevation: 50, color: '#fff0dc', intensity: 3.0 },
    sky: { top: '#3a5f9c', horizon: '#c9cdd2' },
    fog: '#c2c7cc',
    fogDensity: 0.0024,
  },
  slate: {
    label: 'Slate quarry',
    params: {
      pitRadius: 28, pitDepth: 30, benchHeight: 6.0, benchWidth: 5.5,
      roughness: 0.95, bulge: 1.05, edgeFray: 0.75, bandThickness: 2.0,
      strataContrast: 1.25, rockDensity: 1.2, rockScale: 1.0,
    },
    strata: ['#93a0a7', '#6c7880', '#a3aeb4', '#5a656c', '#48525a', '#7d8a92'],
    shaleEvery: 4,
    rockTint: ['#79858c', '#616c74'],
    ground: { grass: '#5d6b4a', dirt: '#5f5a4e', rock: '#6f767c', dust: '#7d848a' },
    sun: { azimuth: 115, elevation: 44, color: '#e8ecff', intensity: 2.7 },
    sky: { top: '#54708f', horizon: '#b9c3c9' },
    fog: '#b4bec4',
    fogDensity: 0.0034,
  },
};

export const DEFAULT_PARAMS = {
  seed: 'quarry-01',
  preset: 'limestone',
  size: 190,
  terrainRes: 0.62,
  pitRadius: 34,
  pitDepth: 24,
  benchHeight: 5.5,
  benchWidth: 5.0,
  roughness: 0.85,
  bulge: 0.9,
  edgeFray: 0.65,
  bandThickness: 2.6,
  strataContrast: 1.0,
  rockDensity: 1.0,
  rockScale: 1.0,
  road: true,
  puddle: true,
  shadows: true,
  autoRotate: false,
};
