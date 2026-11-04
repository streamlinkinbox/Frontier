#====================================================================================================================================
#                                                     SHOWCASECOMPOSER.PY
#====================================================================================================================================
# 🧩 Composes Exhibits/Showcase.html: one self-contained page (no external resources) with kernel models, evidence tables and stills

"""
Run:  python Exhibits/GeometryProcessing/Code/Tooling/ShowcaseComposer.py --meshes DIR [--output Exhibits/Showcase.html]

--meshes   directory written by `PatternStressValidation --export DIR` (one JSON tessellation per built row)
--output   the composed page (default Exhibits/Showcase.html)

Models    each listed row is un-welded per face (smooth shading inside a face, creases between faces), quantised to
          16 bits over its bounding box and embedded as base64 typed arrays, with the shared B-rep edge samplings as lines.
Tables    the pattern-stress rows are parsed from Exhibits/GeometryProcessing/Results/PatternStressValidation.md.
Stills    gallery panels, walkthrough captures and viewport frames are re-encoded as WebP data URIs. Requires Pillow.
"""

import argparse
import base64
import html
import io
import json
import re
import struct
import subprocess
from pathlib import Path

from PIL import Image

#------------------------------------------------------------------------------------------------------------------------
#                                                    SETTINGS
#------------------------------------------------------------------------------------------------------------------------

RepositoryRoot  = Path(__file__).resolve().parents[4]
ExhibitRoot     = RepositoryRoot / "Exhibits"
TemplatePath    = Path(__file__).resolve().parent / "ShowcaseTemplate.html"
PayloadMarker   = "/*@ShowcasePayload@*/"
Categories      = ["Plane", "Cylinder", "Cone", "Sphere", "Torus", "RollingBall", "Nurbs", "Other"]
GalleryWidth    = 640                                                                     # [px] - Gallery panel width
CaptureWidth    = 1280                                                                    # [px] - Walkthrough capture width
ViewportWidth   = 640                                                                     # [px] - Viewport gate frame width
GalleryQuality  = 72                                                                      # [-]  - WebP quality
CaptureQuality  = 78                                                                      # [-]

Models = [                                                                                # Code, title, caption
    ("T4",  "Rounded block, filleted loop",   "4 verticals r 0.5, then the top loop r 0.2: lines meet quarter tori at smooth joints"),
    ("T5",  "Rounded block, chamfered loop",  "the same loop as a chamfer: planes meet cones at tangent joints"),
    ("T8",  "Tangent chain from one edge",    "one seed line grows to all 8 loop edges; equals T5 exactly"),
    ("T3",  "Stadium slot, both rims",        "top and bottom rims r 0.1 in one operation"),
    ("T6",  "Boss foot loop",                 "rounded boss verticals, then the concave foot loop r 0.1"),
    ("P1",  "Slot cusps",                     "fillets on arcs that meet at cusps: mitred by the bisector section"),
    ("P2",  "Scallops, filleted",             "asymmetric mitres from the general A ∩ B section"),
    ("P3",  "Scallops, chamfered",            "chamfer contacts that do not meet, closed through the far face"),
    ("P4",  "Spoke–hub concave fillets",      "concave ends extend the neighbouring edge instead of consuming it"),
    ("P5",  "Spoke tops ending on the hub",   "blend caps are sections of the blend by the curved hub"),
    ("P6",  "Ring of cylinders",              "convex outer and concave inner cusps, every top edge r 0.05"),
    ("P9",  "Touching bosses, feet",          "6 foot circles r 0.1 on bosses with coplanar contact"),
    ("B13", "Six blocks merged, all filleted", "needs maximal faces: 6 unit blocks fuse to one, then every edge r 0.2"),
    ("B18", "Cross-bores with rim fillets",   "M5 cross-bores + 12 bore entry rims r 0.05"),
    ("M5",  "3 × 3 Steinmetz cross-bores",    "9 equal-radius crossings, 18 singular points, genus 15"),
    ("P7",  "Radial bore mouths, filleted",   "8 traced mouths r 0.03 on exact procedural rolling-ball pipes"),
    ("P8",  "Cross-bore crossings, filleted",  "elliptical crossing edges r 0.03; degree-8 singular points closed by pinch vertices"),
    ("V1",  "L block, every edge filleted",    "2 setback vertices where convex meets concave: fair rational quads, G1 to 0.17°"),
    ("V2",  "Pyramid, every edge filleted",    "degree-4 apex closed by a setback patch of 8 fair NURBS quads"),
    ("O1",  "Thin plate, cliff fillet",        "r 0.3 on a 0.2 plate: the front face is consumed, the bottom face trims the blend"),
    ("O2",  "Thin plate, cliff chamfer",       "c 0.3 on a 0.2 plate: the chamfer plane runs off the front face onto the bottom"),
    ("NotchBossFillet", "Boss rim, notch overflow", "r 0.3 on a boss rim wider than its wall: the ledge trims the torus and grows under it"),
    ("SmoothFillet",    "Smooth overflow",          "r 0.3 rolls over the tangent edge onto an r 0.85 fillet: the strip is consumed"),
    ("VariableFillet",  "Variable fillet",          "r 0.1 → 0.4 along one edge: the exact cone of the balls, elliptical caps"),
    ("VariableChamfer", "Variable chamfer",         "d 0.1 → 0.4: the contact lines meet at the apex, so the chamfer is one plane"),
    ("TiltedBoreChamfer", "Tilted bore, chamfered mouth", "d 0.1 on a bore tilted 20°: the dihedral varies along the traced rim, so the ball radius follows d·tan(θ/2)"),
    ("TiltedBossChamfer", "Tilted boss, chamfered foot",  "the concave twin: d 0.1 on the traced foot of a tilted boss, one ruled chord face"),
    ("VariableArcFillet",  "Variable fillet on a curved edge",  "r 0.1 → 0.3 around a half-rim: the canal surface of the balls, whose circles lean by dr/ds, ending on plane caps"),
    ("VariableArcChamfer", "Variable chamfer on a curved edge", "d 0.1 → 0.3 around the same half-rim: a ball-law spine with a chord carrier, every ruling in its meridian plane"),
    ("P10", "Radial bore mouths, chamfered",               "8 traced mouths d 0.03 on ruled chord carriers between ball-law contact rails"),
]

