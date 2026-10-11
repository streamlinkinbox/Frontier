import { compileProfile } from '../core/profile';
import { ROAD_CLASS_DEFAULTS, type RoadClass } from '../core/model';

for (const cls of ['motorway', 'boulevard', 'avenue', 'street', 'local', 'lane'] as RoadClass[]) {
  const cp = compileProfile(ROAD_CLASS_DEFAULTS[cls]());
  const zones = cp.zones
    .map((z) => `${z.kind}[${z.from.toFixed(2)},${z.to.toFixed(2)}]`)
    .join(' ');
  console.log(`${cls.padEnd(10)} width ${(cp.bounds.right - cp.bounds.left).toFixed(2)} kerb ${cp.kerb.left.toFixed(2)}/${cp.kerb.right.toFixed(2)} | ${zones}`);
}
