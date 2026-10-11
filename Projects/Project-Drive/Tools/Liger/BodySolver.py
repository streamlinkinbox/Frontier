#!/usr/bin/env python3
"""Fit the main-body reference with canonical shared cubic rims and adaptive tensor-product interiors.
Retain source three-face junction charts as explicit independent sheets, never delete or cap them.
This is a positional starting skin, not a smoothness or manufacturing certificate.
"""
import argparse
from collections import Counter
from copy import deepcopy
import hashlib
from fractions import Fraction
import json
from pathlib import Path
import numpy as np
from scipy.interpolate import BSpline
from PatchSequence import Patch, Join, Refinement


def Knots(Count):
    return np.r_[np.zeros(4), np.arange(1, Count-3)/(Count-3), np.ones(4)]


def Quantize(PatchSurface):
    # Chart widths are integer tile counts (at most 16); original knots use at most four spans.
    # Recover those rationals after floating-point rotations rather than inventing near-coincident knots.
    for Attribute in ('KnotsU', 'KnotsV'):
        Existing = getattr(PatchSurface, Attribute)
        Canonical = np.array([float(Fraction(float(Knot)).limit_denominator(256)) for Knot in Existing])
        if np.max(np.abs(Canonical-Existing)) > 1e-13:
            raise RuntimeError('Knot is not a supported chart rational')
        setattr(PatchSurface, Attribute, Canonical)
    return PatchSurface


def Sample(PatchSurface, Parameters):
    Along = BSpline(PatchSurface.KnotsU, np.eye(PatchSurface.Poles.shape[0]), 3)(Parameters)
    Across = BSpline(PatchSurface.KnotsV, np.eye(PatchSurface.Poles.shape[1]), 3)(Parameters)
    return np.einsum('ai,ijc,bj->abc', Along, PatchSurface.Poles, Across, optimize=True)


