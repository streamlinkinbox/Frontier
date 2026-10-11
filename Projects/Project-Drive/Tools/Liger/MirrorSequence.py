#!/usr/bin/env python3
"""Repartition the existing Liger skin on one half and reflect its actual spline topology.
Restriction and joining preserve the prior skin; a coefficient bound audits reflection changes.
No triangle decimation, hidden edges, caps, or invented thickness are used.
"""
import argparse
from collections import Counter
from copy import deepcopy
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from scipy.interpolate import BSpline
from scipy.spatial import cKDTree
from PatchSequence import Patch, Join, Refinement


def Canonical(Knots):
    Result = np.array([float(Fraction(float(Knot)).limit_denominator(4096)) for Knot in Knots])
    if np.max(np.abs(Result-Knots)) > 1e-12:
        raise ValueError('Unsupported non-rational chart knot')
    return Result


def Read(Source):
    Patches = []
    for Line in Source.read_text().splitlines():
        if not Line.startswith('patch '):
            continue
        Tokens = Line.split()
        Along, Across = int(Tokens[1]), int(Tokens[2])
        Poles = np.array([[float(Coordinate) for Coordinate in Point.split(',')]
                          for Point in re.findall(r'\(([^)]+)\)', Line)]).reshape(Along, Across, 3)
        Knots = [np.array([float(Knot) for Knot in re.search('--knots-'+Axis+'=([^ ]+)', Line)[1].split(',')])
                 for Axis in ('u', 'v')]
        if '--degree=3' not in Line or not np.isfinite(Poles).all():
            raise ValueError('Expected finite cubic spline poles')
        Patches.append(Patch(Poles, *Knots, np.zeros((2, 2), dtype=int), {}))
    return Patches


def Points(Surface, Coordinates):
    Along = BSpline(Surface.KnotsU, np.eye(Surface.Poles.shape[0]), 3)(Coordinates[0])
    Across = BSpline(Surface.KnotsV, np.eye(Surface.Poles.shape[1]), 3)(Coordinates[1])
    return np.einsum('ai,ijc,aj->ac', Along, Surface.Poles, Across, optimize=True)


def Restrict(Surface, Transform, Identity):
    Corners = np.array([[0, 1, 1, 0], [0, 0, 1, 1], [1, 1, 1, 1]])
    Mapped = Transform@Corners
    Lower, Upper = Mapped[:2].min(axis=1), Mapped[:2].max(axis=1)
    Result = deepcopy(Surface)
    for Axis, Attribute in enumerate(('KnotsU', 'KnotsV')):
        Knots = getattr(Result, Attribute)
        Low, High = Canonical(np.array([Lower[Axis], Upper[Axis]]))
        Coefficients = np.moveaxis(Result.Poles, Axis, 0)
        Spline = BSpline(Knots, Coefficients, 3)
        for Knot in (Low, High):
            Multiplicity = int(np.count_nonzero(np.abs(Spline.t-Knot) < 1e-12))
            if Multiplicity < 4:
                Spline = Spline.insert_knot(Knot, 4-Multiplicity)
        Begin = np.flatnonzero(np.abs(Spline.t-Low) < 1e-12)[0]
        End = np.flatnonzero(np.abs(Spline.t-High) < 1e-12)[-1]
        Result.Poles = np.moveaxis(Spline.c[Begin:End-3], 0, Axis)
        setattr(Result, Attribute, Canonical((Spline.t[Begin:End+1]-Low)/(High-Low)))
    Local = np.eye(3)
    Local[:2] = (Transform[:2]-np.column_stack([np.zeros((2, 2)), Lower]))/(Upper-Lower)[:, None]
    Rotation = np.array([[0., -1., 1.], [1., 0., 0.], [0., 0., 1.]])
    for _ in range(4):
        if np.max(np.abs(Local-np.eye(3))) < 1e-12:
            Result.Charts = {Identity: np.eye(3)}
            Result.KnotsU, Result.KnotsV = Canonical(Result.KnotsU), Canonical(Result.KnotsV)
            return Result
        Result = Result.Rotate()
        Local = Rotation@Local
    raise ValueError('Chart transform is not an orientation-preserving rectangle')


