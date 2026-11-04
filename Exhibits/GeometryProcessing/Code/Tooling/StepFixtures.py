#!/usr/bin/env python3
# ==================================================================================================================================
#                                                        STEPFIXTURES.PY
# ==================================================================================================================================
# 🧩 Third-party STEP files for the import gates (ExchangeValidation EX10): solids built and written by OpenCascade, so the reader
#    meets another writer's conventions (seam edges, pole vertices, SURFACE_CURVE wrappers, PCURVEs, B-spline blends, surfaces
#    of revolution and extrusion, voids, inch units). Each fixture comes with OpenCascade's own volume, area and topology counts
#    and with points of its triangulation (on the exact surfaces) in millimetres. GProp (adaptive, ε 10⁻¹²) is the reference
#    except where its quadrature is measurably off — the area of a SURFACE_OF_LINEAR_EXTRUSION and the volume of a
#    SURFACE_OF_REVOLUTION — where a Gauss–Legendre integral of the exact profile (evaluated by OpenCascade) replaces it;
#    both values are recorded in Expectations.txt.
#
#    python3 StepFixtures.py --folder Exhibits/GeometryProcessing/Code/StepFixtures
#    Headless Linux: LD_LIBRARY_PATH=_ClaudeScratch/stubs (PrepareLinuxBuild.sh exchange builds a no-op libGL.so.1 for OCP).

import argparse
import math
import os
import re

from numpy.polynomial.legendre import leggauss

from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeEdge, BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakeWire
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepFilletAPI import BRepFilletAPI_MakeChamfer, BRepFilletAPI_MakeFillet
from OCP.BRepGProp import BRepGProp
from OCP.collections import Array1_gp_Pnt
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepOffsetAPI import BRepOffsetAPI_ThruSections
from OCP.BRepPrimAPI import (BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCone, BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakePrism,
                             BRepPrimAPI_MakeRevol, BRepPrimAPI_MakeSphere, BRepPrimAPI_MakeTorus)
from OCP.GC import GC_MakeCircle, GC_MakeEllipse
from OCP.GeomAbs import GeomAbs_Plane
from OCP.GeomAPI import GeomAPI_PointsToBSpline
from OCP.GProp import GProp_GProps
from OCP.gp import gp_Ax1, gp_Ax2, gp_Dir, gp_Elips, gp_Pnt, gp_Vec
from OCP.IFSelect import IFSelect_RetDone
from OCP.Interface import Interface_Static
from OCP.STEPControl import STEPControl_AsIs, STEPControl_Writer
from OCP.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_SHELL, TopAbs_SOLID, TopAbs_VERTEX
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS

# Independent references by fixture name: {'Volume': …} / {'Area': …}, with the GProp value they replace.
REFERENCES = {}


# ----------------------------------------------------------------------------------------------------------------------
#                                                    SHAPES
# ----------------------------------------------------------------------------------------------------------------------

def Edges(Shape):
    Explorer, Out = TopExp_Explorer(Shape, TopAbs_EDGE), []
    while Explorer.More():
        Out.append(TopoDS.Edge(Explorer.Current()))
        Explorer.Next()
    return Out


def GaussIntegral(Function, Low, High, Pieces=64):
    # Composite Gauss–Legendre, 40 nodes per piece: far below 10⁻¹² relative for polynomial-smooth integrands.
    Nodes, Weights = leggauss(40)
    Total = 0.0
    for Piece in range(Pieces):
        A = Low + (High - Low) * Piece / Pieces
        B = Low + (High - Low) * (Piece + 1) / Pieces
        Total += sum(Weight * Function(0.5 * (B - A) * Node + 0.5 * (A + B)) for Node, Weight in zip(Nodes, Weights)) * 0.5 * (B - A)
    return Total


def Derivative(Curve, Parameter):
    Point, Tangent = gp_Pnt(), gp_Vec()
    Curve.D1(Parameter, Point, Tangent)
    return Point, Tangent


def Spline(Points):
    Array = Array1_gp_Pnt(1, len(Points))
    for Index, Point in enumerate(Points):
        Array.SetValue(Index + 1, gp_Pnt(*Point))
    return GeomAPI_PointsToBSpline(Array).Curve()


