#!/usr/bin/env python3
"""Join compatible rectangular NURBS charts exactly; keep C0 knots for conforming exterior edge sewing.
This reduces face fragmentation, not a claim of Class-A continuity or a minimal control-pole layout.
"""
import argparse
from collections import Counter
from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from scipy.interpolate import BSpline


@dataclass
class Patch:
    Poles: np.ndarray
    KnotsU: np.ndarray
    KnotsV: np.ndarray
    Corners: np.ndarray
    Charts: dict

    def Rotate(self):
        Rotation = np.array([[0., -1., 1.], [1., 0., 0.], [0., 0., 1.]])
        return Patch(np.rot90(self.Poles), 1-self.KnotsV[::-1], self.KnotsU, np.rot90(self.Corners),
                     {Identity: Rotation@Transform for Identity, Transform in self.Charts.items()})

    def Normal(self, U):
        Parameters = np.linspace(.02, .98, 25)
        AlongU = BSpline(self.KnotsU, np.eye(self.Poles.shape[0]), 3)
        AlongV = BSpline(self.KnotsV, np.eye(self.Poles.shape[1]), 3)
        Du = np.einsum('i,ijc,aj->ac', AlongU(U, nu=1), self.Poles, AlongV(Parameters))
        Dv = np.einsum('i,ijc,aj->ac', AlongU(U), self.Poles, AlongV(Parameters, nu=1))
        Normals = np.cross(Du, Dv)
        Lengths = np.linalg.norm(Normals, axis=1)
        if np.any(Lengths < 1e-14):
            return None
        return Normals/Lengths[:, None]


def Join(A, B):
    Fraction = (A.Corners.shape[0]-1)/(A.Corners.shape[0]+B.Corners.shape[0]-2)
    Knots = np.r_[A.KnotsU[:-4]*Fraction, np.repeat(Fraction, 3), Fraction+B.KnotsU[4:]*(1-Fraction)]
    Left = np.diag([Fraction, 1., 1.])
    Right = np.diag([1-Fraction, 1., 1.]); Right[0, 2] = Fraction
    Charts = {Identity: Left@Transform for Identity, Transform in A.Charts.items()}
    Charts.update({Identity: Right@Transform for Identity, Transform in B.Charts.items()})
    return Patch(np.concatenate([A.Poles, B.Poles[1:]]), Knots, A.KnotsV,
                 np.concatenate([A.Corners, B.Corners[1:]]), Charts)


def Refinement(Knots, Target):
    Spline = BSpline(Knots, np.eye(len(Knots)-4), 3)
    Existing = Counter(Knots)
    for Knot, Multiplicity in Counter(Target).items():
        Missing = Multiplicity-Existing[Knot]
        if Missing > 0:
            Spline = Spline.insert_knot(Knot, Missing)
    return Spline.c


def BernsteinBound(Difference, AlongU, AlongV, Tolerance):
    Simple = float(np.linalg.norm(Difference, axis=-1).max())
    if Simple <= Tolerance:
        return Simple
    if Simple > Tolerance*100:
        return float('inf')
    Coefficients = np.einsum('ai,ijc,bj->abc', AlongU, Difference, AlongV, optimize=True)
    U = np.arange(0, len(AlongU)-1, 3)[:, None]+np.arange(4)
    V = np.arange(0, len(AlongV)-1, 3)[:, None]+np.arange(4)
    Cells = Coefficients[U[:, None, :, None], V[None, :, None, :]].reshape(-1, 4, 4, 3)
    Left = np.array([[1, 0, 0, 0], [.5, .5, 0, 0], [.25, .5, .25, 0], [.125, .375, .375, .125]])
    Right = Left[::-1, ::-1]
    Proven = 0.
    for _ in range(6):
        Bounds = np.linalg.norm(Cells, axis=-1).max(axis=(1, 2))
        Accepted = Bounds <= Tolerance
        if Accepted.any():
            Proven = max(Proven, float(Bounds[Accepted].max()))
        Cells = Cells[~Accepted]
        if not len(Cells):
            return Proven
        if np.linalg.norm(Cells[:, ::3, ::3], axis=-1).max() > Tolerance:
            return float('inf')
        Cells = np.concatenate([np.einsum('ai,pijc,bj->pabc', A, Cells, B, optimize=True)
                                for A in (Left, Right) for B in (Left, Right)])
    return float('inf')


