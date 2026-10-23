/**
 * Standalone bark laboratory primitives.
 *
 * This module deliberately does not import the main vegetation generator. It
 * produces deterministic cluster placements and a height field that can be
 * inspected, baked and later ported into the production material pipeline.
 */

export type BarkProfileId = 'cork' | 'pine' | 'birch' | 'redwood';

export interface BarkProfile
{
    id: BarkProfileId;
    name: string;
    description: string;
    base: [number, number, number];
    dark: [number, number, number];
    light: [number, number, number];
    clusterShape: 'plate' | 'scale' | 'paper' | 'furrow';
    clusterCount: number;
    minHeight: number;
    maxHeight: number;
    minWidth: number;
    maxWidth: number;
    relief: number;
    thickness: number;
    rings: number;
    columns: number;
    mapScale: number;
}

export interface ClusterSpec
{
    angle: number;
    y: number;
    height: number;
    width: number;
    relief: number;
    thickness: number;
    lean: number;
    seed: number;
    row: number;
}

export const BARK_PROFILES: BarkProfile[] = [
    {
        id: 'cork',
        name: 'Cork Oak · plated',
        description: 'Deep vertical fissures between thick, irregular cork plates.',
        base: [0.24, 0.14, 0.075],
        dark: [0.055, 0.025, 0.012],
        light: [0.48, 0.31, 0.16],
        clusterShape: 'plate',
        clusterCount: 86,
        minHeight: 0.18,
        maxHeight: 0.52,
        minWidth: 0.16,
        maxWidth: 0.38,
        relief: 0.075,
        thickness: 0.045,
        rings: 4,
        columns: 3,
        mapScale: 1.25,
    },
    {
        id: 'pine',
        name: 'Pine · fibrous plates',
        description: 'Long, warm scales with narrow resinous furrows and broken edges.',
        base: [0.31, 0.16, 0.075],
        dark: [0.075, 0.026, 0.012],
        light: [0.62, 0.34, 0.13],
        clusterShape: 'scale',
        clusterCount: 118,
        minHeight: 0.12,
        maxHeight: 0.35,
        minWidth: 0.12,
        maxWidth: 0.3,
        relief: 0.045,
        thickness: 0.026,
        rings: 3,
        columns: 3,
        mapScale: 1.75,
    },
    {
        id: 'birch',
        name: 'Birch · paper bark',
        description: 'Pale horizontal sheets that curl and peel over dark seams.',
        base: [0.66, 0.64, 0.54],
        dark: [0.075, 0.055, 0.04],
        light: [0.92, 0.88, 0.72],
        clusterShape: 'paper',
        clusterCount: 104,
        minHeight: 0.055,
        maxHeight: 0.15,
        minWidth: 0.24,
        maxWidth: 0.62,
        relief: 0.028,
        thickness: 0.012,
        rings: 2,
        columns: 4,
        mapScale: 2.7,
    },
    {
        id: 'redwood',
        name: 'Redwood · furrowed plates',
        description: 'Tall red plates separated by soft, deeply shadowed vertical channels.',
        base: [0.29, 0.075, 0.032],
        dark: [0.045, 0.012, 0.008],
        light: [0.56, 0.20, 0.075],
        clusterShape: 'furrow',
        clusterCount: 72,
        minHeight: 0.3,
        maxHeight: 0.78,
        minWidth: 0.18,
        maxWidth: 0.4,
        relief: 0.09,
        thickness: 0.055,
        rings: 5,
        columns: 3,
        mapScale: 1.05,
    },
];

export function barkProfile(id: BarkProfileId): BarkProfile
{
    return BARK_PROFILES.find((profile) => profile.id === id) ?? BARK_PROFILES[0];
}

export function hash01(x: number, y: number, seed: number): number
{
    let n = Math.imul(x | 0, 0x1f123bb5) ^ Math.imul(y | 0, 0x5f356495) ^ Math.imul(seed | 0, 0x27d4eb2d);
    n = Math.imul(n ^ (n >>> 15), 0x85ebca6b);
    n ^= n >>> 13;
    return ((n >>> 0) & 0x00ffffff) / 0x01000000;
}

function smooth(t: number): number
{
    return t * t * (3 - 2 * t);
}

function valueNoise(x: number, y: number, seed: number): number
{
    const x0 = Math.floor(x);
    const y0 = Math.floor(y);
    const tx = smooth(x - x0);
    const ty = smooth(y - y0);
    const a = hash01(x0, y0, seed);
    const b = hash01(x0 + 1, y0, seed);
    const c = hash01(x0, y0 + 1, seed);
    const d = hash01(x0 + 1, y0 + 1, seed);
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
}

function fbm(x: number, y: number, seed: number): number
{
    let sum = 0;
    let amplitude = 0.5;
    let frequency = 1;
    for (let octave = 0; octave < 4; octave++)
    {
        sum += valueNoise(x * frequency, y * frequency, seed + octave * 101) * amplitude;
        amplitude *= 0.5;
        frequency *= 2;
    }
    return sum;
}

