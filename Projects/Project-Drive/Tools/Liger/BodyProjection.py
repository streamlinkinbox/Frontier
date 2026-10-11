#!/usr/bin/env python3
"""Sample the main body in one uninterrupted Blender subdivision, including semi-sharp creases.
Face-varying parameters identify first-refinement charts without restarting crease propagation.
The original file is never saved or modified. CPU geometry extraction only.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import bpy
import numpy as np
from scipy.spatial import cKDTree


def Project(Source, Supplied, Destination, Level):
    if not 3 <= Level <= 5:
        raise ValueError('Supported sampling levels are 3 through 5')
    bpy.ops.wm.open_mainfile(filepath=str(Source.resolve()))
    Original = bpy.data.objects['Body_Main_Shell']
    Subdivision = next(Modifier for Modifier in Original.modifiers if Modifier.type == 'SUBSURF')
    if not Subdivision.show_viewport or not Subdivision.use_limit_surface or Subdivision.levels != 2:
        raise RuntimeError('Unexpected main-body subdivision settings')
    if [Modifier.type for Modifier in Original.modifiers if Modifier.show_viewport] != ['MIRROR', 'SUBSURF']:
        raise RuntimeError('Unexpected enabled modifier sequence')
    Centre = float(Original.matrix_world.translation.y)
    Transform = np.array(Original.matrix_world)
    Geometry = bpy.data.meshes.new_from_object(Original.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    Geometry.calc_loop_triangles()
    Baseline = np.array([Original.matrix_world @ Vertex.co for Vertex in Geometry.vertices], dtype=np.float32)
    Triangles = np.array([list(Triangle.vertices) for Triangle in Geometry.loop_triangles], dtype=np.int32)
    Reference = np.load(Supplied)
    if not np.array_equal(Baseline, Reference['V']) or Triangles.shape != Reference['T'].shape:
        raise RuntimeError('Original evaluation differs from supplied main-body vertices or triangle count')
    Changed = np.flatnonzero(np.any(Triangles != Reference['T'], axis=1))
    ChangedPolygons = sorted({Geometry.loop_triangles[int(Slot)].polygon_index for Slot in Changed})
    Diagonals = []
    def Boundary(Facets):
        Edges = Counter((int(Start), int(End)) for Facet in Facets for Start, End in zip(Facet, np.roll(Facet, -1)))
        return sorted(Edge for Edge, Multiplicity in Edges.items() for _ in range(max(0, Multiplicity-Edges[Edge[::-1]])))
    for PolygonIndex in ChangedPolygons:
        Rows = [Triangle.index for Triangle in Geometry.loop_triangles if Triangle.polygon_index == PolygonIndex]
        if len(Rows) != 2 or Boundary(Triangles[Rows]) != Boundary(Reference['T'][Rows]):
            raise RuntimeError('Supplied triangle discrepancy is not an alternate quad diagonal')
        Quad = Baseline[list(Geometry.polygons[PolygonIndex].vertices)].astype(np.float64)
        Normal = np.cross(Quad[1]-Quad[0], Quad[2]-Quad[0])
        Departure = abs(np.dot(Quad[3]-Quad[0], Normal))/np.linalg.norm(Normal)*10
        Diagonals.append({'polygon': PolygonIndex, 'triangleRows': Rows, 'quadPlaneDepartureMm': float(Departure)})
    Baseline = Baseline.astype(np.float64)
    Baseline[:, 1] -= Centre
    Baseline *= .01
    bpy.data.meshes.remove(Geometry)
    Subdivision.show_viewport = False
    bpy.context.view_layer.update()
    Control = bpy.data.meshes.new_from_object(Original.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    # Preserve the original mirrored control coordinates, connectivity and both crease attributes.
    # Other display/selection attributes are not required for geometry or chart coordinates.
    Retained = {'position', '.edge_verts', '.corner_vert', '.corner_edge', 'crease_edge', 'crease_vert', 'material_index'}
    for Attribute in list(Control.attributes):
        if Attribute.name not in Retained:
            Control.attributes.remove(Attribute)
    Sampling = bpy.data.objects.new('LigerBodySampling', Control)
    Sampling.matrix_world = Original.matrix_world.copy()
    bpy.context.collection.objects.link(Sampling)
    Identity = Control.attributes.new('SolidArcPolygon', 'INT', 'FACE')
    Identity.data.foreach_set('value', np.arange(len(Control.polygons), dtype=np.int32))
    Counts = np.array([len(Polygon.vertices) for Polygon in Control.polygons], dtype=np.int32)
    Offsets = np.r_[0, np.cumsum(Counts)]
    Coordinates = Control.uv_layers.new(name='SolidArcParameters')
    Angles = np.concatenate([np.arange(Count)*2*np.pi/Count for Count in Counts])
    Coordinates.data.foreach_set('uv', np.column_stack([np.cos(Angles), np.sin(Angles)]).ravel())
    EdgeIndices = {tuple(sorted(Edge.vertices)): Edge.index+len(Control.vertices) for Edge in Control.edges}
    Corners = []
    for Polygon in Control.polygons:
        Vertices = list(Polygon.vertices)
        for Slot, Vertex in enumerate(Vertices):
            Corners.append([Vertex, EdgeIndices[tuple(sorted((Vertex, Vertices[(Slot+1)%len(Vertices)])))],
                            len(Control.vertices)+len(Control.edges)+Polygon.index,
                            EdgeIndices[tuple(sorted((Vertices[Slot-1], Vertex)))]])
    Creases = np.array([Entry.value for Entry in Control.attributes['crease_edge'].data])
    Modifier = Sampling.modifiers.new('SourceSubdivision', 'SUBSURF')
    for Attribute in ['quality', 'boundary_smooth', 'subdivision_type', 'use_limit_surface']:
        setattr(Modifier, Attribute, getattr(Subdivision, Attribute))
    Modifier.uv_smooth = 'NONE'
    Modifier.levels = Level
    bpy.context.view_layer.update()
    Evaluation = Sampling.evaluated_get(bpy.context.evaluated_depsgraph_get())
    Geometry = Evaluation.to_mesh()
    if any(Polygon.loop_total != 4 for Polygon in Geometry.polygons):
        raise RuntimeError('Subdivision did not yield quadrilateral samples')
    Positions = np.empty(len(Geometry.vertices)*3, dtype=np.float32)
    Geometry.vertices.foreach_get('co', Positions)
    Positions = Positions.reshape(-1, 3).astype(np.float64)
    Positions = Positions @ Transform[:3, :3].T + Transform[:3, 3]
    Positions[:, 1] -= Centre
    Positions *= .01
    Identities = np.empty(len(Geometry.polygons), dtype=np.int32)
    Geometry.attributes['SolidArcPolygon'].data.foreach_get('value', Identities)
    Indices = np.empty(len(Geometry.loops), dtype=np.int32)
    Geometry.loops.foreach_get('vertex_index', Indices)
    Parameters = np.empty(len(Geometry.loops)*2, dtype=np.float32)
    Geometry.uv_layers['SolidArcParameters'].data.foreach_get('uv', Parameters)
    Parameters = Parameters.reshape(-1, 4, 2)
    Extent = 2**(Level-1)+1
    Charts = np.full((Offsets[-1], Extent, Extent, 3), np.nan)
    ParameterError = 0.
    for Begin in range(0, len(Identities), 32768):
        End = min(Begin+32768, len(Identities))
        PolygonIndices = Identities[Begin:End]
        Parameters2 = Parameters[Begin:End].astype(np.float64)
        Centres = Parameters2.mean(axis=1)
        Sectors = np.rint(np.arctan2(Centres[:, 1], Centres[:, 0])*Counts[PolygonIndices]/(2*np.pi)).astype(np.int32)%Counts[PolygonIndices]
        ChartIndices = np.repeat(Offsets[PolygonIndices]+Sectors, 4)
        SectorCounts = np.repeat(Counts[PolygonIndices], 4)
        Angles = np.repeat(Sectors, 4)*2*np.pi/SectorCounts
        Steps = 2*np.pi/SectorCounts
        Corner = np.column_stack([np.cos(Angles), np.sin(Angles)])
        Along = (np.column_stack([np.cos(Angles+Steps), np.sin(Angles+Steps)])-Corner)*.5
        Across = (np.column_stack([np.cos(Angles-Steps), np.sin(Angles-Steps)])-Corner)*.5
        Mixed = -Corner-Along-Across
        Local = np.full((len(ChartIndices), 2), .5)
        for Iteration in range(12):
            Residual = Corner+Local[:, :1]*Along+Local[:, 1:]*Across+Local[:, :1]*Local[:, 1:]*Mixed-Parameters2.reshape(-1, 2)
            TangentU, TangentV = Along+Local[:, 1:]*Mixed, Across+Local[:, :1]*Mixed
            Determinant = TangentU[:, 0]*TangentV[:, 1]-TangentU[:, 1]*TangentV[:, 0]
            Local[:, 0] -= (Residual[:, 0]*TangentV[:, 1]-Residual[:, 1]*TangentV[:, 0])/Determinant
            Local[:, 1] -= (TangentU[:, 0]*Residual[:, 1]-TangentU[:, 1]*Residual[:, 0])/Determinant
        Quantized = np.rint(Local*(Extent-1)).astype(np.int32)
        ParameterError = max(ParameterError, float(np.max(np.abs(Local*(Extent-1)-Quantized))))
        if ParameterError > .001 or Quantized.min() < 0 or Quantized.max() >= Extent:
            raise RuntimeError('Ambiguous subdivision parameters')
        Projected = Positions[Indices[Begin*4:End*4]]
        Existing = Charts[ChartIndices, Quantized[:, 0], Quantized[:, 1]]
        Known = np.isfinite(Existing).all(axis=1)
        if Known.any() and np.max(np.linalg.norm(Existing[Known]-Projected[Known], axis=1)) > 1e-9:
            raise RuntimeError('Conflicting coordinates for a shared chart sample')
        Charts[ChartIndices, Quantized[:, 0], Quantized[:, 1]] = Projected
    if not np.isfinite(Charts).all():
        raise RuntimeError('Incomplete chart sampling')
    Stride = 2**(Level-2)
    Aligned = Charts[:, ::Stride, ::Stride].reshape(-1, 3)
    Mismatch = max(cKDTree(Aligned).query(Baseline)[0].max(), cKDTree(Baseline).query(Aligned)[0].max())
    if Mismatch > 2e-6:
        raise RuntimeError(f'Source sampling changed: {Mismatch*1000} mm')
    Details = {'sourceCommit': '72c69b61e70427f7351f7fc3393cd25af56591db',
               'sourceSha256': hashlib.sha256(Source.read_bytes()).hexdigest(),
               'suppliedSha256': hashlib.sha256(Supplied.read_bytes()).hexdigest(),
               'part': Original.name, 'blenderVersion': bpy.app.version_string,
               'sourceAgreement': 'Vertices match NPZ exactly; oriented quad boundaries retained where triangulation chooses another diagonal',
               'alternateDiagonals': Diagonals,
               'alignedSamplingMaximumMm': float(Mismatch*1000), 'metresPerSourceUnit': .01,
               'sourceMirrorCentreY': Centre, 'originalLevel': 2, 'samplingLevel': Level,
               'quality': Modifier.quality, 'boundarySmoothing': Modifier.boundary_smooth,
               'semiSharpMirroredEdges': int(np.count_nonzero((Creases > 0) & (Creases < 1))),
               'chartCount': len(Charts), 'samplesPerDirection': Extent,
               'parameterQuantizationMaximum': ParameterError,
               'sampling': 'Mirror evaluated first; a single original-control subdivision retains crease propagation',
               'scaleBasis': 'Supplied centimetre convention; physical scale is not independently established'}
    Destination.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(Destination, Charts=Charts, Corners=np.array(Corners, dtype=np.int32),
                        PolygonOffsets=Offsets, PolygonMaterials=np.array([Polygon.material_index for Polygon in Control.polygons]))
    Destination.with_suffix('.json').write_text(json.dumps(Details, indent=2)+'\n')
    print(json.dumps(Details, indent=2), flush=True)
    Evaluation.to_mesh_clear()


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('source', type=Path)
    Parser.add_argument('supplied', type=Path)
    Parser.add_argument('destination', type=Path)
    Parser.add_argument('--level', type=int, default=5)
    Arguments = Parser.parse_args()
    Project(Arguments.source, Arguments.supplied, Arguments.destination, Arguments.level)
