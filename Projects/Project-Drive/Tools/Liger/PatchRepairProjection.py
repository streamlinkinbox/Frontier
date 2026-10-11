#!/usr/bin/env python3
"""Assemble the native, same-camera patch-repair renders into the requested review sheets."""

import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"
Background = (17, 23, 32)
Foreground = (231, 239, 247)
Muted = (155, 176, 198)
Accent = (105, 197, 250)
Views = [
    ("Front_Quarter", "FRONT QUARTER"),
    ("Rear_Quarter", "REAR QUARTER"),
    ("Side", "SIDE"),
    ("Top", "TOP"),
]


def ResolveTypeface(Size):
    for Name in ["DejaVuSans.ttf", "arial.ttf"]:
        try:
            return ImageFont.truetype(Name, Size)
        except OSError:
            pass
    return ImageFont.load_default(size=Size)


def PlacePanel(Sheet, File, Position, Size, Label):
    Horizontal, Vertical = Position
    Width, Height = Size
    ImageDraw.Draw(Sheet).text(
        (Horizontal, Vertical), Label, font=ResolveTypeface(24), fill=Accent
    )
    Picture = (
        Image.open(File)
        .convert("RGB")
        .resize((Width, Height), Image.Resampling.LANCZOS)
    )
    Sheet.paste(Picture, (Horizontal, Vertical + 39))


def AssembleReview(Proofs):
    Sheet = Image.new("RGB", (2400, 1740), Background)
    Labels = ImageDraw.Draw(Sheet)
    Labels.text(
        (30, 23), "LIGER / REBUILT PATCHES", font=ResolveTypeface(42), fill=Foreground
    )
    Labels.text(
        (30, 80),
        "14 mirrored areas  |  216 source faces replaced by 22 quad surfaces  |  body: 888 faces",
        font=ResolveTypeface(27),
        fill=Muted,
    )
    for Index, (Name, Label) in enumerate(Views):
        PlacePanel(
            Sheet,
            Proofs / f"Liger_Patch_After_{Name}.png",
            (30 + (Index % 2) * 1190, 145 + (Index // 2) * 790),
            (1160, 725),
            Label,
        )
    Labels.text(
        (30, 1700),
        "Remaining orange = unfinished. Purple/red constant-width window offsets are not yet applied.",
        font=ResolveTypeface(25),
        fill=Muted,
    )
    Sheet.save(Root / "Liger_Patch_Repair_Four_Views.png")

    Sheet = Image.new("RGB", (2400, 3350), Background)
    Labels = ImageDraw.Draw(Sheet)
    Labels.text(
        (30, 23), "LIGER / BEFORE AND AFTER", font=ResolveTypeface(42), fill=Foreground
    )
    Labels.text(
        (30, 83),
        "BEFORE / approved window review",
        font=ResolveTypeface(28),
        fill=Muted,
    )
    Labels.text(
        (1220, 83),
        "AFTER / actual surface replacement",
        font=ResolveTypeface(28),
        fill=Accent,
    )
    for Index, (Name, Label) in enumerate(Views):
        for Column, Stage in enumerate(["Before", "After"]):
            PlacePanel(
                Sheet,
                Proofs / f"Liger_Patch_{Stage}_{Name}.png",
                (30 + Column * 1190, 145 + Index * 790),
                (1160, 725),
                Label,
            )
    Labels.text(
        (30, 3310),
        "Same cameras. Both arch borders and approved guides retained. Orange work outside the selected areas remains.",
        font=ResolveTypeface(24),
        fill=Muted,
    )
    Sheet.save(Root / "Liger_Patch_Repair_Before_After.png")

    Sheet = Image.new("RGB", (4000, 1620), Background)
    Labels = ImageDraw.Draw(Sheet)
    Labels.text(
        (30, 22),
        "LIGER / ROOF AND WINDOW PATCH DETAIL",
        font=ResolveTypeface(47),
        fill=Foreground,
    )
    Labels.text(
        (30, 89),
        "Fixed perimeter curves; mirrored new quad sectors; live 25 mm roof offset retained.",
        font=ResolveTypeface(33),
        fill=Muted,
    )
    for Column, Stage in enumerate(["Before", "After"]):
        PlacePanel(
            Sheet,
            Proofs / f"Liger_Patch_{Stage}_Detail.png",
            (30 + Column * 1990, 158),
            (1950, 1365),
            Stage.upper(),
        )
    Labels.text(
        (30, 1576),
        "Purple/red borders are retained references, not completed uniform offsets. This is not a Class-A or G1/G2 continuity certification.",
        font=ResolveTypeface(29),
        fill=Muted,
    )
    Sheet.save(Root / "Liger_Patch_Repair_Detail.png")


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument(
        "proofs", type=Path, help="LigerPatchVerification output folder"
    )
    AssembleReview(Parser.parse_args().proofs)
