#!/usr/bin/env python3
"""Replace the first approved orange roof fan with an editable mirrored tensor skin.
Exact restrictions preserve the rest of the vehicle. A Coons perimeter and constrained
physical-height fairing replace the interior; soft tangency is not a Class-A certificate.
Native edge coefficients come from FeatureVerification, never annotation pixels.
"""

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from numpy.polynomial.chebyshev import chebvander2d, chebval2d, chebder
from scipy.interpolate import BSpline
from scipy.sparse import csr_matrix, vstack
from scipy.sparse.linalg import spsolve
from scipy.spatial import cKDTree
from MirrorSequence import Read, Points, Restrict
from PatchSequence import Patch, Refinement


def Cut(Curve, Low, High):
    for K in [Low, High]:
        M = np.count_nonzero(abs(Curve.t - K) < 1e-11)
        if M < 4:
            Curve = Curve.insert_knot(K, 4 - M)
    A = np.flatnonzero(abs(Curve.t - Low) < 1e-11)[0]
    B = np.flatnonzero(abs(Curve.t - High) < 1e-11)[-1]
    return BSpline((Curve.t[A : B + 1] - Low) / (High - Low), Curve.c[A : B - 3], 3)


def Reverse(C):
    return BSpline(1 - C.t[::-1], C.c[::-1], 3)


def Sides(P):
    return [
        BSpline(P.KnotsV, P.Poles[0], 3),
        BSpline(P.KnotsU, P.Poles[:, -1], 3),
        Reverse(BSpline(P.KnotsV, P.Poles[-1], 3)),
        Reverse(BSpline(P.KnotsU, P.Poles[:, 0], 3)),
    ]


def JoinCurves(Group, Stops=None):
    Curves = [C for _, C in Group]
    Lengths = np.array([np.linalg.norm(np.diff(C(np.linspace(0, 1, 33)), axis=0), axis=1).sum() for C in Curves])
    if Stops is None:
        Stops = np.r_[0, np.cumsum(Lengths) / Lengths.sum()]
        Stops[-1] = 1
    Poles = []
    Knots = [0.0] * 4
    for Index, (C, A, B) in enumerate(zip(Curves, Stops[:-1], Stops[1:])):
        if Index:
            assert np.linalg.norm(Poles[-1] - C.c[0]) < 1e-8
        Poles.extend(C.c if Index == 0 else C.c[1:])
        Knots.extend(A + (B - A) * C.t[4:-4])
        Knots.extend([B] * (4 if Index == len(Curves) - 1 else 3))
    return BSpline(Knots, np.array(Poles), 3), Stops


def AlignedStops(A, B, Tolerance):
    A = A.copy()
    B = B.copy()
    Pairs = []
    for I, V in enumerate(A):
        J = int(np.argmin(abs(B - V)))
        if abs(B[J] - V) < Tolerance and int(np.argmin(abs(A - B[J]))) == I:
            Pairs.append((I, J, (V + B[J]) / 2))
    for I, J, V in Pairs:
        A[I] = V
        B[J] = V
    assert np.all(np.diff(A) > 0) and np.all(np.diff(B) > 0)
    return A, B


def TensorRows(U, V, KnotsU, KnotsV, DerivativeU=0, DerivativeV=0):
    BU = BSpline(KnotsU, np.eye(len(KnotsU) - 4), 3)(U, nu=DerivativeU)
    BV = BSpline(KnotsV, np.eye(len(KnotsV) - 4), 3)(V, nu=DerivativeV)
    Rows = []
    Columns = []
    Values = []
    for I, (A, B) in enumerate(zip(BU, BV)):
        J = np.flatnonzero(A)
        K = np.flatnonzero(B)
        Columns.extend((J[:, None] * len(B) + K).ravel())
        Values.extend((A[J, None] * B[K]).ravel())
        Rows.extend([I] * (len(J) * len(K)))
    return csr_matrix((Values, (Rows, Columns)), shape=(len(U), BU.shape[1] * BV.shape[1]))


