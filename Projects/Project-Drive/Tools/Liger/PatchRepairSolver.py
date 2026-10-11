#!/usr/bin/env python3
"""Construct guide-bounded cubic patch groups with fixed rims and physical tangent constraints.
The fitted scalar coordinate moves actual NURBS poles; no texture/displacement rendering is used.
Measurements are sampled checks, not global Hausdorff or Class-A continuity certificates.
"""

from collections import Counter, defaultdict
from copy import deepcopy
import numpy as np
from numpy.polynomial.chebyshev import chebvander2d, chebval2d, chebder
from scipy.interpolate import BSpline, make_interp_spline
from scipy.spatial import cKDTree
from scipy.sparse import vstack
from scipy.sparse.linalg import spsolve
from RoofSequence import (
    Cut,
    Reverse,
    JoinCurves,
    Evaluate,
    TensorRows,
    Parameters,
    Gradient,
    GridSamples,
    AlignedStops,
)
from PatchSequence import Patch, Refinement


def TraversePerimeter(Specification, Axes, Edges, Barriers):
    Rim = [Edges[SlotIndex] for SlotIndex in Specification["rim"]]
    Incident = defaultdict(list)
    for SlotIndex, ActiveEdge in enumerate(Rim):
        Incident[ActiveEdge["start"]].append((SlotIndex, 0))
        Incident[ActiveEdge["end"]].append((SlotIndex, 1))
    assert all(
        len(AcrossParameters) == 2 for AcrossParameters in Incident.values()
    ), "perimeter not one regular cycle"
    Ordered = []
    SlotIndex, End = 0, 0
    Seen = set()
    while SlotIndex not in Seen:
        Seen.add(SlotIndex)
        ActiveEdge = Rim[SlotIndex]
        ActivePatch = np.array(ActiveEdge["poles"])
        assert np.max(abs(ActivePatch[:, 3] - 1)) < 1e-12
        KnotValues = np.array(ActiveEdge["knots"])
        KnotValues = (KnotValues - KnotValues[3]) / (KnotValues[-4] - KnotValues[3])
        ThirdOperand = BSpline(KnotValues, ActivePatch[:, :3] @ Axes, 3)
        if End:
            ThirdOperand = Reverse(ThirdOperand)
        Ordered.append((ActiveEdge["edge"], ThirdOperand))
        SlotIndex, End = next(
            AcrossParameters
            for AcrossParameters in Incident[ActiveEdge["start" if End else "end"]]
            if AcrossParameters[0] != SlotIndex
        )
    assert len(Ordered) == len(Rim), "multiple perimeter cycles"
    Polygon = np.array([ThirdOperand(0)[:2] for SlotIndex, ThirdOperand in Ordered])
    Area = np.sum(
        Polygon[:, 0] * np.roll(Polygon[:, 1], -1)
        - Polygon[:, 1] * np.roll(Polygon[:, 0], -1)
    )
    if Area < 0:
        Ordered = [
            (SlotIndex, Reverse(ThirdOperand))
            for SlotIndex, ThirdOperand in Ordered[::-1]
        ]
    Labels = [
        Barriers.get(str(SlotIndex), "Unclassified")
        for SlotIndex, ThirdOperand in Ordered
    ]
    Start = next(
        SlotIndex
        for SlotIndex in range(len(Labels))
        if Labels[SlotIndex] != Labels[SlotIndex - 1]
    )
    Ordered = Ordered[Start:] + Ordered[:Start]
    Labels = Labels[Start:] + Labels[:Start]
    Groups = []
    for (SlotIndex, ThirdOperand), Label in zip(Ordered, Labels):
        if not Groups or Label != Groups[-1][0]:
            Groups.append((Label, []))
        Groups[-1][1].append((SlotIndex, ThirdOperand))
    assert 3 <= len(Groups) <= 5, (
        len(Groups),
        [GradientValues[0] for GradientValues in Groups],
    )
    return Groups