def Fit(Reference, Destination):
    Content = np.load(Reference)
    Charts, Corners = Content['Charts'], Content['Corners']
    Extent = Charts.shape[1]
    if Extent != 17:
        raise ValueError('This fitting route requires 17 samples per first-refinement chart')
    Parameters = np.linspace(0, 1, Extent)
    Training = np.arange(0, Extent, 2)
    HeldOut = np.ones((Extent, Extent), dtype=bool)
    HeldOut[np.ix_(Training, Training)] = False
    Bases = {Count: BSpline(Knots(Count), np.eye(Count), 3)(Parameters) for Count in (4, 5, 7)}
    Inverses = {Count: np.linalg.pinv(Bases[Count][Training, 1:-1]) for Count in Bases}
    Incidence, RimSamples = {}, {}
    for Identity, (Chart, Vertices) in enumerate(zip(Charts, Corners)):
        Rims = [Chart[:, 0], Chart[-1], Chart[::-1, -1], Chart[0, ::-1]]
        for Side, Points in enumerate(Rims):
            Start, End = int(Vertices[Side]), int(Vertices[(Side+1)%4])
            Key = tuple(sorted((Start, End)))
            Incidence.setdefault(Key, []).append((Identity, Side))
            Oriented = Points if Start < End else Points[::-1]
            if Key in RimSamples and np.max(np.linalg.norm(RimSamples[Key]-Oriented, axis=1)) > 1e-9:
                raise RuntimeError('Source charts disagree at a shared rim')
            RimSamples[Key] = Oriented
    Junctions = {Key: Faces for Key, Faces in Incidence.items() if len(Faces) > 2}
    Isolated = {Identity for Faces in Junctions.values() for Identity, Side in Faces}
    Curves = {}
    CurveMaximum = 0.
    for Key, Points in RimSamples.items():
        for Count in (4, 5, 7):
            Basis = Bases[Count]
            Curve = np.empty((Count, 3))
            Curve[0], Curve[-1] = Points[0], Points[-1]
            Residual = Points[Training]-Basis[Training, :1]*Curve[0]-Basis[Training, -1:]*Curve[-1]
            Curve[1:-1] = Inverses[Count]@Residual
            Error = float(np.linalg.norm(Basis@Curve-Points, axis=1).max())
            if Error <= 2e-5 or Count == 7:
                break
        if Error > .001:
            raise RuntimeError(f'Canonical rim exceeds 1 mm: {Key}: {Error*1000}')
        CurveMaximum = max(CurveMaximum, Error)
        Curves[Key] = Curve
    print('RIMS', len(Curves), 'MAXIMUM_MM', CurveMaximum*1000, 'POLE_COUNTS', dict(Counter(len(Curve) for Curve in Curves.values())), flush=True)
    Lifts = {(Source, Target): Refinement(Knots(Source), Knots(Target)) for Source in Bases for Target in Bases if Source <= Target}
    Patches = []
    Maximum, Squared, SampleCount, Reversals = 0., 0., 0, 0
    for Identity, (Chart, Vertices) in enumerate(zip(Charts, Corners)):
        Rims = []
        for Side in range(4):
            Start, End = int(Vertices[Side]), int(Vertices[(Side+1)%4])
            Curve = Curves[tuple(sorted((Start, End)))]
            Rims.append(Curve if Start < End else Curve[::-1])
        MinimumU = max(len(Rims[0]), len(Rims[2]))
        MinimumV = max(len(Rims[1]), len(Rims[3]))
        Choices = sorted(((CountU, CountV) for CountU in Bases for CountV in Bases if CountU >= MinimumU and CountV >= MinimumV), key=lambda Counts: (Counts[0]*Counts[1], Counts))
        ReferenceNormals = np.cross(Chart[2:, 1:-1]-Chart[:-2, 1:-1], Chart[1:-1, 2:]-Chart[1:-1, :-2])
        Valid = np.linalg.norm(ReferenceNormals, axis=-1) > 1e-16
        for CountU, CountV in Choices:
            Poles = np.zeros((CountU, CountV, 3))
            Poles[:, 0] = Lifts[len(Rims[0]), CountU]@Rims[0]
            Poles[-1] = Lifts[len(Rims[1]), CountV]@Rims[1]
            Poles[::-1, -1] = Lifts[len(Rims[2]), CountU]@Rims[2]
            Poles[0, ::-1] = Lifts[len(Rims[3]), CountV]@Rims[3]
            Along, Across = Bases[CountU], Bases[CountV]
            Residual = Chart[Training][:, Training]-np.einsum('ai,ijc,bj->abc', Along[Training], Poles, Across[Training], optimize=True)
            Poles[1:-1, 1:-1] = np.einsum('ia,abc,jb->ijc', Inverses[CountU], Residual, Inverses[CountV], optimize=True)
            Fitted = np.einsum('ai,ijc,bj->abc', Along, Poles, Across, optimize=True)
            Deviations = np.linalg.norm(Fitted-Chart, axis=-1)
            DerivativeU = BSpline(Knots(CountU), np.eye(CountU), 3)(Parameters[1:-1], nu=1)
            DerivativeV = BSpline(Knots(CountV), np.eye(CountV), 3)(Parameters[1:-1], nu=1)
            TangentU = np.einsum('ai,ijc,bj->abc', DerivativeU, Poles, Across[1:-1], optimize=True)
            TangentV = np.einsum('ai,ijc,bj->abc', Along[1:-1], Poles, DerivativeV, optimize=True)
            Reversed = int(np.count_nonzero(np.sum(np.cross(TangentU, TangentV)*ReferenceNormals, axis=-1)[Valid] <= 0))
            if Deviations.max() <= .0005 and Reversed == 0:
                break
        if Deviations.max() > .001 or Reversed:
            raise RuntimeError(f'Chart {Identity} rejected: maximum {Deviations.max()*1000} mm, reversed {Reversed}')
        Maximum = max(Maximum, float(Deviations[HeldOut].max()))
        Squared += float(np.sum(Deviations[HeldOut]**2))
        SampleCount += int(HeldOut.sum())
        Reversals += Reversed
        Patches.append(Patch(Poles, Knots(CountU), Knots(CountV), np.array([[Vertices[0], Vertices[3]], [Vertices[1], Vertices[2]]]), {Identity: np.eye(3)}))
        if Identity % 2000 == 0:
            print('FITTED', Identity, 'MAXIMUM_MM', Maximum*1000, flush=True)
    np.savez_compressed(Destination.with_suffix('.fit.npz'), **{f'Poles{Identity}': PatchSurface.Poles for Identity, PatchSurface in enumerate(Patches)})
    OriginalPoles = sum(PatchSurface.Poles.shape[0]*PatchSurface.Poles.shape[1] for PatchSurface in Patches)
    # Join disjoint pairs in batches: quadratic single-pair rescanning is inappropriate for the main body.
    Angles = []
    while True:
        Starts, Rotations = {}, []
        for Identity, PatchSurface in enumerate(Patches):
            if any(Chart in Isolated for Chart in PatchSurface.Charts):
                continue
            for Rotation in range(4):
                PatchSurface = Quantize(PatchSurface)
                Rotations.append((Identity, PatchSurface))
                Starts.setdefault(tuple(PatchSurface.Corners[0]), []).append((Identity, PatchSurface))
                PatchSurface = PatchSurface.Rotate()
        Used, Joined = set(), []
        for LeftIndex, Left in Rotations:
            if LeftIndex in Used:
                continue
            for RightIndex, Right in Starts.get(tuple(Left.Corners[-1]), []):
                if RightIndex == LeftIndex or RightIndex in Used or len(Left.Charts)+len(Right.Charts) > 16:
                    continue
                SharedCorners = Left.Corners[-1]
                if any(tuple(sorted((int(Start), int(End)))) in Junctions for Start, End in zip(SharedCorners, SharedCorners[1:])):
                    continue
                Multiplicities = Counter(Left.KnotsV) | Counter(Right.KnotsV)
                Transverse = np.array(sorted(Multiplicities.elements()))
                LeftLift, RightLift = Refinement(Left.KnotsV, Transverse), Refinement(Right.KnotsV, Transverse)
                if np.max(np.linalg.norm(LeftLift@Left.Poles[-1]-RightLift@Right.Poles[0], axis=1)) > 1e-9:
                    continue
                NormalLeft, NormalRight = Left.Normal(1.), Right.Normal(0.)
                if NormalLeft is None or NormalRight is None:
                    continue
                Angle = float(np.degrees(np.arccos(np.clip(np.sum(NormalLeft*NormalRight, axis=1), -1, 1))).max())
                if Angle > .5:
                    continue
                First, Second = deepcopy(Left), deepcopy(Right)
                First.Poles = np.einsum('bj,ijc->ibc', LeftLift, Left.Poles)
                Second.Poles = np.einsum('bj,ijc->ibc', RightLift, Right.Poles)
                First.KnotsV = Second.KnotsV = Transverse
                Joined.append(Quantize(Join(First, Second)))
                Angles.append(Angle)
                Used.update((LeftIndex, RightIndex))
                break
        if not Joined:
            break
        Patches = [PatchSurface for Identity, PatchSurface in enumerate(Patches) if Identity not in Used]+Joined
        print('CONSOLIDATED', len(Patches), flush=True)
    Patches.sort(key=lambda PatchSurface: min(PatchSurface.Charts))
    Lines = ['# SolidArc native document v1', '# Liger main body: positional starting skin, metres, original vehicle coordinates.',
             '# Source three-face junction charts remain separate sheets; no source area removed, no caps or thickness.',
             '# Not a G1/G2 or manufacturing certificate. See adjacent metrics and reference provenance.',
             'reset', 'require open-sew', 'require knot-skin']
    Queries, Sewn = [None]*len(Charts), []
    for Identity, PatchSurface in enumerate(Patches):
        Junction = any(Chart in Isolated for Chart in PatchSurface.Charts)
        Name = f'Main_Junction_{Identity:04d}' if Junction else f'Main_{Identity:04d}'
        Coordinates = ' '.join('('+','.join(f'{Coordinate:.12f}' for Coordinate in Point)+')' for Point in PatchSurface.Poles.reshape(-1, 3))
        Along = ','.join(f'{Knot:.17g}' for Knot in PatchSurface.KnotsU)
        Across = ','.join(f'{Knot:.17g}' for Knot in PatchSurface.KnotsV)
        Lines.append(f'patch {PatchSurface.Poles.shape[0]} {PatchSurface.Poles.shape[1]} {Coordinates} --degree=3 --knots-u={Along} --knots-v={Across} --name={Name}')
        if not Junction:
            Sewn.append(Name)
        for Chart, Transform in PatchSurface.Charts.items():
            Queries[Chart] = [Identity, int(Junction), *Transform[:2].ravel().tolist()]
    if any(Query is None for Query in Queries):
        raise RuntimeError('A source chart was lost during consolidation')
    Lines.append('sew '+' '.join(Sewn)+' --open --knot-edges --name=Liger_Main_Body')
    Destination.write_text('\n'.join(Lines)+'\n')
    Destination.with_suffix('.queries').write_text('\n'.join(' '.join(str(Coefficient) for Coefficient in Query) for Query in Queries)+'\n')
    Details = {'status': 'Positional starting skin; surface-layout refinement and source-junction resolution remain',
               'measuredOn': '2026-10-03', 'source': json.loads(Reference.with_suffix('.json').read_text()),
               'documentSha256': hashlib.sha256(Destination.read_bytes()).hexdigest(),
               'sourceCharts': len(Charts), 'surfaceCount': len(Patches), 'sourcePoleCount': OriginalPoles,
               'controlPoleCount': sum(PatchSurface.Poles.shape[0]*PatchSurface.Poles.shape[1] for PatchSurface in Patches),
               'heldOutSamples': SampleCount,
               'validationUse': 'Samples excluded from coefficient fitting, but used for adaptive pole-count selection and acceptance; not a blind test set', 'heldOutMaximumMm': Maximum*1000,
               'heldOutRmsMm': float(np.sqrt(Squared/SampleCount)*1000), 'reversedInteriorSamples': Reversals,
               'canonicalRimMaximumMm': CurveMaximum*1000, 'maximumJoinedNormalAngleDegrees': max(Angles, default=0),
               'junctionCharts': sorted(Isolated),
               'sourceThreeFaceRims': [{'corners': list(Key), 'charts': [Identity for Identity, Side in Faces]} for Key, Faces in Junctions.items()],
               'junctionTreatment': 'All incident charts are retained as independent editable sheets, not erased or silently welded',
               'claimsNotMade': ['Continuous Hausdorff bound', 'G1/G2 continuity', 'Manufacturing solid', 'Resolved source junctions']}
    Destination.with_suffix('.metrics.json').write_text(json.dumps(Details, indent=2)+'\n')
    print(json.dumps({Key: Value for Key, Value in Details.items() if Key not in ('source', 'sourceThreeFaceRims')}, indent=2), flush=True)


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('reference', type=Path)
    Parser.add_argument('destination', type=Path)
    Arguments = Parser.parse_args()
    Arguments.destination.parent.mkdir(parents=True, exist_ok=True)
    Fit(Arguments.reference, Arguments.destination)
