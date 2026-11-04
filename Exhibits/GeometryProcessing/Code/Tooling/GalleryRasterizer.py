#====================================================================================================================================
#                                                     GALLERYRASTERIZER.PY
#====================================================================================================================================
# 🧩 Before | After exhibit panels for the blend, face-operation and feature-tree catalogues, rasterised from the C++ inspection host

"""
Run:  python Exhibits/GeometryProcessing/Code/Tooling/GalleryRasterizer.py [--kind Blend|FaceOperation|Feature|Model|Boolean] [--build-dir DIR] [--output DIR] [--only NAME ...]

Before panel  the source body, shaded neutral, with the selection in amber (edges for blends, face outlines for face
              operations; hidden parts dashed).
After panel   the result coloured by provenance — blends: blend faces teal, corner faces gold, modified faces slate, spines
              dotted cyan, contact rails green; face operations: replaced orange, trimmed teal, generated violet, unchanged
              slate. A refusal is shown as text instead.
Feature trees  Authored (catalogue defaults) | Regenerated (FeatureVariants below): faces coloured by the step that created
              them (persistent-name prefix), sketches dashed violet; refused steps are listed over the kept body.
Camera, lighting and colours follow Viewer/index.html (Yaw -0.7, Pitch -0.45: eye above). Requires Pillow + numpy.
"""

import argparse
import colorsys
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

#------------------------------------------------------------------------------------------------------------------------
#                                                    SETTINGS
#------------------------------------------------------------------------------------------------------------------------

PanelWidth     = 640                                                                     # [px] - One panel (final)
PanelHeight    = 500                                                                     # [px]
CaptionHeight  = 74                                                                      # [px] - Caption strip
Supersample    = 2                                                                       # [-]  - Drawn at 2x, then downsampled
Background     = (16, 19, 25)
OriginHues     = { "Edge": 172, "Vertex": 45, "Modified": 215 }                          # [deg] - Viewer BlendOriginHue
ChangeHues     = { "Replaced": 28, "Trimmed": 172, "Generated": 290, "Unchanged": 215 }   # [deg] - Viewer FaceChangeHue
FontPath       = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FeatureVariants = {                                                                      # Regenerated panel: parameters changed from the defaults
    "BlockFillet":     ["Width=5", "Radius=0.4"],
    "BossFillet":      ["BossX=1", "BossRadius=0.7", "Radius=0.25"],
    "SteppedShaft":    ["PinRadius=0.4", "Chamfer=0.2"],
    "SlotPocket":      ["SlotLength=0.5", "Push=-0.1"],
    "TorusRing":       ["Major=0.8", "Minor=0.7"],
    "HoleLeavesPlate": ["HoleX=5"],
    "SplitFacePush":   ["GrooveX=0.5"],
}

#------------------------------------------------------------------------------------------------------------------------
#                                                    HOST
#------------------------------------------------------------------------------------------------------------------------

def PlainText(Text: str) -> str:
    """DejaVu has no emoji glyphs: the planned-work marker becomes words, any other astral character is dropped."""
    return "".join(Character for Character in Text.replace("\U0001F6A7", "planned") if ord(Character) <= 0xFFFF)


def AskHost(Executable: Path, *Words: str) -> dict:
    Completed = subprocess.run([str(Executable), *Words], capture_output=True, text=True, encoding="utf-8", timeout=600)
    return json.loads(Completed.stdout)

#------------------------------------------------------------------------------------------------------------------------
#                                                    CAMERA
#------------------------------------------------------------------------------------------------------------------------

