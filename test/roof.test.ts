/**
 * roof.test.ts — generate every roof type and run the full verification.
 *   npx tsx test/roof.test.ts
 */
import { RoofField, type RoofType } from '../src/gen/roofField';
import { layTiles, tilePreset, estimateTileCount } from '../src/gen/tiles';
import { generateFrame, frameSpec } from '../src/gen/frame';
import { generateRidges, ridgeSpec } from '../src/gen/ridges';
import { verifyRoof, formatReport } from '../src/gen/verify';

const frame = frameSpec();
const surfaceLift = frame.rafterDiameter + 0.03;

function build(type: RoofType, width: number, depth: number): RoofField {
  return new RoofField({
    type,
    width,
    depth,
    riseRatio: 1 / 3,
    steps: 5,
    eaveProjection: 0.95,
    flyOverEave: 0.6,
    flyPitch: 1.2,
    hipInsetRatio: 1.0,
    breakT: 0.55,
    gableOverhang: 0.55,
    surfaceLift,
    cornerUpturn: 0.26,
    cornerRadius: 3.2,
  });
}

const tile = tilePreset('chinese');
let allPass = true;

for (const type of ['gable', 'hip', 'hipGable', 'pyramid'] as RoofType[]) {
  const width = type === 'pyramid' ? 7 : 8.4;
  const depth = type === 'pyramid' ? 7 : 6.2;
  const field = build(type, width, depth);
  const est = estimateTileCount(field, tile);
  const tiled = layTiles(field, tile);
  const fr = generateFrame(field, frame, tile.pitch);
  const rg = generateRidges(field, tile, ridgeSpec());

  const report = verifyRoof({
    field,
    tileSpec: tile,
    tiles: tiled.instances,
    frame: fr.instances,
    frameSpec: frame,
    ridges: rg,
    ridgeSpec: ridgeSpec(),
  });

  console.log('='.repeat(96));
  console.log(`ROOF TYPE: ${type}   ${width} × ${depth} m   (${tiled.instances.length} tiles, est ${est}${tiled.truncated ? ' TRUNCATED' : ''})`);
  console.log('='.repeat(96));
  console.log(formatReport(report));
  console.log('');
  allPass = allPass && report.pass;
}

console.log(allPass ? '✓ EVERY ROOF TYPE VERIFIED' : '✗ VERIFICATION FAILURES ABOVE');
process.exit(allPass ? 0 : 1);
