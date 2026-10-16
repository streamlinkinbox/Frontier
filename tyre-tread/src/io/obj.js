/**
 * Wavefront OBJ writer for a quad mesh. Writes quads only ("f a b c d"), 1-based indices.
 * Positions are written in metres. Y-up convention is not applied: the tyre axis is +x, as in the generator.
 */
export function quadMeshToObj({ positions, quads }, { name = 'tyre' } = {}) {
    const lines = [`# quad-only tyre mesh`, `o ${name}`];
    const nv = positions.length / 3;
    for (let i = 0; i < nv; i++) {
        lines.push(`v ${positions[3 * i].toFixed(6)} ${positions[3 * i + 1].toFixed(6)} ${positions[3 * i + 2].toFixed(6)}`);
    }
    for (let f = 0; f < quads.length / 4; f++) {
        lines.push(`f ${quads[4 * f] + 1} ${quads[4 * f + 1] + 1} ${quads[4 * f + 2] + 1} ${quads[4 * f + 3] + 1}`);
    }
    return lines.join('\n') + '\n';
}
