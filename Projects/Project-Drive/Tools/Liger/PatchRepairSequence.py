#!/usr/bin/env python3
"""Rebuild the selected mirrored patch groups and retain every untouched support and guide."""

import argparse
import hashlib
import json
from pathlib import Path
import re

import numpy as np
from scipy.spatial import cKDTree

from MirrorSequence import Read
from RoofSequence import Mirror, Declaration
from PatchRepairSolver import Construct

Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"


def Fingerprint(File):
    return hashlib.sha256(File.read_bytes()).hexdigest()


def ConstructDocument(EdgesPath, OriginalEdgesPath):
    Selection = json.loads((Root / "Liger_Patch_Repair.selection.json").read_text())
    Source = Root / Selection["source"]
    assert Fingerprint(Source) == Selection["sourceSha256"]
    assert Fingerprint(EdgesPath) == Selection["nativeEdgesSha256"]
    assert Fingerprint(OriginalEdgesPath) == Selection["originalEdgesSha256"]
    Text = Source.read_text()
    Lines = Text.splitlines()
    Last = max(Index for Index, Line in enumerate(Lines) if Line.startswith("sew "))
    Sewn = [Name for Name in Lines[Last].split()[1:] if not Name.startswith("--")]
    Names = [
        re.search(r"--name=(\S+)", Line)[1]
        for Line in Lines
        if Line.startswith("patch ")
    ]
    Patches = dict(zip(Names, Read(Source)))
    Edges = json.loads(EdgesPath.read_text())
    OriginalEdges = json.loads(OriginalEdgesPath.read_text())
    assert len(Sewn) == 1082 and len(Edges) == 9137 and len(OriginalEdges) == 9150
    Search = cKDTree([Patches[Name].Poles.mean((0, 1)) for Name in Sewn])
    Changed, NewNames, Declarations, Records = set(), [], [], []
    for Specification in Selection["groups"]:
        assert Specification[
            "approvedOrangeCopies"
        ], "Repair must touch an approved orange selection"
        assert [Sewn[Index] for Index in Specification["faces"]] == Specification[
            "names"
        ]
        Quads, Measures, Axes = Construct(
            Specification, Patches, Sewn, Edges, Specification["barriers"]
        )
        assert all(Measure["maximumDisplacementMm"] < 2 for Measure in Measures)
        assert all(Measure["rimNormalTargetMaximumDegrees"] < 1 for Measure in Measures)
        Mirrored = []
        for Name in Specification["names"]:
            Reflected = Mirror(Patches[Name])
            Indices = Search.query(Reflected.Poles.mean((0, 1)), k=8)[1]
            Matches = []
            for Index in Indices:
                Candidate = Patches[Sewn[Index]]
                if (
                    Reflected.Poles.shape == Candidate.Poles.shape
                    and np.max(abs(Reflected.Poles - Candidate.Poles)) < 1e-10
                    and np.max(abs(Reflected.KnotsU - Candidate.KnotsU)) < 1e-12
                    and np.max(abs(Reflected.KnotsV - Candidate.KnotsV)) < 1e-12
                ):
                    Matches.append(Sewn[Index])
            assert len(Matches) == 1, (Name, Matches)
            Mirrored.append(Matches[0])
        Sources = Specification["names"] + Mirrored
        assert not Changed.intersection(Sources), "Repair groups must not overlap"
        Changed.update(Sources)
        ReplacementNames = []
        for Index, Patch in enumerate(Quads):
            for Side, Surface in [("Left", Patch), ("Right", Mirror(Patch))]:
                Name = f'PatchRepair_{Specification["component"]:02d}_{Side}_{Index}'
                Declarations.append(Declaration(Surface, Name))
                NewNames.append(Name)
                ReplacementNames.append(Name)
        Records.append(
            {
                "label": Specification["label"],
                "selection": Specification,
                "mirroredSources": Mirrored,
                "newSurfaces": ReplacementNames,
                "measures": Measures,
                "projectionAxes": Axes.tolist(),
            }
        )
    Approved = set(
        json.loads((Root / "Liger_Feature_Review.features.json").read_text())[
            "repairEdges"
        ]
    )
    PriorHidden = set(
        json.loads((Root / "Liger_Roof_Repair.repair.json").read_text())[
            "hiddenCompletedOrangeCopies"
        ]
    )
    Search = cKDTree([Edge["points"][8][:3] for Edge in Edges])
    Hidden = []
    for Index in sorted(Approved - PriorHidden):
        Distance, Nearest = Search.query(OriginalEdges[Index]["points"][8][:3])
        Edge = Edges[Nearest]
        if (
            Distance < 1e-7
            and len(Edge["faces"]) == 2
            and all(Sewn[Face] in Changed for Face in Edge["faces"])
        ):
            Hidden.append(Index)
    FinalNames = [Name for Name in Sewn if Name not in Changed] + NewNames
    Insertion = [
        "# Actual guide-bounded surface reconstruction; archived source construction remains above.",
        "delete " + " ".join(sorted(Changed)),
        *Declarations,
        "sew "
        + " ".join(FinalNames)
        + " --open --knot-edges --split-junctions --name=Liger_Main_Body",
    ]
    FinalLines = Lines[:Last] + Insertion + Lines[Last + 1 :]
    Prefix = re.search(r"feature-copy .*?--name=([^\s]+)", Text)[1]
    FinalLines += [
        "# Hide only completed internal orange copies; preserve every original selection in the archive.",
        "hide " + " ".join(f"{Prefix}.e{Index}" for Index in Hidden),
    ]
    Destination = Root / "Liger_Patch_Repair.arc"
    Destination.write_text("\n".join(FinalLines) + "\n")
    Report = {
        "source": Source.name,
        "sourceSha256": Fingerprint(Source),
        "document": Destination.name,
        "documentSha256": Fingerprint(Destination),
        "sourceFaceCount": len(Sewn),
        "newFaceCount": len(FinalNames),
        "removedSurfaces": sorted(Changed),
        "retainedSurfaceCount": len(Sewn) - len(Changed),
        "newSurfaces": NewNames,
        "groups": Records,
        "hiddenCompletedInternalOrangeCopies": Hidden,
        "remainingVisibleOrangeCopies": len(Approved - PriorHidden) - len(Hidden),
        "geometryChanged": True,
        "wideWindowOffsetsApplied": False,
        "boundaryPolicy": "Existing rim curves retained. Both front arch borders remain distinct. Roof offset regenerates at 25 mm on the new body.",
        "scope": "Seven paired guide-bounded areas: roof junction, middle roof sides, window sides, crossbar strip, front arch bands, rear roof sides and rear roof centre.",
        "remaining": "Other orange regions, including bumper/fender junctions and the outer window surround, are not claimed repaired. Purple/red constant-width offsets remain pending.",
        "measurementLimit": "Training samples and soft tangent targets are not global shape or G1/G2 continuity certificates. Independent held-out and native verification are separate.",
        "validation": "Run PatchRepairVerification.py and LigerPatchVerification for native sewing, replay and held-out geometry checks.",
    }
    (Root / "Liger_Patch_Repair.json").write_text(json.dumps(Report, indent=2) + "\n")
    print(
        f"WROTE {Destination}: {len(Sewn)} -> {len(FinalNames)} faces; {len(Hidden)} completed internal orange copies archived"
    )


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument(
        "edges", type=Path, help="FeatureVerification export of Liger_Layout.arc"
    )
    Parser.add_argument(
        "original_edges",
        type=Path,
        help="FeatureVerification export of Liger_Main_Body_Simplified.arc",
    )
    Arguments = Parser.parse_args()
    ConstructDocument(Arguments.edges, Arguments.original_edges)
