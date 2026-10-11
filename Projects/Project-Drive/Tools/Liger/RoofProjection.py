#!/usr/bin/env python3
"""Label native before/after renders without retouching the CAD pixels."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image, ImageDraw, ImageFont


def Run(Source, Destination, FontPath):
    Destination.mkdir(parents=True, exist_ok=True)
    Title = ImageFont.truetype(str(FontPath), 48)
    Label = ImageFont.truetype(str(FontPath), 32)
    Small = ImageFont.truetype(str(FontPath), 27)
    Background, Ink, Muted = "#f5f6f8", "#172332", "#46586a"
    Views = ["Front_Quarter", "Rear_Quarter", "Side", "Top"]
    Images = {}
    for Version in ["Original", "New"]:
        for View in Views + ["Closeup"]:
            Name = f"Liger_Roof_{Version}_{View}.png"
            Path = Source / Name
            ImageFile = Image.open(Path).convert("RGB")
            if ImageFile.size != (1600, 1000):
                raise ValueError(f"Unexpected native frame size: {Path}")
            Images[Version, View] = ImageFile
            shutil.copyfile(Path, Destination / Name)

    def Header(Canvas, Heading, Description):
        Draw = ImageDraw.Draw(Canvas)
        Draw.text((42, 28), Heading, font=Title, fill=Ink)
        Draw.text((44, 98), Description, font=Small, fill=Muted)
        return Draw

    Comparison = Image.new("RGB", (3200, 4500), Background)
    Draw = Header(
        Comparison,
        "LIGER  /  ORIGINAL AND FIRST ROOF REPAIR",
        "Matched native cameras. Only the mirrored roof fan junction is repaired; other orange areas remain pending.",
    )
    for Row, View in enumerate(Views):
        Top = 164 + Row * 1064
        for Column, Version in enumerate(["Original", "New"]):
            Draw.text(
                (Column * 1600 + 42, Top + 9), f'{Version.upper()}  /  {View.replace("_", " ")}', font=Label, fill=Ink
            )
            Comparison.paste(Images[Version, View], (Column * 1600, Top + 60))
    Draw.text(
        (42, 4440),
        "Clean review images: overlays hidden in both; real CAD edges remain visible. Draw your corrections on either version.",
        font=Small,
        fill=Muted,
    )
    Comparison.save(Destination / "Liger_Roof_Before_After.png")

    for Version in ["Original", "New"]:
        Canvas = Image.new("RGB", (3200, 2376), Background)
        Draw = Header(
            Canvas,
            f"LIGER  /  {Version.upper()}  /  FOUR VIEWS",
            "First mirrored roof-junction repair only. Front + rear three-quarter, side and top; same cameras as the reference.",
        )
        for Index, View in enumerate(Views):
            X, Y = (Index % 2) * 1600, 164 + (Index // 2) * 1064
            Draw.text((X + 42, Y + 9), View.replace("_", " "), font=Label, fill=Ink)
            Canvas.paste(Images[Version, View], (X, Y + 60))
        Draw.text(
            (42, 2320),
            "Annotation-ready native render. Feature guides are retained in the documents, but hidden here for a clear comparison.",
            font=Small,
            fill=Muted,
        )
        Canvas.save(Destination / f"Liger_Roof_{Version}_Four_Views.png")

    Closeup = Image.new("RGB", (3200, 1284), Background)
    Draw = Header(
        Closeup,
        "LIGER  /  FIRST ORANGE-AREA REPAIR  /  ROOF CLOSE-UP",
        "Same location and camera. The opposite side has the identical mirrored repair. These are actual surface boundaries.",
    )
    for Column, Version in enumerate(["Original", "New"]):
        Description = (
            "ORIGINAL  /  fan-shaped source patches" if Version == "Original" else "NEW  /  replacement tensor surfaces"
        )
        Draw.text((Column * 1600 + 42, 174), Description, font=Label, fill=Ink)
        Closeup.paste(Images[Version, "Closeup"], (Column * 1600, 224))
    Draw.text(
        (42, 1240),
        "First fairing pass, not final Class-A surfacing. Protected-rim continuity still needs refinement. Please mark what to keep or correct.",
        font=Small,
        fill=Muted,
    )
    Closeup.save(Destination / "Liger_Roof_Closeup_Comparison.png")
    Report = {
        "renderer": "Native SolidArc RoofRepairVerification, 1600x1000; no generated or retouched CAD pixels",
        "cameraMatching": "The original CameraProjection is copied directly to the repaired host for every view.",
        "overlays": "Feature curves hidden in both image sets; native surface boundaries on.",
        "sourceDocuments": {
            Name: hashlib.sha256((Destination / Name).read_bytes()).hexdigest()
            for Name in ["Liger_Feature_Aligned.arc", "Liger_Roof_Repair.arc"]
        },
        "images": {
            Path.name: hashlib.sha256(Path.read_bytes()).hexdigest()
            for Path in sorted(Destination.glob("Liger_Roof_*.png"))
        },
    }
    (Destination / "Liger_Roof_Views.json").write_text(json.dumps(Report, indent=2) + "\n")


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("Renders", type=Path)
    Parser.add_argument("Destination", type=Path)
    Parser.add_argument("--font", type=Path, default=Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"))
    Arguments = Parser.parse_args()
    Run(Arguments.Renders, Arguments.Destination, Arguments.font)
