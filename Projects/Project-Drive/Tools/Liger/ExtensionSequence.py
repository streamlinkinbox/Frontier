#!/usr/bin/env python3
"""Extend approved Liger guides to neighbouring curves and cut exact roof faces along the new continuations."""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import numpy as np
from scipy.interpolate import BSpline
from scipy.optimize import brentq, minimize_scalar
from ConsolidationSequence import ReadCurves, Join
from ContourSequence import Coordinates, Fit
from GuideSequence import SurfaceProjection, Hermite
from MirrorSequence import Read, Points
from RoofSequence import Crop, Declaration


def Isoline(Patch, Axis, Value):
    if Axis == 0:
        Basis = BSpline(Patch.KnotsU, np.eye(Patch.Poles.shape[0]), 3)(Value)
        return np.einsum("i,ijc->jc", Basis, Patch.Poles), Patch.KnotsV.copy()
    Basis = BSpline(Patch.KnotsV, np.eye(Patch.Poles.shape[1]), 3)(Value)
    return np.einsum("j,ijc->ic", Basis, Patch.Poles), Patch.KnotsU.copy()


def Spline(Curve):
    return BSpline(Curve[1], Curve[0], 3)


def Reflect(Curve):
    return Curve[0] * [1, -1, 1], Curve[1].copy()


def Nearest(Curve, Point):
    T = np.linspace(0, 1, 10001)
    Index = int(np.linalg.norm(Curve(T) - Point, axis=1).argmin())
    Result = minimize_scalar(
        lambda U: np.linalg.norm(Curve(U) - Point),
        bounds=(T[max(0, Index - 2)], T[min(10000, Index + 2)]),
        method="bounded",
        options={"xatol": 1e-15},
    )
    return float(Result.x), float(Result.fun)


def AtHeight(Curve, Height, NearX):
    T = np.linspace(0, 1, 4097)
    Z = Curve(T)[:, 2] - Height
    Roots = [
        brentq(lambda U: Curve(U)[2] - Height, A, B, xtol=1e-15)
        for A, B, X, Y in zip(T[:-1], T[1:], Z[:-1], Z[1:])
        if X * Y < 0
    ]
    Roots = [U for U in Roots if Curve(U)[1] > 0]
    if not Roots:
        raise ValueError("No contour crossing at the requested height")
    return Curve(min(Roots, key=lambda U: abs(Curve(U)[0] - NearX)))