def FilletedBox():
    Box = BRepPrimAPI_MakeBox(40.0, 30.0, 20.0).Shape()
    Fillet = BRepFilletAPI_MakeFillet(Box)
    for Edge in Edges(Box):
        Fillet.Add(4.0, Edge)
    return Fillet.Shape()


def ChamferedBore():
    Block = BRepPrimAPI_MakeBox(gp_Pnt(-20.0, -20.0, 0.0), 40.0, 40.0, 15.0).Shape()
    Bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.0, 0.0, -1.0), gp_Dir(0.0, 0.0, 1.0)), 8.0, 17.0).Shape()
    Drilled = BRepAlgoAPI_Cut(Block, Bore).Shape()
    Chamfer = BRepFilletAPI_MakeChamfer(Drilled)
    for Edge in Edges(Drilled):
        Curve = BRepAdaptor_Curve(Edge)
        Middle = Curve.Value(0.5 * (Curve.FirstParameter() + Curve.LastParameter()))
        if abs(math.hypot(Middle.X(), Middle.Y()) - 8.0) < 1e-6:
            Chamfer.Add(1.5, Edge)
    return Chamfer.Shape()


def Torus():
    return BRepPrimAPI_MakeTorus(gp_Ax2(gp_Pnt(5.0, -3.0, 2.0), gp_Dir(0.0, 0.6, 0.8)), 20.0, 6.0).Shape()


def Sphere():
    return BRepPrimAPI_MakeSphere(gp_Pnt(1.0, 2.0, 3.0), 12.5).Shape()


def ApexCone():
    return BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)), 10.0, 0.0, 25.0).Shape()


def SplineLoft():
    Sections = BRepOffsetAPI_ThruSections(True, False)
    Bottom = GC_MakeCircle(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), 15.0).Value()
    Middle = GC_MakeEllipse(gp_Elips(gp_Ax2(gp_Pnt(2.0, 0.0, 20.0), gp_Dir(0.0, 0.0, 1.0)), 12.0, 7.0)).Value()
    Top = GC_MakeCircle(gp_Ax2(gp_Pnt(0.0, 3.0, 40.0), gp_Dir(0.0, 0.2, 1.0)), 9.0).Value()
    for Curve in (Bottom, Middle, Top):
        Sections.AddWire(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(Curve).Edge()).Wire())
    Sections.Build()
    return Sections.Shape()


def RevolvedSpline():
    # Open profile in the XZ half-plane, closed by two radial lines to the axis: a vase of revolution.
    Profile = Spline([(10.0, 0.0, 0.0), (14.0, 0.0, 8.0), (9.0, 0.0, 18.0), (11.0, 0.0, 26.0), (7.0, 0.0, 32.0)])
    Wire = BRepBuilderAPI_MakeWire()
    Wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0.0, 0.0, 0.0), gp_Pnt(10.0, 0.0, 0.0)).Edge())
    Wire.Add(BRepBuilderAPI_MakeEdge(Profile).Edge())
    Wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(7.0, 0.0, 32.0), gp_Pnt(0.0, 0.0, 32.0)).Edge())
    Wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0.0, 0.0, 32.0), gp_Pnt(0.0, 0.0, 0.0)).Edge())
    Face = BRepBuilderAPI_MakeFace(Wire.Wire()).Face()
    # Volume of revolution: π ∫ x² dz along the profile (the radial lines and the axis add nothing).
    REFERENCES['RevolvedSpline'] = {'Volume': GaussIntegral(lambda T: math.pi * Derivative(Profile, T)[0].X() ** 2 * Derivative(Profile, T)[1].Z(),
                                                            Profile.FirstParameter(), Profile.LastParameter())}
    return BRepPrimAPI_MakeRevol(Face, gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), 2.0 * math.pi).Shape()


