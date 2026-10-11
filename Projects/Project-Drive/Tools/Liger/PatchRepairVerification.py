#!/usr/bin/env python3
"""Held-out shape, rim, reflection and numerical chart-orientation checks for the repaired CAD asset."""

import argparse
from copy import deepcopy
import hashlib
import json
from math import comb
from pathlib import Path
import re

import numpy as np
from scipy.interpolate import BSpline
from scipy.spatial import cKDTree
from MirrorSequence import Read
from RoofSequence import Evaluate, Parameters, Sides, GridSamples
from PatchSequence import Refinement
from ExtensionSequence import Nearest

Root = Path(__file__).resolve().parents[2] / "Content/Vehicles/Liger/Reconstruction"


def ParseSurfaces(File):
    Text = File.read_text()
    Names = [
        re.search(r"--name=(\S+)", Line)[1]
        for Line in Text.splitlines()
        if Line.startswith("patch ")
    ]
    return dict(zip(Names, Read(File)))


def VerifyChartOrientation(Patch, Axes):
    Refinements = []
    for Knots in [Patch.KnotsU, Patch.KnotsV]:
        Target = np.r_[
            np.repeat(Knots[0], 4),
            np.repeat(np.unique(Knots[4:-4]), 3),
            np.repeat(Knots[-1], 4),
        ]
        Refinements.append(Refinement(Knots, Target))
    Poles = np.einsum(
        "ai,ijc,bj->abc",
        *[Refinements[0], Patch.Poles @ Axes, Refinements[1]],
        optimize=True,
    )
    Along = np.arange(0, len(Poles) - 1, 3)[:, None] + np.arange(4)
    Across = np.arange(0, Poles.shape[1] - 1, 3)[:, None] + np.arange(4)
    Cells = Poles[Along[:, None, :, None], Across[None, :, None, :]].reshape(
        -1, 4, 4, 3
    )
    First = 3 * np.diff(Cells[..., :2], axis=1)
    Second = 3 * np.diff(Cells[..., :2], axis=2)
    Coefficients = np.zeros((len(Cells), 6, 6))
    for FirstAlong in range(3):
        for FirstAcross in range(4):
            for SecondAlong in range(4):
                for SecondAcross in range(3):
                    Factor = (
                        comb(2, FirstAlong)
                        * comb(3, SecondAlong)
                        / comb(5, FirstAlong + SecondAlong)
                        * comb(3, FirstAcross)
                        * comb(2, SecondAcross)
                        / comb(5, FirstAcross + SecondAcross)
                    )
                    Coefficients[
                        :, FirstAlong + SecondAlong, FirstAcross + SecondAcross
                    ] += Factor * (
                        First[:, FirstAlong, FirstAcross, 0]
                        * Second[:, SecondAlong, SecondAcross, 1]
                        - First[:, FirstAlong, FirstAcross, 1]
                        * Second[:, SecondAlong, SecondAcross, 0]
                    )
    Count = len(Coefficients)
    Left = np.array(
        [
            [
                comb(Index, Slot) * 0.5**Index if Slot <= Index else 0
                for Slot in range(6)
            ]
            for Index in range(6)
        ]
    )
    Right = Left[::-1, ::-1]
    Minimum = float("inf")
    for Depth in range(7):
        Bounds = Coefficients.min(axis=(1, 2))
        Accepted = Bounds >= -1e-18
        if Accepted.any():
            Minimum = min(Minimum, float(Bounds[Accepted].min()))
        Coefficients = Coefficients[~Accepted]
        if not len(Coefficients):
            return {
                "bezierCells": Count,
                "subdivisionDepth": Depth,
                "minimumJacobianCoefficient": Minimum,
            }
        assert (
            np.min(Coefficients[:, ::5, ::5]) > -1e-16
        ), "Negative chart Jacobian at a subcell corner"
        Coefficients = np.concatenate(
            [
                np.einsum("ai,pij,bj->pab", First, Coefficients, Second, optimize=True)
                for First in [Left, Right]
                for Second in [Left, Right]
            ]
        )
    raise AssertionError(
        "Chart orientation could not be established by numerical Bernstein subdivision"
    )


