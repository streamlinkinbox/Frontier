export function pondDimensions(width,depth){
  if(!Number.isFinite(width)||!Number.isFinite(depth))throw new RangeError('Pond dimensions must be finite');
  return {width:Math.max(8,Math.min(20,Math.round(width))),depth:Math.max(6,Math.min(14,Math.round(depth)))};
}
// Resolution selects a physical spacing, not a fixed count stretched over the pond.
export function baseSamples(quality,depth){return Math.round((quality-1)*depth/8)+1;}
