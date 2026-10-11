#!/usr/bin/env python3
"""Verify orange geometry and retained guide contact on the rebuilt portions of the skin."""

import argparse
from copy import deepcopy
import json
from pathlib import Path
import numpy as np
from scipy.interpolate import BSpline
from ConsolidationSequence import ReadCurves
from PatchRepairVerification import Root, ParseSurfaces, TraceSourceCoordinates, Verify
from RoofSequence import Parameters, Evaluate, GridSamples
from OrangeRepairSequence import ReconstructSeed


def MeasureKnotNormalJump(Surface):
    Maximum = 0.0
    for Axis, Knots in enumerate([Surface.KnotsU, Surface.KnotsV]):
        Other = GridSamples(Surface.KnotsV if Axis == 0 else Surface.KnotsU, 3)
        Unique, Multiplicity = np.unique(Knots, return_counts=True)
        for Index in range(1, len(Unique) - 1):
            if Multiplicity[Index] < 3:
                continue
            Parameter = Unique[Index]
            Distance = (
                min(Parameter - Unique[Index - 1], Unique[Index + 1] - Parameter) * 1e-7
            )
            Normals = []
            for Sign in [-1, 1]:
                Fixed = np.full(len(Other), Parameter + Sign * Distance)
                Along, Across = (Fixed, Other) if Axis == 0 else (Other, Fixed)
                Normal = np.cross(
                    Evaluate(Surface, Along, Across, 1, 0),
                    Evaluate(Surface, Along, Across, 0, 1),
                )
                Normal /= np.linalg.norm(Normal, axis=1)[:, None]
                Normals.append(Normal)
            Angles = np.degrees(
                np.arccos(np.clip(np.sum(Normals[0] * Normals[1], axis=1), -1, 1))
            )
            Maximum = max(Maximum, float(Angles.max()))
    return Maximum


def VerifyGuideContact():
    Report = json.loads((Root / "Liger_Orange_Repair.json").read_text())
    Before = ParseSurfaces(Root / Report["source"])
    After = ParseSurfaces(Root / Report["document"])
    Text = (Root / Report["source"]).read_text()
    Hidden = set()
    for Line in Text.splitlines():
        if Line.startswith("hide "):
            Hidden.update(Line.split()[1:])
        elif Line.startswith("unhide "):
            Hidden.difference_update(Line.split()[1:])
    Positions = np.concatenate(
        [
            BSpline(Knots, Poles, 3)(np.linspace(0, 1, 4097))
            for Name, (Poles, Knots) in ReadCurves(Text).items()
            if Name not in Hidden
        ]
    )
    Records = []
    Tangency = []
    for Group in Report["groups"]:
        Axes = np.array(Group["projectionAxes"])
        Transforms = (
            TraceSourceCoordinates(Group["selection"]["joinHistory"])
            if "joinHistory" in Group["selection"]
            else None
        )
        Target = deepcopy(
            After[next(Name for Name in Group["newSurfaces"] if "_Left_" in Name)]
        )
        AfterAngle = MeasureKnotNormalJump(Target)
        BeforeAngle = (
            MeasureKnotNormalJump(
                ReconstructSeed(Group["selection"]["joinHistory"], Before)
            )
            if Transforms
            else None
        )
        if BeforeAngle is not None:
            assert AfterAngle < BeforeAngle + 1e-5, (
                Group["label"],
                BeforeAngle,
                AfterAngle,
            )
        Tangency.append(
            {
                "label": Group["label"],
                "originalJoinedKnotMaximumDegrees": BeforeAngle,
                "repairedKnotMaximumDegrees": AfterAngle,
            }
        )
        Target.Poles = Target.Poles @ Axes
        Maximum, Count = 0.0, 0
        for Name in Group["selection"]["names"]:
            Source = deepcopy(Before[Name])
            Low, High = Source.Poles.min((0, 1)), Source.Poles.max((0, 1))
            Chosen = Positions[
                np.all((Positions >= Low - 1e-7) & (Positions <= High + 1e-7), axis=1)
            ]
            if not len(Chosen):
                continue
            Chosen = Chosen @ Axes
            Source.Poles = Source.Poles @ Axes
            Coordinates = Parameters(Source, Chosen[:, :2])
            Sampled = Evaluate(Source, *Coordinates.T)
            Valid = np.linalg.norm(Sampled - Chosen, axis=1) < 2e-6
            Coordinates, Chosen = Coordinates[Valid], Chosen[Valid]
            if not len(Chosen):
                continue
            if Transforms:
                Mapped = (
                    np.c_[Coordinates, np.ones(len(Coordinates))] @ Transforms[Name].T
                )[:, :2]
                Normal = np.cross(
                    Evaluate(Source, np.array([0.5]), np.array([0.5]), 1, 0),
                    Evaluate(Source, np.array([0.5]), np.array([0.5]), 0, 1),
                )[0]
                if Normal[2] < 0:
                    Mapped[:, 0] = 1 - Mapped[:, 0]
            else:
                Mapped = Parameters(Target, Chosen[:, :2])
            Errors = np.linalg.norm(Evaluate(Target, *Mapped.T) - Chosen, axis=1)
            Maximum = max(Maximum, float(Errors.max()))
            Count += len(Chosen)
        assert Maximum < 2e-6, (Group["label"], Maximum)
        Records.append(
            {
                "label": Group["label"],
                "stationsOnOriginalSkin": Count,
                "maximumNewDistanceMetres": Maximum,
            }
        )
    assert sum(Record["stationsOnOriginalSkin"] for Record in Records) > 0
    File = Root / "Liger_Orange_Repair.verification.json"
    Verification = json.loads(File.read_text())
    Verification["retainedGuideContact"] = Records
    Verification["sampledKnotNormalJumps"] = Tangency
    Verification["guideContactScope"] = (
        "4097 stations per visible authored cubic guide, restricted to stations lying on modified source supports. Sampled contact check, not an interval certificate. Live roof-offset regeneration is verified natively."
    )
    File.write_text(json.dumps(Verification, indent=2) + "\n")
    print(
        "GREEN: original guide stations remain on the repaired skin within 2 micrometres"
    )


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("edges", type=Path)
    Arguments = Parser.parse_args()
    Verify(Arguments.edges, "Liger_Orange_Repair")
    VerifyGuideContact()