def Extract(Source):
    Surfaces = Read(Source)
    Queries = np.loadtxt(Source.with_suffix('.queries'))
    Charts = []
    Parameters = np.linspace(0, 1, 5)
    U, V = np.meshgrid(Parameters, Parameters, indexing='ij')
    Coordinates = np.stack([U.ravel(), V.ravel(), np.ones(U.size)])
    Maximum = 0.
    for Identity, Query in enumerate(Queries):
        Transform = np.eye(3)
        Transform[:2] = Query[2:].reshape(2, 3)
        Surface = Surfaces[int(Query[0])]
        Chart = Restrict(Surface, Transform, Identity)
        Maximum = max(Maximum, float(np.linalg.norm(Points(Surface, Transform@Coordinates)-Points(Chart, Coordinates), axis=1).max()))
        Charts.append(Chart)
    if Maximum > 1e-10:
        raise RuntimeError('Chart restriction changed prior geometry')
    Corners = np.array([Surface.Poles[[0, -1, -1, 0], [0, 0, -1, -1]] for Surface in Charts])
    Rounded, Inverse = np.unique(np.round(Corners.reshape(-1, 3), 9), axis=0, return_inverse=True)
    # Rounding is used only for adjacency identities, never for spline geometry.
    Tree = cKDTree(Rounded)
    if Tree.query_pairs(2e-10):
        raise RuntimeError('Corner quantization split a coincident vertex')
    for Surface, Indices in zip(Charts, Inverse.reshape(-1, 4)):
        Surface.Corners = Indices[[0, 3, 1, 2]].reshape(2, 2)
    print('EXTRACTED', len(Charts), 'RESTRICTION_MAXIMUM_MM', Maximum*1000, flush=True)
    return Surfaces, Charts, Queries, Maximum


def Reflect(Surface):
    Result = deepcopy(Surface)
    Result.Poles = Surface.Poles[::-1].copy()
    Result.Poles[:, :, 1] *= -1
    Result.KnotsU = Canonical(1-Surface.KnotsU[::-1])
    Result.KnotsV = Canonical(Surface.KnotsV)
    Result.Corners = Surface.Corners[::-1].copy()
    return Result


def DifferenceBound(First, Second):
    Knots = [np.array(sorted((Counter(A) | Counter(B)).elements()))
             for A, B in ((First.KnotsU, Second.KnotsU), (First.KnotsV, Second.KnotsV))]
    def Lift(Surface):
        Along = Refinement(Surface.KnotsU, Knots[0])
        Across = Refinement(Surface.KnotsV, Knots[1])
        return np.einsum('ai,ijc,bj->abc', Along, Surface.Poles, Across, optimize=True)
    # Common nonnegative partition-of-unity basis: a bound everywhere, not a sample maximum.
    return float(np.linalg.norm(Lift(First)-Lift(Second), axis=-1).max())