def Reduce(P, Tolerance):
    OriginalPoles = P.Poles.copy()
    OriginalKnots = [P.KnotsU.copy(), P.KnotsV.copy()]
    P = deepcopy(P)
    Bezier = []
    for Knots in OriginalKnots:
        Target = np.r_[np.repeat(Knots[0], 4), np.repeat(np.unique(Knots[4:-4]), 3), np.repeat(Knots[-1], 4)]
        Bezier.append(Refinement(Knots, Target))
    for Axis in range(2):
        Knots = P.KnotsU if Axis == 0 else P.KnotsV
        Candidates = [K for K, N in Counter(Knots).items() if N == 1 and Knots[0] < K < Knots[-1]]
        for Knot in Candidates:
            Shorter = np.delete(Knots, np.flatnonzero(Knots == Knot)[0])
            Lift = Refinement(Shorter, Knots)
            Coefficients = np.moveaxis(P.Poles, Axis, 0)
            Reduced = np.linalg.lstsq(Lift, Coefficients.reshape(len(Knots)-4, -1), rcond=None)[0]
            Reduced = np.moveaxis(Reduced.reshape((len(Shorter)-4, *Coefficients.shape[1:])), 0, Axis)
            Ku, Kv = (Shorter, P.KnotsV) if Axis == 0 else (P.KnotsU, Shorter)
            Ru, Rv = Refinement(Ku, OriginalKnots[0]), Refinement(Kv, OriginalKnots[1])
            Restored = np.einsum('ai,ijc,bj->abc', Ru, Reduced, Rv, optimize=True)
            # Identical non-negative partition-of-unity bases: the maximum coefficient displacement
            # bounds displacement everywhere on the surface, not just at sampled parameters.
            if BernsteinBound(Restored-OriginalPoles, *Bezier, Tolerance) <= Tolerance:
                P.Poles = Reduced
                Knots = Shorter
                P.KnotsU, P.KnotsV = Ku, Kv
    Ru, Rv = Refinement(P.KnotsU, OriginalKnots[0]), Refinement(P.KnotsV, OriginalKnots[1])
    Restored = np.einsum('ai,ijc,bj->abc', Ru, P.Poles, Rv, optimize=True)
    return P, BernsteinBound(Restored-OriginalPoles, *Bezier, Tolerance)


