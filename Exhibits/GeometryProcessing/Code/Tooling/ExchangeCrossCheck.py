#!/usr/bin/env python3
# ==================================================================================================================================
#                                                     EXCHANGECROSSCHECK.PY
# ==================================================================================================================================
# 🧩 Independent read-back of the exchange files written by ExchangeValidation --files: STL through trimesh (closure, winding,
#    Euler number, volume against the kernel's exact mass properties); STEP through OpenCascade (BRepCheck validity, solids,
#    shells, faces, surface types, volume and area against the kernel's exact values, distance from points of the exact model
#    to the shape read); galleries shaded from what each reader built.
#
#    Import (X3): OpenCascade reads the kernel's STEP of every OpenCascade-authored fixture it imported (OCCT → kernel → OCCT),
#    trimesh reads the kernel's tessellation of each imported fixture, and the import gallery shades those tessellations.
#
#    python3 ExchangeCrossCheck.py --files <folder> --report ExchangeValidation.md --markdown <out.md> --gallery <stl.png> --step-gallery <step.png>
#                                  [--fixtures Exhibits/GeometryProcessing/Code/StepFixtures --import-gallery <import.png>]
#    Headless Linux: LD_LIBRARY_PATH=_ClaudeScratch/stubs (PrepareLinuxBuild.sh exchange builds a no-op libGL.so.1 for OCP).

import argparse
import os
import re
import sys

import numpy
import trimesh

# ------------------------------------------------------------------------------------------------------------------------
#                                                    STL
# ------------------------------------------------------------------------------------------------------------------------

def ReadExactVolumes(ReportPath):
    # EX2 rows: "| 🟢 | EX2 | Block | V facets 24 vs exact 24: …"
    Volumes = {}
    for Line in open(ReportPath, encoding='utf-8'):
        Match = re.match(r'\| \S+ \| EX2 \| (\w+) \| V facets [\d.e+-]+ vs exact ([\d.e+-]+):', Line)
        if Match:
            Volumes[Match.group(1)] = float(Match.group(2))
    return Volumes

def CrossCheckStl(Folder, Volumes, Suffix=''):
    Rows = []
    for Name in sorted(Volumes):
        Path = os.path.join(Folder, Name + Suffix + '.stl')
        if not os.path.exists(Path):
            Rows.append((Name, None))
            continue
        Model = trimesh.load(Path, file_type='stl', process=True)
        Components = len(Model.split(only_watertight=False))
        Rows.append((Name, {
            'Faces': len(Model.faces), 'Vertices': len(Model.vertices), 'Watertight': Model.is_watertight, 'Winding': Model.is_winding_consistent,
            'Euler': Model.euler_number, 'Components': Components, 'Volume': Model.volume, 'Exact': Volumes[Name], 'Area': Model.area, 'Model': Model }))
    return Rows

# ------------------------------------------------------------------------------------------------------------------------
#                                                    STEP
# ------------------------------------------------------------------------------------------------------------------------

def ReadStepExpectations(ReportPath):
    # EX5 rows carry the B-rep counts ("faces 6/6, … shells 1/1, solids 1/1"), EX6 rows the exact V and A.
    Expected = {}
    for Line in open(ReportPath, encoding='utf-8'):
        Structure = re.match(r'\| \S+ \| EX5 \| (\w+) \| .*faces \d+/(\d+), edges \d+/(\d+), vertices \d+/(\d+), shells \d+/(\d+), solids \d+/(\d+)', Line)
        if Structure:
            Expected.setdefault(Structure.group(1), {}).update(dict(zip(('Faces', 'Edges', 'Vertices', 'Shells', 'Solids'), map(int, Structure.groups()[1:]))))
        Geometry = re.match(r'\| \S+ \| EX6 \| (\w+) \| .*exact V ([\d.e+-]+), A ([\d.e+-]+)', Line)
        if Geometry:
            Expected.setdefault(Geometry.group(1), {}).update({ 'Volume': float(Geometry.group(2)), 'Area': float(Geometry.group(3)) })
    return Expected