def ConstructCoonsPatch(Sides):
    Bottom, Right, Top, Left = Sides[0], Sides[1], Reverse(Sides[2]), Reverse(Sides[3])
    for FirstOperand, SecondOperand in [(Bottom, Top), (Left, Right)]:
        for SlotIndex, KnotValues in enumerate(SecondOperand.t):
            Near = FirstOperand.t[np.argmin(abs(FirstOperand.t - KnotValues))]
            if abs(Near - KnotValues) < 1e-12:
                SecondOperand.t[SlotIndex] = Near
    AlongKnots = np.array(sorted((Counter(Bottom.t) | Counter(Top.t)).elements()))
    AcrossKnots = np.array(sorted((Counter(Left.t) | Counter(Right.t)).elements()))
    BottomPoles = Refinement(Bottom.t, AlongKnots) @ Bottom.c
    TopPoles = Refinement(Top.t, AlongKnots) @ Top.c
    LeftPoles = Refinement(Left.t, AcrossKnots) @ Left.c
    RightPoles = Refinement(Right.t, AcrossKnots) @ Right.c
    AlongParameters = np.array(
        [
            np.mean(AlongKnots[SlotIndex + 1 : SlotIndex + 4])
            for SlotIndex in range(len(AlongKnots) - 4)
        ]
    )
    AcrossParameters = np.array(
        [
            np.mean(AcrossKnots[SlotIndex + 1 : SlotIndex + 4])
            for SlotIndex in range(len(AcrossKnots) - 4)
        ]
    )
    ActivePatch = (
        (1 - AcrossParameters[None, :, None]) * BottomPoles[:, None]
        + AcrossParameters[None, :, None] * TopPoles[:, None]
        + (1 - AlongParameters[:, None, None]) * LeftPoles[None]
        + AlongParameters[:, None, None] * RightPoles[None]
    )
    ActivePatch -= (1 - AlongParameters[:, None, None]) * (
        (1 - AcrossParameters[None, :, None]) * BottomPoles[0]
        + AcrossParameters[None, :, None] * TopPoles[0]
    ) + AlongParameters[:, None, None] * (
        (1 - AcrossParameters[None, :, None]) * BottomPoles[-1]
        + AcrossParameters[None, :, None] * TopPoles[-1]
    )
    return Patch(ActivePatch, AlongKnots, AcrossKnots, np.zeros((2, 2), int), {})