CaptureCaptions = {
    "01Workspace"       : "Workspace layout with the specimen: outliner, feature tree, viewport, inspector",
    "02FacesSelected"   : "Face mode (3), Shift adds a second cap face",
    "04BevelPreview"    : "B opens the bevel session; the live preview adds 1 face before committing",
    "05FilletApplied"   : "Fillet 0.3 committed: 12 faces, audit sound, undo depth 6",
    "06BevelRefusal"    : "An honest refusal: the chamfer would consume the corner chamfer's edge, a shape cliff overflow does not cover",
    "07Navigated"       : "Plasticity navigation: middle orbit, right pan, wheel dolly",
    "09FrontParallel"   : "Front view, orthographic (Numpad 1, Numpad 5)",
    "09RightParallel"   : "Right view, orthographic (Numpad 3)",
    "10FaceEdit"        : "Face edit on the selected cap",
    "13Constructed"     : "Construct dialog result: a new primitive in the tree",
    "16WelcomeBack"     : "Welcome back: restore the previous session",
    "17Recovered"       : "Recovered document after an unclean exit",
    "19BevelSignChamfer": "Inside the bevel session C switches to chamfer, D back to fillet",
    "20ShortcutExtrude" : "E opens the extrude session; Enter commits",
    "21TangentChain"    : "Alt+click extends the edge selection along the tangent chain (5 edges)",
    "50CurvesImported"  : "Import curves…: 11 bonnet loops (26 noisy limit points each) fitted within 0.2 mm, one undo entry",
    "51ControlPoints"   : "Key 1, control-point mode: every fitted curve shows its poles and control polygon",
    "52SectionsSelected": "Six sections picked in loft order (badges), curvature combs on the selection",
    "53BonnetLoft"      : "L lofts an open NURBS sheet; the 5 check loops it never saw lie on it (0.076 mm)",
    "54PoleDragLive"    : "Mode 1 pole drag: the curve previews live before the release commits",
    "55PoleMoved"       : "The release refits Section3 and regenerates the loft; undo restores it bit-exactly",
    "56SheetUnderside"  : "The open sheet seen from below: back faces render darker",
    "111MovePreview"    : "G · X with the pointer: the staged copy moves 1 along x before the click commits a Transform step",
    "112Rotate"         : "R · Z · 45 typed: the box turns about its centre, names and picks unchanged",
    "113Scale"          : "S · Z · 2: the height doubles (V 4); a second S is cancelled by Esc",
    "114Duplicate"      : "Shift+D · Y · 3: the copy stands 3 along y, picked and active",
    "115Paste"          : "Ctrl+C, X, Ctrl+V: the copy comes back as it stood when copied",
    "116Hide"           : "H hides the picked body: one drawn, two in the document",
    "117InvertHidden"   : "Shift+H then Ctrl+H swap which body is shown",
    "118Join"           : "J welds two touching line sketches into one closed square",
    "119OffsetPreview"  : "O · 0.25: the green outline previews the offset sketch",
    "120PipePreview"    : "P · 0.2 · R along a drawn line: the staged tube",
    "121Pipe"           : "Pipe1 as a second body: π · 0.04 · 2 exact, audit sound",
    "122ConstrainOpen"  : "K on a drawn rectangle: H / V glyphs, every point free (blue), Constraints card DOF 4",
    "123ConstrainFull"  : "] corner, D 3, D 2: fully constrained, all green, the extrude follows (V 6)",
    "124ConstrainConflict": "D 5 on the top line refused: it conflicts with V1, V2 and D1, named in the card",
    "125ConstrainEdit"  : "Click D1 and type 4: the width and the extrude follow (V 8)",
    "126ConstrainDragging": "D2 deleted, the far corner dragged: the free top edge previews live, DOF 1",
    "127ConstrainAngle" : "A line chain: ], H, D 1.5, N 45°, Q equal: fully constrained at the closed-form corner",
    "128ConstrainSplineG1"       : "Line locked, curve + line, L: the NURBS curve leaves the joint along the line (T1), DOF 5",
    "129ConstrainSplineG2"       : "C replaces T1 by K1 (G2), H1 levels the far end: a smooth blend out of the line, DOF 3",
    "130ConstrainReference"      : "Shift+D on the diagonal of a fully constrained plate: a grey reference (1.803), nothing moves",
    "131ConstrainReferenceFollows": "D2 = 2: the reference reads (2.5); E is refused because H1, V1, D1, D2 already imply it",
    "132ConstrainLinkPick"       : "K on a later sketch: the plate's sketch is drawn dashed and its far corner is pickable",
    "133ConstrainLinked"         : "Linked corner and top line in amber: the plate's D1 = 2 carries the later line's start to (2, 2)",
    "134RevolvePreview"          : "Alt+R on a vase profile (polylines + Bezier): the proposed sketch y-axis dashed amber, the staged full turn",
    "135RevolvePreviewIsometric" : "The staged revolve from the isometric view: spline profile turned exactly about the y-axis",
    "136RevolveEdgeAxis"         : "A plate turned 90 degrees about its own left edge (a click picked Line4 as the axis), new body",
    "137EvolutionProposed"       : "Shift+P: a circle swept along the proposed path, the newest one-curve sketch",
    "138EvolutionPath"           : "A click on the Bezier switches the path: the circle sweeps along it by rotation-minimising frames",
    "139Evolution"               : "Revolve1 4.10109, Revolve2 0.05655, Evolution1 0.31908 m3 (pi r^2 L): three sound bodies",
    "140ReplayTailEdit"          : "Incremental replay: Fillet1 0.25 -> 0.3 replays only Fillet1 and Chamfer1 (Body card: replayed 2/8)",
    "141ReplayMiddleEdit"        : "The hole radius 0.35 replays from its sketch: replayed 4/8, the first four steps restored",
    "142ReplayUndo"              : "Ctrl+Z restores every step from the saved checkpoints: replayed 0/8 in 0.1 ms",
    "143ReplayRedo"              : "Ctrl+Shift+Z: replayed 0/8 again, bit-identical to a full replay",
    "144ReplayFullEdit"          : "Block1 width is a sketch dimension: the whole tree replays (8/8)",
    "145ReplayPreview"           : "B stages the bevel preview from the document's checkpoints: only the new fillet runs (replayed 1/9)",
    "146XRayBoxOpaque"           : "Box selection without x-ray: the 17 visible edges of the bracket",
    "147XRayBoxThrough"          : "Alt+Z x-ray: ghost faces over the grid, the same box takes all 24 edges including the hidden ones",
    "148XRayHover"               : "Hover through the solid: the hidden back-bottom edge lights up and is named",
    "149XRayPicked"              : "A click picks the hidden edge through the top face",
    "150XRayFillet"              : "B fillets the hidden edge; the preview is seen through the ghost faces (12 faces, 10.1691 m3)",
    "151XRayFilleted"            : "Applied: 10.22278 - (1 - pi/4) 0.25^2 4 = 10.16912 m3, sound",
    "152MeasureBody"             : "I with nothing selected: mass properties of the bracket, V 10.2228 m3, A 36.0477 m2, centroid marker",
    "153MeasureFace"             : "Boss top: area pi 0.5^2 = 0.7853982 m2, perimeter pi",
    "154MeasureDistance"         : "Boss top + front face: exact minimum distance 1.060660 at 90 deg, closest points drawn",
    "155MeasureEdges"            : "Two circular edges: minimum distance 1.415097 between the boss rim and the hole rim",
    "156SectionY"                : "Alt+S at y = 1.25: hatched caps, exact cut A 4.116587 = 4 - chamfer - fillet - hole + boss, 2 regions",
    "157SectionCapHover"         : "Hover on the cap: 'section cap', pickable but it selects nothing",
    "158SectionX"                : "X plane at x = 1: one region through the boss, A 3.25 m2",
    "159SectionFlipped"          : "F flips the kept side (x >= 1): same cut, the other half shown",
}

