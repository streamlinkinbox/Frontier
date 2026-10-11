#!/usr/bin/env python3
"""Measure the original roof strip, append its live offset recipe, and publish native review renders."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from MirrorSequence import Read, Points

Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"
Name = "Liger_Surface_Offset"


def Construct():
    Source = Root / "Liger_Layout.arc"
    Text = Source.read_text()
    Names = [re.search(r"--name=(\S+)", Line)[1] for Line in Text.splitlines() if Line.startswith("patch ")]
    Patches = dict(zip(Names, Read(Source)))
    Measurements = {}
    AllWidths = []
    Refinement = 0.0
    for Strip in ["Main_0164", "Main_0302"]:
        Widths = []
        for U in np.linspace(0, 1, 65):
            Lengths = []
            for Samples in [513, 1025]:
                Positions = Points(Patches[Strip], np.array([np.full(Samples, U), np.linspace(0, 1, Samples)]))
                Lengths.append(float(np.linalg.norm(np.diff(Positions, axis=0), axis=1).sum()))
            Refinement = max(Refinement, abs(Lengths[1] - Lengths[0]))
            Widths.append(Lengths[1])
        Measurements[Strip] = Widths
        AllWidths.extend(Widths)
    Median = float(np.median(AllWidths))
    Width = round(Median, 3)
    Destination = Root / f"{Name}.arc"
    Destination.write_text(
        Text
        + "\n# Live inner roof guide: 25 mm along the oriented support skin, not a planar projection.\n"
        + "require surface-offset\n"
        + f"surface-offset Guide_B03_UpperCanopy_Across Liger_Main_Body -{Width:.3f} "
        + "--name=Guide_O01_RoofSurfaceOffset_Across\n"
    )
    Report = {
        "source": Source.name,
        "sourceSha256": hashlib.sha256(Source.read_bytes()).hexdigest(),
        "document": Destination.name,
        "documentSha256": hashlib.sha256(Destination.read_bytes()).hexdigest(),
        "bodyChanged": False,
        "parentCurveChanged": False,
        "orangeRepairsChanged": False,
        "parent": "Guide_B03_UpperCanopy_Across",
        "support": "Liger_Main_Body",
        "offset": "Guide_O01_RoofSurfaceOffset_Across",
        "signedDistanceMetres": -Width,
        "toleranceMetres": 0.00005,
        "stripMedianMetres": Median,
        "stripLengthRefinementMetres": Refinement,
        "stripLengthsMetres": Measurements,
        "measurement": "Transverse NURBS surface arc lengths, 65 stations per original strip; representative median rounded to 1 mm.",
        "measurementLimit": "Original strip widths are not constant. Transverse isocurve lengths select the design width; they are not perpendicular shortest-distance measurements.",
        "solverLimit": "Numerical normal-geodesic marching and local corner distance estimates. Sampled convergence checks are not a global shortest-path, cut-locus, or continuous intersection certificate.",
    }
    (Root / f"{Name}.json").write_text(json.dumps(Report, indent=2) + "\n")
    print(f"Strip median: {Median * 1000:.9f} mm; chosen uniform width: {Width * 1000:.3f} mm")
    print(f"513/1025 chord-sum refinement: {Refinement:.12g} m")


def Publish(Proofs, Font):
    Title = ImageFont.truetype(str(Font), 54)
    Label = ImageFont.truetype(str(Font), 32)
    Small = ImageFont.truetype(str(Font), 28)
    Views = ["Front_Quarter", "Rear_Quarter", "Side", "Top"]
    Overview = Image.new("RGB", (4000, 2830), "#101922")
    Draw = ImageDraw.Draw(Overview)
    Draw.text((38, 25), "LIGER / LIVE SURFACE-DISTANCE ROOF OFFSET", font=Title, fill="#edf2f8")
    Draw.text(
        (40, 102),
        "BLUE: retained parent / guides     LIME: 25 mm live offset     ORANGE: unresolved repair selections",
        font=Label,
        fill="#cfdae5",
    )
    for I, View in enumerate(Views):
        File = f"{Name}_{View}.png"
        shutil.copyfile(Proofs / File, Root / File)
        ImageOfView = Image.open(Proofs / File).convert("RGB")
        assert ImageOfView.size == (2000, 1250)
        X, Y = (I % 2) * 2000, 175 + (I // 2) * 1310
        Draw.text((X + 38, Y), View.replace("_", " ").upper(), font=Label, fill="#edf2f8")
        Overview.paste(ImageOfView, (X, Y + 45))
    Draw.text(
        (38, 2790),
        "Native CAD renders. Parent and skin remain live-linked. Body surfaces and approved orange selections unchanged.",
        font=Small,
        fill="#c4cfda",
    )
    Overview.save(Root / f"{Name}_Four_Views.png")
    Comparison = Image.new("RGB", (4000, 1480), "#101922")
    Draw = ImageDraw.Draw(Comparison)
    Draw.text((38, 25), "ROOF GUIDE / BEFORE AND AFTER", font=Title, fill="#edf2f8")
    for I, View in enumerate(["Before", "After"]):
        File = f"{Name}_{View}.png"
        shutil.copyfile(Proofs / File, Root / File)
        Draw.text(
            (I * 2000 + 38, 108),
            "BEFORE: retained blue parent" if I == 0 else "AFTER: live lime offset, 25 mm along skin",
            font=Label,
            fill="#edf2f8",
        )
        Comparison.paste(Image.open(Proofs / File).convert("RGB"), (I * 2000, 158))
    Draw.text(
        (38, 1435),
        "Same camera and unchanged skin. This adds a dependent guide; curved topology cuts and orange repairs are not included.",
        font=Small,
        fill="#c4cfda",
    )
    Comparison.save(Root / f"{Name}_Before_After.png")
    for File in ["SurfaceOffsetMeasures.json", "SurfaceOffsetSamples.csv"]:
        shutil.copyfile(Proofs / File, Root / f"{Name}_{File}")


if __name__ == "__main__":
    Parser = argparse.ArgumentParser()
    Parser.add_argument("--proofs", type=Path)
    Parser.add_argument("--font", type=Path, default=Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"))
    Arguments = Parser.parse_args()
    Construct()
    if Arguments.proofs:
        Publish(Arguments.proofs, Arguments.font)