def Pair(Charts):
    Centres = np.array([Points(Surface, np.array([[.5], [.5], [1.]]))[0] for Surface in Charts])
    Positive = np.flatnonzero(Centres[:, 1] > 1e-9)
    Negative = np.flatnonzero(Centres[:, 1] < -1e-9)
    if len(Positive) != len(Negative) or len(Positive)+len(Negative) != len(Charts):
        raise RuntimeError('Expected complete, non-crossing reflected chart pairs')
    MirroredCentres = Centres[Positive].copy()
    MirroredCentres[:, 1] *= -1
    Distances, Slots = cKDTree(Centres[Negative]).query(MirroredCentres)
    if len(set(Slots)) != len(Slots) or Distances.max() > 2e-6:
        raise RuntimeError('Mirror correspondence is ambiguous or exceeds 0.002 mm')
    Pairs, Reverse, Bounds = {}, np.array([[-1., 0., 1.], [0., 1., 0.], [0., 0., 1.]]), []
    Rotation = np.array([[0., -1., 1.], [1., 0., 0.], [0., 0., 1.]])
    for FirstIndex, SecondIndex in zip(Positive, Negative[Slots]):
        First, Second = Charts[FirstIndex], Charts[SecondIndex]
        if First.Poles[:, :, 1].min() < -2e-8:
            raise RuntimeError('A source chart crosses the mirror plane')
        Mirrored = Reflect(First)
        CoordinateTransform = Reverse.copy()
        Candidates = []
        for _ in range(4):
            Candidates.append((np.max(np.linalg.norm(Mirrored.Poles[[0, -1, -1, 0], [0, 0, -1, -1]]-
                                                          Second.Poles[[0, -1, -1, 0], [0, 0, -1, -1]], axis=-1)),
                               deepcopy(Mirrored), CoordinateTransform.copy()))
            Mirrored = Mirrored.Rotate()
            Mirrored.KnotsU, Mirrored.KnotsV = Canonical(Mirrored.KnotsU), Canonical(Mirrored.KnotsV)
            CoordinateTransform = Rotation@CoordinateTransform
        Error, Mirrored, CoordinateTransform = min(Candidates, key=lambda Candidate: Candidate[0])
        Bound = DifferenceBound(Mirrored, Second)
        if Error > 2e-6 or Bound > 1e-5:
            raise RuntimeError(f'Reflection changes chart {SecondIndex} beyond 0.01 mm: {Bound*1000}')
        Bounds.append(Bound)
        Pairs[int(FirstIndex)] = (int(SecondIndex), CoordinateTransform)
    print('MIRROR_PAIRS', len(Pairs), 'CONTINUOUS_CHANGE_BOUND_MM', max(Bounds)*1000, flush=True)
    return Pairs, max(Bounds)


def RimNormals(Surface, Along, Parameters):
    BasisU = BSpline(Surface.KnotsU, np.eye(Surface.Poles.shape[0]), 3)
    BasisV = BSpline(Surface.KnotsV, np.eye(Surface.Poles.shape[1]), 3)
    TangentU = np.einsum('i,ijc,aj->ac', BasisU(Along, nu=1), Surface.Poles, BasisV(Parameters), optimize=True)
    TangentV = np.einsum('i,ijc,aj->ac', BasisU(Along), Surface.Poles, BasisV(Parameters, nu=1), optimize=True)
    Normals = np.cross(TangentU, TangentV)
    Lengths = np.linalg.norm(Normals, axis=1)
    return None if np.any(Lengths < 1e-14) else Normals/Lengths[:, None]


