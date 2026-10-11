#!/usr/bin/env python3
"""Constrained cubic surface reconstruction with shared rims and held-out positional checks.
This is the reference-faithful starting skin, not a claim of a finished consolidated automotive surface layout.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.interpolate import BSpline


def Basis(Parameters, Count, Derivative=0):
    Knots = np.r_[np.zeros(4), np.arange(1, Count-3)/(Count-3), np.ones(4)]
    return BSpline(Knots, np.eye(Count), 3)(Parameters, nu=Derivative)


def Fit(Reference, Destination, Count):
    if Count < 4:
        raise ValueError('Cubic surfaces require at least four poles per direction')
    Content = np.load(Reference)
    Charts, Corners = Content['Charts'], Content['Corners']
    Extent = Charts.shape[1]
    Parameters = np.linspace(0, 1, Extent)
    Samples = Basis(Parameters, Count)
    Training = np.arange(0, Extent, 2)
    if len(Training) < Count:
        raise RuntimeError('Not enough training samples for independent validation')
    Curves, Incidence = {}, {}
    SurfacePoles = []
    for Identity, (Chart, Vertices) in enumerate(zip(Charts, Corners)):
        Poles = np.zeros((Count, Count, 3))
        # Follow the same physical edge in the same direction on both incident faces.
        Rims = [Chart[:, 0], Chart[-1], Chart[::-1, -1], Chart[0, ::-1]]
        for Side, Points in enumerate(Rims):
            Start, End = int(Vertices[Side]), int(Vertices[(Side+1)%4])
            Key = tuple(sorted((Start, End)))
            Incidence.setdefault(Key, []).append((Identity, Side))
            if Start > End:
                Points = Points[::-1]
            if Key not in Curves:
                Curve = np.empty((Count, 3))
                Curve[0], Curve[-1] = Points[0], Points[-1]
                Residual = Points[Training]-Samples[Training, :1]*Curve[0]-Samples[Training, -1:]*Curve[-1]
                Curve[1:-1] = np.linalg.lstsq(Samples[Training, 1:-1], Residual, rcond=None)[0]
                Curves[Key] = Curve
            Curve = Curves[Key] if Start < End else Curves[Key][::-1]
            if Side == 0: Poles[:, 0] = Curve
            elif Side == 1: Poles[-1] = Curve
            elif Side == 2: Poles[::-1, -1] = Curve
            else: Poles[0, ::-1] = Curve
        Projection = np.einsum('ai,bj->abij', Samples[Training], Samples[Training]).reshape(len(Training)**2, Count**2)
        Interior = np.array([I*Count+J for I in range(1, Count-1) for J in range(1, Count-1)])
        Residual = Chart[np.ix_(Training, Training)].reshape(-1, 3)-Projection@Poles.reshape(-1, 3)
        Poles.reshape(-1, 3)[Interior] = np.linalg.lstsq(Projection[:, Interior], Residual, rcond=None)[0]
        SurfacePoles.append(Poles)
    SurfacePoles = np.array(SurfacePoles)
    Fitted = np.einsum('ai,pijc,bj->pabc', Samples, SurfacePoles, Samples)
    Deviations = np.linalg.norm(Fitted-Charts, axis=-1)
    HeldOut = np.ones((Extent, Extent), dtype=bool)
    HeldOut[np.ix_(Training, Training)] = False
    Gaps = []
    for Key, Incident in Incidence.items():
        if len(Incident) > 2:
            raise RuntimeError('Non-manifold reference edge; do not silently repair it')
        if len(Incident) == 2:
            Rims = []
            for Identity, Side in Incident:
                Poles = SurfacePoles[Identity]
                Curve = [Poles[:, 0], Poles[-1], Poles[::-1, -1], Poles[0, ::-1]][Side]
                if Corners[Identity, Side] > Corners[Identity, (Side+1)%4]: Curve = Curve[::-1]
                Rims.append(Samples@Curve)
            Gaps.extend(np.linalg.norm(Rims[0]-Rims[1], axis=-1))
    # Orientation is compared inside every chart, not guessed from a radial direction about the car.
    Derivatives = Basis(Parameters[1:-1], Count, 1)
    Inner = Samples[1:-1]
    TangentU = np.einsum('ai,pijc,bj->pabc', Derivatives, SurfacePoles, Inner)
    TangentV = np.einsum('ai,pijc,bj->pabc', Inner, SurfacePoles, Derivatives)
    Normals = np.cross(TangentU, TangentV)
    ReferenceNormals = np.cross(Charts[:, 2:, 1:-1]-Charts[:, :-2, 1:-1], Charts[:, 1:-1, 2:]-Charts[:, 1:-1, :-2])
    Orientation = np.sum(Normals*ReferenceNormals, axis=-1)
    Valid = np.linalg.norm(ReferenceNormals, axis=-1)>1e-16
    Reversed = int(np.count_nonzero(Orientation[Valid] <= 0))
    Details = {'status': 'reference-faithful starting skin; consolidation and continuity review still required',
               'surfaceCount': len(Charts), 'degree': [3, 3], 'polesPerDirection': Count,
               'heldOutSampleCount': int(len(Charts)*HeldOut.sum()),
               'heldOutMaximumMm': float(Deviations[:, HeldOut].max()*1000),
               'heldOutRmsMm': float(np.sqrt(np.mean(Deviations[:, HeldOut]**2))*1000),
               'allSamplesMaximumMm': float(Deviations.max()*1000),
               'sharedRimMaximumGapMm': float(max(Gaps, default=0)*1000),
               'referenceBoundaryEdges': sum(len(Incident)==1 for Incident in Incidence.values()),
               'referenceInteriorEdges': sum(len(Incident)==2 for Incident in Incidence.values()),
               'reversedInteriorSamples': Reversed,
               'claimsNotMade': ['Final Class-A surface quality', 'G1/G2 continuity', 'Manufacturing solid', 'Complete vehicle conversion']}
    Destination.parent.mkdir(parents=True, exist_ok=True)
    print(json.dumps(Details, indent=2))
    if Reversed or Details['heldOutMaximumMm'] > 1.0 or Details['sharedRimMaximumGapMm'] > 0.001:
        raise RuntimeError('Surface reconstruction failed the positional/orientation acceptance gate')
    Details['reference'] = json.loads(Reference.with_suffix('.json').read_text())
    Destination.with_suffix('.metrics.json').write_text(json.dumps(Details, indent=2)+'\n')
    Lines = ['# SolidArc native document v1', '# Liger front cowl: cubic NURBS starting skin, metres, common vehicle coordinates.',
             '# Source reference and limitations are recorded in the adjacent metrics file.', 'reset', 'require open-sew']
    for Identity, Poles in enumerate(SurfacePoles):
        Coordinates = ' '.join('('+','.join(f'{Coordinate:.10f}' for Coordinate in Pole)+')' for Pole in Poles.reshape(-1, 3))
        Lines.append(f'patch {Count} {Count} {Coordinates} --degree=3 --name=Cowl_{Identity:03d}')
    Lines.append('sew '+' '.join(f'Cowl_{Identity:03d}' for Identity in range(len(Charts)))+' --open --name=Liger_Front_Cowl')
    Destination.write_text('\n'.join(Lines)+'\n')
    np.savez_compressed(Destination.with_suffix('.poles.npz'), Poles=SurfacePoles, Corners=Corners)


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('reference', type=Path)
    Parser.add_argument('destination', type=Path)
    Parser.add_argument('--poles', type=int, default=11)
    Arguments = Parser.parse_args()
    Fit(Arguments.reference, Arguments.destination, Arguments.poles)
