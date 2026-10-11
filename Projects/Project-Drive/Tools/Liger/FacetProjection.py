#!/usr/bin/env python3
"""Preserve the supplied, deliberately unsubdivided roof/glass frame as planar triangular CAD faces.
No smoothing, solidification or alternate triangulation is inferred. Not a smooth/Class-A frame redesign.
"""
import argparse
import hashlib
import json
from pathlib import Path
import bpy
import numpy as np


def Project(Source, Reference, Destination, Cowl=None):
    bpy.ops.wm.open_mainfile(filepath=str(Source.resolve()))
    Original = bpy.data.objects['Body_Roof_Glass_Frame']
    Modifiers = {Modifier.name: {'operation': Modifier.type, 'enabled': Modifier.show_viewport}
                 for Modifier in Original.modifiers}
    if any(Modifier.show_viewport and Modifier.type != 'MIRROR' for Modifier in Original.modifiers):
        raise RuntimeError('The supplied frame route requires its original mirror-only evaluation')
    Evaluated = Original.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh()
    Evaluated.calc_loop_triangles()
    Vertices = np.array([Original.matrix_world@Vertex.co for Vertex in Evaluated.vertices], dtype=np.float32)
    Triangles = np.array([list(Triangle.vertices) for Triangle in Evaluated.loop_triangles], dtype=np.int32)
    Supplied = np.load(Reference)
    if not np.array_equal(Vertices, Supplied['V']) or not np.array_equal(Triangles, Supplied['T']):
        raise RuntimeError('Blender evaluation differs from the supplied frame; refusing an unverified reference')
    Centre = float(bpy.data.objects['Body_Main_Shell'].matrix_world.translation.y)
    Vertices = Vertices.astype(np.float64)
    Vertices[:, 1] -= Centre
    Vertices *= .01
    Lines = ['# SolidArc native document v1', '# Liger roof/glass frame: original faceting retained; metres.',
             'reset', 'require open-sew']
    Positions = Vertices[Triangles]
    Normals = np.cross(Positions[:, 1]-Positions[:, 0], Positions[:, 2]-Positions[:, 0])
    if np.any(np.linalg.norm(Normals, axis=-1)<1e-12):
        raise RuntimeError('Degenerate reference triangle requires review')
    Edges = {}
    for Identity, (Indices, Points) in enumerate(zip(Triangles, Positions)):
        # A collapsed top edge represents a genuine triangle, not a fabricated fourth corner.
        Poles = Points[[0, 2, 1, 2]]
        Coordinates = ' '.join('('+','.join(f'{X:.12f}' for X in Point)+')' for Point in Poles)
        Lines.append(f'patch 2 2 {Coordinates} --degree=1 --name=Frame_{Identity:03d}')
        for A, B in zip(Indices, np.roll(Indices, -1)):
            Key = tuple(sorted((int(A), int(B))))
            Edges[Key] = Edges.get(Key, 0)+1
    Lines.append('sew '+' '.join(f'Frame_{I:03d}' for I in range(len(Triangles)))+' --open --name=Liger_Roof_Glass_Frame')
    Unique, Welded = np.unique(Vertices, axis=0, return_inverse=True)
    WeldedEdges = {}
    Directions = {}
    for Indices in Welded[Triangles]:
        for A, B in zip(Indices, np.roll(Indices, -1)):
            Key = tuple(sorted((int(A), int(B))))
            WeldedEdges[Key] = WeldedEdges.get(Key, 0)+1
            Directions.setdefault(Key, []).append(int(A) < int(B))
    Details = {'status': 'Reference-faithful faceted frame, not a smoothed reconstruction',
               'sourceCommit': '72c69b61e70427f7351f7fc3393cd25af56591db',
               'sourceSha256': hashlib.sha256(Source.read_bytes()).hexdigest(),
               'suppliedNpzSha256': hashlib.sha256(Reference.read_bytes()).hexdigest(),
               'sourceModifiers': Modifiers, 'metresPerSourceUnit': .01, 'sourceMirrorCentreY': Centre,
               'scaleBasis': 'Supplied centimetre extraction convention, not inferred from Blender unit settings',
               'sourceAgreement': 'Vertex coordinates and oriented triangle indices match the supplied NPZ exactly',
               'surfaceCount': len(Triangles), 'controlPoleCount': len(Triangles)*4,
               'referencedVertices': len(np.unique(Triangles)), 'unusedSourceVertices': len(Vertices)-len(np.unique(Triangles)),
               'referenceOpenEdges': sum(N == 1 for N in Edges.values()),
               'referenceNonManifoldEdges': sum(N > 2 for N in Edges.values()),
               'exactCoincidentVertexPairs': len(Vertices)-len(Unique),
               'weldedReferenceVertices': len(Unique),
               'weldedReferenceEdges': len(WeldedEdges),
               'weldedReferenceOpenEdges': sum(N == 1 for N in WeldedEdges.values()),
               'sewing': 'Join exactly coincident reference edges only; no opening bridged',
               'sourceMisorientedGeometricEdges': sum(len(Sides) == 2 and Sides[0] == Sides[1] for Sides in Directions.values()),
               'orientation': 'Native sewing repairs inconsistent source winding without moving any point',
               'referenceAreaM2': float(np.linalg.norm(Normals, axis=-1).sum()/2),
               'limitations': ['Faceting is retained intentionally', 'Main body not rebuilt', 'No invented thickness or glass infill']}
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Destination.write_text('\n'.join(Lines)+'\n')
    Destination.with_suffix('.triangles').write_text('\n'.join(' '.join(f'{X:.17g}' for X in Face.ravel()) for Face in Positions)+'\n')
    Details['documentSha256'] = hashlib.sha256(Destination.read_bytes()).hexdigest()
    Destination.with_suffix('.reference.json').write_text(json.dumps(Details, indent=2)+'\n')
    if Cowl:
        Existing = Cowl.read_text()
        if 'require knot-skin' not in Existing or '--name=Liger_Front_Cowl' not in Existing:
            raise ValueError('Assembly requires the consolidated cowl document')
        Commands = [Line for Line in Existing.splitlines() if not Line.startswith('#')]
        Commands += [Line for Line in Lines if not Line.startswith('#') and Line != 'reset']
        Combined = ['# SolidArc native document v1',
                    '# PARTIAL EXTERIOR: cowl and source-faceted roof/glass frame only. Main body is not rebuilt.', *Commands]
        Assembly = Destination.with_name('Liger_Exterior_Partial.arc')
        Assembly.write_text('\n'.join(Combined)+'\n')
        Assembly.with_suffix('.assembly.json').write_text(json.dumps({
            'status': 'Partial exterior only; main body remains unbuilt',
            'parts': [Cowl.name, Destination.name],
            'partSha256': [hashlib.sha256(Cowl.read_bytes()).hexdigest(), Details['documentSha256']],
            'documentSha256': hashlib.sha256(Assembly.read_bytes()).hexdigest(),
            'placement': 'Original shared vehicle coordinates; no per-part repositioning or added joins'}, indent=2)+'\n')
    print(json.dumps(Details, indent=2))


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    for Name in ['source', 'reference', 'destination']:
        Parser.add_argument(Name, type=Path)
    Parser.add_argument('--cowl', type=Path)
    Args = Parser.parse_args()
    Project(Args.source, Args.reference, Args.destination, Args.cowl)
