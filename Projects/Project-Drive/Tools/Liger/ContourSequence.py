#!/usr/bin/env python3
"""Follow tangent-continuous native body edges, not annotation pixels, and fit editable feature curves.
The two long cabin rails wrap across the centreline. The shorter inner rail ends at the existing
junction, rather than inventing a path through it. Wheel lip contours are measured independently.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy.interpolate import BSpline
from scipy.optimize import least_squares


def Trace(Edges, AngleDegrees=12):
    Samples = np.asarray([Edge['points'] for Edge in Edges])[:, :, :3]
    Directions = np.stack([Samples[:, 1]-Samples[:, 0], Samples[:, -2]-Samples[:, -1]], axis=1)
    Lengths = np.linalg.norm(Directions, axis=2)
    if not np.isfinite(Samples).all() or np.any(Lengths < 1e-12):
        raise ValueError('Native edges must have finite, non-degenerate endpoint tangents')
    Directions /= Lengths[:, :, None]
    Incident = defaultdict(list)
    for Index, Edge in enumerate(Edges):
        for End, Key in enumerate(('start', 'end')):
            Incident[Edge[Key]].append((Index, End))
    Continuations = {}
    for HalfEdges in Incident.values():
        for Index, End in HalfEdges:
            Candidates = [(float(Directions[Index, End]@Directions[Other, Tip]), Other, Tip)
                          for Other, Tip in HalfEdges if Other != Index]
            if not Candidates:
                continue
            Alignment, Other, Tip = min(Candidates)
            if Alignment < -np.cos(np.radians(AngleDegrees)):
                Continuations[Index, End] = (Other, Tip)
    # Mutual choices prevent a transverse branch from hijacking a long feature at a junction.
    Continuations = {Half: Opposite for Half, Opposite in Continuations.items()
                     if Continuations.get(Opposite) == Half}
    Visited, Chains = set(), []
    for Index in range(len(Edges)):
        if Index in Visited:
            continue
        Start = (Index, 0)
        while Start in Continuations:
            Other, Tip = Continuations[Start]
            Start = Other, 1-Tip
            if Start == (Index, 0):
                break
        Chain = []
        while Start[0] not in Visited:
            Other, Tip = Start
            Visited.add(Other)
            Chain.append((Other, Tip))
            Outgoing = Other, 1-Tip
            if Outgoing not in Continuations:
                break
            Start = Continuations[Outgoing]
        Points = np.concatenate([Samples[Other, ::1 if Tip == 0 else -1] for Other, Tip in Chain])
        Segments = np.linalg.norm(np.diff(Points, axis=0), axis=1)
        if Segments.max(initial=0) > .5:
            raise ValueError('Native chain contains an unexplained jump')
        Chains.append({'edges': Chain, 'points': Points, 'lengthMetres': float(Segments.sum())})
    return Chains


def Locate(Chains, Seed, MinimumLength, MaximumDistance=.025):
    Candidates = [(float(np.linalg.norm(Chain['points']-Seed, axis=1).min()), Index)
                  for Index, Chain in enumerate(Chains) if Chain['lengthMetres'] >= MinimumLength]
    if not Candidates:
        raise ValueError('No sufficiently long body feature near the geometric seed')
    Distance, Index = min(Candidates)
    if Distance > MaximumDistance:
        raise ValueError(f'No source-supported contour at seed {Seed}: nearest is {Distance} m away')
    return Chains[Index]


def Resample(Points, Count):
    Distance = np.r_[0., np.cumsum(np.linalg.norm(np.diff(Points, axis=0), axis=1))]
    Unique = np.r_[True, np.diff(Distance) > 1e-12]
    if Distance[-1] < 1e-8:
        raise ValueError('Cannot fit a zero-length contour')
    Parameters = Distance[Unique]/Distance[-1]
    Uniform = np.linspace(0, 1, Count)
    return np.column_stack([np.interp(Uniform, Parameters, Points[Unique, Axis]) for Axis in range(3)])


def Fit(Points, Symmetric, Tolerance=.001):
    Samples = Resample(Points, 4097)
    if Symmetric:
        Opposite = Samples[::-1].copy()
        Opposite[:, 1] *= -1
        if np.linalg.norm(Samples-Opposite, axis=1).max() > 1e-7:
            raise ValueError('Selected full-width body feature is not reflection-paired')
        Samples = .5*(Samples+Opposite)
    # Fitting and validation use disjoint arclength samples; no screen-space landmark is fitted.
    Parameters = np.linspace(0, 1, len(Samples))
    for Count in (16, 24, 32, 48, 64, 96, 128, 192):
        Knots = np.r_[np.zeros(4), np.arange(1, Count-3)/(Count-3), np.ones(4)]
        Basis = BSpline(Knots, np.eye(Count), 3)(Parameters)
        Poles = np.zeros((Count, 3))
        Poles[0], Poles[-1] = Samples[0], Samples[-1]
        Residual = Samples-Basis[:, :1]*Poles[0]-Basis[:, -1:]*Poles[-1]
        Poles[1:-1] = np.linalg.lstsq(Basis[::2, 1:-1], Residual[::2], rcond=None)[0]
        if Symmetric:
            Opposite = Poles[::-1].copy()
            Opposite[:, 1] *= -1
            Poles = .5*(Poles+Opposite)
        Errors = np.linalg.norm(Basis@Poles-Samples, axis=1)
        if Errors.max() <= Tolerance:
            return Poles, {'controlPoles': Count, 'sampledMaximumFitMm': float(Errors.max()*1000),
                           'heldOutMaximumFitMm': float(Errors[1::2].max()*1000),
                           'sampledRmsFitMm': float(np.sqrt(np.mean(Errors**2))*1000),
                           'samples': len(Samples), 'toleranceMetres': Tolerance}
    raise ValueError('Cubic feature fit exceeded its positional tolerance')


def Circle(Points, CentreX):
    Samples = Resample(Points, 4097)
    Angles = np.degrees(np.arctan2(Samples[:, 2]-.43, Samples[:, 0]-CentreX))
    Crown = Samples[(Angles >= 25) & (Angles <= 155)]
    if len(Crown) < 100:
        raise ValueError('Insufficient independent crown samples')
    def Residual(Parameters):
        return np.linalg.norm(Crown[:, [0, 2]]-Parameters[:2], axis=1)-Parameters[2]
    Solution = least_squares(Residual, [CentreX, .43, .45], loss='soft_l1', f_scale=.001)
    if not Solution.success or not np.isfinite(Solution.x).all() or Solution.x[2] <= 0:
        raise ValueError('Circular crown fit did not converge')
    Error = Residual(Solution.x)
    if np.max(np.abs(Error)) > .012:
        raise ValueError('Crown is not sufficiently close to a circular target')
    return Solution.x, {'sourceCrownMaximumRadialMm': float(np.max(np.abs(Error))*1000),
                        'sourceCrownRmsRadialMm': float(np.sqrt(np.mean(Error**2))*1000),
                        'crownSamples': len(Crown), 'sourceLateralRangeMetres': [float(Crown[:, 1].min()), float(Crown[:, 1].max())]}


def NegativeBernstein(Coefficients, Depth=0):
    if not np.isfinite(Coefficients).all():
        return False
    if np.max(Coefficients) < 0:
        return True
    if np.min(Coefficients) >= 0 or Depth >= 16:
        return False
    Left, Right = [Coefficients[0]], [Coefficients[-1]]
    while len(Coefficients) > 1:
        Coefficients = .5*(Coefficients[:-1]+Coefficients[1:])
        Left.append(Coefficients[0]); Right.append(Coefficients[-1])
    return NegativeBernstein(np.asarray(Left), Depth+1) and NegativeBernstein(np.asarray(Right[::-1]), Depth+1)


def Fade(CentreX, CentreZ, Radius, PlaneY, AngleDegrees, SourcePoints, Front):
    Samples = Resample(SourcePoints, 1025)
    End = Samples[-1] if Samples[-1, 2] < Samples[0, 2] else Samples[0]
    if Samples[-1, 2] >= Samples[0, 2]:
        Samples = Samples[::-1]
    End = Samples[-1].copy()
    End[1] = PlaneY
    Tangent = Samples[-1]-Samples[-5]
    Tangent[1] = 0
    Tangent /= np.linalg.norm(Tangent)
    Angle = np.radians(AngleDegrees)
    Radial = np.array([np.cos(Angle), 0., np.sin(Angle)])
    Begin = np.array([CentreX, PlaneY, CentreZ])+Radius*Radial
    Direction = np.array([np.sin(Angle), 0., -np.cos(Angle)])*(1 if Front else -1)
    Speed = np.linalg.norm(End-Begin)*1.1
    Poles = np.zeros((6, 3))
    Poles[0], Poles[-1] = Begin, End
    Poles[1] = Begin+Speed*Direction/5
    Poles[2] = 2*Poles[1]-Begin-Speed**2*Radial/(20*Radius)
    Poles[-2] = End-Speed*Tangent/5
    Poles[-3] = 2*Poles[-2]-End
    Curve = BSpline(np.r_[np.zeros(6), np.ones(6)], Poles, 5)
    Parameters = np.linspace(0, 1, 1001)
    First, Second = Curve(Parameters, nu=1), Curve(Parameters, nu=2)
    Curvature = np.linalg.norm(np.cross(First, Second), axis=1)/np.linalg.norm(First, axis=1)**3
    Descending = NegativeBernstein(np.diff(Poles[:, 2]))
    if not Descending or not np.isfinite(Curvature).all() or np.any(First[:, 2] >= 0) or Curvature.max() > 12:
        raise ValueError('Lower transition folds, reverses height or develops excessive curvature')
    return Poles, {'startCurvaturePerMetre': float(Curvature[0]),
                   'endCurvaturePerMetre': float(Curvature[-1]),
                   'maximumSampledCurvaturePerMetre': float(Curvature.max()),
                   'sourceEndpointXZMetres': End[[0, 2]].tolist(), 'continuousDescendingHeightVerified': Descending,
                   'scope': 'Side-elevation target; G2 to the crown, straight-curvature limit at its lower endpoint'}


def Coordinates(Points):
    return ' '.join('('+','.join(f'{Coordinate:.12f}' for Coordinate in Point)+')' for Point in Points)


def Construct(Source, Measurements, Review, Destination):
    Prior = json.loads(Review.read_text())
    SourceHash = hashlib.sha256(Source.read_bytes()).hexdigest()
    if SourceHash != Prior['bodySourceSha256']:
        raise ValueError('Approved repair-edge selections belong to a different body')
    Edges = json.loads(Measurements.read_text())
    Chains = Trace(Edges)
    Lines = [Source.read_text().rstrip(), 'require feature-curves',
             '# Guides fitted to actual native body-edge chains; body surface geometry is unchanged.']
    DesignCurves = []
    Seeds = [('Design_Rail_1', [-1., .634, .842], 7., True),
             ('Design_Rail_2', [-1., .474, .919], 6., True),
             ('Design_Rail_3', [-1., .271, .978], 1.5, False)]
    for Name, Seed, MinimumLength, Symmetric in Seeds:
        Chain = Locate(Chains, Seed, MinimumLength)
        Points = Chain['points']
        if Points[0, 1] < 0 or (not Symmetric and Points[0, 0] > Points[-1, 0]):
            Points = Points[::-1]
        Poles, Metrics = Fit(Points, Symmetric)
        for Sign in ((1,) if Symmetric else (1, -1)):
            Reflected = Poles.copy()
            Reflected[:, 1] *= Sign
            Identity = Name if Symmetric else Name+('_Left' if Sign > 0 else '_Right')
            Lines.extend(['cpcurve '+Coordinates(Reflected)+' --degree=3 --name='+Identity, 'feature design '+Identity])
        DesignCurves.append({'name': Name, 'sourceEdges': Chain['edges'], 'sourceLengthMetres': Chain['lengthMetres'],
                       'longitudinalSpanMetres': float(np.ptp(Points[:, 0])), **Metrics})
    # These are exactly the approved selections; no new image-based re-selection is performed.
    Repair = Prior['repairEdges']
    Lines.append('feature-copy Liger_Main_Body repair --edges='+','.join(str(Index) for Index in Repair)+' --name=Repair')
    Arches, DisplayPlanes, Profiles = [], {}, {}
    ArcSeeds = [('Rear', 'Inner', [-1.08, 1.161, .880], 1.9, -1.074),
                ('Rear', 'Outer', [-1.08, 1.140, .926], .9, -1.074),
                ('Front', 'Inner', [1.745, 1.138, .860], 1.8, 1.751),
                ('Front', 'Outer', [1.750, 1.131, .877], 1.9, 1.751)]
    for Wheel, Contour, Seed, MinimumLength, CentreX in ArcSeeds:
        Chain = Locate(Chains, Seed, MinimumLength, .012)
        Parameters, Metrics = Circle(Chain['points'], CentreX)
        CentreX, CentreZ, Radius = Parameters
        # Display references sit outboard; they are not falsely attached to unrepaired body surfaces.
        if Contour == 'Inner':
            DisplayPlanes[Wheel] = float(Chain['points'][:, 1].max()+.006)
        PlaneY = DisplayPlanes[Wheel]
        Ordered = Chain['points']
        if Ordered[0, 0] > Ordered[-1, 0]:
            Ordered = Ordered[::-1]
        CrownIndex = int(np.argmax(Ordered[:, 2]))
        LowerSources = [Ordered[:CrownIndex+1][::-1], Ordered[CrownIndex:]]
        if Wheel == 'Rear' and Contour == 'Outer':
            LowerChains = [Locate(Chains, [-1.567, 1.119, .38], .2, .03),
                           Locate(Chains, [-.638, 1.135, .18], .3, .025)]
            LowerSources = [Lower['points'] for Lower in LowerChains]
        else:
            LowerChains = [Chain, Chain]
        Fades = []
        for EndIndex, SourcePoints in enumerate(LowerSources):
            Poles, FadeMetrics = Fade(CentreX, CentreZ, Radius, PlaneY, 155 if EndIndex == 0 else 25,
                                     SourcePoints, EndIndex == 1)
            FadeMetrics['sourceEdges'] = LowerChains[EndIndex]['edges']
            Fades.append((Poles, FadeMetrics))
        Profiles[Wheel, Contour] = Fades
        for Sign in (1, -1):
            Points = [[CentreX+Radius*np.cos(Angle), Sign*PlaneY, CentreZ+Radius*np.sin(Angle)]
                      for Angle in np.radians([25, 90, 155])]
            Identity = f'{Wheel}_{Contour}_Crown_{"Left" if Sign > 0 else "Right"}'
            Lines.extend(['arc --three '+Coordinates(Points)+' --name='+Identity, 'feature circular '+Identity])
            if Contour == 'Outer':
                Lines.append('tint '+Identity+' .16 .32 1')
            for EndIndex, (Poles, FadeMetrics) in enumerate(Fades):
                Reflected = Poles.copy()
                Reflected[:, 1] *= Sign
                FadeName = Identity+('_TrailingFade' if EndIndex == 0 else '_LeadingFade')
                Lines.extend(['cpcurve '+Coordinates(Reflected)+' --degree=5 --name='+FadeName, 'feature design '+FadeName,
                              'tint '+FadeName+(' .16 .32 1' if Contour == 'Outer' else ' .08 .82 1')])
        Arches.append({'wheel': Wheel, 'contour': Contour, 'centreXMetres': float(CentreX),
                       'centreZMetres': float(CentreZ), 'radiusMetres': float(Radius), 'sourceEdges': Chain['edges'],
                       'guidePlaneAbsYMetres': PlaneY, 'guideAngularExtentDegrees': [25, 155], 'lowerTransitions': [Metrics for Poles, Metrics in Fades], **Metrics})
    Clearance = []
    for Wheel in ('Rear', 'Front'):
        Inner, Outer = [next(Arc for Arc in Arches if Arc['wheel'] == Wheel and Arc['contour'] == Contour)
                        for Contour in ('Inner', 'Outer')]
        Centres = np.array([Outer['centreXMetres']-Inner['centreXMetres'], Outer['centreZMetres']-Inner['centreZMetres']])
        CrownGap = Outer['radiusMetres']-Inner['radiusMetres']-np.linalg.norm(Centres)
        MinimumFadeGap = float('inf')
        for EndIndex in range(2):
            Curves = [BSpline(np.r_[np.zeros(6), np.ones(6)], Profiles[Wheel, Contour][EndIndex][0], 5)(np.linspace(0, 1, 4097))
                      for Contour in ('Inner', 'Outer')]
            Lower = max(Curve[-1, 2] for Curve in Curves)
            Upper = min(Curve[0, 2] for Curve in Curves)
            Height = np.linspace(Lower, Upper, 2049)
            Along = [np.interp(Height, Curve[::-1, 2], Curve[::-1, 0]) for Curve in Curves]
            Gap = (Along[1]-Along[0])*(1 if EndIndex == 1 else -1)
            MinimumFadeGap = min(MinimumFadeGap, float(Gap.min()))
        if CrownGap <= .005 or MinimumFadeGap <= .005:
            raise ValueError('Inner and outer target contours cross or lose their separation')
        Clearance.append({'wheel': Wheel, 'circularContourMinimumSeparationMm': float(CrownGap*1000),
                          'sampledLowerContourMinimumSeparationMm': MinimumFadeGap*1000})
    Lines.extend(['show cages off', 'show iso off', 'show edges on', 'show features on'])
    Destination.parent.mkdir(parents=True, exist_ok=True)
    Destination.write_text('\n'.join(Lines)+'\n')
    Details = {'bodySourceSha256': SourceHash, 'documentSha256': hashlib.sha256(Destination.read_bytes()).hexdigest(),
               'bodyChanged': False, 'nativeEdgeMeasurementsSha256': hashlib.sha256(Measurements.read_bytes()).hexdigest(),
               'featureSource': 'Native edge geometry and shared vertices; mutual tangent continuation within 12 degrees',
               'designCurves': DesignCurves, 'archContours': Arches, 'contourClearance': Clearance, 'repairEdges': Repair,
               'repairSelectionsUnchanged': True,
               'limits': ['Fits are sample-based, not continuous error certificates',
                          'Inner rear-deck rail stops at its existing interrupted junction; no invented continuation',
                          'Circular references are in side elevation and offset outboard for inspection',
                          'Crown and lower-fade targets are authored; replacing and fairing the body surfaces remains unfinished']}
    Destination.with_suffix('.contours.json').write_text(json.dumps(Details, indent=2)+'\n')
    print(json.dumps({Key: Value for Key, Value in Details.items() if Key != 'repairEdges'}, indent=2))


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    for Name in ('source', 'measurements', 'review', 'destination'):
        Parser.add_argument(Name, type=Path)
    Arguments = Parser.parse_args()
    Construct(Arguments.source, Arguments.measurements, Arguments.review, Arguments.destination)
