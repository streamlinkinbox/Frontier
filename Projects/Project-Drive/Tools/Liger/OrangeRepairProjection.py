#!/usr/bin/env python3
"""Present the native orange-repair comparison, four views and front/rear detail views."""

import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw
from PatchRepairProjection import (
    Root,
    Background,
    Foreground,
    Muted,
    Accent,
    Views,
    ResolveTypeface,
    PlacePanel,
)


def AssembleReview(Proofs):
    Report = json.loads((Root / "Liger_Orange_Repair.json").read_text())
    Count = len(Report["groups"]) * 2
    Caption = f'{Count} mirrored subsets  |  {len(Report["removedSurfaces"])} old faces -> {len(Report["newSurfaces"])} quad supports  |  body: {Report["newFaceCount"]} faces'
    Footer = "Orange still visible = unfinished. Arch borders and approved guides retained; wider window offsets remain pending."
    Sheet = Image.new("RGB", (2400, 1740), Background)
    Labels = ImageDraw.Draw(Sheet)
    Labels.text(
        (30, 23),
        "LIGER / ORANGE PATCH REPAIRS",
        font=ResolveTypeface(42),
        fill=Foreground,
    )
    Labels.text((30, 80), Caption, font=ResolveTypeface(27), fill=Muted)
    for Index, (Name, Label) in enumerate(Views):
        PlacePanel(
            Sheet,
            Proofs / f"Liger_Patch_After_{Name}.png",
            (30 + (Index % 2) * 1190, 145 + (Index // 2) * 790),
            (1160, 725),
            Label,
        )
    Labels.text((30, 1700), Footer, font=ResolveTypeface(23), fill=Muted)
    Sheet.save(Root / "Liger_Orange_Repair_Four_Views.png")

    Sheet = Image.new("RGB", (2400, 3350), Background)
    Labels = ImageDraw.Draw(Sheet)
    Labels.text(
        (30, 23),
        "LIGER / ORANGE BEFORE AND AFTER",
        font=ResolveTypeface(42),
        fill=Foreground,
    )
    Labels.text(
        (30, 83), "BEFORE / approved patch repair", font=ResolveTypeface(28), fill=Muted
    )
    Labels.text(
        (1220, 83),
        "AFTER / rebuilt orange subsets",
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
    Labels.text((30, 3310), Footer, font=ResolveTypeface(23), fill=Muted)
    Sheet.save(Root / "Liger_Orange_Repair_Before_After.png")

    Sheet = Image.new("RGB", (4000, 3110), Background)
    Labels = ImageDraw.Draw(Sheet)
    Labels.text(
        (30, 22),
        "LIGER / ORANGE JUNCTION CLOSE-UPS",
        font=ResolveTypeface(47),
        fill=Foreground,
    )
    Labels.text((30, 89), "BEFORE", font=ResolveTypeface(33), fill=Muted)
    Labels.text(
        (2020, 89),
        "AFTER / actual supports joined and faired",
        font=ResolveTypeface(33),
        fill=Accent,
    )
    for Row, (Name, Label) in enumerate(
        [("Front_Orange", "FRONT CORNER"), ("Rear_Orange", "REAR DECK / FENDER")]
    ):
        for Column, Stage in enumerate(["Before", "After"]):
            PlacePanel(
                Sheet,
                Proofs / f"Liger_Patch_{Stage}_{Name}.png",
                (30 + Column * 1990, 158 + Row * 1445),
                (1950, 1365),
                Label,
            )
    Labels.text(
        (30, 3066),
        "Same cameras. Only removed internal orange edges are hidden; unresolved selections remain visible. No Class-A or G1/G2 certification.",
        font=ResolveTypeface(29),
        fill=Muted,
    )
    Sheet.save(Root / "Liger_Orange_Repair_Detail.png")


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("proofs", type=Path)
    AssembleReview(Parser.parse_args().proofs)