def ExtrudedSpline():
    Upper = Spline([(0.0, 0.0, 0.0), (10.0, 6.0, 0.0), (20.0, 4.0, 0.0), (30.0, 9.0, 0.0)])
    Wire = BRepBuilderAPI_MakeWire()
    Wire.Add(BRepBuilderAPI_MakeEdge(Upper).Edge())
    Wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(30.0, 9.0, 0.0), gp_Pnt(30.0, -6.0, 0.0)).Edge())
    Wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(30.0, -6.0, 0.0), gp_Pnt(0.0, -6.0, 0.0)).Edge())
    Wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0.0, -6.0, 0.0), gp_Pnt(0.0, 0.0, 0.0)).Edge())
    Face = BRepBuilderAPI_MakeFace(Wire.Wire()).Face()
    Extrusion = gp_Vec(2.0, 1.0, 12.0)
    Prism = BRepPrimAPI_MakePrism(Face, Extrusion).Shape()
    # Area: the spline wall ∫ |C′ × V| du exactly, plus the planar faces (GProp is exact on planes).
    Wall = GaussIntegral(lambda T: Derivative(Upper, T)[1].Crossed(Extrusion).Magnitude(), Upper.FirstParameter(), Upper.LastParameter())
    Planes, Explorer = 0.0, TopExp_Explorer(Prism, TopAbs_FACE)
    while Explorer.More():
        if BRepAdaptor_Surface(TopoDS.Face(Explorer.Current())).GetType() == GeomAbs_Plane:
            Area = GProp_GProps()
            BRepGProp.SurfaceProperties_s(Explorer.Current(), Area, 1e-12, False)
            Planes += Area.Mass()
        Explorer.Next()
    REFERENCES['ExtrudedSpline'] = {'Area': Wall + Planes}
    return Prism


def HollowBlock():
    Outer = BRepPrimAPI_MakeBox(30.0, 20.0, 16.0).Shape()
    Cavity = BRepPrimAPI_MakeSphere(gp_Pnt(15.0, 10.0, 8.0), 5.0).Shape()
    return BRepAlgoAPI_Cut(Outer, Cavity).Shape()


def FilletedBoss():
    Plate = BRepPrimAPI_MakeBox(gp_Pnt(-25.0, -25.0, -8.0), 50.0, 50.0, 8.0).Shape()
    Boss = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0.0, 0.0, -1.0), gp_Dir(0.0, 0.0, 1.0)), 9.0, 16.0).Shape()
    Joined = BRepAlgoAPI_Fuse(Plate, Boss).Shape()
    Fillet = BRepFilletAPI_MakeFillet(Joined)
    for Edge in Edges(Joined):
        Curve = BRepAdaptor_Curve(Edge)
        Middle = Curve.Value(0.5 * (Curve.FirstParameter() + Curve.LastParameter()))
        if abs(Middle.Z()) < 1e-6 and abs(math.hypot(Middle.X(), Middle.Y()) - 9.0) < 1e-6:
            Fillet.Add(2.5, Edge)
    return Fillet.Shape()


def InchBlock():
    return BRepPrimAPI_MakeBox(gp_Pnt(-12.7, -6.35, 0.0), 25.4, 12.7, 50.8).Shape()


FIXTURES = [
    ('FilletedBox', FilletedBox, 'MM'),
    ('ChamferedBore', ChamferedBore, 'MM'),
    ('TiltedTorus', Torus, 'MM'),
    ('OffsetSphere', Sphere, 'MM'),
    ('ApexCone', ApexCone, 'MM'),
    ('SplineLoft', SplineLoft, 'MM'),
    ('RevolvedSpline', RevolvedSpline, 'MM'),
    ('ExtrudedSpline', ExtrudedSpline, 'MM'),
    ('HollowBlock', HollowBlock, 'MM'),
    ('FilletedBoss', FilletedBoss, 'MM'),
    ('InchBlock', InchBlock, 'INCH'),
]


# ----------------------------------------------------------------------------------------------------------------------
#                                                    MEASURES
# ----------------------------------------------------------------------------------------------------------------------

def Unique(Shape, Kind):
    # Distinct sub-shapes (IsSame ignores orientation); counts are small, so a pairwise test is enough. Degenerated edges
    # (OpenCascade's zero-length edges at poles and apexes) are not counted: the kernel has a bare vertex there.
    Seen, Explorer = [], TopExp_Explorer(Shape, Kind)
    while Explorer.More():
        if Kind == TopAbs_EDGE and BRep_Tool.Degenerated_s(TopoDS.Edge(Explorer.Current())):
            Explorer.Next()
            continue
        if not any(Explorer.Current().IsSame(Other) for Other in Seen):
            Seen.append(Explorer.Current())
        Explorer.Next()
    return len(Seen)