class ViewerCamera:
    """Orthographic view with the viewer's axes: Toward points at the eye, Right x Up = -Ahead."""

    def __init__(self, Yaw: float = -0.7, Pitch: float = -0.45):
        CosYaw, SinYaw, CosPitch, SinPitch = np.cos(Yaw), np.sin(Yaw), np.cos(Pitch), np.sin(Pitch)
        self.Toward = np.array([-CosPitch * SinYaw, -CosPitch * CosYaw, -SinPitch])
        self.Up     = np.array([-SinPitch * SinYaw, -SinPitch * CosYaw, CosPitch])
        self.Right  = np.array([CosYaw, -SinYaw, 0.0])
        Light       = self.Toward + 0.35 * self.Up + 0.25 * self.Right
        self.Light  = Light / np.linalg.norm(Light)
        self.Scale, self.Centre = 1.0, np.zeros(2)

    def Fit(self, Points: np.ndarray, Width: int, Height: int, Margin: float = 0.1):
        Plane = np.stack([Points @ self.Right, Points @ self.Up], axis=1)
        Low, High = Plane.min(axis=0), Plane.max(axis=0)
        self.Scale  = min(Width * (1 - 2 * Margin) / max(High[0] - Low[0], 1e-9), Height * (1 - 2 * Margin) / max(High[1] - Low[1], 1e-9))
        self.Centre = 0.5 * (Low + High)
        self.Width, self.Height = Width, Height

    def Project(self, Points: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        X = (Points @ self.Right - self.Centre[0]) * self.Scale + 0.5 * self.Width
        Y = 0.5 * self.Height - (Points @ self.Up - self.Centre[1]) * self.Scale
        return np.stack([X, Y], axis=1), Points @ self.Toward

#------------------------------------------------------------------------------------------------------------------------
#                                                    RASTERISATION
#------------------------------------------------------------------------------------------------------------------------

def HslColour(Hue: float, Saturation: float, Lightness: float) -> tuple[int, int, int]:
    Red, Green, Blue = colorsys.hls_to_rgb(Hue / 360.0, Lightness, Saturation)
    return int(Red * 255), int(Green * 255), int(Blue * 255)


def ShadeComplex(Camera: ViewerCamera, Complex: dict, Colour) -> tuple[Image.Image, np.ndarray]:
    """Z-buffer rasterisation (depth interpolated per pixel, nearer = larger Toward·P). Returns colour and depth."""
    Width, Height = Camera.Width, Camera.Height
    Positions = np.asarray(Complex["Positions"], dtype=float).reshape(-1, 3)
    Triangles = np.asarray(Complex["Triangles"], dtype=int).reshape(-1, 3)
    Faces     = np.asarray(Complex["TriangleFaces"], dtype=int)
    Screen, Ahead = Camera.Project(Positions)
    Corners  = Positions[Triangles]
    Normals  = np.cross(Corners[:, 1] - Corners[:, 0], Corners[:, 2] - Corners[:, 0])
    Normals /= np.maximum(np.linalg.norm(Normals, axis=1, keepdims=True), 1e-300)
    Facing   = Normals @ Camera.Toward
    Diffuse  = np.clip(Normals @ Camera.Light, 0.0, 1.0)
    Tokens   = [Face["Token"] for Face in Complex["Faces"]]
    Palette  = np.zeros((len(Triangles) + 1, 3), dtype=np.uint8)
    Palette[-1] = Background
    for Triangle in range(len(Triangles)):
        if Facing[Triangle] < 0: Palette[Triangle] = HslColour(0, 0.85, 0.30 + 0.25 * abs(Facing[Triangle]))       # Visible back face = defect
        else:
            Hue, Saturation = Colour(Tokens[Faces[Triangle]])
            Palette[Triangle] = HslColour(Hue, Saturation, 0.18 + 0.50 * Diffuse[Triangle])
    Depth = np.full((Height, Width), -np.inf)
    Owner = np.full((Height, Width), len(Triangles), dtype=np.int64)
    for Triangle, (A, B, C) in enumerate(Triangles):
        Points = Screen[[A, B, C]]
        Low  = np.maximum(np.floor(Points.min(axis=0)).astype(int), 0)
        High = np.minimum(np.ceil(Points.max(axis=0)).astype(int), [Width - 1, Height - 1])
        if (High < Low).any(): continue
        Columns, Rows = np.meshgrid(np.arange(Low[0], High[0] + 1) + 0.5, np.arange(Low[1], High[1] + 1) + 0.5)
        Determinant = (Points[1, 0] - Points[0, 0]) * (Points[2, 1] - Points[0, 1]) - (Points[2, 0] - Points[0, 0]) * (Points[1, 1] - Points[0, 1])
        if abs(Determinant) < 1e-12: continue
        Second = ((Columns - Points[0, 0]) * (Points[2, 1] - Points[0, 1]) - (Points[2, 0] - Points[0, 0]) * (Rows - Points[0, 1])) / Determinant
        Third  = ((Points[1, 0] - Points[0, 0]) * (Rows - Points[0, 1]) - (Columns - Points[0, 0]) * (Points[1, 1] - Points[0, 1])) / Determinant
        First  = 1.0 - Second - Third
        Inside = (First >= -1e-9) & (Second >= -1e-9) & (Third >= -1e-9)
        Value  = First * Ahead[A] + Second * Ahead[B] + Third * Ahead[C]
        Window = Depth[Low[1]:High[1] + 1, Low[0]:High[0] + 1]
        Nearer = Inside & (Value > Window)
        Window[Nearer] = Value[Nearer]
        Owner[Low[1]:High[1] + 1, Low[0]:High[0] + 1][Nearer] = Triangle
    return Image.fromarray(Palette[Owner], "RGB"), Depth


def DrawPolylines(Camera: ViewerCamera, Picture: Image.Image, Depth: Image.Image, Polylines, Colour, Width: int, Allowance: float,
                  HiddenColour=None, Dotted: bool = False):
    """Segments visible against the depth image are solid; hidden ones dashed in HiddenColour (skipped when None)."""
    Paint   = ImageDraw.Draw(Picture)
    Samples = Depth
    for Polyline in Polylines:
        Points = np.asarray(Polyline, dtype=float)
        if len(Points) < 2: continue
        Dense = np.concatenate([np.linspace(Points[Index], Points[Index + 1], 12, endpoint=False) for Index in range(len(Points) - 1)] + [Points[-1:]])
        Screen, Ahead = Camera.Project(Dense)
        for Index in range(len(Dense) - 1):
            Middle  = 0.5 * (Screen[Index] + Screen[Index + 1])
            Column, Row = int(np.clip(Middle[0], 0, Camera.Width - 1)), int(np.clip(Middle[1], 0, Camera.Height - 1))
            Window  = Samples[max(Row - 1, 0):Row + 2, max(Column - 1, 0):Column + 2]          # Edges sit on face borders: 3×3 minimum
            Visible = 0.5 * (Ahead[Index] + Ahead[Index + 1]) >= Window.min() - Allowance
            if Dotted and Index % 3 == 2: continue
            if Visible: Paint.line([tuple(Screen[Index]), tuple(Screen[Index + 1])], fill=Colour, width=Width)
            elif HiddenColour is not None and Index % 4 < 2: Paint.line([tuple(Screen[Index]), tuple(Screen[Index + 1])], fill=HiddenColour, width=max(1, Width // 2))

#------------------------------------------------------------------------------------------------------------------------
#                                                    PANELS
#------------------------------------------------------------------------------------------------------------------------

def RenderSpecimen(Executable: Path, Name: str, Output: Path, Kind: str = "Blend"):
    After        = AskHost(Executable, "specimen", Name)
    Before       = AskHost(Executable, "specimen", Name, "Source=1")
    BeforeShaded = AskHost(Executable, "tessellate", Name, "Source=1", "SagRatio=0.0015")
    Blend        = After[Kind]                                                                 # Blend or FaceOperation section
    Refused      = "Refusal" in Blend
    AfterShaded  = None if Refused else AskHost(Executable, "tessellate", Name, "SagRatio=0.0015")

    Size, Font = Supersample, ImageFont.truetype(FontPath, 13 * Supersample)
    Camera = ViewerCamera()
    Cloud  = [np.asarray(BeforeShaded["Positions"], dtype=float).reshape(-1, 3)]
    if AfterShaded is not None and Kind == "FaceOperation": Cloud.append(np.asarray(AfterShaded["Positions"], dtype=float).reshape(-1, 3))
    Camera.Fit(np.concatenate(Cloud), PanelWidth * Size, PanelHeight * Size)
    Allowance = 0.004 * max(np.ptp(Cloud[0], axis=0).max(), 1e-9)

    # ---- Before: neutral shading, B-rep edges, selection in amber
    BeforePicture, BeforeDepth = ShadeComplex(Camera, BeforeShaded, lambda Token: (215, 0.10))
    DrawPolylines(Camera, BeforePicture, BeforeDepth, [Edge["Polyline"] for Edge in Before["Edges"]], (10, 12, 16), 2 * Size, Allowance)
    Selection = [Selected["Polyline"] for Selected in Blend["Selection"]] if Kind == "Blend" else [Polyline for Selected in Blend["Selection"] for Polyline in Selected["Outline"]]
    DrawPolylines(Camera, BeforePicture, BeforeDepth, Selection, (255, 179, 71), 4 * Size, Allowance, (200, 140, 60))

    # ---- After: provenance shading, B-rep edges, spines and contacts
    if Refused:
        AfterPicture = Image.new("RGB", (PanelWidth * Size, PanelHeight * Size), Background)
        Paint = ImageDraw.Draw(AfterPicture)
        Verdict = "REFUSED, AS EXPECTED" if Blend["RefusalExpected"] else "REFUSED"
        Paint.text((40 * Size, PanelHeight * Size // 2 - 40 * Size), Verdict, fill=(120, 230, 150) if Blend["RefusalExpected"] else (255, 120, 120), font=ImageFont.truetype(FontPath, 22 * Size))
        Words, Line, Lines = PlainText(Blend["Refusal"]).split(), "", []
        for Word in Words:
            if len(Line) + len(Word) > 58: Lines.append(Line); Line = ""
            Line += Word + " "
        Lines.append(Line)
        for Index, Text in enumerate(Lines): Paint.text((40 * Size, PanelHeight * Size // 2 + (Index * 20 - 4) * Size), Text, fill=(215, 220, 228), font=Font)
    else:
        Origins = { Entry["Face"]: Entry["Origin"] for Entry in Blend["FaceOrigins"] }
        Hues, Plain = (OriginHues, "Modified") if Kind == "Blend" else (ChangeHues, "Unchanged")
        Colour  = lambda Token: (Hues.get(Origins.get(Token, Plain), 215), 0.14 if Origins.get(Token, Plain) == Plain else 0.62)
        AfterPicture, AfterDepth = ShadeComplex(Camera, AfterShaded, Colour)
        DrawPolylines(Camera, AfterPicture, AfterDepth, [Edge["Polyline"] for Edge in After["Edges"]], (10, 12, 16), 2 * Size, Allowance)
        if Kind == "Blend":
            DrawPolylines(Camera, AfterPicture, AfterDepth, [Polyline for Sheet in Blend["Sheets"] for Polyline in Sheet["SpinePolylines"]], (86, 212, 224), 2 * Size, 1e30, Dotted=True)
            DrawPolylines(Camera, AfterPicture, AfterDepth, [Contact["Polyline"] for Sheet in Blend["Sheets"] for Contact in Sheet["Contacts"]], (127, 224, 160), 2 * Size, Allowance)

    # ---- Compose: titles, caption
    Sheet = Image.new("RGB", (2 * PanelWidth * Size, (PanelHeight + CaptionHeight) * Size), Background)
    Sheet.paste(BeforePicture, (0, 0)); Sheet.paste(AfterPicture, (PanelWidth * Size, 0))
    Paint = ImageDraw.Draw(Sheet)
    Title = ImageFont.truetype(FontPath, 16 * Size)
    Paint.text((14 * Size, 10 * Size), "Before: selection", fill=(255, 179, 71), font=Title)
    Heading = "After: blend teal, corner gold, spine cyan, contacts green" if Kind == "Blend" else "After: replaced orange, trimmed teal, generated violet"
    Paint.text(((PanelWidth + 14) * Size, 10 * Size), Heading if not Refused else "After", fill=(160, 220, 230), font=Title)
    Paint.line([(PanelWidth * Size, 0), (PanelWidth * Size, PanelHeight * Size)], fill=(50, 56, 66), width=Size)
    Summary, Statistics = After["Summary"], Blend["Statistics"]
    if Kind == "FaceOperation":
        First = f"{Name}  ·  {After['Family']}  ·  {' -> '.join(Blend['Steps'])}  ·  {After['Description']}"
        if Refused: Second = f"Source body untouched (V = {Summary['SourceVolume']:.9f}); the refusal is atomic and verb-prefixed."
        else:
            Expected = f"V* {Summary['ExpectedVolume']:.10f}  ·  |error| {Summary['VolumeError']:.1e}" if "ExpectedVolume" in Summary else ("open sheet" if Summary["Category"] == "Sheet" else "no closed form")
            Volume   = f"V {Summary['Volume']:.10f}  ·  " if "Volume" in Summary else ""
            Counts   = " / ".join(str(next(Count["Measured"] for Count in Summary["Counts"] if Count["Name"] == Key)) for Key in ("Faces", "Edges", "Vertices"))
            Gates    = "N1-N7 pass" if all(Gate["Passed"] for Gate in AfterShaded["Gates"]) else "N gate FAILED"
            Second   = f"{Volume}{Expected}  ·  F/E/V {Counts}  ·  {Statistics['ReplacedFaces']} carrier(s) replaced  ·  {len(AfterShaded['Triangles']) // 3} triangles, {Gates}"
    elif Refused: Second = f"Source body untouched (V = {Summary['SourceVolume']:.9f}); the refusal is atomic and prefixed 'Blend:'."
    else:
        Carriers = ", ".join(f"{Statistics[Key]} {Label}" for Key, Label in (("Planes", "plane"), ("Cylinders", "cylinder"), ("Cones", "cone"), ("Tori", "torus"), ("CornerSpheres", "sphere")) if Statistics[Key])
        Expected = f"closed form {Summary['ExpectedDelta']:.12f}  ·  |error| {Summary['DeltaError']:.1e}" if "ExpectedDelta" in Summary else "no closed form at this size"
        Gates    = "N1-N7 pass" if all(Gate["Passed"] for Gate in AfterShaded["Gates"]) else "N gate FAILED"
        Faces    = next(Count["Measured"] for Count in Summary["Counts"] if Count["Name"] == "Faces")
        Second   = f"dV {Summary['Delta']:.12f}  ·  {Expected}  ·  {Faces} faces  ·  {Carriers}  ·  {len(AfterShaded['Triangles']) // 3} triangles, {Gates}"
    Paint.text((14 * Size, (PanelHeight + 12) * Size), PlainText(First), fill=(215, 220, 228), font=Font)
    Paint.text((14 * Size, (PanelHeight + 38) * Size), PlainText(Second), fill=(150, 160, 175), font=Font)
    # 📝 Downsample (anti-aliasing), then a 256-colour palette: about a third of the bytes, no visible banding on flat shading.
    Sheet = Sheet.resize((2 * PanelWidth, PanelHeight + CaptionHeight), Image.LANCZOS).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    Output.mkdir(parents=True, exist_ok=True)
    Sheet.save(Output / f"{Name}.png", optimize=True)
    return Refused


def RenderFeature(Executable: Path, Name: str, Output: Path):
    """Authored | regenerated panels of one feature tree; returns True when the regenerated tree refused a step."""
    Variant = FeatureVariants.get(Name, [])
    Answers = [AskHost(Executable, "specimen", Name, *Words) for Words in ([], Variant)]
    Shaded  = [AskHost(Executable, "tessellate", Name, *Words, "SagRatio=0.0015") if Answer["Summary"].get("Volume") is not None else None for Answer, Words in zip(Answers, ([], Variant))]
    Size, Font = Supersample, ImageFont.truetype(FontPath, 13 * Supersample)
    Camera = ViewerCamera()
    Cloud  = np.concatenate([np.asarray(Complex["Positions"], dtype=float).reshape(-1, 3) for Complex in Shaded if Complex is not None])
    Camera.Fit(Cloud, PanelWidth * Size, PanelHeight * Size)
    Allowance = 0.004 * max(np.ptp(Cloud, axis=0).max(), 1e-9)
    Pictures = []
    for Answer, Complex in zip(Answers, Shaded):
        Feature = Answer["Feature"]
        Names   = { Entry["Token"]: Entry["Name"] for Entry in Feature["Names"]["Faces"] }
        Labels  = [Step["Label"] for Step in Feature["Steps"]]
        def Colour(Token, Names=Names, Labels=Labels):
            Index = next((Position for Position, Label in enumerate(Labels) if Names.get(Token, "").startswith(Label + ".")), -1)
            return ((200 + 67 * Index) % 360, 0.55) if Index >= 0 else (215, 0.10)
        Picture, Depth = ShadeComplex(Camera, Complex, Colour)
        DrawPolylines(Camera, Picture, Depth, [Edge["Polyline"] for Edge in Answer["Edges"]], (10, 12, 16), 2 * Size, Allowance)
        Sketches = [Curve["Polyline"] for Sketch in Feature["Sketches"] for Curve in Sketch["Curves"] if not Curve["Construction"]]
        DrawPolylines(Camera, Picture, Depth, Sketches, (197, 139, 255), 2 * Size, 1e30, Dotted=True)
        Paint = ImageDraw.Draw(Picture)
        Refused = [Step for Step in Feature["Steps"] if Step["Status"] == "Refused"]
        for Index, Step in enumerate(Refused):
            Expected = Step["Label"] in Feature["ExpectedRefusals"]
            Paint.text((14 * Size, (40 + 20 * Index) * Size), PlainText(f"{Step['Label']} refused{' (as expected)' if Expected else ''}: {Step['Refusal']}")[:96], fill=(120, 230, 150) if Expected else (255, 120, 120), font=Font)
        Pictures.append(Picture)

    Sheet = Image.new("RGB", (2 * PanelWidth * Size, (PanelHeight + CaptionHeight) * Size), Background)
    Sheet.paste(Pictures[0], (0, 0)); Sheet.paste(Pictures[1], (PanelWidth * Size, 0))
    Paint = ImageDraw.Draw(Sheet)
    Title = ImageFont.truetype(FontPath, 16 * Size)
    Paint.text((14 * Size, 10 * Size), "Authored (catalogue defaults)", fill=(197, 139, 255), font=Title)
    Paint.text(((PanelWidth + 14) * Size, 10 * Size), "Regenerated: " + ", ".join(Word.replace("=", " = ") for Word in Variant), fill=(160, 220, 230), font=Title)
    Paint.line([(PanelWidth * Size, 0), (PanelWidth * Size, PanelHeight * Size)], fill=(50, 56, 66), width=Size)
    def Describe(Answer):
        Summary = Answer["Summary"]
        Methods = ", ".join(Reference["Method"] for Step in Answer["Feature"]["Steps"] for Reference in Step["References"]) or "none"
        Volume  = f"V {Summary['Volume']:.10f} vs V* {Summary['ExpectedVolume']:.10f} (|error| {Summary['VolumeError']:.1e})" if "ExpectedVolume" in Summary else "no body"
        return f"{Volume} · references: {Methods}"
    First = f"{Name}  ·  {' -> '.join(Step['Label'] for Step in Answers[0]['Feature']['Steps'])}  ·  {Answers[0]['Feature']['Oracle']}"
    Paint.text((14 * Size, (PanelHeight + 12) * Size), PlainText(First)[:170], fill=(215, 220, 228), font=Font)
    Paint.text((14 * Size, (PanelHeight + 38) * Size), PlainText("Authored " + Describe(Answers[0]) + "   |   regenerated " + Describe(Answers[1]))[:190], fill=(150, 160, 175), font=Font)
    Sheet = Sheet.resize((2 * PanelWidth, PanelHeight + CaptionHeight), Image.LANCZOS).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    Output.mkdir(parents=True, exist_ok=True)
    Sheet.save(Output / f"{Name}.png", optimize=True)
    return any(Step["Status"] == "Refused" for Step in Answers[1]["Feature"]["Steps"])


def TriangleDistances(Triangles: np.ndarray, Point: np.ndarray) -> np.ndarray:
    """Exact distance from Point to every triangle (N × 3 × 3): plane distance when the projection is inside, else the
    nearest of the three sides."""
    First, Second, Third = Triangles[:, 0], Triangles[:, 1], Triangles[:, 2]
    Normal  = np.cross(Second - First, Third - First)
    Area2   = np.maximum(np.einsum("ij,ij->i", Normal, Normal), 1e-300)                     # [len⁴] |N|² = (2·area)²
    Height  = np.einsum("ij,ij->i", Point - First, Normal) / Area2
    Foot    = Point - Normal * Height[:, None]
    def Turning(Start, Finish): return np.einsum("ij,ij->i", np.cross(Finish - Start, Foot - Start), Normal)
    Inside  = (Turning(First, Second) >= 0) & (Turning(Second, Third) >= 0) & (Turning(Third, First) >= 0)
    def Segment(Start, Finish):
        Span     = Finish - Start
        Fraction = np.clip(np.einsum("ij,ij->i", Point - Start, Span) / np.maximum(np.einsum("ij,ij->i", Span, Span), 1e-300), 0.0, 1.0)
        return np.linalg.norm(Point - (Start + Span * Fraction[:, None]), axis=1)
    Sides = np.minimum(np.minimum(Segment(First, Second), Segment(Second, Third)), Segment(Third, First))
    return np.where(Inside, np.abs(Height) * np.sqrt(Area2), Sides)


def RenderSolid(Executable: Path, Name: str, Output: Path, Kind: str):
    """Model specimen (one panel, faces in distinct hues) or Boolean scene (2 × 2 panels ∪ ∩ − −′: faces tinted by
    argument, A blue / B orange, section edges gold). Returns True when any shown result was refused."""
    Variants = [("Union", ["Operation=0"]), ("Intersection", ["Operation=1"]), ("Difference", ["Operation=2"]), ("Reversed difference", ["Operation=3"])] if Kind == "Boolean" else [("", [])]
    Answers  = [AskHost(Executable, "specimen", Name, *Words) for _, Words in Variants]
    Shaded   = [None if Answer.get("Refusal") else AskHost(Executable, "tessellate", Name, *Words, "SagRatio=0.0015") for Answer, (_, Words) in zip(Answers, Variants)]
    Size, Font, Title = Supersample, ImageFont.truetype(FontPath, 13 * Supersample), ImageFont.truetype(FontPath, 16 * Supersample)
    Camera = ViewerCamera()
    Cloud  = np.concatenate([np.asarray(Complex["Positions"], dtype=float).reshape(-1, 3) for Complex in Shaded if Complex is not None and len(Complex["Positions"])])
    Camera.Fit(Cloud, PanelWidth * Size, PanelHeight * Size)
    Allowance = 0.004 * max(np.ptp(Cloud, axis=0).max(), 1e-9)
    Pictures, Refused = [], False
    for (Label, _), Answer, Complex in zip(Variants, Answers, Shaded):
        if Complex is None or not len(Complex["Positions"]):
            Refused = Refused or bool(Answer.get("Refusal"))
            Picture = Image.new("RGB", (PanelWidth * Size, PanelHeight * Size), Background)
            ImageDraw.Draw(Picture).text((14 * Size, 40 * Size), PlainText("refused: " + Answer.get("Refusal", "") if Answer.get("Refusal") else "empty result")[:90], fill=(255, 120, 120), font=Font)
        else:
            if Kind == "Boolean":
                # ⚠️ specimen and tessellate are separate host runs: tokens differ, so faces are matched by geometry —
                #    each specimen face's on-face anchor goes to the tessellated face whose triangles pass nearest to it.
                Arguments = { History["Face"]: History["Argument"] for History in Answer["FaceHistories"] }
                Triangles = np.asarray(Complex["Positions"], dtype=float).reshape(-1, 3)[np.asarray(Complex["Triangles"], dtype=int).reshape(-1, 3)]
                Owner     = np.asarray(Complex["TriangleFaces"], dtype=int)
                Owners    = {}
                for Face in Answer["Faces"]:
                    Distances = TriangleDistances(Triangles, np.asarray(Face["Normal"]["Origin"], dtype=float))
                    Owners[Complex["Faces"][int(Owner[int(np.argmin(Distances))])]["Token"]] = Arguments.get(Face["Token"], "B")
                Colour = lambda Token, Owners=Owners: (215, 0.45) if Owners.get(Token) == "A" else (28, 0.62)
            else:
                Order  = { Face["Token"]: Index for Index, Face in enumerate(Complex["Faces"]) }
                Colour = lambda Token, Order=Order: ((200 + 137.5 * Order.get(Token, 0)) % 360, 0.50)
            Picture, Depth = ShadeComplex(Camera, Complex, Colour)
            Sections = { History["Edge"] for History in Answer.get("EdgeHistories", []) if History["Origin"] == "Section" }
            DrawPolylines(Camera, Picture, Depth, [Edge["Polyline"] for Edge in Answer["Edges"] if Edge["Token"] not in Sections], (10, 12, 16), 2 * Size, Allowance)
            DrawPolylines(Camera, Picture, Depth, [Edge["Polyline"] for Edge in Answer["Edges"] if Edge["Token"] in Sections], (255, 196, 64), 3 * Size, Allowance, (160, 120, 40))
            Summary, Paint = Answer["Summary"], ImageDraw.Draw(Picture)
            Faces   = Summary["Faces"] if Kind == "Boolean" else next(Count["Measured"] for Count in Summary["Counts"] if Count["Name"] == "Faces")
            Error   = Summary.get("RelativeVolumeError", float("nan"))
            Gates   = all(Gate["Passed"] for Gate in Complex["Gates"])
            Line    = f"F {Faces} · V {Summary['Volume']:.10f} · rel. err {Error:.1e} · {'sound' if Answer['Audit']['Sound'] else 'UNSOUND'} · tessellation {'ok' if Gates else 'FAILED'}"
            Paint.text((14 * Size, (PanelHeight - 26) * Size), Line, fill=(150, 160, 175), font=Font)
        if Label: ImageDraw.Draw(Picture).text((14 * Size, 10 * Size), Label, fill=(160, 220, 230), font=Title)
        Pictures.append(Picture)
    Columns = 2 if len(Pictures) > 1 else 1
    Rows    = (len(Pictures) + Columns - 1) // Columns
    Sheet   = Image.new("RGB", (Columns * PanelWidth * Size, (Rows * PanelHeight + CaptionHeight) * Size), Background)
    for Index, Picture in enumerate(Pictures): Sheet.paste(Picture, ((Index % Columns) * PanelWidth * Size, (Index // Columns) * PanelHeight * Size))
    Paint = ImageDraw.Draw(Sheet)
    for Column in range(1, Columns): Paint.line([(Column * PanelWidth * Size, 0), (Column * PanelWidth * Size, Rows * PanelHeight * Size)], fill=(50, 56, 66), width=Size)
    for Row in range(1, Rows): Paint.line([(0, Row * PanelHeight * Size), (Columns * PanelWidth * Size, Row * PanelHeight * Size)], fill=(50, 56, 66), width=Size)
    Paint.text((14 * Size, (Rows * PanelHeight + 12) * Size), PlainText(f"{Name}  ·  {Answers[0]['Description']}")[:170], fill=(215, 220, 228), font=Font)
    if Kind == "Boolean":
        Fuse = Answers[0]["Fuse"]
        Second = f"General Fuse: {Fuse['SectionBranches']} section branches · {Fuse['InterferenceVertices']} interference vertices · {Fuse['SectionEdges']} section edges (gold) · A blue, B orange"
    else:
        Second = "Parameters: " + ", ".join(f"{Key} = {Value:g}" for Key, Value in Answers[0]["Parameters"].items()) + " · one hue per face, B-rep edges dark"
    Paint.text((14 * Size, (Rows * PanelHeight + 38) * Size), PlainText(Second)[:190], fill=(150, 160, 175), font=Font)
    Sheet = Sheet.resize((Columns * PanelWidth, Rows * PanelHeight + CaptionHeight), Image.LANCZOS).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    Output.mkdir(parents=True, exist_ok=True)
    Sheet.save(Output / f"{Name}.png", optimize=True)
    return Refused


def main():
    Root = Path(__file__).resolve().parents[1]
    Parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    Parser.add_argument("--build-dir", default=str(Root / "build"))
    Parser.add_argument("--kind", choices=("Blend", "FaceOperation", "Feature", "Model", "Boolean"), default="Blend")
    Parser.add_argument("--output", default=None, help="default: Exhibits/GeometryProcessing/Images/<Kind>Gallery")
    Parser.add_argument("--only", nargs="*", default=None)
    Arguments = Parser.parse_args()
    Output = Path(Arguments.output) if Arguments.output else Root.parents[2] / "GeometryProcessing" / "Images" / (Arguments.kind + "Gallery")
    Executable = Path(Arguments.build_dir) / ("GeometryInspection.exe" if sys.platform == "win32" else "GeometryInspection")
    Catalogue  = AskHost(Executable, "catalogue")
    Names      = [Entry["Name"] for Entry in Catalogue["Specimens"] if Entry.get("Kind") == Arguments.kind]
    for Name in Names:
        if Arguments.only and Name not in Arguments.only: continue
        if Arguments.kind == "Feature": Refused = RenderFeature(Executable, Name, Output)
        elif Arguments.kind in ("Model", "Boolean"): Refused = RenderSolid(Executable, Name, Output, Arguments.kind)
        else: Refused = RenderSpecimen(Executable, Name, Output, Arguments.kind)
        print(f"{Name:28s} {'refused' if Refused else 'rendered'}")


if __name__ == "__main__":
    main()
