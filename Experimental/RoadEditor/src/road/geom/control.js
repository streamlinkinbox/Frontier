// Traffic control resolution per junction approach.

export function nodeControl(J) {
  const N = J.node;
  if (J.kind === 'roundabout') return 'yield';
  if (J.kind !== 'junction') return 'none';
  if (N.control && N.control !== 'auto') return N.control;
  const ranks = J.ends.map(e => e.rank).sort((a, b) => b - a);
  if (ranks[0] <= 0) return 'none';
  if (J.ends.length < 3) return 'none';
  if (ranks[0] >= 5 && ranks[1] >= 4) return 'signal';
  if (ranks[0] >= 5) return 'twoWayStop';
  if (ranks[0] <= 2) return 'none';
  return 'allWayStop';
}

export function endControl(J, end, net) {
  const kind = nodeControl(J);
  const isPath = end.rank <= 0;
  const sidewalks = end.edge && (end.lay.hasCurbL || end.lay.hasCurbR);
  const res = { kind, signal: false, stop: false, yield: false, crosswalk: false };
  if (J.kind === 'continuation') { res.kind = 'continuation'; return res; }
  if (isPath) return res;
  if (kind === 'signal') { res.signal = true; res.crosswalk = sidewalks; }
  else if (kind === 'allWayStop') { res.stop = true; res.crosswalk = sidewalks; }
  else if (kind === 'twoWayStop') {
    const top = Math.max(...J.ends.map(e => e.rank));
    if (end.rank < top - 1) { res.stop = true; res.crosswalk = sidewalks; }
  } else if (kind === 'yield') { res.yield = true; res.crosswalk = false; }
  else if (kind === 'stop') { res.stop = true; res.crosswalk = sidewalks; }
  else if (kind === 'none' && J.kind === 'junction' && J.ends.length >= 3) { res.crosswalk = false; }
  return res;
}
