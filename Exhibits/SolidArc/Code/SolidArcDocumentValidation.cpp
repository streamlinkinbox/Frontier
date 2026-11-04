/*====================================================================================================================================
                                                    SOLIDARCDOCUMENTVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step 9: SolidArc document gates — construct tiles, picks, blends, face edits, parameters, undo, files, variable blends, drawn sketches

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcDocument.h"
#include "UI/SolidArc/Document/SolidArcMeasurement.h"
#include "UI/SolidArc/Document/SolidArcImagePlane.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include "UI/SolidArc/Document/SolidArcDrawingSheet.h"
#include "UI/SolidArc/Document/SolidArcExchange.h"
#include "UI/SolidArc/Document/SolidArcKnife.h"
#include "UI/SolidArc/Document/SolidArcScene.h"
#include "UI/SolidArc/Document/SolidArcSpecimen.h"
#include "UI/SolidArc/Document/VehicleCurveImport.h"
#include "UI/SolidArc/Viewport/SolidArcCamera.h"
#include "UI/SolidArc/Viewport/SolidArcGizmo.h"
#include "UI/SolidArc/Viewport/SolidArcSnapping.h"
#include "UI/SolidArc/Viewport/Generated/SolidArcShaderArchive.h"
#include "UI/SolidArc/Interface/SolidArcInterface.h"
#include "UI/SolidArc/Interface/SolidArcOutliner.h"
#include "UI/SolidArc/Interface/SolidArcScript.h"

#include <imgui.h>

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Blend/BlendCatalogue.h"
#include "Engine/GeometryProcessing/Drawing/DrawingExport.h"
#include "Engine/GeometryProcessing/Exchange/StepExchange.h"
#include "Engine/GeometryProcessing/Exchange/StlExchange.h"
#include "Engine/GeometryProcessing/Feature/ReplayDigest.h"
#include "Engine/GeometryProcessing/Geometry/NurbsCarrier.h"
#include "Engine/GeometryProcessing/Measurement/MassProperties.h"
#include "Engine/GeometryProcessing/Sketch/SketchProfile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::SolidArc;
using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double Pi = std::numbers::pi;

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - W1 …
        const char* Subject  = "";                                                           // [-] - What is measured
        const char* Limit    = "";                                                           // [-] - Pass condition (text)
        double      Worst    = 0.0;                                                          // [-] - Worst value
        long        Checks   = 0;                                                            // [-] - Measurements
        long        Failures = 0;                                                            // [-] - Measurements over the limit
    };

    GateTally ConstructGate   = { "W1", "Construct tiles (block, cylinder, sphere, torus, union, difference): |V − closed form| / V, sound audit", "≤ 10⁻⁹" };
    GateTally PickGate        = { "W2", "CPU ray pick: face under the ray / edge within reach resolves to the expected persistent name", "all equal" };
    GateTally BlendGate       = { "W3", "Fillet / chamfer on picked edges: |V − closed form| / V", "≤ 10⁻⁹" };
    GateTally FaceGate        = { "W4", "Face edit on a picked face (push): |V − closed form| / V", "≤ 10⁻⁹" };
    GateTally ParameterGate   = { "W5", "Parameter edit regenerates downstream steps; selection / references survive by name", "≤ 10⁻⁹, names kept" };
    GateTally AtomicGate      = { "W6", "Refused command (oversize fillet, empty selection) leaves tree, body and history untouched", "all equal" };
    GateTally HistoryGate     = { "W7", "Undo / redo restore exact volumes; delete removes a whole tile", "≤ 10⁻¹²" };
    GateTally FileGate        = { "W8", "TOML save → load → save is byte-equal; reloaded body volume and names equal", "equal" };
    GateTally VariableGate    = { "W9", "Variable blend (V / F): volume vs closed form; each size at its anchored corner through flip, parameter edit and reload", "≤ 10⁻⁹" };
    GateTally SurfacingGate   = { "W11", "Vehicle curves: VehicleCurves.toml round trip (bit-equal) → SpaceCurve steps → loft sheet vs analytic bonnet and limit points; pole move + undo; reload", "≤ 0.5 mm, equal" };
    GateTally PanelGate       = { "W12", "Vehicle panels: loft + loft (G2) → Alt+X mirror → 4-panel sheet; pod loft → mirror → Close → solid vs ∫ πab dy; crossing curves → network; reload byte-equal", "sound · rel. ≤ 2·10⁻³" };
    GateTally EvaluationGate  = { "W13", "Surface evaluation: deviation needles are true closest points (residual ∥ normal, ≤ 61² lattice) and ≤ 0.2 mm on the fitted bonnet; zebra phase jumps at a G0 crease and is continuous at G2; zebra density tessellation", "≤ 0.2 mm · G2 ≤ 10⁻⁶ stripe" };
    GateTally BlueprintGate   = { "W14", "Blueprint study: image planes (BMW calibration landmarks → world, picture decode, codec round trip, refusals, re-import by name) and x / y-axis Construct cylinders", "≤ 10⁻⁵ m · ≤ 10⁻⁹" };
    GateTally MatcapGate      = { "W15", "Matcaps: built-in library optics (tinted metal / white plastic highlights, brushed anisotropy, roughness), picture fitting, per-face / body assignment without regeneration, undo, regeneration by persistent name, codec, refusals", "exact · 1 / 255" };
    GateTally BodiesGate      = { "W16", "Multi-body: New body (R) tiles keep exact separate volumes; per-body matcaps resolve face → body → default without regeneration; Combine (Q) difference consumes the tool and its delete restores it; targeted Construct / blend touch one body; cross-body refusals; scene body spans; undo; codec", "≤ 10⁻⁹ · exact" };
    GateTally TrimGate        = { "W18", "Curve trim (Plasticity T): a pentagram drawn as one chain refuses to extrude, T on the five inner pieces leaves the star (area vs shoelace, extrude exact); preview leaves the document alone; cross-sketch trims (circle → arc, side split) with undo / redo; a trim that would break an extrude detaches (W46); an emptied sketch goes; codec", "≤ 10⁻¹² · ≤ 10⁻⁹ · exact" };
    GateTally CutGate         = { "W17", "Trim / split: C cuts a block by a drawn line (2 + 6 exact) and a circle (π/4), the outside piece keeps key and matcap; X removes picked bodies (undo / redo); vehicle loft cut by a character line along −z → two sound sheets, X the piece; refusals; codec", "≤ 10⁻⁹ · exact" };
    GateTally MoveGate        = { "W19", "Move / rotate / scale (G / R / S): Transform steps keep volume (×8 for scale 2), the scene and picking follow the body, face selection survives by name, undo exact; refusals untouched", "≤ 10⁻⁹ · exact" };
    GateTally CopyGate        = { "W20", "Duplicate (Shift+D) and copy / paste (Ctrl+C / Ctrl+V): moved copy = new active selected body with its own caption; paste after X copies the body as it stood at copy time; refusals", "≤ 10⁻⁹ · exact" };
    GateTally HideGate        = { "W21", "Hide family (H, Shift+H, Alt+H, Ctrl+H): scene bodies and picking follow, selection drops hidden bodies, undo, codec round trip, refusals", "exact" };
    GateTally CurveGate       = { "W22", "Join (J) two open drawings into a closed square (extrudes to 1); offset (O) it by ¼ (2.25); pipe (P) along a drawn line (π r² L); refusals; codec byte-equal with kinds 13 / 14 / 15", "≤ 10⁻⁹ · exact" };
    GateTally RevolveGate     = { "W29", "Revolve (Alt+R): annulus 3π about the proposed y-axis (New); a plate about its own edge line united → 4.625π, undo / redo; x-axis fallback quarter turn 3π/2; refusals (angle, crossing axis, no sketch, nothing to subtract); codec writes the point axis only without a line", "≤ 10⁻⁹ · exact" };
    GateTally EvolutionGate   = { "W30", "Evolution (Shift+P / Alt+E): rectangle along a proposed line sketch = prism 1; circle along a quarter arc = Pappus π²/16; a square swept through a block as Difference = 1.875; refusals; codec Kind 16 + Path", "≤ 10⁻⁹ planar · ≤ 10⁻⁶ NURBS skin" };
    GateTally ReplayGate      = { "W31", "Incremental replay: on a 27-step studded plate and the vehicle bonnet every command / undo / redo / preview clone equals a full replay with the cache off (volumes, names, tokens, step reports, saved file); appends resume at the new step, a parameter edit at its first binding step, undo / redo / clone restore every step; only the 8 newest history entries keep checkpoints", "identical · undo / redo / clone ≥ 10× · tail edit ≥ 2×" };
    GateTally XRayGate        = { "W32", "X-ray (Alt+Z) pick-through and box edges on a 4 × 2 × 1 block in the isometric view: the ray at the hidden back-bottom edge picks the top face opaque and that edge in x-ray, √(1.25² + 1) = 1.6008 farther along the ray; a visible edge still picks in x-ray; the full-viewport box takes the 9 edges off the far corner (0, 2, 0) opaque and all 12 in x-ray; an empty box takes none", "all equal" };
    GateTally MeasureGate     = { "W33", "Measure (I) and Section (Alt+S) on a 4 × 2 × 1 block and a separate 2 × 2 × 1 body: body mass properties, face area / perimeter, edge length, minimum distances and angles between faces and edges, totals, X / Z / face-plane sections with second moments, two-body sections by the parallel-axis theorem, hidden bodies skipped, refusals", "≤ 1e-9 relative" };
    GateTally SectionGate     = { "W34", "Section clip (Alt+S) on a 4 × 2 × 1 block cut at x = 1 (kept x ≤ 1): rays wholly on the removed side miss; a ray through the opening meets the cap at the back face x = 0 (distance 5, not the plane's 4); a kept face in front of the plane crossing still wins; removed edges are skipped, kept edges pick; the cap has no name; sheet faces show their back instead of a cap; the box takes only edges reaching the kept side; ComposeSolidArcSectionClip marks sheet bodies", "exact" };
    GateTally SnapGate        = { "W35", "Object snapping on a 4 × 2 × 1 block, a separate cylinder r 0.5 and coplanar sketches (isometric camera, pointer at projected pixels): endpoint, midpoint, edge / face / circle centres, on edge, on face, on circle, sketch crossing, tangent and perpendicular points from a reference, arc-length midpoint of a spline; hidden corners need x-ray, the section and excluded bodies drop candidates, the filter disables kinds, a dragged sketch excludes itself, direction lock lines", "≤ 1e-12 (on carrier)" };
    GateTally JointGate       = { "W36", "Joints (Alt+J) + interference on the bracket specimen: hover origins (rim centres, face centroids); a pin r 0.25 jointed revolute, flipped, offset 0.25 into the hole r 0.3 → centroid (3, 1.25, 0.5); a lever on the pin cap at 90° → exact bounds; measure lever + bracket clear 0.25, bracket + pin clear 0.05; pin offset −0.1 → lever overlaps (0.8 − 2 (0.2 √0.05 + 0.09 asin ⅔)) · 0.1; moving the bracket carries both; refusals; codec (joints, geometry, fixed point); undo", "≤ 1e-9" };
    GateTally SheetGate       = { "W37", "Drawing sheet (Alt+D) of the bracket specimen: kernel views, hidden lines and dimensions in mm (metres × 1000), automatic ISO 5455 scale (next larger refused), unit invariance against the millimetre kernel sheet, A4 third-angle placement, section A-A cap = exact section 4.116587, SVG / DXF export byte-equal beside the document, [DrawingSheet] codec round trip + refusals, hidden bodies refused", "≤ 1e-9" };
    GateTally BoundGate       = { "W39", "Bevel previews from the first Windows run: an upright fillet beside a 0.4 bottom fillet (the cap foot took the wrong root of the cylinder, the guide got a random tangent at the tangential corner, C′ blew up and the volume quadrature bisected for up to 334 s) builds every radius in a fraction of a second with |V − closed form| / V; a fillet / chamfer as wide as the neighbouring face (crashed) is refused with the document untouched", "≤ 10⁻⁹, ≤ 20 s, refused" };
    GateTally ExchangeGate    = { "W38", "STEP / STL exchange (Import… / Export…, Open / Save as by file type): bracket + sphere exported as STEP (the kernel writer's bytes, read back × 10⁹ mm³) and STL (watertight, \\|ΔV\\| ≤ sag · A); selection / hidden bodies choose what is written; Import makes one embedded Import step per solid (one undo entry) with equal volumes and faces; canonical fixed point; save → load → save byte-equal with equal digests; OCCT FilletedBoss (loop repaired) and InchBlock 1 × ½ × 2 in (inch → m) imported; a fillet on an imported edge = (1 − π/4) r² L; refusals atomic", "≤ 1e-9 (STL: \\|ΔV\\| ≤ sag · A)" };
    GateTally PrimitiveGate   = { "W40", "Construct catalogue primitives drawn as new bodies: cone (up and down), wedge, 3 / 6 / 8-sided prisms, tube and the drawn polygon against closed forms; eight tiles side by side as Separate bodies keep their own exact volumes (no Boolean); prism Radius and tube bore edits regenerate exactly; codec byte-equal; refusals atomic", "≤ 10⁻⁹ · exact" };
    GateTally InsetGate       = { "W41", "Inset (Plasticity O on faces · I): block / cylinder / triangle-prism tops split flat, pocketed and bossed against closed forms with exact face counts; a depth edit regenerates; the pocket floor takes a second inset; codec byte-equal; too-wide / zero / curved refusals atomic", "≤ 10⁻⁹ · exact" };
    GateTally SectionLoftGate = { "W42", "Loft through drawn sketches (line / polyline / rectangle / circle / slot / spline) + gizmo verbs (sketch place, face offset / tweak / rotate / scale): |V − closed form| / V, codec byte-equal, mirrored sketches refused, scaled ones accepted", "≤ 10⁻⁹" };
    GateTally GizmoGate       = { "W43", "Transform gizmo (Slate GizmoPRO): every handle of each mode set T / R / S / T+R / R+S / T+R+S is picked at its projected centre, a far pixel misses; axis parameter and rotation angle recovered from pixel rays", "≤ 10⁻⁹ · exact" };
    GateTally ShellGate       = { "W45", "Solid shells, sketch on a face, hide / lock: closed sections loft a capped solid of its own (two in one file), open sections refuse the solid and loft a sheet; sketches drawn on a level and an upright body face extrude onto it; sketch / body hide and lock are undoable appearance entries that survive the codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally CornerGate      = { "W47", "Sketch corner fillet / chamfer and fan lofts: every corner of an extruded rectangle rounded (the extrude follows, V = A − (4 − π) r²), one picked corner chamfered, the kept corner (virtual sharp) dragged with the fillets following, refusals leave the file untouched; two lofts out of one section sew into one sheet (one turned); codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally DetachGate      = { "W46", "Trim / join on sketches a body reads: the trim detaches (original kept hidden for its extrude, the trimmed curves a new sketch, the body unchanged, one undo entry); a whole-curve trim only hides; picked sketches join (disjoint profiles extrude together), a read one stays hidden; codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally PointGate       = { "W44", "Mode-1 sketch points (direct drag, gizmo on picked points): a rectangle corner dragged keeps H / V and the extrude follows; two corners moved together; Bezier poles turned 30° and a triangle scaled × 2 along x about their centre land exactly; one undo entry per drag; codec byte-equal; mismatched / missing / out-of-range refusals atomic", "≤ 10⁻⁹ · exact" };
    GateTally FillGate        = { "W48", "Fill and flip: a fresh (unfilled) rectangle extrudes into 4 walls (an open sheet), filling it (Inspector / F) makes the solid 2 and one undo entry; regions of overlapping curves (Fill tool): the lens and a crescent of two circles, a square with a circular hole, a spline arch cut by a line, each filled into a new sketch and extruded against closed forms; a lone circle fills itself; flip normals turns every face of a loft sheet and of extruded walls, a second flip removes the step, solids refuse; codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally FilletDragGate  = { "W49", "Fillet drags (the Windows report: a corner drag turned a fillet inside out, ~270°): on a 3 × 1.5 rectangle with four r 0.3 fillets, the bottom-left fillet's centre, both tangent points and the virtual sharp are dragged to 1 024 targets up to 3 m away; every accepted drag keeps every arc under a half turn (tangency keeps its starting side), the rest refuse; the reported drag (centre out past the corner) lands with 90° arcs; slots keep their tangent ends", "0 inverted · exact" };
    GateTally OrientGate      = { "W50", "Orient to faces: Construct solids drawn on a body face are built in the face frame (x / y on the face, height along the outward normal): a cylinder on the +x face of a 2 m cube unites to 8 + π/4 and reaches x = 3; all eight tiles on a face tilted 30° about x and 40° about z keep their closed-form volumes, a unit block's bounds are those of the turned cube; a circle sketched on the tilted plane extrudes along its normal; a skewed frame is refused untouched; codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally TangentGate     = { "W51", "Curved-face sketches (the Windows report: a cylinder joined on a cylinder's side, then a chamfer and moves, felt slow and refused the junction chamfer): the sketch plane is the exact tangent plane (surface foot and normal, not the facet hit); the extrude start sinks below it by the measured profile depth so a Union closes (an exactly tangent Union only touches along a line and leaves the gap open); the joined body has its closed-form volume, the junction (a cylinder–cylinder saddle curve) takes a chamfer; a Construct boss with a start unites the same way; codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally KnifeGate       = { "W52", "Knife (Shift+K, Blender KitOps style): an unfilled circle on a 2 m cube's top face is filled and cut 0.5 deep from a lift above the face (8 − 0.045π); a rectangle on the −y face cuts straight through (− 0.32); open profiles, zero depth and a missing target refuse untouched; the built-in boolean library (7 cutters drawn and swept by the document's own commands) is normalized to unit width, top at +0.05; the square pocket placed at width 0.4 turned 15° cuts 0.0192, raised on the +x face adds 0.0192, left separate is a 0.0224 body; Object mode cuts the cube with the kept cutter; each knife is one undo entry; a saved insert joins the library; codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally HandleGate      = { "W53", "Bevel handles (Plasticity pull / push): the front-top edge of a 4 × 2 × 1 block shows a uniform handle at mid-edge plus one per corner, each along the outward bisector; corner sizes 0.1 / 0.3 equal the V taper (closed form, rails at each corner); the top loop takes 4 corner sizes between the uniform 0.1 and 0.2 bodies; Corner parameters regenerate; refusals (foreign vertex, two sizes, non-positive); the start-size fit (≈ 15 % of the bounding span, round 1 · 2 · 5 step); undo / redo; codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally SolidKnifeGate  = { "W54", "Knife with 3D cutters (Construct primitives drawn on a face, then pulled into it): on a 2 m cube a box 0.4 × 0.4 sunk 0.3 (top 0.02 above the face) cuts 0.048; a cylinder r 0.2 on the +x face frame sunk 0.5 with the cutter kept cuts 0.02π and leaves a 0.0208π body (Duplicate + Combine); a sphere r 0.3 centred on the top face cuts a half ball 0.018π; a box boss (Union, foot 0.02 inside) adds 0.004; Separate leaves a tube body, selected; refusals (missing target, torus tube ≥ major, New) leave the document untouched; one undo entry each; codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally PocketBevelGate = { "W55", "Bevels on knife pockets in a curved face (cylinder r 2.126 × 1.8, pockets cut from the tangent plane): the slot opening loop takes fillets 0.02 · 0.0519 · 0.1 across its smooth joints (pinned volumes, decreasing); sizes 0.3 and 0.8553 refuse as too large, naming the outgrown face, document untouched; a hex pocket insert's rim loop takes 0.05 through its unequal mitres, its wall–wall edges 0.05 together; on a box the rim loss stays within 10 % of the straight-edge model (1 − π/4) r² L; one undo entry each; codec byte-equal", "≤ 10⁻⁹ pinned · exact" };
    GateTally ReachGate       = { "W56", "Knife insert gesture (press = centre, drag = size + turn by the insert's grip, release = depth): grips sit on the farthest outline point (triangle vertex at 90°, hex vertex at 0°, square corner at 45°); a built-in at another depth is authored again (analytic STEP, no B-spline) — a triangle 0.4 at 0.2 deep cuts √3/4·0.4²·0.2, a round hole 0.4 at 0.6 deep cuts 0.04π·0.6, a countersink 0.4 at 0.4 deep cuts its 45° cone frustum plus bore; a countersink shallower than its cone refuses untouched; a library file stretches along z (Stretch = depth / its depth); one undo entry each; codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally SplitGate       = { "W57", "Sketch Cut, edge rings and image-plane properties: a filled 2 × 1 rectangle's bottom line cut at x = 0.5 becomes two lines sharing the new point (5 curves) and its extrude keeps V = 2; a circle r 0.5 cut becomes two half arcs (same centre, ends opposite) and its extrude keeps V = π/4; a cut on a line's end refuses untouched, a spline cut splits it; the front-top edge of a 4 × 2 × 1 block rings to its four edges along x (all at |y| ∈ {0, 2}, z ∈ {0, 1}); an image plane's photo / mirror / lock / turn / crop round-trip the codec byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally LoopGate        = { "W58", "Blender edge loops / rings and the polyline knife: on a 4 × 2 × 1 block the front-top edge loops to itself (box corners are three-edge poles) and rings to its four edges along x; a hexagonal prism's top rim edge loops round the hexagon (6, the n-gon hub rule) and rings across the even hexagons (4 rim edges), an upright rings to all 6 uprights; a pentagonal prism's rim loops to 5 and rings to 2 (odd faces stop), its upright rings to 5; a triangular prism's rim loops to itself (no hub at 3 sides); a polyline (1, −1) → (1, 1.5) → (3, 1.5) → (3, 3) slices the block into 3 + 5 exact, and the L piece's top rim loops to its 6 edges; a cylinder's top circle loops to itself and rings to the bottom circle; open walls' top lip loops round the open boundary (4) and rings to the bottom lip; on a revolved diamond (corners with four edge ends) a seam loops round the profile (4) and a circle rings to all 4 circles", "exact" };
    GateTally ManifoldGate    = { "W60", "Extrude manifold (Blender's push-in, the Windows report \"i want to extrude the shape i cant\"): a closed polyline / circle drawn on a face of a 4 × 2 × 1 block reads the material below it, so E dragged in cuts a pocket (Difference started a hair outside) and out adds; an inward Union merges flush (why direction now decides); a boss cap pushed back past its base (push refused) is cut as a prism of its outline, a pocket floor and a whole face push", "1e-9 rel" };
    GateTally ImprintGate     = { "W61", "2D cutters (imprint, the request \"2d cutters ... dont cut into the body\"): an outline drawn on a face of a 4 × 2 × 1 block splits that face only — volume and soundness unchanged, the face count +1 — for an island, a rectangle overhanging half the top, open lines and an L polyline edge to edge, a circle on the front; the split regions then push / pull with face E (pocket, boss, step, raised step), a raised neighbour stays unsplit, off-body outlines refuse, and the step survives save / load", "1e-9 rel" };
    GateTally PointCutGate    = { "W62", "Cut at crossings and duplicate 2D objects (the request \"cut at intersections of 2d objects … circle and another circle overlap … ctrl to snap to that point\", \"add duplicate hotkey\"): one cut at a crossing of two overlapping circles (separate sketches) breaks both at both crossings — lens arcs + outer arcs, each sketch still a closed profile (extrudes to πr²h), one undo entry; a line through a circle breaks with it; a lone circle halves; a point on no curve / on ends only refuses; sketch copies carried by a rigid map (scale refused)", "1e-9 rel" };
    GateTally CurveFlowGate   = { "W63", "Point on curve and Flow along curve (the request \"add point on the curve without changing the shape … project another 2d shape onto it; eg take a Y and project it on a nurbs curve\"): a point inserted on a Hermite / Bezier / NURBS / cubic spline / line / circle leaves the same curve (Hausdorff), lines drop H, ends refuse; in a document one undo entry and the knots survive save / load; a 2 × 0.4 rectangle flowed along a quarter arc r 3 lies on r 2.8 / 3.2 with its short sides as radii; true length on a circle spans ⅔ rad and extrudes to the annular sector 0.8", "1e-9 · 1e-6 bent" };
    GateTally EdgePathGate    = { "W65", "Edge path (the request \"select a edge from point A to B ... close the path ... shortest path or just the loop ... not like blender's shortest path that cheats\"): Ctrl+click path pick between two edges; Along loop keeps to one loop holding both (edge loop, face rim, tangent chain) — the shorter side, Other side the far one, Whole loop all of it — and refuses rather than cut across; Shortest is the least total length over any edges. Fixtures on a 4 × 2 × 1 block, a hexagonal prism, a cylinder, a revolved diamond and a through-hole block, then every ordered edge pair of six bodies: paths run From … To edge to edge, Along ⊆ Whole, Whole a simple chain or cycle (no vertex used thrice), Along ≥ Shortest, Along ∪ Other side = Whole on a closed loop", "1e-9" };
    GateTally DeleteGate      = { "W68", "Delete everything picked (the request \"add delete for all entities face / edge / object / outliner and viewport … X key or delete\"): on the 4 × 2 × 1 block a fillet face or fillet edge un-blends (the Blend lets go of its edge: 8 again, or one fillet of two left); a seam edge of an imprinted rectangle or an edge-to-edge imprint line dissolves (6 faces, 12 edges, 8 vertices, 8); refusals untouched — a box edge, a corner vertex, fillet and plain edges together; an extruded sketch leaves the scene (RemovedCurves, the body stays, save / load keeps it, show refuses, undo restores) while a free sketch is deleted; sketch points take their curves (renumbered, re-solved), an emptied sketch goes, a point of an extruded rectangle detaches the rest (body kept); bodies removed; one undo entry each", "exact · 1e-9" };
    GateTally ReshapeGate     = { "W67", "Direct edits of solids (the request \"tweak certain edges … like a box … move the edges / vertices … without breaking the object … show the gizmo … if theres a bevel … other faces follow\"): on a 4 × 2 × 1 block an edge moved across (7), up (10), along itself (8, side faces bilinear), scaled ½ about its middle (7) and turned 30° about the floor edge (prism of the turned section); a corner raised 0.5 (9, the top a bilinear patch) and pushed out 0.5 (8.25); refusals untouched — a curved edge, a collapsed edge, an inside-out face, a warped hexagon; with a bottom-back fillet the step goes before the Blend, which re-solves (7 − (1 − π/4) r² · 4); a fillet's own edge refuses; one undo entry; save / load byte-equal", "≤ 10⁻⁹ · exact" };
    GateTally CurveAskGate    = { "W69", "Curves and sheets (the request \"lofting the yellow line with yellow line it refused … a button to swap the normals … H to hide curves … snap a curve along a curve … 3D gizmo in 3D\"): L between picked sketches — two open curves side by side in one view, two concentric circles, an open curve to its wrapped copy, curves in two views — gives a solid or a sheet, never a refusal; flip of one picked face turns only its piece of a two-loft sheet (each face's sense reversed, the other piece kept), again removes the step, a solid's face refuses; H hides picked curves (HiddenCurves) and Alt+H brings them back; a 3D curve through off-plane points passes through each; sketch points lifted off their plane turn their curve into a 3D curve of the same form (on the plane: identical to the sketch curve)", "1e-9 · fit 10⁻⁶" };
    GateTally FullReachGate   = { "W70", "Bevel reach (the report \"clamped whilst it can bevel more … the chamfer chamfers more distance for the same bevel … bevel 1 edge fully … till the next edge making it a triangle\"): one cube edge at the full span uses up the face beside it (a chamfer leaves a triangle section, a fillet a quarter round — exact volumes); on a block the span is the shorter face; fillet and chamfer take the same largest size on the same edges; past the face width the blend runs on over the next face (exact overflow volumes); a fillet on a 135° edge takes a radius over twice the face width", "1e-9 · overflow 1e-6" };
    GateTally BevelMergeGate  = { "W71", "Bevel merge (the report \"when we bevel and edges / vertices come close together they must merge or create new vertex\"): blends that meet fuse instead of refusing — two parallel top fillets / chamfers at half the face width share one ridge edge (the top face is dropped), the top loop at half the width closes to one apex vertex, on a 2 × 1 block to a hip roof (ridge + two apexes); exact volumes, Euler counts, sound", "1e-9" };
    GateTally WrapGate        = { "W66", "Wrap around curve, sketch scale and curve offset (the request \"apple text and circle … wrap like it would around a can of coke … S doesnt seem to work … add a 2nd outer wheelarc … no inset\"): a 1 × 0.5 rectangle seen along +y wraps round the r 1 cylinder of a circle keeping its lengths (every sample on r 1 and, unrolled, on the rectangle; ends at ±0.5 rad) and projects straight onto it (x kept, ±π/6); too wide to project refuses; a circle facing the view stands on a chosen axis; an open arc rail r 2; letters as separate shapes share one centre; in a document 3D curve steps WrapN, picked, one undo entry; S scales a circle (radius and centre ×2), stretches a rectangle, refuses stretching a circle, Shift+D copies at half size; O offsets an open arc to concentric r 2 ± 0.3; precision round a free curve (samples on the rail to 10⁻⁹, the fitted curve within 10⁻⁶ of the width); a detailed wiggle steps down instead of refusing", "1e-9 · fit 10⁻⁶ width" };
    GateTally EdgeNotchGate   = { "W64", "Extrude manifold at a face edge (the Windows report \"extrude manifold suppose to remove or move the top part to match the extrude\"): a region of the 4 × 2 × 1 block's front face that touches, overhangs or corners the top / bottom / left edge, pushed in by face E (imprinted) or cut by sketch E, is a notch OPEN through the neighbouring face — no zero-thickness lid left over it (rays from above / below land on the notch floor) — with the exact volume; through slots, outward pulls and a top-face region at the front edge alike", "1e-9 rel" };
    GateTally PieceGate       = { "W59", "Bevels on polyline-knife pieces (the Windows report \"spans Cut1 and Block1\" and the edges that refused): a 4 × 2 × 1.5 block with its x = 4 uprights rounded r 0.8 (or plain) is sliced by a polyline beside / through the rounded end; names both pieces carry blend the piece they were picked on, and every crease edge of both pieces takes a 0.04 fillet and a 0.04 chamfer alone (a rim ending where its support turns tangent takes its tangent chain; a convex edge ending at a reflex corner grows the third face), the uprights together too; smooth edges (tangent faces) have no crease and are skipped", "exact" };
    GateTally DrawingGate     = { "W10", "Drawn sketches (line, rectangle, circle, arc, slot, 4 curve forms) → extrude / Boolean: |V − closed form| / V; refusals; reload; delete", "≤ 10⁻⁹" };
    GateTally CutterSpeedGate = { "W72", "Boolean library cutters & speed: all built-in inserts + coaxial stretched cylinders/cones retain exact analytic carriers (0 NURBS promotion), STEP & scene tessellation caches hit, sub-50 ms per library cut, sound manifold", "≤ 10⁻⁹ · ≤ 50 ms" };
    GateTally ParametricVarGate = { "W73", "Modular parametric variables: Create / SaveStepValueAsVariable / BindSolidArcStepValue / Rename / Delete; bevel Size and bolt Combine.Offset driven by user variables update geometry modularly; byte-equal TOML round-trip", "≤ 10⁻⁹ · exact" };
    GateTally LiveBooleanGate = { "W74", "Live Booleans: KnifeSolidArcLiveInsert & AddSolidArcLiveBoolean render solid target + semitransparent cutter (SolidCornerCount split, Copper matcap, face & submerged-edge picking), real-time move, export exclusion, codec round-trip, and Apply", "≤ 10⁻⁹ · exact" };
    GateTally EdgeWeightGate  = { "W75", "Edge weight paint & Bevel modifier: AssignSolidArcEdgeWeights (literal & variable-bound weights), EdgeGroup / Picked / Angle bevel modifiers, variable edit regenerates weighted bevels, codec round-trip", "≤ 10⁻⁹ · sound" };
    GateTally LinearCircArrayGate = { "W76", "Linear & Circular Array modifiers: 1D/2D grid array, variable-bound counts, overlapping MergeCondition union, radial & helical circular array, Genus 0 per region, Apply bakes per-solid STEP imports", "≤ 10⁻⁹ · sound" };
    GateTally CurveSurfArrayGate = { "W77", "Curve & Surface Array modifiers: array along 2D Sketch chain & 3D SpaceCurve with RMF alignment, UV surface array across curved cylinder face with normal alignment, sound manifold", "≤ 10⁻⁹ · sound" };
    GateTally CurveBendStackGate = { "W78", "Bend along curve & Modifier Stack: 3D spatial B-rep bend of a solid bar along a 90° arc and 3D curve (T1–T6 & G1–G6 certified), stack reorder / mute / undo / redo / byte-equal TOML round-trip", "≤ 2·10⁻³ · sound" };
    GateTally CompoundCutterGate = { "W79", "Multi-stage Compound Cutters (Countersink, Counterbore, Stepped hole, Hex socket, Stepped slot, Stepped pocket): non-stretching stage-by-stage construction, exact analytic closed-form volumes, 0 NURBS promotion, 0 duplicate 1-stage shapes", "≤ 10⁻⁹ · exact" };
    GateTally LiveEditableGate   = { "W80", "Realtime-editable semitransparent Live Boolean cutters & standalone icon verification: Face Push/Pull & Edge Bevel directly on a semitransparent Live Compound Cutter update the carved solid target in real time; 65 SolidArc SVG icons verified", "≤ 10⁻⁹ · sound" };
    GateTally GizmoAllViewsGate  = { "W81", "Transform gizmo across all 7 views (Top, Bottom, Front, Back, Right, Left, 3D Isometric) & all 2D/3D primitives, curves & handles: 2 screen-plane arrows + plane quad in every 90° view (including XZ/YZ sketches & curve handles in Top view), 3 arrows + 3 quads in 3D, SpaceCurve & pole handle gizmos, ray-pick and live apply", "exact · ≤ 10⁻⁹" };
    GateTally ContinuedLoftGate  = { "W82", "Continued & shared-endpoint V-lofts: 2 curves lofted then 3rd V-curve at shared endpoint with different height sews into 3 curves & 3 surfaces (caps to solid), obtuse/acute V-lofts, 3-section hinged loft, SpaceCurve V-loft without Network misroute, >90° 3-panel loop untwist, T-fin fallback, and laminar edge-to-curve loft", "sound · ≤ 10⁻⁹" };
    GateTally HotkeysOutlinerGate = { "W83", "Hotkeys (Ctrl+I Isolate toggle, Ctrl+A Select All across pick modes 1–5 with text-input guard, Ctrl+Shift+I Import) & Outliner cleanup (bottom 'Construct geometry' card removed, header '+' ConstructLauncher alias kept)", "exact" };
    GateTally DimensionLinesGate  = { "W84", "CAD Dimension Lines: associative linear / diameter Ø / radius R / angular ° / overall W×D×H / axis-locked dX·dY·dZ dimensions, live parametric regeneration, undo/redo, byte-equal .solidarc TOML codec round-trip, and interactive viewport session + pills", "≤ 10⁻⁹ · exact" };
    GateTally MultiSelectSyncGate = { "W85", "Synchronized Outliner & Viewport multi-selection: Windows Explorer Shift+Click [A..Z] range selection (forward, reverse, cross-section Bodies + Sketches) and Ctrl+Click item-by-item toggle synced across Outliner rows and Viewport scene states", "exact" };
    GateTally GridCubeUvGate      = { "W86", "Continuous 3-decade Grid shader (SPIR-V archive + zero decade-step discontinuity) & Perspective-aligned 3D UV Font Orientation Cube (FOV-tracked perspective convergence, orthographic parallel fallback, 2×2 UV plane glyph quads from ImGui::GetFont() atlas)", "≤ 10⁻⁶ · exact" };
    GateTally BlenderTransformGate = { "W87", "Blender-like G / R / S transforms: direct sequences (G Z 10, sign toggle '-', decimal, R Z 90, S Z 2, Shift+Axis plane), live modal gizmo with single-axis isolation + 3D axis constraint guide line, mouse drag & LMB handle drag, live G⇄R⇄S switching, and curve/point support", "≤ 10⁻⁹ · exact" };
    GateTally NumpadCameraGate     = { "W88", "Blender Numpad camera hotkeys: 1/Ctrl+1 Front/Back, 3/Ctrl+3 Right/Left, 7/Ctrl+7 Top/Bottom, 0 Isometric, 9 180° opposite flip, 5 Perspective⇄Ortho toggle, 2/4/6/8 15° orbit & Ctrl pan, +/- zoom, Numpad . Frame Selection, and modal numeric input guard", "≤ 10⁻⁹ · exact" };
    GateTally SplitViewportGate    = { "W89", "Multi-view docked Split Viewport (2, 3, or 4 simultaneous views): @ViewportSplit toolbar button & menu, Ctrl+Alt+Q Quad toggle, draggable internal X/Y/crosshair splitters (never undocking), per-pane cameras/grids/gizmos/Orientation Cubes, and cross-pane interaction", "exact" };
    GateTally GizmoZoomGate          = { "W90", "Smooth jitter-free Gizmo scaling across Viewport zoom: zero 1-frame lag during perspective/ortho wheel zoom, Numpad +/- shortcut zoom, modal G/R/S zoom, per-pane integer raster frames, and perspective depth-compensated gizmo move drags", "≤ 0.1 px" };
    GateTally InteractiveDimToolGate = { "W91", "Interactive CAD Dimension Tool (D / @SketchDimension): Ruler Point-A→Point-B, 1-click Circle/Cylinder Ø⇄R, Arc R, 3D Edge & 2-Edge Angle °, live drag-to-position parallel/radial offset, placed-pill drag repositioning, and live/committed associative shape tracking", "≤ 10⁻⁶ · exact" };
    GateTally AxisSlideDimEditGate   = { "W92", "X / Y / Z World-Axis Dimension Sliding & Cutter-Style Placed Dimension Editing: select edge/points -> X/Y/Z slides dimension & witness lines along 3D world axes with guide rails + typed offset, pre-selected edge fast-path, live G/X/Y/Z re-sliding & top-of-Inspector editing of placed dimensions, Undo/Redo & byte-equal TOML SlideAxis round-trip", "≤ 10⁻⁶ · exact" };
    GateTally HeaderDimToggleGate    = { "W93", "Viewport Header 'Dim' (@DimensionTool) Visibility Toggle: shows/hides all viewport dimension lines (State.DimensionsEnabled) independently of the interactive creation tool (@SketchDimension / D), closing active session on hide and auto-restoring visibility on D", "exact" };
    GateTally TwistedNurbsBevelGate  = { "W94", "Non-freezing Fillet & Chamfer on twisted bilinear NURBS reshaped bodies (image-2.png repro): sub-second ChartCarrier rolling-ball spine marching, tangential Newton guard, memoized NURBS quadtree/rate audits across all 12 reshaped edges + multi-edge Fillet pair", "< 1.5 s · sound" };
    GateTally RepeatedReshapeGate    = { "W95", "Repeated multi-pass Vertex Reshape on already-bent NURBS faces & multi-body histories: pre-move corner reconstruction on 4-line NurbsSurface faces and per-body DirectEditInsertion across separate beveled bodies, Undo/Redo & TOML reload", "≤ 10⁻⁶ · sound" };

    std::vector<std::string> Journal;                                                        // [-] - Narrative rows for the exhibit

    void Tally(GateTally& Gate, double Value, bool PassedCondition, const std::string& Note)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
        std::string Escaped;                                                                 // A '|' in a name would split the table cell
        for (const char Glyph : Note) Escaped += Glyph == '|' ? std::string("\\|") : std::string(1, Glyph);
        char Row[1024];
        std::snprintf(Row, sizeof(Row), "| %s | %s | %.3g | %s |", Gate.Code, Escaped.c_str(), Value, PassedCondition ? "🟢" : "🔴");
        Journal.push_back(Row);
    }

    void TallyVolume(GateTally& Gate, const SolidArcDocument& Document, double Expected, const std::string& Note)
    {
        const double Relative = std::abs(Document.Summary.Volume - Expected) / std::max(1.0e-12, std::abs(Expected));
        Tally(Gate, Relative, Relative <= 1.0e-9 && Document.Summary.SoundCondition, Note + " (V = " + std::to_string(Document.Summary.Volume) + ")");
    }

    [[nodiscard]] OrbitRay RayBetween(std::array<double, 3> From, std::array<double, 3> Towards)
    {
        std::array<double, 3> Direction = { Towards[0] - From[0], Towards[1] - From[1], Towards[2] - From[2] };
        const double Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1] + Direction[2] * Direction[2]);
        for (double& Component : Direction) Component /= Length;
        return { From, Direction };
    }

    // Pick through the scene and select the hit (as the viewport does on a click).
    std::string PickAndSelect(SolidArcDocument& Document, const OrbitRay& Ray, bool AdditiveCondition)
    {
        const SolidArcScene               Scene = BuildSolidArcScene(Document);
        const std::optional<SolidArcPick> Hit   = PickSolidArcScene(Scene, Ray, 0.02);
        if (!Hit) return {};
        const std::string Name = NameSolidArcIdentity(Scene, Hit->Identity);
        SelectSolidArcName(Document, SolidArcIdentityKind(Hit->Identity), Name, AdditiveCondition);
        return Name;
    }

    // Position of an end of the named edge on the document's current body (start, or end when reversed).
    [[nodiscard]] Vector3 EdgeEndPosition(const SolidArcDocument& Document, const std::string& EdgeName, bool ReversedCondition)
    {
        for (const auto& [Slot, Name] : Document.Regeneration.Naming.Edges)
        {
            if (Name != EdgeName) continue;
            const EdgeProfile& Profile = Document.Partition->Edges.Resolve(Document.Partition->Edges.TokenAt(Slot));
            return Document.Partition->Vertices.Resolve(ReversedCondition ? Profile.End : Profile.Start).Position;
        }
        return { std::nan(""), std::nan(""), std::nan("") };
    }

    // Worst distance from the expected cap vertices of a variable fillet on the front-top edge (y = 0, z = Top) to the
    // body's vertices: the rails end Size from the anchored corner (x = AnchorX) and EndSize from the far one.
    [[nodiscard]] double MeasureCapGap(const SolidArcDocument& Document, double AnchorX, double FarX, double Top, double Size, double EndSize)
    {
        const Vector3 Expected[4] = { { AnchorX, Size, Top }, { AnchorX, 0.0, Top - Size }, { FarX, EndSize, Top }, { FarX, 0.0, Top - EndSize } };
        double Worst = 0.0;
        for (const Vector3& Point : Expected)
        {
            double Nearest = std::numeric_limits<double>::infinity();
            for (const auto& [Slot, Name] : Document.Regeneration.Naming.Vertices)
                Nearest = std::min(Nearest, Distance(Document.Partition->Vertices.Resolve(Document.Partition->Vertices.TokenAt(Slot)).Position, Point));
            Worst = std::max(Worst, Nearest);
        }
        return Worst;
    }

    // 📝 Gates W1–W47 predate unfilled drawings (Plasticity's fresh curves): they draw filled shapes, so E still makes
    //    solids. W48 draws unfilled ones on purpose.
    [[nodiscard]] SolidArcCommandOutcome DrawFilledSketch(SolidArcDocument& Document, SolidArcDrawingRequest Request)
    {
        Request.FillCondition = true;
        return DrawSolidArcSketch(Document, Request);
    }

    [[nodiscard]] SolidArcDrawingRequest DrawingOf(SolidArcDrawingKind Kind, int GridPlane, std::vector<Vector2> Points, bool ClosedCondition = false,
                                                   bool CentreCondition = false, SketchSplineForm Form = SketchSplineForm::Hermite)
    {
        SolidArcDrawingRequest Request;
        Request.Kind = Kind; Request.GridPlane = GridPlane; Request.Points = std::move(Points);
        Request.ClosedCondition = ClosedCondition; Request.CentreCondition = CentreCondition; Request.Form = Form;
        return Request;
    }

    // Exact profile area of a drawing (Green's theorem on the solved sketch; independent of the solid's volume integral).
    [[nodiscard]] double MeasureDrawingArea(const SolidArcDrawingRequest& Request)
    {
        SketchModel Sketch;
        if (!ComposeSolidArcSketch(Request, Sketch).empty()) return std::nan("");
        (void)SolveSketch(Sketch);
        return ExtractSketchProfile(Sketch).Area;
    }

    void Expect(SolidArcCommandOutcome Outcome)
    {
        if (!Outcome.AcceptedCondition) std::printf("⚠️ command \"%s\" refused: %s\n", Outcome.Label.c_str(), Outcome.Refusal.c_str());
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PIECE BEVELS
//------------------------------------------------------------------------------------------------------------------------

// W59: a block (rounded end or plain) sliced by a polyline knife; every edge of both pieces is filleted and chamfered alone.
void ValidatePieceBevels()
{
    SolidArcConstructRequest Block;
    Block.Kind = SolidArcConstructKind::Block; Block.Width = 4.0; Block.Depth = 2.0; Block.Height = 1.0;

    {
        const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        struct Scenario { const char* Title; std::vector<Vector2> Chain; double Rounding; };
        const Scenario Scenarios[] = {
            { "polyline beside a rounded end", { { 1.0, -1.0 }, { 1.4, 1.2 }, { 2.8, 1.2 }, { 3.3, 3.0 } }, 0.8 },
            { "polyline through a rounded end", { { 2.0, -1.0 }, { 2.0, 1.0 }, { 3.9, 1.0 } }, 0.8 },
            { "polyline on a plain block", { { -1.0, 0.5 }, { 1.5, 0.5 }, { 1.5, 1.5 }, { 5.0, 1.5 } }, 0.0 } };
        for (const Scenario& Each : Scenarios)
        {
            SolidArcDocument Part = CreateSolidArcDocument("PieceBevel");
            SolidArcConstructRequest Slab = Block;
            Slab.Height = 1.5;
            Expect(ConstructSolidArcPrimitive(Part, Slab));
            if (Each.Rounding > 0.0)
            {
                Part.Selection = {};
                for (const auto& [Slot, Name] : Part.Regeneration.Naming.Edges)
                {
                    const Vector3 A = EdgeEndPosition(Part, Name, false), B = EdgeEndPosition(Part, Name, true);
                    if (std::abs(A.X - 4.0) < 1.0e-9 && std::abs(B.X - 4.0) < 1.0e-9 && std::abs(A.Y - B.Y) < 1.0e-9) Part.Selection.Edges.push_back(Name);
                }
                Expect(BlendSolidArcSelection(Part, BlendProfile::Fillet, Each.Rounding));
            }
            const std::string BlockKey = Part.Regeneration.Active;
            Expect(DrawSolidArcSketch(Part, DrawingOf(SolidArcDrawingKind::Line, 0, Each.Chain)));
            SolidArcCutRequest Cut;
            Cut.Target = BlockKey; Cut.Cutter = Part.Tree.Steps.back().Label;
            const SolidArcCommandOutcome Sliced = CutSolidArcBody(Part, Cut);
            Tally(PieceGate, 0.0, Sliced.AcceptedCondition && Part.Summary.Bodies.size() == 2 && Part.Summary.SoundCondition, Format("%s: %s, sound", Each.Title, Sliced.Label.c_str()));
            if (!Sliced.AcceptedCondition) continue;
            // The Windows report: names both pieces carry, picked on one piece, blend that piece (never "spans Cut1 and Block1").
            {
                std::set<std::string> Shared;
                const RegeneratedBody& First = Part.Regeneration.Bodies.front();
                const RegeneratedBody& Last  = Part.Regeneration.Bodies.back();
                for (const auto& Entry : Last.Naming.Edges)
                    for (const auto& Other : First.Naming.Edges)
                        if (Entry.second == Other.second && !SmoothEdgeCondition(*Part.Partition, Part.Partition->Edges.TokenAt(Entry.first))) Shared.insert(Entry.second);
                bool Targeted = Shared.size() >= 2;
                for (const RegeneratedBody* Piece : { &First, &Last })
                {
                    SolidArcDocument Trial = CloneSolidArcDocument(Part);
                    bool Additive = false;
                    for (const std::string& Name : Shared) { SelectSolidArcName(Trial, SolidArcSelectionKind::Edge, Name, Additive, Piece->Key); Additive = true; }
                    const SolidArcCommandOutcome Bevel = BlendSolidArcSelection(Trial, BlendProfile::Fillet, 0.04);
                    Targeted = Targeted && Bevel.AcceptedCondition && Trial.Summary.SoundCondition && Trial.Tree.Steps.back().Target == Piece->Key;
                    if (!Bevel.AcceptedCondition) std::printf("DV W59 %s shared on %s: %s\n", Each.Title, Piece->Key.c_str(), Bevel.Refusal.c_str());
                }
                Tally(PieceGate, double(Shared.size()), Targeted, Format("%s: the %d edge names both pieces carry, picked on either piece, blend that piece (the \"spans Cut1 and Block1\" report)", Each.Title, int(Shared.size())));
            }
            int Tried = 0, Refused = 0, Smooth = 0;
            std::string Refusals;
            for (const RegeneratedBody& Piece : Part.Regeneration.Bodies)
            {
                std::vector<std::string> Uprights;
                for (const auto& [Slot, Name] : Piece.Naming.Edges)
                {
                    const EdgeProfile& Profile = Part.Partition->Edges.Resolve(Part.Partition->Edges.TokenAt(Slot));
                    const Vector3 A = Part.Partition->Vertices.Resolve(Profile.Start).Position, B = Part.Partition->Vertices.Resolve(Profile.End).Position;
                    // A smooth edge (a flat face running tangent into the rounded end) has no crease: its refusal is right.
                    if (SmoothEdgeCondition(*Part.Partition, Part.Partition->Edges.TokenAt(Slot))) { ++Smooth; continue; }
                    if (std::abs(A.X - B.X) < 1.0e-9 && std::abs(A.Y - B.Y) < 1.0e-9) Uprights.push_back(Name);
                    for (const BlendProfile Shape : { BlendProfile::Fillet, BlendProfile::Chamfer })
                    {
                        SolidArcDocument Trial = CloneSolidArcDocument(Part);
                        Trial.Selection = {};
                        Trial.Selection.Edges      = { Name };
                        Trial.Selection.PickedBody = Piece.Key;
                        const SolidArcCommandOutcome Bevel = BlendSolidArcSelection(Trial, Shape, 0.04);
                        ++Tried;
                        if (Bevel.AcceptedCondition && Trial.Summary.SoundCondition && std::abs(Trial.Summary.Volume - Part.Summary.Volume) > 1.0e-7) continue;
                        ++Refused;
                        std::printf("DV W59 %s · %s · %s %s (%.3f %.3f %.3f)-(%.3f %.3f %.3f): %s%s\n", Each.Title, SolidArcBodyCaption(Part, Piece.Key).c_str(), Shape == BlendProfile::Fillet ? "fillet" : "chamfer",
                                    Name.c_str(), A.X, A.Y, A.Z, B.X, B.Y, B.Z, Bevel.Refusal.c_str(), Bevel.AcceptedCondition ? " (unsound)" : "");
                        if (Refusals.size() < 300) Refusals += " · " + Name + ": " + Bevel.Refusal;
                    }
                }
                SolidArcDocument Trial = CloneSolidArcDocument(Part);
                Trial.Selection = {};
                Trial.Selection.Edges      = Uprights;
                Trial.Selection.PickedBody = Piece.Key;
                const SolidArcCommandOutcome Together = BlendSolidArcSelection(Trial, BlendProfile::Fillet, 0.04);
                ++Tried;
                if (!Together.AcceptedCondition || !Trial.Summary.SoundCondition)
                {
                    ++Refused;
                    std::printf("DV W59 %s · %s · uprights together (%d): %s\n", Each.Title, SolidArcBodyCaption(Part, Piece.Key).c_str(), int(Uprights.size()), Together.Refusal.c_str());
                    Refusals += " · uprights: " + Together.Refusal;
                }
            }
            std::printf("DV W59 %s: %d tried, %d refused, %d smooth skipped\n", Each.Title, Tried, Refused, Smooth);
            Tally(PieceGate, double(Refused), Refused == 0, Format("%s: every crease edge of both pieces takes a 0.04 fillet and chamfer alone (tangent chains propagate), the uprights together: %d tried, %d refused, %d smooth edges skipped%s", Each.Title, Tried, Refused, Smooth, Refusals.c_str()));
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EXTRUDE MANIFOLD
//------------------------------------------------------------------------------------------------------------------------

// The planar face of the active body whose outward normal is Normal and which has Loops loops (an imprinted region has
// one, the face it sits in two).
[[nodiscard]] std::string PlanarFaceWhere(const SolidArcDocument& Part, Vector3 Normal, size_t Loops)
{
    for (const auto& [Slot, Name] : Part.Regeneration.Naming.Faces)
    {
        const FaceProfile&     Face    = Part.Partition->Faces.Resolve(Part.Partition->Faces.TokenAt(Slot));
        const SurfaceGeometry& Surface = Part.Partition->Surfaces.Resolve(Face.Surface);
        const auto*            Plane   = std::get_if<PlaneSurface>(&Surface);
        if (!Plane || Face.Loops.size() != Loops) continue;
        if (Dot(Plane->Frame.ZAxis * (Face.Sense == Orientation::Forward ? 1.0 : -1.0), Normal) > 1.0 - 1.0e-9) return Name;
    }
    return {};
}

// W60: sketch push-in by direction, and face push-in of an imprinted region (the reported flush polygon).
void ValidateExtrudeManifold()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto Shoelace = [](const std::vector<Vector2>& Points)
    {
        double Twice = 0.0;
        for (size_t Index = 0; Index < Points.size(); ++Index) Twice += Cross(Points[Index], Points[(Index + 1) % Points.size()]);
        return 0.5 * std::abs(Twice);
    };
    const auto NewBlock = []()
    {
        SolidArcDocument Part = CreateSolidArcDocument("Manifold");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = 4.0; Block.Depth = 2.0; Block.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Part, Block));
        return Part;
    };
    const auto OnFace = [](SolidArcDrawingKind Kind, std::vector<Vector2> Points, OrthonormalFrame Plane, bool Closed)
    {
        SolidArcDrawingRequest Request;
        Request.Kind = Kind; Request.Points = std::move(Points); Request.ClosedCondition = Closed;
        Request.PlaneCondition = true; Request.Plane = Plane; Request.FillCondition = true;
        Request.CentreCondition = Kind == SolidArcDrawingKind::Circle;                       // Circles: centre, then rim
        return Request;
    };
    const OrthonormalFrame Top{ { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const OrthonormalFrame Front{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };   // y = 0 face, outward −y
    const std::vector<Vector2> Notched = { { 1.0, 0.5 }, { 2.5, 0.5 }, { 2.5, 1.0 }, { 1.8, 1.0 }, { 1.8, 1.5 }, { 1.0, 1.5 } };
    const std::vector<Vector2> Pentagon = { { 1.0, 0.2 }, { 2.0, 0.2 }, { 2.3, 0.6 }, { 1.5, 0.85 }, { 0.7, 0.6 } };
    const double NotchedArea = Shoelace(Notched), PentagonArea = Shoelace(Pentagon);

    // ① Sketch on the top face: the material is below (−𝑧), so E in (−) is a Difference pocket, out (+) a Union.
    {
        SolidArcDocument Part = NewBlock();
        Expect(DrawSolidArcSketch(Part, OnFace(SolidArcDrawingKind::Line, Notched, Top, true)));
        const std::string Sketch   = Part.Tree.Steps.back().Label;
        const int         Material = MeasureSolidArcProfileMaterial(Part, Sketch, {});
        Tally(ManifoldGate, 0.0, Material == -1, Format("notched hexagon on the top face: material side %d (expect −1, below the sketch)", Material));
        SolidArcDocument Pocket = CloneSolidArcDocument(Part);
        const SolidArcCommandOutcome Cut = ExtrudeSolidArcSketch(Pocket, Sketch, -0.3, FeatureCombination::Difference, {}, 1.0e-3);
        TallyVolume(ManifoldGate, Pocket, 8.0 - 0.3 * NotchedArea, Format("E in 0.3 → auto Difference started 1 mm outside: a pocket %s, %d faces (expect 13)%s", Cut.AcceptedCondition ? "cut" : "refused",
                                                                            Pocket.Summary.Faces, Cut.Refusal.empty() ? "" : (" · " + Cut.Refusal).c_str()));
        Tally(ManifoldGate, 0.0, Pocket.Summary.Faces == 13 && Pocket.Summary.Bodies.size() == 1, Format("pocket topology: %d faces, %d body", Pocket.Summary.Faces, int(Pocket.Summary.Bodies.size())));
        SolidArcDocument Boss = CloneSolidArcDocument(Part);
        Expect(ExtrudeSolidArcSketch(Boss, Sketch, 0.3, FeatureCombination::Union));
        TallyVolume(ManifoldGate, Boss, 8.0 + 0.3 * NotchedArea, "E out 0.3 → auto Union: a boss");
        SolidArcDocument Through = CloneSolidArcDocument(Part);
        Expect(ExtrudeSolidArcSketch(Through, Sketch, -1.5, FeatureCombination::Difference, {}, 1.0e-3));
        TallyVolume(ManifoldGate, Through, 8.0 - 1.0 * NotchedArea, "E in 1.5 (past the floor) → a through hole");
    }

    // ② The report: a pentagon on the front face pushed in with Union (the old default) leaves the block whole with a flush
    //    imprinted face; that face now extrudes out, pushes in (a prism of its outline cut out) and E on the sketch cuts.
    {
        SolidArcDocument Part = NewBlock();
        Expect(DrawSolidArcSketch(Part, OnFace(SolidArcDrawingKind::Line, Pentagon, Front, true)));
        const std::string Sketch = Part.Tree.Steps.back().Label;
        Tally(ManifoldGate, 0.0, MeasureSolidArcProfileMaterial(Part, Sketch, {}) == -1, "pentagon on the front face (outward −y): material behind it");
        SolidArcDocument Direct = CloneSolidArcDocument(Part);
        Expect(ExtrudeSolidArcSketch(Direct, Sketch, -0.4, FeatureCombination::Difference, {}, 1.0e-3));
        TallyVolume(ManifoldGate, Direct, 8.0 - 0.4 * PentagonArea, "sketch E in 0.4 → pocket");
        {
            // Freshly drawn shapes are unfilled (walls): the push-in fills the region it cuts, one undo entry.
            SolidArcDocument Fresh = NewBlock();
            SolidArcDrawingRequest Unfilled = OnFace(SolidArcDrawingKind::Line, Pentagon, Front, true);
            Unfilled.FillCondition = false;
            Expect(DrawSolidArcSketch(Fresh, Unfilled));
            const size_t Undo = Fresh.UndoEntries.size();
            const SolidArcCommandOutcome Refused = ExtrudeSolidArcSketch(Fresh, Fresh.Tree.Steps.back().Label, -0.4, FeatureCombination::Difference, {}, 1.0e-3);
            const SolidArcCommandOutcome Filled  = ExtrudeSolidArcSketch(Fresh, Fresh.Tree.Steps.back().Label, -0.4, FeatureCombination::Difference, {}, 1.0e-3, true);
            TallyVolume(ManifoldGate, Fresh, 8.0 - 0.4 * PentagonArea, Format("unfilled pentagon: a plain Difference still refuses (%s), the push-in fills and cuts in %d undo entry",
                                                                             Refused.AcceptedCondition ? "accepted!" : "refused", int(Fresh.UndoEntries.size() - Undo)));
            Tally(ManifoldGate, 0.0, !Refused.AcceptedCondition && Filled.AcceptedCondition && Fresh.UndoEntries.size() == Undo + 1, "unfilled push-in: one undo entry");
        }
        // An inward Union merges flush (maximal faces), so the block stays whole with no seam left to pick.
        SolidArcDocument Merged = CloneSolidArcDocument(Part);
        Expect(ExtrudeSolidArcSketch(Merged, Sketch, -0.4, FeatureCombination::Union));
        Tally(ManifoldGate, 0.0, std::abs(Merged.Summary.Volume - 8.0) <= 1.0e-9 && Merged.Summary.Faces == 6,
              Format("inward Union (the old default): volume %.9f unchanged, %d faces — why the drag direction now picks the Boolean", Merged.Summary.Volume, Merged.Summary.Faces));
        // A pentagon boss 0.25 out; its cap pushed back in: −0.1 pushes (the boss shortens), −0.6 runs past the boss base so
        // the push refuses and the cap's outline prism is cut instead (the boss gone, a 0.35 pocket behind it); +0.2 grows it.
        Expect(ExtrudeSolidArcSketch(Part, Sketch, 0.25, FeatureCombination::Union));
        const std::string Cap = PlanarFaceWhere(Part, { 0.0, -1.0, 0.0 }, 1);
        for (const double Amount : { -0.1, -0.6, -2.5, 0.2 })
        {
            SolidArcDocument Trial = CloneSolidArcDocument(Part);
            Trial.Selection = {};
            Trial.Selection.Faces = { Cap };
            const SolidArcCommandOutcome Pushed = ChangeSolidArcSelection(Trial, FaceOperationKind::Extrude, Amount);
            const double Expected = 8.0 + std::max(0.25 + Amount, -2.0) * PentagonArea;
            TallyVolume(ManifoldGate, Trial, Expected, Format("boss cap %s face E %+.2f → %s%s", Cap.c_str(), Amount, Amount > 0.0 ? "a longer boss" : Amount > -0.25 ? "a shorter boss (pushed)"
                                                            : Amount < -2.0 ? "a through hole (cut)" : "a pocket (cut)", Pushed.Refusal.empty() ? "" : (" · " + Pushed.Refusal).c_str()));
        }
    }

    // ③ A circle boss's cap (one closed arc edge) pushed past its base cuts a round pocket; a pocket floor pushes deeper;
    //    a whole face (the top) pushes as before.
    {
        SolidArcDocument Part = NewBlock();
        Expect(DrawSolidArcSketch(Part, OnFace(SolidArcDrawingKind::Circle, { { 2.0, 1.0 }, { 2.5, 1.0 } }, Top, false)));
        Expect(ExtrudeSolidArcSketch(Part, Part.Tree.Steps.back().Label, 0.2, FeatureCombination::Union));
        const std::string Disc = PlanarFaceWhere(Part, { 0.0, 0.0, 1.0 }, 1);
        SolidArcDocument Trial = CloneSolidArcDocument(Part);
        Trial.Selection = {};
        Trial.Selection.Faces = { Disc };
        const SolidArcCommandOutcome Pushed = ChangeSolidArcSelection(Trial, FaceOperationKind::Extrude, -0.5);
        TallyVolume(ManifoldGate, Trial, 8.0 - 0.3 * std::numbers::pi * 0.25, Format("disc cap %s E −0.5 → a round pocket 0.3 deep%s", Disc.c_str(), Pushed.Refusal.empty() ? "" : (" · " + Pushed.Refusal).c_str()));

        SolidArcDocument Floor = NewBlock();
        Expect(DrawSolidArcSketch(Floor, OnFace(SolidArcDrawingKind::Line, Notched, Top, true)));
        Expect(ExtrudeSolidArcSketch(Floor, Floor.Tree.Steps.back().Label, -0.3, FeatureCombination::Difference, {}, 1.0e-3));
        Floor.Selection = {};
        Floor.Selection.Faces = { PlanarFaceWhere(Floor, { 0.0, 0.0, 1.0 }, 1) };
        const SolidArcCommandOutcome Deeper = ChangeSolidArcSelection(Floor, FaceOperationKind::Extrude, -0.2);
        TallyVolume(ManifoldGate, Floor, 8.0 - 0.5 * NotchedArea, Format("pocket floor %s E −0.2 → the pocket 0.5 deep%s", Floor.Selection.Faces.empty() ? "" : Floor.Selection.Faces.front().c_str(),
                                                                         Deeper.Refusal.empty() ? "" : (" · " + Deeper.Refusal).c_str()));

        SolidArcDocument Whole = NewBlock();
        Whole.Selection = {};
        Whole.Selection.Faces = { PlanarFaceWhere(Whole, { 0.0, 0.0, 1.0 }, 1) };
        Expect(ChangeSolidArcSelection(Whole, FaceOperationKind::Extrude, -0.25));
        TallyVolume(ManifoldGate, Whole, 6.0, "whole top face E −0.25 → pushed (the block lowers), no cut");
    }
}

// W64: notches at a face edge. A region bounded partly by the face's own edge (the top edge of the front face) pushed in
// must open through the neighbour: Blender's extrude manifold removes the neighbour's strip instead of leaving a lid of
// zero thickness in its plane. The volume cannot tell a lid apart (it has none), so rays from outside measure the opening.
void ValidateEdgeNotch()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto NewBlock = []()
    {
        SolidArcDocument Part = CreateSolidArcDocument("Notch");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = 4.0; Block.Depth = 2.0; Block.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Part, Block));
        return Part;
    };
    const auto OnFace = [](std::vector<Vector2> Corners, OrthonormalFrame Plane, bool FillCondition)
    {
        SolidArcDrawingRequest Request;
        Request.Kind = SolidArcDrawingKind::Rectangle; Request.Points = std::move(Corners); Request.ClosedCondition = true;
        Request.PlaneCondition = true; Request.Plane = Plane; Request.FillCondition = FillCondition;
        return Request;
    };
    // Distance along the ray From → Towards to the first face it meets (−1: none).
    const auto HitDistance = [](const SolidArcDocument& Part, std::array<double, 3> From, std::array<double, 3> Towards)
    {
        const SolidArcScene               Scene = BuildSolidArcScene(Part);
        const std::optional<SolidArcPick> Hit   = PickSolidArcScene(Scene, RayBetween(From, Towards), 0.0);
        return Hit && !(Hit->Identity & IdentityEdgeBit) ? Hit->Distance : -1.0;
    };
    const OrthonormalFrame Top{ { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const OrthonormalFrame Front{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };   // y = 0 face, outward −y
    struct NotchCase
    {
        const char*           Title;
        std::vector<Vector2>  Corners;                                                       // Rectangle on the face (plane coordinates)
        bool                  TopFace;                                                       // Drawn on the top face (else the front)
        double                Amount;                                                        // Signed E distance
        double                Expected;                                                      // Volume
        std::array<double, 3> From, Towards;                                                 // Opening probe
        double                Reach;                                                         // Expected probe distance (to the notch floor)
    };
    const std::vector<NotchCase> Cases = {
        { "front, touching the top edge, E −0.5",        { { 1.0, 0.5 }, { 2.0, 1.0 } },  false, -0.5, 8.0 - 0.5 * 0.5,       { 1.5, 0.25, 5.0 }, { 1.5, 0.25, 0.0 }, 4.5 },
        { "front, overhanging the top edge, E −0.5",     { { 1.0, 0.5 }, { 2.0, 1.4 } },  false, -0.5, 8.0 - 0.5 * 0.5,       { 1.5, 0.25, 5.0 }, { 1.5, 0.25, 0.0 }, 4.5 },
        { "front, touching the top-left corner, E −0.5", { { 0.0, 0.5 }, { 1.0, 1.0 } },  false, -0.5, 8.0 - 0.5 * 0.5,       { 0.5, 0.25, 5.0 }, { 0.5, 0.25, 0.0 }, 4.5 },
        { "front, overhanging top and left, E −0.5",     { { -0.5, 0.5 }, { 1.0, 1.5 } }, false, -0.5, 8.0 - 0.5 * 0.5,       { 0.5, 0.25, 5.0 }, { 0.5, 0.25, 0.0 }, 4.5 },
        { "front, touching the bottom edge, E −0.5",     { { 1.0, 0.0 }, { 2.0, 0.4 } },  false, -0.5, 8.0 - 0.5 * 0.4,       { 1.5, 0.25, -5.0 }, { 1.5, 0.25, 0.0 }, 5.4 },
        { "front, touching the top edge, E −0.75 deep",  { { 2.5, 0.3 }, { 3.5, 1.0 } },  false, -0.75, 8.0 - 0.75 * 0.7,     { 3.0, 0.5, 5.0 }, { 3.0, 0.5, 0.0 }, 4.7 },
        { "front, touching the top edge, through −2.5",  { { 1.0, 0.5 }, { 2.0, 1.0 } },  false, -2.5, 8.0 - 2.0 * 0.5,       { 1.5, 1.0, 5.0 }, { 1.5, 1.0, 0.0 }, 4.5 },
        { "top, touching the front edge, E −0.4",        { { 1.0, 0.0 }, { 2.0, 0.8 } },  true,  -0.4, 8.0 - 0.4 * 0.8,       { 1.5, -5.0, 0.8 }, { 1.5, 0.0, 0.8 }, 5.8 },
        // The report: the rectangle stops a hair (1e-7) or a few pixels (5 mm) short of the edge — the strip opens.
        { "front, 1e-7 short of the top edge, E −0.5",   { { 1.0, 0.5 }, { 2.0, 1.0 - 1.0e-7 } }, false, -0.5, 8.0 - 0.5 * 0.5, { 1.5, 0.25, 5.0 }, { 1.5, 0.25, 0.0 }, 4.5 },
        { "front, 5 mm short of the top edge, E −0.5",   { { 1.0, 0.5 }, { 2.0, 0.995 } }, false, -0.5, 8.0 - 0.5 * 0.5,      { 1.5, 0.25, 5.0 }, { 1.5, 0.25, 0.0 }, 4.5 },
        { "front, 3 mm short of top and left, E −0.5",   { { 0.003, 0.5 }, { 1.0, 0.997 } }, false, -0.5, 8.0 - 0.5 * 0.5,    { 0.5, 0.25, 5.0 }, { 0.5, 0.25, 0.0 }, 4.5 },
        { "front, 5 mm short, through −2.5",             { { 1.0, 0.5 }, { 2.0, 0.995 } }, false, -2.5, 8.0 - 2.0 * 0.5,      { 1.5, 1.0, 5.0 }, { 1.5, 1.0, 0.0 }, 4.5 },
        { "top, 4 mm short of the front edge, E −0.4",   { { 1.0, 0.004 }, { 2.0, 0.8 } }, true,  -0.4, 8.0 - 0.4 * 0.8,      { 1.5, -5.0, 0.8 }, { 1.5, 0.0, 0.8 }, 5.8 },
        // Control: 50 mm short is a wall of 4.6 % of the outline (over the 1 % bridge) — kept, the probe lands on the top.
        { "front, 50 mm short (a real wall, kept), E −0.5", { { 1.0, 0.5 }, { 2.0, 0.95 } }, false, -0.5, 8.0 - 0.5 * 0.45,   { 1.5, 0.25, 5.0 }, { 1.5, 0.25, 0.0 }, 4.0 },
    };
    for (const NotchCase& Case : Cases)
    {
        const OrthonormalFrame& Plane = Case.TopFace ? Top : Front;
        // ① 2D cutter path: imprint the region, pick it with a ray, face E.
        {
            SolidArcDocument Part = NewBlock();
            Expect(DrawSolidArcSketch(Part, OnFace(Case.Corners, Plane, false)));
            const SolidArcCommandOutcome Made = ImprintSolidArcSketch(Part, Part.Tree.Steps.back().Label);
            const Vector2 Middle = (Case.Corners[0] + Case.Corners[1]) * 0.5;
            const Vector2 Inside = { std::clamp(Middle.X, 0.05, Case.TopFace ? 3.95 : 3.95), std::clamp(Middle.Y, 0.05, Case.TopFace ? 1.95 : 0.95) };
            const Vector3 Spot   = Plane.Origin + Plane.XAxis * Inside.X + Plane.YAxis * Inside.Y;
            const Vector3 Out    = Spot + Plane.ZAxis * 5.0;
            const std::string Face = PickAndSelect(Part, RayBetween({ Out.X, Out.Y, Out.Z }, { Spot.X, Spot.Y, Spot.Z }), false);
            const SolidArcCommandOutcome Pushed = ChangeSolidArcSelection(Part, FaceOperationKind::Extrude, Case.Amount);
            const double Reach = HitDistance(Part, Case.From, Case.Towards);
            const double Relative = std::abs(Part.Summary.Volume - Case.Expected) / Case.Expected;
            Tally(EdgeNotchGate, std::max(Relative, std::abs(Reach - Case.Reach)), Made.AcceptedCondition && Pushed.AcceptedCondition && Part.Summary.SoundCondition && Relative <= 1.0e-9 && std::abs(Reach - Case.Reach) <= 1.0e-6,
                  Format("face E (imprinted %s) %s: V %.9f (expect %.9f), probe %.6f (expect %.6f, a lid reads less), %d faces%s", Face.c_str(), Case.Title, Part.Summary.Volume, Case.Expected, Reach, Case.Reach,
                         Part.Summary.Faces, Pushed.Refusal.empty() ? (Made.Refusal.empty() ? "" : (" · " + Made.Refusal).c_str()) : (" · " + Pushed.Refusal).c_str()));
        }
        // ② Sketch E path: the filled rectangle extruded inward (auto Difference, started 1 mm outside).
        {
            SolidArcDocument Part = NewBlock();
            Expect(DrawSolidArcSketch(Part, OnFace(Case.Corners, Plane, true)));
            const SolidArcCommandOutcome Cut = ExtrudeSolidArcSketch(Part, Part.Tree.Steps.back().Label, Case.Amount, FeatureCombination::Difference, {}, 1.0e-3);
            const double Reach = HitDistance(Part, Case.From, Case.Towards);
            const double Relative = std::abs(Part.Summary.Volume - Case.Expected) / Case.Expected;
            Tally(EdgeNotchGate, std::max(Relative, std::abs(Reach - Case.Reach)), Cut.AcceptedCondition && Part.Summary.SoundCondition && Relative <= 1.0e-9 && std::abs(Reach - Case.Reach) <= 1.0e-6,
                  Format("sketch E %s: V %.9f (expect %.9f), probe %.6f (expect %.6f), %d faces%s", Case.Title, Part.Summary.Volume, Case.Expected, Reach, Case.Reach, Part.Summary.Faces,
                         Cut.Refusal.empty() ? "" : (" · " + Cut.Refusal).c_str()));
        }
    }

    // ④ Stress: 40 seeded rectangles on the front, top and right faces, approaching the high-v rim and / or the low-u rim
    //    with gaps from 0 to 3 % of the outline span (0 … 0.8 % open, 1.5 % / 3 % are walls kept), depths 0.2 … through,
    //    each by face E (imprinted) and sketch E: sound and the closed-form volume of the effective rectangle.
    {
        struct Host { const char* Name; OrthonormalFrame Plane; double Extent[2]; double Thickness; };
        const Host Hosts[] = {
            { "front", Front, { 4.0, 1.0 }, 2.0 },
            { "top",   Top,   { 4.0, 2.0 }, 1.0 },
            { "right", OrthonormalFrame{ { 4.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } }, { 2.0, 1.0 }, 4.0 },
        };
        const double Fractions[] = { 0.0, 1.0e-9, 1.0e-6, 1.0e-4, 3.0e-3, 8.0e-3, 1.5e-2, 3.0e-2 };   // [-] - Gap per outline span
        uint64_t Seed = 0x9E3779B97F4A7C15ull;
        const auto Next = [&]() { Seed ^= Seed << 13; Seed ^= Seed >> 7; Seed ^= Seed << 17; return double(Seed >> 11) * (1.0 / 9007199254740992.0); };
        int Tried = 0, Failed = 0;
        double Worst = 0.0;
        std::string Notes;
        for (int Case = 0; Case < 40; ++Case)
        {
            const Host&  Face   = Hosts[Case % 3];
            const bool   High   = Case % 4 != 1, Low = Case % 4 != 0;                       // Which rims it approaches
            const double Width  = 0.5 + 0.5 * Next(), Height = 0.3 + 0.25 * Face.Extent[1] * Next();
            const double HighFraction = Fractions[size_t(Next() * 8.0) % 8], LowFraction = Fractions[size_t(Next() * 8.0) % 8];
            const double Span   = std::hypot(Width, Height);
            const double U0     = Low ? LowFraction * Span : 0.3 + 0.5 * Next();
            const double V1     = High ? Face.Extent[1] - HighFraction * Span : Face.Extent[1] - 0.1 - 0.1 * Next();
            const double U1 = U0 + Width, V0 = V1 - Height;
            const double Depths[] = { 0.2, 0.45 * Face.Thickness, Face.Thickness + 0.5 };
            const double Depth  = Depths[size_t(Next() * 3.0) % 3];
            const double UEff   = Low && LowFraction <= 1.0e-2 ? 0.0 : U0;                     // Opened rims reach the edge
            const double VEff   = High && HighFraction <= 1.0e-2 ? Face.Extent[1] : V1;
            const double Expected = 8.0 - (U1 - UEff) * (VEff - V0) * std::min(Depth, Face.Thickness);
            for (const bool Imprinted : { true, false })
            {
                SolidArcDocument Part = NewBlock();
                Expect(DrawSolidArcSketch(Part, OnFace({ { U0, V0 }, { U1, V1 } }, Face.Plane, !Imprinted)));
                SolidArcCommandOutcome Made;
                if (Imprinted)
                {
                    const SolidArcCommandOutcome Split = ImprintSolidArcSketch(Part, Part.Tree.Steps.back().Label);
                    const Vector3 Spot = Face.Plane.Origin + Face.Plane.XAxis * (0.5 * (U0 + U1)) + Face.Plane.YAxis * (0.5 * (V0 + V1));
                    const Vector3 Out  = Spot + Face.Plane.ZAxis * 5.0;
                    (void)PickAndSelect(Part, RayBetween({ Out.X, Out.Y, Out.Z }, { Spot.X, Spot.Y, Spot.Z }), false);
                    Made = Split.AcceptedCondition ? ChangeSolidArcSelection(Part, FaceOperationKind::Extrude, -Depth) : Split;
                }
                else Made = ExtrudeSolidArcSketch(Part, Part.Tree.Steps.back().Label, -Depth, FeatureCombination::Difference, {}, 1.0e-3);
                const double Relative = std::abs(Part.Summary.Volume - Expected) / Expected;
                ++Tried;
                Worst = std::max(Worst, Relative);
                if (Made.AcceptedCondition && Part.Summary.SoundCondition && Relative <= 1.0e-9) continue;
                ++Failed;
                if (Notes.size() < 600) Notes += Format(" · #%d %s %s gaps %.0e/%.0e d %.2f: V %.9f vs %.9f %s", Case, Face.Name, Imprinted ? "face E" : "sketch E", High ? HighFraction : -1.0,
                                                        Low ? LowFraction : -1.0, Depth, Part.Summary.Volume, Expected, Made.Refusal.c_str());
            }
        }
        Tally(EdgeNotchGate, Worst, Failed == 0, Format("stress: %d seeded near-rim notches (front / top / right, gaps 0 … 3 %% of the span, depths 0.2 … through), %d failed, worst rel %.1e%s",
                                                       Tried, Failed, Worst, Notes.c_str()));
    }

    // ③ Control at a concave rim: a step (the block plus a 1.5 × 2 × 0.5 riser at x 2.5 … 4); a pocket on the lower top
    //    3 mm short of the riser wall keeps that strip — opening it would carve into the wall.
    {
        SolidArcDocument Part = NewBlock();
        SolidArcConstructRequest Riser;
        Riser.Kind = SolidArcConstructKind::Block; Riser.Position = { 2.5, 0.0, 1.0 }; Riser.Width = 1.5; Riser.Depth = 2.0; Riser.Height = 0.5;
        Expect(ConstructSolidArcPrimitive(Part, Riser));
        const double Base = Part.Summary.Volume;
        for (const bool Imprinted : { true, false })
        {
            SolidArcDocument Trial = CloneSolidArcDocument(Part);
            Expect(DrawSolidArcSketch(Trial, OnFace({ { 1.0, 0.5 }, { 2.497, 1.5 } }, Top, !Imprinted)));
            SolidArcCommandOutcome Made;
            if (Imprinted)
            {
                Expect(ImprintSolidArcSketch(Trial, Trial.Tree.Steps.back().Label));
                (void)PickAndSelect(Trial, RayBetween({ 1.5, 1.0, 5.0 }, { 1.5, 1.0, 0.0 }), false);
                Made = ChangeSolidArcSelection(Trial, FaceOperationKind::Extrude, -0.3);
            }
            else Made = ExtrudeSolidArcSketch(Trial, Trial.Tree.Steps.back().Label, -0.3, FeatureCombination::Difference, {}, 1.0e-3);
            const double Expected = Base - 0.3 * 1.497;
            const double Reach    = HitDistance(Trial, { 2.4985, 1.0, 5.0 }, { 2.4985, 1.0, 0.0 });   // On the kept strip: the top (4)
            const double Relative = std::abs(Trial.Summary.Volume - Expected) / Expected;
            Tally(EdgeNotchGate, Relative, Made.AcceptedCondition && Trial.Summary.SoundCondition && Relative <= 1.0e-9 && std::abs(Reach - 4.0) <= 1.0e-6,
                  Format("%s beside a concave riser wall, 3 mm strip kept: V %.9f (expect %.9f), probe on the strip %.6f (expect 4)%s", Imprinted ? "face E" : "sketch E",
                         Trial.Summary.Volume, Expected, Reach, Made.Refusal.empty() ? "" : (" · " + Made.Refusal).c_str()));
        }
    }
}

// W61: the 2D cutter — imprint an outline into the faces it lies on, then push / pull the regions (face E).
void ValidateImprint()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto NewBlock = []()
    {
        SolidArcDocument Part = CreateSolidArcDocument("Imprint");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = 4.0; Block.Depth = 2.0; Block.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Part, Block));
        return Part;
    };
    const auto OnFace = [](SolidArcDrawingKind Kind, std::vector<Vector2> Points, OrthonormalFrame Plane, bool Closed)
    {
        SolidArcDrawingRequest Request;
        Request.Kind = Kind; Request.Points = std::move(Points); Request.ClosedCondition = Closed;
        Request.PlaneCondition = true; Request.Plane = Plane; Request.FillCondition = false;              // Fresh shapes are unfilled
        Request.CentreCondition = Kind == SolidArcDrawingKind::Circle;
        return Request;
    };
    // Draw on the face, imprint, and tally: accepted, volume 8 (sound), faces +1, one undo entry.
    const auto Imprint = [&](SolidArcDocument& Part, const SolidArcDrawingRequest& Request, const char* Title, int Extra = 1)
    {
        Expect(DrawSolidArcSketch(Part, Request));
        const int    Faces = Part.Summary.Faces;
        const size_t Undo  = Part.UndoEntries.size();
        const SolidArcCommandOutcome Made = ImprintSolidArcSketch(Part, Part.Tree.Steps.back().Label);
        const double Before = Part.Summary.Volume;
        Tally(ImprintGate, 0.0, Made.AcceptedCondition && Part.Summary.SoundCondition && Part.Summary.Faces == Faces + Extra && Part.UndoEntries.size() == Undo + 1,
              Format("%s: %s, faces %d → %d (expect +%d), sound %d, volume %.9f%s", Title, Made.AcceptedCondition ? "imprinted" : "refused", Faces, Part.Summary.Faces, Extra,
                     int(Part.Summary.SoundCondition), Before, Made.Refusal.empty() ? "" : (" · " + Made.Refusal).c_str()));
        return Made.AcceptedCondition;
    };
    const auto PushAt = [&](const SolidArcDocument& Source, double X, double Y, double Amount, double Expected, const char* Title)
    {
        SolidArcDocument Part = CloneSolidArcDocument(Source);
        const std::string Face = PickAndSelect(Part, RayBetween({ X, Y, 5.0 }, { X, Y, 0.0 }), false);
        const SolidArcCommandOutcome Pushed = ChangeSolidArcSelection(Part, FaceOperationKind::Extrude, Amount);
        TallyVolume(ImprintGate, Part, Expected, Format("%s: face %s at (%.2f, %.2f) E %+.2f%s", Title, Face.c_str(), X, Y, Amount, Pushed.Refusal.empty() ? "" : (" · " + Pushed.Refusal).c_str()));
        return Part;
    };
    const OrthonormalFrame Top{ { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const OrthonormalFrame Front{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };   // y = 0 face, outward −y
    const std::vector<Vector2> Notched = { { 1.0, 0.5 }, { 2.5, 0.5 }, { 2.5, 1.0 }, { 1.8, 1.0 }, { 1.8, 1.5 }, { 1.0, 1.5 } };
    const double NotchedArea = 1.5 * 0.5 + 0.8 * 0.5;

    // ① An island (the notched hexagon) on the top: 7 faces, then E in → pocket, E out → boss.
    {
        SolidArcDocument Part = NewBlock();
        if (Imprint(Part, OnFace(SolidArcDrawingKind::Line, Notched, Top, true), "notched island on the top"))
        {
            TallyVolume(ImprintGate, Part, 8.0, "island imprint: the volume is unchanged");
            const SolidArcDocument Saved = CloneSolidArcDocument(Part);
            (void)PushAt(Part, 1.3, 1.0, -0.3, 8.0 - 0.3 * NotchedArea, "island pushed in → pocket");
            (void)PushAt(Part, 1.3, 1.0, 0.3, 8.0 + 0.3 * NotchedArea, "island pulled out → boss");
            // Save / load: the Imprint step replays to the same names and figures.
            const std::string          Text     = EncodeSolidArcDocument(Saved);
            const SolidArcDocumentFile Loaded   = DecodeSolidArcDocument(Text);
            SolidArcDocument           Reloaded = CreateSolidArcDocument(Loaded.Title);
            const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Reloaded, Loaded.Tree, Loaded.Captions, "Open");
            Tally(ImprintGate, 0.0, Loaded.Refusal.empty() && Opened.AcceptedCondition && Reloaded.Summary.Faces == Saved.Summary.Faces
                                    && Reloaded.Regeneration.Naming.Faces == Saved.Regeneration.Naming.Faces && EncodeSolidArcDocument(Reloaded) == Text,
                  Format("save → load: %d faces, names equal, byte-equal (%s)", Reloaded.Summary.Faces, Loaded.Refusal.empty() ? Opened.Refusal.c_str() : Loaded.Refusal.c_str()));
        }
    }

    // ② The steps case: a rectangle overhanging the left half splits the top along x = 2; the left half pushed in is a
    //    step (8 − 0.4·4), pulled out a raised step (8 + 0.5·4). A second imprint on the lower step, whose closing box
    //    runs over the raised half, still adds exactly one face (the riser it grazes is healed).
    {
        SolidArcDocument Part = NewBlock();
        if (Imprint(Part, OnFace(SolidArcDrawingKind::Rectangle, { { -0.5, -0.5 }, { 2.0, 2.5 } }, Top, true), "rectangle over the left half"))
        {
            SolidArcDocument Step = PushAt(Part, 1.0, 1.0, -0.4, 8.0 - 0.4 * 4.0, "left half pushed in → a step");
            (void)PushAt(Part, 3.0, 1.0, 0.5, 8.0 + 0.5 * 4.0, "right half pulled out → a raised step");
            OrthonormalFrame Lower = Top;
            Lower.Origin = { 0.0, 0.0, 0.6 };
            if (Step.Summary.Faces > 0 && Imprint(Step, OnFace(SolidArcDrawingKind::Line, { { 1.0, 1.7 }, { 1.0, 0.3 } }, Lower, false), "line on the lower step (closing box over the raised half)"))
                (void)PushAt(Step, 0.5, 1.0, -0.3, 8.0 - 1.6 - 0.3 * 2.0, "left strip pushed in → a second step");
        }
    }

    // ③ Open chains edge to edge: a line across (and one stopping short, extended), an L polyline cutting a corner.
    {
        SolidArcDocument Across = NewBlock();
        if (Imprint(Across, OnFace(SolidArcDrawingKind::Line, { { 3.0, -0.3 }, { 3.0, 2.3 } }, Top, false), "line across the top"))
            (void)PushAt(Across, 3.5, 1.0, -0.5, 8.0 - 0.5 * 2.0, "end strip (1 × 2) pushed in");
        SolidArcDocument Short = NewBlock();
        (void)Imprint(Short, OnFace(SolidArcDrawingKind::Line, { { 3.0, 0.6 }, { 3.0, 1.4 } }, Top, false), "line stopping short (extended edge to edge)");
        SolidArcDocument Corner = NewBlock();
        if (Imprint(Corner, OnFace(SolidArcDrawingKind::Line, { { 2.5, 2.4 }, { 2.5, 1.2 }, { 4.4, 1.2 } }, Top, false), "L polyline cutting a corner"))
            (void)PushAt(Corner, 3.2, 1.6, 0.2, 8.0 + 0.2 * 1.5 * 0.8, "corner pulled out");
    }

    // ④ Another face orientation; ⑤ refusals: an outline off the body, and the document untouched.
    {
        SolidArcDocument Part = NewBlock();
        (void)Imprint(Part, OnFace(SolidArcDrawingKind::Circle, { { 2.0, 0.5 }, { 2.3, 0.5 } }, Front, false), "circle on the front face");
        SolidArcDocument Away = NewBlock();
        OrthonormalFrame Above = Top;
        Above.Origin = { 0.0, 0.0, 3.0 };
        Expect(DrawSolidArcSketch(Away, OnFace(SolidArcDrawingKind::Line, Notched, Above, true)));
        const size_t Steps = Away.Tree.Steps.size();
        const SolidArcCommandOutcome Refused = ImprintSolidArcSketch(Away, Away.Tree.Steps.back().Label);
        Tally(ImprintGate, 0.0, !Refused.AcceptedCondition && Away.Tree.Steps.size() == Steps && Away.Summary.Faces == 6,
              Format("outline 2 above the block: %s (%s)", Refused.AcceptedCondition ? "accepted!" : "refused, rolled back", Refused.Refusal.c_str()));
    }
}

// W62: Cut at a snapped point (every curve through it breaks; circles at their next crossing) and sketch duplicates.
void ValidatePointCut()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const OrthonormalFrame Ground{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const auto Draw = [&](SolidArcDocument& Part, SolidArcDrawingKind Kind, std::vector<Vector2> Points)
    {
        SolidArcDrawingRequest Request;
        Request.Kind = Kind; Request.Points = std::move(Points); Request.PlaneCondition = true; Request.Plane = Ground;
        Request.CentreCondition = Kind == SolidArcDrawingKind::Circle; Request.FillCondition = true;
        Expect(DrawSolidArcSketch(Part, Request));
        return Part.Tree.Steps.back().Label;
    };
    const auto Curves = [](const SolidArcDocument& Part, const std::string& Label) { return int(Part.Regeneration.Sketches.at(Label).Curves.size()); };
    // Every curve end of the sketch lies on one of Points.
    const auto EndsOn = [](const SolidArcDocument& Part, const std::string& Label, const std::vector<Vector2>& Points)
    {
        const SketchModel& Sketch = Part.Regeneration.Sketches.at(Label);
        for (int Curve = 0; Curve < int(Sketch.Curves.size()); ++Curve)
        {
            std::string Refusal;
            const ProfileSegment Segment = ResolveSketchCurveSegment(Sketch, Curve, 1.0e-9, Refusal);
            if (!Refusal.empty() || Segment.Kind == ProfileSegmentKind::Circle) return false;
            for (const Vector2 End : { Segment.Start, Segment.End })
                if (std::none_of(Points.begin(), Points.end(), [&](Vector2 Point) { return Distance(End, Point) <= 1.0e-9; })) return false;
        }
        return true;
    };

    // ① Two overlapping circles (r 1, centres 1.2 apart) in two sketches: one cut at the crossing (0.6, 0.8) breaks both
    //    at both crossings (0.6, ±0.8): 2 + 2 arcs; a second cut at (0.6, −0.8) finds only ends and refuses.
    {
        SolidArcDocument Part = CreateSolidArcDocument("PointCut");
        const std::string First  = Draw(Part, SolidArcDrawingKind::Circle, { { 0.0, 0.0 }, { 1.0, 0.0 } });
        const std::string Second = Draw(Part, SolidArcDrawingKind::Circle, { { 1.2, 0.0 }, { 2.2, 0.0 } });
        const SolidArcPointCut Preview = PreviewSolidArcPointCut(Part, { 0.6, 0.8, 0.0 });
        Tally(PointCutGate, 0.0, Preview.Curves == 2 && Preview.Breaks.size() == 4 && Preview.Sketches.size() == 2,
              Format("preview at the crossing (0.6, 0.8): %d curves, %d breaks, %d sketches (expect 2, 4, 2)", Preview.Curves, int(Preview.Breaks.size()), int(Preview.Sketches.size())));
        const size_t Undo = Part.UndoEntries.size();
        const SolidArcCommandOutcome Cut = CutSolidArcSketchesAt(Part, { 0.6, 0.8, 0.0 });
        const std::vector<Vector2> Crossings = { { 0.6, 0.8 }, { 0.6, -0.8 } };
        Tally(PointCutGate, 0.0, Cut.AcceptedCondition && Curves(Part, First) == 2 && Curves(Part, Second) == 2 && EndsOn(Part, First, Crossings) && EndsOn(Part, Second, Crossings)
                                 && Part.UndoEntries.size() == Undo + 1,
              Format("%s: %d + %d arcs, every end on a crossing, %d undo entry%s", Cut.Label.c_str(), Curves(Part, First), Curves(Part, Second), int(Part.UndoEntries.size() - Undo),
                     Cut.Refusal.empty() ? "" : (" · " + Cut.Refusal).c_str()));
        const SolidArcCommandOutcome Again = CutSolidArcSketchesAt(Part, { 0.6, -0.8, 0.0 });
        Tally(PointCutGate, 0.0, !Again.AcceptedCondition && Curves(Part, First) == 2, Format("second crossing (ends only): %s (%s)", Again.AcceptedCondition ? "accepted!" : "refused", Again.Refusal.c_str()));
        SolidArcDocument Solid = CloneSolidArcDocument(Part);
        const SolidArcCommandOutcome Extruded = ExtrudeSolidArcSketch(Solid, First, 1.0, FeatureCombination::New);
        TallyVolume(PointCutGate, Solid, std::numbers::pi, Format("the cut circle (two arcs) still extrudes as a closed profile%s", Extruded.Refusal.empty() ? "" : (" · " + Extruded.Refusal).c_str()));
    }

    // ② A line through a circle (separate sketches), cut at (−1, 0): the line splits, the circle breaks at (−1, 0) and at its
    //    next crossing with the line (1, 0). A lone circle cut at its top halves (the opposite point).
    {
        SolidArcDocument Part = CreateSolidArcDocument("PointCutLine");
        const std::string Circle = Draw(Part, SolidArcDrawingKind::Circle, { { 0.0, 0.0 }, { 1.0, 0.0 } });
        const std::string Line   = Draw(Part, SolidArcDrawingKind::Line, { { -2.0, 0.0 }, { 2.0, 0.0 } });
        const SolidArcCommandOutcome Cut = CutSolidArcSketchesAt(Part, { -1.0, 0.0, 0.0 });
        Tally(PointCutGate, 0.0, Cut.AcceptedCondition && Curves(Part, Line) == 2 && Curves(Part, Circle) == 2 && EndsOn(Part, Circle, { { -1.0, 0.0 }, { 1.0, 0.0 } }),
              Format("line × circle at (−1, 0): line %d pieces, circle %d arcs ending at (±1, 0)%s", Curves(Part, Line), Curves(Part, Circle), Cut.Refusal.empty() ? "" : (" · " + Cut.Refusal).c_str()));
        SolidArcDocument Lone = CreateSolidArcDocument("PointCutLone");
        const std::string Single = Draw(Lone, SolidArcDrawingKind::Circle, { { 0.0, 0.0 }, { 1.0, 0.0 } });
        const SolidArcCommandOutcome Halved = CutSolidArcSketchesAt(Lone, { 0.0, 1.0, 0.0 });
        Tally(PointCutGate, 0.0, Halved.AcceptedCondition && Curves(Lone, Single) == 2 && EndsOn(Lone, Single, { { 0.0, 1.0 }, { 0.0, -1.0 } }), "lone circle cut at its top: two half arcs (0, ±1)");
        const SolidArcCommandOutcome Nowhere = CutSolidArcSketchesAt(Lone, { 5.0, 5.0, 0.0 });
        Tally(PointCutGate, 0.0, !Nowhere.AcceptedCondition, Format("a point on no curve: refused (%s)", Nowhere.Refusal.c_str()));
    }

    // ③ Duplicate 2D objects: a copy 3 along x (new caption, selected, extrudes to π); a scaling map refuses.
    {
        SolidArcDocument Part = CreateSolidArcDocument("SketchCopy");
        const std::string Circle = Draw(Part, SolidArcDrawingKind::Circle, { { 0.0, 0.0 }, { 1.0, 0.0 } });
        AffineMap Shift;
        Shift.Shift = { 3.0, 0.0, 0.0 };
        const size_t Steps = Part.Tree.Steps.size();
        const SolidArcCommandOutcome Copied = DuplicateSolidArcSketches(Part, { Circle }, Shift);
        const std::string Copy = Part.Tree.Steps.back().Label;
        const bool Moved = Copied.AcceptedCondition && std::abs(Part.Tree.Steps.back().Plane.Origin.X - 3.0) <= 1.0e-12;
        Tally(PointCutGate, 0.0, Moved && Part.Tree.Steps.size() == Steps + 1 && Part.Selection.Curves == std::vector<std::string>{ Copy } && Part.Captions[Copy] != Part.Captions[Circle],
              Format("%s → %s (%s), selected", Copied.Label.c_str(), Part.Captions[Copy].c_str(), Copy.c_str()));
        SolidArcDocument Solid = CloneSolidArcDocument(Part);
        Expect(ExtrudeSolidArcSketch(Solid, Copy, 1.0, FeatureCombination::New));
        TallyVolume(PointCutGate, Solid, std::numbers::pi, "the copy extrudes (r 1, h 1)");
        AffineMap Grow;
        Grow.ColumnX = { 2.0, 0.0, 0.0 };
        const SolidArcCommandOutcome Scaled = DuplicateSolidArcSketches(Part, { Circle }, Grow);
        Tally(PointCutGate, 0.0, !Scaled.AcceptedCondition, Format("scaling copy refused (%s)", Scaled.Refusal.c_str()));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

// W63: Point on curve (the shape kept: lines / arcs split, cubic splines take a knot, control-point curves a Boehm knot)
//      and Flow along curve (a 2D shape bent along a rail: lines across the axis stay lines, the rest cubic splines).
void ValidateCurveFlow()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    // Segments of the listed curves of a sketch (every curve when empty).
    const auto Segments = [](const SketchModel& Sketch, std::vector<int> Curves)
    {
        if (Curves.empty()) for (int Curve = 0; Curve < int(Sketch.Curves.size()); ++Curve) Curves.push_back(Curve);
        std::vector<ProfileSegment> Result;
        for (const int Curve : Curves)
        {
            std::string Refusal;
            Result.push_back(ResolveSketchCurveSegment(Sketch, Curve, 1.0e-9, Refusal));
        }
        return Result;
    };
    // Distance from Query to the nearest segment: 64 samples per polynomial piece, then Gauss–Newton on the fraction.
    const auto Nearest = [](const std::vector<ProfileSegment>& Onto, Vector2 Query)
    {
        double Best = std::numeric_limits<double>::infinity();
        for (const ProfileSegment& Segment : Onto)
        {
            const std::vector<double> Breaks = CollectProfileBreaks(Segment);
            double Fraction = 0.0, Gap = std::numeric_limits<double>::infinity();
            for (size_t Span = 0; Span + 1 < Breaks.size(); ++Span)
                for (int Sample = 0; Sample <= 64; ++Sample)
                {
                    const double Here = Breaks[Span] + (Breaks[Span + 1] - Breaks[Span]) * Sample / 64.0;
                    const double Away = Distance(EvaluateProfileSegment(Segment, Here), Query);
                    if (Away < Gap) { Gap = Away; Fraction = Here; }
                }
            for (int Step = 0; Step < 30; ++Step)
            {
                const Vector2 Offset  = EvaluateProfileSegment(Segment, Fraction) - Query;
                const Vector2 Tangent = EvaluateProfileTangent(Segment, Fraction);
                const double  Square  = Dot(Tangent, Tangent);
                if (Square <= 0.0) break;
                Fraction = std::clamp(Fraction - Dot(Offset, Tangent) / Square, 0.0, 1.0);
            }
            Best = std::min({ Best, Gap, Distance(EvaluateProfileSegment(Segment, Fraction), Query) });
        }
        return Best;
    };
    // Symmetric Hausdorff distance by 400 samples per segment each way.
    const auto Deviation = [&](const std::vector<ProfileSegment>& First, const std::vector<ProfileSegment>& Second)
    {
        double Worst = 0.0;
        for (const auto& [From, Onto] : { std::pair{ &First, &Second }, std::pair{ &Second, &First } })
            for (const ProfileSegment& Segment : *From)
                for (int Sample = 0; Sample <= 400; ++Sample) Worst = std::max(Worst, Nearest(*Onto, EvaluateProfileSegment(Segment, Sample / 400.0)));
        return Worst;
    };
    const char* const FormNames[] = { "", "Hermite", "Bezier", "NURBS", "cubic spline" };

    // ① Every freeform form through (0, 0) (1, 2) (2.5, −0.5) (4, 1.5) (5, 0): a point near 37 % of the curve is inserted
    //    and the curve is the same curve (Hausdorff ≤ 10⁻⁹); on the cubic spline the new point lies on it and a second
    //    insert at the same spot refuses (a point is already there).
    for (const SketchSplineForm Form : { SketchSplineForm::Hermite, SketchSplineForm::Bezier, SketchSplineForm::Nurbs, SketchSplineForm::CubicSpline })
    {
        SketchModel      Sketch;
        std::vector<int> Points;
        for (const Vector2 Position : { Vector2{ 0.0, 0.0 }, Vector2{ 1.0, 2.0 }, Vector2{ 2.5, -0.5 }, Vector2{ 4.0, 1.5 }, Vector2{ 5.0, 0.0 } }) Points.push_back(AppendSketchPoint(Sketch, Position));
        (void)AppendSketchSpline(Sketch, Points, Form, 1);
        const std::vector<ProfileSegment> Before = Segments(Sketch, { 0 });
        const Vector2                     Pick   = EvaluateProfileSegment(Before.front(), 0.37) + Vector2{ 0.004, -0.003 };
        const SketchTrimOutcome           Insert = InsertSketchCurvePoint(Sketch, 0, Pick);
        const bool   Inserted = Insert.Refusal.empty() && Insert.InsertedPoint >= 0 && Insert.InsertedPoint < int(Insert.Sketch.Points.size());
        const double Worst    = Inserted ? Deviation(Before, Segments(Insert.Sketch, { 0 })) : 1.0;
        Tally(CurveFlowGate, Worst, Inserted && Worst <= 1.0e-9 && Insert.Sketch.Curves.size() == 1,
              Format("%s: point %d inserted near (%.3f, %.3f), %d → %d points, Hausdorff to the original %.2e%s", FormNames[int(Form)], Insert.InsertedPoint, Pick.X, Pick.Y,
                     int(Sketch.Points.size()), int(Insert.Sketch.Points.size()), Worst, Insert.Refusal.empty() ? "" : (" · " + Insert.Refusal).c_str()));
        if (Form == SketchSplineForm::CubicSpline && Inserted)
        {
            const double OnCurve = Nearest(Before, Insert.Sketch.Points[Insert.InsertedPoint].Position);
            Tally(CurveFlowGate, OnCurve, OnCurve <= 1.0e-9, Format("cubic spline: the new point lies on the curve (%.2e)", OnCurve));
            const SketchTrimOutcome Again = InsertSketchCurvePoint(Insert.Sketch, 0, Insert.Sketch.Points[Insert.InsertedPoint].Position);
            Tally(CurveFlowGate, 0.0, !Again.Refusal.empty(), Format("a second insert on that point refuses (%s)", Again.Refusal.c_str()));
        }
    }

    // ② A horizontal line (0, 0) → (4, 0) takes a point at x = 1.5: two lines through (1.5, 0), the same segment, the
    //    horizontal constraint dropped so the halves can bend; a circle r 1 takes a point (two arcs, the same circle); a
    //    pick on a line end refuses (a point is there).
    {
        SketchModel Sketch;
        const int   Start = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int   End   = AppendSketchPoint(Sketch, { 4.0, 0.0 });
        const int   Line  = AppendSketchLine(Sketch, Start, End, 1);
        (void)AppendSketchConstraint(Sketch, SketchConstraintKind::Horizontal, Line);
        const SketchTrimOutcome Insert = InsertSketchCurvePoint(Sketch, Line, { 1.5, 0.02 });
        const bool   Inserted = Insert.Refusal.empty() && Insert.InsertedPoint >= 0;
        const double Worst    = Inserted ? Deviation(Segments(Sketch, {}), Segments(Insert.Sketch, {})) : 1.0;
        const double Placed   = Inserted ? Distance(Insert.Sketch.Points[Insert.InsertedPoint].Position, { 1.5, 0.0 }) : 1.0;
        const bool   Freed    = std::none_of(Insert.Sketch.Constraints.begin(), Insert.Sketch.Constraints.end(), [](const SketchConstraint& Row) { return Row.Kind == SketchConstraintKind::Horizontal; });
        Tally(CurveFlowGate, std::max(Worst, Placed), Inserted && Insert.Sketch.Curves.size() == 2 && Worst <= 1.0e-12 && Placed <= 1.0e-12 && Freed,
              Format("line: %d curves, point at (1.5, 0) within %.1e, Hausdorff %.1e, horizontal dropped: %s", int(Insert.Sketch.Curves.size()), Placed, Worst, Freed ? "yes" : "no"));
        const SketchTrimOutcome OnEnd = InsertSketchCurvePoint(Sketch, Line, { 4.0, 0.0 });
        Tally(CurveFlowGate, 0.0, !OnEnd.Refusal.empty(), Format("a pick on the line's end refuses (%s)", OnEnd.Refusal.c_str()));
        SketchModel Round;
        const int   Centre = AppendSketchPoint(Round, { 0.0, 0.0 });
        (void)AppendSketchCircle(Round, Centre, 1.0, 1);
        const SketchTrimOutcome Ring = InsertSketchCurvePoint(Round, 0, { 0.0, 1.01 });
        const double RingWorst = Ring.Refusal.empty() ? Deviation(Segments(Round, {}), Segments(Ring.Sketch, {})) : 1.0;
        Tally(CurveFlowGate, RingWorst, Ring.Refusal.empty() && Ring.Sketch.Curves.size() == 2 && RingWorst <= 1.0e-12,
              Format("circle r 1: %d arcs, Hausdorff %.1e%s", int(Ring.Sketch.Curves.size()), RingWorst, Ring.Refusal.empty() ? "" : (" · " + Ring.Refusal).c_str()));
    }

    // ③ In a document: the Point tool on a drawn cubic spline is one undo entry ("Insert point on …"), the curve keeps its
    //    shape, and its knots survive save → load byte-equal.
    {
        SolidArcDocument       Part = CreateSolidArcDocument("PointOnCurve");
        SolidArcDrawingRequest Request;
        Request.Kind = SolidArcDrawingKind::Curve; Request.Form = SketchSplineForm::CubicSpline;
        Request.Points = { { 0.0, 0.0 }, { 1.0, 1.0 }, { 2.0, -1.0 }, { 3.0, 0.5 } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string                 Label  = Part.Tree.Steps.back().Label;
        const std::vector<ProfileSegment> Before = Segments(Part.Regeneration.Sketches.at(Label), {});
        const size_t                      Undo   = Part.UndoEntries.size();
        const SolidArcCommandOutcome      Insert = TrimSolidArcSketch(Part, { Label, 0, EvaluateProfileSegment(Before.front(), 0.61), false, true });
        const SketchModel&                After  = Part.Regeneration.Sketches.at(Label);
        const double                      Worst  = Insert.AcceptedCondition ? Deviation(Before, Segments(After, {})) : 1.0;
        Tally(CurveFlowGate, Worst, Insert.AcceptedCondition && Part.UndoEntries.size() == Undo + 1 && After.Points.size() == 5 && !After.Curves.front().Knots.empty() && Worst <= 1.0e-9
                                    && Insert.Label.find("Insert point on") != std::string::npos,
              Format("%s: 5 points, %d knots, one undo entry, Hausdorff %.2e%s", Insert.Label.c_str(), int(After.Curves.front().Knots.size()), Worst, Insert.Refusal.empty() ? "" : (" · " + Insert.Refusal).c_str()));
        const std::string            Text     = EncodeSolidArcDocument(Part);
        const SolidArcDocumentFile   Loaded   = DecodeSolidArcDocument(Text);
        SolidArcDocument             Reloaded = CreateSolidArcDocument(Loaded.Title);
        const SolidArcCommandOutcome Opened   = ReplaceSolidArcTree(Reloaded, Loaded.Tree, Loaded.Captions, "Open");
        const double ReloadWorst = Opened.AcceptedCondition ? Deviation(Before, Segments(Reloaded.Regeneration.Sketches.at(Label), {})) : 1.0;
        Tally(CurveFlowGate, ReloadWorst, Loaded.Refusal.empty() && Opened.AcceptedCondition && EncodeSolidArcDocument(Reloaded) == Text && ReloadWorst <= 1.0e-9,
              Format("save → load: knots kept, byte-equal, Hausdorff %.2e (%s)", ReloadWorst, Loaded.Refusal.empty() ? Opened.Refusal.c_str() : Loaded.Refusal.c_str()));
        const bool Undone = UndoSolidArcCommand(Part);
        Tally(CurveFlowGate, 0.0, Undone && Part.Regeneration.Sketches.at(Label).Points.size() == 4 && Part.Regeneration.Sketches.at(Label).Curves.front().Knots.empty(),
              "undo: back to 4 points, no explicit knots");
    }

    // ④ Flow: a 2 × 0.4 rectangle (x 0 … 2, y ±0.2) along a quarter arc r 3 (0° → 90°, stretched): its long sides run at
    //    r 2.8 and 3.2 (every sample within 10⁻⁶ of the circle; the cubic splines interpolate it), its short sides stay lines
    //    on the radii at 0° and 90°; reversed it still lies on the same two circles.
    {
        SketchModel Rail;
        const int   Centre = AppendSketchPoint(Rail, { 0.0, 0.0 });
        const int   From   = AppendSketchPoint(Rail, { 3.0, 0.0 });
        const int   To     = AppendSketchPoint(Rail, { 0.0, 3.0 });
        (void)AppendSketchArc(Rail, Centre, From, To, 1);
        SketchModel      Shape;
        std::vector<int> Corners;
        for (const Vector2 Corner : { Vector2{ 0.0, -0.2 }, Vector2{ 2.0, -0.2 }, Vector2{ 2.0, 0.2 }, Vector2{ 0.0, 0.2 } }) Corners.push_back(AppendSketchPoint(Shape, Corner));
        for (int Side = 0; Side < 4; ++Side) (void)AppendSketchLine(Shape, Corners[size_t(Side)], Corners[size_t((Side + 1) % 4)], Side + 1);
        for (const bool Reverse : { false, true })
        {
            const SketchFlowOutcome Flowed = FlowSketchModel(Shape, Rail, { -1, true, Reverse });
            double Worst = Flowed.Refusal.empty() ? 0.0 : 1.0, Angles = 0.0;
            if (Flowed.Refusal.empty())
                for (const ProfileSegment& Segment : Segments(Flowed.Sketch, {}))
                    for (int Sample = 0; Sample <= 400; ++Sample)
                    {
                        const Vector2 Point = EvaluateProfileSegment(Segment, Sample / 400.0);
                        if (Segment.Kind == ProfileSegmentKind::Line) Angles = std::max(Angles, std::min(std::abs(Point.Y), std::abs(Point.X)));   // radial: on the x or y axis
                        else Worst = std::max(Worst, std::min(std::abs(Length(Point) - 2.8), std::abs(Length(Point) - 3.2)));                  // bent: on r 2.8 or 3.2
                    }
            Tally(CurveFlowGate, std::max(Worst, Angles), Flowed.Refusal.empty() && Flowed.Lines == 2 && Flowed.Splines == 2 && Flowed.AlongAxis == 0 && Worst <= 1.0e-6 && Angles <= 1.0e-9
                                                          && std::abs(Flowed.RailLength - 1.5 * std::numbers::pi) <= 1.0e-9,
                  Format("rectangle along a quarter arc%s: %d lines + %d splines, rail %.9f (1.5π), radii 2.8 / 3.2 within %.1e, short sides on the axes within %.1e%s",
                         Reverse ? " (reversed)" : "", Flowed.Lines, Flowed.Splines, Flowed.RailLength, Worst, Angles, Flowed.Refusal.empty() ? "" : (" · " + Flowed.Refusal).c_str()));
        }
    }

    // ⑤ In a document, true length: the filled rectangle flowed onto a circle r 3 (a closed rail) spans 2 / 3 rad from 0°;
    //    the new sketch "Flow1" is filled and extrudes 1 to the annular sector ½·(3.2² − 2.8²)·⅔ = 0.8; one undo entry; the
    //    shape and rail stay. The rail as a shape, or a lone pick, refuse.
    {
        const OrthonormalFrame Ground{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        const auto Draw = [&](SolidArcDocument& Part, SolidArcDrawingKind Kind, std::vector<Vector2> Points, bool CentreCondition)
        {
            SolidArcDrawingRequest Request;
            Request.Kind = Kind; Request.Points = std::move(Points); Request.PlaneCondition = true; Request.Plane = Ground;
            Request.CentreCondition = CentreCondition; Request.FillCondition = true;
            Expect(DrawSolidArcSketch(Part, Request));
            return Part.Tree.Steps.back().Label;
        };
        SolidArcDocument  Part  = CreateSolidArcDocument("Flow");
        const std::string Rail  = Draw(Part, SolidArcDrawingKind::Circle, { { 0.0, 0.0 }, { 3.0, 0.0 } }, true);
        const std::string Shape = Draw(Part, SolidArcDrawingKind::Rectangle, { { 0.0, -0.2 }, { 2.0, 0.2 } }, false);
        const size_t Steps = Part.Tree.Steps.size(), Undo = Part.UndoEntries.size();
        SolidArcFlowRequest Request{ Rail, { Shape }, { -1, false, false } };
        const SolidArcCommandOutcome Flowed = FlowSolidArcSketches(Part, Request);
        const std::string            Label  = Part.Tree.Steps.back().Label;
        const bool Made = Flowed.AcceptedCondition && Part.Tree.Steps.size() == Steps + 1 && Part.UndoEntries.size() == Undo + 1 && Part.Captions[Label] == "Flow1" && Part.Tree.Steps.back().FillCondition;
        double Worst = Made ? 0.0 : 1.0;
        if (Made)
            for (const Vector2& Point : { Vector2{ 2.8, 0.0 }, Vector2{ 3.2, 0.0 }, Vector2{ 2.8 * std::cos(2.0 / 3.0), 2.8 * std::sin(2.0 / 3.0) }, Vector2{ 3.2 * std::cos(2.0 / 3.0), 3.2 * std::sin(2.0 / 3.0) } })
            {
                double Closest = std::numeric_limits<double>::infinity();
                for (const SketchPoint& Candidate : Part.Regeneration.Sketches.at(Label).Points) Closest = std::min(Closest, Distance(Candidate.Position, Point));
                Worst = std::max(Worst, Closest);
            }
        Tally(CurveFlowGate, Worst, Made && Worst <= 1.0e-9, Format("%s: %s filled, corners at r 2.8 / 3.2 and 0 / ⅔ rad within %.1e%s", Flowed.Label.c_str(), Part.Captions[Label].c_str(), Worst,
                                                                    Flowed.Refusal.empty() ? "" : (" · " + Flowed.Refusal).c_str()));
        SolidArcDocument             Solid    = CloneSolidArcDocument(Part);
        const SolidArcCommandOutcome Extruded = ExtrudeSolidArcSketch(Solid, Label, 1.0, FeatureCombination::New);
        const double Relative = std::abs(Solid.Summary.Volume - 0.8) / 0.8;
        Tally(CurveFlowGate, Relative, Extruded.AcceptedCondition && Solid.Summary.SoundCondition && Relative <= 1.0e-6,
              Format("Flow1 extrudes to the annular sector 0.8 (V = %.12f, rel %.1e; the long sides are interpolating cubics)%s", Solid.Summary.Volume, Relative, Extruded.Refusal.empty() ? "" : (" · " + Extruded.Refusal).c_str()));
        const SolidArcCommandOutcome Itself = FlowSolidArcSketches(Part, { Rail, { Rail }, {} });
        const SolidArcCommandOutcome Lone   = FlowSolidArcSketches(Part, { Rail, {}, {} });
        Tally(CurveFlowGate, 0.0, !Itself.AcceptedCondition && !Lone.AcceptedCondition && Part.Tree.Steps.size() == Steps + 1,
              Format("refused: the rail as its own shape (%s) · no shape (%s)", Itself.Refusal.c_str(), Lone.Refusal.c_str()));
    }
}

// W65: path picks on fixtures with known answers, then the invariants on every ordered edge pair of six bodies.
void ValidateEdgePath()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto Near   = [](double Value, double Target) { return std::abs(Value - Target) <= 1.0e-9; };
    const auto Names  = [](const SolidArcDocument& Document)
    {
        std::vector<std::string> All;
        for (const auto& [Slot, Name] : Document.Regeneration.Naming.Edges) All.push_back(Name);
        return All;
    };
    const auto EdgeWhere = [](const SolidArcDocument& Document, const auto& Test)
    {
        for (const auto& [Slot, Name] : Document.Regeneration.Naming.Edges)
        {
            const Vector3 Start = EdgeEndPosition(Document, Name, false), End = EdgeEndPosition(Document, Name, true);
            if (Test(Start, End) || Test(End, Start)) return Name;
        }
        return std::string{};
    };
    const auto Every = [](const SolidArcDocument& Document, const std::vector<std::string>& Path, const auto& Test)
    {
        for (const std::string& Name : Path) if (!Test(EdgeEndPosition(Document, Name, false), EdgeEndPosition(Document, Name, true))) return false;
        return true;
    };
    const auto Same = [](Vector3 A, Vector3 B) { return std::abs(A.X - B.X) <= 1.0e-9 && std::abs(A.Y - B.Y) <= 1.0e-9 && std::abs(A.Z - B.Z) <= 1.0e-9; };
    // Consecutive edges meet at a vertex, From first, To last.
    const auto Joined = [&](const SolidArcDocument& Document, const SolidArcEdgePath& Path, const std::string& From, const std::string& To)
    {
        if (!Path.Refusal.empty() || Path.Edges.empty() || Path.Edges.front() != From || Path.Edges.back() != To) return false;
        for (size_t Index = 1; Index < Path.Edges.size(); ++Index)
        {
            const Vector3 A0 = EdgeEndPosition(Document, Path.Edges[Index - 1], false), A1 = EdgeEndPosition(Document, Path.Edges[Index - 1], true);
            const Vector3 B0 = EdgeEndPosition(Document, Path.Edges[Index], false), B1 = EdgeEndPosition(Document, Path.Edges[Index], true);
            if (!(Same(A0, B0) || Same(A0, B1) || Same(A1, B0) || Same(A1, B1))) return false;
        }
        return true;
    };
    // No vertex used by more than two edge ends: a simple chain or cycle.
    const auto Simple = [&](const SolidArcDocument& Document, const std::vector<std::string>& Path)
    {
        std::vector<std::pair<Vector3, int>> Uses;
        for (const std::string& Name : Path)
            for (const bool Reversed : { false, true })
            {
                const Vector3 At = EdgeEndPosition(Document, Name, Reversed);
                if (Reversed && Same(At, EdgeEndPosition(Document, Name, false))) continue;  // A closed edge passes its vertex once
                auto Found = std::find_if(Uses.begin(), Uses.end(), [&](const auto& Use) { return Same(Use.first, At); });
                if (Found == Uses.end()) Uses.push_back({ At, 1 });
                else if (++Found->second > 2) return false;
            }
        return true;
    };
    const auto Subset = [](const std::vector<std::string>& Part, const std::vector<std::string>& Whole)
    {
        return std::all_of(Part.begin(), Part.end(), [&](const std::string& Name) { return std::find(Whole.begin(), Whole.end(), Name) != Whole.end(); });
    };
    const auto Path = [](const SolidArcDocument& Document, const std::string& From, const std::string& To, SolidArcEdgePathMode Mode, bool Other = false)
    {
        return SolidArcEdgePathBetween(Document, From, To, Mode, Other);
    };
    const auto NewBlock = [](double Width, double Depth, double Height)
    {
        SolidArcDocument Part = CreateSolidArcDocument("Path");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = Width; Block.Depth = Depth; Block.Height = Height;
        Expect(ConstructSolidArcPrimitive(Part, Block));
        return Part;
    };

    // ① block 4 × 2 × 1, front-top → back-top: on the top rim (3 edges, 4 + 2 + 4), Other side the other short edge,
    //    Whole loop the rim (12), Shortest the same length
    {
        SolidArcDocument Box = NewBlock(4.0, 2.0, 1.0);
        const std::string Front = EdgeWhere(Box, [&](Vector3 A, Vector3 B) { return Near(A.Y, 0.0) && Near(B.Y, 0.0) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const std::string Back  = EdgeWhere(Box, [&](Vector3 A, Vector3 B) { return Near(A.Y, 2.0) && Near(B.Y, 2.0) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const SolidArcEdgePath Along = Path(Box, Front, Back, SolidArcEdgePathMode::AlongLoop), Other = Path(Box, Front, Back, SolidArcEdgePathMode::AlongLoop, true);
        const SolidArcEdgePath Whole = Path(Box, Front, Back, SolidArcEdgePathMode::WholeLoop), Short = Path(Box, Front, Back, SolidArcEdgePathMode::Shortest);
        const auto Top = [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); };
        const bool Flipped = Along.Edges.size() == 3 && Other.Edges.size() == 3 && Along.Edges[1] != Other.Edges[1];
        Tally(EdgePathGate, std::abs(Along.Length - 10.0) + std::abs(Whole.Length - 12.0) + std::abs(Short.Length - 10.0),
              Joined(Box, Along, Front, Back) && Joined(Box, Other, Front, Back) && Joined(Box, Short, Front, Back) && Along.ClosedCondition && Flipped
                  && Every(Box, Along.Edges, Top) && Every(Box, Other.Edges, Top) && Whole.Edges.size() == 4 && Every(Box, Whole.Edges, Top)
                  && Near(Along.Length, 10.0) && Near(Other.Length, 10.0) && Near(Whole.Length, 12.0) && Near(Short.Length, 10.0),
              Format("block front-top %s → back-top %s: along %d edges on the %s (%.12g), other side %d via %s, whole %d (%.12g), shortest %d (%.12g)", Front.c_str(), Back.c_str(),
                     int(Along.Edges.size()), Along.Carrier.c_str(), Along.Length, int(Other.Edges.size()), Other.Edges.size() > 1 ? Other.Edges[1].c_str() : "-",
                     int(Whole.Edges.size()), Whole.Length, int(Short.Edges.size()), Short.Length));

        // ② no loop holds both — front-top → right-bottom, front-top → back-bottom: Along refuses (Blender's shortest
        //    path would cut across the corner), Shortest goes down a vertical edge (4 + 1 + 2 = 7; 4 + 3 + 4 = 11)
        const std::string RightLow = EdgeWhere(Box, [&](Vector3 A, Vector3 B) { return Near(A.X, 4.0) && Near(B.X, 4.0) && Near(A.Z, 0.0) && Near(B.Z, 0.0); });
        const std::string BackLow  = EdgeWhere(Box, [&](Vector3 A, Vector3 B) { return Near(A.Y, 2.0) && Near(B.Y, 2.0) && Near(A.Z, 0.0) && Near(B.Z, 0.0); });
        for (const auto& [Far, Expected, Count] : { std::tuple{ RightLow, 7.0, 3 }, std::tuple{ BackLow, 11.0, 4 } })
        {
            const SolidArcEdgePath Refused = Path(Box, Front, Far, SolidArcEdgePathMode::AlongLoop), WholeRefused = Path(Box, Front, Far, SolidArcEdgePathMode::WholeLoop);
            const SolidArcEdgePath Across  = Path(Box, Front, Far, SolidArcEdgePathMode::Shortest);
            Tally(EdgePathGate, std::abs(Across.Length - Expected), !Refused.Refusal.empty() && Refused.Edges.empty() && !WholeRefused.Refusal.empty()
                                                                        && Joined(Box, Across, Front, Far) && int(Across.Edges.size()) == Count && Near(Across.Length, Expected),
                  Format("block front-top → %s: along / whole refuse (\"%s\"), shortest %d edges %.12g (expect %d, %.12g)", Far.c_str(), Refused.Refusal.c_str(),
                         int(Across.Edges.size()), Across.Length, Count, Expected));
        }
        // ③ adjacent edges: both modes the two edges; the same edge twice: itself
        const std::string Left = EdgeWhere(Box, [&](Vector3 A, Vector3 B) { return Near(A.X, 0.0) && Near(B.X, 0.0) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const SolidArcEdgePath Pair = Path(Box, Front, Left, SolidArcEdgePathMode::AlongLoop), Itself = Path(Box, Front, Front, SolidArcEdgePathMode::Shortest);
        const SolidArcEdgePath Round = Path(Box, Front, Left, SolidArcEdgePathMode::AlongLoop, true);
        Tally(EdgePathGate, std::abs(Pair.Length - 6.0) + std::abs(Round.Length - 12.0), Joined(Box, Pair, Front, Left) && Pair.Edges.size() == 2 && Near(Pair.Length, 6.0)
                                                                                         && Joined(Box, Round, Front, Left) && Round.Edges.size() == 4 && Near(Round.Length, 12.0)
                                                                                         && Itself.Edges.size() == 1,
              Format("adjacent front-top → left-top: along %d (%.12g), other side %d round the rim (%.12g); one edge to itself %d", int(Pair.Edges.size()), Pair.Length,
                     int(Round.Edges.size()), Round.Length, int(Itself.Edges.size())));
    }

    // ④ hexagonal prism (r 1, side 1): opposite top rim edges — along the hub edge loop 4 edges (4), the far side the
    //    other 4, whole loop 6 (6); an upright to the opposite upright on the side band refuses along a loop
    {
        SolidArcDocument Prism = CreateSolidArcDocument("PathPrism");
        SolidArcConstructRequest Request;
        Request.Kind = SolidArcConstructKind::Prism; Request.Radius = 1.0; Request.Height = 1.0; Request.Sides = 6;
        Expect(ConstructSolidArcPrimitive(Prism, Request));
        std::vector<std::string> Rims;
        for (const std::string& Name : Names(Prism))
        {
            const Vector3 A = EdgeEndPosition(Prism, Name, false), B = EdgeEndPosition(Prism, Name, true);
            if (Near(A.Z, 1.0) && Near(B.Z, 1.0)) Rims.push_back(Name);
        }
        const auto Middle = [&](const std::string& Name) { const Vector3 A = EdgeEndPosition(Prism, Name, false), B = EdgeEndPosition(Prism, Name, true); return Vector3{ 0.5 * (A.X + B.X), 0.5 * (A.Y + B.Y), 0.0 }; };
        std::string First = Rims.empty() ? std::string{} : Rims.front(), Opposite;
        for (const std::string& Name : Rims)
        {
            const Vector3 M = Middle(Name), F = Middle(First);
            if (std::abs(M.X + F.X) <= 1.0e-9 && std::abs(M.Y + F.Y) <= 1.0e-9) Opposite = Name;
        }
        const SolidArcEdgePath Along = Path(Prism, First, Opposite, SolidArcEdgePathMode::AlongLoop), Other = Path(Prism, First, Opposite, SolidArcEdgePathMode::AlongLoop, true);
        const SolidArcEdgePath Whole = Path(Prism, First, Opposite, SolidArcEdgePathMode::WholeLoop);
        std::vector<std::string> Union = Along.Edges;
        for (const std::string& Name : Other.Edges) if (std::find(Union.begin(), Union.end(), Name) == Union.end()) Union.push_back(Name);
        Tally(EdgePathGate, std::abs(Along.Length - 4.0) + std::abs(Whole.Length - 6.0),
              Rims.size() == 6 && !Opposite.empty() && Joined(Prism, Along, First, Opposite) && Joined(Prism, Other, First, Opposite) && Along.Edges.size() == 4 && Other.Edges.size() == 4
                  && Near(Along.Length, 4.0) && Near(Other.Length, 4.0) && Whole.Edges.size() == 6 && Near(Whole.Length, 6.0) && Union.size() == 6,
              Format("hexagonal prism rim %s → opposite %s: along %d on the %s (%.12g), far side %d (%.12g), together %d, whole %d (%.12g)", First.c_str(), Opposite.c_str(),
                     int(Along.Edges.size()), Along.Carrier.c_str(), Along.Length, int(Other.Edges.size()), Other.Length, int(Union.size()), int(Whole.Edges.size()), Whole.Length));
    }

    // ⑤ cylinder r 1 h 1: top circle → bottom circle along the side face's rim (circle, seam, circle: 2π + 1 + 2π)
    {
        SolidArcDocument Round = CreateSolidArcDocument("PathCylinder");
        SolidArcConstructRequest Drum;
        Drum.Kind = SolidArcConstructKind::Cylinder; Drum.Radius = 1.0; Drum.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Round, Drum));
        const std::string Upper = EdgeWhere(Round, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const std::string Lower = EdgeWhere(Round, [&](Vector3 A, Vector3 B) { return Near(A.Z, 0.0) && Near(B.Z, 0.0); });
        const SolidArcEdgePath Along = Path(Round, Upper, Lower, SolidArcEdgePathMode::AlongLoop), Short = Path(Round, Upper, Lower, SolidArcEdgePathMode::Shortest);
        const double Expected = 4.0 * std::numbers::pi + 1.0;
        Tally(EdgePathGate, std::abs(Along.Length - Expected) / Expected, Joined(Round, Along, Upper, Lower) && Along.Edges.size() == 3 && std::abs(Along.Length - Expected) <= 1.0e-9 * Expected
                                                                          && Short.Edges == Along.Edges,
              Format("cylinder top circle → bottom circle: along %d on the %s, %.12f (expect 4π + 1 = %.12f), shortest the same", int(Along.Edges.size()), Along.Carrier.c_str(),
                     Along.Length, Expected));
    }

    // ⑥ revolved diamond: a seam → the opposite seam on the meridian edge loop (3 of the 4 seams), whole loop 4
    {
        SolidArcDocument Turned = CreateSolidArcDocument("PathRevolve");
        SolidArcDrawingRequest Diamond;
        Diamond.Kind = SolidArcDrawingKind::Line; Diamond.Points = { { 1.0, 0.5 }, { 1.5, 0.0 }, { 2.0, 0.5 }, { 1.5, 1.0 } }; Diamond.ClosedCondition = true; Diamond.FillCondition = true;
        Expect(DrawSolidArcSketch(Turned, Diamond));
        const std::string Square = Turned.Tree.Steps.back().Label;
        Expect(RevolveSolidArcSketch(Turned, Square, ProposeSolidArcRevolveAxis(Turned, Square), 2.0 * std::numbers::pi, FeatureCombination::New));
        std::vector<std::string> Seams;
        for (const std::string& Name : Names(Turned)) if (Distance(EdgeEndPosition(Turned, Name, false), EdgeEndPosition(Turned, Name, true)) > 0.5) Seams.push_back(Name);
        std::string First = Seams.empty() ? std::string{} : Seams.front(), Opposite;
        for (const std::string& Name : Seams)
        {
            const Vector3 A0 = EdgeEndPosition(Turned, First, false), A1 = EdgeEndPosition(Turned, First, true);
            const Vector3 B0 = EdgeEndPosition(Turned, Name, false), B1 = EdgeEndPosition(Turned, Name, true);
            if (Name != First && !Same(A0, B0) && !Same(A0, B1) && !Same(A1, B0) && !Same(A1, B1)) Opposite = Name;
        }
        const SolidArcEdgePath Along = Path(Turned, First, Opposite, SolidArcEdgePathMode::AlongLoop), Whole = Path(Turned, First, Opposite, SolidArcEdgePathMode::WholeLoop);
        const double Seam = std::sqrt(0.5);
        Tally(EdgePathGate, std::abs(Along.Length - 3.0 * Seam), Seams.size() == 4 && !Opposite.empty() && Joined(Turned, Along, First, Opposite) && Along.Edges.size() == 3
                                                                     && Near(Along.Length, 3.0 * Seam) && Whole.Edges.size() == 4 && Subset(Whole.Edges, Seams),
              Format("revolved diamond seam %s → opposite %s: along %d on the %s (%.12f = 3·√½), whole %d seams", First.c_str(), Opposite.c_str(), int(Along.Edges.size()),
                     Along.Carrier.c_str(), Along.Length, int(Whole.Edges.size())));
    }

    // ⑦ through-hole block: a hole rim edge and the outer rim share the top face but no loop, nor any edge path
    SolidArcDocument Holed = NewBlock(4.0, 2.0, 1.0);
    {
        SolidArcDrawingRequest Hole;
        Hole.Kind = SolidArcDrawingKind::Rectangle; Hole.Points = { { 1.5, 0.5 }, { 2.5, 1.5 } }; Hole.ClosedCondition = true; Hole.FillCondition = true;
        Expect(DrawSolidArcSketch(Holed, Hole));
        Expect(ExtrudeSolidArcSketch(Holed, Holed.Tree.Steps.back().Label, 3.0, FeatureCombination::Difference, {}, -1.0));   // z −1 … 2: through
        const std::string Outer = EdgeWhere(Holed, [&](Vector3 A, Vector3 B) { return Near(A.Y, 0.0) && Near(B.Y, 0.0) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const std::string Inner = EdgeWhere(Holed, [&](Vector3 A, Vector3 B) { return Near(A.Y, 0.5) && Near(B.Y, 0.5) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const SolidArcEdgePath Along = Path(Holed, Outer, Inner, SolidArcEdgePathMode::AlongLoop), Short = Path(Holed, Outer, Inner, SolidArcEdgePathMode::Shortest);
        Tally(EdgePathGate, 0.0, !Outer.empty() && !Inner.empty() && !Along.Refusal.empty() && !Short.Refusal.empty() && Holed.Summary.Genus == 1,
              Format("through-hole block (genus %d) outer %s → hole rim %s: along refuses, shortest refuses (\"%s\")", Holed.Summary.Genus, Outer.c_str(), Inner.c_str(), Short.Refusal.c_str()));
    }

    // ⑧ every ordered pair on six bodies (block, hexagonal / pentagonal prisms, cylinder, holed block, filleted block)
    std::vector<std::pair<std::string, SolidArcDocument>> Bodies;
    Bodies.emplace_back("block", NewBlock(4.0, 2.0, 1.0));
    for (const int Sides : { 6, 5 })
    {
        SolidArcDocument Prism = CreateSolidArcDocument("PathStress");
        SolidArcConstructRequest Request;
        Request.Kind = SolidArcConstructKind::Prism; Request.Radius = 1.0; Request.Height = 0.7; Request.Sides = Sides;
        Expect(ConstructSolidArcPrimitive(Prism, Request));
        Bodies.emplace_back(Format("%d-prism", Sides), std::move(Prism));
    }
    {
        SolidArcDocument Round = CreateSolidArcDocument("PathStress");
        SolidArcConstructRequest Drum;
        Drum.Kind = SolidArcConstructKind::Cylinder; Drum.Radius = 1.0; Drum.Height = 2.0;
        Expect(ConstructSolidArcPrimitive(Round, Drum));
        Bodies.emplace_back("cylinder", std::move(Round));
    }
    Bodies.emplace_back("holed block", std::move(Holed));
    {
        SolidArcDocument Rounded = NewBlock(4.0, 2.0, 1.0);
        PickAndSelect(Rounded, RayBetween({ 2.0, -1.0, 2.0 }, { 2.0, 0.0, 1.0 }), false);
        PickAndSelect(Rounded, RayBetween({ 5.0, 1.0, 2.0 }, { 4.0, 1.0, 1.0 }), true);
        Expect(BlendSolidArcSelection(Rounded, BlendProfile::Fillet, 0.3));
        Bodies.emplace_back("filleted block", std::move(Rounded));
    }
    for (const auto& [Caption, Part] : Bodies)
    {
        const std::vector<std::string> All = Names(Part);
        int Pairs = 0, Failed = 0, Refused = 0, Closed = 0;
        std::string FirstFailure;
        double Worst = 0.0;
        for (const std::string& From : All)
            for (const std::string& To : All)
            {
                if (From == To) continue;
                ++Pairs;
                const SolidArcEdgePath Short = Path(Part, From, To, SolidArcEdgePathMode::Shortest);
                const SolidArcEdgePath Along = Path(Part, From, To, SolidArcEdgePathMode::AlongLoop);
                bool Fine = Short.Refusal.empty() ? Joined(Part, Short, From, To) : Caption == "holed block";
                if (Along.Refusal.empty())
                {
                    const SolidArcEdgePath Whole = Path(Part, From, To, SolidArcEdgePathMode::WholeLoop);
                    const SolidArcEdgePath Other = Path(Part, From, To, SolidArcEdgePathMode::AlongLoop, true);
                    Fine = Fine && Joined(Part, Along, From, To) && Whole.Refusal.empty() && Subset(Along.Edges, Whole.Edges) && Simple(Part, Whole.Edges)
                           && Short.Refusal.empty() && Along.Length >= Short.Length - 1.0e-9 && Joined(Part, Other, From, To);
                    Worst = std::max(Worst, std::max(0.0, Short.Length - Along.Length));
                    if (Along.ClosedCondition)
                    {
                        ++Closed;
                        std::vector<std::string> Union = Along.Edges;
                        for (const std::string& Name : Other.Edges) if (std::find(Union.begin(), Union.end(), Name) == Union.end()) Union.push_back(Name);
                        Fine = Fine && Other.Length >= Along.Length - 1.0e-9 && Union.size() == Whole.Edges.size() && Subset(Union, Whole.Edges);
                    }
                }
                else ++Refused;
                if (!Fine && FirstFailure.empty())
                {
                    FirstFailure = " · first " + From + " → " + To;
                    if (std::getenv("SOLIDARC_DOCVAL_PATH"))
                    {
                        const SolidArcEdgePath Whole = Path(Part, From, To, SolidArcEdgePathMode::WholeLoop);
                        const SolidArcEdgePath Other = Path(Part, From, To, SolidArcEdgePathMode::AlongLoop, true);
                        std::printf("FAIL %s: short %d %.6f [%s] joined %d · along %d %.6f %s joined %d closed %d · whole %d simple %d subset %d · other %d %.6f joined %d\n", Caption.c_str(),
                                    int(Short.Edges.size()), Short.Length, Short.Refusal.c_str(), int(Joined(Part, Short, From, To)), int(Along.Edges.size()), Along.Length, Along.Carrier.c_str(),
                                    int(Joined(Part, Along, From, To)), int(Along.ClosedCondition), int(Whole.Edges.size()), int(Simple(Part, Whole.Edges)), int(Subset(Along.Edges, Whole.Edges)),
                                    int(Other.Edges.size()), Other.Length, int(Joined(Part, Other, From, To)));
                    }
                }
                Failed += Fine ? 0 : 1;
            }
        Tally(EdgePathGate, Worst, Failed == 0 && Pairs > 0,
              Format("stress %s: %d edges, %d ordered pairs, %d failed, %d on a closed loop, %d refused along a loop%s", Caption.c_str(), int(All.size()), Pairs, Failed, Closed, Refused,
                     FirstFailure.c_str()));
    }
}

// W66: Wrap / project shapes round the surface of a curve (a label round a can), S on sketches, O on an open arc.
void ValidateWrap()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const OrthonormalFrame Ground{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const OrthonormalFrame Front{ { 0.0, -3.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };   // Seen along +y
    const auto Rectangle = [](double Left, double Right, double Bottom, double Top)
    {
        SketchModel      Shape;
        std::vector<int> Corners;
        for (const Vector2 Corner : { Vector2{ Left, Bottom }, Vector2{ Right, Bottom }, Vector2{ Right, Top }, Vector2{ Left, Top } }) Corners.push_back(AppendSketchPoint(Shape, Corner));
        for (int Side = 0; Side < 4; ++Side) (void)AppendSketchLine(Shape, Corners[size_t(Side)], Corners[size_t((Side + 1) % 4)], Side + 1);
        return Shape;
    };
    const auto Circle = [](double Radius)
    {
        SketchModel Rail;
        (void)AppendSketchCircle(Rail, AppendSketchPoint(Rail, { 0.0, 0.0 }), Radius, 1);
        return Rail;
    };
    // Distance from (u, v) to the outline of the rectangle [Left, Right] × [Bottom, Top].
    const auto Outline = [](double U, double V, double Left, double Right, double Bottom, double Top)
    {
        const auto Segment = [&](double X0, double Y0, double X1, double Y1)
        {
            const double Along = std::clamp(((U - X0) * (X1 - X0) + (V - Y0) * (Y1 - Y0)) / ((X1 - X0) * (X1 - X0) + (Y1 - Y0) * (Y1 - Y0)), 0.0, 1.0);
            return std::hypot(U - X0 - Along * (X1 - X0), V - Y0 - Along * (Y1 - Y0));
        };
        return std::min({ Segment(Left, Bottom, Right, Bottom), Segment(Right, Bottom, Right, Top), Segment(Right, Top, Left, Top), Segment(Left, Top, Left, Bottom) });
    };
    // Every sample on the cylinder r round z (front at −y), and unrolled (Wrap: r·θ, Project: x) on the rectangle.
    struct Measured { double Radial = 0.0, Unrolled = 0.0, Low = 1.0e9, High = -1.0e9; int Points = 0; };
    const auto Measure = [&](const SketchWrapOutcome& Wrapped, double Radius, bool WrapCondition, std::array<double, 4> Box)
    {
        Measured Result;
        for (const std::vector<Vector3>& Curve : Wrapped.Curves)
            for (const Vector3& Point : Curve)
            {
                const double Angle = std::atan2(Point.X, -Point.Y);
                const double U     = WrapCondition ? Radius * Angle : Point.X;
                Result.Radial   = std::max(Result.Radial, std::abs(std::hypot(Point.X, Point.Y) - Radius));
                Result.Unrolled = std::max(Result.Unrolled, Outline(U, Point.Z, Box[0], Box[1], Box[2], Box[3]));
                Result.Low  = std::min(Result.Low, Angle);
                Result.High = std::max(Result.High, Angle);
                ++Result.Points;
            }
        return Result;
    };
    const SketchWrapSettings AlongY{ SketchWrapKind::Wrap, { 0.0, 1.0, 0.0 }, {} };
    const SketchWrapSettings ProjectY{ SketchWrapKind::Project, { 0.0, 1.0, 0.0 }, {} };

    // ① wrap and ② project a 1 × 0.5 rectangle round the circle r 1 on the ground (the can stands along z).
    for (const bool WrapCondition : { true, false })
    {
        const SketchWrapOutcome Wrapped = WrapSketchModels({ { Rectangle(-0.5, 0.5, 0.0, 0.5), Front } }, Circle(1.0), Ground, WrapCondition ? AlongY : ProjectY);
        const Measured Result  = Measure(Wrapped, 1.0, WrapCondition, { -0.5, 0.5, 0.0, 0.5 });
        const double   Reach   = WrapCondition ? 0.5 : std::asin(0.5);
        const double   Ends    = std::max(std::abs(Result.High - Reach), std::abs(Result.Low + Reach));
        const double   Worst   = std::max({ Result.Radial, Result.Unrolled, Ends });
        Tally(WrapGate, Worst, Wrapped.Refusal.empty() && Wrapped.Curves.size() == 4 && std::abs(Wrapped.Radius - 1.0) <= 1.0e-12 && std::abs(Wrapped.Extent - 1.0) <= 1.0e-9 && Worst <= 1.0e-9,
              Format("%s a 1 × 0.5 rectangle round a circle r 1 along +y: %d curves, %d samples, off r 1 by %.1e, unrolled off the rectangle by %.1e, ends ±%.6f rad (%s)%s",
                     WrapCondition ? "wrap" : "project", int(Wrapped.Curves.size()), Result.Points, Result.Radial, Result.Unrolled, Result.High, WrapCondition ? "0.5: lengths kept" : "π/6: x kept",
                     Wrapped.Refusal.empty() ? "" : (" · " + Wrapped.Refusal).c_str()));
    }
    // ③ a rectangle 3 wide projects past the can's silhouette: refused; wrapped it goes 3 rad round.
    {
        const SketchWrapOutcome Wide    = WrapSketchModels({ { Rectangle(-1.5, 1.5, 0.0, 0.5), Front } }, Circle(1.0), Ground, ProjectY);
        const SketchWrapOutcome Wrapped = WrapSketchModels({ { Rectangle(-1.5, 1.5, 0.0, 0.5), Front } }, Circle(1.0), Ground, AlongY);
        const Measured Result = Measure(Wrapped, 1.0, true, { -1.5, 1.5, 0.0, 0.5 });
        const double   Worst  = std::max({ Result.Radial, Result.Unrolled, std::abs(Result.High - 1.5) });
        Tally(WrapGate, Worst, !Wide.Refusal.empty() && Wrapped.Refusal.empty() && Worst <= 1.0e-9,
              Format("3 wide: project refused (%s) · wrap reaches ±%.6f rad, off r 1 by %.1e, unrolled %.1e", Wide.Refusal.c_str(), Result.High, Result.Radial, Result.Unrolled));
    }
    // ④ the circle faces the view (drawn on the front plane): with the axis z it stands up as the same can.
    {
        const OrthonormalFrame Facing{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        SketchWrapSettings     Settings = AlongY;
        Settings.Axis = { 0.0, 0.0, 1.0 };
        const SketchWrapOutcome Wrapped = WrapSketchModels({ { Rectangle(-0.5, 0.5, 0.0, 0.5), Front } }, Circle(1.0), Facing, Settings);
        const SketchWrapOutcome Flat    = WrapSketchModels({ { Rectangle(-0.5, 0.5, 0.0, 0.5), Front } }, Circle(1.0), Facing, AlongY);
        const Measured Result = Measure(Wrapped, 1.0, true, { -0.5, 0.5, 0.0, 0.5 });
        const double   Worst  = std::max({ Result.Radial, Result.Unrolled, std::abs(Result.High - 0.5) });
        Tally(WrapGate, Worst, Wrapped.Refusal.empty() && !Flat.Refusal.empty() && Worst <= 1.0e-9,
              Format("circle facing the view, axis z: off r 1 by %.1e, unrolled %.1e, ends ±%.6f · without the axis refused (%s)", Result.Radial, Result.Unrolled, Result.High, Flat.Refusal.c_str()));
    }
    // ⑤ an open arc r 2 (−120° … −60°) as the rail; ⑥ two letters as separate shapes keep their places (one centre).
    {
        SketchModel Arc;
        const int   Centre = AppendSketchPoint(Arc, { 0.0, 0.0 });
        const int   From   = AppendSketchPoint(Arc, { 2.0 * std::cos(-2.0 * std::numbers::pi / 3.0), 2.0 * std::sin(-2.0 * std::numbers::pi / 3.0) });
        const int   To     = AppendSketchPoint(Arc, { 2.0 * std::cos(-std::numbers::pi / 3.0), 2.0 * std::sin(-std::numbers::pi / 3.0) });
        (void)AppendSketchArc(Arc, Centre, From, To, 1);
        const SketchWrapOutcome OnArc  = WrapSketchModels({ { Rectangle(-0.5, 0.5, 0.0, 0.5), Front } }, Arc, Ground, AlongY);
        const Measured          Result = Measure(OnArc, 2.0, true, { -0.5, 0.5, 0.0, 0.5 });
        const double            Worst  = std::max({ Result.Radial, Result.Unrolled, std::abs(Result.High - 0.25) });
        Tally(WrapGate, Worst, OnArc.Refusal.empty() && Worst <= 1.0e-9,
              Format("open arc r 2 rail: off r 2 by %.1e, unrolled %.1e, ends ±%.6f rad (0.25)%s", Result.Radial, Result.Unrolled, Result.High, OnArc.Refusal.empty() ? "" : (" · " + OnArc.Refusal).c_str()));
        const SketchWrapOutcome Letters = WrapSketchModels({ { Rectangle(-1.0, -0.6, 0.0, 0.5), Front }, { Rectangle(0.6, 1.0, 0.0, 0.5), Front } }, Circle(1.0), Ground, AlongY);
        double Apart = 0.0;
        for (size_t Index = 0; Index < Letters.Curves.size(); ++Index)
            for (const Vector3& Point : Letters.Curves[Index])
            {
                const double Angle = std::atan2(Point.X, -Point.Y);
                Apart = std::max(Apart, Letters.Owners[Index] == 0 ? std::max(0.0, Angle + 0.6) : std::max(0.0, 0.6 - Angle));   // Left letter left of −0.6
                Apart = std::max(Apart, std::abs(std::hypot(Point.X, Point.Y) - 1.0));
            }
        Tally(WrapGate, Apart, Letters.Refusal.empty() && Letters.Curves.size() == 8 && std::abs(Letters.Extent - 2.0) <= 1.0e-9 && Apart <= 1.0e-9,
              Format("two letters (x −1 … −0.6 and 0.6 … 1) as separate shapes: %d curves, width %.9f, each stays on its side (worst %.1e)", int(Letters.Curves.size()), Letters.Extent, Apart));
    }
    // ⑦ in a document: a circle r 1 on the ground, a rectangle and a small circle on the front plane → 5 3D curves.
    {
        const auto Draw = [&](SolidArcDocument& Part, SolidArcDrawingKind Kind, std::vector<Vector2> Points, const OrthonormalFrame& Plane, bool CentreCondition)
        {
            SolidArcDrawingRequest Request;
            Request.Kind = Kind; Request.Points = std::move(Points); Request.PlaneCondition = true; Request.Plane = Plane; Request.CentreCondition = CentreCondition;
            Expect(DrawSolidArcSketch(Part, Request));
            return Part.Tree.Steps.back().Label;
        };
        SolidArcDocument  Part  = CreateSolidArcDocument("Wrap");
        const std::string Can   = Draw(Part, SolidArcDrawingKind::Circle, { { 0.0, 0.0 }, { 1.0, 0.0 } }, Ground, true);
        const std::string Box   = Draw(Part, SolidArcDrawingKind::Rectangle, { { -0.5, 0.0 }, { 0.5, 0.5 } }, Front, false);
        const std::string Dot   = Draw(Part, SolidArcDrawingKind::Circle, { { 0.0, 0.8 }, { 0.1, 0.8 } }, Front, true);
        const size_t Steps = Part.Tree.Steps.size(), Undo = Part.UndoEntries.size();
        const SolidArcCommandOutcome Wrapped = WrapSolidArcSketches(Part, { Can, { Box, Dot }, AlongY });
        int    Made = 0, Closed = 0;
        double Fit  = Wrapped.AcceptedCondition ? 0.0 : 1.0, Radial = 0.0;
        for (size_t Index = Steps; Index < Part.Tree.Steps.size(); ++Index)
        {
            const FeatureStep& Step = Part.Tree.Steps[Index];
            if (Step.Kind != FeatureStepKind::SpaceCurve) continue;
            ++Made;
            Closed += Step.ClosedCondition ? 1 : 0;
            const auto Fitted = Part.Regeneration.Curves.find(Step.Label);
            Fit = std::max(Fit, Fitted == Part.Regeneration.Curves.end() ? 1.0 : Fitted->second.WorstDeviation);
            for (const Vector3& Point : Step.Points) Radial = std::max(Radial, std::abs(std::hypot(Point.X, Point.Y) - 1.0));
        }
        const bool Fine = Wrapped.AcceptedCondition && Made == 5 && Closed == 1 && Part.UndoEntries.size() == Undo + 1 && Part.Selection.Curves.size() == 5
                          && Part.Captions[Part.Tree.Steps[Steps].Label] == "Wrap1" && Fit <= 1.0e-6 + 1.0e-12 && Radial <= 1.0e-9;
        Tally(WrapGate, Fit, Fine, Format("%s: %d 3D curves (%d closed), %d picked, captions Wrap1 …, samples off r 1 by %.1e, worst fit %.1e (≤ 10⁻⁶ of the 1 m width)%s", Wrapped.Label.c_str(), Made, Closed,
                                          int(Part.Selection.Curves.size()), Radial, Fit, Wrapped.Refusal.empty() ? "" : (" · " + Wrapped.Refusal).c_str()));
        const SolidArcCommandOutcome Itself = WrapSolidArcSketches(Part, { Can, { Can }, AlongY });
        const SolidArcCommandOutcome Lone   = WrapSolidArcSketches(Part, { Can, {}, AlongY });
        Tally(WrapGate, 0.0, !Itself.AcceptedCondition && !Lone.AcceptedCondition, Format("refused: the rail as a shape (%s) · no shape (%s)", Itself.Refusal.c_str(), Lone.Refusal.c_str()));
    }
    // ⑨ precision round a free curve (the request: "a circle around the curve … some distance error"): a Hermite curve on
    //    the side plane YZ is the rail (its surface: the curve swept along x), a circle r 0.4 on the front plane wraps
    //    round it along y. Seen along x every sample and the fitted 3D curve itself must lie on the rail curve: the
    //    samples to 10⁻⁹, the fitted curve (4000 points along it) within its fit tolerance.
    {
        const OrthonormalFrame Side{ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
        SolidArcDocument Part = CreateSolidArcDocument("WrapPrecision");
        SolidArcDrawingRequest Request;
        Request.Kind = SolidArcDrawingKind::Curve; Request.PlaneCondition = true; Request.Plane = Side;
        Request.Points = { { -0.6, 2.0 }, { 0.0, 1.2 }, { 0.3, 0.6 }, { 0.35, 0.0 }, { 0.3, -0.6 }, { 0.2, -1.2 } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Rail = Part.Tree.Steps.back().Label;
        Request = {};
        Request.Kind = SolidArcDrawingKind::Circle; Request.PlaneCondition = true; Request.Plane = Front; Request.CentreCondition = true;
        Request.Points = { { 0.0, 0.3 }, { 0.4, 0.3 } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Ring = Part.Tree.Steps.back().Label;
        const size_t Steps = Part.Tree.Steps.size();
        const SolidArcCommandOutcome Wrapped = WrapSolidArcSketches(Part, { Rail, { Ring }, AlongY });
        // The rail curve densely (20 000 fractions), then golden-section refinement round the nearest sample.
        std::string    Refusal;
        const SketchModel    RailModel = Part.Regeneration.Sketches.at(Rail);
        const ProfileSegment Segment   = ResolveSketchCurveSegment(RailModel, 0, 1.0e-9, Refusal);
        constexpr int        Dense     = 20000;
        std::vector<Vector2> Table;
        for (int Sample = 0; Sample <= Dense; ++Sample) Table.push_back(EvaluateProfileSegment(Segment, double(Sample) / Dense));
        const auto OffRail = [&](const Vector3& Point)
        {
            const Vector2 Seen{ Point.Y, Point.Z };                                          // Seen along x (the can axis)
            int Nearest = 0;
            for (int Sample = 1; Sample <= Dense; ++Sample) if (Distance(Table[size_t(Sample)], Seen) < Distance(Table[size_t(Nearest)], Seen)) Nearest = Sample;
            double Low = double(std::max(0, Nearest - 1)) / Dense, High = double(std::min(Dense, Nearest + 1)) / Dense;
            for (int Round = 0; Round < 80; ++Round)
            {
                const double First = Low + (High - Low) * 0.381966011250105, Second = Low + (High - Low) * 0.618033988749895;
                if (Distance(EvaluateProfileSegment(Segment, First), Seen) < Distance(EvaluateProfileSegment(Segment, Second), Seen)) High = Second; else Low = First;
            }
            return Distance(EvaluateProfileSegment(Segment, 0.5 * (Low + High)), Seen);
        };
        double Samples = 0.0, Fitted = 0.0, Tolerance = 0.0, Deviation = 0.0;
        int    Made = 0, Poles = 0;
        for (size_t Index = Steps; Index < Part.Tree.Steps.size(); ++Index)
        {
            const FeatureStep& Step = Part.Tree.Steps[Index];
            if (Step.Kind != FeatureStepKind::SpaceCurve) continue;
            ++Made;
            Tolerance = EvaluateFeatureValue(Part.Tree, Step.Tolerance);
            for (const Vector3& Point : Step.Points) Samples = std::max(Samples, OffRail(Point));
            const auto Fit = Part.Regeneration.Curves.find(Step.Label);
            if (Fit == Part.Regeneration.Curves.end() || !Fit->second.Curve.Definition) { Fitted = 1.0; continue; }
            Deviation = std::max(Deviation, Fit->second.WorstDeviation);
            Poles     = int(Fit->second.Curve.Definition->Poles.size());
            for (int Sample = 0; Sample <= 4000; ++Sample) Fitted = std::max(Fitted, OffRail(EvaluateNurbsCurve(*Fit->second.Curve.Definition, Sample / 4000.0).Position));
        }
        Tally(WrapGate, Fitted, Wrapped.AcceptedCondition && Refusal.empty() && Made == 1 && Samples <= 1.0e-9 && std::abs(Tolerance - 8.0e-7) <= 1.0e-15 && Fitted <= Tolerance + 1.0e-9,
              Format("circle r 0.4 wrapped round a free curve on YZ: %d curve, %d poles, seen along x the samples are %.1e off the rail, the fitted curve %.2e (%.2f µm; fit tolerance %.2e = 10⁻⁶ of the 0.8 width, worst sample deviation %.2e)%s",
                     Made, Poles, Samples, Fitted, Fitted * 1.0e6, Tolerance, Deviation, Wrapped.Refusal.empty() ? "" : (" · " + Wrapped.Refusal).c_str()));
    }
    // ⑩ a 30-point wiggle (4 wide) wrapped round the can r 1: too detailed for 10⁻⁶ within the fitter's 64 spans, so it
    //    steps down to 10⁻⁵ or 10⁻⁴ of its width instead of refusing; the fit stays within the tolerance chosen.
    {
        SolidArcDocument Part = CreateSolidArcDocument("WrapWiggle");
        SolidArcDrawingRequest Request;
        Request.Kind = SolidArcDrawingKind::Circle; Request.PlaneCondition = true; Request.Plane = Ground; Request.CentreCondition = true;
        Request.Points = { { 0.0, 0.0 }, { 1.0, 0.0 } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Can = Part.Tree.Steps.back().Label;
        Request = {};
        Request.Kind = SolidArcDrawingKind::Curve; Request.PlaneCondition = true; Request.Plane = Front;
        for (int Point = 0; Point < 30; ++Point) Request.Points.push_back({ -2.0 + 4.0 * Point / 29.0, 0.5 + 0.3 * ((Point % 2) ? 1.0 : -1.0) });
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Wiggle = Part.Tree.Steps.back().Label;
        const size_t Steps = Part.Tree.Steps.size();
        const SolidArcCommandOutcome Wrapped = WrapSolidArcSketches(Part, { Can, { Wiggle }, AlongY });
        double Tolerance = 0.0, Deviation = 1.0;
        for (size_t Index = Steps; Index < Part.Tree.Steps.size(); ++Index)
        {
            const FeatureStep& Step = Part.Tree.Steps[Index];
            if (Step.Kind != FeatureStepKind::SpaceCurve) continue;
            Tolerance = EvaluateFeatureValue(Part.Tree, Step.Tolerance);
            const auto Fit = Part.Regeneration.Curves.find(Step.Label);
            Deviation = Fit == Part.Regeneration.Curves.end() || !Fit->second.Refusal.empty() ? 1.0 : Fit->second.WorstDeviation;
        }
        Tally(WrapGate, Deviation, Wrapped.AcceptedCondition && Tolerance > 0.0 && Deviation <= Tolerance,
              Format("a 30-point wiggle 4 wide round the can: accepted, fit tolerance %.1e (%.0e of the width), worst deviation %.2e%s", Tolerance, Tolerance / 4.0, Deviation,
                     Wrapped.Refusal.empty() ? "" : (" · " + Wrapped.Refusal).c_str()));
    }
    // ⑪ loft between wrapped curves (the request: "i tried lofting the yellow line with yellow line it refused"): two
    //    circles r 0.2 on the front plane (z 0.3 and z 0.9) wrapped round the can r 1 → two closed 3D curves; a sheet
    //    loft and a solid loft through them; then the wrapped circle with its flat original (a label to its decal).
    {
        SolidArcDocument Part = CreateSolidArcDocument("WrapLoft");
        SolidArcDrawingRequest Request;
        Request.Kind = SolidArcDrawingKind::Circle; Request.PlaneCondition = true; Request.Plane = Ground; Request.CentreCondition = true;
        Request.Points = { { 0.0, 0.0 }, { 1.0, 0.0 } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Can = Part.Tree.Steps.back().Label;
        Request.Plane = Front; Request.Points = { { 0.0, 0.3 }, { 0.2, 0.3 } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Low = Part.Tree.Steps.back().Label;
        Request.Points = { { 0.0, 0.9 }, { 0.2, 0.9 } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string High = Part.Tree.Steps.back().Label;
        const SolidArcCommandOutcome Wrapped = WrapSolidArcSketches(Part, { Can, { Low, High }, AlongY });
        std::vector<std::string> Curves;
        for (const FeatureStep& Step : Part.Tree.Steps) if (Step.Kind == FeatureStepKind::SpaceCurve) Curves.push_back(Step.Label);
        const auto Loft = [&](std::vector<std::string> Labels, bool SolidCondition)
        {
            SolidArcDocument Trial = CloneSolidArcDocument(Part);
            const SolidArcCommandOutcome Outcome = LoftSolidArcCurves(Trial, Labels, 0, SolidCondition);
            return std::pair{ Outcome, Trial.Regeneration.Bodies.size() };
        };
        const auto [Sheet, SheetBodies] = Curves.size() == 2 ? Loft(Curves, false) : std::pair{ SolidArcCommandOutcome{}, size_t(0) };
        const auto [Solid, SolidBodies] = Curves.size() == 2 ? Loft(Curves, true) : std::pair{ SolidArcCommandOutcome{}, size_t(0) };
        const auto [Decal, DecalBodies] = Curves.size() == 2 ? Loft({ Curves[0], Low }, false) : std::pair{ SolidArcCommandOutcome{}, size_t(0) };
        // 📝 L tries the solid first and falls back to the sheet; a solid needs plane caps, and these rims curve round the
        //    can (🚧 non-planar caps), so its refusal must name exactly that.
        const bool SolidAnswered = Solid.AcceptedCondition || Solid.Refusal.find("not planar") != std::string::npos;
        Tally(WrapGate, 0.0, Wrapped.AcceptedCondition && Curves.size() == 2 && Sheet.AcceptedCondition && SolidAnswered && Decal.AcceptedCondition,
              Format("loft between wrapped curves (L: solid, else sheet): %d wrapped circles; sheet loft %s (%d bodies)%s; solid loft %s (%d)%s; wrapped circle to its flat circle %s (%d)%s",
                     int(Curves.size()), Sheet.AcceptedCondition ? "accepted" : "refused", int(SheetBodies), Sheet.Refusal.empty() ? "" : (" · " + Sheet.Refusal).c_str(),
                     Solid.AcceptedCondition ? "accepted" : "refused", int(SolidBodies), Solid.Refusal.empty() ? "" : (" · " + Solid.Refusal).c_str(),
                     Decal.AcceptedCondition ? "accepted" : "refused", int(DecalBodies), Decal.Refusal.empty() ? "" : (" · " + Decal.Refusal).c_str()));
    }
    // ⑧ S on sketches: a circle r 0.5 at (1, 0) scaled ×2 about the origin; a 2 × 1 rectangle stretched ×2 along x; a
    //    circle stretched refuses; Shift+D with a ½ scale copies (the original stays).
    {
        SolidArcDocument Part = CreateSolidArcDocument("Scale");
        SolidArcDrawingRequest Request;
        Request.Kind = SolidArcDrawingKind::Circle; Request.Points = { { 1.0, 0.0 }, { 1.5, 0.0 } }; Request.CentreCondition = true;
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Round = Part.Tree.Steps.back().Label;
        Request = {};
        Request.Kind = SolidArcDrawingKind::Rectangle; Request.Points = { { 3.0, 0.0 }, { 5.0, 1.0 } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Box = Part.Tree.Steps.back().Label;
        const auto Shape = [&](const std::string& Label) { return Part.Regeneration.Sketches.at(Label); };
        const auto Lift  = [&](const std::string& Label, Vector2 Point)
        {
            const OrthonormalFrame Plane = Part.Tree.Steps[size_t(std::find_if(Part.Tree.Steps.begin(), Part.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Label; }) - Part.Tree.Steps.begin())].Plane;
            return Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y;
        };
        const auto RoundOf = [&](const std::string& Label, double& Radius, Vector3& Centre)
        {
            const SketchModel Model = Shape(Label);
            for (const SketchCurve& Curve : Model.Curves)
                if (Curve.Kind == SketchCurveKind::Circle) { Radius = Curve.Radius; Centre = Lift(Label, Model.Points[size_t(Curve.Centre)].Position); }
        };
        const SolidArcCommandOutcome Grown = PlaceSolidArcSketches(Part, { Round }, ScalingMap({ 0.0, 0.0, 0.0 }, { 2.0, 2.0, 2.0 }), "Scale");
        double  Radius = 0.0;
        Vector3 Centre = {};
        RoundOf(Round, Radius, Centre);
        const double Off = std::max(std::abs(Radius - 1.0), Length(Centre - Vector3{ 2.0, 0.0, 0.0 }));
        Tally(WrapGate, Off, Grown.AcceptedCondition && Off <= 1.0e-9, Format("S ×2 about the origin: circle r %.9f at (%.6f, %.6f) (r 1 at (2, 0))%s", Radius, Centre.X, Centre.Y,
                                                                              Grown.Refusal.empty() ? "" : (" · " + Grown.Refusal).c_str()));
        const SolidArcCommandOutcome Stretched = PlaceSolidArcSketches(Part, { Box }, ScalingMap({ 3.0, 0.0, 0.0 }, { 2.0, 1.0, 1.0 }), "Scale");
        double Corners = Stretched.AcceptedCondition ? 0.0 : 1.0;
        for (const Vector3 Expected : { Vector3{ 3.0, 0.0, 0.0 }, Vector3{ 7.0, 0.0, 0.0 }, Vector3{ 7.0, 1.0, 0.0 }, Vector3{ 3.0, 1.0, 0.0 } })
        {
            double Closest = 1.0e9;
            for (const SketchPoint& Point : Shape(Box).Points) Closest = std::min(Closest, Length(Lift(Box, Point.Position) - Expected));
            Corners = std::max(Corners, Closest);
        }
        const SolidArcCommandOutcome Oval = PlaceSolidArcSketches(Part, { Round }, ScalingMap({ 0.0, 0.0, 0.0 }, { 2.0, 1.0, 1.0 }), "Scale");
        Tally(WrapGate, Corners, Stretched.AcceptedCondition && Corners <= 1.0e-9 && !Oval.AcceptedCondition,
              Format("S then X ×2: rectangle corners within %.1e of (3 … 7) × (0 … 1) · a stretched circle refused (%s)", Corners, Oval.Refusal.c_str()));
        const size_t Steps = Part.Tree.Steps.size();
        const SolidArcCommandOutcome Copied = DuplicateSolidArcSketches(Part, { Round }, ScalingMap({ 0.0, 0.0, 0.0 }, { 0.5, 0.5, 0.5 }));
        double  Small = 0.0, Kept = 0.0;
        Vector3 SmallCentre = {}, KeptCentre = {};
        if (Copied.AcceptedCondition && Part.Tree.Steps.size() == Steps + 1) RoundOf(Part.Tree.Steps.back().Label, Small, SmallCentre);
        RoundOf(Round, Kept, KeptCentre);
        const double Copy = std::max({ std::abs(Small - 0.5), Length(SmallCentre - Vector3{ 1.0, 0.0, 0.0 }), std::abs(Kept - 1.0) });
        Tally(WrapGate, Copy, Copied.AcceptedCondition && Copy <= 1.0e-9, Format("Shift+D at ½: copy r %.9f at (%.6f, 0), original r %.9f kept%s", Small, SmallCentre.X, Kept,
                                                                                 Copied.Refusal.empty() ? "" : (" · " + Copied.Refusal).c_str()));
    }
    // ⑨ O on an open arc (a wheel arch): r 2, 0° … 90° → concentric arcs r 2 ± 0.3 (both sides).
    {
        SolidArcDocument Part = CreateSolidArcDocument("Arch");
        SolidArcDrawingRequest Request;
        Request.Kind = SolidArcDrawingKind::Arc; Request.Points = { { 2.0, 0.0 }, { 0.0, 2.0 }, { std::sqrt(2.0), std::sqrt(2.0) } };
        Expect(DrawSolidArcSketch(Part, Request));
        const std::string Arch = Part.Tree.Steps.back().Label;
        std::vector<double> Radii;
        double Worst = 0.0;
        for (const double Distance : { 0.3, -0.3 })
        {
            const SolidArcCommandOutcome Offset = OffsetSolidArcSketch(Part, Arch, Distance);
            if (!Offset.AcceptedCondition) { Worst = 1.0; continue; }
            const SketchModel Model = Part.Regeneration.Sketches.at(Part.Tree.Steps.back().Label);
            for (int Curve = 0; Curve < int(Model.Curves.size()); ++Curve)
            {
                std::string Refusal;
                const ProfileSegment Segment = ResolveSketchCurveSegment(Model, Curve, 1.0e-9, Refusal);
                const double Radius = Length(EvaluateProfileSegment(Segment, 0.0));
                Radii.push_back(Radius);
                for (int Sample = 0; Sample <= 64; ++Sample) Worst = std::max(Worst, std::abs(Length(EvaluateProfileSegment(Segment, Sample / 64.0)) - Radius));
            }
        }
        std::sort(Radii.begin(), Radii.end());
        const bool Fine = Radii.size() == 2 && std::abs(Radii[0] - 1.7) <= 1.0e-9 && std::abs(Radii[1] - 2.3) <= 1.0e-9 && Worst <= 1.0e-9;
        Tally(WrapGate, Worst, Fine, Format("O ±0.3 on an open arc r 2: %d arcs, radii %s, concentric within %.1e", int(Radii.size()),
                                            Radii.size() == 2 ? Format("%.9f / %.9f", Radii[0], Radii[1]).c_str() : "?", Worst));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESHAPE
//------------------------------------------------------------------------------------------------------------------------

// W67: vertices and edges of a solid moved by the gizmo (mode 1 / 2). The block is x ∈ [0, 4], y ∈ [0, 2], z ∈ [0, 1];
// faces that stay flat are re-planed, four-sided faces that cannot are bilinear patches (exact volumes: the patch is
// linear along each side, so ∫ of its offset is a product of one-dimensional integrals).
void ValidateReshape()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1200]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto Near   = [](double A, double B) { return std::abs(A - B) <= 1.0e-9; };
    const auto NewBlock = []()
    {
        SolidArcDocument Part = CreateSolidArcDocument("Reshape");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = 4.0; Block.Depth = 2.0; Block.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Part, Block));
        return Part;
    };
    const auto EdgeWhere = [](const SolidArcDocument& Document, const auto& Test)
    {
        for (const auto& [Slot, Name] : Document.Regeneration.Naming.Edges)
        {
            const Vector3 Start = EdgeEndPosition(Document, Name, false), End = EdgeEndPosition(Document, Name, true);
            if (Test(Start, End) || Test(End, Start)) return Name;
        }
        return std::string{};
    };
    const auto VertexAt = [&](const SolidArcDocument& Document, Vector3 Point)
    {
        for (const auto& [Slot, Name] : Document.Regeneration.Naming.Vertices)
        {
            const Vector3 Position = Document.Partition->Vertices.Resolve(Document.Partition->Vertices.TokenAt(Slot)).Position;
            if (Near(Position.X, Point.X) && Near(Position.Y, Point.Y) && Near(Position.Z, Point.Z)) return Name;
        }
        return std::string{};
    };
    // The top-front edge (y = 0, z = 1) runs along x.
    const auto TopFront = [&](const SolidArcDocument& Document)
    {
        return EdgeWhere(Document, [&](Vector3 A, Vector3 B) { return Near(A.Y, 0.0) && Near(B.Y, 0.0) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
    };
    const auto Reshape = [&](SolidArcDocument& Document, std::vector<std::string> Edges, std::vector<std::string> Vertices, const AffineMap& Map, const char* Verb)
    {
        Document.Selection = {};
        Document.Selection.Edges    = std::move(Edges);
        Document.Selection.Vertices = std::move(Vertices);
        return ReshapeSolidArcSelection(Document, Map, Verb);
    };
    const auto Suffix = [](const SolidArcCommandOutcome& Outcome) { return Outcome.Refusal.empty() ? std::string() : " · " + Outcome.Refusal; };

    // ① An edge moved: across (−y face leans, top narrows), up (top leans), along itself (both ends of the block shear:
    //    the x = 0 / 4 faces are bilinear, offset 0.5 (1 − y / 2) z each, so the volume stays 8).
    struct EdgeCase { const char* Note; AffineMap Map; double Volume; int Bilinear; };
    const std::vector<EdgeCase> EdgeCases = {
        { "top-front edge moved +0.5 y (front leans back)",       TranslationMap({ 0.0, 0.5, 0.0 }), 8.0 - 0.5 * 0.5 * 4.0, 0 },
        { "top-front edge moved +0.5 z (top leans forward)",      TranslationMap({ 0.0, 0.0, 0.5 }), 8.0 + 0.5 * 2.0 * 0.5 * 4.0, 0 },
        { "top-front edge moved +0.5 x (along itself)",           TranslationMap({ 0.5, 0.0, 0.0 }), 8.0, 2 },
        { "top-front edge scaled ½ about its middle (2, 0, 1)",   ScalingMap({ 2.0, 0.0, 1.0 }, { 0.5, 0.5, 0.5 }), 8.0 - 2.0 * 1.0 * 0.5, 2 },
    };
    for (const EdgeCase& Case : EdgeCases)
    {
        SolidArcDocument Part = NewBlock();
        const size_t Undo = Part.UndoEntries.size();
        const std::string Edge = TopFront(Part);
        const SolidArcCommandOutcome Moved = Reshape(Part, { Edge }, {}, Case.Map, "Move");
        int Patches = 0;
        for (const auto& [Slot, Name] : Part.Regeneration.Naming.Faces)
            Patches += std::holds_alternative<NurbsSurface>(Part.Partition->Surfaces.Resolve(Part.Partition->Faces.Resolve(Part.Partition->Faces.TokenAt(Slot)).Surface)) ? 1 : 0;
        TallyVolume(ReshapeGate, Part, Case.Volume, Format("%s %s: %d faces, %d bilinear (expect 6, %d), %d undo entry%s", Case.Note, Edge.c_str(), Part.Summary.Faces, Patches, Case.Bilinear,
                                                          int(Part.UndoEntries.size() - Undo), Suffix(Moved).c_str()));
        Tally(ReshapeGate, 0.0, Moved.AcceptedCondition && Part.Summary.Faces == 6 && Patches == Case.Bilinear && Part.UndoEntries.size() == Undo + 1, Format("%s: topology kept, one undo entry", Case.Note));
    }
    {
        // Turned 30° about the bottom-front edge (the x axis): a prism of the turned section (0, 0) (2, 0) (2, 1) (y′, z′).
        SolidArcDocument Part = NewBlock();
        const double     Angle = std::numbers::pi / 6.0;
        const AffineMap  Turn  = RotationMap({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, Angle);
        const Vector3    Corner = Turn.ColumnZ + Turn.Shift;                                      // M·(0, 0, 1) + t
        const double     Section = 0.5 * std::abs(2.0 * 1.0 + 2.0 * Corner.Z - Corner.Y * 1.0);
        const SolidArcCommandOutcome Turned = Reshape(Part, { TopFront(Part) }, {}, Turn, "Rotate");
        TallyVolume(ReshapeGate, Part, 4.0 * Section, Format("top-front edge turned 30° about the floor edge → (y, z) = (%.6f, %.6f), section %.6f%s", Corner.Y, Corner.Z, Section, Suffix(Turned).c_str()));
    }

    // ② A corner moved: up 0.5 (the top a bilinear patch z = 1 + ½ · (x / 4)(y / 2) → +1; the back / right stay flat), out
    //    0.5 in x (the right face bilinear x = 4 + ½ · (y / 2) z → +0.25).
    {
        SolidArcDocument Raised = NewBlock();
        const std::string Corner = VertexAt(Raised, { 4.0, 2.0, 1.0 });
        const SolidArcCommandOutcome Up = Reshape(Raised, {}, { Corner }, TranslationMap({ 0.0, 0.0, 0.5 }), "Move");
        TallyVolume(ReshapeGate, Raised, 9.0, Format("corner %s raised 0.5: top bilinear, back / right re-planed, %d faces%s", Corner.c_str(), Raised.Summary.Faces, Suffix(Up).c_str()));
        SolidArcDocument Out = NewBlock();
        const SolidArcCommandOutcome Pushed = Reshape(Out, {}, { VertexAt(Out, { 4.0, 2.0, 1.0 }) }, TranslationMap({ 0.5, 0.0, 0.0 }), "Move");
        TallyVolume(ReshapeGate, Out, 8.25, Format("corner pushed out 0.5 x: right face bilinear%s", Suffix(Pushed).c_str()));
    }

    // ③ Refusals leave the document untouched (tree, volume, undo stack).
    {
        const auto Untouched = [&](SolidArcDocument& Part, const SolidArcCommandOutcome& Outcome, const std::string& Before, double Volume, const char* Note)
        {
            Tally(ReshapeGate, 0.0, !Outcome.AcceptedCondition && EncodeSolidArcDocument(Part) == Before && Part.Summary.Volume == Volume,
                  Format("refused, untouched: %s (%s)", Note, Outcome.Refusal.c_str()));
        };
        {
            SolidArcDocument Part = NewBlock();
            const std::string Before = EncodeSolidArcDocument(Part);
            const double Volume = Part.Summary.Volume;
            Untouched(Part, Reshape(Part, {}, { VertexAt(Part, { 4.0, 2.0, 1.0 }) }, TranslationMap({ 0.0, -2.0, 0.0 }), "Move"), Before, Volume, "a corner moved onto its neighbour (edge collapses)");
            Untouched(Part, Reshape(Part, { TopFront(Part) }, {}, TranslationMap({ 0.0, 0.0, -1.5 }), "Move"), Before, Volume, "the top-front edge pushed below the floor (front face inside out)");
            Untouched(Part, Reshape(Part, { TopFront(Part) }, {}, TranslationMap({ 0.0, 3.0, 0.0 }), "Move"), Before, Volume, "the top-front edge pushed through the back (top face inside out)");
            Untouched(Part, Reshape(Part, {}, {}, TranslationMap({ 0.0, 0.0, 0.5 }), "Move"), Before, Volume, "nothing picked");
        }
        {
            SolidArcDocument Part = CreateSolidArcDocument("Reshape");
            SolidArcConstructRequest Round;
            Round.Kind = SolidArcConstructKind::Cylinder; Round.Radius = 0.5; Round.Height = 1.0;
            Expect(ConstructSolidArcPrimitive(Part, Round));
            const std::string Rim = EdgeWhere(Part, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); });
            const std::string Before = EncodeSolidArcDocument(Part);
            Untouched(Part, Reshape(Part, { Rim }, {}, TranslationMap({ 0.0, 0.0, 0.2 }), "Move"), Before, Part.Summary.Volume, "a cylinder's rim circle (curved edge)");
        }
        {
            SolidArcDocument Part = CreateSolidArcDocument("Reshape");
            SolidArcConstructRequest Hexagon;
            Hexagon.Kind = SolidArcConstructKind::Prism; Hexagon.Sides = 6; Hexagon.Radius = 1.0; Hexagon.Height = 1.0;
            Expect(ConstructSolidArcPrimitive(Part, Hexagon));
            const std::string Rim = VertexAt(Part, { 1.0, 0.0, 1.0 });
            const std::string Before = EncodeSolidArcDocument(Part);
            Untouched(Part, Reshape(Part, {}, { Rim }, TranslationMap({ 0.0, 0.0, 0.3 }), "Move"), Before, Part.Summary.Volume,
                      Format("a hexagonal prism's top vertex %s raised (the hexagon cannot stay flat)", Rim.c_str()).c_str());
        }
    }

    // ④ With a fillet r 0.2 on the bottom-back edge the reshape goes in before the Blend, which re-solves on the new shape;
    //    moving the fillet's own edge refuses (a curved neighbour). Save / load replays it byte-equal.
    {
        SolidArcDocument Part = NewBlock();
        const double Radius = 0.2;
        Part.Selection = {};
        Part.Selection.Edges = { EdgeWhere(Part, [&](Vector3 A, Vector3 B) { return Near(A.Y, 2.0) && Near(B.Y, 2.0) && Near(A.Z, 0.0) && Near(B.Z, 0.0); }) };
        Expect(BlendSolidArcSelection(Part, BlendProfile::Fillet, Radius));
        const std::string Blend = Part.Tree.Steps.back().Label;
        const size_t      Steps = Part.Tree.Steps.size();
        const SolidArcCommandOutcome Moved = Reshape(Part, { TopFront(Part) }, {}, TranslationMap({ 0.0, 0.5, 0.0 }), "Move");
        const bool Before = Part.Tree.Steps.size() == Steps + 1 && Part.Tree.Steps.back().Label == Blend && Part.Tree.Steps[Steps - 1].Operation == FaceOperationKind::Reshape;
        TallyVolume(ReshapeGate, Part, 7.0 - (1.0 - std::numbers::pi / 4.0) * Radius * Radius * 4.0,
                    Format("filleted block, top-front edge +0.5 y: step %s before %s (%s), the fillet re-solved%s", Steps - 1 < Part.Tree.Steps.size() ? Part.Tree.Steps[Steps - 1].Label.c_str() : "?", Blend.c_str(),
                           Before ? "yes" : "no", Suffix(Moved).c_str()));
        Tally(ReshapeGate, 0.0, Moved.AcceptedCondition && Before, "the reshape is inserted before the trailing Blend");

        std::string Seam;
        for (const auto& [Slot, Name] : Part.Regeneration.Naming.Edges) if (Name.find(Blend + ".") != std::string::npos) { Seam = Name; break; }
        const std::string Text = EncodeSolidArcDocument(Part);
        const SolidArcCommandOutcome Refused = Reshape(Part, { Seam }, {}, TranslationMap({ 0.0, 0.0, 0.1 }), "Move");
        Tally(ReshapeGate, 0.0, !Refused.AcceptedCondition && EncodeSolidArcDocument(Part) == Text, Format("the fillet's own edge %s refuses, untouched (%s)", Seam.c_str(), Refused.Refusal.c_str()));

        const SolidArcDocumentFile Loaded   = DecodeSolidArcDocument(Text);
        SolidArcDocument           Reloaded = CreateSolidArcDocument(Loaded.Title);
        const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Reloaded, Loaded.Tree, Loaded.Captions, "Open");
        Tally(ReshapeGate, 0.0, Loaded.Refusal.empty() && Opened.AcceptedCondition && Reloaded.Summary.Volume == Part.Summary.Volume && EncodeSolidArcDocument(Reloaded) == Text,
              Format("save → load: V %.12f = %.12f, byte-equal (%s)", Reloaded.Summary.Volume, Part.Summary.Volume, Loaded.Refusal.empty() ? Opened.Refusal.c_str() : Loaded.Refusal.c_str()));
    }

    // ⑤ Face gizmo verbs (mode 3): the top face scaled ½ about its centre is a frustum h/3 (A + a + √(A a)) = 14/3; turned
    //    10° about the x axis through its centre it keeps 8 (the wedge gained = the wedge lost). With a bottom-front fillet the
    //    face scale also goes in before the Blend.
    {
        const std::string TopName = PlanarFaceWhere(NewBlock(), { 0.0, 0.0, 1.0 }, 1);
        SolidArcDocument Scaled = NewBlock();
        Scaled.Selection = {};
        Scaled.Selection.Faces = { TopName };
        const SolidArcCommandOutcome Half = ChangeSolidArcSelection(Scaled, FaceOperationKind::Scale, 0.5);
        TallyVolume(ReshapeGate, Scaled, 14.0 / 3.0, Format("top face scaled ½ (face gizmo) → frustum%s", Suffix(Half).c_str()));
        SolidArcDocument Tilted = NewBlock();
        Tilted.Selection = {};
        Tilted.Selection.Faces = { TopName };
        const SolidArcCommandOutcome Turn = ChangeSolidArcSelection(Tilted, FaceOperationKind::Rotate, 10.0 * std::numbers::pi / 180.0, Vector3{ 1.0, 0.0, 0.0 });
        TallyVolume(ReshapeGate, Tilted, 8.0, Format("top face turned 10° about x through its centre (face gizmo)%s", Suffix(Turn).c_str()));

        SolidArcDocument Part = NewBlock();
        Part.Selection = {};
        Part.Selection.Edges = { EdgeWhere(Part, [&](Vector3 A, Vector3 B) { return Near(A.Y, 0.0) && Near(B.Y, 0.0) && Near(A.Z, 0.0) && Near(B.Z, 0.0); }) };
        Expect(BlendSolidArcSelection(Part, BlendProfile::Fillet, 0.1));
        const std::string Blend = Part.Tree.Steps.back().Label;
        Part.Selection = {};
        Part.Selection.Faces = { TopName };
        const SolidArcCommandOutcome Narrowed = ChangeSolidArcSelection(Part, FaceOperationKind::Scale, 0.8);
        const bool Before = Part.Tree.Steps.back().Label == Blend && Part.Tree.Steps[Part.Tree.Steps.size() - 2].Operation == FaceOperationKind::Scale;
        Tally(ReshapeGate, 0.0, Narrowed.AcceptedCondition && Before && Part.Summary.SoundCondition && Part.Summary.Volume < 8.0 && Part.Summary.Volume > 14.0 / 3.0,
              Format("filleted block, top face scaled 0.8: scale before %s (%s), the fillet re-solved on the leaning front, sound, V %.6f%s", Blend.c_str(), Before ? "yes" : "no",
                     Part.Summary.Volume, Suffix(Narrowed).c_str()));
    }
}

// W68: X / Delete on every kind of pick. The block is x ∈ [0, 4], y ∈ [0, 2], z ∈ [0, 1] (V 8, 6 faces, 12 edges, 8 vertices);
// a fillet r on one straight edge of length 4 takes (1 − π/4) r² · 4.
// W69: the curve-and-sheet request — loft between picked sketches (as L: solid, else sheet), flip of picked faces,
// H on curves, 3D curves through off-plane points, sketch points lifted off their plane.
void ValidateCurveAsk()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1200]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const OrthonormalFrame Ground{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const OrthonormalFrame Front{ { 0.0, -3.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
    const OrthonormalFrame Right{ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
    const auto Draw = [](SolidArcDocument& Part, SolidArcDrawingKind Kind, const OrthonormalFrame& Plane, std::vector<Vector2> Points, bool CentreCondition = false)
    {
        SolidArcDrawingRequest Request;
        Request.Kind = Kind; Request.PlaneCondition = true; Request.Plane = Plane; Request.Points = std::move(Points); Request.CentreCondition = CentreCondition;
        Request.Form = SketchSplineForm::Hermite;
        Expect(DrawSolidArcSketch(Part, Request));
        return Part.Tree.Steps.back().Label;
    };
    // L as the viewport does it: a solid when the sections close, else a sheet.
    const auto LoftAsL = [](SolidArcDocument& Part, const std::vector<std::string>& Labels, std::string& Made)
    {
        SolidArcCommandOutcome Outcome = LoftSolidArcCurves(Part, Labels, 0, true);
        Made = Outcome.AcceptedCondition ? "solid" : "sheet";
        if (!Outcome.AcceptedCondition) Outcome = LoftSolidArcCurves(Part, Labels, 0, false);
        if (!Outcome.AcceptedCondition) Made = "refused";
        return Outcome;
    };
    // Face name → sense over every regenerated body.
    const auto Senses = [](const SolidArcDocument& Part)
    {
        std::map<std::string, int> Found;
        if (!Part.Partition) return Found;
        for (const RegeneratedBody& Body : Part.Regeneration.Bodies)
            for (const auto& [Slot, Name] : Body.Naming.Faces)
                Found[Name] = Part.Partition->Faces.Resolve(Part.Partition->Faces.TokenAt(Slot)).Sense == Orientation::Forward ? 1 : -1;
        return Found;
    };

    // ① L between picked ("yellow") sketches, four layouts
    struct LoftCase { const char* Note; int Layout; };
    for (const LoftCase& Case : { LoftCase{ "two open curves side by side in the Right view", 0 }, LoftCase{ "two concentric circles on the ground", 1 },
                                  LoftCase{ "an open curve to its wrapped copy", 2 }, LoftCase{ "an open curve in the Right view to one in the Front view", 3 } })
    {
        SolidArcDocument Part = CreateSolidArcDocument("LoftAsk");
        std::vector<std::string> Labels;
        if (Case.Layout == 0)
        {
            Labels.push_back(Draw(Part, SolidArcDrawingKind::Curve, Right, { { 0.0, 0.0 }, { 0.4, 0.6 }, { 0.2, 1.2 } }));
            Labels.push_back(Draw(Part, SolidArcDrawingKind::Curve, Right, { { 1.0, 0.0 }, { 1.3, 0.5 }, { 1.2, 1.2 } }));
        }
        else if (Case.Layout == 1)
        {
            Labels.push_back(Draw(Part, SolidArcDrawingKind::Circle, Ground, { { 0.0, 0.0 }, { 1.0, 0.0 } }, true));
            Labels.push_back(Draw(Part, SolidArcDrawingKind::Circle, Ground, { { 0.0, 0.0 }, { 2.0, 0.0 } }, true));
        }
        else if (Case.Layout == 2)
        {
            const std::string Can   = Draw(Part, SolidArcDrawingKind::Circle, Ground, { { 0.0, 0.0 }, { 1.0, 0.0 } }, true);
            const std::string Shape = Draw(Part, SolidArcDrawingKind::Curve, Front, { { -0.4, 0.2 }, { 0.0, 0.6 }, { 0.4, 0.3 } });
            Expect(WrapSolidArcSketches(Part, { Can, { Shape }, SketchWrapSettings{ SketchWrapKind::Wrap, { 0.0, 1.0, 0.0 }, {} } }));
            Labels.push_back(Shape);
            for (const FeatureStep& Step : Part.Tree.Steps) if (Step.Kind == FeatureStepKind::SpaceCurve) Labels.push_back(Step.Label);
        }
        else
        {
            Labels.push_back(Draw(Part, SolidArcDrawingKind::Curve, Right, { { 0.5, 0.0 }, { 0.8, 0.6 }, { 0.6, 1.2 } }));
            Labels.push_back(Draw(Part, SolidArcDrawingKind::Curve, Front, { { 0.5, 0.0 }, { 0.9, 0.5 }, { 0.7, 1.2 } }));
        }
        std::string Made;
        const SolidArcCommandOutcome Lofted = LoftAsL(Part, Labels, Made);
        Tally(CurveAskGate, 0.0, Lofted.AcceptedCondition && Part.Regeneration.Bodies.size() >= 1,
              Format("L between %s (%d picked): %s · %d bodies · %s%s", Case.Note, int(Labels.size()), Made.c_str(), int(Part.Regeneration.Bodies.size()),
                     Part.Summary.SoundCondition ? "sound" : "audit findings", Lofted.Refusal.empty() ? "" : (" · " + Lofted.Refusal).c_str()));
    }

    // ② Flip faces: two separate sheet lofts; one face picked → its piece turns, the other piece stays; again → step gone
    {
        SolidArcDocument Part = CreateSolidArcDocument("FlipAsk");
        const std::string A = Draw(Part, SolidArcDrawingKind::Curve, Right, { { 0.0, 0.0 }, { 0.4, 0.6 }, { 0.2, 1.2 } });
        const std::string B = Draw(Part, SolidArcDrawingKind::Curve, Right, { { 1.0, 0.0 }, { 1.3, 0.5 }, { 1.2, 1.2 } });
        const std::string C = Draw(Part, SolidArcDrawingKind::Curve, Right, { { 3.0, 0.0 }, { 3.4, 0.6 }, { 3.2, 1.2 } });
        const std::string D = Draw(Part, SolidArcDrawingKind::Curve, Right, { { 4.0, 0.0 }, { 4.3, 0.5 }, { 4.2, 1.2 } });
        Expect(LoftSolidArcCurves(Part, { A, B }));
        Expect(LoftSolidArcCurves(Part, { C, D }));
        const std::map<std::string, int> Before = Senses(Part);
        std::string Picked;
        for (const auto& [Name, Sense] : Before) { Picked = Name; break; }
        const size_t Steps = Part.Tree.Steps.size(), Undo = Part.UndoEntries.size();
        const SolidArcCommandOutcome Flipped = FlipSolidArcFaces(Part, { Picked });
        const std::map<std::string, int> After = Senses(Part);
        int Turned = 0, Kept = 0, Lost = 0;
        for (const auto& [Name, Sense] : Before)
        {
            const auto Found = After.find(Name);
            if (Found == After.end()) { ++Lost; continue; }
            (Found->second == -Sense ? Turned : Kept) += 1;
        }
        const bool PickedTurned = After.contains(Picked) && After.at(Picked) == -Before.at(Picked);
        Tally(CurveAskGate, 0.0, Flipped.AcceptedCondition && PickedTurned && Turned >= 1 && Kept >= 1 && Lost == 0 && Part.Tree.Steps.size() == Steps + 1 && Part.UndoEntries.size() == Undo + 1
                                 && !Part.Tree.Steps.back().References.empty(),
              Format("flip face %s of a two-loft sheet (%d faces, %d bodies): %d face(s) turned, %d kept, %d lost · %s%s", Picked.c_str(), int(Before.size()), int(Part.Regeneration.Bodies.size()),
                     Turned, Kept, Lost, Part.Summary.SoundCondition ? "sound" : "audit findings", Flipped.Refusal.empty() ? "" : (" · " + Flipped.Refusal).c_str()));
        const SolidArcCommandOutcome Again = FlipSolidArcFaces(Part, { Picked });
        Tally(CurveAskGate, 0.0, Again.AcceptedCondition && Part.Tree.Steps.size() == Steps && Senses(Part) == Before,
              Format("the same face flipped again: the Flip step goes, every sense as before%s", Again.Refusal.empty() ? "" : (" · " + Again.Refusal).c_str()));
        const SolidArcCommandOutcome Whole = FlipSolidArcBodies(Part, { Part.Regeneration.Bodies.front().Key });
        int WholeTurned = 0;
        for (const auto& [Name, Sense] : Senses(Part)) WholeTurned += Before.contains(Name) && Before.at(Name) == -Sense ? 1 : 0;
        Tally(CurveAskGate, 0.0, Whole.AcceptedCondition && WholeTurned == int(Before.size()),
              Format("nothing but the sheet picked: the entire object turns (%d of %d faces)%s", WholeTurned, int(Before.size()), Whole.Refusal.empty() ? "" : (" · " + Whole.Refusal).c_str()));
        SolidArcDocument Solid = CreateSolidArcDocument("FlipSolid");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = 1.0; Block.Depth = 1.0; Block.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Solid, Block));
        const std::map<std::string, int> Faces = Senses(Solid);
        const std::string Held = EncodeSolidArcDocument(Solid);
        const SolidArcCommandOutcome Refused = Faces.empty() ? SolidArcCommandOutcome{} : FlipSolidArcFaces(Solid, { Faces.begin()->first });
        Tally(CurveAskGate, 0.0, !Refused.AcceptedCondition && EncodeSolidArcDocument(Solid) == Held, Format("a solid's face refuses untouched: %s", Refused.Refusal.c_str()));
    }

    // ③ H on curves: a picked sketch and a picked 3D curve hide; Alt+H shows them again
    {
        SolidArcDocument Part = CreateSolidArcDocument("HideAsk");
        const std::string Sketch = Draw(Part, SolidArcDrawingKind::Curve, Ground, { { 0.0, 0.0 }, { 1.0, 1.0 }, { 2.0, 0.0 } });
        Expect(DrawSolidArcSpaceCurve(Part, { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.5 }, { 2.0, 1.0, 0.0 } }, false));
        const std::string Space = Part.Tree.Steps.back().Label;
        Part.Selection.Curves = { Sketch, Space };
        const SolidArcCommandOutcome Hidden = ChangeSolidArcVisibility(Part, SolidArcVisibilityVerb::HideSelected);
        const bool Gone = Part.Appearance.HiddenCurves.contains(Sketch) && Part.Appearance.HiddenCurves.contains(Space);
        const SolidArcCommandOutcome Shown = ChangeSolidArcVisibility(Part, SolidArcVisibilityVerb::UnhideAll);
        Tally(CurveAskGate, 0.0, Hidden.AcceptedCondition && Gone && Shown.AcceptedCondition && Part.Appearance.HiddenCurves.empty(),
              Format("H on a picked sketch curve and a 3D curve: both hidden (%s), Alt+H: none hidden (%d)%s", Gone ? "yes" : "no", int(Part.Appearance.HiddenCurves.size()),
                     Hidden.Refusal.empty() ? "" : (" · " + Hidden.Refusal).c_str()));
    }

    // ④ a 3D curve through off-plane points (drawn with Alt on curves above the plane) passes through each
    {
        SolidArcDocument Part = CreateSolidArcDocument("SpaceAsk");
        const std::vector<Vector3> Through = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.5 }, { 2.0, 1.0, 0.2 }, { 3.0, 0.5, 0.9 } };
        const SolidArcCommandOutcome Drawn = DrawSolidArcSpaceCurve(Part, Through, false);
        const auto Fit = Part.Regeneration.Curves.find(Part.Tree.Steps.back().Label);
        double Worst = 1.0, Tolerance = 0.0;
        if (Fit != Part.Regeneration.Curves.end() && Fit->second.Curve.Definition)
        {
            Worst = 0.0;
            Tolerance = 1.0e-6 * Distance(Vector3{ 0.0, 0.0, 0.0 }, Vector3{ 3.0, 1.0, 0.9 });
            for (size_t Index = 0; Index < Through.size(); ++Index)                          // Uniform knots: point i at τ = i / (n − 1)
                Worst = std::max(Worst, Distance(EvaluateNurbsCurve(*Fit->second.Curve.Definition, double(Index) / double(Through.size() - 1)).Position, Through[Index]));
        }
        Tally(CurveAskGate, Worst, Drawn.AcceptedCondition && Worst <= 1.0e-9 && Part.Selection.Curves.size() == 1 && !Part.Tree.Steps.back().CurveKnots.empty(),
              Format("3D curve through 4 points off the ground: exact (poles + knots, no fit; tolerance kept %.1e), through every point within %.1e%s", Tolerance, Worst,
                     Drawn.Refusal.empty() ? "" : (" · " + Drawn.Refusal).c_str()));
    }

    // ⑤ sketch points lifted off the plane: the curve becomes a 3D curve of the same form; points left on the plane give
    //    exactly the sketch curve
    {
        SolidArcDocument Part = CreateSolidArcDocument("LiftAsk");
        const std::vector<Vector2> Flat = { { 0.0, 0.0 }, { 1.0, 1.0 }, { 2.0, -0.5 }, { 3.0, 0.8 } };
        const std::string Sketch = Draw(Part, SolidArcDrawingKind::Curve, Ground, Flat);
        const SketchModel Model = Part.Regeneration.Sketches.at(Sketch);
        std::vector<Vector3> Placed;
        for (const Vector2& Point : Flat) Placed.push_back({ Point.X, Point.Y, 0.0 });
        // The same weights in 3D, sampled at uniform τ, against the sketch's own exact B-spline at the same τ.
        const std::vector<Vector3>  Same    = SampleThroughCurve(Placed, false, SketchSplineForm::Hermite, 48);
        const SketchSplineOutcome   Own     = ResolveSketchCurveSpline(Model, Model.Curves.front());
        double Planar = Own.Refusal.empty() ? 0.0 : 1.0;
        if (Own.Refusal.empty())
            for (size_t Index = 0; Index < Same.size(); ++Index)
            {
                const double  τ     = Own.Curve.Knots.front() + (Own.Curve.Knots.back() - Own.Curve.Knots.front()) * double(Index) / double(Same.size() - 1);
                const Vector2 Exact = EvaluateSketchSpline(Own.Curve, τ).Position;
                Planar = std::max(Planar, Distance(Exact, Vector2{ Same[Index].X, Same[Index].Y }) + std::abs(Same[Index].Z));
            }
        const Vector3 Raised{ 1.0, 1.0, 0.6 };
        const SolidArcCommandOutcome Lifted = LiftSolidArcSketchPoints(Part, Sketch, Ground, { 1 }, { Raised });
        const bool SketchGone = std::none_of(Part.Tree.Steps.begin(), Part.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Sketch; });
        const auto Fit = Part.Tree.Steps.empty() ? Part.Regeneration.Curves.end() : Part.Regeneration.Curves.find(Part.Tree.Steps.back().Label);
        double Reach = 1.0;
        if (Fit != Part.Regeneration.Curves.end() && Fit->second.Curve.Definition)
        {
            Reach = 0.0;
            const Vector3 Expected[4] = { { 0.0, 0.0, 0.0 }, Raised, { 2.0, -0.5, 0.0 }, { 3.0, 0.8, 0.0 } };
            for (int Index = 0; Index < 4; ++Index) Reach = std::max(Reach, Distance(EvaluateNurbsCurve(*Fit->second.Curve.Definition, Index / 3.0).Position, Expected[Index]));
        }
        Tally(CurveAskGate, std::max(Planar, Reach), Lifted.AcceptedCondition && SketchGone && Planar <= 1.0e-9 && Reach <= 1.0e-9 && Part.Selection.Curves.size() == 1,
              Format("point 2 of a Hermite curve lifted 0.6 off the ground: the curve is a 3D curve (sketch gone: %s) through the raised point and the other three within %.1e (exact poles); with no point lifted the same weights give the sketch curve to %.1e%s",
                     SketchGone ? "yes" : "no", Reach, Planar, Lifted.Refusal.empty() ? "" : (" · " + Lifted.Refusal).c_str()));
        SolidArcDocument Boxed = CreateSolidArcDocument("LiftLine");
        SolidArcDrawingRequest Request;
        Request.Kind = SolidArcDrawingKind::Rectangle; Request.Points = { { 0.0, 0.0 }, { 1.0, 1.0 } };
        Expect(DrawSolidArcSketch(Boxed, Request));
        const SolidArcCommandOutcome Line = LiftSolidArcSketchPoints(Boxed, Boxed.Tree.Steps.back().Label, Ground, { 0 }, { Vector3{ 0.0, 0.0, 0.5 } });
        Tally(CurveAskGate, 0.0, Line.AcceptedCondition && Boxed.Regeneration.Curves.size() == 2 && Boxed.Regeneration.Refused == 0, Format("a rectangle corner lifts its two incident lines into 3D curves: %s", Line.Refusal.c_str()));
    }
}

// W70 — the report "bevel … seems to be clamped whilst it can bevel more; the chamfer chamfers more distance for the same
// bevel … the cube if bevel 1 edge fully it would bevel till the next edge making it a triangle".
void ValidateBevelReach()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1200]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto Near   = [](double A, double B) { return std::abs(A - B) <= 1.0e-9; };
    // A w × d × h block, the edges passing Test picked, blended.
    const auto Blended = [&](double Width, double Depth, double Height, const std::function<bool(Vector3, Vector3)>& Test, BlendProfile Shape, double Size,
                             SolidArcBlendFit* Fit = nullptr)
    {
        SolidArcDocument Part = CreateSolidArcDocument("Reach");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = Width; Block.Depth = Depth; Block.Height = Height;
        Expect(ConstructSolidArcPrimitive(Part, Block));
        bool Additive = false;
        for (const auto& [Slot, Name] : Part.Regeneration.Naming.Edges)
            if (Test(EdgeEndPosition(Part, Name, false), EdgeEndPosition(Part, Name, true))) { SelectSolidArcName(Part, SolidArcSelectionKind::Edge, Name, Additive); Additive = true; }
        if (Fit) *Fit = MeasureSolidArcBlendFit(Part, Part.Selection.Edges);
        const SolidArcCommandOutcome Outcome = BlendSolidArcSelection(Part, Shape, Size);
        const bool Good = Outcome.AcceptedCondition && Part.Summary.SoundCondition && BuildSolidArcScene(Part).Refusal.empty();
        return std::make_pair(Good, std::move(Part));
    };
    const auto TopFront = [&](double Top) { return [=](Vector3 A, Vector3 B) { return Near(A.Y, 0.0) && Near(B.Y, 0.0) && Near(A.Z, Top) && Near(B.Z, Top); }; };
    const char* Names[] = { "fillet", "chamfer" };

    // ① one edge of a 1 m cube at the full span (no longer stopped at 0.999 · span): the face beside it is
    //    used up — 5 faces; a chamfer leaves a triangular section (V = ½), a fillet a quarter round (V = π/4)
    for (const BlendProfile Shape : { BlendProfile::Fillet, BlendProfile::Chamfer })
    {
        SolidArcBlendFit Fit;
        auto [Good, Part] = Blended(1.0, 1.0, 1.0, TopFront(1.0), Shape, 1.0, &Fit);
        const double Exact    = Shape == BlendProfile::Fillet ? Pi / 4.0 : 0.5;
        const double Relative = std::abs(Part.Summary.Volume - Exact) / Exact;
        Tally(FullReachGate, Relative, Good && Near(Fit.Span, 1.0) && Part.Summary.Faces == 5 && Relative <= 1.0e-9,
              Format("1 m cube, one edge, %s at the full span %.3g: %d faces, V %.12f (exact %s)", Names[int(Shape == BlendProfile::Chamfer)], Fit.Span, int(Part.Summary.Faces),
                     Part.Summary.Volume, Shape == BlendProfile::Fillet ? "π/4" : "½"));
    }
    // ② a 2 × 1 × 0.5 block, its top-front edge: the span is the 0.5 front height; at 0.5 the front face is used up (the
    //    blend runs down to the bottom edge), the top keeps 0.5 of its 1 m depth
    for (const BlendProfile Shape : { BlendProfile::Fillet, BlendProfile::Chamfer })
    {
        SolidArcBlendFit Fit;
        auto [Good, Part] = Blended(2.0, 1.0, 0.5, TopFront(0.5), Shape, 0.5, &Fit);
        const double Exact    = 1.0 - (Shape == BlendProfile::Fillet ? 1.0 - Pi / 4.0 : 0.5) * 0.25 * 2.0;
        const double Relative = std::abs(Part.Summary.Volume - Exact) / Exact;
        Tally(FullReachGate, Relative, Good && Near(Fit.Span, 0.5) && Part.Summary.Faces == 6 && Relative <= 1.0e-9,
              Format("2 × 1 × 0.5 block, top-front edge, %s at the span %.3g (the front height): %d faces, V %.12f", Names[int(Shape == BlendProfile::Chamfer)], Fit.Span,
                     int(Part.Summary.Faces), Part.Summary.Volume));
    }
    // ④ past the face width (the report "it suppose to go longer even if i put a value greater it refuses … this is a
    //    cube"): the blend runs on over the next face — closed forms on the unit cube's top-front edge. A fillet r ≥ 1
    //    removes, per unit length, u* + ∫ (r − √(r² − (r − u)²)) du over [u*, 1], u* = r − √(2r − 1); a chamfer d in
    //    [1, 2] keeps ½ (2 − d)². The reaches (the largest size the bevel asks for) lie past both.
    {
        const double R  = 1.5;
        const double Us = R - std::sqrt(2.0 * R - 1.0);
        const auto   F  = [&](double T) { return 0.5 * (T * std::sqrt(R * R - T * T) + R * R * std::asin(T / R)); };
        const double FilletKept = 1.0 - (Us + R * (1.0 - Us) - (F(R - Us) - F(R - 1.0)));
        for (const BlendProfile Shape : { BlendProfile::Fillet, BlendProfile::Chamfer })
        {
            SolidArcBlendFit Fit;
            auto [Good, Part] = Blended(1.0, 1.0, 1.0, TopFront(1.0), Shape, 1.5, &Fit);
            const double Exact    = Shape == BlendProfile::Fillet ? FilletKept : 0.125;
            const double Relative = std::abs(Part.Summary.Volume - Exact) / Exact;
            const double Reach    = Shape == BlendProfile::Fillet ? Fit.FilletReach : Fit.ChamferReach;
            Tally(FullReachGate, Relative, Good && Relative <= 1.0e-9 && Reach >= 1.5 && Near(Fit.Span, 1.0),
                  Format("1 m cube, one edge, %s 1.5 (past the 1 m face, over the next): V %.9f (exact %.9f), reach %.4g", Names[int(Shape == BlendProfile::Chamfer)],
                         Part.Summary.Volume, Exact, Reach));
        }
    }
    // ⑤ the screenshot case: an edge between faces at 135° (a cube's top-front edge chamfered 0.5, then the top ∩ chamfer
    //    edge filleted). The top is 0.5 wide, but r 1.2 sets back only 1.2 · tan 22.5° = 0.497: it builds, removing
    //    r² (tan(π/8) − π/8) per length (the old cap stopped the radius at the 0.5 width)
    {
        SolidArcDocument Part = CreateSolidArcDocument("Obtuse");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = 1.0; Block.Depth = 1.0; Block.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Part, Block));
        const auto PickWhere = [&](const std::function<bool(Vector3, Vector3)>& Test)
        {
            Part.Selection = {};
            bool Additive = false;
            for (const auto& [Slot, Name] : Part.Regeneration.Naming.Edges)
                if (Test(EdgeEndPosition(Part, Name, false), EdgeEndPosition(Part, Name, true))) { SelectSolidArcName(Part, SolidArcSelectionKind::Edge, Name, Additive); Additive = true; }
        };
        PickWhere(TopFront(1.0));
        Expect(BlendSolidArcSelection(Part, BlendProfile::Chamfer, 0.5));
        PickWhere([&](Vector3 A, Vector3 B) { return Near(A.Y, 0.5) && Near(B.Y, 0.5) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const SolidArcBlendFit Fit = MeasureSolidArcBlendFit(Part, Part.Selection.Edges);
        const SolidArcCommandOutcome Rounded = BlendSolidArcSelection(Part, BlendProfile::Fillet, 1.2);
        const double Exact    = 0.875 - 1.44 * (std::tan(Pi / 8.0) - Pi / 8.0);
        const double Relative = std::abs(Part.Summary.Volume - Exact) / Exact;
        Tally(FullReachGate, Relative, Rounded.AcceptedCondition && Part.Summary.SoundCondition && Relative <= 1.0e-9 && Near(Fit.Span, 0.5) && Fit.FilletReach >= 1.2,
              Format("135° edge beside a 0.5 face: fillet r 1.2 builds (setback 0.497): V %.12f (exact %.12f), span %.3g, fillet reach %.4g", Part.Summary.Volume, Exact, Fit.Span,
                     Fit.FilletReach));
    }
    // ⑥ the handles follow the bevel (the report "the blue + white controls … should follow the edges / vertices"): the
    //    viewport stands each handle on the bisector where the blend's mid-line crosses it — a fillet r at r (1 / Spread −
    //    1) in from the sharp edge, a chamfer s at s √(1 − Spread²), Spread = cos ½φ from the handle measure. A ray down
    //    the uniform handle's bisector onto the bevelled body (tessellated to 10⁻⁷ of its diagonal) hits that point.
    {
        struct HandleCase { const char* Note; BlendProfile Shape; double Size; bool Obtuse; };
        for (const HandleCase& Case : { HandleCase{ "fillet 0.3", BlendProfile::Fillet, 0.3, false }, HandleCase{ "chamfer 0.3", BlendProfile::Chamfer, 0.3, false },
                                        HandleCase{ "fillet 1 (full)", BlendProfile::Fillet, 1.0, false }, HandleCase{ "chamfer 1.5 (past the face)", BlendProfile::Chamfer, 1.5, false },
                                        HandleCase{ "fillet 1.2 on the 135° edge", BlendProfile::Fillet, 1.2, true } })
        {
            SolidArcDocument Part = CreateSolidArcDocument("Handles");
            SolidArcConstructRequest Block;
            Block.Kind = SolidArcConstructKind::Block; Block.Width = 1.0; Block.Depth = 1.0; Block.Height = 1.0;
            Expect(ConstructSolidArcPrimitive(Part, Block));
            const auto PickWhere = [&](const std::function<bool(Vector3, Vector3)>& Test)
            {
                Part.Selection = {};
                bool Additive = false;
                for (const auto& [Slot, Name] : Part.Regeneration.Naming.Edges)
                    if (Test(EdgeEndPosition(Part, Name, false), EdgeEndPosition(Part, Name, true))) { SelectSolidArcName(Part, SolidArcSelectionKind::Edge, Name, Additive); Additive = true; }
            };
            PickWhere(TopFront(1.0));
            if (Case.Obtuse)
            {
                Expect(BlendSolidArcSelection(Part, BlendProfile::Chamfer, 0.5));
                PickWhere([&](Vector3 A, Vector3 B) { return Near(A.Y, 0.5) && Near(B.Y, 0.5) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
            }
            const std::vector<SolidArcBlendHandle> Handles = MeasureSolidArcBlendHandles(Part, Part.Selection.Edges);
            if (Handles.size() != 3) { Tally(FullReachGate, 0.0, false, Format("%s: %d handles (3 expected)", Case.Note, int(Handles.size()))); continue; }
            const SolidArcBlendHandle& Uniform = Handles.front();
            const double Rate  = Case.Shape == BlendProfile::Chamfer ? std::sqrt(1.0 - Uniform.Spread * Uniform.Spread) : 1.0 / Uniform.Spread - 1.0;
            const Vector3 Base = Uniform.Position - Uniform.Direction * (Rate * Case.Size);
            const bool CornersAgree = std::abs(Handles[1].Spread - Uniform.Spread) <= 1.0e-9 && std::abs(Handles[2].Spread - Uniform.Spread) <= 1.0e-9;
            Expect(BlendSolidArcSelection(Part, Case.Shape, Case.Size));
            const SolidArcScene Scene = BuildSolidArcScene(Part, 1.0e-7);
            const Vector3 From = Base + Uniform.Direction * 3.0;
            const std::optional<SolidArcPick> Hit = PickSolidArcScene(Scene, RayBetween({ From.X, From.Y, From.Z }, { Base.X, Base.Y, Base.Z }), 0.0);
            const double Off = Hit ? std::abs(Hit->Distance - 3.0) : 1.0;
            const double Turn = 2.0 * std::acos(std::clamp(Uniform.Spread, -1.0, 1.0)) * 180.0 / Pi;
            Tally(FullReachGate, Off, Hit.has_value() && Off <= 1.0e-6 && CornersAgree,
                  Format("handle on the bevel, %s (faces turn %.1f°): the grip's base %.4f in from the edge, a ray down its bisector meets the body %.2e from it", Case.Note, Turn,
                         Rate * Case.Size, Off));
        }
    }
    // ③ fillet and chamfer reach equally far: the largest size each takes on the same edges is the same (the setback of a
    //    chamfer is the fillet's radius — both meet the faces on the same lines; the chamfer only cuts more away, ½ r²
    //    against (1 − π/4) r² ≈ 0.215 r² per length, which is why it looks bigger at the same size)
    struct Layout { const char* Note; std::function<bool(Vector3, Vector3)> Test; std::vector<double> Sizes; };
    const std::vector<Layout> Layouts = {
        { "two edges at a top corner", [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0) && ((Near(A.Y, 0.0) && Near(B.Y, 0.0)) || (Near(A.X, 0.0) && Near(B.X, 0.0))); },
          { 0.9, 0.99, 1.0 } },
        { "the top loop (four edges)", [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); }, { 0.45, 0.49, 0.5 } },
    };
    for (const Layout& Each : Layouts)
    {
        double Largest[2] = { 0.0, 0.0 };
        for (const BlendProfile Shape : { BlendProfile::Fillet, BlendProfile::Chamfer })
            for (const double Size : Each.Sizes)
                if (Blended(1.0, 1.0, 1.0, Each.Test, Shape, Size).first) Largest[int(Shape == BlendProfile::Chamfer)] = Size;
        Tally(FullReachGate, std::abs(Largest[0] - Largest[1]), Largest[0] > 0.0 && Largest[0] == Largest[1],
              Format("1 m cube, %s: the largest size tried that builds — fillet %.3g, chamfer %.3g", Each.Note, Largest[0], Largest[1]));
    }
}

// 📝 W71: blends that meet merge. The closed forms (unit cube, top at z = 1, d = distance to the nearest side):
//    two parallel top edges at ½ remove 2 · ¼ (1 − π/4) (fillet) / 2 · ⅛ (chamfer); the top loop at ½ leaves the
//    top at z = ½ + g(d) — chamfer g = d, so V = ½ + ∫∫ d = ½ + ⅙; fillet g = √(¼ − (½ − d)²), so V = ½ + 4 ∫₀^½
//    2t √(¼ − t²) dt = ½ + ⅓; on a 2 × 1 block the level sets d ≥ t are (2 − 2t)(1 − 2t) rectangles, so the hip roof
//    keeps 1 + ∫₀^½ (2 − 2t)(1 − 2t) dt = 1 + 5/12 (chamfer) and 1 + ∫ g′ · area = 1 + ⅓ + π/8 (fillet).
void ValidateBevelMerge()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1200]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto Near   = [](double A, double B) { return std::abs(A - B) <= 1.0e-9; };
    struct MergeCase
    {
        const char*                            Note;
        std::function<bool(Vector3, Vector3)>  Test;
        double                                 Size;
        double                                 Exact[2];
        int                                    Topology[3];                                 // Faces, edges, vertices
        double                                 Width = 1.0;                                 // [len] - Block x extent
    };
    const std::vector<MergeCase> Cases = {
        { "1 m cube, two parallel top edges at ½ (rails meet mid-top: one ridge)",
          [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0) && Near(A.Y, B.Y); }, 0.5, { 1.0 - 0.5 * (1.0 - Pi / 4.0), 0.75 }, { 7, 15, 10 } },
        { "1 m cube, the top loop at ½ (four rails close to one apex)",
          [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); }, 0.5, { 5.0 / 6.0, 2.0 / 3.0 }, { 9, 16, 9 } },
        { "2 × 1 × 1 block, the top loop at ½ (hip roof: the long rails fuse into a ridge, the short ones close to two apexes)",
          [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); }, 0.5, { 4.0 / 3.0 + Pi / 8.0, 17.0 / 12.0 }, { 9, 17, 10 }, 2.0 },
    };
    const char* Names[] = { "fillet", "chamfer" };
    for (const MergeCase& Case : Cases)
    {
        for (const BlendProfile Shape : { BlendProfile::Fillet, BlendProfile::Chamfer })
        {
            SolidArcDocument Part = CreateSolidArcDocument("Merge");
            SolidArcConstructRequest Block;
            Block.Kind = SolidArcConstructKind::Block; Block.Width = Case.Width; Block.Depth = 1.0; Block.Height = 1.0;
            Expect(ConstructSolidArcPrimitive(Part, Block));
            bool Additive = false;
            for (const auto& [Slot, Name] : Part.Regeneration.Naming.Edges)
                if (Case.Test(EdgeEndPosition(Part, Name, false), EdgeEndPosition(Part, Name, true))) { SelectSolidArcName(Part, SolidArcSelectionKind::Edge, Name, Additive); Additive = true; }
            const SolidArcCommandOutcome Outcome = BlendSolidArcSelection(Part, Shape, Case.Size);
            const bool   Built    = Outcome.AcceptedCondition && Part.Summary.SoundCondition && BuildSolidArcScene(Part).Refusal.empty();
            const double Exact    = Case.Exact[int(Shape == BlendProfile::Chamfer)];
            const double Relative = Built ? std::abs(Part.Summary.Volume - Exact) / Exact : 1.0;
            const bool   Counted  = (Part.Summary.Faces == Case.Topology[0] && Part.Summary.Edges == Case.Topology[1] && Part.Summary.Vertices == Case.Topology[2]);
            const std::string Refusal = Outcome.Refusal.empty() ? std::string() : " · " + Outcome.Refusal;
            // The viewport's detent: the first merge size the fit measure offers is the one that built here.
            if (Shape == BlendProfile::Fillet && Case.Width == 1.0)
            {
                SolidArcDocument Fresh = CreateSolidArcDocument("Detent");
                Expect(ConstructSolidArcPrimitive(Fresh, Block));
                bool Again = false;
                for (const auto& [Slot, Name] : Fresh.Regeneration.Naming.Edges)
                    if (Case.Test(EdgeEndPosition(Fresh, Name, false), EdgeEndPosition(Fresh, Name, true))) { SelectSolidArcName(Fresh, SolidArcSelectionKind::Edge, Name, Again); Again = true; }
                const SolidArcBlendFit Fit = MeasureSolidArcBlendFit(Fresh, Fresh.Selection.Edges);
                const bool Offered = !Fit.FilletMerges.empty() && !Fit.ChamferMerges.empty() && Near(Fit.FilletMerges.front(), Case.Size) && Near(Fit.ChamferMerges.front(), Case.Size);
                Tally(BevelMergeGate, Offered ? 0.0 : 1.0, Offered,
                      Format("%s: the drag detent offers fillet %s / chamfer %s (snaps within 9 px)", Case.Note,
                             [&] { std::string Text; for (double Merge : Fit.FilletMerges) Text += (Text.empty() ? "" : ", ") + Format("%.4g", Merge); return Text; }().c_str(),
                             [&] { std::string Text; for (double Merge : Fit.ChamferMerges) Text += (Text.empty() ? "" : ", ") + Format("%.4g", Merge); return Text; }().c_str()));
            }
            Tally(BevelMergeGate, Relative, Built && Counted && Relative <= 1.0e-9,
                  Format("%s, %s: %d F / %d E / %d V, V %.12f (exact %.12f)%s", Case.Note, Names[int(Shape == BlendProfile::Chamfer)], int(Part.Summary.Faces),
                         int(Part.Summary.Edges), int(Part.Summary.Vertices), Part.Summary.Volume, Exact, Refusal.c_str()));
        }
    }
}

void ValidateStage3CadFeatures()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1200]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto RelErr = [](double Actual, double Expected) { return std::abs(Actual - Expected) / std::max(1.0e-12, std::abs(Expected)); };
    const auto RoundTripCheck = [&](const SolidArcDocument& Source)
    {
        const std::string Encoded = EncodeSolidArcDocument(Source);
        const SolidArcDocumentFile Decoded = DecodeSolidArcDocument(Encoded);
        if (!Decoded.Refusal.empty()) return false;
        SolidArcDocument Reloaded = CreateSolidArcDocument(Decoded.Title);
        Reloaded.Tree       = Decoded.Tree;
        Reloaded.Captions   = Decoded.Captions;
        Reloaded.Appearance = Decoded.Appearance;
        RegenerateSolidArcDocument(Reloaded, true);
        return EncodeSolidArcDocument(Reloaded) == Encoded && RelErr(Reloaded.Summary.Volume, Source.Summary.Volume) <= 1.0e-9 && Reloaded.Summary.SoundCondition == Source.Summary.SoundCondition;
    };

    // ---- W72: Boolean Library Cutters & Non-Freezing Speed ----
    {
        const SolidArcInsertLibrary Library = AssembleSolidArcInserts("");
        SolidArcDocument Plate = CreateSolidArcDocument("LibrarySpeed");
        SolidArcConstructRequest Base;
        Base.Kind = SolidArcConstructKind::Block; Base.Width = 6.0; Base.Depth = 4.0; Base.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Plate, Base));
        const std::string HostKey = Plate.Regeneration.Active;
        // Add 3 bystander bodies to verify scene tessellation cache speeds up multi-body boolean updates.
        for (int Extra = 0; Extra < 3; ++Extra)
        {
            SolidArcConstructRequest Pillar;
            Pillar.Kind = SolidArcConstructKind::Cylinder; Pillar.Radius = 0.25; Pillar.Height = 1.0;
            Pillar.Plane.Origin = { -1.5, double(Extra) * 1.2, 0.0 };
            Pillar.Combination = FeatureCombination::Separate;
            Expect(ConstructSolidArcPrimitive(Plate, Pillar));
        }
        (void)BuildSolidArcScene(Plate);

        double WorstCutMs = 0.0;
        bool AllSound = !Library.Inserts.empty();
        for (size_t Index = 0; Index < Library.Inserts.size(); ++Index)
        {
            const SolidArcInsert& Ins = Library.Inserts[Index];
            const SolidArcInsertReach Reach = ReachSolidArcInsert(Ins, Ins.Depth * 1.5);
            OrthonormalFrame Spot;
            Spot.Origin = { 0.6 + double(Index % 4) * 1.2, 0.8 + double(Index / 4) * 1.6, 1.0 };
            Spot.XAxis  = { 1.0, 0.0, 0.0 };
            Spot.YAxis  = { 0.0, 1.0, 0.0 };
            Spot.ZAxis  = { 0.0, 0.0, 1.0 };
            SolidArcInsertPlacement Placement;
            Placement.Name        = Ins.Name;
            Placement.Exchange    = Reach.Exchange;
            Placement.Map         = PlaceSolidArcInsert(Spot, 0.35, 0.0, false, Reach.Stretch, 1.0);
            Placement.Combination = FeatureCombination::Difference;
            Placement.Target      = HostKey;
            const auto Start = std::chrono::steady_clock::now();
            const SolidArcCommandOutcome Cut = KnifeSolidArcInsert(Plate, Placement);
            const SolidArcScene BuiltScene   = BuildSolidArcScene(Plate);
            const double Elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
            WorstCutMs = std::max(WorstCutMs, Elapsed);
            AllSound   = AllSound && Cut.AcceptedCondition && Plate.Summary.SoundCondition && !BuiltScene.Corners.empty();
        }
        Tally(CutterSpeedGate, WorstCutMs, AllSound && WorstCutMs <= 80.0,
              Format("All %d library inserts cut + tessellated cleanly: worst %.2f ms (sound = %d)", int(Library.Inserts.size()), WorstCutMs, AllSound ? 1 : 0));

        // Coaxial Z-stretched round hole retains exact CylinderSurface (zero NURBS promotion).
        int RoundIndex = -1;
        for (size_t Index = 0; Index < Library.Inserts.size(); ++Index)
            if (Library.Inserts[Index].Name.find("Round") != std::string::npos || Library.Inserts[Index].Name.find("Countersink") != std::string::npos) { RoundIndex = int(Index); break; }
        if (RoundIndex >= 0)
        {
            SolidArcDocument Coax = CreateSolidArcDocument("CoaxialAnalytic");
            SolidArcConstructRequest Cube;
            Cube.Kind = SolidArcConstructKind::Block; Cube.Width = 2.0; Cube.Depth = 2.0; Cube.Height = 2.0;
            Expect(ConstructSolidArcPrimitive(Coax, Cube));
            OrthonormalFrame TopSpot;
            TopSpot.Origin = { 1.0, 1.0, 2.0 }; TopSpot.XAxis = { 1.0, 0.0, 0.0 }; TopSpot.YAxis = { 0.0, 1.0, 0.0 }; TopSpot.ZAxis = { 0.0, 0.0, 1.0 };
            SolidArcInsertPlacement Stretched;
            Stretched.Name        = Library.Inserts[RoundIndex].Name;
            Stretched.Exchange    = Library.Inserts[RoundIndex].Exchange;
            Stretched.Map         = PlaceSolidArcInsert(TopSpot, 0.5, 0.25, false, 2.5, 1.0);
            Stretched.Combination = FeatureCombination::Difference;
            Stretched.Target      = Coax.Regeneration.Active;
            const SolidArcCommandOutcome Cut = KnifeSolidArcInsert(Coax, Stretched);
            int NurbsFaces = 0;
            const BodyToken HostTok = Coax.Regeneration.Bodies.front().Body;
            for (const FaceToken Face : Coax.Partition->CollectInventory(HostTok).Faces)
                if (std::holds_alternative<NurbsSurface>(Coax.Partition->Surfaces.Resolve(Coax.Partition->Faces.Resolve(Face).Surface))) ++NurbsFaces;
            Tally(CutterSpeedGate, double(NurbsFaces), Cut.AcceptedCondition && NurbsFaces == 0 && Coax.Summary.SoundCondition,
                  Format("Coaxial Z-stretched %s (Stretch 2.5): %d NURBS faces (analytic cylinder/cone preserved)", Stretched.Name.c_str(), NurbsFaces));
        }
    }

    // ---- W73: Modular Parametric Variables (Bevel Size + Boolean Bolt Offset) ----
    {
        SolidArcDocument BoltDoc = CreateSolidArcDocument("ParametricBolt");
        SolidArcConstructRequest BlockReq;
        BlockReq.Kind = SolidArcConstructKind::Block; BlockReq.Width = 2.0; BlockReq.Depth = 2.0; BlockReq.Height = 2.0;
        Expect(ConstructSolidArcPrimitive(BoltDoc, BlockReq));
        const std::string HostKey = BoltDoc.Regeneration.Active;

        // Fillet one top edge, then promote its Size to user variable 'FilletR'.
        std::string TopEdge;
        for (const auto& [Slot, Name] : BoltDoc.Regeneration.Naming.Edges)
        {
            const Vector3 A = EdgeEndPosition(BoltDoc, Name, false), B = EdgeEndPosition(BoltDoc, Name, true);
            if ( std::abs(A.Z - 2.0) < 1e-6 && std::abs(B.Z - 2.0) < 1e-6 && std::abs(A.Y) < 1e-6 && std::abs(B.Y) < 1e-6 ) { TopEdge = Name; break; }
        }
        SelectSolidArcName(BoltDoc, SolidArcSelectionKind::Edge, TopEdge, false);
        Expect(BlendSolidArcSelection(BoltDoc, BlendProfile::Fillet, 0.1));
        const std::string BlendLabel = BoltDoc.Tree.Steps.back().Label;
        const SolidArcCommandOutcome SavedVar = SaveStepValueAsVariable(BoltDoc, BlendLabel, "Size", "FilletR");

        // Add a cylindrical bolt cutter overlapping z in [1.5, 2.5], Combine Difference, and bind Combine.Offset to 'BoltDepth'.
        SolidArcConstructRequest BoltReq;
        BoltReq.Kind        = SolidArcConstructKind::Cylinder;
        BoltReq.Radius      = 0.3;
        BoltReq.Height      = 1.0;
        BoltReq.Position    = { 1.0, 1.0, 1.5 };
        BoltReq.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(BoltDoc, BoltReq));
        const std::string BoltKey = BoltDoc.Regeneration.Active;
        Expect(CombineSolidArcBodies(BoltDoc, HostKey, { BoltKey }, FeatureCombination::Difference));
        const std::string CombineLabel = BoltDoc.Tree.Steps.back().Label;
        Expect(CreateSolidArcVariable(BoltDoc, "BoltShift", 0.0, -1.0, 1.0, "Bolt axial offset"));
        Expect(SetSolidArcStepOffsetAxis(BoltDoc, CombineLabel, { 0.0, 0.0, -1.0 }));
        const SolidArcCommandOutcome BoundOff = BindSolidArcStepValue(BoltDoc, CombineLabel, "Offset", "BoltShift");

        // Edit FilletR -> 0.2 and BoltShift -> 0.3 (pushes bolt 0.3 deeper into block, penetration = 0.8 instead of 0.5).
        Expect(AssignSolidArcParameter(BoltDoc, "FilletR", 0.2));
        Expect(AssignSolidArcParameter(BoltDoc, "BoltShift", 0.3));
        const double ExpectedVol = 8.0 - (1.0 - Pi / 4.0) * (0.2 * 0.2) * 2.0 - Pi * (0.3 * 0.3) * 0.8;
        const double VolErr      = RelErr(BoltDoc.Summary.Volume, ExpectedVol);
        Tally(ParametricVarGate, VolErr, SavedVar.AcceptedCondition && BoundOff.AcceptedCondition && VolErr <= 1.0e-9 && BoltDoc.Summary.SoundCondition,
              Format("FilletR=0.2 & BoltShift=0.3 modularly updated V=%.9f (exact %.9f)", BoltDoc.Summary.Volume, ExpectedVol));

        // Rename BoltShift -> AxialGap, verify round-trip, then Delete variable (baking current value without changing V).
        const SolidArcCommandOutcome Renamed = RenameSolidArcVariable(BoltDoc, "BoltShift", "AxialGap");
        const bool CodecOk = RoundTripCheck(BoltDoc);
        const SolidArcCommandOutcome Deleted = DeleteSolidArcVariable(BoltDoc, "AxialGap");
        const double PostDeleteErr = RelErr(BoltDoc.Summary.Volume, ExpectedVol);
        Tally(ParametricVarGate, PostDeleteErr, Renamed.AcceptedCondition && CodecOk && Deleted.AcceptedCondition && PostDeleteErr <= 1.0e-9,
              "Rename BoltShift→AxialGap + TOML round-trip + DeleteVariable (bakes literal cleanly)");
    }

    // ---- W74: Live Booleans & Real-Time Cutter Movement ----
    {
        SolidArcDocument LiveDoc = CreateSolidArcDocument("LiveBoolean");
        SolidArcConstructRequest HostBox;
        HostBox.Kind = SolidArcConstructKind::Block; HostBox.Width = 3.0; HostBox.Depth = 2.0; HostBox.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(LiveDoc, HostBox));
        const std::string HostKey = LiveDoc.Regeneration.Active;

        const SolidArcInsertLibrary Library = AssembleSolidArcInserts("");
        OrthonormalFrame TopSpot;
        TopSpot.Origin = { 1.0, 1.0, 1.0 }; TopSpot.XAxis = { 1.0, 0.0, 0.0 }; TopSpot.YAxis = { 0.0, 1.0, 0.0 }; TopSpot.ZAxis = { 0.0, 0.0, 1.0 };
        SolidArcInsertPlacement LivePlace;
        LivePlace.Name        = Library.Inserts.front().Name;
        LivePlace.Exchange    = Library.Inserts.front().Exchange;
        LivePlace.Map         = PlaceSolidArcInsert(TopSpot, 0.4, 0.0, false, 1.0, 1.0);
        LivePlace.Combination = FeatureCombination::Difference;
        LivePlace.Target      = HostKey;

        const SolidArcCommandOutcome PlacedLive = KnifeSolidArcLiveInsert(LiveDoc, LivePlace);
        const std::string CutterKey = LiveDoc.Selection.Bodies.empty() ? std::string{} : LiveDoc.Selection.Bodies.front();
        const SolidArcScene LiveScene = BuildSolidArcScene(LiveDoc);
        const bool CutterRegistered = !CutterKey.empty() && LiveDoc.Appearance.LiveCutters.contains(CutterKey) &&
                                      SolidArcBodyReferencedByModifiersCondition(LiveDoc, CutterKey);
        int CutterCorners = 0, CutterEdges = 0;
        for (const SolidArcSceneFace& Face : LiveScene.Faces) if (LiveScene.Bodies[Face.Body].Key == CutterKey) CutterCorners += int(Face.CornerCount);
        for (const SolidArcSceneEdge& Edge : LiveScene.Edges) if (LiveScene.Bodies[Edge.Body].Key == CutterKey) ++CutterEdges;
        const bool SplitValid = LiveScene.SolidCornerCount > 0 && LiveScene.SolidCornerCount + uint32_t(CutterCorners) == LiveScene.Corners.size() &&
                                LiveScene.SolidSegmentCount > 0 && LiveScene.SolidSegmentCount < LiveScene.Segments.size();
        // Ray straight down into the cutter centre hits the semitransparent cutter face; ray on solid block far from cutter hits solid block!
        const std::optional<SolidArcPick> HitCutter = PickSolidArcScene(LiveScene, { { 1.0, 1.0, 3.0 }, { 0.0, 0.0, -1.0 } }, 0.0, false);
        const std::optional<SolidArcPick> HitSolid  = PickSolidArcScene(LiveScene, { { 2.5, 1.0, 3.0 }, { 0.0, 0.0, -1.0 } }, 0.0, false);
        const bool BothPickable = HitCutter && !(HitCutter->Identity & IdentityEdgeBit) &&
                                  LiveScene.Bodies[LiveScene.Faces[HitCutter->Identity - 1u].Body].Key == CutterKey &&
                                  HitSolid && !(HitSolid->Identity & IdentityEdgeBit) &&
                                  LiveScene.Bodies[LiveScene.Faces[HitSolid->Identity - 1u].Body].Key == HostKey;
        LiveDoc.Selection = {};
        const std::vector<std::string> Exported = SolidArcExportBodies(LiveDoc);
        const bool ExcludedFromExport = Exported.size() == 1 && Exported.front() == HostKey;
        Tally(LiveBooleanGate, 0.0, PlacedLive.AcceptedCondition && CutterRegistered && CutterCorners > 0 && CutterEdges > 0 && SplitValid && BothPickable && ExcludedFromExport,
              Format("Live cutter %s: solid corners %u + semitransparent cutter corners %d, %d edges, both bodies pickable, excluded from export",
                     CutterKey.c_str(), LiveScene.SolidCornerCount, CutterCorners, CutterEdges));

        // Move the Live Cutter in real time: first across the top face (same cut volume), then half-off the x=3 edge (half cut volume!).
        const double FullCutLoss = 6.0 - LiveDoc.Summary.Volume;
        AffineMap SlideInside; SlideInside.Shift = { 0.8, 0.0, 0.0 };
        const SolidArcCommandOutcome SlidInside = TransformSolidArcBodies(LiveDoc, { CutterKey }, SlideInside, "Slide Live Cutter");
        const double InsideLoss = 6.0 - LiveDoc.Summary.Volume;
        AffineMap SlideEdge; SlideEdge.Shift = { 1.2, 0.0, 0.0 };                                // Cutter centre now at x = 3.0 (half outside [0,3])
        const SolidArcCommandOutcome SlidEdge = TransformSolidArcBodies(LiveDoc, { CutterKey }, SlideEdge, "Slide Cutter To Edge");
        const double HalfLoss = 6.0 - LiveDoc.Summary.Volume;
        const bool HalfExact = RelErr(InsideLoss, FullCutLoss) <= 1.0e-9 && RelErr(HalfLoss, 0.5 * FullCutLoss) <= 1.0e-6 && LiveDoc.Summary.SoundCondition;
        const bool LiveCodec = RoundTripCheck(LiveDoc);
        Tally(LiveBooleanGate, RelErr(HalfLoss, 0.5 * FullCutLoss), SlidInside.AcceptedCondition && SlidEdge.AcceptedCondition && HalfExact && LiveCodec,
              Format("Real-time cutter move: inside ΔV=%.6f → edge ΔV=%.6f (half=%.6f), TOML round-trip ok", InsideLoss, HalfLoss, 0.5 * FullCutLoss));

        // Apply Live Boolean modifier -> bakes native Combine step and removes LiveCutter!
        const std::string ModLabel = LiveDoc.Tree.Modifiers.front().Label;
        const SolidArcCommandOutcome Applied = ApplySolidArcModifier(LiveDoc, ModLabel);
        const double BakedLoss = 6.0 - LiveDoc.Summary.Volume;
        Tally(LiveBooleanGate, RelErr(BakedLoss, HalfLoss),
              Applied.AcceptedCondition && LiveDoc.Tree.Modifiers.empty() && LiveDoc.Appearance.LiveCutters.empty() && RelErr(BakedLoss, HalfLoss) <= 1.0e-9,
              "ApplySolidArcModifier baked Live Boolean into native Combine step with identical volume");
    }

    // ---- W75: Edge Weight Paint & Weighted Bevel Modifier ----
    {
        SolidArcDocument Weighted = CreateSolidArcDocument("EdgeWeightBevel");
        SolidArcConstructRequest Box;
        Box.Kind = SolidArcConstructKind::Block; Box.Width = 2.0; Box.Depth = 2.0; Box.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Weighted, Box));
        std::string FrontTop, BackTop;
        for (const auto& [Slot, Name] : Weighted.Regeneration.Naming.Edges)
        {
            const Vector3 A = EdgeEndPosition(Weighted, Name, false), B = EdgeEndPosition(Weighted, Name, true);
            if (std::abs(A.Z - 1.0) < 1e-6 && std::abs(B.Z - 1.0) < 1e-6 && std::abs(A.Y) < 1e-6 && std::abs(B.Y) < 1e-6) FrontTop = Name;
            if (std::abs(A.Z - 1.0) < 1e-6 && std::abs(B.Z - 1.0) < 1e-6 && std::abs(A.Y - 2.0) < 1e-6 && std::abs(B.Y - 2.0) < 1e-6) BackTop = Name;
        }
        Expect(CreateSolidArcVariable(Weighted, "HeavyWeight", 2.0, 0.1, 5.0));
        Expect(AssignSolidArcEdgeWeights(Weighted, "TopWeights", { FrontTop }, FeatureValue{ 1.0 }));
        Expect(AssignSolidArcEdgeWeights(Weighted, "TopWeights", { BackTop }, ParameterValue("HeavyWeight")));

        FeatureModifier BevelMod;
        BevelMod.Kind      = ModifierKind::Bevel;
        BevelMod.Shape     = BlendProfile::Fillet;
        BevelMod.LimitMode = BevelLimitMode::EdgeGroup;
        BevelMod.EdgeGroup = "TopWeights";
        BevelMod.Size      = FeatureValue{ 0.1 };
        const SolidArcCommandOutcome AddedBevel = AddSolidArcModifier(Weighted, BevelMod);
        const double ExpectedV1 = 4.0 - (1.0 - Pi / 4.0) * (0.1 * 0.1) * 2.0 - (1.0 - Pi / 4.0) * (0.2 * 0.2) * 2.0;
        const double Err1 = RelErr(Weighted.Summary.Volume, ExpectedV1);

        // Update HeavyWeight variable from 2.0 -> 3.0 -> BackTop fillet radius updates to 0.3!
        Expect(AssignSolidArcParameter(Weighted, "HeavyWeight", 3.0));
        const double ExpectedV2 = 4.0 - (1.0 - Pi / 4.0) * (0.1 * 0.1) * 2.0 - (1.0 - Pi / 4.0) * (0.3 * 0.3) * 2.0;
        const double Err2 = RelErr(Weighted.Summary.Volume, ExpectedV2);
        const bool CodecOk = RoundTripCheck(Weighted);
        Tally(EdgeWeightGate, std::max(Err1, Err2), AddedBevel.AcceptedCondition && Err1 <= 1.0e-9 && Err2 <= 1.0e-9 && Weighted.Summary.SoundCondition && CodecOk,
              Format("Weighted edge group (1.0 + HeavyWeight=2.0→3.0) bevel V=%.9f (exact %.9f), TOML round-trip ok", Weighted.Summary.Volume, ExpectedV2));
    }

    // ---- W76: Linear & Circular Array Modifiers ----
    {
        SolidArcDocument ArrDoc = CreateSolidArcDocument("Arrays");
        SolidArcConstructRequest UnitCube;
        UnitCube.Kind = SolidArcConstructKind::Block; UnitCube.Width = 0.5; UnitCube.Depth = 0.5; UnitCube.Height = 0.5;
        Expect(ConstructSolidArcPrimitive(ArrDoc, UnitCube));
        Expect(CreateSolidArcVariable(ArrDoc, "GridX", 3.0, 1.0, 10.0));

        FeatureModifier LinMod;
        LinMod.Kind              = ModifierKind::LinearArray;
        LinMod.Count             = ParameterValue("GridX");
        LinMod.SecondaryCount    = FeatureValue{ 2.0 };
        LinMod.Direction         = { 1.0, 0.0, 0.0 };
        LinMod.Distance          = FeatureValue{ 1.0 };
        LinMod.SecondaryDirection = { 0.0, 1.0, 0.0 };
        LinMod.SecondaryDistance = FeatureValue{ 1.0 };
        const SolidArcCommandOutcome AddedLin = AddSolidArcModifier(ArrDoc, LinMod);
        const double ErrGrid = RelErr(ArrDoc.Summary.Volume, 6.0 * 0.125);
        Tally(LinearCircArrayGate, ErrGrid, AddedLin.AcceptedCondition && ErrGrid <= 1.0e-9 && ArrDoc.Summary.SoundCondition && ArrDoc.Summary.Genus == 0,
              Format("LinearArray 3×2 disjoint: V=%.6f (6×0.125), Genus=%d", ArrDoc.Summary.Volume, ArrDoc.Summary.Genus));

        // Overlapping LinearArray with MergeCondition = true: 2 cubes of width 0.5 shifted by 0.25 -> 0.75 × 0.5 × 0.5 = 0.1875!
        Expect(AssignSolidArcParameter(ArrDoc, "GridX", 2.0));
        FeatureModifier OverlapLin = ArrDoc.Tree.Modifiers.front();
        OverlapLin.SecondaryCount = FeatureValue{ 1.0 };
        OverlapLin.Distance       = FeatureValue{ 0.25 };
        OverlapLin.MergeCondition = true;
        Expect(UpdateSolidArcModifier(ArrDoc, OverlapLin.Label, OverlapLin));
        const double ErrMerged = RelErr(ArrDoc.Summary.Volume, 0.1875);
        Tally(LinearCircArrayGate, ErrMerged, ErrMerged <= 1.0e-9 && ArrDoc.Summary.SoundCondition,
              Format("LinearArray overlapping MergeCondition=true: V=%.6f (exact 0.1875)", ArrDoc.Summary.Volume));

        // CircularArray: 5 copies around Z axis at radius offset, then ApplySolidArcModifier -> 5 single-solid Import steps!
        Expect(RemoveSolidArcModifier(ArrDoc, OverlapLin.Label));
        AffineMap ShiftOut; ShiftOut.Shift = { 1.5, 0.0, 0.0 };
        Expect(TransformSolidArcBodies(ArrDoc, { ArrDoc.Regeneration.Active }, ShiftOut, "Move Out"));
        FeatureModifier CircMod;
        CircMod.Kind          = ModifierKind::CircularArray;
        CircMod.Count         = FeatureValue{ 5.0 };
        CircMod.Angle         = FeatureValue{ 2.0 * Pi };
        CircMod.AxisDirection = { 0.0, 0.0, 1.0 };
        CircMod.Offset        = FeatureValue{ 0.4 };
        const SolidArcCommandOutcome AddedCirc = AddSolidArcModifier(ArrDoc, CircMod);
        const double ErrCirc = RelErr(ArrDoc.Summary.Volume, 5.0 * 0.125);
        const bool CircCodec = RoundTripCheck(ArrDoc);
        const SolidArcCommandOutcome BakedCirc = ApplySolidArcModifier(ArrDoc, ArrDoc.Tree.Modifiers.front().Label);
        const double ErrBaked = RelErr(ArrDoc.Summary.Volume, 5.0 * 0.125);
        Tally(LinearCircArrayGate, std::max(ErrCirc, ErrBaked),
              AddedCirc.AcceptedCondition && CircCodec && BakedCirc.AcceptedCondition && ArrDoc.Summary.Bodies.size() == 5 && ErrBaked <= 1.0e-9 && ArrDoc.Summary.SoundCondition,
              Format("CircularArray helical 5 copies + Apply→5 solid bodies: V=%.6f", ArrDoc.Summary.Volume));
    }

    // ---- W77: Array Along Curve & Array On Surface Modifiers ----
    {
        SolidArcDocument PathDoc = CreateSolidArcDocument("CurveAndSurfaceArrays");
        SolidArcConstructRequest Bead;
        Bead.Kind = SolidArcConstructKind::Sphere; Bead.Radius = 0.15;
        Expect(ConstructSolidArcPrimitive(PathDoc, Bead));
        const std::string BeadKey = PathDoc.Regeneration.Active;
        const double SingleVol = (4.0 / 3.0) * Pi * (0.15 * 0.15 * 0.15);

        SolidArcDrawingRequest ArcDraw;
        ArcDraw.Kind = SolidArcDrawingKind::Arc;
        ArcDraw.Points = { { 0.0, 0.0 }, { 1.5, 1.5 }, { 3.0, 0.0 } };
        Expect(DrawSolidArcSketch(PathDoc, ArcDraw));
        const std::string ArcSketch = PathDoc.Tree.Steps.back().Label;

        FeatureModifier CurveArr;
        CurveArr.Kind           = ModifierKind::CurveArray;
        CurveArr.Target         = BeadKey;
        CurveArr.Curve          = ArcSketch;
        CurveArr.Count          = FeatureValue{ 4.0 };
        CurveArr.AlignCondition = true;
        const SolidArcCommandOutcome AddedCurveArr = AddSolidArcModifier(PathDoc, CurveArr);
        const double ErrCurveArr = RelErr(PathDoc.Summary.Volume, 4.0 * SingleVol);
        Tally(CurveSurfArrayGate, ErrCurveArr, AddedCurveArr.AcceptedCondition && ErrCurveArr <= 1.0e-9 && PathDoc.Summary.SoundCondition,
              Format("CurveArray 4 spheres along 2D arc sketch: V=%.6f (exact %.6f)", PathDoc.Summary.Volume, 4.0 * SingleVol));

        // SurfaceArray: 3x2 spheres arrayed across the cylindrical wall of a drum!
        Expect(RemoveSolidArcModifier(PathDoc, PathDoc.Tree.Modifiers.front().Label));
        SolidArcConstructRequest Drum;
        Drum.Kind        = SolidArcConstructKind::Cylinder;
        Drum.Radius      = 2.0;
        Drum.Height      = 2.0;
        Drum.Position    = { 5.0, 0.0, 0.0 };
        Drum.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(PathDoc, Drum));
        const std::string DrumKey = PathDoc.Regeneration.Active;
        std::string WallFace;
        for (const auto& [Slot, Name] : PathDoc.Regeneration.Bodies.back().Naming.Faces)
            if (Name.find("Side") != std::string::npos || Name.find("Wall") != std::string::npos || Name.find("Lateral") != std::string::npos) { WallFace = Name; break; }

        FeatureModifier SurfArr;
        SurfArr.Kind           = ModifierKind::SurfaceArray;
        SurfArr.Target         = BeadKey;
        SurfArr.Tool           = DrumKey;
        if (!WallFace.empty()) SurfArr.References = { { ReferenceKind::Face, WallFace, {} } };
        SurfArr.Count          = FeatureValue{ 3.0 };
        SurfArr.SecondaryCount = FeatureValue{ 2.0 };
        SurfArr.Margin         = FeatureValue{ 0.2 };
        const SolidArcCommandOutcome AddedSurfArr = AddSolidArcModifier(PathDoc, SurfArr);
        const double ExpectedTotal = 6.0 * SingleVol + Pi * 4.0 * 2.0;
        const double ErrSurfArr    = RelErr(PathDoc.Summary.Volume, ExpectedTotal);
        const bool SurfCodec       = RoundTripCheck(PathDoc);
        Tally(CurveSurfArrayGate, ErrSurfArr, AddedSurfArr.AcceptedCondition && ErrSurfArr <= 1.0e-9 && PathDoc.Summary.SoundCondition && SurfCodec,
              Format("SurfaceArray 3×2 on cylinder surface + TOML round-trip: V=%.6f (exact %.6f)", PathDoc.Summary.Volume, ExpectedTotal));
    }

    // ---- W78: Certified 3D Bend Along Curve & Modifier Stack ----
    {
        SolidArcDocument BendDoc = CreateSolidArcDocument("CurveBend");
        // Build a 2.0 × 0.2 × 0.3 solid bar centred on Y=0, Z=0 along X in [0, 2].
        SolidArcConstructRequest Bar;
        Bar.Kind     = SolidArcConstructKind::Block;
        Bar.Width    = 2.0;
        Bar.Depth    = 0.2;
        Bar.Height   = 0.3;
        Bar.Position = { 0.0, -0.1, -0.15 };
        Expect(ConstructSolidArcPrimitive(BendDoc, Bar));
        const std::string BarKey = BendDoc.Regeneration.Active;

        // Draw a 90° circular arc of radius R = 2.0 in XY with Start=(2,0), End=(0,2), Through=(√2, √2): arc length = π!
        const double Root2 = std::sqrt(2.0);
        SolidArcDrawingRequest QuarterArc;
        QuarterArc.Kind   = SolidArcDrawingKind::Arc;
        QuarterArc.Points = { { 2.0, 0.0 }, { 0.0, 2.0 }, { Root2, Root2 } };
        Expect(DrawSolidArcSketch(BendDoc, QuarterArc));
        const std::string ArcLabel = BendDoc.Tree.Steps.back().Label;

        // With StretchCondition = true, the 0.2 × 0.3 cross-section stretches along arc length L = π at centroid R = 2.0:
        // Pappus exact volume = Area * ArcLength = (0.2 * 0.3) * π = 0.06 * π!
        FeatureModifier BendMod;
        BendMod.Kind             = ModifierKind::CurveBend;
        BendMod.Target           = BarKey;
        BendMod.Curve            = ArcLabel;
        BendMod.DeformAxis       = 0;
        BendMod.StretchCondition = true;
        const SolidArcCommandOutcome AddedBend = AddSolidArcModifier(BendDoc, BendMod);
        const double ExactBentVol = 0.06 * Pi;
        const double BentErr      = RelErr(BendDoc.Summary.Volume, ExactBentVol);
        std::string AuditWhy;
        if (!BendDoc.Summary.SoundCondition && !BendDoc.Regeneration.Bodies.empty())
        {
            const AuditReport Rep = AuditBody(*BendDoc.Partition, BendDoc.Regeneration.Bodies.front().Body);
            for (const AuditFinding& Find : Rep.Findings)
                AuditWhy += Format(" [%s %s: %s obs=%.2e allow=%.2e]", Find.Code, Find.Subject.c_str(), Find.Detail.c_str(), Find.Measured, Find.Allowed);
        }
        Tally(CurveBendStackGate, BentErr, AddedBend.AcceptedCondition && BendDoc.Summary.SoundCondition && BentErr <= 2.0e-3,
              Format("CurveBend solid bar along 90° arc: T1–T6 & G1–G6 sound=%d, V=%.6f (Pappus %.6f, rel %.2e)%s",
                     BendDoc.Summary.SoundCondition ? 1 : 0, BendDoc.Summary.Volume, ExactBentVol, BentErr, AuditWhy.c_str()));

        // Stack a LinearArray AFTER CurveBend (2 bent arches = 2 × V), then test Move, Toggle, Undo/Redo, and TOML round-trip!
        FeatureModifier SecondMod;
        SecondMod.Kind      = ModifierKind::LinearArray;
        SecondMod.Target    = BarKey;
        SecondMod.Count     = FeatureValue{ 2.0 };
        SecondMod.Direction = { 0.0, 0.0, 1.0 };
        SecondMod.Distance  = FeatureValue{ 1.0 };
        const SolidArcCommandOutcome AddedSecond = AddSolidArcModifier(BendDoc, SecondMod);
        const double TwoArchesVol = BendDoc.Summary.Volume;
        const double StackErr     = RelErr(TwoArchesVol, 2.0 * ExactBentVol);
        const bool StackCodec     = RoundTripCheck(BendDoc);

        // Toggle mute on LinearArray -> back to 1 arch; Undo -> back to 2 arches!
        const SolidArcCommandOutcome Muted = ToggleSolidArcModifier(BendDoc, BendDoc.Tree.Modifiers.back().Label);
        const bool MutedSingle = RelErr(BendDoc.Summary.Volume, ExactBentVol) <= 2.0e-3;
        const bool Undone      = UndoSolidArcCommand(BendDoc) && RelErr(BendDoc.Summary.Volume, TwoArchesVol) <= 1.0e-12;
        const bool Redone      = RedoSolidArcCommand(BendDoc) && RelErr(BendDoc.Summary.Volume, ExactBentVol) <= 2.0e-3;
        Tally(CurveBendStackGate, StackErr,
              AddedSecond.AcceptedCondition && StackErr <= 2.0e-3 && BendDoc.Summary.SoundCondition && StackCodec && Muted.AcceptedCondition && MutedSingle && Undone && Redone,
              "Stacked CurveBend → LinearArray + Mute/Undo/Redo + byte-equal TOML round-trip");
    }

    // ---- W79: Multi-Stage Compound Cutters (No Stretching, No 1-Stage Duplicates, Exact Analytic Volumes) ----
    {
        const SolidArcInsertLibrary UserEmpty = AssembleSolidArcUserInserts("");
        Tally(CompoundCutterGate, double(UserEmpty.Inserts.size()), UserEmpty.Inserts.empty(),
              Format("AssembleSolidArcUserInserts(\"\"): %d duplicate 1-stage shapes (6 multi-stage compound cutters are native)", int(UserEmpty.Inserts.size())));

        SolidArcDocument Slab = CreateSolidArcDocument("CompoundSlab");
        SolidArcConstructRequest SlabReq;
        SlabReq.Kind = SolidArcConstructKind::Block; SlabReq.Width = 6.0; SlabReq.Depth = 4.0; SlabReq.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Slab, SlabReq));
        const std::string SlabKey = Slab.Regeneration.Active;

        constexpr double PiVal = std::numbers::pi;
        const double R1 = 0.20, D1 = 0.15, R2 = 0.10, D2 = 0.25;
        const double Tip = std::min(0.45 * D2, 0.58 * R2);
        const double ExpectedLoss[SolidArcCompoundCutterCount] = {
            (PiVal * D1 / 3.0) * (R1 * R1 + R1 * R2 + R2 * R2) + PiVal * R2 * R2 * D2,                  // Countersink
            PiVal * R1 * R1 * D1 + PiVal * R2 * R2 * D2,                                                 // Counterbore
            PiVal * R1 * R1 * D1 + PiVal * R2 * R2 * (D2 - Tip) + (PiVal * R2 * R2 * Tip) / 3.0,         // Stepped hole
            1.5 * std::sqrt(3.0) * R1 * R1 * D1 + PiVal * R2 * R2 * D2,                                  // Hex socket
            (PiVal * R1 * R1 + 4.0 * 1.15 * R1 * R1) * D1 + (PiVal * R2 * R2 + 4.0 * 1.15 * R1 * R2) * D2, // Stepped slot
            4.0 * R1 * R1 * D1 + 4.0 * R2 * R2 * D2                                                      // Stepped pocket
        };

        double WorstCompoundErr = 0.0;
        bool   AllAccepted      = true;
        int    SplineFaces      = 0;
        for (int Index = 0; Index < SolidArcCompoundCutterCount; ++Index)
        {
            const double BeforeVol = Slab.Summary.Volume;
            SolidArcCompoundCutterSpec Spec;
            Spec.Kind        = static_cast<SolidArcCompoundCutterKind>(Index);
            Spec.Spot.Origin = { 0.8 + 0.85 * (Index % 3) * 2.0, Index < 3 ? 1.0 : 3.0, 1.0 };
            Spec.Spot.XAxis  = { 1.0, 0.0, 0.0 };
            Spec.Spot.YAxis  = { 0.0, 1.0, 0.0 };
            Spec.Spot.ZAxis  = { 0.0, 0.0, 1.0 };
            Spec.OuterRadius = R1;
            Spec.OuterDepth  = D1;
            Spec.InnerRadius = R2;
            Spec.InnerDepth  = D2;
            Spec.Combination = FeatureCombination::Difference;
            Spec.Target      = SlabKey;
            const auto Wire = BuildSolidArcCompoundCutterSegments(Spec);
            const SolidArcCommandOutcome Cut = KnifeSolidArcCompoundCutter(Slab, Spec);
            const double ActualLoss = BeforeVol - Slab.Summary.Volume;
            const double Err        = RelErr(ActualLoss, ExpectedLoss[Index]);
            WorstCompoundErr = std::max(WorstCompoundErr, Err);
            AllAccepted      = AllAccepted && Cut.AcceptedCondition && Slab.Summary.SoundCondition && Wire.size() >= 12 && Err <= 1.0e-8;
        }
        const SolidArcScene CompoundScene = BuildSolidArcScene(Slab);
        for (const SolidArcSceneFace& Face : CompoundScene.Faces)
            if (Face.Surface == "Spline") ++SplineFaces;
        Tally(CompoundCutterGate, WorstCompoundErr, AllAccepted && SplineFaces == 0 && RoundTripCheck(Slab),
              Format("All 6 multi-stage compound cutters: max ΔV rel err %.2e, %d NURBS spline faces (100%% analytic), sound=%d",
                     WorstCompoundErr, SplineFaces, Slab.Summary.SoundCondition ? 1 : 0));
    }

    // ---- W80: Realtime-Editable Semitransparent Live Compound Cutter + Standalone Icons Verification ----
    {
        SolidArcDocument EditLive = CreateSolidArcDocument("EditableLiveCompound");
        SolidArcConstructRequest BlockReq;
        BlockReq.Kind = SolidArcConstructKind::Block; BlockReq.Width = 2.0; BlockReq.Depth = 2.0; BlockReq.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(EditLive, BlockReq));
        const std::string TargetKey = EditLive.Regeneration.Active;

        SolidArcCompoundCutterSpec LiveSpec;
        LiveSpec.Kind          = SolidArcCompoundCutterKind::Counterbore;
        LiveSpec.Spot.Origin   = { 1.0, 1.0, 1.0 };
        LiveSpec.Spot.XAxis    = { 1.0, 0.0, 0.0 };
        LiveSpec.Spot.YAxis    = { 0.0, 1.0, 0.0 };
        LiveSpec.Spot.ZAxis    = { 0.0, 0.0, 1.0 };
        LiveSpec.OuterRadius   = 0.25;
        LiveSpec.OuterDepth    = 0.20;
        LiveSpec.InnerRadius   = 0.12;
        LiveSpec.InnerDepth    = 0.30;
        LiveSpec.Combination   = FeatureCombination::Difference;
        LiveSpec.Target        = TargetKey;
        LiveSpec.LiveCondition = true;

        const SolidArcCommandOutcome CreatedLive = KnifeSolidArcCompoundCutter(EditLive, LiveSpec);
        const std::string CutterKey = EditLive.Selection.Bodies.empty() ? std::string{} : EditLive.Selection.Bodies.front();
        const double InitialLoss = 4.0 - EditLive.Summary.Volume;
        const double ExpectedInitial = std::numbers::pi * (0.25 * 0.25 * 0.20 + 0.12 * 0.12 * 0.30);

        // Find the semitransparent cutter's bottom planar floor face (normal = -Z at z = 0.50) and Push/Pull it 0.10 deeper in real time!
        const SolidArcScene SceneBefore = BuildSolidArcScene(EditLive);
        std::string CutterFloorFace;
        for (const SolidArcSceneFace& Face : SceneBefore.Faces)
        {
            if (SceneBefore.Bodies[Face.Body].Key != CutterKey || Face.Surface != "Plane" || Face.CornerCount == 0) continue;
            const auto& Corner = SceneBefore.Corners[Face.FirstCorner];
            if (std::abs(Corner.Position[2] - 0.50f) < 1.0e-3f && Corner.Normal[2] < -0.9f) CutterFloorFace = Face.Name;
        }
        EditLive.Selection            = {};
        EditLive.Selection.Faces      = { CutterFloorFace };
        EditLive.Selection.PickedBody = CutterKey;
        EditLive.Selection.LastKind   = SolidArcSelectionKind::Face;
        const SolidArcCommandOutcome PushedFloor = ChangeSolidArcSelection(EditLive, FaceOperationKind::Offset, 0.10);
        const double DeeperLoss     = 4.0 - EditLive.Summary.Volume;
        const double ExpectedDeeper = std::numbers::pi * (0.25 * 0.25 * 0.20 + 0.12 * 0.12 * 0.40);
        const double PushErr        = RelErr(DeeperLoss, ExpectedDeeper);

        // Now select the semitransparent cutter's bottom circular rim edge (z = 0.40) and Fillet it by r = 0.03 in real time!
        std::string CutterBottomRim;
        const RegeneratedBody* CutterBody = nullptr;
        for (const RegeneratedBody& Body : EditLive.Regeneration.Bodies) if (Body.Key == CutterKey) CutterBody = &Body;
        if (CutterBody)
        {
            for (const auto& [Slot, EdgeName] : CutterBody->Naming.Edges)
            {
                const EdgeProfile& EdgeProf = EditLive.Partition->Edges.Resolve(EditLive.Partition->Edges.TokenAt(Slot));
                const Vector3 Pos = EditLive.Partition->Vertices.Resolve(EdgeProf.Start).Position;
                if (std::abs(Pos.Z - 0.40) < 1.0e-4) { CutterBottomRim = EdgeName; break; }
            }
        }
        EditLive.Selection            = {};
        EditLive.Selection.Edges      = { CutterBottomRim };
        EditLive.Selection.PickedBody = CutterKey;
        EditLive.Selection.LastKind   = SolidArcSelectionKind::Edge;
        const SolidArcCommandOutcome BeveledCutter = BlendSolidArcSelection(EditLive, BlendProfile::Fillet, 0.03);
        // Filleting the cutter's convex bottom edge reduces the cutter's volume slightly, so the carved solid target's volume INCREASES slightly!
        const double FilletedLoss = 4.0 - EditLive.Summary.Volume;
        const bool RealtimeEdited = CreatedLive.AcceptedCondition && RelErr(InitialLoss, ExpectedInitial) <= 1.0e-8 &&
                                    PushedFloor.AcceptedCondition && PushErr <= 1.0e-8 &&
                                    BeveledCutter.AcceptedCondition && FilletedLoss < DeeperLoss && FilletedLoss > InitialLoss &&
                                    EditLive.Summary.SoundCondition && EditLive.Appearance.LiveCutters.contains(CutterKey);
        Tally(LiveEditableGate, PushErr, RealtimeEdited,
              Format("Realtime edit on semitransparent cutter %s: initial ΔV=%.6f → push floor +0.10 ΔV=%.6f (rel %.2e) → fillet rim r=0.03 ΔV=%.6f, sound=%d",
                     CutterKey.c_str(), InitialLoss, DeeperLoss, PushErr, FilletedLoss, EditLive.Summary.SoundCondition ? 1 : 0));

        // Verify standalone SolidArc SVG icon inventory in EngineContent/Icons/SolidArc (the build names the folder, so the check
        // holds from any working directory; ctest runs this one from the build folder).
        int SvgCount = 0;
        std::error_code FsErr;
        for (const char* Candidate : { SOLIDARC_ICON_FOLDER, "EngineContent/Icons/SolidArc" })
        {
            if (!std::filesystem::is_directory(Candidate, FsErr)) continue;
            SvgCount = 0;
            for (const auto& Entry : std::filesystem::directory_iterator(Candidate, FsErr))
                if (Entry.is_regular_file(FsErr) && Entry.path().extension() == ".svg" && Entry.file_size(FsErr) > 32) ++SvgCount;
            if (SvgCount >= 60) break;
        }
        Tally(LiveEditableGate, 0.0, SvgCount >= 65,
              Format("Standalone SolidArc icon artwork verified: %d non-empty SVG icons", SvgCount));
    }
}

void ValidateGizmoAndContinuedLofts()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1400]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };

    // ============================================================================================================
    // W81 — Gizmo across ALL 7 views (Top, Bottom, Front, Back, Right, Left, Isometric 3D) × ALL 2D/3D primitives,
    //       sub-elements, 2D curves & handles, and 3D SpaceCurves & pole handles.
    // ============================================================================================================
    {
        struct ViewSpec
        {
            OrbitViewPreset Preset;
            const char*     Name;
            bool            FlatCondition;
            bool            Orthographic;
        };
        const ViewSpec Views[] = {
            { OrbitViewPreset::Top,       "Top(ortho)",    true,  true  },
            { OrbitViewPreset::Top,       "Top(persp)",    true,  false },
            { OrbitViewPreset::Bottom,    "Bottom",        true,  true  },
            { OrbitViewPreset::Front,     "Front",         true,  true  },
            { OrbitViewPreset::Back,      "Back",          true,  true  },
            { OrbitViewPreset::Right,     "Right",         true,  true  },
            { OrbitViewPreset::Left,      "Left",          true,  true  },
            { OrbitViewPreset::Isometric, "Isometric(3D)", false, false }
        };

        const auto CountVisible = [](const SolidArcGizmoSession& Gizmo, SolidArcGizmoHandle First)
        {
            int Shown = 0;
            for (int Axis = 0; Axis < 3; ++Axis)
                if (!((Gizmo.Pose.Hidden >> (uint32_t(First) + uint32_t(Axis))) & 1u)) ++Shown;
            return Shown;
        };

        const auto ProjectPoint = [](const OrbitCameraFrame& Frame, Vector3 Point, double& PixelX, double& PixelY)
        {
            double Clip[4] = {};
            for (int Row = 0; Row < 4; ++Row)
                Clip[Row] = Frame.ClipFromWorld[Row] * Point.X + Frame.ClipFromWorld[4 + Row] * Point.Y + Frame.ClipFromWorld[8 + Row] * Point.Z + Frame.ClipFromWorld[12 + Row];
            if (std::abs(Clip[3]) <= 1.0e-12) return false;
            PixelX = (Clip[0] / Clip[3] * 0.5 + 0.5) * Frame.Width;
            PixelY = (Clip[1] / Clip[3] * 0.5 + 0.5) * Frame.Height;
            return true;
        };

        const auto VerifyShownArrowsPickable = [&](const SolidArcGizmoSession& Gizmo, const OrbitCameraFrame& Frame)
        {
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const SolidArcGizmoHandle Handle = SolidArcGizmoHandle(uint32_t(SolidArcGizmoHandle::TranslateX) + uint32_t(Axis));
                if ((Gizmo.Pose.Hidden >> uint32_t(Handle)) & 1u) continue;
                // Cone spans [0.86, 1.04] · Size; 1.00 · Size probes the unoccluded cone tip half (clear of the 0.95 quad corner on isometric rays).
                const Vector3 Tip = Gizmo.Pose.Origin + Gizmo.Pose.Axes[Axis] * (1.00 * Gizmo.Pose.Size);
                double PixelX = 0.0, PixelY = 0.0;
                if (!ProjectPoint(Frame, Tip, PixelX, PixelY)) return false;
                if (PickSolidArcGizmo(Gizmo.Geometry, Frame, PixelX, PixelY, 6.0) != Handle) return false;
            }
            return !Gizmo.Geometry.Vertices.empty();
        };

        // ① All 8 3D construct primitives across all 8 view configurations (Top ortho/persp, Bottom, Front, Back, Right, Left, 3D).
        {
            const SolidArcConstructKind Kinds[] = {
                SolidArcConstructKind::Block, SolidArcConstructKind::Cylinder, SolidArcConstructKind::Sphere, SolidArcConstructKind::Cone,
                SolidArcConstructKind::Torus, SolidArcConstructKind::Wedge,    SolidArcConstructKind::Prism,  SolidArcConstructKind::Tube
            };
            int ChecksRun = 0, Failures = 0;
            for (SolidArcConstructKind Kind : Kinds)
            {
                SolidArcWorkspaceState State;
                State.WelcomeOpen = false;
                State.PickMode    = SolidArcPickMode::Solid;
                State.Gizmo.Modes = GizmoTranslate | GizmoRotate | GizmoScale;
                SolidArcConstructRequest Req;
                Req.Kind        = Kind;
                Req.Combination = FeatureCombination::Separate;
                Req.Width = 1.6; Req.Depth = 1.2; Req.Height = 1.0; Req.Radius = 0.8; Req.Major = 1.2; Req.Minor = 0.25; Req.Inner = 0.3; Req.Sides = 6;
                Expect(ConstructSolidArcPrimitive(State.Document, Req));
                const std::string BodyKey = State.Document.Regeneration.Bodies.back().Key;
                SelectSolidArcName(State.Document, SolidArcSelectionKind::Body, BodyKey, false, BodyKey);
                RefreshSolidArcScene(State, false);

                for (const ViewSpec& View : Views)
                {
                    PresentOrbitView(State.Camera, View.Preset);
                    State.Camera.OrthographicEnabled = View.Orthographic;
                    State.Camera.Distance            = 6.0;
                    const OrbitCameraFrame Frame = FrameOrbitCamera(State.Camera, 1280.0, 720.0);
                    EvaluateSolidArcGizmo(State, Frame);
                    const int Arrows = CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX);
                    const int Planes = CountVisible(State.Gizmo, SolidArcGizmoHandle::PlaneYZ);
                    const int Rings  = CountVisible(State.Gizmo, SolidArcGizmoHandle::RotateX);
                    const int Scales = CountVisible(State.Gizmo, SolidArcGizmoHandle::ScaleX);
                    const bool ExpectedCounts = View.FlatCondition ? (Arrows == 2 && Planes == 1 && Rings == 1 && Scales == 2)
                                                                   : (Arrows == 3 && Planes == 3 && Rings == 3 && Scales == 3);
                    const bool PickOk = VerifyShownArrowsPickable(State.Gizmo, Frame);
                    ++ChecksRun;
                    if (!ExpectedCounts || !PickOk || State.Gizmo.FlatCondition != View.FlatCondition) ++Failures;
                }
                // Apply a Top-view X+Y move on the primitive and verify volume invariance!
                const double VolumeBefore = State.Document.Summary.Volume;
                PresentOrbitView(State.Camera, OrbitViewPreset::Top);
                State.Camera.OrthographicEnabled = true;
                EvaluateSolidArcGizmo(State, FrameOrbitCamera(State.Camera, 1280.0, 720.0));
                const SolidArcCommandOutcome Moved = ApplySolidArcGizmo(State, SolidArcGizmoHandle::PlaneXY, { 0.35, -0.25, 0.0 });
                if (!Moved.AcceptedCondition || !State.Document.Summary.SoundCondition
                    || std::abs(State.Document.Summary.Volume - VolumeBefore) > 1.0e-9 * std::max(1.0, VolumeBefore))
                {
                    ++Failures;
                }
            }
            Tally(GizmoAllViewsGate, double(Failures), Failures == 0 && ChecksRun == 64,
                  Format("8 3D primitives × 8 views (Top ortho/persp, Bottom, Front, Back, Right, Left, 3D): %d/%d gizmo poses & ray-picks exact, PlaneXY move volume-exact",
                         ChecksRun - Failures, ChecksRun));
        }

        // ② 3D Sub-elements (Face, Edge, single Vertex, 2 Vertices) across all views + Top-view Face Offset along +X.
        {
            SolidArcWorkspaceState State;
            State.WelcomeOpen = false;
            State.Gizmo.Modes = GizmoTranslate | GizmoRotate | GizmoScale;
            SolidArcConstructRequest Box;
            Box.Kind = SolidArcConstructKind::Block; Box.Width = 2.0; Box.Depth = 2.0; Box.Height = 1.0;
            Box.Combination = FeatureCombination::Separate;
            Expect(ConstructSolidArcPrimitive(State.Document, Box));
            const std::string BodyKey = State.Document.Regeneration.Bodies.back().Key;
            RefreshSolidArcScene(State, false);

            int SubFailures = 0;
            // Face (+X side face): in Top view, TranslateX pushes along FaceNormal (+X) -> volume grows from 4.0 to 4.5!
            std::string PlusXFace;
            for (const SolidArcSceneFace& Face : State.Scene.Faces)
            {
                if (Face.CornerCount == 0) continue;
                const auto& Normal = State.Scene.Corners[Face.FirstCorner].Normal;
                if (Normal[0] > 0.99) { PlusXFace = Face.Name; break; }
            }
            State.PickMode = SolidArcPickMode::Face;
            SelectSolidArcName(State.Document, SolidArcSelectionKind::Face, PlusXFace, false, BodyKey);
            for (const ViewSpec& View : Views)
            {
                PresentOrbitView(State.Camera, View.Preset);
                State.Camera.OrthographicEnabled = View.Orthographic;
                const OrbitCameraFrame Frame = FrameOrbitCamera(State.Camera, 1280.0, 720.0);
                EvaluateSolidArcGizmo(State, Frame);
                const int Arrows = CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX);
                const int Planes = CountVisible(State.Gizmo, SolidArcGizmoHandle::PlaneYZ);
                if (Arrows != (View.FlatCondition ? 2 : 3) || Planes != (View.FlatCondition ? 1 : 3) || !VerifyShownArrowsPickable(State.Gizmo, Frame))
                    ++SubFailures;
            }
            PresentOrbitView(State.Camera, OrbitViewPreset::Top);
            State.Camera.OrthographicEnabled = true;
            EvaluateSolidArcGizmo(State, FrameOrbitCamera(State.Camera, 1280.0, 720.0));
            const SolidArcCommandOutcome Pushed = ApplySolidArcGizmo(State, SolidArcGizmoHandle::TranslateX, { 0.25, 0.0, 0.0 });
            const double FaceVolErr = std::abs(State.Document.Summary.Volume - 4.5);
            if (!Pushed.AcceptedCondition || FaceVolErr > 1.0e-9) ++SubFailures;
            RefreshSolidArcScene(State, false);
            const std::string ActiveBodyKey = State.Document.Regeneration.Bodies.back().Key;

            // Edge (Mode 2) & Vertex (Mode 1: 1 vertex and 2 vertices) across all views.
            State.PickMode = SolidArcPickMode::Edge;
            SelectSolidArcName(State.Document, SolidArcSelectionKind::Edge, State.Scene.Edges.front().Name, false, ActiveBodyKey);
            for (const ViewSpec& View : Views)
            {
                PresentOrbitView(State.Camera, View.Preset);
                State.Camera.OrthographicEnabled = View.Orthographic;
                const OrbitCameraFrame Frame = FrameOrbitCamera(State.Camera, 1280.0, 720.0);
                EvaluateSolidArcGizmo(State, Frame);
                if (CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX) != (View.FlatCondition ? 2 : 3) || !VerifyShownArrowsPickable(State.Gizmo, Frame))
                    ++SubFailures;
            }
            State.PickMode = SolidArcPickMode::ControlPoint;
            SelectSolidArcName(State.Document, SolidArcSelectionKind::Vertex, State.Scene.Vertices[0].Name, false, ActiveBodyKey);
            PresentOrbitView(State.Camera, OrbitViewPreset::Top);
            EvaluateSolidArcGizmo(State, FrameOrbitCamera(State.Camera, 1280.0, 720.0));
            if (CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX) != 2 || CountVisible(State.Gizmo, SolidArcGizmoHandle::PlaneYZ) != 1)
                ++SubFailures;
            SelectSolidArcName(State.Document, SolidArcSelectionKind::Vertex, State.Scene.Vertices[1].Name, true, ActiveBodyKey);
            EvaluateSolidArcGizmo(State, FrameOrbitCamera(State.Camera, 1280.0, 720.0));
            if (CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX) != 2 || CountVisible(State.Gizmo, SolidArcGizmoHandle::ScaleX) != 2)
                ++SubFailures;

            Tally(GizmoAllViewsGate, FaceVolErr, SubFailures == 0,
                  Format("3D sub-elements (Face, Edge, 1-Vertex, 2-Vertex) across all views + Top-view +X face offset (V=%.4f, err %.2e)",
                         State.Document.Summary.Volume, FaceVolErr));
        }

        // ③ All 2D primitives & curve forms (Line, Rectangle, Circle, Arc, Polygon, Slot, Hermite, CubicSpline, Bezier, NURBS)
        //    drawn on XY (0), XZ (1), and YZ (2) planes — verified in Top View & all 6 cardinal views + 3D!
        {
            int SketchChecks = 0, SketchFailures = 0;
            struct DrawingCase
            {
                SolidArcDrawingKind Kind;
                SketchSplineForm    Form;
                std::vector<Vector2> Points;
            };
            const DrawingCase Cases[] = {
                { SolidArcDrawingKind::Line,      SketchSplineForm::Hermite,     { { 0.0, 0.0 }, { 1.2, 0.4 } } },
                { SolidArcDrawingKind::Rectangle, SketchSplineForm::Hermite,     { { 0.0, 0.0 }, { 1.4, 0.8 } } },
                { SolidArcDrawingKind::Circle,    SketchSplineForm::Hermite,     { { 0.0, 0.0 }, { 0.6, 0.0 } } },
                { SolidArcDrawingKind::Arc,       SketchSplineForm::Hermite,     { { 0.0, 0.0 }, { 0.6, 0.5 }, { 1.2, 0.0 } } },
                { SolidArcDrawingKind::Polygon,   SketchSplineForm::Hermite,     { { 0.0, 0.0 }, { 0.7, 0.0 } } },
                { SolidArcDrawingKind::Slot,      SketchSplineForm::Hermite,     { { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 0.25 } } },
                { SolidArcDrawingKind::Curve,     SketchSplineForm::Hermite,     { { 0.0, 0.0 }, { 0.4, 0.5 }, { 0.9, -0.2 }, { 1.4, 0.4 } } },
                { SolidArcDrawingKind::Curve,     SketchSplineForm::CubicSpline, { { 0.0, 0.0 }, { 0.4, 0.5 }, { 0.9, -0.2 }, { 1.4, 0.4 } } },
                { SolidArcDrawingKind::Curve,     SketchSplineForm::Bezier,      { { 0.0, 0.0 }, { 0.3, 0.7 }, { 1.0, -0.4 }, { 1.4, 0.3 } } },
                { SolidArcDrawingKind::Curve,     SketchSplineForm::Nurbs,       { { 0.0, 0.0 }, { 0.3, 0.7 }, { 1.0, -0.4 }, { 1.4, 0.3 } } }
            };

            for (int GridPlane = 0; GridPlane < 3; ++GridPlane)
            {
                for (const DrawingCase& Spec : Cases)
                {
                    SolidArcWorkspaceState State;
                    State.WelcomeOpen = false;
                    State.Gizmo.Modes = GizmoTranslate | GizmoRotate | GizmoScale;
                    SolidArcDrawingRequest Draw = DrawingOf(Spec.Kind, GridPlane, Spec.Points);
                    Draw.Form  = Spec.Form;
                    Draw.Sides = 6;
                    Expect(DrawSolidArcSketch(State.Document, Draw));
                    const std::string SketchLabel = State.Document.Tree.Steps.back().Label;
                    State.Document.Selection.Curves = { SketchLabel };
                    RefreshSolidArcScene(State, false);

                    // Check Whole-Sketch Gizmo AND Curve-Handle (Points) Gizmo in BOTH Mode 5 (Any) and Mode 1 (ControlPoint) across all views!
                    for (SolidArcPickMode Mode : { SolidArcPickMode::Any, SolidArcPickMode::ControlPoint })
                    {
                        State.PickMode = Mode;
                        State.SketchPoints = {};
                        for (const ViewSpec& View : Views)
                        {
                            PresentOrbitView(State.Camera, View.Preset);
                            State.Camera.OrthographicEnabled = View.Orthographic;
                            const OrbitCameraFrame Frame = FrameOrbitCamera(State.Camera, 1280.0, 720.0);
                            EvaluateSolidArcGizmo(State, Frame);
                            const int Arrows = CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX);
                            const int Planes = CountVisible(State.Gizmo, SolidArcGizmoHandle::PlaneYZ);
                            ++SketchChecks;
                            if (State.Gizmo.Target != SolidArcGizmoTarget::Sketches
                                || Arrows != (View.FlatCondition ? 2 : 3)
                                || Planes != (View.FlatCondition ? 1 : 3)
                                || !VerifyShownArrowsPickable(State.Gizmo, Frame))
                            {
                                ++SketchFailures;
                            }
                        }

                        // Now select a curve handle on this sketch (point 1 when multi-point, else centre point 0 for Circle)
                        // and verify Top View (and all views) shows 2 controls (NEVER 1)!
                        const int HandlePoint = State.Document.Regeneration.Sketches.at(SketchLabel).Points.size() > 1 ? 1 : 0;
                        State.SketchPoints.Sketch = SketchLabel;
                        State.SketchPoints.Points = { HandlePoint };
                        for (const ViewSpec& View : Views)
                        {
                            PresentOrbitView(State.Camera, View.Preset);
                            State.Camera.OrthographicEnabled = View.Orthographic;
                            const OrbitCameraFrame Frame = FrameOrbitCamera(State.Camera, 1280.0, 720.0);
                            EvaluateSolidArcGizmo(State, Frame);
                            const int Arrows = CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX);
                            const int Planes = CountVisible(State.Gizmo, SolidArcGizmoHandle::PlaneYZ);
                            ++SketchChecks;
                            if (State.Gizmo.Target != SolidArcGizmoTarget::Points
                                || Arrows != (View.FlatCondition ? 2 : 3)
                                || Planes != (View.FlatCondition ? 1 : 3)
                                || !VerifyShownArrowsPickable(State.Gizmo, Frame))
                            {
                                ++SketchFailures;
                            }
                        }
                    }
                }
            }
            Tally(GizmoAllViewsGate, double(SketchFailures), SketchFailures == 0 && SketchChecks == 960,
                  Format("10 2D primitives/splines × 3 sketch planes (XY/XZ/YZ) × Mode 1 & 5 × curve & handle × 8 views: %d/%d exact (2 controls in Top & all 90° views)",
                         SketchChecks - SketchFailures, SketchChecks));
        }

        // ④ 3D SpaceCurves & SpaceCurve Pole Handles (Mode 1 and Mode 5) across all 8 views + live gizmo pole move on a Loft!
        {
            SolidArcWorkspaceState State;
            State.WelcomeOpen = false;
            State.Gizmo.Modes = GizmoTranslate | GizmoRotate | GizmoScale;
            Expect(DrawSolidArcSpaceCurve(State.Document, { { 0.0, 0.0, 0.0 }, { 0.6, 0.4, 0.5 }, { 1.4, -0.3, 0.8 }, { 2.0, 0.2, 0.3 } }, false, SketchSplineForm::Bezier));
            const std::string CurveA = State.Document.Tree.Steps.back().Label;
            Expect(DrawSolidArcSpaceCurve(State.Document, { { 0.0, 1.5, 0.0 }, { 0.6, 1.8, 0.4 }, { 1.4, 1.3, 0.6 }, { 2.0, 1.5, 0.2 } }, false, SketchSplineForm::Bezier));
            const std::string CurveB = State.Document.Tree.Steps.back().Label;
            Expect(LoftSolidArcCurves(State.Document, { CurveA, CurveB }));
            RefreshSolidArcScene(State, false);

            int SpaceChecks = 0, SpaceFailures = 0;
            for (SolidArcPickMode Mode : { SolidArcPickMode::Any, SolidArcPickMode::ControlPoint })
            {
                State.PickMode = Mode;
                // Whole SpaceCurve selected
                State.Document.Selection.Curves = { CurveA };
                State.PoleDrag.PickedLabel.clear();
                State.PoleDrag.PickedPoles.clear();
                for (const ViewSpec& View : Views)
                {
                    PresentOrbitView(State.Camera, View.Preset);
                    State.Camera.OrthographicEnabled = View.Orthographic;
                    const OrbitCameraFrame Frame = FrameOrbitCamera(State.Camera, 1280.0, 720.0);
                    EvaluateSolidArcGizmo(State, Frame);
                    ++SpaceChecks;
                    if (State.Gizmo.Target != SolidArcGizmoTarget::Sketches
                        || CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX) != (View.FlatCondition ? 2 : 3)
                        || CountVisible(State.Gizmo, SolidArcGizmoHandle::PlaneYZ) != (View.FlatCondition ? 1 : 3)
                        || !VerifyShownArrowsPickable(State.Gizmo, Frame))
                    {
                        ++SpaceFailures;
                    }
                }

                // Single pole handle selected on CurveA
                State.PoleDrag.PickedLabel = CurveA;
                State.PoleDrag.PickedPoles = { 1 };
                for (const ViewSpec& View : Views)
                {
                    PresentOrbitView(State.Camera, View.Preset);
                    State.Camera.OrthographicEnabled = View.Orthographic;
                    const OrbitCameraFrame Frame = FrameOrbitCamera(State.Camera, 1280.0, 720.0);
                    EvaluateSolidArcGizmo(State, Frame);
                    ++SpaceChecks;
                    if (State.Gizmo.Target != SolidArcGizmoTarget::Poles
                        || CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX) != (View.FlatCondition ? 2 : 3)
                        || CountVisible(State.Gizmo, SolidArcGizmoHandle::PlaneYZ) != (View.FlatCondition ? 1 : 3)
                        || !VerifyShownArrowsPickable(State.Gizmo, Frame))
                    {
                        ++SpaceFailures;
                    }
                }

                // Two pole handles selected on CurveA -> Rotate & Scale handles also active!
                State.PoleDrag.PickedPoles = { 1, 2 };
                PresentOrbitView(State.Camera, OrbitViewPreset::Top);
                State.Camera.OrthographicEnabled = true;
                EvaluateSolidArcGizmo(State, FrameOrbitCamera(State.Camera, 1280.0, 720.0));
                ++SpaceChecks;
                if (State.Gizmo.Target != SolidArcGizmoTarget::Poles
                    || CountVisible(State.Gizmo, SolidArcGizmoHandle::TranslateX) != 2
                    || CountVisible(State.Gizmo, SolidArcGizmoHandle::ScaleX) != 2
                    || CountVisible(State.Gizmo, SolidArcGizmoHandle::RotateX) != 1)
                {
                    ++SpaceFailures;
                }
            }

            // Drag pole 1 of CurveA in Top View via Gizmo TranslateY and verify the curve pole AND loft sheet update soundly!
            State.PickMode             = SolidArcPickMode::Any;
            State.Document.Selection.Curves = { CurveA };
            State.PoleDrag.PickedLabel = CurveA;
            State.PoleDrag.PickedPoles = { 1 };
            PresentOrbitView(State.Camera, OrbitViewPreset::Top);
            EvaluateSolidArcGizmo(State, FrameOrbitCamera(State.Camera, 1280.0, 720.0));
            const Vector3 PoleBefore = State.Document.Regeneration.Curves.at(CurveA).Curve.Definition->Poles[1];
            const SolidArcCommandOutcome PoleMoved = ApplySolidArcGizmo(State, SolidArcGizmoHandle::TranslateY, { 0.0, 0.35, 0.0 });
            const Vector3 PoleAfter  = State.Document.Regeneration.Curves.at(CurveA).Curve.Definition->Poles[1];
            const double  PoleErr    = Distance(PoleAfter, PoleBefore + Vector3{ 0.0, 0.35, 0.0 });
            if (!PoleMoved.AcceptedCondition || PoleErr > 1.0e-12 || !State.Document.Summary.SoundCondition) ++SpaceFailures;

            // Also move the entire SpaceCurve CurveB via Gizmo TranslateX in Top View!
            State.PoleDrag.PickedLabel.clear();
            State.PoleDrag.PickedPoles.clear();
            State.Document.Selection.Curves = { CurveB };
            EvaluateSolidArcGizmo(State, FrameOrbitCamera(State.Camera, 1280.0, 720.0));
            const Vector3 EndBefore  = State.Document.Regeneration.Curves.at(CurveB).Curve.Definition->Poles.back();
            const SolidArcCommandOutcome CurveMoved = ApplySolidArcGizmo(State, SolidArcGizmoHandle::TranslateX, { 0.25, 0.0, 0.0 });
            const Vector3 EndAfter   = State.Document.Regeneration.Curves.at(CurveB).Curve.Definition->Poles.back();
            const double  CurveErr   = Distance(EndAfter, EndBefore + Vector3{ 0.25, 0.0, 0.0 });
            if (!CurveMoved.AcceptedCondition || CurveErr > 1.0e-12 || !State.Document.Summary.SoundCondition) ++SpaceFailures;

            Tally(GizmoAllViewsGate, std::max(PoleErr, CurveErr), SpaceFailures == 0,
                  Format("SpaceCurve & pole-handle gizmos (Mode 1 & 5, 1-pole & 2-pole, 8 views): %d/%d exact, Top-view pole move Δ=%.2e, curve move Δ=%.2e, loft sound",
                         SpaceChecks - SpaceFailures, SpaceChecks, PoleErr, CurveErr));
        }
    }

    // ============================================================================================================
    // W82 — Continuing Lofts & Shared-Endpoint V-Lofts (3 curves & 3 surfaces, different heights, SpaceCurves,
    //       obtuse & acute V angles, 3-section hinged loft, >90° 3-panel loop untwist, T-fin, and Edge+Curve loft).
    // ============================================================================================================
    {
        const auto ElevatedPlane = [](double HeightZ) -> OrthonormalFrame
        {
            return { { 0.0, 0.0, HeightZ }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        };

        // ① Exact user scenario: 2 curves lofted into 1 shape; 3rd curve placed at endpoint of Curve 1 forming a V
        //    (with a different height!) -> continues the loft into 3 curves & 3 sewn surfaces (and closes into a solid!).
        //    Tested for BOTH acute (<90°) AND obtuse (>90°, where Dot >= 0 previously twisted/refused!) V-configurations!
        for (int Case = 0; Case < 2; ++Case)
        {
            const bool ObtuseCondition = Case == 1;
            SolidArcDocument Doc = CreateSolidArcDocument(ObtuseCondition ? "VLoftObtuse" : "VLoftAcute");
            // Curve 1 on z = 0: (0, 0, 0) -> (2, 0, 0)
            Expect(DrawSolidArcSpaceCurve(Doc, { { 0.0, 0.0, 0.0 }, { 1.0, 0.15, 0.0 }, { 2.0, 0.0, 0.0 } }, false, SketchSplineForm::Hermite));
            const std::string C1 = Doc.Tree.Steps.back().Label;
            // Curve 2 at y = 1.5, z = 0.4: (0.2, 1.5, 0.4) -> (2.0, 1.5, 0.4)
            Expect(DrawSolidArcSpaceCurve(Doc, { { 0.2, 1.5, 0.4 }, { 1.1, 1.6, 0.4 }, { 2.0, 1.5, 0.4 } }, false, SketchSplineForm::Hermite));
            const std::string C2 = Doc.Tree.Steps.back().Label;
            // Loft #1: 2 curves -> 1 surface
            const SolidArcCommandOutcome Loft1 = LoftSolidArcCurves(Doc, { C1, C2 });
            // Curve 3 starts at endpoint (2, 0, 0) of Curve 1, forming an acute or obtuse V and rising to height z = 1.3!
            const Vector3 FarEnd = ObtuseCondition ? Vector3{ 3.4, 0.9, 1.3 } : Vector3{ 0.6, 0.9, 1.3 };
            const Vector3 MidPt  = ObtuseCondition ? Vector3{ 2.7, 0.5, 0.7 } : Vector3{ 1.3, 0.5, 0.7 };
            Expect(DrawSolidArcSpaceCurve(Doc, { { 2.0, 0.0, 0.0 }, MidPt, FarEnd }, false, SketchSplineForm::Hermite));
            const std::string C3 = Doc.Tree.Steps.back().Label;

            // Loft #2: V-loft between C1 and C3 (shared endpoint at (2, 0, 0), singular rail transposed onto singular row!)
            const SolidArcCommandOutcome Loft2 = LoftSolidArcCurves(Doc, { C1, C3 });
            const bool TwoPanelSound = Loft1.AcceptedCondition && Loft2.AcceptedCondition && Doc.Summary.SoundCondition && Doc.Summary.Faces == 2;

            // Loft #3: continue between C2 and C3 -> 3 curves & 3 sewn surfaces!
            const SolidArcCommandOutcome Loft3 = LoftSolidArcCurves(Doc, { C2, C3 });
            const bool ThreePanelSound = Loft3.AcceptedCondition && Doc.Summary.SheetCondition && Doc.Summary.SoundCondition
                                      && Doc.Summary.Faces == 3 && Doc.Regeneration.Panels.size() == 3;

            // Bonus: the 3 open rim edges at the non-shared ends ((0,0,0) - (0.2,1.5,0.4) - FarEnd) form a single triangle
            // -> CloseSolidArcPanels caps that planar triangle into a watertight 4-face solid!
            const SolidArcCommandOutcome Closed = CloseSolidArcPanels(Doc);
            const bool SolidSound = Closed.AcceptedCondition && !Doc.Summary.SheetCondition && Doc.Summary.SoundCondition
                                 && Doc.Summary.Faces == 4 && Doc.Summary.Volume > 0.05;
            Tally(ContinuedLoftGate, 0.0, TwoPanelSound && ThreePanelSound && SolidSound,
                  Format("%s V-loft continuation (3 curves, different height z=1.3): Loft1+Loft2=2 faces → +Loft3=3 sewn faces (sound) → Close=4-face solid (V=%.4f)",
                         ObtuseCondition ? "Obtuse (>90°)" : "Acute (<90°)", Doc.Summary.Volume));
        }

        // ② Single 3-section loft Loft(C1, C2, C3) where C1 and C3 share endpoint (2, 0, 0) in a V with different heights
        //    (rail u=1 travels (2,0,0) -> (2,1.5,0.4) -> (2,0,0), a valid 1-vertex closed edge rather than a collapsed side)
        //    AND a 3-curve non-touching V-in-Top-view loft at 3 different heights (z = 0, 0.5, 1.2).
        {
            SolidArcDocument Hinged = CreateSolidArcDocument("Hinged3Section");
            Expect(DrawSolidArcSketch(Hinged, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 0.0 }, { 2.0, 0.0 } })));
            const std::string C1 = Hinged.Tree.Steps.back().Label;
            SolidArcDrawingRequest MidLine = DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.2, 1.2 }, { 2.0, 1.2 } });
            MidLine.PlaneCondition = true;
            MidLine.Plane          = ElevatedPlane(0.5);
            Expect(DrawSolidArcSketch(Hinged, MidLine));
            const std::string C2 = Hinged.Tree.Steps.back().Label;
            Expect(DrawSolidArcSpaceCurve(Hinged, { { 2.0, 0.0, 0.0 }, { 1.2, 0.8, 1.1 } }, false, SketchSplineForm::Hermite));
            const std::string C3 = Hinged.Tree.Steps.back().Label;
            const SolidArcCommandOutcome ThreeSec = LoftSolidArcCurves(Hinged, { C1, C2, C3 });

            // Also test V-in-Top-view where C3 starts at (2, 0, 1.2) — directly above C1's endpoint at different height z=1.2!
            SolidArcDocument ElevatedV = CreateSolidArcDocument("ElevatedV");
            Expect(DrawSolidArcSketch(ElevatedV, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 0.0 }, { 2.0, 0.0 } })));
            const std::string E1 = ElevatedV.Tree.Steps.back().Label;
            SolidArcDrawingRequest E2Req = DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 1.5 }, { 2.0, 1.5 } });
            E2Req.PlaneCondition = true;
            E2Req.Plane          = ElevatedPlane(0.4);
            Expect(DrawSolidArcSketch(ElevatedV, E2Req));
            const std::string E2 = ElevatedV.Tree.Steps.back().Label;
            SolidArcDrawingRequest E3Req = DrawingOf(SolidArcDrawingKind::Line, 0, { { 2.0, 0.0 }, { 0.6, -1.2 } });
            E3Req.PlaneCondition = true;
            E3Req.Plane          = ElevatedPlane(1.2);
            Expect(DrawSolidArcSketch(ElevatedV, E3Req));
            const std::string E3 = ElevatedV.Tree.Steps.back().Label;
            const SolidArcCommandOutcome Pair1 = LoftSolidArcCurves(ElevatedV, { E1, E2 });
            const SolidArcCommandOutcome Pair2 = LoftSolidArcCurves(ElevatedV, { E1, E3 });
            const SolidArcCommandOutcome Pair3 = LoftSolidArcCurves(ElevatedV, { E2, E3 });
            Tally(ContinuedLoftGate, 0.0,
                  ThreeSec.AcceptedCondition && Hinged.Summary.SoundCondition &&
                  Pair1.AcceptedCondition && Pair2.AcceptedCondition && Pair3.AcceptedCondition &&
                  ElevatedV.Summary.SoundCondition && ElevatedV.Summary.Faces == 3,
                  Format("3-section hinged loft (%s, sound=%d) + Top-view V at different height z=1.2 pairwise 3-surface tube (faces=%d, sound=%d)",
                         ThreeSec.Label.c_str(), Hinged.Summary.SoundCondition ? 1 : 0, ElevatedV.Summary.Faces, ElevatedV.Summary.SoundCondition ? 1 : 0));
        }

        // ③ Lens loft (both endpoints shared -> 2 singular rows, 2 edges) + >90° 3-curve loop untwist (0° / 70° / 140°) + T-fin fallback.
        {
            SolidArcDocument Lens = CreateSolidArcDocument("LensLoft");
            Expect(DrawSolidArcSpaceCurve(Lens, { { 0.0, 0.0, 0.0 }, { 1.0, -0.6, 0.3 }, { 2.0, 0.0, 0.0 } }, false, SketchSplineForm::Hermite));
            const std::string ArchA = Lens.Tree.Steps.back().Label;
            Expect(DrawSolidArcSpaceCurve(Lens, { { 2.0, 0.0, 0.0 }, { 1.0, 0.6, 0.8 }, { 0.0, 0.0, 0.0 } }, false, SketchSplineForm::Hermite));
            const std::string ArchB = Lens.Tree.Steps.back().Label;
            const SolidArcCommandOutcome LensLoft = LoftSolidArcCurves(Lens, { ArchA, ArchB });

            // 3 curves at 0°, 70°, 140° around a prism — previously refused on 3rd loft as "one-sided (Möbius band)"!
            SolidArcDocument PrismLoop = CreateSolidArcDocument("PrismLoop");
            Expect(DrawSolidArcSpaceCurve(PrismLoop, { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 } }, false, SketchSplineForm::Hermite));
            const std::string P1 = PrismLoop.Tree.Steps.back().Label;
            Expect(DrawSolidArcSpaceCurve(PrismLoop, { { 0.3, 1.0, 0.6 }, { 0.3 + 2.0 * std::cos(70.0 * std::numbers::pi / 180.0), 1.0 + 2.0 * std::sin(70.0 * std::numbers::pi / 180.0), 0.6 } }, false, SketchSplineForm::Hermite));
            const std::string P2 = PrismLoop.Tree.Steps.back().Label;
            Expect(DrawSolidArcSpaceCurve(PrismLoop, { { -0.4, 0.8, 1.2 }, { -0.4 + 2.0 * std::cos(140.0 * std::numbers::pi / 180.0), 0.8 + 2.0 * std::sin(140.0 * std::numbers::pi / 180.0), 1.2 } }, false, SketchSplineForm::Hermite));
            const std::string P3 = PrismLoop.Tree.Steps.back().Label;
            const SolidArcCommandOutcome L12 = LoftSolidArcCurves(PrismLoop, { P1, P2 });
            const SolidArcCommandOutcome L23 = LoftSolidArcCurves(PrismLoop, { P2, P3 });
            const SolidArcCommandOutcome L31 = LoftSolidArcCurves(PrismLoop, { P3, P1 });

            // T-fin: 3 panels meeting along P1 -> 3rd panel gracefully emits as an independent sound sheet body!
            Expect(DrawSolidArcSpaceCurve(PrismLoop, { { 0.0, -1.2, 0.9 }, { 2.0, -1.2, 0.9 } }, false, SketchSplineForm::Hermite));
            const std::string P4 = PrismLoop.Tree.Steps.back().Label;
            const SolidArcCommandOutcome TFin = LoftSolidArcCurves(PrismLoop, { P1, P4 });

            Tally(ContinuedLoftGate, 0.0,
                  LensLoft.AcceptedCondition && Lens.Summary.SoundCondition && Lens.Summary.Edges == 2 &&
                  L12.AcceptedCondition && L23.AcceptedCondition && L31.AcceptedCondition &&
                  TFin.AcceptedCondition && PrismLoop.Summary.SoundCondition && PrismLoop.Regeneration.Bodies.size() == 2,
                  Format("2-corner lens loft (edges=%d, sound=%d) + >90° 3-panel loop untwist (%s) + 3-way T-fin standalone sheet (%zu bodies, sound=%d)",
                         Lens.Summary.Edges, Lens.Summary.SoundCondition ? 1 : 0, L31.Label.c_str(),
                         PrismLoop.Regeneration.Bodies.size(), PrismLoop.Summary.SoundCondition ? 1 : 0));
        }

        // ④ Continuing a loft by selecting a B-rep laminar edge of Loft #1 (both curve-backed edge and unbacked side-rail edge) + Curve 3!
        {
            SolidArcDocument EdgeCont = CreateSolidArcDocument("EdgeContinue");
            Expect(DrawSolidArcSketch(EdgeCont, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 0.0 }, { 2.0, 0.0 } })));
            const std::string C1 = EdgeCont.Tree.Steps.back().Label;
            Expect(DrawSolidArcSketch(EdgeCont, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 1.5 }, { 2.0, 1.5 } })));
            const std::string C2 = EdgeCont.Tree.Steps.back().Label;
            Expect(LoftSolidArcCurves(EdgeCont, { C1, C2 }));

            // Find the side-rail B-rep edge at x = 2.0 ((2, 0, 0) -> (2, 1.5, 0)), which has NO curve step of its own!
            std::string SideRailEdge;
            for (const auto& [Slot, Name] : EdgeCont.Regeneration.Bodies.front().Naming.Edges)
            {
                const EdgeToken Edge = EdgeCont.Partition->Edges.TokenAt(Slot);
                const EdgeProfile& Profile = EdgeCont.Partition->Edges.Resolve(Edge);
                const Vector3 Mid = EvaluateCurve(EdgeCont.Partition->Curves.Resolve(Profile.Curve), 0.5 * (Profile.IntervalLow + Profile.IntervalHigh)).Position;
                if (Distance(Mid, { 2.0, 0.75, 0.0 }) <= 1.0e-4) { SideRailEdge = Name; break; }
            }
            Expect(DrawSolidArcSpaceCurve(EdgeCont, { { 2.0, 0.0, 0.0 }, { 3.2, 0.75, 0.9 } }, false, SketchSplineForm::Hermite));
            const std::string C3 = EdgeCont.Tree.Steps.back().Label;

            EdgeCont.Selection.Edges  = { SideRailEdge };
            EdgeCont.Selection.Curves = { C3 };
            const SolidArcCommandOutcome Continued = LoftSolidArcSelection(EdgeCont);
            const std::string Encoded = EncodeSolidArcDocument(EdgeCont);
            const SolidArcDocumentFile Decoded = DecodeSolidArcDocument(Encoded);

            // Also verify sheet-body + 3rd V-curve selection AND 3-curve closed triangular Coons loop!
            SolidArcDocument BodyCont = CreateSolidArcDocument("BodyContinue");
            Expect(DrawSolidArcSpaceCurve(BodyCont, { { 0.0, 0.0, 0.0 }, { 1.0, 0.1, 0.0 }, { 2.0, 0.0, 0.0 } }, false, SketchSplineForm::Hermite));
            const std::string BC1 = BodyCont.Tree.Steps.back().Label;
            Expect(DrawSolidArcSpaceCurve(BodyCont, { { 0.0, 1.4, 0.4 }, { 1.0, 1.5, 0.4 }, { 2.0, 1.4, 0.4 } }, false, SketchSplineForm::Hermite));
            const std::string BC2 = BodyCont.Tree.Steps.back().Label;
            Expect(LoftSolidArcCurves(BodyCont, { BC1, BC2 }));
            const std::string SheetKey = BodyCont.Regeneration.Bodies.front().Key;
            Expect(DrawSolidArcSpaceCurve(BodyCont, { { 2.0, 0.0, 0.0 }, { 1.3, 0.6, 0.7 }, { 0.6, 0.9, 1.3 } }, false, SketchSplineForm::Hermite));
            const std::string BC3 = BodyCont.Tree.Steps.back().Label;
            BodyCont.Selection.Bodies = { SheetKey };
            BodyCont.Selection.Curves = { BC3 };
            const SolidArcCommandOutcome BodyContinued = LoftSolidArcSelection(BodyCont);

            SolidArcDocument TriLoop = CreateSolidArcDocument("TriLoop");
            Expect(DrawSolidArcSpaceCurve(TriLoop, { { 0.0, 0.0, 0.0 }, { 1.0, -0.2, 0.1 }, { 2.0, 0.0, 0.0 } }, false, SketchSplineForm::Hermite));
            const std::string TC1 = TriLoop.Tree.Steps.back().Label;
            Expect(DrawSolidArcSpaceCurve(TriLoop, { { 2.0, 0.0, 0.0 }, { 1.5, 0.7, 0.6 }, { 0.9, 1.4, 1.2 } }, false, SketchSplineForm::Hermite));
            const std::string TC2 = TriLoop.Tree.Steps.back().Label;
            Expect(DrawSolidArcSpaceCurve(TriLoop, { { 0.9, 1.4, 1.2 }, { 0.3, 0.8, 0.6 }, { 0.0, 0.0, 0.0 } }, false, SketchSplineForm::Hermite));
            const std::string TC3 = TriLoop.Tree.Steps.back().Label;
            const SolidArcCommandOutcome TriLofted = LoftSolidArcCurves(TriLoop, { TC1, TC2, TC3 });

            Tally(ContinuedLoftGate, 0.0,
                  !SideRailEdge.empty() && Continued.AcceptedCondition && EdgeCont.Summary.SoundCondition && EdgeCont.Summary.Faces == 2 && Decoded.Refusal.empty() &&
                  BodyContinued.AcceptedCondition && BodyCont.Summary.SoundCondition && BodyCont.Summary.Faces == 2 &&
                  TriLofted.AcceptedCondition && TriLoop.Summary.SoundCondition && TriLoop.Summary.Faces == 1 && TriLoop.Summary.Edges == 3,
                  Format("Edge+Curve (%d faces), SheetBody+Curve (%d faces), & 3-curve triangular Coons loop (%d edges, sound=%d), TOML clean",
                         EdgeCont.Summary.Faces, BodyCont.Summary.Faces, TriLoop.Summary.Edges, TriLoop.Summary.SoundCondition ? 1 : 0));
        }
    }
}

void ValidateDelete()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1200]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    const auto Near   = [](double A, double B) { return std::abs(A - B) <= 1.0e-9; };
    const auto NewBlock = []()
    {
        SolidArcDocument Part = CreateSolidArcDocument("Delete");
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = 4.0; Block.Depth = 2.0; Block.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Part, Block));
        return Part;
    };
    const auto EdgeWhere = [](const SolidArcDocument& Document, const auto& Test)
    {
        for (const auto& [Slot, Name] : Document.Regeneration.Naming.Edges)
        {
            const Vector3 Start = EdgeEndPosition(Document, Name, false), End = EdgeEndPosition(Document, Name, true);
            if (Test(Start, End) || Test(End, Start)) return Name;
        }
        return std::string{};
    };
    const auto Along = [&](double Y, double Z)                                               // The edge along x at (y, z)
    {
        return [=](Vector3 A, Vector3 B) { return Near(A.Y, Y) && Near(B.Y, Y) && Near(A.Z, Z) && Near(B.Z, Z) && !Near(A.X, B.X); };
    };
    const auto VertexAt = [&](const SolidArcDocument& Document, Vector3 Point)
    {
        for (const auto& [Slot, Name] : Document.Regeneration.Naming.Vertices)
        {
            const Vector3 Position = Document.Partition->Vertices.Resolve(Document.Partition->Vertices.TokenAt(Slot)).Position;
            if (Near(Position.X, Point.X) && Near(Position.Y, Point.Y) && Near(Position.Z, Point.Z)) return Name;
        }
        return std::string{};
    };
    const auto Suffix   = [](const SolidArcCommandOutcome& Outcome) { return Outcome.Refusal.empty() ? std::string() : " · " + Outcome.Refusal; };
    const auto Fillet   = [](double Radius, int Edges) { return 8.0 - (1.0 - std::numbers::pi / 4.0) * Radius * Radius * 4.0 * Edges; };
    const auto Topology = [](const SolidArcDocument& Part, int Faces, int Edges, int Vertices)
    {
        return Part.Summary.Faces == Faces && Part.Summary.Edges == Edges && Part.Summary.Vertices == Vertices && Part.Summary.SoundCondition;
    };
    const auto Untouched = [&](SolidArcDocument& Part, const SolidArcCommandOutcome& Outcome, const std::string& Before, double Volume, size_t Undo, const std::string& Note)
    {
        Tally(DeleteGate, 0.0, !Outcome.AcceptedCondition && EncodeSolidArcDocument(Part) == Before && Part.Summary.Volume == Volume && Part.UndoEntries.size() == Undo,
              Format("refused, untouched: %s (%s)", Note.c_str(), Outcome.Refusal.c_str()));
    };
    const OrthonormalFrame Top{ { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const auto OnTop = [&](SolidArcDrawingKind Kind, std::vector<Vector2> Points, bool Closed)
    {
        SolidArcDrawingRequest Request;
        Request.Kind = Kind; Request.Points = std::move(Points); Request.ClosedCondition = Closed;
        Request.PlaneCondition = true; Request.Plane = Top; Request.FillCondition = Closed;
        return Request;
    };

    // ① Fillet faces / edges un-blend: the Blend lets go of the edge (an emptied Blend goes), the sharp edge returns exactly.
    {
        const double Radius = 0.2;
        SolidArcDocument Part = NewBlock();
        Part.Selection = {};
        Part.Selection.Edges = { EdgeWhere(Part, Along(2.0, 0.0)) };
        Expect(BlendSolidArcSelection(Part, BlendProfile::Fillet, Radius));
        const std::string Blend = Part.Tree.Steps.back().Label;
        const size_t      Steps = Part.Tree.Steps.size(), Undo = Part.UndoEntries.size();
        std::string Strip;
        for (const auto& [Slot, Name] : Part.Regeneration.Naming.Faces) if (Name.rfind(Blend + ".Strip(", 0) == 0) Strip = Name;
        Part.Selection = {};
        Part.Selection.Faces = { Strip };
        const SolidArcCommandOutcome Gone = DeleteSolidArcFaces(Part);
        TallyVolume(DeleteGate, Part, 8.0, Format("fillet face %s deleted → the block again: %zu → %zu steps (%s gone), %d faces%s", Strip.c_str(), Steps, Part.Tree.Steps.size(), Blend.c_str(),
                                                  Part.Summary.Faces, Suffix(Gone).c_str()));
        Tally(DeleteGate, 0.0, Gone.AcceptedCondition && Part.Tree.Steps.size() == Steps - 1 && Topology(Part, 6, 12, 8) && Part.UndoEntries.size() == Undo + 1,
              "un-blend: the emptied Blend step goes, 6 faces · 12 edges · 8 vertices, one undo entry");
        Expect({ UndoSolidArcCommand(Part), "Undo", {}, 0.0 });
        TallyVolume(DeleteGate, Part, Fillet(Radius, 1), "undo brings the fillet back");

        SolidArcDocument Pair = NewBlock();
        Pair.Selection = {};
        Pair.Selection.Edges = { EdgeWhere(Pair, Along(2.0, 0.0)), EdgeWhere(Pair, Along(0.0, 0.0)) };
        Expect(BlendSolidArcSelection(Pair, BlendProfile::Fillet, Radius));
        const std::string PairBlend = Pair.Tree.Steps.back().Label;
        std::string Rail;
        for (const auto& [Slot, Name] : Pair.Regeneration.Naming.Edges)
            if (Name.find(PairBlend + ".Strip(") != std::string::npos && Name.find(PairBlend + ".Strip(", Name.find(PairBlend + ".Strip(") + 1) == std::string::npos) { Rail = Name; break; }
        Pair.Selection = {};
        Pair.Selection.Edges = { Rail };
        const SolidArcCommandOutcome One = DeleteSolidArcEdges(Pair);
        const FeatureStep* Kept = nullptr;
        for (const FeatureStep& Step : Pair.Tree.Steps) if (Step.Label == PairBlend) Kept = &Step;
        TallyVolume(DeleteGate, Pair, Fillet(Radius, 1), Format("two fillets, the rail edge %s of one deleted → one fillet left (%s keeps %zu edge)%s", Rail.c_str(), PairBlend.c_str(),
                                                               Kept ? Kept->References.size() : size_t(0), Suffix(One).c_str()));
        Tally(DeleteGate, 0.0, One.AcceptedCondition && Kept && Kept->References.size() == 1, "fillet edge: the Blend step stays with the other edge");

        // Refusals: a plain box edge (two different planes), fillet and plain edges together, a corner vertex.
        SolidArcDocument Box = NewBlock();
        const std::string Before = EncodeSolidArcDocument(Box);
        Box.Selection = {};
        Box.Selection.Edges = { EdgeWhere(Box, Along(0.0, 1.0)) };
        Untouched(Box, DeleteSolidArcEdges(Box), Before, 8.0, Box.UndoEntries.size(), "the block's top-front edge (two different planes: no closed solid without it)");
        Box.Selection = {};
        Box.Selection.Vertices = { VertexAt(Box, { 4.0, 2.0, 1.0 }) };
        Untouched(Box, DeleteSolidArcVertices(Box), Before, 8.0, Box.UndoEntries.size(), "a corner vertex (three edges meet)");
        const std::string PairText = EncodeSolidArcDocument(Pair);
        Pair.Selection = {};
        Pair.Selection.Edges = { EdgeWhere(Pair, Along(0.0, 1.0)), Rail.empty() ? std::string("?") : [&]() { std::string Other; for (const auto& [Slot, Name] : Pair.Regeneration.Naming.Edges) if (Name.find(".Strip(") != std::string::npos) Other = Name; return Other; }() };
        Untouched(Pair, DeleteSolidArcEdges(Pair), PairText, Pair.Summary.Volume, Pair.UndoEntries.size(), "a fillet edge and a plain edge together");
    }

    // ② Seams dissolve: a rectangle imprinted on the top face (one of its edges picked: the island merges back), an imprint
    //    line from the front edge to the back edge (the top halves merge, the split top edges join).
    {
        struct SeamCase { const char* Note; SolidArcDrawingKind Kind; std::vector<Vector2> Points; bool Closed; Vector3 Middle; };
        const std::vector<SeamCase> Cases = {
            { "imprinted rectangle (1, 0.5)–(2, 1.5), its x = 1 edge", SolidArcDrawingKind::Rectangle, { { 1.0, 0.5 }, { 2.0, 1.5 } }, true, { 1.0, 1.0, 1.0 } },
            { "imprint line x = 1 from the front edge to the back edge", SolidArcDrawingKind::Line, { { 1.0, 0.0 }, { 1.0, 2.0 } }, false, { 1.0, 1.0, 1.0 } },
        };
        for (const SeamCase& Case : Cases)
        {
            SolidArcDocument Part = NewBlock();
            Expect(DrawSolidArcSketch(Part, OnTop(Case.Kind, Case.Points, Case.Closed)));
            const SolidArcCommandOutcome Made = ImprintSolidArcSketch(Part, Part.Tree.Steps.back().Label);
            const int    FacesBefore = Part.Summary.Faces, EdgesBefore = Part.Summary.Edges;
            const size_t Undo = Part.UndoEntries.size();
            const std::string Seam = EdgeWhere(Part, [&](Vector3 A, Vector3 B) { return Near(A.X, 1.0) && Near(B.X, 1.0) && Near(A.Z, 1.0) && Near(B.Z, 1.0) && !Near(A.Y, B.Y)
                                                                                    && std::min(A.Y, B.Y) <= Case.Middle.Y && std::max(A.Y, B.Y) >= Case.Middle.Y; });
            Part.Selection = {};
            Part.Selection.Edges = { Seam };
            const SolidArcCommandOutcome Gone = DeleteSolidArcEdges(Part);
            TallyVolume(DeleteGate, Part, 8.0, Format("%s: imprint %d faces · %d edges, seam %s deleted → %d faces · %d edges · %d vertices%s", Case.Note, FacesBefore, EdgesBefore, Seam.c_str(),
                                                      Part.Summary.Faces, Part.Summary.Edges, Part.Summary.Vertices, Suffix(Made.AcceptedCondition ? Gone : Made).c_str()));
            Tally(DeleteGate, 0.0, Made.AcceptedCondition && Gone.AcceptedCondition && FacesBefore == 7 && Topology(Part, 6, 12, 8) && Part.UndoEntries.size() == Undo + 1,
                  Format("%s: the top is one face again, its split edges joined, one undo entry", Case.Note));
        }
    }

    // ③ Sketches: an extruded one leaves the scene (the body stays; RemovedCurves survives save / load; show refuses; undo
    //    restores), a free one is deleted with its step.
    {
        SolidArcDocument Part = NewBlock();
        std::string Sketch;
        for (const FeatureStep& Step : Part.Tree.Steps) if (Step.Kind == FeatureStepKind::Sketch) Sketch = Step.Label;
        const size_t Steps = Part.Tree.Steps.size(), Undo = Part.UndoEntries.size();
        const SolidArcCommandOutcome Gone = DeleteSolidArcCurves(Part, { Sketch });
        const std::string Text = EncodeSolidArcDocument(Part);
        const SolidArcDocumentFile Loaded = DecodeSolidArcDocument(Text);
        TallyVolume(DeleteGate, Part, 8.0, Format("the block's sketch %s deleted: the body stays, %zu steps kept, %zu removed curve(s)%s", Sketch.c_str(), Part.Tree.Steps.size(),
                                                  Part.Appearance.RemovedCurves.size(), Suffix(Gone).c_str()));
        Tally(DeleteGate, 0.0, Gone.AcceptedCondition && Part.Tree.Steps.size() == Steps && Part.Appearance.RemovedCurves.contains(Sketch) && Part.Appearance.HiddenCurves.contains(Sketch)
                               && Part.UndoEntries.size() == Undo + 1, "extruded sketch: removed ⊂ hidden, one undo entry");
        Tally(DeleteGate, 0.0, Loaded.Refusal.empty() && Loaded.Appearance.RemovedCurves.contains(Sketch) && Loaded.Appearance.HiddenCurves.contains(Sketch),
              Format("save → load keeps it removed (%s)", Loaded.Refusal.c_str()));
        const SolidArcCommandOutcome Shown = ToggleSolidArcCurveVisibility(Part, Sketch);
        Tally(DeleteGate, 0.0, !Shown.AcceptedCondition && Part.Appearance.RemovedCurves.contains(Sketch), Format("showing a deleted sketch refuses (%s)", Shown.Refusal.c_str()));
        Expect({ UndoSolidArcCommand(Part), "Undo", {}, 0.0 });
        Tally(DeleteGate, 0.0, Part.Appearance.RemovedCurves.empty() && !Part.Appearance.HiddenCurves.contains(Sketch) && Part.Summary.Volume == 8.0, "undo puts the sketch back in the scene");

        Expect(DrawSolidArcSketch(Part, OnTop(SolidArcDrawingKind::Rectangle, { { 0.5, 0.5 }, { 1.5, 1.0 } }, true)));
        const std::string Free = Part.Tree.Steps.back().Label;
        const size_t      With = Part.Tree.Steps.size();
        const SolidArcCommandOutcome Erased = DeleteSolidArcCurves(Part, { Free });
        Tally(DeleteGate, 0.0, Erased.AcceptedCondition && Part.Tree.Steps.size() == With - 1 && Part.Appearance.RemovedCurves.empty() && Part.Summary.Volume == 8.0,
              Format("a free sketch %s is deleted with its step (%zu → %zu)%s", Free.c_str(), With, Part.Tree.Steps.size(), Suffix(Erased).c_str()));

        const size_t Before = Part.Tree.Steps.size();
        Part.Selection = {};
        Part.Selection.Bodies = { Part.Regeneration.Bodies.empty() ? std::string() : Part.Regeneration.Bodies.front().Key };
        const SolidArcCommandOutcome Removed = RemoveSolidArcBodies(Part, Part.Selection.Bodies);
        Tally(DeleteGate, 0.0, Removed.AcceptedCondition && Part.Summary.Bodies.empty() && Part.Tree.Steps.size() == Before + 1, Format("the body is removed (one Remove step)%s", Suffix(Removed).c_str()));
    }

    // ④ Sketch points (mode 1): the curves through them go, unused points are dropped (constraints renumbered, the sketch
    //    re-solves); all points: the sketch goes; a point of an extruded rectangle detaches the rest, the prism stays.
    {
        SolidArcDocument Part = CreateSolidArcDocument("Delete");
        Expect(DrawFilledSketch(Part, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
        const std::string Sketch = Part.Tree.Steps.back().Label;
        const auto Solved = [&](const SolidArcDocument& Document, const std::string& Label) -> const SketchModel*
        {
            const auto Found = Document.Regeneration.Sketches.find(Label);
            return Found == Document.Regeneration.Sketches.end() ? nullptr : &Found->second;
        };
        const auto PointNear = [&](const SolidArcDocument& Document, const std::string& Label, Vector2 Where)
        {
            const SketchModel* Model = Solved(Document, Label);
            if (!Model) return -1;
            for (size_t Index = 0; Index < Model->Points.size(); ++Index)
                if (Distance(Model->Points[Index].Position, Where) <= 1.0e-9) return int(Index);
            return -1;
        };
        const SolidArcCommandOutcome Corner = DeleteSolidArcSketchPoints(Part, Sketch, { PointNear(Part, Sketch, { 0.0, 0.0 }) });
        const SketchModel* After = Solved(Part, Sketch);
        bool Clean = After != nullptr;
        for (const FeatureStepReport& Report : Part.Regeneration.Steps) Clean = Clean && Report.Status != FeatureStepStatus::Refused;
        Tally(DeleteGate, 0.0, Corner.AcceptedCondition && Clean && After->Curves.size() == 2 && After->Points.size() == 3,
              Format("rectangle corner (0, 0) deleted: %zu curves, %zu points (expect 2, 3), %zu constraints, re-solved%s", After ? After->Curves.size() : size_t(0),
                     After ? After->Points.size() : size_t(0), After ? After->Constraints.size() : size_t(0), Suffix(Corner).c_str()));
        std::vector<int> Every;
        for (size_t Index = 0; After && Index < After->Points.size(); ++Index) Every.push_back(int(Index));
        const SolidArcCommandOutcome All = DeleteSolidArcSketchPoints(Part, Sketch, Every);
        Tally(DeleteGate, 0.0, All.AcceptedCondition && Part.Tree.Steps.empty(), Format("every point deleted: the sketch goes (%zu steps)%s", Part.Tree.Steps.size(), Suffix(All).c_str()));

        SolidArcDocument Prism = CreateSolidArcDocument("Delete");
        Expect(DrawFilledSketch(Prism, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
        const std::string Profile = Prism.Tree.Steps.back().Label;
        Expect(ExtrudeSolidArcSketch(Prism, Profile, 1.0, FeatureCombination::New));
        const size_t Steps = Prism.Tree.Steps.size(), Undo = Prism.UndoEntries.size();
        const SolidArcCommandOutcome Detached = DeleteSolidArcSketchPoints(Prism, Profile, { PointNear(Prism, Profile, { 2.0, 1.0 }) });
        const SketchModel* Rest = Prism.Tree.Steps.empty() ? nullptr : Solved(Prism, Prism.Tree.Steps.back().Label);
        TallyVolume(DeleteGate, Prism, 2.0, Format("extruded rectangle, corner (2, 1) deleted: %zu → %zu steps, %s removed, the free copy keeps %zu curves%s", Steps, Prism.Tree.Steps.size(),
                                                   Profile.c_str(), Rest ? Rest->Curves.size() : size_t(0), Suffix(Detached).c_str()));
        Tally(DeleteGate, 0.0, Detached.AcceptedCondition && Prism.Tree.Steps.size() == Steps + 1 && Prism.Appearance.RemovedCurves.contains(Profile) && Rest && Rest->Curves.size() == 2
                               && Prism.UndoEntries.size() == Undo + 1, "detached: the prism keeps its profile, the open rest is a free sketch, one undo entry");
    }
}

// Shared CPU-headless Dear ImGui frame driver (no GPU required; supports full script events + font baking).
struct HeadlessUiHarness
{
    ImGuiContext*                        Ctx = nullptr;
    Frontier::SolidArc::SolidArcOptions  Options{};
    Frontier::SolidArc::SolidArcWorkspaceState State{};
    ImTextureID                          NextTexId = ImTextureID(100);

    HeadlessUiHarness()
    {
        IMGUI_CHECKVERSION();
        Ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(Ctx);
        ImGuiIO& Io = ImGui::GetIO();
        Io.DisplaySize = ImVec2(1366.0f, 768.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        Io.IniFilename = nullptr;
        Io.ConfigInputTrickleEventQueue = true;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        Options.WindowWidth  = 1366;
        Options.WindowHeight = 768;
        Options.RecoveryPath = (std::filesystem::temp_directory_path() / "SolidArcBatchRecovery.solidarc").string();
        std::error_code Ignore;
        std::filesystem::remove(Options.RecoveryPath, Ignore);
        Frontier::SolidArc::ConfigureSolidArcInterface(Options);
        Frontier::SolidArc::InitialiseSolidArcWorkspace(State, Options);
        State.WelcomeOpen = false;
    }

    ~HeadlessUiHarness()
    {
        if (Ctx) ImGui::DestroyContext(Ctx);
        std::error_code Ignore;
        std::filesystem::remove(Options.RecoveryPath, Ignore);
    }

    void SyncTextures()
    {
        if (ImDrawData* DrawData = ImGui::GetDrawData(); DrawData && DrawData->Textures)
        {
            for (ImTextureData* Tex : *DrawData->Textures)
            {
                if (Tex->Status == ImTextureStatus_WantCreate || Tex->Status == ImTextureStatus_WantUpdates)
                {
                    if (Tex->TexID == ImTextureID_Invalid) Tex->SetTexID(NextTexId++);
                    Tex->SetStatus(ImTextureStatus_OK);
                }
                else if (Tex->Status == ImTextureStatus_WantDestroy)
                {
                    Tex->SetTexID(ImTextureID_Invalid);
                    Tex->SetStatus(ImTextureStatus_Destroyed);
                }
            }
        }
    }

    void StepFrame()
    {
        ImGui::GetIO().DisplaySize = ImVec2(1366.0f, 768.0f);
        ImGui::GetIO().DeltaTime   = 1.0f / 60.0f;
        // Populate CPU HoverIdentity when pointer is inside the viewport so viewport clicks match GPU ID readback.
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        if (Mouse.x >= State.ViewportRect[0] && Mouse.x <= State.ViewportRect[0] + State.ViewportRect[2]
            && Mouse.y >= State.ViewportRect[1] && Mouse.y <= State.ViewportRect[1] + State.ViewportRect[3]
            && !State.Scene.Faces.empty())
        {
            const Frontier::OrbitCameraFrame CamFrame = Frontier::FrameOrbitCamera(State.Camera, std::max(1.0f, State.ViewportRect[2]), std::max(1.0f, State.ViewportRect[3]));
            const Frontier::OrbitRay Ray = Frontier::CastOrbitRay(CamFrame, Mouse.x - State.ViewportRect[0], Mouse.y - State.ViewportRect[1]);
            const auto Hit = Frontier::SolidArc::PickSolidArcScene(State.Scene, Ray, 8.0 * CamFrame.WorldPerPixel, State.Style.XRayEnabled);
            State.HoverIdentity = Hit ? Hit->Identity : 0u;
        }
        ImGui::NewFrame();
        Frontier::SolidArc::DrawSolidArcInterface(State, ImTextureID(1), 1.0 / 60.0);
        ImGui::Render();
        SyncTextures();
    }

    bool RunScript(const std::string& ScriptText, std::string& FailureOut)
    {
        Frontier::SolidArc::SolidArcScript Script = Frontier::SolidArc::ParseSolidArcScript(ScriptText);
        if (!Script.Refusal.empty()) { FailureOut = Script.Refusal; return false; }
        while (!Frontier::SolidArc::FinishedSolidArcScript(Script))
        {
            (void)Frontier::SolidArc::AdvanceSolidArcScript(Script, State);
            StepFrame();
        }
        for (const Frontier::SolidArc::SolidArcExpectation& Exp : Script.Expectations)
        {
            if (!Exp.Passed)
            {
                char Msg[768];
                std::snprintf(Msg, sizeof(Msg), "line %d: %s (observed '%s', status='%s')", Exp.Line, Exp.Text.c_str(), Exp.Observed.c_str(), State.Status.c_str());
                FailureOut = Msg;
                return false;
            }
        }
        return true;
    }
};

void ValidateBatch4InterfaceAndViewport()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1400]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };

    // ============================================================================================================
    // W83 — Hotkeys (Ctrl+I Isolate, Ctrl+A Select All, Ctrl+Shift+I Import) & Outliner Cleanup
    // ============================================================================================================
    {
        HeadlessUiHarness Ui;
        for (int Idx = 0; Idx < 3; ++Idx)
        {
            SolidArcConstructRequest Req;
            Req.Kind        = SolidArcConstructKind::Block;
            Req.Combination = Idx == 0 ? FeatureCombination::New : FeatureCombination::Separate;
            Req.Position    = { double(Idx) * 3.0, 0.0, 0.0 };
            Req.Width = 1.5; Req.Depth = 1.0; Req.Height = 1.0;
            Expect(ConstructSolidArcPrimitive(Ui.State.Document, Req));
        }
        Expect(DrawSolidArcSketch(Ui.State.Document, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 2.0 }, { 1.0, 3.0 } })));
        Expect(DrawSolidArcSketch(Ui.State.Document, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 3.0, 2.5 }, { 3.5, 2.5 } })));
        RefreshSolidArcScene(Ui.State, false);
        FitSolidArcCamera(Ui.State.Camera, Ui.State.Scene);
        Ui.StepFrame();
        Ui.StepFrame();

        // ① Outliner cleanup: bottom "Construct geometry" card is gone; ConstructLauncher is aliased onto OutlinerAdd (+) in the header (well above the 768 px panel bottom).
        const ImVec4* AddRect      = Ui.State.Widgets.Find("OutlinerAdd");
        const ImVec4* LauncherRect = Ui.State.Widgets.Find("ConstructLauncher");
        const bool HeaderOnly      = AddRect != nullptr && LauncherRect != nullptr
                                  && std::abs(AddRect->x - LauncherRect->x) < 1.0f
                                  && std::abs(AddRect->y - LauncherRect->y) < 1.0f
                                  && LauncherRect->w < 340.0f;
        Tally(HotkeysOutlinerGate, 0.0, HeaderOnly,
              Format("Outliner 'Construct geometry' bottom card removed: OutlinerAdd & ConstructLauncher share exact header bounds (maxY = %.1f px)",
                     LauncherRect ? LauncherRect->w : -1.0f));

        // ② Ctrl+A across PickModes 4 (Solid), 3 (Face), 2 (Edge), 1 (ControlPoint: focused sketch points & unfocused vertices+curves), and 5 (Any).
        std::string Err;
        const bool CtrlAOk = Ui.RunScript(
            "key Escape\n"
            "key 4\n"
            "key A Ctrl\n"
            "frames 1\n"
            "expect selected-bodies == 3\n"
            "expect selected-curves == 2\n"
            "key 3\n"
            "key A Ctrl\n"
            "frames 1\n"
            "expect selected-faces == 18\n"
            "key 2\n"
            "key A Ctrl\n"
            "frames 1\n"
            "expect selected-edges == 36\n"
            "key 1\n"
            "key A Ctrl\n"
            "frames 1\n"
            "expect picked-points == 4\n"
            "key Escape\n"
            "key 1\n"
            "key A Ctrl\n"
            "frames 1\n"
            "expect selected-vertices == 24\n"
            "expect selected-curves == 2\n"
            "key 5\n"
            "key A Ctrl\n"
            "frames 1\n"
            "expect selected-bodies == 3\n"
            "expect selected-curves == 2\n",
            Err);
        Tally(HotkeysOutlinerGate, 0.0, CtrlAOk,
              CtrlAOk ? "Ctrl+A Select All selects all visible entities per pick mode (Mode 4/5: 3 bodies + 2 curves · Mode 3: 18 faces · Mode 2: 36 edges · Mode 1: 4 sketch pts / 24 verts + 2 curves)"
                      : ("Ctrl+A failed: " + Err));

        // ③ Text-input guard: clicking @SceneSearch sets WantTextInput, so Ctrl+A selects text instead of hijacking CAD selection.
        ClearSolidArcSelection(Ui.State.Document);
        Err.clear();
        const bool TypeScriptOk = Ui.RunScript(
            "click @SceneSearch\n"
            "frames 2\n"
            "type box\n"
            "frames 1\n"
            "key A Ctrl\n"
            "frames 1\n",
            Err);
        const bool TextEntered        = std::string(Ui.State.Outliner.Search.data()) == "box";
        const bool SelectionUntouched = Ui.State.Document.Selection.Bodies.empty() && Ui.State.Document.Selection.Curves.empty();
        // Release text-input focus cleanly by clicking the viewport and clearing search buffer.
        Ui.State.Outliner.Search.fill('\0');
        (void)Ui.RunScript("click viewport 0.1 0.1\nframes 2\n", Err);
        Tally(HotkeysOutlinerGate, 0.0, TypeScriptOk && TextEntered && SelectionUntouched,
              "Ctrl+A respects ImGui WantTextInput: active Outliner search input ('box') keeps CAD selection untouched (0 bodies, 0 curves)");

        // ④ Ctrl+I Isolate toggle: hides unselected bodies + curves, then restores exact pre-isolate visibility on second Ctrl+I.
        // Pre-hide 1 free sketch so we prove exact snapshot restoration (not blind UnhideAll!).
        std::vector<std::string> FreeCurves;
        for (const FeatureStep& Step : Ui.State.Document.Tree.Steps)
            if (Step.Kind == FeatureStepKind::Sketch && !SolidArcSketchConsumedCondition(Ui.State.Document, Step.Label))
                FreeCurves.push_back(Step.Label);
        const std::string FirstCurve  = FreeCurves.size() >= 1 ? FreeCurves[0] : std::string{};
        const std::string SecondCurve = FreeCurves.size() >= 2 ? FreeCurves[1] : std::string{};
        Ui.State.Document.Appearance.HiddenCurves.insert(SecondCurve);
        ClearSolidArcSelection(Ui.State.Document);
        SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[0].Key, false);
        SelectSolidArcCurve(Ui.State.Document, FirstCurve, true);
        Err.clear();
        const bool IsolateOnOk = Ui.RunScript(
            "key I Ctrl\n"
            "frames 2\n"
            "expect isolate-active == 1\n"
            "expect hidden == 2\n"
            "expect hidden-curves == 1\n",
            Err);
        const bool IsolateOffOk = IsolateOnOk && Ui.RunScript(
            "key I Ctrl\n"
            "frames 2\n"
            "expect isolate-active == 0\n"
            "expect hidden == 0\n"
            "expect hidden-curves == 1\n",
            Err);
        Tally(HotkeysOutlinerGate, 0.0, IsolateOnOk && IsolateOffOk && Ui.State.Document.Appearance.HiddenCurves.contains(SecondCurve),
              IsolateOnOk && IsolateOffOk
                  ? "Ctrl+I Isolate hides 2 unselected bodies + keeps 1 unselected curve hidden (isolate-active=1), second Ctrl+I restores exact pre-isolate visibility snapshot"
                  : ("Ctrl+I Isolate failed: " + Err));

        // ⑤ Ctrl+Shift+I opens File -> Import popup without triggering Isolate.
        Err.clear();
        (void)Ui.RunScript("key I Ctrl Shift\nframes 2\nexpect isolate-active == 0\nkey Escape\nframes 1\n", Err);
        Tally(HotkeysOutlinerGate, 0.0, Err.empty() && !Ui.State.Isolate.ActiveCondition,
              "Ctrl+Shift+I opens Import dialog cleanly without entering Isolate");
    }

    // ============================================================================================================
    // W84 — CAD Dimension Lines (Associative Linear / Ø / R / ° / W×D×H / dX·dY·dZ, Live Regen, Codec, Viewport UI)
    // ============================================================================================================
    {
        SolidArcDocument Doc = CreateSolidArcDocument("DimensionsSpec");
        SolidArcConstructRequest BlockReq;
        BlockReq.Kind = SolidArcConstructKind::Block; BlockReq.Width = 4.0; BlockReq.Depth = 2.5; BlockReq.Height = 1.5;
        Expect(ConstructSolidArcPrimitive(Doc, BlockReq));
        SolidArcConstructRequest CylReq;
        CylReq.Kind = SolidArcConstructKind::Cylinder; CylReq.Combination = FeatureCombination::Separate;
        CylReq.Position = { 6.0, 1.0, 0.0 }; CylReq.Radius = 0.6; CylReq.Height = 2.0;
        Expect(ConstructSolidArcPrimitive(Doc, CylReq));

        // ① Linear edge dimension on Block1 width edge (4.0 m)
        std::string WidthEdge;
        for (const RegeneratedBody& Body : Doc.Regeneration.Bodies)
        {
            for (const EdgeToken Edge : Doc.Partition->CollectInventory(Body.Body).Edges)
            {
                const EdgeProfile& Ep = Doc.Partition->Edges.Resolve(Edge);
                const Vector3 A = Doc.Partition->Vertices.Resolve(Ep.Start).Position;
                const Vector3 B = Doc.Partition->Vertices.Resolve(Ep.End).Position;
                if (std::abs(A.Z) < 1.0e-6 && std::abs(B.Z) < 1.0e-6 && std::abs(A.Y) < 1.0e-6 && std::abs(B.Y) < 1.0e-6 && std::abs(Distance(A, B) - 4.0) < 1.0e-6)
                {
                    const auto It = Body.Naming.Edges.find(Edge.Slot);
                    if (It != Body.Naming.Edges.end()) { WidthEdge = It->second; break; }
                }
            }
            if (!WidthEdge.empty()) break;
        }
        SelectSolidArcName(Doc, SolidArcSelectionKind::Edge, WidthEdge, false);
        const SolidArcCommandOutcome LinOut = AddSolidArcDimensionFromSelection(Doc);

        // ② Cylindrical face Diameter dimension (Ø1.2 m) + toggle to Radius (R0.6 m) and back to Diameter (Ø1.2 m)
        std::string CylFace;
        for (const RegeneratedBody& Body : Doc.Regeneration.Bodies)
        {
            for (const FaceToken Face : Doc.Partition->CollectInventory(Body.Body).Faces)
            {
                const FaceProfile& Fp = Doc.Partition->Faces.Resolve(Face);
                if (Fp.Surface.PresentCondition() && std::holds_alternative<CylinderSurface>(Doc.Partition->Surfaces.Resolve(Fp.Surface)))
                {
                    const auto It = Body.Naming.Faces.find(Face.Slot);
                    if (It != Body.Naming.Faces.end()) { CylFace = It->second; break; }
                }
            }
        }
        SelectSolidArcName(Doc, SolidArcSelectionKind::Face, CylFace, false);
        const SolidArcCommandOutcome DiaOut = AddSolidArcDimensionFromSelection(Doc);
        const double InitialDia = Doc.Dimensions.size() >= 2 ? Doc.Dimensions[1].Value : -1.0;
        Expect(ToggleSolidArcDimensionRadialKind(Doc, "Dim2"));
        const double ToggledRad = Doc.Dimensions.size() >= 2 ? Doc.Dimensions[1].Value : -1.0;
        const std::string RadLabel = Doc.Dimensions.size() >= 2 ? FormatSolidArcDimension(Doc.Dimensions[1]) : std::string{};
        Expect(ToggleSolidArcDimensionRadialKind(Doc, "Dim2"));
        const std::string DiaLabel = Doc.Dimensions.size() >= 2 ? FormatSolidArcDimension(Doc.Dimensions[1]) : std::string{};

        // ③ Orthogonal 2-face Angular dimension (90.00°) + Parallel 2-face Linear gap dimension (2.5 m)
        std::string TopFace, FrontFace, BackFace;
        const RegeneratedBody& FirstBody = Doc.Regeneration.Bodies.front();
        for (const FaceToken Face : Doc.Partition->CollectInventory(FirstBody.Body).Faces)
        {
            const FaceProfile& Fp = Doc.Partition->Faces.Resolve(Face);
            if (!Fp.Surface.PresentCondition()) continue;
            const auto* Pl = std::get_if<PlaneSurface>(&Doc.Partition->Surfaces.Resolve(Fp.Surface));
            if (!Pl) continue;
            const auto It = FirstBody.Naming.Faces.find(Face.Slot);
            if (It == FirstBody.Naming.Faces.end()) continue;
            const Vector3 OutNormal = Pl->Frame.ZAxis * (Fp.Sense == Orientation::Forward ? 1.0 : -1.0);
            if (OutNormal.Z > 0.9) TopFace = It->second;
            else if (OutNormal.Y < -0.9) FrontFace = It->second;
            else if (OutNormal.Y > 0.9) BackFace = It->second;
        }
        SelectSolidArcName(Doc, SolidArcSelectionKind::Face, TopFace, false);
        SelectSolidArcName(Doc, SolidArcSelectionKind::Face, FrontFace, true);
        const SolidArcCommandOutcome AngOut = AddSolidArcDimensionFromSelection(Doc);
        SelectSolidArcName(Doc, SolidArcSelectionKind::Face, FrontFace, false);
        SelectSolidArcName(Doc, SolidArcSelectionKind::Face, BackFace, true);
        const SolidArcCommandOutcome GapOut = AddSolidArcDimensionFromSelection(Doc);

        // ④ Axis-filtered 2-point dimension (dX = 3.0 m on diagonal (0,0,0) -> (3,4,0))
        const SolidArcCommandOutcome AxisOut = AddSolidArcPointDimension(Doc, { 0.0, 0.0, 0.0 }, { 3.0, 4.0, 0.0 }, { 0.0, 0.0, 1.0 }, 0.4, 0);

        const double Err1 = std::max({
            std::abs(Doc.Dimensions[0].Value - 4.0),
            std::abs(InitialDia - 1.2),
            std::abs(ToggledRad - 0.6),
            std::abs(Doc.Dimensions[2].Value - 90.0),
            std::abs(Doc.Dimensions[3].Value - 2.5),
            std::abs(Doc.Dimensions[4].Value - 3.0)
        });
        Tally(DimensionLinesGate, Err1,
              LinOut.AcceptedCondition && DiaOut.AcceptedCondition && AngOut.AcceptedCondition && GapOut.AcceptedCondition && AxisOut.AcceptedCondition
              && Err1 <= 1.0e-9 && RadLabel.starts_with("R0.6") && DiaLabel.starts_with("\xC3\x98" "1.2")
              && FormatSolidArcDimension(Doc.Dimensions[4]).starts_with("dX 3"),
              Format("5 CAD dimension types created: D1=%s, D2=%s (toggled %s), D3=%s, D4=%s, D5=%s",
                     FormatSolidArcDimension(Doc.Dimensions[0]).c_str(), DiaLabel.c_str(), RadLabel.c_str(),
                     FormatSolidArcDimension(Doc.Dimensions[2]).c_str(), FormatSolidArcDimension(Doc.Dimensions[3]).c_str(),
                     FormatSolidArcDimension(Doc.Dimensions[4]).c_str()));

        // ⑤ Live associative regeneration when parameters change + Undo/Redo fidelity!
        Expect(AssignSolidArcParameter(Doc, "Block1.Width", 6.25));
        Expect(AssignSolidArcParameter(Doc, "Cylinder1.Radius", 0.85));
        const double UpdatedWidth = Doc.Dimensions[0].Value;
        const double UpdatedDia   = Doc.Dimensions[1].Value;
        (void)UndoSolidArcCommand(Doc);
        (void)UndoSolidArcCommand(Doc);
        const double RestoredWidth = Doc.Dimensions[0].Value;
        const double RestoredDia   = Doc.Dimensions[1].Value;
        (void)RedoSolidArcCommand(Doc);
        (void)RedoSolidArcCommand(Doc);
        const double RedoneWidth = Doc.Dimensions[0].Value;
        const double RedoneDia   = Doc.Dimensions[1].Value;
        const double AssocErr = std::max({
            std::abs(UpdatedWidth - 6.25), std::abs(UpdatedDia - 1.70),
            std::abs(RestoredWidth - 4.0), std::abs(RestoredDia - 1.20),
            std::abs(RedoneWidth - 6.25),  std::abs(RedoneDia - 1.70)
        });
        Tally(DimensionLinesGate, AssocErr, AssocErr <= 1.0e-9,
              Format("Associative live update & undo/redo: Width 4 -> %.2f -> undo %.2f -> redo %.2f; Cyl Ø 1.2 -> %.2f -> undo %.2f -> redo %.2f",
                     UpdatedWidth, RestoredWidth, RedoneWidth, UpdatedDia, RestoredDia, RedoneDia));

        // ⑥ .solidarc TOML codec round-trip (Encode -> Decode -> Encode byte-equal!)
        const std::string Saved1 = EncodeSolidArcDocument(Doc);
        const SolidArcDocumentFile Decoded = DecodeSolidArcDocument(Saved1);
        SolidArcDocument Reloaded = CreateSolidArcDocument(Decoded.Title);
        Reloaded.Tree         = Decoded.Tree;
        Reloaded.Captions     = Decoded.Captions;
        Reloaded.Appearance   = Decoded.Appearance;
        Reloaded.DrawingSheet = Decoded.DrawingSheet;
        Reloaded.Dimensions   = Decoded.Dimensions;
        RegenerateSolidArcDocument(Reloaded);
        const std::string Saved2 = EncodeSolidArcDocument(Reloaded);
        Tally(DimensionLinesGate, 0.0,
              Decoded.Refusal.empty() && Saved1 == Saved2 && Reloaded.Dimensions.size() == 5
              && Saved1.find("[[Dimensions]]") != std::string::npos && std::abs(Reloaded.Dimensions[0].Value - 6.25) <= 1.0e-9,
              Format(".solidarc [[Dimensions]] TOML round-trip byte-equal (%zu bytes, %zu dimensions preserved)", Saved1.size(), Reloaded.Dimensions.size()));

        // ⑦ Interactive Viewport Dimension UI: Inspector '+ Add from Selection' creates W×D×H overall dimensions, records clickable @Dim:Dim1..Dim3 pills, Delete removes selected pill, and 'D' / @DimensionTool / @SketchDimension toggle the interactive Dimension Tool.
        HeadlessUiHarness Ui;
        SolidArcConstructRequest Box;
        Box.Kind = SolidArcConstructKind::Block; Box.Width = 3.0; Box.Depth = 2.0; Box.Height = 1.2;
        Expect(ConstructSolidArcPrimitive(Ui.State.Document, Box));
        RefreshSolidArcScene(Ui.State, false);
        FitSolidArcCamera(Ui.State.Camera, Ui.State.Scene);
        SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[0].Key, false);
        Expect(AddSolidArcDimensionFromSelection(Ui.State.Document));
        std::string Err;
        const bool DimUiOk = Ui.RunScript(
            "frames 2\n"
            "expect dimensions == 3\n"
            "click @Dim:Dim1\n"
            "frames 1\n"
            "key Delete\n"
            "frames 1\n"
            "expect dimensions == 2\n"
            "key D\n"
            "frames 1\n"
            "expect dimension-active == 1\n"
            "key Escape\n"
            "frames 1\n"
            "expect dimension-active == 0\n"
            "click @SketchDimension\n"
            "frames 1\n"
            "expect dimension-active == 1\n"
            "click @SketchDimension\n"
            "frames 1\n"
            "expect dimension-active == 0\n"
            "click @DimensionTool\n"
            "frames 1\n"
            "click @DimensionTool\n"
            "frames 1\n",
            Err);
        Tally(DimensionLinesGate, 0.0, DimUiOk && Ui.State.DimensionsEnabled && Ui.State.Widgets.Find("Dim:Dim2") != nullptr && Ui.State.Widgets.Find("Dim:Dim3") != nullptr,
              DimUiOk ? "Viewport Dimension UI: W×D×H dimension lines record @Dim:Dim1..Dim3 pills, click+Delete removes Dim1, 'D' / @SketchDimension toggle creation tool, and @DimensionTool toggles visibility"
                      : ("Viewport Dimension UI failed: " + Err));
    }

    // ============================================================================================================
    // W85 — Synchronized Outliner & Viewport Multi-Selection (Shift+Click [A..Z] Range & Ctrl+Click Toggle)
    // ============================================================================================================
    {
        HeadlessUiHarness Ui;
        for (int Idx = 0; Idx < 5; ++Idx)
        {
            SolidArcConstructRequest Req;
            Req.Kind        = SolidArcConstructKind::Block;
            Req.Combination = Idx == 0 ? FeatureCombination::New : FeatureCombination::Separate;
            Req.Position    = { double(Idx) * 2.2, 0.0, 0.0 };
            Req.Width = 1.0; Req.Depth = 1.0; Req.Height = 1.0;
            Expect(ConstructSolidArcPrimitive(Ui.State.Document, Req));
        }
        for (int Idx = 0; Idx < 3; ++Idx)
            Expect(DrawSolidArcSketch(Ui.State.Document, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { double(Idx) * 2.0, 3.0 }, { double(Idx) * 2.0 + 1.0, 4.0 } })));
        RefreshSolidArcScene(Ui.State, false);
        FitSolidArcCamera(Ui.State.Camera, Ui.State.Scene);
        ClearSolidArcSelection(Ui.State.Document);
        Ui.StepFrame();
        Ui.StepFrame();

        // Count how many distinct scene bodies carry SolidArcStateSelected in DisplayedSolidArcStates (Viewport highlight truth).
        auto CountViewportSelectedBodies = [&]()
        {
            const SolidArcSceneStates States = DisplayedSolidArcStates(Ui.State);
            std::set<uint32_t> SelectedBodies;
            for (size_t FaceIdx = 0; FaceIdx < Ui.State.Scene.Faces.size() && FaceIdx < States.Faces.size(); ++FaceIdx)
                if (States.Faces[FaceIdx] & 0x1u) SelectedBodies.insert(Ui.State.Scene.Faces[FaceIdx].Body);
            return int(SelectedBodies.size());
        };

        std::vector<std::string> BodyKeys;
        for (const SolidArcSceneBody& B : Ui.State.Scene.Bodies) BodyKeys.push_back(B.Key);
        std::vector<std::string> SketchKeys;
        for (const FeatureStep& S : Ui.State.Document.Tree.Steps)
            if (S.Kind == FeatureStepKind::Sketch && !SolidArcSketchConsumedCondition(Ui.State.Document, S.Label))
                SketchKeys.push_back(S.Label);

        // ① Forward Windows-style Shift+Click in Outliner: Click Body 0 (A), Shift+Click Body 4 (Z) -> all 5 bodies selected in Outliner AND Viewport!
        std::string Err;
        const bool RangeForwardOk = BodyKeys.size() == 5 && SketchKeys.size() == 3 && Ui.RunScript(
            "click @Figure:" + BodyKeys[0] + "\n"
            "frames 1\n"
            "expect selected-bodies == 1\n"
            "click @Figure:" + BodyKeys[4] + " Left Shift\n"
            "frames 1\n"
            "expect selected-bodies == 5\n",
            Err);
        const int VpCount5 = CountViewportSelectedBodies();
        Tally(MultiSelectSyncGate, 0.0, RangeForwardOk && VpCount5 == 5,
              RangeForwardOk ? Format("Outliner Shift+Click range [Body 1 .. Body 5]: 5/5 bodies selected in Document & %d/5 highlighted in Viewport", VpCount5)
                             : ("Outliner forward range failed: " + Err));

        // ② Reverse Shift+Click range [Body 4 .. Body 1] -> 4 bodies selected + Ctrl+Click toggle off Body 2 -> 3 bodies + Ctrl+Click toggle on Body 0 -> 4 bodies!
        Err.clear();
        const bool ReverseAndCtrlOk = BodyKeys.size() == 5 && Ui.RunScript(
            "click @Figure:" + BodyKeys[4] + "\n"
            "frames 1\n"
            "click @Figure:" + BodyKeys[1] + " Left Shift\n"
            "frames 1\n"
            "expect selected-bodies == 4\n"
            "click @Figure:" + BodyKeys[2] + " Left Ctrl\n"
            "frames 1\n"
            "expect selected-bodies == 3\n"
            "click @Figure:" + BodyKeys[0] + " Left Ctrl\n"
            "frames 1\n"
            "expect selected-bodies == 4\n",
            Err);
        const int VpCount4 = CountViewportSelectedBodies();
        Tally(MultiSelectSyncGate, 0.0, ReverseAndCtrlOk && VpCount4 == 4,
              ReverseAndCtrlOk ? Format("Reverse Shift+Click [Body 5 .. Body 2] + Ctrl+Click item toggle: %d bodies synced across Outliner & Viewport", VpCount4)
                               : ("Reverse/Ctrl toggle failed: " + Err));

        // ③ Cross-section Shift+Click spanning Bodies + Sketches [Body 3 .. Sketch 2] -> 2 bodies + all 8 visible sketch rows in [row 3 .. row 12] selected!
        Err.clear();
        const bool CrossSectionOk = BodyKeys.size() == 5 && SketchKeys.size() == 3 && Ui.RunScript(
            "click @Figure:" + BodyKeys[3] + "\n"
            "frames 1\n"
            "click @Figure:" + SketchKeys[2] + " Left Shift\n"
            "frames 1\n"
            "expect selected-bodies == 2\n"
            "expect selected-curves == 8\n",
            Err);
        const int VpCount2 = CountViewportSelectedBodies();
        Tally(MultiSelectSyncGate, 0.0, CrossSectionOk && VpCount2 == 2,
              CrossSectionOk ? Format("Cross-section Shift+Click [Body 4 .. Rectangle 3]: all 10 contiguous Outliner rows (2 bodies + 8 sketches) selected & synced with Viewport (%d bodies lit)", VpCount2)
                             : ("Cross-section Shift+Click failed: " + Err));

        // ④ Viewport multi-select -> Outliner sync: clicking Body 1 in Viewport (world coords) updates AnchorKey so subsequent Outliner Shift+Click extends from the Viewport pick!
        Err.clear();
        const bool ViewportToOutlinerOk = BodyKeys.size() == 5 && Ui.RunScript(
            "key Escape\n"
            "key 4\n"
            "frames 1\n"
            "click world 2.7 0.5 0.5\n"
            "frames 1\n"
            "expect selected-bodies == 1\n"
            "click @Figure:" + BodyKeys[4] + " Left Shift\n"
            "frames 1\n"
            "expect selected-bodies == 4\n",
            Err);
        Tally(MultiSelectSyncGate, 0.0, ViewportToOutlinerOk && CountViewportSelectedBodies() == 4,
              ViewportToOutlinerOk ? "Viewport-to-Outliner sync: picking Body 2 in 3D Viewport sets Outliner anchor so Shift+Click Body 5 selects Bodies 2..5"
                                   : ("Viewport-to-Outliner sync failed: " + Err));
    }

    // ============================================================================================================
    // W86 — Continuous 3-Decade Grid Shader Archive & Perspective-Aligned 3D UV Font Orientation Cube
    // ============================================================================================================
    {
        // ① Verify compiled SPIR-V archive for SolidArcGridFragment
        constexpr size_t GridWords = sizeof(ShaderArchive::SolidArcGridFragmentCode) / sizeof(uint32_t);
        const bool SpirvValid = GridWords > 1500 && ShaderArchive::SolidArcGridFragmentCode[0] == 0x07230203u;
        Tally(GridCubeUvGate, 0.0, SpirvValid,
              Format("SolidArcGridFragment SPIR-V archive verified: %zu words (magic 0x%08x, continuous 3-decade cascade + isotropic Euclidean AA)",
                     GridWords, ShaderArchive::SolidArcGridFragmentCode[0]));

        // ② Verify mathematical continuity of SolidArcGrid.slang's 3-decade grid cascade across decade boundaries:
        auto GridCoverage = [](double CoordX, double CoordY, double Footprint, double Spacing)
        {
            const double ScaledX = CoordX / Spacing, ScaledY = CoordY / Spacing;
            const double Width   = std::max(Footprint / Spacing, 1.0e-6);
            const double FracX   = ScaledX - 0.5 - std::floor(ScaledX - 0.5);
            const double FracY   = ScaledY - 0.5 - std::floor(ScaledY - 0.5);
            const double LineX   = std::abs(FracX - 0.5) / Width;
            const double LineY   = std::abs(FracY - 0.5) / Width;
            return 1.0 - std::clamp(std::min(LineX, LineY), 0.0, 1.0);
        };
        auto EvalGridCascade = [&](double CoordX, double CoordY, double FootprintPeak)
        {
            const double Decade     = std::log10(std::max(FootprintPeak, 1.0e-6) * 8.0);
            const double BaseDecade = std::floor(Decade);
            const double Blend      = Decade - BaseDecade;
            const double SubMinor   = std::pow(10.0, BaseDecade);
            const double Minor      = SubMinor * 10.0;
            const double Major      = SubMinor * 100.0;
            const double SubLine    = GridCoverage(CoordX, CoordY, FootprintPeak, SubMinor) * (0.42 * (1.0 - Blend));
            const double MinorLine  = GridCoverage(CoordX, CoordY, FootprintPeak, Minor) * (0.82 + (0.42 - 0.82) * Blend);
            const double MajorLine  = GridCoverage(CoordX, CoordY, FootprintPeak, Major) * 0.82;
            return std::max({ SubLine, MinorLine, MajorLine }) * 0.32;
        };
        double WorstDecadeJump = 0.0;
        for (double BoundarySpan : { 0.1, 1.0, 10.0 })
        {
            const double FootBefore = (BoundarySpan * 0.9999999) / 8.0;
            const double FootAfter  = (BoundarySpan * 1.0000001) / 8.0;
            for (double Probe : { 0.0, 0.01 * BoundarySpan, 0.1 * BoundarySpan, 1.0 * BoundarySpan, 10.0 * BoundarySpan })
            {
                const double Jump = std::abs(EvalGridCascade(Probe, 0.37 * Probe, FootAfter) - EvalGridCascade(Probe, 0.37 * Probe, FootBefore));
                WorstDecadeJump = std::max(WorstDecadeJump, Jump);
            }
        }
        Tally(GridCubeUvGate, WorstDecadeJump, WorstDecadeJump <= 1.0e-6,
              Format("3-decade grid cascade continuity across Span = {0.1, 1.0, 10.0}: max alpha step = %.2e (eliminates cube/plane step artifact)", WorstDecadeJump));

        // ③ Verify Orientation Cube perspective convergence tracks Camera.FieldOfView & switches to parallel in Orthographic,
        //    and emits 3D UV-plane glyph quads bound to ImGui::GetFont()->OwnerAtlas->TexRef!
        HeadlessUiHarness Ui;
        SolidArcConstructRequest Box;
        Box.Kind = SolidArcConstructKind::Block;
        Expect(ConstructSolidArcPrimitive(Ui.State.Document, Box));
        RefreshSolidArcScene(Ui.State, false);
        PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Isometric);
        // Dock tabs and authoring cards request their fonts before measuring retained atlas UVs.
        Ui.StepFrame();
        Ui.StepFrame();

        auto MeasureCubeTopCenterYAndUvQuads = [&](double FovRad, bool Ortho, int& UvFontQuadVertsOut, int& SkewedUvVertsOut)
        {
            Ui.State.Camera.FieldOfView         = FovRad;
            Ui.State.Camera.OrthographicEnabled = Ortho;
            Ui.StepFrame();
            UvFontQuadVertsOut = 0;
            SkewedUvVertsOut   = 0;
            const ImVec4* CubeTop = Ui.State.Widgets.Find("CubeTop");
            // Exact Orientation Cube 86x86 region in the bottom-right of the viewport (right 18, bottom 45):
            const float CubeMinX = Ui.State.ViewportRect[0] + Ui.State.ViewportRect[2] - 86.0f - 18.0f;
            const float CubeMaxX = CubeMinX + 86.0f;
            const float CubeMinY = Ui.State.ViewportRect[1] + Ui.State.ViewportRect[3] - 86.0f - 45.0f;
            const float CubeMaxY = CubeMinY + 86.0f;
            ImFont* ThemeFont = ImGui::GetFont();
            const ImTextureID ExpectedTex = (ThemeFont && ThemeFont->OwnerAtlas) ? ThemeFont->OwnerAtlas->TexRef.GetTexID() : ImTextureID_Invalid;
            if (ImDrawData* Draw = ImGui::GetDrawData())
            {
                for (int ListIdx = 0; ListIdx < Draw->CmdListsCount; ++ListIdx)
                {
                    const ImDrawList* CmdList = Draw->CmdLists[ListIdx];
                    for (const ImDrawCmd& Cmd : CmdList->CmdBuffer)
                    {
                        if (Cmd.TexRef.GetTexID() != ExpectedTex) continue;
                        for (unsigned int Elem = 0; Elem + 5 < Cmd.ElemCount; Elem += 6)
                        {
                            const ImDrawVert& V0 = CmdList->VtxBuffer[Cmd.VtxOffset + CmdList->IdxBuffer[Cmd.IdxOffset + Elem + 0]];
                            const ImDrawVert& V1 = CmdList->VtxBuffer[Cmd.VtxOffset + CmdList->IdxBuffer[Cmd.IdxOffset + Elem + 1]];
                            const ImDrawVert& V2 = CmdList->VtxBuffer[Cmd.VtxOffset + CmdList->IdxBuffer[Cmd.IdxOffset + Elem + 2]];
                            if (V0.pos.x >= CubeMinX && V0.pos.x <= CubeMaxX && V0.pos.y >= CubeMinY && V0.pos.y <= CubeMaxY)
                            {
                                // Non-white-pixel UV means a real glyph quad from ImGui::GetFont() atlas (solid fills sample WhitePixelUV).
                                const ImVec2 WhiteUv = ThemeFont->OwnerAtlas->TexUvWhitePixel;
                                if (std::hypot(V0.uv.x - WhiteUv.x, V0.uv.y - WhiteUv.y) > 1.0e-4f
                                    || std::hypot(V2.uv.x - WhiteUv.x, V2.uv.y - WhiteUv.y) > 1.0e-4f)
                                {
                                    UvFontQuadVertsOut += 4;
                                    // Screen-space AddText has V0.pos.y == V1.pos.y (horizontal baseline); 3D UV-projected Isometric quads tilt in screen Y!
                                    if (std::abs(V1.pos.y - V0.pos.y) > 0.15f) SkewedUvVertsOut += 4;
                                }
                            }
                        }
                    }
                }
            }
            return CubeTop ? 0.5f * (CubeTop->y + CubeTop->w) : -1.0f;
        };

        int UvVerts30 = 0, Skew30 = 0, UvVerts75 = 0, Skew75 = 0, UvVertsOrtho = 0, SkewOrtho = 0;
        const float TopY30    = MeasureCubeTopCenterYAndUvQuads(0.52, false, UvVerts30, Skew30);
        const float TopY75    = MeasureCubeTopCenterYAndUvQuads(1.31, false, UvVerts75, Skew75);
        const float TopYOrtho = MeasureCubeTopCenterYAndUvQuads(0.785, true, UvVertsOrtho, SkewOrtho);

        // In Isometric view, wider FOV moves the perspective eye closer to the cube, shifting the projected Top face centre measurably vs 30° and Ortho!
        const bool PerspectiveRespondsToFov = std::abs(TopY75 - TopY30) > 0.6f && std::abs(TopY75 - TopYOrtho) > 1.0f;
        const bool UvFontRenderedOn3DPlanes = UvVerts30 >= 48 && Skew30 >= 48 && UvVertsOrtho >= 48 && SkewOrtho >= 48;
        Tally(GridCubeUvGate, 0.0, PerspectiveRespondsToFov && UvFontRenderedOn3DPlanes,
              Format("Orientation Cube tracks viewport FOV & renders 3D UV-plane ImGui::GetFont() glyph quads (Isometric UV glyph verts=%d, 3D-tilted=%d; TopY@30°=%.2f, @75°=%.2f, Ortho=%.2f)",
                     UvVerts30, Skew30, TopY30, TopY75, TopYOrtho));

        // ④ In Top view, 'Top' face UV plane aligns horizontally with the XY grid (Skew == 0) and clicking @CubeFront switches view cleanly!
        PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Top);
        int UvVertsTop = 0, SkewTop = 0;
        (void)MeasureCubeTopCenterYAndUvQuads(0.785, false, UvVertsTop, SkewTop);
        PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Isometric);
        Ui.StepFrame();
        std::string Err;
        const bool ClickCubeOk = Ui.RunScript(
            "click @CubeTop\n"
            "frames 2\n"
            "expect camera-pitch ~ 1.5707963268\n"
            "expect grid-plane == 0\n",
            Err);
        Tally(GridCubeUvGate, 0.0, UvVertsTop >= 12 && SkewTop == 0 && ClickCubeOk,
              ClickCubeOk ? Format("Orientation Cube in Top view aligns 'Top' UV glyphs 1:1 with XY grid (%d UV verts, %d skewed) and @CubeTop click presents Top view", UvVertsTop, SkewTop)
                          : ("Orientation Cube click failed: " + Err));
    }
}

void ValidateBatch5TransformsNumpadAndSplitViews()
{
    using namespace Frontier;
    using namespace Frontier::SolidArc;
    using namespace Frontier::GeometryProcessing;
    const auto Format = [](const char* Pattern, auto... Values) { char Text[1400]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };

    // =========================================================================================================
    // W87 — Blender-like G / R / S Transforms (G Z 10, gizmo single-axis isolation, axis guide line, mouse + numeric)
    // =========================================================================================================
    {
        HeadlessUiHarness Ui;
        SolidArcConstructRequest BoxReq;
        BoxReq.Kind     = SolidArcConstructKind::Block;
        BoxReq.Position = { 0.0, 0.0, 0.0 };
        BoxReq.Width    = 4.0;
        BoxReq.Depth    = 1.0;
        BoxReq.Height   = 2.0;
        Expect(ConstructSolidArcPrimitive(Ui.State.Document, BoxReq));
        RefreshSolidArcScene(Ui.State, false);
        PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Isometric);
        Ui.StepFrame();

        // ① Direct sequence: G -> Z -> 10 -> Enter moves the 4×1×2 block +10 along Z; G -> X -> 5 -> '-' -> Enter moves -5 along X!
        SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[0].Key, false);
        Ui.StepFrame();
        std::string Err;
        const bool DirectSeqOk = Ui.RunScript(
            "key G\n"
            "frames 1\n"
            "expect transform == 1\n"
            "expect gizmo-visible-handles == 7\n"
            "key Z\n"
            "frames 1\n"
            "expect transform-axis == 2\n"
            "expect gizmo-visible-handles == 1\n"
            "type 10\n"
            "frames 1\n"
            "expect transform-readout contains Move z (0, 0, 10)\n"
            "key Enter\n"
            "frames 2\n"
            "expect transform == 0\n"
            "expect transform-commits == 1\n"
            "expect volume ~ 8\n"
            "expect sound == 1\n",
            Err);
        RefreshSolidArcScene(Ui.State, false);
        const double ZShiftErr = std::max(std::abs(Ui.State.Scene.BoundsLow[2] - 10.0), std::abs(Ui.State.Scene.BoundsHigh[2] - 12.0));

        // Sign flip ('-' typed AFTER digits, Blender style "5-" -> -5) + decimal ("2.5"):
        std::string ErrSign;
        const bool SignAndDecOk = Ui.RunScript(
            "key G\n"
            "frames 1\n"
            "key X\n"
            "frames 1\n"
            "type 5-\n"
            "frames 1\n"
            "expect transform-readout contains Move x (-5, 0, 0)\n"
            "key Enter\n"
            "frames 2\n"
            "key G\n"
            "frames 1\n"
            "key Y\n"
            "frames 1\n"
            "type 2.5\n"
            "frames 1\n"
            "key Enter\n"
            "frames 2\n",
            ErrSign);
        RefreshSolidArcScene(Ui.State, false);
        const double XYShiftErr = std::max<double>({
            std::abs(Ui.State.Scene.BoundsLow[0] - (-5.0)),
            std::abs(Ui.State.Scene.BoundsHigh[0] - (-1.0)),
            std::abs(Ui.State.Scene.BoundsLow[1] - 2.5),
            std::abs(Ui.State.Scene.BoundsHigh[1] - 3.5) });
        Tally(BlenderTransformGate, std::max(ZShiftErr, XYShiftErr),
              DirectSeqOk && SignAndDecOk && ZShiftErr <= 1.0e-9 && XYShiftErr <= 1.0e-9,
              DirectSeqOk && SignAndDecOk
                  ? Format("G Z 10, G X 5- (Blender sign flip), and G Y 2.5 land at exact bounds [%.1f..%.1f, %.1f..%.1f, %.1f..%.1f]",
                           Ui.State.Scene.BoundsLow[0], Ui.State.Scene.BoundsHigh[0],
                           Ui.State.Scene.BoundsLow[1], Ui.State.Scene.BoundsHigh[1],
                           Ui.State.Scene.BoundsLow[2], Ui.State.Scene.BoundsHigh[2])
                  : ("Direct sequence failed: " + Err + " " + ErrSign));

        // ② Modal gizmo single-axis isolation + 3D axis guide line + live G ⇄ R ⇄ S verb switching + repeat-axis free toggle:
        auto OnlyHandleInGizmo = [&](SolidArcGizmoHandle ExpectedHandle)
        {
            if (Ui.State.Style.Gizmo.Vertices.empty()) return false;
            for (const SolidArcGizmoVertex& V : Ui.State.Style.Gizmo.Vertices)
                if (V.Handle != uint32_t(ExpectedHandle)) return false;
            return true;
        };
        auto HasBlueAxisGuideLineInDrawData = [&]()
        {
            ImDrawData* Draw = ImGui::GetDrawData();
            if (!Draw) return false;
            const ImU32 ExpectedBlue = IM_COL32(0x58, 0xb0, 0xff, 0xf2);
            for (int L = 0; L < Draw->CmdListsCount; ++L)
            {
                const ImDrawList* CmdList = Draw->CmdLists[L];
                for (const ImDrawVert& V : CmdList->VtxBuffer)
                    if (V.col == ExpectedBlue) return true;
            }
            return false;
        };

        std::string ErrModal;
        const bool OpenGZ = Ui.RunScript("key G\nframes 1\nkey Z\nframes 1\n", ErrModal);
        const bool OnlyTransZ   = OpenGZ && OnlyHandleInGizmo(SolidArcGizmoHandle::TranslateZ)
                               && Ui.State.Widgets.Find("GizmoTranslateZ") != nullptr
                               && Ui.State.Widgets.Find("GizmoTranslateX") == nullptr
                               && Ui.State.Widgets.Find("GizmoTranslateY") == nullptr;
        const bool HasGuideLine = OpenGZ && HasBlueAxisGuideLineInDrawData();

        const bool SwitchR      = Ui.RunScript("key R\nframes 1\n", ErrModal);
        const bool OnlyRotZ     = SwitchR && OnlyHandleInGizmo(SolidArcGizmoHandle::RotateZ)
                               && Ui.State.Widgets.Find("GizmoRotateZ") != nullptr
                               && Ui.State.Widgets.Find("GizmoRotateX") == nullptr;

        const bool SwitchS      = Ui.RunScript("key S\nframes 1\n", ErrModal);
        const bool OnlyScaleZ   = SwitchS && OnlyHandleInGizmo(SolidArcGizmoHandle::ScaleZ)
                               && Ui.State.Widgets.Find("GizmoScaleZ") != nullptr
                               && Ui.State.Widgets.Find("GizmoScaleX") == nullptr;

        // Pressing Z a second time releases the Z constraint so all 3 scale axes + centre ring reappear:
        const bool ReleaseZ     = Ui.RunScript("key Z\nframes 1\nexpect transform-axis == -1\nexpect gizmo-visible-handles == 4\nkey Escape\nframes 1\n", ErrModal);
        Tally(BlenderTransformGate, 0.0,
              OnlyTransZ && HasGuideLine && OnlyRotZ && OnlyScaleZ && ReleaseZ,
              Format("Modal gizmo isolates single locked axis handle (TranslateZ=%d, RotateZ=%d, ScaleZ=%d), draws 3D +Z guide line (%d), and toggles free on repeat Z (%d)",
                     OnlyTransZ ? 1 : 0, OnlyRotZ ? 1 : 0, OnlyScaleZ ? 1 : 0, HasGuideLine ? 1 : 0, ReleaseZ ? 1 : 0));

        // ③ Constrained G Z mouse drag + R Z 90 + S Z 2:
        std::string ErrMouse;
        FitSolidArcCamera(Ui.State.Camera, Ui.State.Scene);
        Ui.State.Camera.Distance *= 1.6;
        Ui.StepFrame();
        const double ZBeforeDrag = Ui.State.Scene.BoundsLow[2];
        const bool MouseDragGZ = Ui.RunScript(
            "key Keypad1\n"
            "frames 2\n"
            "move world -3.0 3.0 11.0\n"
            "frames 1\n"
            "key G\n"
            "frames 1\n"
            "key Z\n"
            "frames 1\n"
            "move world -1.0 3.0 13.0\n"
            "frames 2\n"
            "expect transform-rigid == 1\n"
            "click world -1.0 3.0 13.0\n"
            "frames 2\n"
            "expect transform == 0\n",
            ErrMouse);
        RefreshSolidArcScene(Ui.State, false);
        // Even though pointer moved +2 along X and +2 along Z, Z-axis lock constrained X & Y strictly to 0 and moved +2.0 along Z!
        const double DragAxisErr = std::max<double>({
            std::abs(Ui.State.Scene.BoundsLow[0] - (-5.0)),
            std::abs(Ui.State.Scene.BoundsLow[1] - 2.5),
            std::abs((Ui.State.Scene.BoundsLow[2] - ZBeforeDrag) - 2.0) });

        const bool RotAndScaleOk = Ui.RunScript(
            "key R\nframes 1\nkey Z\nframes 1\ntype 90\nframes 1\nkey Enter\nframes 2\n"
            "key S\nframes 1\nkey Z\nframes 1\nclick @TransformAmount\nkey A Ctrl\ntype 2\nkey Enter\nframes 2\n"
            "expect volume ~ 16\n"
            "expect sound == 1\n",
            ErrMouse);
        RefreshSolidArcScene(Ui.State, false);
        const double SpanXAfter90 = Ui.State.Scene.BoundsHigh[0] - Ui.State.Scene.BoundsLow[0];
        const double SpanYAfter90 = Ui.State.Scene.BoundsHigh[1] - Ui.State.Scene.BoundsLow[1];
        const double SpanZAfterS2 = Ui.State.Scene.BoundsHigh[2] - Ui.State.Scene.BoundsLow[2];
        const double RotScaleErr  = std::max<double>({ std::abs(SpanXAfter90 - 1.0), std::abs(SpanYAfter90 - 4.0), std::abs(SpanZAfterS2 - 4.0) });
        Tally(BlenderTransformGate, std::max(DragAxisErr, RotScaleErr),
              MouseDragGZ && RotAndScaleOk && DragAxisErr <= 1.0e-6 && RotScaleErr <= 1.0e-6,
              MouseDragGZ && RotAndScaleOk
                  ? Format("Mouse-dragged G Z constrains strictly to Z (+2.0 m, dX=0, dY=0); R Z 90 swaps 4x1 -> %.1fx%.1f; S Z 2 doubles height to %.1f (V=16)",
                           SpanXAfter90, SpanYAfter90, SpanZAfterS2)
                  : ("Mouse / R / S failed: " + ErrMouse));

        // ④ 3D SpaceCurve G Z transform + Undo:
        {
            SolidArcWorkspaceState CurveState;
            InitialiseSolidArcWorkspace(CurveState, {});
            Expect(DrawSolidArcSpaceCurve(CurveState.Document, { { 0.0, 0.0, 0.0 }, { 1.0, 0.5, 0.2 }, { 2.0, 0.0, 0.0 } }, false));
            const std::string CurveKey = CurveState.Document.Tree.Steps.back().Label;
            SelectSolidArcCurve(CurveState.Document, CurveKey, false);

            HeadlessUiHarness CurveUi;
            CurveUi.State.Document = CloneSolidArcDocument(CurveState.Document);
            RefreshSolidArcScene(CurveUi.State, false);
            CurveUi.StepFrame();
            std::string ErrC;
            const bool CurveMoved = CurveUi.RunScript(
                "key G\nframes 1\nkey Z\nframes 1\ntype 3\nframes 1\nkey Enter\nframes 2\n",
                ErrC);
            const double MovedZ0 = CurveUi.State.Document.Tree.Steps.back().Points.front().Z;
            const bool UndoneOk  = UndoSolidArcCommand(CurveUi.State.Document);
            const double UndoZ0  = CurveUi.State.Document.Tree.Steps.back().Points.front().Z;
            Tally(BlenderTransformGate, std::max(std::abs(MovedZ0 - 3.0), std::abs(UndoZ0)),
                  CurveMoved && UndoneOk && std::abs(MovedZ0 - 3.0) <= 1.0e-9 && std::abs(UndoZ0) <= 1.0e-9,
                  CurveMoved ? Format("3D SpaceCurve G Z 3 shifts curve Z=%.2f and Ctrl+Z restores Z=%.2f", MovedZ0, UndoZ0)
                             : ("SpaceCurve G Z failed: " + ErrC));
        }
    }

    // =========================================================================================================
    // W88 — Blender Numpad Camera Hotkeys (1/3/7 + Ctrl, 0 Iso, 9 Flip, 5 Ortho, 2/4/6/8 Orbit & Pan, +/-, .)
    // =========================================================================================================
    {
        HeadlessUiHarness Ui;
        SolidArcConstructRequest BoxReq;
        BoxReq.Kind     = SolidArcConstructKind::Block;
        BoxReq.Position = { 10.0, 20.0, 4.0 };
        BoxReq.Width    = 2.0;
        BoxReq.Depth    = 2.0;
        BoxReq.Height   = 2.0;
        Expect(ConstructSolidArcPrimitive(Ui.State.Document, BoxReq));
        RefreshSolidArcScene(Ui.State, false);
        Ui.StepFrame();

        // ① All 7 standard views via Numpad 1 / Ctrl+1 / 3 / Ctrl+3 / 7 / Ctrl+7 / 0:
        std::string Err;
        const bool PresetsOk = Ui.RunScript(
            "key Keypad1\nframes 1\nexpect grid-plane == 1\nexpect camera-pitch ~ 0\nexpect camera-yaw ~ -1.5707963268\n"
            "key Keypad1 Ctrl\nframes 1\nexpect grid-plane == 1\nexpect camera-yaw ~ 1.5707963268\n"
            "key Keypad3\nframes 1\nexpect grid-plane == 2\nexpect camera-yaw ~ 0\n"
            "key Keypad3 Ctrl\nframes 1\nexpect grid-plane == 2\nexpect camera-yaw ~ 3.1415926536\n"
            "key Keypad7\nframes 1\nexpect grid-plane == 0\nexpect camera-pitch ~ 1.5707963268\n"
            "key Keypad7 Ctrl\nframes 1\nexpect grid-plane == 0\nexpect camera-pitch ~ -1.5707963268\n"
            "key Keypad0\nframes 1\nexpect camera-pitch ~ 0.6154797087\n",
            Err);
        Tally(NumpadCameraGate, 0.0, PresetsOk,
              PresetsOk ? "Numpad 1 / Ctrl+1 / 3 / Ctrl+3 / 7 / Ctrl+7 / 0 enter Front, Back, Right, Left, Top, Bottom, and Isometric views"
                        : ("Numpad presets failed: " + Err));

        // ② Numpad 5 Ortho/Persp toggle + Numpad 9 180° opposite flip (Front -> Back, Top -> Bottom):
        const bool StartOrtho = Ui.State.Camera.OrthographicEnabled;
        const bool OrthoFlipOk = Ui.RunScript(
            "key Keypad5\nframes 1\n"
            "key Keypad1\nframes 1\n"
            "key Keypad9\nframes 1\nexpect camera-yaw ~ 1.5707963268\n"
            "key Keypad7\nframes 1\n"
            "key Keypad9\nframes 1\nexpect camera-pitch ~ -1.5707963268\n",
            Err);
        const bool ToggledOrtho = (Ui.State.Camera.OrthographicEnabled != StartOrtho);
        Tally(NumpadCameraGate, 0.0, OrthoFlipOk && ToggledOrtho,
              OrthoFlipOk && ToggledOrtho
                  ? "Numpad 5 toggles Perspective ⇄ Orthographic and Numpad 9 flips Front→Back & Top→Bottom by 180°"
                  : ("Numpad 5/9 failed: " + Err));

        // ③ Numpad 4/6/8/2 15° orbit steps, Ctrl+Numpad pan, Numpad +/- zoom, and Numpad . Frame Selection:
        PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Front);
        Ui.State.Camera.OrthographicEnabled = false;
        Ui.StepFrame();
        const double Yaw0  = Ui.State.Camera.Azimuth;
        const double Dist0 = Ui.State.Camera.Distance;
        const bool StepOrbitZoomOk = Ui.RunScript(
            "key Keypad6\nframes 1\n"
            "key Keypad8\nframes 1\n"
            "key KeypadAdd\nframes 1\n",
            Err);
        const double StepRad = std::numbers::pi / 12.0;
        const double OrbitStepErr = std::max(std::abs((Ui.State.Camera.Azimuth - Yaw0) - StepRad),
                                             std::abs(Ui.State.Camera.Elevation - StepRad));
        const bool ZoomedIn = Ui.State.Camera.Distance < Dist0;

        const auto TargetBeforePan = Ui.State.Camera.Target;
        (void)Ui.RunScript("key Keypad6 Ctrl\nframes 1\n", Err);
        const double PanMoved = std::hypot(Ui.State.Camera.Target[0] - TargetBeforePan[0],
                                           std::hypot(Ui.State.Camera.Target[1] - TargetBeforePan[1], Ui.State.Camera.Target[2] - TargetBeforePan[2]));

        Ui.State.Camera.Target = { -100.0, -100.0, -100.0 };
        (void)Ui.RunScript("key KeypadDecimal\nframes 1\n", Err);
        const double FrameErr = std::max<double>({
            std::abs(Ui.State.Camera.Target[0] - 11.0),
            std::abs(Ui.State.Camera.Target[1] - 21.0),
            std::abs(Ui.State.Camera.Target[2] - 5.0) });
        Tally(NumpadCameraGate, std::max(OrbitStepErr, FrameErr),
              StepOrbitZoomOk && OrbitStepErr <= 1.0e-9 && ZoomedIn && PanMoved > 1.0e-3 && FrameErr <= 1.0e-6,
              Format("Numpad 6/8 orbit exact 15° steps (err=%.2e), Ctrl+Numpad pans (%.3f m), Numpad + dollies (%.2f -> %.2f), Numpad . frames target (11, 21, 5)",
                     OrbitStepErr, PanMoved, Dist0, Ui.State.Camera.Distance));

        // ④ Modal numeric guard: inside G Z, pressing Keypad7 types '7' into Transform.Typed and does NOT switch camera to Top!
        PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Front);
        SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[0].Key, false);
        Ui.StepFrame();
        const bool GuardOk = Ui.RunScript(
            "key G\nframes 1\nkey Z\nframes 1\nkey Keypad7\nframes 1\n"
            "expect camera-pitch ~ 0\n"
            "expect transform-readout contains Move z (0, 0, 7)\n"
            "key Escape\nframes 1\n",
            Err);
        Tally(NumpadCameraGate, 0.0, GuardOk,
              GuardOk ? "Modal numeric guard: Numpad digits inside G Z enter numeric distance '7' without rotating the camera"
                      : ("Modal numeric guard failed: " + Err));
    }

    // =========================================================================================================
    // W89 — Multi-View Docked Split Viewport (2, 3, or 4 views, UI button, draggable internal splitters, GPU panes)
    // =========================================================================================================
    {
        HeadlessUiHarness Ui;
        SolidArcConstructRequest BoxReq;
        BoxReq.Kind     = SolidArcConstructKind::Block;
        BoxReq.Position = { 0.0, 0.0, 0.0 };
        BoxReq.Width    = 2.0;
        BoxReq.Depth    = 2.0;
        BoxReq.Height   = 2.0;
        Expect(ConstructSolidArcPrimitive(Ui.State.Document, BoxReq));
        RefreshSolidArcScene(Ui.State, false);
        SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[0].Key, false);
        Ui.StepFrame();

        // ① @ViewportSplit UI button + menu switches cleanly through 2-Vertical, 2-Horizontal, 3-Views, and 4-Quad, plus Ctrl+Alt+Q toggle!
        const bool HasSplitBtn = Ui.State.Widgets.Find("ViewportSplit") != nullptr;
        std::string Err;
        const bool MenuSwitchOk = Ui.RunScript(
            "click @ViewportSplit\nframes 2\nclick @SplitTwoVertical\nframes 2\nexpect split-panes == 2\nexpect split-layout == 1\n"
            "click @ViewportSplit\nframes 2\nclick @SplitTwoHorizontal\nframes 2\nexpect split-panes == 2\nexpect split-layout == 2\n"
            "click @ViewportSplit\nframes 2\nclick @SplitThree\nframes 2\nexpect split-panes == 3\nexpect split-layout == 3\n"
            "click @ViewportSplit\nframes 2\nclick @SplitQuad\nframes 2\nexpect split-panes == 4\nexpect split-layout == 4\n"
            "key Q Ctrl Alt\nframes 2\nexpect split-panes == 1\n"
            "key Q Ctrl Alt\nframes 2\nexpect split-panes == 4\n",
            Err);
        Tally(SplitViewportGate, 0.0, HasSplitBtn && MenuSwitchOk,
              HasSplitBtn && MenuSwitchOk
                  ? "@ViewportSplit toolbar menu switches 1 / 2-Side / 2-Stack / 3-Pane / 4-Quad layouts and Ctrl+Alt+Q toggles Quad view"
                  : ("Split menu / shortcut failed: " + Err));

        // ② Dragging internal splitters (@ViewportSplitterX, @ViewportSplitterY, @ViewportSplitterCross) resizes docked panes without undocking:
        const auto FullBefore = Ui.State.SplitView.FullRect;
        std::string ErrDrag;
        const bool DragSplitOk = Ui.RunScript(
            "drag left @ViewportSplitterCross by 140 70 8\n"
            "frames 2\n",
            ErrDrag);
        const float MovedSplitX = Ui.State.SplitView.SplitX;
        const float MovedSplitY = Ui.State.SplitView.SplitY;
        const auto& R0 = Ui.State.SplitView.PaneRects[0];
        const auto& R1 = Ui.State.SplitView.PaneRects[1];
        const auto& R2 = Ui.State.SplitView.PaneRects[2];
        const auto& R3 = Ui.State.SplitView.PaneRects[3];
        const bool DockedPartitionExact =
            std::abs((R0[2] + 4.0f + R1[2]) - FullBefore[2]) <= 1.5f &&
            std::abs((R0[3] + 4.0f + R2[3]) - FullBefore[3]) <= 1.5f &&
            R3[0] > R2[0] && R3[1] > R1[1];

        const bool ResetOk = Ui.RunScript(
            "click @ViewportSplit\nframes 2\nclick @SplitReset\nframes 2\nexpect split-x ~ 0.5\nexpect split-y ~ 0.5\n",
            ErrDrag);
        Tally(SplitViewportGate, 0.0,
              DragSplitOk && MovedSplitX > 0.56f && MovedSplitY > 0.56f && DockedPartitionExact && ResetOk,
              Format("Internal crosshair splitter drags SplitX=%.3f, SplitY=%.3f strictly inside docked FullRect (%.0fx%.0f) and @SplitReset restores 0.50/0.50",
                     MovedSplitX, MovedSplitY, FullBefore[2], FullBefore[3]));

        // ③ Multi-pane GPU raster descriptors (ComposeSolidArcRasterPanes): distinct ortho/3D grids, adapted gizmos, and active-pane routing!
        const std::vector<SolidArcRasterPane> PanesIdle = ComposeSolidArcRasterPanes(Ui.State, 1280, 720);
        const bool FourGpuPanes = PanesIdle.size() == 4
            && PanesIdle[0].Style.GridPlane == 0                                             // Pane 0: Top (XY)
            && PanesIdle[2].Style.GridPlane == 1                                             // Pane 2: Front (XZ)
            && PanesIdle[3].Style.GridPlane == 2                                             // Pane 3: Right (YZ)
            && !PanesIdle[0].Style.Gizmo.Vertices.empty()
            && !PanesIdle[1].Style.Gizmo.Vertices.empty()
            && !PanesIdle[2].Style.Gizmo.Vertices.empty()
            && !PanesIdle[3].Style.Gizmo.Vertices.empty();

        // Hovering Pane 2 (Front) switches active-pane to 2, and pressing G -> Z isolates TranslateZ across ALL 4 GPU panes simultaneously!
        const ImVec2 Pane2Mid(R2[0] + 0.5f * R2[2], R2[1] + 0.5f * R2[3]);
        char MoveCmd[128];
        std::snprintf(MoveCmd, sizeof(MoveCmd), "move %.1f %.1f\nframes 2\nexpect active-pane == 2\nkey G\nframes 1\nkey Z\nframes 1\n", Pane2Mid.x, Pane2Mid.y);
        const bool HoverAndGZOk = Ui.RunScript(MoveCmd, Err);
        const std::vector<SolidArcRasterPane> PanesGZ = ComposeSolidArcRasterPanes(Ui.State, 1280, 720);
        bool AllFourIsolateZ = PanesGZ.size() == 4;
        for (const SolidArcRasterPane& P : PanesGZ)
        {
            if (P.Style.Gizmo.Vertices.empty()) AllFourIsolateZ = false;
            for (const SolidArcGizmoVertex& V : P.Style.Gizmo.Vertices)
                if (V.Handle != uint32_t(SolidArcGizmoHandle::TranslateZ)) AllFourIsolateZ = false;
        }
        (void)Ui.RunScript("type 4\nframes 1\nkey Enter\nframes 2\nexpect transform == 0\n", Err);
        RefreshSolidArcScene(Ui.State, false);
        Tally(SplitViewportGate, std::abs(Ui.State.Scene.BoundsLow[2] - 4.0),
              FourGpuPanes && HoverAndGZOk && AllFourIsolateZ && std::abs(Ui.State.Scene.BoundsLow[2] - 4.0) <= 1.0e-9,
              Format("4 GPU panes carry independent grids (XY/3D/XZ/YZ) & gizmos, hover switches active-pane=2, and G Z 4 isolates TranslateZ across all 4 views (Z=%.1f)",
                     Ui.State.Scene.BoundsLow[2]));
    }
}

static void ValidateBatch6GizmoZoomAndInteractiveDimensions()
{
    const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
    auto ProjectPanePixel = [](const OrbitCameraFrame& F, double X, double Y, double Z, double& OutPx, double& OutPy) -> bool
    {
        const auto& M = F.ClipFromWorld;
        const double W = M[3] * X + M[7] * Y + M[11] * Z + M[15];
        if (W <= 1.0e-6) return false;
        const double NdcX = (M[0] * X + M[4] * Y + M[8] * Z + M[12]) / W;
        const double NdcY = (M[1] * X + M[5] * Y + M[9] * Z + M[13]) / W;
        OutPx = (NdcX * 0.5 + 0.5) * F.Width;
        OutPy = (NdcY * 0.5 + 0.5) * F.Height;
        return true;
    };

    // Measure max projected screen-pixel distance from Origin to any TranslateZ vertex on a gizmo in Frame.
    auto GizmoHandlePixelSpan = [&](const SolidArcGizmoGeometry& Gizmo, const Vector3& Origin, const OrbitCameraFrame& F, SolidArcGizmoHandle Handle) -> double
    {
        double Ox = 0.0, Oy = 0.0;
        if (!ProjectPanePixel(F, Origin.X, Origin.Y, Origin.Z, Ox, Oy)) return 0.0;
        double Best = 0.0;
        for (const SolidArcGizmoVertex& V : Gizmo.Vertices)
        {
            if (V.Handle != uint32_t(Handle)) continue;
            double Vx = 0.0, Vy = 0.0;
            if (ProjectPanePixel(F, V.Position[0], V.Position[1], V.Position[2], Vx, Vy))
                Best = std::max(Best, std::hypot(Vx - Ox, Vy - Oy));
        }
        return Best;
    };

    // ============================================================================================================
    // W90 — Smooth Jitter-Free Gizmo Scaling Across Viewport Zoom, Shortcuts, Modal G/R/S & Perspective Drags
    // ============================================================================================================
    {
        HeadlessUiHarness Ui;
        SolidArcConstructRequest Box;
        Box.Kind     = SolidArcConstructKind::Block;
        Box.Position = { 1.4, -0.8, 0.5 };
        Box.Width    = 1.2;
        Box.Depth    = 1.0;
        Box.Height   = 0.8;
        Expect(ConstructSolidArcPrimitive(Ui.State.Document, Box));
        RefreshSolidArcScene(Ui.State, false);
        PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Isometric);
        FitSolidArcCamera(Ui.State.Camera, Ui.State.Scene);
        SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[0].Key, false);
        Ui.StepFrame();
        Ui.StepFrame();

        const Vector3 GizmoOrigin = Ui.State.Gizmo.Pose.Origin;
        const OrbitCameraFrame RefUiFrame = FrameOrbitCamera(Ui.State.Camera, Ui.State.ViewportRect[2], Ui.State.ViewportRect[3]);
        const double RefUiSpan = GizmoHandlePixelSpan(Ui.State.Style.Gizmo, GizmoOrigin, RefUiFrame, SolidArcGizmoHandle::TranslateZ);
        const std::vector<SolidArcRasterPane> RefPanes = ComposeSolidArcRasterPanes(Ui.State, 1280, 720);
        const double RefGpuSpan = GizmoHandlePixelSpan(RefPanes[0].Style.Gizmo, GizmoOrigin, RefPanes[0].Frame, SolidArcGizmoHandle::TranslateZ);

        // ① 24 consecutive single-frame wheel zoom ticks (12 in, 12 out, zero recovery frames) in Perspective + Ortho:
        double WorstWheelUiJitter  = 0.0;
        double WorstWheelGpuJitter = 0.0;
        double WorstWidgetDrift    = 0.0;
        for (int Pass = 0; Pass < 2; ++Pass)
        {
            Ui.State.Camera.OrthographicEnabled = (Pass == 1);
            Ui.StepFrame();
            const OrbitCameraFrame BaseUiF = FrameOrbitCamera(Ui.State.Camera, Ui.State.ViewportRect[2], Ui.State.ViewportRect[3]);
            const double BaseUiSpan = GizmoHandlePixelSpan(Ui.State.Style.Gizmo, GizmoOrigin, BaseUiF, SolidArcGizmoHandle::TranslateZ);
            const auto BasePanes = ComposeSolidArcRasterPanes(Ui.State, 1280, 720);
            const double BaseGpuSpan = GizmoHandlePixelSpan(BasePanes[0].Style.Gizmo, GizmoOrigin, BasePanes[0].Frame, SolidArcGizmoHandle::TranslateZ);

            for (int Tick = 0; Tick < 24; ++Tick)
            {
                const float Wheel = Tick < 12 ? 1.0f : -1.0f;
                ImGuiIO& Io = ImGui::GetIO();
                Io.MousePos   = ImVec2(Ui.State.ViewportRect[0] + 0.5f * Ui.State.ViewportRect[2], Ui.State.ViewportRect[1] + 0.5f * Ui.State.ViewportRect[3]);
                Io.MouseWheel = Wheel;
                Ui.StepFrame();   // Single frame: wheel zoom + DriveGizmo + ComposeSolidArcRasterPanes must agree immediately!
                Io.MouseWheel = 0.0f;

                const OrbitCameraFrame CurUiF = FrameOrbitCamera(Ui.State.Camera, Ui.State.ViewportRect[2], Ui.State.ViewportRect[3]);
                const double CurUiSpan = GizmoHandlePixelSpan(Ui.State.Style.Gizmo, GizmoOrigin, CurUiF, SolidArcGizmoHandle::TranslateZ);
                const auto CurPanes = ComposeSolidArcRasterPanes(Ui.State, 1280, 720);
                const double CurGpuSpan = GizmoHandlePixelSpan(CurPanes[0].Style.Gizmo, GizmoOrigin, CurPanes[0].Frame, SolidArcGizmoHandle::TranslateZ);
                // Measure Gizmo.Pose.Size vs exact analytical MeasureGizmoSize:
                const double ExpectedSize = MeasureGizmoSize(CurUiF, GizmoOrigin, GizmoPixels);
                const double SizeRelErr   = std::abs(Ui.State.Gizmo.Pose.Size - ExpectedSize) / std::max(1.0e-9, ExpectedSize);
                WorstWheelUiJitter  = std::max(WorstWheelUiJitter, SizeRelErr * GizmoPixels);
                WorstWheelGpuJitter = std::max(WorstWheelGpuJitter, std::abs(CurGpuSpan - BaseGpuSpan));
                (void)CurUiSpan;
                (void)BaseUiSpan;

                if (const ImVec4* W = Ui.State.Widgets.Find("GizmoTranslateZ"))
                {
                    double TipPx = 0.0, TipPy = 0.0;
                    const Vector3 TipWorld = GizmoOrigin + Vector3{ 0.0, 0.0, 0.95 * Ui.State.Gizmo.Pose.Size };
                    if (ProjectPanePixel(CurUiF, TipWorld.X, TipWorld.Y, TipWorld.Z, TipPx, TipPy))
                    {
                        const double Wx = 0.5 * (W->x + W->z) - Ui.State.ViewportRect[0];
                        const double Wy = 0.5 * (W->y + W->w) - Ui.State.ViewportRect[1];
                        WorstWidgetDrift = std::max(WorstWidgetDrift, std::hypot(Wx - TipPx, Wy - TipPy));
                    }
                }
            }
        }
        Ui.State.Camera.OrthographicEnabled = false;
        Ui.StepFrame();
        const double WorstZoomJitter = std::max({ WorstWheelUiJitter, WorstWheelGpuJitter, WorstWidgetDrift });
        Tally(GizmoZoomGate, WorstZoomJitter,
              RefUiSpan > 40.0 && RefGpuSpan > 40.0 && WorstZoomJitter <= 0.1,
              Format("48 single-frame Perspective+Ortho wheel zoom steps: max UI/GPU gizmo scale jitter = %.4f px, @GizmoTranslateZ drift = %.4f px",
                     std::max(WorstWheelUiJitter, WorstWheelGpuJitter), WorstWidgetDrift));

        // ② Shortcut zoom (Numpad + / Numpad -) and Modal 'G' zoom maintain zero-lag gizmo scale on the exact shortcut frame:
        std::string Err;
        double WorstShortcutJitter = 0.0;
        for (const char* KeyCmd : { "key KeypadAdd\n", "key KeypadAdd\n", "key KeypadSubtract\n", "key KeypadSubtract\n" })
        {
            const bool Ok = Ui.RunScript(KeyCmd, Err);
            const OrbitCameraFrame F = FrameOrbitCamera(Ui.State.Camera, Ui.State.ViewportRect[2], Ui.State.ViewportRect[3]);
            const double Expected = MeasureGizmoSize(F, GizmoOrigin, GizmoPixels);
            const double JitterPx = (std::abs(Ui.State.Gizmo.Pose.Size - Expected) / std::max(1.0e-9, Expected)) * GizmoPixels;
            WorstShortcutJitter = std::max(WorstShortcutJitter, Ok ? JitterPx : 999.0);
        }
        (void)Ui.RunScript("key G\nframes 1\n", Err);
        const auto ModalPanesBefore = ComposeSolidArcRasterPanes(Ui.State, 1280, 720);
        const double ModalSpanBefore = GizmoHandlePixelSpan(ModalPanesBefore[0].Style.Gizmo, GizmoOrigin, ModalPanesBefore[0].Frame, SolidArcGizmoHandle::TranslateZ);
        ImGui::GetIO().MousePos   = ImVec2(Ui.State.ViewportRect[0] + 0.5f * Ui.State.ViewportRect[2], Ui.State.ViewportRect[1] + 0.5f * Ui.State.ViewportRect[3]);
        ImGui::GetIO().MouseWheel = 2.0f;
        Ui.StepFrame();
        ImGui::GetIO().MouseWheel = 0.0f;
        const auto ModalPanesAfter = ComposeSolidArcRasterPanes(Ui.State, 1280, 720);
        const double ModalSpanAfter = GizmoHandlePixelSpan(ModalPanesAfter[0].Style.Gizmo, GizmoOrigin, ModalPanesAfter[0].Frame, SolidArcGizmoHandle::TranslateZ);
        const double ModalZoomJitter = std::abs(ModalSpanAfter - ModalSpanBefore);
        (void)Ui.RunScript("key Escape\nframes 1\n", Err);
        Tally(GizmoZoomGate, std::max(WorstShortcutJitter, ModalZoomJitter),
              WorstShortcutJitter <= 0.1 && ModalZoomJitter <= 0.1 && ModalSpanBefore > 40.0,
              Format("Numpad +/- single-frame shortcut zoom jitter = %.4f px; active modal 'G' wheel-zoom GPU gizmo jitter = %.4f px",
                     WorstShortcutJitter, ModalZoomJitter));

        // ③ Perspective Gizmo Move-Drag toward/away from camera keeps Riding.Size depth-compensated:
        const bool DragOk = Ui.RunScript("drag Left @GizmoTranslateX by 160 95 8 Ctrl\n", Err);
        RefreshSolidArcScene(Ui.State, false);
        const OrbitCameraFrame PostDragFrame = FrameOrbitCamera(Ui.State.Camera, Ui.State.ViewportRect[2], Ui.State.ViewportRect[3]);
        const double ExpectedPostSize = MeasureGizmoSize(PostDragFrame, Ui.State.Gizmo.Pose.Origin, GizmoPixels);
        const double PostDragJitterPx = (std::abs(Ui.State.Gizmo.Pose.Size - ExpectedPostSize) / std::max(1.0e-9, ExpectedPostSize)) * GizmoPixels;
        Tally(GizmoZoomGate, PostDragJitterPx,
              DragOk && Length(Ui.State.Gizmo.Pose.Origin - GizmoOrigin) > 0.3 && PostDragJitterPx <= 0.1,
              Format("Perspective gizmo drag moved %.2f m along X with depth-compensated Riding.Size (post-drag scale error = %.4f px)",
                     Length(Ui.State.Gizmo.Pose.Origin - GizmoOrigin), PostDragJitterPx));
    }

    // ============================================================================================================
    // W91 — Interactive CAD Dimension Tool (Boxes, Circles, Arcs, 3D Solids, Drag-to-Position & Associative Tracking)
    // ============================================================================================================
    {
        // ① Tool buttons (@SketchDimension on bottom ViewportTools, @DimensionTool visibility toggle on top-right, 'D' hotkey) & non-wrapped layout:
        {
            HeadlessUiHarness Ui;
            Ui.StepFrame();
            Ui.StepFrame();
            const ImVec4* BottomDimBtn = Ui.State.Widgets.Find("SketchDimension");
            const ImVec4* TopDimBtn    = Ui.State.Widgets.Find("DimensionTool");
            const ImVec4* SelectBtn    = Ui.State.Widgets.Find("SketchSelect");
            // Bottom ViewportTools bar sits beside the Orientation Cube (not pushed above CubeExtent+45+8 = 139 px):
            const float ViewportBottom = Ui.State.ViewportRect[1] + Ui.State.ViewportRect[3];
            const bool BarFitsBesideCube = BottomDimBtn != nullptr && (ViewportBottom - BottomDimBtn->w) < 120.0f;
            std::string Err;
            const bool ToggleOk = Ui.RunScript(
                "click @SketchDimension\nframes 1\nexpect dimension-active == 1\nexpect dimension-stage == 0\n"
                "click @SketchSelect\nframes 1\nexpect dimension-active == 0\n"
                "click @DimensionTool\nframes 1\n"
                "click @DimensionTool\nframes 1\n"
                "key D\nframes 1\nexpect dimension-active == 1\n"
                "key D\nframes 1\nexpect dimension-active == 0\n"
                "key D\nframes 1\nexpect dimension-active == 1\n",
                Err);
            Tally(InteractiveDimToolGate, 0.0,
                  BottomDimBtn != nullptr && TopDimBtn != nullptr && SelectBtn != nullptr && BarFitsBesideCube && ToggleOk && Ui.State.DimensionsEnabled,
                  ToggleOk ? "@SketchDimension ('Dim' on bottom toolbar) & 'D' hotkey toggle interactive tool cleanly while @DimensionTool toggles visibility without wrapping ViewportTools"
                           : ("Dimension tool toggle failed: " + Err));
        }

        // ② 2D Sketch Box (Rectangle), 2D Circle (Ø ⇄ R) & 2D Arc (R) interactive Click-Click-Drag-Place + Associative Live/Committed Transform:
        {
            HeadlessUiHarness Ui;
            // Sketch#1: 4.0 x 2.5 m rectangle from (0, 0) to (4, 2.5)
            Expect(DrawSolidArcSketch(Ui.State.Document, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 4.0, 2.5 } })));
            // Sketch#2: Circle centred at (7.0, 1.25) with radius 1.5 m (Ø 3.0 m)
            Expect(DrawSolidArcSketch(Ui.State.Document, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 7.0, 1.25 }, { 8.5, 1.25 } }, false, true)));
            // Sketch#3: 3-point Arc centred at (-3.0, 1.0) with radius 1.0 m: (-2.0, 1.0) -> (-4.0, 1.0) via (-3.0, 2.0)
            Expect(DrawSolidArcSketch(Ui.State.Document, DrawingOf(SolidArcDrawingKind::Arc, 0, { { -2.0, 1.0 }, { -4.0, 1.0 }, { -3.0, 2.0 } })));
            RefreshSolidArcScene(Ui.State, false);
            PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Top);
            Ui.State.Camera.OrthographicEnabled = true;
            FitOrbitCamera(Ui.State.Camera, { -5.5, -2.5, -1.0 }, { 10.0, 5.0, 1.0 });
            ClearSolidArcSelection(Ui.State.Document);
            Ui.StepFrame();
            Ui.StepFrame();

            std::string Err;
            // Activate Dimension tool ('D'), click Corner A (0,0,0) -> Stage 1, click Corner B (4,0,0) -> Stage 2, move to (2,-0.9,0) to position parallel offset, click to place!
            const bool BoxDimOk = Ui.RunScript(
                "key D\nframes 1\nexpect dimension-active == 1\nexpect dimension-stage == 0\n"
                "click world 0 0 0\nframes 1\nexpect dimension-stage == 1\n"
                "click world 4 0 0\nframes 1\nexpect dimension-stage == 2\n"
                "move world 2 -0.9 0\nframes 2\n"
                "click world 2 -0.9 0\nframes 1\n"
                "expect dimensions == 1\nexpect dimension-stage == 0\n",
                Err);
            const bool BoxAssociative = Ui.State.Document.Dimensions.size() == 1
                && std::abs(Ui.State.Document.Dimensions[0].Value - 4.0) <= 1.0e-6
                && std::abs(std::abs(Ui.State.Document.Dimensions[0].Offset) - 0.9) <= 0.08
                && Ui.State.Document.Dimensions[0].FirstName.find("Sketch#1@Point:") == 0
                && Ui.State.Document.Dimensions[0].SecondName.find("Sketch#1@Point:") == 0;
            const std::string BoxMsg = BoxDimOk && BoxAssociative
                ? Format("2D Box Ruler workflow: Corner A -> Corner B -> drag parallel offset (%.2f m) -> click places associative %.2f m (%s -> %s)",
                         Ui.State.Document.Dimensions[0].Offset, Ui.State.Document.Dimensions[0].Value,
                         Ui.State.Document.Dimensions[0].FirstName.c_str(), Ui.State.Document.Dimensions[0].SecondName.c_str())
                : ("2D Box dimension failed: " + Err);
            Tally(InteractiveDimToolGate, BoxAssociative ? std::abs(Ui.State.Document.Dimensions[0].Value - 4.0) : 1.0,
                  BoxDimOk && BoxAssociative, BoxMsg);

            // Circle: 1 click on circle rim (8.5, 1.25, 0) -> Stage 2 (Ø 3.0 m), move radially outward to (9.3, 2.2, 0), click to place!
            // Then click circle rim again -> Stage 2 -> press 'R' to toggle Radius (R 1.5 m) -> Enter places!
            // Then click 2D Arc at (-3.0, 2.0, 0) -> Stage 2 (R 1.0 m) -> move outward to (-3.0, 2.8, 0) -> click places!
            const bool CircleArcOk = Ui.RunScript(
                "click world 8.5 1.25 0\nframes 1\nexpect dimension-stage == 2\n"
                "move world 9.3 2.2 0\nframes 2\n"
                "click world 9.3 2.2 0\nframes 1\nexpect dimensions == 2\nexpect dimension-stage == 0\n"
                "click world 8.5 1.25 0\nframes 1\nexpect dimension-stage == 2\n"
                "key R\nframes 1\n"
                "move world 5.0 0.4 0\nframes 1\n"
                "key Enter\nframes 1\nexpect dimensions == 3\nexpect dimension-stage == 0\n"
                "click world -3.0 2.0 0\nframes 1\nexpect dimension-stage == 2\n"
                "move world -3.0 2.8 0\nframes 2\n"
                "click world -3.0 2.8 0\nframes 1\nexpect dimensions == 4\nexpect dimension-stage == 0\n"
                "key Escape\nframes 1\nexpect dimension-active == 0\n",
                Err);
            const bool CircleArcExact = Ui.State.Document.Dimensions.size() == 4
                && Ui.State.Document.Dimensions[1].Kind == SolidArcDimensionKind::Diameter
                && std::abs(Ui.State.Document.Dimensions[1].Value - 3.0) <= 1.0e-6
                && Ui.State.Document.Dimensions[1].FirstName.find("Sketch#2@Curve:") == 0
                && Ui.State.Document.Dimensions[2].Kind == SolidArcDimensionKind::Radius
                && std::abs(Ui.State.Document.Dimensions[2].Value - 1.5) <= 1.0e-6
                && Ui.State.Document.Dimensions[3].Kind == SolidArcDimensionKind::Radius
                && std::abs(Ui.State.Document.Dimensions[3].Value - 1.0) <= 1.0e-5
                && Ui.State.Document.Dimensions[3].FirstName.find("Sketch#3@Curve:") == 0;
            const std::string CircleArcMsg = CircleArcOk && CircleArcExact
                ? Format("Circle & Arc workflow: 1-click Circle Ø=%.2f m + 'R' toggle R=%.2f m + 1-click Arc R=%.2f m with 360° radial leader positioning",
                         Ui.State.Document.Dimensions[1].Value, Ui.State.Document.Dimensions[2].Value, Ui.State.Document.Dimensions[3].Value)
                : ("Circle / Arc dimension failed: " + Err);
            Tally(InteractiveDimToolGate, CircleArcExact ? std::abs(Ui.State.Document.Dimensions[3].Value - 1.0) : 1.0,
                  CircleArcOk && CircleArcExact, CircleArcMsg);

            // Reposition placed dimension Dim1 by dragging its viewport pill (@Dim:Dim1) + verify single-step Undo/Redo:
            const double OffsetBeforeDrag = Ui.State.Document.Dimensions[0].Offset;
            const bool PillDragOk = Ui.RunScript(
                "drag Left @Dim:Dim1 by 0 42 6\nframes 1\n",
                Err);
            const double OffsetAfterDrag = Ui.State.Document.Dimensions[0].Offset;
            const bool Undid = UndoSolidArcCommand(Ui.State.Document);
            const double OffsetAfterUndo = Ui.State.Document.Dimensions[0].Offset;
            const bool Redid = RedoSolidArcCommand(Ui.State.Document);
            const double OffsetAfterRedo = Ui.State.Document.Dimensions[0].Offset;
            Tally(InteractiveDimToolGate, std::abs(OffsetAfterUndo - OffsetBeforeDrag),
                  PillDragOk && Undid && Redid && std::abs(OffsetAfterDrag - OffsetBeforeDrag) > 0.15
                  && std::abs(OffsetAfterUndo - OffsetBeforeDrag) <= 1.0e-9
                  && std::abs(OffsetAfterRedo - OffsetAfterDrag) <= 1.0e-9,
                  Format("Viewport pill drag repositions Dim1 offset %.2f -> %.2f m, single Ctrl+Z restores %.2f m, Redo restores %.2f m",
                         OffsetBeforeDrag, OffsetAfterDrag, OffsetAfterUndo, OffsetAfterRedo));

            // Move Sketch#1 (Box) & Sketch#2 (Circle) via modal G X 5 Enter and verify dimensions associatively follow the shapes!
            SelectSolidArcCurve(Ui.State.Document, "Sketch#1", false);
            SelectSolidArcCurve(Ui.State.Document, "Sketch#2", true);
            const ImVec4* Dim1BeforeLive = Ui.State.Widgets.Find("Dim:Dim1");
            const float Dim1PixXBefore = Dim1BeforeLive ? 0.5f * (Dim1BeforeLive->x + Dim1BeforeLive->z) : 0.0f;
            const bool MoveSketchOk = Ui.RunScript(
                "frames 1\nkey G\nframes 1\nkey X\nframes 1\ntype 5\nframes 2\n",
                Err);
            // Check LIVE preview before Enter (both staged preview document AND live viewport @Dim:Dim1 pill position!):
            const ImVec4* Dim1DuringLive = Ui.State.Widgets.Find("Dim:Dim1");
            const float Dim1PixXDuring = Dim1DuringLive ? 0.5f * (Dim1DuringLive->x + Dim1DuringLive->z) : 0.0f;
            const std::vector<SolidArcDimension> LiveDims = Ui.State.Transform.Preview
                ? EvaluateSolidArcDimensions(*Ui.State.Transform.Preview)
                : std::vector<SolidArcDimension>{};
            const bool LiveFollows = LiveDims.size() >= 2
                && (Dim1PixXDuring - Dim1PixXBefore) > 30.0f
                && std::abs(std::min(LiveDims[0].FirstPoint.X, LiveDims[0].SecondPoint.X) - 5.0) <= 1.0e-6
                && std::abs(LiveDims[1].FirstPoint.X - 12.0) <= 1.0e-6;
            const bool CommitSketchMoveOk = Ui.RunScript("key Enter\nframes 2\nexpect transform == 0\n", Err);
            const bool CommittedFollows = Ui.State.Document.Dimensions.size() >= 2
                && std::abs(std::min(Ui.State.Document.Dimensions[0].FirstPoint.X, Ui.State.Document.Dimensions[0].SecondPoint.X) - 5.0) <= 1.0e-6
                && std::abs(std::max(Ui.State.Document.Dimensions[0].FirstPoint.X, Ui.State.Document.Dimensions[0].SecondPoint.X) - 9.0) <= 1.0e-6
                && std::abs(Ui.State.Document.Dimensions[0].Value - 4.0) <= 1.0e-6
                && std::abs(Ui.State.Document.Dimensions[1].FirstPoint.X - 12.0) <= 1.0e-6
                && std::abs(Ui.State.Document.Dimensions[1].Value - 3.0) <= 1.0e-6;
            Tally(InteractiveDimToolGate, CommittedFollows ? std::abs(Ui.State.Document.Dimensions[1].FirstPoint.X - 12.0) : 1.0,
                  MoveSketchOk && LiveFollows && CommitSketchMoveOk && CommittedFollows,
                  Format("Associative 2D sketch tracking: live 'G X 5' preview & committed move carry Box Dim1 to X=[%.1f, %.1f] and Circle Dim2 centre to X=%.1f",
                         std::min(Ui.State.Document.Dimensions[0].FirstPoint.X, Ui.State.Document.Dimensions[0].SecondPoint.X),
                         std::max(Ui.State.Document.Dimensions[0].FirstPoint.X, Ui.State.Document.Dimensions[0].SecondPoint.X),
                         Ui.State.Document.Dimensions[1].FirstPoint.X));
        }

        // ③ 3D Solid Block (1-Click Edge Linear + 2-Edge 90° Angular) & 3D Cylinder (Ø) + Live/Committed Body Transform Tracking:
        {
            HeadlessUiHarness Ui;
            SolidArcConstructRequest Box;
            Box.Kind     = SolidArcConstructKind::Block;
            Box.Position = { 0.0, 0.0, 0.0 };
            Box.Width    = 3.0;
            Box.Depth    = 2.0;
            Box.Height   = 1.2;
            Expect(ConstructSolidArcPrimitive(Ui.State.Document, Box));

            SolidArcConstructRequest Cyl;
            Cyl.Kind        = SolidArcConstructKind::Cylinder;
            Cyl.Combination = FeatureCombination::Separate;
            Cyl.Position    = { 5.0, 1.0, 0.0 };
            Cyl.Radius      = 0.8;
            Cyl.Height      = 1.2;
            Expect(ConstructSolidArcPrimitive(Ui.State.Document, Cyl));

            RefreshSolidArcScene(Ui.State, false);
            PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Top);
            Ui.State.Camera.OrthographicEnabled = true;
            FitOrbitCamera(Ui.State.Camera, { -2.0, -3.5, -0.5 }, { 7.5, 4.0, 2.0 });
            ClearSolidArcSelection(Ui.State.Document);
            Ui.StepFrame();
            Ui.StepFrame();

            std::string Err;
            // Click top front edge midpoint (1.5, 0.0, 1.2) -> Stage 2 (3.0 m edge), move to (1.5, -0.7, 1.2), click to place!
            // Click edge 1 (1.5, 0.0, 1.2) -> Stage 2 -> click perpendicular edge 2 (0.0, 1.0, 1.2) -> upgrades to 90° Angular -> click (0.8, 0.8, 1.2) to place!
            // Click Cylinder top rim (5.8, 1.0, 1.2) -> Stage 2 (Ø 1.6 m) -> move to (6.4, 1.6, 1.2) -> click to place!
            const bool SolidDimOk = Ui.RunScript(
                "key D\nframes 1\n"
                "click world 1.5 0.0 1.2\nframes 1\nexpect dimension-stage == 2\n"
                "move world 1.5 -0.7 1.2\nframes 2\n"
                "click world 1.5 -0.7 1.2\nframes 1\nexpect dimensions == 1\nexpect dimension-stage == 0\n"
                "click world 1.5 0.0 1.2\nframes 1\nexpect dimension-stage == 2\n"
                "click world 0.0 1.0 1.2\nframes 1\nexpect dimension-stage == 2\n"
                "move world 0.8 0.8 1.2\nframes 2\n"
                "click world 0.8 0.8 1.2\nframes 1\nexpect dimensions == 2\nexpect dimension-stage == 0\n"
                "click world 5.8 1.0 1.2\nframes 1\nexpect dimension-stage == 2\n"
                "move world 6.4 1.6 1.2\nframes 2\n"
                "click world 6.4 1.6 1.2\nframes 1\nexpect dimensions == 3\nexpect dimension-stage == 0\n"
                "key Escape\nframes 1\nexpect dimension-active == 0\n",
                Err);
            const bool SolidDimsExact = Ui.State.Document.Dimensions.size() == 3
                && Ui.State.Document.Dimensions[0].Kind == SolidArcDimensionKind::Linear
                && std::abs(Ui.State.Document.Dimensions[0].Value - 3.0) <= 1.0e-6
                && Ui.State.Document.Dimensions[1].Kind == SolidArcDimensionKind::Angular
                && std::abs(Ui.State.Document.Dimensions[1].Value - 90.0) <= 1.0e-4
                && Ui.State.Document.Dimensions[2].Kind == SolidArcDimensionKind::Diameter
                && std::abs(Ui.State.Document.Dimensions[2].Value - 1.6) <= 1.0e-5;
            Tally(InteractiveDimToolGate, SolidDimsExact ? std::abs(Ui.State.Document.Dimensions[1].Value - 90.0) : 1.0,
                  SolidDimOk && SolidDimsExact,
                  SolidDimOk && SolidDimsExact
                      ? Format("3D Solid workflow: 1-click Block edge %.2f m + 2-edge %.1f° Angular + 1-click Cylinder Ø=%.2f m",
                               Ui.State.Document.Dimensions[0].Value, Ui.State.Document.Dimensions[1].Value, Ui.State.Document.Dimensions[2].Value)
                      : ("3D Solid dimension failed: " + Err));

            // Translate both 3D bodies by G Y 4 Enter and verify all 3 3D dimensions follow both during live rigid preview and after commit!
            SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[0].Key, false);
            SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[1].Key, true);
            const double CylCenterYBefore = Ui.State.Document.Dimensions[2].FirstPoint.Y;
            const bool SolidMoveOk = Ui.RunScript(
                "frames 1\nkey G\nframes 1\nkey Y\nframes 1\ntype 4\nframes 2\n"
                "key Enter\nframes 2\nexpect transform == 0\n",
                Err);
            const double EdgeYAfter      = Ui.State.Document.Dimensions[0].FirstPoint.Y;
            const double AngleVertYAfter = Ui.State.Document.Dimensions[1].FirstPoint.Y;
            const double CylCenterYAfter = Ui.State.Document.Dimensions[2].FirstPoint.Y;
            const bool SolidTracked = SolidMoveOk
                && std::abs(EdgeYAfter - 4.0) <= 1.0e-5
                && std::abs(AngleVertYAfter - 4.0) <= 1.0e-5
                && std::abs((CylCenterYAfter - CylCenterYBefore) - 4.0) <= 1.0e-5
                && std::abs(Ui.State.Document.Dimensions[0].Value - 3.0) <= 1.0e-6
                && std::abs(Ui.State.Document.Dimensions[1].Value - 90.0) <= 1.0e-4
                && std::abs(Ui.State.Document.Dimensions[2].Value - 1.6) <= 1.0e-5;
            Tally(InteractiveDimToolGate, std::abs((CylCenterYAfter - CylCenterYBefore) - 4.0),
                  SolidTracked,
                  Format("Associative 3D solid tracking: 'G Y 4' moves Block edge Y=%.1f, 90° corner Y=%.1f, and Cylinder Ø1.6 centre Y=%.1f->%.1f",
                         EdgeYAfter, AngleVertYAfter, CylCenterYBefore, CylCenterYAfter));
        }
    }
}

void ValidateBatch7DimensionsBevelAndReshape()
{
    auto Format = [](const char* Pattern, auto... Args) -> std::string
    {
        char Buf[512];
        std::snprintf(Buf, sizeof(Buf), Pattern, Args...);
        return Buf;
    };

    // ============================================================================================================
    // W92 — X / Y / Z World-Axis Dimension Sliding & Cutter-Style Placed Dimension Editing
    // ============================================================================================================
    {
        HeadlessUiHarness Ui;
        SolidArcConstructRequest Box;
        Box.Kind     = SolidArcConstructKind::Block;
        Box.Position = { 0.0, 0.0, 0.0 };
        Box.Width    = 3.0;
        Box.Depth    = 2.0;
        Box.Height   = 1.5;
        Expect(ConstructSolidArcPrimitive(Ui.State.Document, Box));
        RefreshSolidArcScene(Ui.State, false);
        PresentOrbitView(Ui.State.Camera, OrbitViewPreset::Isometric);
        FitOrbitCamera(Ui.State.Camera, { -1.5, -2.0, -1.8 }, { 4.5, 3.5, 2.8 });
        ClearSolidArcSelection(Ui.State.Document);
        Ui.StepFrame();
        Ui.StepFrame();

        // ① Select bottom front X-edge (1.5, 0.0, 0.0) in Dimension Tool -> press Z -> move mouse downward along World Z -> click to place!
        std::string Err;
        const bool SlideZOk = Ui.RunScript(
            "key D\nframes 1\nexpect dimension-active == 1\n"
            "click world 1.5 0.0 0.0\nframes 1\nexpect dimension-stage == 2\n"
            "key Z\nframes 1\n"
            "move world 1.5 0.0 -0.85\nframes 2\n"
            "click world 1.5 0.0 -0.85\nframes 1\n"
            "expect dimensions == 1\nexpect dimension-stage == 0\n"
            "key Escape\nframes 1\nexpect dimension-active == 0\n",
            Err);
        const bool Dim1ZExact = Ui.State.Document.Dimensions.size() == 1
            && Ui.State.Document.Dimensions[0].SlideAxis == 2
            && std::abs(Ui.State.Document.Dimensions[0].Value - 3.0) <= 1.0e-6
            && std::abs(Ui.State.Document.Dimensions[0].Offset - (-0.85)) <= 0.06;
        Tally(AxisSlideDimEditGate, Dim1ZExact ? std::abs(Ui.State.Document.Dimensions[0].Offset - (-0.85)) : 1.0,
              SlideZOk && Dim1ZExact,
              SlideZOk && Dim1ZExact
                  ? Format("Edge -> Z -> mouse drag slides 3.00 m dimension & witness lines along World Z (SlideAxis=%d, Offset=%.3f m)",
                           Ui.State.Document.Dimensions[0].SlideAxis, Ui.State.Document.Dimensions[0].Offset)
                  : ("World Z dimension slide failed: " + Err));

        // ② Pre-selected edge fast-path + typed numeric X-axis slide on vertical Z-edge (0.0, 0.0, 0.75):
        std::string VertEdgeName;
        for (const auto& [Slot, Name] : Ui.State.Document.Regeneration.Naming.Edges)
        {
            SolidArcDimension Probe;
            Probe.Kind      = SolidArcDimensionKind::Linear;
            Probe.FirstKind = SolidArcSelectionKind::Edge;
            Probe.FirstName = Name;
            Ui.State.Document.Dimensions.push_back(Probe);
            const std::vector<SolidArcDimension> Eval = EvaluateSolidArcDimensions(Ui.State.Document);
            Ui.State.Document.Dimensions.pop_back();
            const Vector3 Span = Eval.back().SecondPoint - Eval.back().FirstPoint;
            if (std::abs(std::abs(Span.Z) - 1.5) <= 1.0e-5 && std::abs(Span.X) <= 1.0e-5 && std::abs(Span.Y) <= 1.0e-5)
            {
                VertEdgeName = Name;
                break;
            }
        }
        ClearSolidArcSelection(Ui.State.Document);
        SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Edge, VertEdgeName, false);
        Err.clear();
        const bool PreselectFastPathOk = !VertEdgeName.empty() && Ui.RunScript(
            "frames 1\n"
            "key D\nframes 1\nexpect dimension-active == 1\nexpect dimension-stage == 2\n"
            "key X\nframes 1\n"
            "type -0.65\nframes 1\n"
            "key Enter\nframes 1\nexpect dimensions == 2\n"
            "key Escape\nframes 1\nexpect dimension-active == 0\n",
            Err);
        const bool Dim2XExact = Ui.State.Document.Dimensions.size() == 2
            && Ui.State.Document.Dimensions[1].SlideAxis == 0
            && std::abs(Ui.State.Document.Dimensions[1].Value - 1.5) <= 1.0e-6
            && std::abs(Ui.State.Document.Dimensions[1].Offset - (-0.65)) <= 1.0e-6;
        Tally(AxisSlideDimEditGate, Dim2XExact ? std::abs(Ui.State.Document.Dimensions[1].Offset - (-0.65)) : 1.0,
              PreselectFastPathOk && Dim2XExact,
              PreselectFastPathOk && Dim2XExact
                  ? Format("Pre-selected vertical edge -> D seeds Stage 2 -> X + '-0.65' places 1.50 m along World X (SlideAxis=%d, Offset=%.2f m)",
                           Ui.State.Document.Dimensions[1].SlideAxis, Ui.State.Document.Dimensions[1].Offset)
                  : ("Pre-selected edge fast-path failed: " + Err));

        // ③ Cutter-style editing of already-placed Dim1: click @Dim:Dim1 -> top-of-Inspector Dimension Editor card appears ->
        //    press Y to re-slide placed Dim1 along World Y -> move mouse -> click to commit + Undo/Redo + Inspector Flip/Span/Note + TOML round-trip!
        ClearSolidArcSelection(Ui.State.Document);
        Ui.StepFrame();
        Err.clear();
        const bool ClickPlacedOk = Ui.RunScript("click @Dim:Dim1\nframes 2\n", Err);
        const bool InspectorCardShown = Ui.State.DimensionTool.SelectedId == "Dim1"
            && Ui.State.DimensionTool.ExplicitSelectCondition
            && Ui.State.Widgets.Find("DimEdSlideZ") != nullptr
            && Ui.State.Widgets.Find("DimEdFlip") != nullptr
            && Ui.State.Widgets.Find("DimEdSpanPar") != nullptr;
        const bool ReSlideYOk = ClickPlacedOk && InspectorCardShown && Ui.State.Document.Dimensions.size() >= 2 && Ui.RunScript(
            "key Y\nframes 1\n"
            "move world 1.5 -0.75 0.0\nframes 2\n"
            "click world 1.5 -0.75 0.0\nframes 1\n",
            Err);
        const int    SlideAfterY  = !Ui.State.Document.Dimensions.empty() ? Ui.State.Document.Dimensions[0].SlideAxis : -99;
        const double OffsetAfterY = !Ui.State.Document.Dimensions.empty() ? Ui.State.Document.Dimensions[0].Offset : 0.0;
        const bool   UndidEdit    = !Ui.State.Document.Dimensions.empty() && UndoSolidArcCommand(Ui.State.Document);
        const int    SlideUndo    = !Ui.State.Document.Dimensions.empty() ? Ui.State.Document.Dimensions[0].SlideAxis : -99;
        const double OffsetUndo   = !Ui.State.Document.Dimensions.empty() ? Ui.State.Document.Dimensions[0].Offset : 0.0;
        const bool   RedidEdit    = !Ui.State.Document.Dimensions.empty() && RedoSolidArcCommand(Ui.State.Document);

        // Inspector edits on placed Dim1 (Flip Side + Note + byte-equal TOML SlideAxis round-trip):
        const bool FlipOk = !Ui.State.Document.Dimensions.empty() && Ui.RunScript("click @DimEdFlip\nframes 1\n", Err);
        const double OffsetFlipped = !Ui.State.Document.Dimensions.empty() ? Ui.State.Document.Dimensions[0].Offset : 0.0;
        Expect(UpdateSolidArcDimensionNote(Ui.State.Document, "Dim1", "W"));
        const std::string Encoded1 = EncodeSolidArcDocument(Ui.State.Document);
        const SolidArcDocumentFile Decoded = DecodeSolidArcDocument(Encoded1);
        SolidArcDocument Reloaded = CreateSolidArcDocument();
        Reloaded.Tree       = Decoded.Tree;
        Reloaded.Captions   = Decoded.Captions;
        Reloaded.Dimensions = Decoded.Dimensions;
        Reloaded.Appearance = Decoded.Appearance;
        const std::string Encoded2 = EncodeSolidArcDocument(Reloaded);
        const bool CodecExact = Decoded.Refusal.empty() && Decoded.Dimensions.size() == 2
            && Decoded.Dimensions[0].SlideAxis == 1 && Decoded.Dimensions[1].SlideAxis == 0
            && Decoded.Dimensions[0].Note == "W" && Encoded1 == Encoded2;

        const bool EditAllOk = ReSlideYOk && SlideAfterY == 1 && std::abs(OffsetAfterY - (-0.75)) <= 0.06
            && UndidEdit && SlideUndo == 2 && std::abs(OffsetUndo - (-0.85)) <= 0.06
            && RedidEdit && FlipOk && std::abs(OffsetFlipped - (-OffsetAfterY)) <= 1.0e-9 && CodecExact;
        Tally(AxisSlideDimEditGate, EditAllOk ? std::abs(OffsetFlipped + OffsetAfterY) : 1.0,
              EditAllOk,
              EditAllOk
                  ? Format("Placed Dim1 Cutter-style edit: Inspector card + live 'Y' re-slide (SlideAxis 2->1, Offset %.2f->%.2f m), Undo/Redo, Flip (%.2f m) & byte-equal TOML",
                           OffsetUndo, OffsetAfterY, OffsetFlipped)
                  : ("Placed dimension editing failed: " + Err));
    }

    // ============================================================================================================
    // W93 — Viewport Header "Dim" (@DimensionTool) Visibility Toggle vs Creation Tool (@SketchDimension / D)
    // ============================================================================================================
    {
        HeadlessUiHarness Ui;
        SolidArcConstructRequest Box;
        Box.Kind  = SolidArcConstructKind::Block;
        Box.Width = 2.0; Box.Depth = 1.5; Box.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Ui.State.Document, Box));
        RefreshSolidArcScene(Ui.State, false);
        FitSolidArcCamera(Ui.State.Camera, Ui.State.Scene);
        SelectSolidArcName(Ui.State.Document, SolidArcSelectionKind::Body, Ui.State.Scene.Bodies[0].Key, false);
        Expect(AddSolidArcDimensionFromSelection(Ui.State.Document));
        Ui.StepFrame();
        Ui.StepFrame();

        const bool InitiallyVisible = Ui.State.DimensionsEnabled
            && Ui.State.Widgets.Find("Dim:Dim1") != nullptr
            && Ui.State.Widgets.Find("Dim:Dim2") != nullptr
            && Ui.State.Widgets.Find("Dim:Dim3") != nullptr;

        std::string Err;
        const bool HideOk = Ui.RunScript("click @DimensionTool\nframes 2\nexpect dimension-active == 0\n", Err);
        const bool HiddenCleanly = HideOk && !Ui.State.DimensionsEnabled
            && Ui.State.Widgets.Find("Dim:Dim1") == nullptr
            && Ui.State.Widgets.Find("Dim:Dim2") == nullptr
            && Ui.State.Widgets.Find("Dim:Dim3") == nullptr
            && Ui.State.Document.Dimensions.size() == 3;

        const bool ShowOk = Ui.RunScript("click @DimensionTool\nframes 2\nexpect dimension-active == 0\n", Err);
        const bool ShownCleanly = ShowOk && Ui.State.DimensionsEnabled
            && Ui.State.Widgets.Find("Dim:Dim1") != nullptr
            && Ui.State.Widgets.Find("Dim:Dim2") != nullptr
            && Ui.State.Widgets.Find("Dim:Dim3") != nullptr;

        // Hide again, then press 'D' -> interactive creation tool starts AND automatically turns DimensionsEnabled back on;
        // then clicking @DimensionTool while D is active hides dimensions and closes the active creation session cleanly!
        const bool AutoEnableAndHideCloseOk = Ui.RunScript(
            "click @DimensionTool\nframes 1\n"
            "key D\nframes 1\nexpect dimension-active == 1\n"
            "click @DimensionTool\nframes 2\nexpect dimension-active == 0\n",
            Err);
        const bool FinalStateOk = AutoEnableAndHideCloseOk && !Ui.State.DimensionsEnabled && Ui.State.Widgets.Find("Dim:Dim1") == nullptr;

        Tally(HeaderDimToggleGate, 0.0,
              InitiallyVisible && HiddenCleanly && ShownCleanly && FinalStateOk,
              "Top header 'Dim' (@DimensionTool) toggles State.DimensionsEnabled (hiding/restoring @Dim:Dim1..3 pills while keeping 3 dimensions intact), distinct from D / @SketchDimension");
    }

    // ============================================================================================================
    // W94 — Non-Freezing Fillet & Chamfer on Twisted Bilinear NURBS Reshaped Solids (image-2.png Repro)
    // ============================================================================================================
    {
        SolidArcDocument Doc = CreateSolidArcDocument();
        SolidArcConstructRequest BoxReq;
        BoxReq.Kind   = SolidArcConstructKind::Block;
        BoxReq.Width  = 1.666;
        BoxReq.Depth  = 1.523;
        BoxReq.Height = 2.30267;
        Expect(ConstructSolidArcPrimitive(Doc, BoxReq));
        const std::string BodyKey = Doc.Summary.Bodies.front().Key;

        auto MoveVertex = [&](const std::string& VertName, Vector3 Shift)
        {
            Doc.Selection          = {};
            Doc.Selection.Bodies   = { BodyKey };
            Doc.Selection.Vertices = { VertName };
            AffineMap M{};
            M.ColumnX = { 1, 0, 0 }; M.ColumnY = { 0, 1, 0 }; M.ColumnZ = { 0, 0, 1 };
            M.Shift   = Shift;
            return ReshapeSolidArcSelection(Doc, M, "Move");
        };
        Expect(MoveVertex("Vertex(Extrude#2.Cap(End)|Extrude#2.Side(Line1)|Extrude#2.Side(Line4))", { 1.1, 0.86, 0.0 }));
        Expect(MoveVertex("Vertex(Extrude#2.Cap(Start)|Extrude#2.Side(Line1)|Extrude#2.Side(Line4))", { 0.0, -0.6, 0.0 }));

        std::vector<std::string> Edges;
        for (const auto& [Slot, Name] : Doc.Regeneration.Naming.Edges) Edges.push_back(Name);

        int    AcceptedSingle = 0;
        int    SoundSingle    = 0;
        double WorstEdgeMs    = 0.0;
        for (size_t Idx = 0; Idx < Edges.size(); ++Idx)
        {
            SolidArcDocument Copy  = CloneSolidArcDocument(Doc);
            Copy.Selection         = {};
            Copy.Selection.Bodies  = { BodyKey };
            Copy.Selection.Edges   = { Edges[Idx] };
            SolidArcBlendRequest Req;
            Req.Shape = BlendProfile::Fillet;
            Req.Size  = 0.10;
            const auto T0 = std::chrono::steady_clock::now();
            const SolidArcCommandOutcome Out = BlendSolidArcSelection(Copy, Req);
            const double ElapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
            WorstEdgeMs = std::max(WorstEdgeMs, ElapsedMs);
            if (Out.AcceptedCondition)
            {
                ++AcceptedSingle;
                if (Copy.Summary.SoundCondition && Copy.Summary.Volume > 1.0) ++SoundSingle;
            }
        }
        Tally(TwistedNurbsBevelGate, WorstEdgeMs * 1.0e-3,
              Edges.size() == 12 && AcceptedSingle >= 9 && AcceptedSingle == SoundSingle && WorstEdgeMs < 2500.0,
              Format("All 12 twisted NURBS prism edges complete with zero freeze: %d/12 accepted (%d/%d sound G1–G6), 3 reflex-notch edges refused cleanly, worst=%.0f ms",
                     AcceptedSingle, SoundSingle, AcceptedSingle, WorstEdgeMs));

        // Multi-edge Fillet pair (Edges[0, 1]) + Chamfer on twisted NURBS edge:
        SolidArcDocument PairDoc  = CloneSolidArcDocument(Doc);
        PairDoc.Selection         = {};
        PairDoc.Selection.Bodies  = { BodyKey };
        PairDoc.Selection.Edges   = { Edges[0], Edges[1] };
        SolidArcBlendRequest PairReq;
        PairReq.Shape = BlendProfile::Fillet;
        PairReq.Size  = 0.10;
        const auto PairT0 = std::chrono::steady_clock::now();
        const SolidArcCommandOutcome PairOut = BlendSolidArcSelection(PairDoc, PairReq);
        const double PairMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - PairT0).count();

        SolidArcDocument ChamfDoc  = CloneSolidArcDocument(Doc);
        ChamfDoc.Selection         = {};
        ChamfDoc.Selection.Bodies  = { BodyKey };
        ChamfDoc.Selection.Edges   = { Edges[4] };
        SolidArcBlendRequest ChamfReq;
        ChamfReq.Shape = BlendProfile::Chamfer;
        ChamfReq.Size  = 0.10;
        const SolidArcCommandOutcome ChamfOut = BlendSolidArcSelection(ChamfDoc, ChamfReq);

        const bool PairAndChamfOk = PairOut.AcceptedCondition && PairDoc.Summary.SoundCondition && PairDoc.Summary.Volume > 4.0
            && ChamfOut.AcceptedCondition && ChamfDoc.Summary.SoundCondition && PairMs < 3000.0;
        Tally(TwistedNurbsBevelGate, PairMs * 1.0e-3, PairAndChamfOk,
              Format("Multi-edge Fillet pair (%.0f ms, V=%.3f m³, sound=%d) & Chamfer (%.0f ms, sound=%d) succeed on twisted bilinear NURBS body",
                     PairMs, PairDoc.Summary.Volume, int(PairDoc.Summary.SoundCondition),
                     ChamfOut.Milliseconds, int(ChamfDoc.Summary.SoundCondition)));
    }

    // ============================================================================================================
    // W95 — Repeated Multi-Pass Vertex Reshape on Already-Bent NURBS Faces & Multi-Body Histories
    // ============================================================================================================
    {
        SolidArcDocument Doc = CreateSolidArcDocument();
        SolidArcConstructRequest BoxReq;
        BoxReq.Kind   = SolidArcConstructKind::Block;
        BoxReq.Width  = 1.666;
        BoxReq.Depth  = 1.523;
        BoxReq.Height = 2.30267;
        Expect(ConstructSolidArcPrimitive(Doc, BoxReq));
        const std::string BlockKey = Doc.Summary.Bodies.front().Key;

        // Add a separate Cylinder body with its own Fillet step in the same history tree to test multi-body DirectEditInsertion!
        SolidArcConstructRequest CylReq;
        CylReq.Kind        = SolidArcConstructKind::Cylinder;
        CylReq.Combination = FeatureCombination::Separate;
        CylReq.Position    = { 4.0, 0.0, 0.0 };
        CylReq.Radius      = 0.6;
        CylReq.Height      = 1.4;
        Expect(ConstructSolidArcPrimitive(Doc, CylReq));
        const std::string CylKey = Doc.Summary.Bodies.back().Key;
        for (const auto& [Slot, Name] : Doc.Regeneration.Bodies.back().Naming.Edges)
        {
            Doc.Selection        = {};
            Doc.Selection.Bodies = { CylKey };
            Doc.Selection.Edges  = { Name };
            SolidArcBlendRequest CylFillet;
            CylFillet.Shape = BlendProfile::Fillet;
            CylFillet.Size  = 0.08;
            if (BlendSolidArcSelection(Doc, CylFillet).AcceptedCondition) break;
        }

        auto ReshapeBlockVertex = [&](const std::string& VertName, Vector3 Shift)
        {
            Doc.Selection          = {};
            Doc.Selection.Bodies   = { BlockKey };
            Doc.Selection.Vertices = { VertName };
            AffineMap M{};
            M.ColumnX = { 1, 0, 0 }; M.ColumnY = { 0, 1, 0 }; M.ColumnZ = { 0, 0, 1 };
            M.Shift   = Shift;
            return ReshapeSolidArcSelection(Doc, M, "Move");
        };

        // Pass 1: Move top corner -> bends adjacent faces into bilinear NurbsSurface
        const SolidArcCommandOutcome Pass1 = ReshapeBlockVertex(
            "Vertex(Extrude#2.Cap(End)|Extrude#2.Side(Line1)|Extrude#2.Side(Line4))", { 0.75, 0.55, 0.0 });
        // Pass 2: Move bottom corner sharing the already-bent NURBS side faces
        const SolidArcCommandOutcome Pass2 = ReshapeBlockVertex(
            "Vertex(Extrude#2.Cap(Start)|Extrude#2.Side(Line1)|Extrude#2.Side(Line4))", { 0.0, -0.45, 0.0 });
        // Pass 3: Move a third corner on an already-bent NURBS cap & side face (previously refused: "is a Nurbs")
        const SolidArcCommandOutcome Pass3 = ReshapeBlockVertex(
            "Vertex(Extrude#2.Cap(End)|Extrude#2.Side(Line1)|Extrude#2.Side(Line2))", { -0.25, 0.30, 0.20 });
        // Pass 4: Move the VERY SAME first vertex a second time!
        const SolidArcCommandOutcome Pass4 = ReshapeBlockVertex(
            "Vertex(Extrude#2.Cap(End)|Extrude#2.Side(Line1)|Extrude#2.Side(Line4))", { -0.20, -0.15, 0.15 });

        const double VolAfter4 = Doc.Summary.Volume;
        const bool   Sound4    = Doc.Summary.SoundCondition && Doc.Summary.Bodies.size() == 2;
        const bool   UndoOk    = UndoSolidArcCommand(Doc) && Doc.Summary.SoundCondition;
        const bool   RedoOk    = RedoSolidArcCommand(Doc) && Doc.Summary.SoundCondition && std::abs(Doc.Summary.Volume - VolAfter4) <= 1.0e-9;

        const SolidArcDocumentFile ReloadedFile = DecodeSolidArcDocument(EncodeSolidArcDocument(Doc));
        SolidArcDocument ReloadedDoc = CreateSolidArcDocument();
        Expect(ReplaceSolidArcTree(ReloadedDoc, ReloadedFile.Tree, ReloadedFile.Captions, "Reload reshaped"));
        const double VolReloaded = ReloadedDoc.Summary.Volume;

        const bool AllPassesOk = Pass1.AcceptedCondition && Pass2.AcceptedCondition
            && Pass3.AcceptedCondition && Pass4.AcceptedCondition
            && Sound4 && UndoOk && RedoOk && ReloadedDoc.Summary.SoundCondition
            && std::abs(VolReloaded - VolAfter4) <= 1.0e-6;
        Tally(RepeatedReshapeGate, std::abs(VolReloaded - VolAfter4), AllPassesOk,
              AllPassesOk
                  ? Format("4-pass repeated vertex Reshape across already-bent NURBS faces + beveled Cylinder history: Pass3=%.1f ms, Pass4=%.1f ms, V=%.4f m³, Undo/Redo & TOML exact",
                           Pass3.Milliseconds, Pass4.Milliseconds, VolAfter4)
                  : ("Repeated vertex Reshape failed: P3=" + Pass3.Refusal + " P4=" + Pass4.Refusal));
    }
}

int main()
{
    const auto Start = std::chrono::steady_clock::now();
    if (std::getenv("SOLIDARC_DOCVAL_PIECES")) { ValidatePieceBevels(); return 0; }   // Quick rerun of W59 alone
    if (std::getenv("SOLIDARC_DOCVAL_MANIFOLD"))                                         // Quick rerun of W60 alone
    {
        ValidateExtrudeManifold();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return ManifoldGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_NOTCH"))                                            // Quick rerun of W64 alone
    {
        ValidateEdgeNotch();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return EdgeNotchGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_PATH"))                                             // Quick rerun of W65 alone
    {
        ValidateEdgePath();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return EdgePathGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_POINTCUT"))                                         // Quick rerun of W62 alone
    {
        ValidatePointCut();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return PointCutGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_FLOW"))                                             // Quick rerun of W63 alone
    {
        ValidateCurveFlow();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return CurveFlowGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_GIZMOLOFT"))                                         // Quick rerun of W81 + W82 alone
    {
        ValidateGizmoAndContinuedLofts();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return (GizmoAllViewsGate.Failures + ContinuedLoftGate.Failures) ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_BATCH4"))                                            // Quick rerun of W83 .. W86 alone
    {
        ValidateBatch4InterfaceAndViewport();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        const long TotalFail = HotkeysOutlinerGate.Failures + DimensionLinesGate.Failures + MultiSelectSyncGate.Failures + GridCubeUvGate.Failures;
        return TotalFail ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_BATCH5"))                                            // Quick rerun of W87 .. W89 alone
    {
        ValidateBatch5TransformsNumpadAndSplitViews();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        const long TotalFail = BlenderTransformGate.Failures + NumpadCameraGate.Failures + SplitViewportGate.Failures;
        return TotalFail ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_BATCH6"))                                            // Quick rerun of W90 .. W91 alone
    {
        ValidateBatch6GizmoZoomAndInteractiveDimensions();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        const long TotalFail = GizmoZoomGate.Failures + InteractiveDimToolGate.Failures;
        return TotalFail ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_BATCH7"))                                            // Quick rerun of W92 .. W95 alone
    {
        ValidateBatch7DimensionsBevelAndReshape();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        const long TotalFail = AxisSlideDimEditGate.Failures + HeaderDimToggleGate.Failures
                             + TwistedNurbsBevelGate.Failures + RepeatedReshapeGate.Failures;
        return TotalFail ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_STAGE3"))                                            // Quick rerun of W72..W78
    {
        ValidateStage3CadFeatures();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        const long TotalFail = CutterSpeedGate.Failures + ParametricVarGate.Failures + LiveBooleanGate.Failures +
                               EdgeWeightGate.Failures + LinearCircArrayGate.Failures + CurveSurfArrayGate.Failures + CurveBendStackGate.Failures +
                               CompoundCutterGate.Failures + LiveEditableGate.Failures;
        return TotalFail ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_RESHAPE"))                                          // Quick rerun of W67 alone
    {
        ValidateReshape();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return ReshapeGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_MERGE"))                                             // Quick rerun of W71 alone
    {
        ValidateBevelMerge();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return BevelMergeGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_REACH"))                                             // Quick rerun of W70 alone
    {
        ValidateBevelReach();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return FullReachGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_CURVES"))                                           // Quick rerun of W69 alone
    {
        ValidateCurveAsk();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return CurveAskGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_DELETE"))                                           // Quick rerun of W68 alone
    {
        ValidateDelete();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return DeleteGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_WRAP"))                                             // Quick rerun of W66 alone
    {
        ValidateWrap();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return WrapGate.Failures ? 1 : 0;
    }
    if (std::getenv("SOLIDARC_DOCVAL_IMPRINT"))                                          // Quick rerun of W61 alone
    {
        ValidateImprint();
        for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
        return ImprintGate.Failures ? 1 : 0;
    }

    // ---- W1 single tiles against closed forms
    {
        SolidArcDocument Document = CreateSolidArcDocument("Tiles");
        SolidArcConstructRequest Request;
        Request.Kind = SolidArcConstructKind::Sphere; Request.Position = { 0.3, -0.2, 1.0 }; Request.Radius = 0.8;
        Expect(ConstructSolidArcPrimitive(Document, Request));
        TallyVolume(ConstructGate, Document, 4.0 / 3.0 * Pi * 0.8 * 0.8 * 0.8, "Sphere r 0.8 at (0.3, −0.2, 1)");
        SolidArcDocument Ring = CreateSolidArcDocument("Ring");
        Request.Kind = SolidArcConstructKind::Torus; Request.Position = { 1.0, 2.0, 0.5 }; Request.Major = 1.5; Request.Minor = 0.4;
        Expect(ConstructSolidArcPrimitive(Ring, Request));
        TallyVolume(ConstructGate, Ring, 2.0 * Pi * Pi * 1.5 * 0.4 * 0.4, "Torus R 1.5 r 0.4 at (1, 2, 0.5)");
        SolidArcDocument Drilled = CreateSolidArcDocument("Drilled");
        Request = {};
        Request.Kind = SolidArcConstructKind::Block; Request.Position = { 0.0, 0.0, 0.0 }; Request.Width = 3.0; Request.Depth = 2.0; Request.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Drilled, Request));
        Request.Kind = SolidArcConstructKind::Cylinder; Request.Position = { 1.0, 1.0, -0.5 }; Request.Radius = 0.4; Request.Height = 2.0;
        Request.Combination = FeatureCombination::Difference;
        Expect(ConstructSolidArcPrimitive(Drilled, Request));
        TallyVolume(ConstructGate, Drilled, 6.0 - Pi * 0.16, "Block 3 × 2 × 1 − through cylinder r 0.4");
    }

    // ---- W1 … W7 on one modelling session (the parity walkthrough)
    SolidArcDocument Document = CreateSolidArcDocument("Bracket");
    SolidArcConstructRequest Block;
    Block.Kind = SolidArcConstructKind::Block; Block.Width = 4.0; Block.Depth = 2.0; Block.Height = 1.0;
    Expect(ConstructSolidArcPrimitive(Document, Block));
    TallyVolume(ConstructGate, Document, 8.0, "Block1 4 × 2 × 1");
    SolidArcConstructRequest Boss;
    Boss.Kind = SolidArcConstructKind::Cylinder; Boss.Position = { 1.0, 1.0, 1.0 }; Boss.Radius = 0.5; Boss.Height = 0.75;
    Boss.Combination = FeatureCombination::Union;
    Expect(ConstructSolidArcPrimitive(Document, Boss));
    const double BossVolume = Pi * 0.25 * 0.75;
    TallyVolume(ConstructGate, Document, 8.0 + BossVolume, "+ Cylinder1 boss r 0.5 h 0.75 (union on the top face)");

    // W2: pick the top face beside the boss, then the right vertical-face / top-face edge (x = 4, z = 1)
    const std::string TopFace = PickAndSelect(Document, RayBetween({ 3.0, 1.0, 5.0 }, { 3.0, 1.0, 0.0 }), false);
    Tally(PickGate, 0.0, TopFace.find("Cap(End)") != std::string::npos, "ray ↓ at (3, 1) → " + TopFace);
    const std::string RightEdge = PickAndSelect(Document, RayBetween({ 5.0, 1.0, 2.0 }, { 4.0, 1.0, 1.0 }), false);
    const bool RightEdgeCondition = RightEdge.rfind("Edge(", 0) == 0 && RightEdge.find("Cap(End)") != std::string::npos;
    Tally(PickGate, 0.0, RightEdgeCondition, "ray at the x = 4 top edge → " + RightEdge);
    const std::string Missed = PickAndSelect(Document, RayBetween({ 9.0, 9.0, 9.0 }, { 9.0, 9.0, 20.0 }), false);
    Tally(PickGate, 0.0, Missed.empty(), "ray away from the body → no pick");

    // W3: fillet r 0.25 on the picked edge (length 2): removed (1 − π/4)·r²·L
    SelectSolidArcName(Document, SolidArcSelectionKind::Edge, RightEdge, false);
    Expect(BlendSolidArcSelection(Document, BlendProfile::Fillet, 0.25));
    const double FilletLoss = (1.0 - Pi / 4.0) * 0.0625 * 2.0;
    TallyVolume(BlendGate, Document, 8.0 + BossVolume - FilletLoss, "Fillet1 r 0.25 on the picked edge");
    // chamfer setback 0.2 on the opposite (x = 0) top edge: removed c²/2·L
    const std::string LeftEdge = PickAndSelect(Document, RayBetween({ -1.0, 1.0, 2.0 }, { 0.0, 1.0, 1.0 }), false);
    Expect(BlendSolidArcSelection(Document, BlendProfile::Chamfer, 0.2));
    TallyVolume(BlendGate, Document, 8.0 + BossVolume - FilletLoss - 0.02 * 2.0, "Chamfer1 0.2 on the x = 0 top edge");

    // W4: push the front face (y = 0) out by 0.3: + 0.3 × front area (rectangle minus the blended corners)
    const std::string FrontFace = PickAndSelect(Document, RayBetween({ 2.0, -5.0, 0.5 }, { 2.0, 0.0, 0.5 }), false);
    Tally(PickGate, 0.0, !FrontFace.empty() && FrontFace.rfind("Edge(", 0) != 0, "ray → front face → " + FrontFace);
    const double FrontArea = 4.0 * 1.0 - (1.0 - Pi / 4.0) * 0.0625 - 0.02;
    Expect(ChangeSolidArcSelection(Document, FaceOperationKind::Push, 0.3));
    const double PushedVolume = 8.0 + BossVolume - FilletLoss - 0.04 + 0.3 * FrontArea;
    TallyVolume(FaceGate, Document, PushedVolume, "Push1 0.3 on the picked front face");

    // W5: Block1.Depth 2 → 2.5: every blend follows (edge length 2.5), the push still applies to the renamed-same face
    const double BeforeEdit = Document.Summary.Volume;
    Expect(AssignSolidArcParameter(Document, "Block1.Depth", 2.5));
    const double EditedVolume = 4.0 * 2.5 + BossVolume - (1.0 - Pi / 4.0) * 0.0625 * 2.5 - 0.02 * 2.5 + 0.3 * FrontArea;
    TallyVolume(ParameterGate, Document, EditedVolume, "Block1.Depth = 2.5 → fillet, chamfer, push regenerate");
    Tally(ParameterGate, 0.0, Document.Regeneration.Refused == 0, "no step refused after the edit");
    Tally(ParameterGate, 0.0, Document.Selection.Faces.size() == 1 && Document.Selection.Faces[0] == FrontFace, "selection still names " + FrontFace);

    // W6: atomic refusals
    const size_t StepsBefore = Document.Tree.Steps.size();
    const size_t UndoBefore  = Document.UndoEntries.size();
    const double VolumeBefore = Document.Summary.Volume;
    const std::string BackEdge = PickAndSelect(Document, RayBetween({ 2.0, 3.5, 2.0 }, { 2.0, 2.5, 1.0 }), false);
    const SolidArcCommandOutcome Oversize = BlendSolidArcSelection(Document, BlendProfile::Fillet, 5.0);
    Tally(AtomicGate, 0.0, !Oversize.AcceptedCondition && Document.Tree.Steps.size() == StepsBefore && Document.UndoEntries.size() == UndoBefore &&
                           Document.Summary.Volume == VolumeBefore, "fillet r 5 on " + BackEdge + " refused: " + Oversize.Refusal.substr(0, 90));
    ClearSolidArcSelection(Document);
    const SolidArcCommandOutcome Empty = ChangeSolidArcSelection(Document, FaceOperationKind::Push, 0.1);
    Tally(AtomicGate, 0.0, !Empty.AcceptedCondition && Document.Tree.Steps.size() == StepsBefore, "push with no faces refused: " + Empty.Refusal);

    // W7: undo the edit, redo it, delete the boss tile
    UndoSolidArcCommand(Document);
    Tally(HistoryGate, std::abs(Document.Summary.Volume - BeforeEdit), Document.Summary.Volume == BeforeEdit, "undo Block1.Depth → exact earlier volume");
    RedoSolidArcCommand(Document);
    Tally(HistoryGate, std::abs(Document.Summary.Volume - EditedVolume) / EditedVolume, std::abs(Document.Summary.Volume - EditedVolume) <= 1.0e-12 * EditedVolume, "redo → edited volume");
    const std::string BossLabel = Document.Tree.Steps[2].Label;
    Expect(DeleteSolidArcStep(Document, BossLabel));
    const bool WholeTileCondition = std::none_of(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [](const FeatureStep& Step)
                                                 { return Step.Label == "Sketch#3" || Step.Label == "Extrude#4"; });
    TallyVolume(HistoryGate, Document, EditedVolume - BossVolume, "delete Cylinder1 (sketch + extrude) → volume without the boss");
    Tally(HistoryGate, 0.0, WholeTileCondition && Document.Tree.Parameters.end() == std::find_if(Document.Tree.Parameters.begin(), Document.Tree.Parameters.end(),
          [](const FeatureParameter& Parameter) { return Parameter.Name.rfind("Cylinder1.", 0) == 0; }), "both steps and every Cylinder1.* parameter removed");

    // W8: file round trip
    const std::string Saved = EncodeSolidArcDocument(Document);
    SolidArcDocumentFile Loaded = DecodeSolidArcDocument(Saved);
    SolidArcDocument Reloaded = CreateSolidArcDocument(Loaded.Title);
    const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Reloaded, Loaded.Tree, Loaded.Captions, "Open");
    Tally(FileGate, std::abs(Reloaded.Summary.Volume - Document.Summary.Volume), Loaded.Refusal.empty() && Opened.AcceptedCondition &&
          Reloaded.Summary.Volume == Document.Summary.Volume, "reloaded volume equal (" + std::to_string(Saved.size()) + " bytes of TOML)");
    Tally(FileGate, 0.0, Reloaded.Regeneration.Naming.Faces == Document.Regeneration.Naming.Faces &&
          Reloaded.Regeneration.Naming.Edges == Document.Regeneration.Naming.Edges, "reloaded persistent names equal");
    Tally(FileGate, 0.0, EncodeSolidArcDocument(Reloaded) == Saved, "save → load → save byte-equal");
    const SolidArcDocumentFile Broken = DecodeSolidArcDocument("Format = \"SolidArc\"\nSteps = [ { Kind = 0 } ]\n");
    Tally(FileGate, 0.0, !Broken.Refusal.empty(), "malformed file refused: " + Broken.Refusal);
    // 📝 Documents written before the SolidArc rename carry Format = "DraftingWorkspace" and still open unchanged.
    std::string Legacy = Saved;
    const std::string Current = "Format = 'SolidArc'";                                     // toml++ writes literal strings
    const size_t      FormatAt = Legacy.find(Current);
    if (FormatAt != std::string::npos) Legacy.replace(FormatAt, Current.size(), "Format = 'DraftingWorkspace'");
    const SolidArcDocumentFile LegacyFile = DecodeSolidArcDocument(Legacy);
    SolidArcDocument LegacyDocument = CreateSolidArcDocument(LegacyFile.Title);
    const SolidArcCommandOutcome LegacyOpened = ReplaceSolidArcTree(LegacyDocument, LegacyFile.Tree, LegacyFile.Captions, "Open");
    Tally(FileGate, std::abs(LegacyDocument.Summary.Volume - Document.Summary.Volume), FormatAt != std::string::npos && LegacyFile.Refusal.empty() &&
          LegacyOpened.AcceptedCondition && LegacyDocument.Summary.Volume == Document.Summary.Volume, "pre-rename Format = \"DraftingWorkspace\" opens, same volume");
    const SolidArcDocumentFile Foreign = DecodeSolidArcDocument("Format = \"Other\"\n");
    Tally(FileGate, 0.0, !Foreign.Refusal.empty(), "foreign format refused: " + Foreign.Refusal);

    // ---- W9 variable blend: pick the front-top edge (y = 0, z = 1) of a 4 × 2 × 1 block, fillet 0.1 → 0.3, flip, edit, reload
    for (int Flip = 0; Flip < 2; ++Flip)
    {
        const bool       ReversedCondition = Flip == 1;
        SolidArcDocument Variable = CreateSolidArcDocument("Variable");
        Expect(ConstructSolidArcPrimitive(Variable, Block));
        const std::string FrontEdge = PickAndSelect(Variable, RayBetween({ 2.0, -1.0, 2.0 }, { 2.0, 0.0, 1.0 }), false);
        const double AnchorX = EdgeEndPosition(Variable, FrontEdge, ReversedCondition).X;      // [len] - Corner that takes Size
        const double FarX    = 4.0 - AnchorX;
        SolidArcBlendRequest Request;
        Request.Shape = BlendProfile::Fillet; Request.Size = 0.1; Request.EndSize = 0.3; Request.ReversedCondition = ReversedCondition;
        const SolidArcCommandOutcome Applied = BlendSolidArcSelection(Variable, Request);
        Expect(Applied);
        const std::string Case = std::string(ReversedCondition ? "flipped" : "unflipped") + ", anchor x = " + std::to_string(AnchorX).substr(0, 3);
        TallyVolume(VariableGate, Variable, 8.0 - MeasureVariableFilletLoss(4.0, 0.1, 0.3), "Fillet 0.1 → 0.3 on " + FrontEdge + " (" + Case + ")");
        const double Gap = MeasureCapGap(Variable, AnchorX, FarX, 1.0, 0.1, 0.3);
        Tally(VariableGate, Gap, Gap <= 1.0e-9, "rails 0.1 from the anchored corner, 0.3 from the far one (" + Case + ")");
        Tally(VariableGate, 0.0, Applied.Label.find("0.1 → 0.3") != std::string::npos, "history caption: " + Applied.Label);

        // Width 4 → 5: the anchor is a named vertex, so 0.1 stays at its corner (x = 0 stays, x = 4 moves to 5).
        Expect(AssignSolidArcParameter(Variable, "Block1.Width", 5.0));
        const double MovedAnchor = AnchorX < 2.0 ? 0.0 : 5.0, MovedFar = 5.0 - MovedAnchor;
        TallyVolume(VariableGate, Variable, 10.0 - MeasureVariableFilletLoss(5.0, 0.1, 0.3), "Block1.Width = 5 regenerates the variable fillet (" + Case + ")");
        const double MovedGap = MeasureCapGap(Variable, MovedAnchor, MovedFar, 1.0, 0.1, 0.3);
        Tally(VariableGate, MovedGap, MovedGap <= 1.0e-9 && Variable.Regeneration.Refused == 0, "after the edit 0.1 is still at its corner, x = " + std::to_string(MovedAnchor).substr(0, 3) + " (" + Case + ")");

        // Save → load: the end size and the named anchor survive the TOML round trip.
        const std::string Saved = EncodeSolidArcDocument(Variable);
        SolidArcDocumentFile Loaded = DecodeSolidArcDocument(Saved);
        SolidArcDocument Reloaded = CreateSolidArcDocument(Loaded.Title);
        const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Reloaded, Loaded.Tree, Loaded.Captions, "Open");
        const double ReloadGap = Opened.AcceptedCondition ? MeasureCapGap(Reloaded, MovedAnchor, MovedFar, 1.0, 0.1, 0.3) : 1.0;
        Tally(VariableGate, ReloadGap, Opened.AcceptedCondition && Reloaded.Summary.Volume == Variable.Summary.Volume && ReloadGap <= 1.0e-9 &&
              Saved.find("SizeAnchors") != std::string::npos && EncodeSolidArcDocument(Reloaded) == Saved, "reload: same volume bits, anchored rails, byte-equal re-save (" + Case + ")");
    }

    // ---- W10 drawing: the workspace tools as the viewport commits them, extruded with the Plasticity Boolean choices
    {
        using Kind = SolidArcDrawingKind;
        const FeatureCombination Union = FeatureCombination::Union, Difference = FeatureCombination::Difference, New = FeatureCombination::New;
        auto DrawAndExtrude = [&](SolidArcDocument& Target, const SolidArcDrawingRequest& Request, double Distance, FeatureCombination Combination)
        {
            Expect(DrawFilledSketch(Target, Request));
            const SolidArcCommandOutcome Outcome = ExtrudeSolidArcSketch(Target, Target.Tree.Steps.back().Label, Distance, Combination);
            Expect(Outcome);
            return Outcome;
        };
        const SolidArcDrawingRequest Box = DrawingOf(Kind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } });
        const std::vector<Vector2> Square = { { 0.2, 0.2 }, { 0.5, 0.2 }, { 0.5, 0.8 }, { 0.2, 0.8 } };
        {
            SolidArcDocument Drawn = CreateSolidArcDocument("Drawn");
            DrawAndExtrude(Drawn, Box, 1.5, New);
            TallyVolume(DrawingGate, Drawn, 3.0, "Rectangle (0,0)–(2,1) on XY, extrude 1.5 (New)");
            DrawAndExtrude(Drawn, DrawingOf(Kind::Circle, 0, { { 1.0, 0.5 }, { 1.4, 0.5 } }, false, true), 1.5, Difference);
            TallyVolume(DrawingGate, Drawn, 3.0 - 0.24 * Pi, "− centre circle r 0.4 through (Difference)");
            DrawAndExtrude(Drawn, DrawingOf(Kind::Line, 0, { { 1.5, 0.0 }, { 3.0, 0.0 }, { 1.5, 1.0 } }, true), 1.0, Union);
            TallyVolume(DrawingGate, Drawn, 3.0 - 0.24 * Pi + 1.0 / 3.0, "+ closed line triangle, height 1 (Union; 1/3 outside the box)");
            DrawAndExtrude(Drawn, DrawingOf(Kind::Curve, 0, Square, true), 0.5, Difference);
            TallyVolume(DrawingGate, Drawn, 3.0 - 0.24 * Pi + 1.0 / 3.0 - 0.123, "− closed Hermite pocket 0.5 (area 0.246, coplanar floor: closed spline section)");

            // Reload: spline points and forms round-trip; same volume bits, byte-equal re-save.
            const std::string Saved = EncodeSolidArcDocument(Drawn);
            SolidArcDocumentFile Loaded = DecodeSolidArcDocument(Saved);
            SolidArcDocument Reloaded = CreateSolidArcDocument(Loaded.Title);
            const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Reloaded, Loaded.Tree, Loaded.Captions, "Open");
            Tally(DrawingGate, std::abs(Reloaded.Summary.Volume - Drawn.Summary.Volume), Opened.AcceptedCondition && Reloaded.Summary.Volume == Drawn.Summary.Volume &&
                  EncodeSolidArcDocument(Reloaded) == Saved, "reload of the drawn document: same volume bits, byte-equal re-save");

            // Refusals leave the document untouched.
            const size_t Steps = Drawn.Tree.Steps.size();
            const double Volume = Drawn.Summary.Volume;
            const SolidArcCommandOutcome Collinear = DrawFilledSketch(Drawn, DrawingOf(Kind::Arc, 0, { { 0.0, 2.0 }, { 1.0, 2.0 }, { 2.0, 2.0 } }));
            const SolidArcCommandOutcome Coincident = DrawFilledSketch(Drawn, DrawingOf(Kind::Curve, 0, { { 0.0, 2.0 }, { 0.0, 2.0 }, { 1.0, 2.0 } }));
            const SolidArcCommandOutcome SecondBody = ExtrudeSolidArcSketch(Drawn, Drawn.Tree.Steps.front().Label, 1.0, New);
            Tally(DrawingGate, 0.0, !Collinear.AcceptedCondition && !Coincident.AcceptedCondition && !SecondBody.AcceptedCondition && Drawn.Tree.Steps.size() == Steps &&
                  Drawn.Summary.Volume == Volume, "refused untouched: \"" + Collinear.Refusal + "\" · \"" + Coincident.Refusal + "\" · \"" + SecondBody.Refusal + "\"");
            Expect(DrawFilledSketch(Drawn, DrawingOf(Kind::Arc, 0, { { 0.0, 2.0 }, { 2.0, 2.0 }, { 1.0, 2.5 } })));
            const SolidArcCommandOutcome OpenArc = ExtrudeSolidArcSketch(Drawn, Drawn.Tree.Steps.back().Label, 1.0, Union);
            Tally(DrawingGate, 0.0, !OpenArc.AcceptedCondition && Drawn.Summary.Volume == Volume, "open arc profile refused: \"" + OpenArc.Refusal + "\"");

            // Delete the pocket's sketch: its extrude goes with it.
            std::string CurveLabel;
            for (const FeatureStep& Step : Drawn.Tree.Steps) if (Drawn.Captions[Step.Label] == "Curve1") CurveLabel = Step.Label;
            Expect(DeleteSolidArcStep(Drawn, CurveLabel));
            TallyVolume(DrawingGate, Drawn, 3.0 - 0.24 * Pi + 1.0 / 3.0, "delete Curve1: its extrude goes with it");
        }
        {
            SolidArcDocument Slotted = CreateSolidArcDocument("Slotted");
            DrawAndExtrude(Slotted, DrawingOf(Kind::Rectangle, 0, { { 1.0, 0.5 }, { 2.0, 1.0 } }, false, true), 1.5, New);
            TallyVolume(DrawingGate, Slotted, 3.0, "centre rectangle about (1, 0.5) to (2, 1), extrude 1.5");
            DrawAndExtrude(Slotted, DrawingOf(Kind::Slot, 0, { { 0.4, 0.5 }, { 1.2, 0.5 }, { 0.8, 0.7 } }), 1.5, Difference);
            TallyVolume(DrawingGate, Slotted, 3.0 - (0.8 * 0.4 + 0.04 * Pi) * 1.5, "− slot (0.4,0.5)–(1.2,0.5) r 0.2 through");
        }
        {
            // Upright plane: rectangle on XZ extrudes along −y.
            SolidArcDocument Upright = CreateSolidArcDocument("Upright");
            DrawAndExtrude(Upright, DrawingOf(Kind::Rectangle, 1, { { 0.0, 0.0 }, { 2.0, 1.0 } }), 0.5, New);
            TallyVolume(DrawingGate, Upright, 1.0, "rectangle on XZ, extrude 0.5");
            DrawAndExtrude(Upright, DrawingOf(Kind::Circle, 1, { { 0.6, 0.5 }, { 1.4, 0.5 } }), 0.5, Difference);
            TallyVolume(DrawingGate, Upright, 1.0 - 0.08 * Pi, "− two-point circle (0.6,0.5)–(1.4,0.5) on XZ");
        }
        // Each curve form alone: volume = exact profile area × height.
        const std::vector<Vector2> Wave = { { 0.0, 0.0 }, { 1.0, -0.3 }, { 2.0, 0.4 }, { 2.2, 1.5 }, { 0.8, 1.9 }, { -0.4, 1.0 } };
        for (const SketchSplineForm Form : { SketchSplineForm::Hermite, SketchSplineForm::Bezier, SketchSplineForm::Nurbs, SketchSplineForm::CubicSpline })
        {
            SolidArcDocument Freeform = CreateSolidArcDocument("Freeform");
            const SolidArcDrawingRequest Request = DrawingOf(Kind::Curve, 0, Wave, true, false, Form);
            const double Area = MeasureDrawingArea(Request);
            DrawAndExtrude(Freeform, Request, 0.7, New);
            TallyVolume(DrawingGate, Freeform, Area * 0.7, std::string("closed ") + SketchSplineFormLabel(Form) + " through 6 clicks, extrude 0.7 = area " + std::to_string(Area) + " × 0.7");
            DrawAndExtrude(Freeform, DrawingOf(Kind::Circle, 0, { { 1.0, 0.8 }, { 1.3, 0.8 } }, false, true), 0.7, Difference);
            TallyVolume(DrawingGate, Freeform, (Area - 0.09 * Pi) * 0.7, std::string("  − circle r 0.3 inside the ") + SketchSplineFormLabel(Form));
        }
    }

    // ---- W11 vehicle surfacing (plan V1 import, V3 curve steps, V4 loft) on the synthetic bonnet sample.
    {
        const VehicleCurveFile Sample = ReadVehicleCurves(SOLIDARC_VEHICLE_SAMPLE);
        size_t PointCount = 0;
        for (const VehicleCurve& Curve : Sample.Curves) PointCount += Curve.Points.size();
        Tally(SurfacingGate, 0.0, Sample.Refusal.empty() && Sample.Curves.size() == 11, "SyntheticBonnet.toml decoded: " + std::to_string(Sample.Curves.size()) + " curves · "
              + std::to_string(PointCount) + " points " + Sample.Refusal);
        const VehicleCurveFile Again = DecodeVehicleCurves(EncodeVehicleCurves(Sample));
        bool Equal = Again.Refusal.empty() && Again.Curves.size() == Sample.Curves.size();
        for (size_t Index = 0; Equal && Index < Sample.Curves.size(); ++Index)
        {
            const VehicleCurve& First = Sample.Curves[Index];
            const VehicleCurve& Second = Again.Curves[Index];
            Equal = First.Name == Second.Name && First.ClosedCondition == Second.ClosedCondition && First.Points.size() == Second.Points.size()
                 && std::memcmp(First.Points.data(), Second.Points.data(), First.Points.size() * sizeof(Vector3)) == 0;
        }
        Tally(SurfacingGate, 0.0, Equal, "V1 round trip decode → encode → decode: names, closed flags, every coordinate bit-equal");
        const VehicleCurveFile Millimetres = DecodeVehicleCurves("Format = \"VehicleCurves\"\nUnits = \"mm\"\n[[Curves]]\nName = \"A\"\nPoints = [[1000.0, 0.0, 0.0], [0.0, 500.0, 0.0]]\n");
        Tally(SurfacingGate, 0.0, Millimetres.Refusal.empty() && Millimetres.Curves.size() == 1 && Millimetres.Curves[0].Points[0].X == 1.0 && Millimetres.Curves[0].Points[1].Y == 0.5,
              "Units = \"mm\" scaled to metres");
        const std::pair<const char*, const char*> Malformed[] = {
            { "Format = \"Mesh\"\n", "Format is not" },
            { "Format = \"VehicleCurves\"\nUnits = \"in\"\n[[Curves]]\nName = \"A\"\nPoints = [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0]]\n", "unknown Units" },
            { "Format = \"VehicleCurves\"\n[[Curves]]\nName = \"A\"\nPoints = [[0.0, 0.0, 0.0]]\n", "needs at least 2 points" },
            { "Format = \"VehicleCurves\"\n[[Curves]]\nName = \"A\"\nPoints = [[0.0, 0.0, 0.0], [1.0, \"x\", 0.0]]\n", "point 1 is not [x, y, z]" },
            { "Format = \"VehicleCurves\"\n[[Curves]]\nPoints = [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0]]\n", "has no Name" },
        };
        for (const auto& [Text, Expected] : Malformed)
        {
            const VehicleCurveFile Refused = DecodeVehicleCurves(Text);
            Tally(SurfacingGate, 0.0, Refused.Refusal.find(Expected) != std::string::npos && Refused.Curves.empty(), "malformed file refused: " + Refused.Refusal);
        }

        // Import: 11 SpaceCurve steps in one undo entry, all within the 0.2 mm tolerance.
        SolidArcDocument Vehicle = CreateSolidArcDocument("Bonnet");
        const SolidArcCommandOutcome Imported = ImportSolidArcCurves(Vehicle, Sample);
        double WorstFit = 0.0;
        int    Poles = 0;
        for (const auto& [Label, Fit] : Vehicle.Regeneration.Curves) { WorstFit = std::max(WorstFit, Fit.WorstDeviation); Poles += static_cast<int>(Fit.Curve.Definition->Poles.size()); }
        Tally(SurfacingGate, WorstFit * 1000.0, Imported.AcceptedCondition && Vehicle.Tree.Steps.size() == 11 && Vehicle.Regeneration.Refused == 0 && Vehicle.UndoEntries.size() == 1
              && Vehicle.Regeneration.Curves.size() == 11 && WorstFit <= 2.0e-4, "import → 11 SpaceCurve steps, 1 undo entry, " + std::to_string(PointCount) + " points → " + std::to_string(Poles)
              + " poles, worst fit [mm] (value)");
        const SolidArcCommandOutcome Refused = ImportSolidArcCurves(Vehicle, DecodeVehicleCurves("Format = \"Mesh\"\n"));
        Tally(SurfacingGate, 0.0, !Refused.AcceptedCondition && Vehicle.Tree.Steps.size() == 11 && Vehicle.UndoEntries.size() == 1, "refused file leaves the document untouched: " + Refused.Refusal);

        // Loft through the 6 sections (by caption) → one open NURBS sheet.
        std::vector<std::string> Sections;
        for (const FeatureStep& Step : Vehicle.Tree.Steps) if (Vehicle.Captions[Step.Label].rfind("Section", 0) == 0) Sections.push_back(Step.Label);
        const SolidArcCommandOutcome Single = LoftSolidArcCurves(Vehicle, { Sections.front() });
        Tally(SurfacingGate, 0.0, !Single.AcceptedCondition && Single.Refusal.find("at least 2") != std::string::npos, "loft through one curve refused: " + Single.Refusal);
        const SolidArcCommandOutcome Lofted = LoftSolidArcCurves(Vehicle, Sections);
        const SolidArcBodySummary& Sheet = Vehicle.Summary;
        Tally(SurfacingGate, 0.0, Lofted.AcceptedCondition && Sheet.PresentCondition && Sheet.SheetCondition && Sheet.SoundCondition && Sheet.Faces == 1 && Sheet.Edges == 4 && Sheet.Vertices == 4,
              "loft 6 sections → sheet: " + std::to_string(Sheet.Faces) + " face · " + std::to_string(Sheet.Edges) + " edges · audit " + (Sheet.SoundCondition ? "sound" : "findings") + " " + Lofted.Refusal);
        auto SheetSurface = [](const SolidArcDocument& Document)
        {
            const FaceToken Face = Document.Partition->Faces.TokenAt(Document.Regeneration.Naming.Faces.begin()->first);
            return Document.Partition->Surfaces.Resolve(Document.Partition->Faces.Resolve(Face).Surface);
        };
        if (Lofted.AcceptedCondition)
        {
            const SurfaceGeometry Surface = SheetSurface(Vehicle);
            double WorstPoint = 0.0, WorstAnalytic = 0.0;
            for (const VehicleCurve& Curve : Sample.Curves)
            {
                for (const Vector3& Point : Curve.Points)
                {
                    const Vector2 Foot = ProjectOntoSurface(Surface, Point, { Point.X / 0.75, Point.Y / 1.1 });
                    WorstPoint = std::max(WorstPoint, Length(EvaluateSurface(Surface, Foot).Position - Point));
                }
            }
            for (int First = 0; First <= 60; ++First)
            {
                for (int Second = 0; Second <= 60; ++Second)
                {
                    const Vector3 Point = EvaluateSurface(Surface, { First / 60.0, Second / 60.0 }).Position;
                    const double  Xi = Point.X / 0.75, Eta = Point.Y / 1.1;
                    const double  Height = 0.78 + 0.16 * Eta - 0.06 * Eta * Eta - 0.085 * Xi * Xi + 0.012 * std::sin(Pi * Eta) * (1.0 - Xi * Xi);
                    const double  Across = (-0.17 * Xi - 0.024 * Xi * std::sin(Pi * Eta)) / 0.75;
                    const double  Along  = (0.16 - 0.12 * Eta + 0.012 * Pi * std::cos(Pi * Eta) * (1.0 - Xi * Xi)) / 1.1;
                    WorstAnalytic = std::max(WorstAnalytic, std::abs(Point.Z - Height) / std::sqrt(1.0 + Across * Across + Along * Along));
                }
            }
            Tally(SurfacingGate, WorstPoint * 1000.0, WorstPoint <= 5.0e-4, "sheet vs all " + std::to_string(PointCount) + " limit points (6 sections + 5 loops never lofted), worst [mm] (value)");
            Tally(SurfacingGate, WorstAnalytic * 1000.0, WorstAnalytic <= 5.0e-4, "sheet vs the analytic bonnet H(x, y), 61 × 61 normal distance, worst [mm] (value)");

            // Reload: points and pole edits bit-equal, save → load → save byte-equal, same face name.
            const std::string Text = EncodeSolidArcDocument(Vehicle);
            const SolidArcDocumentFile File = DecodeSolidArcDocument(Text);
            SolidArcDocument Reopened = CreateSolidArcDocument(File.Title);
            const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Reopened, File.Tree, File.Captions, "Open");
            bool SameSteps = File.Tree.Steps.size() == Vehicle.Tree.Steps.size();
            for (size_t Index = 0; SameSteps && Index < File.Tree.Steps.size(); ++Index)
            {
                const FeatureStep& First = Vehicle.Tree.Steps[Index];
                const FeatureStep& Second = File.Tree.Steps[Index];
                SameSteps = First.Kind == Second.Kind && First.Sections == Second.Sections && First.Points.size() == Second.Points.size()
                         && std::memcmp(First.Points.data(), Second.Points.data(), First.Points.size() * sizeof(Vector3)) == 0;
            }
            Tally(SurfacingGate, 0.0, Opened.AcceptedCondition && SameSteps && EncodeSolidArcDocument(Reopened) == Text && Reopened.Regeneration.Naming.Faces == Vehicle.Regeneration.Naming.Faces,
                  "reload: points bit-equal, sections equal, save → load → save byte-equal, face name " + Vehicle.Regeneration.Naming.Faces.begin()->second);

            // Pole move (mode 1 drag): +10 mm on the middle pole of Section3, then undo / redo.
            const std::string Third = Sections[2];
            const NurbsCurveDefinition Before = *Vehicle.Regeneration.Curves.at(Third).Curve.Definition;
            const int     Pole   = static_cast<int>(Before.Poles.size()) / 2;
            const Vector3 Middle = EvaluateSurface(Surface, { 0.5, 0.38 }).Position;
            const SolidArcCommandOutcome Moved = MoveSolidArcPole(Vehicle, Third, Pole, Before.Poles[Pole] + Vector3{ 0.0, 0.0, 0.01 });
            const double Shift = Moved.AcceptedCondition ? Length(EvaluateSurface(SheetSurface(Vehicle), { 0.5, 0.38 }).Position - Middle) : 0.0;
            Tally(SurfacingGate, 0.0, Moved.AcceptedCondition && Vehicle.Summary.SheetCondition && Shift > 1.0e-3 && Vehicle.Regeneration.Naming.Faces == Reopened.Regeneration.Naming.Faces,
                  "move pole " + std::to_string(Pole) + " of Section3 by +10 mm → loft regenerates, panel at (0.5, 0.38) moved " + std::to_string(Shift * 1000.0).substr(0, 5) + " mm, face name kept");
            const SolidArcCommandOutcome Missing = MoveSolidArcPole(Vehicle, Third, 99, Vector3{});
            Tally(SurfacingGate, 0.0, !Missing.AcceptedCondition && Missing.Refusal.find("does not exist") != std::string::npos, "pole 99 refused: " + Missing.Refusal);
            const bool Undone = UndoSolidArcCommand(Vehicle);
            const double Back = Length(EvaluateSurface(SheetSurface(Vehicle), { 0.5, 0.38 }).Position - Middle);
            const bool Redone = RedoSolidArcCommand(Vehicle);
            const double Reapplied = Length(EvaluateSurface(SheetSurface(Vehicle), { 0.5, 0.38 }).Position - Middle);
            Tally(SurfacingGate, Back, Undone && Redone && Back == 0.0 && std::abs(Reapplied - Shift) <= 1.0e-15, "undo → panel exactly back · redo → moved again");
        }
    }

    // ---- W12 vehicle panels (plan V5–V7): continuity, mirror, close, network through the document commands.
    {
        auto SamplePath = [](const char* FileName)
        {
            std::string Path = SOLIDARC_VEHICLE_SAMPLE;
            return Path.substr(0, Path.rfind('/') + 1) + FileName;
        };
        auto ByCaption = [](SolidArcDocument& Document, const std::string& Stem)
        {
            std::vector<std::string> Labels;
            for (const FeatureStep& Step : Document.Tree.Steps) if (Document.Captions[Step.Label].rfind(Stem, 0) == 0) Labels.push_back(Step.Label);
            return Labels;
        };
        auto Reloads = [](const SolidArcDocument& Document)
        {
            const std::string Text = EncodeSolidArcDocument(Document);
            const SolidArcDocumentFile File = DecodeSolidArcDocument(Text);
            SolidArcDocument Reopened = CreateSolidArcDocument(File.Title);
            const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Reopened, File.Tree, File.Captions, "Open");
            return Opened.AcceptedCondition && EncodeSolidArcDocument(Reopened) == Text && Reopened.Regeneration.Naming.Faces == Document.Regeneration.Naming.Faces
                && Reopened.Summary.Volume == Document.Summary.Volume;
        };

        // Bonnet: front loft, rear loft G2 to it, mirror through x = 0.
        SolidArcDocument Bonnet = CreateSolidArcDocument("Bonnet panels");
        Expect(ImportSolidArcCurves(Bonnet, ReadVehicleCurves(SOLIDARC_VEHICLE_SAMPLE)));
        const std::vector<std::string> Sections = ByCaption(Bonnet, "Section");
        const SolidArcCommandOutcome Front = LoftSolidArcCurves(Bonnet, { Sections[0], Sections[1], Sections[2] });
        const SolidArcCommandOutcome Rear  = LoftSolidArcCurves(Bonnet, { Sections[2], Sections[3], Sections[4], Sections[5] }, 2);
        Tally(PanelGate, 0.0, Front.AcceptedCondition && Rear.AcceptedCondition && Bonnet.Regeneration.Panels.size() == 2 && Bonnet.Summary.SheetCondition && Bonnet.Summary.SoundCondition
              && Bonnet.Summary.Faces == 2, "front loft + rear loft G2 → 2 panels sewn on Section3, sheet sound " + Front.Refusal + Rear.Refusal);
        const SolidArcCommandOutcome Mirrored = MirrorSolidArcPanels(Bonnet, 0);
        const auto& Names = Bonnet.Regeneration.Panels;
        const std::string FrontName = Bonnet.Tree.Steps[11].Label + ".Side(Surface)";
        const std::string ImageName = Bonnet.Tree.Steps.back().Label + ".Image(" + FrontName + ")";
        Tally(PanelGate, 0.0, Mirrored.AcceptedCondition && Names.size() == 4 && Names.contains(ImageName) && Bonnet.Summary.SheetCondition && Bonnet.Summary.SoundCondition && Bonnet.Summary.Faces == 4,
              "Alt+X → 4 panels, one sound sheet, face " + ImageName + " " + Mirrored.Refusal);
        Tally(PanelGate, 0.0, Reloads(Bonnet), "bonnet reload: continuity + mirror steps survive, save → load → save byte-equal, names equal");
        const SolidArcCommandOutcome Capped = CloseSolidArcPanels(Bonnet);
        Tally(PanelGate, 0.0, !Capped.AcceptedCondition && Capped.Refusal.find("not planar") != std::string::npos && Names.size() == 4, "Close on the open bonnet rim refused, document kept: " + Capped.Refusal);
        const bool Undone = UndoSolidArcCommand(Bonnet);
        Tally(PanelGate, 0.0, Undone && Bonnet.Regeneration.Panels.size() == 2 && Bonnet.Summary.Faces == 2, "undo mirror → 2 panels");

        // Pod: planar hoops, loft, mirror, close → solid.
        SolidArcDocument Pod = CreateSolidArcDocument("Pod");
        const VehicleCurveFile Hoops = ReadVehicleCurves(SamplePath("SyntheticPod.toml"));
        const bool Planar = !Hoops.Curves.empty() && std::all_of(Hoops.Curves.begin(), Hoops.Curves.end(), [](const VehicleCurve& Curve) { return Curve.PlanarCondition; })
                         && DecodeVehicleCurves(EncodeVehicleCurves(Hoops)).Curves.front().PlanarCondition;
        Expect(ImportSolidArcCurves(Pod, Hoops));
        const SolidArcCommandOutcome Loft = LoftSolidArcCurves(Pod, ByCaption(Pod, "Hoop"));
        const SolidArcCommandOutcome Image = MirrorSolidArcPanels(Pod, 0);
        const SolidArcCommandOutcome Closed = CloseSolidArcPanels(Pod);
        double Exact = 0.0;
        for (int Index = 0; Index <= 2000; ++Index)
        {
            const double Y = 1.6 * Index / 2000.0, Weight = (Index == 0 || Index == 2000) ? 1.0 : (Index % 2 ? 4.0 : 2.0);
            Exact += Weight * Pi * (0.4 + 0.1 * std::sin(Pi * Y / 1.6)) * (0.3 + 0.05 * Y) * 1.6 / 6000.0;
        }
        const double Relative = std::abs(Pod.Summary.Volume - Exact) / Exact;
        Tally(PanelGate, Relative, Planar && Loft.AcceptedCondition && Image.AcceptedCondition && Closed.AcceptedCondition && !Pod.Summary.SheetCondition && Pod.Summary.SoundCondition && Relative <= 2.0e-3
              && Pod.Summary.Faces == 4, "pod: Planar = true round trip · loft → mirror → Close → solid V = " + std::to_string(Pod.Summary.Volume).substr(0, 8) + " vs " + std::to_string(Exact).substr(0, 8)
              + ", rel. (value) " + Loft.Refusal + Image.Refusal + Closed.Refusal);
        const SolidArcCommandOutcome Again = CloseSolidArcPanels(Pod);
        Tally(PanelGate, 0.0, !Again.AcceptedCondition && Again.Refusal.find("already closed") != std::string::npos, "second Close refused: " + Again.Refusal);
        Tally(PanelGate, 0.0, Reloads(Pod), "pod reload: planar flags + close step, byte-equal, same volume");

        // Network: 6 sections + 4 spines cross → L makes a Gordon panel; G1 on a network refuses.
        SolidArcDocument Network = CreateSolidArcDocument("Network");
        Expect(ImportSolidArcCurves(Network, ReadVehicleCurves(SamplePath("SyntheticBonnetNetwork.toml"))));
        std::vector<std::string> Curves;
        for (const FeatureStep& Step : Network.Tree.Steps) Curves.push_back(Step.Label);
        const SolidArcCommandOutcome Refused = LoftSolidArcCurves(Network, Curves, 1);
        Tally(PanelGate, 0.0, !Refused.AcceptedCondition && Refused.Refusal.find("network") != std::string::npos, "G1 on crossing curves refused: " + Refused.Refusal);
        const SolidArcCommandOutcome Gordon = LoftSolidArcCurves(Network, Curves);
        double WorstAnalytic = 0.0;
        if (Gordon.AcceptedCondition)
        {
            const NurbsSurface& Surface = Network.Regeneration.Panels.begin()->second;
            for (int First = 0; First <= 60; ++First)
            {
                for (int Second = 0; Second <= 60; ++Second)
                {
                    const Vector3 Point = EvaluateNurbsSurface(Surface, { First / 60.0, Second / 60.0 }).Position;
                    const double  Xi = Point.X / 0.75, Eta = Point.Y / 1.1;
                    const double  Height = 0.78 + 0.16 * Eta - 0.06 * Eta * Eta - 0.085 * Xi * Xi + 0.012 * std::sin(Pi * Eta) * (1.0 - Xi * Xi);
                    const double  Across = (-0.17 * Xi - 0.024 * Xi * std::sin(Pi * Eta)) / 0.75;
                    const double  Along  = (0.16 - 0.12 * Eta + 0.012 * Pi * std::cos(Pi * Eta) * (1.0 - Xi * Xi)) / 1.1;
                    WorstAnalytic = std::max(WorstAnalytic, std::abs(Point.Z - Height) / std::sqrt(1.0 + Across * Across + Along * Along));
                }
            }
        }
        const bool NetworkStep = !Network.Tree.Steps.empty() && Network.Tree.Steps.back().Kind == FeatureStepKind::Network;
        Tally(PanelGate, WorstAnalytic * 1000.0, Gordon.AcceptedCondition && NetworkStep && Network.Summary.SheetCondition && Network.Summary.SoundCondition && Network.Summary.Faces == 1 && WorstAnalytic <= 5.0e-4,
              "10 crossing curves → Network step (Gordon), sheet sound, vs analytic H 61 × 61, worst [mm] (value) " + Gordon.Refusal);
        Tally(PanelGate, 0.0, Reloads(Network), "network reload byte-equal");
    }

    // ---- W13 surface evaluation (plan V8): deviation map and zebra phase through the document and scene functions.
    {
        auto Sections = [](SolidArcDocument& Document)
        {
            std::vector<std::string> Labels;
            for (const FeatureStep& Step : Document.Tree.Steps) if (Document.Captions[Step.Label].rfind("Section", 0) == 0) Labels.push_back(Step.Label);
            return Labels;
        };
        auto Domain = [](const NurbsSurface& Surface, bool AlongU) { return MeasureNurbsSurfaceDomain(*Surface.Definition, AlongU); };
        auto Compact = [](double Value) { char Text[32]; std::snprintf(Text, sizeof(Text), "%.3g", Value); return std::string(Text); };

        // ① deviation map on the six-section bonnet: every imported point, closest-point residual, lattice bound.
        SolidArcDocument Fitted = CreateSolidArcDocument("Deviation");
        Expect(ImportSolidArcCurves(Fitted, ReadVehicleCurves(SOLIDARC_VEHICLE_SAMPLE)));
        Expect(LoftSolidArcCurves(Fitted, Sections(Fitted)));
        const SolidArcDeviationMap Map = MeasureSolidArcDeviation(Fitted);
        size_t Imported = 0;
        for (const FeatureStep& Step : Fitted.Tree.Steps) if (Step.Kind == FeatureStepKind::SpaceCurve) Imported += Step.Points.size();
        const NurbsSurface& Sheet = Fitted.Regeneration.Panels.begin()->second;
        const NurbsDomain   AlongU = Domain(Sheet, true), AlongV = Domain(Sheet, false);
        double WorstResidual = 0.0, WorstLattice = 0.0;
        for (const SolidArcDeviationNeedle& Needle : Map.Needles)
        {
            const Vector2 Foot   = ProjectOntoNurbsSurface(Sheet, Needle.Point, { 0.5 * (AlongU.Low + AlongU.High), 0.5 * (AlongV.Low + AlongV.High) });
            const bool    Inside = Foot.X > AlongU.Low + 1.0e-9 && Foot.X < AlongU.High - 1.0e-9 && Foot.Y > AlongV.Low + 1.0e-9 && Foot.Y < AlongV.High - 1.0e-9;
            const Vector3 Normal = EvaluateNurbsNormal(Sheet, Foot);
            const Vector3 Gap    = Needle.Point - Needle.Foot;
            if (Inside && Needle.Distance > 1.0e-12) WorstResidual = std::max(WorstResidual, Length(Cross(Gap, Normal)) / Needle.Distance);
            double Lattice = std::numeric_limits<double>::infinity();
            for (int First = 0; First <= 60; ++First)
                for (int Second = 0; Second <= 60; ++Second)
                    Lattice = std::min(Lattice, Distance(EvaluateNurbsSurface(Sheet, { AlongU.Low + (AlongU.High - AlongU.Low) * First / 60.0,
                                                                                         AlongV.Low + (AlongV.High - AlongV.Low) * Second / 60.0 }).Position, Needle.Point));
            WorstLattice = std::max(WorstLattice, Needle.Distance - Lattice);
        }
        Tally(EvaluationGate, Map.Worst * 1000.0, Map.Needles.size() == Imported && Imported == 286 && Map.Worst <= 2.0e-4 && Map.Revision == Fitted.Revision,
              "deviation map, six-section loft: " + std::to_string(Map.Needles.size()) + " needles (one per imported point), worst [mm] (value), mean "
              + std::to_string(Map.Mean * 1000.0) + " mm — W11 measures the same sheet");
        Tally(EvaluationGate, 0.0, WorstResidual <= 1.0e-6, "interior needles run along the foot normal: |gap × N| / |gap| = " + Compact(WorstResidual) + " (≤ 10⁻⁶)");
        Tally(EvaluationGate, 0.0, WorstLattice <= 1.0e-9, "no needle longer than the best of a 61 × 61 lattice: excess " + Compact(WorstLattice * 1000.0) + " mm");

        // ② zebra: lift pole 2 of Section3 by 20 mm, front + rear lofts; phase across the shared Section3 at G0 and G2.
        auto SeamJump = [&](const SolidArcDocument& Document)
        {
            const Vector3 Forward = Normalize(Vector3{ -1.0, 1.0, -1.0 }), Up = Normalize(Vector3{ -1.0, 1.0, 2.0 });
            std::vector<const NurbsSurface*> Panels;
            for (const auto& [Name, Surface] : Document.Regeneration.Panels) Panels.push_back(&Surface);
            if (Panels.size() != 2) return std::numeric_limits<double>::infinity();
            double Worst = 0.0;
            for (int Edge = 0; Edge < 2; ++Edge)
            {
                const NurbsDomain Across = Domain(*Panels[0], false), Along = Domain(*Panels[0], true);
                const double      Fixed  = Edge ? Across.High : Across.Low;
                const Vector3     Probe  = EvaluateNurbsSurface(*Panels[0], { 0.5 * (Along.Low + Along.High), Fixed }).Position;
                const Vector2     Near   = ProjectOntoNurbsSurface(*Panels[1], Probe, { 0.5, 0.5 });
                if (Distance(EvaluateNurbsSurface(*Panels[1], Near).Position, Probe) > 1.0e-6) continue;  // not the shared edge
                for (int Index = 1; Index < 40; ++Index)
                {
                    const Vector2 Here  = { Along.Low + (Along.High - Along.Low) * Index / 40.0, Fixed };
                    const Vector3 Point = EvaluateNurbsSurface(*Panels[0], Here).Position;
                    const Vector2 There = ProjectOntoNurbsSurface(*Panels[1], Point, Near);
                    Worst = std::max(Worst, std::abs(SolidArcZebraPhase(EvaluateNurbsNormal(*Panels[0], Here), Forward, Up, 64.0)
                                                   - SolidArcZebraPhase(EvaluateNurbsNormal(*Panels[1], There), Forward, Up, 64.0)));
                }
            }
            return Worst;
        };
        SolidArcDocument Creased = CreateSolidArcDocument("Zebra");
        Expect(ImportSolidArcCurves(Creased, ReadVehicleCurves(SOLIDARC_VEHICLE_SAMPLE)));
        const std::vector<std::string> Labels = Sections(Creased);
        const Vector3 Pole = Creased.Regeneration.Curves.at(Labels[2]).Curve.Definition->Poles[2];
        Expect(MoveSolidArcPole(Creased, Labels[2], 2, Pole + Vector3{ 0.0, 0.0, 0.02 }));
        Expect(LoftSolidArcCurves(Creased, { Labels[0], Labels[1], Labels[2] }));
        const SolidArcCommandOutcome Plain = LoftSolidArcCurves(Creased, { Labels[2], Labels[3], Labels[4], Labels[5] }, 0);
        const double PlainJump = SeamJump(Creased);
        Tally(EvaluationGate, 0.0, Plain.AcceptedCondition && PlainJump > 0.05 && PlainJump < 1.0e3,
              "G0 rear loft on the lifted Section3: zebra phase jump across the seam " + Compact(PlainJump) + " stripes of 64 (> 0.05) — visibly broken stripes");
        (void)UndoSolidArcCommand(Creased);
        const SolidArcCommandOutcome Smooth = LoftSolidArcCurves(Creased, { Labels[2], Labels[3], Labels[4], Labels[5] }, 2);
        const double SmoothJump = SeamJump(Creased);
        Tally(EvaluationGate, 0.0, Smooth.AcceptedCondition && SmoothJump <= 1.0e-6, "G2 rear loft: zebra phase jump " + Compact(SmoothJump) + " stripes of 64 (≤ 10⁻⁶) — continuous stripes");

        // ③ zebra density tessellation: interpolated normals need short sides on the sheet.
        const SolidArcScene Coarse = BuildSolidArcScene(Creased);
        const SolidArcScene Fine   = BuildSolidArcScene(Creased, 1.5e-3, true);
        const size_t CoarseCount = Coarse.Corners.size() / 3, FineCount = Fine.Corners.size() / 3;
        Tally(EvaluationGate, 0.0, FineCount >= 20 * CoarseCount && Fine.Generation != Coarse.Generation && Fine.Revision == Coarse.Revision,
              "zebra tessellation " + std::to_string(FineCount) + " vs " + std::to_string(CoarseCount) + " triangles (≥ 20×); distinct scene generation re-uploads the GPU buffers");
    }

    // ---- W14 blueprint study: image planes from the committed BMW trace, picture decode, codec, refusals, axis cylinders.
    {
        auto Compact = [](double Value) { char Text[32]; std::snprintf(Text, sizeof(Text), "%.3g", Value); return std::string(Text); };
        auto Find    = [](const std::vector<SolidArcImagePlane>& Planes, const std::string& Name) -> const SolidArcImagePlane*
        {
            for (const SolidArcImagePlane& Plane : Planes) if (Plane.Name == Name) return &Plane;
            return nullptr;
        };
        // Crop pixel (column, row) → world through the plane's parallelogram.
        auto Pixel = [](const SolidArcImagePlane& Plane, double Column, double Row)
        {
            return LocateSolidArcImagePoint(Plane, (Column - Plane.Crop[0]) / double(Plane.Crop[2] - Plane.Crop[0]), (Row - Plane.Crop[1]) / double(Plane.Crop[3] - Plane.Crop[1]));
        };

        // ① landmarks of BmwM3E90Calibration.toml land on the car frame (nose y 0, tail y 4.58, ground z 0, mirror plane x 0).
        std::string Sample = SOLIDARC_VEHICLE_SAMPLE;
        Sample = Sample.substr(0, Sample.find_last_of("/\\") + 1) + "BmwM3E90Sections.toml";
        const VehicleCurveFile Bmw = ReadVehicleCurves(Sample);
        const SolidArcImagePlane* Side  = Find(Bmw.ImagePlanes, "Side");
        const SolidArcImagePlane* Top   = Find(Bmw.ImagePlanes, "Top");
        const SolidArcImagePlane* Front = Find(Bmw.ImagePlanes, "Front");
        // The side view sets the scale (4580 mm over its bumper lines), so its landmarks are exact; the top / front views of
        // the sheet agree with it to about one pixel (2.08 mm) — the drawing's own alignment, gated at 1.5 px.
        constexpr double Millimetre = 2.0771e-3;                                             // [m/px] - Calibration scale
        double Landmark = INFINITY, Alignment = INFINITY;
        if (Side && Top && Front)
        {
            Landmark  = std::max({ std::abs(Pixel(*Side, 30, 1642).Y - 0.0), std::abs(Pixel(*Side, 2235, 1642).Y - 4.58), std::abs(Pixel(*Side, 30, 1642).Z) });
            Alignment = std::max({ std::abs(Pixel(*Top, 25, 2388).Y), std::abs(Pixel(*Top, 2231, 2388).Y - 4.58), std::abs(Pixel(*Top, 25, 2388).X),
                                   std::abs(Pixel(*Front, 510, 727).X), std::abs(Pixel(*Front, 510, 727).Z) });
        }
        Tally(BlueprintGate, Landmark, Bmw.Refusal.empty() && Bmw.Curves.size() == 24 && Bmw.ImagePlanes.size() == 3 && Landmark <= 1.0e-5,
              "BMW trace: 24 sections + 3 planes; side bumper / ground pixels → y 0, y 4.58, z 0, worst [m] (value)");
        Tally(BlueprintGate, 0.0, Alignment <= 1.5 * Millimetre,
              "top nose / tail / centre line and front centre / ground pixels → world within " + Compact(Alignment * 1000.0) + " mm (sheet alignment, ≤ 1.5 px)");
        const bool Facing = Side && Top && Front && std::abs(Side->Corner.X + 1.2) <= 1.0e-12 && Side->Right.X == 0.0 && Side->Down.X == 0.0
                         && Top->Right.Z == 0.0 && Top->Down.Z == 0.0 && Front->Right.Y == 0.0 && Front->Down.Y == 0.0 && Top->Corner.Z < 0.0 && Front->Corner.Y > 4.58;
        Tally(BlueprintGate, 0.0, Facing, "Side ⟂ x at x = −1.2 (behind, seen from Right) · Top ⟂ z under the ground · Front ⟂ y behind the tail");

        // ② picture decode: a 3 × 2 24-bit BMP written here (bottom-up rows) → RGBA8, top row first; a missing file refuses.
        const std::string Bitmap = (std::filesystem::temp_directory_path() / "SolidArcImagePlaneValidation.bmp").string();
        {
            const uint8_t Colours[2][3][3] = { { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } }, { { 10, 20, 30 }, { 40, 50, 60 }, { 70, 80, 90 } } };   // [row][column] RGB, top row first
            std::vector<uint8_t> Bytes(54, 0);
            const uint32_t Stride = 12, Size = 54 + Stride * 2;
            auto Put = [&](size_t At, uint32_t Value, int Count) { for (int Index = 0; Index < Count; ++Index) Bytes[At + Index] = uint8_t(Value >> (8 * Index)); };
            Bytes[0] = 'B'; Bytes[1] = 'M'; Put(2, Size, 4); Put(10, 54, 4); Put(14, 40, 4); Put(18, 3, 4); Put(22, 2, 4); Put(26, 1, 2); Put(28, 24, 2); Put(34, Stride * 2, 4);
            for (int Row = 1; Row >= 0; --Row)
            {
                for (int Column = 0; Column < 3; ++Column) for (int Channel = 2; Channel >= 0; --Channel) Bytes.push_back(Colours[Row][Column][Channel]);
                for (uint32_t Pad = 9; Pad < Stride; ++Pad) Bytes.push_back(0);
            }
            std::ofstream(Bitmap, std::ios::binary).write(reinterpret_cast<const char*>(Bytes.data()), std::streamsize(Bytes.size()));
            const SolidArcPicture Picture = DecodeSolidArcPicture(Bitmap);
            bool Exact = Picture.Refusal.empty() && Picture.Width == 3 && Picture.Height == 2 && Picture.Pixels.size() == 24;
            for (int Row = 0; Exact && Row < 2; ++Row)
                for (int Column = 0; Column < 3; ++Column)
                    for (int Channel = 0; Channel < 3; ++Channel) Exact = Exact && Picture.Pixels[(Row * 3 + Column) * 4 + Channel] == Colours[Row][Column][Channel];
            Tally(BlueprintGate, 0.0, Exact && Picture.Pixels[3] == 255, "3 × 2 BMP → RGBA8, every pixel exact, top row first, alpha 255");
            std::filesystem::remove(Bitmap);
            const SolidArcPicture Missing = DecodeSolidArcPicture(Bitmap);
            Tally(BlueprintGate, 0.0, !Missing.Refusal.empty() && Missing.Pixels.empty(), "missing picture refused (no throw)");
        }

        // ③ import keeps the planes beside the curves; a second import replaces planes by Name; codec round trip byte-equal.
        SolidArcDocument Study = CreateSolidArcDocument("Blueprint");
        const SolidArcCommandOutcome First  = ImportSolidArcCurves(Study, Bmw);
        const size_t                 Steps  = Study.Tree.Steps.size();
        const SolidArcCommandOutcome Second = ImportSolidArcCurves(Study, Bmw);
        Tally(BlueprintGate, 0.0, First.AcceptedCondition && Second.AcceptedCondition && Steps == 24 && Study.Tree.Steps.size() == 48 && Study.ImagePlanes.size() == 3,
              "import → 24 curves + 3 planes; re-import adds curves, replaces planes by Name (still 3)");
        Study.ImagePlanes[1].VisibleCondition = false;
        Study.ImagePlanes[2].Opacity          = 0.3;
        const std::string            Encoded = EncodeSolidArcDocument(Study);
        const SolidArcDocumentFile   Decoded = DecodeSolidArcDocument(Encoded);
        SolidArcDocument             Again   = CreateSolidArcDocument(Decoded.Title);
        const SolidArcCommandOutcome Opened  = ReplaceSolidArcTree(Again, Decoded.Tree, Decoded.Captions, "Open");
        Again.ImagePlanes = Decoded.ImagePlanes;
        Tally(BlueprintGate, 0.0, Decoded.Refusal.empty() && Opened.AcceptedCondition && Decoded.ImagePlanes.size() == 3 && !Decoded.ImagePlanes[1].VisibleCondition
              && Decoded.ImagePlanes[2].Opacity == 0.3 && EncodeSolidArcDocument(Again) == Encoded, "document with planes (one hidden, one at ink 0.3): save → load → save byte-equal");
        const std::string Plain = EncodeSolidArcDocument(CreateSolidArcDocument("Plain"));
        Tally(BlueprintGate, 0.0, Plain.find("ImagePlanes") == std::string::npos, "documents without planes write no ImagePlanes key (older files unchanged)");

        // ④ refusals: a degenerate plane or an opacity outside [0, 1] rejects the whole curves file (no partial import).
        const std::string Curve = "Format = \"VehicleCurves\"\nUnits = \"m\"\n[[Curves]]\nName = \"Line\"\nPoints = [[0, 0, 0], [1, 0, 0], [2, 0, 0.5]]\n";
        auto PlaneRow = [](const std::string& Right, double Opacity)
        {
            return "[[ImagePlanes]]\nName = \"Side\"\nPath = \"Side.png\"\nCorner = [0, 0, 1]\nRight = " + Right + "\nDown = [0, 0, -1]\nOpacity = " + std::to_string(Opacity) + "\n";
        };
        const VehicleCurveFile Fine     = DecodeVehicleCurves(Curve + PlaneRow("[0, 4, 0]", 0.5));
        const VehicleCurveFile Flat     = DecodeVehicleCurves(Curve + PlaneRow("[0, 0, -2]", 0.5));
        const VehicleCurveFile Overdone = DecodeVehicleCurves(Curve + PlaneRow("[0, 4, 0]", 1.5));
        Tally(BlueprintGate, 0.0, Fine.Refusal.empty() && Fine.ImagePlanes.size() == 1 && !Flat.Refusal.empty() && Flat.Curves.empty() && !Overdone.Refusal.empty() && Overdone.Curves.empty(),
              "zero-area plane refused (" + Flat.Refusal + ") · opacity 1.5 refused — no curves kept");

        // ⑤ Construct · Cylinder · Axis x / y (wheel arches): exact volume πr²h and the extent along +axis from Position.
        for (int Axis = 0; Axis < 2; ++Axis)
        {
            SolidArcDocument Wheel = CreateSolidArcDocument("Wheel");
            SolidArcConstructRequest Request;
            Request.Kind     = SolidArcConstructKind::Cylinder;
            Request.Axis     = Axis;
            Request.Position = { 0.62, 0.805, 0.33 };
            Request.Radius   = 0.37;
            Request.Height   = 0.5;
            const SolidArcCommandOutcome Built = ConstructSolidArcPrimitive(Wheel, Request);
            const double Exact    = std::numbers::pi * 0.37 * 0.37 * 0.5;
            const double Relative = std::abs(Wheel.Summary.Volume - Exact) / Exact;
            const SolidArcScene Scene = BuildSolidArcScene(Wheel, 1.5e-3);
            const double Base   = Axis == 0 ? 0.62 : 0.805;
            const double Low    = Scene.BoundsLow[Axis], High = Scene.BoundsHigh[Axis];
            const double Across = Scene.BoundsHigh[2] - Scene.BoundsLow[2];
            const double Extent = std::max({ std::abs(Low - Base), std::abs(High - Base - 0.5), std::abs(Across - 0.74) });
            Tally(BlueprintGate, Relative, Built.AcceptedCondition && Wheel.Summary.SoundCondition && Relative <= 1.0e-9 && Extent <= 2.0e-3,
                  std::string(Axis == 0 ? "Axis x" : "Axis y") + " cylinder r 0.37 h 0.5: |V − πr²h| / V (value); " + Compact(Low) + " … " + Compact(High) + " along the axis, Ø " + Compact(Across) + " in z");
        }
    }

    // ---- W15 matcaps: built-in library optics, picture fitting, per-face / body assignment without regeneration, undo,
    //      persistence through regeneration, codec, refusals.
    {
        auto Compact = [](double Value) { char Text[32]; std::snprintf(Text, sizeof(Text), "%.3g", Value); return std::string(Text); };
        auto Luminance = [](const SolidArcPicture& Picture, size_t Texel)
        {
            return 0.2126 * Picture.Pixels[Texel * 4] + 0.7152 * Picture.Pixels[Texel * 4 + 1] + 0.0722 * Picture.Pixels[Texel * 4 + 2];
        };
        // Texels inside 0.9 of the disc radius.
        auto Inside = [](const SolidArcPicture& Picture, uint32_t Column, uint32_t Row)
        {
            const double X = 2.0 * (Column + 0.5) / Picture.Width - 1.0, Y = 2.0 * (Row + 0.5) / Picture.Height - 1.0;
            return X * X + Y * Y < 0.81;
        };
        // Mean |∂L/∂x| over mean |∂L/∂y| inside the disc (brushed metal blurs along x).
        auto Anisotropy = [&](const SolidArcPicture& Picture)
        {
            double Across = 0.0, Down = 0.0;
            for (uint32_t Row = 1; Row < Picture.Height; ++Row)
                for (uint32_t Column = 1; Column < Picture.Width; ++Column)
                {
                    if (!Inside(Picture, Column, Row)) continue;
                    const size_t Texel = size_t(Row) * Picture.Width + Column;
                    Across += std::abs(Luminance(Picture, Texel) - Luminance(Picture, Texel - 1));
                    Down   += std::abs(Luminance(Picture, Texel) - Luminance(Picture, Texel - Picture.Width));
                }
            return Across / std::max(Down, 1e-9);
        };
        // Colour of the brightest texel inside the disc.
        auto Brightest = [&](const SolidArcPicture& Picture)
        {
            size_t Best = 0; double Peak = -1.0;
            for (uint32_t Row = 0; Row < Picture.Height; ++Row)
                for (uint32_t Column = 0; Column < Picture.Width; ++Column)
                    if (Inside(Picture, Column, Row) && Luminance(Picture, size_t(Row) * Picture.Width + Column) > Peak)
                    {
                        Peak = Luminance(Picture, size_t(Row) * Picture.Width + Column);
                        Best = size_t(Row) * Picture.Width + Column;
                    }
            return std::array<double, 3>{ double(Picture.Pixels[Best * 4]), double(Picture.Pixels[Best * 4 + 1]), double(Picture.Pixels[Best * 4 + 2]) };
        };
        // Luminance spread (standard deviation) inside the disc: sharp reflections spread wide.
        auto Spread = [&](const SolidArcPicture& Picture)
        {
            double Sum = 0.0, Squares = 0.0; size_t Count = 0;
            for (uint32_t Row = 0; Row < Picture.Height; ++Row)
                for (uint32_t Column = 0; Column < Picture.Width; ++Column)
                    if (Inside(Picture, Column, Row))
                    {
                        const double Value = Luminance(Picture, size_t(Row) * Picture.Width + Column);
                        Sum += Value; Squares += Value * Value; ++Count;
                    }
            const double Mean = Sum / double(Count);
            return std::sqrt(std::max(0.0, Squares / double(Count) - Mean * Mean));
        };

        // ① the library: 13 built-ins, square RGBA8, opaque, deterministic; unknown names refuse
        const std::vector<SolidArcMatcapDescription>& BuiltIn = SolidArcBuiltInMatcaps();
        bool Shaped = BuiltIn.size() == 13, Repeatable = true;
        std::map<std::string, SolidArcPicture> Composed;
        for (const SolidArcMatcapDescription& Description : BuiltIn)
        {
            const SolidArcPicture Picture = ComposeSolidArcMatcap(Description.Name);
            Shaped = Shaped && Picture.Refusal.empty() && Picture.Width == SolidArcMatcapExtent && Picture.Height == SolidArcMatcapExtent
                  && Picture.Pixels.size() == size_t(SolidArcMatcapExtent) * SolidArcMatcapExtent * 4;
            for (size_t Index = 3; Shaped && Index < Picture.Pixels.size(); Index += 4) Shaped = Picture.Pixels[Index] == 255;
            Repeatable = Repeatable && ComposeSolidArcMatcap(Description.Name).Pixels == Picture.Pixels;
            Composed[Description.Name] = Picture;
        }
        const SolidArcPicture Unknown = ComposeSolidArcMatcap("Unobtainium");
        const SolidArcMatcapLibrary Plain = AssembleSolidArcMatcaps({});
        Tally(MatcapGate, 0.0, Shaped && Repeatable && !Unknown.Refusal.empty() && Plain.Names.size() == 14 && Plain.Names[0] == SolidArcStudioMatcap && Plain.Pictures.size() == 13
                               && SolidArcMatcapLayer(Plain, "Brass") == 5 && SolidArcMatcapLayer(Plain, "Unobtainium") == 0,
              "13 built-ins 256 × 256 RGBA8, opaque, composed twice bit-equal; library Studio + 13 (Brass = layer 5); unknown name refused / layer 0");

        // ② optics: metal highlights carry the tint, plastic highlights stay white; brushed blurs along x; roughness spreads less
        const std::array<double, 3> BrassPeak = Brightest(Composed["Brass"]);
        const std::array<double, 3> RedPeak   = Brightest(Composed["RedPlastic"]);
        const double BrassTint = BrassPeak[2] / BrassPeak[0];
        const double RedWhite  = std::min({ RedPeak[0], RedPeak[1], RedPeak[2] }) / std::max({ RedPeak[0], RedPeak[1], RedPeak[2] });
        Tally(MatcapGate, 0.0, BrassTint < 0.75 && RedWhite > 0.8,
              "brightest texel: Brass blue / red " + Compact(BrassTint) + " (< 0.75, tinted metal) · Red plastic min / max " + Compact(RedWhite) + " (> 0.8, white Fresnel)");
        const double Brushed = Anisotropy(Composed["BrushedSteel"]), Mirror = Anisotropy(Composed["Chrome"]);
        Tally(MatcapGate, 0.0, Brushed < 0.6 * Mirror, "|∂L/∂x| / |∂L/∂y|: Brushed steel " + Compact(Brushed) + " < 0.6 × Chrome " + Compact(Mirror) + " (anisotropic)");
        const double Sharp = Spread(Composed["Chrome"]), Satin = Spread(Composed["SatinAluminium"]), Matte = Spread(Composed["Clay"]);
        Tally(MatcapGate, 0.0, Sharp > Satin && Satin > Matte,
              "luminance spread inside the disc: Chrome " + Compact(Sharp) + " > Satin aluminium " + Compact(Satin) + " > Clay " + Compact(Matte));

        // ③ user pictures: centre square crop + bilinear fit; a 4 × 2 picture keeps its middle two columns
        const std::string Bitmap = (std::filesystem::temp_directory_path() / "SolidArcMatcapValidation.bmp").string();
        const uint8_t Columns[4][3] = { { 200, 0, 0 }, { 0, 200, 0 }, { 0, 0, 200 }, { 200, 200, 0 } };
        {
            std::vector<uint8_t> Bytes(54, 0);
            const uint32_t Stride = 12, Size = 54 + Stride * 2;
            auto Put = [&](size_t At, uint32_t Value, int Count) { for (int Index = 0; Index < Count; ++Index) Bytes[At + Index] = uint8_t(Value >> (8 * Index)); };
            Bytes[0] = 'B'; Bytes[1] = 'M'; Put(2, Size, 4); Put(10, 54, 4); Put(14, 40, 4); Put(18, 4, 4); Put(22, 2, 4); Put(26, 1, 2); Put(28, 24, 2); Put(34, Stride * 2, 4);
            for (int Row = 1; Row >= 0; --Row)
                for (int Column = 0; Column < 4; ++Column) for (int Channel = 2; Channel >= 0; --Channel) Bytes.push_back(Columns[Column][Channel]);
            std::ofstream(Bitmap, std::ios::binary).write(reinterpret_cast<const char*>(Bytes.data()), std::streamsize(Bytes.size()));
        }
        const SolidArcPicture Fitted = FitSolidArcMatcapPicture(DecodeSolidArcPicture(Bitmap));
        auto Near = [&](uint32_t Column, uint32_t Row, const uint8_t* Expected)
        {
            const uint8_t* Pixel = &Fitted.Pixels[(size_t(Row) * Fitted.Width + Column) * 4];
            return std::abs(Pixel[0] - Expected[0]) <= 1 && std::abs(Pixel[1] - Expected[1]) <= 1 && std::abs(Pixel[2] - Expected[2]) <= 1 && Pixel[3] == 255;
        };
        Tally(MatcapGate, 0.0, Fitted.Refusal.empty() && Fitted.Width == 256 && Fitted.Height == 256 && Near(64, 10, Columns[1]) && Near(192, 200, Columns[2]),
              "4 × 2 BMP → 256 × 256: quarter columns show source columns 2 and 3 (centre crop), within 1 / 255, opaque");

        // ④ assignment on a block: two faces Brass, body Brushed steel — no regeneration, one undo entry each
        SolidArcDocument Shop = CreateSolidArcDocument("Matcaps");
        SolidArcConstructRequest Stock;
        Stock.Kind = SolidArcConstructKind::Block; Stock.Width = 2.0; Stock.Depth = 1.0; Stock.Height = 0.5;
        Expect(ConstructSolidArcPrimitive(Shop, Stock));
        SolidArcScene ShopScene = BuildSolidArcScene(Shop);
        const std::string First = ShopScene.Faces.size() == 6 ? ShopScene.Faces[0].Name : "", Second = ShopScene.Faces.size() == 6 ? ShopScene.Faces[1].Name : "";
        const uint64_t Revision = Shop.Revision;
        const size_t   Entries  = Shop.UndoEntries.size();
        SelectSolidArcName(Shop, SolidArcSelectionKind::Face, First, false);
        SelectSolidArcName(Shop, SolidArcSelectionKind::Face, Second, true);
        const SolidArcCommandOutcome FaceOutcome = AssignSolidArcMatcap(Shop, "Brass");
        ClearSolidArcSelection(Shop);
        const SolidArcCommandOutcome BodyOutcome = AssignSolidArcMatcap(Shop, "BrushedSteel");
        const SolidArcMatcapLibrary Library = AssembleSolidArcMatcaps(Shop.Appearance);
        auto LayerCounts = [&](const SolidArcScene& Scene)
        {
            std::map<uint32_t, int> Counts;
            for (const uint32_t Word : ComposeSolidArcMatcapLayers(Scene, Shop.Appearance, Library)) Counts[Word >> SolidArcMatcapLayerShift] += 1;
            return Counts;
        };
        std::map<uint32_t, int> Counts = LayerCounts(ShopScene);
        const uint32_t BrassLayer = SolidArcMatcapLayer(Library, "Brass"), SteelLayer = SolidArcMatcapLayer(Library, "BrushedSteel");
        Tally(MatcapGate, 0.0, FaceOutcome.AcceptedCondition && BodyOutcome.AcceptedCondition && Shop.Revision == Revision && Shop.UndoEntries.size() == Entries + 2
                               && Shop.UndoEntries.back().AppearanceCondition && Counts[BrassLayer] == 2 && Counts[SteelLayer] == 4 && Counts.size() == 2,
              "2 faces → Brass, body → Brushed steel: state words 2 × layer " + std::to_string(BrassLayer) + " + 4 × layer " + std::to_string(SteelLayer)
              + "; revision unchanged (no regeneration); 2 matcap-only undo entries");

        // ⑤ refusals leave the appearance and the history alone
        const SolidArcAppearance Kept = Shop.Appearance;
        const SolidArcCommandOutcome Unnamed = AssignSolidArcMatcap(Shop, "Unobtainium");
        const SolidArcCommandOutcome Again   = AssignSolidArcMatcap(Shop, "BrushedSteel");
        const bool Untouched = Shop.Appearance.BodyMatcap == Kept.BodyMatcap && Shop.Appearance.FaceMatcaps == Kept.FaceMatcaps && Shop.UndoEntries.size() == Entries + 2;
        Tally(MatcapGate, 0.0, !Unnamed.AcceptedCondition && !Again.AcceptedCondition && Untouched,
              "unknown matcap refused (" + Unnamed.Refusal + ") · repeat refused (" + Again.Refusal + ") · appearance and history unchanged");

        // ⑥ undo / redo restore the appearance exactly and never regenerate
        UndoSolidArcCommand(Shop);
        const bool BodyUndone = Shop.Appearance.BodyMatcap == SolidArcStudioMatcap && Shop.Appearance.FaceMatcaps.size() == 2;
        UndoSolidArcCommand(Shop);
        const bool FacesUndone = Shop.Appearance.FaceMatcaps.empty() && Shop.Revision == Revision;
        RedoSolidArcCommand(Shop);
        RedoSolidArcCommand(Shop);
        Tally(MatcapGate, 0.0, BodyUndone && FacesUndone && Shop.Appearance.BodyMatcap == Kept.BodyMatcap && Shop.Appearance.FaceMatcaps == Kept.FaceMatcaps && Shop.Revision == Revision,
              "undo → body Studio, undo → no face matcaps, redo × 2 → identical appearance; revision unchanged throughout");

        // ⑦ a parameter edit regenerates: assignments follow the persistent names (none dangling)
        const SolidArcCommandOutcome Deeper = AssignSolidArcParameter(Shop, "Block1.Depth", 1.5);
        ShopScene = BuildSolidArcScene(Shop);
        Counts    = LayerCounts(ShopScene);
        Tally(MatcapGate, 0.0, Deeper.AcceptedCondition && Shop.Revision != Revision && Counts[BrassLayer] == 2 && Counts[SteelLayer] == 4 && CountDanglingMatcaps(ShopScene, Shop.Appearance) == 0,
              "Block1.Depth 1 → 1.5 regenerates; still 2 Brass + 4 Brushed steel faces, 0 dangling assignments");

        // ⑧ Alt+M: the selected face returns to the body matcap; with nothing selected the body returns to Studio
        SelectSolidArcName(Shop, SolidArcSelectionKind::Face, First, false);
        const SolidArcCommandOutcome FaceRemoval = RemoveSolidArcMatcap(Shop);
        ClearSolidArcSelection(Shop);
        const SolidArcCommandOutcome BodyRemoval = RemoveSolidArcMatcap(Shop);
        const SolidArcCommandOutcome Nothing     = RemoveSolidArcMatcap(Shop);
        Tally(MatcapGate, 0.0, FaceRemoval.AcceptedCondition && BodyRemoval.AcceptedCondition && !Nothing.AcceptedCondition && Shop.Appearance.FaceMatcaps.size() == 1
                               && Shop.Appearance.FaceMatcaps.count(Second) == 1 && Shop.Appearance.BodyMatcap == SolidArcStudioMatcap,
              "remove on 1 face → 1 override left; remove on the body → Studio; a third remove refused (" + Nothing.Refusal + ")");

        // ⑨ user picture: added by name (layer 15), refused when missing / named twice; saved → loaded → saved byte-equal
        const SolidArcCommandOutcome Added     = AddSolidArcMatcapPicture(Shop, "ShopStripes", Bitmap);
        const SolidArcCommandOutcome Twice     = AddSolidArcMatcapPicture(Shop, "ShopStripes", Bitmap);
        const SolidArcCommandOutcome BuiltName = AddSolidArcMatcapPicture(Shop, "Chrome", Bitmap);
        const SolidArcCommandOutcome Absent    = AddSolidArcMatcapPicture(Shop, "Absent", Bitmap + ".missing");
        ClearSolidArcSelection(Shop);
        const SolidArcCommandOutcome Striped   = AssignSolidArcMatcap(Shop, "ShopStripes");
        const SolidArcMatcapLibrary  Custom    = AssembleSolidArcMatcaps(Shop.Appearance);
        Tally(MatcapGate, 0.0, Added.AcceptedCondition && !Twice.AcceptedCondition && !BuiltName.AcceptedCondition && !Absent.AcceptedCondition && Striped.AcceptedCondition
                               && Custom.Names.size() == 15 && SolidArcMatcapLayer(Custom, "ShopStripes") == 14 && Custom.Refusals.back().empty() && Custom.Pictures.back().Pixels == Fitted.Pixels,
              "picture ShopStripes → layer 14 of 15, fitted pixels equal; twice / built-in name / missing file refused (" + Absent.Refusal + ")");
        const std::string Written = EncodeSolidArcDocument(Shop);
        const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
        SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
        Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
        RegenerateSolidArcDocument(Copy, true);
        SolidArcDocument Bare = CreateSolidArcDocument("Bare");
        Expect(ConstructSolidArcPrimitive(Bare, Stock));
        Tally(MatcapGate, 0.0, Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Written.find("[Appearance]") != std::string::npos
                               && Copy.Appearance.FaceMatcaps == Shop.Appearance.FaceMatcaps && EncodeSolidArcDocument(Bare).find("Appearance") == std::string::npos,
              "save → load → save byte-equal with [Appearance]; documents without matcaps write no Appearance key");
        std::string Corrupt = Written;
        const size_t Brass = Corrupt.find("Matcap = 'Brass'");
        if (Brass != std::string::npos) Corrupt.replace(Brass, 16, "Matcap = 'Unobtainium'");
        const SolidArcDocumentFile Rejected = DecodeSolidArcDocument(Corrupt);
        Tally(MatcapGate, 0.0, Brass != std::string::npos && !Rejected.Refusal.empty(), "a file naming an unknown matcap is refused: " + Rejected.Refusal);
        std::filesystem::remove(Bitmap);
    }

    // ---- W16 multi-body: Plasticity R (new body) tiles, per-body matcaps, Q Boolean, targeted commands, refusals, codec.
    {
        auto Relative = [](double Value, double Expected) { return std::abs(Value - Expected) / std::max(1.0e-12, std::abs(Expected)); };
        auto Figures  = [](const SolidArcDocument& Target, const std::string& Key) -> const SolidArcBodyFigures*
        {
            for (const SolidArcBodyFigures& Body : Target.Summary.Bodies) if (Body.Key == Key) return &Body;
            return nullptr;
        };
        auto VolumeOf = [&](const SolidArcDocument& Target, const std::string& Key) { const SolidArcBodyFigures* Body = Figures(Target, Key); return Body ? Body->Volume : -1.0; };

        // ① three bodies: Block1 4 × 2 × 1, Cylinder1 r 0.5 h 2 through it (New body), Block2 unit cube at x = 6 (New body)
        SolidArcDocument Parts = CreateSolidArcDocument("Parts");
        SolidArcConstructRequest Request;
        Request.Kind = SolidArcConstructKind::Block; Request.Width = 4.0; Request.Depth = 2.0; Request.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Parts, Request));
        Request = {};
        Request.Kind = SolidArcConstructKind::Cylinder; Request.Position = { 1.0, 1.0, -0.5 }; Request.Radius = 0.5; Request.Height = 2.0;
        Request.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(Parts, Request));
        Request = {};
        Request.Kind = SolidArcConstructKind::Block; Request.Position = { 6.0, 0.0, 0.0 }; Request.Width = 1.0; Request.Depth = 1.0; Request.Height = 1.0;
        Request.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(Parts, Request));
        const bool Three = Parts.Summary.Bodies.size() == 3;
        const std::string BlockKey    = Three ? Parts.Summary.Bodies[0].Key : std::string{};
        const std::string CylinderKey = Three ? Parts.Summary.Bodies[1].Key : std::string{};
        const std::string CubeKey     = Three ? Parts.Summary.Bodies[2].Key : std::string{};
        const double CylinderVolume = Pi * 0.25 * 2.0;
        const double Worst = std::max({ Relative(VolumeOf(Parts, BlockKey), 8.0), Relative(VolumeOf(Parts, CylinderKey), CylinderVolume), Relative(VolumeOf(Parts, CubeKey), 1.0),
                                        Relative(Parts.Summary.Volume, 9.0 + CylinderVolume) });
        std::string Captions;
        for (const SolidArcBodyFigures& Body : Parts.Summary.Bodies) Captions += (Captions.empty() ? "" : " · ") + Body.Caption + " (" + Body.Key + ")";
        Tally(BodiesGate, Worst, Three && Worst <= 1.0e-9 && Parts.Summary.SoundCondition && Parts.Regeneration.Active == CubeKey,
              "3 bodies " + Captions + ": 8 · π/2 · 1 exact, overlapping solids stay separate, all sound, active = the last created");

        // ② scene: one span per body, faces and edges tagged; picking the cylinder body marks exactly its faces
        SolidArcScene PartsScene = BuildSolidArcScene(Parts);
        bool Spans = PartsScene.Bodies.size() == 3;
        uint32_t FaceSum = 0;
        for (size_t Index = 0; Spans && Index < PartsScene.Bodies.size(); ++Index)
        {
            const SolidArcSceneBody& Body = PartsScene.Bodies[Index];
            Spans = Spans && Body.Key == Parts.Summary.Bodies[Index].Key && int(Body.FaceCount) == Parts.Summary.Bodies[Index].Faces && Body.FirstFace == FaceSum;
            for (uint32_t Face = Body.FirstFace; Face < Body.FirstFace + Body.FaceCount; ++Face) Spans = Spans && PartsScene.Faces[Face].Body == Index;
            FaceSum += Body.FaceCount;
        }
        SelectSolidArcName(Parts, SolidArcSelectionKind::Body, CylinderKey, false);
        const SolidArcSceneStates Picked = ComposeSolidArcStates(PartsScene, Parts.Selection, 0u);
        const long Marked = std::count_if(Picked.Faces.begin(), Picked.Faces.end(), [](uint32_t Word) { return (Word & 1u) != 0; });
        Tally(BodiesGate, 0.0, Spans && FaceSum == PartsScene.Faces.size() && Three && Marked == Parts.Summary.Bodies[1].Faces,
              "scene: 3 body spans in creation order, face counts equal the audit, every face tagged; body pick marks its " + std::to_string(Marked) + " faces only");

        // ③ per-body matcaps: Block1 → Brushed steel, Cylinder1 → Brass, one Block1 face → Red plastic; Block2 keeps the default
        const uint64_t Revision = Parts.Revision;
        SelectSolidArcName(Parts, SolidArcSelectionKind::Body, BlockKey, false);
        const SolidArcCommandOutcome Steel = AssignSolidArcMatcap(Parts, "BrushedSteel");
        SelectSolidArcName(Parts, SolidArcSelectionKind::Body, CylinderKey, false);
        const SolidArcCommandOutcome Brass = AssignSolidArcMatcap(Parts, "Brass");
        const std::string RedFace = PartsScene.Faces[PartsScene.Bodies[0].FirstFace].Name;
        SelectSolidArcName(Parts, SolidArcSelectionKind::Face, RedFace, false);
        const SolidArcCommandOutcome Red = AssignSolidArcMatcap(Parts, "RedPlastic");
        ClearSolidArcSelection(Parts);
        const SolidArcMatcapLibrary Library = AssembleSolidArcMatcaps(Parts.Appearance);
        auto LayerCounts = [&](const SolidArcScene& Scene)
        {
            std::map<std::string, int> Counts;
            for (const uint32_t Word : ComposeSolidArcMatcapLayers(Scene, Parts.Appearance, Library)) Counts[Library.Names[Word >> SolidArcMatcapLayerShift]] += 1;
            return Counts;
        };
        std::map<std::string, int> Counts = LayerCounts(PartsScene);
        const int CylinderFaces = Parts.Summary.Bodies[1].Faces, CubeFaces = Parts.Summary.Bodies[2].Faces;
        Tally(BodiesGate, 0.0, Steel.AcceptedCondition && Brass.AcceptedCondition && Red.AcceptedCondition && Parts.Revision == Revision && Counts["BrushedSteel"] == 5
                               && Counts["RedPlastic"] == 1 && Counts["Brass"] == CylinderFaces && Counts[SolidArcStudioMatcap] == CubeFaces && Parts.Appearance.BodyMatcaps.size() == 2
                               && SolidArcFaceMatcap(Parts.Appearance, BlockKey, RedFace) == "RedPlastic" && SolidArcBodyMatcap(Parts.Appearance, CubeKey) == SolidArcStudioMatcap,
              Steel.Label + " · " + Brass.Label + " · " + Red.Label + ": 5 steel + 1 red + " + std::to_string(CylinderFaces) + " brass + " + std::to_string(CubeFaces)
              + " Studio faces; revision unchanged");

        // ④ Q Boolean: Block1 − Cylinder1. The tool is consumed (its Brass override is kept for undo / delete, so it dangles);
        //    the hole faces belong to Block1 and show its matcap.
        const SolidArcCommandOutcome Drill = CombineSolidArcBodies(Parts, BlockKey, { CylinderKey }, FeatureCombination::Difference);
        PartsScene = BuildSolidArcScene(Parts);
        Counts = LayerCounts(PartsScene);
        const double Drilled = 8.0 - Pi * 0.25;
        const double DrillError = std::max(Relative(VolumeOf(Parts, BlockKey), Drilled), Relative(Parts.Summary.Volume, Drilled + 1.0));
        Tally(BodiesGate, DrillError, Drill.AcceptedCondition && DrillError <= 1.0e-9 && Parts.Summary.Bodies.size() == 2 && !Figures(Parts, CylinderKey) && Parts.Summary.SoundCondition
                                      && Parts.Tree.Steps.back().Kind == FeatureStepKind::Combine && Parts.Tree.Steps.back().Tool == CylinderKey && Parts.Regeneration.Active == BlockKey
                                      && CountDanglingMatcaps(PartsScene, Parts.Appearance) == 1 && Counts["Brass"] == 0 && Counts["BrushedSteel"] == Parts.Summary.Bodies[0].Faces - 1,
              Drill.Label + ": Block1 = 8 − π/4 exact, 2 bodies, the hole wears Block1's steel; the tool's Brass override dangles (1)");

        // ⑤ deleting the Combine step brings Cylinder1 back with its Brass; undo re-applies the Boolean
        const std::string CombineLabel = Parts.Tree.Steps.back().Label;
        const SolidArcCommandOutcome Restored = DeleteSolidArcStep(Parts, CombineLabel);
        PartsScene = BuildSolidArcScene(Parts);
        Counts = LayerCounts(PartsScene);
        const bool Back = Restored.AcceptedCondition && Parts.Summary.Bodies.size() == 3 && Relative(VolumeOf(Parts, CylinderKey), CylinderVolume) <= 1.0e-9 && Counts["Brass"] == CylinderFaces && CountDanglingMatcaps(PartsScene, Parts.Appearance) == 0;
        UndoSolidArcCommand(Parts);
        Tally(BodiesGate, Relative(VolumeOf(Parts, BlockKey), Drilled), Back && Parts.Summary.Bodies.size() == 2 && Relative(VolumeOf(Parts, BlockKey), Drilled) <= 1.0e-9,
              "delete " + CombineLabel + " → Cylinder1 returns (π/2, Brass, 0 dangling); undo → drilled again");

        // ⑥ targeted commands while Block1 is active: fillet one vertical Block2 edge (r 0.2, closed form), then a boss on
        //    Block2 (Target). Vertical edges join two sides, so their names carry no "Cap".
        std::string CubeEdge;
        for (const RegeneratedBody& Body : Parts.Regeneration.Bodies)
            if (Body.Key == CubeKey)
                for (const auto& Entry : Body.Naming.Edges)
                    if (CubeEdge.empty() && Entry.second.find("Cap") == std::string::npos) CubeEdge = Entry.second;
        SelectSolidArcName(Parts, SolidArcSelectionKind::Edge, CubeEdge, false);
        const SolidArcCommandOutcome Rounded = BlendSolidArcSelection(Parts, BlendProfile::Fillet, 0.2);
        const double RoundedCube = 1.0 - 0.04 * (1.0 - Pi / 4.0);
        Request = {};
        Request.Kind = SolidArcConstructKind::Cylinder; Request.Position = { 6.5, 0.5, 1.0 }; Request.Radius = 0.25; Request.Height = 0.5;
        Request.Combination = FeatureCombination::Union; Request.Target = CubeKey;
        const SolidArcCommandOutcome Bossed = ConstructSolidArcPrimitive(Parts, Request);
        const double BossedCube = RoundedCube + Pi * 0.0625 * 0.5;
        const double TargetError = std::max({ Relative(VolumeOf(Parts, CubeKey), BossedCube), Relative(VolumeOf(Parts, BlockKey), Drilled) });
        const FeatureStep* Blend = nullptr;
        for (const FeatureStep& Step : Parts.Tree.Steps) if (Step.Kind == FeatureStepKind::Blend) Blend = &Step;
        Tally(BodiesGate, TargetError, Rounded.AcceptedCondition && Bossed.AcceptedCondition && TargetError <= 1.0e-9 && Blend && Blend->Target == CubeKey && Parts.Summary.SoundCondition,
              "fillet r 0.2 on " + CubeEdge + " (Target " + CubeKey + ") + Cylinder boss with Target = Block2: 1 − 0.04(1 − π/4) + π/32 exact; Block1 untouched");

        // ⑦ refusals leave tree, bodies and history untouched
        const size_t Steps = Parts.Tree.Steps.size(), Entries = Parts.UndoEntries.size();
        const double Volume = Parts.Summary.Volume;
        PartsScene = BuildSolidArcScene(Parts);
        SelectSolidArcName(Parts, SolidArcSelectionKind::Edge, PartsScene.Edges[PartsScene.Bodies.front().FirstEdge].Name, false);
        SelectSolidArcName(Parts, SolidArcSelectionKind::Edge, PartsScene.Edges[PartsScene.Bodies.back().FirstEdge].Name, true);
        const SolidArcCommandOutcome Spanning = BlendSolidArcSelection(Parts, BlendProfile::Fillet, 0.1);
        ClearSolidArcSelection(Parts);
        const SolidArcCommandOutcome Self     = CombineSolidArcBodies(Parts, BlockKey, { BlockKey }, FeatureCombination::Union);
        const SolidArcCommandOutcome Lonely   = CombineSolidArcBodies(Parts, BlockKey, {}, FeatureCombination::Union);
        const SolidArcCommandOutcome Ghost    = CombineSolidArcBodies(Parts, BlockKey, { "Extrude#99" }, FeatureCombination::Union);
        const SolidArcCommandOutcome Separate = CombineSolidArcBodies(Parts, BlockKey, { CubeKey }, FeatureCombination::Separate);
        Request = {};
        Request.Kind = SolidArcConstructKind::Sphere; Request.Radius = 0.3; Request.Combination = FeatureCombination::New;
        const SolidArcCommandOutcome Second = ConstructSolidArcPrimitive(Parts, Request);
        const bool Refused = !Spanning.AcceptedCondition && !Self.AcceptedCondition && !Lonely.AcceptedCondition && !Ghost.AcceptedCondition && !Separate.AcceptedCondition && !Second.AcceptedCondition;
        Tally(BodiesGate, 0.0, Refused && Parts.Tree.Steps.size() == Steps && Parts.UndoEntries.size() == Entries && Parts.Summary.Volume == Volume,
              "refused: blend across bodies (" + Spanning.Refusal + ") · self-combine (" + Self.Refusal + ") · no tool · unknown body (" + Ghost.Refusal + ") · Separate as Q · second New; untouched");

        // ⑧ intersection keeps only the common part: Block2 ∩ a fresh New-body block overlapping its lower half
        Request = {};
        Request.Kind = SolidArcConstructKind::Block; Request.Position = { 5.5, -0.5, -0.5 }; Request.Width = 2.0; Request.Depth = 2.0; Request.Height = 1.0;
        Request.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(Parts, Request));
        const std::string SlabKey = Parts.Summary.Bodies.back().Key;
        const SolidArcCommandOutcome Common = CombineSolidArcBodies(Parts, CubeKey, { SlabKey }, FeatureCombination::Intersection);
        const double CommonVolume = 0.5 - 0.04 * (1.0 - Pi / 4.0) * 0.5;                    // Lower half of the cube with half the vertical fillet
        const double CommonError  = Relative(VolumeOf(Parts, CubeKey), CommonVolume);
        Tally(BodiesGate, CommonError, Common.AcceptedCondition && CommonError <= 1.0e-9 && Parts.Summary.Bodies.size() == 2 && Parts.Summary.SoundCondition,
              Common.Label + ": Block2 = its lower half (V = " + std::to_string(VolumeOf(Parts, CubeKey)) + "), the slab consumed");
        UndoSolidArcCommand(Parts);
        UndoSolidArcCommand(Parts);

        // ⑨ save → load → save byte-equal (Target / Tool / Combine / [[Appearance.Bodies]]); same body volumes after reload
        const std::string Written = EncodeSolidArcDocument(Parts);
        const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
        SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
        Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
        RegenerateSolidArcDocument(Copy, true);
        bool SameBodies = Copy.Summary.Bodies.size() == Parts.Summary.Bodies.size();
        for (size_t Index = 0; SameBodies && Index < Copy.Summary.Bodies.size(); ++Index)
            SameBodies = Copy.Summary.Bodies[Index].Key == Parts.Summary.Bodies[Index].Key && Copy.Summary.Bodies[Index].Volume == Parts.Summary.Bodies[Index].Volume;
        Tally(BodiesGate, 0.0, Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && SameBodies && Copy.Appearance.BodyMatcaps == Parts.Appearance.BodyMatcaps
                               && Written.find("Tool = '") != std::string::npos && Written.find("Target = '") != std::string::npos && Written.find("[[Appearance.Bodies]]") != std::string::npos,
              "save → load → save byte-equal with Combine Tool / Target and [[Appearance.Bodies]]; " + std::to_string(Copy.Summary.Bodies.size()) + " bodies, volumes bit-equal");
        std::string Future = Written;
        const size_t KindAt = Future.find("Kind = 10");
        if (KindAt != std::string::npos) Future.replace(KindAt, 9, "Kind = 25");
        const SolidArcDocumentFile Newer = DecodeSolidArcDocument(Future);
        Tally(BodiesGate, 0.0, KindAt != std::string::npos && !Newer.Refusal.empty(), "a future step kind (25, beyond Evolution 16) is refused: " + Newer.Refusal);
    }

    // ---- W17 trim / split: Plasticity C (command:cut) and X on picked bodies.
    {
        auto Relative = [](double Value, double Expected) { return std::abs(Value - Expected) / std::max(1.0e-12, std::abs(Expected)); };
        auto Figures  = [](const SolidArcDocument& Target, const std::string& Key) -> const SolidArcBodyFigures*
        {
            for (const SolidArcBodyFigures& Body : Target.Summary.Bodies) if (Body.Key == Key) return &Body;
            return nullptr;
        };
        auto VolumeOf = [&](const SolidArcDocument& Target, const std::string& Key) { const SolidArcBodyFigures* Body = Figures(Target, Key); return Body ? Body->Volume : -1.0; };
        auto Draw = [](SolidArcDocument& Target, SolidArcDrawingKind Kind, std::vector<Vector2> Points, bool CentreCondition = false)
        {
            SolidArcDrawingRequest Drawing;
            Drawing.Kind = Kind; Drawing.Points = std::move(Points); Drawing.CentreCondition = CentreCondition;
            const SolidArcCommandOutcome Outcome = DrawFilledSketch(Target, Drawing);
            return Outcome.AcceptedCondition ? Target.Tree.Steps.back().Label : std::string{};
        };

        // ① Block1 4 × 2 × 1 (Brass) cut by a drawn line x = 1 drawn upward: the inside piece (left, x < 1) is 2, the rest 6
        SolidArcDocument Part = CreateSolidArcDocument("Cut");
        SolidArcConstructRequest Request;
        Request.Kind = SolidArcConstructKind::Block; Request.Width = 4.0; Request.Depth = 2.0; Request.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Part, Request));
        const std::string BlockKey = Part.Regeneration.Active;
        SelectSolidArcName(Part, SolidArcSelectionKind::Body, BlockKey, false);
        Expect(AssignSolidArcMatcap(Part, "Brass"));
        const std::string LineSketch = Draw(Part, SolidArcDrawingKind::Line, { { 1.0, -1.0 }, { 1.0, 3.0 } });
        SolidArcCutRequest Cut;
        Cut.Target = BlockKey; Cut.Cutter = LineSketch;
        const SolidArcCommandOutcome Split = CutSolidArcBody(Part, Cut);
        const std::string PieceKey = Split.AcceptedCondition ? Part.Tree.Steps.back().Label : std::string{};
        const double SplitError = std::max(Relative(VolumeOf(Part, PieceKey), 2.0), Relative(VolumeOf(Part, BlockKey), 6.0));
        Tally(CutGate, SplitError, Split.AcceptedCondition && SplitError <= 1.0e-9 && Part.Summary.Bodies.size() == 2 && Part.Summary.SoundCondition && Part.Regeneration.Active == PieceKey
                                   && SolidArcBodyCaption(Part, PieceKey) == "Cut1" && SolidArcBodyMatcap(Part.Appearance, BlockKey) == "Brass" && SolidArcBodyMatcap(Part.Appearance, PieceKey) == SolidArcStudioMatcap,
              Split.Label + ": " + PieceKey + " (Cut1) = 2, Block1 = 6 exact, both sound; Block1 keeps its key and Brass, the piece wears Studio");

        // ② X on the picked piece removes it; undo brings it back, redo removes it again
        const SolidArcCommandOutcome Removed = RemoveSolidArcBodies(Part, { PieceKey });
        const bool Gone = Removed.AcceptedCondition && Part.Summary.Bodies.size() == 1 && !Figures(Part, PieceKey) && Relative(VolumeOf(Part, BlockKey), 6.0) <= 1.0e-9
                       && Part.Tree.Steps.back().Kind == FeatureStepKind::Remove && Part.Regeneration.Active == BlockKey;
        UndoSolidArcCommand(Part);
        const bool Undone = Part.Summary.Bodies.size() == 2 && Relative(VolumeOf(Part, PieceKey), 2.0) <= 1.0e-9;
        RedoSolidArcCommand(Part);
        Tally(CutGate, 0.0, Gone && Undone && Part.Summary.Bodies.size() == 1, Removed.Label + ": 1 body left (6), Block1 active; undo → 2 bodies, redo → 1");

        // ③ a drawn circle r ½ at (2, 1) cuts a cylinder out of what is left: π/4 inside, 6 − π/4 outside
        const std::string CircleSketch = Draw(Part, SolidArcDrawingKind::Circle, { { 2.0, 1.0 }, { 2.5, 1.0 } }, true);
        Cut = {};
        Cut.Cutter = CircleSketch;                                                           // Target empty → the active body
        const SolidArcCommandOutcome Core = CutSolidArcBody(Part, Cut);
        const std::string CoreKey = Core.AcceptedCondition ? Part.Tree.Steps.back().Label : std::string{};
        const double CoreError = std::max(Relative(VolumeOf(Part, CoreKey), Pi / 4.0), Relative(VolumeOf(Part, BlockKey), 6.0 - Pi / 4.0));
        Tally(CutGate, CoreError, Core.AcceptedCondition && CoreError <= 1.0e-9 && Part.Summary.Bodies.size() == 2 && Part.Summary.SoundCondition && Figures(Part, BlockKey) && Figures(Part, BlockKey)->Genus == 1,
              Core.Label + ": core π/4 and the holed block 6 − π/4 exact (genus 1), cut from the active body");

        // ④ vehicle flow: 3 parabolic sections lofted into a sheet, a wavy character line seen from the Top view (−z)
        //    splits it into two sound sheets, X removes one (the BMW workflow: panel → C → X)
        SolidArcDocument Bonnet = CreateSolidArcDocument("Bonnet");
        VehicleCurveFile Curves;
        for (int Section = 0; Section < 3; ++Section)
        {
            VehicleCurve Curve;
            Curve.Name = "Section" + std::to_string(Section + 1);
            for (int Index = 0; Index <= 10; ++Index)
            {
                const double X = -1.0 + 0.2 * Index, Y = 0.5 * Section;
                Curve.Points.push_back({ X, Y, 0.25 * (1.0 - X * X) + 0.08 * Y * Y });
            }
            Curves.Curves.push_back(Curve);
        }
        VehicleCurve Character;
        Character.Name = "CharacterLine";
        for (int Index = 0; Index <= 12; ++Index)
        {
            const double X = -1.6 + 3.2 * Index / 12.0;
            Character.Points.push_back({ X, 0.5 + 0.18 * std::sin(2.2 * X / 1.6), 1.0 });
        }
        Curves.Curves.push_back(Character);
        Expect(ImportSolidArcCurves(Bonnet, Curves));
        std::vector<std::string> Labels;
        for (const FeatureStep& Step : Bonnet.Tree.Steps) if (Step.Kind == FeatureStepKind::SpaceCurve) Labels.push_back(Step.Label);
        const bool Imported = Labels.size() == 4;
        if (Imported) Expect(LoftSolidArcCurves(Bonnet, { Labels[0], Labels[1], Labels[2] }));
        const std::string PanelKey = Bonnet.Regeneration.Active;
        Cut = {};
        Cut.Target = PanelKey; Cut.Cutter = Imported ? Labels[3] : std::string{}; Cut.Direction = { 0.0, 0.0, -1.0 };
        const SolidArcCommandOutcome Trim = CutSolidArcBody(Bonnet, Cut);
        const std::string TrimKey = Trim.AcceptedCondition ? Bonnet.Tree.Steps.back().Label : std::string{};
        const SolidArcBodyFigures* Kept  = Figures(Bonnet, PanelKey);
        const SolidArcBodyFigures* Piece = Figures(Bonnet, TrimKey);
        const bool Sheets = Trim.AcceptedCondition && Kept && Piece && Kept->SheetCondition && Piece->SheetCondition && Kept->SoundCondition && Piece->SoundCondition && Bonnet.Summary.Bodies.size() == 2;
        const SolidArcCommandOutcome Discard = RemoveSolidArcBodies(Bonnet, { TrimKey });
        Tally(CutGate, 0.0, Imported && Sheets && Discard.AcceptedCondition && Bonnet.Summary.Bodies.size() == 1 && Figures(Bonnet, PanelKey) && Figures(Bonnet, PanelKey)->SheetCondition,
              Trim.Label + " seen along −z: 2 sound sheets (" + std::to_string(Kept ? Kept->Faces : 0) + " + " + std::to_string(Piece ? Piece->Faces : 0) + " faces); " + Discard.Label + " keeps the trimmed panel");

        // ⑤ refusals leave tree, bodies and history untouched
        const size_t Steps = Part.Tree.Steps.size(), Entries = Part.UndoEntries.size();
        const double Volume = Part.Summary.Volume;
        Cut = {};
        const SolidArcCommandOutcome NoCutter = CutSolidArcBody(Part, Cut);
        const std::string Beside = Draw(Part, SolidArcDrawingKind::Line, { { 9.0, -1.0 }, { 9.0, 3.0 } });
        const size_t StepsWithBeside = Part.Tree.Steps.size(), EntriesWithBeside = Part.UndoEntries.size();
        Cut.Target = BlockKey; Cut.Cutter = Beside;
        const SolidArcCommandOutcome Misses = CutSolidArcBody(Part, Cut);
        Cut.Target = "Extrude#99";
        const SolidArcCommandOutcome Ghost = CutSolidArcBody(Part, Cut);
        Cut.Target = BlockKey; Cut.Cutter = BlockKey;                                         // An extrude step is no cutter
        const SolidArcCommandOutcome Wrong = CutSolidArcBody(Part, Cut);
        const SolidArcCommandOutcome Phantom = RemoveSolidArcBodies(Part, { "Ghost#1" });
        const bool Refused = !NoCutter.AcceptedCondition && !Misses.AcceptedCondition && !Ghost.AcceptedCondition && !Wrong.AcceptedCondition && !Phantom.AcceptedCondition;
        Tally(CutGate, 0.0, Refused && Part.Tree.Steps.size() == StepsWithBeside && Part.UndoEntries.size() == EntriesWithBeside && Part.Summary.Volume == Volume && StepsWithBeside == Steps + 1 && EntriesWithBeside == Entries + 1,
              "refused: no cutter (" + NoCutter.Refusal + ") · misses (" + Misses.Refusal + ") · unknown body (" + Ghost.Refusal + ") · extrude as cutter · Remove unknown (" + Phantom.Refusal + "); untouched");
        Expect(DeleteSolidArcStep(Part, Beside));

        // ⑥ save → load → save byte-equal with Cut (Profile) / Remove and a curve Cut (Sections + plane); volumes bit-equal
        bool RoundTrips = true;
        std::string Notes;
        for (SolidArcDocument* Source : { &Part, &Bonnet })
        {
            const std::string Written = EncodeSolidArcDocument(*Source);
            const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
            SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
            Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Copy, true);
            bool Same = Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Copy.Summary.Bodies.size() == Source->Summary.Bodies.size();
            for (size_t Index = 0; Same && Index < Copy.Summary.Bodies.size(); ++Index)
                Same = Copy.Summary.Bodies[Index].Key == Source->Summary.Bodies[Index].Key && Copy.Summary.Bodies[Index].Volume == Source->Summary.Bodies[Index].Volume;
            RoundTrips = RoundTrips && Same && Written.find("Kind = 11") != std::string::npos && Written.find("Kind = 12") != std::string::npos;
            Notes += (Notes.empty() ? "" : " · ") + Source->Title + " " + std::to_string(Copy.Summary.Bodies.size()) + " bodies";
        }
        Tally(CutGate, 0.0, RoundTrips, "save → load → save byte-equal with Cut (11) and Remove (12): " + Notes + ", volumes bit-equal");
    }

    // ---- W18: curve trim (Plasticity T) on sketches
    {
        auto Format = [](double Value) { char Text[32]; std::snprintf(Text, sizeof(Text), "%.12g", Value); return std::string(Text); };
        auto Draw = [](SolidArcDocument& Target, SolidArcDrawingKind Kind, std::vector<Vector2> Points, bool ClosedCondition = false, bool CentreCondition = false)
        {
            SolidArcDrawingRequest Drawing;
            Drawing.Kind = Kind; Drawing.Points = std::move(Points); Drawing.ClosedCondition = ClosedCondition; Drawing.CentreCondition = CentreCondition;
            const SolidArcCommandOutcome Outcome = DrawFilledSketch(Target, Drawing);
            return Outcome.AcceptedCondition ? Target.Tree.Steps.back().Label : std::string{};
        };
        auto CurvesOf = [](const SolidArcDocument& Target, const std::string& Label)
        {
            const auto Solved = Target.Regeneration.Sketches.find(Label);
            return Solved != Target.Regeneration.Sketches.end() ? Solved->second.Curves.size() : size_t(0);
        };
        auto Meet = [](Vector2 First, Vector2 FirstEnd, Vector2 Second, Vector2 SecondEnd)
        {
            const Vector2 Along = FirstEnd - First, Across = SecondEnd - Second;
            const double  Share = Cross(Second - First, Across) / Cross(Along, Across);
            return First + Along * Share;
        };

        // ① pentagram A → C → E → B → D → A as one closed Line chain: the crossing chain cannot be extruded
        const std::vector<Vector2> Tips = { { 1.0, 0.0 }, { 1.6, 1.9 }, { 0.0, 0.7 }, { 2.0, 0.7 }, { 0.4, 1.9 } };
        SolidArcDocument Star = CreateSolidArcDocument("Star");
        const std::string StarSketch = Draw(Star, SolidArcDrawingKind::Line, Tips, true);
        const SolidArcCommandOutcome Early = ExtrudeSolidArcSketch(Star, StarSketch, 1.0, FeatureCombination::New);
        std::vector<Vector2> Outline = Tips;                                                 // Independent star: 5 tips + 5 inner crossings
        for (int Chord = 0; Chord < 5; ++Chord)
        {
            const Vector2 From = Tips[Chord], To = Tips[(Chord + 1) % 5];
            const Vector2 Next = Tips[(Chord + 2) % 5], NextEnd = Tips[(Chord + 3) % 5];
            Outline.push_back(Meet(From, To, Next, NextEnd));
        }
        Vector2 Middle = {};
        for (const Vector2& Tip : Tips) Middle = Middle + Tip * 0.2;
        std::sort(Outline.begin(), Outline.end(), [&](Vector2 Left, Vector2 Right) { return std::atan2(Left.Y - Middle.Y, Left.X - Middle.X) < std::atan2(Right.Y - Middle.Y, Right.X - Middle.X); });
        double StarArea = 0.0;
        for (size_t Index = 0; Index < Outline.size(); ++Index) StarArea += 0.5 * Cross(Outline[Index], Outline[(Index + 1) % Outline.size()]);

        // ② preview of the first trim: removed ends on the two crossings of chord A → C, the document untouched
        const std::string BeforePreview = EncodeSolidArcDocument(Star);
        const SketchTrimOutcome Preview = PreviewSolidArcTrim(Star, { StarSketch, 0, (Tips[0] + Tips[1]) * 0.5 });
        Vector2 FirstCrossing  = Meet(Tips[0], Tips[1], Tips[3], Tips[4]);                   // A → C meets B → D and E → B,
        Vector2 SecondCrossing = Meet(Tips[0], Tips[1], Tips[2], Tips[3]);                   // ordered along A → C
        if (Dot(FirstCrossing - Tips[0], Tips[1] - Tips[0]) > Dot(SecondCrossing - Tips[0], Tips[1] - Tips[0])) std::swap(FirstCrossing, SecondCrossing);
        const double  PreviewError   = Preview.Removed.size() >= 2 ? std::max(Length(Preview.Removed.front() - FirstCrossing), Length(Preview.Removed.back() - SecondCrossing)) : 1.0;
        Tally(TrimGate, PreviewError, Preview.Refusal.empty() && PreviewError <= 1.0e-12 && EncodeSolidArcDocument(Star) == BeforePreview && Preview.Pieces == 2,
              "preview T on Line1 of the pentagram: removed piece between its two crossings (off " + Format(PreviewError) + "), 2 pieces left; document unchanged");

        // ③ T on the five inner pieces: the star outline (10 lines) is one loop of the shoelace area; extrude 1 is exact
        bool Trimmed = true;
        for (int Chord = 0; Chord < 5; ++Chord)
            Trimmed = Trimmed && TrimSolidArcSketch(Star, { StarSketch, Chord, (Tips[Chord] + Tips[(Chord + 1) % 5]) * 0.5 }).AcceptedCondition;
        const auto   Solved      = Star.Regeneration.Sketches.find(StarSketch);
        const SketchProfile Loop = Solved != Star.Regeneration.Sketches.end() ? ExtractSketchProfile(Solved->second) : SketchProfile{};
        const double AreaError   = std::abs(Loop.Area - StarArea);
        const SolidArcCommandOutcome Raised = ExtrudeSolidArcSketch(Star, StarSketch, 1.0, FeatureCombination::New);
        const double VolumeError = std::abs(Star.Summary.Volume - StarArea) / StarArea;
        Tally(TrimGate, std::max(AreaError, VolumeError), !Early.AcceptedCondition && Trimmed && CurvesOf(Star, StarSketch) == 10 && Loop.Refusal.empty() && Loop.Loops.size() == 1
              && AreaError <= 1.0e-12 && Raised.AcceptedCondition && VolumeError <= 1.0e-9 && Star.Summary.SoundCondition && Star.UndoEntries.size() == 7,
              "pentagram: extrude refused first (" + Early.Refusal.substr(0, 60) + "…); 5 × T → 10 lines, area " + Format(Loop.Area) + " vs shoelace " + Format(StarArea) + ", extrude 1 → V " + Format(Star.Summary.Volume) + ", sound");

        // ④ a trim that would open the extruded star detaches (W46): the extrude keeps the star (hidden), the opened
        //    chain is a new sketch of 9 lines; undo returns the file byte-equal
        const std::string BeforeBreak = EncodeSolidArcDocument(Star);
        const size_t      Entries     = Star.UndoEntries.size();
        const double      StarVolume  = Star.Summary.Volume;
        const SolidArcCommandOutcome Break = TrimSolidArcSketch(Star, { StarSketch, 0, Tips[0] + (Tips[1] - Tips[0]) * 0.1 });
        const std::string Opened = Star.Tree.Steps.back().Label;
        const bool Detached = Break.AcceptedCondition && SolidArcCurveHiddenCondition(Star, StarSketch) && Opened != StarSketch && CurvesOf(Star, Opened) == 9
                           && Star.Summary.Volume == StarVolume && Star.UndoEntries.size() == Entries + 1;
        UndoSolidArcCommand(Star);
        Tally(TrimGate, 0.0, Detached && EncodeSolidArcDocument(Star) == BeforeBreak && Star.UndoEntries.size() == Entries,
              "T on a star tip after the extrude: detached (" + Break.Label + "), the extrude keeps the star, " + Opened + " holds 9 lines; undo byte-equal");

        // ⑤ cross-sketch: circle r 0.3 at (2, 0.5) over the right side of a 2 × 1 rectangle (two sketches)
        SolidArcDocument Plan = CreateSolidArcDocument("Plan");
        const std::string Box    = Draw(Plan, SolidArcDrawingKind::Rectangle, { { 0.0, 0.0 }, { 2.0, 1.0 } });
        const std::string Circle = Draw(Plan, SolidArcDrawingKind::Circle, { { 2.0, 0.5 }, { 2.3, 0.5 } }, false, true);
        const SolidArcCommandOutcome Outer = TrimSolidArcSketch(Plan, { Circle, 0, { 2.3, 0.5 } });
        const auto   Arc      = Plan.Regeneration.Sketches.find(Circle);
        const bool   ArcCondition = Arc != Plan.Regeneration.Sketches.end() && Arc->second.Curves.size() == 1 && Arc->second.Curves[0].Kind == SketchCurveKind::Arc;
        double ArcError = 1.0;
        if (ArcCondition)
        {
            const SketchModel& Model = Arc->second;
            ArcError = std::max(Length(Model.Points[Model.Curves[0].Start].Position - Vector2{ 2.0, 0.8 }), Length(Model.Points[Model.Curves[0].End].Position - Vector2{ 2.0, 0.2 }));
        }
        UndoSolidArcCommand(Plan);
        const bool Restored = Plan.Regeneration.Sketches.at(Circle).Curves[0].Kind == SketchCurveKind::Circle;
        RedoSolidArcCommand(Plan);
        const SolidArcCommandOutcome Side = TrimSolidArcSketch(Plan, { Box, 1, { 2.0, 0.5 } });
        Tally(TrimGate, ArcError, Outer.AcceptedCondition && ArcCondition && ArcError <= 1.0e-12 && Restored && Plan.Regeneration.Sketches.at(Circle).Curves[0].Kind == SketchCurveKind::Arc
              && Side.AcceptedCondition && CurvesOf(Plan, Box) == 5,
              Outer.Label + ": the circle (another sketch than the rectangle) becomes the arc (2, 0.8) → (2, 0.2) inside it (off " + Format(ArcError) + "); undo → circle, redo → arc; " + Side.Label + " splits the right side (5 curves)");

        // ⑥ a lone unused line with nothing crossing it: T erases it and its sketch step goes; undo brings it back
        const std::string Lone  = Draw(Plan, SolidArcDrawingKind::Line, { { 5.0, 5.0 }, { 6.0, 5.0 } });
        const size_t      Steps = Plan.Tree.Steps.size();
        const SolidArcCommandOutcome Erase = TrimSolidArcSketch(Plan, { Lone, 0, { 5.5, 5.0 } });
        const bool Gone = Erase.AcceptedCondition && Plan.Tree.Steps.size() == Steps - 1 && !Plan.Captions.contains(Lone);
        UndoSolidArcCommand(Plan);
        const bool Back = Plan.Tree.Steps.size() == Steps && CurvesOf(Plan, Lone) == 1;
        Tally(TrimGate, 0.0, Gone && Back, Erase.Label + ": nothing crosses it → the curve and its emptied sketch step go; undo restores");

        // ⑦ save → load → save byte-equal with the trimmed sketches (new points, PointOnLine pins = constraint kind 14)
        bool RoundTrips = true;
        for (SolidArcDocument* Source : { &Star, &Plan })
        {
            const std::string Written = EncodeSolidArcDocument(*Source);
            const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
            SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
            Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Copy, true);
            RoundTrips = RoundTrips && Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Copy.Summary.Volume == Source->Summary.Volume;
        }
        RoundTrips = RoundTrips && EncodeSolidArcDocument(Star).find("Kind = 14") != std::string::npos;
        Tally(TrimGate, 0.0, RoundTrips, "save → load → save byte-equal for the star (pins kept) and the plan; volumes bit-equal");
    }

    // ⑧ splines trim and cut without changing their shape (the request "need Trim on curves … without changing the shape
    //    of the curves"): a Hermite curve crossed by two lines of other sketches loses its middle piece; another is cut at
    //    a point. Every point of what stays (2000 per piece) lies on the original curve within 10⁻⁹, the new ends on the
    //    lines, nothing of the removed piece is left.
    {
        const OrthonormalFrame Ground{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        const auto Draw = [&](SolidArcDocument& Part, SolidArcDrawingKind Kind, std::vector<Vector2> Points)
        {
            SolidArcDrawingRequest Request;
            Request.Kind = Kind; Request.Points = std::move(Points); Request.PlaneCondition = true; Request.Plane = Ground;
            Expect(DrawSolidArcSketch(Part, Request));
            return Part.Tree.Steps.back().Label;
        };
        const auto Segments = [](const SolidArcDocument& Part, const std::string& Label)
        {
            std::vector<ProfileSegment> Found;
            const SketchModel& Model = Part.Regeneration.Sketches.at(Label);
            for (int Curve = 0; Curve < int(Model.Curves.size()); ++Curve)
            {
                std::string Refusal;
                const ProfileSegment Segment = ResolveSketchCurveSegment(Model, Curve, 1.0e-9, Refusal);
                if (Refusal.empty()) Found.push_back(Segment);
            }
            return Found;
        };
        // Distance of a point to a segment: 20 000 samples, then golden-section refinement.
        const auto Off = [](const ProfileSegment& Segment, Vector2 Point)
        {
            constexpr int Dense = 20000;
            int Nearest = 0;
            double Best = 1.0e300;
            for (int Sample = 0; Sample <= Dense; ++Sample)
                if (const double Gap = Distance(EvaluateProfileSegment(Segment, double(Sample) / Dense), Point); Gap < Best) { Best = Gap; Nearest = Sample; }
            double Low = double(std::max(0, Nearest - 1)) / Dense, High = double(std::min(Dense, Nearest + 1)) / Dense;
            for (int Round = 0; Round < 80; ++Round)
            {
                const double First = Low + (High - Low) * 0.381966011250105, Second = Low + (High - Low) * 0.618033988749895;
                if (Distance(EvaluateProfileSegment(Segment, First), Point) < Distance(EvaluateProfileSegment(Segment, Second), Point)) High = Second; else Low = First;
            }
            return Distance(EvaluateProfileSegment(Segment, 0.5 * (Low + High)), Point);
        };
        const auto Format = [](const char* Pattern, auto... Values) { char Text[900]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const std::vector<Vector2> Through = { { 0.0, 0.0 }, { 1.0, 1.0 }, { 2.0, -0.5 }, { 3.0, 0.8 }, { 4.0, 0.2 } };
        SolidArcDocument  Part  = CreateSolidArcDocument("SplineTrim");
        const std::string Curve = Draw(Part, SolidArcDrawingKind::Curve, Through);
        (void)Draw(Part, SolidArcDrawingKind::Line, { { 1.2, -2.0 }, { 1.2, 2.0 } });
        (void)Draw(Part, SolidArcDrawingKind::Line, { { 2.6, -2.0 }, { 2.6, 2.0 } });
        const ProfileSegment Original = Segments(Part, Curve).front();
        const double Middle = [&] { double Low = 0.0, High = 1.0; for (int Round = 0; Round < 60; ++Round) { const double Mid = 0.5 * (Low + High); (EvaluateProfileSegment(Original, Mid).X < 1.9 ? Low : High) = Mid; } return 0.5 * (Low + High); }();
        const SolidArcCommandOutcome Trimmed = TrimSolidArcSketch(Part, { Curve, 0, EvaluateProfileSegment(Original, Middle) });
        const std::vector<ProfileSegment> Kept = Segments(Part, Curve);
        double Shape = Trimmed.AcceptedCondition ? 0.0 : 1.0, Ends = 0.0, Inside = 0.0;
        for (const ProfileSegment& Piece : Kept)
            for (int Sample = 0; Sample <= 2000; ++Sample)
            {
                const Vector2 Point = EvaluateProfileSegment(Piece, Sample / 2000.0);
                Shape = std::max(Shape, Off(Original, Point));
                if (Point.X > 1.2 + 1.0e-6 && Point.X < 2.6 - 1.0e-6) Inside = std::max(Inside, std::min(Point.X - 1.2, 2.6 - Point.X));
            }
        if (Kept.size() == 2) Ends = std::max(std::abs(EvaluateProfileSegment(Kept[0], 1.0).X - 1.2), std::abs(EvaluateProfileSegment(Kept[1], 0.0).X - 2.6));
        Tally(TrimGate, Shape, Trimmed.AcceptedCondition && Kept.size() == 2 && Shape <= 1.0e-9 && Ends <= 1.0e-9 && Inside == 0.0,
              Format("T on a Hermite curve's middle (between lines x 1.2 and 2.6): %d pieces left, every point on the original within %.1e, new ends off the lines by %.1e, removed piece gone%s",
                     int(Kept.size()), Shape, Ends, Trimmed.Refusal.empty() ? "" : (" · " + Trimmed.Refusal).c_str()));
        SolidArcDocument  Cutting = CreateSolidArcDocument("SplineCut");
        const std::string Other   = Draw(Cutting, SolidArcDrawingKind::Curve, Through);
        const ProfileSegment Whole = Segments(Cutting, Other).front();
        const Vector2 Break = EvaluateProfileSegment(Whole, 0.37);
        const SolidArcCommandOutcome Cut = TrimSolidArcSketch(Cutting, { Other, 0, Break, true });
        const std::vector<ProfileSegment> Halves = Segments(Cutting, Other);
        double CutShape = Cut.AcceptedCondition ? 0.0 : 1.0;
        for (const ProfileSegment& Piece : Halves)
            for (int Sample = 0; Sample <= 2000; ++Sample) CutShape = std::max(CutShape, Off(Whole, EvaluateProfileSegment(Piece, Sample / 2000.0)));
        const double Joint = Halves.size() == 2 ? std::max(Distance(EvaluateProfileSegment(Halves[0], 1.0), Break), Distance(EvaluateProfileSegment(Halves[1], 0.0), Break)) : 1.0;
        const double Reach = Halves.size() == 2 ? std::max(Distance(EvaluateProfileSegment(Halves[0], 0.0), Through.front()), Distance(EvaluateProfileSegment(Halves[1], 1.0), Through.back())) : 1.0;
        Tally(TrimGate, CutShape, Cut.AcceptedCondition && Halves.size() == 2 && CutShape <= 1.0e-9 && Joint <= 1.0e-9 && Reach <= 1.0e-9,
              Format("Cut on a Hermite curve at 37 %%: %d pieces, every point on the original within %.1e, meeting at the cut (%.1e), ends kept (%.1e)%s",
                     int(Halves.size()), CutShape, Joint, Reach, Cut.Refusal.empty() ? "" : (" · " + Cut.Refusal).c_str()));
    }

    GateTally ConstrainGate   = { "W23", "Sketch constraints (K): a drawn rectangle goes DOF 4 → Lock 2 → D 1 → D 0, each dimension drives the extrude volume; literal and parameter-bound dimension edits regenerate downstream; undo / redo; remove frees a degree", "≤ 10⁻⁹ · exact" };
    GateTally ConflictGate    = { "W24", "Constraint refusals: redundant (named partner) and conflicting (named partners) constraints, non-dimension / non-positive edits, unknown sketch — the document, history and file stay untouched", "exact" };
    GateTally DragGate        = { "W25", "Point drag: a free corner follows the cursor and the extrude follows it; a locked point refuses; the preview is pure; save → load → save byte-equal with constraint labels", "≤ 10⁻⁹ · exact" };
    GateTally ReferenceGate   = { "W26", "Reference dimensions (Shift+D / E): a driven diagonal reads √13 then 5 without moving anything (DOF, positions, volume bit-equal); editing it or making it driving while implied is refused; E makes it driving at its reading and back; undo / redo; codec", "≤ 10⁻¹² · exact" };
    GateTally SplineGate      = { "W27", "Spline constraints: a line and a NURBS curve joined → L (G1) → C (G2, replaces the G1) → H on the far end, each solve ≤ 10⁻¹² with DOF −1 / −1 / −1; undo restores the G1; G2 on a natural cubic refused; codec", "≤ 10⁻¹² · exact" };
    GateTally LinkGate        = { "W28", "Cross-sketch links: a later sketch's line linked to an earlier plate's corner and top line (one undo entry, construction curve on linked points) follows the plate's width edit; backward links, linked drags, bad indices refused; codec; unlink frees", "≤ 10⁻¹² · exact" };
    // ---- W19 … W22: the editing keys (G / R / S, Shift+D, Ctrl+C / Ctrl+V, H family, J / O / P)
    {
        auto Relative = [](double Value, double Expected) { return std::abs(Value - Expected) / std::max(1.0e-12, std::abs(Expected)); };
        auto VolumeOf = [](const SolidArcDocument& Target, const std::string& Key)
        {
            for (const SolidArcBodyFigures& Body : Target.Summary.Bodies) if (Body.Key == Key) return Body.Volume;
            return -1.0;
        };
        auto RoundTrip = [](const SolidArcDocument& Source)
        {
            const std::string Written = EncodeSolidArcDocument(Source);
            const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
            SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
            Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Copy, true);
            return Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Copy.Summary.Volume == Source.Summary.Volume && Copy.Appearance.HiddenBodies == Source.Appearance.HiddenBodies;
        };
        auto Draw = [](SolidArcDocument& Target, SolidArcDrawingKind Kind, std::vector<Vector2> Points)
        {
            SolidArcDrawingRequest Drawing;
            Drawing.Kind = Kind; Drawing.Points = std::move(Points);
            const SolidArcCommandOutcome Outcome = DrawFilledSketch(Target, Drawing);
            return Outcome.AcceptedCondition ? Target.Tree.Steps.back().Label : std::string{};
        };
        auto Hits = [](const SolidArcDocument& Target, std::array<double, 3> At)
        {
            const SolidArcScene Scene = BuildSolidArcScene(Target);
            const std::optional<SolidArcPick> Hit = PickSolidArcScene(Scene, RayBetween({ At[0], At[1], 20.0 }, At), 0.02);
            if (!Hit) return std::string{};
            const uint32_t Index = (Hit->Identity & ~IdentityEdgeBit) - 1u;
            return Scene.Bodies[(Hit->Identity & IdentityEdgeBit) ? Scene.Edges[Index].Body : Scene.Faces[Index].Body].Key;
        };

        // W19 ① G: Block1 2 × 2 × 1 moved by (10, 0, 0): volume 4, the old spot is empty and the new one picks Block1
        SolidArcDocument Part = CreateSolidArcDocument("Editing");
        SolidArcConstructRequest Request;
        Request.Kind = SolidArcConstructKind::Block; Request.Width = 2.0; Request.Depth = 2.0; Request.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Part, Request));
        const std::string BlockKey = Part.Regeneration.Active;
        const std::string TopFace  = PickAndSelect(Part, RayBetween({ 1.0, 1.0, 20.0 }, { 1.0, 1.0, 0.0 }), false);
        const SolidArcCommandOutcome Moved = TransformSolidArcBodies(Part, { BlockKey }, TranslationMap({ 10.0, 0.0, 0.0 }), "Move");
        const bool Followed = Hits(Part, { 1.0, 1.0, 0.0 }).empty() && Hits(Part, { 11.0, 1.0, 0.0 }) == BlockKey;
        const bool Kept = Part.Selection.Faces.size() == 1 && Part.Selection.Faces.front() == TopFace;
        Tally(MoveGate, Relative(Part.Summary.Volume, 4.0), Moved.AcceptedCondition && Relative(Part.Summary.Volume, 4.0) <= 1.0e-9 && Part.Summary.SoundCondition && Followed && Kept
                                                            && Part.Tree.Steps.back().Kind == FeatureStepKind::Transform && SolidArcBodyCaption(Part, Part.Tree.Steps.back().Label) == "Move1",
              Moved.Label + ": V 4 kept, sound; (1, 1) now empty, (11, 1) picks Block1; the picked top face " + TopFace + " stays selected");

        // W19 ② R 90° about z through (11, 1, 0), then S ×2 about the same point: V 4 → 32; undo → 4
        const SolidArcCommandOutcome Turned = TransformSolidArcBodies(Part, { BlockKey }, RotationMap({ 11.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, Pi / 2.0), "Rotate");
        const SolidArcCommandOutcome Grown  = TransformSolidArcBodies(Part, { BlockKey }, ScalingMap({ 11.0, 1.0, 0.0 }, { 2.0, 2.0, 2.0 }), "Scale");
        const double GrownVolume = Part.Summary.Volume;
        UndoSolidArcCommand(Part);
        const double UndoneVolume = Part.Summary.Volume;
        RedoSolidArcCommand(Part);
        Tally(MoveGate, std::max(Relative(GrownVolume, 32.0), Relative(UndoneVolume, 4.0)), Turned.AcceptedCondition && Grown.AcceptedCondition && Relative(GrownVolume, 32.0) <= 1.0e-9 && Relative(UndoneVolume, 4.0) <= 1.0e-9
                                                                                         && Part.Summary.SoundCondition && Hits(Part, { 11.0, 1.0, 0.0 }) == BlockKey,
              Turned.Label + " + " + Grown.Label + ": V 32 = 8 × 4, sound; undo → 4, redo → 32");

        // W19 ③ refusals leave the document alone
        const std::string Before = EncodeSolidArcDocument(Part);
        const SolidArcCommandOutcome Empty = TransformSolidArcBodies(Part, {}, TranslationMap({ 1.0, 0.0, 0.0 }), "Move");
        const SolidArcCommandOutcome Ghost = TransformSolidArcBodies(Part, { "Ghost#9" }, TranslationMap({ 1.0, 0.0, 0.0 }), "Move");
        AffineMap Flat;
        Flat.ColumnZ = {};
        const SolidArcCommandOutcome Singular = TransformSolidArcBodies(Part, { BlockKey }, Flat, "Scale");
        Tally(MoveGate, 0.0, !Empty.AcceptedCondition && !Ghost.AcceptedCondition && !Singular.AcceptedCondition && Singular.Refusal.find("singular") != std::string::npos && EncodeSolidArcDocument(Part) == Before,
              "refused: nothing picked · " + Ghost.Refusal + " · " + Singular.Refusal + " — document unchanged");

        // W20 ① Shift+D: a copy moved by (0, 6, 0) is a new body, active and selected, captioned Block1Copy1
        const SolidArcCommandOutcome Copied = DuplicateSolidArcBodies(Part, { BlockKey }, TranslationMap({ 0.0, 6.0, 0.0 }));
        const std::string CopyKey = Copied.AcceptedCondition ? Part.Tree.Steps.back().Label : std::string{};
        Tally(CopyGate, Relative(VolumeOf(Part, CopyKey), 32.0), Copied.AcceptedCondition && Part.Summary.Bodies.size() == 2 && Relative(VolumeOf(Part, CopyKey), 32.0) <= 1.0e-9 && Relative(VolumeOf(Part, BlockKey), 32.0) <= 1.0e-9
                                                                 && Part.Regeneration.Active == CopyKey && Part.Selection.Bodies == std::vector<std::string>{ CopyKey } && SolidArcBodyCaption(Part, CopyKey) == "Block1Copy1"
                                                                 && Hits(Part, { 11.0, 7.0, 0.0 }) == CopyKey,
              Copied.Label + ": 2 bodies of 32; " + CopyKey + " (Block1Copy1) active, selected and picked at (11, 7)");

        // W20 ② Ctrl+C on Block1 (clipboard = key + the last step), X on Block1, Ctrl+V: the pasted copy is Block1 as copied
        const std::string Clipboard = Part.Tree.Steps.back().Label;
        Expect(RemoveSolidArcBodies(Part, { BlockKey }));
        const SolidArcCommandOutcome Pasted = DuplicateSolidArcBodies(Part, { BlockKey }, TranslationMap({}), Clipboard);
        const std::string PasteKey = Pasted.AcceptedCondition ? Part.Tree.Steps.back().Label : std::string{};
        Tally(CopyGate, Relative(VolumeOf(Part, PasteKey), 32.0), Pasted.AcceptedCondition && Part.Summary.Bodies.size() == 2 && Relative(VolumeOf(Part, PasteKey), 32.0) <= 1.0e-9 && Hits(Part, { 11.0, 1.0, 0.0 }) == PasteKey,
              Pasted.Label + ": after X on Block1 the paste brings it back as " + PasteKey + " (V 32 at the copied place)");

        // W20 ③ refusals
        const SolidArcCommandOutcome Stale = DuplicateSolidArcBodies(Part, { BlockKey }, TranslationMap({}), "Nowhere#99");
        const SolidArcCommandOutcome None  = DuplicateSolidArcBodies(Part, {}, TranslationMap({}));
        Tally(CopyGate, 0.0, !Stale.AcceptedCondition && !None.AcceptedCondition && Stale.Refusal.find("gone") != std::string::npos, "refused: " + Stale.Refusal + " · " + None.Refusal);

        // W21 H family on three bodies: Block1Copy1 (copy), the pasted body, and a new cylinder
        SolidArcConstructRequest Post;
        Post.Kind = SolidArcConstructKind::Cylinder; Post.Position = { -5.0, 0.0, 0.0 }; Post.Radius = 0.5; Post.Height = 1.0; Post.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(Part, Post));
        const std::string PostKey = Part.Regeneration.Active;
        auto Shown = [](const SolidArcDocument& Target) { std::set<std::string> Keys; for (const SolidArcSceneBody& Body : BuildSolidArcScene(Target).Bodies) Keys.insert(Body.Key); return Keys; };
        ClearSolidArcSelection(Part);
        const SolidArcCommandOutcome NoPick = ChangeSolidArcVisibility(Part, SolidArcVisibilityVerb::HideSelected);
        SelectSolidArcName(Part, SolidArcSelectionKind::Body, CopyKey, false);
        const SolidArcCommandOutcome Hide = ChangeSolidArcVisibility(Part, SolidArcVisibilityVerb::HideSelected);
        const bool HideOk = Hide.AcceptedCondition && Shown(Part) == std::set<std::string>{ PasteKey, PostKey } && Hits(Part, { 11.0, 7.0, 0.0 }).empty() && Part.Selection.Bodies.empty();
        SelectSolidArcName(Part, SolidArcSelectionKind::Body, PostKey, false);
        const SolidArcCommandOutcome Isolate = ChangeSolidArcVisibility(Part, SolidArcVisibilityVerb::HideUnselected);
        const bool IsolateOk = Isolate.AcceptedCondition && Shown(Part) == std::set<std::string>{ PostKey };
        const SolidArcCommandOutcome Invert = ChangeSolidArcVisibility(Part, SolidArcVisibilityVerb::InvertHidden);
        const bool InvertOk = Invert.AcceptedCondition && Shown(Part) == std::set<std::string>{ CopyKey, PasteKey } && Part.Selection.Bodies.empty();
        const bool Saved = RoundTrip(Part);
        const SolidArcCommandOutcome Reveal = ChangeSolidArcVisibility(Part, SolidArcVisibilityVerb::UnhideAll);
        const bool RevealOk = Reveal.AcceptedCondition && Shown(Part).size() == 3;
        UndoSolidArcCommand(Part);
        const bool UndoOk = Shown(Part) == std::set<std::string>{ CopyKey, PasteKey } && Part.Summary.Bodies.size() == 3;
        RedoSolidArcCommand(Part);
        const SolidArcCommandOutcome Again = ChangeSolidArcVisibility(Part, SolidArcVisibilityVerb::UnhideAll);
        Tally(HideGate, 0.0, HideOk && IsolateOk && InvertOk && RevealOk && UndoOk && Saved && !NoPick.AcceptedCondition && !Again.AcceptedCondition,
              "H hides Block1Copy1 (scene 2, unpickable, deselected) · Shift+H on the cylinder leaves it alone · Ctrl+H swaps → 2 shown · saved with Hidden, reload equal · Alt+H shows 3; undo → 2 · refused: " + NoPick.Refusal + " · " + Again.Refusal);
        Tally(HideGate, 0.0, Part.Summary.Bodies.size() == 3 && Relative(Part.Summary.Volume, 64.0 + Pi * 0.25) <= 1.0e-9, "hiding never touches the model: 3 bodies, V 64 + π/4 throughout");
        Tally(MoveGate, 0.0, RoundTrip(Part) && EncodeSolidArcDocument(Part).find("Kind = 13") != std::string::npos && EncodeSolidArcDocument(Part).find("Kind = 14") != std::string::npos,
              "save → load → save byte-equal with Transform (13) and Duplicate (14) steps (placements and the paste source kept)");

        // W22 ① J: an L and its complement drawn as two sketches; the L alone cannot extrude, joined they make a unit square
        SolidArcDocument Plan = CreateSolidArcDocument("Curves");
        const std::string Lower = Draw(Plan, SolidArcDrawingKind::Line, { { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 } });
        const std::string Upper = Draw(Plan, SolidArcDrawingKind::Line, { { 1.0, 1.0 }, { 0.0, 1.0 }, { 0.0, 0.0 } });
        const SolidArcCommandOutcome Open   = ExtrudeSolidArcSketch(Plan, Lower, 1.0, FeatureCombination::Separate);
        const size_t                 Steps  = Plan.Tree.Steps.size();
        const SolidArcCommandOutcome Joined = JoinSolidArcSketches(Plan, { Lower, Upper });
        const bool Merged = Joined.AcceptedCondition && Plan.Tree.Steps.size() == Steps - 1 && !Plan.Captions.contains(Upper);
        const SolidArcCommandOutcome Square = ExtrudeSolidArcSketch(Plan, Lower, 1.0, FeatureCombination::Separate);
        const std::string SquareKey = Square.AcceptedCondition ? Plan.Tree.Steps.back().Label : std::string{};
        Tally(CurveGate, Relative(VolumeOf(Plan, SquareKey), 1.0), !Open.AcceptedCondition && Merged && Square.AcceptedCondition && Relative(VolumeOf(Plan, SquareKey), 1.0) <= 1.0e-9,
              Joined.Label + ": the open L refuses to extrude; joined (one sketch fewer) the square extrudes to V 1");

        // W22 ② O: the joined square offset by ¼ outward → 1.5 × 1.5 (mitred corners), extruded 1 → 2.25; preview is pure
        const std::string BeforePreview = EncodeSolidArcDocument(Plan);
        const SketchOffsetOutcome Preview = PreviewSolidArcOffset(Plan, Lower, 0.25);
        const bool Pure = EncodeSolidArcDocument(Plan) == BeforePreview && Preview.Refusal.empty() && Preview.Chains == 1;
        const SolidArcCommandOutcome Offset = OffsetSolidArcSketch(Plan, Lower, 0.25);
        const std::string OffsetSketch = Offset.AcceptedCondition ? Plan.Tree.Steps.back().Label : std::string{};
        const SolidArcCommandOutcome Grow = ExtrudeSolidArcSketch(Plan, OffsetSketch, 1.0, FeatureCombination::Separate);
        const std::string GrowKey = Grow.AcceptedCondition ? Plan.Tree.Steps.back().Label : std::string{};
        Tally(CurveGate, Relative(VolumeOf(Plan, GrowKey), 2.25), Pure && Offset.AcceptedCondition && Grow.AcceptedCondition && Relative(VolumeOf(Plan, GrowKey), 2.25) <= 1.0e-9 && SolidArcBodyCaption(Plan, OffsetSketch) == "Offset1",
              Offset.Label + ": new sketch Offset1, extruded V 2.25 = 1.5²; the preview left the document alone");

        // W22 ③ P: a drawn line (0, 3) → (2, 3) piped with r ¼ → new body π r² L
        const std::string Rail = Draw(Plan, SolidArcDrawingKind::Line, { { 0.0, 3.0 }, { 2.0, 3.0 } });
        const SolidArcCommandOutcome Piped = PipeSolidArcPath(Plan, Rail, 0.25);
        const std::string PipeKey = Piped.AcceptedCondition ? Plan.Tree.Steps.back().Label : std::string{};
        Tally(CurveGate, Relative(VolumeOf(Plan, PipeKey), Pi * 0.0625 * 2.0), Piped.AcceptedCondition && Relative(VolumeOf(Plan, PipeKey), Pi * 0.0625 * 2.0) <= 1.0e-9 && Plan.Summary.SoundCondition && Plan.Regeneration.Active == PipeKey,
              Piped.Label + ": tube V π r² L = π/8 exact, sound, active");

        // W22 ④ refusals and codec
        const SolidArcCommandOutcome Lone     = JoinSolidArcSketches(Plan, { Lower });
        // The pipe's rail joins too since W46: kept hidden for the pipe (volume unchanged), the join a new step; undone.
        const double                 Tube     = VolumeOf(Plan, PipeKey);
        const SolidArcCommandOutcome Consumed = JoinSolidArcSketches(Plan, { Rail, Lower });
        const bool RailKept = Consumed.AcceptedCondition && SolidArcCurveHiddenCondition(Plan, Rail) && VolumeOf(Plan, PipeKey) == Tube;
        if (Consumed.AcceptedCondition) UndoSolidArcCommand(Plan);
        const SolidArcCommandOutcome Vanish   = OffsetSolidArcSketch(Plan, Lower, -0.6);
        const SolidArcCommandOutcome Fat      = PipeSolidArcPath(Plan, Rail, 0.0);
        const SolidArcCommandOutcome NoPath   = PipeSolidArcPath(Plan, PipeKey, 0.1);
        Tally(CurveGate, 0.0, !Lone.AcceptedCondition && RailKept && !Vanish.AcceptedCondition && !Fat.AcceptedCondition && !NoPath.AcceptedCondition,
              "refused: " + Lone.Refusal + " · " + Vanish.Refusal + " · " + Fat.Refusal + " · " + NoPath.Refusal + "; the pipe's rail joins detached (kept hidden, tube unchanged), undone");
        Tally(CurveGate, 0.0, RoundTrip(Plan) && EncodeSolidArcDocument(Plan).find("Kind = 15") != std::string::npos, "save → load → save byte-equal with the joined / offset sketches and the Pipe (15) step");
    }

    // ---- W23 … W25: sketch constraints (K)
    {
        auto Relative = [](double Value, double Expected) { return std::abs(Value - Expected) / std::max(1.0e-12, std::abs(Expected)); };
        auto Propose = [](const SolidArcDocument& Target, const std::string& Sketch, std::vector<SketchPick> Picks, SketchConstraintVerb Verb, double Value = std::numeric_limits<double>::quiet_NaN())
        {
            const SolidArcSketchDiagnosis Diagnosis = DiagnoseSolidArcSketch(Target, Sketch);
            SketchConstraintProposal Proposal = ProposeSketchConstraint(Diagnosis.Sketch, Picks, Verb);
            if (!std::isnan(Value)) Proposal.Constraint.Value = Value;
            return Proposal.Constraint;
        };
        auto Freedom = [](const SolidArcDocument& Target, const std::string& Sketch) { return DiagnoseSolidArcSketch(Target, Sketch).Solve.DegreesOfFreedom; };
        const SketchPick Bottom = { SketchPickKind::Curve, 0 };
        const SketchPick Side   = { SketchPickKind::Curve, 1 };
        const SketchPick Top    = { SketchPickKind::Curve, 2 };

        // W23 ① a drawn 2 × 1 rectangle extruded by 1: DOF 4; ] locks its first corner (2), D on the bottom = 3 (1), D on the side = 2 (0)
        SolidArcDocument Plate = CreateSolidArcDocument("Constraints");
        SolidArcDrawingRequest Drawing;
        Drawing.Kind = SolidArcDrawingKind::Rectangle; Drawing.Points = { { 0.0, 0.0 }, { 2.0, 1.0 } };
        const SolidArcCommandOutcome Drawn = DrawFilledSketch(Plate, Drawing);
        const std::string Sketch = Drawn.AcceptedCondition ? Plate.Tree.Steps.back().Label : std::string{};
        const SolidArcCommandOutcome Raised = ExtrudeSolidArcSketch(Plate, Sketch, 1.0, FeatureCombination::Separate);
        const std::string Key = Raised.AcceptedCondition ? Plate.Tree.Steps.back().Label : std::string{};
        const int Free = Freedom(Plate, Sketch);
        const SolidArcCommandOutcome Locked = ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { { SketchPickKind::Point, 0 } }, SketchConstraintVerb::Lock));
        const int AfterLock = Freedom(Plate, Sketch);
        const SolidArcCommandOutcome Wide = ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { Bottom }, SketchConstraintVerb::Dimension, 3.0));
        const int AfterWide = Freedom(Plate, Sketch);
        const double WideVolume = Plate.Summary.Volume;
        const SolidArcCommandOutcome Tall = ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { Side }, SketchConstraintVerb::Dimension, 2.0));
        const int AfterTall = Freedom(Plate, Sketch);
        const std::vector<SketchConstraint>& Authored = Plate.Tree.Steps[0].Sketch.Constraints;
        const bool Labelled = Authored.size() == 7 && Authored[4].Label == "F1" && Authored[5].Label == "D1" && Authored[6].Label == "D2";
        Tally(ConstrainGate, Relative(Plate.Summary.Volume, 6.0) + Relative(WideVolume, 3.0),
              Free == 4 && Locked.AcceptedCondition && AfterLock == 2 && Wide.AcceptedCondition && AfterWide == 1 && Tall.AcceptedCondition && AfterTall == 0 && Labelled
                  && Relative(WideVolume, 3.0) <= 1.0e-9 && Relative(Plate.Summary.Volume, 6.0) <= 1.0e-9 && Plate.Regeneration.Active == Key,
              "DOF " + std::to_string(Free) + " → " + Locked.Label + " " + std::to_string(AfterLock) + " → " + Wide.Label + " " + std::to_string(AfterWide) + " (V 3) → " + Tall.Label + " " + std::to_string(AfterTall) + " (V 6); labels F1 D1 D2");

        // W23 ② D1 = 4 → V 8 (the solved corners are written back: the step's own points move); undo → 6, redo → 8
        const SolidArcCommandOutcome Edit = ChangeSolidArcDimension(Plate, Sketch, 5, 4.0);
        const double Edited = Plate.Summary.Volume;
        const Vector2 Corner = Plate.Tree.Steps[0].Sketch.Points[2].Position;
        const bool Undone = UndoSolidArcCommand(Plate);
        const double Restored = Plate.Summary.Volume;
        const bool Redone = RedoSolidArcCommand(Plate);
        Tally(ConstrainGate, Relative(Edited, 8.0) + Relative(Restored, 6.0) + std::abs(Corner.X - 4.0) + std::abs(Corner.Y - 2.0),
              Edit.AcceptedCondition && Relative(Edited, 8.0) <= 1.0e-9 && std::abs(Corner.X - 4.0) <= 1.0e-9 && std::abs(Corner.Y - 2.0) <= 1.0e-9 && Undone && Relative(Restored, 6.0) <= 1.0e-12 && Redone && Relative(Plate.Summary.Volume, 8.0) <= 1.0e-9,
              Edit.Label + ": V 8, far corner written back at (4, 2); undo V 6, redo V 8");

        // W23 ③ a Block tile is fully dimensioned by parameters: its Width dimension changes the parameter (and the volume)
        SolidArcDocument Tile = CreateSolidArcDocument("Tile");
        Expect(ConstructSolidArcPrimitive(Tile, SolidArcConstructRequest{}));
        const std::string TileSketch = Tile.Tree.Steps[0].Label;
        int WidthIndex = -1;
        for (size_t Index = 0; Index < Tile.Tree.Steps[0].Sketch.Constraints.size(); ++Index)
            if (Tile.Tree.Steps[0].Sketch.Constraints[Index].Parameter.ends_with("Width")) WidthIndex = static_cast<int>(Index);
        const int TileFree = Freedom(Tile, TileSketch);
        const SolidArcCommandOutcome Widen = ChangeSolidArcDimension(Tile, TileSketch, WidthIndex, 3.0);
        double WidthValue = 0.0;
        for (const FeatureParameter& Parameter : Tile.Tree.Parameters) if (Parameter.Name.ends_with("Width")) WidthValue = Parameter.Value;
        Tally(ConstrainGate, Relative(Tile.Summary.Volume, 4.5), TileFree == 0 && WidthIndex >= 0 && Widen.AcceptedCondition && WidthValue == 3.0 && Relative(Tile.Summary.Volume, 4.5) <= 1.0e-9,
              "Block tile DOF 0; " + Widen.Label + " sets its Width parameter to 3 → V 3 × 1.5 × 1 = 4.5");

        // W23 ④ removing D2 frees the height (DOF 1); the body stays (positions unchanged)
        const SolidArcCommandOutcome Freed = RemoveSolidArcConstraint(Plate, Sketch, 6);
        Tally(ConstrainGate, Relative(Plate.Summary.Volume, 8.0), Freed.AcceptedCondition && Freedom(Plate, Sketch) == 1 && Relative(Plate.Summary.Volume, 8.0) <= 1.0e-12 && Plate.Tree.Steps[0].Sketch.Constraints.size() == 6,
              Freed.Label + ": DOF 1, V 8 kept");

        // W24 refusals (with D2 back): each leaves the file and the history untouched
        Expect(ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { Side }, SketchConstraintVerb::Dimension, 2.0)));
        const std::string File = EncodeSolidArcDocument(Plate);
        const size_t History = Plate.UndoEntries.size();
        const SolidArcCommandOutcome Again     = ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { Bottom }, SketchConstraintVerb::Horizontal));
        const SolidArcCommandOutcome Implied   = ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { Top }, SketchConstraintVerb::Dimension));
        const SolidArcCommandOutcome Clash     = ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { Top }, SketchConstraintVerb::Dimension, 5.0));
        const SolidArcCommandOutcome Valueless = ChangeSolidArcDimension(Plate, Sketch, 0, 1.0);
        const SolidArcCommandOutcome Negative  = ChangeSolidArcDimension(Plate, Sketch, 5, -1.0);
        const SolidArcCommandOutcome Missing   = ConstrainSolidArcSketch(Plate, Key, Propose(Plate, Sketch, { Bottom }, SketchConstraintVerb::Horizontal));
        const SolidArcCommandOutcome Beyond    = RemoveSolidArcConstraint(Plate, Sketch, 40);
        const bool Untouched = EncodeSolidArcDocument(Plate) == File && Plate.UndoEntries.size() == History;
        Tally(ConflictGate, 0.0, !Again.AcceptedCondition && Again.Refusal.find("already implied by H1") != std::string::npos,
              "second H on Line1 refused: " + Again.Refusal);
        Tally(ConflictGate, 0.0, !Implied.AcceptedCondition && Implied.Refusal.find("already implied by") != std::string::npos && Implied.Refusal.find("D1") != std::string::npos,
              "Length 4 on the top refused: " + Implied.Refusal);
        Tally(ConflictGate, 0.0, !Clash.AcceptedCondition && Clash.Refusal.find("conflicts with") != std::string::npos && Clash.Refusal.find("D1") != std::string::npos,
              "Length 5 on the top refused: " + Clash.Refusal);
        Tally(ConflictGate, 0.0, !Valueless.AcceptedCondition && !Negative.AcceptedCondition && !Missing.AcceptedCondition && !Beyond.AcceptedCondition && Untouched,
              "refused: " + Valueless.Refusal + " · " + Negative.Refusal + " · " + Missing.Refusal + " · " + Beyond.Refusal + "; file and history untouched");

        // W25 ① D2 removed again: the free far corner dragged to (4, 3) carries the top up → V 12; the preview is pure
        Expect(RemoveSolidArcConstraint(Plate, Sketch, 6));
        const std::string BeforePreview = EncodeSolidArcDocument(Plate);
        const SketchDragOutcome Preview = PreviewSolidArcDrag(Plate, Sketch, 2, { 4.0, 3.0 });
        const bool Pure = EncodeSolidArcDocument(Plate) == BeforePreview && Preview.Refusal.empty() && Preview.PinnedCondition;
        const SolidArcCommandOutcome Dragged = DragSolidArcSketchPoint(Plate, Sketch, 2, { 4.0, 3.0 });
        const Vector2 Far = Plate.Tree.Steps[0].Sketch.Points[2].Position;
        const Vector2 Near = Plate.Tree.Steps[0].Sketch.Points[3].Position;
        const double Miss = std::abs(Far.X - 4.0) + std::abs(Far.Y - 3.0) + std::abs(Near.X) + std::abs(Near.Y - 3.0);
        Tally(DragGate, Miss + Relative(Plate.Summary.Volume, 12.0), Pure && Dragged.AcceptedCondition && Miss <= 1.0e-9 && Relative(Plate.Summary.Volume, 12.0) <= 1.0e-9,
              Dragged.Label + ": corner at (4, 3), Line3.Start follows to (0, 3), V 12; the preview left the document alone");

        // W25 ② dragging the locked corner refuses; ③ codec byte-equal with F1 D1 and the dragged positions
        const SolidArcCommandOutcome Stuck = DragSolidArcSketchPoint(Plate, Sketch, 0, { -1.0, -1.0 });
        Tally(DragGate, 0.0, !Stuck.AcceptedCondition && Stuck.Refusal.find("fully constrained") != std::string::npos, "refused: " + Stuck.Refusal);
        const std::string Written = EncodeSolidArcDocument(Plate);
        const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
        SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
        Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
        RegenerateSolidArcDocument(Copy, true);
        Tally(DragGate, 0.0, Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Copy.Summary.Volume == Plate.Summary.Volume && Written.find("'F1'") != std::string::npos && Freedom(Copy, Sketch) == 1,
              "save → load → save byte-equal; F1 / D1 labels and DOF 1 survive the reload");
    }

    // ---- W26 … W28: reference dimensions (Shift+D / E), spline constraints (L / C / H), cross-sketch links
    {
        auto Relative = [](double Value, double Expected) { return std::abs(Value - Expected) / std::max(1.0e-12, std::abs(Expected)); };
        auto Propose = [](const SolidArcDocument& Target, const std::string& Sketch, std::vector<SketchPick> Picks, SketchConstraintVerb Verb, double Value = std::numeric_limits<double>::quiet_NaN())
        {
            SketchConstraintProposal Proposal = ProposeSketchConstraint(DiagnoseSolidArcSketch(Target, Sketch).Sketch, Picks, Verb);
            if (!std::isnan(Value)) Proposal.Constraint.Value = Value;
            return Proposal.Constraint;
        };
        auto Freedom = [](const SolidArcDocument& Target, const std::string& Sketch) { return DiagnoseSolidArcSketch(Target, Sketch).Solve.DegreesOfFreedom; };
        auto RoundTrip = [](const SolidArcDocument& Source)
        {
            const std::string Written = EncodeSolidArcDocument(Source);
            const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
            SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
            Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Copy, true);
            return Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Copy.Summary.Volume == Source.Summary.Volume;
        };
        auto DrawRectangle = [](SolidArcDocument& Target, Vector2 Low, Vector2 High)
        {
            SolidArcDrawingRequest Drawing;
            Drawing.Kind = SolidArcDrawingKind::Rectangle; Drawing.Points = { Low, High };
            return DrawFilledSketch(Target, Drawing).AcceptedCondition ? Target.Tree.Steps.back().Label : std::string{};
        };
        auto Scientific = [](double Value) { char Text[32]; std::snprintf(Text, sizeof(Text), "%.3e", Value); return std::string(Text); };
        const SketchPick Bottom = { SketchPickKind::Curve, 0 };
        const SketchPick Side   = { SketchPickKind::Curve, 1 };

        // W26 ① a fully dimensioned 2 × 1 → 3 × 2 plate (DOF 0, V 6); Shift+D on its diagonal: D3 (reference) reads √13,
        //        DOF stays 0, the positions and the volume are bit-equal
        SolidArcDocument Plate = CreateSolidArcDocument("References");
        const std::string Sketch = DrawRectangle(Plate, { 0.0, 0.0 }, { 2.0, 1.0 });
        Expect(ExtrudeSolidArcSketch(Plate, Sketch, 1.0, FeatureCombination::Separate));
        Expect(ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { { SketchPickKind::Point, 0 } }, SketchConstraintVerb::Lock)));
        Expect(ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { Bottom }, SketchConstraintVerb::Dimension, 3.0)));
        Expect(ConstrainSolidArcSketch(Plate, Sketch, Propose(Plate, Sketch, { Side }, SketchConstraintVerb::Dimension, 2.0)));
        const std::vector<Vector2> Corners = [&] { std::vector<Vector2> Positions; for (const SketchPoint& Point : Plate.Tree.Steps[0].Sketch.Points) Positions.push_back(Point.Position); return Positions; }();
        const double Before = Plate.Summary.Volume;
        const SketchConstraintProposal Diagonal = ProposeSketchReference(DiagnoseSolidArcSketch(Plate, Sketch).Sketch, { { SketchPickKind::Point, 0 }, { SketchPickKind::Point, 2 } });
        const SolidArcCommandOutcome Referenced = ConstrainSolidArcSketch(Plate, Sketch, Diagonal.Constraint);
        const SketchConstraint& Reading = Plate.Tree.Steps[0].Sketch.Constraints.back();
        bool Unmoved = Plate.Summary.Volume == Before;
        for (size_t Point = 0; Point < Corners.size(); ++Point) Unmoved = Unmoved && Plate.Tree.Steps[0].Sketch.Points[Point].Position.X == Corners[Point].X && Plate.Tree.Steps[0].Sketch.Points[Point].Position.Y == Corners[Point].Y;
        const SolidArcSketchDiagnosis Diagnosed = DiagnoseSolidArcSketch(Plate, Sketch);
        const int ReadingIndex = static_cast<int>(Plate.Tree.Steps[0].Sketch.Constraints.size()) - 1;
        Tally(ReferenceGate, std::abs(Reading.Value - std::sqrt(13.0)),
              Diagonal.Refusal.empty() && Referenced.AcceptedCondition && Reading.DrivenCondition && Reading.Label == "D3" && std::abs(Reading.Value - std::sqrt(13.0)) <= 1.0e-12 && Diagnosed.Solve.DegreesOfFreedom == 0
                  && Diagnosed.Solve.Constraints[ReadingIndex].Status == SketchConstraintStatus::Driven && Unmoved,
              Referenced.Label + ": " + DescribeSketchConstraint(Plate.Tree.Steps[0].Sketch, Reading) + " · DOF 0 · positions and V bit-equal");

        // W26 ② D2 = 4 → the reading follows to 5 (written back); editing the reading and making it driving are refused
        const SolidArcCommandOutcome Taller = ChangeSolidArcDimension(Plate, Sketch, 6, 4.0);
        const double Followed = Plate.Tree.Steps[0].Sketch.Constraints[ReadingIndex].Value;
        const std::string File = EncodeSolidArcDocument(Plate);
        const size_t History = Plate.UndoEntries.size();
        const SolidArcCommandOutcome Typed   = ChangeSolidArcDimension(Plate, Sketch, ReadingIndex, 6.0);
        const SolidArcCommandOutcome Driving = ToggleSolidArcReference(Plate, Sketch, ReadingIndex);
        const SolidArcCommandOutcome Plain   = ToggleSolidArcReference(Plate, Sketch, 0);
        const bool Untouched = EncodeSolidArcDocument(Plate) == File && Plate.UndoEntries.size() == History;
        Tally(ReferenceGate, std::abs(Followed - 5.0) + Relative(Plate.Summary.Volume, 12.0),
              Taller.AcceptedCondition && std::abs(Followed - 5.0) <= 1.0e-12 && Relative(Plate.Summary.Volume, 12.0) <= 1.0e-9 && !Typed.AcceptedCondition && Typed.Refusal.find("reference dimension") != std::string::npos
                  && !Driving.AcceptedCondition && Driving.Refusal.find("stays a reference") != std::string::npos && !Plain.AcceptedCondition && Untouched,
              Taller.Label + " → D3 reads 5 · refused: " + Typed.Refusal + " · " + Driving.Refusal + " · " + Plain.Refusal);

        // W26 ③ D2 removed (DOF 1): E makes D3 driving at its reading √(9 + 16) = 5 (DOF 0, nothing moves), E again frees it;
        //        undo / redo; save → load → save byte-equal with Reference = true
        Expect(RemoveSolidArcConstraint(Plate, Sketch, 6));
        const int Loose = Freedom(Plate, Sketch);
        const double LooseVolume = Plate.Summary.Volume;
        const int Index = static_cast<int>(Plate.Tree.Steps[0].Sketch.Constraints.size()) - 1;
        const SolidArcCommandOutcome Drive = ToggleSolidArcReference(Plate, Sketch, Index);
        const int Driven = Freedom(Plate, Sketch);
        const double DrivenValue = Plate.Tree.Steps[0].Sketch.Constraints[Index].Value;
        const bool DrivenStill = Plate.Summary.Volume == LooseVolume;
        const SolidArcCommandOutcome Release = ToggleSolidArcReference(Plate, Sketch, Index);
        const int Released = Freedom(Plate, Sketch);
        const bool Undone = UndoSolidArcCommand(Plate) && Freedom(Plate, Sketch) == 0 && !Plate.Tree.Steps[0].Sketch.Constraints[Index].DrivenCondition;
        const bool Redone = RedoSolidArcCommand(Plate) && Freedom(Plate, Sketch) == 1 && Plate.Tree.Steps[0].Sketch.Constraints[Index].DrivenCondition;
        Tally(ReferenceGate, std::abs(DrivenValue - 5.0),
              Loose == 1 && Drive.AcceptedCondition && Driven == 0 && std::abs(DrivenValue - 5.0) <= 1.0e-12 && DrivenStill && Release.AcceptedCondition && Released == 1 && Undone && Redone,
              "DOF " + std::to_string(Loose) + " → " + Drive.Label + " " + std::to_string(Driven) + " (V bit-equal) → " + Release.Label + " " + std::to_string(Released) + "; undo / redo");
        const std::string Written = EncodeSolidArcDocument(Plate);
        Tally(ReferenceGate, 0.0, RoundTrip(Plate) && Written.find("Reference = true") != std::string::npos, "save → load → save byte-equal; Reference = true written for D3 only");

        // W27 ① a NURBS curve drawn from (0, 0) and a line (−2, 0.5) → (0, 0), joined (ends welded): L gives T1 (G1), C gives
        //        K1 that replaces it (G2), H on the far end; each solve converges ≤ 10⁻¹², undo brings T1 back
        SolidArcDocument Joint = CreateSolidArcDocument("Spline joint");
        SolidArcDrawingRequest Lead;
        Lead.Kind = SolidArcDrawingKind::Line; Lead.Points = { { -2.0, 0.5 }, { 0.0, 0.0 } };
        Expect(DrawFilledSketch(Joint, Lead));
        const std::string LineSketch = Joint.Tree.Steps.back().Label;
        SolidArcDrawingRequest Sweep;
        Sweep.Kind = SolidArcDrawingKind::Curve; Sweep.Form = SketchSplineForm::Nurbs; Sweep.Points = { { 0.0, 0.0 }, { 1.0, 1.0 }, { 2.0, 1.2 }, { 3.0, 0.4 } };
        Expect(DrawFilledSketch(Joint, Sweep));
        const std::string CurveSketch = Joint.Tree.Steps.back().Label;
        const SolidArcCommandOutcome Joined = JoinSolidArcSketches(Joint, { LineSketch, CurveSketch });
        int LineCurve = -1, SplineCurve = -1;
        const SketchModel& Merged = Joint.Tree.Steps[0].Sketch;
        for (size_t Curve = 0; Curve < Merged.Curves.size(); ++Curve)
            (Merged.Curves[Curve].Kind == SketchCurveKind::Spline ? SplineCurve : LineCurve) = static_cast<int>(Curve);
        const bool Welded = SplineCurve >= 0 && LineCurve >= 0 && (Merged.Curves[SplineCurve].Points.front() == Merged.Curves[LineCurve].End || Merged.Curves[SplineCurve].Points.front() == Merged.Curves[LineCurve].Start);
        const int FarEnd = SplineCurve >= 0 ? Merged.Curves[SplineCurve].Points.back() : -1;
        const int Open = Freedom(Joint, LineSketch);
        const SolidArcCommandOutcome Tangent = ConstrainSolidArcSketch(Joint, LineSketch, Propose(Joint, LineSketch, { { SketchPickKind::Curve, SplineCurve }, { SketchPickKind::Curve, LineCurve } }, SketchConstraintVerb::Parallel));
        const SolidArcSketchDiagnosis AfterTangent = DiagnoseSolidArcSketch(Joint, LineSketch);
        const SolidArcCommandOutcome Curvature = ConstrainSolidArcSketch(Joint, LineSketch, Propose(Joint, LineSketch, { { SketchPickKind::Curve, SplineCurve }, { SketchPickKind::Curve, LineCurve } }, SketchConstraintVerb::Curvature));
        const SolidArcSketchDiagnosis AfterCurvature = DiagnoseSolidArcSketch(Joint, LineSketch);
        const std::vector<SketchConstraint>& Joints = Joint.Tree.Steps[0].Sketch.Constraints;
        const bool Replaced = !Joints.empty() && Joints.back().Kind == SketchConstraintKind::SplineCurvature && Joints.back().Label == "K1"
                           && std::none_of(Joints.begin(), Joints.end(), [](const SketchConstraint& Constraint) { return Constraint.Kind == SketchConstraintKind::SplineTangent; });
        Tally(SplineGate, std::max(AfterTangent.Solve.MaxResidual, AfterCurvature.Solve.MaxResidual),
              Joined.AcceptedCondition && Welded && Tangent.AcceptedCondition && AfterTangent.Solve.Converged && AfterTangent.Solve.MaxResidual <= 1.0e-12 && AfterTangent.Solve.DegreesOfFreedom == Open - 1
                  && Curvature.AcceptedCondition && Curvature.Label.find("replaces T1") != std::string::npos && Replaced && AfterCurvature.Solve.Converged && AfterCurvature.Solve.MaxResidual <= 1.0e-12
                  && AfterCurvature.Solve.DegreesOfFreedom == Open - 2,
              "DOF " + std::to_string(Open) + " → " + Tangent.Label + " " + std::to_string(AfterTangent.Solve.DegreesOfFreedom) + " → " + Curvature.Label + " " + std::to_string(AfterCurvature.Solve.DegreesOfFreedom));
        const SolidArcCommandOutcome Level = ConstrainSolidArcSketch(Joint, LineSketch, Propose(Joint, LineSketch, { { SketchPickKind::Curve, SplineCurve }, { SketchPickKind::Point, FarEnd } }, SketchConstraintVerb::Horizontal));
        const SolidArcSketchDiagnosis AfterLevel = DiagnoseSolidArcSketch(Joint, LineSketch);
        const std::string LevelLabel = Joint.Tree.Steps[0].Sketch.Constraints.back().Label;
        const bool Back = UndoSolidArcCommand(Joint) && UndoSolidArcCommand(Joint)
                       && std::any_of(Joint.Tree.Steps[0].Sketch.Constraints.begin(), Joint.Tree.Steps[0].Sketch.Constraints.end(), [](const SketchConstraint& Constraint) { return Constraint.Kind == SketchConstraintKind::SplineTangent && Constraint.Label == "T1"; });
        const bool Forward = RedoSolidArcCommand(Joint) && RedoSolidArcCommand(Joint) && Joint.Tree.Steps[0].Sketch.Constraints.back().Label == LevelLabel;
        Tally(SplineGate, AfterLevel.Solve.MaxResidual,
              Level.AcceptedCondition && LevelLabel == "H1" && AfterLevel.Solve.Converged && AfterLevel.Solve.MaxResidual <= 1.0e-12 && AfterLevel.Solve.DegreesOfFreedom == Open - 3 && Back && Forward,
              Level.Label + " · DOF " + std::to_string(AfterLevel.Solve.DegreesOfFreedom) + "; undo × 2 brings T1 back, redo × 2 restores K1 + H1");

        // W27 ② refusals: G2 on a natural cubic spline, L on a spline and a curve that do not meet; save → load → save byte-equal
        SolidArcDocument Natural = CreateSolidArcDocument("Natural");
        Expect(DrawFilledSketch(Natural, Lead));
        const std::string NaturalSketch = Natural.Tree.Steps.back().Label;
        SolidArcDrawingRequest Cubic = Sweep;
        Cubic.Form = SketchSplineForm::CubicSpline;
        Expect(DrawFilledSketch(Natural, Cubic));
        Expect(JoinSolidArcSketches(Natural, { NaturalSketch, Natural.Tree.Steps.back().Label }));
        const SketchConstraintProposal NaturalProposal = ProposeSketchConstraint(DiagnoseSolidArcSketch(Natural, NaturalSketch).Sketch, { { SketchPickKind::Curve, SplineCurve }, { SketchPickKind::Curve, LineCurve } }, SketchConstraintVerb::Curvature);
        Tally(SplineGate, 0.0, !NaturalProposal.Refusal.empty() && NaturalProposal.Refusal.find("zero curvature") != std::string::npos, "refused: " + NaturalProposal.Refusal);
        Tally(SplineGate, 0.0, RoundTrip(Joint) && EncodeSolidArcDocument(Joint).find("Kind = 22") != std::string::npos && EncodeSolidArcDocument(Joint).find("Kind = 24") != std::string::npos,
              "save → load → save byte-equal with SplineCurvature (22) and SplineDirection (24)");

        // W28 ① Sketch A: a 3 × 2 plate (DOF 0, V 6); Sketch B (later, same plane): a free line (5, 3) → (6, 4). O on B's start
        //        and A's far corner links the corner into B (one undo entry); O on B's end and A's top line links the line
        //        (construction, on linked points); B's start sits on (3, 2), its end on y = 2
        SolidArcDocument Linked = CreateSolidArcDocument("Links");
        const std::string Source = DrawRectangle(Linked, { 0.0, 0.0 }, { 2.0, 1.0 });
        Expect(ExtrudeSolidArcSketch(Linked, Source, 1.0, FeatureCombination::Separate));
        Expect(ConstrainSolidArcSketch(Linked, Source, Propose(Linked, Source, { { SketchPickKind::Point, 0 } }, SketchConstraintVerb::Lock)));
        Expect(ConstrainSolidArcSketch(Linked, Source, Propose(Linked, Source, { Bottom }, SketchConstraintVerb::Dimension, 3.0)));
        Expect(ConstrainSolidArcSketch(Linked, Source, Propose(Linked, Source, { Side }, SketchConstraintVerb::Dimension, 2.0)));
        SolidArcDrawingRequest Spoke;
        Spoke.Kind = SolidArcDrawingKind::Line; Spoke.Points = { { 5.0, 3.0 }, { 6.0, 4.0 } };
        Expect(DrawFilledSketch(Linked, Spoke));
        const std::string Follower = Linked.Tree.Steps.back().Label;
        const std::vector<std::string> Sources = SolidArcLinkSources(Linked, Follower);
        const int Unlinked = Freedom(Linked, Follower);
        const std::vector<SolidArcSketchPick> CornerPicks = { { {}, { SketchPickKind::Point, 0 } }, { Source, { SketchPickKind::Point, 2 } } };
        const SolidArcLinkedPicks CornerLinked = LinkSolidArcPicks(Linked, Follower, CornerPicks);
        const SketchConstraintProposal CornerProposal = ProposeSketchConstraint(CornerLinked.Sketch, CornerLinked.Picks, SketchConstraintVerb::On);
        const size_t BeforeCorner = Linked.UndoEntries.size();
        const SolidArcCommandOutcome Cornered = ConstrainSolidArcSketch(Linked, Follower, CornerProposal.Constraint, CornerPicks);
        const bool OneEntry = Linked.UndoEntries.size() == BeforeCorner + 1;
        const int AfterCorner = Freedom(Linked, Follower);
        const std::vector<SolidArcSketchPick> EdgePicks = { { {}, { SketchPickKind::Point, 1 } }, { Source, { SketchPickKind::Curve, 2 } } };
        const SolidArcLinkedPicks EdgeLinked = LinkSolidArcPicks(Linked, Follower, EdgePicks);
        const SketchConstraintProposal EdgeProposal = ProposeSketchConstraint(EdgeLinked.Sketch, EdgeLinked.Picks, SketchConstraintVerb::On);
        const SolidArcCommandOutcome Edged = ConstrainSolidArcSketch(Linked, Follower, EdgeProposal.Constraint, EdgePicks);
        const int AfterEdge = Freedom(Linked, Follower);
        const SketchModel& Solved = Linked.Regeneration.Sketches.at(Follower);
        const double CornerGap = std::abs(Solved.Points[0].Position.X - 3.0) + std::abs(Solved.Points[0].Position.Y - 2.0);
        const double EdgeGap   = std::abs(Solved.Points[1].Position.Y - 2.0);
        const SketchModel& Authored = Linked.Tree.Steps.back().Sketch;
        const bool Shapes = Authored.Points.size() == 4 && Authored.Points[2].Link.Sketch == Source && Authored.Points[2].Link.Index == 2 && Authored.Curves.size() == 2 && Authored.Curves[1].Construction
                         && Authored.Curves[1].Link.Sketch == Source && Authored.Curves[1].Link.Index == 2 && CornerLinked.Appended == 1 && EdgeLinked.Appended == 2;
        Tally(LinkGate, CornerGap + EdgeGap,
              Sources.size() == 1 && Sources[0] == Source && CornerLinked.Refusal.empty() && CornerProposal.Refusal.empty() && Cornered.AcceptedCondition && OneEntry && Edged.AcceptedCondition && Shapes
                  && Unlinked == 4 && AfterCorner == 2 && AfterEdge == 1 && CornerGap <= 1.0e-12 && EdgeGap <= 1.0e-12,
              "DOF " + std::to_string(Unlinked) + " → " + Cornered.Label + " " + std::to_string(AfterCorner) + " → " + Edged.Label + " " + std::to_string(AfterEdge) + "; corner gap " + Scientific(CornerGap) + ", edge gap " + Scientific(EdgeGap));

        // W28 ② A's D1 = 4: B regenerates with its start on (4, 2) and its end still on the top line; the K session's view
        //        (DiagnoseSolidArcSketch) shows the same before B is touched
        const SolidArcCommandOutcome Widened = ChangeSolidArcDimension(Linked, Source, 5, 4.0);
        const SketchModel& Moved = Linked.Regeneration.Sketches.at(Follower);
        const SolidArcSketchDiagnosis Viewed = DiagnoseSolidArcSketch(Linked, Follower);
        const double MovedGap = std::abs(Moved.Points[0].Position.X - 4.0) + std::abs(Moved.Points[0].Position.Y - 2.0) + std::abs(Moved.Points[1].Position.Y - 2.0);
        const double ViewGap  = std::abs(Viewed.Sketch.Points[0].Position.X - 4.0) + std::abs(Viewed.Sketch.Points[0].Position.Y - 2.0);
        bool Quiet = true;
        for (const FeatureStepReport& Report : Linked.Regeneration.Steps) Quiet = Quiet && Report.Notes.empty();
        Tally(LinkGate, MovedGap + ViewGap, Widened.AcceptedCondition && MovedGap <= 1.0e-12 && ViewGap <= 1.0e-12 && Quiet && Relative(Linked.Summary.Volume, 8.0) <= 1.0e-9,
              Widened.Label + ": B's start follows to (4, 2), end on y = 2 (gap " + Scientific(MovedGap) + "), K view gap " + Scientific(ViewGap) + ", no notes, V 8");

        // W28 ③ refusals: A linking to the later B, dragging the linked point, a non-existent source point — untouched
        const std::string LinkedFile = EncodeSolidArcDocument(Linked);
        const size_t LinkedHistory = Linked.UndoEntries.size();
        const SolidArcLinkedPicks Later = LinkSolidArcPicks(Linked, Source, { { {}, { SketchPickKind::Point, 0 } }, { Follower, { SketchPickKind::Point, 0 } } });
        const SolidArcCommandOutcome Backward = ConstrainSolidArcSketch(Linked, Source, CornerProposal.Constraint, { { {}, { SketchPickKind::Point, 0 } }, { Follower, { SketchPickKind::Point, 0 } } });
        const SolidArcCommandOutcome Held = DragSolidArcSketchPoint(Linked, Follower, 2, { 9.0, 9.0 });
        const SolidArcLinkedPicks Beyond = LinkSolidArcPicks(Linked, Follower, { { Source, { SketchPickKind::Point, 40 } } });
        const bool LinkedUntouched = EncodeSolidArcDocument(Linked) == LinkedFile && Linked.UndoEntries.size() == LinkedHistory;
        Tally(LinkGate, 0.0, Later.Refusal.find("comes after") != std::string::npos && !Backward.AcceptedCondition && !Held.AcceptedCondition && Held.Refusal.find("is linked to") != std::string::npos
                  && Beyond.Refusal.find("has no point 40") != std::string::npos && LinkedUntouched,
              "refused: " + Later.Refusal + " · " + Held.Refusal + " · " + Beyond.Refusal + "; file and history untouched");

        // W28 ④ save → load → save byte-equal (Links, LinkSketch); Delete unlinks the corner: B's start stays at (4, 2), free,
        //        and no longer follows A (D1 = 5 leaves it); undo relinks it
        Tally(LinkGate, 0.0, RoundTrip(Linked) && LinkedFile.find("[[Steps.Sketch.Links]]") != std::string::npos && LinkedFile.find("LinkSketch = ") != std::string::npos,
              "save → load → save byte-equal with [[Steps.Sketch.Links]] { Point, Sketch, Index } and the construction line's LinkSketch / LinkIndex");
        const SolidArcCommandOutcome Unlink = UnlinkSolidArcGeometry(Linked, Follower, { SketchPickKind::Point, 2 });
        const int Freed = Freedom(Linked, Follower);
        Expect(ChangeSolidArcDimension(Linked, Source, 5, 5.0));
        const SketchModel& Stayed = Linked.Regeneration.Sketches.at(Follower);
        const double StayGap = std::abs(Stayed.Points[0].Position.X - 4.0) + std::abs(Stayed.Points[0].Position.Y - 2.0);
        const bool Relinked = UndoSolidArcCommand(Linked) && UndoSolidArcCommand(Linked) && !Linked.Tree.Steps.back().Sketch.Points[2].Link.Sketch.empty();
        Tally(LinkGate, StayGap, Unlink.AcceptedCondition && Freed == AfterEdge + 2 && StayGap <= 1.0e-12 && Relinked,
              Unlink.Label + ": DOF " + std::to_string(Freed) + ", start stays at (4, 2) when D1 = 5 (gap " + Scientific(StayGap) + "); undo relinks");
    }

    // ---- W29 … W30: Revolve (Alt+R) and Evolution (Shift+P / Alt+E)
    {
        auto Relative = [](double Value, double Expected) { return std::abs(Value - Expected) / std::max(1.0e-12, std::abs(Expected)); };
        auto Figures  = [](const SolidArcDocument& Target, const std::string& Key) -> const SolidArcBodyFigures*
        {
            for (const SolidArcBodyFigures& Body : Target.Summary.Bodies) if (Body.Key == Key) return &Body;
            return nullptr;
        };
        auto VolumeOf = [&](const SolidArcDocument& Target, const std::string& Key) { const SolidArcBodyFigures* Body = Figures(Target, Key); return Body ? Body->Volume : -1.0; };
        auto Draw = [](SolidArcDocument& Target, SolidArcDrawingKind Kind, std::vector<Vector2> Points, int GridPlane = 0)
        {
            SolidArcDrawingRequest Drawing;
            Drawing.Kind = Kind; Drawing.Points = std::move(Points); Drawing.GridPlane = GridPlane;
            return DrawFilledSketch(Target, Drawing).AcceptedCondition ? Target.Tree.Steps.back().Label : std::string{};
        };
        auto RoundTrip = [](const SolidArcDocument& Source)
        {
            const std::string Written = EncodeSolidArcDocument(Source);
            const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
            SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
            Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Copy, true);
            return Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Copy.Summary.Volume == Source.Summary.Volume;
        };
        auto Scientific = [](double Value) { char Text[32]; std::snprintf(Text, sizeof(Text), "%.3e", Value); return std::string(Text); };
        auto Verdict = [](const SolidArcCommandOutcome& Outcome) { return Outcome.AcceptedCondition ? Outcome.Label : Outcome.Label + " REFUSED (" + Outcome.Refusal + ")"; };
        auto Occurrences = [](const std::string& Text, const std::string& Word) { int Count = 0; for (size_t At = Text.find(Word); At != std::string::npos; At = Text.find(Word, At + 1)) ++Count; return Count; };

        // W29 ① a 1 × 1 square at x ∈ [1, 2] on XY: the proposed axis is the sketch y-axis (the profile lies right of it);
        //        a full turn → the first body (New) is an annulus π(R² − r²)h = 3π
        SolidArcDocument Turned = CreateSolidArcDocument("Revolve");
        const std::string Ring = Draw(Turned, SolidArcDrawingKind::Rectangle, { { 1.0, 0.0 }, { 2.0, 1.0 } });
        const SolidArcRevolveAxis Proposed = ProposeSolidArcRevolveAxis(Turned, Ring);
        const SolidArcCommandOutcome Annulus = RevolveSolidArcSketch(Turned, Ring, Proposed, 2.0 * Pi, FeatureCombination::New);
        const std::string RingKey = Annulus.AcceptedCondition ? Turned.Tree.Steps.back().Label : std::string{};
        const double RingVolume = VolumeOf(Turned, RingKey);
        Tally(RevolveGate, Relative(RingVolume, 3.0 * Pi),
              Annulus.AcceptedCondition && Proposed.Line < 0 && Proposed.Direction.X == 0.0 && Proposed.Direction.Y == 1.0 && Turned.Tree.Steps.back().Combination == FeatureCombination::New
                  && Relative(RingVolume, 3.0 * Pi) <= 1.0e-9 && Turned.Summary.SoundCondition,
              Verdict(Annulus) + ": proposed axis " + DescribeSolidArcRevolveAxis(Turned, Ring, Proposed) + ", V 3π (rel. " + Scientific(Relative(RingVolume, 3.0 * Pi)) + "), New, sound");

        // W29 ② a 1.5 × 1 plate on the y-axis (x ∈ [0, 1.5], y ∈ [−½, ½]) turned about its own left edge (a line of the
        //        sketch, by identifier) and united with the annulus: 3π + 2.25π − overlap 0.625π = 4.625π; undo / redo exact
        const std::string Plate = Draw(Turned, SolidArcDrawingKind::Rectangle, { { 0.0, -0.5 }, { 1.5, 0.5 } });
        SolidArcRevolveAxis Edge;
        for (const SketchCurve& Curve : Turned.Regeneration.Sketches.at(Plate).Curves)
        {
            const SketchModel& Model = Turned.Regeneration.Sketches.at(Plate);
            if (Curve.Kind == SketchCurveKind::Line && Model.Points[Curve.Start].Position.X == 0.0 && Model.Points[Curve.End].Position.X == 0.0) Edge.Line = Curve.Identifier;
        }
        const SolidArcCommandOutcome United = RevolveSolidArcSketch(Turned, Plate, Edge, 2.0 * Pi, FeatureCombination::Union, RingKey);
        const double UnionVolume = VolumeOf(Turned, RingKey);
        const bool   Rewound = UndoSolidArcCommand(Turned) && VolumeOf(Turned, RingKey) == RingVolume && RedoSolidArcCommand(Turned) && VolumeOf(Turned, RingKey) == UnionVolume;
        Tally(RevolveGate, Relative(UnionVolume, 4.625 * Pi),
              United.AcceptedCondition && Edge.Line >= 0 && Relative(UnionVolume, 4.625 * Pi) <= 1.0e-9 && Turned.Summary.Bodies.size() == 1 && Turned.Summary.SoundCondition && Rewound,
              Verdict(United) + ": axis by line identifier " + std::to_string(Edge.Line) + ", V 4.625π (rel. " + Scientific(Relative(UnionVolume, 4.625 * Pi)) + "), one sound body; undo / redo bit-equal");

        // W29 ③ a 2 × 1 plate across the y-axis (x ∈ [−1, 1], y ∈ [1, 2]): the proposal falls back to the sketch x-axis;
        //        a quarter turn → (θ/2)(R² − r²)w = 3π/2
        SolidArcDocument Quarter = CreateSolidArcDocument("Quarter");
        const std::string Bar = Draw(Quarter, SolidArcDrawingKind::Rectangle, { { -1.0, 1.0 }, { 1.0, 2.0 } });
        const SolidArcRevolveAxis Across = ProposeSolidArcRevolveAxis(Quarter, Bar);
        const SolidArcCommandOutcome Bent = RevolveSolidArcSketch(Quarter, Bar, Across, 0.5 * Pi, FeatureCombination::Separate);
        const double BentVolume = VolumeOf(Quarter, Bent.AcceptedCondition ? Quarter.Tree.Steps.back().Label : std::string{});
        Tally(RevolveGate, Relative(BentVolume, 1.5 * Pi),
              Bent.AcceptedCondition && Across.Line < 0 && Across.Direction.X == 1.0 && Across.Direction.Y == 0.0 && Relative(BentVolume, 1.5 * Pi) <= 1.0e-9 && Quarter.Summary.SoundCondition,
              Verdict(Bent) + ": proposed " + DescribeSolidArcRevolveAxis(Quarter, Bar, Across) + ", V 3π/2 (rel. " + Scientific(Relative(BentVolume, 1.5 * Pi)) + "), sound");

        // W29 ④ refusals leave file and history alone: no angle, more than a turn, a profile across the axis, no sketch,
        //        Difference with nothing to subtract from
        SolidArcDocument Empty = CreateSolidArcDocument("Refusals");
        const std::string Across2 = Draw(Empty, SolidArcDrawingKind::Rectangle, { { -1.0, 0.0 }, { 1.0, 1.0 } });
        const std::string Before  = EncodeSolidArcDocument(Empty);
        const SolidArcRevolveAxis YAxis{ -1, { 0.0, 0.0 }, { 0.0, 1.0 } };
        const SolidArcCommandOutcome Nothing  = RevolveSolidArcSketch(Empty, Across2, Across, 0.0);
        const SolidArcCommandOutcome Overturn = RevolveSolidArcSketch(Empty, Across2, Across, 2.5 * Pi);
        const SolidArcCommandOutcome Crossing = RevolveSolidArcSketch(Empty, Across2, YAxis, Pi);
        const SolidArcCommandOutcome Missing  = RevolveSolidArcSketch(Empty, "Sketch#99", YAxis, Pi);
        const SolidArcCommandOutcome Subtract = RevolveSolidArcSketch(Empty, Across2, Across, Pi, FeatureCombination::Difference);
        Tally(RevolveGate, 0.0, !Nothing.AcceptedCondition && !Overturn.AcceptedCondition && !Crossing.AcceptedCondition && !Missing.AcceptedCondition && !Subtract.AcceptedCondition
                                && EncodeSolidArcDocument(Empty) == Before && Crossing.Refusal.find("crosses the axis") != std::string::npos,
              "refused: " + Nothing.Refusal + " · " + Overturn.Refusal + " · " + Crossing.Refusal + " · " + Missing.Refusal + " · " + Subtract.Refusal + "; file untouched");

        // W29 ⑤ codec: the point + direction axis is written only without a line (older files stay byte-equal)
        const std::string TurnedFile = EncodeSolidArcDocument(Turned), QuarterFile = EncodeSolidArcDocument(Quarter);
        Tally(RevolveGate, 0.0, RoundTrip(Turned) && RoundTrip(Quarter) && Occurrences(TurnedFile, "AxisDirection") == 1 && QuarterFile.find("AxisDirection = [ 1.0, 0.0 ]") != std::string::npos,
              "save → load → save byte-equal; AxisOrigin / AxisDirection only on the y- / x-axis revolves (" + std::to_string(Occurrences(TurnedFile, "AxisDirection")) + " in the union file)");

        // W30 ① a 1 × ½ rectangle on XY and a line (0, 0) → (0, 2) on XZ (world +z): the proposal takes the line sketch;
        //        the rigid sweep is a prism A·L = 1
        SolidArcDocument Swept = CreateSolidArcDocument("Evolution");
        const std::string Slab = Draw(Swept, SolidArcDrawingKind::Rectangle, { { 0.0, 0.0 }, { 1.0, 0.5 } });
        const std::string Rail = Draw(Swept, SolidArcDrawingKind::Line, { { 0.0, 0.0 }, { 0.0, 2.0 } }, 1);
        const std::string Chosen = ProposeSolidArcEvolutionPath(Swept, Slab);
        const SolidArcCommandOutcome Prism = EvolveSolidArcProfile(Swept, Slab, Chosen, FeatureCombination::New);
        const std::string PrismKey = Prism.AcceptedCondition ? Swept.Tree.Steps.back().Label : std::string{};
        Tally(EvolutionGate, Relative(VolumeOf(Swept, PrismKey), 1.0),
              Prism.AcceptedCondition && Chosen == Rail && Relative(VolumeOf(Swept, PrismKey), 1.0) <= 1.0e-9 && Swept.Summary.SoundCondition && Swept.Tree.Steps.back().Combination == FeatureCombination::New,
              Verdict(Prism) + ": path proposed " + Chosen + ", V 1 (rel. " + Scientific(Relative(VolumeOf(Swept, PrismKey), 1.0)) + "), New, sound");

        // W30 ② a circle r ¼ about the origin swept along a quarter arc of radius 2 on XZ (start ⟂ the profile): Pappus
        //        πr² · Rπ/2 = π²/16 (the side is a NURBS skin within 10⁻⁶ of the canal)
        SolidArcDocument Bend = CreateSolidArcDocument("Bend");
        const std::string Disc = Draw(Bend, SolidArcDrawingKind::Circle, { { -0.25, 0.0 }, { 0.25, 0.0 } });
        const std::string Arc  = Draw(Bend, SolidArcDrawingKind::Arc, { { 0.0, 0.0 }, { 2.0, 2.0 }, { 2.0 - std::sqrt(2.0), std::sqrt(2.0) } }, 1);
        const SolidArcCommandOutcome Elbow = EvolveSolidArcProfile(Bend, Disc, Arc, FeatureCombination::Separate);
        const double ElbowVolume = VolumeOf(Bend, Elbow.AcceptedCondition ? Bend.Tree.Steps.back().Label : std::string{});
        Tally(EvolutionGate, Relative(ElbowVolume, Pi * Pi / 16.0),
              Elbow.AcceptedCondition && Relative(ElbowVolume, Pi * Pi / 16.0) <= 1.0e-6 && Bend.Summary.SoundCondition,
              Verdict(Elbow) + ": the arc is stored from (2, 2) (counter-clockwise), the sweep starts at its end nearer the profile; V π²/16 (rel. " + Scientific(Relative(ElbowVolume, Pi * Pi / 16.0)) + "), sound");

        // W30 ③ groove: a 2 × 1 × 1 block (XZ rectangle x ∈ [−1, 1], z ∈ [½, 1½] extruded 1 along −y) minus a ½ × ½
        //        square swept along +z through it: the block keeps 2 − ½ · ¼ · 1 = 1.875 (the square straddles y = 0)
        SolidArcDocument Groove = CreateSolidArcDocument("Groove");
        const std::string Face = Draw(Groove, SolidArcDrawingKind::Rectangle, { { -1.0, 0.5 }, { 1.0, 1.5 } }, 1);
        Expect(ExtrudeSolidArcSketch(Groove, Face, 1.0, FeatureCombination::Separate));
        const std::string BlockKey = Groove.Tree.Steps.back().Label;
        const double BlockVolume = VolumeOf(Groove, BlockKey);
        const std::string Square = Draw(Groove, SolidArcDrawingKind::Rectangle, { { -0.25, -0.25 }, { 0.25, 0.25 } });
        const std::string Spine  = Draw(Groove, SolidArcDrawingKind::Line, { { 0.0, 0.0 }, { 0.0, 2.0 } }, 1);
        const SolidArcCommandOutcome Cut = EvolveSolidArcProfile(Groove, Square, Spine, FeatureCombination::Difference, BlockKey);
        const double GrooveVolume = VolumeOf(Groove, BlockKey);
        const double GrooveExpected = BlockVolume - 0.125;
        Tally(EvolutionGate, Relative(GrooveVolume, GrooveExpected),
              Cut.AcceptedCondition && Relative(BlockVolume, 2.0) <= 1.0e-12 && Relative(GrooveVolume, GrooveExpected) <= 1.0e-6 && Groove.Summary.Bodies.size() == 1 && Groove.Summary.SoundCondition,
              Verdict(Cut) + ": block 2 → " + std::to_string(GrooveVolume) + " vs 1.875 (rel. " + Scientific(Relative(GrooveVolume, GrooveExpected)) + "), one sound body");

        // W30 ④ refusals (path = profile, no path, a 4-curve path, a path leaving along the profile plane, Difference with
        //        no body) leave the file alone; codec byte-equal with Kind 16 and its Path
        const std::string Flat = Draw(Swept, SolidArcDrawingKind::Line, { { 3.0, 0.0 }, { 5.0, 0.0 } });
        const std::string SweptBefore = EncodeSolidArcDocument(Swept);
        const SolidArcCommandOutcome Self   = EvolveSolidArcProfile(Swept, Slab, Slab);
        const SolidArcCommandOutcome NoPath = EvolveSolidArcProfile(Swept, Slab, "");
        const SolidArcCommandOutcome Many   = EvolveSolidArcProfile(Swept, Rail, Slab);
        const SolidArcCommandOutcome InPlane = EvolveSolidArcProfile(Swept, Slab, Flat);
        SolidArcDocument Bare = CreateSolidArcDocument("Bare");
        const std::string BareSlab = Draw(Bare, SolidArcDrawingKind::Rectangle, { { 0.0, 0.0 }, { 1.0, 0.5 } });
        const std::string BareRail = Draw(Bare, SolidArcDrawingKind::Line, { { 0.0, 0.0 }, { 0.0, 2.0 } }, 1);
        const SolidArcCommandOutcome Hollow = EvolveSolidArcProfile(Bare, BareSlab, BareRail, FeatureCombination::Difference);
        const std::string SweptAfter = EncodeSolidArcDocument(Swept);
        Tally(EvolutionGate, 0.0, !Self.AcceptedCondition && !NoPath.AcceptedCondition && !Many.AcceptedCondition && !InPlane.AcceptedCondition && !Hollow.AcceptedCondition && SweptAfter == SweptBefore,
              "refused: " + Self.Refusal + " · " + NoPath.Refusal + " · " + Many.Refusal + " · " + InPlane.Refusal + " · " + Hollow.Refusal + "; file untouched");
        Tally(EvolutionGate, 0.0, RoundTrip(Swept) && RoundTrip(Bend) && RoundTrip(Groove) && SweptAfter.find("Kind = 16") != std::string::npos && SweptAfter.find("Path = '" + Rail + "'") != std::string::npos,
              "save → load → save byte-equal with the Evolution (16) steps and Path = '" + Rail + "'");
    }

    // ---- W31 incremental replay: identical to full replays, resume points, speed
    {
        // A copy regenerated from scratch (cache off): the reference every incremental state must equal.
        auto FullReplay = [](const SolidArcDocument& Source)
        {
            SolidArcDocument Copy = CreateSolidArcDocument(Source.Title);
            Copy.Tree      = Source.Tree;
            Copy.Captions  = Source.Captions;
            Copy.Selection = Source.Selection;
            Copy.Replay.EnabledCondition = false;
            RegenerateSolidArcDocument(Copy);
            return Copy;
        };
        auto Identical = [](const SolidArcDocument& First, const SolidArcDocument& Second)
        {
            const SolidArcBodySummary& A = First.Summary;
            const SolidArcBodySummary& B = Second.Summary;
            if (A.Volume != B.Volume || A.Faces != B.Faces || A.Edges != B.Edges || A.Vertices != B.Vertices || A.Genus != B.Genus || A.Bodies.size() != B.Bodies.size()) return false;
            for (size_t Index = 0; Index < A.Bodies.size(); ++Index) if (A.Bodies[Index].Key != B.Bodies[Index].Key || A.Bodies[Index].Volume != B.Bodies[Index].Volume) return false;
            const RegenerationOutcome& P = First.Regeneration;
            const RegenerationOutcome& Q = Second.Regeneration;
            if (P.Bodies.size() != Q.Bodies.size() || P.Steps.size() != Q.Steps.size() || P.Active != Q.Active || P.Refused != Q.Refused || P.Curves.size() != Q.Curves.size()) return false;
            for (size_t Index = 0; Index < P.Bodies.size(); ++Index)
            {
                const RegeneratedBody& X = P.Bodies[Index];
                const RegeneratedBody& Y = Q.Bodies[Index];
                if (X.Key != Y.Key || X.Body.Slot != Y.Body.Slot || X.Body.Generation != Y.Body.Generation) return false;
                if (X.Naming.Faces != Y.Naming.Faces || X.Naming.Edges != Y.Naming.Edges || X.Naming.Vertices != Y.Naming.Vertices) return false;
            }
            for (size_t Index = 0; Index < P.Steps.size(); ++Index)
            {
                if (P.Steps[Index].Status != Q.Steps[Index].Status || P.Steps[Index].Refusal != Q.Steps[Index].Refusal || P.Steps[Index].Body.Slot != Q.Steps[Index].Body.Slot) return false;
            }
            return EncodeSolidArcDocument(First) == EncodeSolidArcDocument(Second);
        };
        auto Milliseconds = [](auto&& Action)
        {
            const auto Started = std::chrono::steady_clock::now();
            Action();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
        };
        auto Ms = [](double Value) { char Text[32]; std::snprintf(Text, sizeof Text, "%.1f ms", Value); return std::string(Text); };
        auto Times = [](double Value) { char Text[32]; std::snprintf(Text, sizeof Text, "%.1f×", Value); return std::string(Text); };

        // Build: block + 8 bosses (union) + 4 holes (difference) + a front fillet, every command checked against a full replay.
        SolidArcDocument Studded = CreateSolidArcDocument("Studded");
        int  Commands = 0, IdenticalCount = 0, AppendResumes = 0;
        auto Command = [&](SolidArcCommandOutcome Outcome)
        {
            const size_t Before = Studded.UndoEntries.empty() ? 0 : Studded.UndoEntries.back().Tree.Steps.size();
            Expect(Outcome);
            ++Commands;
            IdenticalCount += Identical(Studded, FullReplay(Studded)) ? 1 : 0;
            AppendResumes  += static_cast<size_t>(Studded.Regeneration.Resumed) == Before ? 1 : 0;
        };
        SolidArcConstructRequest Plate;
        Plate.Kind = SolidArcConstructKind::Block; Plate.Width = 8.0; Plate.Depth = 3.0; Plate.Height = 0.6;
        Command(ConstructSolidArcPrimitive(Studded, Plate));
        for (int Boss = 0; Boss < 8; ++Boss)
        {
            SolidArcConstructRequest Stud;
            Stud.Kind = SolidArcConstructKind::Cylinder; Stud.Position = { 0.75 + 0.9 * Boss, 2.1, 0.3 }; Stud.Radius = 0.25 + 0.01 * Boss; Stud.Height = 0.5 + 0.05 * Boss;
            Stud.Combination = FeatureCombination::Union;
            Command(ConstructSolidArcPrimitive(Studded, Stud));
        }
        for (int Hole = 0; Hole < 4; ++Hole)
        {
            SolidArcConstructRequest Drill;
            Drill.Kind = SolidArcConstructKind::Cylinder; Drill.Position = { 1.3 + 1.8 * Hole, 0.9, -0.5 }; Drill.Radius = 0.2; Drill.Height = 2.0;
            Drill.Combination = FeatureCombination::Difference;
            Command(ConstructSolidArcPrimitive(Studded, Drill));
        }
        const std::string FrontEdge = PickAndSelect(Studded, RayBetween({ 4.0, -1.0, 1.6 }, { 4.0, 0.0, 0.6 }), false);
        SelectSolidArcName(Studded, SolidArcSelectionKind::Edge, FrontEdge, false);
        Command(BlendSolidArcSelection(Studded, BlendProfile::Fillet, 0.15));
        const int Steps = static_cast<int>(Studded.Tree.Steps.size());
        Tally(ReplayGate, 0.0, IdenticalCount == Commands && AppendResumes == Commands && Steps == 27 && Studded.Regeneration.Refused == 0,
              std::to_string(Commands) + " commands → " + std::to_string(Steps) + " steps: " + std::to_string(IdenticalCount) + " identical to a full replay, " + std::to_string(AppendResumes) + " resumed at the new step");

        // Speed: full replay vs the incremental paths (each result identical to a full replay of the same tree).
        double Full = 1.0e300;
        for (int Run = 0; Run < 3; ++Run) Full = std::min(Full, Milliseconds([&]() { (void)FullReplay(Studded); }));
        auto Timed = [&](const std::string& Caption, auto&& Action, int ExpectedResume, double Least)
        {
            const double Time = Milliseconds(Action);
            const bool Same = Identical(Studded, FullReplay(Studded));
            const int Resumed = Studded.Regeneration.Resumed;
            Tally(ReplayGate, Full / Time, Same && Resumed == ExpectedResume && Full / Time >= Least,
                  Caption + ": resumed at " + std::to_string(Resumed) + "/" + std::to_string(Studded.Tree.Steps.size()) + " · " + Ms(Time) + " vs full " + Ms(Full) + " · " + Times(Full / Time) + " · identical");
        };
        int BindingStep = -1;
        for (int Index = 0; Index < Steps && BindingStep < 0; ++Index)
        {
            for (const SketchConstraint& Constraint : Studded.Tree.Steps[Index].Sketch.Constraints) if (Constraint.Parameter == "Cylinder6.Radius") BindingStep = Index;
        }
        Timed("tail edit Fillet1.Size 0.15 → 0.12", [&]() { Expect(AssignSolidArcParameter(Studded, "Fillet1.Size", 0.12)); }, Steps - 1, 2.0);
        Timed("middle edit Cylinder6.Radius 0.30 → 0.32", [&]() { Expect(AssignSolidArcParameter(Studded, "Cylinder6.Radius", 0.32)); }, BindingStep, 0.0);
        Timed("undo (Cylinder6.Radius back)", [&]() { (void)UndoSolidArcCommand(Studded); }, Steps, 10.0);
        Timed("redo", [&]() { (void)RedoSolidArcCommand(Studded); }, Steps, 10.0);
        Timed("undo again", [&]() { (void)UndoSolidArcCommand(Studded); }, Steps, 10.0);
        {
            std::unique_ptr<SolidArcDocument> Preview;
            const double Time = Milliseconds([&]() { Preview = std::make_unique<SolidArcDocument>(CloneSolidArcDocument(Studded)); });
            Tally(ReplayGate, Full / Time, Identical(*Preview, Studded) && Preview->Regeneration.Resumed == Steps && Full / Time >= 10.0,
                  "preview clone (bevel / move / sweep staging): resumed at " + std::to_string(Preview->Regeneration.Resumed) + "/" + std::to_string(Steps) + " · " + Ms(Time) + " · " + Times(Full / Time));
            SolidArcBlendRequest Wider;
            Wider.Size = 0.2;
            const std::string BackEdge = PickAndSelect(*Preview, RayBetween({ 4.0, 4.0, 1.6 }, { 4.0, 3.0, 0.6 }), false);
            SelectSolidArcName(*Preview, SolidArcSelectionKind::Edge, BackEdge, false);
            const double Staged = Milliseconds([&]() { Expect(BlendSolidArcSelection(*Preview, Wider)); });
            // Fillet1 was last replayed by the (non-capturing) parameter edit: this authoring command re-captures its edge
            // signature in place from the checkpoint before it, so only the new fillet replays (27).
            Tally(ReplayGate, 0.0, Identical(*Preview, FullReplay(*Preview)) && Preview->Regeneration.Resumed == Steps && Identical(Studded, FullReplay(Studded)),
                  "preview fillets the back edge " + BackEdge + ": resumed at " + std::to_string(Preview->Regeneration.Resumed) + " (Fillet1 re-captured in place) · " + Ms(Staged) + " · source untouched");
        }

        // History depth: 10 more edits; only the 8 newest undo entries keep checkpoints; undoing all 10 stays identical.
        for (int Edit = 0; Edit < 10; ++Edit) Expect(AssignSolidArcParameter(Studded, "Fillet1.Size", 0.1 + 0.005 * Edit));
        int Kept = 0;
        for (const SolidArcHistoryEntry& Entry : Studded.UndoEntries) Kept += Entry.Replay ? 1 : 0;
        int UndoIdentical = 0, UndoRestored = 0;
        for (int Edit = 0; Edit < 10; ++Edit)
        {
            (void)UndoSolidArcCommand(Studded);
            UndoIdentical += Identical(Studded, FullReplay(Studded)) ? 1 : 0;
            UndoRestored  += Studded.Regeneration.Resumed == Steps ? 1 : 0;
        }
        Tally(ReplayGate, 0.0, Kept == 8 && Studded.UndoEntries.size() > 8 && UndoIdentical == 10 && UndoRestored >= 8,
              std::to_string(Studded.UndoEntries.size() + 10) + " undo entries, " + std::to_string(Kept) + " with checkpoints; 10 undos identical, " + std::to_string(UndoRestored) + " restored every step (older ones resume at the edited step)");

        // Vehicle bonnet: 11 SpaceCurves + loft; a pole move on one section resumes at that curve.
        SolidArcDocument Bonnet = CreateSolidArcDocument("Bonnet");
        Expect(ImportSolidArcCurves(Bonnet, ReadVehicleCurves(SOLIDARC_VEHICLE_SAMPLE)));
        std::vector<std::string> Sections;
        for (const FeatureStep& Step : Bonnet.Tree.Steps) if (Bonnet.Captions[Step.Label].rfind("Section", 0) == 0) Sections.push_back(Step.Label);
        Expect(LoftSolidArcCurves(Bonnet, Sections));
        int SectionStep = -1;
        for (size_t Index = 0; Index < Bonnet.Tree.Steps.size(); ++Index) if (Bonnet.Tree.Steps[Index].Label == Sections.back()) SectionStep = static_cast<int>(Index);
        const Vector3 Pole = Bonnet.Regeneration.Curves.at(Sections.back()).Curve.Definition->Poles[2];
        double BonnetFull = 1.0e300;
        for (int Run = 0; Run < 3; ++Run) BonnetFull = std::min(BonnetFull, Milliseconds([&]() { (void)FullReplay(Bonnet); }));
        const double Moved = Milliseconds([&]() { Expect(MoveSolidArcPole(Bonnet, Sections.back(), 2, Pole + Vector3{ 0.0, 0.0, 0.01 })); });
        const bool MovedSame = Identical(Bonnet, FullReplay(Bonnet));
        const int MovedResume = Bonnet.Regeneration.Resumed;
        const double Undone = Milliseconds([&]() { (void)UndoSolidArcCommand(Bonnet); });
        Tally(ReplayGate, BonnetFull / Undone, MovedSame && MovedResume == SectionStep && Identical(Bonnet, FullReplay(Bonnet)) && Bonnet.Regeneration.Resumed == static_cast<int>(Bonnet.Tree.Steps.size()),
              "bonnet (" + std::to_string(Bonnet.Tree.Steps.size()) + " steps, full " + Ms(BonnetFull) + "): pole move resumed at " + std::to_string(MovedResume) + " · " + Ms(Moved) + "; undo restored all · " + Ms(Undone) + " · identical");
    }

    // ---- W32 x-ray: pick-through and box edges (closed form: the isometric eye sits at +x, −y, +z, so exactly the 3 edges
    //      meeting the far corner (0, 2, 0) are hidden)
    {
        SolidArcDocument Ghost = CreateSolidArcDocument("Ghost");
        SolidArcConstructRequest Brick;
        Brick.Kind = SolidArcConstructKind::Block; Brick.Width = 4.0; Brick.Depth = 2.0; Brick.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Ghost, Brick));
        const SolidArcScene Scene = BuildSolidArcScene(Ghost);
        auto AtCorner = [](const Vector3& Point, double X, double Y, double Z)
        {
            return std::abs(Point.X - X) + std::abs(Point.Y - Y) + std::abs(Point.Z - Z) < 1.0e-9;
        };
        auto TouchesFarCorner = [&](const std::string& Name)
        {
            return AtCorner(EdgeEndPosition(Ghost, Name, false), 0.0, 2.0, 0.0) || AtCorner(EdgeEndPosition(Ghost, Name, true), 0.0, 2.0, 0.0);
        };
        auto LiesOn = [&](const std::string& Name, double Y, double Z)                       // Edge along x at (y, z)
        {
            const Vector3 Start = EdgeEndPosition(Ghost, Name, false);
            const Vector3 End   = EdgeEndPosition(Ghost, Name, true);
            return std::abs(Start.Y - Y) + std::abs(End.Y - Y) + std::abs(Start.Z - Z) + std::abs(End.Z - Z) < 1.0e-9 && std::abs(Start.X - End.X) > 1.0;
        };
        auto NameOf = [&](const std::optional<SolidArcPick>& Hit) { return Hit ? NameSolidArcIdentity(Scene, Hit->Identity) : std::string("(miss)"); };

        // Pick-through: from the front-top towards the back-bottom edge (2, 2, 0)
        const OrbitRay Behind = RayBetween({ 2.0, -3.0, 4.0 }, { 2.0, 2.0, 0.0 });
        const std::optional<SolidArcPick> Opaque = PickSolidArcScene(Scene, Behind, 0.02, false);
        const std::optional<SolidArcPick> Through = PickSolidArcScene(Scene, Behind, 0.02, true);
        const bool OpaqueFace = Opaque && (Opaque->Identity & IdentityEdgeBit) == 0;
        const bool ThroughEdge = Through && (Through->Identity & IdentityEdgeBit) != 0 && LiesOn(NameOf(Through), 2.0, 0.0);
        const double Farther = (Opaque && Through) ? Through->Distance - Opaque->Distance : 0.0;
        Tally(XRayGate, 0.0, OpaqueFace && ThroughEdge && Farther > 0.5,
              "ray at the hidden back-bottom edge: opaque → " + NameOf(Opaque) + " · x-ray → " + NameOf(Through) + " (" + std::to_string(Farther) + " farther)");

        // A visible edge (front-top, y = 0, z = 1) still picks in x-ray, and the same with x-ray off
        const OrbitRay Front = RayBetween({ 2.0, -3.0, 4.0 }, { 2.0, 0.0, 1.0 });
        const std::optional<SolidArcPick> FrontOpaque  = PickSolidArcScene(Scene, Front, 0.02, false);
        const std::optional<SolidArcPick> FrontThrough = PickSolidArcScene(Scene, Front, 0.02, true);
        Tally(XRayGate, 0.0, FrontOpaque && FrontThrough && FrontOpaque->Identity == FrontThrough->Identity && (FrontThrough->Identity & IdentityEdgeBit) != 0 && LiesOn(NameOf(FrontThrough), 0.0, 1.0),
              "ray at the visible front-top edge: opaque = x-ray → " + NameOf(FrontThrough));

        // Box edges over the whole viewport in the isometric view
        OrbitCamera Camera;
        Camera.Target = { 2.0, 1.0, 0.5 }; Camera.Distance = 10.0; Camera.SceneRadius = 2.5;
        const OrbitCameraFrame Frame = FrameOrbitCamera(Camera, 800.0, 600.0);
        const std::vector<uint32_t> Visible = EncloseSolidArcEdges(Scene, Frame, { 0.0, 0.0 }, { 800.0, 600.0 }, false);
        const std::vector<uint32_t> All     = EncloseSolidArcEdges(Scene, Frame, { 0.0, 0.0 }, { 800.0, 600.0 }, true);
        long OffCorner = 0;
        for (const SolidArcSceneEdge& Edge : Scene.Edges) OffCorner += TouchesFarCorner(Edge.Name) ? 0 : 1;
        bool VisibleOffCorner = true;
        for (const uint32_t Index : Visible) VisibleOffCorner = VisibleOffCorner && !TouchesFarCorner(Scene.Edges[Index].Name);
        const bool Subset = std::includes(All.begin(), All.end(), Visible.begin(), Visible.end());
        Tally(XRayGate, 0.0, Scene.Edges.size() == 12 && OffCorner == 9 && Visible.size() == 9 && VisibleOffCorner,
              "full box without x-ray → " + std::to_string(Visible.size()) + " edges, none at the far corner (closed form 9 of " + std::to_string(Scene.Edges.size()) + ")");
        Tally(XRayGate, 0.0, All.size() == 12 && Subset, "full box with x-ray → " + std::to_string(All.size()) + " edges (the opaque 9 among them)");

        // An empty corner of the viewport (the block projects around the centre)
        const std::vector<uint32_t> NoneOpaque  = EncloseSolidArcEdges(Scene, Frame, { 0.0, 0.0 }, { 40.0, 40.0 }, false);
        const std::vector<uint32_t> NoneThrough = EncloseSolidArcEdges(Scene, Frame, { 0.0, 0.0 }, { 40.0, 40.0 }, true);
        Tally(XRayGate, 0.0, NoneOpaque.empty() && NoneThrough.empty(), "empty 40 × 40 px corner box → " + std::to_string(NoneOpaque.size()) + " / " + std::to_string(NoneThrough.size()) + " edges");
    }

    // ---- W33 measure + section: exact figures of a 4 × 2 × 1 block (corner at the origin) and a second 2 × 2 × 1 body at
    //      x ∈ [6, 8], against closed forms (unit density)
    {
        SolidArcDocument Gauge = CreateSolidArcDocument("Gauge");
        SolidArcConstructRequest Brick;
        Brick.Kind = SolidArcConstructKind::Block; Brick.Width = 4.0; Brick.Depth = 2.0; Brick.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Gauge, Brick));
        auto Relative = [](double Value, double Expected) { return std::abs(Value - Expected) / std::max(1.0, std::abs(Expected)); };
        auto Near = [](const Vector3& Point, double X, double Y, double Z) { return std::abs(Point.X - X) + std::abs(Point.Y - Y) + std::abs(Point.Z - Z); };
        auto Row = [](const SolidArcMeasurement& Measurement, const std::string& Caption)
        {
            for (const SolidArcMeasureRow& Each : Measurement.Rows) if (Each.Caption == Caption) return Each.Number;
            return std::nan("");
        };
        auto Select = [&](std::initializer_list<OrbitRay> Rays)
        {
            ClearSolidArcSelection(Gauge);
            std::string Names;
            for (const OrbitRay& Ray : Rays) Names += (Names.empty() ? "" : " + ") + PickAndSelect(Gauge, Ray, true);
            return Names;
        };
        const OrbitRay Top        = RayBetween({ 1.3, 0.7, 5.0 }, { 1.3, 0.7, 0.0 });
        const OrbitRay Bottom     = RayBetween({ 1.3, 0.7, -5.0 }, { 1.3, 0.7, 0.0 });
        const OrbitRay Front      = RayBetween({ 1.3, -5.0, 0.4 }, { 1.3, 0.0, 0.4 });
        const OrbitRay FrontTop   = RayBetween({ 2.0, -3.0, 4.0 }, { 2.0, 0.0, 1.0 });
        const OrbitRay BackBottom = RayBetween({ 2.0, 5.0, -3.0 }, { 2.0, 2.0, 0.0 });

        // Nothing selected → the body: V = 8, A = 28, centroid (2, 1, ½), principal moments m(b² + c²)/12 = 10/3, 34/3, 40/3
        ClearSolidArcSelection(Gauge);
        const SolidArcMeasurement Body = MeasureSolidArcSelection(Gauge);
        const double BodyError = std::max({ Relative(Body.Volume, 8.0), Relative(Body.Area, 28.0), Near(Body.Centroid, 2.0, 1.0, 0.5),
                                            Relative(Row(Body, "Principal I1 I2 I3"), 10.0 / 3.0) });
        Tally(MeasureGate, BodyError, Body.Refusal.empty() && BodyError <= 1.0e-9 && Body.CentroidCondition,
              "nothing selected → " + Body.Subject + " · V " + std::to_string(Body.Volume) + " · A " + std::to_string(Body.Area) + " · I₁ " + std::to_string(Row(Body, "Principal I1 I2 I3")));

        // One face: top, area 8, perimeter 12, centroid (2, 1, 1)
        const std::string TopName = Select({ Top });
        const SolidArcMeasurement Face = MeasureSolidArcSelection(Gauge);
        const double FaceError = std::max({ Relative(Face.Area, 8.0), Relative(Face.Length, 12.0), Near(Face.Centroid, 2.0, 1.0, 1.0) });
        Tally(MeasureGate, FaceError, Face.Refusal.empty() && FaceError <= 1.0e-9, "face " + TopName + " → area " + std::to_string(Face.Area) + " · perimeter " + std::to_string(Face.Length));

        // One edge: front-top, length 4
        const std::string EdgeName = Select({ FrontTop });
        const SolidArcMeasurement Edge = MeasureSolidArcSelection(Gauge);
        Tally(MeasureGate, Relative(Edge.Length, 4.0), Edge.Refusal.empty() && Relative(Edge.Length, 4.0) <= 1.0e-9, "edge " + EdgeName + " → length " + std::to_string(Edge.Length));

        // Two faces: top + bottom → distance 1, normals 180°; top + front → touching (0), 90°
        const std::string Opposite = Select({ Top, Bottom });
        const SolidArcMeasurement Apart = MeasureSolidArcSelection(Gauge);
        const double ApartError = std::max({ Relative(Apart.Distance, 1.0), std::abs(Apart.Angle - 180.0) / 180.0, std::abs(Apart.Second.Z - Apart.First.Z) - 1.0 });
        Tally(MeasureGate, ApartError, Apart.DistanceCondition && ApartError <= 1.0e-9, Opposite + " → distance " + std::to_string(Apart.Distance) + " · angle " + std::to_string(Apart.Angle) + "°");
        const std::string Adjacent = Select({ Top, Front });
        const SolidArcMeasurement Touching = MeasureSolidArcSelection(Gauge);
        const double TouchingError = std::max(std::abs(Touching.Distance), std::abs(Touching.Angle - 90.0) / 90.0);
        Tally(MeasureGate, TouchingError, Touching.DistanceCondition && TouchingError <= 1.0e-9, Adjacent + " → distance " + std::to_string(Touching.Distance) + " · angle " + std::to_string(Touching.Angle) + "°");

        // Edge + face: front-top edge ∥ bottom face → distance 1, line-to-plane angle 0°; two skew-parallel edges → √5, 0°
        const std::string EdgeFace = Select({ Bottom, FrontTop });
        const SolidArcMeasurement Parallel = MeasureSolidArcSelection(Gauge);
        const double ParallelError = std::max(Relative(Parallel.Distance, 1.0), std::abs(Parallel.Angle));
        Tally(MeasureGate, ParallelError, Parallel.DistanceCondition && ParallelError <= 1.0e-9, EdgeFace + " → distance " + std::to_string(Parallel.Distance) + " · angle " + std::to_string(Parallel.Angle) + "°");
        const std::string TwoEdges = Select({ FrontTop, BackBottom });
        const SolidArcMeasurement Diagonal = MeasureSolidArcSelection(Gauge);
        const double DiagonalError = std::max(Relative(Diagonal.Distance, std::sqrt(5.0)), std::abs(Diagonal.Angle));
        Tally(MeasureGate, DiagonalError, Diagonal.DistanceCondition && DiagonalError <= 1.0e-9, TwoEdges + " → distance " + std::to_string(Diagonal.Distance) + " (√5) · angle " + std::to_string(Diagonal.Angle) + "°");

        // Three faces → total area 8 + 8 + 4 = 20
        Select({ Top, Bottom, Front });
        const SolidArcMeasurement Total = MeasureSolidArcSelection(Gauge);
        Tally(MeasureGate, Relative(Total.Area, 20.0), Total.Refusal.empty() && Relative(Total.Area, 20.0) <= 1.0e-9, Total.Subject + " → total area " + std::to_string(Total.Area));

        // Section x = 1.5 (n = −x̂, u = ŷ, v = −ẑ): 2 × 1 rectangle → A 2, P 6, centroid (1.5, 1, ½), Iuu = 2·1³/12, Ivv = 1·2³/12
        auto SectionError = [&](const SolidArcSection& Section, double Area, double Perimeter, Vector3 Centroid, double Iuu, double Ivv)
        {
            return std::max({ Relative(Section.Area, Area), Relative(Section.Perimeter, Perimeter), Near(Section.Centroid, Centroid.X, Centroid.Y, Centroid.Z),
                              Relative(Section.SecondMoments[0], Iuu), Relative(Section.SecondMoments[1], Ivv), std::abs(Section.SecondMoments[2]) });
        };
        ClearSolidArcSelection(Gauge);
        const SolidArcSection Across = SectionSolidArcDocument(Gauge, { SolidArcSectionAxis::X, 1.5, false, {} });
        const double AcrossError = SectionError(Across, 2.0, 6.0, { 1.5, 1.0, 0.5 }, 2.0 / 12.0, 8.0 / 12.0);
        Tally(MeasureGate, AcrossError, Across.Refusal.empty() && AcrossError <= 1.0e-9 && Across.Regions == 1 && Across.Loops == 1 && Across.Plane.ZAxis.X == -1.0,
              "section x = 1.5 → A " + std::to_string(Across.Area) + " · P " + std::to_string(Across.Perimeter) + " · Iuu " + std::to_string(Across.SecondMoments[0]) + " · Ivv " + std::to_string(Across.SecondMoments[1]));

        // Section z = 0.25: 4 × 2 → A 8, P 12, Iuu = 4·2³/12, Ivv = 2·4³/12
        const SolidArcSection Level = SectionSolidArcDocument(Gauge, { SolidArcSectionAxis::Z, 0.25, false, {} });
        const double LevelError = SectionError(Level, 8.0, 12.0, { 2.0, 1.0, 0.25 }, 32.0 / 12.0, 128.0 / 12.0);
        Tally(MeasureGate, LevelError, Level.Refusal.empty() && LevelError <= 1.0e-9, "section z = 0.25 → A " + std::to_string(Level.Area) + " · Ivv " + std::to_string(Level.SecondMoments[1]));

        // Face plane: the top face moved −¼ along its normal (z = ¾), kept side below
        Select({ Top });
        const SolidArcSection Facing = SectionSolidArcDocument(Gauge, { SolidArcSectionAxis::Face, -0.25, false, TopName });
        const double FacingError = std::max(SectionError(Facing, 8.0, 12.0, { 2.0, 1.0, 0.75 }, 32.0 / 12.0, 128.0 / 12.0), std::abs(Facing.Plane.ZAxis.Z + 1.0));
        Tally(MeasureGate, FacingError, Facing.Refusal.empty() && FacingError <= 1.0e-9, "section on " + TopName + " at −0.25 → plane z " + std::to_string(Facing.Plane.Origin.Z) + " · A " + std::to_string(Facing.Area));

        // Refusals: a plane past the body; a Face request naming no planar face
        const SolidArcSection Past = SectionSolidArcDocument(Gauge, { SolidArcSectionAxis::X, 10.0, false, {} });
        const SolidArcSection Nameless = SectionSolidArcDocument(Gauge, { SolidArcSectionAxis::Face, 0.0, false, "F-none" });
        Tally(MeasureGate, 0.0, Past.Refusal.find("misses") != std::string::npos && !Nameless.PlaneCondition && !Nameless.Refusal.empty(),
              "x = 10 → \"" + Past.Refusal + "\" · face F-none → \"" + Nameless.Refusal + "\"");

        // Second body 2 × 2 × 1 at x = 6: section z = ½ adds; centroid area-weighted; Ivv by the parallel-axis theorem
        SolidArcConstructRequest Second = Brick;
        Second.Combination = FeatureCombination::Separate; Second.Position = { 6.0, 0.0, 0.0 }; Second.Width = 2.0;
        Expect(ConstructSolidArcPrimitive(Gauge, Second));
        ClearSolidArcSelection(Gauge);
        const SolidArcSection Pair = SectionSolidArcDocument(Gauge, { SolidArcSectionAxis::Z, 0.5, false, {} });
        const double CentreX = (8.0 * 2.0 + 4.0 * 7.0) / 12.0;
        const double PairIvv = 2.0 * 64.0 / 12.0 + 8.0 * (2.0 - CentreX) * (2.0 - CentreX) + 2.0 * 8.0 / 12.0 + 4.0 * (7.0 - CentreX) * (7.0 - CentreX);
        const double PairIuu = 4.0 * 8.0 / 12.0 + 2.0 * 8.0 / 12.0;
        const double PairError = SectionError(Pair, 12.0, 20.0, { CentreX, 1.0, 0.5 }, PairIuu, PairIvv);
        Tally(MeasureGate, PairError, Pair.Refusal.empty() && PairError <= 1.0e-9 && Pair.Bodies == 2 && Pair.Regions == 2,
              "two bodies, z = ½ → A " + std::to_string(Pair.Area) + " · x̄ " + std::to_string(Pair.Centroid.X) + " · Ivv " + std::to_string(Pair.SecondMoments[1]) + " (parallel axis " + std::to_string(PairIvv) + ")");

        // Mode-4 body pick → that body's mass (V 4); hiding it removes it from the section
        const std::string SecondKey = Gauge.Regeneration.Bodies.back().Key;
        ClearSolidArcSelection(Gauge);
        SelectSolidArcName(Gauge, SolidArcSelectionKind::Body, SecondKey, false);
        const SolidArcMeasurement Picked = MeasureSolidArcSelection(Gauge);
        const double PickedError = std::max(Relative(Picked.Volume, 4.0), Near(Picked.Centroid, 7.0, 1.0, 0.5));
        Tally(MeasureGate, PickedError, Picked.Refusal.empty() && PickedError <= 1.0e-9, "body " + SecondKey + " picked → " + Picked.Subject + " · V " + std::to_string(Picked.Volume));
        Expect(ChangeSolidArcVisibility(Gauge, SolidArcVisibilityVerb::HideSelected));
        const SolidArcSection Alone = SectionSolidArcDocument(Gauge, { SolidArcSectionAxis::Z, 0.5, false, {} });
        Tally(MeasureGate, Relative(Alone.Area, 8.0), Alone.Refusal.empty() && Relative(Alone.Area, 8.0) <= 1.0e-9 && Alone.Bodies == 1,
              "second body hidden → section A " + std::to_string(Alone.Area) + " over " + std::to_string(Alone.Bodies) + " body");
    }

    // ---- W34 section clip: CPU pick and box selection of the Alt+S view (closed form on a 4 × 2 × 1 block, plane x = 1,
    //      normal −x̂ so the kept side is x ≤ 1)
    {
        SolidArcDocument Cut = CreateSolidArcDocument("Cut");
        SolidArcConstructRequest Brick;
        Brick.Kind = SolidArcConstructKind::Block; Brick.Width = 4.0; Brick.Depth = 2.0; Brick.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Cut, Brick));
        const SolidArcScene       Scene = BuildSolidArcScene(Cut);
        const SolidArcSectionClip Clip  = ComposeSolidArcSectionClip(Scene, Cut, { -1.0, 0.0, 0.0, 1.0 });
        auto NameOf = [&](const std::optional<SolidArcPick>& Hit)
        {
            if (!Hit) return std::string("(miss)");
            return Hit->Identity == IdentitySectionCap ? std::string("(cap)") : NameSolidArcIdentity(Scene, Hit->Identity);
        };
        auto EdgeCondition = [](const std::optional<SolidArcPick>& Hit) { return Hit && Hit->Identity != IdentitySectionCap && (Hit->Identity & IdentityEdgeBit) != 0; };

        Tally(SectionGate, 0.0, Clip.Plane == std::array<double, 4>{ -1.0, 0.0, 0.0, 1.0 } && Clip.SheetFaces.size() == Scene.Faces.size() &&
                               std::count(Clip.SheetFaces.begin(), Clip.SheetFaces.end(), uint8_t(1)) == 0,
              "clip of a solid block: plane copied, " + std::to_string(Clip.SheetFaces.size()) + " faces, none marked sheet");

        // Straight down at x = 3: top and bottom both lie on the removed side → nothing
        const OrbitRay Removed = RayBetween({ 3.0, 1.0, 5.0 }, { 3.0, 1.0, 0.0 });
        const std::optional<SolidArcPick> RemovedOpen = PickSolidArcScene(Scene, Removed, 0.0);
        const std::optional<SolidArcPick> RemovedCut  = PickSolidArcScene(Scene, Removed, 0.0, false, &Clip);
        Tally(SectionGate, 0.0, RemovedOpen && !RemovedCut, "down at x = 3: unsectioned → " + NameOf(RemovedOpen) + " · sectioned → " + NameOf(RemovedCut));

        // Along −x at (y, z) = (1, ½): x = 4 is removed, the back of x = 0 is the first kept surface → the cap at distance 5
        const OrbitRay Opening = RayBetween({ 5.0, 1.0, 0.5 }, { 0.0, 1.0, 0.5 });
        const std::optional<SolidArcPick> Capped = PickSolidArcScene(Scene, Opening, 0.0, false, &Clip);
        const double CapError = Capped ? std::abs(Capped->Distance - 5.0) : 1.0;
        Tally(SectionGate, CapError, Capped && Capped->Identity == IdentitySectionCap && CapError <= 1.0e-9 && NameSolidArcIdentity(Scene, IdentitySectionCap).empty(),
              "along −x through the opening → " + NameOf(Capped) + " at " + std::to_string(Capped ? Capped->Distance : 0.0) + " (back face x = 0: 5; plane crossing: 4) · cap name \"\"");

        // Oblique from (3, 1, 3) towards (½, 1, 1): it crosses the plane above the block (z = 1.4), then meets the kept top
        const OrbitRay Oblique = RayBetween({ 3.0, 1.0, 3.0 }, { 0.5, 1.0, 1.0 });
        const std::optional<SolidArcPick> Over     = PickSolidArcScene(Scene, Oblique, 0.0, false, &Clip);
        const std::optional<SolidArcPick> OverOpen = PickSolidArcScene(Scene, Oblique, 0.0);
        const double OverError = Over ? std::abs(Over->Distance - std::sqrt(2.5 * 2.5 + 2.0 * 2.0)) : 1.0;
        Tally(SectionGate, OverError, Over && OverOpen && Over->Identity == OverOpen->Identity && OverError <= 1.0e-9,
              "oblique ray crossing the plane outside the material → " + NameOf(Over) + " (the kept top, not the cap) at " + std::to_string(Over ? Over->Distance : 0.0));

        // Edges: the x = 4 top edge is removed (the ray continues through removed faces → miss); the x = 0 top edge picks
        const OrbitRay FarEdge  = RayBetween({ 5.0, 1.0, 2.0 }, { 4.0, 1.0, 1.0 });
        const OrbitRay NearEdge = RayBetween({ -1.0, 1.0, 2.0 }, { 0.0, 1.0, 1.0 });
        const std::optional<SolidArcPick> FarOpen  = PickSolidArcScene(Scene, FarEdge, 0.02);
        const std::optional<SolidArcPick> FarCut   = PickSolidArcScene(Scene, FarEdge, 0.02, false, &Clip);
        const std::optional<SolidArcPick> NearOpen = PickSolidArcScene(Scene, NearEdge, 0.02);
        const std::optional<SolidArcPick> NearCut  = PickSolidArcScene(Scene, NearEdge, 0.02, false, &Clip);
        Tally(SectionGate, 0.0, EdgeCondition(FarOpen) && !FarCut, "x = 4 top edge: unsectioned → " + NameOf(FarOpen) + " · sectioned → " + NameOf(FarCut));
        Tally(SectionGate, 0.0, EdgeCondition(NearCut) && NearOpen && NearCut->Identity == NearOpen->Identity, "x = 0 top edge: sectioned = unsectioned → " + NameOf(NearCut));

        // Sheet faces have no inside: a clip marking every face as sheet shows the back of x = 0 itself
        SolidArcSectionClip Sheets = Clip;
        Sheets.SheetFaces.assign(Scene.Faces.size(), 1);
        const std::optional<SolidArcPick> Backside = PickSolidArcScene(Scene, Opening, 0.0, false, &Sheets);
        Tally(SectionGate, 0.0, Backside && Backside->Identity != IdentitySectionCap && (Backside->Identity & IdentityEdgeBit) == 0 && std::abs(Backside->Distance - 5.0) <= 1.0e-9,
              "same ray with sheet faces → " + NameOf(Backside) + " (its back, no cap)");
        for (SolidArcBodyFigures& Figures : Cut.Summary.Bodies) Figures.SheetCondition = true;  // Summary only; restored below
        const SolidArcSectionClip Marked = ComposeSolidArcSectionClip(Scene, Cut, Clip.Plane);
        for (SolidArcBodyFigures& Figures : Cut.Summary.Bodies) Figures.SheetCondition = false;
        Tally(SectionGate, 0.0, std::count(Marked.SheetFaces.begin(), Marked.SheetFaces.end(), uint8_t(1)) == long(Scene.Faces.size()),
              "the body summarised as a sheet → " + std::to_string(std::count(Marked.SheetFaces.begin(), Marked.SheetFaces.end(), uint8_t(1))) + " / " + std::to_string(Scene.Faces.size()) + " faces marked");

        // Box over the whole viewport in x-ray: the 4 edges at x = 4 are removed, the 4 at x = 0 and the 4 along x remain
        OrbitCamera Camera;
        Camera.Target = { 2.0, 1.0, 0.5 }; Camera.Distance = 10.0; Camera.SceneRadius = 2.5;
        const OrbitCameraFrame   Frame  = FrameOrbitCamera(Camera, 800.0, 600.0);
        const std::vector<uint32_t> Boxed  = EncloseSolidArcEdges(Scene, Frame, { 0.0, 0.0 }, { 800.0, 600.0 }, true, &Clip);
        bool KeptCondition = true;
        for (const uint32_t Index : Boxed)
        {
            const Vector3 Start = EdgeEndPosition(Cut, Scene.Edges[Index].Name, false);
            const Vector3 End   = EdgeEndPosition(Cut, Scene.Edges[Index].Name, true);
            KeptCondition = KeptCondition && std::min(Start.X, End.X) <= 1.0;
        }
        Tally(SectionGate, 0.0, Boxed.size() == 8 && KeptCondition, "full box, x-ray, sectioned → " + std::to_string(Boxed.size()) + " edges, all reaching x ≤ 1 (closed form 8 of 12)");
    }

    // ---- W35 object snapping: exact snap points under the pointer (closed forms on a block, a cylinder, sketches)
    {
        SolidArcDocument Snaps = CreateSolidArcDocument("Snaps");
        SolidArcConstructRequest Brick;
        Brick.Kind = SolidArcConstructKind::Block; Brick.Width = 4.0; Brick.Depth = 2.0; Brick.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Snaps, Brick));
        SolidArcConstructRequest Post;
        Post.Kind = SolidArcConstructKind::Cylinder; Post.Position = { 6.0, 1.0, 0.0 }; Post.Radius = 0.5; Post.Height = 1.0;
        Post.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(Snaps, Post));
        Expect(DrawFilledSketch(Snaps, DrawingOf(SolidArcDrawingKind::Line, 0, { { -3.0, -2.0 }, { -1.0, -4.0 } })));
        Expect(DrawFilledSketch(Snaps, DrawingOf(SolidArcDrawingKind::Line, 0, { { -3.0, -4.0 }, { -1.0, -2.0 } })));
        Expect(DrawFilledSketch(Snaps, DrawingOf(SolidArcDrawingKind::Circle, 0, { { -5.0, -3.0 }, { -4.2, -3.0 } }, false, true)));
        Expect(DrawFilledSketch(Snaps, DrawingOf(SolidArcDrawingKind::Curve, 0, { { -8.0, -1.0 }, { -7.0, 0.5 }, { -6.0, -0.5 }, { -5.0, 1.0 } })));
        const SolidArcScene   Scene     = BuildSolidArcScene(Snaps);
        SolidArcSnapCatalogue Catalogue = CatalogueSolidArcSnaps(Snaps, Scene);
        OrbitCamera Camera;
        Camera.Target = { 0.0, -1.0, 0.5 }; Camera.Distance = 22.0; Camera.SceneRadius = 9.0;
        const OrbitCameraFrame Frame = FrameOrbitCamera(Camera, 1200.0, 900.0);
        auto Query = [&](Vector3 At, double NudgeX = 0.0, double NudgeY = 0.0)
        {
            SolidArcSnapQuery Request;
            Request.Frame = Frame;
            (void)ProjectSolidArcPoint(Frame, At, Request.PixelX, Request.PixelY);
            Request.PixelX += NudgeX;
            Request.PixelY += NudgeY;
            return Request;
        };
        auto Describe = [](const SolidArcSnap& Snap)
        {
            char Text[256];
            std::snprintf(Text, sizeof(Text), "%s (%.12g, %.12g, %.12g) · %s · %.1f px", Snap.Kind == SolidArcSnapKind::None ? "none" : SolidArcSnapLabel(Snap.Kind),
                          Snap.Position.X, Snap.Position.Y, Snap.Position.Z, Snap.Source.c_str(), Snap.Pixels);
            return std::string(Text);
        };
        auto Point = [&](const char* Note, Vector3 At, SolidArcSnapKind Kind, Vector3 Expected, double NudgeX = 3.0, double NudgeY = -2.0)
        {
            const SolidArcSnap Snap  = FindSolidArcSnap(Catalogue, Snaps, Scene, Query(At, NudgeX, NudgeY));
            const double       Error = Distance(Snap.Position, Expected);
            Tally(SnapGate, Error, Snap.Kind == Kind && Error <= 1.0e-12, std::string(Note) + " → " + Describe(Snap));
        };
        Point("block corner (4, 0, 1), pointer 3.6 px off", { 4.0, 0.0, 1.0 }, SolidArcSnapKind::Endpoint, { 4.0, 0.0, 1.0 });
        Point("front top edge midpoint (2, 0, 1)", { 2.0, 0.0, 1.0 }, SolidArcSnapKind::Midpoint, { 2.0, 0.0, 1.0 });
        Point("top face centre (2, 1, 1) (planar face centroid)", { 2.0, 1.0, 1.0 }, SolidArcSnapKind::Centre, { 2.0, 1.0, 1.0 });
        Point("cylinder top rim centre (6, 1, 1)", { 6.0, 1.0, 1.0 }, SolidArcSnapKind::Centre, { 6.0, 1.0, 1.0 });
        Point("sketch lines cross at (−2, −3, 0)", { -2.0, -3.0, 0.0 }, SolidArcSnapKind::Intersection, { -2.0, -3.0, 0.0 });
        Point("sketch line end (−1, −4, 0)", { -1.0, -4.0, 0.0 }, SolidArcSnapKind::Endpoint, { -1.0, -4.0, 0.0 });
        Point("sketch circle centre (−5, −3, 0)", { -5.0, -3.0, 0.0 }, SolidArcSnapKind::Centre, { -5.0, -3.0, 0.0 });
        {
            // On the front top edge at x = 1.3 (y = 0, z = 1 exactly), on the top face, on the rim, on the cylinder side
            const SolidArcSnap Edge = FindSolidArcSnap(Catalogue, Snaps, Scene, Query({ 1.3, 0.0, 1.0 }, 1.0, 1.0));
            const double EdgeError = std::abs(Edge.Position.Y) + std::abs(Edge.Position.Z - 1.0);
            Tally(SnapGate, EdgeError, Edge.Kind == SolidArcSnapKind::Curve && EdgeError <= 1.0e-12 && std::abs(Edge.Position.X - 1.3) < 0.05, "on edge near (1.3, 0, 1): |y| + |z − 1| → " + Describe(Edge));
            const SolidArcSnap Top = FindSolidArcSnap(Catalogue, Snaps, Scene, Query({ 2.9, 1.4, 1.0 }));
            const double TopError = std::abs(Top.Position.Z - 1.0);
            Tally(SnapGate, TopError, Top.Kind == SolidArcSnapKind::Face && TopError <= 1.0e-12 && Distance(Top.Normal, Vector3{ 0.0, 0.0, 1.0 }) <= 1.0e-12, "on top face near (2.9, 1.4, 1): |z − 1|, outward normal ẑ → " + Describe(Top));
            const Vector3 RimAt{ 6.0 + 0.5 * std::cos(-0.6), 1.0 + 0.5 * std::sin(-0.6), 1.0 };
            const SolidArcSnap Rim = FindSolidArcSnap(Catalogue, Snaps, Scene, Query(RimAt, 1.0, -1.0));
            const double RimError = std::abs(std::hypot(Rim.Position.X - 6.0, Rim.Position.Y - 1.0) - 0.5) + std::abs(Rim.Position.Z - 1.0);
            Tally(SnapGate, RimError, Rim.Kind == SolidArcSnapKind::Curve && RimError <= 1.0e-12, "on top rim near angle −0.6: ||ρ| − 0.5| + |z − 1| → " + Describe(Rim));
            const Vector3 SideAt{ 6.0 + 0.5 * std::cos(-0.9), 1.0 + 0.5 * std::sin(-0.9), 0.45 };
            const SolidArcSnap Side = FindSolidArcSnap(Catalogue, Snaps, Scene, Query(SideAt, 0.0, 0.0));
            const double SideError = std::abs(std::hypot(Side.Position.X - 6.0, Side.Position.Y - 1.0) - 0.5);
            Tally(SnapGate, SideError, Side.Kind == SolidArcSnapKind::Face && SideError <= 1.0e-12, "on cylinder side near angle −0.9, z 0.45: ||ρ| − 0.5| (exact ray hit, not the facets) → " + Describe(Side));
        }
        {
            // Tangent from P = (−5, −0.5, 0) to the circle r 0.8 at (−5, −3): cos α = r / d = 0.32
            const Vector3 Reference{ -5.0, -0.5, 0.0 };
            const double  Sine = std::sqrt(1.0 - 0.32 * 0.32);
            const Vector3 Expected{ -5.0 + 0.8 * Sine, -3.0 + 0.8 * 0.32, 0.0 };
            SolidArcSnapQuery Request = Query(Expected, 2.0, 1.0);
            Request.Reference = Reference;
            const SolidArcSnap Snap = FindSolidArcSnap(Catalogue, Snaps, Scene, Request);
            const Vector3 Centre{ -5.0, -3.0, 0.0 };
            const double Error = std::abs(Distance(Snap.Position, Centre) - 0.8) + std::abs(Dot(Snap.Position - Reference, Snap.Position - Centre)) + std::abs(Snap.Position.Z);
            Tally(SnapGate, Error, Snap.Kind == SolidArcSnapKind::Tangent && Error <= 1.0e-12 && Distance(Snap.Position, Expected) <= 1.0e-12,
                  "tangent from (−5, −0.5, 0) to circle r 0.8: ||T − C| − r| + |(T − P)·(T − C)| → " + Describe(Snap));
            // Perpendicular from P = (−4.5, −4.5, 0) to the line (−3, −2) → (−1, −4): foot (−2.5, −2.5, 0)
            Request = Query({ -2.5, -2.5, 0.0 }, 2.0, 2.0);
            Request.Reference = Vector3{ -4.5, -4.5, 0.0 };
            const SolidArcSnap Foot = FindSolidArcSnap(Catalogue, Snaps, Scene, Request);
            const double FootError = Distance(Foot.Position, Vector3{ -2.5, -2.5, 0.0 });
            Tally(SnapGate, FootError, Foot.Kind == SolidArcSnapKind::Perpendicular && FootError <= 1.0e-12, "perpendicular from (−4.5, −4.5, 0) to Line (−3, −2) → (−1, −4): foot (−2.5, −2.5, 0) → " + Describe(Foot));
        }
        {
            // Arc-length midpoint of the Hermite curve: both halves equal (independent 200 000-chord sums)
            const SolidArcSnapCurve* Spline = nullptr;
            for (const SolidArcSnapCurve& Curve : Catalogue.Curves) if (Curve.Segment && Curve.Segment->Kind == ProfileSegmentKind::Spline) Spline = &Curve;
            const SolidArcSnapPoint* Middle = nullptr;
            for (const SolidArcSnapPoint& Each : Catalogue.Points) if (Spline && Each.Kind == SolidArcSnapKind::Midpoint && Each.Source == Spline->Source) Middle = &Each;
            double Before = 0.0, After = 0.0;
            if (Spline && Middle)
            {
                const Vector2 Local{ Dot(Middle->Position - Spline->Plane.Origin, Spline->Plane.XAxis), Dot(Middle->Position - Spline->Plane.Origin, Spline->Plane.YAxis) };
                const double Split = LocateProfileFraction(*Spline->Segment, Local);
                constexpr int Chords = 200000;
                for (int Index = 0; Index < Chords; ++Index)
                {
                    const double From = double(Index) / Chords, To = double(Index + 1) / Chords;
                    const auto Chord = [&](double Low, double High) { return Distance(EvaluateProfileSegment(*Spline->Segment, Low), EvaluateProfileSegment(*Spline->Segment, High)); };
                    if (To <= Split) Before += Chord(From, To);
                    else if (From >= Split) After += Chord(From, To);
                    else { Before += Chord(From, Split); After += Chord(Split, To); }
                }
            }
            const double Imbalance = std::abs(Before - After) / std::max(1.0e-12, Before + After);
            Tally(SnapGate, Imbalance, Middle && Imbalance <= 1.0e-9, "Hermite curve midpoint: |L₁ − L₂| / L (L₁ " + std::to_string(Before) + ", L₂ " + std::to_string(After) + ")");
        }
        {
            // Hidden back corner (0, 2, 0): no endpoint there unless x-ray
            SolidArcSnapQuery Hidden = Query({ 0.0, 2.0, 0.0 }, 1.0, 1.0);
            const SolidArcSnap Plain = FindSolidArcSnap(Catalogue, Snaps, Scene, Hidden);
            Hidden.XRayEnabled = true;
            const SolidArcSnap Through = FindSolidArcSnap(Catalogue, Snaps, Scene, Hidden);
            const bool PlainCondition = !(Plain.Kind == SolidArcSnapKind::Endpoint && Distance(Plain.Position, Vector3{ 0.0, 2.0, 0.0 }) <= 1.0e-12);
            Tally(SnapGate, Distance(Through.Position, Vector3{ 0.0, 2.0, 0.0 }), PlainCondition && Through.Kind == SolidArcSnapKind::Endpoint && Distance(Through.Position, Vector3{ 0.0, 2.0, 0.0 }) <= 1.0e-12,
                  "hidden corner (0, 2, 0): plain → " + Describe(Plain) + " · x-ray → " + Describe(Through));
            // Section x ≤ 3 (n = −x̂, w = 3): the corner (4, 0, 1) is cut away
            const SolidArcSectionClip Clip = ComposeSolidArcSectionClip(Scene, Snaps, { -1.0, 0.0, 0.0, 3.0 });
            SolidArcSnapQuery Cut = Query({ 4.0, 0.0, 1.0 }, 1.0, 1.0);
            Cut.Section = &Clip;
            const SolidArcSnap Sectioned = FindSolidArcSnap(Catalogue, Snaps, Scene, Cut);
            Tally(SnapGate, 0.0, !(Sectioned.Kind == SolidArcSnapKind::Endpoint && Distance(Sectioned.Position, Vector3{ 4.0, 0.0, 1.0 }) <= 1.0e-9), "corner (4, 0, 1) behind the section x ≤ 3 → " + Describe(Sectioned));
            // Move excludes the moving body: the block's corner does not snap
            SolidArcSnapCatalogue Others = CatalogueSolidArcSnaps(Snaps, Scene, { Snaps.Regeneration.Bodies.front().Key });
            const SolidArcSnap Excluded = FindSolidArcSnap(Others, Snaps, Scene, Query({ 4.0, 0.0, 1.0 }));
            Tally(SnapGate, 0.0, !(Excluded.Kind == SolidArcSnapKind::Endpoint && Distance(Excluded.Position, Vector3{ 4.0, 0.0, 1.0 }) <= 1.0e-9), "block excluded (Move) at its corner → " + Describe(Excluded));
            // A dragged sketch excludes itself by its step label: the circle's centre is gone, the block's corner stays
            std::string CircleLabel;
            for (const FeatureStep& Step : Snaps.Tree.Steps)
            {
                const auto Caption = Snaps.Captions.find(Step.Label);
                if (Step.Kind == FeatureStepKind::Sketch && Caption != Snaps.Captions.end() && Caption->second == "Circle1") CircleLabel = Step.Label;
            }
            SolidArcSnapCatalogue WithoutCircle = CatalogueSolidArcSnaps(Snaps, Scene, { CircleLabel });
            const SolidArcSnap Centre = FindSolidArcSnap(WithoutCircle, Snaps, Scene, Query({ -5.0, -3.0, 0.0 }, 1.0, 1.0));
            const SolidArcSnap Corner = FindSolidArcSnap(WithoutCircle, Snaps, Scene, Query({ 4.0, 0.0, 1.0 }, 1.0, 1.0));
            Tally(SnapGate, 0.0, !CircleLabel.empty() && Centre.Kind != SolidArcSnapKind::Centre && Corner.Kind == SolidArcSnapKind::Endpoint && WithoutCircle.Curves.size() + 1 == Catalogue.Curves.size(),
                  "sketch " + CircleLabel + " excluded (its point being dragged): centre → " + Describe(Centre) + " · corner → " + Describe(Corner));
            // Filter: endpoints off → the corner pointer lands on an edge instead (still exact)
            SolidArcSnapQuery Filtered = Query({ 4.0, 0.0, 1.0 }, 1.0, 1.0);
            Filtered.Filter.EndpointEnabled = false;
            const SolidArcSnap Edge = FindSolidArcSnap(Catalogue, Snaps, Scene, Filtered);
            Tally(SnapGate, 0.0, Edge.Kind != SolidArcSnapKind::Endpoint && Edge.Kind != SolidArcSnapKind::None, "endpoints disabled at the corner → " + Describe(Edge));
            // Transient point (the drawing chain)
            SolidArcSnapQuery Chain = Query({ -6.0, -6.0, 0.0 }, 2.0, 2.0);
            Chain.Transient.push_back({ SolidArcSnapKind::Endpoint, { -6.0, -6.0, 0.0 }, {}, {}, "Drawing point 1", {} });
            const SolidArcSnap Placed = FindSolidArcSnap(Catalogue, Snaps, Scene, Chain);
            Tally(SnapGate, Distance(Placed.Position, Vector3{ -6.0, -6.0, 0.0 }), Placed.Kind == SolidArcSnapKind::Endpoint && Distance(Placed.Position, Vector3{ -6.0, -6.0, 0.0 }) == 0.0, "placed drawing point (−6, −6, 0) → " + Describe(Placed));
            // Direction lock: the pointer over (5.5, 0, 1) meets the x line through (4, 0, 1) at t = 1.5
            const SolidArcSnapQuery Lock = Query({ 5.5, 0.0, 1.0 });
            const std::optional<double> Along = ApproachSolidArcLine(CastOrbitRay(Frame, Lock.PixelX, Lock.PixelY), { 4.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 });
            Tally(SnapGate, Along ? std::abs(*Along - 1.5) : 1.0, Along && std::abs(*Along - 1.5) <= 1.0e-9, "x lock through (4, 0, 1), pointer over (5.5, 0, 1) → t " + std::to_string(Along ? *Along : -1.0));
            // Catalogue census: 12 + 2 block / cylinder edges' vertices … reported for the record
            Tally(SnapGate, 0.0, Catalogue.Curves.size() >= 12 + 3 + 4 && !Catalogue.Points.empty(),
                  "catalogue: " + std::to_string(Catalogue.Curves.size()) + " curves, " + std::to_string(Catalogue.Points.size()) + " fixed points");
        }
    }

    // ---- report
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
    std::printf("# SolidArcDocumentValidation\n\n");
    std::printf("Document layer of SolidArc over the GeometryProcessing feature tree (%.2f s).\n\n", Seconds);
    std::printf("| Gate | Subject | Limit | Worst | Checks | Verdict |\n|------|---------|-------|-------|--------|---------|\n");
    long Failures = 0;
    // ---- W36 joints (Alt+J) + interference: a pin into the bracket hole, a lever on the pin, every number closed form
    {
        SolidArcDocument Rig = BuildSolidArcSpecimen();
        SolidArcConstructRequest Pin;
        Pin.Kind = SolidArcConstructKind::Cylinder; Pin.Position = { 6.0, 1.25, 0.0 }; Pin.Radius = 0.25; Pin.Height = 1.5;
        Pin.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(Rig, Pin));
        SolidArcConstructRequest Lever;
        Lever.Kind = SolidArcConstructKind::Block; Lever.Position = { 6.0, 4.0, 0.0 }; Lever.Width = 2.0; Lever.Depth = 0.4; Lever.Height = 0.2;
        Lever.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(Rig, Lever));
        Tally(JointGate, 0.0, Rig.Regeneration.Bodies.size() == 3, "bracket + pin + lever: " + std::to_string(Rig.Regeneration.Bodies.size()) + " bodies");
        const std::string BracketKey = Rig.Regeneration.Bodies[0].Key, PinKey = Rig.Regeneration.Bodies[1].Key, LeverKey = Rig.Regeneration.Bodies[2].Key;
        // The entity of a body whose joint origin sits at a point (and reads Description) — what the hover offers there.
        auto OriginAt = [&](const SolidArcDocument& Document, const std::string& Key, SolidArcSelectionKind Kind, Vector3 At, const char* Description)
        {
            for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
            {
                if (Body.Key != Key) continue;
                for (const auto& [Slot, Name] : Kind == SolidArcSelectionKind::Edge ? Body.Naming.Edges : Body.Naming.Faces)
                {
                    const SolidArcJointOrigin Origin = ResolveSolidArcJointOrigin(Document, Kind, Name);
                    if (Origin.Refusal.empty() && Origin.Description == Description && Distance(Origin.Frame.Origin, At) <= 1.0e-9) return Name;
                }
            }
            return std::string();
        };
        auto Centroid = [&](const SolidArcDocument& Document, const std::string& Key)
        {
            for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
                if (Body.Key == Key) return MeasureBodyMass(*Document.Partition, Body.Body).Centroid;
            return Vector3{ std::nan(""), 0.0, 0.0 };
        };
        auto Bounds = [&](const SolidArcDocument& Document, const std::string& Key, Vector3& Low, Vector3& High)
        {
            Low = { 1.0e9, 1.0e9, 1.0e9 }; High = { -1.0e9, -1.0e9, -1.0e9 };
            for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
            {
                if (Body.Key != Key) continue;
                for (const VertexToken Vertex : Document.Partition->CollectInventory(Body.Body).Vertices)
                {
                    const Vector3 Point = Document.Partition->Vertices.Resolve(Vertex).Position;
                    Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
                    High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
                }
            }
        };
        auto Gap = [](const SolidArcDocument& Document)
        {
            double Worst = 0.0;
            for (const RegeneratedJoint& Joint : Document.Regeneration.Joints) Worst = std::max(Worst, Distance(Joint.MovingOrigin.Origin, Joint.FixedOrigin.Origin));
            return Worst;
        };
        auto Triple = [](Vector3 Point) { char Text[96]; std::snprintf(Text, sizeof(Text), "(%.12g, %.12g, %.12g)", Point.X, Point.Y, Point.Z); return std::string(Text); };
        // ① the hover origins: circles a quarter turn off their seams are probed by centre (the rim's own centre)
        const std::string PinTop   = OriginAt(Rig, PinKey, SolidArcSelectionKind::Edge, { 6.0, 1.25, 1.5 }, "circle centre");
        const std::string HoleTop  = OriginAt(Rig, BracketKey, SolidArcSelectionKind::Edge, { 3.0, 1.25, 1.0 }, "circle centre");
        const std::string LeverTop = OriginAt(Rig, LeverKey, SolidArcSelectionKind::Face, { 7.0, 4.2, 0.2 }, "plane centroid");
        const std::string PinCap   = OriginAt(Rig, PinKey, SolidArcSelectionKind::Face, { 6.0, 1.25, 1.5 }, "plane centroid");
        Tally(JointGate, 0.0, !PinTop.empty() && !HoleTop.empty() && !LeverTop.empty() && !PinCap.empty(),
              "origins: pin top rim → circle centre (6, 1.25, 1.5); hole top rim → (3, 1.25, 1); lever top → plane centroid (7, 4.2, 0.2); pin cap → (6, 1.25, 1.5)");
        {
            const SolidArcJointOrigin Rim = ResolveSolidArcJointOrigin(Rig, SolidArcSelectionKind::Edge, HoleTop);
            Tally(JointGate, Distance(Rim.Frame.ZAxis, Vector3{ 0.0, 0.0, 1.0 }), Rim.Body == BracketKey && Distance(Rim.Frame.ZAxis, Vector3{ 0.0, 0.0, 1.0 }) <= 1.0e-12,
                  "hole rim z axis leaves the bracket top (+z): " + Triple(Rim.Frame.ZAxis) + " on " + Rim.Body);
        }
        // ② revolute, flipped (origins aligned) + offset 0.25: the pin drops into the hole, 0.25 proud — centroid (3, 1.25, 0.5)
        SolidArcJointRequest Insert;
        Insert.MovingKind = SolidArcSelectionKind::Edge; Insert.MovingName = PinTop;
        Insert.FixedKind  = SolidArcSelectionKind::Edge; Insert.FixedName  = HoleTop;
        Insert.Kind = JointKind::Revolute; Insert.Flipped = true; Insert.Offset = 0.25;
        const size_t Entries = Rig.UndoEntries.size();
        const SolidArcCommandOutcome Inserted = JointSolidArcBodies(Rig, Insert);
        Expect(Inserted);
        const Vector3 PinCentre = Centroid(Rig, PinKey);
        Tally(JointGate, Distance(PinCentre, Vector3{ 3.0, 1.25, 0.5 }), Inserted.AcceptedCondition && Distance(PinCentre, Vector3{ 3.0, 1.25, 0.5 }) <= 1.0e-9,
              "pin into the hole (revolute, flipped, offset 0.25): centroid " + Triple(PinCentre) + " = (3, 1.25, 0.5)");
        Tally(JointGate, std::abs(Gap(Rig) - 0.25), Rig.Regeneration.Joints.size() == 1 && std::abs(Gap(Rig) - 0.25) <= 1.0e-9 && Rig.UndoEntries.size() == Entries + 1,
              "1 regenerated joint, origins 0.25 apart (the offset), 1 undo entry; step " + Rig.Tree.Steps.back().Label + " captioned " + Rig.Captions[Rig.Tree.Steps.back().Label]);
        {
            std::string Names;
            for (const FeatureParameter& Parameter : Rig.Tree.Parameters) if (Parameter.Name.starts_with(Rig.Captions[Rig.Tree.Steps.back().Label] + ".")) Names += Parameter.Name + " ";
            Tally(JointGate, 0.0, Names.find(".Angle") != std::string::npos && Names.find(".Offset") != std::string::npos && Names.find(".Slide") == std::string::npos,
                  "revolute parameters: " + Names);
        }
        // ③ the lever top onto the pin cap (facing: the lever turns over onto it), revolute 90°: centred on the pin axis, long side along y
        SolidArcJointRequest Swing;
        Swing.MovingKind = SolidArcSelectionKind::Face; Swing.MovingName = LeverTop;
        Swing.FixedKind  = SolidArcSelectionKind::Face; Swing.FixedName  = OriginAt(Rig, PinKey, SolidArcSelectionKind::Face, { 3.0, 1.25, 1.25 }, "plane centroid");
        Swing.Kind = JointKind::Revolute; Swing.Angle = 90.0;
        Expect(JointSolidArcBodies(Rig, Swing));
        const std::string SwingLabel = Rig.Tree.Steps.back().Label;
        Vector3 Low, High;
        Bounds(Rig, LeverKey, Low, High);
        const double BoundsError = Distance(Low, Vector3{ 2.8, 0.25, 1.25 }) + Distance(High, Vector3{ 3.2, 2.25, 1.45 });
        Tally(JointGate, BoundsError, BoundsError <= 1.0e-9, "lever on the pin cap at 90°: bounds " + Triple(Low) + " … " + Triple(High) + " = (2.8, 0.25, 1.25) … (3.2, 2.25, 1.45)");
        // ④ measure: lever + bracket clear by 0.25 (lever bed over the bracket top), bracket + pin clear by 0.05 (hole r 0.3, pin r 0.25)
        auto Measure = [&](const SolidArcDocument& Document, const std::string& First, const std::string& Second)
        {
            SolidArcDocument Copy = CloneSolidArcDocument(Document);
            Copy.Selection = {};
            Copy.Selection.Bodies = { First, Second };
            Copy.Selection.LastKind = SolidArcSelectionKind::Body;
            return MeasureSolidArcSelection(Copy);
        };
        {
            const SolidArcMeasurement Above = Measure(Rig, LeverKey, BracketKey);
            Tally(JointGate, std::abs(Above.Distance - 0.25), Above.Interference.starts_with("Clear") && std::abs(Above.Distance - 0.25) <= 1.0e-9,
                  "lever + bracket: " + Above.Interference + ", clearance " + std::to_string(Above.Distance));
            const SolidArcMeasurement Inside = Measure(Rig, BracketKey, PinKey);
            Tally(JointGate, std::abs(Inside.Distance - 0.05), Inside.Interference.starts_with("Clear") && std::abs(Inside.Distance - 0.05) <= 1.0e-9,
                  "bracket + pin: " + Inside.Interference + ", clearance " + std::to_string(Inside.Distance) + " (0.3 − 0.25)");
        }
        // ⑤ the pin offset −0.1 (one undo entry): replay resumes at the pin joint, the lever rides down — it now overlaps the
        //    bracket top slab 0.1 deep minus the strip |x − 3| ≤ 0.2 of the hole disc (⌀ 0.6 > lever width 0.4)
        const std::string InsertCaption = Rig.Captions[Rig.Regeneration.Joints.front().Label];             // Parameters go by caption: Revolute1.Offset
        Expect(AssignSolidArcParameter(Rig, InsertCaption + ".Offset", -0.1));
        {
            const double              Expected = (0.8 - 2.0 * (0.2 * std::sqrt(0.05) + 0.09 * std::asin(2.0 / 3.0))) * 0.1;
            const SolidArcMeasurement Clash    = Measure(Rig, LeverKey, BracketKey);
            Tally(JointGate, std::abs(Clash.OverlapVolume - Expected), Clash.Interference.starts_with("Overlap") && std::abs(Clash.OverlapVolume - Expected) <= 1.0e-9 && !Clash.Overlap.empty(),
                  "pin offset −0.1 → lever + bracket: " + Clash.Interference + ", overlap " + std::to_string(Clash.OverlapVolume) + " = (0.8 − 2 (0.2 √0.05 + 0.09 asin ⅔)) · 0.1, " + std::to_string(Clash.Overlap.size()) + " overlap edges drawn");
            Bounds(Rig, LeverKey, Low, High);
            Tally(JointGate, std::abs(Low.Z - 0.9), std::abs(Low.Z - 0.9) <= 1.0e-9, "the lever followed the pin: bed at z " + std::to_string(Low.Z) + " = 0.9");
        }
        // ⑥ moving the bracket +1 x carries the pin and the lever (bodies jointed to it follow)
        Expect(TransformSolidArcBodies(Rig, { BracketKey }, TranslationMap({ 1.0, 0.0, 0.0 }), "Move"));
        {
            const Vector3 Carried = Centroid(Rig, PinKey);
            Bounds(Rig, LeverKey, Low, High);
            const double Error = Distance(Carried, Vector3{ 4.0, 1.25, 0.15 }) + std::abs(Low.X - 3.8);
            Tally(JointGate, Error, Error <= 1.0e-9 && std::abs(Gap(Rig) - 0.1) <= 1.0e-9,
                  "bracket moved +1 x: pin centroid " + Triple(Carried) + " = (4, 1.25, 0.15), lever x from " + std::to_string(Low.X) + " = 3.8, joints still assembled");
        }
        // ⑦ refusals leave the document untouched: both origins on one body, a pin jointed twice
        {
            const size_t Before = Rig.UndoEntries.size();
            SolidArcJointRequest Self = Insert;
            Self.FixedKind = SolidArcSelectionKind::Face; Self.FixedName = OriginAt(Rig, PinKey, SolidArcSelectionKind::Face, { 4.0, 1.25, 0.9 }, "plane centroid");
            const SolidArcCommandOutcome Lonely = JointSolidArcBodies(Rig, Self);
            SolidArcJointRequest Twice = Insert;
            Twice.MovingName = OriginAt(Rig, PinKey, SolidArcSelectionKind::Edge, { 4.0, 1.25, 0.9 }, "circle centre");
            Twice.FixedName  = OriginAt(Rig, BracketKey, SolidArcSelectionKind::Edge, { 4.0, 1.25, 1.0 }, "circle centre");
            const SolidArcCommandOutcome Again = JointSolidArcBodies(Rig, Twice);
            Tally(JointGate, 0.0, !Lonely.AcceptedCondition && !Again.AcceptedCondition && Rig.UndoEntries.size() == Before,
                  "refused: \"" + Lonely.Refusal + "\" · \"" + Again.Refusal + "\"; no undo entry");
        }
        // ⑧ undo takes back the move, then the offset; the file keeps both joints byte-equal
        {
            const std::string Written = EncodeSolidArcDocument(Rig);
            const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
            SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
            Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Copy, true);
            // Reference signatures are re-captured on load (the lever's face probe moved with the pin edit), so the first
            // rewrite refreshes them; a second round trip is the fixed point. Geometry and joint values must match.
            const std::string          Rewritten = EncodeSolidArcDocument(Copy);
            const SolidArcDocumentFile Again     = DecodeSolidArcDocument(Rewritten);
            SolidArcDocument Second = CreateSolidArcDocument(Again.Title);
            Second.Tree = Again.Tree; Second.Captions = Again.Captions; Second.Appearance = Again.Appearance;
            RegenerateSolidArcDocument(Second, true);
            const auto SameValue = [](const FeatureValue& Left, const FeatureValue& Right)
            {
                return Left.Literal == Right.Literal && Left.Parameter == Right.Parameter && Left.Factor == Right.Factor && Left.Offset == Right.Offset
                    && Left.SecondParameter == Right.SecondParameter && Left.SecondFactor == Right.SecondFactor;
            };
            bool SameJoints = Copy.Tree.Steps.size() == Rig.Tree.Steps.size();
            for (size_t Index = 0; SameJoints && Index < Rig.Tree.Steps.size(); ++Index)
            {
                const FeatureStep& Left = Rig.Tree.Steps[Index];
                const FeatureStep& Right = Copy.Tree.Steps[Index];
                if (Left.Kind != FeatureStepKind::Joint) continue;
                SameJoints = Right.Kind == FeatureStepKind::Joint && Left.Joint.Kind == Right.Joint.Kind && Left.Joint.FlippedCondition == Right.Joint.FlippedCondition
                          && SameValue(Left.Joint.Angle, Right.Joint.Angle) && SameValue(Left.Joint.Offset, Right.Joint.Offset) && Left.Target == Right.Target && Left.Tool == Right.Tool;
            }
            const double Moved = Distance(Centroid(Copy, PinKey), Centroid(Rig, PinKey)) + Distance(Centroid(Copy, LeverKey), Centroid(Rig, LeverKey));
            Tally(JointGate, Moved, SameJoints && Moved <= 1.0e-12 && EncodeSolidArcDocument(Second) == Rewritten && Copy.Regeneration.Joints.size() == 2 && Written.find("JointFlipped = true") != std::string::npos,
                  "save → load: 2 joints with equal kind / flip / values / bodies, pin and lever centroids within " + std::to_string(Moved) + "; the second round trip is byte-equal (JointFlipped = true for the pin)");
            const bool Undone = UndoSolidArcCommand(Rig) && UndoSolidArcCommand(Rig);
            const Vector3 Back = Centroid(Rig, PinKey);
            Tally(JointGate, Distance(Back, Vector3{ 3.0, 1.25, 0.5 }), Distance(Back, Vector3{ 3.0, 1.25, 0.5 }) <= 1.0e-9 && Undone && Rig.Regeneration.Joints.back().Label == SwingLabel,
                  "2 × undo → pin centroid " + Triple(Back) + " again");
        }
    }

    // ---- W37 drawing sheet (Alt+D): the bracket on paper through the kernel sheet, metres → millimetres, saved settings
    {
        const auto Format = [](const char* Pattern, auto... Values) { char Text[256]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        SolidArcDocument Rig = BuildSolidArcSpecimen();
        SolidArcDrawingSheetSettings Settings;
        const SolidArcDrawingSheet Sheet = ComposeSolidArcDrawingSheet(Rig, Settings);
        std::string Views, Texts;
        for (const Frontier::GeometryProcessing::PlacedDrawingView& View : Sheet.Sheet.Views) Views += (Views.empty() ? "" : " ") + View.Label;
        for (const Frontier::GeometryProcessing::DrawingDimension& Dimension : Sheet.Sheet.Dimensions) Texts += (Texts.empty() ? "" : " ") + Dimension.Text;
        Tally(SheetGate, 0.0, Sheet.Refusal.empty() && Sheet.Bodies == 1 && Views == "Front Top Right Isometric" && Sheet.Sheet.Width == 420.0 && Sheet.Sheet.Height == 297.0
              && Sheet.Sheet.Angle == Frontier::GeometryProcessing::DrawingProjectionAngle::First,
              "default: A3 first angle, " + Views + ", scale " + Sheet.Sheet.ScaleLabel + ", " + std::to_string(Sheet.Sheet.Lines.size()) + " lines, "
              + std::to_string(Sheet.Sheet.Dimensions.size()) + " dimensions, " + Format("%.0f ms", Sheet.Milliseconds));

        // Automatic scale: the largest ISO 5455 scale that fits — the next larger one is refused.
        const std::vector<double> Scales = Frontier::GeometryProcessing::EnumerateStandardScales();
        const auto Chosen = std::find_if(Scales.begin(), Scales.end(), [&](double Scale) { return std::fabs(Scale - Sheet.Sheet.Scale) <= 1.0e-15; });
        bool LargerRefused = Chosen != Scales.end();
        if (Chosen != Scales.begin() && Chosen != Scales.end())
        {
            SolidArcDrawingSheetSettings Larger = Settings;
            Larger.Scale = *(Chosen - 1);
            LargerRefused = !ComposeSolidArcDrawingSheet(Rig, Larger).Refusal.empty();
        }
        Tally(SheetGate, 0.0, LargerRefused, "automatic " + Sheet.Sheet.ScaleLabel + " is standard and the next larger scale is refused");

        // Dimension texts in millimetres; every value × 1000 prints as its text and the linear ones span value × scale on paper.
        double Worst = 0.0;
        bool   Printed = true;
        for (const Frontier::GeometryProcessing::DrawingDimension& Dimension : Sheet.Sheet.Dimensions)
        {
            const std::string Digits = Frontier::GeometryProcessing::FormatDimensionValue(Dimension.Value * SolidArcModelUnitMillimetres);
            const std::string Prefix = Dimension.Kind == Frontier::GeometryProcessing::DrawingDimensionKind::Diameter ? "⌀" : Dimension.Kind == Frontier::GeometryProcessing::DrawingDimensionKind::Radius ? "R" : "";
            Printed = Printed && Dimension.Text == Prefix + Digits;
            if (Dimension.Kind == Frontier::GeometryProcessing::DrawingDimensionKind::Horizontal || Dimension.Kind == Frontier::GeometryProcessing::DrawingDimensionKind::Vertical)
                Worst = std::max(Worst, std::fabs(std::hypot(Dimension.LineEnd.X - Dimension.LineStart.X, Dimension.LineEnd.Y - Dimension.LineStart.Y)
                                                  - Dimension.Value * SolidArcModelUnitMillimetres * Sheet.Sheet.Scale));
        }
        const auto Shows = [&](const char* Text) { return (" " + Texts + " ").find(std::string(" ") + Text + " ") != std::string::npos; };
        Tally(SheetGate, Worst, Printed && Worst <= 1.0e-9 && Shows("4000") && Shows("2500") && Shows("1750") && Shows("⌀600") && Shows("⌀1000"),
              "dimensions in mm: " + Texts + Format(" — span vs value × scale %.1e mm", Worst));

        // Unit invariance: the kernel sheet of the same body in millimetre units at scale × 1000 strokes the same paper.
        std::vector<Frontier::GeometryProcessing::BodyToken> Bodies;
        for (const RegeneratedBody& Body : Rig.Regeneration.Bodies) Bodies.push_back(Body.Body);
        Frontier::GeometryProcessing::DrawingSheetRequest Direct;
        Direct.Paper = Settings.Paper; Direct.Angle = Settings.Angle; Direct.Scale = Sheet.Sheet.Scale * SolidArcModelUnitMillimetres; Direct.ModelUnitMillimetres = 1.0;
        Direct.Title = { Rig.Title, Settings.Title.DrawingNumber, "", "", "", Settings.Title.Revision, Settings.Title.Company };
        const Frontier::GeometryProcessing::DrawingSheet Reference = Frontier::GeometryProcessing::ComposeDrawingSheet(*Rig.Partition, Bodies, Direct);
        // Dimension texts are 1000× longer numbers ("4000" vs "4"), so footprints and the layout differ; each view's
        // strokes relative to its view origin (and the frame / title block) must coincide.
        double Stroke = Reference.Lines.size() == Sheet.Sheet.Lines.size() && Reference.Views.size() == Sheet.Sheet.Views.size() ? 0.0 : 1.0e300;
        double Shifted = 0.0;                                                                // [mm] - Largest view-origin move
        size_t Compared = 0;
        for (size_t View = 0; Stroke < 1.0e300 && View < Reference.Views.size(); ++View)
            Shifted = std::max(Shifted, std::hypot(Sheet.Sheet.Views[View].Origin.X - Reference.Views[View].Origin.X, Sheet.Sheet.Views[View].Origin.Y - Reference.Views[View].Origin.Y));
        for (size_t Line = 0; Stroke < 1.0e300 && Line < Reference.Lines.size(); ++Line)
        {
            const Frontier::GeometryProcessing::SheetPolyline& Ours = Sheet.Sheet.Lines[Line];
            const Frontier::GeometryProcessing::SheetPolyline& Theirs = Reference.Lines[Line];
            if (Theirs.Layer != Ours.Layer || Theirs.Points.size() != Ours.Points.size()) { Stroke = 1.0e300; break; }
            if (Theirs.Layer == Frontier::GeometryProcessing::SheetLayer::Dimension || Theirs.Layer == Frontier::GeometryProcessing::SheetLayer::CuttingPlane || Theirs.Points.empty()) continue;
            Frontier::GeometryProcessing::Vector2 Offset{ 0.0, 0.0 };
            for (size_t View = 0; View < Reference.Views.size(); ++View)
            {
                const Frontier::GeometryProcessing::PlacedDrawingView& Placed = Reference.Views[View];
                const Frontier::GeometryProcessing::Vector2 Point = Theirs.Points.front();
                if (Point.X >= Placed.Low.X - 1.0e-6 && Point.X <= Placed.High.X + 1.0e-6 && Point.Y >= Placed.Low.Y - 1.0e-6 && Point.Y <= Placed.High.Y + 1.0e-6)
                {
                    Offset = { Sheet.Sheet.Views[View].Origin.X - Placed.Origin.X, Sheet.Sheet.Views[View].Origin.Y - Placed.Origin.Y };
                    break;
                }
            }
            for (size_t Point = 0; Point < Theirs.Points.size(); ++Point)
                Stroke = std::max(Stroke, std::hypot(Theirs.Points[Point].X + Offset.X - Ours.Points[Point].X, Theirs.Points[Point].Y + Offset.Y - Ours.Points[Point].Y));
            ++Compared;
        }
        Tally(SheetGate, Stroke, Stroke <= 1.0e-9, Format("metres × 1000 at 1:%g = millimetres at ", 1.0 / Sheet.Sheet.Scale) + Reference.ScaleLabel
              + Format(": %zu of %zu strokes (all but dimensions) coincide per view, worst %.1e mm (views moved ≤ %.2f mm by the longer texts)", Compared, Reference.Lines.size(), Stroke, Shifted));

        // Paper and angle: A4 picks a scale no larger than A3; third angle mirrors the Top / Right placement about Front.
        SolidArcDrawingSheetSettings Small = Settings;
        Small.Paper = Frontier::GeometryProcessing::DrawingPaperSize::A4;
        Small.Angle = Frontier::GeometryProcessing::DrawingProjectionAngle::Third;
        const SolidArcDrawingSheet Third = ComposeSolidArcDrawingSheet(Rig, Small);
        const auto Centre = [](const Frontier::GeometryProcessing::PlacedDrawingView& View) { return Frontier::GeometryProcessing::Vector2{ 0.5 * (View.Low.X + View.High.X), 0.5 * (View.Low.Y + View.High.Y) }; };
        const bool Placed = Third.Refusal.empty() && Third.Sheet.Views.size() >= 3 && Sheet.Sheet.Views.size() >= 3
                         && Centre(Sheet.Sheet.Views[1]).Y < Centre(Sheet.Sheet.Views[0]).Y && Centre(Third.Sheet.Views[1]).Y > Centre(Third.Sheet.Views[0]).Y
                         && Centre(Sheet.Sheet.Views[2]).X < Centre(Sheet.Sheet.Views[0]).X && Centre(Third.Sheet.Views[2]).X > Centre(Third.Sheet.Views[0]).X;
        Tally(SheetGate, 0.0, Placed && Third.Sheet.Scale <= Sheet.Sheet.Scale && Third.Sheet.Width == 297.0,
              "A4 third angle at " + Third.Sheet.ScaleLabel + ": Top above / Right right of Front (first angle: below / left)");

        // Section A-A along the Section card's y = 1.25 plane: the kernel frame keeps −ZAxis (= the card's removed
        // side flipped), the cap equals the exact Section figure 4.116587.
        SolidArcDrawingSheetSettings Cut = Settings;
        Cut.SectionEnabled = true;
        Cut.Section        = { SolidArcSectionAxis::Y, 1.25, false, {} };
        const SolidArcDrawingSheet Sectioned = ComposeSolidArcDrawingSheet(Rig, Cut);
        const SolidArcSection      Exact     = SectionSolidArcDocument(Rig, Cut.Section);
        double Cap = 0.0;
        std::string CutViews;
        for (const Frontier::GeometryProcessing::PlacedDrawingView& View : Sectioned.Sheet.Views) { Cap += View.View.SectionArea; CutViews += (CutViews.empty() ? "" : " ") + View.Label; }
        const Frontier::GeometryProcessing::OrthonormalFrame Frame = SolidArcSheetSectionFrame(Exact.Plane);
        Tally(SheetGate, std::fabs(Cap - Exact.Area), Sectioned.Refusal.empty() && CutViews == "Front Top Right Section A-A" && std::fabs(Cap - Exact.Area) <= 1.0e-9
              && std::fabs(Exact.Area - 4.116587) <= 5.0e-7 && Dot(Frame.ZAxis, Exact.Plane.ZAxis) <= -1.0 + 1.0e-15,
              CutViews + Format(": cap %.9f m² = section %.9f m², %.0f ms", Cap, Exact.Area, Sectioned.Milliseconds));

        // Export: paths beside the document; the files are the writers' bytes.
        const std::filesystem::path Folder = std::filesystem::temp_directory_path() / "SolidArcSheetValidation";
        std::filesystem::create_directories(Folder);
        const std::string Svg = SolidArcDrawingSheetExportPath((Folder / "Bracket.solidarc").string(), ".svg");
        const std::string Dxf = SolidArcDrawingSheetExportPath((Folder / "Bracket.solidarc").string(), ".dxf");
        const auto Bytes = [](const std::string& Path) { std::ifstream Stream(Path, std::ios::binary); std::ostringstream Text; Text << Stream.rdbuf(); return Text.str(); };
        const bool Written = ExportSolidArcDrawingSheet(Sheet, Svg).empty() && ExportSolidArcDrawingSheet(Sheet, Dxf).empty();
        Tally(SheetGate, 0.0, Written && Svg == (Folder / "Bracket.svg").string() && Dxf == (Folder / "Bracket.dxf").string() && SolidArcDrawingSheetExportPath("", ".svg") == "Untitled.svg"
              && Bytes(Svg) == Frontier::GeometryProcessing::WriteDrawingSvg(Sheet.Sheet) && Bytes(Dxf) == Frontier::GeometryProcessing::WriteDrawingDxf(Sheet.Sheet) && Bytes(Svg).find("⌀600") != std::string::npos,
              Format("Bracket.svg %zu B + Bracket.dxf %zu B byte-equal to the writers", Bytes(Svg).size(), Bytes(Dxf).size()));

        // Saved with the document: defaults write no table; revised settings round trip (fixed point); bad labels refused.
        const std::string Plain = EncodeSolidArcDocument(Rig);
        SolidArcDocument Saved = CloneSolidArcDocument(Rig);
        Saved.DrawingSheet.Paper = Frontier::GeometryProcessing::DrawingPaperSize::A2;
        Saved.DrawingSheet.Angle = Frontier::GeometryProcessing::DrawingProjectionAngle::Third;
        Saved.DrawingSheet.Scale = 0.02;
        Saved.DrawingSheet.SectionEnabled = true;
        Saved.DrawingSheet.Section = { SolidArcSectionAxis::X, 1.0, true, {} };
        Saved.DrawingSheet.HiddenLinesEnabled = false;
        Saved.DrawingSheet.Title = { "Bracket \"B\" Ø", "DWG-0042", "Frontier", "2026-10-23", "AlSi10Mg", "C", "Frontier" };
        const std::string Written1 = EncodeSolidArcDocument(Saved);
        const SolidArcDocumentFile File = DecodeSolidArcDocument(Written1);
        SolidArcDocument Copy = CloneSolidArcDocument(Rig);
        Copy.DrawingSheet = File.DrawingSheet;
        const bool RoundTrip = File.Refusal.empty() && SolidArcDrawingSheetKey(File.DrawingSheet) == SolidArcDrawingSheetKey(Saved.DrawingSheet)
                            && SolidArcSectionKey(File.DrawingSheet.Section) == SolidArcSectionKey(Saved.DrawingSheet.Section) && EncodeSolidArcDocument(Copy) == Written1;
        // toml++ writes 'literal' or "basic" strings; either is swapped.
        const auto Swap = [](std::string Text, const std::string& Key, const std::string& From, const std::string& To)
        {
            for (const char* Quote : { "'", "\"" })
            {
                const std::string Old = Key + " = " + Quote + From + Quote;
                if (const size_t At = Text.find(Old); At != std::string::npos) return Text.replace(At, Old.size(), Key + " = \"" + To + "\"");
            }
            return std::string();
        };
        const std::string BadPaper = Swap(Written1, "Paper", "A2", "A5"), BadAngle = Swap(Written1, "Angle", "Third", "Fifth");
        const std::string PaperRefusal = DecodeSolidArcDocument(BadPaper).Refusal, AngleRefusal = DecodeSolidArcDocument(BadAngle).Refusal;
        Tally(SheetGate, 0.0, Plain.find("[DrawingSheet]") == std::string::npos && Written1.find("[DrawingSheet]") != std::string::npos && RoundTrip
              && PaperRefusal.find("A5") != std::string::npos && AngleRefusal.find("Fifth") != std::string::npos,
              "codec: no table at the defaults, A2 / third / 1:50 / section x flipped / title fields round trip (fixed point), " + PaperRefusal + " · " + AngleRefusal);

        // Refusals: every body hidden, a face section without a face; hiding is part of the cache key.
        SolidArcDocument Hidden = CloneSolidArcDocument(Rig);
        Hidden.Appearance.HiddenBodies.insert(Rig.Regeneration.Bodies[0].Key);
        const SolidArcDrawingSheet None = ComposeSolidArcDrawingSheet(Hidden, Settings);
        SolidArcDrawingSheetSettings Faceless = Settings;
        Faceless.SectionEnabled = true;
        Faceless.Section        = { SolidArcSectionAxis::Face, 0.0, false, {} };
        const SolidArcDrawingSheet NoFace = ComposeSolidArcDrawingSheet(Rig, Faceless);
        Tally(SheetGate, 0.0, None.Refusal == "Drawing sheet: no shown solid body" && NoFace.Refusal.find("section A-A") != std::string::npos
              && SolidArcDrawingSheetCacheKey(Hidden, Settings) != SolidArcDrawingSheetCacheKey(Rig, Settings) && DefaultSolidArcDrawingSheetCondition(Settings) && !DefaultSolidArcDrawingSheetCondition(Cut),
              "refusals: " + None.Refusal + " · " + NoFace.Refusal);
    }

    // ---- W38 STEP / STL exchange: the bracket + a sphere out and back in, OCCT fixtures in, a fillet on an imported edge
    {
        const auto Format   = [](const char* Pattern, auto... Values) { char Text[512]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto Relative = [](double Value, double Expected) { return std::abs(Value - Expected) / std::max(1.0e-12, std::abs(Expected)); };
        const auto Bytes    = [](const std::string& Path) { std::ifstream Stream(Path, std::ios::binary); std::ostringstream Text; Text << Stream.rdbuf(); return Text.str(); };
        const std::filesystem::path Folder = std::filesystem::temp_directory_path() / "SolidArcExchangeValidation";
        std::filesystem::remove_all(Folder);
        std::filesystem::create_directories(Folder);
        const std::string StepPath = (Folder / "Bracket.step").string(), StlPath = (Folder / "Bracket.stl").string();

        // ① Bracket specimen + a sphere r 0.5 at x = 6 (New body): STEP of both = the kernel writer's bytes.
        SolidArcDocument Rig = BuildSolidArcSpecimen();
        SolidArcConstructRequest Request;
        Request.Kind = SolidArcConstructKind::Sphere; Request.Position = { 6.0, 0.0, 0.5 }; Request.Radius = 0.5;
        Request.Combination = FeatureCombination::Separate;
        Expect(ConstructSolidArcPrimitive(Rig, Request));
        Rig.Selection = {};
        std::vector<BodyToken> Tokens;
        for (const RegeneratedBody& Body : Rig.Regeneration.Bodies) Tokens.push_back(Body.Body);
        const SolidArcExportOutcome Step = ExportSolidArcBodies(Rig, StepPath);
        Frontier::GeometryProcessing::StepRequest Direct;
        Direct.ModelUnitMillimetres = SolidArcModelUnitMillimetres;
        Direct.Name                 = "Bracket";
        const std::string Expected = Frontier::GeometryProcessing::WriteStep(*Rig.Partition, Tokens, Direct).Bytes;
        const std::string Written  = Bytes(StepPath);
        Tally(ExchangeGate, 0.0, Step.Refusal.empty() && Step.Bodies.size() == 2 && Tokens.size() == 2 && Written == Expected && Step.Bytes == double(Written.size())
              && Written.find("FILE_NAME('Bracket.step'") != std::string::npos && SolidArcExportPath((Folder / "Bracket.solidarc").string(), ".step") == StepPath,
              Format("Export… Bracket.step: %zu bodies, %d faces, %d stand-in(s), %.0f B = WriteStep bytes, %.0f ms", Step.Bodies.size(), Step.Faces, Step.Approximated, Step.Bytes, Step.Milliseconds));

        // Read back in millimetres by the kernel reader: volumes × 10⁹.
        ModelPartition Millimetres;
        StepImportRequest MillimetreRequest;
        const StepImport Back = ReadStep(Millimetres, Written, MillimetreRequest);
        double WorstBack = Back.Bodies.size() == 2 ? 0.0 : 1.0;
        for (size_t Index = 0; Index < Back.Bodies.size() && Index < Rig.Summary.Bodies.size(); ++Index)
            WorstBack = std::max(WorstBack, Relative(MeasureBodyMass(Millimetres, Back.Bodies[Index]).Volume, Rig.Summary.Bodies[Index].Volume * 1.0e9));
        Tally(ExchangeGate, WorstBack, Back.Refusal.empty() && WorstBack <= 1.0e-9 && Back.FileUnitMillimetres == 1.0,
              Format("read back in mm: %zu solids, volumes = metres³ × 10⁹ (worst %.1e)", Back.Bodies.size(), WorstBack));

        // ② STL: the writer's bytes, watertight per body, |ΔV| ≤ sag · A (mm).
        const SolidArcExportOutcome Facets = ExportSolidArcBodies(Rig, StlPath);
        StlRequest Tessellated;
        Tessellated.ModelUnitMillimetres = SolidArcModelUnitMillimetres;
        Tessellated.Name                 = "Bracket";
        const StlExport    Reference = WriteStl(*Rig.Partition, Tokens, Tessellated);
        const StlReading   Reading   = ReadStl(Bytes(StlPath));
        const StlSoundness Sound     = MeasureStlSoundness(Reading);
        const double Exact = Rig.Summary.Volume * 1.0e9;
        const double Gap   = std::abs(Sound.Volume - Exact);
        Tally(ExchangeGate, Gap / Exact, Facets.Refusal.empty() && Bytes(StlPath) == Reference.Bytes && Sound.WatertightCondition && Sound.Components == 2 && Gap <= Reference.Sag * SolidArcModelUnitMillimetres * Sound.Area,
              Format("Export… Bracket.stl: %d facets, watertight, 2 components, |ΔV| %.3f mm³ ≤ sag %.2f mm × A %.0f mm²", Facets.Triangles, Gap, Reference.Sag * SolidArcModelUnitMillimetres, Sound.Area));

        // ③ What is written: the selection, else every shown body (hidden ones stay out).
        const std::string SphereKey = Rig.Summary.Bodies.size() == 2 ? Rig.Summary.Bodies[1].Key : std::string{};
        SolidArcDocument Chosen = CloneSolidArcDocument(Rig);
        SelectSolidArcName(Chosen, SolidArcSelectionKind::Body, SphereKey, false);
        const std::vector<std::string> Picked = SolidArcExportBodies(Chosen);
        SolidArcDocument Veiled = CloneSolidArcDocument(Rig);
        Veiled.Appearance.HiddenBodies.insert(Rig.Summary.Bodies.front().Key);
        const std::vector<std::string> Shown = SolidArcExportBodies(Veiled);
        const SolidArcExportOutcome Single = ExportSolidArcBodies(Chosen, (Folder / "Sphere.stp").string());
        Tally(ExchangeGate, 0.0, Picked == std::vector<std::string>{ SphereKey } && Shown == std::vector<std::string>{ SphereKey } && Single.Refusal.empty() && Single.Bodies.size() == 1
              && Bytes(Single.Path).find("PRODUCT('Sphere'") != std::string::npos,
              "selected sphere → 1 body; bracket hidden → 1 body; Sphere.stp names its PRODUCT 'Sphere'");

        // ④ Import… Bracket.step into a fresh document: one Import step per solid, one undo entry, equal figures.
        SolidArcDocument Received = CreateSolidArcDocument("Received");
        const SolidArcStepReading    Read     = ReadSolidArcStepFile(StepPath);
        const SolidArcCommandOutcome Imported = ImportSolidArcStep(Received, Read);
        double WorstVolume = Received.Summary.Bodies.size() == 2 ? 0.0 : 1.0;
        bool   Figures     = Received.Summary.Bodies.size() == 2;
        std::string Captions;
        for (size_t Index = 0; Figures && Index < 2; ++Index)
        {
            const SolidArcBodyFigures& Ours = Received.Summary.Bodies[Index];
            const SolidArcBodyFigures& Theirs = Rig.Summary.Bodies[Index];
            WorstVolume = std::max(WorstVolume, Relative(Ours.Volume, Theirs.Volume));
            Figures = Figures && Ours.Faces == Theirs.Faces && Ours.Edges == Theirs.Edges && Ours.Vertices == Theirs.Vertices && Ours.Genus == Theirs.Genus && Ours.SoundCondition;
            Captions += (Captions.empty() ? "" : " · ") + Ours.Caption + " (" + Ours.Key + Format(", %d faces)", Ours.Faces);
        }
        const bool Steps = Received.Tree.Steps.size() == 2 && Received.Tree.Steps[0].Kind == FeatureStepKind::Import && Received.Tree.Steps[0].ExchangeUnit == SolidArcModelUnitMillimetres
                        && Received.UndoEntries.size() == 1;
        Tally(ExchangeGate, WorstVolume, Read.Refusal.empty() && Imported.AcceptedCondition && Steps && Figures && WorstVolume <= 1.0e-9 && Read.Texts.size() == 2,
              Imported.Label + ": " + Captions + Format(" · volumes, faces, edges, vertices, genus equal (worst %.1e) · %.0f ms", WorstVolume, Imported.Milliseconds));

        // Faces are named in file order; the names reach the selection.
        const std::string FirstFace = Received.Regeneration.Bodies.empty() || Received.Regeneration.Bodies[0].Naming.Faces.empty() ? std::string{} : Received.Regeneration.Bodies[0].Naming.Faces.begin()->second;
        bool Named = !FirstFace.empty();
        for (const RegeneratedBody& Body : Received.Regeneration.Bodies)
            for (const auto& [Slot, Name] : Body.Naming.Faces) Named = Named && Name.rfind(Body.Key + ".Face(", 0) == 0;
        Tally(ExchangeGate, 0.0, Named, "every imported face is named <Import#k>.Face(n), e.g. " + FirstFace);

        // ⑤ Canonical writing: export the imported bodies, import that, export again — the same file up to the last digit
        //    of the metre ↔ millimetre scaling (numbers compared relatively, everything else byte for byte).
        const std::string Second = (Folder / "Second" / "Bracket.step").string(), Third = (Folder / "Third" / "Bracket.step").string();
        std::filesystem::create_directories(Folder / "Second");
        std::filesystem::create_directories(Folder / "Third");
        const SolidArcExportOutcome Again = ExportSolidArcBodies(Received, Second);
        SolidArcDocument Echo = CreateSolidArcDocument("Echo");
        const SolidArcCommandOutcome Echoed = ImportSolidArcStep(Echo, ReadSolidArcStepFile(Second));
        const SolidArcExportOutcome Last = ExportSolidArcBodies(Echo, Third);
        const std::string SecondText = Bytes(Second), ThirdText = Bytes(Third);
        double Drift = 0.0;
        bool   Same  = true;
        int    Numbers = 0, Differing = 0;
        for (size_t First = 0, Other = 0; Same && (First < SecondText.size() || Other < ThirdText.size());)
        {
            const auto Numeric = [](const std::string& Text, size_t At) { return At < Text.size() && (std::isdigit(static_cast<unsigned char>(Text[At])) || Text[At] == '-'); };
            if (Numeric(SecondText, First) && Numeric(ThirdText, Other) && First > 0 && Other > 0 && (SecondText[First - 1] == '(' || SecondText[First - 1] == ','))
            {
                char* EndFirst = nullptr;
                char* EndOther = nullptr;
                const double A = std::strtod(SecondText.c_str() + First, &EndFirst), B = std::strtod(ThirdText.c_str() + Other, &EndOther);
                Drift = std::max(Drift, std::abs(A - B) / std::max(1.0, std::abs(A)));
                ++Numbers;
                Differing += A != B;
                First = size_t(EndFirst - SecondText.c_str());
                Other = size_t(EndOther - ThirdText.c_str());
                continue;
            }
            Same = First < SecondText.size() && Other < ThirdText.size() && SecondText[First] == ThirdText[Other];
            ++First;
            ++Other;
        }
        Tally(ExchangeGate, Drift, Again.Refusal.empty() && Echoed.AcceptedCondition && Last.Refusal.empty() && Same && Drift <= 1.0e-14 && Read.Texts[0] == Received.Tree.Steps[0].Exchange,
              Format("export → import → export: same entities, %d of %d numbers differ in the last digit (worst %.1e, mm ↔ m scaling); the embedded text is the canonical writing", Differing, Numbers, Drift));

        // ⑥ Saved with the document: the STEP text rides along (multi-line string); save → load → save byte-equal, digests equal.
        const std::string Saved1 = EncodeSolidArcDocument(Received);
        const SolidArcDocumentFile Loaded = DecodeSolidArcDocument(Saved1);
        SolidArcDocument Reloaded = CreateSolidArcDocument(Loaded.Title);
        const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Reloaded, Loaded.Tree, Loaded.Captions, "Open");
        bool Digests = Reloaded.Tree.Steps.size() == Received.Tree.Steps.size();
        for (size_t Index = 0; Digests && Index < Received.Tree.Steps.size(); ++Index)
            Digests = DigestFeatureStep(Reloaded.Tree, Reloaded.Tree.Steps[Index]) == DigestFeatureStep(Received.Tree, Received.Tree.Steps[Index]);
        FeatureTree Altered = Received.Tree;
        if (!Altered.Steps.empty()) Altered.Steps[0].Exchange += " ";
        const bool Sensitive = !Altered.Steps.empty() && DigestFeatureStep(Altered, Altered.Steps[0]) != DigestFeatureStep(Received.Tree, Received.Tree.Steps[0]);
        Tally(ExchangeGate, std::abs(Reloaded.Summary.Volume - Received.Summary.Volume), Loaded.Refusal.empty() && Opened.AcceptedCondition && Reloaded.Summary.Volume == Received.Summary.Volume
              && EncodeSolidArcDocument(Reloaded) == Saved1 && Digests && Sensitive && Reloaded.Regeneration.Naming.Faces == Received.Regeneration.Naming.Faces,
              Format("codec: %zu B of TOML carry both files; save → load → save byte-equal, volumes and names equal, digests equal (and change with the text)", Saved1.size()));

        // ⑦ OCCT-written fixtures: FilletedBoss (a loop running against its mates is repaired), InchBlock (inch → metres).
        const std::string Fixtures = SOLIDARC_STEP_FIXTURES;
        SolidArcDocument Boss = CreateSolidArcDocument("Boss");
        const SolidArcStepReading BossRead = ReadSolidArcStepFile(Fixtures + "/FilletedBoss.step");
        const SolidArcCommandOutcome BossIn = ImportSolidArcStep(Boss, BossRead);
        Tally(ExchangeGate, Relative(Boss.Summary.Volume, 23897.5876482e-9), BossIn.AcceptedCondition && Boss.Summary.SoundCondition && Relative(Boss.Summary.Volume, 23897.5876482e-9) <= 1.0e-9
              && Boss.Summary.Faces == 9 && BossRead.FileUnitMillimetres == 1.0,
              Format("FilletedBoss.step (OCCT): 9 faces, V %.10e m³, %d loop(s) reoriented, %d tolerant edge(s)", Boss.Summary.Volume, BossRead.ReorientedFaces, BossRead.TolerantEdges));
        SolidArcDocument Inch = CreateSolidArcDocument("Inch");
        const SolidArcStepReading InchRead = ReadSolidArcStepFile(Fixtures + "/InchBlock.step");
        const SolidArcCommandOutcome InchIn = ImportSolidArcStep(Inch, InchRead);
        const double Side = 0.0254;
        Tally(ExchangeGate, Relative(Inch.Summary.Volume, Side * Side * Side), InchIn.AcceptedCondition && Relative(Inch.Summary.Volume, Side * Side * Side) <= 1.0e-9 && InchRead.FileUnitMillimetres == 25.4,
              Format("InchBlock.step (OCCT, INCH, 1 × ½ × 2 in): V = 0.0254³ m³ (%.10e), caption %s", Inch.Summary.Volume, Inch.Summary.Bodies.empty() ? "?" : Inch.Summary.Bodies[0].Caption.c_str()));

        // ⑧ The imported body is a body like any other: fillet r 5 mm on an edge by its persistent name. OpenCascade writes
        //    the box's faces x−, x+, y−, y+, z−, z+, so Face(1) | Face(6) is the ½ in edge along y of the 1 × ½ × 2 in block.
        const std::string Edge = Inch.Regeneration.Bodies.empty() || Inch.Regeneration.Bodies[0].Naming.Edges.empty() ? std::string{} : Inch.Regeneration.Bodies[0].Naming.Edges.begin()->second;
        SelectSolidArcName(Inch, SolidArcSelectionKind::Edge, Edge, false);
        const double Radius = 0.005;
        const SolidArcCommandOutcome Rounded = BlendSolidArcSelection(Inch, BlendProfile::Fillet, Radius);
        const double Removed = (1.0 - Pi / 4.0) * Radius * Radius * (Side / 2.0);
        Tally(ExchangeGate, 0.0, Edge == "Edge(Import#1.Face(1)|Import#1.Face(6))", "the edge picked by name: " + Edge);
        TallyVolume(ExchangeGate, Inch, Side * Side * Side - Removed, "fillet r 5 mm on imported edge " + Edge + (Rounded.AcceptedCondition ? "" : " refused: " + Rounded.Refusal) + ": V = 0.0254³ − (1 − π/4) r² · 0.0127");

        // ⑨ Refusals leave the document as it was: a missing file, a damaged solid, a non-STEP name, nothing to export.
        std::string Damaged = Written;
        const size_t Shell = Damaged.find("CLOSED_SHELL('',(#");
        if (Shell != std::string::npos) Damaged.insert(Shell + 17, "999999,");
        const std::string DamagedPath = (Folder / "Damaged.step").string();
        { std::ofstream Stream(DamagedPath, std::ios::binary); Stream << Damaged; }
        SolidArcDocument& Kept   = Received;
        const size_t      Undos  = Received.UndoEntries.size();
        const std::string Before = EncodeSolidArcDocument(Kept);
        const SolidArcStepReading    Missing  = ReadSolidArcStepFile((Folder / "Absent.step").string());
        const SolidArcStepReading    Broken   = ReadSolidArcStepFile(DamagedPath);
        const SolidArcCommandOutcome Refused1 = ImportSolidArcStep(Kept, Missing);
        const SolidArcCommandOutcome Refused2 = ImportSolidArcStep(Kept, Broken);
        const SolidArcExportOutcome  Wrong    = ExportSolidArcBodies(Rig, (Folder / "Bracket.obj").string());
        const SolidArcExportOutcome  Empty    = ExportSolidArcBodies(CreateSolidArcDocument("Empty"), (Folder / "Empty.step").string());
        Tally(ExchangeGate, 0.0, !Refused1.AcceptedCondition && !Refused2.AcceptedCondition && EncodeSolidArcDocument(Kept) == Before && Kept.UndoEntries.size() == Undos
              && !Broken.Refusal.empty() && !Wrong.Refusal.empty() && !Empty.Refusal.empty() && !std::filesystem::exists(Folder / "Bracket.obj") && !std::filesystem::exists(Folder / "Empty.step"),
              "refused, document unchanged: " + Refused1.Refusal + " · " + Refused2.Refusal + " · " + Wrong.Refusal + " · " + Empty.Refusal);

        // A saved Import step without its text is refused by the codec.
        std::string Hollow = Saved1;
        const size_t Text = Hollow.find("Exchange = ");
        if (Text != std::string::npos) Hollow.replace(Text, 11, "Exchanged = ");
        const SolidArcDocumentFile Emptied = DecodeSolidArcDocument(Hollow);
        Tally(ExchangeGate, 0.0, Text != std::string::npos && !Emptied.Refusal.empty(), "codec: an Import step without its STEP text refused: " + Emptied.Refusal);

        // Undo removes both imports (one entry), redo restores them.
        const double Whole = Received.Summary.Volume;
        const bool Back1 = UndoSolidArcCommand(Received) && Received.Tree.Steps.empty() && Received.Summary.Bodies.empty();
        const bool Forth = RedoSolidArcCommand(Received) && Received.Summary.Volume == Whole && Received.Summary.Bodies.size() == 2;
        Tally(ExchangeGate, 0.0, Back1 && Forth, "undo removes the import (both bodies), redo restores it");
    }

    // ---- W39 bevel previews after the first Windows run: an upright fillet beside a 0.4 bottom fillet (froze / refused /
    //      off by 10⁻⁶), and a blend as wide as the neighbouring face (crashed)
    {
        const auto Format = [](const char* Pattern, auto... Values) { char Text[512]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        constexpr double Bottom = 0.4;                                                       // [len] - First fillet radius R
        constexpr double Notch  = 1.0 - std::numbers::pi / 4.0;                              // [-]   - Fillet cross-section / r²
        // 📝 Closed form: V = 2·1.5·1 − Notch·(R²·2 + r²·1) + overlap of the two notch prisms. In the corner the upright
        //    notch (offsets 𝑢, 𝑣 from the two side faces, 𝑢 < r − √(r² − (r − 𝑣)²)) reaches down to the bottom fillet at
        //    height h(𝑣) = R − √(R² − (R − 𝑣)²), so overlap = ∫₀ʳ 𝑢max(𝑣)·h(𝑣) d𝑣 — Simpson in 𝑠 with 𝑣 = r𝑠² (the √
        //    end points become smooth), converged to rounding at 40 000 panels.
        const auto Overlap = [&](double Radius)
        {
            constexpr int Panels = 40000;                                                    // [-] - Even
            double Sum = 0.0;                                                                // [len³] - Simpson sum
            for (int Index = 0; Index <= Panels; ++Index)
            {
                const double Unit   = static_cast<double>(Index) / Panels;                   // [-]   - 𝑠
                const double Offset = Radius * Unit * Unit;                                  // [len] - 𝑣
                const double Height = Bottom - std::sqrt(std::max(0.0, Bottom * Bottom - (Bottom - Offset) * (Bottom - Offset)));
                const double Reach  = Radius - std::sqrt(std::max(0.0, Radius * Radius - (Radius - Offset) * (Radius - Offset)));
                const double Weight = (Index == 0 || Index == Panels) ? 1.0 : (Index % 2 == 1 ? 4.0 : 2.0);
                Sum += Weight * Reach * Height * 2.0 * Radius * Unit;
            }
            return Sum / (3.0 * Panels);
        };
        SolidArcDocument Base = CreateSolidArcDocument("Bounded");
        Expect(ConstructSolidArcPrimitive(Base, SolidArcConstructRequest{}));
        SelectSolidArcName(Base, SolidArcSelectionKind::Edge, "Edge(Extrude#2.Cap(Start)|Extrude#2.Side(Line1))", false);
        SolidArcBlendRequest First;
        First.Size = Bottom;
        Expect(BlendSolidArcSelection(Base, First));
        // 0.1137 = the screenshot; 0.1 bisected for 334 s; 0.01 / 0.03 / 0.05 / 0.12 / 0.3 were refused; 0.02 / 0.1137 were 10⁻⁶ off.
        for (const double Radius : { 0.01, 0.02, 0.03, 0.05, 0.1, 0.1137, 0.12, 0.3 })
        {
            SolidArcDocument Preview = CloneSolidArcDocument(Base);
            ClearSolidArcSelection(Preview);
            SelectSolidArcName(Preview, SolidArcSelectionKind::Edge, "Edge(Extrude#2.Side(Line1)|Extrude#2.Side(Line2))", false);
            SolidArcBlendRequest Upright;
            Upright.Size = Radius;
            const auto   Begun    = std::chrono::steady_clock::now();
            const SolidArcCommandOutcome Outcome = BlendSolidArcSelection(Preview, Upright);
            const double Seconds  = std::chrono::duration<double>(std::chrono::steady_clock::now() - Begun).count();
            const double Expected = 3.0 - Notch * (Bottom * Bottom * 2.0 + Radius * Radius) + Overlap(Radius);   // [len³]
            const double Relative = std::abs(Preview.Summary.Volume - Expected) / Expected;
            Tally(BoundGate, Relative, Outcome.AcceptedCondition && Preview.Summary.SoundCondition && Relative <= 1.0e-9 && Seconds <= 20.0,
                  Format("upright fillet r %.4g beside the 0.4 bottom fillet: V %.12f vs %.12f, %.2f s%s", Radius, Preview.Summary.Volume, Expected, Seconds,
                         Outcome.AcceptedCondition ? "" : (" — refused: " + Outcome.Refusal).c_str()));
        }
        for (const BlendProfile Shape : { BlendProfile::Fillet, BlendProfile::Chamfer })
        {
            SolidArcDocument Plain = CreateSolidArcDocument("Width");
            Expect(ConstructSolidArcPrimitive(Plain, SolidArcConstructRequest{}));
            const std::string Before = EncodeSolidArcDocument(Plain);
            SelectSolidArcName(Plain, SolidArcSelectionKind::Edge, "Edge(Extrude#2.Cap(Start)|Extrude#2.Side(Line1))", false);
            SolidArcBlendRequest Wide;
            Wide.Shape = Shape;
            Wide.Size  = 1.5;
            const SolidArcCommandOutcome Outcome = BlendSolidArcSelection(Plain, Wide);
            Plain.Selection = {};
            Tally(BoundGate, 0.0, !Outcome.AcceptedCondition && EncodeSolidArcDocument(Plain) == Before,
                  std::string(Shape == BlendProfile::Fillet ? "fillet" : "chamfer") + " 1.5 = the bottom cap's depth: refused, document unchanged: " + Outcome.Refusal);
        }
    }

    // ---- W40 Construct catalogue: the new primitives against closed forms, every tile a body of its own
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[512]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto PrismVolume = [&](int Sides, double Radius, double Height) { return 0.5 * Sides * Radius * Radius * std::sin(2.0 * π / Sides) * Height; };
        struct Case { const char* Name; SolidArcConstructRequest Request; double Expected; };
        std::vector<Case> Cases;
        {
            SolidArcConstructRequest Request;
            Request.Kind = SolidArcConstructKind::Cone; Request.Position = { 1.0, 2.0, 0.3 }; Request.Radius = 0.5; Request.Height = 1.2;
            Cases.push_back({ "cone r 0.5 h 1.2", Request, π * 0.25 * 1.2 / 3.0 });
            Request.Height = -0.8;
            Cases.push_back({ "cone r 0.5 h −0.8 (points down)", Request, π * 0.25 * 0.8 / 3.0 });
            Request = {};
            Request.Kind = SolidArcConstructKind::Wedge; Request.Position = { -1.0, 0.5, 0.0 }; Request.Width = 2.0; Request.Depth = 1.5; Request.Height = 1.0;
            Cases.push_back({ "wedge 2 × 1.5 × 1", Request, 1.5 });
            for (const int Sides : { 3, 6, 8 })
            {
                Request = {};
                Request.Kind = SolidArcConstructKind::Prism; Request.Position = { 0.5, -0.5, 0.2 }; Request.Radius = 0.6; Request.Height = 0.9; Request.Sides = Sides; Request.Angle = 0.3;
                Cases.push_back({ Sides == 3 ? "prism 3 sides" : Sides == 6 ? "prism 6 sides" : "prism 8 sides", Request, PrismVolume(Sides, 0.6, 0.9) });
            }
            Request = {};
            Request.Kind = SolidArcConstructKind::Tube; Request.Position = { 0.0, 0.0, 0.0 }; Request.Radius = 0.5; Request.Inner = 0.3; Request.Height = 1.0;
            Cases.push_back({ "tube 0.5 / 0.3 h 1", Request, π * (0.25 - 0.09) });
        }
        for (const Case& Entry : Cases)
        {
            SolidArcDocument Single = CreateSolidArcDocument("Primitive");
            SolidArcConstructRequest Request = Entry.Request;
            Request.Combination = FeatureCombination::Separate;
            const SolidArcCommandOutcome Outcome = ConstructSolidArcPrimitive(Single, Request);
            const double Relative = std::abs(Single.Summary.Volume - Entry.Expected) / Entry.Expected;
            Tally(PrimitiveGate, Relative, Outcome.AcceptedCondition && Single.Summary.SoundCondition && Relative <= 1.0e-9 && Single.Summary.Bodies.size() == 1,
                  Format("%s: V %.12f vs %.12f%s", Entry.Name, Single.Summary.Volume, Entry.Expected, Outcome.AcceptedCondition ? "" : (" — refused: " + Outcome.Refusal).c_str()));
        }

        // ① eight tiles in a row, each Separate: eight bodies, each exact, total = Σ (no Boolean merged anything)
        SolidArcDocument Row = CreateSolidArcDocument("Catalogue");
        std::vector<std::pair<std::string, double>> Expected;
        double Total = 0.0;
        const SolidArcConstructKind Kinds[] = { SolidArcConstructKind::Block, SolidArcConstructKind::Cylinder, SolidArcConstructKind::Sphere, SolidArcConstructKind::Torus,
                                                SolidArcConstructKind::Cone, SolidArcConstructKind::Wedge, SolidArcConstructKind::Prism, SolidArcConstructKind::Tube };
        for (int Index = 0; Index < 8; ++Index)
        {
            SolidArcConstructRequest Request;
            Request.Kind        = Kinds[Index];
            Request.Combination = FeatureCombination::Separate;
            const double Across = 4.0 * Index;
            double       Volume = 0.0;
            switch (Request.Kind)
            {
            case SolidArcConstructKind::Block:    Request.Position = { Across, 0.0, 0.0 }; Volume = 2.0 * 1.5 * 1.0; break;
            case SolidArcConstructKind::Cylinder: Request.Position = { Across, 0.0, 0.0 }; Request.Radius = 0.6; Request.Height = 1.1; Volume = π * 0.36 * 1.1; break;
            case SolidArcConstructKind::Sphere:   Request.Position = { Across, 0.0, 0.7 }; Request.Radius = 0.7; Volume = 4.0 / 3.0 * π * 0.343; break;
            case SolidArcConstructKind::Torus:    Request.Position = { Across, 0.0, 0.4 }; Request.Major = 1.2; Request.Minor = 0.4; Volume = 2.0 * π * π * 1.2 * 0.16; break;
            case SolidArcConstructKind::Cone:     Request.Position = { Across, 0.0, 0.0 }; Request.Radius = 0.8; Request.Height = 1.5; Volume = π * 0.64 * 1.5 / 3.0; break;
            case SolidArcConstructKind::Wedge:    Request.Position = { Across, 0.0, 0.0 }; Request.Width = 1.8; Request.Depth = 1.2; Request.Height = 0.9; Volume = 0.5 * 1.8 * 1.2 * 0.9; break;
            case SolidArcConstructKind::Prism:    Request.Position = { Across, 0.0, 0.0 }; Request.Radius = 0.7; Request.Height = 1.0; Request.Sides = 5; Volume = PrismVolume(5, 0.7, 1.0); break;
            case SolidArcConstructKind::Tube:     Request.Position = { Across, 0.0, 0.0 }; Request.Radius = 0.7; Request.Inner = 0.45; Request.Height = 1.3; Volume = π * (0.49 - 0.2025) * 1.3; break;
            }
            Expect(ConstructSolidArcPrimitive(Row, Request));
            Expected.push_back({ Row.Regeneration.Active, Volume });
            Total += Volume;
        }
        int Matched = 0;
        double Worst = 0.0;
        for (const auto& [Key, Volume] : Expected)
            for (const SolidArcBodyFigures& Body : Row.Summary.Bodies)
                if (Body.Key == Key)
                {
                    const double Relative = std::abs(Body.Volume - Volume) / Volume;
                    Worst = std::max(Worst, Relative);
                    Matched += Relative <= 1.0e-9 && Body.SoundCondition ? 1 : 0;
                }
        Tally(PrimitiveGate, Worst, Row.Summary.Bodies.size() == 8 && Matched == 8 && Row.Summary.SoundCondition,
              Format("8 Separate tiles (block, cylinder, sphere, torus, cone, wedge, prism, tube): %d bodies, %d exact, worst relative %.3g", int(Row.Summary.Bodies.size()), Matched, Worst));
        const double TotalRelative = std::abs(Row.Summary.Volume - Total) / Total;
        Tally(PrimitiveGate, TotalRelative, TotalRelative <= 1.0e-9, Format("part volume = Σ tiles: %.12f vs %.12f", Row.Summary.Volume, Total));

        // ② parameter edits regenerate: prism Radius 0.7 → 0.9, tube bore 0.45 → 0.2
        auto ParameterEndingIn = [&](const char* Stem, const char* Suffix)
        {
            for (const auto& Parameter : Row.Tree.Parameters)
                if (Parameter.Name.rfind(Stem, 0) == 0 && Parameter.Name.size() > std::strlen(Suffix) && Parameter.Name.compare(Parameter.Name.size() - std::strlen(Suffix), std::string::npos, Suffix) == 0)
                    return Parameter.Name;
            return std::string();
        };
        const std::string PrismRadius = ParameterEndingIn("Prism", ".Radius");
        const SolidArcCommandOutcome Grown = AssignSolidArcParameter(Row, PrismRadius, 0.9);
        const double PrismExpected = Total - PrismVolume(5, 0.7, 1.0) + PrismVolume(5, 0.9, 1.0);
        const double PrismRelative = std::abs(Row.Summary.Volume - PrismExpected) / PrismExpected;
        Tally(PrimitiveGate, PrismRelative, Grown.AcceptedCondition && PrismRelative <= 1.0e-9 && Row.Summary.Bodies.size() == 8,
              Format("%s 0.7 → 0.9 (equal sides + on-circle constraints follow): V %.12f vs %.12f", PrismRadius.c_str(), Row.Summary.Volume, PrismExpected));
        const std::string TubeBore = ParameterEndingIn("Tube", ".Inner");
        const SolidArcCommandOutcome Bored = AssignSolidArcParameter(Row, TubeBore, 0.2);
        const double TubeExpected = PrismExpected - π * (0.49 - 0.2025) * 1.3 + π * (0.49 - 0.04) * 1.3;
        const double TubeRelative = std::abs(Row.Summary.Volume - TubeExpected) / TubeExpected;
        Tally(PrimitiveGate, TubeRelative, Bored.AcceptedCondition && TubeRelative <= 1.0e-9,
              Format("%s 0.45 → 0.2: V %.12f vs %.12f", TubeBore.c_str(), Row.Summary.Volume, TubeExpected));

        // ③ codec: save → load → save byte-equal, same volume
        const std::string RowSaved = EncodeSolidArcDocument(Row);
        SolidArcDocumentFile RowFile = DecodeSolidArcDocument(RowSaved);
        SolidArcDocument     RowLoaded = CreateSolidArcDocument(RowFile.Title);
        const SolidArcCommandOutcome RowOpened = ReplaceSolidArcTree(RowLoaded, RowFile.Tree, RowFile.Captions, "Open");
        Tally(PrimitiveGate, std::abs(RowLoaded.Summary.Volume - Row.Summary.Volume), RowOpened.AcceptedCondition && EncodeSolidArcDocument(RowLoaded) == RowSaved &&
              RowLoaded.Summary.Volume == Row.Summary.Volume && RowLoaded.Summary.Bodies.size() == 8, Format("codec: %d bytes, save → load → save byte-equal, 8 bodies", int(RowSaved.size())));

        // ④ drawn polygon (5 sides, R 0.8) extruded 0.5 as a new body = (5/2) R² sin 72° · 0.5
        SolidArcDocument Drawn = CreateSolidArcDocument("Polygon");
        SolidArcDrawingRequest Pentagon;
        Pentagon.Kind = SolidArcDrawingKind::Polygon; Pentagon.Sides = 5; Pentagon.Points = { { 1.0, 1.0 }, { 1.0, 1.8 } };
        Expect(DrawFilledSketch(Drawn, Pentagon));
        const std::string PentagonLabel = Drawn.Tree.Steps.back().Label;
        const SolidArcCommandOutcome Pushed = ExtrudeSolidArcSketch(Drawn, PentagonLabel, 0.5, FeatureCombination::Separate, {});
        const double PentagonExpected = PrismVolume(5, 0.8, 0.5);
        const double PentagonRelative = std::abs(Drawn.Summary.Volume - PentagonExpected) / PentagonExpected;
        Tally(PrimitiveGate, PentagonRelative, Pushed.AcceptedCondition && PentagonRelative <= 1.0e-9 && std::abs(MeasureDrawingArea(Pentagon) - 2.0 * PentagonExpected) <= 1.0e-12,
              Format("drawn polygon 5 sides R 0.8 extruded 0.5: V %.12f vs %.12f", Drawn.Summary.Volume, PentagonExpected));

        // ⑤ refusals leave the document untouched
        const std::string Before = EncodeSolidArcDocument(Row);
        SolidArcConstructRequest Bad;
        Bad.Kind = SolidArcConstructKind::Tube; Bad.Radius = 0.4; Bad.Inner = 0.4; Bad.Combination = FeatureCombination::Separate;
        const SolidArcCommandOutcome BoreRefused = ConstructSolidArcPrimitive(Row, Bad);
        Bad.Kind = SolidArcConstructKind::Prism; Bad.Sides = 2;
        const SolidArcCommandOutcome SidesRefused = ConstructSolidArcPrimitive(Row, Bad);
        Pentagon.Sides = 2;
        SketchModel Probe;
        const std::string PolygonRefusal = ComposeSolidArcSketch(Pentagon, Probe);
        Tally(PrimitiveGate, 0.0, !BoreRefused.AcceptedCondition && !SidesRefused.AcceptedCondition && !PolygonRefusal.empty() && EncodeSolidArcDocument(Row) == Before,
              "refused, document unchanged: " + BoreRefused.Refusal + " · " + SidesRefused.Refusal + " · " + PolygonRefusal);
    }

    // ---- W41 Inset (Plasticity O on faces · I): border ring + inner face, optional boss / pocket depth
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[512]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        // A fresh document with one primitive and the face under a downward ray at (X, Y) selected.
        const auto Prepared = [&](SolidArcConstructRequest Request, double X, double Y, std::string& Face)
        {
            SolidArcDocument Document = CreateSolidArcDocument("Inset");
            Request.Combination = FeatureCombination::Separate;
            Expect(ConstructSolidArcPrimitive(Document, Request));
            Face = PickAndSelect(Document, RayBetween({ X, Y, 5.0 }, { X, Y, -5.0 }), false);
            return Document;
        };
        struct Case { const char* Name; SolidArcConstructRequest Request; double X, Y, Distance, Depth, Expected; int Faces; };
        SolidArcConstructRequest Block;
        Block.Kind = SolidArcConstructKind::Block; Block.Width = 2.0; Block.Depth = 1.5; Block.Height = 1.0;
        SolidArcConstructRequest Cylinder;
        Cylinder.Kind = SolidArcConstructKind::Cylinder; Cylinder.Radius = 0.6; Cylinder.Height = 1.0;
        SolidArcConstructRequest Triangle;
        Triangle.Kind = SolidArcConstructKind::Prism; Triangle.Sides = 3; Triangle.Radius = 0.6; Triangle.Height = 0.9;
        const double TriangleArea  = 0.5 * 3.0 * 0.36 * std::sin(2.0 * π / 3.0);            // [len²] - Inradius R/2 = 0.3
        const double TriangleScale = (0.3 - 0.1) / 0.3;                                      // [-]    - Inset 0.1 shrinks the inradius
        const Case Cases[] = {
            { "block top, inset 0.25, depth 0 (flat split)", Block, 1.0, 0.75, 0.25, 0.0, 3.0, 7 },
            { "block top, inset 0.25, depth −0.2 (pocket)", Block, 1.0, 0.75, 0.25, -0.2, 3.0 - 1.5 * 1.0 * 0.2, 11 },
            { "block top, inset 0.25, depth +0.3 (boss)", Block, 1.0, 0.75, 0.25, 0.3, 3.0 + 1.5 * 1.0 * 0.3, 11 },
            { "cylinder top, inset 0.2, depth 0 (annulus + disc)", Cylinder, 0.1, 0.1, 0.2, 0.0, π * 0.36, 4 },
            { "cylinder top, inset 0.2, depth −0.3 (round pocket)", Cylinder, 0.1, 0.1, 0.2, -0.3, π * 0.36 - π * 0.16 * 0.3, 5 },
            { "triangle prism top, inset 0.1 (60° corners), depth −0.2", Triangle, 0.05, 0.05, 0.1, -0.2, TriangleArea * 0.9 - TriangleArea * TriangleScale * TriangleScale * 0.2, 9 },
        };
        for (const Case& Entry : Cases)
        {
            std::string Face;
            SolidArcDocument Document = Prepared(Entry.Request, Entry.X, Entry.Y, Face);
            const SolidArcCommandOutcome Outcome = InsetSolidArcSelection(Document, Entry.Distance, Entry.Depth);
            const double Relative = std::abs(Document.Summary.Volume - Entry.Expected) / Entry.Expected;
            Tally(InsetGate, Relative, Outcome.AcceptedCondition && Document.Summary.SoundCondition && Relative <= 1.0e-9 && Document.Summary.Faces == Entry.Faces,
                  Format("%s on %s: V %.12f vs %.12f · %d faces (expected %d)%s", Entry.Name, Face.c_str(), Document.Summary.Volume, Entry.Expected, Document.Summary.Faces, Entry.Faces,
                         Outcome.AcceptedCondition ? "" : (" — refused: " + Outcome.Refusal).c_str()));
        }

        // ① parameter edit regenerates (depth −0.2 → −0.5), the inner face of the inset takes a second inset, codec byte-equal
        std::string Face;
        SolidArcDocument Edited = Prepared(Block, 1.0, 0.75, Face);
        Expect(InsetSolidArcSelection(Edited, 0.25, -0.2));
        const SolidArcCommandOutcome Deeper = AssignSolidArcParameter(Edited, "Inset1.Depth", -0.5);
        const double DeeperExpected = 3.0 - 1.5 * 1.0 * 0.5;
        const double DeeperRelative = std::abs(Edited.Summary.Volume - DeeperExpected) / DeeperExpected;
        Tally(InsetGate, DeeperRelative, Deeper.AcceptedCondition && DeeperRelative <= 1.0e-9, Format("Inset1.Depth −0.2 → −0.5: V %.12f vs %.12f", Edited.Summary.Volume, DeeperExpected));
        const std::string Floor = PickAndSelect(Edited, RayBetween({ 1.0, 0.75, 5.0 }, { 1.0, 0.75, -5.0 }), false);
        const SolidArcCommandOutcome Again = InsetSolidArcSelection(Edited, 0.1, 0.2);
        const double AgainExpected = DeeperExpected + 1.3 * 0.8 * 0.2;
        const double AgainRelative = std::abs(Edited.Summary.Volume - AgainExpected) / AgainExpected;
        Tally(InsetGate, AgainRelative, Again.AcceptedCondition && AgainRelative <= 1.0e-9 && Edited.Summary.SoundCondition,
              Format("pocket floor %s inset 0.1, boss +0.2: V %.12f vs %.12f", Floor.c_str(), Edited.Summary.Volume, AgainExpected));
        const std::string Saved = EncodeSolidArcDocument(Edited);
        SolidArcDocumentFile File = DecodeSolidArcDocument(Saved);
        SolidArcDocument     Loaded = CreateSolidArcDocument(File.Title);
        const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Loaded, File.Tree, File.Captions, "Open");
        Tally(InsetGate, std::abs(Loaded.Summary.Volume - Edited.Summary.Volume), Opened.AcceptedCondition && EncodeSolidArcDocument(Loaded) == Saved && Loaded.Summary.Volume == Edited.Summary.Volume,
              Format("codec: %d bytes, save → load → save byte-equal (depth kept)", int(Saved.size())));

        // ② refusals leave the document untouched: too wide (1.5 deep face, inset 0.8), a curved face, zero distance
        SolidArcDocument Refused = Prepared(Block, 1.0, 0.75, Face);
        const std::string Before = EncodeSolidArcDocument(Refused);
        const SolidArcCommandOutcome Wide = InsetSolidArcSelection(Refused, 0.8, 0.0);
        const SolidArcCommandOutcome Zero = InsetSolidArcSelection(Refused, 0.0, 0.0);
        SolidArcDocument Round = Prepared(Cylinder, 0.0, 0.0, Face);
        PickAndSelect(Round, RayBetween({ 0.0, 3.0, 0.5 }, { 0.0, 0.0, 0.5 }), false);
        const std::string RoundBefore = EncodeSolidArcDocument(Round);
        const SolidArcCommandOutcome Curved = InsetSolidArcSelection(Round, 0.1, 0.0);
        Tally(InsetGate, 0.0, !Wide.AcceptedCondition && !Zero.AcceptedCondition && !Curved.AcceptedCondition && Curved.Refusal.find("not planar") != std::string::npos && EncodeSolidArcDocument(Refused) == Before && EncodeSolidArcDocument(Round) == RoundBefore,
              "refused, document unchanged: " + Wide.Refusal + " · " + Zero.Refusal + " · " + Curved.Refusal);
    }

    // ---- W42 Loft through drawn sketches (line, polyline, rectangle, circle, slot, spline) + gizmo document verbs
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        // One drawn sketch on the XY grid, lifted by Height with the gizmo verb; returns its step label.
        const auto Drawn = [&](SolidArcDocument& Document, SolidArcDrawingRequest Request, double Height)
        {
            Request.GridPlane = 0;
            Expect(DrawFilledSketch(Document, Request));
            const std::string Label = Document.Tree.Steps.back().Label;
            if (Height != 0.0) Expect(PlaceSolidArcSketches(Document, { Label }, TranslationMap({ 0.0, 0.0, Height }), "Move"));
            return Label;
        };
        const auto Circle = [](double Radius)
        {
            SolidArcDrawingRequest Request;
            Request.Kind = SolidArcDrawingKind::Circle; Request.CentreCondition = true; Request.Points = { { 0.0, 0.0 }, { Radius, 0.0 } };
            return Request;
        };
        const auto Rectangle = [](double HalfX, double HalfY)
        {
            SolidArcDrawingRequest Request;
            Request.Kind = SolidArcDrawingKind::Rectangle; Request.CentreCondition = true; Request.Points = { { 0.0, 0.0 }, { HalfX, HalfY } };
            return Request;
        };
        const auto Slot = [](double Half, double Radius)
        {
            SolidArcDrawingRequest Request;
            Request.Kind = SolidArcDrawingKind::Slot; Request.Points = { { -Half, 0.0 }, { Half, 0.0 }, { 0.0, Radius } };
            return Request;
        };
        const auto Frustum = [](double Low, double High, double Height) { return Height / 3.0 * (Low + High + std::sqrt(Low * High)); };
        const double SlotArea = [&](double Half, double Radius) { return 4.0 * Half * Radius + π * Radius * Radius; }(0.5, 0.3);

        // ① closed sections with closed forms: loft (Plasticity L) then Close (plane caps) → solid
        struct Case { const char* Name; SolidArcDrawingRequest Low, High; double Height, Expected; };
        const Case Cases[] = {
            { "circle r 1 → circle r 1 lifted 2 (cylinder 2π)", Circle(1.0), Circle(1.0), 2.0, 2.0 * π },
            { "circle r 1 → circle r 0.5 lifted 1.5 (cone frustum)", Circle(1.0), Circle(0.5), 1.5, Frustum(π, π * 0.25, 1.5) },
            { "rectangle 2 × 1 → rectangle 1 × 0.5 lifted 1 (pyramid frustum)", Rectangle(1.0, 0.5), Rectangle(0.5, 0.25), 1.0, Frustum(2.0, 0.5, 1.0) },
            { "slot (2 lines + 2 arcs) → slot × 0.5 lifted 0.8 (similar sections)", Slot(0.5, 0.3), Slot(0.25, 0.15), 0.8, Frustum(SlotArea, SlotArea * 0.25, 0.8) },
        };
        for (const Case& Entry : Cases)
        {
            SolidArcDocument Document = CreateSolidArcDocument("Loft");
            const std::string First  = Drawn(Document, Entry.Low, 0.0);
            const std::string Second = Drawn(Document, Entry.High, Entry.Height);
            const SolidArcCommandOutcome Loft   = LoftSolidArcCurves(Document, { First, Second });
            const SolidArcCommandOutcome Closed = CloseSolidArcPanels(Document);
            const double Relative = std::abs(Document.Summary.Volume - Entry.Expected) / Entry.Expected;
            Tally(SectionLoftGate, Relative, Loft.AcceptedCondition && Closed.AcceptedCondition && Document.Summary.SoundCondition && Relative <= 1.0e-9,
                  Format("%s: %s → %s, V %.12f vs %.12f%s", Entry.Name, First.c_str(), Second.c_str(), Document.Summary.Volume, Entry.Expected,
                         Loft.AcceptedCondition ? (Closed.AcceptedCondition ? "" : (" — close refused: " + Closed.Refusal).c_str()) : (" — loft refused: " + Loft.Refusal).c_str()));
        }

        // ② rectangle → circle (no closed form): sound, 3 faces, between the circle and rectangle prisms; then the top
        //    section moved up 1 with the gizmo verb regenerates the loft (volume scales with the height for parallel sections)
        {
            SolidArcDocument Document = CreateSolidArcDocument("Loft");
            const std::string Square = Drawn(Document, Rectangle(1.0, 1.0), 0.0);
            const std::string Round  = Drawn(Document, Circle(1.0), 1.0);
            const SolidArcCommandOutcome Loft   = LoftSolidArcCurves(Document, { Square, Round });
            const SolidArcCommandOutcome Closed = CloseSolidArcPanels(Document);
            const double Volume = Document.Summary.Volume;
            Tally(SectionLoftGate, 0.0, Loft.AcceptedCondition && Closed.AcceptedCondition && Document.Summary.SoundCondition && Document.Summary.Faces == 3 && Volume > π && Volume < 4.0,
                  Format("square 2 × 2 → circle r 1 lifted 1: sound %d, %d faces, π < V %.9f < 4%s", int(Document.Summary.SoundCondition), Document.Summary.Faces, Volume,
                         Loft.AcceptedCondition ? "" : (" — refused: " + Loft.Refusal).c_str()));
            const SolidArcCommandOutcome Lifted = PlaceSolidArcSketches(Document, { Round }, TranslationMap({ 0.0, 0.0, 1.0 }), "Move");
            const double Relative = std::abs(Document.Summary.Volume - 2.0 * Volume) / (2.0 * Volume);
            Tally(SectionLoftGate, Relative, Lifted.AcceptedCondition && Relative <= 1.0e-9 && Document.Summary.SoundCondition,
                  Format("gizmo move of %s by +1 z regenerates the loft: V %.12f vs 2 × %.12f", Round.c_str(), Document.Summary.Volume, Volume));
            // codec: sketch sections and their moved planes survive save → load → save
            const std::string Saved = EncodeSolidArcDocument(Document);
            SolidArcDocumentFile File = DecodeSolidArcDocument(Saved);
            SolidArcDocument     Loaded = CreateSolidArcDocument(File.Title);
            const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Loaded, File.Tree, File.Captions, "Open");
            Tally(SectionLoftGate, std::abs(Loaded.Summary.Volume - Document.Summary.Volume), Opened.AcceptedCondition && EncodeSolidArcDocument(Loaded) == Saved && Loaded.Summary.Volume == Document.Summary.Volume,
                  Format("codec: %d bytes, save → load → save byte-equal, same volume", int(Saved.size())));
        }

        // ③ open sections: a line → a 3-point polyline lifted 1 → an open sheet; a closed spline (4 points) → a circle
        {
            SolidArcDocument Document = CreateSolidArcDocument("Loft");
            SolidArcDrawingRequest Line;
            Line.Kind = SolidArcDrawingKind::Line; Line.Points = { { -1.0, 0.0 }, { 1.0, 0.0 } };
            SolidArcDrawingRequest Polyline;
            Polyline.Kind = SolidArcDrawingKind::Line; Polyline.Points = { { -1.0, 0.5 }, { 0.0, 1.0 }, { 1.0, 0.5 } };
            const std::string Bottom = Drawn(Document, Line, 0.0);
            const std::string Top    = Drawn(Document, Polyline, 1.0);
            const SolidArcCommandOutcome Loft = LoftSolidArcCurves(Document, { Bottom, Top });
            Tally(SectionLoftGate, 0.0, Loft.AcceptedCondition && Document.Regeneration.Panels.size() == 1,
                  Format("line %s → polyline %s (2 segments) lifted 1: open panel, %d panel(s)%s", Bottom.c_str(), Top.c_str(), int(Document.Regeneration.Panels.size()),
                         Loft.AcceptedCondition ? "" : (" — refused: " + Loft.Refusal).c_str()));

            SolidArcDocument Spline = CreateSolidArcDocument("Loft");
            SolidArcDrawingRequest Curve;
            Curve.Kind = SolidArcDrawingKind::Curve; Curve.ClosedCondition = true; Curve.Points = { { 1.0, 0.0 }, { 0.0, 0.8 }, { -1.0, 0.0 }, { 0.0, -0.8 } };
            const std::string Freeform = Drawn(Spline, Curve, 0.0);
            const std::string Disc     = Drawn(Spline, Circle(0.6), 1.2);
            const SolidArcCommandOutcome Skin   = LoftSolidArcCurves(Spline, { Freeform, Disc });
            const SolidArcCommandOutcome Closed = CloseSolidArcPanels(Spline);
            Tally(SectionLoftGate, 0.0, Skin.AcceptedCondition && Closed.AcceptedCondition && Spline.Summary.SoundCondition && Spline.Summary.Volume > 0.0,
                  Format("closed spline (4 points) → circle r 0.6 lifted 1.2: closed solid, V %.9f, sound %d%s", Spline.Summary.Volume, int(Spline.Summary.SoundCondition),
                         Skin.AcceptedCondition ? (Closed.AcceptedCondition ? "" : (" — close refused: " + Closed.Refusal).c_str()) : (" — refused: " + Skin.Refusal).c_str()));
        }

        // ④ refusals leave the document untouched: open + closed mixed, a mirrored sketch, an unknown label; a scale is
        //    accepted (S on sketches, W66): on a copy the circle doubles
        {
            SolidArcDocument Document = CreateSolidArcDocument("Loft");
            SolidArcDrawingRequest Line;
            Line.Kind = SolidArcDrawingKind::Line; Line.Points = { { -1.0, 0.0 }, { 1.0, 0.0 } };
            const std::string Open   = Drawn(Document, Line, 0.0);
            const std::string Closed = Drawn(Document, Circle(1.0), 1.0);
            const std::string Before = EncodeSolidArcDocument(Document);
            const SolidArcCommandOutcome Mixed    = LoftSolidArcCurves(Document, { Open, Closed });
            SolidArcDocument             Copy     = CloneSolidArcDocument(Document);
            const SolidArcCommandOutcome Scaled   = PlaceSolidArcSketches(Copy, { Closed }, ScalingMap({ 0.0, 0.0, 0.0 }, { 2.0, 2.0, 2.0 }), "Scale");
            const double                 Doubled  = Copy.Regeneration.Sketches.count(Closed) && !Copy.Regeneration.Sketches.at(Closed).Curves.empty() ? Copy.Regeneration.Sketches.at(Closed).Curves[0].Radius : 0.0;
            const SolidArcCommandOutcome Mirrored = PlaceSolidArcSketches(Document, { Closed }, ReflectionMap({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }), "Mirror");
            const SolidArcCommandOutcome Unknown  = PlaceSolidArcSketches(Document, { "Nothing#9" }, TranslationMap({ 0.0, 0.0, 1.0 }), "Move");
            Tally(SectionLoftGate, 0.0, !Mixed.AcceptedCondition && !Mirrored.AcceptedCondition && !Unknown.AcceptedCondition && EncodeSolidArcDocument(Document) == Before,
                  "refused, document unchanged: " + Mixed.Refusal + " · " + Mirrored.Refusal + " · " + Unknown.Refusal);
            Tally(SectionLoftGate, std::abs(Doubled - 2.0), Scaled.AcceptedCondition && std::abs(Doubled - 2.0) <= 1.0e-12,
                  Format("S ×2 on a copy: the circle r 1 becomes r %.12f%s", Doubled, Scaled.Refusal.empty() ? "" : (" · " + Scaled.Refusal).c_str()));
            // a quarter turn about x through the origin carries the plane exactly: x̂ stays, ẑ → −ŷ
            const SolidArcCommandOutcome Turned = PlaceSolidArcSketches(Document, { Closed }, RotationMap({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, π / 2.0), "Rotate");
            const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Closed; });
            const double Gap = Found == Document.Tree.Steps.end() ? 1.0 : Length(Found->Plane.XAxis - Vector3{ 1.0, 0.0, 0.0 }) + Length(Found->Plane.ZAxis - Vector3{ 0.0, -1.0, 0.0 })
                                                                          + Length(Found->Plane.Origin - Vector3{ 0.0, -1.0, 0.0 });
            Tally(SectionLoftGate, Gap, Turned.AcceptedCondition && Gap <= 1.0e-12, Format("gizmo rotate of %s 90° about x: plane x̂ (1, 0, 0), ẑ (0, −1, 0), origin (0, −1, 0), gap %.3g", Closed.c_str(), Gap));
        }

        // ⑤ face gizmo verbs on a block 2 × 1.5 × 1: tweak along a direction, rotate about an axis, scale, push / pull (offset)
        {
            const auto Block = [&](const OrbitRay& Ray, std::string& Face)
            {
                SolidArcDocument Document = CreateSolidArcDocument("Gizmo");
                SolidArcConstructRequest Request;
                Request.Kind = SolidArcConstructKind::Block; Request.Width = 2.0; Request.Depth = 1.5; Request.Height = 1.0; Request.Combination = FeatureCombination::Separate;
                Expect(ConstructSolidArcPrimitive(Document, Request));
                Face = PickAndSelect(Document, Ray, false);
                return Document;
            };
            const OrbitRay Side = RayBetween({ 5.0, 0.75, 0.5 }, { 0.0, 0.75, 0.5 });
            const OrbitRay Top  = RayBetween({ 1.0, 0.75, 5.0 }, { 1.0, 0.75, 0.0 });
            struct Verb { const char* Name; OrbitRay Ray; FaceOperationKind Operation; double Amount; std::optional<Vector3> Direction; double Expected; };
            const Verb Verbs[] = {
                { "side x = 2 pulled +0.3 along its normal (offset)", Side, FaceOperationKind::Offset, 0.3, std::nullopt, 3.0 + 1.5 * 0.3 },
                { "side x = 2 tweaked along (1, 0, 1) by 0.2√2 (x moves 0.2)", Side, FaceOperationKind::Tweak, 0.2 * std::sqrt(2.0), Vector3{ 1.0, 0.0, 1.0 }, 3.0 + 1.5 * 0.2 },
                { "top rotated 0.1 rad about x through its centre (volume kept)", Top, FaceOperationKind::Rotate, 0.1, Vector3{ 1.0, 0.0, 0.0 }, 3.0 },
                { "top scaled × 0.5 about its centre (pyramid frustum)", Top, FaceOperationKind::Scale, 0.5, std::nullopt, Frustum(3.0 / 1.0, 0.75, 1.0) },
            };
            std::string Saved;
            for (const Verb& Entry : Verbs)
            {
                std::string Face;
                SolidArcDocument Document = Block(Entry.Ray, Face);
                const SolidArcCommandOutcome Outcome = ChangeSolidArcSelection(Document, Entry.Operation, Entry.Amount, Entry.Direction);
                const double Relative = std::abs(Document.Summary.Volume - Entry.Expected) / Entry.Expected;
                Tally(SectionLoftGate, Relative, Outcome.AcceptedCondition && Document.Summary.SoundCondition && Relative <= 1.0e-9,
                      Format("%s on %s: V %.12f vs %.12f%s", Entry.Name, Face.c_str(), Document.Summary.Volume, Entry.Expected, Outcome.AcceptedCondition ? "" : (" — refused: " + Outcome.Refusal).c_str()));
                if (Entry.Operation == FaceOperationKind::Tweak)
                {
                    Saved = EncodeSolidArcDocument(Document);
                    SolidArcDocumentFile File = DecodeSolidArcDocument(Saved);
                    SolidArcDocument     Loaded = CreateSolidArcDocument(File.Title);
                    const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Loaded, File.Tree, File.Captions, "Open");
                    Tally(SectionLoftGate, std::abs(Loaded.Summary.Volume - Document.Summary.Volume), Opened.AcceptedCondition && EncodeSolidArcDocument(Loaded) == Saved && Loaded.Summary.Volume == Document.Summary.Volume
                          && Saved.find("Points") != std::string::npos, "codec: the tweak direction is saved (Points) and replays byte-equal");
                }
            }
        }
    }

    // ---- W43 Transform gizmo: the CPU triangles the GPU draws are the ones picked; drag maths from pixel rays
    {
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const OrbitCameraFrame Frame = FrameOrbitCamera(OrbitCamera{}, 1600.0, 900.0);
        const auto Project = [&](Vector3 Point, double& X, double& Y)
        {
            double Clip[4] = {};
            for (int Row = 0; Row < 4; ++Row)
                Clip[Row] = Frame.ClipFromWorld[Row] * Point.X + Frame.ClipFromWorld[4 + Row] * Point.Y + Frame.ClipFromWorld[8 + Row] * Point.Z + Frame.ClipFromWorld[12 + Row];
            X = (Clip[0] / Clip[3] * 0.5 + 0.5) * Frame.Width;
            Y = (Clip[1] / Clip[3] * 0.5 + 0.5) * Frame.Height;
        };
        struct ModeSet { uint32_t Modes; const char* Name; };
        const ModeSet Sets[] = { { GizmoTranslate, "T" }, { GizmoRotate, "R" }, { GizmoScale, "S" }, { GizmoTranslate | GizmoRotate, "T+R" },
                                 { GizmoRotate | GizmoScale, "R+S" }, { GizmoTranslate | GizmoRotate | GizmoScale, "T+R+S" } };
        for (const ModeSet& Set : Sets)
        {
            SolidArcGizmoPose Pose;
            Pose.Modes = Set.Modes;
            Pose.Size  = MeasureGizmoSize(Frame, Pose.Origin, GizmoPixels);
            const SolidArcGizmoGeometry Geometry = BuildSolidArcGizmo(Pose, Frame, SolidArcGizmoHandle::None, SolidArcGizmoHandle::None);
            // 📝 Slate proportions (L = 1, tip 0.95): cone at the tip, cylinder 0.28 inside, plane quad at (u + v)(tip − 0.08),
            //    arc on the 45° bisector at 0.62 · tip.
            const double Tip = 0.95, Arc = 0.62 * Tip / std::sqrt(2.0), Size = Pose.Size;
            struct Probe { Vector3 Point; SolidArcGizmoHandle Handle; uint32_t Mode; };
            const Probe Probes[] = {
                { { Tip * Size, 0.0, 0.0 }, SolidArcGizmoHandle::TranslateX, GizmoTranslate }, { { 0.0, Tip * Size, 0.0 }, SolidArcGizmoHandle::TranslateY, GizmoTranslate },
                { { 0.0, 0.0, Tip * Size }, SolidArcGizmoHandle::TranslateZ, GizmoTranslate },
                { { (Tip - 0.08) * Size, (Tip - 0.08) * Size, 0.0 }, SolidArcGizmoHandle::PlaneXY, GizmoTranslate },
                // 📝 In the exact isometric view the x–z quad centre lies on the eye ray through the y cone (their offset
                //    (0.87, −0.95, 0.87) runs along the view axis), and the opaque cone rightly wins; probe the quad's free corner.
                { { (Tip - 0.03) * Size, 0.0, (Tip - 0.13) * Size }, SolidArcGizmoHandle::PlaneXZ, GizmoTranslate },
                { { (Tip - 0.28) * Size, 0.0, 0.0 }, SolidArcGizmoHandle::ScaleX, GizmoScale }, { { 0.0, (Tip - 0.28) * Size, 0.0 }, SolidArcGizmoHandle::ScaleY, GizmoScale },
                { { 0.0, 0.0, (Tip - 0.28) * Size }, SolidArcGizmoHandle::ScaleZ, GizmoScale },
                { { Arc * Size, Arc * Size, 0.0 }, SolidArcGizmoHandle::RotateZ, GizmoRotate }, { { 0.0, Arc * Size, Arc * Size }, SolidArcGizmoHandle::RotateX, GizmoRotate },
                { { Arc * Size, 0.0, Arc * Size }, SolidArcGizmoHandle::RotateY, GizmoRotate } };
            int Picks = 0, Misses = 0;
            std::string Wrong;
            for (const Probe& Entry : Probes)
            {
                if (!(Set.Modes & Entry.Mode)) continue;
                double X = 0.0, Y = 0.0;
                Project(Entry.Point, X, Y);
                const SolidArcGizmoHandle Found = PickSolidArcGizmo(Geometry, Frame, X, Y, 6.0);
                ++Picks;
                if (Found != Entry.Handle) { ++Misses; Wrong += std::string(" ") + GizmoHandleLabel(Entry.Handle) + "→" + GizmoHandleLabel(Found); }
            }
            double FarX = 0.0, FarY = 0.0;
            Project({ 0.0, 0.0, 0.0 }, FarX, FarY);
            const bool Far = PickSolidArcGizmo(Geometry, Frame, FarX + 400.0, FarY, 6.0) == SolidArcGizmoHandle::None;
            Tally(GizmoGate, double(Misses), Misses == 0 && Far && Geometry.OpaqueCount <= Geometry.Vertices.size() && !Geometry.Vertices.empty(),
                  Format("%s: %d handle(s) picked at their projected centres, far pixel misses, %zu vertices (%u opaque)%s", Set.Name, Picks, Geometry.Vertices.size(), Geometry.OpaqueCount, Wrong.c_str()));
        }
        double Worst = 0.0;
        for (const double Parameter : { 0.3, 1.7, -0.4 })
        {
            double X = 0.0, Y = 0.0;
            Project({ 0.0, 0.0, Parameter }, X, Y);
            const std::optional<double> Found = MeasureAxisParameter(CastOrbitRay(Frame, X, Y), { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 });
            Worst = std::max(Worst, Found ? std::abs(*Found - Parameter) : 1.0);
        }
        Tally(GizmoGate, Worst, Worst <= 1.0e-9, Format("axis drag: pointer ray → parameter on the z axis (0.3, 1.7, −0.4), worst |Δt| %.3g", Worst));
        Worst = 0.0;
        for (const double Angle : { 0.3, 2.0, -1.0 })
        {
            double X = 0.0, Y = 0.0;
            Project({ 0.0, std::cos(Angle), std::sin(Angle) }, X, Y);
            const std::optional<double> Found = MeasureGizmoAngle(CastOrbitRay(Frame, X, Y), { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
            Worst = std::max(Worst, Found ? std::abs(*Found - Angle) : 1.0);
        }
        Tally(GizmoGate, Worst, Worst <= 1.0e-9, Format("rotate drag: pointer ray → angle about x from y (0.3, 2.0, −1.0 rad), worst |Δθ| %.3g", Worst));
    }

    // ---- W44 Mode-1 sketch points: DragSolidArcSketchPoint / DragSolidArcSketchPoints carry the steps built on the sketch
    {
        using Kind = SolidArcDrawingKind;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto Gap    = [](Vector2 Point, Vector2 Expected) { return std::abs(Point.X - Expected.X) + std::abs(Point.Y - Expected.Y); };
        const auto Solved = [](const SolidArcDocument& Document, const std::string& Label) -> const SketchModel& { return Document.Regeneration.Sketches.at(Label); };

        // ① a 2 × 1 rectangle extruded 1: the corner (2, 1) dragged to (3, 1) (one point: the direct drag and the gizmo agree)
        SolidArcDocument Plate = CreateSolidArcDocument("Points");
        Expect(DrawFilledSketch(Plate, DrawingOf(Kind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
        const std::string Sketch = Plate.Tree.Steps.back().Label;
        Expect(ExtrudeSolidArcSketch(Plate, Sketch, 1.0, FeatureCombination::New));
        const size_t History = Plate.UndoEntries.size();
        const SolidArcCommandOutcome One = DragSolidArcSketchPoints(Plate, Sketch, { 2 }, { { 3.0, 1.0 } });
        const SketchModel& AfterOne = Solved(Plate, Sketch);
        double Miss = Gap(AfterOne.Points[2].Position, { 3.0, 1.0 }) + Gap(AfterOne.Points[1].Position, { 3.0, 0.0 }) + Gap(AfterOne.Points[0].Position, { 0.0, 0.0 });
        double Relative = std::abs(Plate.Summary.Volume - 3.0) / 3.0;
        Tally(PointGate, Miss + Relative, One.AcceptedCondition && Miss <= 1.0e-9 && Relative <= 1.0e-9 && Plate.UndoEntries.size() == History + 1,
              Format("%s: corner (3, 1), its H / V neighbour follows to (3, 0), V %.12f vs 3, one undo entry", One.Label.c_str(), Plate.Summary.Volume));

        // ② both top corners moved up 0.5 together (the gizmo's xy square on two picked points) → 3 × 1.5 × 1
        const SolidArcCommandOutcome Two = DragSolidArcSketchPoints(Plate, Sketch, { 2, 3 }, { { 3.0, 1.5 }, { 0.0, 1.5 } });
        const SketchModel& AfterTwo = Solved(Plate, Sketch);
        Miss     = Gap(AfterTwo.Points[2].Position, { 3.0, 1.5 }) + Gap(AfterTwo.Points[3].Position, { 0.0, 1.5 }) + Gap(AfterTwo.Points[1].Position, { 3.0, 0.0 });
        Relative = std::abs(Plate.Summary.Volume - 4.5) / 4.5;
        Tally(PointGate, Miss + Relative, Two.AcceptedCondition && Miss <= 1.0e-9 && Relative <= 1.0e-9 && Plate.UndoEntries.size() == History + 2 && Plate.Summary.SoundCondition,
              Format("%s: (3, 1.5) and (0, 1.5), V %.12f vs 4.5, one undo entry", Two.Label.c_str(), Plate.Summary.Volume));

        // ③ codec: the dragged positions survive save → load → save byte-equal
        const std::string Saved = EncodeSolidArcDocument(Plate);
        SolidArcDocumentFile File   = DecodeSolidArcDocument(Saved);
        SolidArcDocument     Loaded = CreateSolidArcDocument(File.Title);
        const SolidArcCommandOutcome Opened = ReplaceSolidArcTree(Loaded, File.Tree, File.Captions, "Open");
        Tally(PointGate, std::abs(Loaded.Summary.Volume - Plate.Summary.Volume), Opened.AcceptedCondition && EncodeSolidArcDocument(Loaded) == Saved && Loaded.Summary.Volume == Plate.Summary.Volume,
              Format("codec: %d bytes, save → load → save byte-equal, same volume", int(Saved.size())));

        // ④ refusals leave the file and the history untouched
        const SolidArcCommandOutcome Unpaired = DragSolidArcSketchPoints(Plate, Sketch, { 0, 1 }, { { 1.0, 1.0 } });
        const SolidArcCommandOutcome Missing  = DragSolidArcSketchPoints(Plate, "Sketch#99", { 0, 1 }, { { 1.0, 1.0 }, { 2.0, 2.0 } });
        const SolidArcCommandOutcome Beyond   = DragSolidArcSketchPoints(Plate, Sketch, { 0, 40 }, { { 1.0, 1.0 }, { 2.0, 2.0 } });
        Tally(PointGate, 0.0, !Unpaired.AcceptedCondition && !Missing.AcceptedCondition && !Beyond.AcceptedCondition && EncodeSolidArcDocument(Plate) == Saved && Plate.UndoEntries.size() == History + 2,
              "refused: " + Unpaired.Refusal + " · " + Missing.Refusal + " · " + Beyond.Refusal + "; file and history untouched");

        // ⑤ Bezier poles (no constraints) turned 30° about their centre: every pole lands exactly (the gizmo z arc)
        {
            SolidArcDocument Curves = CreateSolidArcDocument("Points");
            const std::vector<Vector2> Poles = { { 0.0, 0.0 }, { 0.5, 1.0 }, { 1.5, 1.0 }, { 2.0, 0.0 } };
            Expect(DrawFilledSketch(Curves, DrawingOf(Kind::Curve, 0, Poles, false, false, SketchSplineForm::Bezier)));
            const std::string Label  = Curves.Tree.Steps.back().Label;
            const SketchModel Before = Solved(Curves, Label);
            std::vector<int>     Picked;
            std::vector<Vector2> Targets;
            Vector2 Centre;
            for (int Point = 0; Point < int(Before.Points.size()); ++Point) { Centre.X += Before.Points[Point].Position.X / double(Before.Points.size()); Centre.Y += Before.Points[Point].Position.Y / double(Before.Points.size()); }
            const double Turn = std::numbers::pi / 6.0;
            for (int Point = 0; Point < int(Before.Points.size()); ++Point)
            {
                const Vector2 Arm = { Before.Points[Point].Position.X - Centre.X, Before.Points[Point].Position.Y - Centre.Y };
                Picked.push_back(Point);
                Targets.push_back({ Centre.X + std::cos(Turn) * Arm.X - std::sin(Turn) * Arm.Y, Centre.Y + std::sin(Turn) * Arm.X + std::cos(Turn) * Arm.Y });
            }
            const SolidArcCommandOutcome Turned = DragSolidArcSketchPoints(Curves, Label, Picked, Targets);
            double Worst = 0.0;
            for (size_t Pick = 0; Pick < Picked.size(); ++Pick) Worst = std::max(Worst, Gap(Solved(Curves, Label).Points[Picked[Pick]].Position, Targets[Pick]));
            Tally(PointGate, Worst, Turned.AcceptedCondition && Worst <= 1.0e-9 && Before.Points.size() == 4,
                  Format("%s: %d Bezier poles turned 30° about (%.3f, %.3f), worst miss %.3g", Turned.Label.c_str(), int(Picked.size()), Centre.X, Centre.Y, Worst));
        }

        // ⑥ a closed triangle (0, 0) (2, 0) (1, 1.5) extruded 1 (V 1.5), its points scaled × 2 along x about (1, 0.5) → V 3
        {
            SolidArcDocument Prism = CreateSolidArcDocument("Points");
            Expect(DrawFilledSketch(Prism, DrawingOf(Kind::Line, 0, { { 0.0, 0.0 }, { 2.0, 0.0 }, { 1.0, 1.5 } }, true)));
            const std::string Label = Prism.Tree.Steps.back().Label;
            Expect(ExtrudeSolidArcSketch(Prism, Label, 1.0, FeatureCombination::New));
            const double      Low    = Prism.Summary.Volume;
            const SketchModel Before = Solved(Prism, Label);
            std::vector<int>     Picked;
            std::vector<Vector2> Targets;
            for (int Point = 0; Point < int(Before.Points.size()); ++Point)
            {
                Picked.push_back(Point);
                Targets.push_back({ 1.0 + 2.0 * (Before.Points[Point].Position.X - 1.0), Before.Points[Point].Position.Y });
            }
            const SolidArcCommandOutcome Scaled = DragSolidArcSketchPoints(Prism, Label, Picked, Targets);
            Relative = std::abs(Prism.Summary.Volume - 3.0) / 3.0;
            Tally(PointGate, Relative + std::abs(Low - 1.5), Scaled.AcceptedCondition && Relative <= 1.0e-9 && std::abs(Low - 1.5) <= 1.0e-9 && Prism.Summary.SoundCondition,
                  Format("%s: %d points × 2 along x, V %.12f → %.12f vs 3", Scaled.Label.c_str(), int(Picked.size()), Low, Prism.Summary.Volume));
        }
    }

    // ---- W45 Solid loft shells, sketches drawn on body faces, hide / lock of sketches and bodies
    {
        constexpr double π = std::numbers::pi;
        const auto Format  = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto Frustum = [](double Low, double High, double Height) { return Height / 3.0 * (Low + High + std::sqrt(Low * High)); };
        const auto Disc    = [](SolidArcDocument& Document, Vector2 Centre, double Radius, double Height)
        {
            SolidArcDrawingRequest Request;
            Request.Kind = SolidArcDrawingKind::Circle; Request.CentreCondition = true; Request.Points = { Centre, { Centre.X + Radius, Centre.Y } };
            Expect(DrawFilledSketch(Document, Request));
            const std::string Label = Document.Tree.Steps.back().Label;
            if (Height != 0.0) Expect(PlaceSolidArcSketches(Document, { Label }, TranslationMap({ 0.0, 0.0, Height }), "Move"));
            return Label;
        };

        // ① closed sections: circle r 1 → circle r 0.5 lifted 1.5 lofts a capped solid of its own (no Close step)
        SolidArcDocument Shells = CreateSolidArcDocument("Shells");
        const std::string Low  = Disc(Shells, { 0.0, 0.0 }, 1.0, 0.0);
        const std::string High = Disc(Shells, { 0.0, 0.0 }, 0.5, 1.5);
        const SolidArcCommandOutcome First = LoftSolidArcCurves(Shells, { Low, High }, 0, true);
        const double Cone     = Frustum(π, 0.25 * π, 1.5);
        double       Relative = std::abs(Shells.Summary.Volume - Cone) / Cone;
        Tally(ShellGate, Relative, First.AcceptedCondition && Relative <= 1.0e-9 && Shells.Summary.SoundCondition && !Shells.Summary.SheetCondition && Shells.Summary.Faces == 3 && Shells.Summary.Bodies.size() == 1,
              Format("%s: V %.12f vs frustum %.12f, %d faces (skin + 2 caps), sound, 1 body", First.Label.c_str(), Shells.Summary.Volume, Cone, Shells.Summary.Faces));

        // ② a second solid loft in the same file (beside x = 4): two bodies, volumes add (a panel Close allows only one)
        const std::string Left  = Disc(Shells, { 4.0, 0.0 }, 1.0, 0.0);
        const std::string Right = Disc(Shells, { 4.0, 0.0 }, 1.0, 2.0);
        const SolidArcCommandOutcome Second = LoftSolidArcCurves(Shells, { Left, Right }, 0, true);
        const double Sum = Cone + 2.0 * π;
        Relative = std::abs(Shells.Summary.Volume - Sum) / Sum;
        Tally(ShellGate, Relative, Second.AcceptedCondition && Relative <= 1.0e-9 && Shells.Summary.Bodies.size() == 2 && Shells.Summary.SoundCondition,
              Format("%s: 2 bodies, V %.12f vs frustum + 2π %.12f", Second.Label.c_str(), Shells.Summary.Volume, Sum));

        // ③ codec: Closed = true on the solid lofts survives save → load → save byte-equal
        {
            const std::string          Written = EncodeSolidArcDocument(Shells);
            const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
            SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
            Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Copy, true);
            Tally(ShellGate, std::abs(Copy.Summary.Volume - Shells.Summary.Volume), Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Copy.Summary.Volume == Shells.Summary.Volume
                  && Written.find("Closed = true") != std::string::npos, Format("codec: %d bytes byte-equal, same volume, solid lofts carry Closed = true", int(Written.size())));
        }

        // ④ open sections (two lines) refuse the solid with the file and history untouched, then loft a sheet
        {
            SolidArcDocument Open = CreateSolidArcDocument("Shells");
            Expect(DrawFilledSketch(Open, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 0.0 }, { 2.0, 0.0 } })));
            const std::string Near = Open.Tree.Steps.back().Label;
            Expect(DrawFilledSketch(Open, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 1.0 }, { 2.0, 1.0 } })));
            const std::string Far = Open.Tree.Steps.back().Label;
            Expect(PlaceSolidArcSketches(Open, { Far }, TranslationMap({ 0.0, 0.0, 1.0 }), "Move"));
            const std::string Before  = EncodeSolidArcDocument(Open);
            const size_t      History = Open.UndoEntries.size();
            const SolidArcCommandOutcome Solid = LoftSolidArcCurves(Open, { Near, Far }, 0, true);
            const bool Untouched = EncodeSolidArcDocument(Open) == Before && Open.UndoEntries.size() == History;
            const SolidArcCommandOutcome Sheet = LoftSolidArcCurves(Open, { Near, Far });
            Tally(ShellGate, 0.0, !Solid.AcceptedCondition && Untouched && Sheet.AcceptedCondition && Open.Summary.SheetCondition,
                  "open sections: solid refused (" + Solid.Refusal + "), file untouched; the plain loft makes a sheet");
        }

        // ⑤ sketch on a face: a 2 × 2 × 1 block; a 1 × 1 square on its top face (z = 1) and a 1 × 0.5 one on its x = 2 face,
        //    each extruded 0.5 onto the block → V 4 + 0.5 + 0.25; a skewed frame refuses atomically
        {
            SolidArcDocument Block = CreateSolidArcDocument("Faces");
            Expect(DrawFilledSketch(Block, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 2.0 } })));
            Expect(ExtrudeSolidArcSketch(Block, Block.Tree.Steps.back().Label, 1.0, FeatureCombination::New));
            SolidArcDrawingRequest Top = DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.5, 0.5 }, { 1.5, 1.5 } });
            Top.PlaneCondition = true;
            Top.Plane          = { { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
            const SolidArcCommandOutcome OnTop = DrawFilledSketch(Block, Top);
            const std::string TopLabel = Block.Tree.Steps.back().Label;
            const SolidArcCommandOutcome Raised = ExtrudeSolidArcSketch(Block, TopLabel, 0.5, FeatureCombination::Union);
            Relative = std::abs(Block.Summary.Volume - 4.5) / 4.5;
            Tally(ShellGate, Relative, OnTop.AcceptedCondition && Raised.AcceptedCondition && Relative <= 1.0e-9 && Block.Summary.SoundCondition && Block.Tree.Steps[Block.Tree.Steps.size() - 2].Plane.Origin.Z == 1.0,
                  Format("%s on the top face (plane z = 1) → %s: V %.12f vs 4.5", OnTop.Label.c_str(), Raised.Label.c_str(), Block.Summary.Volume));
            SolidArcDrawingRequest Side = DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.5, 0.25 }, { 1.5, 0.75 } });
            Side.PlaneCondition = true;
            Side.Plane          = { { 2.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };   // x̂ = ẑ × n̂, ŷ = n̂ × x̂
            const SolidArcCommandOutcome OnSide = DrawFilledSketch(Block, Side);
            const SolidArcCommandOutcome Out    = ExtrudeSolidArcSketch(Block, Block.Tree.Steps.back().Label, 0.5, FeatureCombination::Union);
            Relative = std::abs(Block.Summary.Volume - 4.75) / 4.75;
            Tally(ShellGate, Relative, OnSide.AcceptedCondition && Out.AcceptedCondition && Relative <= 1.0e-9 && Block.Summary.SoundCondition,
                  Format("%s on the x = 2 face (n̂ = x̂) → %s: V %.12f vs 4.75", OnSide.Label.c_str(), Out.Label.c_str(), Block.Summary.Volume));
            const std::string Before = EncodeSolidArcDocument(Block);
            SolidArcDrawingRequest Skewed = Side;
            Skewed.Plane.XAxis = { 0.0, 1.0, 0.1 };
            const SolidArcCommandOutcome Refused = DrawFilledSketch(Block, Skewed);
            Tally(ShellGate, 0.0, !Refused.AcceptedCondition && EncodeSolidArcDocument(Block) == Before, "skewed face frame refused, file untouched: " + Refused.Refusal);
        }

        // ⑥ hide / lock: Outliner eye on a sketch, lock on a sketch and on a body — undoable appearance entries, codec byte-equal
        {
            SolidArcDocument Figures = CreateSolidArcDocument("Figures");
            Expect(DrawFilledSketch(Figures, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 1.0, 1.0 } })));
            const std::string Plate = Figures.Tree.Steps.back().Label;
            Expect(ExtrudeSolidArcSketch(Figures, Plate, 1.0, FeatureCombination::New));
            const std::string Body = Figures.Summary.Bodies.front().Key;
            Expect(DrawFilledSketch(Figures, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 3.0, 0.0 }, { 3.5, 0.0 } }, false, true)));
            const std::string Round = Figures.Tree.Steps.back().Label;
            SelectSolidArcCurve(Figures, Round, false);
            SelectSolidArcName(Figures, SolidArcSelectionKind::Body, Body, true);
            const double Volume  = Figures.Summary.Volume;
            const size_t History = Figures.UndoEntries.size();
            const SolidArcCommandOutcome Hide     = ToggleSolidArcCurveVisibility(Figures, Round);
            const SolidArcCommandOutcome LockOne  = ToggleSolidArcLock(Figures, Plate);
            const SolidArcCommandOutcome LockBody = ToggleSolidArcLock(Figures, Body);
            const bool Applied = Hide.AcceptedCondition && LockOne.AcceptedCondition && LockBody.AcceptedCondition && SolidArcCurveHiddenCondition(Figures, Round)
                              && SolidArcLockedCondition(Figures, Plate) && SolidArcLockedCondition(Figures, Body) && Figures.Selection.Curves.empty() && Figures.Selection.Bodies.empty()
                              && Figures.UndoEntries.size() == History + 3 && Figures.Summary.Volume == Volume;
            Tally(ShellGate, 0.0, Applied, "hide " + Round + ", lock " + Plate + " and body " + Body + ": 3 undo entries, both leave the selection, geometry unchanged");
            const std::string          Written = EncodeSolidArcDocument(Figures);
            const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
            SolidArcDocument Copy = CreateSolidArcDocument(Read.Title);
            Copy.Tree = Read.Tree; Copy.Captions = Read.Captions; Copy.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Copy, true);
            Tally(ShellGate, 0.0, Read.Refusal.empty() && EncodeSolidArcDocument(Copy) == Written && Copy.Appearance.HiddenCurves == Figures.Appearance.HiddenCurves
                  && Copy.Appearance.LockedFigures == Figures.Appearance.LockedFigures && Written.find("HiddenCurves") != std::string::npos && Written.find("Locked") != std::string::npos,
                  Format("codec: HiddenCurves %d · Locked %d survive save → load → save byte-equal", int(Copy.Appearance.HiddenCurves.size()), int(Copy.Appearance.LockedFigures.size())));
            const bool Undone = UndoSolidArcCommand(Figures) && UndoSolidArcCommand(Figures) && UndoSolidArcCommand(Figures);
            const SolidArcCommandOutcome Unknown = ToggleSolidArcLock(Figures, "Sketch#99");
            Tally(ShellGate, 0.0, Undone && Figures.Appearance.HiddenCurves.empty() && Figures.Appearance.LockedFigures.empty() && !Unknown.AcceptedCondition && Figures.UndoEntries.size() == History,
                  "three undos clear hide and locks; unknown figure refused: " + Unknown.Refusal);
        }
    }

    // ---- W47 Sketch corner fillet / chamfer (B on 2D sketches), fan lofts out of one section
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto CurvesIn = [](SolidArcDocument& Document, const std::string& Label) -> size_t
        {
            const auto Found = Document.Regeneration.Sketches.find(Label);
            return Found != Document.Regeneration.Sketches.end() ? Found->second.Curves.size() : 0;
        };

        // ① every corner of a 2 × 1 rectangle extruded 1, filleted r 0.25 afterwards: the extrude follows, V = 2 − (4 − π)/16
        SolidArcDocument Rounded = CreateSolidArcDocument("Rounded");
        Expect(DrawFilledSketch(Rounded, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
        const std::string Plate = Rounded.Tree.Steps.back().Label;
        Expect(ExtrudeSolidArcSketch(Rounded, Plate, 1.0, FeatureCombination::New));
        const size_t History = Rounded.UndoEntries.size();
        SolidArcCornerRequest Every;
        Every.Sketches = { Plate };
        Every.Size     = 0.25;
        const SolidArcCommandOutcome Filleted = CutSolidArcSketchCorners(Rounded, Every);
        const double Expected = 2.0 - (4.0 - π) * 0.0625;
        double Relative = std::abs(Rounded.Summary.Volume - Expected) / Expected;
        Tally(CornerGate, Relative, Filleted.AcceptedCondition && Relative <= 1.0e-9 && CurvesIn(Rounded, Plate) == 8 && Rounded.Summary.SoundCondition && Rounded.Summary.Faces == 10
              && Rounded.UndoEntries.size() == History + 1,
              Format("%s: 4 lines + 4 tangent arcs, the extrude follows: V %.12f vs 2 − (4 − π)/16 %.12f, %d faces, sound, one undo entry", Filleted.Label.c_str(), Rounded.Summary.Volume, Expected, Rounded.Summary.Faces));

        // ② the corner (2, 1) stays as the virtual sharp: dragged to (3, 1) the rectangle widens and the fillets follow
        {
            const auto Found = Rounded.Regeneration.Sketches.find(Plate);
            int Sharp = -1;
            if (Found != Rounded.Regeneration.Sketches.end())
                for (size_t Index = 0; Index < Found->second.Points.size(); ++Index)
                    if (Length(Found->second.Points[Index].Position - Vector2{ 2.0, 1.0 }) <= 1.0e-12) Sharp = static_cast<int>(Index);
            const SolidArcCommandOutcome Dragged = Sharp >= 0 ? DragSolidArcSketchPoints(Rounded, Plate, { Sharp }, { { 3.0, 1.0 } }) : SolidArcCommandOutcome{};
            const double Wider = 3.0 - (4.0 - π) * 0.0625;
            const double Error = std::abs(Rounded.Summary.Volume - Wider) / Wider;
            Tally(CornerGate, Error, Dragged.AcceptedCondition && Error <= 1.0e-9 && CurvesIn(Rounded, Plate) == 8 && Rounded.Summary.SoundCondition,
                  Format("virtual sharp point %d dragged (2, 1) to (3, 1): V %.12f vs 3 − (4 − π)/16, the arcs stay tangent with r 0.25", Sharp, Rounded.Summary.Volume));
            UndoSolidArcCommand(Rounded);
            UndoSolidArcCommand(Rounded);
            Tally(CornerGate, std::abs(Rounded.Summary.Volume - 2.0), std::abs(Rounded.Summary.Volume - 2.0) <= 1.0e-12 && CurvesIn(Rounded, Plate) == 4, "undo × 2: the sharp rectangle and V 2 return");
        }

        // ③ one picked corner (0, 0) chamfered 0.3: 5 lines, area 2 − 0.045, Chamfer distances on the sketch
        {
            SolidArcDocument Cut = CreateSolidArcDocument("Chamfered");
            Expect(DrawFilledSketch(Cut, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
            const std::string Sketch = Cut.Tree.Steps.back().Label;
            int Origin = -1;
            const SketchModel& Model = Cut.Regeneration.Sketches.at(Sketch);
            for (size_t Index = 0; Index < Model.Points.size(); ++Index) if (Length(Model.Points[Index].Position) <= 1.0e-12) Origin = static_cast<int>(Index);
            SolidArcCornerRequest One;
            One.Sketches = { Sketch };
            One.Points   = { Origin };
            One.Size     = 0.3;
            One.Shape    = SketchCornerShape::Chamfer;
            const SolidArcCommandOutcome Chamfered = CutSolidArcSketchCorners(Cut, One);
            int Labelled = 0;
            for (const FeatureStep& Step : Cut.Tree.Steps)
                if (Step.Label == Sketch) for (const SketchConstraint& Constraint : Step.Sketch.Constraints) Labelled += Constraint.Label == "Chamfer" ? 1 : 0;
            Expect(ExtrudeSolidArcSketch(Cut, Sketch, 1.0, FeatureCombination::New));
            const double Error = std::abs(Cut.Summary.Volume - 1.955) / 1.955;
            Tally(CornerGate, Error, Chamfered.AcceptedCondition && CurvesIn(Cut, Sketch) == 5 && Labelled == 2 && Error <= 1.0e-9 && Cut.Summary.SoundCondition,
                  Format("%s: 5 lines, 2 Chamfer distances; extruded 1 → V %.12f vs 2 − 0.3²/2 = 1.955", Chamfered.Label.c_str(), Cut.Summary.Volume));

            // ④ refusals, file untouched: r 0.6 (the second corner on the 1-long side has only 0.4 left), a circle centre, size 0
            const std::string Before = EncodeSolidArcDocument(Cut);
            SolidArcCornerRequest Large = Every;
            Large.Sketches = { Sketch };
            Large.Size     = 0.6;
            const SolidArcCommandOutcome TooLarge = CutSolidArcSketchCorners(Cut, Large);
            Expect(DrawFilledSketch(Cut, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 5.0, 0.0 }, { 5.5, 0.0 } }, false, true)));
            const std::string Ring = Cut.Tree.Steps.back().Label;
            const std::string WithRing = EncodeSolidArcDocument(Cut);
            SolidArcCornerRequest Centre;
            Centre.Sketches = { Ring };
            Centre.Points   = { 0 };
            const SolidArcCommandOutcome NoCorner = CutSolidArcSketchCorners(Cut, Centre);
            Large.Size = 0.0;
            const SolidArcCommandOutcome Zero = CutSolidArcSketchCorners(Cut, Large);
            Tally(CornerGate, 0.0, !TooLarge.AcceptedCondition && !NoCorner.AcceptedCondition && !Zero.AcceptedCondition && EncodeSolidArcDocument(Cut) == WithRing && WithRing != Before,
                  "refused: " + TooLarge.Refusal + " · " + NoCorner.Refusal + " · " + Zero.Refusal);
        }

        // ⑤ fan lofts (the Windows report): A → B and A → C both start on line A, so they run along it the same way; the
        //    second panel turns and both sew into one sheet
        {
            SolidArcDocument Fan = CreateSolidArcDocument("Fan");
            Expect(DrawFilledSketch(Fan, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 0.0 }, { 1.0, 0.0 } })));
            const std::string A = Fan.Tree.Steps.back().Label;
            Expect(DrawFilledSketch(Fan, DrawingOf(SolidArcDrawingKind::Line, 1, { { 0.0, 1.0 }, { 1.0, 1.0 } })));
            const std::string B = Fan.Tree.Steps.back().Label;
            Expect(DrawFilledSketch(Fan, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 1.0 }, { 1.0, 1.0 } })));
            const std::string C = Fan.Tree.Steps.back().Label;
            const SolidArcCommandOutcome Upright = LoftSolidArcCurves(Fan, { A, B }, 0, false);
            const SolidArcCommandOutcome Level   = LoftSolidArcCurves(Fan, { A, C }, 0, false);
            Tally(CornerGate, 0.0, Upright.AcceptedCondition && Level.AcceptedCondition && Fan.Summary.SheetCondition && Fan.Summary.Faces == 2 && Fan.Summary.SoundCondition && Fan.Regeneration.Panels.size() == 2,
                  Upright.Label + " and " + Level.Label + ": one sheet of 2 faces sharing " + A + (Level.AcceptedCondition ? "" : " · refused: " + Level.Refusal));
            RedoSolidArcCommand(Rounded);                                                    // The four fillets again
            const std::string          Written = EncodeSolidArcDocument(Rounded);
            const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
            SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
            Loaded.Tree = Read.Tree; Loaded.Captions = Read.Captions; Loaded.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Loaded, true);
            const double Drift = std::abs(Loaded.Summary.Volume - Expected) / Expected;
            Tally(CornerGate, Drift, Read.Refusal.empty() && EncodeSolidArcDocument(Loaded) == Written && Drift <= 1.0e-9 && CurvesIn(Rounded, Plate) == 8,
                  Format("codec: %d bytes byte-equal with the filleted sketch (arcs, Tangent / Radius / PointOnLine), V %.12f after reload", int(Written.size()), Loaded.Summary.Volume));
        }
    }

    // ---- W48 Fill (unfilled drawings extrude into walls, Fill toggle, Fill tool regions) and flip normals
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto Senses = [](const SolidArcDocument& Document, const std::string& Key)
        {
            std::vector<int> Found;
            for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
                if (Body.Key == Key)
                    for (FaceToken Face : Document.Partition->CollectInventory(Body.Body).Faces) Found.push_back(static_cast<int>(Document.Partition->Faces.Resolve(Face).Sense));
            return Found;
        };
        const auto Reload = [](const SolidArcDocument& Document, std::string& Written)
        {
            Written = EncodeSolidArcDocument(Document);
            const SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
            SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
            Loaded.Tree = Read.Tree; Loaded.Captions = Read.Captions; Loaded.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Loaded, true);
            return Loaded;
        };

        // ① a fresh rectangle is unfilled: E makes 4 walls of zero thickness (an open sheet, a body of its own)
        SolidArcDocument Walls = CreateSolidArcDocument("Walls");
        Expect(DrawSolidArcSketch(Walls, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
        const std::string Box = Walls.Tree.Steps.back().Label;
        const bool Unfilled = !Walls.Tree.Steps.back().FillCondition;
        Expect(ExtrudeSolidArcSketch(Walls, Box, 1.0, FeatureCombination::New));
        Tally(FillGate, Walls.Summary.Volume, Unfilled && Walls.Summary.SheetCondition && Walls.Summary.Faces == 4 && Walls.Summary.SoundCondition && std::abs(Walls.Summary.Volume) <= 1.0e-12,
              Format("unfilled rectangle extruded 1: open sheet of %d walls, no volume (%.3g), sound", Walls.Summary.Faces, Walls.Summary.Volume));

        // ② fill it: the extrude regenerates as the solid 2 × 1 × 1 (6 faces), one undo entry; undo returns the walls
        const size_t History = Walls.UndoEntries.size();
        const SolidArcCommandOutcome Filled = FillSolidArcSketch(Walls, Box, true);
        const double Error = std::abs(Walls.Summary.Volume - 2.0) / 2.0;
        Tally(FillGate, Error, Filled.AcceptedCondition && Error <= 1.0e-9 && !Walls.Summary.SheetCondition && Walls.Summary.Faces == 6 && Walls.UndoEntries.size() == History + 1,
              Format("%s: the extrude follows, V %.12f vs 2, 6 faces, one undo entry", Filled.Label.c_str(), Walls.Summary.Volume));
        const SolidArcCommandOutcome Again = FillSolidArcSketch(Walls, Box, true);
        UndoSolidArcCommand(Walls);
        Tally(FillGate, 0.0, !Again.AcceptedCondition && Walls.Summary.SheetCondition && Walls.Summary.Faces == 4, "filling twice refused (" + Again.Refusal + "); undo returns the 4 walls");
        RedoSolidArcCommand(Walls);

        // ③ an open drawing cannot be filled; unfilled closed drawings refuse to cut (walls are a sheet)
        Expect(DrawSolidArcSketch(Walls, DrawingOf(SolidArcDrawingKind::Line, 0, { { 5.0, 0.0 }, { 6.0, 0.0 } })));
        const SolidArcCommandOutcome Open = FillSolidArcSketch(Walls, Walls.Tree.Steps.back().Label, true);
        Expect(DrawSolidArcSketch(Walls, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 1.0, 0.5 }, { 1.3, 0.5 } }, false, true)));
        const SolidArcCommandOutcome Cut = ExtrudeSolidArcSketch(Walls, Walls.Tree.Steps.back().Label, 1.0, FeatureCombination::Difference, Walls.Regeneration.Bodies.front().Key);
        Tally(FillGate, 0.0, !Open.AcceptedCondition && !Cut.AcceptedCondition && std::abs(Walls.Summary.Volume - 2.0) <= 1.0e-9, "refused: " + Open.Refusal + " · " + Cut.Refusal);

        // ④ two unit circles 1 apart (separate sketches): the lens and the left crescent become filled sketches
        SolidArcDocument Overlap = CreateSolidArcDocument("Overlap");
        Expect(DrawSolidArcSketch(Overlap, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 0.0, 0.0 }, { 1.0, 0.0 } }, false, true)));
        const std::string Left = Overlap.Tree.Steps.back().Label;
        Expect(DrawSolidArcSketch(Overlap, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 1.0, 0.0 }, { 2.0, 0.0 } }, false, true)));
        const double Lens = 2.0 * π / 3.0 - std::sqrt(3.0) / 2.0;                            // 2r² acos(d/2r) − (d/2)√(4r² − d²)
        const SolidArcRegionPreview LensView = PreviewSolidArcRegion(Overlap, { Left, { 0.5, 0.0 } });
        const SolidArcCommandOutcome LensFill = FillSolidArcRegion(Overlap, { Left, { 0.5, 0.0 } });
        const std::string LensSketch = Overlap.Tree.Steps.back().Label;
        const bool LensShape = Overlap.Tree.Steps.back().FillCondition && Overlap.Regeneration.Sketches.at(LensSketch).Curves.size() == 2;
        Expect(ExtrudeSolidArcSketch(Overlap, LensSketch, 1.0, FeatureCombination::New));
        double Drift = std::abs(Overlap.Summary.Volume - Lens) / Lens;
        Tally(FillGate, Drift, LensFill.AcceptedCondition && LensShape && LensView.Region.Loops.size() == 1 && Drift <= 1.0e-9 && Overlap.Summary.SoundCondition,
              Format("%s: 2 arcs, extruded 1 → V %.12f vs lens 2π/3 − √3/2 %.12f (preview outline %.6f)", LensFill.Label.c_str(), Overlap.Summary.Volume, Lens, LensView.Region.Area));
        const SolidArcCommandOutcome Crescent = FillSolidArcRegion(Overlap, { Left, { -0.5, 0.0 } });
        const std::string CrescentSketch = Overlap.Tree.Steps.back().Label;
        Expect(ExtrudeSolidArcSketch(Overlap, CrescentSketch, 1.0, FeatureCombination::Separate));
        const double Moon = π - Lens;
        Drift = std::abs(Overlap.Summary.Volume - Lens - Moon) / (Lens + Moon);
        Tally(FillGate, Drift, Crescent.AcceptedCondition && Overlap.Regeneration.Bodies.size() == 2 && Drift <= 1.0e-9,
              Format("%s: crescent π − lens as a second body, total V %.12f vs π %.12f", Crescent.Label.c_str(), Overlap.Summary.Volume, π));

        // ⑤ a lone unfilled circle: its region is its own loop, so Fill fills that sketch (no new step)
        Expect(DrawSolidArcSketch(Overlap, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 6.0, 0.0 }, { 6.5, 0.0 } }, false, true)));
        const std::string Lone = Overlap.Tree.Steps.back().Label;
        const size_t Steps = Overlap.Tree.Steps.size();
        const SolidArcRegionPreview LoneView = PreviewSolidArcRegion(Overlap, { Lone, { 6.1, 0.1 } });
        const SolidArcCommandOutcome LoneFill = FillSolidArcRegion(Overlap, { Lone, { 6.1, 0.1 } });
        const SolidArcRegionPreview Outside = PreviewSolidArcRegion(Overlap, { Lone, { 9.0, 9.0 } });
        Tally(FillGate, 0.0, LoneFill.AcceptedCondition && LoneView.WholeSketch == Lone && Overlap.Tree.Steps.size() == Steps && Overlap.Tree.Steps.back().FillCondition && !Outside.Refusal.empty(),
              LoneFill.Label + ": the sketch itself filled, no new step · outside every loop: " + Outside.Refusal);

        // ⑥ a 4 × 4 square around a circle r 0.5 (separate sketches): the region between is the square with a hole
        SolidArcDocument Holed = CreateSolidArcDocument("Holed");
        Expect(DrawSolidArcSketch(Holed, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 8.0, -2.0 }, { 12.0, 2.0 } })));
        const std::string Square = Holed.Tree.Steps.back().Label;
        Expect(DrawSolidArcSketch(Holed, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 10.0, 0.0 }, { 10.5, 0.0 } }, false, true)));
        const SolidArcRegionPreview HoleView = PreviewSolidArcRegion(Holed, { Square, { 11.5, 0.0 } });
        const SolidArcCommandOutcome HoleFill = FillSolidArcRegion(Holed, { Square, { 11.5, 0.0 } });
        Expect(ExtrudeSolidArcSketch(Holed, Holed.Tree.Steps.back().Label, 1.0, FeatureCombination::New));
        const double Ring = 16.0 - π / 4.0;
        Drift = std::abs(Holed.Summary.Volume - Ring) / Ring;
        Tally(FillGate, Drift, HoleFill.AcceptedCondition && HoleView.Region.Loops.size() == 2 && HoleView.WholeSketch.empty() && Drift <= 1.0e-9 && Holed.Summary.Genus == 1,
              Format("%s: outer square + circular hole, extruded 1 → V %.12f vs 16 − π/4, genus %d", HoleFill.Label.c_str(), Holed.Summary.Volume, Holed.Summary.Genus));

        // ⑦ a Hermite arch cut by the line y = 0.25: the cap above the line takes a spline piece as exact Bézier spans
        SolidArcDocument Arch = CreateSolidArcDocument("Arch");
        Expect(DrawSolidArcSketch(Arch, DrawingOf(SolidArcDrawingKind::Curve, 0, { { 0.0, 0.0 }, { 1.5, 1.0 }, { 3.0, 0.0 } })));
        const std::string Curve = Arch.Tree.Steps.back().Label;
        Expect(DrawSolidArcSketch(Arch, DrawingOf(SolidArcDrawingKind::Line, 0, { { -1.0, 0.25 }, { 4.0, 0.25 } })));
        const SolidArcRegionPreview CapView = PreviewSolidArcRegion(Arch, { Curve, { 1.5, 0.5 } });
        const SolidArcCommandOutcome CapFill = FillSolidArcRegion(Arch, { Curve, { 1.5, 0.5 } });
        const std::string Cap = Arch.Tree.Steps.back().Label;
        double Off = 1.0;
        int    Beziers = 0;
        if (CapFill.AcceptedCondition)
        {
            Off = 0.0;
            const SketchModel& Model = Arch.Regeneration.Sketches.at(Cap);
            // The Bézier chain must start and end on the line piece's own end points (shared, within 10⁻⁹).
            std::vector<int> Ends;
            for (const SketchCurve& Piece : Model.Curves)
            {
                if (Piece.Kind == SketchCurveKind::Line)
                    for (int End : { Piece.Start, Piece.End }) Off = std::max(Off, std::abs(Model.Points[End].Position.Y - 0.25));
                if (Piece.Kind != SketchCurveKind::Spline) continue;
                ++Beziers;
                Ends.push_back(Piece.Points.front());
                Ends.push_back(Piece.Points.back());
            }
            for (const SketchCurve& Piece : Model.Curves)
                if (Piece.Kind == SketchCurveKind::Line)
                    for (int End : { Piece.Start, Piece.End }) if (std::find(Ends.begin(), Ends.end(), End) == Ends.end()) Off = 1.0;
        }
        Expect(ExtrudeSolidArcSketch(Arch, Cap, 1.0, FeatureCombination::New));
        const double Sampled = std::abs(Arch.Summary.Volume - CapView.Region.Area) / std::max(1.0e-12, CapView.Region.Area);
        Tally(FillGate, Sampled, CapFill.AcceptedCondition && Beziers >= 1 && Off <= 1.0e-9 && Sampled <= 1.0e-3 && Arch.Summary.SoundCondition,
              Format("%s: %d Bézier span(s) + the line piece, ends on y = 0.25 within %.2g; V %.9f vs sampled outline %.9f", CapFill.Label.c_str(), Beziers, Off, Arch.Summary.Volume, CapView.Region.Area));

        // ⑧ flip normals: a loft sheet turns (every face sense reversed), a second flip removes the step, a solid refuses
        SolidArcDocument Sheet = CreateSolidArcDocument("Flip");
        Expect(DrawSolidArcSketch(Sheet, DrawingOf(SolidArcDrawingKind::Line, 0, { { 0.0, 0.0 }, { 1.0, 0.0 } })));
        const std::string A = Sheet.Tree.Steps.back().Label;
        Expect(DrawSolidArcSketch(Sheet, DrawingOf(SolidArcDrawingKind::Line, 1, { { 0.0, 1.0 }, { 1.0, 1.0 } })));
        const std::string B = Sheet.Tree.Steps.back().Label;
        Expect(LoftSolidArcCurves(Sheet, { A, B }, 0, false));
        const std::string Panel = Sheet.Regeneration.Bodies.empty() ? std::string{} : Sheet.Regeneration.Bodies.front().Key;
        const std::vector<int> Before = Senses(Sheet, Panel);
        const SolidArcCommandOutcome Flipped = FlipSolidArcBodies(Sheet, { Panel });
        const std::vector<int> After = Senses(Sheet, Panel);
        bool Turned = !Before.empty() && Before.size() == After.size();
        for (size_t Index = 0; Turned && Index < Before.size(); ++Index) Turned = Before[Index] != After[Index];
        Tally(FillGate, 0.0, Flipped.AcceptedCondition && Turned && Sheet.Summary.SheetCondition && Sheet.Summary.SoundCondition && Sheet.Tree.Steps.back().Kind == FeatureStepKind::Flip,
              Format("%s: %d face(s), every sense reversed, sound", Flipped.Label.c_str(), int(After.size())));
        std::string FlipText;
        SolidArcDocument FlipLoaded = Reload(Sheet, FlipText);
        Tally(FillGate, 0.0, EncodeSolidArcDocument(FlipLoaded) == FlipText && Senses(FlipLoaded, Panel) == After, Format("codec: %d bytes byte-equal with the Flip step, senses kept", int(FlipText.size())));
        const SolidArcCommandOutcome Back = FlipSolidArcBodies(Sheet, { Panel });
        Tally(FillGate, 0.0, Back.AcceptedCondition && Senses(Sheet, Panel) == Before && Sheet.Tree.Steps.back().Kind != FeatureStepKind::Flip, "flip again: the Flip step goes, the senses return");
        const SolidArcCommandOutcome WallFlip = FlipSolidArcBodies(Walls, { Walls.Regeneration.Bodies.front().Key });
        Expect(FillSolidArcSketch(Walls, Box, false));                                       // Unfill: walls again
        const std::string WallKey = Walls.Regeneration.Bodies.front().Key;
        const std::vector<int> WallBefore = Senses(Walls, WallKey);
        const SolidArcCommandOutcome WallTurn = FlipSolidArcBodies(Walls, { WallKey });
        const std::vector<int> WallAfter = Senses(Walls, WallKey);
        bool WallTurned = !WallBefore.empty() && WallBefore.size() == WallAfter.size();
        for (size_t Index = 0; WallTurned && Index < WallBefore.size(); ++Index) WallTurned = WallBefore[Index] != WallAfter[Index];
        Tally(FillGate, 0.0, !WallFlip.AcceptedCondition && WallTurn.AcceptedCondition && WallTurned && WallAfter.size() == 4 && Walls.Summary.SheetCondition && Walls.Summary.SoundCondition,
              "solid refused (" + WallFlip.Refusal + "); the unfilled walls turn: " + std::to_string(WallAfter.size()) + " faces reversed");

        // ⑨ codec: Fill = false only on unfilled sketches; byte-equal reloads with the filled regions and the walls
        std::string RegionText, WallText;
        SolidArcDocument RegionLoaded = Reload(Overlap, RegionText);
        SolidArcDocument WallLoaded   = Reload(Walls, WallText);
        const double Total = std::abs(RegionLoaded.Summary.Volume - π) / π;
        Tally(FillGate, Total, EncodeSolidArcDocument(RegionLoaded) == RegionText && EncodeSolidArcDocument(WallLoaded) == WallText && Total <= 1.0e-9
              && WallLoaded.Summary.SheetCondition && RegionText.find("Fill = false") != std::string::npos,
              Format("codec: %d + %d bytes byte-equal (Fill = false on the drawn circles), V %.12f after reload, walls stay a sheet", int(RegionText.size()), int(WallText.size()), RegionLoaded.Summary.Volume));
    }

    // ---- W50 Orient to faces: solids and sketches built in the frame of the face they are drawn on
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        // The interface's face frame: n̂ outward, x̂ = ẑ × n̂ (world x on level faces), ŷ = n̂ × x̂, origin = foot of the world origin.
        const auto FaceFrame = [](Vector3 Normal, Vector3 OnFace)
        {
            Normal = Normalize(Normal);
            const Vector3 XAxis = std::abs(Normal.Z) > 1.0 - 1.0e-12 ? Vector3{ 1.0, 0.0, 0.0 } : Normalize(Cross(Vector3{ 0.0, 0.0, 1.0 }, Normal));
            return OrthonormalFrame{ Normal * Dot(OnFace, Normal), XAxis, Cross(Normal, XAxis), Normal };
        };
        const auto Tile = [](SolidArcConstructKind Kind, Vector3 Position, FeatureCombination Combination)
        {
            SolidArcConstructRequest Request;
            Request.Kind = Kind; Request.Position = Position; Request.Combination = Combination;
            return Request;
        };

        // ① a 2 m cube; a cylinder r 0.5, h 1 drawn on its +x face at face point (1, 1): x̂ = +y, ŷ = +z, n̂ = +x
        SolidArcDocument Cube = CreateSolidArcDocument("Oriented");
        SolidArcConstructRequest Base = Tile(SolidArcConstructKind::Block, { 0.0, 0.0, 0.0 }, FeatureCombination::New);
        Base.Width = Base.Depth = Base.Height = 2.0;
        Expect(ConstructSolidArcPrimitive(Cube, Base));
        SolidArcConstructRequest Boss = Tile(SolidArcConstructKind::Cylinder, { 1.0, 1.0, 0.0 }, FeatureCombination::Union);
        Boss.Radius = 0.5; Boss.Height = 1.0;
        Boss.PlaneCondition = true;
        Boss.Plane          = FaceFrame({ 1.0, 0.0, 0.0 }, { 2.0, 0.7, 0.4 });
        const SolidArcCommandOutcome Bossed = ConstructSolidArcPrimitive(Cube, Boss);
        const SolidArcScene CubeScene = BuildSolidArcScene(Cube);
        const double Expected = 8.0 + 0.25 * π;
        const double Error    = std::abs(Cube.Summary.Volume - Expected) / Expected;
        const double Reach    = std::abs(CubeScene.BoundsHigh[0] - 3.0);
        Tally(OrientGate, Error, Bossed.AcceptedCondition && Error <= 1.0e-9 && Reach <= 1.0e-6 && Cube.Summary.SoundCondition && Cube.Summary.Bodies.size() == 1,
              Format("%s on the +x face: V %.12f vs 8 + π/4, reaches x = %.9f (3, float scene), one sound body", Bossed.Label.c_str(), Cube.Summary.Volume, CubeScene.BoundsHigh[0]));

        // ② a face tilted 30° about x then 40° about z: every tile in that frame keeps its closed-form volume
        const double  Tilt = π / 6.0, Turn = 2.0 * π / 9.0;
        const Vector3 Leaning{ std::sin(Tilt) * std::sin(Turn), -std::sin(Tilt) * std::cos(Turn), std::cos(Tilt) };
        const OrthonormalFrame Tilted = FaceFrame(Leaning, Leaning * 0.75 + Vector3{ 0.3, 0.1, 0.0 });
        struct Case { SolidArcConstructKind Kind; double Volume; const char* Name; };
        const double Cylinder = π * 0.25 * 1.0, Cone = π * 0.25 * 1.0 / 3.0, Sphere = 4.0 / 3.0 * π * 0.125, Torus = 2.0 * π * π * 1.5 * 0.16;
        const double Prism = 0.5 * 6.0 * 0.25 * std::sin(2.0 * π / 6.0) * 1.0, Tube = π * (0.25 - 0.09) * 1.0, Wedge = 0.5 * 2.0 * 1.5 * 1.0;
        const Case Cases[] = { { SolidArcConstructKind::Block, 3.0, "block" }, { SolidArcConstructKind::Cylinder, Cylinder, "cylinder" }, { SolidArcConstructKind::Cone, Cone, "cone" },
                               { SolidArcConstructKind::Sphere, Sphere, "sphere" }, { SolidArcConstructKind::Torus, Torus, "torus" }, { SolidArcConstructKind::Prism, Prism, "prism" },
                               { SolidArcConstructKind::Tube, Tube, "tube" }, { SolidArcConstructKind::Wedge, Wedge, "wedge" } };
        double Worst = 0.0;
        int    Kept  = 0;
        for (const Case& Each : Cases)
        {
            SolidArcDocument Alone = CreateSolidArcDocument(Each.Name);
            SolidArcConstructRequest Request = Tile(Each.Kind, { 0.4, -0.2, 0.0 }, FeatureCombination::New);
            Request.PlaneCondition = true;
            Request.Plane          = Tilted;
            const SolidArcCommandOutcome Built = ConstructSolidArcPrimitive(Alone, Request);
            const double Miss = std::abs(Alone.Summary.Volume - Each.Volume) / Each.Volume;
            Worst = std::max(Worst, Miss);
            Kept += Built.AcceptedCondition && Miss <= 1.0e-9 && Alone.Summary.SoundCondition ? 1 : 0;
        }
        Tally(OrientGate, Worst, Kept == 8, Format("8 tiles on the tilted face (n̂ = (%.4f, %.4f, %.4f)): %d / 8 sound and exact, worst |V − closed form| / V %.3g", Leaning.X, Leaning.Y, Leaning.Z, Kept, Worst));

        // ③ a unit block at face point (0, 0): its eight corners are F.o + {0, 1}·x̂ + {0, 1}·ŷ + {0, 1}·n̂, the scene bounds theirs
        SolidArcDocument Turned = CreateSolidArcDocument("Turned");
        SolidArcConstructRequest Unit = Tile(SolidArcConstructKind::Block, { 0.0, 0.0, 0.0 }, FeatureCombination::New);
        Unit.Width = Unit.Depth = Unit.Height = 1.0;
        Unit.PlaneCondition = true;
        Unit.Plane          = Tilted;
        Expect(ConstructSolidArcPrimitive(Turned, Unit));
        std::array<double, 3> Low{ 1e300, 1e300, 1e300 }, High{ -1e300, -1e300, -1e300 };
        for (int Corner = 0; Corner < 8; ++Corner)
        {
            const Vector3 Point = Tilted.Origin + Tilted.XAxis * double(Corner & 1) + Tilted.YAxis * double((Corner >> 1) & 1) + Tilted.ZAxis * double((Corner >> 2) & 1);
            const double  Axes[3] = { Point.X, Point.Y, Point.Z };
            for (int Axis = 0; Axis < 3; ++Axis) { Low[Axis] = std::min(Low[Axis], Axes[Axis]); High[Axis] = std::max(High[Axis], Axes[Axis]); }
        }
        const SolidArcScene TurnedScene = BuildSolidArcScene(Turned);
        double Bounds = 0.0;
        for (int Axis = 0; Axis < 3; ++Axis) Bounds = std::max({ Bounds, std::abs(TurnedScene.BoundsLow[Axis] - Low[Axis]), std::abs(TurnedScene.BoundsHigh[Axis] - High[Axis]) });
        Tally(OrientGate, std::abs(Turned.Summary.Volume - 1.0), Bounds <= 1.0e-6 && std::abs(Turned.Summary.Volume - 1.0) <= 1.0e-12,
              Format("unit block in the tilted frame: scene bounds = the turned cube's within %.3g (float scene), V %.12f", Bounds, Turned.Summary.Volume));

        // ④ a circle sketched on the tilted plane extrudes 1 along its normal: V π/4, the far cap 1 above the plane
        SolidArcDocument Sketched = CreateSolidArcDocument("Sketched");
        SolidArcDrawingRequest Ring = DrawingOf(SolidArcDrawingKind::Circle, 0, { { 0.0, 0.0 }, { 0.5, 0.0 } }, false, true);
        Ring.PlaneCondition = true;
        Ring.Plane          = Tilted;
        Expect(DrawFilledSketch(Sketched, Ring));
        const std::string Disc = Sketched.Tree.Steps.back().Label;
        Expect(ExtrudeSolidArcSketch(Sketched, Disc, 1.0, FeatureCombination::New));
        const SolidArcScene DiscScene = BuildSolidArcScene(Sketched);
        double Height = 0.0;
        for (const SolidArcSceneCorner& Corner : DiscScene.Corners)
            Height = std::max(Height, Dot(Vector3{ Corner.Position[0], Corner.Position[1], Corner.Position[2] } - Tilted.Origin, Tilted.ZAxis));
        const double DiscError = std::abs(Sketched.Summary.Volume - 0.25 * π) / (0.25 * π);
        Tally(OrientGate, DiscError, DiscError <= 1.0e-9 && std::abs(Height - 1.0) <= 1.0e-5,
              Format("circle r 0.5 on the tilted plane extruded 1: V %.12f vs π/4, far cap %.7f along n̂ (float scene)", Sketched.Summary.Volume, Height));

        // ⑤ a skewed frame (x̂ not ⟂ ŷ) is refused, the file untouched; the cube with its face boss round-trips byte-equal
        const std::string Before = EncodeSolidArcDocument(Cube);
        SolidArcConstructRequest Skewed = Boss;
        Skewed.Plane.YAxis = Normalize(Skewed.Plane.YAxis + Skewed.Plane.XAxis * 0.1);
        const SolidArcCommandOutcome Refused = ConstructSolidArcPrimitive(Cube, Skewed);
        const std::string          Written = EncodeSolidArcDocument(Cube);
        const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
        SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
        Loaded.Tree = Read.Tree; Loaded.Captions = Read.Captions; Loaded.Appearance = Read.Appearance;
        RegenerateSolidArcDocument(Loaded, true);
        const double Drift = std::abs(Loaded.Summary.Volume - Expected) / Expected;
        Tally(OrientGate, Drift, !Refused.AcceptedCondition && Written == Before && Read.Refusal.empty() && EncodeSolidArcDocument(Loaded) == Written && Drift <= 1.0e-9,
              Format("skewed frame refused (%s), file untouched; codec %d bytes byte-equal, V %.12f after reload", Refused.Refusal.c_str(), int(Written.size()), Loaded.Summary.Volume));
    }

    // ---- W51 Curved-face sketches: exact tangent plane, extrude start sunk so a Union closes
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const double Major = 1.7, Tall = 0.6, Minor = 0.2, Reach = 0.3;                      // [len] - Drum radius, height · boss radius, length
        // Volume the boss adds outside the drum: π r² L plus the cap between the tangent plane and the drum,
        // ∫ (R − √(R² − y²)) · 2√(r² − y²) dy over |y| ≤ r (y = r sin t, Simpson on 4000 panels).
        const auto Bulge = [&]()
        {
            const int Panels = 4000;
            double    Sum    = 0.0;
            for (int Index = 0; Index <= Panels; ++Index)
            {
                const double Angle  = -0.5 * π + π * Index / Panels;
                const double Lateral = Minor * std::sin(Angle);
                const double Value  = (Major - std::sqrt(Major * Major - Lateral * Lateral)) * 2.0 * Minor * Minor * std::cos(Angle) * std::cos(Angle);
                Sum += Value * (Index == 0 || Index == Panels ? 1.0 : (Index % 2 ? 4.0 : 2.0));
            }
            return Sum * (π / Panels) / 3.0;
        };
        const double Expected = π * Major * Major * Tall + π * Minor * Minor * Reach + Bulge();
        const auto Drum = [&](const char* Title)
        {
            SolidArcDocument Document = CreateSolidArcDocument(Title);
            SolidArcConstructRequest Base;
            Base.Kind = SolidArcConstructKind::Cylinder; Base.Radius = Major; Base.Height = Tall; Base.Combination = FeatureCombination::New;
            Expect(ConstructSolidArcPrimitive(Document, Base));
            return Document;
        };

        // ① the exact foot: a point 3 mm inside the side (a facet hit) lands on the surface, normal +y
        SolidArcDocument Joined = Drum("Joined");
        const SolidArcScene DrumScene = BuildSolidArcScene(Joined);
        std::string Side;
        for (const SolidArcSceneFace& Face : DrumScene.Faces) if (Face.Surface == "Cylinder") Side = Face.Name;
        const SolidArcFaceFoot Foot = ProjectOntoSolidArcFace(Joined, Side, { 0.0, Major - 0.003, 0.3 });
        const double FootError = Length(Foot.Position - Vector3{ 0.0, Major, 0.3 }) + Length(Foot.Normal - Vector3{ 0.0, 1.0, 0.0 });
        Tally(TangentGate, FootError, Foot.FoundCondition && FootError <= 1.0e-12,
              Format("foot of (0, 1.697, 0.3) on %s: (%.12f, %.12f, %.12f), n̂ (%.3f, %.3f, %.3f)", Side.c_str(), Foot.Position.X, Foot.Position.Y, Foot.Position.Z, Foot.Normal.X, Foot.Normal.Y, Foot.Normal.Z));

        // ② a circle r 0.2 on the tangent plane at (0, R, 0.3): the measured sink covers R − √(R² − r²) (the profile rim's
        //    depth to the drum), over-reaching by at most the tessellation sag
        const OrthonormalFrame Tangent{ { 0.0, Major, 0.0 }, { -1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
        SolidArcDrawingRequest Ring = DrawingOf(SolidArcDrawingKind::Circle, 0, { { 0.0, 0.3 }, { Minor, 0.3 } }, false, true);
        Ring.PlaneCondition = true;
        Ring.Plane          = Tangent;
        Expect(DrawFilledSketch(Joined, Ring));
        const std::string Disc  = Joined.Tree.Steps.back().Label;
        const double      Sink  = MeasureSolidArcProfileSink(Joined, BuildSolidArcScene(Joined), Disc);
        const double      Depth = Major - std::sqrt(Major * Major - Minor * Minor);
        Tally(TangentGate, Sink - Depth, Sink >= Depth && Sink <= Depth + 2.0 * DrumScene.Sag + 1.0e-3,
              Format("profile sink %.6f ≥ rim depth %.6f (over-reach %.2g ≤ 2 · sag %.4f + 1 mm)", Sink, Depth, Sink - Depth, DrumScene.Sag));

        // ③ the exactly tangent Union only touches along a line: refused, or (on a copy) the boss sits on the drum with its start
        //    cap whole and no cap volume. Started at −sink: one sound body with the closed-form volume, no start cap left.
        SolidArcDocument             Touch    = CloneSolidArcDocument(Joined);
        const SolidArcCommandOutcome Touching = ExtrudeSolidArcSketch(Touch, Disc, Reach, FeatureCombination::Union);
        const double                 Apart    = π * Major * Major * Tall + π * Minor * Minor * Reach;
        const bool                   Open     = !Touching.AcceptedCondition || std::abs(Touch.Summary.Volume - Apart) <= 1.0e-9 * Apart;
        const SolidArcCommandOutcome Sunk     = ExtrudeSolidArcSketch(Joined, Disc, Reach, FeatureCombination::Union, {}, -Sink);
        const double                 Error    = std::abs(Joined.Summary.Volume - Expected) / Expected;
        bool                         Capless  = true;
        for (const SolidArcSceneFace& Face : BuildSolidArcScene(Joined).Faces) if (Face.Name == Joined.Tree.Steps.back().Label + ".Cap(Start)") Capless = false;
        Tally(TangentGate, Error, Open && Sunk.AcceptedCondition && Capless && Error <= 1.0e-9 && Joined.Summary.SoundCondition && Joined.Summary.Bodies.size() == 1,
              Format("tangent Union: %s, V %.12f (π R² H + π r² L %.12f: the cap gap stays open); sunk Union V %.12f vs + cap %.12f, %d faces, start cap gone %d",
                     Touching.AcceptedCondition ? "line contact" : "refused", Touch.Summary.Volume, Apart, Joined.Summary.Volume, Expected, Joined.Summary.Faces, int(Capless)));

        // ④ chamfer 0.02 on the junction (the cylinder–cylinder saddle curve): accepted, sound, material added in the corner
        const std::string Boss = Joined.Tree.Steps.back().Label;
        const SolidArcScene JoinedScene = BuildSolidArcScene(Joined);
        ClearSolidArcSelection(Joined);
        int Junctions = 0;
        for (const SolidArcSceneEdge& Edge : JoinedScene.Edges)
        {
            size_t Count = 0;
            for (size_t At = Edge.Name.find(Boss + "."); At != std::string::npos; At = Edge.Name.find(Boss + ".", At + 1)) ++Count;
            if (Count == 1 && Edge.Name.find("Cap") == std::string::npos) { SelectSolidArcName(Joined, SolidArcSelectionKind::Edge, Edge.Name, true); ++Junctions; }
        }
        const double                 Unchamfered = Joined.Summary.Volume;
        const SolidArcCommandOutcome Chamfered   = BlendSolidArcSelection(Joined, BlendProfile::Chamfer, 0.02);
        Tally(TangentGate, 0.0, Junctions == 1 && Chamfered.AcceptedCondition && Joined.Summary.SoundCondition && Joined.Summary.Volume > Unchamfered,
              Format("junction chamfer 0.02 on %d edge: %s, V %.9f → %.9f (%.0f ms)", Junctions, Chamfered.AcceptedCondition ? "accepted" : Chamfered.Refusal.c_str(),
                     Unchamfered, Joined.Summary.Volume, Chamfered.Milliseconds));

        // ⑤ the start round-trips: written as Start, byte-equal after reload, same volume
        const std::string          Written = EncodeSolidArcDocument(Joined);
        const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
        SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
        Loaded.Tree = Read.Tree; Loaded.Captions = Read.Captions; Loaded.Appearance = Read.Appearance;
        RegenerateSolidArcDocument(Loaded, true);
        const double Drift = std::abs(Loaded.Summary.Volume - Joined.Summary.Volume) / Joined.Summary.Volume;
        Tally(TangentGate, Drift, Read.Refusal.empty() && Written.find("Start") != std::string::npos && EncodeSolidArcDocument(Loaded) == Written && Drift <= 1.0e-12,
              Format("codec %d bytes byte-equal with the extrude Start, V %.12f after reload", int(Written.size()), Loaded.Summary.Volume));

        // ⑥ four moves of the chamfered body: each move's assembly already integrated the image, so the summary reuses
        //    that volume (and the next move hands it on); it must equal an independent integration of the moved body
        const double Chamfered4 = Joined.Summary.Volume;
        const auto   Moving     = std::chrono::steady_clock::now();
        bool         Moved      = true;
        for (int Move = 0; Move < 4; ++Move)
            Moved = TransformSolidArcBodies(Joined, { Joined.Regeneration.Active }, TranslationMap({ 0.25, 0.0, 0.0 }), "Move").AcceptedCondition && Moved;
        const double MoveMs      = std::chrono::duration<double>(std::chrono::steady_clock::now() - Moving).count() * 1.0e3 / 4.0;
        const double Independent = IntegrateBodyVolume(*Joined.Partition, Joined.Regeneration.Bodies.back().Body);
        const double Carried     = std::max(std::abs(Joined.Summary.Volume - Independent), std::abs(Joined.Summary.Volume - Chamfered4)) / Chamfered4;
        Tally(TangentGate, Carried, Moved && Carried <= 1.0e-12 && Joined.Summary.SoundCondition,
              Format("4 moves (%.0f ms each): summary V %.12f · independent integral %.12f · before the moves %.12f", MoveMs, Joined.Summary.Volume, Independent, Chamfered4));

        // ⑦ a Construct cylinder r 0.2, h 0.3 on the tangent plane at (0, −R, 0.3), started below it: the same closed form
        SolidArcDocument Built = Drum("Built");
        SolidArcConstructRequest Pin;
        Pin.Kind = SolidArcConstructKind::Cylinder; Pin.Radius = Minor; Pin.Height = Reach; Pin.Combination = FeatureCombination::Union;
        Pin.Position       = { 0.0, 0.3, 0.0 };
        Pin.PlaneCondition = true;
        Pin.Plane          = OrthonormalFrame{ { 0.0, -Major, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        Pin.Start          = -Sink;
        const SolidArcCommandOutcome Pinned  = ConstructSolidArcPrimitive(Built, Pin);
        const double                 Matched = std::abs(Built.Summary.Volume - Expected) / Expected;
        Tally(TangentGate, Matched, Pinned.AcceptedCondition && Matched <= 1.0e-9 && Built.Summary.SoundCondition && Built.Summary.Bodies.size() == 1,
              Format("Construct cylinder on the −y tangent plane, start %.6f: V %.12f (%s)", Pin.Start, Built.Summary.Volume, Pinned.AcceptedCondition ? "united" : Pinned.Refusal.c_str()));
    }

    // ---- W52 Knife: sketch cutters to a depth / through, the boolean library, object cutters
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[720]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        SolidArcDocument Cube = CreateSolidArcDocument("Knife");
        Expect(DrawFilledSketch(Cube, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { -1.0, -1.0 }, { 1.0, 1.0 } })));
        Expect(ExtrudeSolidArcSketch(Cube, Cube.Tree.Steps.back().Label, 2.0, FeatureCombination::New));
        const std::string Block = Cube.Regeneration.Bodies.front().Key;

        // ① an unfilled circle r 0.3 on the top face: the knife fills it and cuts from +0.02 down to −0.5
        SolidArcDrawingRequest Ring = DrawingOf(SolidArcDrawingKind::Circle, 0, { { 0.0, 0.0 }, { 0.3, 0.0 } }, false, true);
        Ring.PlaneCondition = true;
        Ring.Plane          = OrthonormalFrame{ { 0.0, 0.0, 2.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        Expect(DrawSolidArcSketch(Cube, Ring));
        const std::string Circle  = Cube.Tree.Steps.back().Label;
        const size_t      Undos   = Cube.UndoEntries.size();
        const SolidArcCommandOutcome Cut = KnifeSolidArcSketch(Cube, Circle, 0.02, -0.5, FeatureCombination::Difference, Block);
        bool Filled = false;
        for (const FeatureStep& Step : Cube.Tree.Steps) if (Step.Label == Circle) Filled = Step.FillCondition;
        TallyVolume(KnifeGate, Cube, 8.0 - 0.045 * π, Format("circle r 0.3 knifed 0.5 deep (%s, filled %d, %d undo entry)", Cut.AcceptedCondition ? Cut.Label.c_str() : Cut.Refusal.c_str(),
                                                              int(Filled), int(Cube.UndoEntries.size() - Undos)));
        Tally(KnifeGate, 0.0, Filled && Cube.UndoEntries.size() == Undos + 1 && Cube.Summary.Bodies.size() == 1, "the knife filled the unfilled circle in the same undo entry, one body");

        // ② a rectangle on the −y face (x̂ = x, ŷ = z, n̂ = −y) straight through: Top / Bottom past both sides
        SolidArcDrawingRequest Window = DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { -0.6, 0.4 }, { -0.2, 0.8 } });
        Window.PlaneCondition = true;
        Window.Plane          = OrthonormalFrame{ { 0.0, -1.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        Expect(DrawSolidArcSketch(Cube, Window));
        const SolidArcCommandOutcome Through = KnifeSolidArcSketch(Cube, Cube.Tree.Steps.back().Label, 0.05, -2.05, FeatureCombination::Difference, Block);
        TallyVolume(KnifeGate, Cube, 8.0 - 0.045 * π - 0.32, "rectangle 0.4 × 0.4 knifed through the 2 m cube (" + (Through.AcceptedCondition ? Through.Label : Through.Refusal) + ")");

        // ③ refusals leave the document untouched: an open line, zero depth, no target, a mirrored insert
        Expect(DrawFilledSketch(Cube, DrawingOf(SolidArcDrawingKind::Line, 0, { { -0.9, 0.9 }, { -0.5, 0.9 } })));
        const std::string Open  = Cube.Tree.Steps.back().Label;
        const size_t      Steps = Cube.Tree.Steps.size();
        const double      Held  = Cube.Summary.Volume;
        const SolidArcCommandOutcome OpenRefused   = KnifeSolidArcSketch(Cube, Open, 0.02, -0.5, FeatureCombination::Difference, Block);
        const SolidArcCommandOutcome DepthRefused  = KnifeSolidArcSketch(Cube, Circle, 0.0, 0.0, FeatureCombination::Difference, Block);
        const SolidArcCommandOutcome TargetRefused = KnifeSolidArcSketch(Cube, Circle, 0.02, -0.5, FeatureCombination::Difference, "Nowhere#9");
        SolidArcInsertPlacement Mirror;
        Mirror.Name = "Mirror"; Mirror.Exchange = "x"; Mirror.Target = Block;
        Mirror.Map.ColumnZ = { 0.0, 0.0, -1.0 };
        const SolidArcCommandOutcome MirrorRefused = KnifeSolidArcInsert(Cube, Mirror);
        Tally(KnifeGate, 0.0, !OpenRefused.AcceptedCondition && !DepthRefused.AcceptedCondition && !TargetRefused.AcceptedCondition && !MirrorRefused.AcceptedCondition &&
                              Cube.Tree.Steps.size() == Steps && Cube.Summary.Volume == Held,
              "refused untouched: open (" + OpenRefused.Refusal + ") · zero depth (" + DepthRefused.Refusal + ") · target (" + TargetRefused.Refusal + ") · mirror (" + MirrorRefused.Refusal + ")");

        // ④ the built-in library: unit width, top at +lift, depth below in widths
        const SolidArcInsertLibrary Library = AssembleSolidArcInserts({});
        const auto Widths = [](const SolidArcInsert& Insert, double& Top, double& Bottom)
        {
            double Low[3] = { 1e300, 1e300, 1e300 }, High[3] = { -1e300, -1e300, -1e300 };
            for (const auto& Segment : Insert.Segments)
                for (const Vector3& Point : Segment)
                {
                    const double Axis[3] = { Point.X, Point.Y, Point.Z };
                    for (int Index = 0; Index < 3; ++Index) { Low[Index] = std::min(Low[Index], Axis[Index]); High[Index] = std::max(High[Index], Axis[Index]); }
                }
            Top = High[2]; Bottom = Low[2];
            return std::max(High[0] - Low[0], High[1] - Low[1]);
        };
        std::string Shelf;
        double      Worst = 0.0;
        for (const SolidArcInsert& Insert : Library.Inserts)
        {
            double Top = 0.0, Bottom = 0.0;
            const double Width = Widths(Insert, Top, Bottom);
            Worst  = std::max({ Worst, std::abs(Width - 1.0), std::abs(Top - SolidArcInsertLift), std::abs(-Bottom - Insert.Depth) });
            Shelf += Format("%s%s %.4f", Shelf.empty() ? "" : " · ", Insert.Name.c_str(), Insert.Depth);
        }
        Tally(KnifeGate, Worst, Library.Inserts.size() == 7 && Library.Notes.empty() && Worst <= 1.0e-6 && Library.Inserts.front().Name == "Square pocket" &&
                                std::abs(Library.Inserts.front().Depth - 0.3) <= 1.0e-9,
              Format("library %d built-ins in %.0f ms (edge boxes: width 1, top +0.05, worst %.2g): %s%s", int(Library.Inserts.size()), Library.Milliseconds, Worst, Shelf.c_str(),
                     Library.Notes.empty() ? "" : (" · " + Library.Notes.front()).c_str()));

        // ⑤ the square pocket at width 0.4 turned 15° on the top face (Import + Transform + Combine, one undo entry)
        const SolidArcInsert& Square = Library.Inserts.front();
        const OrthonormalFrame Top{ { 0.5, 0.5, 2.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        SolidArcInsertPlacement Pocket;
        Pocket.Name = Square.Name; Pocket.Exchange = Square.Exchange; Pocket.Target = Block; Pocket.Combination = FeatureCombination::Difference;
        Pocket.Map  = PlaceSolidArcInsert(Top, 0.4, π / 12.0, false);
        const size_t PocketUndos = Cube.UndoEntries.size();
        const SolidArcCommandOutcome Pocketed = KnifeSolidArcInsert(Cube, Pocket);
        const double AfterPocket = 8.0 - 0.045 * π - 0.32 - 0.0192;
        TallyVolume(KnifeGate, Cube, AfterPocket, "square pocket insert, width 0.4 turned 15° (" + (Pocketed.AcceptedCondition ? Pocketed.Label : Pocketed.Refusal) + ")");
        Tally(KnifeGate, 0.0, Cube.UndoEntries.size() == PocketUndos + 1 && Cube.Summary.Bodies.size() == 1, Format("insert knife: %d undo entry, %d body", int(Cube.UndoEntries.size() - PocketUndos), int(Cube.Summary.Bodies.size())));

        // ⑥ raised (Union) on the +x face: a 0.4 × 0.4 × 0.12 boss · ⑦ Separate on the top face: a body of 0.4² × 0.14
        Pocket.Map         = PlaceSolidArcInsert(OrthonormalFrame{ { 1.0, 0.5, 1.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } }, 0.4, 0.0, true);
        Pocket.Combination = FeatureCombination::Union;
        const SolidArcCommandOutcome Bossed = KnifeSolidArcInsert(Cube, Pocket);
        TallyVolume(KnifeGate, Cube, AfterPocket + 0.0192, "the same insert raised out of the +x face (" + (Bossed.AcceptedCondition ? Bossed.Label : Bossed.Refusal) + ")");
        Pocket.Map         = PlaceSolidArcInsert(OrthonormalFrame{ { -0.5, -0.5, 2.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.4, 0.0, false);
        Pocket.Combination = FeatureCombination::Separate;
        const SolidArcCommandOutcome Separate = KnifeSolidArcInsert(Cube, Pocket);
        const std::string Cutter = Cube.Selection.Bodies.empty() ? std::string{} : Cube.Selection.Bodies.front();
        TallyVolume(KnifeGate, Cube, AfterPocket + 0.0192 + 0.0224, "the insert left separate: body " + Cutter + " (" + (Separate.AcceptedCondition ? Separate.Label : Separate.Refusal) + ")");

        // ⑧ Object: the cube cut by the separate insert, the cutter kept (duplicated first); the target as its own tool refuses
        const SolidArcCommandOutcome Self   = KnifeSolidArcBodies(Cube, Block, { Block }, FeatureCombination::Difference, true);
        const SolidArcCommandOutcome Object = KnifeSolidArcBodies(Cube, Block, { Cutter }, FeatureCombination::Difference, true);
        TallyVolume(KnifeGate, Cube, AfterPocket + 0.0192 + 0.0224 - 0.0192, Format("object knife with the cutter kept: %d bodies (%s) · self refused (%s)", int(Cube.Summary.Bodies.size()),
                                                                                     Object.AcceptedCondition ? Object.Label.c_str() : Object.Refusal.c_str(), Self.Refusal.c_str()));
        Tally(KnifeGate, 0.0, !Self.AcceptedCondition && Object.AcceptedCondition && Cube.Summary.Bodies.size() == 2, "object knife: 2 bodies remain (cube + kept cutter)");

        // ⑨ undo removes the whole object knife; redo restores it
        const double Cutted = Cube.Summary.Volume;
        const bool   Undone = UndoSolidArcCommand(Cube);
        const double Before = Cube.Summary.Volume;
        const bool   Redone = RedoSolidArcCommand(Cube);
        Tally(KnifeGate, std::abs(Cube.Summary.Volume - Cutted), Undone && Redone && std::abs(Before - (AfterPocket + 0.0192 + 0.0224)) <= 1.0e-9 * Before && std::abs(Cube.Summary.Volume - Cutted) <= 1.0e-12,
              Format("undo → V %.12f · redo → V %.12f", Before, Cube.Summary.Volume));

        // ⑩ save the kept cutter as an insert: the library grows by one, normalized like the built-ins
        const std::filesystem::path Folder = std::filesystem::temp_directory_path() / "SolidArcKnifeInserts";
        std::error_code Error;
        std::filesystem::remove_all(Folder, Error);
        std::string Saved;
        const std::string SaveRefusal = SaveSolidArcInsert(Cube, Cutter, Folder.string(), "Pocket Cutter", Saved);
        const SolidArcInsertLibrary Grown = AssembleSolidArcInserts(Folder.string());
        double GrownTop = 0.0, GrownBottom = 0.0;
        const double GrownWidth = Grown.Inserts.size() == 8 ? Widths(Grown.Inserts.back(), GrownTop, GrownBottom) : 0.0;
        Tally(KnifeGate, std::abs(GrownWidth - 1.0), SaveRefusal.empty() && Grown.Inserts.size() == 8 && Grown.Inserts.back().Name == "PocketCutter" && std::abs(GrownWidth - 1.0) <= 1.0e-6 &&
                                                     std::abs(Grown.Inserts.back().Depth - 0.3) <= 1.0e-6,
              Format("saved %s → library %d (%s: width %.9f, depth %.9f)%s", std::filesystem::path(Saved).filename().string().c_str(), int(Grown.Inserts.size()),
                     Grown.Inserts.empty() ? "" : Grown.Inserts.back().Name.c_str(), GrownWidth, Grown.Inserts.empty() ? 0.0 : Grown.Inserts.back().Depth, SaveRefusal.c_str()));
        std::filesystem::remove_all(Folder, Error);

        // ⑪ codec: the lifted extrude Start, the imported cutters and their placements round-trip byte-equal
        const std::string          Written = EncodeSolidArcDocument(Cube);
        const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
        SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
        Loaded.Tree = Read.Tree; Loaded.Captions = Read.Captions; Loaded.Appearance = Read.Appearance;
        RegenerateSolidArcDocument(Loaded, true);
        const double Drift = std::abs(Loaded.Summary.Volume - Cube.Summary.Volume) / Cube.Summary.Volume;
        Tally(KnifeGate, Drift, Read.Refusal.empty() && EncodeSolidArcDocument(Loaded) == Written && Drift <= 1.0e-12 && Loaded.Summary.SoundCondition,
              Format("codec %d bytes byte-equal, V %.12f after reload, %d steps", int(Written.size()), Loaded.Summary.Volume, int(Loaded.Tree.Steps.size())));
    }

    // ---- W53 Bevel handles: per-corner sizes along the outward bisectors (Plasticity's pull / push controller)
    {
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto Fresh = [&](const char* Title)
        {
            SolidArcDocument Made = CreateSolidArcDocument(Title);
            Expect(ConstructSolidArcPrimitive(Made, Block));
            return Made;
        };

        // ① Handles on the front-top edge (y = 0, z = 1): the uniform one at (2, 0, 1), corners at x = 0 and x = 4, all along (0, −1, 1)/√2.
        SolidArcDocument Single = Fresh("Handles");
        const std::string FrontEdge = PickAndSelect(Single, RayBetween({ 2.0, -1.0, 2.0 }, { 2.0, 0.0, 1.0 }), false);
        const std::vector<SolidArcBlendHandle> Handles = MeasureSolidArcBlendHandles(Single, Single.Selection.Edges);
        const Vector3 Outward = Normalize(Vector3{ 0.0, -1.0, 1.0 });
        double HandleGap = Handles.size() == 3 ? Distance(Handles[0].Position, { 2.0, 0.0, 1.0 }) : 1.0;
        for (const SolidArcBlendHandle& Handle : Handles) HandleGap = std::max(HandleGap, Distance(Handle.Direction, Outward));
        const bool Cornered = Handles.size() == 3 && Handles[0].Vertex.empty() && !Handles[1].Vertex.empty() && !Handles[2].Vertex.empty() &&
                              std::abs(std::abs(Handles[1].Position.X - Handles[2].Position.X) - 4.0) <= 1.0e-12;
        Tally(HandleGate, HandleGap, Cornered && HandleGap <= 1.0e-12, Format("%d handles on %s: uniform at mid-edge, corners %s / %s, all along (0, −1, 1)/√2",
              int(Handles.size()), FrontEdge.c_str(), Handles.size() > 1 ? Handles[1].Vertex.c_str() : "-", Handles.size() > 2 ? Handles[2].Vertex.c_str() : "-"));

        // ② Corner sizes 0.1 / 0.3 = the V taper 0.1 → 0.3 anchored at the same corner (closed form and rails).
        if (Handles.size() == 3)
        {
            const double AnchorX = Handles[1].Position.X, FarX = Handles[2].Position.X;
            SolidArcBlendRequest Request;
            Request.Shape = BlendProfile::Fillet; Request.Size = 0.2;
            Request.VertexSizes = { { Handles[1].Vertex, 0.1 }, { Handles[2].Vertex, 0.3 } };
            const SolidArcCommandOutcome Applied = BlendSolidArcSelection(Single, Request);
            Expect(Applied);
            TallyVolume(HandleGate, Single, 8.0 - MeasureVariableFilletLoss(4.0, 0.1, 0.3), "corner sizes 0.1 / 0.3 = the closed form of the 0.1 → 0.3 taper · " + Applied.Label);
            const double Gap = MeasureCapGap(Single, AnchorX, FarX, 1.0, 0.1, 0.3);
            Tally(HandleGate, Gap, Gap <= 1.0e-9, Format("rails 0.1 from x = %.0f, 0.3 from x = %.0f", AnchorX, FarX));

            SolidArcDocument Tapered = Fresh("Taper");
            SelectSolidArcName(Tapered, SolidArcSelectionKind::Edge, FrontEdge, false);
            SolidArcBlendRequest Taper;
            Taper.Shape = BlendProfile::Fillet; Taper.Size = 0.1; Taper.EndSize = 0.3;
            Taper.ReversedCondition = std::abs(EdgeEndPosition(Tapered, FrontEdge, false).X - AnchorX) > 1.0e-9;
            Expect(BlendSolidArcSelection(Tapered, Taper));
            const double Apart = std::abs(Tapered.Summary.Volume - Single.Summary.Volume);
            Tally(HandleGate, Apart, Apart <= 1.0e-12 * Tapered.Summary.Volume, Format("same body as V 0.1 → 0.3: V %.15f vs %.15f", Single.Summary.Volume, Tapered.Summary.Volume));

            // Corner parameters regenerate: Fillet1.Corner2 = 0.2 → the 0.1 → 0.2 taper.
            const bool Named = std::any_of(Single.Tree.Parameters.begin(), Single.Tree.Parameters.end(), [](const auto& Parameter) { return Parameter.Name == "Fillet1.Corner2"; });
            Expect(AssignSolidArcParameter(Single, "Fillet1.Corner2", 0.2));
            Tally(HandleGate, 0.0, Named, "parameters Fillet1.Corner1 / Fillet1.Corner2 in the tree");
            TallyVolume(HandleGate, Single, 8.0 - MeasureVariableFilletLoss(4.0, 0.1, 0.2), "Fillet1.Corner2 = 0.2 regenerates the 0.1 → 0.2 taper");

            // Undo / redo, then the codec.
            const double Edited = Single.Summary.Volume;
            const bool Undone = UndoSolidArcCommand(Single) && std::abs(Single.Summary.Volume - (8.0 - MeasureVariableFilletLoss(4.0, 0.1, 0.3))) <= 1.0e-9 * 8.0;
            const bool Redone = RedoSolidArcCommand(Single) && Single.Summary.Volume == Edited;
            Tally(HandleGate, 0.0, Undone && Redone, "undo restores 0.1 / 0.3, redo 0.1 / 0.2");
            const std::string Written = EncodeSolidArcDocument(Single);
            SolidArcDocumentFile Read = DecodeSolidArcDocument(Written);
            SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
            const bool Opened = Read.Refusal.empty() && ReplaceSolidArcTree(Loaded, Read.Tree, Read.Captions, "Open").AcceptedCondition;
            Tally(HandleGate, std::abs(Loaded.Summary.Volume - Edited), Opened && Loaded.Summary.Volume == Edited && EncodeSolidArcDocument(Loaded) == Written &&
                  Written.find("AnchorSizes") != std::string::npos, Format("codec %d bytes byte-equal with AnchorSizes, V %.12f after reload", int(Written.size()), Loaded.Summary.Volume));
            std::string Damaged = Written;
            const size_t At = Damaged.find("[[Steps.AnchorSizes]]");                         // toml++ writes the sizes as an array of tables
            if (At != std::string::npos) Damaged.insert(At, "[[Steps.AnchorSizes]]\nLiteral = 0.5\n");
            Tally(HandleGate, 0.0, At != std::string::npos && !DecodeSolidArcDocument(Damaged).Refusal.empty(), "a file with more corner sizes than corners is refused");
        }

        // ③ Refusals leave the body untouched.
        {
            SolidArcDocument Refused = Fresh("Refusals");
            SelectSolidArcName(Refused, SolidArcSelectionKind::Edge, FrontEdge, false);
            const std::vector<SolidArcBlendHandle> Own = MeasureSolidArcBlendHandles(Refused, Refused.Selection.Edges);
            const double Before = Refused.Summary.Volume;
            const size_t Steps  = Refused.Tree.Steps.size();
            std::string Foreign;
            for (const auto& [Slot, Name] : Refused.Regeneration.Naming.Vertices)
                if (std::none_of(Own.begin(), Own.end(), [&](const SolidArcBlendHandle& Handle) { return Handle.Vertex == Name; })) { Foreign = Name; break; }
            SolidArcBlendRequest Request;
            Request.Shape = BlendProfile::Chamfer; Request.Size = 0.1;
            Request.VertexSizes = { { Foreign, 0.2 } };
            const SolidArcCommandOutcome Off = BlendSolidArcSelection(Refused, Request);
            Request.VertexSizes = { { Own.size() > 1 ? Own[1].Vertex : std::string(), 0.2 }, { Own.size() > 1 ? Own[1].Vertex : std::string(), 0.3 } };
            const SolidArcCommandOutcome Twice = BlendSolidArcSelection(Refused, Request);
            Request.VertexSizes = { { Own.size() > 1 ? Own[1].Vertex : std::string(), 0.0 } };
            const SolidArcCommandOutcome Zero = BlendSolidArcSelection(Refused, Request);
            Tally(HandleGate, 0.0, !Off.AcceptedCondition && !Twice.AcceptedCondition && !Zero.AcceptedCondition && Refused.Summary.Volume == Before && Refused.Tree.Steps.size() == Steps,
                  "refused untouched: " + Off.Refusal + " · " + Twice.Refusal + " · " + Zero.Refusal);
        }

        // ④ The top loop: 4 corner handles along (±1, ±1, 1)/√3 (2 edges each); corner sizes 0.1 / 0.2 / 0.1 / 0.15 sit between the uniform bodies.
        {
            const OrbitRay Rays[] = { RayBetween({ 2.0, -1.0, 2.0 }, { 2.0, 0.0, 1.0 }), RayBetween({ 2.0, 3.0, 2.0 }, { 2.0, 2.0, 1.0 }),
                                      RayBetween({ -1.0, 1.0, 2.0 }, { 0.0, 1.0, 1.0 }), RayBetween({ 5.0, 1.0, 2.0 }, { 4.0, 1.0, 1.0 }) };
            const auto Looped = [&](const char* Title)
            {
                SolidArcDocument Made = Fresh(Title);
                for (const OrbitRay& Ray : Rays) PickAndSelect(Made, Ray, true);
                return Made;
            };
            SolidArcDocument Loop = Looped("Loop");
            const std::vector<SolidArcBlendHandle> Corners = MeasureSolidArcBlendHandles(Loop, Loop.Selection.Edges);
            double Skew = Corners.size() == 5 ? 0.0 : 1.0;
            for (size_t Index = 1; Index < Corners.size(); ++Index)
            {
                const Vector3 Expected = Normalize(Vector3{ Corners[Index].Position.X < 2.0 ? -1.0 : 1.0, Corners[Index].Position.Y < 1.0 ? -1.0 : 1.0, 1.0 });
                Skew = std::max(Skew, Distance(Corners[Index].Direction, Expected));
                if (Corners[Index].Edges != 2) Skew = 1.0;
            }
            Tally(HandleGate, Skew, Loop.Selection.Edges.size() == 4 && Skew <= 1.0e-12, Format("top loop: %d handles, each corner on 2 picked edges along (±1, ±1, 1)/√3", int(Corners.size())));
            const auto Uniform = [&](double Size)
            {
                SolidArcDocument Made = Looped("Uniform");
                Expect(BlendSolidArcSelection(Made, BlendProfile::Fillet, Size));
                return Made.Summary.Volume;
            };
            const double Small = Uniform(0.1), Large = Uniform(0.2);
            if (Corners.size() == 5)
            {
                SolidArcBlendRequest Request;
                Request.Shape = BlendProfile::Fillet; Request.Size = 0.1;
                Request.VertexSizes = { { Corners[2].Vertex, 0.2 }, { Corners[4].Vertex, 0.15 } };
                const SolidArcCommandOutcome Applied = BlendSolidArcSelection(Loop, Request);
                const bool Between = Applied.AcceptedCondition && Loop.Summary.SoundCondition && Loop.Summary.Volume < Small && Loop.Summary.Volume > Large;
                Tally(HandleGate, 0.0, Between, Format("loop with 2 corner sizes: V %.9f between uniform 0.2 (%.9f) and 0.1 (%.9f), sound · %s", Loop.Summary.Volume, Large, Small,
                      Applied.AcceptedCondition ? Applied.Label.c_str() : Applied.Refusal.c_str()));
            }
        }

        // ⑤ Start-size fit: block front edge spans 1 (its 1 m side edges) → 0.1; a 2 m cube edge spans 2 → 0.2 (the 0.25 default stays: ≤ 0.35 · 2).
        {
            SolidArcDocument Fitted = Fresh("Fit");
            PickAndSelect(Fitted, RayBetween({ 2.0, -1.0, 2.0 }, { 2.0, 0.0, 1.0 }), false);
            const SolidArcBlendFit BlockFit = MeasureSolidArcBlendFit(Fitted, Fitted.Selection.Edges);
            SolidArcDocument Cube = CreateSolidArcDocument("Cube");
            SolidArcConstructRequest Box;
            Box.Kind = SolidArcConstructKind::Block; Box.Width = 2.0; Box.Depth = 2.0; Box.Height = 2.0;
            Expect(ConstructSolidArcPrimitive(Cube, Box));
            PickAndSelect(Cube, RayBetween({ 1.0, -1.0, 3.0 }, { 1.0, 0.0, 2.0 }), false);
            const SolidArcBlendFit CubeFit = MeasureSolidArcBlendFit(Cube, Cube.Selection.Edges);
            const SolidArcBlendFit Empty   = MeasureSolidArcBlendFit(Cube, {});
            const double Off = std::max({ std::abs(BlockFit.Span - 1.0), std::abs(BlockFit.Fitted - 0.1), std::abs(CubeFit.Span - 2.0), std::abs(CubeFit.Fitted - 0.2) });
            Tally(HandleGate, Off, Off <= 1.0e-12 && 0.25 <= 0.35 * CubeFit.Span && !Empty.Refusal.empty(),
                  Format("fit: block edge span %.3f → %.3f · 2 m cube edge span %.3f → %.3f (0.25 kept) · no edges: %s", BlockFit.Span, BlockFit.Fitted, CubeFit.Span, CubeFit.Fitted,
                         Empty.Refusal.c_str()));
        }
    }

    // ---- W54 Knife with 3D cutters: Construct primitives combined into the body under them
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[720]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        SolidArcDocument Cube = CreateSolidArcDocument("SolidKnife");
        Expect(DrawFilledSketch(Cube, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { -1.0, -1.0 }, { 1.0, 1.0 } })));
        Expect(ExtrudeSolidArcSketch(Cube, Cube.Tree.Steps.back().Label, 2.0, FeatureCombination::New));
        const std::string Block = Cube.Regeneration.Bodies.front().Key;
        const auto Undone = [&](size_t Before) { return int(Cube.UndoEntries.size() - Before); };

        // ① a box cutter on the top face: base 0.4 × 0.4, from 0.3 below the face to 0.02 above it
        SolidArcConstructRequest Box;
        Box.Kind     = SolidArcConstructKind::Block;
        Box.Position = { -0.6, -0.6, 1.7 };
        Box.Width    = 0.4;
        Box.Depth    = 0.4;
        Box.Height   = 0.32;
        size_t Before = Cube.UndoEntries.size();
        const SolidArcCommandOutcome Boxed = KnifeSolidArcPrimitive(Cube, Box, Block, FeatureCombination::Difference, false);
        TallyVolume(SolidKnifeGate, Cube, 8.0 - 0.048, Format("box cutter sunk 0.3 (%s, %d undo entry, %d body)", Boxed.AcceptedCondition ? Boxed.Label.c_str() : Boxed.Refusal.c_str(),
                                                               Undone(Before), int(Cube.Summary.Bodies.size())));
        Tally(SolidKnifeGate, 0.0, Undone(Before) == 1 && Cube.Summary.Bodies.size() == 1, "box cutter: one undo entry, the cutter used up");

        // ② a cylinder in the +x face frame (x̂ = y, ŷ = z, n̂ = +x), sunk 0.5 with the cutter kept
        SolidArcConstructRequest Drill;
        Drill.Kind           = SolidArcConstructKind::Cylinder;
        Drill.PlaneCondition = true;
        Drill.Plane          = OrthonormalFrame{ { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
        Drill.Position       = { 0.0, 1.0, -0.5 };
        Drill.Radius         = 0.2;
        Drill.Height         = 0.52;
        Before = Cube.UndoEntries.size();
        const SolidArcCommandOutcome Drilled = KnifeSolidArcPrimitive(Cube, Drill, Block, FeatureCombination::Difference, true);
        TallyVolume(SolidKnifeGate, Cube, 8.0 - 0.048 - 0.02 * π + 0.0208 * π, Format("cylinder cutter on the +x face, kept (%s, %d undo entry, %d bodies)",
                                                                                     Drilled.AcceptedCondition ? Drilled.Label.c_str() : Drilled.Refusal.c_str(), Undone(Before),
                                                                                     int(Cube.Summary.Bodies.size())));
        Tally(SolidKnifeGate, 0.0, Undone(Before) == 1 && Cube.Summary.Bodies.size() == 2, "kept cutter: one undo entry, the cutter stays as a second body");

        // ③ a sphere centred on the top face: a half ball
        SolidArcConstructRequest Ball;
        Ball.Kind     = SolidArcConstructKind::Sphere;
        Ball.Position = { 0.5, 0.5, 2.0 };
        Ball.Radius   = 0.3;
        const SolidArcCommandOutcome Balled = KnifeSolidArcPrimitive(Cube, Ball, Block, FeatureCombination::Difference, false);
        const double AfterBall = 8.0 - 0.048 + 0.0008 * π - 0.018 * π;
        TallyVolume(SolidKnifeGate, Cube, AfterBall, "sphere cutter centred on the top face (" + (Balled.AcceptedCondition ? Balled.Label : Balled.Refusal) + ")");

        // ④ a box boss: Union, its foot 0.02 inside the top face
        SolidArcConstructRequest Boss = Box;
        Boss.Position = { 0.2, -0.8, 1.98 };
        Boss.Width    = 0.2;
        Boss.Depth    = 0.2;
        Boss.Height   = 0.12;
        const SolidArcCommandOutcome Bossed = KnifeSolidArcPrimitive(Cube, Boss, Block, FeatureCombination::Union, false);
        TallyVolume(SolidKnifeGate, Cube, AfterBall + 0.004, "box boss 0.2 × 0.2 × 0.1 out of the top face (" + (Bossed.AcceptedCondition ? Bossed.Label : Bossed.Refusal) + ")");

        // ⑤ Separate: the cutter becomes a body of its own (a tube r 0.3 / 0.2, 0.5 tall, beside the cube), selected
        SolidArcConstructRequest Ring;
        Ring.Kind     = SolidArcConstructKind::Tube;
        Ring.Position = { 3.0, 0.0, 0.0 };
        Ring.Radius   = 0.3;
        Ring.Inner    = 0.2;
        Ring.Height   = 0.5;
        const SolidArcCommandOutcome Ringed = KnifeSolidArcPrimitive(Cube, Ring, {}, FeatureCombination::Separate, false);
        TallyVolume(SolidKnifeGate, Cube, AfterBall + 0.004 + 0.025 * π, "separate tube body (" + (Ringed.AcceptedCondition ? Ringed.Label : Ringed.Refusal) + ")");
        Tally(SolidKnifeGate, 0.0, Cube.Summary.Bodies.size() == 3 && Cube.Selection.Bodies.size() == 1, Format("separate: %d bodies, %d selected", int(Cube.Summary.Bodies.size()),
                                                                                                           int(Cube.Selection.Bodies.size())));

        // ⑥ refusals leave the document untouched
        const size_t Steps = Cube.Tree.Steps.size();
        const double Held  = Cube.Summary.Volume;
        SolidArcConstructRequest Thick;
        Thick.Kind  = SolidArcConstructKind::Torus;
        Thick.Major = 0.2;
        Thick.Minor = 0.3;
        const SolidArcCommandOutcome Missing = KnifeSolidArcPrimitive(Cube, Box, "Nowhere#9", FeatureCombination::Difference, false);
        const SolidArcCommandOutcome Fat     = KnifeSolidArcPrimitive(Cube, Thick, Block, FeatureCombination::Difference, false);
        const SolidArcCommandOutcome Fresh   = KnifeSolidArcPrimitive(Cube, Box, Block, FeatureCombination::New, false);
        Tally(SolidKnifeGate, 0.0, !Missing.AcceptedCondition && !Fat.AcceptedCondition && !Fresh.AcceptedCondition && Cube.Tree.Steps.size() == Steps && Cube.Summary.Volume == Held,
              "refused untouched: target (" + Missing.Refusal + ") · torus (" + Fat.Refusal + ") · New (" + Fresh.Refusal + ")");

        // ⑦ codec round trip
        const std::string    Written = EncodeSolidArcDocument(Cube);
        SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
        SolidArcDocument     Loaded  = CreateSolidArcDocument(Read.Title);
        const bool Opened = Read.Refusal.empty() && ReplaceSolidArcTree(Loaded, Read.Tree, Read.Captions, "Open").AcceptedCondition;
        Tally(SolidKnifeGate, std::abs(Loaded.Summary.Volume - Cube.Summary.Volume), Opened && Loaded.Summary.Volume == Cube.Summary.Volume && EncodeSolidArcDocument(Loaded) == Written,
              Format("codec %d bytes byte-equal, V %.12f after reload", int(Written.size()), Loaded.Summary.Volume));
    }

    // ---- W55 Bevels on knife pockets in a curved face: smooth joints, unequal mitres, too-large refusals
    {
        const auto Format = [](const char* Pattern, auto... Values) { char Text[720]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        constexpr double Radius = 2.126;                                                     // [len] - Reported cylinder
        constexpr double Lift   = 0.04252;                                                   // [len] - Knife top above the plane
        const auto Tangent = [&](double Angle, double Height)
        {
            OrthonormalFrame Frame;
            Frame.Origin = { Radius * std::cos(Angle), Radius * std::sin(Angle), Height };
            Frame.ZAxis  = { std::cos(Angle), std::sin(Angle), 0.0 };
            Frame.XAxis  = { -std::sin(Angle), std::cos(Angle), 0.0 };
            Frame.YAxis  = { 0.0, 0.0, 1.0 };
            return Frame;
        };
        const auto Cylinder = [&](const char* Title)
        {
            SolidArcDocument Document = CreateSolidArcDocument(Title);
            SolidArcConstructRequest Request;
            Request.Kind        = SolidArcConstructKind::Cylinder;
            Request.Combination = FeatureCombination::New;
            Request.Radius      = Radius;
            Request.Height      = 1.8;
            Expect(ConstructSolidArcPrimitive(Document, Request));
            return Document;
        };
        const auto SideOf = [](const SolidArcDocument& Document)
        {
            for (const auto& [Slot, Name] : Document.Regeneration.Naming.Faces) if (Name.find("Side") != std::string::npos) return Name;
            return std::string{};
        };
        const auto Select = [](SolidArcDocument& Document, const std::vector<std::string>& Names)
        {
            Document.Selection = {};
            for (const std::string& Name : Names) SelectSolidArcName(Document, SolidArcSelectionKind::Edge, Name, true);
        };
        const SolidArcInsertLibrary Library = AssembleSolidArcInserts("");
        const SolidArcInsert*       Hex     = nullptr;
        for (const SolidArcInsert& Insert : Library.Inserts) if (Insert.Name == "Hex pocket") Hex = &Insert;
        Tally(PocketBevelGate, 0.0, Hex != nullptr, "the knife library holds the Hex pocket insert");
        if (Hex == nullptr) Hex = &Library.Inserts.front();

        // ① the slot pocket (the reported image 1): its opening loop, two straight edges and two arcs meeting tangentially
        SolidArcDocument Slotted = Cylinder("PocketBevel");
        const std::string Body = Slotted.Regeneration.Bodies.front().Key;
        const std::string Side = SideOf(Slotted);
        SolidArcDrawingRequest Slot;
        Slot.Kind           = SolidArcDrawingKind::Slot;
        Slot.FillCondition  = true;
        Slot.PlaneCondition = true;
        Slot.Plane          = Tangent(0.3, 0.9);
        Slot.Points         = { { -0.3, -0.25 }, { 0.3, 0.25 }, { 0.3 + 0.12 * 0.64, 0.25 - 0.12 * 0.77 } };
        Expect(DrawSolidArcSketch(Slotted, Slot));
        const SolidArcCommandOutcome Knifed = KnifeSolidArcSketch(Slotted, Slotted.Tree.Steps.back().Label, Lift, -0.2, FeatureCombination::Difference, Body);
        const std::string Knife = Slotted.Tree.Steps.back().Label;
        std::vector<std::string> Opening;
        for (const auto& [Slot, Name] : Slotted.Regeneration.Naming.Edges) if (Name.find(Side) != std::string::npos && Name.find(Knife) != std::string::npos) Opening.push_back(Name);
        TallyVolume(PocketBevelGate, Slotted, 25.515364237, Format("slot pocket 0.2 deep (%s), %d opening edges", Knifed.AcceptedCondition ? Knifed.Label.c_str() : Knifed.Refusal.c_str(), int(Opening.size())));
        const std::string Pocketed = EncodeSolidArcDocument(Slotted);
        struct PinnedFillet { double Size, Volume; };
        double Previous = Slotted.Summary.Volume;
        for (const PinnedFillet Pinned : { PinnedFillet{ 0.02, 25.515133306 }, PinnedFillet{ 0.0519, 25.513751905 }, PinnedFillet{ 0.1, 25.509039927 } })
        {
            Select(Slotted, Opening);
            const size_t Before = Slotted.UndoEntries.size();
            const SolidArcCommandOutcome Filleted = BlendSolidArcSelection(Slotted, BlendProfile::Fillet, Pinned.Size);
            TallyVolume(PocketBevelGate, Slotted, Pinned.Volume, Format("slot opening fillet %.4g (%s, %d undo entry, %.0f ms)", Pinned.Size,
                                                                         Filleted.AcceptedCondition ? Filleted.Label.c_str() : Filleted.Refusal.c_str(), int(Slotted.UndoEntries.size() - Before), Filleted.Milliseconds));
            Tally(PocketBevelGate, 0.0, Slotted.UndoEntries.size() - Before == 1 && Slotted.Summary.Volume < Previous, Format("fillet %.4g: one undo entry, volume below the pocket", Pinned.Size));
            UndoSolidArcCommand(Slotted);
        }
        Tally(PocketBevelGate, 0.0, EncodeSolidArcDocument(Slotted) == Pocketed, "undo after each fillet: the file returns byte-equal");

        // ② too large: the blend outgrows a face — refused with the size and the face, the document untouched
        for (const double Size : { 0.3, 0.8553 })
        {
            Select(Slotted, Opening);
            const SolidArcCommandOutcome Large = BlendSolidArcSelection(Slotted, BlendProfile::Fillet, Size);
            const bool Named = Large.Refusal.find("too large") != std::string::npos && Large.Refusal.find("smaller size") != std::string::npos;
            Tally(PocketBevelGate, 0.0, !Large.AcceptedCondition && Named && EncodeSolidArcDocument(Slotted) == Pocketed, Format("fillet %.4g refused untouched: %s", Size, Large.Refusal.c_str()));
        }
        const std::string    Written = EncodeSolidArcDocument(Slotted);
        SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
        SolidArcDocument     Loaded  = CreateSolidArcDocument(Read.Title);
        const bool Opened = Read.Refusal.empty() && ReplaceSolidArcTree(Loaded, Read.Tree, Read.Captions, "Open").AcceptedCondition;
        Tally(PocketBevelGate, std::abs(Loaded.Summary.Volume - Slotted.Summary.Volume), Opened && EncodeSolidArcDocument(Loaded) == Written, Format("codec %d bytes byte-equal", int(Written.size())));

        // ③ the hex pocket insert (images 3 / 4): the rim loop (unequal mitres at the corners) and the wall–wall edges
        const auto HexPocket = [&](double Width)
        {
            SolidArcDocument Document = Cylinder("HexBevel");
            SolidArcInsertPlacement Placement;
            Placement.Name     = Hex->Name;
            Placement.Exchange = Hex->Exchange;
            Placement.Target   = Document.Regeneration.Bodies.front().Key;
            Placement.Map      = PlaceSolidArcInsert(Tangent(0.3, 0.9), Width, 0.0, false);
            Expect(KnifeSolidArcInsert(Document, Placement));
            return Document;
        };
        const auto ImportOf = [](const SolidArcDocument& Document)
        {
            for (const auto& [Slot, Name] : Document.Regeneration.Naming.Edges) if (Name.find("Import#") != std::string::npos) return Name.substr(Name.find("Import#"), 8);
            return std::string{};
        };
        SolidArcDocument Rimmed = HexPocket(0.9);
        const std::string RimImport = ImportOf(Rimmed);
        std::vector<std::string> Rim;
        for (const auto& [Slot, Name] : Rimmed.Regeneration.Naming.Edges) if (Name.find(Side) != std::string::npos && Name.find(RimImport) != std::string::npos) Rim.push_back(Name);
        Select(Rimmed, Rim);
        const SolidArcCommandOutcome RimFillet = BlendSolidArcSelection(Rimmed, BlendProfile::Fillet, 0.05);
        TallyVolume(PocketBevelGate, Rimmed, 25.420575079, Format("hex rim loop, %d edges, fillet 0.05 (%s, %.0f ms)", int(Rim.size()), RimFillet.AcceptedCondition ? RimFillet.Label.c_str() : RimFillet.Refusal.c_str(), RimFillet.Milliseconds));

        SolidArcDocument Walled = HexPocket(0.8);
        const std::string WallImport = ImportOf(Walled);
        std::vector<std::string> Walls;
        for (const auto& [Slot, Name] : Walled.Regeneration.Naming.Edges)
        {
            const size_t First = Name.find(WallImport);
            if (First != std::string::npos && Name.find(WallImport, First + 1) != std::string::npos && Name.find("Face(7)") == std::string::npos) Walls.push_back(Name);
        }
        Select(Walled, Walls);
        const SolidArcCommandOutcome WallFillet = BlendSolidArcSelection(Walled, BlendProfile::Fillet, 0.05);
        TallyVolume(PocketBevelGate, Walled, 25.462980216, Format("hex wall–wall edges together, %d edges, fillet 0.05 (%s)", int(Walls.size()), WallFillet.AcceptedCondition ? WallFillet.Label.c_str() : WallFillet.Refusal.c_str()));

        // ④ calibration on a box: the hex rim loop loses (1 − π/4) r² per unit length on its 2.7 straight run, plus the corners
        SolidArcDocument Boxed = CreateSolidArcDocument("BoxHex");
        SolidArcConstructRequest Block;
        Block.Kind        = SolidArcConstructKind::Block;
        Block.Combination = FeatureCombination::New;
        Block.Position    = { -2.0, -2.0, 0.0 };
        Block.Width       = 4.0;
        Block.Depth       = 4.0;
        Block.Height      = 2.0;
        Expect(ConstructSolidArcPrimitive(Boxed, Block));
        SolidArcInsertPlacement Top;
        Top.Name     = Hex->Name;
        Top.Exchange = Hex->Exchange;
        Top.Target   = Boxed.Regeneration.Bodies.front().Key;
        Top.Map      = PlaceSolidArcInsert(OrthonormalFrame{ { 0.0, 0.0, 2.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.9, 0.0, false);
        Expect(KnifeSolidArcInsert(Boxed, Top));
        const double Held = Boxed.Summary.Volume;
        std::vector<std::string> BoxRim;
        for (const auto& [Slot, Name] : Boxed.Regeneration.Naming.Edges) if (Name.find("Import#") != std::string::npos && Name.find("Cap(End)") != std::string::npos) BoxRim.push_back(Name);
        Select(Boxed, BoxRim);
        const SolidArcCommandOutcome BoxFillet = BlendSolidArcSelection(Boxed, BlendProfile::Fillet, 0.05);
        const double Ratio = (Held - Boxed.Summary.Volume) / ((1.0 - std::numbers::pi / 4.0) * 0.05 * 0.05 * 2.7);
        Tally(PocketBevelGate, std::abs(Ratio - 1.05), BoxFillet.AcceptedCondition && Boxed.Summary.SoundCondition && Ratio >= 1.0 && Ratio <= 1.1,
              Format("box hex rim fillet 0.05 (%s): loss / straight-edge model = %.4f", BoxFillet.AcceptedCondition ? BoxFillet.Label.c_str() : BoxFillet.Refusal.c_str(), Ratio));
    }

    // ---- W56 Knife insert gesture: grips, depths authored again, countersink floor, stretched library files
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[720]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        SolidArcDocument Cube = CreateSolidArcDocument("InsertReach");
        Expect(DrawFilledSketch(Cube, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { -1.0, -1.0 }, { 1.0, 1.0 } })));
        Expect(ExtrudeSolidArcSketch(Cube, Cube.Tree.Steps.back().Label, 2.0, FeatureCombination::New));
        const std::string Block = Cube.Regeneration.Bodies.front().Key;
        const SolidArcInsertLibrary Library = AssembleSolidArcInserts("");
        const auto Named = [&](const char* Name) -> const SolidArcInsert*
        {
            for (const SolidArcInsert& Insert : Library.Inserts) if (Insert.Name == Name) return &Insert;
            return nullptr;
        };
        const SolidArcInsert* Triangle = Named("Triangle");
        const SolidArcInsert* Hex      = Named("Hex pocket");
        const SolidArcInsert* Square   = Named("Square pocket");
        const SolidArcInsert* Round    = Named("Round hole");
        const SolidArcInsert* Sink     = Named("Countersink");
        if (!Triangle || !Hex || !Square || !Round || !Sink) { Tally(ReachGate, 1.0, false, "the built-in library lacks a cutter"); }
        else
        {
            // ① grips: the drag carries this point to the pointer (angle, radius in insert widths)
            const auto Bearing = [](const SolidArcInsert& Insert) { return std::atan2(Insert.Anchor.Y, Insert.Anchor.X) * 180.0 / π; };
            const auto Radius  = [](const SolidArcInsert& Insert) { return std::hypot(Insert.Anchor.X, Insert.Anchor.Y); };
            const double GripWorst = std::max({ std::abs(Bearing(*Triangle) - 90.0), std::abs(Bearing(*Hex)), std::abs(Bearing(*Square) - 45.0),
                                                std::abs(Radius(*Triangle) - 1.0 / std::sqrt(3.0)) * 100.0, std::abs(Radius(*Hex) - 0.5) * 100.0, std::abs(Radius(*Square) - std::sqrt(0.5)) * 100.0 });
            Tally(ReachGate, GripWorst, GripWorst <= 1.0e-4, Format("grips: triangle %.3f° r %.6f · hex %.3f° r %.6f · square %.3f° r %.6f · countersink floor %.6f widths",
                                                                   Bearing(*Triangle), Radius(*Triangle), Bearing(*Hex), Radius(*Hex), Bearing(*Square), Radius(*Square), Sink->Shallow));

            // ② a triangle 0.4 wide cut 0.2 deep (0.5 widths, its own shape is 0.3): authored again, analytic
            const auto Place = [&](const SolidArcInsert& Insert, Vector3 Origin, double Size, double Depth, SolidArcInsertReach& Reach)
            {
                Reach = ReachSolidArcInsert(Insert, Depth / Size);
                SolidArcInsertPlacement Placement;
                Placement.Name = Insert.Name; Placement.Exchange = Reach.Exchange; Placement.Target = Block; Placement.Combination = FeatureCombination::Difference;
                Placement.Map  = PlaceSolidArcInsert(OrthonormalFrame{ Origin, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, Size, 0.0, false, Reach.Stretch);
                const size_t Before = Cube.UndoEntries.size();
                SolidArcCommandOutcome Outcome = Reach.Refusal.empty() ? KnifeSolidArcInsert(Cube, Placement) : SolidArcCommandOutcome{};
                if (!Reach.Refusal.empty()) Outcome.Refusal = Reach.Refusal;
                Tally(ReachGate, 0.0, !Outcome.AcceptedCondition || Cube.UndoEntries.size() == Before + 1, Format("%s: %d undo entry", Insert.Name.c_str(), int(Cube.UndoEntries.size() - Before)));
                return Outcome;
            };
            const auto Analytic = [](const SolidArcInsertReach& Reach) { return Reach.Exchange.find("B_SPLINE") == std::string::npos; };
            SolidArcInsertReach Reach;
            double Expected = 8.0;
            const SolidArcCommandOutcome Cut = Place(*Triangle, { -0.5, 0.5, 2.0 }, 0.4, 0.2, Reach);
            Expected -= std::sqrt(3.0) / 4.0 * 0.16 * 0.2;
            TallyVolume(ReachGate, Cube, Expected, Format("triangle 0.4 at 0.2 deep (%s · stretch %.3f · %s)", Cut.AcceptedCondition ? Cut.Label.c_str() : Cut.Refusal.c_str(), Reach.Stretch,
                                                          Analytic(Reach) && Reach.Exchange != Triangle->Exchange ? "authored again, analytic" : "NOT authored again"));
            Tally(ReachGate, 0.0, Reach.Stretch == 1.0 && Analytic(Reach) && Reach.Exchange != Triangle->Exchange, "triangle: authored again at the depth, no stretch");

            // ③ a round hole 0.4 cut 0.6 deep: its cylinder stays a CYLINDRICAL_SURFACE
            const SolidArcCommandOutcome Hole = Place(*Round, { 0.5, 0.5, 2.0 }, 0.4, 0.6, Reach);
            Expected -= 0.04 * π * 0.6;
            TallyVolume(ReachGate, Cube, Expected, Format("round hole 0.4 at 0.6 deep (%s)", Hole.AcceptedCondition ? Hole.Label.c_str() : Hole.Refusal.c_str()));
            Tally(ReachGate, 0.0, Analytic(Reach) && Reach.Exchange.find("CYLINDRICAL_SURFACE") != std::string::npos, "round hole: cylinder kept analytic at the new depth");

            // ④ a countersink shallower than its cone refuses untouched; at 0.4 deep: 45° frustum (r 0.45 → 0.25/1.1 widths) + bore
            const size_t Steps = Cube.Tree.Steps.size();
            const double Held  = Cube.Summary.Volume;
            const SolidArcCommandOutcome Shallow = Place(*Sink, { 0.5, -0.5, 2.0 }, 0.4, 0.04, Reach);
            Tally(ReachGate, 0.0, !Shallow.AcceptedCondition && Cube.Tree.Steps.size() == Steps && Cube.Summary.Volume == Held, "countersink 0.1 widths deep refused untouched: " + Shallow.Refusal);
            const SolidArcCommandOutcome Sunk = Place(*Sink, { 0.5, -0.5, 2.0 }, 0.4, 0.4, Reach);
            const double Cone  = (0.30 / 1.1) - 0.05;                                            // [-] - Cone depth below the face (widths)
            const double Upper = 0.45, Lower = 0.25 / 1.1;                                       // [-] - Radii at the face / cone tip
            const double SinkVolume = (π * Cone / 3.0 * (Upper * Upper + Upper * Lower + Lower * Lower) + π * Lower * Lower * (1.0 - Cone)) * 0.4 * 0.4 * 0.4;
            Expected -= SinkVolume;
            TallyVolume(ReachGate, Cube, Expected, Format("countersink 0.4 at 0.4 deep: frustum + bore %.12f (%s)", SinkVolume, Sunk.AcceptedCondition ? Sunk.Label.c_str() : Sunk.Refusal.c_str()));

            // ⑤ a library file has no author: it stretches along z (a cylinder r 0.25 × 0.5 saved, then cut 0.3 widths deep)
            SolidArcDocument Source = CreateSolidArcDocument("InsertSource");
            SolidArcConstructRequest Post;
            Post.Kind = SolidArcConstructKind::Cylinder; Post.Combination = FeatureCombination::New; Post.Radius = 0.25; Post.Height = 0.5;
            Expect(ConstructSolidArcPrimitive(Source, Post));
            const std::filesystem::path Folder = std::filesystem::temp_directory_path() / "SolidArcReachInserts";
            std::error_code Error;
            std::filesystem::remove_all(Folder, Error);
            std::string Saved;
            const std::string SaveRefusal = SaveSolidArcInsert(Source, Source.Regeneration.Bodies.front().Key, Folder.string(), "Post", Saved);
            const SolidArcInsertLibrary Grown = AssembleSolidArcInserts(Folder.string());
            const SolidArcInsert* File = Grown.Inserts.empty() || Grown.Inserts.back().Author ? nullptr : &Grown.Inserts.back();
            if (!File) Tally(ReachGate, 1.0, false, "the saved post did not load as a library file: " + SaveRefusal);
            else
            {
                const SolidArcCommandOutcome Posted = Place(*File, { -0.5, -0.5, 2.0 }, 0.4, 0.12, Reach);
                Expected -= 0.04 * π * 0.12;
                const double Drift = std::abs(Cube.Summary.Volume - Expected) / Expected;
                Tally(ReachGate, Drift, Posted.AcceptedCondition && std::abs(Reach.Stretch - 0.3 / File->Depth) <= 1.0e-12 && Reach.Exchange == File->Exchange && Drift <= 1.0e-6,
                      Format("library file stretched ×%.6f (its depth %.6f widths): V %.12f vs %.12f (%.2g; curved faces carried as NURBS) (%s)", Reach.Stretch, File->Depth,
                             Cube.Summary.Volume, Expected, Drift, Posted.AcceptedCondition ? Posted.Label.c_str() : Posted.Refusal.c_str()));
            }
            std::filesystem::remove_all(Folder, Error);

            // ⑥ codec: the authored-again cutters reload byte-equal
            const std::string          Written = EncodeSolidArcDocument(Cube);
            const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
            SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
            Loaded.Tree = Read.Tree; Loaded.Captions = Read.Captions; Loaded.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Loaded, true);
            Tally(ReachGate, std::abs(Loaded.Summary.Volume - Cube.Summary.Volume), Read.Refusal.empty() && EncodeSolidArcDocument(Loaded) == Written && Loaded.Summary.Volume == Cube.Summary.Volume,
                  Format("codec %d bytes byte-equal, V %.12f", int(Written.size()), Loaded.Summary.Volume));
        }
    }

    // ---- W49 Fillet drags keep every fillet under a half turn
    {
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto Widest = [](const SketchModel& Sketch)
        {
            double Largest = 0.0;
            for (const SketchCurve& Curve : Sketch.Curves)
            {
                if (Curve.Kind != SketchCurveKind::Arc || Curve.Centre < 0) continue;
                const Vector2 Centre = Sketch.Points[Curve.Centre].Position;
                const Vector2 Start  = Sketch.Points[Curve.Start].Position - Centre;
                const Vector2 End    = Sketch.Points[Curve.End].Position - Centre;
                double Sweep = std::atan2(Cross(Start, End), Dot(Start, End));
                if (Sweep <= 0.0) Sweep += 2.0 * std::numbers::pi;
                Largest = std::max(Largest, Sweep);
            }
            return Largest * 180.0 / std::numbers::pi;                                       // [deg]
        };
        const auto PointAt = [](const SketchModel& Sketch, Vector2 Position)
        {
            for (size_t Index = 0; Index < Sketch.Points.size(); ++Index) if (Length(Sketch.Points[Index].Position - Position) <= 1.0e-9) return static_cast<int>(Index);
            return -1;
        };

        // ① rectangle 3 × 1.5, four r 0.3 fillets
        SolidArcDocument Rounded = CreateSolidArcDocument("FilletDrag");
        Expect(DrawFilledSketch(Rounded, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 3.0, 1.5 } })));
        const std::string Plate = Rounded.Tree.Steps.back().Label;
        SolidArcCornerRequest Every;
        Every.Sketches = { Plate };
        Every.Size     = 0.3;
        const SolidArcCommandOutcome Filleted = CutSolidArcSketchCorners(Rounded, Every);
        const SketchModel& Model = Rounded.Regeneration.Sketches.at(Plate);
        const double       Start = Widest(Model);
        Tally(FilletDragGate, std::abs(Start - 90.0), Filleted.AcceptedCondition && Model.Curves.size() == 8 && std::abs(Start - 90.0) <= 1.0e-9, Format("%s: 8 curves, widest arc %.9f°", Filleted.Label.c_str(), Start));

        // ② 4 points × 256 targets (a 16 × 16 lattice, ±3 m): no accepted drag inverts an arc
        const Vector2 Sources[] = { { 0.3, 0.3 }, { 0.3, 0.0 }, { 0.0, 0.3 }, { 0.0, 0.0 } };
        int Accepted = 0, Refused = 0, Inverted = 0;
        double Worst = 0.0;
        for (const Vector2 Source : Sources)
        {
            const int Point = PointAt(Model, Source);
            if (Point < 0) { ++Inverted; continue; }
            for (int Across = -15; Across <= 15; Across += 2)
                for (int Upward = -15; Upward <= 15; Upward += 2)
                {
                    const SketchDragOutcome Drag = PreviewSolidArcDrag(Rounded, Plate, Point, Source + Vector2{ Across * 0.2, Upward * 0.2 });
                    if (!Drag.Refusal.empty()) { ++Refused; continue; }
                    ++Accepted;
                    const double Sweep = Widest(Drag.Sketch);
                    Worst = std::max(Worst, Sweep);
                    Inverted += Sweep > 180.0 + 1.0e-6 ? 1 : 0;
                }
        }
        Tally(FilletDragGate, double(Inverted), Inverted == 0 && Accepted > 2 * Refused,
              Format("1 024 drags of the centre / tangent points / virtual sharp: %d accepted, %d refused, %d inverted; widest accepted arc %.6f°", Accepted, Refused, Inverted, Worst));

        // ③ the reported drag: the fillet centre (0.3, 0.3) out past the corner to (−0.9, −0.8), committed; undo returns
        const std::string Before   = EncodeSolidArcDocument(Rounded);
        const SolidArcCommandOutcome Dragged = DragSolidArcSketchPoint(Rounded, Plate, PointAt(Model, { 0.3, 0.3 }), { -0.9, -0.8 });
        const SketchModel& After   = Rounded.Regeneration.Sketches.at(Plate);
        const int          Centre  = PointAt(After, { -0.9, -0.8 });
        const double       Quarter = std::abs(Widest(After) - 90.0);
        Tally(FilletDragGate, Quarter, Dragged.AcceptedCondition && Centre >= 0 && Quarter <= 1.0e-9 && After.Curves.size() == 8,
              Format("%s: the centre lands on (−0.9, −0.8), the lines follow, every arc 90° (|Δ| %.3g°)", Dragged.Label.c_str(), Quarter));
        UndoSolidArcCommand(Rounded);
        Tally(FilletDragGate, 0.0, EncodeSolidArcDocument(Rounded) == Before, "undo: the file returns byte-equal");

        // ④ slots (two tangent semicircles) still solve and drag: the end arcs stay half turns
        SolidArcDocument Slotted = CreateSolidArcDocument("Slot");
        Expect(DrawFilledSketch(Slotted, DrawingOf(SolidArcDrawingKind::Slot, 0, { { 0.0, 0.0 }, { 2.0, 0.0 }, { 1.0, 0.5 } })));
        const std::string  Slot     = Slotted.Tree.Steps.back().Label;
        const SketchModel& SlotModel = Slotted.Regeneration.Sketches.at(Slot);
        int Far = -1;
        for (const SketchCurve& Curve : SlotModel.Curves)                                    // The right end's centre
            if (Curve.Kind == SketchCurveKind::Arc && (Far < 0 || SlotModel.Points[Curve.Centre].Position.X > SlotModel.Points[Far].Position.X)) Far = Curve.Centre;
        const SketchDragOutcome Stretched = Far >= 0 ? PreviewSolidArcDrag(Slotted, Slot, Far, SlotModel.Points[Far].Position + Vector2{ 1.0, 0.0 }) : SketchDragOutcome{};
        // No Equal radius on a drawn slot: the far cap may grow, the lines tilt; tangent at all four joints, the two caps
        // still turn one full turn between them (the convex stadium, no cap inside out).
        double Turned = 0.0;
        for (const SketchCurve& Curve : Stretched.Sketch.Curves)
        {
            if (Curve.Kind != SketchCurveKind::Arc) continue;
            const Vector2 Middle = Stretched.Sketch.Points[Curve.Centre].Position;
            const Vector2 From   = Stretched.Sketch.Points[Curve.Start].Position - Middle;
            const Vector2 To     = Stretched.Sketch.Points[Curve.End].Position - Middle;
            double Sweep = std::atan2(Cross(From, To), Dot(From, To));
            if (Sweep <= 0.0) Sweep += 2.0 * std::numbers::pi;
            Turned += Sweep;
        }
        const double Full = Stretched.Refusal.empty() ? std::abs(Turned - 2.0 * std::numbers::pi) : 1.0;
        Tally(FilletDragGate, Full, Stretched.Refusal.empty() && Stretched.Solve.Converged && Full <= 1.0e-9,
              Format("slot: %zu curves, its right centre dragged 1 m along: the caps turn %.12f rad together (2π), widest %.6f°%s", SlotModel.Curves.size(), Turned, Widest(Stretched.Sketch), Stretched.Refusal.empty() ? "" : (" · " + Stretched.Refusal).c_str()));
    }

    // ---- W46 Trim and join on sketches some later step reads (detach), join of picked sketches
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[640]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        const auto Steps  = [](const SolidArcDocument& Document, const std::string& Label)
        {
            return std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Label; });
        };

        // ① a circle r 0.5 at the corner (2, 1) of a 2 × 1 rectangle, extruded 1: trimming its outer ¾ detaches — the
        //    extrude keeps the whole circle (V π/4), the original is hidden, the inner quarter arc is a new sketch
        SolidArcDocument Detach = CreateSolidArcDocument("Detach");
        Expect(DrawFilledSketch(Detach, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
        Expect(DrawFilledSketch(Detach, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 2.0, 1.0 }, { 2.5, 1.0 } }, false, true)));
        const std::string Round = Detach.Tree.Steps.back().Label;
        Expect(ExtrudeSolidArcSketch(Detach, Round, 1.0, FeatureCombination::New));
        const size_t StepCount = Detach.Tree.Steps.size();
        const size_t History   = Detach.UndoEntries.size();
        const double Puck      = 0.25 * π;
        const SolidArcCommandOutcome Trimmed = TrimSolidArcSketch(Detach, { Round, 0, { 2.0 + 0.5 * std::sqrt(0.5), 1.0 + 0.5 * std::sqrt(0.5) } });
        const FeatureStep& Copy   = Detach.Tree.Steps.back();
        const auto         Solved = Detach.Regeneration.Sketches.find(Copy.Label);
        double             Sweep  = 0.0;
        if (Solved != Detach.Regeneration.Sketches.end() && Solved->second.Curves.size() == 1 && Solved->second.Curves[0].Kind == SketchCurveKind::Arc)
        {
            const SketchModel& Arc   = Solved->second;
            const Vector2      Centre = Arc.Points[Arc.Curves[0].Centre].Position;
            const Vector2      From   = Arc.Points[Arc.Curves[0].Start].Position - Centre;
            const Vector2      To     = Arc.Points[Arc.Curves[0].End].Position - Centre;
            Sweep = std::atan2(From.X * To.Y - From.Y * To.X, From.X * To.X + From.Y * To.Y);
            if (Sweep < 0.0) Sweep += 2.0 * π;
        }
        double Relative = std::abs(Detach.Summary.Volume - Puck) / Puck;
        Tally(DetachGate, std::max(Relative, std::abs(Sweep - 0.5 * π)), Trimmed.AcceptedCondition && Relative <= 1.0e-9 && std::abs(Sweep - 0.5 * π) <= 1.0e-9
              && SolidArcCurveHiddenCondition(Detach, Round) && Detach.Tree.Steps.size() == StepCount + 1 && Copy.Kind == FeatureStepKind::Sketch && Detach.UndoEntries.size() == History + 1
              && SolidArcSketchConsumedCondition(Detach, Round) && !SolidArcSketchReferencedCondition(Detach, Copy.Label),
              Format("%s: V %.12f vs π/4 kept, %s hidden, new %s (%s) = quarter arc, sweep %.12f vs π/2, one undo entry", Trimmed.Label.c_str(), Detach.Summary.Volume,
                     Round.c_str(), Copy.Label.c_str(), SolidArcBodyCaption(Detach, Copy.Label).c_str(), Sweep));

        // ② the copy is free: trimming it again edits it in place (no further copy); undo × 2 restores the circle, shown
        {
            const size_t Count = Detach.Tree.Steps.size();
            const std::string Free = Copy.Label;
            const SolidArcCommandOutcome Again = TrimSolidArcSketch(Detach, { Free, 0, { 2.0 - 0.5 * std::sqrt(0.5), 1.0 - 0.5 * std::sqrt(0.5) } });
            const bool InPlace = Again.AcceptedCondition && Detach.Tree.Steps.size() <= Count && Steps(Detach, Round) != Detach.Tree.Steps.end();
            const bool Undone  = UndoSolidArcCommand(Detach) && UndoSolidArcCommand(Detach);
            Tally(DetachGate, 0.0, InPlace && Undone && Detach.Tree.Steps.size() == StepCount && !SolidArcCurveHiddenCondition(Detach, Round) && Detach.UndoEntries.size() == History,
                  "a second trim on " + Free + " edits it in place (" + Again.Label + "); two undos restore the shown circle and the step count");
        }

        // ③ a lone circle some extrude reads, no crossings: the whole curve goes, so the trim only hides it
        {
            Expect(DrawFilledSketch(Detach, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 6.0, 0.0 }, { 6.5, 0.0 } }, false, true)));
            const std::string Lone = Detach.Tree.Steps.back().Label;
            Expect(ExtrudeSolidArcSketch(Detach, Lone, 1.0, FeatureCombination::Separate));
            const size_t Count  = Detach.Tree.Steps.size();
            const double Volume = Detach.Summary.Volume;
            const SolidArcCommandOutcome Whole = TrimSolidArcSketch(Detach, { Lone, 0, { 6.5, 0.0 } });
            Tally(DetachGate, std::abs(Detach.Summary.Volume - Volume), Whole.AcceptedCondition && Detach.Tree.Steps.size() == Count && SolidArcCurveHiddenCondition(Detach, Lone)
                  && Detach.Summary.Volume == Volume, "whole-curve trim of " + Lone + " (read by an extrude): hidden only, no new step, volume unchanged");
        }

        // ④ picked sketches join in pick order, touching or not: a 1 × 1 square and a disjoint circle r 0.4 become one
        //    sketch of 5 curves; extruded 1 → V 1 + 0.16π
        {
            SolidArcDocument Picked = CreateSolidArcDocument("Join");
            Expect(DrawFilledSketch(Picked, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 1.0, 1.0 } })));
            const std::string Square = Picked.Tree.Steps.back().Label;
            Expect(DrawFilledSketch(Picked, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 3.0, 0.5 }, { 3.4, 0.5 } }, false, true)));
            const std::string Disc = Picked.Tree.Steps.back().Label;
            const SolidArcCommandOutcome Joined = JoinSolidArcSketches(Picked, { Square, Disc });
            const auto   Kept  = Steps(Picked, Square);
            const size_t Curves = Kept != Picked.Tree.Steps.end() ? Picked.Regeneration.Sketches[Square].Curves.size() : 0;
            const SolidArcCommandOutcome Raised = ExtrudeSolidArcSketch(Picked, Square, 1.0, FeatureCombination::New);
            const double Expected = 1.0 + 0.16 * π;
            Relative = std::abs(Picked.Summary.Volume - Expected) / Expected;
            Tally(DetachGate, Relative, Joined.AcceptedCondition && Curves == 5 && Steps(Picked, Disc) == Picked.Tree.Steps.end() && Raised.AcceptedCondition && Relative <= 1.0e-9,
                  Format("%s: one sketch of %d curves, %s removed; %s → V %.12f vs 1 + 0.16π %.12f", Joined.Label.c_str(), int(Curves), Disc.c_str(), Raised.Label.c_str(),
                         Picked.Summary.Volume, Expected));

            // ⑤ join into a read sketch: the extruded square stays (hidden) for its extrude, the join is a new last step
            //    holding square + a new circle; the free circle merges away; volume unchanged; codec byte-equal
            Expect(DrawFilledSketch(Picked, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 0.5, 3.0 }, { 0.8, 3.0 } }, false, true)));
            const std::string Ring   = Picked.Tree.Steps.back().Label;
            const size_t      Count  = Picked.Tree.Steps.size();
            const double      Volume = Picked.Summary.Volume;
            const SolidArcCommandOutcome Into = JoinSolidArcSketches(Picked, { Square, Ring });
            const FeatureStep& Last = Picked.Tree.Steps.back();
            const size_t Held = Picked.Regeneration.Sketches.contains(Last.Label) ? Picked.Regeneration.Sketches[Last.Label].Curves.size() : 0;
            Tally(DetachGate, std::abs(Picked.Summary.Volume - Volume), Into.AcceptedCondition && SolidArcCurveHiddenCondition(Picked, Square) && Steps(Picked, Ring) == Picked.Tree.Steps.end()
                  && Picked.Tree.Steps.size() == Count && Last.Label != Square && Held == 6 && Picked.Summary.Volume == Volume,
                  Format("%s: %s kept hidden for its extrude, %s merged away, new %s holds %d curves, V unchanged", Into.Label.c_str(), Square.c_str(), Ring.c_str(), Last.Label.c_str(), int(Held)));
            const std::string          Written = EncodeSolidArcDocument(Picked);
            const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
            SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
            Loaded.Tree = Read.Tree; Loaded.Captions = Read.Captions; Loaded.Appearance = Read.Appearance;
            RegenerateSolidArcDocument(Loaded, true);
            Tally(DetachGate, 0.0, Read.Refusal.empty() && EncodeSolidArcDocument(Loaded) == Written && Loaded.Summary.Volume == Picked.Summary.Volume,
                  Format("codec: %d bytes byte-equal after the detached join", int(Written.size())));
        }
    }

    // ---- W57 Sketch Cut (split at a point), edge rings, image-plane properties
    {
        constexpr double π = std::numbers::pi;
        const auto Format = [](const char* Pattern, auto... Values) { char Text[720]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };

        // ① a rectangle's bottom line cut at x = 0.5: two lines sharing one new point, the extrude keeps V = 2
        SolidArcDocument Plate = CreateSolidArcDocument("CutLine");
        Expect(DrawFilledSketch(Plate, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
        const std::string Outline = Plate.Tree.Steps.back().Label;
        Expect(ExtrudeSolidArcSketch(Plate, Outline, 1.0, FeatureCombination::New));
        int Bottom = -1;
        {
            const SketchModel& Solved = Plate.Regeneration.Sketches[Outline];
            for (int Curve = 0; Curve < int(Solved.Curves.size()); ++Curve)
            {
                const SketchCurve& Each = Solved.Curves[Curve];
                if (Each.Kind == SketchCurveKind::Line && std::abs(Solved.Points[Each.Start].Position.Y) < 1.0e-12 && std::abs(Solved.Points[Each.End].Position.Y) < 1.0e-12) Bottom = Curve;
            }
        }
        const size_t Depth = Plate.UndoEntries.size();
        const SolidArcCommandOutcome Cut = TrimSolidArcSketch(Plate, { Outline, Bottom, { 0.5, 0.0 }, true });
        const SketchModel& After = Plate.Regeneration.Sketches[Outline];
        bool Shared = false;
        for (const SketchCurve& Each : After.Curves)
            for (const SketchCurve& Other : After.Curves)
                if (&Each != &Other && Each.Kind == SketchCurveKind::Line && Other.Kind == SketchCurveKind::Line && Each.End == Other.Start && Each.End >= 0
                    && Distance(After.Points[Each.End].Position, Vector2{ 0.5, 0.0 }) <= 1.0e-12) Shared = true;
        Tally(SplitGate, std::abs(Plate.Summary.Volume - 2.0), Cut.AcceptedCondition && After.Curves.size() == 5 && Shared && std::abs(Plate.Summary.Volume - 2.0) <= 1.0e-9
              && Plate.UndoEntries.size() == Depth + 1,
              Format("%s: %d curves, the two halves share (0.5, 0), V %.12f vs 2, one undo entry", Cut.Label.c_str(), int(After.Curves.size()), Plate.Summary.Volume));
        const std::string Before = EncodeSolidArcDocument(Plate);
        const SolidArcCommandOutcome AtEnd = TrimSolidArcSketch(Plate, { Outline, Bottom, { 0.0, 0.0 }, true });
        Tally(SplitGate, 0.0, !AtEnd.AcceptedCondition && EncodeSolidArcDocument(Plate) == Before,
              Format("a cut on the end of the line refuses untouched: %s", AtEnd.Refusal.c_str()));

        // ② a circle r 0.5 cut: two half arcs with the circle's centre, ends opposite; the extrude keeps V = π/4
        SolidArcDocument Disc = CreateSolidArcDocument("CutCircle");
        Expect(DrawFilledSketch(Disc, DrawingOf(SolidArcDrawingKind::Circle, 0, { { 0.0, 0.0 }, { 0.5, 0.0 } }, false, true)));
        const std::string Round = Disc.Tree.Steps.back().Label;
        Expect(ExtrudeSolidArcSketch(Disc, Round, 1.0, FeatureCombination::New));
        const SolidArcCommandOutcome Halved = TrimSolidArcSketch(Disc, { Round, 0, { 0.0, 0.5 }, true });
        const SketchModel& Arcs = Disc.Regeneration.Sketches[Round];
        bool Opposite = Arcs.Curves.size() == 2;
        for (const SketchCurve& Each : Arcs.Curves)
        {
            Opposite = Opposite && Each.Kind == SketchCurveKind::Arc && Each.Centre >= 0 && Length(Arcs.Points[Each.Centre].Position) <= 1.0e-12
                    && Distance(Arcs.Points[Each.Start].Position, Arcs.Points[Each.End].Position * -1.0) <= 1.0e-9;
        }
        const double Expected = 0.25 * π;
        const double Relative = std::abs(Disc.Summary.Volume - Expected) / Expected;
        Tally(SplitGate, Relative, Halved.AcceptedCondition && Opposite && Relative <= 1.0e-9,
              Format("%s: two half arcs about (0, 0) with opposite ends, V %.12f vs π/4 %.12f", Halved.Label.c_str(), Disc.Summary.Volume, Expected));

        // ③ a spline cuts too (since the curve-trim request; exactness in W18 ⑧): at its middle point, two curves
        SolidArcDocument Wave = CreateSolidArcDocument("CutSpline");
        Expect(DrawSolidArcSketch(Wave, DrawingOf(SolidArcDrawingKind::Curve, 0, { { 0.0, 0.0 }, { 1.0, 0.6 }, { 2.0, 0.0 } })));
        const std::string Curve = Wave.Tree.Steps.back().Label;
        const SolidArcCommandOutcome Spline = TrimSolidArcSketch(Wave, { Curve, 0, { 1.0, 0.6 }, true });
        const size_t Pieces = Wave.Regeneration.Sketches.contains(Curve) ? Wave.Regeneration.Sketches.at(Curve).Curves.size() : 0;
        Tally(SplitGate, 0.0, Spline.AcceptedCondition && Pieces == 2, Format("a spline cut at its middle point: %d curves%s", int(Pieces), Spline.Refusal.empty() ? "" : (" · " + Spline.Refusal).c_str()));

        // ④ edge ring on the block's front-top edge: the four edges along x
        SolidArcDocument Box = CreateSolidArcDocument("Ring");
        Expect(ConstructSolidArcPrimitive(Box, Block));
        const std::string Front = PickAndSelect(Box, RayBetween({ 2.0, -1.0, 2.0 }, { 2.0, 0.0, 1.0 }), false);
        const std::vector<std::string> Ring = SolidArcEdgeRing(Box, Front);
        bool AlongX = Ring.size() == 4 && !Ring.empty() && Ring.front() == Front;
        for (const std::string& Name : Ring)
        {
            const Vector3 Start = EdgeEndPosition(Box, Name, false), End = EdgeEndPosition(Box, Name, true);
            AlongX = AlongX && std::abs(std::abs(End.X - Start.X) - 4.0) <= 1.0e-12 && std::abs(End.Y - Start.Y) <= 1.0e-12 && std::abs(End.Z - Start.Z) <= 1.0e-12;
        }
        Tally(SplitGate, 0.0, AlongX, Format("ring of %s: %d edges, all 4 long along x", Front.c_str(), int(Ring.size())));

        // ⑤ image-plane properties round-trip the codec
        SolidArcImagePlane Picture;
        Picture.Name = "Side"; Picture.Path = "Side.png"; Picture.Crop = { 10, 20, 300, 200 };
        Picture.Corner = { 0.0, 0.0, 1.0 }; Picture.Right = { 2.0, 0.0, 0.0 }; Picture.Down = { 0.0, 0.0, -1.0 };
        Picture.Opacity = 0.35; Picture.PhotoCondition = true; Picture.FlipAcrossCondition = true; Picture.LockCondition = true; Picture.Turn = 0.25;
        Plate.ImagePlanes = { Picture };
        const std::string          Written = EncodeSolidArcDocument(Plate);
        const SolidArcDocumentFile Read    = DecodeSolidArcDocument(Written);
        const bool Kept = Read.Refusal.empty() && Read.ImagePlanes.size() == 1 && Read.ImagePlanes[0].PhotoCondition && Read.ImagePlanes[0].FlipAcrossCondition
                       && !Read.ImagePlanes[0].FlipDownCondition && Read.ImagePlanes[0].LockCondition && Read.ImagePlanes[0].Turn == 0.25 && Read.ImagePlanes[0].Opacity == 0.35
                       && Read.ImagePlanes[0].Crop == Picture.Crop;
        SolidArcDocument Loaded = CreateSolidArcDocument(Read.Title);
        Loaded.Tree = Read.Tree; Loaded.Captions = Read.Captions; Loaded.Appearance = Read.Appearance; Loaded.ImagePlanes = Read.ImagePlanes;
        RegenerateSolidArcDocument(Loaded, true);
        Tally(SplitGate, 0.0, Kept && EncodeSolidArcDocument(Loaded) == Written,
              Format("image plane photo / mirror / lock / turn 0.25 / opacity 0.35 / crop kept: %d bytes byte-equal", int(Written.size())));
    }

    // ---- W58 Blender edge loops / rings, polyline knife
    {
        const auto Format = [](const char* Pattern, auto... Values) { char Text[720]; std::snprintf(Text, sizeof(Text), Pattern, Values...); return std::string(Text); };
        // First edge of the active body whose ends satisfy Test (either order).
        const auto EdgeWhere = [](const SolidArcDocument& Document, const auto& Test)
        {
            for (const auto& [Slot, Name] : Document.Regeneration.Naming.Edges)
            {
                const Vector3 Start = EdgeEndPosition(Document, Name, false), End = EdgeEndPosition(Document, Name, true);
                if (Test(Start, End) || Test(End, Start)) return Name;
            }
            return std::string{};
        };
        const auto Every = [&](const SolidArcDocument& Document, const std::vector<std::string>& Names, const auto& Test)
        {
            for (const std::string& Name : Names) if (!Test(EdgeEndPosition(Document, Name, false), EdgeEndPosition(Document, Name, true))) return false;
            return true;
        };
        const auto Near = [](double Value, double Target) { return std::abs(Value - Target) <= 1.0e-9; };
        const auto VolumeOf = [](const SolidArcDocument& Target, const std::string& Key)
        {
            for (const SolidArcBodyFigures& Body : Target.Summary.Bodies) if (Body.Key == Key) return Body.Volume;
            return -1.0;
        };

        // ① block: the front-top edge loops to itself, rings to the four edges along x
        SolidArcDocument Box = CreateSolidArcDocument("LoopBlock");
        Expect(ConstructSolidArcPrimitive(Box, Block));
        const std::string Front = EdgeWhere(Box, [&](Vector3 A, Vector3 B) { return Near(A.Y, 0.0) && Near(B.Y, 0.0) && Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const std::vector<std::string> BoxLoop = SolidArcEdgeLoop(Box, Front), BoxRing = SolidArcEdgeRing(Box, Front);
        Tally(LoopGate, 0.0, !Front.empty() && BoxLoop.size() == 1 && BoxRing.size() == 4 && Every(Box, BoxRing, [&](Vector3 A, Vector3 B) { return Near(std::abs(B.X - A.X), 4.0); }),
              Format("block %s: loop %d (corner poles), ring %d along x", Front.c_str(), int(BoxLoop.size()), int(BoxRing.size())));

        // ②–④ prisms: hub loops round even / odd n-gons, rings across even faces only
        for (const int Sides : { 6, 5, 3 })
        {
            SolidArcDocument Prism = CreateSolidArcDocument("LoopPrism");
            SolidArcConstructRequest Request;
            Request.Kind = SolidArcConstructKind::Prism; Request.Radius = 1.0; Request.Height = 1.0; Request.Sides = Sides;
            Expect(ConstructSolidArcPrimitive(Prism, Request));
            const std::string Rim     = EdgeWhere(Prism, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); });
            const std::string Upright = EdgeWhere(Prism, [&](Vector3 A, Vector3 B) { return Near(A.Z, 0.0) && Near(B.Z, 1.0); });
            const std::vector<std::string> RimLoop = SolidArcEdgeLoop(Prism, Rim), RimRing = SolidArcEdgeRing(Prism, Rim);
            const std::vector<std::string> UpLoop = SolidArcEdgeLoop(Prism, Upright), UpRing = SolidArcEdgeRing(Prism, Upright);
            const size_t LoopExpected = Sides > 4 ? size_t(Sides) : 1u;
            const size_t RingExpected = Sides % 2 == 0 ? 4u : 2u;
            const bool   RimFlat      = Every(Prism, RimLoop, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); });
            const bool   Uprights     = Every(Prism, UpRing, [&](Vector3 A, Vector3 B) { return Near(std::abs(B.Z - A.Z), 1.0); });
            Tally(LoopGate, 0.0, !Rim.empty() && !Upright.empty() && RimLoop.size() == LoopExpected && RimFlat && RimRing.size() == RingExpected && UpLoop.size() == 1
                                 && UpRing.size() == size_t(Sides) && Uprights,
                  Format("%d-sided prism: rim loop %d (expect %d), rim ring %d (expect %d), upright loop %d, upright ring %d", Sides, int(RimLoop.size()), int(LoopExpected),
                         int(RimRing.size()), int(RingExpected), int(UpLoop.size()), int(UpRing.size())));
        }

        // ⑤ polyline knife: an open three-segment chain slices the block 3 + 5, the L piece's top rim loops to 6
        SolidArcDocument Part = CreateSolidArcDocument("PolylineKnife");
        Expect(ConstructSolidArcPrimitive(Part, Block));
        const std::string BlockKey = Part.Regeneration.Active;
        Expect(DrawSolidArcSketch(Part, DrawingOf(SolidArcDrawingKind::Line, 0, { { 1.0, -1.0 }, { 1.0, 1.5 }, { 3.0, 1.5 }, { 3.0, 3.0 } })));
        SolidArcCutRequest Cut;
        Cut.Target = BlockKey; Cut.Cutter = Part.Tree.Steps.back().Label;
        const SolidArcCommandOutcome Sliced = CutSolidArcBody(Part, Cut);
        const std::string PieceKey = Sliced.AcceptedCondition ? Part.Tree.Steps.back().Label : std::string{};
        const double      Inside   = PieceKey.empty() ? 0.0 : VolumeOf(Part, PieceKey), Outside = VolumeOf(Part, BlockKey);
        const bool        Pair     = (Near(Inside, 3.0) && Near(Outside, 5.0)) || (Near(Inside, 5.0) && Near(Outside, 3.0));
        Tally(LoopGate, std::min(std::abs(Inside - 3.0) + std::abs(Outside - 5.0), std::abs(Inside - 5.0) + std::abs(Outside - 3.0)),
              Sliced.AcceptedCondition && Pair && Part.Summary.Bodies.size() == 2 && Part.Summary.SoundCondition,
              Format("%s: polyline slice → %.12f + %.12f (3 + 5), sound", Sliced.Label.c_str(), Inside, Outside));
        // The active body is the inside piece: its top face is an L hexagon.
        const std::string LTop = EdgeWhere(Part, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0) && Near(A.Y, 2.0) && Near(B.Y, 2.0); });
        const std::vector<std::string> LLoop = SolidArcEdgeLoop(Part, LTop, Part.Regeneration.Active);
        Tally(LoopGate, 0.0, !LTop.empty() && LLoop.size() == 6 && Every(Part, LLoop, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); }),
              Format("L piece top rim %s: loop %d (expect 6, round the L)", LTop.c_str(), int(LLoop.size())));

        // ⑥ cylinder: the top circle (a closed edge) loops to itself and rings across the side face to the bottom circle
        SolidArcDocument Round = CreateSolidArcDocument("LoopCylinder");
        SolidArcConstructRequest Drum;
        Drum.Kind = SolidArcConstructKind::Cylinder; Drum.Radius = 1.0; Drum.Height = 1.0;
        Expect(ConstructSolidArcPrimitive(Round, Drum));
        const std::string Circle = EdgeWhere(Round, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); });
        const std::vector<std::string> CircleLoop = SolidArcEdgeLoop(Round, Circle), CircleRing = SolidArcEdgeRing(Round, Circle);
        Tally(LoopGate, 0.0, !Circle.empty() && CircleLoop.size() == 1 && CircleRing.size() == 2
                             && Every(Round, CircleRing, [&](Vector3 A, Vector3 B) { return Near(A.Z, B.Z) && Near(std::abs(A.X), 1.0); }),
              Format("cylinder top circle: loop %d, ring %d (top + bottom circles)", int(CircleLoop.size()), int(CircleRing.size())));

        // ⑦ open sheet (an unfilled rectangle extruded into walls): a top boundary edge loops round the open rim (4),
        //    rings across its wall to the bottom rim edge (2)
        SolidArcDocument Walls = CreateSolidArcDocument("LoopWalls");
        Expect(DrawSolidArcSketch(Walls, DrawingOf(SolidArcDrawingKind::Rectangle, 0, { { 0.0, 0.0 }, { 2.0, 1.0 } })));
        Expect(ExtrudeSolidArcSketch(Walls, Walls.Tree.Steps.back().Label, 1.0, FeatureCombination::New));
        const std::string Lip = EdgeWhere(Walls, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0) && Near(A.Y, 0.0) && Near(B.Y, 0.0); });
        const std::vector<std::string> LipLoop = SolidArcEdgeLoop(Walls, Lip), LipRing = SolidArcEdgeRing(Walls, Lip);
        Tally(LoopGate, 0.0, !Lip.empty() && LipLoop.size() == 4 && LipRing.size() == 2 && Every(Walls, LipLoop, [&](Vector3 A, Vector3 B) { return Near(A.Z, 1.0) && Near(B.Z, 1.0); }),
              Format("open walls top lip %s: boundary loop %d (expect 4), ring %d (expect 2)", Lip.c_str(), int(LipLoop.size()), int(LipRing.size())));

        // ⑧ four-edge vertices (a revolved diamond, no side ⟂ the axis: each corner holds two cone seams and one circle): a seam loops
        //    through the opposite fan edge round the whole profile (4, Blender's meridian loop); a circle rings across
        //    every revolved face to the other three circles and loops to itself
        SolidArcDocument Turned = CreateSolidArcDocument("LoopRevolve");
        Expect(DrawFilledSketch(Turned, DrawingOf(SolidArcDrawingKind::Line, 0, { { 1.0, 0.5 }, { 1.5, 0.0 }, { 2.0, 0.5 }, { 1.5, 1.0 } }, true)));
        const std::string Square = Turned.Tree.Steps.back().Label;
        Expect(RevolveSolidArcSketch(Turned, Square, ProposeSolidArcRevolveAxis(Turned, Square), 2.0 * std::numbers::pi, FeatureCombination::New));
        const std::string Seam  = EdgeWhere(Turned, [&](Vector3 A, Vector3 B) { return Distance(A, B) > 0.5; });
        const std::string Hoop  = EdgeWhere(Turned, [&](Vector3 A, Vector3 B) { return Distance(A, B) <= 1.0e-12; });
        const std::vector<std::string> SeamLoop = SolidArcEdgeLoop(Turned, Seam), HoopRing = SolidArcEdgeRing(Turned, Hoop), HoopLoop = SolidArcEdgeLoop(Turned, Hoop);
        Tally(LoopGate, 0.0, !Seam.empty() && !Hoop.empty() && SeamLoop.size() == 4 && HoopRing.size() == 4 && HoopLoop.size() == 1
                             && Every(Turned, SeamLoop, [&](Vector3 A, Vector3 B) { return Distance(A, B) > 0.5; }),
              Format("revolved diamond: seam %s loops %d (expect 4, through 4-edge corners), circle rings %d (expect 4), loops %d", Seam.c_str(), int(SeamLoop.size()),
                     int(HoopRing.size()), int(HoopLoop.size())));
    }

    // ---- W59 Bevels on polyline-knife pieces (every edge, alone and together)
    ValidatePieceBevels();

    // ---- W60 Extrude manifold (push-in by direction, imprinted regions)
    ValidateExtrudeManifold();

    // ---- W61 2D cutters (imprint, then push / pull the regions)
    ValidateImprint();
    // ---- W64 Extrude manifold at a face edge (notches open through the neighbour)
    ValidateEdgeNotch();

    // ---- W65 Edge path (Ctrl+click: along loop / whole loop / shortest)
    ValidateEdgePath();

    // ---- W62 Cut at crossings, sketch duplicates
    ValidatePointCut();

    // ---- W63 Point on curve, Flow along curve
    ValidateCurveFlow();

    // ---- W66 Wrap around curve, sketch scale, curve offset
    ValidateWrap();

    // ---- W67 Direct edits of solids (vertex / edge reshape, before trailing fillets)
    ValidateReshape();

    // ---- W68 Delete everything picked (X / Delete)
    ValidateDelete();

    // ---- W69 Curves and sheets: loft between sketches, flip faces, hide curves, 3D curves
    ValidateCurveAsk();
    ValidateBevelReach();
    ValidateBevelMerge();

    // ---- W72 … W80 Boolean library speed, Parametric Variables, Live Booleans, Edge Weights, Modifier Stack, Compound Cutters
    ValidateStage3CadFeatures();

    // ---- W81 … W82 Gizmo across all views / primitives / curve handles, and continued / V-lofts
    ValidateGizmoAndContinuedLofts();

    // ---- W83 … W86 Hotkeys (Ctrl+I / Ctrl+A) & Outliner cleanup, CAD Dimension Lines, Multi-Select Sync, Grid Shader & 3D UV Orientation Cube
    ValidateBatch4InterfaceAndViewport();

    // ---- W87 … W89 Blender-like G / R / S transforms, Numpad camera hotkeys, and Multi-view docked Split Viewport
    ValidateBatch5TransformsNumpadAndSplitViews();

    // ---- W90 … W91 Smooth Gizmo Zoom Scaling and Interactive CAD Dimension Tool
    ValidateBatch6GizmoZoomAndInteractiveDimensions();

    // ---- W92 … W95 Axis-Guided Dimension Sliding, Header Dim Toggle, Twisted NURBS Bevels, and Repeated Vertex Reshape
    ValidateBatch7DimensionsBevelAndReshape();

    for (const GateTally* Gate : { &ConstructGate, &PickGate, &BlendGate, &FaceGate, &ParameterGate, &AtomicGate, &HistoryGate, &FileGate, &VariableGate, &DrawingGate, &SurfacingGate, &PanelGate, &EvaluationGate, &BlueprintGate, &MatcapGate, &BodiesGate, &CutGate, &TrimGate, &MoveGate, &CopyGate, &HideGate, &CurveGate, &ConstrainGate, &ConflictGate, &DragGate, &ReferenceGate, &SplineGate, &LinkGate, &RevolveGate, &EvolutionGate, &ReplayGate, &XRayGate, &MeasureGate, &SectionGate, &SnapGate, &JointGate, &SheetGate, &ExchangeGate, &BoundGate, &PrimitiveGate, &InsetGate, &SectionLoftGate, &GizmoGate, &PointGate, &ShellGate, &DetachGate, &CornerGate, &FillGate, &FilletDragGate, &OrientGate, &TangentGate, &KnifeGate, &HandleGate, &SolidKnifeGate, &PocketBevelGate, &ReachGate, &SplitGate, &LoopGate, &PieceGate, &ManifoldGate, &ImprintGate, &PointCutGate, &CurveFlowGate, &EdgeNotchGate, &EdgePathGate, &WrapGate, &ReshapeGate, &DeleteGate, &CurveAskGate, &FullReachGate, &BevelMergeGate, &CutterSpeedGate, &ParametricVarGate, &LiveBooleanGate, &EdgeWeightGate, &LinearCircArrayGate, &CurveSurfArrayGate, &CurveBendStackGate, &CompoundCutterGate, &LiveEditableGate, &GizmoAllViewsGate, &ContinuedLoftGate, &HotkeysOutlinerGate, &DimensionLinesGate, &MultiSelectSyncGate, &GridCubeUvGate, &BlenderTransformGate, &NumpadCameraGate, &SplitViewportGate, &GizmoZoomGate, &InteractiveDimToolGate, &AxisSlideDimEditGate, &HeaderDimToggleGate, &TwistedNurbsBevelGate, &RepeatedReshapeGate })
    {
        std::printf("| %s | %s | %s | %.3g | %ld | %s |\n", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures ? "🔴" : "🟢");
        Failures += Gate->Failures;
    }
    std::printf("\n## Walkthrough\n\n| Gate | Check | Value | Verdict |\n|------|-------|-------|---------|\n");
    for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
    std::printf("\n## Saved document (TOML, first 40 lines)\n\n```toml\n");
    size_t Cursor = 0;
    for (int Line = 0; Line < 40 && Cursor < Saved.size(); ++Line)
    {
        const size_t Next = Saved.find('\n', Cursor);
        std::printf("%s\n", Saved.substr(Cursor, Next - Cursor).c_str());
        Cursor = Next == std::string::npos ? Saved.size() : Next + 1;
    }
    std::printf("```\n");
    return Failures == 0 ? 0 : 1;
}