def Consolidate(Source, Reference, Destination, AngleLimit, PoleTolerance):
    if not np.isfinite(AngleLimit) or not 0 < AngleLimit <= .5 or not np.isfinite(PoleTolerance) or not 0 < PoleTolerance <= 2e-7:
        raise ValueError('Use a positive angle up to 0.5 degrees and a reduction bound up to 0.0002 mm')
    Corners = np.load(Reference)['Corners']
    Patches = []
    for Line in Source.read_text().splitlines():
        if not Line.startswith('patch '):
            continue
        _, U, V, *_ = Line.split()
        U, V = int(U), int(V)
        if U != 11 or V != 11 or '--degree=3' not in Line or '--knots-' in Line:
            raise ValueError('This route requires the original uniform cubic cowl charts')
        Points = np.array([[float(X) for X in Token.split(',')] for Token in re.findall(r'\(([^)]+)\)', Line)])
        if not np.isfinite(Points).all():
            raise ValueError('Non-finite source poles')
        KnotsU = np.r_[np.zeros(4), np.arange(1, U-3)/(U-3), np.ones(4)]
        KnotsV = np.r_[np.zeros(4), np.arange(1, V-3)/(V-3), np.ones(4)]
        Identity = len(Patches)
        C = Corners[Identity]
        Patches.append(Patch(Points.reshape(U, V, 3), KnotsU, KnotsV, np.array([[C[0], C[3]], [C[1], C[2]]]), {Identity: np.eye(3)}))
    if len(Patches) != len(Corners):
        raise ValueError('Source document and reference chart counts differ')
    Original = deepcopy(Patches)
    Angles = []
    while True:
        Rotations = []
        Starts = {}
        for Identity, P in enumerate(Patches):
            for _ in range(4):
                Rotations.append((Identity, P))
                Starts.setdefault(tuple(P.Corners[0]), []).append((Identity, P))
                P = P.Rotate()
        Candidates = []
        for I, A in Rotations:
            for J, B in Starts.get(tuple(A.Corners[-1]), []):
                if I == J or len(A.Charts)+len(B.Charts)>16:
                    continue
                if A.KnotsV.shape != B.KnotsV.shape or not np.allclose(A.KnotsV, B.KnotsV, atol=1e-14, rtol=0):
                    continue
                if np.max(np.linalg.norm(A.Poles[-1]-B.Poles[0], axis=-1)) > 1e-9:
                    continue
                Na, Nb = A.Normal(1.), B.Normal(0.)
                if Na is None or Nb is None:
                    continue
                Angle = float(np.degrees(np.arccos(np.clip(np.sum(Na*Nb, axis=-1), -1, 1))).max())
                if Angle > AngleLimit:
                    continue
                Rows, Columns = A.Corners.shape[0]+B.Corners.shape[0]-2, A.Corners.shape[1]-1
                Score = (max(Rows, Columns)/min(Rows, Columns), -(len(A.Charts)+len(B.Charts)), I, J)
                Candidates.append((Score, I, J, A, B, Angle))
        if not Candidates:
            break
        _, I, J, A, B, Angle = min(Candidates, key=lambda C: C[0])
        Patches = [P for K, P in enumerate(Patches) if K not in (I, J)]+[Join(A, B)]
        Angles.append(Angle)
    Reduced = [Reduce(P, PoleTolerance) for P in Patches]
    Patches = [P for P, Bound in Reduced]
    ChangeBound = max(Bound for P, Bound in Reduced)
    Patches.sort(key=lambda P: min(P.Charts))
    Lines = ['# SolidArc native document v1', '# Liger cowl: consolidated charts with bounded knot reduction, metres; no caps or thickness.',
             'reset', 'require open-sew', 'require knot-skin']
    Queries = [None]*len(Original)
    MaximumDifference = 0.
    Parameters = np.linspace(0, 1, 33)
    U, V = np.meshgrid(Parameters, Parameters, indexing='ij')
    Coordinates = np.stack([U.ravel(), V.ravel(), np.ones(U.size)])
    for Identity, P in enumerate(Patches):
        Points = ' '.join('('+','.join(f'{X:.12f}' for X in Point)+')' for Point in P.Poles.reshape(-1, 3))
        Ku = ','.join(f'{X:.17g}' for X in P.KnotsU)
        Kv = ','.join(f'{X:.17g}' for X in P.KnotsV)
        Lines.append(f'patch {P.Poles.shape[0]} {P.Poles.shape[1]} {Points} --degree=3 --knots-u={Ku} --knots-v={Kv} --name=Cowl_{Identity:03d}')
        for Chart, Transform in P.Charts.items():
            Queries[Chart] = [Identity, *Transform[:2].ravel().tolist()]
            Mapped = Transform@Coordinates
            Bu = BSpline(P.KnotsU, np.eye(P.Poles.shape[0]), 3)(Mapped[0])
            Bv = BSpline(P.KnotsV, np.eye(P.Poles.shape[1]), 3)(Mapped[1])
            Sampled = np.einsum('ai,ijc,aj->ac', Bu, P.Poles, Bv)
            O = Original[Chart]
            Ou = BSpline(O.KnotsU, np.eye(O.Poles.shape[0]), 3)(Coordinates[0])
            Ov = BSpline(O.KnotsV, np.eye(O.Poles.shape[1]), 3)(Coordinates[1])
            Prior = np.einsum('ai,ijc,aj->ac', Ou, O.Poles, Ov)
            MaximumDifference = max(MaximumDifference, float(np.linalg.norm(Sampled-Prior, axis=-1).max()))
    if any(Q is None for Q in Queries) or MaximumDifference > PoleTolerance+1e-9:
        raise RuntimeError('Consolidation changed geometry or lost a reference chart')
    Lines.append('sew '+' '.join(f'Cowl_{I:03d}' for I in range(len(Patches)))+' --open --knot-edges --name=Liger_Front_Cowl')
    Details = {'sourceDocumentSha256': hashlib.sha256(Source.read_bytes()).hexdigest(),
               'sourceFaceCount': len(Original), 'consolidatedFaceCount': len(Patches),
               'sourcePoleCount': sum(P.Poles.shape[0]*P.Poles.shape[1] for P in Original),
               'consolidatedPoleCount': sum(P.Poles.shape[0]*P.Poles.shape[1] for P in Patches),
               'sampledChangeFromPriorMm': MaximumDifference*1000,
               'continuousChangeBoundMm': ChangeBound*1000,
               'maximumJoinedNormalAngleBeforeReductionDegrees': max(Angles, default=0),
               'limitations': ['Not a G1/G2 certificate', 'Cowl only', 'Original Blender deviation remains a sampled comparison'],
               'queries': 'One row per original chart: face index and two affine rows mapping original UV to the consolidated face'}
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Destination.write_text('\n'.join(Lines)+'\n')
    Destination.with_suffix('.queries').write_text('\n'.join(' '.join(str(X) for X in Q) for Q in Queries)+'\n')
    Destination.with_suffix('.consolidation.json').write_text(json.dumps(Details, indent=2)+'\n')
    print(json.dumps(Details, indent=2))


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    for Name in ['source', 'reference', 'destination']:
        Parser.add_argument(Name, type=Path)
    Parser.add_argument('--angle-limit', type=float, default=.5)
    Parser.add_argument('--pole-tolerance-mm', type=float, default=.0002)
    Args = Parser.parse_args()
    Consolidate(Args.source, Args.reference, Args.destination, Args.angle_limit, Args.pole_tolerance_mm*.001)