def Construct(Specification, Patches, Sewn, Edges, Barriers, Seed=None):
    print("BEGIN", Specification["component"], len(Specification["names"]), flush=True)
    Pieces = [deepcopy(Patches[PatchName]) for PatchName in Specification["names"]]
    Axes = np.eye(3)
    if Specification["projection"] == "principal":
        Cloud = np.concatenate(
            [ActivePatch.Poles.reshape(-1, 3) for ActivePatch in Pieces]
        )
        _, Directions = np.linalg.eigh(np.cov(Cloud.T))
        Normal = Directions[:, 0]
        if Normal[Specification["normalAxis"]] < 0:
            Normal = -Normal
        Along = np.array([1.0, 0, 0])
        Along -= Normal * (Along @ Normal)
        Along /= np.linalg.norm(Along)
        Across = np.cross(Normal, Along)
        Axes = np.column_stack([Along, Across, Normal])
        print("AXES", Axes.tolist(), flush=True)
        for ActivePatch in Pieces:
            ActivePatch.Poles = ActivePatch.Poles @ Axes
    if Seed is not None:
        Seed = deepcopy(Seed)
        Seed.Poles = Seed.Poles @ Axes
        AlongTangent = Evaluate(Seed, np.array([0.5]), np.array([0.5]), 1, 0)[0]
        AcrossTangent = Evaluate(Seed, np.array([0.5]), np.array([0.5]), 0, 1)[0]
        if np.cross(AlongTangent, AcrossTangent)[2] < 0:
            Seed.Poles = Seed.Poles[::-1].copy()
            Seed.KnotsU = 1 - Seed.KnotsU[::-1]
        Sides = [None] * 4
    else:
        Groups = TraversePerimeter(Specification, Axes, Edges, Barriers)
        print(
            "SIDES",
            [
                (KnotValues, len(AcrossParameters))
                for KnotValues, AcrossParameters in Groups
            ],
            flush=True,
        )
        Sides = [JoinCurves(GradientValues)[0] for _, GradientValues in Groups]
        if len(Groups) == 4:
            for First, Opposite in [(0, 2), (1, 3)]:
                FirstOperand = Groups[First][1]
                SecondOperand = [
                    (SlotIndex, Reverse(ThirdOperand))
                    for SlotIndex, ThirdOperand in Groups[Opposite][1][::-1]
                ]
                _, FirstStops = JoinCurves(FirstOperand)
                _, SecondStops = JoinCurves(SecondOperand)
                FirstStops, SecondStops = AlignedStops(FirstStops, SecondStops, 0.02)
                Sides[First] = JoinCurves(FirstOperand, FirstStops)[0]
                Sides[Opposite] = Reverse(JoinCurves(SecondOperand, SecondStops)[0])
    Samples = []
    Weights = []
    for ActivePatch in Pieces:
        FirstOperand, SecondOperand = np.meshgrid(
            np.linspace(0.001, 0.999, 23), np.linspace(0.001, 0.999, 23), indexing="ij"
        )
        FirstOperand, SecondOperand = FirstOperand.ravel(), SecondOperand.ravel()
        Samples.append(Evaluate(ActivePatch, FirstOperand, SecondOperand))
        Weights.extend(
            abs(
                np.cross(
                    Evaluate(ActivePatch, FirstOperand, SecondOperand, 1),
                    Evaluate(ActivePatch, FirstOperand, SecondOperand, 0, 1),
                )[:, 2]
            )
        )
    Samples = np.concatenate(Samples)
    Weights = np.sqrt(np.array(Weights))
    Weights /= np.sqrt(np.mean(Weights**2))
    Low = Samples[:, :2].min(0)
    Span = Samples[:, :2].max(0) - Low
    PlanarCoordinates = 2 * (Samples[:, :2] - Low) / Span - 1
    Design = chebvander2d(PlanarCoordinates[:, 0], PlanarCoordinates[:, 1], [7, 7])
    Coefficients = np.linalg.lstsq(
        Design * Weights[:, None], Samples[:, 2] * Weights, rcond=1e-12
    )[0].reshape(8, 8)
    Error = (Design @ Coefficients.ravel() - Samples[:, 2]) * 1000
    print("FIELD_MAX_RMS_MM", abs(Error).max(), np.sqrt(np.mean(Error**2)), flush=True)

    def SamplePolynomialField(PlanarCoordinates):
        FirstOperand = 2 * (PlanarCoordinates - Low) / Span - 1
        Height = chebval2d(FirstOperand[:, 0], FirstOperand[:, 1], Coefficients)
        Derivatives = np.column_stack(
            [
                chebval2d(
                    FirstOperand[:, 0],
                    FirstOperand[:, 1],
                    chebder(Coefficients, axis=SlotIndex),
                )
                * 2
                / Span[SlotIndex]
                for SlotIndex in range(2)
            ]
        )
        return Height, Derivatives

    RimPoints = np.concatenate(
        [
            np.array(Edges[SlotIndex]["points"])[:, :3] @ Axes
            for SlotIndex in Specification["rim"]
        ]
    )
    RimSearch = cKDTree(RimPoints)
    LocalSupports = {
        FaceIndex: deepcopy(Patches[Sewn[FaceIndex]])
        for FaceIndex in Specification["faces"]
    }
    for ActivePatch in LocalSupports.values():
        ActivePatch.Poles = ActivePatch.Poles @ Axes

    def ProjectRimGradient(Positions):
        _, NearestRim = RimSearch.query(Positions)
        SourceFaces = [
            next(
                FaceIndex
                for FaceIndex in Edges[Specification["rim"][SlotIndex // 17]]["faces"]
                if FaceIndex in LocalSupports
            )
            for SlotIndex in NearestRim
        ]
        Derivatives = np.zeros((len(Positions), 2))
        for FaceIndex in set(SourceFaces):
            Chosen = np.flatnonzero(np.array(SourceFaces) == FaceIndex)
            Derivatives[Chosen] = Gradient(
                LocalSupports[FaceIndex], Positions[Chosen, :2]
            )
        return Derivatives

    Quads = []
    if Seed is not None:
        Quads = [Seed]
    elif len(Sides) == 4:
        Quads = [ConstructCoonsPatch(Sides)]
    else:
        Corners = np.array([ThirdOperand(0) for ThirdOperand in Sides])
        Centre = Corners.mean(0)
        Centre[2] = SamplePolynomialField(Centre[None, :2])[0][0]
        Spokes = []
        for ThirdOperand in Sides:
            Midpoint = ThirdOperand(0.5)
            Direction = Centre[:2] - Midpoint[:2]
            ParameterValues = np.linspace(0, 1, 17)
            PlanarCoordinates = (
                Midpoint[None, :2] + ParameterValues[:, None] * Direction
            )
            Heights, FourthOperand = SamplePolynomialField(PlanarCoordinates)
            Correction = Midpoint[2] - Heights[0]
            Departure = ProjectRimGradient(Midpoint[None])[0] @ Direction
            SlopeCorrection = Departure - FourthOperand[0] @ Direction + 3 * Correction
            Heights += (
                Correction * (1 - ParameterValues) ** 3
                + SlopeCorrection * ParameterValues * (1 - ParameterValues) ** 2
            )
            SpatialCoordinates = np.column_stack([PlanarCoordinates, Heights])
            SpatialCoordinates[0] = Midpoint
            SpatialCoordinates[-1] = Centre
            First = np.r_[Direction, Departure]
            Last = np.r_[Direction, FourthOperand[-1] @ Direction]
            Spokes.append(
                make_interp_spline(
                    ParameterValues,
                    SpatialCoordinates,
                    k=3,
                    bc_type=([(1, First)], [(1, Last)]),
                )
            )
        for SlotIndex, ThirdOperand in enumerate(Sides):
            Quads.append(
                ConstructCoonsPatch(
                    [
                        Cut(ThirdOperand, 0, 0.5),
                        Spokes[SlotIndex],
                        Reverse(Spokes[SlotIndex - 1]),
                        Cut(Sides[SlotIndex - 1], 0.5, 1),
                    ]
                )
            )
    Measures = []
    for Index, ActivePatch in enumerate(Quads):
        if ActivePatch.Poles.shape[0] * ActivePatch.Poles.shape[1] > Specification.get(
            "maximumPoles", 12000
        ):
            raise ValueError(
                "Patch pole budget exceeded; partition the selected area before fitting"
            )
        print("QUAD", Index, ActivePatch.Poles.shape, flush=True)

        def ConstructRows(AlongParameters, AcrossParameters):
            SecondOperand = TensorRows(
                AlongParameters,
                AcrossParameters,
                ActivePatch.KnotsU,
                ActivePatch.KnotsV,
            )
            AlongRows = TensorRows(
                AlongParameters,
                AcrossParameters,
                ActivePatch.KnotsU,
                ActivePatch.KnotsV,
                1,
            )
            AcrossRows = TensorRows(
                AlongParameters,
                AcrossParameters,
                ActivePatch.KnotsU,
                ActivePatch.KnotsV,
                0,
                1,
            )
            FirstOperand = AlongRows @ ActivePatch.Poles.reshape(-1, 3)
            ThirdOperand = AcrossRows @ ActivePatch.Poles.reshape(-1, 3)
            Jacobian = (
                FirstOperand[:, 0] * ThirdOperand[:, 1]
                - FirstOperand[:, 1] * ThirdOperand[:, 0]
            )
            if np.min(Jacobian) <= 1e-12:
                raise ValueError(
                    (
                        "folded chart",
                        Specification["component"],
                        Index,
                        np.min(Jacobian),
                    )
                )
            FirstGradientRows = AlongRows.multiply(
                (ThirdOperand[:, 1] / Jacobian)[:, None]
            ) - AcrossRows.multiply((FirstOperand[:, 1] / Jacobian)[:, None])
            SecondGradientRows = AcrossRows.multiply(
                (FirstOperand[:, 0] / Jacobian)[:, None]
            ) - AlongRows.multiply((ThirdOperand[:, 0] / Jacobian)[:, None])
            return SecondOperand, FirstGradientRows.tocsr(), SecondGradientRows.tocsr()

        Coordinates2d = Parameters(ActivePatch, Samples[:, :2])
        ProjectedPositions = Evaluate(ActivePatch, *Coordinates2d.T)
        Inside = (
            np.linalg.norm(ProjectedPositions[:, :2] - Samples[:, :2], axis=1) < 1e-8
        )
        AlongParameters, AcrossParameters = np.meshgrid(
            GridSamples(ActivePatch.KnotsU, 2),
            GridSamples(ActivePatch.KnotsV, 2),
            indexing="ij",
        )
        AlongParameters, AcrossParameters = (
            AlongParameters.ravel(),
            AcrossParameters.ravel(),
        )
        SecondOperand, FirstGradientRows, SecondGradientRows = ConstructRows(
            AlongParameters, AcrossParameters
        )
        SpatialCoordinates = SecondOperand @ ActivePatch.Poles.reshape(-1, 3)
        HeightValues, GradientValues = SamplePolynomialField(SpatialCoordinates[:, :2])
        Matrices = [
            SecondOperand * 0.7,
            FirstGradientRows * 0.004,
            SecondGradientRows * 0.004,
        ]
        Targets = [
            HeightValues * 0.7,
            GradientValues[:, 0] * 0.004,
            GradientValues[:, 1] * 0.004,
        ]
        CurvatureWeight = Specification.get(
            "curvatureWeight", 0.0002 if Specification["component"] == 5 else 0
        )
        if CurvatureWeight:
            # Control physical curvature, not just point fit: narrow window charts
            # otherwise permit visible between-sample ripples in the unconstrained interior.
            Along = Evaluate(ActivePatch, AlongParameters, AcrossParameters, 1, 0)
            Across = Evaluate(ActivePatch, AlongParameters, AcrossParameters, 0, 1)
            Determinant = Along[:, 0] * Across[:, 1] - Along[:, 1] * Across[:, 0]
            FirstOperand, ThirdOperand = (
                Across[:, 1] / Determinant,
                -Across[:, 0] / Determinant,
            )
            InverseAlong, FourthOperand = (
                -Along[:, 1] / Determinant,
                Along[:, 0] / Determinant,
            )
            Second = []
            for Orders in [(2, 0), (1, 1), (0, 2)]:
                Basis = TensorRows(
                    AlongParameters,
                    AcrossParameters,
                    ActivePatch.KnotsU,
                    ActivePatch.KnotsV,
                    *Orders
                )
                Shape = Basis @ ActivePatch.Poles.reshape(-1, 3)
                Second.append(
                    Basis
                    - FirstGradientRows.multiply(Shape[:, 0, None])
                    - SecondGradientRows.multiply(Shape[:, 1, None])
                )
            FirstCurvatureRows = (
                Second[0].multiply((FirstOperand * FirstOperand)[:, None])
                + Second[1].multiply((2 * FirstOperand * InverseAlong)[:, None])
                + Second[2].multiply((InverseAlong * InverseAlong)[:, None])
            )
            MixedCurvatureRows = (
                Second[0].multiply((FirstOperand * ThirdOperand)[:, None])
                + Second[1].multiply(
                    (FirstOperand * FourthOperand + InverseAlong * ThirdOperand)[
                        :, None
                    ]
                )
                + Second[2].multiply((InverseAlong * FourthOperand)[:, None])
            )
            SecondCurvatureRows = (
                Second[0].multiply((ThirdOperand * ThirdOperand)[:, None])
                + Second[1].multiply((2 * ThirdOperand * FourthOperand)[:, None])
                + Second[2].multiply((FourthOperand * FourthOperand)[:, None])
            )
            Coordinates = 2 * (SpatialCoordinates[:, :2] - Low) / Span - 1
            for Matrix, Orders in zip(
                [FirstCurvatureRows, MixedCurvatureRows, SecondCurvatureRows],
                [(2, 0), (1, 1), (0, 2)],
            ):
                Values = chebder(
                    chebder(Coefficients, m=Orders[0], axis=0), m=Orders[1], axis=1
                )
                Target = (
                    chebval2d(*Coordinates.T, Values)
                    * (2 / Span[0]) ** Orders[0]
                    * (2 / Span[1]) ** Orders[1]
                )
                Matrices.append(Matrix * CurvatureWeight)
                Targets.append(Target * CurvatureWeight)
        SecondOperand, _, _ = ConstructRows(*Coordinates2d[Inside].T)
        SampleWeights = Weights[Inside]
        Matrices.append(SecondOperand.multiply((SampleWeights * 5)[:, None]))
        Targets.append(Samples[Inside, 2] * SampleWeights * 5)
        RimChecks = []
        for Side in range(4):
            Across = GridSamples(
                ActivePatch.KnotsU if Side in [0, 2] else ActivePatch.KnotsV, 3
            )
            if Side == 0:
                AlongParameters, AcrossParameters = Across, np.zeros(len(Across))
            elif Side == 1:
                AlongParameters, AcrossParameters = np.ones(len(Across)), Across
            elif Side == 2:
                AlongParameters, AcrossParameters = Across, np.ones(len(Across))
            else:
                AlongParameters, AcrossParameters = np.zeros(len(Across)), Across
            SecondOperand, FirstGradientRows, SecondGradientRows = ConstructRows(
                AlongParameters, AcrossParameters
            )
            Positions = SecondOperand @ ActivePatch.Poles.reshape(-1, 3)
            HeightValues, GradientValues = SamplePolynomialField(Positions[:, :2])
            External = len(Sides) == 4 or Side in [0, 3]
            if External:
                GradientValues = ProjectRimGradient(Positions)
            else:
                Tangent = (
                    Evaluate(ActivePatch, AlongParameters, AcrossParameters, 0, 1)
                    if Side in [1, 3]
                    else Evaluate(ActivePatch, AlongParameters, AcrossParameters, 1, 0)
                )
                Planar = Tangent[:, :2]
                Residual = Tangent[:, 2] - np.sum(GradientValues * Planar, axis=1)
                GradientValues += (
                    Planar * (Residual / np.sum(Planar**2, axis=1))[:, None]
                )
            Strength = 0.8
            RimChecks.append(
                (FirstGradientRows, SecondGradientRows, GradientValues, External)
            )
            Matrices.extend(
                [FirstGradientRows * Strength, SecondGradientRows * Strength]
            )
            Targets.extend(
                [GradientValues[:, 0] * Strength, GradientValues[:, 1] * Strength]
            )
        # Match physical gradients across internal C0 chart knots.
        for Axis, KnotValues in enumerate([ActivePatch.KnotsU, ActivePatch.KnotsV]):
            Cross = GridSamples(
                ActivePatch.KnotsV if Axis == 0 else ActivePatch.KnotsU, 3
            )
            Unique, Multiplicity = np.unique(KnotValues, return_counts=True)
            for SlotIndex in range(1, len(Unique) - 1):
                if Multiplicity[SlotIndex] < 3:
                    continue
                ParameterValues = Unique[SlotIndex]
                Delta = (
                    min(
                        ParameterValues - Unique[SlotIndex - 1],
                        Unique[SlotIndex + 1] - ParameterValues,
                    )
                    * 1e-7
                )
                BeforeAlong, BeforeAcross = (
                    (np.full(len(Cross), ParameterValues - Delta), Cross)
                    if Axis == 0
                    else (Cross, np.full(len(Cross), ParameterValues - Delta))
                )
                AfterAlong, AfterAcross = (
                    (np.full(len(Cross), ParameterValues + Delta), Cross)
                    if Axis == 0
                    else (Cross, np.full(len(Cross), ParameterValues + Delta))
                )
                _, BeforeFirstGradient, BeforeSecondGradient = ConstructRows(
                    BeforeAlong, BeforeAcross
                )
                _, AfterFirstGradient, AfterSecondGradient = ConstructRows(
                    AfterAlong, AfterAcross
                )
                Matrices.extend(
                    [
                        (AfterFirstGradient - BeforeFirstGradient) * 0.3,
                        (AfterSecondGradient - BeforeSecondGradient) * 0.3,
                    ]
                )
                Targets.extend([np.zeros(len(Cross))] * 2)
        Matrix = vstack(Matrices).tocsr()
        Target = np.concatenate(Targets)
        Old = ActivePatch.Poles[:, :, 2].ravel().copy()
        Fixed = np.zeros(ActivePatch.Poles.shape[:2], bool)
        Fixed[[0, -1], :] = True
        Fixed[:, [0, -1]] = True
        Free = ~Fixed.ravel()
        Reduced = Matrix[:, Free]
        Scale = np.sqrt(np.asarray(Reduced.power(2).sum(0))).ravel()
        assert np.all(Scale > 0)
        Reduced = Reduced.multiply(1 / Scale).tocsr()
        RightHandSide = Target - Matrix @ Old
        print("SOLVE", Reduced.shape, Reduced.nnz, flush=True)
        Change = spsolve((Reduced.T @ Reduced).tocsc(), Reduced.T @ RightHandSide)
        New = Old.copy()
        New[Free] += Change / Scale
        assert np.isfinite(New).all()
        ActivePatch.Poles[:, :, 2] = New.reshape(ActivePatch.Poles.shape[:2])
        Discrepancy = (
            Evaluate(ActivePatch, *Coordinates2d[Inside].T)[:, 2] - Samples[Inside, 2]
        ) * 1000
        Report = {
            "sampleCount": int(Inside.sum()),
            "maximumDisplacementMm": float(abs(Discrepancy).max()),
            "rmsDisplacementMm": float(
                np.sqrt(np.average(Discrepancy**2, weights=SampleWeights**2))
            ),
        }
        for External, Label in [(True, "rim"), (False, "interior")]:
            Angles = []
            for (
                FirstGradientRows,
                SecondGradientRows,
                GradientValues,
                AtRim,
            ) in RimChecks:
                if AtRim != External:
                    continue
                FirstOperand = np.column_stack(
                    [-GradientValues, np.ones(len(GradientValues))]
                )
                SecondOperand = np.column_stack(
                    [
                        -FirstGradientRows @ New,
                        -SecondGradientRows @ New,
                        np.ones(len(GradientValues)),
                    ]
                )
                ThirdOperand = (
                    np.sum(FirstOperand * SecondOperand, axis=1)
                    / np.linalg.norm(FirstOperand, axis=1)
                    / np.linalg.norm(SecondOperand, axis=1)
                )
                Angles.extend(np.degrees(np.arccos(np.clip(ThirdOperand, -1, 1))))
            if Angles:
                Report[Label + "NormalTargetMaximumDegrees"] = float(max(Angles))
                Report[Label + "NormalTargetRmsDegrees"] = float(
                    np.sqrt(np.mean(np.array(Angles) ** 2))
                )
        print("MEASURES", Report, flush=True)
        Measures.append(Report)
    for ActivePatch in Quads:
        ActivePatch.Poles = ActivePatch.Poles @ Axes.T
    return Quads, Measures, Axes
