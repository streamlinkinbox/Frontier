#!/usr/bin/env python3
"""Project manually read review landmarks into native 3D spline guides; body geometry stays unchanged.
Landmarks refer to the user's annotated 1568x1096 four-view overview. They are approximate intent,
not a measured surface specification. Circular guides fit opening crowns in side elevation only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
from scipy.optimize import least_squares
from scipy.interpolate import PchipInterpolator


# Top-view strokes, read manually from the supplied annotation; one half is traced, then reflected.
DesignStrokes = [
    [[895, 731], [1134, 694], [1298, 694], [1337, 698], [1370, 715], [1396, 754], [1414, 823]],
    [[879, 769], [1119, 716], [1210, 717], [1255, 734], [1294, 770], [1310, 823]],
    [[879, 788], [1118, 766], [1208, 766], [1296, 769]],
]
RepairCentres = [[920, 653], [914, 698], [895, 954], [1143, 706], [1144, 769],
                 [1249, 766], [1155, 940], [1375, 696], [1448, 651], [1491, 711],
                 [1504, 928], [1458, 978], [1378, 954]]

SideRepairCentres = [[112, 814], [154, 783], [365, 781], [519, 811]]


def Pixel(Positions):
    return (np.asarray(Positions, dtype=float)-[784, 580])*(100/49)


def ProjectStroke(Triangles, Projection, Stroke):
    Projected = np.c_[Triangles.reshape(-1, 3), np.ones(Triangles.size//3)]@Projection
    Projected = Projected.reshape(-1, 3, 2)
    Lower, Upper = Projected.min(axis=1), Projected.max(axis=1)
    Result = []
    for Point in Stroke:
        # Screen-space bounding tests consider every triangle, so a small underside facet cannot hide a larger top face.
        Indices = np.flatnonzero(np.all((Point >= Lower-1e-8) & (Point <= Upper+1e-8), axis=1))
        Along = Projected[Indices, 1]-Projected[Indices, 0]
        Across = Projected[Indices, 2]-Projected[Indices, 0]
        Offset = Point-Projected[Indices, 0]
        Determinant = Along[:, 0]*Across[:, 1]-Along[:, 1]*Across[:, 0]
        Valid = np.abs(Determinant) > 1e-12
        U = np.zeros(len(Indices)); V = U.copy()
        U[Valid] = (Offset[Valid, 0]*Across[Valid, 1]-Offset[Valid, 1]*Across[Valid, 0])/Determinant[Valid]
        V[Valid] = (Along[Valid, 0]*Offset[Valid, 1]-Along[Valid, 1]*Offset[Valid, 0])/Determinant[Valid]
        Valid &= (U >= -1e-8) & (V >= -1e-8) & (U+V <= 1+1e-8)
        Found = None
        if Valid.any():
            Facets = Triangles[Indices[Valid]]
            Positions = Facets[:, 0]+U[Valid, None]*(Facets[:, 1]-Facets[:, 0])+V[Valid, None]*(Facets[:, 2]-Facets[:, 0])
            Found = Positions[np.argmax(Positions[:, 2])]
        if Found is None:
            raise RuntimeError(f'Design guide leaves the supplied surface near {Point}')
        Result.append(Found)
    return np.array(Result)


def Construct(Source, Measurements, Destination):
    Edges = json.loads(Measurements.read_text())
    Samples = np.array([Edge['points'] for Edge in Edges])
    Midpoints = Samples[:, 8, :3]
    Reflected = Midpoints.copy()
    Reflected[:, 1] *= -1
    Distances, Mirror = cKDTree(Midpoints).query(Reflected)
    if Distances.max() > 1e-7 or len(set(Mirror)) != len(Edges):
        raise RuntimeError('Native edge pairing is not a complete mirrored correspondence')
    Flat = Samples.reshape(-1, 7)
    Projection = np.linalg.lstsq(np.c_[Flat[:, :3], np.ones(len(Flat))], Flat[:, 3:5], rcond=None)[0]
    Triangles = np.fromfile(Measurements.with_suffix('.triangles.f64'), dtype='<f8').reshape(-1, 3, 3)
    Guides = []
    for Stroke in DesignStrokes:
        Stroke = Pixel(Stroke)
        Along = np.linspace(Stroke[0, 0], Stroke[-1, 0], 65)
        Across = PchipInterpolator(Stroke[:, 0], Stroke[:, 1])(Along)
        Guides.append(ProjectStroke(Triangles, Projection, np.column_stack([Along, Across])))
    Repair = set()
    Flat = Samples.reshape(-1, 7)
    Nearby = cKDTree(Flat[:, 3:5]).query(Samples[:, 8, 3:5], k=32)[1]
    Visible = Samples[:, 8, 2] >= Flat[Nearby, 2].max(axis=1)-.03
    for Centre in Pixel(RepairCentres):
        Distances = np.linalg.norm(Samples[:, :, 3:5]-Centre, axis=-1).min(axis=1)
        Repair.update(np.flatnonzero((Distances < 23) & Visible).tolist())
    SideNearby = cKDTree(Flat[:, 5:7]).query(Samples[:, 8, 5:7], k=32)[1]
    Outboard = np.abs(Samples[:, 8, 1]) >= np.abs(Flat[SideNearby, 1]).max(axis=1)-.03
    for Centre in (np.asarray(SideRepairCentres)-[0, 580])*(100/49):
        Distances = np.linalg.norm(Samples[:, :, 5:7]-Centre, axis=-1).min(axis=1)
        Repair.update(np.flatnonzero((Distances < 27) & Outboard).tolist())
    Repair.update(int(Mirror[Edge]) for Edge in list(Repair))
    Lines = [Source.read_text().rstrip(), 'require feature-curves',
             '# Independent editable feature copies. These are review guides, not completed surface repairs.']
    NamedGuides = []
    for Identity, Guide in enumerate(Guides):
        if Identity < 2:
            Guide[-1, 1] = 0.
            Opposite = Guide[-2::-1].copy()
            Opposite[:, 1] *= -1
            NamedGuides.append((f'Design_Rail_{Identity+1}', np.concatenate([Guide, Opposite])))
        else:
            for Sign in (-1, 1):
                Points = Guide.copy()
                Points[:, 1] *= Sign
                NamedGuides.append((f'Design_Rail_{Identity+1}_{"Left" if Sign > 0 else "Right"}', Points))
    for Name, Points in NamedGuides:
        Lines.append('spline '+' '.join('('+','.join(f'{Coordinate:.12f}' for Coordinate in Point)+')' for Point in Points)+' --degree=3 --name='+Name)
        Lines.append('feature design '+Name)
    if Repair:
        Lines.append('feature-copy Liger_Main_Body repair --edges='+','.join(str(Index) for Index in sorted(Repair))+' --name=Repair')
    Arches = []
    for Name, CentreX in [('Rear', -1.10), ('Front', 1.70)]:
        Chosen = [Index for Index, (Edge, Points) in enumerate(zip(Edges, Samples))
                  if Edge['open'] and Points[:, 1].mean() > 1.10 and abs(Points[:, 0].mean()-CentreX) < .5
                  and .56 < Points[:, 2].mean() < .9]
        Positions = Samples[Chosen, :, :3].reshape(-1, 3)
        Positions = Positions[(Positions[:, 2] > .56) & (Positions[:, 2] < .9)]
        if len(Positions) < 30:
            raise RuntimeError('Insufficient upper wheel-opening samples')
        XZ = Positions[:, [0, 2]]
        def Residual(Circle):
            return np.linalg.norm(XZ-Circle[:2], axis=1)-Circle[2]
        Fitted = least_squares(Residual, [CentreX, .4, .44], loss='soft_l1', f_scale=.001)
        if not Fitted.success or not np.isfinite(Fitted.x).all() or Fitted.x[2] <= 0:
            raise RuntimeError('Circular guide fit did not converge')
        Circle = Fitted.x
        Error = np.abs(Residual(Circle))
        if Error.max() > .01:
            raise RuntimeError('Opening crown is not close enough to a single circular reference')
        # A guide plane outside the sampled opening makes the comparison visible; it does not move the body.
        PlaneY = float(Positions[:, 1].max()+.006)
        for Sign in (-1, 1):
            Points = [[Circle[0]+Circle[2]*np.cos(Angle), Sign*PlaneY, Circle[1]+Circle[2]*np.sin(Angle)]
                      for Angle in np.radians([20, 90, 160])]
            Identity = f'{Name}_Opening_Circle_{"Left" if Sign > 0 else "Right"}'
            Lines.append('arc --three '+' '.join('('+','.join(f'{Coordinate:.12f}' for Coordinate in Point)+')' for Point in Points)+' --name='+Identity)
            Lines.append('feature circular '+Identity)
        Arches.append({'wheel': Name, 'centreXMetres': float(Circle[0]), 'centreZMetres': float(Circle[1]),
                       'radiusMetres': float(Circle[2]), 'sourceCrownFitMaximumMm': float(Error.max()*1000),
                       'sourceCrownFitRmsMm': float(np.sqrt(np.mean(Error**2))*1000), 'sampleCount': len(Positions),
                       'angularExtentDegrees': [20, 160], 'guidePlaneAbsYMetres': PlaneY,
                       'scope': 'Provisional inner opening crown only, in side elevation. Outer contour and lower fade are not rebuilt.'})
    Lines += ['show cages off', 'show iso off', 'show edges on', 'show features on']
    Destination.write_text('\n'.join(Lines)+'\n')
    Details = {'bodySource': Source.name, 'bodySourceSha256': hashlib.sha256(Source.read_bytes()).hexdigest(),
               'documentSha256': hashlib.sha256(Destination.read_bytes()).hexdigest(),
               'bodyChanged': False, 'designGuideCount': len(NamedGuides), 'repairEdges': sorted(Repair),
               'manuallyReadTopStrokes': DesignStrokes, 'manuallyReadRepairCentres': RepairCentres,
               'manuallyReadSideRepairCentres': SideRepairCentres,
               'annotationBasis': 'Manual reading of user-supplied four-view markup; screen locations are approximate intent',
               'circleGuides': Arches,
               'limitations': ['Guides are independent copies, not associative constraints',
                               'Circled regions not yet re-faired', 'Two-contour circular arch rebuilding and lower fade remain',
                               'Design guides interpolate manually read intent projected onto native 2 mm chord tessellation; not exact on-surface constraints']}
    Destination.with_suffix('.features.json').write_text(json.dumps(Details, indent=2)+'\n')
    print('DESIGN_GUIDES', len(NamedGuides), 'REPAIR_EDGES', len(Repair), 'CIRCLE_GUIDES', 4, flush=True)
    print(json.dumps(Arches, indent=2))


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    for Name in ('source', 'measurements', 'destination'):
        Parser.add_argument(Name, type=Path)
    Arguments = Parser.parse_args()
    Construct(Arguments.source, Arguments.measurements, Arguments.destination)
