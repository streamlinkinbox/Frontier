#!/usr/bin/env python3
"""Measure native main-body samples against held-out Blender charts and pre-consolidation splines."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy.interpolate import BSpline


def Measure(Reference, Samples, Document, Fitted, Destination):
    Content = np.load(Reference)
    Charts, Corners = Content['Charts'], Content['Corners']
    Native = np.fromfile(Samples, dtype='<f8')
    if Native.size != Charts.size or not np.isfinite(Native).all():
        raise RuntimeError('Native samples are incomplete or non-finite')
    Native = Native.reshape(Charts.shape)
    Extent = Charts.shape[1]
    HeldOut = np.ones((Extent, Extent), dtype=bool)
    HeldOut[::2, ::2] = False
    Deviations = np.linalg.norm(Native-Charts, axis=-1)
    Rims, Gap = {}, 0.
    for Identity, Vertices in enumerate(Corners):
        for Side, Points in enumerate([Native[Identity, :, 0], Native[Identity, -1], Native[Identity, ::-1, -1], Native[Identity, 0, ::-1]]):
            Start, End = int(Vertices[Side]), int(Vertices[(Side+1)%4])
            Key = tuple(sorted((Start, End)))
            Points = Points if Start < End else Points[::-1]
            if Key in Rims:
                Gap = max(Gap, float(np.linalg.norm(Rims[Key]-Points, axis=1).max()))
            else:
                Rims[Key] = Points
    Normals = np.cross(Native[:, 2:, 1:-1]-Native[:, :-2, 1:-1], Native[:, 1:-1, 2:]-Native[:, 1:-1, :-2])
    ReferenceNormals = np.cross(Charts[:, 2:, 1:-1]-Charts[:, :-2, 1:-1], Charts[:, 1:-1, 2:]-Charts[:, 1:-1, :-2])
    Valid = np.linalg.norm(ReferenceNormals, axis=-1) > 1e-16
    Reversals = int(np.count_nonzero(np.sum(Normals*ReferenceNormals, axis=-1)[Valid] <= 0))
    Poles = np.load(Fitted)
    if len(Poles.files) != len(Charts):
        raise RuntimeError('Pre-consolidation chart count differs')
    Parameters = np.linspace(0, 1, Extent)
    Change = 0.
    for Identity in range(len(Charts)):
        Coefficients = Poles[f'Poles{Identity}']
        Bases = []
        for Count in Coefficients.shape[:2]:
            Knots = np.r_[np.zeros(4), np.arange(1, Count-3)/(Count-3), np.ones(4)]
            Bases.append(BSpline(Knots, np.eye(Count), 3)(Parameters))
        Before = np.einsum('ai,ijc,bj->abc', Bases[0], Coefficients, Bases[1], optimize=True)
        Change = max(Change, float(np.linalg.norm(Native[Identity]-Before, axis=-1).max()))
    Result = {'measuredOn': '2026-10-03', 'documentSha256': hashlib.sha256(Document.read_bytes()).hexdigest(),
              'reference': json.loads(Reference.with_suffix('.json').read_text()),
              'sampledCharts': len(Charts), 'samplesPerDirection': Extent,
              'heldOutSampleCount': int(len(Charts)*HeldOut.sum()),
               'validationUse': 'Samples excluded from coefficient fitting, but used for adaptive pole-count selection and acceptance; not a blind test set',
              'heldOutMaximumMm': float(Deviations[:, HeldOut].max()*1000),
              'heldOutRmsMm': float(np.sqrt(np.mean(Deviations[:, HeldOut]**2))*1000),
              'allSamplesMaximumMm': float(Deviations.max()*1000),
              'sharedRimMaximumMm': Gap*1000, 'reversedInteriorFiniteDifferenceSamples': Reversals,
              'nativeChangeFromPreconsolidationMm': Change*1000,
              'claimsNotMade': ['Continuous Hausdorff bound', 'G1/G2 continuity', 'Resolved source junctions', 'Manufacturing solid']}
    print(json.dumps(Result, indent=2), flush=True)
    if Result['allSamplesMaximumMm'] > 1 or Gap > 1e-6 or Reversals or Change > 1e-8:
        raise RuntimeError('Native body failed positional, shared-rim, orientation or consolidation checks')
    Destination.write_text(json.dumps(Result, indent=2)+'\n')


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    for Name in ('reference', 'samples', 'document', 'fitted', 'destination'):
        Parser.add_argument(Name, type=Path)
    Arguments = Parser.parse_args()
    Measure(Arguments.reference, Arguments.samples, Arguments.document, Arguments.fitted, Arguments.destination)