def MeasureSharedSeams(Patches):
    if len(Patches) != 3:
        return []
    ParametersAlong = np.linspace(0.00001, 0.99999, 193)
    Ones = np.ones_like(ParametersAlong)
    Seams = []
    for Index, First in enumerate(Patches):
        Second = Patches[(Index + 1) % len(Patches)]
        FirstPoints = Evaluate(First, Ones, ParametersAlong)
        SecondPoints = Evaluate(Second, ParametersAlong, Ones)
        Gap = float(np.linalg.norm(FirstPoints - SecondPoints, axis=1).max())
        FirstNormals = np.cross(
            Evaluate(First, Ones, ParametersAlong, 1, 0),
            Evaluate(First, Ones, ParametersAlong, 0, 1),
        )
        SecondNormals = np.cross(
            Evaluate(Second, ParametersAlong, Ones, 1, 0),
            Evaluate(Second, ParametersAlong, Ones, 0, 1),
        )
        FirstNormals /= np.linalg.norm(FirstNormals, axis=1)[:, None]
        SecondNormals /= np.linalg.norm(SecondNormals, axis=1)[:, None]
        Angles = np.degrees(
            np.arccos(np.clip(np.sum(FirstNormals * SecondNormals, axis=1), -1, 1))
        )
        assert Gap < 1e-10
        Seams.append(
            {
                "samples": len(ParametersAlong),
                "maximumGapMetres": Gap,
                "maximumNormalAngleDegrees": float(Angles.max()),
            }
        )
    return Seams


