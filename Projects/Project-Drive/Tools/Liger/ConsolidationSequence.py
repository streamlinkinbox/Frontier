#!/usr/bin/env python3
"""Consolidate the approved Liger guide selection without changing the body or erasing the candidate archive."""

import argparse
import copy
import hashlib
import itertools
import json
from pathlib import Path
import re
import numpy as np
from scipy.interpolate import BSpline
from ContourSequence import Coordinates


def ReadCurves(Text):
    Curves = {}
    for Line in Text.splitlines():
        if not Line.startswith("cpcurve ") or "--name=Guide_" not in Line:
            continue
        Name = re.search(r"--name=(\S+)", Line)[1]
        Poles = np.array([[float(V) for V in P.split(",")] for P in re.findall(r"\(([^)]+)\)", Line)])
        Degree = int(re.search(r"--degree=(\d+)", Line)[1])
        assert Degree == 3
        Explicit = re.search(r"--knots=(\S+)", Line)
        Knots = (
            np.array([float(V) for V in Explicit[1].split(",")])
            if Explicit
            else np.r_[np.zeros(4), np.linspace(0, 1, len(Poles) - 2)[1:-1], np.ones(4)]
        )
        assert len(Knots) == len(Poles) + 4
        Curves[Name] = (Poles, Knots)
    return Curves


def Join(Curves, Tolerance=0.0002):
    """Preserve cubic spans, orient nearest endpoints and weld only bounded endpoint discrepancies."""
    Choices = []
    for Directions in itertools.product([False, True], repeat=len(Curves)):
        Parts = [
            (P[::-1].copy(), 1 - K[::-1]) if Reverse else (P.copy(), K.copy())
            for (P, K), Reverse in zip(Curves, Directions)
        ]
        Gaps = [float(np.linalg.norm(A[0][-1] - B[0][0])) for A, B in zip(Parts, Parts[1:])]
        Choices.append((max(Gaps), sum(Gaps), Parts, Gaps, Directions))
    Maximum, _, Parts, Gaps, Directions = min(Choices, key=lambda C: C[:2])
    if Maximum > Tolerance:
        raise ValueError(f"Disconnected joins: {Gaps}")
    Lengths = [
        np.linalg.norm(np.diff(BSpline(K, P, 3)(np.linspace(0, 1, 1025)), axis=0), axis=1).sum() for P, K in Parts
    ]
    Stops = np.r_[0, np.cumsum(Lengths) / sum(Lengths)]
    Stops[-1] = 1
    Poles, Knots = [], [0.0] * 4
    for Index, ((P, K), A, B) in enumerate(zip(Parts, Stops[:-1], Stops[1:])):
        Poles.extend(P if Index == 0 else P[1:])
        Knots.extend(A + (B - A) * K[4:-4])
        Knots.extend([B] * (4 if Index == len(Parts) - 1 else 3))
    P, K = np.array(Poles), np.array(Knots)
    Spline = BSpline(K, P, 3)
    Error = 0.0
    for (Original, OriginalKnots), A, B in zip(Parts, Stops[:-1], Stops[1:]):
        Samples = np.linspace(0, 1, 513)
        Error = max(
            Error,
            float(
                np.linalg.norm(
                    Spline(A + (B - A) * Samples) - BSpline(OriginalKnots, Original, 3)(Samples), axis=1
                ).max()
            ),
        )
    assert Error <= Tolerance
    return (
        P,
        K,
        {
            "joinGapsMm": [G * 1000 for G in Gaps],
            "reversed": list(Directions),
            "joinParameters": Stops[1:-1].tolist(),
            "sampledSpanChangeMm": Error * 1000,
        },
    )