function clamp(value: number, lo: number, hi: number): number
{
    return Math.max(lo, Math.min(hi, value));
}

/** Normalised procedural bark height. u wraps around the trunk, v runs bottom to top. */
export function barkHeight(profile: BarkProfile, u: number, v: number, seed: number): number
{
    const n = fbm(u * profile.mapScale * 2.2, v * profile.mapScale * 3.0, seed);
    const fine = valueNoise(u * profile.mapScale * 18, v * profile.mapScale * 18, seed + 17);
    const angular = u - Math.floor(u);
    let height: number;

    switch (profile.id)
    {
        case 'cork':
        {
            const plates = 0.5 + 0.5 * Math.sin(angular * Math.PI * 2 * 13 + n * 2.8);
            const fissure = Math.pow(Math.abs(plates * 2 - 1), 5);
            height = 0.25 + n * 0.28 + fissure * 0.48 + fine * 0.12;
            break;
        }
        case 'pine':
        {
            const fibres = 0.5 + 0.5 * Math.sin(angular * Math.PI * 2 * 22 + n * 4 + v * 2);
            const broken = valueNoise(angular * 28, v * 5, seed + 61);
            height = 0.2 + fibres * 0.48 + broken * 0.22 + n * 0.12;
            break;
        }
        case 'birch':
        {
            const sheets = 0.5 + 0.5 * Math.sin(v * Math.PI * 2 * 28 + n * 2);
            const peel = valueNoise(angular * 7, v * 38, seed + 83);
            height = 0.22 + sheets * 0.28 + peel * 0.38 + fine * 0.08;
            break;
        }
        case 'redwood':
        {
            const ribs = 0.5 + 0.5 * Math.sin(angular * Math.PI * 2 * 10 + n * 1.7);
            const furrow = Math.pow(Math.abs(ribs * 2 - 1), 3.5);
            height = 0.18 + ribs * 0.48 + furrow * 0.28 + n * 0.15;
            break;
        }
    }
    return clamp(height, 0, 1);
}

export function barkColour(profile: BarkProfile, height: number, u: number, v: number, seed: number): [number, number, number]
{
    const variation = (fbm(u * 3.5, v * 4.5, seed + 211) - 0.5) * 0.16;
    const crack = clamp((0.38 - height) * 2.6, 0, 1);
    const ridge = clamp((height - 0.36) * 1.65, 0, 1);
    const r = profile.base[0] + (profile.dark[0] - profile.base[0]) * crack + (profile.light[0] - profile.base[0]) * ridge + variation;
    const g = profile.base[1] + (profile.dark[1] - profile.base[1]) * crack + (profile.light[1] - profile.base[1]) * ridge + variation * 0.72;
    const b = profile.base[2] + (profile.dark[2] - profile.base[2]) * crack + (profile.light[2] - profile.base[2]) * ridge + variation * 0.45;
    return [clamp(r, 0, 1), clamp(g, 0, 1), clamp(b, 0, 1)];
}

/** Deterministic, deliberately separate slabs that can later be baked to an atlas. */
export function makeClusterSpecs(profile: BarkProfile, seed: number, trunkHeight = 2.4): ClusterSpec[]
{
    const specs: ClusterSpec[] = [];
    const circumferential = Math.max(8, Math.round(profile.clusterCount / Math.max(1, trunkHeight * 1.2)));
    let index = 0;
    for (let row = 0; row < Math.ceil(trunkHeight / (profile.maxHeight * 0.78)); row++)
    {
        const y = row * profile.maxHeight * 0.78 - 0.05;
        const count = Math.max(6, circumferential + ((row * 7) % 5) - 2);
        for (let column = 0; column < count; column++)
        {
            const n0 = hash01(index, row, seed + 3);
            const n1 = hash01(index, row, seed + 19);
            const n2 = hash01(index, row, seed + 41);
            const angle = (column / count) * Math.PI * 2 + (row & 1 ? Math.PI / count : 0) + (n0 - 0.5) * 0.12;
            const height = profile.minHeight + n1 * (profile.maxHeight - profile.minHeight);
            const width = profile.minWidth + n2 * (profile.maxWidth - profile.minWidth);
            const shapeBias = profile.clusterShape === 'paper' ? 0.25 : profile.clusterShape === 'scale' ? 0.7 : 1;
            specs.push({
                angle,
                y: y + (n0 - 0.5) * profile.maxHeight * 0.22,
                height,
                width: width * (0.88 + n0 * 0.24),
                relief: profile.relief * (0.72 + n1 * 0.5),
                thickness: profile.thickness * (0.75 + n2 * 0.5),
                lean: (n1 - 0.5) * shapeBias * 0.18,
                seed: seed + index * 17,
                row,
            });
            index++;
        }
    }
    return specs;
}