def SurfacePoints(Shape, PerFace):
    # Triangulation nodes lie on the exact surfaces; a spread of them per face, in millimetres.
    BRepMesh_IncrementalMesh(Shape, 0.5, False, 0.3, True)
    Out, Explorer = [], TopExp_Explorer(Shape, TopAbs_FACE)
    while Explorer.More():
        Face = TopoDS.Face(Explorer.Current())
        Location = TopLoc_Location()
        Triangulation = BRep_Tool.Triangulation_s(Face, Location)
        if Triangulation is not None:
            Count = Triangulation.NbNodes()
            Stride = max(1, Count // PerFace)
            for Index in range(1, Count + 1, Stride):
                Point = Triangulation.Node(Index).Transformed(Location.Transformation())
                Out.append((Point.X(), Point.Y(), Point.Z()))
        Explorer.Next()
    return Out


# ----------------------------------------------------------------------------------------------------------------------
#                                                    MAIN
# ----------------------------------------------------------------------------------------------------------------------

def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--folder', required=True)
    Arguments = Parser.parse_args()
    os.makedirs(Arguments.folder, exist_ok=True)
    Notes = []
    Lines = ['# Name Unit Volume[mm3] Area[mm2] Solids Shells Faces Edges(non-degenerated) Vertices — OpenCascade 8 (cadquery-ocp), written by StepFixtures.py']
    for Name, Build, Unit in FIXTURES:
        Shape = Build()
        if not BRepCheck_Analyzer(Shape).IsValid():
            raise SystemExit(Name + ': OpenCascade built an invalid shape')
        Interface_Static.SetCVal_s('write.step.schema', 'AP214IS')
        Interface_Static.SetCVal_s('write.step.unit', Unit)
        Writer = STEPControl_Writer()
        Writer.Transfer(Shape, STEPControl_AsIs)
        Path = os.path.join(Arguments.folder, Name + '.step')
        if Writer.Write(Path) != IFSelect_RetDone:
            raise SystemExit(Name + ': write failed')
        # Fixed header time stamp, so regenerating the fixtures only changes them when OpenCascade's output changes.
        with open(Path, encoding='latin-1') as Stream:
            Text = Stream.read()
        Text = re.sub(r"FILE_NAME\('([^']*)','[^']*'", r"FILE_NAME('\1','1970-01-01T00:00:00'", Text, count=1)
        with open(Path, 'w', encoding='latin-1', newline='\n') as Stream:
            Stream.write(Text)
        Volume, Area = GProp_GProps(), GProp_GProps()
        BRepGProp.VolumeProperties_s(Shape, Volume, 1e-12, False)
        BRepGProp.SurfaceProperties_s(Shape, Area, 1e-12, False)
        Counts = [Unique(Shape, Kind) for Kind in (TopAbs_SOLID, TopAbs_SHELL, TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX)]
        Measured = {'Volume': Volume.Mass(), 'Area': Area.Mass()}
        for Quantity, Value in REFERENCES.get(Name, {}).items():
            Notes.append('# %s %s: Gauss–Legendre %.12g replaces GProp %.12g (relative %.1e)' % (Name, Quantity, Value, Measured[Quantity], abs(Value - Measured[Quantity]) / Value))
            Measured[Quantity] = Value
        Lines.append('%s %s %.12g %.12g %s' % (Name, Unit, Measured['Volume'], Measured['Area'], ' '.join(str(Count) for Count in Counts)))
        with open(os.path.join(Arguments.folder, Name + '.points.txt'), 'w', newline='\n') as Stream:
            for Point in SurfacePoints(Shape, 12):
                Stream.write('%.12g %.12g %.12g\n' % Point)
        print('%-16s %s V %.6f A %.6f solids/shells/faces/edges/vertices %s' % (Name, Unit, Volume.Mass(), Area.Mass(), Counts))
    with open(os.path.join(Arguments.folder, 'Expectations.txt'), 'w', newline='\n') as Stream:
        Stream.write('\n'.join(Lines[:1] + Notes + Lines[1:]) + '\n')


if __name__ == '__main__':
    Main()