def Construct(Source, Destination):
    Text = Source.read_text()
    Metadata = json.loads(Source.with_suffix(".guides.json").read_text())
    assert hashlib.sha256(Source.read_bytes()).hexdigest() == Metadata["documentSha256"]
    Native = ReadCurves(Text)
    Records = Metadata["curves"]
    ByCode = {C["code"]: C for C in Records}
    Rejected = [f"Y{I:02d}" for I in range(1, 6)] + ["R03", "R04", "R05"]
    Deferred = ["M02"]
    Decisions, Retained, Representatives = [], [], {}
    for Curve in Records:
        Code = Curve["code"]
        if Code in Rejected or Code in Deferred:
            Decisions.append(
                {
                    "code": Code,
                    "action": "excluded",
                    "reason": "user rejected" if Code in Rejected else "connects rejected Y01 and consolidated G31",
                }
            )
            continue
        Relations = sorted(Curve.get("parallelTo", []), key=lambda R: R["medianSeparationMm"])
        Match = None
        if not Curve.get("pairedArchBorder"):
            for Relation in Relations:
                Peer = Records[Relation["family"] - 1]["code"]
                Representative, PriorDistance = Representatives[Peer]
                Distance = PriorDistance + Relation["medianSeparationMm"]
                if Distance <= 40 and Relation["overlapFraction"] >= 0.8:
                    Match = (Representative, Distance)
                    break
        if Match:
            Representatives[Code] = Match
            Decisions.append(
                {
                    "code": Code,
                    "action": "parallel suppressed",
                    "representative": Match[0],
                    "sampledMedianPathSeparationMm": Match[1],
                }
            )
        else:
            Representatives[Code] = (Code, 0.0)
            if not Curve["defaultVisible"]:
                Decisions.append(
                    {
                        "code": Code,
                        "action": "left hidden",
                        "reason": "not a close duplicate of retained guides; not shown in prior review",
                    }
                )
                continue
            Retained.append(copy.deepcopy(Curve))
            Decisions.append({"code": Code, "action": "retained"})
    Declarations = []
    Joined = []
    for Code, Description, Members in [
        ("C01", "JoinedRoofRail", ["B27", "M01", "B53"]),
        ("C02", "JoinedRearOuterArch", ["G92", "M03", "G50", "M04", "G91"]),
    ]:
        Parts = [Native[ByCode[Member]["names"][0]] for Member in Members]
        P, K, Proof = Join(Parts)
        Names = []
        for Side, Sign in [("Left", 1), ("Right", -1)]:
            Points = P * [1, Sign, 1]
            Name = f"Guide_{Code}_{Description}_{Side}"
            Names.append(Name)
            Declarations += [
                f"cpcurve {Coordinates(Points)} --degree=3 --knots="
                + ",".join(f"{V:.17g}" for V in K)
                + f" --name={Name}",
                f"feature design {Name}",
                f"tint {Name} .04 .48 1",
            ]
        Points = BSpline(K, P, 3)(np.linspace(0, 1, 4097))
        Joined.append(
            {
                "code": Code,
                "description": Description,
                "names": Names,
                "role": "joined approved guide spans",
                "defaultVisible": True,
                "lengthMetres": float(np.linalg.norm(np.diff(Points, axis=0), axis=1).sum()),
                "connects": Members,
                "controlPoles": len(P),
                **Proof,
            }
        )
        Retained = [C for C in Retained if C["code"] not in Members]
        for Decision in Decisions:
            if Decision["code"] in Members:
                Decision.update(action="joined", representative=Code)
            elif Decision.get("representative") in Members:
                Decision["representative"] = Code
    Retained += Joined
    AllNames = [Name for C in Records for Name in C["names"]]
    VisibleNames = [Name for C in Retained for Name in C["names"]]
    Lines = [
        Text.rstrip(),
        "# Consolidated approved guide review; rejected and superseded candidates retained hidden.",
        "hide " + " ".join(AllNames),
        *Declarations,
    ]
    Lines += ["unhide " + " ".join(VisibleNames)]
    for C in Retained:
        C["defaultVisible"] = True
        C.pop("parallelTo", None)
        for Name in C["names"]:
            Lines.append(f"tint {Name} " + ("1 .12 .16" if C["code"].startswith("R") else ".04 .48 1"))
    Lines += ["show cages off", "show iso off", "show edges off", "show features on", "view top", "view fit"]
    Destination.write_text("\n".join(Lines) + "\n")
    Report = {
        "sourceSha256": Metadata["documentSha256"],
        "documentSha256": hashlib.sha256(Destination.read_bytes()).hexdigest(),
        "bodyChanged": False,
        "rejected": Rejected,
        "dependentExcluded": Deferred,
        "curves": Retained,
        "decisions": Decisions,
        "visibleCurveCount": len(VisibleNames),
        "mirrorPairCount": sum(N.endswith("_Left") for N in VisibleNames),
        "parallelRule": "Prefer an existing longer representative; sampled median relation-path distance <=40 mm and overlap >=80%; protect both arch borders. Do not average or alter skin.",
        "caution": "Guide simplification proposal only, not patch merging. Joined curves are C0, not certified tangent-continuous. Original candidates remain hidden and recoverable.",
    }
    Destination.with_suffix(".guides.json").write_text(json.dumps(Report, indent=2) + "\n")
    print("Visible families", len(Retained), "curves", len(VisibleNames), "mirrored pairs", Report["mirrorPairCount"])
    print("Joined", json.dumps(Joined, indent=2))


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("Source", type=Path)
    Parser.add_argument("Destination", type=Path)
    Arguments = Parser.parse_args()
    Construct(Arguments.Source, Arguments.Destination)
