#!/usr/bin/env python3
"""Extract connected sampling charts from Liger's actual Blender modifier geometry; never change the source file.
Run using Python with bpy 5.0.1 and numpy. Intended initially for Body_Front_Cowl, whose creases are binary.
"""
import argparse
import hashlib
import json
from pathlib import Path
import bpy
import numpy as np
from scipy.spatial import cKDTree


def Extract(Source, Destination, Part, Refinement):
    if Part != 'Body_Front_Cowl' or not 2 <= Refinement <= 6:
        raise ValueError('This route is validated for the cowl only, with refinement 2 through 6')
    bpy.ops.wm.open_mainfile(filepath=str(Source.resolve()))
    Original = bpy.data.objects[Part]
    # One coordinate convention for every part, not independently re-centred bounding boxes.
    Centre = float(bpy.data.objects['Body_Main_Shell'].matrix_world.translation.y)
    Subdivision = next(Modifier for Modifier in Original.modifiers if Modifier.type == 'SUBSURF')
    if not Subdivision.show_viewport:
        raise RuntimeError('Subdivision is disabled on this reference part; do not silently smooth its design')
    OriginalLevels = Subdivision.levels
    if not Subdivision.use_limit_surface or OriginalLevels > Refinement+1:
        raise RuntimeError('The reference limit surface and original sampling level must be retained')
    Baseline = bpy.data.meshes.new_from_object(Original.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    BaselinePositions = np.array([Original.matrix_world@Vertex.co for Vertex in Baseline.vertices], dtype=np.float64)
    BaselinePositions[:, 1] -= Centre
    BaselinePositions *= 0.01
    Creases = np.array([Entry.value for Entry in Original.data.attributes['crease_edge'].data])
    if np.any((Creases != 0.0) & (Creases != 1.0)):
        raise RuntimeError('Semi-sharp crease propagation is not validated by this extraction route')
    # One real Catmull-Clark refinement converts extraordinary polygons to quads. Do not project this
    # intermediate control cage to the limit surface before subdividing it again.
    Subdivision.levels = 1
    Subdivision.use_limit_surface = False
    bpy.context.view_layer.update()
    Intermediate = bpy.data.meshes.new_from_object(Original.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    if any(len(Polygon.vertices) != 4 for Polygon in Intermediate.polygons):
        raise RuntimeError('Sampling requires a quad chart after the first refinement')
    Sampling = bpy.data.objects.new('LigerSurfaceSampling', Intermediate)
    Sampling.matrix_world = Original.matrix_world.copy()
    bpy.context.collection.objects.link(Sampling)
    Identity = Intermediate.attributes.new('SolidArcChart', 'INT', 'FACE')
    Identity.data.foreach_set('value', np.arange(len(Intermediate.polygons), dtype=np.int32))
    Coordinates = Intermediate.uv_layers.new(name='SolidArcCoordinates')
    Coordinates.data.foreach_set('uv', np.tile([0., 0., 1., 0., 1., 1., 0., 1.], len(Intermediate.polygons)))
    Corners = np.array([list(Polygon.vertices) for Polygon in Intermediate.polygons], dtype=np.int32)
    Modifier = Sampling.modifiers.new('ReferenceRefinement', 'SUBSURF')
    Modifier.levels = Refinement
    Modifier.quality = Subdivision.quality
    Modifier.boundary_smooth = Subdivision.boundary_smooth
    Modifier.subdivision_type = Subdivision.subdivision_type
    Modifier.use_limit_surface = True
    Modifier.uv_smooth = 'NONE'
    bpy.context.view_layer.update()
    Evaluated = Sampling.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh()
    Positions = np.empty(len(Evaluated.vertices)*3, dtype=np.float32)
    Evaluated.vertices.foreach_get('co', Positions)
    Positions = Positions.reshape(-1, 3).astype(np.float64)
    Transform = np.array(Sampling.matrix_world)
    Positions = Positions @ Transform[:3, :3].T + Transform[:3, 3]
    Positions[:, 1] -= Centre
    Positions *= 0.01  # The supplied vehicle feature sheets and prior extraction declare centimetres.
    Identities = np.empty(len(Evaluated.polygons), dtype=np.int32)
    Evaluated.attributes['SolidArcChart'].data.foreach_get('value', Identities)
    Indices = np.empty(len(Evaluated.loops), dtype=np.int32)
    Evaluated.loops.foreach_get('vertex_index', Indices)
    Parameters = np.empty(len(Evaluated.loops)*2, dtype=np.float32)
    Evaluated.uv_layers['SolidArcCoordinates'].data.foreach_get('uv', Parameters)
    Parameters = np.rint(Parameters.reshape(-1, 2)*(2**Refinement)).astype(np.int32)
    Extent = 2**Refinement+1
    Charts = np.full((len(Corners), Extent, Extent, 3), np.nan)
    Charts[np.repeat(Identities, 4), Parameters[:, 0], Parameters[:, 1]] = Positions[Indices]
    if not np.isfinite(Charts).all():
        raise RuntimeError('Missing chart samples: refusing an incomplete surface')
    Stride = 2**(Refinement-OriginalLevels+1)
    Aligned = Charts[:, ::Stride, ::Stride].reshape(-1, 3)
    Forward = cKDTree(Aligned).query(BaselinePositions)[0]
    Backward = cKDTree(BaselinePositions).query(Aligned)[0]
    SourceMismatch = float(max(Forward.max(), Backward.max()))
    if SourceMismatch > 2e-6:
        raise RuntimeError(f'Sampling no longer reproduces the supplied Blender geometry: {SourceMismatch} metres')
    Destination.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(Destination, Charts=Charts, Corners=Corners)
    Details = {'part': Part, 'sourceSha256': hashlib.sha256(Source.read_bytes()).hexdigest(),
               'sourceCommit': '72c69b61e70427f7351f7fc3393cd25af56591db',
               'metresPerSourceUnit': 0.01, 'sourceMirrorCentreY': Centre,
               'scaleBasis': 'Supplied extraction scripts declare centimetres; Blender unit setting alone does not establish physical scale',
               'originalLevel': OriginalLevels, 'sourceAgreementMaximumMm': SourceMismatch*1000,
               'boundarySmoothing': Subdivision.boundary_smooth, 'subdivisionQuality': Subdivision.quality,
               'chartCount': len(Corners), 'samplesPerDirection': Extent,
               'sourceModifier': 'Catmull-Clark, binary edge creases, mirror preserved',
               'sampling': 'One non-limit refinement, then limit-projected refinement',
               'blenderVersion': bpy.app.version_string}
    Destination.with_suffix('.json').write_text(json.dumps(Details, indent=2)+'\n')
    print(json.dumps(Details, indent=2))


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('source', type=Path)
    Parser.add_argument('destination', type=Path)
    Parser.add_argument('--part', default='Body_Front_Cowl')
    Parser.add_argument('--refinement', type=int, default=5)
    Arguments = Parser.parse_args()
    Extract(Arguments.source, Arguments.destination, Arguments.part, Arguments.refinement)