def Consolidate(Charts, Pairs, Isolated, ChartLimit, PoleLimit):
    Surfaces = [deepcopy(Charts[Identity]) for Identity in Pairs]
    Angles = []
    while True:
        Rotations, Starts = [], {}
        for Identity, Surface in enumerate(Surfaces):
            if any(Chart in Isolated for Chart in Surface.Charts):
                continue
            for _ in range(4):
                Surface.KnotsU, Surface.KnotsV = Canonical(Surface.KnotsU), Canonical(Surface.KnotsV)
                Rotations.append((Identity, Surface))
                Starts.setdefault(tuple(Surface.Corners[0]), []).append((Identity, Surface))
                Surface = Surface.Rotate()
        Candidates = []
        for LeftIndex, Left in Rotations:
            for RightIndex, Right in Starts.get(tuple(Left.Corners[-1]), []):
                if LeftIndex >= RightIndex or len(Left.Charts)+len(Right.Charts) > ChartLimit:
                    continue
                Rows, Columns = Left.Corners.shape[0]+Right.Corners.shape[0]-2, Left.Corners.shape[1]-1
                Transverse = np.array(sorted((Counter(Left.KnotsV) | Counter(Right.KnotsV)).elements()))
                PoleCount = (Left.Poles.shape[0]+Right.Poles.shape[0]-1)*(len(Transverse)-4)
                if PoleCount > PoleLimit:
                    continue
                AlongLeft, AlongRight = Refinement(Left.KnotsV, Transverse), Refinement(Right.KnotsV, Transverse)
                if np.max(np.linalg.norm(AlongLeft@Left.Poles[-1]-AlongRight@Right.Poles[0], axis=1)) > 1e-9:
                    continue
                Spans = np.unique(Transverse)
                # Inspect each polynomial span, not 25 points spread over an arbitrarily long joined rim.
                Parameters = (Spans[:-1, None]+np.diff(Spans)[:, None]*np.linspace(.001, .999, 25)).ravel()
                NormalLeft = RimNormals(Left, 1., Parameters)
                NormalRight = RimNormals(Right, 0., Parameters)
                if NormalLeft is None or NormalRight is None:
                    continue
                Angle = float(np.degrees(np.arccos(np.clip(np.sum(NormalLeft*NormalRight, axis=1), -1, 1))).max())
                if Angle > .5:
                    continue
                Score = (max(Rows, Columns)/min(Rows, Columns), -(Rows*Columns), PoleCount, LeftIndex, RightIndex)
                Candidates.append((Score, LeftIndex, RightIndex, Left, Right, Transverse, AlongLeft, AlongRight, Angle))
        Used, Joined = set(), []
        for _, LeftIndex, RightIndex, Left, Right, Transverse, AlongLeft, AlongRight, Angle in sorted(Candidates, key=lambda Entry: Entry[0]):
            if LeftIndex in Used or RightIndex in Used:
                continue
            First, Second = deepcopy(Left), deepcopy(Right)
            First.Poles = np.einsum('bj,ijc->ibc', AlongLeft, Left.Poles)
            Second.Poles = np.einsum('bj,ijc->ibc', AlongRight, Right.Poles)
            First.KnotsV = Second.KnotsV = Transverse
            Surface = Join(First, Second)
            Surface.KnotsU, Surface.KnotsV = Canonical(Surface.KnotsU), Canonical(Surface.KnotsV)
            Joined.append(Surface)
            Used.update((LeftIndex, RightIndex))
            Angles.append(Angle)
        if not Joined:
            break
        Surfaces = [Surface for Identity, Surface in enumerate(Surfaces) if Identity not in Used]+Joined
        print('HALF_SURFACES', len(Surfaces), flush=True)
    Surfaces.sort(key=lambda Surface: min(Surface.Charts))
    Complete, PairIndices = [], []
    Reverse = np.array([[-1., 0., 1.], [0., 1., 0.], [0., 0., 1.]])
    for Surface in Surfaces:
        Mirrored = Reflect(Surface)
        Mirrored.Charts = {Pairs[Chart][0]: Reverse@Transform@np.linalg.inv(Pairs[Chart][1])
                           for Chart, Transform in Surface.Charts.items()}
        PairIndices.append([len(Complete), len(Complete)+1])
        Complete.extend([Surface, Mirrored])
    return Complete, PairIndices, max(Angles, default=0.)


