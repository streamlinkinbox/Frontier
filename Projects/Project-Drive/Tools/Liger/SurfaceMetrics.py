#!/usr/bin/env python3
"""Compare actual SolidArc samples (CowlVerification) with independent Blender sampling and the supplied NPZ."""
import argparse
from datetime import date
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree


def Measure(Reference, Native, Document, Original, Destination, MeasuredOn, Consolidation=None):
    Content = np.load(Reference)
    Charts, Corners = Content['Charts'], Content['Corners']
    if Charts.shape != (308, 33, 33, 3):
        raise ValueError('Native cowl verification samples 308 charts at 33 by 33 parameters')
    Samples = np.fromfile(Native, dtype='<f8').reshape(Charts.shape)
    if not np.isfinite(Samples).all():
        raise RuntimeError('Native surface contains non-finite positions')
    Provenance = json.loads(Reference.with_suffix('.json').read_text())
    Vertices = np.load(Original)['V'].astype(np.float64)
    Vertices[:, 1] -= Provenance['sourceMirrorCentreY']
    Vertices *= Provenance['metresPerSourceUnit']
    Aligned = Charts[:, ::16, ::16].reshape(-1, 3)
    SourceDeviation = max(cKDTree(Aligned).query(Vertices)[0].max(), cKDTree(Vertices).query(Aligned)[0].max())
    Deviations = np.linalg.norm(Samples-Charts, axis=-1)
    HeldOut = np.ones((33, 33), dtype=bool)
    HeldOut[::2, ::2] = False
    Rims = {}
    Gaps = []
    for Identity, Vertices in enumerate(Corners):
        Chart = Samples[Identity]
        for Side, Rim in enumerate([Chart[:, 0], Chart[-1], Chart[::-1, -1], Chart[0, ::-1]]):
            Start, End = int(Vertices[Side]), int(Vertices[(Side+1)%4])
            Key = tuple(sorted((Start, End)))
            if Start > End: Rim = Rim[::-1]
            if Key in Rims: Gaps.extend(np.linalg.norm(Rims[Key]-Rim, axis=-1))
            else: Rims[Key] = Rim
    def Normals(Points):
        return np.cross(Points[:, 2:, 1:-1]-Points[:, :-2, 1:-1], Points[:, 1:-1, 2:]-Points[:, 1:-1, :-2])
    SourceNormals, NativeNormals = Normals(Charts), Normals(Samples)
    Valid = np.linalg.norm(SourceNormals, axis=-1) > 1e-16
    Reversed = int(np.count_nonzero(np.sum(SourceNormals*NativeNormals, axis=-1)[Valid] <= 0))
    Details = {'status': 'Cowl checkpoint only; not a finished clean-surface vehicle reconstruction',
               'measuredOn': MeasuredOn.isoformat(), 'documentSha256': hashlib.sha256(Document.read_bytes()).hexdigest(),
               'suppliedNpzSha256': hashlib.sha256(Original.read_bytes()).hexdigest(),
               'nativeHeldOutMaximumMm': float(Deviations[:, HeldOut].max()*1000),
               'nativeHeldOutRmsMm': float(np.sqrt(np.mean(Deviations[:, HeldOut]**2))*1000),
               'heldOutSampleCount': int(len(Charts)*HeldOut.sum()),
               'nativeSharedRimMaximumGapMm': float(max(Gaps)*1000),
               'nativeReversedInteriorSamples': Reversed,
               'extractionToSuppliedNpzMaximumMm': float(SourceDeviation*1000),
               'reference': Provenance,
               'limitations': ['308 patches require consolidation', 'G1/G2 not certified',
                               'Sampled positional comparison is not a continuous Hausdorff bound',
                               'No main-body or roof/frame reconstruction is delivered by this document',
                               'Physical scale follows the supplied centimetre extraction convention']}
    if Consolidation:
        Details['consolidation'] = json.loads(Consolidation.read_text())
        Details['limitations'][0] = '30 consolidated faces; further control-net and continuity review remains'
    print(json.dumps(Details, indent=2))
    if SourceDeviation > 2e-6 or Reversed or max(Gaps)>1e-6 or Deviations[:, HeldOut].max()>1e-3:
        raise RuntimeError('Independent native reconstruction check failed')
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Destination.write_text(json.dumps(Details, indent=2)+'\n')


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    for Name in ['reference', 'native', 'document', 'original', 'destination']:
        Parser.add_argument(Name, type=Path)
    Parser.add_argument('--measured-on', type=date.fromisoformat, default=date.today())
    Parser.add_argument('--consolidation', type=Path)
    Arguments = Parser.parse_args()
    Measure(Arguments.reference, Arguments.native, Arguments.document, Arguments.original, Arguments.destination, Arguments.measured_on, Arguments.consolidation)