def CrossCheckStep(Folder, Expected):
    from OCP.BRep import BRep_Builder, BRep_Tool
    from OCP.BRepAdaptor import BRepAdaptor_Surface
    from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeVertex
    from OCP.BRepCheck import BRepCheck_Analyzer
    from OCP.BRepExtrema import BRepExtrema_DistShapeShape
    from OCP.BRepGProp import BRepGProp
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.gp import gp_Pnt
    from OCP.GProp import GProp_GProps
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.STEPControl import STEPControl_Reader
    from OCP.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_REVERSED, TopAbs_SHELL, TopAbs_SOLID
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopLoc import TopLoc_Location
    from OCP.TopoDS import TopoDS, TopoDS_Compound

    def Unique(Shape, Kind):
        # Distinct sub-shapes (IsSame ignores orientation); counts are small, so a pairwise test is enough.
        Seen, Explorer = [], TopExp_Explorer(Shape, Kind)
        while Explorer.More():
            if not any(Explorer.Current().IsSame(Other) for Other in Seen):
                Seen.append(Explorer.Current())
            Explorer.Next()
        return len(Seen)

    Rows = []
    for Name in sorted(Expected):
        Path = os.path.join(Folder, Name + '.step')
        if not os.path.exists(Path):
            Rows.append((Name, None))
            continue
        Reader = STEPControl_Reader()
        if Reader.ReadFile(Path) != IFSelect_RetDone:
            Rows.append((Name, { 'Read': False }))
            continue
        Reader.TransferRoots()
        Shape = Reader.OneShape()
        Volume, Area = GProp_GProps(), GProp_GProps()
        BRepGProp.VolumeProperties_s(Shape, Volume, 1.0e-9, False)
        BRepGProp.SurfaceProperties_s(Shape, Area, 1.0e-9, False)
        Kinds, Explorer = {}, TopExp_Explorer(Shape, TopAbs_FACE)
        while Explorer.More():
            Kind = str(BRepAdaptor_Surface(TopoDS.Face(Explorer.Current())).GetType()).split('.')[-1].replace('GeomAbs_', '')
            Kinds[Kind] = Kinds.get(Kind, 0) + 1
            Explorer.Next()
        # Boundary only (a compound of the shells), so a point is measured to the surface, not to the solid's inside.
        Boundary, Builder = TopoDS_Compound(), BRep_Builder()
        Builder.MakeCompound(Boundary)
        Explorer = TopExp_Explorer(Shape, TopAbs_SHELL)
        while Explorer.More():
            Builder.Add(Boundary, Explorer.Current())
            Explorer.Next()
        Distance, Points = 0.0, 0
        PointsPath = os.path.join(Folder, Name + '.points.txt')
        if os.path.exists(PointsPath):
            for Line in open(PointsPath):
                X, Y, Z = map(float, Line.split())
                Measure = BRepExtrema_DistShapeShape(Boundary, BRepBuilderAPI_MakeVertex(gp_Pnt(X, Y, Z)).Vertex())
                Distance = max(Distance, Measure.Value())
                Points += 1
        # OCCT's own tessellation for the gallery.
        BRepMesh_IncrementalMesh(Shape, 0.004 * max(1.0, Area.Mass() ** 0.5), False, 0.25, True)
        Triangles, Explorer = [], TopExp_Explorer(Shape, TopAbs_FACE)
        while Explorer.More():
            Face = TopoDS.Face(Explorer.Current())
            Kind = str(BRepAdaptor_Surface(Face).GetType()).split('.')[-1].replace('GeomAbs_', '')
            Location = TopLoc_Location()
            Mesh = BRep_Tool.Triangulation_s(Face, Location)
            if Mesh is not None:
                Nodes = [Mesh.Node(Index) for Index in range(1, Mesh.NbNodes() + 1)]
                for Index in range(1, Mesh.NbTriangles() + 1):
                    First, Second, Third = Mesh.Triangle(Index).Get()
                    if Face.Orientation() == TopAbs_REVERSED:
                        Second, Third = Third, Second
                    Triangles.append((Kind, [(Nodes[Corner - 1].X(), Nodes[Corner - 1].Y(), Nodes[Corner - 1].Z()) for Corner in (First, Second, Third)]))
            Explorer.Next()
        Rows.append((Name, { 'Read': True, 'Valid': BRepCheck_Analyzer(Shape).IsValid(), 'Solids': Unique(Shape, TopAbs_SOLID), 'Shells': Unique(Shape, TopAbs_SHELL),
                             'Faces': Unique(Shape, TopAbs_FACE), 'Edges': Unique(Shape, TopAbs_EDGE), 'Volume': Volume.Mass(), 'Area': Area.Mass(), 'Kinds': Kinds,
                             'Distance': Distance, 'Points': Points, 'Triangles': Triangles, 'Expected': Expected[Name] }))
    return Rows