def Evaluate(P, U, V, DU=0, DV=0):
    return TensorRows(U, V, P.KnotsU, P.KnotsV, DU, DV) @ P.Poles.reshape(-1, 3)


def Parameters(P, XY):
    Grid = np.linspace(0, 1, 17)
    U, V = np.meshgrid(Grid, Grid, indexing="ij")
    UV = np.column_stack([U.ravel(), V.ravel()])
    Samples = Points(P, UV.T)
    Result = UV[cKDTree(Samples[:, :2]).query(XY)[1]].copy()
    for _ in range(14):
        Value = Evaluate(P, *Result.T)[:, :2]
        DU = Evaluate(P, *Result.T, 1)[:, :2]
        DV = Evaluate(P, *Result.T, 0, 1)[:, :2]
        J = np.stack([DU, DV], axis=-1)
        Delta = np.linalg.solve(J, (XY - Value)[..., None])[..., 0]
        Result = np.clip(Result + Delta, 0, 1)
    return Result


def Gradient(P, XY):
    UV = Parameters(P, XY)
    DU = Evaluate(P, *UV.T, 1)
    DV = Evaluate(P, *UV.T, 0, 1)
    N = np.cross(DU, DV)
    assert np.min(abs(N[:, 2])) > 1e-12
    return -N[:, :2] / N[:, 2:3]


def GridSamples(K, Count):
    Unique = np.unique(K)
    return np.concatenate([np.linspace(A, B, Count + 2)[1:-1] for A, B in zip(Unique[:-1], Unique[1:])])


def Crop(P, Axis, Low, High):
    R = deepcopy(P)
    K = R.KnotsU if Axis == 0 else R.KnotsV
    C = Cut(BSpline(K, np.moveaxis(R.Poles, Axis, 0), 3), Low, High)
    R.Poles = np.moveaxis(C.c, 0, Axis)
    if Axis == 0:
        R.KnotsU = C.t
    else:
        R.KnotsV = C.t
    return R


def Mirror(P):
    R = deepcopy(P)
    R.Poles = P.Poles[::-1].copy()
    R.Poles[:, :, 1] *= -1
    R.KnotsU = 1 - P.KnotsU[::-1]
    return R


def Declaration(P, Name):
    XYZ = " ".join("(" + ",".join(f"{Value:.15g}" for Value in Q) + ")" for Q in P.Poles.reshape(-1, 3))
    KU = ",".join(f"{V:.17g}" for V in P.KnotsU)
    KV = ",".join(f"{V:.17g}" for V in P.KnotsV)
    return f"patch {P.Poles.shape[0]} {P.Poles.shape[1]} {XYZ} --degree=3 --knots-u={KU} --knots-v={KV} --name={Name}"