def Write(Surfaces, Isolated, Destination):
    Lines = ['# SolidArc native document v1', '# Liger main body: mirrored patch simplification, metres.',
             '# Exact chart restriction and joining; reflection change is bounded in adjacent metrics.',
             '# Junction sheets retained; no caps, thickness or manufacturing-solid claim.',
             'reset', 'require open-sew', 'require knot-skin']
    Queries, Sewn = {}, []
    for Identity, Surface in enumerate(Surfaces):
        Junction = any(Chart in Isolated for Chart in Surface.Charts)
        Name = f'Main_Junction_{Identity:04d}' if Junction else f'Main_{Identity:04d}'
        Coordinates = ' '.join('('+','.join(f'{Coordinate:.12f}' for Coordinate in Point)+')' for Point in Surface.Poles.reshape(-1, 3))
        Along = ','.join(f'{Knot:.17g}' for Knot in Surface.KnotsU)
        Across = ','.join(f'{Knot:.17g}' for Knot in Surface.KnotsV)
        Lines.append(f'patch {Surface.Poles.shape[0]} {Surface.Poles.shape[1]} {Coordinates} --degree=3 --knots-u={Along} --knots-v={Across} --name={Name}')
        if not Junction:
            Sewn.append(Name)
        for Chart, Transform in Surface.Charts.items():
            if Chart in Queries or np.linalg.det(Transform[:2, :2]) <= 0:
                raise RuntimeError('Repeated or reversed source chart')
            Queries[Chart] = [Identity, int(Junction), *Transform[:2].ravel().tolist()]
    if sorted(Queries) != list(range(len(Queries))):
        raise RuntimeError('Missing source chart')
    Lines.append('sew '+' '.join(Sewn)+' --open --knot-edges --name=Liger_Main_Body')
    Destination.write_text('\n'.join(Lines)+'\n')
    Destination.with_suffix('.queries').write_text('\n'.join(' '.join(str(Coefficient) for Coefficient in Queries[Chart]) for Chart in range(len(Queries)))+'\n')


def Run(Source, Destination, ChartLimit, PoleLimit):
    Prior, Charts, Queries, RestrictionError = Extract(Source)
    Isolated = set(np.flatnonzero(Queries[:, 1]))
    Pairs, ReflectionBound = Pair(Charts)
    if {Pairs[Chart][0] for Chart in Pairs if Chart in Isolated} != {Chart for Chart in Isolated if Chart not in Pairs}:
        raise RuntimeError('Junction sheets do not correspond under reflection')
    Surfaces, PairIndices, Angle = Consolidate(Charts, Pairs, Isolated, ChartLimit, PoleLimit)
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Write(Surfaces, Isolated, Destination)
    Details = {'measuredOn': '2026-10-03', 'sourceDocumentSha256': hashlib.sha256(Source.read_bytes()).hexdigest(),
               'documentSha256': hashlib.sha256(Destination.read_bytes()).hexdigest(),
               'priorSurfaceCount': len(Prior), 'surfaceCount': len(Surfaces), 'sewnFaceCount': len(Surfaces)-len(Isolated),
               'junctionSheets': len(Isolated), 'sourceCharts': len(Charts),
               'priorControlPoleCount': sum(Surface.Poles.shape[0]*Surface.Poles.shape[1] for Surface in Prior),
               'controlPoleCount': sum(Surface.Poles.shape[0]*Surface.Poles.shape[1] for Surface in Surfaces),
               'restrictionSampledMaximumMm': RestrictionError*1000,
               'reflectionContinuousChangeBoundMm': ReflectionBound*1000,
               'maximumJoinedSampledNormalAngleDegrees': Angle,
               'chartLimit': ChartLimit, 'poleLimit': PoleLimit, 'mirrorPlane': 'Y=0 metres',
               'mirrorSurfacePairs': PairIndices,
               'limitations': ['First simplification, not final panel layout', 'No G1/G2 certificate',
                               '24 source-junction sheets remain', 'Not a manufacturing solid',
                               'Original-source accuracy remains the prior sampled measurement, not a continuous bound']}
    Destination.with_suffix('.simplification.json').write_text(json.dumps(Details, indent=2)+'\n')
    print(json.dumps({Key: Content for Key, Content in Details.items() if Key != 'mirrorSurfacePairs'}, indent=2), flush=True)


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('source', type=Path)
    Parser.add_argument('destination', type=Path)
    Parser.add_argument('--chart-limit', type=int, default=128)
    Parser.add_argument('--pole-limit', type=int, default=4096)
    Arguments = Parser.parse_args()
    Run(Arguments.source, Arguments.destination, Arguments.chart_limit, Arguments.pole_limit)