def Run(Source, Triangles, Native, Scratch, Destination):
    Text = Source.read_text()
    Prior = json.loads(Source.with_suffix(".guides.json").read_text())
    assert hashlib.sha256(Source.read_bytes()).hexdigest() == Prior["documentSha256"]
    Curves = ReadCurves(Text)
    Records = copy.deepcopy(Prior["curves"])
    ByCode = {C["code"]: C for C in Records}
    CurvesByCode = {C["code"]: Curves[C["names"][0]] for C in Records}
    Surfaces = Read(Source)
    Lines = Text.splitlines()
    PatchNames = [re.search(r"--name=(\S+)", L)[1] for L in Lines if L.startswith("patch ")]
    Patches = dict(zip(PatchNames, Surfaces))
    LastSew = max(I for I, L in enumerate(Lines) if L.startswith("sew "))
    Names = [T for T in Lines[LastSew].split()[1:] if not T.startswith("--")]
    Cuts, Extensions, Connections = {}, {}, []

    def Exact(Code, Specs, Target):
        Parts = []
        for Name, Axis, Value in Specs:
            Cuts[Name] = (Axis, Value)
            Parts.append(Isoline(Patches[Name], Axis, Value))
        Extension = Parts[0] if len(Parts) == 1 else Join(Parts, 1e-7)[:2]
        Old = CurvesByCode[Code]
        Candidates = [Spline(Extension)(T) for T in [0, 1]]
        Inner = Spline(Old)(0 if Code == "B42" else 1)
        Outer = max(Candidates, key=lambda P: np.linalg.norm(P - Inner))
        Parameter, Gap = Nearest(Spline(CurvesByCode[Target]), Outer)
        if Gap > 1e-7:
            raise ValueError(f"{Code} misses target {Target}: {Gap}")
        Across = ByCode[Code]["names"][0].endswith("_Across")
        Combined = Join(
            (
                [Reflect(Extension), Old, Extension]
                if Code == "B41"
                else [Extension, Old, Reflect(Extension)] if Across else [Old, Extension]
            ),
            1e-7,
        )
        Extensions[Code] = Combined[:2]
        Connections.append(
            {
                "code": Code,
                "target": Target,
                "targetParameter": Parameter,
                "targetGapMm": Gap * 1000,
                "construction": "exact native surface isoparametric continuation",
                "supports": [{"surface": N, "axis": A, "parameter": V} for N, A, V in Specs],
                "join": Combined[2],
            }
        )

    End = Spline(CurvesByCode["B41"])(1)
    Value = brentq(lambda T: Points(Patches["Main_0156"], np.array([[1], [T]]))[0, 0] - End[0], 0, 1, xtol=1e-15)
    Exact("B41", [("Main_0156", 1, Value)], "B03")
    Mid = Spline(Isoline(Patches["Main_0014"], 0, 5 / 6))(0)
    Value = brentq(lambda T: Points(Patches["Main_0164"], np.array([[T], [1]]))[0, 0] - Mid[0], 0, 1, xtol=1e-15)
    Exact("B42", [("Main_0014", 0, 5 / 6), ("Main_0164", 0, Value)], "B03")
    Exact("C01", [("Main_0302", 0, 2 / 3)], "B03")

    Projection = SurfaceProjection(np.fromfile(Triangles, dtype=np.float64).reshape(-1, 3, 3))
    Proposals = []

    def Projected(Code, Begin, End, Axes, Target, Tangent=None):
        Samples = np.linspace(Begin, End, 257) if Tangent is None else Hermite(Begin, End, Tangent, End - Begin)
        if Code == "B45":
            Rail = Spline(CurvesByCode["B36"])
            Knee = Rail(brentq(lambda T: Rail(T)[0] - 0.136, 0, 1))
            Upper = Projection.Ray(np.linspace(Begin, Knee, 257)[:, [0, 1]], (0, 1))
            Lower = Projection.Ray(np.linspace(Knee, End, 257)[:, [0, 2]], (0, 2))
            Upper[-1] = Knee
            Lower[0] = Knee
            Seeds = np.vstack([Upper, Lower[1:]])
        else:
            Seeds = Projection.Ray(Samples[:, Axes], Axes)
        Seeds[0], Seeds[-1] = Begin, End
        Proposals.append({"code": Code, "points": Seeds, "begin": Begin, "end": End, "target": Target})

    Old = Spline(CurvesByCode["B45"])
    Projected("B45", Spline(CurvesByCode["B29"])(0), Old(0), (0, 2), "B29")
    Old = Spline(CurvesByCode["R02"])
    Projected("R02Rear", AtHeight(Spline(CurvesByCode["C02"]), 0.64, -0.6), Old(0), (0, 2), "C02")
    Projected("R02Front", Old(1), AtHeight(Spline(CurvesByCode["B22"]), 0.64, 1.28), (0, 2), "B22")
    Old = Spline(CurvesByCode["B17"])
    End = AtHeight(Spline(CurvesByCode["B22"]), 0.6064, 2.3)
    Projected("B17", Old(1), End, (0, 2), "B22", Old(1, nu=1))
    Old = Spline(CurvesByCode["B52"])
    Target = Spline(CurvesByCode["B17"])
    End = AtHeight(Target, 0.469, 2.49)
    # The nose turns back underneath: constant lateral interpolation avoids tracing a tessellation zig-zag.
    Projected("B52", Old(1), End, (1, 2), "B17")
    Scratch.mkdir(parents=True, exist_ok=True)
    Query, Output = Scratch / "ExtensionSeeds.xyz", Scratch / "ExtensionProjected.xyz"
    np.savetxt(Query, np.concatenate([P["points"] for P in Proposals]), fmt="%.17g")
    with (Scratch / "Projection.log").open("w") as Log:
        subprocess.run(
            [str(Native), "--project", str(Source), str(Query), str(Output)],
            stdout=Log,
            stderr=subprocess.STDOUT,
            check=True,
        )
    ProjectedPoints = np.loadtxt(Output)
    Offset = 0
    Fitted = {}
    for Proposal in Proposals:
        Count = len(Proposal["points"])
        Samples = ProjectedPoints[Offset : Offset + Count]
        Offset += Count
        if Samples[:, 6].max() > 0.004 or np.linalg.norm(np.diff(Samples[:, :3], axis=0), axis=1).max() > 0.035:
            raise ValueError(f"Projection crossed a hole or departed from the local skin: {Proposal['code']}")
        XYZ = Samples[:, :3].copy()
        XYZ[0], XYZ[-1] = Proposal["begin"], Proposal["end"]
        Poles, Metrics = Fit(XYZ, False, 0.00035)
        Knots = np.r_[np.zeros(4), np.arange(1, len(Poles) - 3) / (len(Poles) - 3), np.ones(4)]
        Fitted[Proposal["code"]] = (Poles, Knots)
        Connections.append(
            {
                "code": Proposal["code"],
                "target": Proposal["target"],
                "construction": "native surface-projected continuation",
                "begin": XYZ[0].tolist(),
                "end": XYZ[-1].tolist(),
                "fit": Metrics,
                "projectionMaximumMm": float(Samples[:, 6].max() * 1000),
            }
        )
    Extensions["B45"] = Join([Fitted["B45"], CurvesByCode["B45"]], 1e-7)[:2]
    Extensions["R02"] = Join([Fitted["R02Rear"], CurvesByCode["R02"], Fitted["R02Front"]], 1e-7)[:2]
    Extensions["B17"] = Join([Reflect(Fitted["B17"]), CurvesByCode["B17"], Fitted["B17"]], 1e-7)[:2]
    Extensions["B52"] = Join([CurvesByCode["B52"], Fitted["B52"]], 1e-7)[:2]

    # Preserve the source supports until the replacement body has been sewn; no source curve is destroyed.
    Lines[LastSew] += " --keep"
    Lines += ["# First guide-aligned topology: exact roof face restrictions, no skin refit.", "delete Liger_Main_Body"]
    NewNames, Restrictions, Changed = [], [], []
    for OldIndex, Name in enumerate(Names):
        Positive = Name
        Mirrored = False
        if Name.startswith("Main_") and Name.rsplit("_", 1)[-1].isdigit() and int(Name.rsplit("_", 1)[-1]) % 2:
            Positive = f'Main_{int(Name.rsplit("_", 1)[-1])-1:04d}'
            Mirrored = True
        if Positive not in Cuts:
            Restrictions.append([OldIndex, len(NewNames), 0, 1, 0, 1])
            NewNames.append(Name)
            continue
        Axis, Value = Cuts[Positive]
        if Mirrored and Axis == 0:
            Value = 1 - Value
        Knots = Patches[Name].KnotsU if Axis == 0 else Patches[Name].KnotsV
        NearestKnot = Knots[np.argmin(abs(Knots - Value))]
        if abs(NearestKnot - Value) < 1e-12:
            Value = float(NearestKnot)
        Lines.append("delete " + Name)
        Changed.append(Name)
        for Index, (Low, High) in enumerate([(0, Value), (Value, 1)]):
            Surface = Crop(Patches[Name], Axis, Low, High)
            NewName = f"Layout_{Name}_{Index}"
            Lines.append(Declaration(Surface, NewName))
            Rect = [0, 1, 0, 1]
            Rect[Axis * 2 : Axis * 2 + 2] = [Low, High]
            Restrictions.append([OldIndex, len(NewNames), *Rect])
            NewNames.append(NewName)
    Lines.append("sew " + " ".join(NewNames) + " --open --knot-edges --split-junctions --name=Liger_Main_Body")
    for Code, (Poles, Knots) in Extensions.items():
        Record = ByCode[Code]
        OldNames = Record["names"]
        Lines.append("hide " + " ".join(OldNames))
        Record["names"] = []
        for OldName in OldNames:
            Side = OldName.rsplit("_", 1)[-1]
            P = Poles * [1, -1, 1] if Side == "Right" else Poles
            Name = f"Guide_{Code}_Extended_{Side}"
            Record["names"].append(Name)
            Lines += [
                f"cpcurve {Coordinates(P)} --degree=3 --knots="
                + ",".join(f"{K:.17g}" for K in Knots)
                + f" --name={Name}",
                "feature design " + Name,
                "tint " + Name + (" 1 .12 .16" if Code.startswith("R") else " .04 .48 1"),
            ]
        Record["previousNames"] = OldNames
        Record["description"] += "Extended"
        Record["lengthMetres"] = float(
            np.linalg.norm(np.diff(BSpline(Knots, Poles, 3)(np.linspace(0, 1, 8193)), axis=0), axis=1).sum()
        )
    Lines += ["show cages off", "show iso off", "show edges off", "show features on", "view top", "view fit"]
    Destination.write_text("\n".join(Lines) + "\n")
    Destination.with_suffix(".layout.tsv").write_text(
        "\n".join(" ".join(f"{V:.17g}" for V in Row) for Row in Restrictions) + "\n"
    )
    Report = {
        "sourceSha256": Prior["documentSha256"],
        "documentSha256": hashlib.sha256(Destination.read_bytes()).hexdigest(),
        "curves": Records,
        "connections": Connections,
        "bodyChanged": True,
        "geometryRefitted": False,
        "changedSourceSurfaces": Changed,
        "sourceFaceCount": len(Names),
        "newFaceCount": len(NewNames),
        "faceRestrictions": Restrictions,
        "scope": "Roof B41/B42/C01 continuations become actual sewn face edges. Side/nose extensions are guide-only pending subsequent topology work. Inner/outer arch borders and orange selections retained.",
    }
    Destination.with_suffix(".guides.json").write_text(json.dumps(Report, indent=2) + "\n")
    print("EXTENDED", list(Extensions), "TOPOLOGY", len(Names), "->", len(NewNames), "CHANGED", Changed)


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    for Name in ["Source", "Triangles", "Native", "Scratch", "Destination"]:
        Parser.add_argument(Name, type=Path)
    Arguments = Parser.parse_args()
    Run(Arguments.Source, Arguments.Triangles, Arguments.Native, Arguments.Scratch, Arguments.Destination)