def Run(ContentRoot, NativePath):
    Source = ContentRoot / "Liger_Main_Body_Simplified.arc"
    if (
        hashlib.sha256(Source.read_bytes()).hexdigest()
        != "8ce67d6fcf329e5fae8266c106f3de372e0d97859121773f712f2af06067fd23"
    ):
        raise ValueError("Roof repair selection requires the accepted simplified source revision")
    Surfaces = Read(Source)
    Full = [80, 82, 158, 160, 162, 188, 222, 224, 310, 632, 682, 1038, 1040, 1116]
    Partial = {102: (1, 1 / 6), 182: (0, 0.125), 680: (0, 0.4)}
    Pieces = {I: Surfaces[I] for I in Full}
    Retained = {}
    for I, (Axis, Value) in Partial.items():
        T = np.eye(3)
        T[Axis, Axis] = Value
        Pieces[I] = Restrict(Surfaces[I], T, I)
        T = np.eye(3)
        T[Axis, Axis] = 1 - Value
        T[Axis, 2] = Value
        Retained[I] = Restrict(Surfaces[I], T, I)
    Edges = defaultdict(list)
    for I, P in Pieces.items():
        for Side, C in enumerate(Sides(P)):
            K, M = np.unique(C.t, return_counts=True)
            K = K[M >= 3]
            for A, B in zip(K[:-1], K[1:]):
                E = Cut(C, A, B)
                Ends = E([0, 1])
                if np.linalg.norm(Ends[0] - Ends[1]) < 1e-10:
                    continue
                Key = tuple(sorted(tuple(np.round(V, 8)) for V in Ends))
                Edges[Key].append((I, Side, E))
    Rim = [V[0] for V in Edges.values() if len(V) == 1]
    print("PIECES", len(Pieces), "RIM", len(Rim), "EDGE_GROUPS", Counter(map(len, Edges.values())), flush=True)
    ByStart = defaultdict(list)
    for Index, (_, _, C) in enumerate(Rim):
        for End in [0, 1]:
            ByStart[tuple(np.round(C(End), 8))].append((Index, End))
    assert all(len(V) == 2 for V in ByStart.values()), [(K, V) for K, V in ByStart.items() if len(V) != 2]
    Ordered = []
    Next = (0, 0)
    while Next[0] not in [R[0] for R in Ordered]:
        I, E = Next
        C = Rim[I][2]
        C = Reverse(C) if E else C
        Ordered.append((I, C))
        Next = next(V for V in ByStart[tuple(np.round(C(1), 8))] if V[0] != I)
    assert len(Ordered) == len(Rim)
    Corners = np.array([C(0) for _, C in Ordered])
    print("Bounds", Corners.min(0), Corners.max(0))
    # Four perimeter corners, each on an existing chart knot.
    Targets = np.array([[0.00686295, 0], [0.300823, 0], [0.300823, 0.566633], [0.00686295, 0.558022]])
    Indices = [int(np.argmin(np.linalg.norm(Corners[:, :2] - P, axis=1))) for P in Targets]
    print("CORNERS", [(I, Corners[I]) for I in Indices])
    # Orient the cycle from the low-X centreline corner toward high X.
    Start = Indices[0]
    Ordered = Ordered[Start:] + Ordered[:Start]
    if Ordered[0][1](1)[1] > 1e-8:
        Ordered = [(I, Reverse(C)) for I, C in Ordered[::-1]]
    Corners = np.array([C(0) for _, C in Ordered])
    Indices = [int(np.argmin(np.linalg.norm(Corners[:, :2] - P, axis=1))) for P in Targets]
    assert Indices == sorted(Indices)
    Groups = [Ordered[A:B] for A, B in zip(Indices, Indices[1:] + [len(Ordered)])]
    # Source selection must overlap the approved orange curves, not merely a nearby box.
    Native = json.loads(NativePath.read_text())
    if len(Native) != 9150:
        raise ValueError("Expected the original simplified body edge export, not a repaired or unrelated body")
    for Edge in Native:
        Edge["index"] = Edge["edge"]
        Poles = np.asarray(Edge["poles"])
        if not np.all(Poles[:, 3] == 1):
            raise ValueError("Expected polynomial source edges")
        Edge["poles"] = Poles[:, :3].tolist()
    Approved = set(json.loads((ContentRoot / "Liger_Feature_Review.features.json").read_text())["repairEdges"])
    Sewn = next(L for L in Source.read_text().splitlines() if L.startswith("sew ")).split()[1:-3]
    FaceSource = [int(N.rsplit("_", 1)[1]) for N in Sewn]
    Selected = [
        E["index"]
        for E in Native
        if any(FaceSource[F] in [1038, 1040, 1116] for F in E["faces"]) and E["index"] in Approved
    ]
    print("APPROVED_ORANGE_FAN_EDGES", Selected)
    assert Selected
    for I, G in enumerate(Groups):
        print("GROUP", I, len(G), G[0][1](0), G[-1][1](1))
    # Native subdivision boundary endpoints must remain exact after concatenation.
    Bottom, BottomStops = JoinCurves(Groups[0])
    Right, RightStops = JoinCurves(Groups[1])
    Top, TopStops = JoinCurves([(I, Reverse(C)) for I, C in Groups[2][::-1]])
    Left, LeftStops = JoinCurves([(I, Reverse(C)) for I, C in Groups[3][::-1]])

    # Corresponding original chart endpoints share a parameter, avoiding artificial sliver spans.
    BottomStops, TopStops = AlignedStops(BottomStops, TopStops, 0.02)
    LeftStops, RightStops = AlignedStops(LeftStops, RightStops, 0.01)
    Bottom, _ = JoinCurves(Groups[0], BottomStops)
    Right, _ = JoinCurves(Groups[1], RightStops)
    Top, _ = JoinCurves([(I, Reverse(C)) for I, C in Groups[2][::-1]], TopStops)
    Left, _ = JoinCurves([(I, Reverse(C)) for I, C in Groups[3][::-1]], LeftStops)
    for First, Second in [(Bottom, Top), (Left, Right)]:
        for I, V in enumerate(Second.t):
            J = int(np.argmin(abs(First.t - V)))
            if abs(First.t[J] - V) < 1e-12:
                Second.t[I] = First.t[J]

    # Coons map retains all four original spatial boundary curves, not straight substitutes.
    KU = np.array(sorted((Counter(Bottom.t) | Counter(Top.t)).elements()))
    KV = np.array(sorted((Counter(Left.t) | Counter(Right.t)).elements()))
    BU = Refinement(Bottom.t, KU) @ Bottom.c
    TU = Refinement(Top.t, KU) @ Top.c
    LV = Refinement(Left.t, KV) @ Left.c
    RV = Refinement(Right.t, KV) @ Right.c
    GU = np.array([np.mean(KU[I + 1 : I + 4]) for I in range(len(KU) - 4)])
    GV = np.array([np.mean(KV[I + 1 : I + 4]) for I in range(len(KV) - 4)])
    Poles = (
        (1 - GV[None, :, None]) * BU[:, None]
        + GV[None, :, None] * TU[:, None]
        + (1 - GU[:, None, None]) * LV[None]
        + GU[:, None, None] * RV[None]
    )
    Poles -= (1 - GU[:, None, None]) * ((1 - GV[None, :, None]) * BU[0] + GV[None, :, None] * TU[0]) + GU[
        :, None, None
    ] * ((1 - GV[None, :, None]) * BU[-1] + GV[None, :, None] * TU[-1])
    Surface = Patch(Poles, KU, KV, np.zeros((2, 2), int), {})
    print("COONS_POLES", Poles.shape, flush=True)
    # Fit a smooth physical-space height target; quadrature weights avoid over-weighting tiny fans.
    Samples = []
    Weights = []
    for P in Pieces.values():
        G = np.linspace(0.01, 0.99, 25)
        U, V = np.meshgrid(G, G, indexing="ij")
        U, V = U.ravel(), V.ravel()
        XYZ = Evaluate(P, U, V)
        DU = Evaluate(P, U, V, 1)
        DV = Evaluate(P, U, V, 0, 1)
        Samples.append(XYZ)
        Weights.append(np.sqrt(abs(np.cross(DU, DV)[:, 2])))
    Samples = np.concatenate(Samples)
    Weights = np.concatenate(Weights)
    Low = Samples[:, :2].min(0)
    Span = Samples[:, :2].max(0) - Low
    A = 2 * (Samples[:, :2] - Low) / Span - 1
    Design = chebvander2d(A[:, 0], A[:, 1], [5, 8])
    Coefficients = np.linalg.lstsq(Design * Weights[:, None], Samples[:, 2] * Weights, rcond=None)[0].reshape(6, 9)
    PolynomialErrors = (Design @ Coefficients.ravel() - Samples[:, 2]) * 1000
    print(
        "POLYNOMIAL_TARGET_MM",
        abs(PolynomialErrors).max(),
        np.sqrt(np.average(PolynomialErrors**2, weights=Weights**2)),
        flush=True,
    )

    def Target(XY):
        A = 2 * (XY - Low) / Span - 1
        Z = chebval2d(A[:, 0], A[:, 1], Coefficients)
        G = np.column_stack(
            [chebval2d(A[:, 0], A[:, 1], chebder(Coefficients, axis=I)) * 2 / Span[I] for I in range(2)]
        )
        return Z, G

    def PhysicalRows(U, V):
        B = TensorRows(U, V, KU, KV)
        DU = TensorRows(U, V, KU, KV, 1)
        DV = TensorRows(U, V, KU, KV, 0, 1)
        A = DU @ Poles.reshape(-1, 3)
        C = DV @ Poles.reshape(-1, 3)
        Det = A[:, 0] * C[:, 1] - A[:, 1] * C[:, 0]
        if np.min(Det) <= 0:
            raise ValueError(("Folded map", np.min(Det)))
        DX = DU.multiply((C[:, 1] / Det)[:, None]) - DV.multiply((A[:, 1] / Det)[:, None])
        DY = DV.multiply((A[:, 0] / Det)[:, None]) - DU.multiply((C[:, 0] / Det)[:, None])
        return B, DX.tocsr(), DY.tocsr()

    U, V = np.meshgrid(GridSamples(KU, 4), GridSamples(KV, 4), indexing="ij")
    U, V = U.ravel(), V.ravel()
    B, DX, DY = PhysicalRows(U, V)
    Z, G = Target((B @ Poles.reshape(-1, 3))[:, :2])
    Matrices = [B * 5, DX * 0.012, DY * 0.012]
    Values = [Z * 5, G[:, 0] * 0.012, G[:, 1] * 0.012]
    # Strong boundary tangent-plane targets are taken from adjacent unchanged source skin.
    NativeByEnds = {tuple(sorted(tuple(np.round(np.array(E["poles"])[J], 8)) for J in [0, -1])): E for E in Native}
    PerimeterError = 0.0
    BoundaryChecks = []
    BaselineAngles = []
    for Side, (Group, Stops) in enumerate(
        [
            (Groups[0], BottomStops),
            (Groups[1], RightStops),
            ([(I, Reverse(C)) for I, C in Groups[2][::-1]], TopStops),
            ([(I, Reverse(C)) for I, C in Groups[3][::-1]], LeftStops),
        ]
    ):
        for (Index, C), A, T in zip(Group, Stops[:-1], Stops[1:]):
            W = np.linspace(0.0001, 0.9999, 17)
            Q = A + (T - A) * W
            XYZ = C(W)
            Identity = Rim[Index][0]
            Key = tuple(sorted(tuple(np.round(C(J), 8)) for J in [0, 1]))
            N = NativeByEnds.get(Key)
            Adjacent = [FaceSource[F] for F in N["faces"] if FaceSource[F] not in Pieces] if N else []
            Support = Surfaces[Adjacent[0] if Adjacent else Identity]
            GradientTarget = Gradient(Support, XYZ[:, :2])
            if Side == 0:
                UU, VV = Q, np.zeros(len(Q))
                GradientTarget[:, 1] = 0
            elif Side == 1:
                UU, VV = np.ones(len(Q)), Q
            elif Side == 2:
                UU, VV = Q, np.ones(len(Q))
            else:
                UU, VV = np.zeros(len(Q)), Q
            BB, XX, YY = PhysicalRows(UU, VV)
            PerimeterError = max(PerimeterError, float(np.linalg.norm(BB @ Poles.reshape(-1, 3) - XYZ, axis=1).max()))
            BeforeGradient = Gradient(Pieces[Identity], XYZ[:, :2])
            N0 = np.column_stack([-BeforeGradient, np.ones(len(W))])
            N1 = np.column_stack([-GradientTarget, np.ones(len(W))])
            Cos = np.sum(N0 * N1, axis=1) / np.linalg.norm(N0, axis=1) / np.linalg.norm(N1, axis=1)
            BaselineAngles.extend(np.degrees(np.arccos(np.clip(Cos, -1, 1))))
            Matrices.extend([XX * 0.3, YY * 0.3])
            Values.extend([GradientTarget[:, 0] * 0.3, GradientTarget[:, 1] * 0.3])
            BoundaryChecks.append((Side, UU, VV, GradientTarget))
    # A C0 spline basis is needed for exact source segmentation. Match physical gradients
    # across its interior knots rather than assuming parametric C1 means geometric G1.
    for Axis, K in enumerate([KU, KV]):
        Cross = GridSamples(KV if Axis == 0 else KU, 9)
        for Value in np.unique(K)[1:-1]:
            Delta = (
                min(Value - np.unique(K)[np.unique(K) < Value][-1], np.unique(K)[np.unique(K) > Value][0] - Value)
                * 1e-6
            )
            U0, V0 = (
                (np.full(len(Cross), Value - Delta), Cross)
                if Axis == 0
                else (Cross, np.full(len(Cross), Value - Delta))
            )
            U1, V1 = (
                (np.full(len(Cross), Value + Delta), Cross)
                if Axis == 0
                else (Cross, np.full(len(Cross), Value + Delta))
            )
            _, X0, Y0 = PhysicalRows(U0, V0)
            _, X1, Y1 = PhysicalRows(U1, V1)
            Matrices.extend([(X1 - X0) * 0.5, (Y1 - Y0) * 0.5])
            Values.extend([np.zeros(len(Cross))] * 2)
    Matrix = vstack(Matrices).tocsr()
    Value = np.concatenate(Values)
    Fixed = np.zeros(Poles.shape[:2], bool)
    Fixed[[0, -1], :] = True
    Fixed[:, [0, -1]] = True
    Fixed = Fixed.ravel()
    Free = ~Fixed
    Old = Poles[:, :, 2].ravel()
    RHS = Value - Matrix @ Old
    Reduced = Matrix[:, Free]
    Scale = np.sqrt(np.asarray(Reduced.power(2).sum(0))).ravel()
    Reduced = Reduced.multiply(1 / Scale).tocsr()
    print("SOLVE", Reduced.shape, Reduced.nnz, flush=True)
    Solution = spsolve((Reduced.T @ Reduced).tocsc(), Reduced.T @ RHS)
    New = Old.copy()
    New[Free] += Solution / Scale
    Surface.Poles[:, :, 2] = New.reshape(Poles.shape[:2])
    print("RESIDUAL", np.linalg.norm(Matrix @ New - Value), flush=True)
    Angles = []
    for Side, U, V, G in BoundaryChecks:
        _, XX, YY = PhysicalRows(U, V)
        F = np.column_stack([XX @ New, YY @ New])
        N0 = np.column_stack([-G, np.ones(len(G))])
        N1 = np.column_stack([-F, np.ones(len(F))])
        Cos = np.sum(N0 * N1, axis=1) / np.linalg.norm(N0, axis=1) / np.linalg.norm(N1, axis=1)
        Angles.extend(np.degrees(np.arccos(np.clip(Cos, -1, 1))))
    BoundaryMaximum = float(max(Angles))
    BoundaryRms = float(np.sqrt(np.mean(np.array(Angles) ** 2)))
    print("BOUNDARY_NORMAL_DEGREES_MAX_RMS", BoundaryMaximum, BoundaryRms, flush=True)
    print(
        "BASELINE_BOUNDARY_DEGREES_MAX_RMS",
        max(BaselineAngles),
        np.sqrt(np.mean(np.array(BaselineAngles) ** 2)),
        flush=True,
    )
    UV = Parameters(Surface, Samples[:, :2])
    Error = (Evaluate(Surface, *UV.T)[:, 2] - Samples[:, 2]) * 1000
    print(
        "SOURCE_XY_INVERSION_MAXIMUM_MM",
        np.linalg.norm(Evaluate(Surface, *UV.T)[:, :2] - Samples[:, :2], axis=1).max() * 1000,
        "MAX_HEIGHT_POSITION",
        Samples[np.argmax(abs(Error))],
        flush=True,
    )
    print(
        "SOURCE_HEIGHT_CHANGE_MM_MAX_RMS",
        abs(Error).max(),
        np.sqrt(np.average(Error**2, weights=Weights**2)),
        flush=True,
    )

    for Side, U, V, G in BoundaryChecks:
        BB, XX, YY = PhysicalRows(U, V)
        F = np.column_stack([XX @ New, YY @ New])
        N0 = np.column_stack([-G, np.ones(len(G))])
        N1 = np.column_stack([-F, np.ones(len(F))])
        Cos = np.sum(N0 * N1, axis=1) / np.linalg.norm(N0, axis=1) / np.linalg.norm(N1, axis=1)
        Angles = np.degrees(np.arccos(np.clip(Cos, -1, 1)))
        J = np.argmax(Angles)
        if Angles[J] > 0.4:
            print("RIM_OUTLIER", Side, (BB @ Surface.Poles.reshape(-1, 3))[J], Angles[J], G[J], F[J])

    Removed = set(Full + [I + 1 for I in Full])
    Replaced = set(Partial) | {I + 1 for I in Partial}
    Changed = Removed | Replaced
    for I in Pieces:
        assert np.max(abs(Mirror(Surfaces[I]).Poles - Surfaces[I + 1].Poles)) < 1e-10
    Original = (ContentRoot / "Liger_Feature_Aligned.arc").read_text()
    Lines = [L + " --keep" if L.startswith("sew ") else L for L in Original.splitlines()]
    Lines += [
        "# First orange-area repair: source junction and exact mirrored counterpart.",
        "require boundary-splits",
        "delete Liger_Main_Body",
        "delete " + " ".join(f"Main_{I:04d}" for I in sorted(Changed)),
    ]
    for I, P in Retained.items():
        Lines.extend([Declaration(P, f"Main_{I:04d}"), Declaration(Mirror(P), f"Main_{I+1:04d}")])
    NewNames = []
    for I, (U0, U1) in enumerate([(0, 0.5), (0.5, 1)]):
        for J, (V0, V1) in enumerate([(0, 0.5), (0.5, 1)]):
            P = Crop(Crop(Surface, 0, U0, U1), 1, V0, V1)
            for Side, Q in [("Left", P), ("Right", Mirror(P))]:
                Name = f"Roof_Repair_{Side}_{I}{J}"
                NewNames.append(Name)
                Lines.append(Declaration(Q, Name))
    Names = [N for N in Sewn if int(N.rsplit("_", 1)[1]) not in Removed] + NewNames
    Lines.append("sew " + " ".join(Names) + " --open --knot-edges --split-junctions --name=Liger_Main_Body")
    Hidden = []
    for E in Native:
        if E["index"] not in Approved:
            continue
        XYZ = np.array(E["poles"])
        XYZ[:, 1] = abs(XYZ[:, 1])
        Middle = XYZ.mean(0)
        if any(FaceSource[F] in Changed for F in E["faces"]) and 0.0068 <= Middle[0] <= 0.302 and Middle[1] <= 0.568:
            Hidden.append(E["index"])
    Prefix = re.search(r"feature-copy .*?--name=([^\s]+)", Original)[1]
    Lines.append("hide " + " ".join(f"{Prefix}.e{I}" for I in Hidden))
    Lines += ["show cages off", "show iso off", "show edges on", "show features on", "view top", "view fit"]
    Destination = ContentRoot / "Liger_Roof_Repair.arc"
    DocumentText = "\n".join(Lines) + "\n"

    # These are sampled diagnostics, not continuous Hausdorff or G1/G2 certificates.
    JumpAngles = []
    CentralJumpAngles = []
    for Axis, K in enumerate([KU, KV]):
        Cross = GridSamples(KV if Axis == 0 else KU, 13)
        for Value in np.unique(K)[1:-1]:
            Delta = (
                min(Value - np.unique(K)[np.unique(K) < Value][-1], np.unique(K)[np.unique(K) > Value][0] - Value)
                * 1e-7
            )
            U0, V0 = (
                (np.full(len(Cross), Value - Delta), Cross)
                if Axis == 0
                else (Cross, np.full(len(Cross), Value - Delta))
            )
            U1, V1 = (
                (np.full(len(Cross), Value + Delta), Cross)
                if Axis == 0
                else (Cross, np.full(len(Cross), Value + Delta))
            )
            _, X0, Y0 = PhysicalRows(U0, V0)
            _, X1, Y1 = PhysicalRows(U1, V1)
            N0 = np.column_stack([-X0 @ New, -Y0 @ New, np.ones(len(Cross))])
            N1 = np.column_stack([-X1 @ New, -Y1 @ New, np.ones(len(Cross))])
            Cos = np.sum(N0 * N1, axis=1) / np.linalg.norm(N0, axis=1) / np.linalg.norm(N1, axis=1)
            LocalAngles = np.degrees(np.arccos(np.clip(Cos, -1, 1)))
            JumpAngles.extend(LocalAngles)
            Keep = (U0 > 0.05) & (U0 < 0.95) & (V0 > 0.05) & (V0 < 0.95)
            CentralJumpAngles.extend(LocalAngles[Keep])
            if LocalAngles.max() > 0.2:
                I = int(np.argmax(LocalAngles))
                print("JUMP_OUTLIER", Axis, Value, LocalAngles[I], (Evaluate(Surface, U0, V0))[I], flush=True)
    Report = {
        "sourceSha256": hashlib.sha256(Source.read_bytes()).hexdigest(),
        "acceptedGuidesSha256": hashlib.sha256((ContentRoot / "Liger_Feature_Aligned.arc").read_bytes()).hexdigest(),
        "documentSha256": hashlib.sha256(DocumentText.encode()).hexdigest(),
        "scope": "First mirrored roof fan only; other orange areas and arch skin integration remain pending.",
        "positiveSourcePatches": Full,
        "exactPartialRestrictions": {str(I): {"axis": Axis, "cut": Cut} for I, (Axis, Cut) in Partial.items()},
        "approvedOrangeFanEdges": Selected,
        "newSurfaceCount": 8,
        "sewnFacesBefore": 1094,
        "sewnFacesAfter": len(Names),
        "unchangedWholeBodySurfaces": 1060,
        "independentJunctionSheetsUnchanged": 24,
        "hiddenCompletedOrangeCopies": Hidden,
        "removedOrangeCopies": 0,
        "replacementParentPoles": list(Surface.Poles.shape[:2]),
        "sampledPerimeterErrorMetres": PerimeterError,
        "sourceHeightChangeSampleCount": len(Error),
        "sampledHeightChangeMaximumMm": float(abs(Error).max()),
        "areaWeightedHeightChangeRmsMm": float(np.sqrt(np.average(Error**2, weights=Weights**2))),
        "sampledRimNormalBeforeMaximumDegrees": float(max(BaselineAngles)),
        "sampledRimNormalBeforeRmsDegrees": float(np.sqrt(np.mean(np.array(BaselineAngles) ** 2))),
        "sampledRimNormalAfterMaximumDegrees": BoundaryMaximum,
        "sampledRimNormalAfterRmsDegrees": BoundaryRms,
        "sampledInteriorKnotNormalJumpMaximumDegrees": float(max(JumpAngles)),
        "sampledCentralKnotNormalJumpMaximumDegrees": float(max(CentralJumpAngles)),
        "continuityCertificate": False,
        "note": "Exact spatial perimeter; soft tangent targets. Locked source-rim corner mismatches remain. No watertight or Class-A claim.",
    }
    print("INTERIOR_KNOT_NORMAL_JUMP_MAXIMUM_DEGREES", max(JumpAngles), "CENTRAL", max(CentralJumpAngles), flush=True)
    if (
        PerimeterError > 1e-10
        or not np.isfinite(New).all()
        or abs(Error).max() > 0.5
        or len(Names) != 1074
        or BoundaryMaximum > 2
        or max(JumpAngles) > 1.7
        or max(CentralJumpAngles) > 0.2
    ):
        raise RuntimeError("Repair exceeded its bounded first-pass acceptance limits")
    Destination.write_text(DocumentText)
    Destination.with_suffix(".repair.json").write_text(json.dumps(Report, indent=2) + "\n")
    print("WROTE", Destination, "FACES", len(Names), "HIDDEN_COMPLETED_ORANGE_COPIES", len(Hidden), flush=True)


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument(
        "NativeEdges", type=Path, help="FeatureVerification source edge JSON, including homogeneous poles"
    )
    Parser.add_argument(
        "--content", type=Path, default=Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"
    )
    Arguments = Parser.parse_args()
    Run(Arguments.content, Arguments.NativeEdges)
