#!/usr/bin/env python3
"""Publish four native guide views, detail views and a numbered curve key. Labels never replace the CAD render."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
from PIL import Image, ImageDraw, ImageFont

Colours = {"B": "#37b9ff", "G": "#57eb7a", "Y": "#ffe35c", "M": "#f577e7", "R": "#ff6b74", "O": "#ffc06b"}


def Run(Source, Destination, FontPath, Consolidated=False, Layout=False):
    Consolidated = Consolidated or Layout
    Prefix = "Liger_Layout" if Layout else "Liger_Consolidated" if Consolidated else "Liger_Guide"
    Palette = dict(Colours)
    if Consolidated:
        Palette.update(C=Colours["B"], G=Colours["B"])
    MetadataName = f"{Prefix}.guides.json" if Consolidated else "Liger_Guide_Candidates.guides.json"
    Metadata = json.loads((Destination / MetadataName).read_text())
    Rows = list(csv.DictReader((Source / "GuidePixels.csv").open()))
    Curves = {Curve["code"]: Curve for Curve in Metadata["curves"]}
    Title = ImageFont.truetype(str(FontPath), 52)
    Label = ImageFont.truetype(str(FontPath), 29)
    Small = ImageFont.truetype(str(FontPath), 26)
    Tiny = ImageFont.truetype(str(FontPath), 23)
    Requested = {
        "Front_Quarter": ["B01", "B02", "B03", "G04", "B22", "G23", "B27", "Y01", "Y05", "R01", "R05"],
        "Rear_Quarter": ["B01", "B07", "G10", "B20", "G50", "B27", "G31", "Y02", "M03", "R03"],
        "Side": ["B20", "G50", "B22", "G23", "G24", "B19", "G43", "B46", "Y03", "Y04", "R01", "R02", "R05"],
        "Top": ["B01", "B02", "B03", "G04", "B27", "G31", "G56", "B53", "Y01", "Y02", "M01", "M02", "R03", "R04"],
        "Roof_Detail": ["B27", "G31", "G56", "B53", "Y01", "Y02", "M01", "M02", "R04"],
        "Rear_Arch_Detail": ["B20", "G50", "G71", "G75", "G90", "G91", "G92", "M03", "M04"],
        "Front_Arch_Detail": ["B22", "G23", "G24", "B35", "B19", "B16"],
    }
    if Consolidated:
        Requested = {
            "Front_Quarter": ["B01", "B02", "B03", "G04", "B22", "G23", "C01", "R01"],
            "Rear_Quarter": ["B01", "B07", "B08", "G12", "B20", "C02", "C01"],
            "Side": ["B20", "C02", "B22", "G23", "B19", "B46", "R01", "R02"],
            "Top": ["B01", "B02", "B03", "G04", "C01", "B29", "B33", "B41", "B42"],
            "Roof_Detail": ["C01", "B29", "B33", "B41", "B42"],
            "Rear_Arch_Detail": ["B20", "C02", "B19", "R01", "R02"],
            "Front_Arch_Detail": ["B22", "G23", "B35", "B19", "B16", "R01", "R02"],
        }
    if Layout:
        Requested["Front_Quarter"] += ["B17", "B52", "B41", "B42"]
        Requested["Side"] += ["B45", "B17", "B52"]
    Images = {}
    Placements = []
    for View, Codes in Requested.items():
        Path = Source / f"{Prefix}_{View}.png"
        shutil.copyfile(Path, Destination / Path.name)
        Canvas = Image.open(Path).convert("RGB")
        Draw = ImageDraw.Draw(Canvas)
        Occupied = []
        for Code in Codes:
            Candidates = [
                Row
                for Row in Rows
                if Row["view"] == View
                and Row["name"].split("_")[1] == Code
                and float(Row["z"]) - float(Row["depth"]) < 0.001
                and 60 < float(Row["x"]) < 1940
                and 50 < float(Row["y"]) < 1200
            ]
            if not Candidates:
                continue
            Candidates.sort(key=lambda Row: abs(float(Row["sample"]) - 32))
            Best = None
            for Row in Candidates:
                X, Y = float(Row["x"]), float(Row["y"])
                for DX, DY in [
                    (45, -65),
                    (45, 40),
                    (-125, -65),
                    (-125, 40),
                    (80, -115),
                    (-160, 90),
                    (30, 110),
                    (-60, -140),
                ]:
                    A, B = X + DX, Y + DY
                    Box = (A, B, A + 80, B + 42)
                    if min(A, B) < 10 or A + 80 > 1990 or B + 42 > 1240:
                        continue
                    Overlap = sum(
                        max(0, min(Box[2], Q[2]) - max(A, Q[0])) * max(0, min(Box[3], Q[3]) - max(B, Q[1]))
                        for Q in Occupied
                    )
                    Score = Overlap * 100 + (DX * DX + DY * DY) * 0.001 + abs(float(Row["sample"]) - 32) * 1.4
                    if Best is None or Score < Best[0]:
                        Best = (Score, Row, Box)
            if Best is None:
                continue
            _, Row, Box = Best
            X, Y = float(Row["x"]), float(Row["y"])
            A, B, C, D = Box
            Colour = Palette[Code[0]]
            Draw.line([(X, Y), ((A + C) / 2, (B + D) / 2)], fill="#e4e9f0", width=2)
            Draw.ellipse((X - 4, Y - 4, X + 4, Y + 4), fill=Colour)
            Draw.rounded_rectangle(Box, radius=7, fill="#101922", outline=Colour, width=2)
            Draw.text((A + 9, B + 3), Code, font=Label, fill=Colour)
            Occupied.append((A - 8, B - 8, C + 8, D + 8))
            Placements.append(
                {
                    "view": View,
                    "code": Code,
                    "nativeCurve": Row["name"],
                    "nativeSample": int(Row["sample"]),
                    "leaderPixel": [X, Y],
                    "labelRectangle": list(Box),
                }
            )
        Canvas.save(Destination / f"{Prefix}_{View}_Labelled.png")
        Images[View] = Canvas

    def Heading(Canvas, Text):
        Draw = ImageDraw.Draw(Canvas)
        Draw.text((38, 24), Text, font=Title, fill="#ecf1f6")
        Legends = [
            ("B", "Blue: long body chains"),
            ("G", "Green: nearby parallel candidates"),
            ("Y", "Yellow: on-surface extensions"),
            ("M", "Magenta: possible joins"),
            ("R", "Red: design-guide options"),
            ("O", "Orange: unresolved repair selections"),
        ]
        if Consolidated:
            Legends = [
                ("B", "Blue: retained / joined guides"),
                ("R", "Red: retained R01 / R02"),
                ("O", "Orange: remaining repair areas"),
            ]
        for Index, (Code, Text) in enumerate(Legends):
            X = 40 + (Index % 3) * 1300
            Y = 100 + (Index // 3) * 49
            Draw.line((X, Y + 16, X + 64, Y + 16), fill=Palette[Code], width=6)
            Draw.text((X + 82, Y), Text, font=Label, fill="#e4e9f0")
        return Draw

    Overview = Image.new("RGB", (4000, 2875), "#101922")
    Draw = Heading(
        Overview,
        (
            "LIGER / EXTENDED GUIDES + FIRST TOPOLOGY CUTS"
            if Layout
            else (
                "LIGER / CONSOLIDATED GUIDES / FOUR-VIEW REVIEW"
                if Consolidated
                else "LIGER / NAMED CURVE CANDIDATES / FOUR-VIEW REVIEW"
            )
        ),
    )
    for Index, View in enumerate(["Front_Quarter", "Rear_Quarter", "Side", "Top"]):
        X = (Index % 2) * 2000
        Y = 210 + (Index // 2) * 1310
        Draw.text((X + 36, Y + 5), View.replace("_", " ").upper(), font=Label, fill="#dce5f0")
        Overview.paste(Images[View], (X, Y + 52))
    Draw.text(
        (38, 2838),
        (
            "Roof: new sewn face edges. Side/nose: extended guides, topology pending. Source skin preserved; both arch borders retained."
            if Layout
            else (
                "53 native guides. Yellow and R03-R05 excluded; close parallel traces consolidated. Body patches unchanged."
                if Consolidated
                else "Guide-only phase: repaired skin unchanged. A close parallel is NOT proof it can be removed. Tell me the IDs to keep, join or discard."
            )
        ),
        font=Small,
        fill="#c7d3e1",
    )
    Overview.save(Destination / f"{Prefix}_Four_Views.png")

    Details = Image.new("RGB", (4000, 2875), "#101922")
    Draw = Heading(
        Details,
        (
            "LIGER / EXTENDED ROOF AND SIDE CONNECTIONS"
            if Layout
            else (
                "LIGER / JOINED ROOF RAIL AND DISTINCT ARCH BORDERS"
                if Consolidated
                else "LIGER / SURFACE EXTENSIONS AND PAIRED-CONTOUR OPTIONS"
            )
        ),
    )
    for Index, View in enumerate(["Roof_Detail", "Top", "Rear_Arch_Detail", "Front_Arch_Detail"]):
        X = (Index % 2) * 2000
        Y = 210 + (Index // 2) * 1310
        Draw.text((X + 36, Y + 5), View.replace("_", " ").upper(), font=Label, fill="#dce5f0")
        Details.paste(Images[View], (X, Y + 52))
    Draw.text(
        (38, 2838),
        (
            "B41 / B42 / C01 now reach B03 on native roof seams. B45, R02, B17 and B52 extended as surface-fitted guides."
            if Layout
            else (
                "C01 = B27 + M01 + B53. C02 = G92 + M03 + G50 + M04 + G91. Both arch borders retained. M02 excluded with Y01."
                if Consolidated
                else "Wheel-arch inner/outer borders remain separate. M01-M04 are proposed connections across existing skin, not repaired patches."
            )
        ),
        font=Small,
        fill="#c7d3e1",
    )
    Details.save(Destination / f"{Prefix}_Details.png")

    KeyHeight = 1480 if Consolidated else 3280
    Key = Image.new("RGB", (2800, KeyHeight), "#101922")
    Draw = ImageDraw.Draw(Key)
    Draw.text(
        (38, 24),
        (
            "LIGER / EXTENDED CURVE KEY"
            if Layout
            else "LIGER / CONSOLIDATED CURVE KEY" if Consolidated else "LIGER / CURVE SELECTION KEY"
        ),
        font=Title,
        fill="#ecf1f6",
    )
    Draw.text(
        (38, 100),
        "IDs match the native curve names. Left/Right share an ID; Across is a full-width or centre curve.",
        font=Small,
        fill="#c7d3e1",
    )
    Draw.text(
        (38, 143),
        (
            "C01 and C02 are joined curves. Other IDs retain their previous meaning; all shown below are visible in this review."
            if Consolidated
            else "Dimmed rows: additional short candidates; reveal with Liger_Guide_All_Candidates.arc after loading the main document."
        ),
        font=Tiny,
        fill="#c7d3e1",
    )
    Records = Metadata["curves"]
    Split = (len(Records) + 1) // 2
    with (Destination / f"{Prefix}_Key.csv").open("w", newline="") as File:
        Writer = csv.writer(File, lineterminator="\n")
        Writer.writerow(
            [
                "ID",
                "Description",
                "Role",
                "LengthPerCurveMetres",
                "ClosestParallelID",
                "MedianSeparationMm",
                "VisibleInitially",
                "NativeNames",
            ]
        )
        for Index, Curve in enumerate(Records):
            Column, Row = divmod(Index, Split)
            X = 38 + Column * 1400
            Y = 220 + Row * 55
            Code = Curve["code"]
            Visible = Curve["defaultVisible"]
            Relations = Curve.get("parallelTo", [])
            Nearest = min(Relations, key=lambda R: R["medianSeparationMm"]) if Relations else None
            Peer = Records[Nearest["family"] - 1]["code"] if Nearest else ""
            Description = re.sub(r"(?<=[a-z])(?=[A-Z])", " ", Curve["description"]).replace("_", " ")
            Tail = f"{Curve['lengthMetres']:.2f} m"
            if Nearest:
                Tail += f" / {Peer}: {Nearest['medianSeparationMm']:.1f} mm"
            if Curve.get("connects"):
                Tail += " / " + " + ".join(Curve["connects"])
            if not Visible:
                Tail += " / hidden"
            Draw.text((X, Y), Code, font=Label, fill=Palette[Code[0]] if Visible else "#7e8a98")
            Draw.text((X + 93, Y), Description, font=Tiny, fill="#e4e9f0" if Visible else "#7e8a98")
            Draw.text((X + 720, Y + 2), Tail, font=Tiny, fill="#b7c7d8" if Visible else "#7e8a98")
            Writer.writerow(
                [
                    Code,
                    Description,
                    Curve["role"],
                    Curve["lengthMetres"],
                    Peer,
                    Nearest["medianSeparationMm"] if Nearest else "",
                    Visible,
                    ";".join(Curve["names"]),
                ]
            )
    Draw.text(
        (38, KeyHeight - 70),
        (
            "Parallel guides consolidated to existing representatives, not averaged. Rejected/superseded guides remain hidden in the document."
            if Consolidated
            else "Blue/green = existing traced geometry. Yellow/magenta/red = proposals. No candidate is approved for deletion or merging yet."
        ),
        font=Tiny,
        fill="#c7d3e1",
    )
    Key.save(Destination / f"{Prefix}_Key.png")
    if Layout:
        Comparison = Image.new("RGB", (4000, 2875), "#101922")
        Draw = ImageDraw.Draw(Comparison)
        Draw.text((38, 24), "LIGER / ACTUAL ROOF TOPOLOGY / BEFORE AND AFTER", font=Title, fill="#ecf1f6")
        Draw.text(
            (38, 104),
            "Guide overlays OFF. New lines here are real sewn face boundaries, not sketches or hidden-edge styling.",
            font=Label,
            fill="#c7d3e1",
        )
        for Row, Caption in enumerate(["CROSSBAR CONTINUATIONS B41 / B42", "FORWARD ROOF RAIL C01"]):
            for Column, State in enumerate(["Before", "After"]):
                Name = f"Liger_Layout_{State}_{Row}.png"
                shutil.copyfile(Source / Name, Destination / Name)
                X, Y = Column * 2000, 210 + Row * 1310
                Draw.text((X + 38, Y + 5), State.upper() + " / " + Caption, font=Label, fill="#e4e9f0")
                Comparison.paste(Image.open(Source / Name).convert("RGB"), (X, Y + 52))
        Draw.text(
            (38, 2838),
            "8 existing support faces partitioned into 16 mirrored faces. 1074 -> 1082 total faces; no surface refit or invented thickness.",
            font=Small,
            fill="#c7d3e1",
        )
        Comparison.save(Destination / "Liger_Layout_Topology_Before_After.png")
    Report = {
        "renderer": "Native SolidArc GuideVerification; only labels, leaders and layout added after rendering",
        "documentSha256": Metadata["documentSha256"],
        "labelPlacements": Placements,
        "images": {
            Path.name: hashlib.sha256(Path.read_bytes()).hexdigest()
            for Path in sorted(Destination.glob(f"{Prefix}_*.png"))
        },
    }
    (Destination / f"{Prefix}_Views.json").write_text(json.dumps(Report, indent=2) + "\n")
    shutil.copyfile(Source / "GuideNative.csv", Destination / f"{Prefix}_Native.csv")


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("Renders", type=Path)
    Parser.add_argument("Destination", type=Path)
    Parser.add_argument("--font", type=Path, default=Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"))
    Parser.add_argument("--consolidated", action="store_true")
    Parser.add_argument("--layout", action="store_true")
    Arguments = Parser.parse_args()
    Run(Arguments.Renders, Arguments.Destination, Arguments.font, Arguments.consolidated, Arguments.layout)
