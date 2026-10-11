#!/usr/bin/env python3
"""Audit native replayed guide samples against their recorded source chains and unchanged body geometry."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from ContourSequence import Resample


def Measure(SourceBody, SourceEdges, NativeEdges, Document, Destination):
    Details = json.loads(Document.with_suffix('.contours.json').read_text())
    Original = json.loads(SourceEdges.read_text())
    Replayed = json.loads(NativeEdges.read_text())
    Samples = np.asarray([Edge['points'] for Edge in Original])[:, :, :3]
    Actual = np.asarray([Edge['points'] for Edge in Replayed])[:, :, :3]
    if not np.array_equal(Samples, Actual):
        raise ValueError('Native body edge samples changed')
    BeforeTriangles = SourceEdges.with_suffix('.triangles.f64').read_bytes()
    AfterTriangles = NativeEdges.with_suffix('.triangles.f64').read_bytes()
    if BeforeTriangles != AfterTriangles:
        raise ValueError('Native body triangulation changed')
    if not Document.read_text().startswith(SourceBody.read_text().rstrip()):
        raise ValueError('Source surface declarations were changed')
    DocumentHash = hashlib.sha256(Document.read_bytes()).hexdigest()
    if DocumentHash != Details['documentSha256'] or hashlib.sha256(SourceBody.read_bytes()).hexdigest() != Details['bodySourceSha256']:
        raise ValueError('Document provenance does not match the measured assets')
    Guides = json.loads(NativeEdges.with_suffix('.guides.json').read_text())
    if {Guide['name'] for Guide in Guides} != {'Design_Rail_1', 'Design_Rail_2', 'Design_Rail_3_Left', 'Design_Rail_3_Right'}:
        raise ValueError('Missing or repeated native design-guide samples')
    Fits = []
    for Guide in Guides:
        Description = next(Curve for Curve in Details['designCurves'] if Guide['name'].startswith(Curve['name']))
        Points = np.concatenate([Samples[Index, ::1 if End == 0 else -1] for Index, End in Description['sourceEdges']])
        if Points[0, 1] < 0 or (Description['name'] == 'Design_Rail_3' and Points[0, 0] > Points[-1, 0]):
            Points = Points[::-1]
        if Guide['name'].endswith('_Right'):
            Points = Points.copy(); Points[:, 1] *= -1
        Reference = Resample(Points, len(Guide['points']))
        Errors = np.linalg.norm(np.asarray(Guide['points'])-Reference, axis=1)
        if not np.isfinite(Errors).all() or Errors.max() > .001:
            raise ValueError('Native fitted guide drifted from the identified body feature')
        Fits.append({'name': Guide['name'], 'maximumFitMm': float(Errors.max()*1000),
                     'rmsFitMm': float(np.sqrt(np.mean(Errors**2))*1000), 'samples': len(Errors)})
    Result = {'documentSha256': DocumentHash, 'bodySourceSha256': Details['bodySourceSha256'],
              'nativeBodyEdgeSamplesExactlyUnchanged': int(Samples.shape[0]*Samples.shape[1]),
              'nativeBodyTrianglesExactlyUnchanged': len(BeforeTriangles)//72, 'allSourceSurfaceCommandsUnchanged': True,
              'nativeCurveFits': Fits, 'scope': 'Aligned feature curves and paired arch/fade targets; no body-surface repair claimed'}
    Destination.write_text(json.dumps(Result, indent=2)+'\n')
    print(json.dumps(Result, indent=2))


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    for Name in ('source_body', 'source_edges', 'native_edges', 'document', 'destination'):
        Parser.add_argument(Name, type=Path)
    Arguments = Parser.parse_args()
    Measure(Arguments.source_body, Arguments.source_edges, Arguments.native_edges, Arguments.document, Arguments.destination)
