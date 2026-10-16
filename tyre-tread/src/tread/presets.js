import { createPattern } from './pattern.js';

/** Starter patterns, written as vector shapes. Pitch is the circumferential repeat length in mm. */
export const PRESET_PATTERNS = [
    {
        name: 'Circumferential channels + lateral sipes',
        pattern: createPattern({
            name: 'Channels + sipes',
            pitchMm: 240,
            mirror: true,
            shapes: [
                { kind: 'stroke', pts: [[0, -34], [240, -34]], closed: true, width: 8, depth: 1 },
                { kind: 'stroke', pts: [[0, 34], [240, 34]], closed: true, width: 8, depth: 1 },
                { kind: 'stroke', pts: [[30, -65], [60, -34], [60, 34], [30, 65]], width: 3, depth: 0.7 },
                { kind: 'stroke', pts: [[150, -65], [180, -34], [180, 34], [150, 65]], width: 3, depth: 0.7 },
            ],
        }),
    },
    {
        name: 'Chevron blocks',
        pattern: createPattern({
            name: 'Chevron',
            pitchMm: 200,
            mirror: false,
            shapes: [
                { kind: 'stroke', pts: [[0, -60], [100, -20], [200, -60]], closed: false, width: 9, depth: 1 },
                { kind: 'stroke', pts: [[0, 0], [100, 40], [200, 0]], closed: false, width: 9, depth: 1 },
                { kind: 'stroke', pts: [[0, 60], [100, 20], [200, 60]], closed: false, width: 9, depth: 1 },
            ],
        }),
    },
    {
        name: 'Dimples',
        pattern: createPattern({
            name: 'Dimples',
            pitchMm: 120,
            mirror: true,
            shapes: [
                { kind: 'dimple', c: [30, 25], r: 10, depth: 0.6 },
                { kind: 'dimple', c: [90, 25], r: 10, depth: 0.6 },
                { kind: 'dimple', c: [60, 60], r: 10, depth: 0.6 },
            ],
        }),
    },
];
