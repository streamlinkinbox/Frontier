#!/usr/bin/env python3
"""Independently audit native replay samples, sewn rims and serialized mirror-paired spline poles."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from MirrorSequence import Read, Reflect


def Measure(PriorSamples, Samples, Document, Topology, Destination):
    Details = json.loads(Document.with_suffix('.simplification.json').read_text())
    Shape = (Details['sourceCharts'], 17, 17, 3)
    Prior = np.memmap(PriorSamples, dtype='<f8', mode='r')
    Native = np.memmap(Samples, dtype='<f8', mode='r')
    if Prior.size != np.prod(Shape) or Native.size != Prior.size or not np.isfinite(Native).all():
        raise RuntimeError('Native sampling is incomplete or non-finite')
    Prior, Native = Prior.reshape(Shape), Native.reshape(Shape)
    Difference = np.linalg.norm(Native-Prior, axis=-1)
    Normals = np.cross(Native[:, 2:, 1:-1]-Native[:, :-2, 1:-1], Native[:, 1:-1, 2:]-Native[:, 1:-1, :-2])
    PriorNormals = np.cross(Prior[:, 2:, 1:-1]-Prior[:, :-2, 1:-1], Prior[:, 1:-1, 2:]-Prior[:, 1:-1, :-2])
    Valid = np.linalg.norm(PriorNormals, axis=-1) > 1e-16
    Reversals = int(np.count_nonzero(np.sum(Normals*PriorNormals, axis=-1)[Valid] <= 0))
    Rims, Gap = {}, 0.
    for Identity in range(len(Native)):
        Corners = [tuple(Point) for Point in np.round(Prior[Identity, [0, -1, -1, 0], [0, 0, -1, -1]], 9)]
        for Side, Curve in enumerate((Native[Identity, :, 0], Native[Identity, -1], Native[Identity, ::-1, -1], Native[Identity, 0, ::-1])):
            Start, End = Corners[Side], Corners[(Side+1)%4]
            Key = tuple(sorted((Start, End)))
            Oriented = Curve if Start < End else Curve[::-1]
            if Key in Rims:
                Gap = max(Gap, float(np.linalg.norm(Rims[Key]-Oriented, axis=1).max()))
            else:
                Rims[Key] = Oriented
    Surfaces = Read(Document)
    Paired = []
    for First, Second in Details['mirrorSurfacePairs']:
        Mirrored = Reflect(Surfaces[First])
        for Expected, Actual in ((Mirrored.Poles, Surfaces[Second].Poles),
                                 (Mirrored.KnotsU, Surfaces[Second].KnotsU),
                                 (Mirrored.KnotsV, Surfaces[Second].KnotsV)):
            if not np.array_equal(Expected, Actual):
                raise RuntimeError(f'Serialized surfaces {First}/{Second} do not have identical reflected spline topology')
        Paired.extend((First, Second))
    if sorted(Paired) != list(range(len(Surfaces))):
        raise RuntimeError('Missing or repeated mirrored surface')
    Report = json.loads(Topology.read_text())
    Result = {'measuredOn': '2026-10-03', 'documentSha256': hashlib.sha256(Document.read_bytes()).hexdigest(),
              'nativeSamples': int(Difference.size), 'sourceCharts': len(Native),
              'nativeChangeFromPriorMaximumMm': float(Difference.max()*1000),
              'nativeChangeFromPriorRmsMm': float(np.sqrt(np.mean(Difference**2))*1000),
              'sharedRimMaximumMm': Gap*1000, 'reversedSampledInteriors': Reversals,
              'exactSerializedMirrorPairs': len(Paired)//2,
              'topology': Report,
              'limitations': ['Prior-source accuracy is inherited, not remeasured against Blender in this pass',
                              'Sampled normal and seam checks are not a G1/G2 certificate',
                              'Open surface body and 24 independent junction sheets, not a manufacturing solid']}
    if Difference.max()*1000 > Details['reflectionContinuousChangeBoundMm']+1e-6 or Gap > 1e-8 or Reversals:
        raise RuntimeError(f'Native position, rim or orientation audit failed: {Result}')
    if Report['nonManifoldEdges'] or Report['misorientedEdges'] or Report['hulls'] != 1 or Report['faces'] != Details['sewnFaceCount']:
        raise RuntimeError('Native sewn topology failed')
    Destination.write_text(json.dumps(Result, indent=2)+'\n')
    print(json.dumps(Result, indent=2))


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    for Name in ('prior_samples', 'samples', 'document', 'topology', 'destination'):
        Parser.add_argument(Name, type=Path)
    Arguments = Parser.parse_args()
    Measure(Arguments.prior_samples, Arguments.samples, Arguments.document, Arguments.topology, Arguments.destination)
