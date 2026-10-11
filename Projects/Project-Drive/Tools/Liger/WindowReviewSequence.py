#!/usr/bin/env python3
"""Publish a patch-edge review without pretending that refused wide surface offsets succeeded."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.interpolate import BSpline

from ConsolidationSequence import ReadCurves
from ContourSequence import Coordinates, Trace
from ExtensionSequence import Nearest, Spline
from GuideSequence import TraceSpline

Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"
Prefix = "Liger_Window_Review"


def Restrict(Curve, First, Last):
    for Parameter in [First, Last]:
        Multiplicity = int(np.sum(Curve.t == Parameter))
        if Multiplicity < 4:
            Curve = Curve.insert_knot(Parameter, 4 - Multiplicity)
    Begin = np.searchsorted(Curve.t, First, side="left")
    End = np.searchsorted(Curve.t, Last, side="right")
    return Curve.c[Begin : End - 4], (Curve.t[Begin:End] - First) / (Last - First)


def Declare(Name, Curve, Colour):
    Poles, Knots = Curve
    return (
        "cpcurve " + Coordinates(Poles) + f" --degree=3 --name={Name} --knots="
        + ",".join(f"{Knot:.17g}" for Knot in Knots)
        + f"\nfeature design {Name}\ntint {Name} " + " ".join(map(str, Colour)) + "\n"
    )


def Construct(EdgesPath):
    Source = Root / "Liger_Surface_Offset.arc"
    Text = Source.read_text()
    Reference = json.loads((Root / "Liger_Surface_Offset.json").read_text())
    assert hashlib.sha256(Source.read_bytes()).hexdigest() == Reference["documentSha256"]
    Curves = ReadCurves(Text)
    Edges = json.loads(EdgesPath.read_text())
    assert len(Edges) == 9137, "Expected approved Liger layout topology"
    Chains = Trace(Edges)
    SourceCurve = Spline(Curves["Guide_G04_WindshieldFoot_Across"])
    RoofCurve = Spline(Curves["Guide_B03_UpperCanopy_Across"])
    Records, Windows = [], []
    Addition = "\n# Window review: unchanged body; exact native edge guides; wide offsets NOT applied.\n"
    for Side, Sign in [("Left", 1), ("Right", -1)]:
        Seed = np.array([1.039229125977, Sign * .559773941040, 1.059514846802])
        Candidates = []
        for Chain in Chains:
            if not .45 < Chain["lengthMetres"] < .6:
                continue
            Positions = Chain["points"][:, :3]
            Candidates.append((float(np.linalg.norm(Positions - Seed, axis=1).min()), Chain))
        Separation, Chain = min(Candidates, key=lambda Entry: Entry[0])
        assert Separation < 1e-8
        Poles, Knots, Measures = TraceSpline(Chain, Edges)
        Curve = BSpline(Knots, Poles, 3)
        if Nearest(RoofCurve, Curve(0))[1] > Nearest(RoofCurve, Curve(1))[1]:
            Poles, Knots = Poles[::-1].copy(), 1 - Knots[::-1]
            Curve = BSpline(Knots, Poles, 3)
        RoofGap = Nearest(RoofCurve, Curve(0))[1]
        FootParameter, FootGap = Nearest(SourceCurve, Curve(1))
        assert max(RoofGap, FootGap) < 1e-7
        Name = f"Guide_W01_WindowPatchEdge_{Side}"
        Addition += Declare(Name, (Poles, Knots), [.3, .9, 1])
        Windows.append(Curve(np.linspace(0, 1, 1025)))
        Records.append({
            "name": Name, "nativeEdges": Chain["edges"], "lengthMetres": Chain["lengthMetres"],
            "roofGapMetres": RoofGap, "greenGapMetres": FootGap, "greenParameter": FootParameter,
            "construction": Measures, "liveLinkedToBody": False,
            "extent": "Exact patch edge from roof border to green foot. No invented continuation beyond green.",
        })
    Reflection = float(np.linalg.norm(Windows[0] * [1, -1, 1] - Windows[1], axis=1).max())
    assert Reflection < 1e-8
    First, Last = sorted(Entry["greenParameter"] for Entry in Records)
    Stations = SourceCurve(np.linspace(First, Last, 129))
    Measurements = []
    for Code, Original, Colour, Width in [
        ("W02", "Guide_B02_LowerCanopy_Across", [.65, .25, 1], .060),
        ("W03", "Guide_B01_OuterShoulder_Across", [1, .12, .16], .145),
    ]:
        Curve = Spline(Curves[Original])
        Distances = [Nearest(Curve, Point)[1] for Point in Stations]
        Cuts = sorted(Nearest(Curve, Point)[0] for Point in [Stations[0], Stations[-1]])
        Addition += f"hide {Original}\n"
        for Suffix, Begin, End, Tint in [
            ("RetainedStart", 0, Cuts[0], [.04, .48, 1]),
            ("ExistingFront", Cuts[0], Cuts[1], Colour),
            ("RetainedEnd", Cuts[1], 1, [.04, .48, 1]),
        ]:
            Restricted = Restrict(Curve, Begin, End)
            Samples = np.linspace(0, 1, 257)
            Error = float(np.linalg.norm(Spline(Restricted)(Samples) - Curve(Begin + (End - Begin) * Samples), axis=1).max())
            assert Error < 1e-10
            Addition += Declare(f"Guide_{Code}_{Suffix}_Across", Restricted, Tint)
        Measurements.append({
            "reference": Original, "colour": "purple" if Code == "W02" else "red",
            "existingChordMinimumMetres": float(min(Distances)),
            "existingChordMedianMetres": float(np.median(Distances)),
            "existingChordMaximumMetres": float(max(Distances)),
            "proposedSurfaceOffsetMetres": Width, "applied": False,
            "sourceDomain": Cuts, "displayedShape": "Exact existing curve, not a uniform offset",
        })
    Addition += "tint Guide_G04_WindshieldFoot_Across .12 .84 .28\n"
    Addition += "show cages off\nshow iso off\nshow edges on\nshow features on\nshow shading plastic\n"
    Destination = Root / f"{Prefix}.arc"
    Destination.write_text(Text + Addition)
    assert Destination.read_text().startswith(Text)
    Report = {
        "source": Source.name, "sourceSha256": hashlib.sha256(Source.read_bytes()).hexdigest(),
        "document": Destination.name, "documentSha256": hashlib.sha256(Destination.read_bytes()).hexdigest(),
        "nativeEdgesSha256": hashlib.sha256(EdgesPath.read_bytes()).hexdigest(),
        "bodyChanged": False, "orangeRepairsChanged": False, "roofOffsetChanged": False,
        "windowGuides": Records, "bilateralDiscrepancyMetres": Reflection,
        "spacing": Measurements, "measurement": "129 nearest-curve 3D chord gaps over the marked frontal span; not surface/geodesic distances.",
        "offsetStatus": "Not applied: production solver refuses wider offsets at the creased window surround.",
        "scope": "Review exact window guides and existing patches before modifying the surround. No topology cuts or surface fairing.",
    }
    (Root / f"{Prefix}.json").write_text(json.dumps(Report, indent=2) + "\n")
    print(json.dumps(Report, indent=2))
    return Destination


def Render(Native, Scratch, Document):
    Scratch.mkdir(parents=True, exist_ok=True)
    Commands = [f'open "{Document}"']
    Views = ["Front_Quarter", "Rear_Quarter", "Side", "Top"]
    Cameras = ["view front; view orbit 55 23; view persp", "view front; view orbit -55 23; view persp", "view front", "view top"]
    for View, Camera in zip(Views, Cameras):
        Commands.extend([Camera, "view fit; view dolly 1.5", f"render {Prefix}_{View} --size=2000x1250"])
    Commands.extend([
        "line (.45,-.93,1.1) (2.08,.93,1.1) --name=WindowInspection",
        "select WindowInspection; view top; view fit selected; delete WindowInspection",
        f"render {Prefix}_Detail --size=2000x1400",
        "show edges off; hide Guide_W01_WindowPatchEdge_Left Guide_W01_WindowPatchEdge_Right",
        f"render {Prefix}_Before --size=2000x1400",
    ])
    Script = Scratch / "WindowReview.arc"
    Script.write_text("\n".join(Commands) + "\n")
    with (Scratch / "WindowRender.log").open("w") as Log:
        subprocess.run([str(Native.resolve()), "--proofs", str(Root), str(Script)], stdout=Log, stderr=subprocess.STDOUT, check=True)
    Font = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    Title = ImageFont.truetype(str(Font), 49)
    Label = ImageFont.truetype(str(Font), 30)
    Small = ImageFont.truetype(str(Font), 27)
    Sheet = Image.new("RGB", (4000, 2870), "#101922")
    Drawing = ImageDraw.Draw(Sheet)
    Drawing.text((38, 22), "LIGER / WINDOW GUIDES + EXISTING PATCHES", font=Title, fill="#eef5ff")
    Drawing.text((38, 92), "CYAN: native window edges     GREEN: reference     PURPLE / RED: existing curves, offsets pending", font=Label, fill="#d6e3ef")
    Drawing.text((38, 137), "LIME: prior 25 mm roof offset     ORANGE: approved repair selections     DARK LINES: actual patch boundaries", font=Label, fill="#d6e3ef")
    for Index, View in enumerate(Views):
        X, Y = (Index % 2) * 2000, 195 + (Index // 2) * 1300
        Drawing.text((X + 38, Y), View.replace("_", " ").upper(), font=Label, fill="#eef5ff")
        Sheet.paste(Image.open(Root / f"{Prefix}_{View}.png").convert("RGB"), (X, Y + 43))
    Drawing.text((38, 2825), "Native CAD render. Body unchanged. Wider constant-width offsets are NOT yet applied.", font=Small, fill="#d6e3ef")
    Sheet.save(Root / f"{Prefix}_Four_Views.png")
    Detail = Image.new("RGB", (2000, 1600), "#101922")
    Drawing = ImageDraw.Draw(Detail)
    Drawing.text((30, 18), "WINDOW / PATCH-EDGE REVIEW", font=Title, fill="#eef5ff")
    Drawing.text((30, 81), "Cyan follows actual patch edges to green; no seam invented beyond green.", font=Small, fill="#a0efff")
    Detail.paste(Image.open(Root / f"{Prefix}_Detail.png").convert("RGB"), (0, 127))
    Drawing.text((30, 1540), "Purple/red retain their existing shape. Approximate target widths: 60 / 145 mm from green.", font=Small, fill="#eef5ff")
    Detail.save(Root / f"{Prefix}_Patch_Detail.png")
    Comparison = Image.new("RGB", (4000, 1570), "#101922")
    Drawing = ImageDraw.Draw(Comparison)
    for Index, (Name, Caption) in enumerate([
        ("Before", "BEFORE / same existing curves, patches hidden"),
        ("Detail", "REVIEW / cyan window edges + actual patches"),
    ]):
        Drawing.text((Index * 2000 + 30, 25), Caption, font=Label, fill="#eef5ff")
        Comparison.paste(Image.open(Root / f"{Prefix}_{Name}.png").convert("RGB"), (Index * 2000, 88))
    Drawing.text((30, 1515), "Same camera and unchanged body. This comparison does not claim completed purple/red offsets.", font=Small, fill="#d6e3ef")
    Comparison.save(Root / f"{Prefix}_Before_After.png")


if __name__ == "__main__":
    Parser = argparse.ArgumentParser()
    Parser.add_argument("edges", type=Path, help="FeatureVerification export of the approved Liger_Layout.arc")
    Parser.add_argument("--native", type=Path)
    Parser.add_argument("--scratch", type=Path, default=Path("_AgentScratch/tmp/WindowReview"))
    Arguments = Parser.parse_args()
    Document = Construct(Arguments.edges)
    if Arguments.native:
        Render(Arguments.native, Arguments.scratch, Document)
