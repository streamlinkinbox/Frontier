#!/usr/bin/env python3
"""Name long native edge chains, compare close parallels, and project proposed continuations onto the repaired skin.
No stroke pixels are fitted, no body surface is changed, and a parallel relation does not authorize removal.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import numpy as np
from scipy.interpolate import BSpline, CubicSpline
from scipy.spatial import cKDTree
from ContourSequence import Trace, Resample, Fit, Coordinates

Colours = {
    "B": [0.04, 0.48, 1.0],
    "G": [0.12, 0.84, 0.28],
    "Y": [1.0, 0.80, 0.04],
    "M": [0.90, 0.12, 0.88],
    "R": [1.0, 0.12, 0.16],
}


def Direction(Points):
    Tangents = np.gradient(Points, axis=0)
    return Tangents / np.linalg.norm(Tangents, axis=1)[:, None]


def Parallel(First, Second, Distance=0.065):
    Separations, Indices = cKDTree(Second).query(First)
    Alignment = abs((Direction(First) * Direction(Second)[Indices]).sum(axis=1))
    Match = (Separations > 0.0015) & (Separations < Distance) & (Alignment > 0.93)
    return float(np.mean(Match)), float(np.median(Separations)), float(np.median(Alignment))


def TraceSpline(Chain, Edges):
    Curves, Lengths = [], []
    for Index, End in Chain["edges"]:
        Edge = Edges[Index]
        if Edge["degree"] != 3 or np.max(abs(np.asarray(Edge["poles"])[:, 3] - 1)) > 1e-12:
            raise ValueError("Expected polynomial cubic source boundaries")
        Knots = np.asarray(Edge["knots"])
        Poles = np.asarray(Edge["poles"])[:, :3] / np.asarray(Edge["poles"])[:, 3:4]
        Knots = (Knots - Knots[3]) / (Knots[-4] - Knots[3])
        if End:
            Knots, Poles = 1 - Knots[::-1], Poles[::-1]
        Curves.append((Knots, Poles))
        Lengths.append(np.linalg.norm(np.diff(np.asarray(Edge["points"])[:, :3], axis=0), axis=1).sum())
    Stops = np.r_[0, np.cumsum(Lengths) / sum(Lengths)]
    Stops[-1] = 1
    Poles = []
    Knots = [0.0] * 4
    Maximum = 0.0
    for Index, ((K, P), A, B) in enumerate(zip(Curves, Stops[:-1], Stops[1:])):
        if Index:
            Maximum = max(Maximum, float(np.linalg.norm(Poles[-1] - P[0])))
        Poles.extend(P if not Index else P[1:])
        Knots.extend(A + (B - A) * K[4:-4])
        Knots.extend([B] * (4 if Index == len(Curves) - 1 else 3))
    if Maximum > 4e-6:
        raise ValueError("Disconnected source edge chain")
    return (
        np.asarray(Poles),
        np.asarray(Knots),
        {
            "controlPoles": len(Poles),
            "sourceJoinMaximumMm": Maximum * 1000,
            "construction": "Exact source cubic spans; endpoint welding bounded by sourceJoinMaximumMm, not a pixel fit",
        },
    )


def Families(Chains, Minimum=0.8):
    Result, Seen = [], set()
    for Index in sorted(range(len(Chains)), key=lambda I: -Chains[I]["lengthMetres"]):
        if Chains[Index]["lengthMetres"] < Minimum or Index in Seen:
            continue
        Points = Resample(Chains[Index]["points"], 501)
        Reflected = Points.copy()
        Reflected[:, 1] *= -1
        Mirrored = []
        for Other, Chain in enumerate(Chains):
            if Other == Index or Other in Seen or abs(Chain["lengthMetres"] - Chains[Index]["lengthMetres"]) > 1e-5:
                continue
            Samples = Resample(Chain["points"], 501)
            if (
                min(
                    np.linalg.norm(Samples - Reflected, axis=1).max(),
                    np.linalg.norm(Samples[::-1] - Reflected, axis=1).max(),
                )
                < 1e-5
            ):
                Mirrored.append(Other)
        if Points[:, 1].mean() < -1e-5 and Mirrored:
            Index, Mirrored = Mirrored[0], [Index]
            Points = Resample(Chains[Index]["points"], 501)
        Seen.update([Index] + Mirrored)
        Relations = []
        for Number, Earlier in enumerate(Result):
            Fraction, Separation, Alignment = Parallel(Points, Earlier["points"])
            if Fraction > 0.75:
                Relations.append(
                    {
                        "family": Number + 1,
                        "overlapFraction": Fraction,
                        "medianSeparationMm": Separation * 1000,
                        "medianTangentAlignment": Alignment,
                    }
                )
        Number = len(Result) + 1
        Result.append(
            {
                "number": Number,
                "chain": Index,
                "mirroredChains": Mirrored,
                "points": Points,
                "lengthMetres": Chains[Index]["lengthMetres"],
                "parallelTo": Relations,
                "code": ("G" if Relations else "B") + f"{Number:02d}",
            }
        )
    return Result


class SurfaceProjection:
    """Triangle rays supply close seeds only; the native NURBS projection provides the final points."""

    def __init__(self, Triangles):
        self.Triangles = Triangles
        self.Search = {}
        for Axes in [(0, 1), (0, 2), (1, 2)]:
            Projected = Triangles[:, :, Axes]
            Centres = Projected.mean(axis=1)
            self.Search[Axes] = (cKDTree(Centres), float(np.linalg.norm(Projected - Centres[:, None], axis=2).max()))

    def Ray(self, Coordinates, Axes):
        Search, Radius = self.Search[Axes]
        Height = ({0, 1, 2} - set(Axes)).pop()
        Results = []
        for Point in Coordinates:
            Indices = Search.query_ball_point(Point, Radius + 1e-8)
            Triangles = self.Triangles[Indices]
            A = Triangles[:, 0, Axes]
            U = Triangles[:, 1, Axes] - A
            V = Triangles[:, 2, Axes] - A
            Difference = Point - A
            Determinant = U[:, 0] * V[:, 1] - U[:, 1] * V[:, 0]
            Valid = abs(Determinant) > 1e-12
            S = np.zeros(len(Indices))
            T = S.copy()
            S[Valid] = (Difference[Valid, 0] * V[Valid, 1] - Difference[Valid, 1] * V[Valid, 0]) / Determinant[Valid]
            T[Valid] = (U[Valid, 0] * Difference[Valid, 1] - U[Valid, 1] * Difference[Valid, 0]) / Determinant[Valid]
            Valid &= (S >= -1e-7) & (T >= -1e-7) & (S + T <= 1 + 1e-7)
            if not np.any(Valid):
                raise ValueError(f"Guide leaves the supplied skin at {Point}, axes {Axes}; do not bridge openings")
            XYZ = Triangles[:, 0] * (1 - S - T)[:, None] + Triangles[:, 1] * S[:, None] + Triangles[:, 2] * T[:, None]
            Results.append(XYZ[Valid][np.argmax(XYZ[Valid, Height])])
        return np.array(Results)


def Hermite(Begin, End, BeginDirection, EndDirection, Count=257):
    Parameters = np.linspace(0, 1, Count)[:, None]
    Distance = np.linalg.norm(End - Begin)
    A = BeginDirection / np.linalg.norm(BeginDirection) * Distance
    B = EndDirection / np.linalg.norm(EndDirection) * Distance
    return (
        (2 * Parameters**3 - 3 * Parameters**2 + 1) * Begin
        + (Parameters**3 - 2 * Parameters**2 + Parameters) * A
        + (-2 * Parameters**3 + 3 * Parameters**2) * End
        + (Parameters**3 - Parameters**2) * B
    )


def Construct(Source, EdgesPath, Native, Scratch, Destination):
    if (
        hashlib.sha256(Source.read_bytes()).hexdigest()
        != "68ac15e17440d73d6371eeebbcf1e088973062b0eb291ccbbcd1beb0b43631ae"
    ):
        raise ValueError("Candidate seeds require the approved first roof-repair revision")
    Scratch.mkdir(parents=True, exist_ok=True)
    Edges = json.loads(EdgesPath.read_text())
    if len(Edges) != 9107:
        raise ValueError("Expected the approved roof-repaired body, not the old body edge indices")
    Chains = Trace(Edges)
    Long = Families(Chains)
    Records, Declarations, Hidden = [], [], []

    def Add(Code, Description, Points, Mirrors, Role, Visible=True, Extra=None):
        Symmetric = np.linalg.norm(Points - Points[::-1] * [1, -1, 1], axis=1).max() < 1e-7
        if Extra and "sourceChain" in Extra:
            Poles, Knots, Metrics = TraceSpline(Chains[Extra["sourceChain"]], Edges)
        else:
            Poles, Metrics = Fit(Points, Symmetric, 0.00065)
            Knots = None
        Names = []
        for Side, Sign in ([("Left", 1), ("Right", -1)] if Mirrors else [("Across", 1)]):
            Name = f"Guide_{Code}_{Description}_{Side}"
            Names.append(Name)
            Reflected = Poles.copy()
            Reflected[:, 1] *= Sign
            Declarations.extend(
                [
                    "cpcurve "
                    + Coordinates(Reflected)
                    + " --degree=3 --name="
                    + Name
                    + (" --knots=" + ",".join(f"{K:.17g}" for K in Knots) if Knots is not None else ""),
                    "feature design " + Name,
                    "tint " + Name + " " + " ".join(map(str, Colours[Code[0]])),
                ]
            )
            if not Visible:
                Hidden.append(Name)
        Records.append(
            {
                "code": Code,
                "description": Description,
                "names": Names,
                "role": Role,
                "defaultVisible": Visible,
                "lengthMetres": float(np.linalg.norm(np.diff(Points, axis=0), axis=1).sum()),
                **Metrics,
                **(Extra or {}),
            }
        )
        print(
            "GUIDE",
            Code,
            Description,
            "fit_mm",
            Metrics.get("sampledMaximumFitMm", Metrics.get("sourceJoinMaximumMm")),
            flush=True,
        )

    Descriptions = {
        1: "OuterShoulder",
        2: "LowerCanopy",
        3: "UpperCanopy",
        4: "WindshieldFoot",
        5: "Centreline",
        6: "RearValance",
        7: "RearDeckRim",
        8: "RearUpperReturn",
        16: "LowerSideSweep",
        17: "FrontLowerReturn",
        18: "NoseReturn",
        19: "SideCrease",
        20: "RearInnerArch",
        22: "FrontOuterArch",
        23: "FrontInnerArch",
        24: "FrontLipParallel",
        27: "InnerRoofRail",
        31: "InnerRoofParallel",
        32: "RearShoulder",
        36: "UpperSideRail",
        43: "SideCreaseParallel",
        46: "UpperBeltRail",
        50: "RearOuterArch",
        53: "ForwardRoofRail",
    }
    Descriptions.update(
        {
            9: "RearReturnParallel",
            10: "RearDeckParallelA",
            11: "RearDeckParallelB",
            12: "RearLowerInset",
            13: "RearInsetParallelA",
            14: "RearDeckParallelC",
            15: "RearInsetParallelB",
            21: "RearDeckInnerRim",
            25: "RearDeckCrossbarOuter",
            26: "RearDeckCrossbarInner",
            28: "RearDeckCentreReturn",
            29: "RoofRearCrossbar",
            30: "BonnetRearCrossbar",
            33: "RoofFrontCrossbar",
            34: "RearDeckCentreInset",
            35: "FrontWingRail",
            37: "RearShoulderParallelA",
            38: "RearShoulderParallelB",
            39: "SideUpperDiagonal",
            40: "NoseInsetCrossbar",
            41: "RoofRepairCrossbar",
            42: "RoofForwardCrossbar",
            44: "NoseReturnParallel",
            45: "UpperDoorFrontTrace",
            47: "NoseLowerInset",
            48: "RearLowerCrossbar",
            49: "SideLowerParallel",
            51: "WaistRailParallel",
            52: "BonnetDiagonal",
            54: "RearCornerParallel",
            55: "WaistOuterParallel",
            56: "InnerRoofShortParallel",
        }
    )
    for Family in Long:
        Index = Family["chain"]
        Description = Descriptions.get(Family["number"], "BodyChain")
        Add(
            Family["code"],
            Description,
            Chains[Index]["points"],
            bool(Family["mirroredChains"]),
            "existing parallel candidate" if Family["parallelTo"] else "existing long body chain",
            Extra={
                "sourceChain": Index,
                "sourceEdges": Chains[Index]["edges"],
                "mirrorSourceChains": Family["mirroredChains"],
                "parallelTo": Family["parallelTo"],
                "pairedArchBorder": Family["number"] in [20, 22, 23, 50],
            },
        )
    # Include shorter parallel chains rather than silently dropping them at the long-curve threshold.
    Short = []
    for Index, Chain in enumerate(Chains):
        if not 0.18 <= Chain["lengthMetres"] < 0.8 or Chain["points"][:, 1].mean() < -0.0001:
            continue
        Points = Resample(Chain["points"], 201)
        Relations = []
        for Family in Long:
            Fraction, Separation, Alignment = Parallel(Points, Resample(Chains[Family["chain"]]["points"], 1501), 0.04)
            if Fraction > 0.8:
                Relations.append(
                    {
                        "family": Family["number"],
                        "overlapFraction": Fraction,
                        "medianSeparationMm": Separation * 1000,
                        "medianTangentAlignment": Alignment,
                    }
                )
        if Relations:
            Short.append((Index, Relations))
    ImportantShort = {31, 44, 54, 151, 315, 325, 383, 733, 762, 785, 846, 1051}
    for Number, (Index, Relations) in enumerate(Short, len(Long) + 1):
        Relation = min(Relations, key=lambda R: R["medianSeparationMm"])
        Add(
            f"G{Number:02d}",
            f'Parallel_{Long[Relation["family"]-1]["code"]}',
            Chains[Index]["points"],
            Chains[Index]["points"][:, 1].mean() > 0.001,
            "short existing parallel candidate",
            Index in ImportantShort,
            {"sourceChain": Index, "sourceEdges": Chains[Index]["edges"], "parallelTo": Relations},
        )
    # Both real rear-arch lower fragments are named as well as the upper outer contour.
    for Code, Index, Description in [("G91", 358, "RearOuterLowerTrailing"), ("G92", 278, "RearOuterLowerLeading")]:
        Add(
            Code,
            Description,
            Chains[Index]["points"],
            True,
            "existing arch continuation fragment",
            Extra={"sourceChain": Index, "sourceEdges": Chains[Index]["edges"]},
        )
    Triangles = np.fromfile(EdgesPath.with_suffix(".triangles.f64"), dtype="<f8").reshape(-1, 3, 3)
    Projection = SurfaceProjection(Triangles)
    Proposals = []

    def Proposed(Code, Description, Samples, Axes, Mirrors, Links):
        Points = Projection.Ray(Samples[:, Axes], Axes)
        Proposals.append(
            {"code": Code, "description": Description, "points": Points, "mirror": Mirrors, "links": Links}
        )

    def Ordered(Index, Axis=0):
        Points = Resample(Chains[Index]["points"], 501)
        return Points if Points[0, Axis] < Points[-1, Axis] else Points[::-1]

    def Extend(Code, Description, Index, Distance, AtEnd, Axes, Links):
        Points = Ordered(Index)
        Tip = Points[-1] if AtEnd else Points[0]
        Tangent = Points[-1] - Points[-8] if AtEnd else Points[0] - Points[7]
        Tangent /= abs(Tangent[0])
        Samples = Tip + np.linspace(0, Distance, 257)[:, None] * Tangent
        Proposed(Code, Description, Samples, Axes, True, Links)

    Extend("Y01", "InnerRoofContinuation", 776, 0.78, True, (0, 1), ["B27"])
    Extend("Y02", "ParallelRoofContinuation", 805, 0.80, True, (0, 1), ["G31"])
    Extend("Y03", "BeltRailContinuation", 17, 0.92, True, (0, 2), ["B46"])
    Extend("Y04", "UpperSideContinuation", 108, 0.55, False, (0, 2), ["B36"])
    Centre = np.column_stack([np.linspace(1.849, 2.49, 257), np.zeros(257), np.zeros(257)])
    Proposed("Y05", "BonnetCentreContinuation", Centre, (0, 1), False, ["B05"])
    First, Second = Ordered(776), Ordered(222)
    Proposed(
        "M01",
        "JoinInnerRoofToForwardRail",
        Hermite(First[-1], Second[0], First[-1] - First[-8], Second[8] - Second[0]),
        (0, 1),
        True,
        ["B27", "B53"],
    )
    First = Ordered(805)
    Target = Proposals[0]["points"][145]
    Previous = Proposals[0]["points"][135]
    Proposed(
        "M02",
        "ConvergeParallelRoofRails",
        Hermite(First[-1], Target, First[-1] - First[-8], Target - Previous),
        (0, 1),
        True,
        ["G31", "Y01"],
    )
    Crown = Ordered(549)
    for Code, Index, Tip, Description in [
        ("M03", 278, -1, "JoinRearArchLeading"),
        ("M04", 358, 0, "JoinRearArchTrailing"),
    ]:
        Lower = Ordered(Index, 2)[::-1]
        Begin = Crown[Tip]
        Tangent = Crown[-1] - Crown[-8] if Tip == -1 else Crown[0] - Crown[7]
        Proposed(
            Code,
            Description,
            Hermite(Begin, Lower[0], Tangent, Lower[8] - Lower[0]),
            (0, 2),
            True,
            ["G50", "G92" if Index == 278 else "G91"],
        )
    for Code, Height in [("R01", 0.56), ("R02", 0.64)]:
        Samples = np.column_stack([np.linspace(-0.60, 1.28, 401), np.zeros(401), np.full(401, Height)])
        Proposed(Code, "SideBeltOption", Samples, (0, 2), True, [])
    Samples = np.column_stack([np.linspace(-1.48, 1.30, 501), np.full(501, 0.79), np.zeros(501)])
    Proposed("R03", "UpperShoulderOption", Samples, (0, 1), True, [])
    Anchors = np.array([[-1.45, 0.26], [-0.60, 0.36], [0.50, 0.39], [0.95, 0.35], [1.16, 0.15], [1.18, 0]])
    Anchors = np.vstack([Anchors, Anchors[-2::-1] * [1, -1]])
    Parameters = np.r_[0, np.cumsum(np.linalg.norm(np.diff(Anchors, axis=0), axis=1))]
    XY = CubicSpline(Parameters, Anchors)(np.linspace(0, Parameters[-1], 701))
    Proposed("R04", "CanopyRailOption", np.column_stack([XY, np.zeros(len(XY))]), (0, 1), False, [])
    Samples = np.column_stack([np.linspace(0.98, 1.43, 257), np.zeros(257), np.linspace(1.115, 0.91, 257)])
    Proposed("R05", "WindshieldDiagonalOption", Samples, (0, 2), True, [])
    Query = Scratch / "GuideProjection.xyz"
    Output = Scratch / "GuideProjected.xyz"
    np.savetxt(Query, np.concatenate([P["points"] for P in Proposals]), fmt="%.17g")
    with (Scratch / "GuideProjection.log").open("w") as Log:
        subprocess.run(
            [str(Native), "--project", str(Source), str(Query), str(Output)],
            stdout=Log,
            stderr=subprocess.STDOUT,
            check=True,
        )
    Projected = np.loadtxt(Output)
    Offset = 0
    for Proposal in Proposals:
        Count = len(Proposal["points"])
        Samples = Projected[Offset : Offset + Count]
        Offset += Count
        if Samples[:, 6].max() > 0.004 or np.linalg.norm(np.diff(Samples[:, :3], axis=0), axis=1).max() > 0.035:
            raise ValueError(f'Projection crossed a hole or left the local skin: {Proposal["code"]}')
        Add(
            Proposal["code"],
            Proposal["description"],
            Samples[:, :3],
            Proposal["mirror"],
            (
                "on-surface join option"
                if Proposal["code"][0] == "M"
                else "on-surface sketch extension" if Proposal["code"][0] == "Y" else "on-surface design option"
            ),
            Extra={
                "connects": Proposal["links"],
                "nativeProjectionMaximumMm": float(Samples[:, 6].max() * 1000),
                "supportFaces": sorted(set(Samples[:, 3].astype(int).tolist())),
            },
        )
    Prior = Source.read_text()
    OldGuides = [
        Line.split("--name=")[-1]
        for Line in Prior.splitlines()
        if Line.startswith(("cpcurve ", "arc ")) and "--name=" in Line
    ]
    Lines = [
        Prior.rstrip(),
        "# Named curve candidates only. No patch merges or skin changes.",
        "require feature-curves",
        "require curve-knots",
        "hide " + " ".join(OldGuides),
        *Declarations,
    ]
    if Hidden:
        Lines.append("hide " + " ".join(Hidden))
    Lines += ["show cages off", "show iso off", "show edges on", "show features on", "view top", "view fit"]
    Destination.write_text("\n".join(Lines) + "\n")
    Report = {
        "sourceSha256": hashlib.sha256(Source.read_bytes()).hexdigest(),
        "documentSha256": hashlib.sha256(Destination.read_bytes()).hexdigest(),
        "bodyChanged": False,
        "longThresholdMetres": 0.8,
        "shortParallelMinimumMetres": 0.18,
        "tracingAngleDegrees": 12,
        "nativeLongChainCount": sum(C["lengthMetres"] >= 0.8 for C in Chains),
        "longFamilies": len(Long),
        "shortParallelFamilies": len(Short),
        "colours": Colours,
        "curves": Records,
        "caution": "Parallel proximity is not proof a curve or strip can be removed. Proposed lines are independent editable surface-fitted sketches, not live constraints. Surface-distance checks are sampled.",
    }
    Destination.with_suffix(".guides.json").write_text(json.dumps(Report, indent=2) + "\n")
    Destination.with_name("Liger_Guide_All_Candidates.arc").write_text(
        "# Run after Liger_Guide_Candidates.arc; reveal additional short parallel candidates.\nunhide "
        + " ".join(Hidden)
        + "\n"
    )
    print("WROTE", Destination, "FAMILIES", len(Records), "CURVES", sum(len(R["names"]) for R in Records), flush=True)


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("Source", type=Path)
    Parser.add_argument("Edges", type=Path)
    Parser.add_argument("Native", type=Path)
    Parser.add_argument("Scratch", type=Path)
    Parser.add_argument("Destination", type=Path)
    Arguments = Parser.parse_args()
    Construct(Arguments.Source, Arguments.Edges, Arguments.Native, Arguments.Scratch, Arguments.Destination)