#------------------------------------------------------------------------------------------------------------------------
#                                                    MODELS
#------------------------------------------------------------------------------------------------------------------------

def EncodeArray(Format, Values):
    return base64.b64encode(struct.pack("<%d%s" % (len(Values), Format), *Values)).decode("ascii")


def ComposeModel(TessellationDirectory, Code, Title, Caption):
    Source    = json.loads((TessellationDirectory / (Code + ".json")).read_text())
    Positions = Source["Positions"]
    Triangles = Source["Triangles"]
    Owners    = Source["TriangleFaces"]
    FaceKinds = Source["Faces"]
    Low       = [min(Positions[Axis::3]) for Axis in range(3)]
    High      = [max(Positions[Axis::3]) for Axis in range(3)]
    Span      = [max(High[Axis] - Low[Axis], 1e-12) for Axis in range(3)]

    def Quantise(Vertex):
        return [round((Positions[3 * Vertex + Axis] - Low[Axis]) / Span[Axis] * 65535) for Axis in range(3)]

    Remap, Quantised, Kinds, FaceIndices, Indices = {}, [], [], [], []
    for Triangle in range(len(Triangles) // 3):
        Face = Owners[Triangle]
        for Corner in range(3):
            Vertex = Triangles[3 * Triangle + Corner]
            Key    = (Face, Vertex)
            if Key not in Remap:
                Remap[Key] = len(Kinds)
                Quantised.extend(Quantise(Vertex))
                Kinds.append(Categories.index(FaceKinds[Face]) if FaceKinds[Face] in Categories else len(Categories) - 1)
                FaceIndices.append(Face)
            Indices.append(Remap[Key])
    EdgeQuantised = []
    for Polyline in Source["Edges"]:
        for Start, Finish in zip(Polyline, Polyline[1:]):
            EdgeQuantised.extend(Quantise(Start) + Quantise(Finish))
    return {
        "Code": Code, "Title": Title, "Caption": Caption, "Low": Low, "Span": Span, "FaceKinds": FaceKinds,
        "Positions": EncodeArray("H", Quantised), "Indices": EncodeArray("I", Indices), "Kinds": EncodeArray("B", Kinds),
        "FaceIndices": EncodeArray("H", FaceIndices), "EdgePositions": EncodeArray("H", EdgeQuantised),
    }

#------------------------------------------------------------------------------------------------------------------------
#                                                    TABLES
#------------------------------------------------------------------------------------------------------------------------

def InlineMarkdown(Text):
    Text = html.escape(Text.replace("\\|", "|"), quote=False)
    Text = re.sub(r"`([^`]+)`", r"<code>\1</code>", Text)
    return re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", Text)


def ComposeStress():
    Lines   = (ExhibitRoot / "GeometryProcessing" / "Results" / "PatternStressValidation.md").read_text(encoding="utf-8").splitlines()
    Summary = next(Line for Line in Lines if Line.startswith("**") and "rows" in Line)
    Rows    = []
    for Line in Lines:
        Pieces = re.split(r"(?<!\\)\|", Line)[1:-1]
        if len(Pieces) < 14:
            continue
        Cells = [Cell.strip() for Cell in Pieces[:13]] + ["|".join(Pieces[13:]).strip()]   # Findings may hold a bare |ΔV|
        if Cells[1] in ("Code", "") or set(Cells[1]) <= set("-"):
            continue
        Verdict, Code, Scene, Ops, Topology, Genus, Lumps, Volume, Closed, Error, Oracle, Triangles, Milliseconds, Finding = Cells
        Rows.append({
            "Verdict": Verdict, "Code": Code, "Family": re.match(r"[A-Z]+", Code).group(0), "Scene": InlineMarkdown(Scene), "Ops": Ops,
            "Topology": Topology, "Genus": Genus, "Lumps": Lumps, "Volume": Volume, "Closed": Closed, "Error": Error, "Oracle": Oracle,
            "Triangles": Triangles, "Milliseconds": Milliseconds, "Finding": InlineMarkdown(Finding),
        })
    return { "Summary": InlineMarkdown(Summary), "Rows": Rows }

#------------------------------------------------------------------------------------------------------------------------
#                                                    STILLS
#------------------------------------------------------------------------------------------------------------------------

def EncodeStill(Path, Width, Quality):
    Picture = Image.open(Path).convert("RGB")
    if Picture.width > Width:
        Picture = Picture.resize((Width, round(Picture.height * Width / Picture.width)), Image.LANCZOS)
    Buffer = io.BytesIO()
    Picture.save(Buffer, "WEBP", quality=Quality, method=6)
    return "data:image/webp;base64," + base64.b64encode(Buffer.getvalue()).decode("ascii")


def SpacedName(Stem):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", Stem)


def ComposeStills():
    Galleries = {}
    for Folder in sorted((ExhibitRoot / "GeometryProcessing" / "Images").glob("*Gallery")):
        Label = SpacedName(Folder.name.replace("Gallery", ""))
        Galleries[Label] = [{ "Name": SpacedName(Path.stem), "Image": EncodeStill(Path, GalleryWidth, GalleryQuality) } for Path in sorted(Folder.glob("*.png"))]
    Order     = ["Blend", "Boolean", "Face Operation", "Feature", "Intersection", "Model", "Tessellation"]
    Galleries = { Name: Galleries[Name] for Name in Order if Name in Galleries }
    Captures  = []
    for Path in sorted((ExhibitRoot / "SolidArc" / "Walkthrough").glob("*.png")):
        Captures.append({ "Name": SpacedName(re.sub(r"^\d+", "", Path.stem)), "Caption": CaptureCaptions.get(Path.stem, ""), "Image": EncodeStill(Path, CaptureWidth, CaptureQuality) })
    Viewport  = [{ "Name": SpacedName(Path.stem.replace("Viewport", "")), "Image": EncodeStill(Path, ViewportWidth, GalleryQuality) }
                 for Path in sorted((ExhibitRoot / "SolidArc" / "Viewport").glob("*.png"))]
    return Galleries, Captures, Viewport

#------------------------------------------------------------------------------------------------------------------------
#                                                    COMPOSITION
#------------------------------------------------------------------------------------------------------------------------

def Main():
    Parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    Parser.add_argument("--meshes", required=True, type=Path)
    Parser.add_argument("--output", default=ExhibitRoot / "Showcase.html", type=Path)
    Arguments = Parser.parse_args()

    Commit, Dated = subprocess.run(["git", "log", "-1", "--format=%h %cs"], cwd=RepositoryRoot, capture_output=True, text=True).stdout.split()
    Galleries, Captures, Viewport = ComposeStills()
    Payload = {
        "Composed" : Dated,
        "Commit"   : Commit,
        "Models"   : [ComposeModel(Arguments.meshes, Code, Title, Caption) for Code, Title, Caption in Models],
        "Stress"   : ComposeStress(),
        "Galleries": Galleries,
        "Captures" : Captures,
        "Viewport" : Viewport,
    }
    Text = json.dumps(Payload, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    Page = TemplatePath.read_text(encoding="utf-8")
    assert PayloadMarker in Page
    Arguments.output.write_text(Page.replace(PayloadMarker, Text), encoding="utf-8")
    Sizes = { Name: len(json.dumps(Payload[Name])) for Name in ("Models", "Stress", "Galleries", "Captures", "Viewport") }
    print("%s · %.2f MB · %s" % (Arguments.output, Arguments.output.stat().st_size / 1e6, " · ".join("%s %.2f MB" % (Name, Size / 1e6) for Name, Size in Sizes.items())))


if __name__ == "__main__":
    Main()