def RenderStepGallery(Rows, Names, Path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as Pyplot
    from matplotlib.patches import Patch
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    Palette = { 'Plane': (0.72, 0.74, 0.78), 'Cylinder': (0.36, 0.56, 0.86), 'Cone': (0.40, 0.72, 0.52), 'Sphere': (0.86, 0.66, 0.30), 'Torus': (0.66, 0.46, 0.80), 'BSplineSurface': (0.90, 0.42, 0.36) }
    Chosen = [(Name, Values) for Name, Values in Rows if Name in Names and Values and Values.get('Read')]
    Columns = 4
    Lines = (len(Chosen) + Columns - 1) // Columns
    Figure = Pyplot.figure(figsize=(4.0 * Columns, 4.0 * Lines + 0.6), dpi=110)
    Light = numpy.array([0.45, -0.35, 0.82])
    Light /= numpy.linalg.norm(Light)
    for Index, (Name, Values) in enumerate(Chosen):
        Axes = Figure.add_subplot(Lines, Columns, Index + 1, projection='3d')
        # Matplotlib paints triangles back to front by centroid; OCCT's long planar fans then overlap wrongly, so split every
        # triangle longer than 1/12 of the model into four until none is (display only).
        Pending, Kinds, Pieces = [numpy.array(Triangle) for Kind, Triangle in Values['Triangles']], [Kind for Kind, Triangle in Values['Triangles']], []
        Limit = numpy.ptp(numpy.concatenate(Pending), axis=0).max() / 12.0
        while Pending:
            Triangle, Kind = Pending.pop(), Kinds.pop()
            if max(numpy.linalg.norm(Triangle[Index] - Triangle[(Index + 1) % 3]) for Index in range(3)) <= Limit:
                Pieces.append((Kind, Triangle))
                continue
            Middle = [(Triangle[Index] + Triangle[(Index + 1) % 3]) / 2.0 for Index in range(3)]
            for Piece in ([Triangle[0], Middle[0], Middle[2]], [Middle[0], Triangle[1], Middle[1]], [Middle[2], Middle[1], Triangle[2]], [Middle[0], Middle[1], Middle[2]]):
                Pending.append(numpy.array(Piece))
                Kinds.append(Kind)
        Values = dict(Values, Triangles=Pieces)
        Corners = numpy.array([Triangle for Kind, Triangle in Pieces])
        Normals = numpy.cross(Corners[:, 1] - Corners[:, 0], Corners[:, 2] - Corners[:, 0])
        Normals /= numpy.maximum(numpy.linalg.norm(Normals, axis=1, keepdims=True), 1e-300)
        Shade = numpy.clip(Normals @ Light, 0.0, 1.0) * 0.6 + 0.4
        Colours = [(*(numpy.array(Palette.get(Kind, (0.5, 0.5, 0.5))) * Level), 1.0) for (Kind, _), Level in zip(Values['Triangles'], Shade)]
        Axes.add_collection3d(Poly3DCollection(Corners, facecolors=Colours, edgecolors='none'))
        Low, High = Corners.reshape(-1, 3).min(axis=0), Corners.reshape(-1, 3).max(axis=0)
        Centre, Half = (Low + High) / 2.0, (High - Low).max() / 2.0
        Axes.set_xlim(Centre[0] - Half, Centre[0] + Half)
        Axes.set_ylim(Centre[1] - Half, Centre[1] + Half)
        Axes.set_zlim(Centre[2] - Half, Centre[2] + Half)
        Axes.set_box_aspect((1, 1, 1))
        Axes.view_init(elev=28, azim=-58)
        Axes.set_axis_off()
        Axes.set_title('%s\n%s · %d faces · ΔV/V %.1e' % (Name, 'valid' if Values['Valid'] else 'INVALID', Values['Faces'], abs(Values['Volume'] - Values['Expected']['Volume']) / Values['Expected']['Volume']), fontsize=9)
    Figure.legend(handles=[Patch(color=Colour, label=Kind) for Kind, Colour in Palette.items()], loc='lower center', ncol=len(Palette), fontsize=10, frameon=False)
    Figure.suptitle('STEP files read by OpenCascade (cadquery-ocp), shaded from its own tessellation, coloured by the surface type it read', fontsize=12)
    Figure.tight_layout(rect=(0, 0.04, 1, 1))
    Figure.savefig(Path)

# ------------------------------------------------------------------------------------------------------------------------
#                                                    IMPORT
# ------------------------------------------------------------------------------------------------------------------------

def ReadFixtureExpectations(Folder):
    Expected = {}
    for Line in open(os.path.join(Folder, 'Expectations.txt'), encoding='utf-8'):
        if not Line.strip() or Line.startswith('#'):
            continue
        Fields = Line.split()
        Expected[Fields[0]] = {'Unit': Fields[1], 'Volume': float(Fields[2]), 'Area': float(Fields[3]),
                               **dict(zip(('Solids', 'Shells', 'Faces', 'Edges', 'Vertices'), map(int, Fields[4:9])))}
    return Expected

def CrossCheckImport(Folder, FixtureFolder, Markdown, GalleryPath):
    from OCP.BRepCheck import BRepCheck_Analyzer
    from OCP.BRepGProp import BRepGProp
    from OCP.GProp import GProp_GProps
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.STEPControl import STEPControl_Reader
    from OCP.TopAbs import TopAbs_FACE, TopAbs_SHELL, TopAbs_SOLID
    from OCP.TopExp import TopExp_Explorer

    def Unique(Shape, Kind):
        Seen, Explorer = [], TopExp_Explorer(Shape, Kind)
        while Explorer.More():
            if not any(Explorer.Current().IsSame(Other) for Other in Seen):
                Seen.append(Explorer.Current())
            Explorer.Next()
        return len(Seen)

    Expected = ReadFixtureExpectations(FixtureFolder)
    MeshRows = CrossCheckStl(Folder, {Name: Values['Volume'] for Name, Values in Expected.items()}, '.imported')
    Meshes = dict(MeshRows)
    Table = ['| | Fixture | Unit | Re-read by OCCT | Solids | Shells | Faces | ΔV / V (OCCT) | ΔA / A (OCCT) | Kernel facets | Watertight | ΔV / V (facets) |',
             '|---|---|---|---|---|---|---|---|---|---|---|---|']
    Passed = True
    for Name, Expect in Expected.items():
        Path = os.path.join(Folder, Name + '.reexported.step')
        Reader = STEPControl_Reader()
        if not os.path.exists(Path) or Reader.ReadFile(Path) != IFSelect_RetDone:
            Table.append('| 🔴 | %s | %s | not read | | | | | | | | |' % (Name, Expect['Unit']))
            Passed = False
            continue
        Reader.TransferRoots()
        Shape = Reader.OneShape()
        Volume, Area = GProp_GProps(), GProp_GProps()
        BRepGProp.VolumeProperties_s(Shape, Volume, 1e-12, False)
        BRepGProp.SurfaceProperties_s(Shape, Area, 1e-12, False)
        Valid = BRepCheck_Analyzer(Shape).IsValid()
        Counts = [Unique(Shape, Kind) for Kind in (TopAbs_SOLID, TopAbs_SHELL, TopAbs_FACE)]
        VolumeError = abs(Volume.Mass() - Expect['Volume']) / Expect['Volume']
        AreaError = abs(Area.Mass() - Expect['Area']) / Expect['Area']
        Mesh = Meshes.get(Name)
        MeshError = abs(Mesh['Volume'] - Expect['Volume']) / Expect['Volume'] if Mesh else 1.0
        Sound = (Valid and Counts == [Expect['Solids'], Expect['Shells'], Expect['Faces']] and VolumeError <= 5.0e-5 and AreaError <= 5.0e-5
                 and Mesh is not None and Mesh['Watertight'] and Mesh['Winding'] and MeshError < 0.03)
        Passed = Passed and Sound
        Table.append('| %s | %s | %s | %s | %d | %d | %d | %.1e | %.1e | %s | %s | %.1e |' % ('🟢' if Sound else '🔴', Name, Expect['Unit'], 'valid' if Valid else 'INVALID', Counts[0], Counts[1], Counts[2],
                                                                            VolumeError, AreaError, Mesh['Faces'] if Mesh else '—', Mesh['Watertight'] if Mesh else '—', MeshError))
    with open(Markdown, 'a', encoding='utf-8') as Stream:
        Stream.write('\n## STEP import\n\n%s %d OpenCascade-authored fixtures (Code/StepFixtures, StepFixtures.py) imported by the kernel (EX11), then (a) written again by the kernel '
                     'and read by OpenCascade — BRepCheck validity, unique solids / shells / faces and adaptive volume / area (ε 10⁻¹²) against the fixture references — '
                     'and (b) tessellated by the kernel and read by trimesh (closure, winding, facet volume within 3 %%). OCCT → kernel → OCCT closes the loop: the kernel '
                     'neither lost nor invented geometry on the way through. Limits as for STEP export (5 × 10⁻⁵, OCCT re-derives trimming curves on B-spline faces).\n\n'
                     % ('🟢' if Passed else '🔴', len(Expected)))
        Stream.write('\n'.join(Table) + '\n')
    if GalleryPath:
        RenderGallery(MeshRows, list(Expected), GalleryPath,
                      'OpenCascade-authored STEP imported by the kernel — the kernel\'s own tessellation of what it read, re-read by trimesh %s' % trimesh.__version__)
    print('\n'.join(Table))
    return Passed

# ------------------------------------------------------------------------------------------------------------------------
#                                                    GALLERY
# ------------------------------------------------------------------------------------------------------------------------

def RenderGallery(Rows, Names, Path, Title=None):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as Pyplot
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    Chosen = [(Name, Values) for Name, Values in Rows if Name in Names and Values]
    Columns = 4
    Lines = (len(Chosen) + Columns - 1) // Columns
    Figure = Pyplot.figure(figsize=(4.0 * Columns, 4.0 * Lines), dpi=110)
    Light = numpy.array([0.45, -0.35, 0.82])
    Light /= numpy.linalg.norm(Light)
    for Index, (Name, Values) in enumerate(Chosen):
        Model = Values['Model']
        Axes = Figure.add_subplot(Lines, Columns, Index + 1, projection='3d')
        Shade = numpy.clip(Model.face_normals @ Light, 0.0, 1.0) * 0.65 + 0.3
        Colours = numpy.stack([Shade * 0.62, Shade * 0.70, Shade * 0.80, numpy.ones_like(Shade)], axis=1)
        Collection = Poly3DCollection(Model.triangles, facecolors=Colours, edgecolors=(0.1, 0.1, 0.1, 0.12), linewidths=0.15)
        Axes.add_collection3d(Collection)
        Low, High = Model.bounds
        Centre, Half = (Low + High) / 2.0, (High - Low).max() / 2.0
        Axes.set_xlim(Centre[0] - Half, Centre[0] + Half)
        Axes.set_ylim(Centre[1] - Half, Centre[1] + Half)
        Axes.set_zlim(Centre[2] - Half, Centre[2] + Half)
        Axes.set_box_aspect((1, 1, 1))
        Axes.view_init(elev=28, azim=-58)
        Axes.set_axis_off()
        Axes.set_title('%s\n%d facets · χ %d · %s' % (Name, Values['Faces'], Values['Euler'], 'watertight' if Values['Watertight'] else 'OPEN'), fontsize=10)
    Figure.suptitle(Title or 'STL files read back by trimesh %s (shaded from the facets it read)' % trimesh.__version__, fontsize=12)
    Figure.tight_layout()
    Figure.savefig(Path)

# ------------------------------------------------------------------------------------------------------------------------
#                                                    MAIN
# ------------------------------------------------------------------------------------------------------------------------

def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--files', required=True)
    Parser.add_argument('--report', required=True)
    Parser.add_argument('--markdown', required=True)
    Parser.add_argument('--gallery')
    Parser.add_argument('--step-gallery')
    Parser.add_argument('--fixtures')
    Parser.add_argument('--import-gallery')
    Arguments = Parser.parse_args()

    Volumes = ReadExactVolumes(Arguments.report)
    Rows = CrossCheckStl(Arguments.files, Volumes)
    Passed = True
    Table = ['| | Specimen | Facets | Vertices | Watertight | Winding | Euler | Components | V trimesh | V exact | ΔV / V |', '|---|---|---|---|---|---|---|---|---|---|---|']
    for Name, Values in Rows:
        if not Values:
            Table.append('| 🔴 | %s | missing | | | | | | | | |' % Name)
            Passed = False
            continue
        Relative = abs(Values['Volume'] - Values['Exact']) / Values['Exact']
        Sound = Values['Watertight'] and Values['Winding'] and Values['Volume'] > 0.0 and Relative < 0.03
        Passed = Passed and Sound
        Table.append('| %s | %s | %d | %d | %s | %s | %d | %d | %.9g | %.9g | %.2e |' % ('🟢' if Sound else '🔴', Name, Values['Faces'], Values['Vertices'], Values['Watertight'], Values['Winding'],
                                                                                   Values['Euler'], Values['Components'], Values['Volume'], Values['Exact'], Relative))
    with open(Arguments.markdown, 'w', encoding='utf-8') as Stream:
        Stream.write('# Exchange cross-check\n\n## STL\n\n%s %d STL files written by ExchangeValidation, read back by trimesh %s (vertices merged by trimesh itself, positive outward volume, '
                     'closure and winding by its own edge test). Exact volumes are the kernel\'s MeasureBodyMass values from ExchangeValidation.md; '
                     'the facet volume sits inside them by at most sag · area (EX2).\n\n' % ('🟢' if Passed else '🔴', len(Rows), trimesh.__version__))
        Stream.write('\n' + '\n'.join(Table) + '\n')
    if Arguments.gallery:
        RenderGallery(Rows, ['Torus', 'PerforatedBlock', 'HollowBlock', 'SweptRing', 'PipeBend', 'MirroredCross', 'BoxAllFillet', 'HoleRimFillet'], Arguments.gallery)

    # 📝 STEP: OCCT's volume of a B-spline face trimmed by 3D edges rests on 2D curves it re-derives by projection (the file
    #    carries no pcurves), which costs up to ~10⁻⁵ relative on promoted planar caps; the point distance has no such step.
    from OCP import __file__ as OcpPath
    StepRows = CrossCheckStep(Arguments.files, ReadStepExpectations(Arguments.report))
    StepTable = ['| | Specimen | BRepCheck | Solids | Shells | Faces | Edges (OCCT) | Surface types read | V OCCT | ΔV / V | ΔA / A | Points | Max distance |', '|---|---|---|---|---|---|---|---|---|---|---|---|---|']
    StepPassed = True
    for Name, Values in StepRows:
        if not Values or not Values.get('Read'):
            StepTable.append('| 🔴 | %s | not read | | | | | | | | | | |' % Name)
            StepPassed = False
            continue
        Expect = Values['Expected']
        VolumeError = abs(Values['Volume'] - Expect['Volume']) / Expect['Volume']
        AreaError = abs(Values['Area'] - Expect['Area']) / Expect['Area']
        Sound = Values['Valid'] and Values['Solids'] == Expect['Solids'] and Values['Shells'] == Expect['Shells'] and Values['Faces'] == Expect['Faces'] and VolumeError <= 5.0e-5 and AreaError <= 5.0e-5 and Values['Distance'] <= 1.0e-5 and Values['Points'] > 0
        StepPassed = StepPassed and Sound
        StepTable.append('| %s | %s | %s | %d | %d | %d / %d | %d | %s | %.10g | %.1e | %.1e | %d | %.1e |' % ('🟢' if Sound else '🔴', Name, 'valid' if Values['Valid'] else 'INVALID', Values['Solids'], Values['Shells'], Values['Faces'], Expect['Faces'],
                         Values['Edges'], ', '.join('%s %d' % Item for Item in sorted(Values['Kinds'].items())), Values['Volume'], VolumeError, AreaError, Values['Points'], Values['Distance']))
    with open(Arguments.markdown, 'a', encoding='utf-8') as Stream:
        Stream.write('\n## STEP\n\n%s %d STEP files read by OpenCascade (cadquery-ocp, %s): BRepCheck_Analyzer validity; unique solids, shells and faces against the B-rep; '
                     'adaptive volume / area integration (ε 10⁻⁹) against the kernel\'s exact values; the largest distance from the sampled points of the exact model '
                     '(tessellation vertices, which lie on the exact carriers) to the boundary OCCT built. OCCT counts the pole and apex degenerate edges it adds. '
                     'Limits: ΔV, ΔA ≤ 5 × 10⁻⁵ (OCCT re-derives 2D trimming curves on B-spline faces by projection, ≈ 10⁻⁵ on promoted planar caps), distance ≤ 10⁻⁵.\n\n'
                     % ('🟢' if StepPassed else '🔴', len(StepRows), os.path.dirname(OcpPath)[-40:].split('site-packages/')[-1]))
        Stream.write('\n'.join(StepTable) + '\n')
    if Arguments.step_gallery:
        RenderStepGallery(StepRows, ['BoxAllFillet', 'HoleRimFillet', 'MirroredCross', 'TiltedBoreChamfer', 'VariableArcFillet', 'PromotedBoxAllFillet', 'ApproximatedHoleRimFillet', 'PromotedTorus'], Arguments.step_gallery)
    ImportPassed = True
    if Arguments.fixtures:
        ImportPassed = CrossCheckImport(Arguments.files, Arguments.fixtures, Arguments.markdown, Arguments.import_gallery)
    print('\n'.join(Table))
    print('\n'.join(StepTable))
    return 0 if Passed and StepPassed and ImportPassed else 1

if __name__ == '__main__':
    sys.exit(Main())
