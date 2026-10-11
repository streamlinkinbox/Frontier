#!/usr/bin/env python3
"""Independently check the review's preservation, native edge copies and unfinished-offset disclosure."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.interpolate import BSpline

from ConsolidationSequence import ReadCurves
from ExtensionSequence import Nearest, Spline

Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"


def Verify(EdgesPath):
    Report = json.loads((Root / "Liger_Window_Review.json").read_text())
    Source = (Root / Report["source"]).read_text()
    Document = (Root / Report["document"]).read_text()
    assert hashlib.sha256(Source.encode()).hexdigest() == Report["sourceSha256"]
    assert hashlib.sha256(Document.encode()).hexdigest() == Report["documentSha256"]
    assert hashlib.sha256(EdgesPath.read_bytes()).hexdigest() == Report["nativeEdgesSha256"]
    assert Document.startswith(Source)
    Addition = Document[len(Source):]
    assert all(Line.split()[0] in {"cpcurve", "feature", "tint", "hide", "show"}
               for Line in Addition.splitlines() if Line.strip() and not Line.startswith("#"))
    Before, After = ReadCurves(Source), ReadCurves(Document)
    for Name, Curve in Before.items():
        assert np.array_equal(Curve[0], After[Name][0]) and np.array_equal(Curve[1], After[Name][1])
    print("PASS original body construction, original curves, roof recipe and orange selections preserved")
    Edges = json.loads(EdgesPath.read_text())
    Maximum = 0.0
    for Record in Report["windowGuides"]:
        Guide = Spline(After[Record["name"]])
        for Index, Direction in Record["nativeEdges"]:
            Edge = Edges[Index]
            Poles = np.array(Edge["poles"])
            assert np.max(abs(Poles[:, 3] - 1)) < 1e-12
            Native = BSpline(Edge["knots"], Poles[:, :3], Edge["degree"])
            Parameters = np.linspace(Edge["knots"][Edge["degree"]], Edge["knots"][-Edge["degree"] - 1], 33)
            for Position in Native(Parameters):
                Maximum = max(Maximum, Nearest(Guide, Position)[1])
        assert Record["liveLinkedToBody"] is False
    assert Maximum < 4e-8
    print(f"PASS copied window guides against native edge samples: maximum nearest discrepancy {Maximum:.12g} m")
    Parameters = np.linspace(0, 1, 4097)
    Left = Spline(After["Guide_W01_WindowPatchEdge_Left"])(Parameters)
    Right = Spline(After["Guide_W01_WindowPatchEdge_Right"])(Parameters)
    Reflection = float(np.linalg.norm(Left * [1, -1, 1] - Right, axis=1).max())
    assert Reflection < 1e-9
    print(f"PASS mirrored window guides: maximum discrepancy {Reflection:.12g} m")
    for Record, Code in zip(Report["spacing"], ["W02", "W03"]):
        assert Record["applied"] is False
        Original = Spline(Before[Record["reference"]])
        First, Last = Record["sourceDomain"]
        for Suffix, Begin, End in [("RetainedStart", 0, First), ("ExistingFront", First, Last), ("RetainedEnd", Last, 1)]:
            Current = Spline(After[f"Guide_{Code}_{Suffix}_Across"])(Parameters)
            assert np.linalg.norm(Current - Original(Begin + (End - Begin) * Parameters), axis=1).max() < 1e-10
    assert "surface-offset" not in Addition
    print("PASS purple/red references retain original shapes; no refused offset is published as successful")
    for View in ["Front_Quarter", "Rear_Quarter", "Side", "Top"]:
        with Image.open(Root / f"Liger_Window_Review_{View}.png") as Picture:
            assert Picture.size == (2000, 1250)
            Picture.verify()
    for View, Size in [("Four_Views", (4000, 2870)), ("Patch_Detail", (2000, 1600)), ("Before_After", (4000, 1570))]:
        with Image.open(Root / f"Liger_Window_Review_{View}.png") as Picture:
            assert Picture.size == Size
            Picture.verify()
    print("PASS four native review views, patch detail and same-camera comparison present")
    print("GREEN: 5 review checks; wider constant-width offsets remain unfinished")


if __name__ == "__main__":
    Parser = argparse.ArgumentParser()
    Parser.add_argument("edges", type=Path)
    Verify(Parser.parse_args().edges)
