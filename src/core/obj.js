// OBJ export preserving QUAD faces (f a b c d) — import into Blender/Maya/
// Houdini and the quad topology comes along intact.

export function toOBJ(positions, colors, quads, name = 'tunnel_network') {
  const lines = [];
  lines.push(`# Frontier procedural tunnel network`);
  lines.push(`# ${positions.length / 3} vertices, ${quads.length} quads (all-quad topology)`);
  lines.push(`o ${name}`);
  for (let i = 0; i < positions.length; i += 3) {
    lines.push(`v ${positions[i].toFixed(5)} ${positions[i + 1].toFixed(5)} ${positions[i + 2].toFixed(5)}`);
  }
  for (const q of quads) {
    lines.push(`f ${q[0] + 1} ${q[1] + 1} ${q[2] + 1} ${q[3] + 1}`);
  }
  return lines.join('\n');
}

/** OBJ with vertex colours (v x y z r g b) — Blender/Meshlab read these. */
export function toOBJColored(positions, colors, quads, name = 'tunnel_network') {
  const lines = [];
  lines.push(`# Frontier procedural tunnel network`);
  lines.push(`# ${positions.length / 3} vertices, ${quads.length} quads (all-quad topology)`);
  lines.push(`o ${name}`);
  for (let i = 0; i < positions.length; i += 3) {
    const c = `${colors[i].toFixed(4)} ${colors[i + 1].toFixed(4)} ${colors[i + 2].toFixed(4)}`;
    lines.push(`v ${positions[i].toFixed(5)} ${positions[i + 1].toFixed(5)} ${positions[i + 2].toFixed(5)} ${c}`);
  }
  for (const q of quads) {
    lines.push(`f ${q[0] + 1} ${q[1] + 1} ${q[2] + 1} ${q[3] + 1}`);
  }
  return lines.join('\n');
}
