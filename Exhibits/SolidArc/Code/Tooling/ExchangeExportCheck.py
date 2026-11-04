#!/usr/bin/env python3
# ==================================================================================================================================
#                                                     EXCHANGEEXPORTCHECK.PY
# ==================================================================================================================================
# 🧩 Independent reading of the files the Exchange walkthrough writes (Export… → Exchange.step, Save as… → Exchange.stl): the STEP
#    file through OpenCascade (STEPControl_Reader, BRepCheck_Analyzer, adaptive GProp ε 10⁻¹²) and the STL through trimesh, both
#    against the bracket's closed-form volume 10 + 0.75 π/4 − 0.09 π − 0.15625 (1 − π/4) − 0.05 m³ in millimetres³.
#
#    python3 ExchangeExportCheck.py --folder _ClaudeScratch/SolidArcWorkspace > Exhibits/SolidArc/Results/ExchangeExport/ExchangeExportCheck.md
#    Headless Linux: LD_LIBRARY_PATH=_ClaudeScratch/stubs (PrepareLinuxBuild.sh exchange installs OCP + trimesh and the libGL stub).

import argparse
import math
import os

from OCP.STEPControl import STEPControl_Reader                                                 # First: OCP 8 crashes otherwise
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepGProp import BRepGProp
from OCP.GProp import GProp_GProps
from OCP.TopAbs import TopAbs_FACE, TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
import trimesh

# ----------------------------------------------------------------------------------------------------------------------
#                                                    MEASURES
# ----------------------------------------------------------------------------------------------------------------------

BracketVolume = (10.0 + 0.75 * math.pi / 4.0 - 0.09 * math.pi - 0.15625 * (1.0 - math.pi / 4.0) - 0.05) * 1.0e9   # [mm³]


def CountShapes(Shape, Kind):
    Explorer = TopExp_Explorer(Shape, Kind)
    Count    = 0
    while Explorer.More():
        Count += 1
        Explorer.Next()
    return Count


def ReadStepFile(Path):
    Reader = STEPControl_Reader()
    Status = Reader.ReadFile(Path)
    Reader.TransferRoots()
    Shape      = Reader.OneShape()
    Properties = GProp_GProps()
    BRepGProp.VolumeProperties_s(Shape, Properties, 1.0e-12, False)
    return {
        'Read':   int(Status) == 1,                                                        # IFSelect_RetDone
        'Valid':  BRepCheck_Analyzer(Shape).IsValid(),
        'Solids': CountShapes(Shape, TopAbs_SOLID),
        'Faces':  CountShapes(Shape, TopAbs_FACE),
        'Volume': Properties.Mass(),
    }


# ----------------------------------------------------------------------------------------------------------------------
#                                                    MAIN
# ----------------------------------------------------------------------------------------------------------------------

def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--folder', required=True)
    Arguments = Parser.parse_args()
    for Name in ('Exchange.step', 'Exchange.stl'):
        if not os.path.isfile(os.path.join(Arguments.folder, Name)):                     # OpenCascade crashes on a missing file
            print('🔴 %s is missing in %s' % (Name, Arguments.folder))
            return 1
    Step   = ReadStepFile(os.path.join(Arguments.folder, 'Exchange.step'))
    Facets = trimesh.load(os.path.join(Arguments.folder, 'Exchange.stl'))
    StepRelative = abs(Step['Volume'] - BracketVolume) / BracketVolume
    StlRelative  = abs(Facets.volume - BracketVolume) / BracketVolume
    StepPassed = Step['Read'] and Step['Valid'] and Step['Solids'] == 1 and Step['Faces'] == 11 and StepRelative <= 1.0e-9
    StlPassed  = Facets.is_watertight and Facets.is_winding_consistent and StlRelative <= 1.0e-3
    print('# Exchange walkthrough files read by other software\n')
    print('Bracket closed form V = %.6f mm³\n' % BracketVolume)
    print('| File | Reader | Findings | ΔV / V | Verdict |')
    print('|---|---|---|---|---|')
    print('| `Exchange.step` | OpenCascade STEPControl_Reader + BRepCheck_Analyzer + GProp (ε 1e-12) | read, valid %s, %d solid, %d faces, V %.6f mm³ | %.2e | %s |'
          % (Step['Valid'], Step['Solids'], Step['Faces'], Step['Volume'], StepRelative, '🟢' if StepPassed else '🔴'))
    print('| `Exchange.stl` | trimesh | %d facets, watertight %s, winding consistent %s, V %.1f mm³ (chordal sag) | %.2e | %s |'
          % (len(Facets.faces), Facets.is_watertight, Facets.is_winding_consistent, Facets.volume, StlRelative, '🟢' if StlPassed else '🔴'))
    return 0 if StepPassed and StlPassed else 1


if __name__ == '__main__':
    raise SystemExit(Main())