def TraceSourceCoordinates(History):
    if "source" in History:
        return {History["source"]: np.eye(3)}
    Rotation = np.array([[0.0, -1.0, 1.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]])
    Result = {}
    for Side, Offset in [("first", 0.0), ("second", 0.5)]:
        Transform = np.array(
            [[0.5, 0.0, Offset], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
        ) @ np.linalg.matrix_power(Rotation, History[Side + "Turn"])
        for Name, Previous in TraceSourceCoordinates(History[Side]).items():
            Result[Name] = Transform @ Previous
    return Result


def Verify(EdgesPath, Prefix="Liger_Patch_Repair"):
    Report = json.loads((Root / f"{Prefix}.json").read_text())
    Selection = json.loads((Root / f"{Prefix}.selection.json").read_text())
    assert (
        hashlib.sha256((Root / Report["source"]).read_bytes()).hexdigest()
        == Report["sourceSha256"]
    )
    assert (
        hashlib.sha256((Root / Report["document"]).read_bytes()).hexdigest()
        == Report["documentSha256"]
    )
    assert (
        hashlib.sha256(EdgesPath.read_bytes()).hexdigest()
        == Selection["nativeEdgesSha256"]
    )
    Before = ParseSurfaces(Root / Report["source"])
    After = ParseSurfaces(Root / Report["document"])
    Edges = json.loads(EdgesPath.read_text())
    Results = []
    for Group in Report["groups"]:
        print("VERIFY", Group["label"], flush=True)
        Axes = np.array(Group["projectionAxes"])
        Originals = [deepcopy(Before[Name]) for Name in Group["selection"]["names"]]
        Replacements = [
            deepcopy(After[Name]) for Name in Group["newSurfaces"] if "_Left_" in Name
        ]
        Certificates = [VerifyChartOrientation(Patch, Axes) for Patch in Replacements]
        Seams = MeasureSharedSeams(Replacements)
        for Patch in Originals + Replacements:
            Patch.Poles = Patch.Poles @ Axes
        Random = np.random.default_rng(72401 + Group["selection"]["component"])
        Samples = []
        Mapped = []
        Transforms = (
            TraceSourceCoordinates(Group["selection"]["joinHistory"])
            if "joinHistory" in Group["selection"]
            else None
        )
        for Name, Patch in zip(Group["selection"]["names"], Originals):
            Coordinates = Random.uniform(0.00001, 0.99999, (193, 2))
            Samples.append(Evaluate(Patch, *Coordinates.T))
            if Transforms is not None:
                Projected = (
                    np.c_[Coordinates, np.ones(len(Coordinates))] @ Transforms[Name].T
                )[:, :2]
                Normal = np.cross(
                    Evaluate(Patch, np.array([0.5]), np.array([0.5]), 1, 0),
                    Evaluate(Patch, np.array([0.5]), np.array([0.5]), 0, 1),
                )[0]
                if Normal[2] < 0:
                    Projected[:, 0] = 1 - Projected[:, 0]
                Mapped.append(Projected)
        Samples = np.concatenate(Samples)
        Errors = np.full(len(Samples), float("inf"))
        for Patch in Replacements:
            Coordinates = (
                np.concatenate(Mapped) if Mapped else Parameters(Patch, Samples[:, :2])
            )
            Positions = Evaluate(Patch, *Coordinates.T)
            Valid = np.linalg.norm(Positions[:, :2] - Samples[:, :2], axis=1) < 1e-8
            Errors[Valid] = np.minimum(
                Errors[Valid], np.linalg.norm(Positions[Valid] - Samples[Valid], axis=1)
            )
        assert np.isfinite(
            Errors
        ).all(), "Some source samples are not covered by replacement charts"
        assert Errors.max() < 0.002, (Group["label"], Errors.max())
        # Check source rim points against the new cubic boundaries without sharing concatenation parameters.
        Boundaries = [Curve for Patch in Replacements for Curve in Sides(Patch)]
        Search = cKDTree(
            np.concatenate([Curve(np.linspace(0, 1, 2049)) for Curve in Boundaries])
        )
        RimMaximum = 0.0
        for Index in Group["selection"]["rim"]:
            Edge = Edges[Index]
            Poles = np.array(Edge["poles"])
            Curve = BSpline(Edge["knots"], Poles[:, :3] / Poles[:, 3:4], Edge["degree"])
            Positions = (
                Curve(
                    np.linspace(
                        Edge["knots"][Edge["degree"]],
                        Edge["knots"][-Edge["degree"] - 1],
                        9,
                    )
                )
                @ Axes
            )
            _, Near = Search.query(Positions)
            for Position, Candidate in zip(Positions, Near):
                Distance = Nearest(Boundaries[Candidate // 2049], Position)[1]
                if Distance > 1e-7:
                    Distance = min(Nearest(Curve, Position)[1] for Curve in Boundaries)
                RimMaximum = max(RimMaximum, Distance)
        assert RimMaximum < 1e-7, (Group["label"], RimMaximum)
        Reflection = 0.0
        for Name in Group["newSurfaces"]:
            if "_Left_" not in Name:
                continue
            Coordinates = Random.uniform(0, 1, (257, 2))
            First = Evaluate(After[Name], *Coordinates.T) * [1, -1, 1]
            Other = Coordinates.copy()
            Other[:, 0] = 1 - Other[:, 0]
            Second = Evaluate(After[Name.replace("_Left_", "_Right_")], *Other.T)
            Reflection = max(
                Reflection, float(np.linalg.norm(First - Second, axis=1).max())
            )
        assert Reflection < 1e-10
        Result = {
            "label": Group["label"],
            "heldOutSamples": len(Samples),
            "maximumHeldOutDisplacementMm": float(Errors.max() * 1000),
            "rmsHeldOutDisplacementMm": float(np.sqrt(np.mean(Errors**2)) * 1000),
            "sampledRimMaximumMetres": RimMaximum,
            "reflectionMaximumMetres": Reflection,
            "numericalChartOrientation": Certificates,
            "sharedSeams": Seams,
        }
        Results.append(Result)
        print(json.dumps(Result), flush=True)
    Output = {
        "documentSha256": Report["documentSha256"],
        "groups": Results,
        "scope": "Independent held-out source-to-chart checks; exact spline samples. Numerical Bernstein checks use floating point, not interval arithmetic. Not a global Hausdorff or G1/G2 certificate.",
    }
    (Root / f"{Prefix}.verification.json").write_text(
        json.dumps(Output, indent=2) + "\n"
    )
    print(
        "GREEN: all selected patch groups passed held-out shape, rim, reflection and chart-orientation checks"
    )


if __name__ == "__main__":
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("edges", type=Path)
    Parser.add_argument("--prefix", default="Liger_Patch_Repair")
    Arguments = Parser.parse_args()
    Verify(Arguments.edges, Arguments.prefix)
