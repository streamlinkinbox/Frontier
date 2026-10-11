#!/usr/bin/env python3
"""Rebuild the approved orange subsets, preserving rims, protected guide curves and mirrored layouts."""

import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from scipy.spatial import cKDTree
from MirrorSequence import Read
from PatchSequence import Patch, Refinement
from PatchRepairSolver import Construct
from RoofSequence import Mirror, Declaration

Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"


def JoinSupports(First, Second):
    Knots = np.array(
        sorted((Counter(First.KnotsV) | Counter(Second.KnotsV)).elements())
    )
    Left = np.einsum("bj,ijc->ibc", Refinement(First.KnotsV, Knots), First.Poles)
    Right = np.einsum("bj,ijc->ibc", Refinement(Second.KnotsV, Knots), Second.Poles)
    assert Left.shape[1] == Right.shape[1]
    assert np.max(np.linalg.norm(Left[-1] - Right[0], axis=1)) < 1e-9
    return Patch(
        np.concatenate([Left, Right[1:]]),
        np.r_[First.KnotsU[:-4] * 0.5, [0.5] * 3, 0.5 + Second.KnotsU[4:] * 0.5],
        Knots,
        np.zeros((2, 2), int),
        {},
    )


def ReconstructSeed(History, Patches):
    if "source" in History:
        return deepcopy(Patches[History["source"]])
    First = ReconstructSeed(History["first"], Patches)
    Second = ReconstructSeed(History["second"], Patches)
    for Turn in range(History["firstTurn"]):
        First = First.Rotate()
    for Turn in range(History["secondTurn"]):
        Second = Second.Rotate()
    return JoinSupports(First, Second)


def ComputeFingerprint(File):
    return hashlib.sha256(File.read_bytes()).hexdigest()


def ConstructDocument(EdgesPath, OriginalEdgesPath):
    Selection = json.loads((Root / "Liger_Orange_Repair.selection.json").read_text())
    Source = Root / Selection["source"]
    assert ComputeFingerprint(Source) == Selection["sourceSha256"]
    assert ComputeFingerprint(EdgesPath) == Selection["nativeEdgesSha256"]
    assert ComputeFingerprint(OriginalEdgesPath) == Selection["originalEdgesSha256"]
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
    assert len(Sewn) == 888 and len(Edges) == 8143 and len(OriginalEdges) == 9150
    Search = cKDTree([Patches[Name].Poles.mean((0, 1)) for Name in Sewn])
    Changed, NewNames, Declarations, Records = set(), [], [], []
    Membership = {}
    for Specification in Selection["groups"]:
        assert Specification[
            "approvedOrangeCopies"
        ], "Repair must touch an approved orange selection"
        assert [Sewn[Index] for Index in Specification["faces"]] == Specification[
            "names"
        ]
        SelectedFaces = set(Specification["faces"])
        assert not any(
            len(Edges[int(Index)]["faces"]) == 2
            and set(Edges[int(Index)]["faces"]) <= SelectedFaces
            for Index in Selection["protectedBodyEdges"]
        ), "An approved body-guide edge cannot be buried inside a replacement"
        Seed = (
            ReconstructSeed(Specification["joinHistory"], Patches)
            if "joinHistory" in Specification
            else None
        )
        Quads, Measures, Axes = Construct(
            Specification,
            Patches,
            Sewn,
            Edges,
            Specification.get("barriers", {}),
            Seed=Seed,
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
        for Name in Sources:
            Membership[Name] = len(Records)
        ReplacementNames = []
        for Index, Patch in enumerate(Quads):
            for Side, Surface in [("Left", Patch), ("Right", Mirror(Patch))]:
                Name = f'OrangeRepair_{Specification["component"]:02d}_{Side}_{Index}'
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
    HiddenNames = set()
    for Line in Lines:
        if Line.startswith("hide "):
            HiddenNames.update(Line.split()[1:])
        elif Line.startswith("unhide "):
            HiddenNames.difference_update(Line.split()[1:])
    PriorHidden = {Index for Index in Approved if f"Repair.e{Index}" in HiddenNames}
    Search = cKDTree([Edge["points"][8][:3] for Edge in Edges])
    Hidden = []
    for Index in sorted(Approved - PriorHidden):
        Distance, Nearest = Search.query(OriginalEdges[Index]["points"][8][:3])
        Edge = Edges[Nearest]
        if (
            Distance < 1e-7
            and len(Edge["faces"]) == 2
            and all(Sewn[Face] in Changed for Face in Edge["faces"])
            and len({Membership[Sewn[Face]] for Face in Edge["faces"]}) == 1
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
    Destination = Root / "Liger_Orange_Repair.arc"
    Destination.write_text("\n".join(FinalLines) + "\n")
    Report = {
        "source": Source.name,
        "sourceSha256": ComputeFingerprint(Source),
        "document": Destination.name,
        "documentSha256": ComputeFingerprint(Destination),
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
        "scope": "Nineteen paired orange subsets at front corners, upper sides and rear deck/fenders. Eighteen groups preserve existing parameter charts while joining complete shared sides; one front-window corner is re-charted.",
        "remaining": "Other orange regions, including bumper/fender junctions and the outer window surround, are not claimed repaired. Purple/red constant-width offsets remain pending.",
        "measurementLimit": "Training samples and soft tangent targets are not global shape or G1/G2 continuity certificates. Independent held-out and native verification are separate.",
        "validation": "Run PatchRepairVerification.py --prefix Liger_Orange_Repair and LigerPatchVerification with --orange for native sewing, replay, held-out geometry checks and close-up images.",
    }
    (Root / "Liger_Orange_Repair.json").write_text(json.dumps(Report, indent=2) + "\n")
    print(
        f"WROTE {Destination}: {len(Sewn)} -> {len(FinalNames)} faces; {len(Hidden)} completed internal orange copies archived"
    )


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument(
        "edges", type=Path, help="FeatureVerification export of Liger_Patch_Repair.arc"
    )
    Parser.add_argument(
        "original_edges",
        type=Path,
        help="FeatureVerification export of Liger_Main_Body_Simplified.arc",
    )
    Arguments = Parser.parse_args()
    ConstructDocument(Arguments.edges, Arguments.original_edges)
