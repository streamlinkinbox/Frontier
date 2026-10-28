/*====================================================================================================================================
                                                     FLUIDEDITORENTRY.JS
====================================================================================================================================*/
// 🧩 Editor assembly — plane pan/zoom, entry dragging, port linking, chrome wiring, solver feed
//
// 📝 Ported from NodeFlowEditor/EditorEntry.js. The graph half is unchanged: pan, zoom, glide, banding,
//    entry drag, port linking, the catalogue menu, the entry toolbar and the split handle are the same
//    code driving the same store. What differs is the far end of the pipe — where the terrain editor
//    resolved five noise parameters off one chosen generator, this walks the graph from the surface
//    output backwards and hands the solver a whole specification.

import { ComposeGlyph } from './Presentation/GlyphOutlines.js';
import
{
    LiquidTokens, SourceTokens, NodeCatalogue, CompactEntryCondition
} from './Graph/CatalogueSpecifications.js';
import { RasterizeEntry, EscapeMarkup } from './Presentation/EntryRasterization.js';
import { FormulateLinkPath } from './Graph/LinkRouting.js';
import
{
    RevealCatalogueMenu, ConcealCatalogueMenu, CatalogueMenuRevealedCondition
} from './Presentation/CatalogueMenu.js';
import
{
    InitializeFluidViewport, ReconfigureFluid, ReconfigureShading,
    ReconfigureCameraMode, ReconfigureResolution, ReconfigureEnvironment,
    ReconfigureInspection, ReconfigureOverlay, RegulateTransport, ResolveSolverTally
} from './Fluid/FluidViewport.js';
import
{
    RecordStore, RetrieveEntry, RetrieveChosenEntry, RetrievePort,
    ConstructCatalogueEntry,
    AttachInboundPort, AttachOutboundPort, DetachInboundPort, DetachOutboundPort,
    ReclaimEntry, IntegrateLink, ReclaimLink, AdmissibleLinkCondition, ProvokeRebuild
} from './Graph/GraphStore.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const MinimumPlaneScale = 0.2;    // [-]  - Furthest zoom-out
const MaximumPlaneScale = 2.5;    // [-]  - Closest zoom-in
const ZoomIncrement     = 1.12;   // [-]  - Multiplier per wheel notch
const DragThreshold     = 3;      // [px] - Pointer travel before a press becomes a drag
const GlideResponse     = 0.22;   // [-]  - Fraction of remaining distance closed per frame
const GlideEpsilon      = 0.0005; // [-]  - Residual below which the glide is considered settled

//------------------------------------------------------------------------------------------------------------------------
//                                                    ELEMENT RETRIEVAL
//------------------------------------------------------------------------------------------------------------------------

const GraphRegion    = document.getElementById('GraphRegion');
const PanSurface     = document.getElementById('PanSurface');
const TransformPlane = document.getElementById('TransformPlane');
const EdgeCanvas     = document.getElementById('EdgeCanvas');
const EntryPlane     = document.getElementById('EntryPlane');
const SelectionBand  = document.getElementById('SelectionBand');
const ViewportRegion = document.getElementById('ViewportRegion');
const ProgressAnchor = document.getElementById('ProgressAnchor');
const ControlAnchor  = document.getElementById('ControlAnchor');

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

let DraftLinkPath  = null;   // In-flight link being dragged from a port
let LinkInFlight   = null;   // { Entry, Port, Direction, Classification }
let EntryDragState = null;
let PlanePanState  = null;
let BandState      = null;
let GlideTarget    = null;   // { Scale, OffsetX, OffsetY } the plane is easing toward
let GlideFrame     = null;   // rAF token for the glide in flight

//------------------------------------------------------------------------------------------------------------------------
//                                                  COORDINATE TRANSFORM
//------------------------------------------------------------------------------------------------------------------------

// Project a client-space point into graph-plane space.
function ProjectToPlane(ClientX, ClientY)
{
    const SurfaceBounds = PanSurface.getBoundingClientRect();
    return {
        PlaneX: (ClientX - SurfaceBounds.left - RecordStore.PlaneOffsetX) / RecordStore.PlaneScale,
        PlaneY: (ClientY - SurfaceBounds.top  - RecordStore.PlaneOffsetY) / RecordStore.PlaneScale
    };
}

function AlignPlaneTransform()
{
    TransformPlane.style.transform =
        `translate(${RecordStore.PlaneOffsetX}px, ${RecordStore.PlaneOffsetY}px) scale(${RecordStore.PlaneScale})`;

    // 📝 The dot grid lives on the un-transformed surface, so its spacing and phase are tracked by hand
    //    to stay locked to plane space through pan and zoom.
    const GridSpacing = 32 * RecordStore.PlaneScale;
    PanSurface.style.backgroundSize = `${GridSpacing}px ${GridSpacing}px`;
    PanSurface.style.backgroundPosition =
        `${RecordStore.PlaneOffsetX}px ${RecordStore.PlaneOffsetY}px`;
}

// Resolve a port's centre in plane space from its rendered dot.
function ResolvePortAnchor(EntryToken, PortToken)
{
    const PortDot = EntryPlane.querySelector(
        `[data-entry="${EntryToken}"][data-port="${PortToken}"]`);
    if (!PortDot) return null;

    const DotBounds     = PortDot.getBoundingClientRect();
    const SurfaceBounds = PanSurface.getBoundingClientRect();

    return {
        PlaneX: (DotBounds.left + DotBounds.width  / 2 - SurfaceBounds.left - RecordStore.PlaneOffsetX) / RecordStore.PlaneScale,
        PlaneY: (DotBounds.top  + DotBounds.height / 2 - SurfaceBounds.top  - RecordStore.PlaneOffsetY) / RecordStore.PlaneScale
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LINK INSCRIPTION
//------------------------------------------------------------------------------------------------------------------------

function InscribeLinks()
{
    while (EdgeCanvas.firstChild) EdgeCanvas.removeChild(EdgeCanvas.firstChild);

    for (const Link of RecordStore.Links)
    {
        const Origin  = ResolvePortAnchor(Link.SourceEntry, Link.SourcePort);
        const Arrival = ResolvePortAnchor(Link.TargetEntry, Link.TargetPort);
        if (!Origin || !Arrival) continue;

        const PathData = FormulateLinkPath(Origin.PlaneX, Origin.PlaneY, Arrival.PlaneX, Arrival.PlaneY);

        // 📝 A transparent wide halo under each curve gives the 2 px stroke a clickable target.
        const Halo = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        Halo.setAttribute('class', 'EdgeHalo');
        Halo.setAttribute('d', PathData);
        Halo.dataset.link = Link.Token;
        EdgeCanvas.appendChild(Halo);

        const Curve = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        Curve.setAttribute('class', 'EdgeCurve');
        Curve.setAttribute('d', PathData);
        Curve.dataset.link = Link.Token;
        EdgeCanvas.appendChild(Curve);
    }

    if (DraftLinkPath) EdgeCanvas.appendChild(DraftLinkPath);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY REFRESH
//------------------------------------------------------------------------------------------------------------------------

function RefreshEntries()
{
    while (EntryPlane.firstChild) EntryPlane.removeChild(EntryPlane.firstChild);
    for (const Entry of RecordStore.Entries) EntryPlane.appendChild(RasterizeEntry(Entry));
    InscribeLinks();
}

// Full refresh of every surface that reads the record store.
function RefreshEditor()
{
    RefreshEntries();
    RefreshProgressPill();
    FeedFluidViewport();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENTRY CHOICE
//------------------------------------------------------------------------------------------------------------------------

function ChooseEntry(EntryToken, AdditiveCondition)
{
    for (const Entry of RecordStore.Entries)
    {
        if (Entry.Token === EntryToken)          Entry.Chosen = true;
        else if (!AdditiveCondition)             Entry.Chosen = false;
    }
    RefreshEditor();
}

function DiscardChoice()
{
    for (const Entry of RecordStore.Entries) Entry.Chosen = false;
    RefreshEditor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOLVER FEED
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve one dial BY ITS PORT LABEL: an incoming link from a scalar regulator wins over a typed-in
//    port scalar, which in turn wins over the default. Label-keyed rather than index-keyed so inserting
//    a port in the catalogue cannot silently re-point every dial after it.
//
// 🔴 The label must match CatalogueSpecifications.InboundLabelOverrides exactly. A mismatch is SILENT:
//    the port renders, the graph links, and the solver quietly keeps its default.
function ResolveDial(Entry, PortLabel, DefaultMagnitude)
{
    if (!Entry) return DefaultMagnitude;

    const Port = Entry.InboundPorts.find((Candidate) => Candidate.Label === PortLabel);
    if (!Port) return DefaultMagnitude;

    let RawMagnitude = Entry.PortScalars[Port.Token];

    const ArrivingLink = RecordStore.Links.find(
        (Link) => Link.TargetEntry === Entry.Token && Link.TargetPort === Port.Token);
    if (ArrivingLink)
    {
        const SourceEntry = RetrieveEntry(ArrivingLink.SourceEntry);
        if (SourceEntry && SourceEntry.CatalogueToken && SourceEntry.CatalogueToken.startsWith('input_'))
        {
            RawMagnitude = SourceEntry.PortScalars['val'];
        }
    }

    if (RawMagnitude === undefined || RawMagnitude === '') return DefaultMagnitude;
    const Parsed = parseFloat(RawMagnitude);
    return Number.isFinite(Parsed) ? Parsed : DefaultMagnitude;
}

// Resolve a three-axis dial, which a vec3 regulator writes as separate x/y/z scalars.
function ResolveAxisDial(Entry, PortLabel, DefaultTriple)
{
    if (!Entry) return DefaultTriple.slice();

    const Port = Entry.InboundPorts.find((Candidate) => Candidate.Label === PortLabel);
    if (!Port) return DefaultTriple.slice();

    const ArrivingLink = RecordStore.Links.find(
        (Link) => Link.TargetEntry === Entry.Token && Link.TargetPort === Port.Token);
    if (!ArrivingLink) return DefaultTriple.slice();

    const SourceEntry = RetrieveEntry(ArrivingLink.SourceEntry);
    if (!SourceEntry) return DefaultTriple.slice();

    const ReadAxis = (AxisKey, Fallback) =>
    {
        const Raw = SourceEntry.PortScalars[AxisKey];
        if (Raw === undefined || Raw === '') return Fallback;
        const Parsed = parseFloat(Raw);
        return Number.isFinite(Parsed) ? Parsed : Fallback;
    };

    return [
        ReadAxis('x', DefaultTriple[0]),
        ReadAxis('y', DefaultTriple[1]),
        ReadAxis('z', DefaultTriple[2])
    ];
}

// 📝 Follow the link arriving at one named port and return the entry on the far side. This is the single
//    primitive the graph walk is built from — the fluid graph is a CHAIN, so the specification cannot be
//    read off one node the way a noise generator's could.
function ResolveUpstream(Entry, PortLabel)
{
    if (!Entry) return null;
    const Port = Entry.InboundPorts.find((Candidate) => Candidate.Label === PortLabel);
    if (!Port) return null;

    const ArrivingLink = RecordStore.Links.find(
        (Link) => Link.TargetEntry === Entry.Token && Link.TargetPort === Port.Token);
    if (!ArrivingLink) return null;
    return RetrieveEntry(ArrivingLink.SourceEntry);
}

// 📝 Choosing a scalar regulator that feeds a solver stage previews the SIM, not the bare number —
//    otherwise dragging a slider would blank the viewport. Walking downstream one hop is enough,
//    because a regulator only ever feeds a stage directly.
function ResolvePreviewEntry(ChosenEntry)
{
    if (!ChosenEntry) return null;
    if (LiquidTokens.includes(ChosenEntry.CatalogueToken)) return ChosenEntry;

    if (ChosenEntry.CatalogueToken &&
       (ChosenEntry.CatalogueToken.startsWith('input_') || ChosenEntry.CatalogueToken.startsWith('math_')))
    {
        const DepartingLink = RecordStore.Links.find((Link) => Link.SourceEntry === ChosenEntry.Token);
        if (DepartingLink)
        {
            const Downstream = RetrieveEntry(DepartingLink.TargetEntry);
            if (Downstream && LiquidTokens.includes(Downstream.CatalogueToken)) return Downstream;
        }
    }
    return ChosenEntry;
}

// 📝 Walk the `Volume` chain from any stage back toward its source, collecting the stages in execution
//    order. A cycle would otherwise spin here forever, so visited tokens are tracked and the walk stops
//    the first time it meets one — a self-referential graph is a user error, not a reason to hang.
function TraceVolumeChain(TerminalEntry)
{
    const Chain = [];
    const Visited = new Set();

    let Cursor = TerminalEntry;
    while (Cursor && !Visited.has(Cursor.Token))
    {
        Visited.add(Cursor.Token);
        Chain.unshift(Cursor);

        // A surface stage reaches back through `Surface`; every sim stage through `Volume`.
        const Upstream = ResolveUpstream(Cursor, 'Volume') || ResolveUpstream(Cursor, 'Surface');
        Cursor = Upstream;
    }
    return Chain;
}

// Collect every region feeding a collider-apply stage, resolved to concrete shapes.
function ComposeColliderSpecification(ApplyEntry)
{
    const RegionEntry = ResolveUpstream(ApplyEntry, 'Region');
    if (!RegionEntry) return null;

    const Friction = ResolveDial(ApplyEntry, 'Friction', 0.0);

    switch (RegionEntry.CatalogueToken)
    {
        case 'coll_box':
            return {
                Shape: 'box', Friction,
                Origin: ResolveAxisDial(RegionEntry, 'Origin', [0.35, 0.0, 0.35]),
                Extent: ResolveAxisDial(RegionEntry, 'Extent', [0.30, 0.30, 0.30])
            };
        case 'coll_sphere':
            return {
                Shape: 'sphere', Friction,
                Centre: ResolveAxisDial(RegionEntry, 'Centre', [0.5, 0.35, 0.5]),
                Radius: ResolveDial(RegionEntry, 'Radius', 0.15)
            };
        case 'coll_terrain':
            return {
                Shape: 'terrain', Friction,
                Height:    ResolveDial(RegionEntry, 'Height', 0.12),
                Roughness: ResolveDial(RegionEntry, 'Roughness', 0.5),
                FieldSeed: ResolveDial(RegionEntry, 'Seed', 0)
            };
        case 'coll_paddle':
            return {
                Shape: 'paddle', Friction,
                Centre: ResolveAxisDial(RegionEntry, 'Centre', [0.5, 0.3, 0.5]),
                Extent: ResolveAxisDial(RegionEntry, 'Extent', [0.06, 0.35, 0.30]),
                Rate:   ResolveDial(RegionEntry, 'Rate', 1.0)
            };
        default:
            return null;
    }
}

// Resolve one force node into an acceleration specification.
function ComposeForceSpecification(ApplyEntry)
{
    const ForceEntry = ResolveUpstream(ApplyEntry, 'Force');
    if (!ForceEntry) return null;

    const Scale = ResolveDial(ApplyEntry, 'Scale', 1.0);

    switch (ForceEntry.CatalogueToken)
    {
        case 'force_gravity':
            return {
                Kind: 'gravity', Scale,
                Acceleration: ResolveAxisDial(ForceEntry, 'Acceleration', [0.0, -9.81, 0.0])
            };
        case 'force_wind':
            return {
                Kind: 'wind', Scale,
                Direction: ResolveAxisDial(ForceEntry, 'Direction', [1.0, 0.0, 0.0]),
                Strength:  ResolveDial(ForceEntry, 'Strength', 2.0)
            };
        case 'force_vortex':
            return {
                Kind: 'vortex', Scale,
                Centre:   ResolveAxisDial(ForceEntry, 'Centre', [0.5, 0.5, 0.5]),
                Axis:     ResolveAxisDial(ForceEntry, 'Axis', [0.0, 1.0, 0.0]),
                Strength: ResolveDial(ForceEntry, 'Strength', 3.0)
            };
        case 'force_turbulence':
            return {
                Kind: 'turbulence', Scale,
                Strength:  ResolveDial(ForceEntry, 'Strength', 1.0),
                FieldScale:ResolveDial(ForceEntry, 'Scale', 2.0),
                FieldSeed: ResolveDial(ForceEntry, 'Seed', 0)
            };
        default:
            return null;
    }
}

// Resolve the emitter that seeds the domain into a source specification.
function ComposeSourceSpecification(SourceEntry)
{
    if (!SourceEntry) return null;

    switch (SourceEntry.CatalogueToken)
    {
        case 'emit_dambreak':
            return {
                Kind: 'dambreak',
                Origin: ResolveAxisDial(SourceEntry, 'Origin', [0.0, 0.0, 0.0]),
                Extent: ResolveAxisDial(SourceEntry, 'Extent', [0.35, 0.65, 1.0]),
                Fill:   ResolveDial(SourceEntry, 'Fill', 1.0)
            };
        case 'emit_pool':
            return { Kind: 'pool', Level: ResolveDial(SourceEntry, 'Level', 0.35) };
        case 'emit_drop':
            return {
                Kind: 'drop',
                Centre: ResolveAxisDial(SourceEntry, 'Centre', [0.5, 0.7, 0.5]),
                Radius: ResolveDial(SourceEntry, 'Radius', 0.12),
                Speed:  ResolveDial(SourceEntry, 'Speed', 0.0)
            };
        case 'emit_inflow':
            return {
                Kind: 'inflow',
                Origin:    ResolveAxisDial(SourceEntry, 'Origin', [0.08, 0.75, 0.5]),
                Direction: ResolveAxisDial(SourceEntry, 'Direction', [1.0, -0.25, 0.0]),
                Speed:     ResolveDial(SourceEntry, 'Speed', 4.0),
                Radius:    ResolveDial(SourceEntry, 'Radius', 0.07)
            };
        case 'emit_volume':
            return { Kind: 'volume', Fill: ResolveDial(SourceEntry, 'Fill', 1.0) };
        default:
            return null;
    }
}

// 📝 The whole specification handed to the solver, assembled by walking the chain the user actually
//    wired. Stages absent from the graph fall back to physical defaults rather than to nothing, so a
//    two-node graph (emitter into output) still simulates.
function ComposeSolverSpecification(PreviewEntry)
{
    const Chain = TraceVolumeChain(PreviewEntry);

    const Specification = {
        SourceSpecification: null,
        Colliders: [],
        Forces:    [],

        // Solver defaults, overridden by any stage present in the chain.
        PicFlipBlend:       0.95,
        Substeps:           2,
        PressureIterations: 40,
        PressureTolerance:  1e-4,
        Viscosity:          0.0,
        DomainExtent:       4.0,
        TimeStep:           1 / 60,
        CflCeiling:         1.0,

        SurfaceRadius:      1.0,
        SurfaceSmoothing:   0.5,
        Absorption:         [0.35, 0.04, 0.02],
        AbsorptionDepth:    1.2,
        SurfaceRoughness:   0.06,
        FoamThreshold:      0.0,
        FoamLifetime:       1.5,

        StageNaming: []
    };

    for (const Stage of Chain)
    {
        Specification.StageNaming.push(Stage.Title);

        switch (Stage.CatalogueToken)
        {
            case 'emit_dambreak': case 'emit_pool': case 'emit_drop':
            case 'emit_inflow':   case 'emit_volume':
                Specification.SourceSpecification = ComposeSourceSpecification(Stage);
                break;

            case 'coll_apply':
            {
                const Collider = ComposeColliderSpecification(Stage);
                if (Collider) Specification.Colliders.push(Collider);
                break;
            }

            case 'force_apply':
            {
                const Force = ComposeForceSpecification(Stage);
                if (Force) Specification.Forces.push(Force);
                break;
            }

            case 'solve_flip':
                Specification.PicFlipBlend = ResolveDial(Stage, 'Blend', 0.95);
                Specification.Substeps     = Math.max(1, Math.round(ResolveDial(Stage, 'Substeps', 2)));
                break;

            case 'solve_pressure':
                Specification.PressureIterations =
                    Math.max(1, Math.round(ResolveDial(Stage, 'Iterations', 40)));
                Specification.PressureTolerance = ResolveDial(Stage, 'Tolerance', 1e-4);
                break;

            case 'solve_viscosity':
                Specification.Viscosity = ResolveDial(Stage, 'Viscosity', 0.0);
                break;

            case 'solve_domain':
                Specification.DomainExtent = ResolveDial(Stage, 'Extent', 4.0);
                break;

            case 'solve_timestep':
                Specification.TimeStep   = ResolveDial(Stage, 'Step', 1 / 60);
                Specification.CflCeiling = ResolveDial(Stage, 'CFL', 1.0);
                break;

            case 'surf_reconstruct':
                Specification.SurfaceRadius    = ResolveDial(Stage, 'Radius', 1.0);
                Specification.SurfaceSmoothing = ResolveDial(Stage, 'Smoothing', 0.5);
                break;

            case 'surf_water':
                Specification.AbsorptionDepth  = ResolveDial(Stage, 'Depth', 1.2);
                Specification.SurfaceRoughness = ResolveDial(Stage, 'Roughness', 0.06);
                break;

            case 'surf_foam':
                Specification.FoamThreshold = ResolveDial(Stage, 'Threshold', 0.0);
                Specification.FoamLifetime  = ResolveDial(Stage, 'Lifetime', 1.5);
                break;
        }
    }

    // 📝 A chain with no emitter cannot be simulated. Reported rather than substituted: silently
    //    inserting a default dam break would make an unwired graph look wired.
    Specification.SimulableCondition = Specification.SourceSpecification !== null;

    // Gravity is not optional physics. Absent an explicit force stage the solver still needs it, or the
    // water hangs in the air and the graph looks broken rather than incomplete.
    if (!Specification.Forces.some((Force) => Force.Kind === 'gravity'))
    {
        Specification.Forces.unshift(
            { Kind: 'gravity', Scale: 1.0, Acceleration: [0.0, -9.81, 0.0], Implicit: true });
    }

    return Specification;
}

function FeedFluidViewport()
{
    if (RecordStore.ViewportSide === 'hidden') return;

    const PreviewEntry = ResolvePreviewEntry(RetrieveChosenEntry());

    if (!PreviewEntry || !LiquidTokens.includes(PreviewEntry.CatalogueToken))
    {
        DispatchedSpecification = { SimulableCondition: false, SourceSpecification: null, StageNaming: [] };
        ReconfigureFluid(DispatchedSpecification);
        RefreshSolverReadout();
        return;
    }

    // 📝 The readout reports the specification actually handed to the solver rather than re-deriving it,
    //    so a change to the walk above cannot leave the panel quietly describing a different sim.
    DispatchedSpecification = ComposeSolverSpecification(PreviewEntry);
    DispatchedSpecification.PreviewNaming = PreviewEntry.Title;

    ReconfigureFluid(DispatchedSpecification);
    RefreshSolverReadout();
}

// The specification last handed to the viewport, kept so a re-feed can reuse it verbatim.
let DispatchedSpecification = null;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROGRESS PILL
//------------------------------------------------------------------------------------------------------------------------

function RefreshProgressPill()
{
    const ChosenEntry = RetrieveChosenEntry();
    if (!ChosenEntry) { ProgressAnchor.innerHTML = ''; return; }

    const BuildCondition = ChosenEntry.BuildCondition || 'built';
    let ProgressTally = ChosenEntry.BuildProgress ?? 100;
    let FillClass = '';
    let ConditionNaming = 'Built';

    if (BuildCondition === 'building')   { FillClass = 'Building'; ConditionNaming = 'Building...'; ProgressTally = 45; }
    else if (BuildCondition === 'failed'){ FillClass = 'Failed';   ConditionNaming = 'Failed'; }
    else                                 { ProgressTally = 100; }

    ProgressAnchor.innerHTML =
        `<div class="ProgressPill">`
      +   `<div class="ProgressStack">`
      +     `<div class="ProgressRow">`
      +       `<span class="ProgressNaming">${ConditionNaming}</span>`
      +       `<span class="ProgressTally ${BuildCondition === 'failed' ? 'Failed' : ''}">${ProgressTally}%</span>`
      +     `</div>`
      +     `<div class="ProgressTrack">`
      +       `<div class="ProgressFill ${FillClass}" style="width:${ProgressTally}%"></div>`
      +     `</div>`
      +   `</div>`
      + `</div>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CANVAS CONTROLS
//------------------------------------------------------------------------------------------------------------------------

function RefreshCanvasControls()
{
    if (!RecordStore.ControlsRevealed) { ControlAnchor.innerHTML = ''; return; }

    const LockGlyph = RecordStore.CanvasImmobilized ? 'Fastened' : 'Unfastened';
    const LockHint  = RecordStore.CanvasImmobilized ? 'Unlock Canvas' : 'Lock Canvas';

    ControlAnchor.innerHTML =
        `<div class="ControlStack">`
      +   `<div class="SideHint HintLeading" data-hint="Zoom In">`
      +     `<button class="ControlAction" data-control="ZoomIn">${ComposeGlyph('Plus', 14)}</button></div>`
      +   `<div class="SideHint HintLeading" data-hint="Zoom Out">`
      +     `<button class="ControlAction" data-control="ZoomOut">${ComposeGlyph('Minus', 14)}</button></div>`
      +   `<div class="SideHint HintLeading" data-hint="Fit to Selected / All">`
      +     `<button class="ControlAction" data-control="Encompass">${ComposeGlyph('Encompass', 14)}</button></div>`
      +   `<div class="ControlRule"></div>`
      +   `<div class="SideHint HintLeading" data-hint="${LockHint}">`
      +     `<button class="ControlAction ${RecordStore.CanvasImmobilized ? 'Engaged' : ''}" `
      +     `data-control="Immobilize">${ComposeGlyph(LockGlyph, 14)}</button></div>`
      + `</div>`;
}

// 📝 Zoom settles toward a target rather than snapping. Offset and scale are eased on the same
//    curve, because easing the scale alone would let the pivot point slide out from under the
//    cursor mid-glide. Nothing animates while a pointer drag is live — a pan must track the hand
//    exactly, so pans write the transform directly and cancel any glide in flight.
function GlideToTransform(TargetScale, TargetOffsetX, TargetOffsetY)
{
    GlideTarget = { Scale: TargetScale, OffsetX: TargetOffsetX, OffsetY: TargetOffsetY };
    if (GlideFrame !== null) return;

    const AdvanceGlide = () =>
    {
        if (!GlideTarget) { GlideFrame = null; return; }

        const ScaleResidual   = GlideTarget.Scale   - RecordStore.PlaneScale;
        const OffsetXResidual = GlideTarget.OffsetX - RecordStore.PlaneOffsetX;
        const OffsetYResidual = GlideTarget.OffsetY - RecordStore.PlaneOffsetY;

        const SettledCondition = Math.abs(ScaleResidual) < GlideEpsilon
                              && Math.abs(OffsetXResidual) < 0.5
                              && Math.abs(OffsetYResidual) < 0.5;

        if (SettledCondition)
        {
            RecordStore.PlaneScale   = GlideTarget.Scale;
            RecordStore.PlaneOffsetX = GlideTarget.OffsetX;
            RecordStore.PlaneOffsetY = GlideTarget.OffsetY;
            GlideTarget = null;
            GlideFrame  = null;
            AlignPlaneTransform();
            InscribeLinks();
            return;
        }

        RecordStore.PlaneScale   += ScaleResidual   * GlideResponse;
        RecordStore.PlaneOffsetX += OffsetXResidual * GlideResponse;
        RecordStore.PlaneOffsetY += OffsetYResidual * GlideResponse;

        AlignPlaneTransform();
        InscribeLinks();

        GlideFrame = requestAnimationFrame(AdvanceGlide);
    };

    GlideFrame = requestAnimationFrame(AdvanceGlide);
}

// Abandon any glide in flight, leaving the plane wherever it currently sits.
function AbandonGlide()
{
    if (GlideFrame !== null) cancelAnimationFrame(GlideFrame);
    GlideFrame  = null;
    GlideTarget = null;
}

function ApplyZoomAboutPoint(TargetScale, PivotClientX, PivotClientY, GlideCondition)
{
    const ClampedScale = Math.max(MinimumPlaneScale, Math.min(MaximumPlaneScale, TargetScale));
    const SurfaceBounds = PanSurface.getBoundingClientRect();

    const PivotSurfaceX = PivotClientX - SurfaceBounds.left;
    const PivotSurfaceY = PivotClientY - SurfaceBounds.top;

    // 📝 Hold the plane point under the pivot fixed across the scale change. The pivot is resolved
    //    against the glide target when one is in flight, so a fast wheel spin compounds toward the
    //    intended scale instead of fighting the partially-eased current one.
    const ReferenceScale   = GlideTarget ? GlideTarget.Scale   : RecordStore.PlaneScale;
    const ReferenceOffsetX = GlideTarget ? GlideTarget.OffsetX : RecordStore.PlaneOffsetX;
    const ReferenceOffsetY = GlideTarget ? GlideTarget.OffsetY : RecordStore.PlaneOffsetY;

    const PlaneX = (PivotSurfaceX - ReferenceOffsetX) / ReferenceScale;
    const PlaneY = (PivotSurfaceY - ReferenceOffsetY) / ReferenceScale;

    const ArrivalOffsetX = PivotSurfaceX - PlaneX * ClampedScale;
    const ArrivalOffsetY = PivotSurfaceY - PlaneY * ClampedScale;

    if (GlideCondition === false)
    {
        AbandonGlide();
        RecordStore.PlaneScale   = ClampedScale;
        RecordStore.PlaneOffsetX = ArrivalOffsetX;
        RecordStore.PlaneOffsetY = ArrivalOffsetY;
        AlignPlaneTransform();
        return;
    }

    GlideToTransform(ClampedScale, ArrivalOffsetX, ArrivalOffsetY);
}

// Frame either the chosen entries or the whole graph.
function EncompassEntries()
{
    const Candidates = RecordStore.Entries.filter((Entry) => Entry.Chosen);
    const Framed = Candidates.length > 0 ? Candidates : RecordStore.Entries;
    if (Framed.length === 0) return;

    let MinimumX = Infinity, MinimumY = Infinity, MaximumX = -Infinity, MaximumY = -Infinity;
    for (const Entry of Framed)
    {
        const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Entry.Token}"]`);
        const EntryWidth  = Wrapper ? Wrapper.offsetWidth  : 220;
        const EntryHeight = Wrapper ? Wrapper.offsetHeight : 140;
        MinimumX = Math.min(MinimumX, Entry.PlaneX);
        MinimumY = Math.min(MinimumY, Entry.PlaneY);
        MaximumX = Math.max(MaximumX, Entry.PlaneX + EntryWidth);
        MaximumY = Math.max(MaximumY, Entry.PlaneY + EntryHeight);
    }

    const SurfaceBounds = PanSurface.getBoundingClientRect();
    const Padding = 80;
    const SpanX = Math.max(1, MaximumX - MinimumX);
    const SpanY = Math.max(1, MaximumY - MinimumY);

    const FittedScale = Math.max(MinimumPlaneScale, Math.min(MaximumPlaneScale, 0.7,
        Math.min((SurfaceBounds.width - Padding * 2) / SpanX, (SurfaceBounds.height - Padding * 2) / SpanY)));

    // Framing glides as well, so pressing F reads as the view travelling rather than teleporting.
    GlideToTransform(
        FittedScale,
        SurfaceBounds.width  / 2 - (MinimumX + SpanX / 2) * FittedScale,
        SurfaceBounds.height / 2 - (MinimumY + SpanY / 2) * FittedScale);
}

// Frame the graph with no animation — used at startup, where a glide from a cold transform
// would read as the editor lurching into place on load.
function EncompassEntriesAbruptly()
{
    EncompassEntries();

    // EncompassEntries queues a glide; adopt its target immediately and drop the animation.
    const Target = GlideTarget;
    AbandonGlide();
    if (Target)
    {
        RecordStore.PlaneScale   = Target.Scale;
        RecordStore.PlaneOffsetX = Target.OffsetX;
        RecordStore.PlaneOffsetY = Target.OffsetY;
    }

    AlignPlaneTransform();
    InscribeLinks();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY SPAWNING
//------------------------------------------------------------------------------------------------------------------------

// Spawn a catalogue item and, when a link was in flight, terminate that link on the new entry.
function SpawnCatalogueEntry(CatalogueItem, Category, ScreenX, ScreenY, PendingLink)
{
    const PlanePoint = ProjectToPlane(ScreenX, ScreenY);
    const SpawnedEntry = ConstructCatalogueEntry(CatalogueItem, Category, PlanePoint.PlaneX, PlanePoint.PlaneY);
    RecordStore.Entries.push(SpawnedEntry);

    // 📝 Ports must exist in the DOM before a link can be routed to them, so render first.
    RefreshEntries();

    if (PendingLink)
    {
        if (PendingLink.PortDirection === 'source' && SpawnedEntry.InboundPorts.length > 0)
        {
            IntegrateLink(PendingLink.Entry, PendingLink.Port,
                          SpawnedEntry.Token, SpawnedEntry.InboundPorts[0].Token);
        }
        else if (PendingLink.PortDirection === 'target' && SpawnedEntry.OutboundPorts.length > 0)
        {
            IntegrateLink(SpawnedEntry.Token, SpawnedEntry.OutboundPorts[0].Token,
                          PendingLink.Entry, PendingLink.Port);
        }
    }

    ChooseEntry(SpawnedEntry.Token, false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PORT LINK DRAGGING
//------------------------------------------------------------------------------------------------------------------------

function CommencePortDrag(PortDot, PointerPress)
{
    if (RecordStore.CanvasImmobilized) return;

    const EntryToken = PortDot.dataset.entry;
    const PortToken  = PortDot.dataset.port;
    const PortDirection = PortDot.dataset.porthit;
    const Port = RetrievePort(EntryToken, PortToken);

    LinkInFlight = {
        Entry: EntryToken,
        Port: PortToken,
        PortDirection: PortDirection,
        PortClassification: (Port && Port.Classification) || 'any'
    };

    DraftLinkPath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    DraftLinkPath.setAttribute('class', 'EdgeDraft');
    EdgeCanvas.appendChild(DraftLinkPath);

    AdvancePortDrag(PointerPress);
    PanSurface.setPointerCapture(PointerPress.pointerId);
}

function AdvancePortDrag(PointerMotion)
{
    if (!LinkInFlight || !DraftLinkPath) return;

    const Anchor = ResolvePortAnchor(LinkInFlight.Entry, LinkInFlight.Port);
    if (!Anchor) return;
    const Cursor = ProjectToPlane(PointerMotion.clientX, PointerMotion.clientY);

    // Draw in the true direction of flow so the elbow shape matches a settled link.
    const PathData = LinkInFlight.PortDirection === 'source'
        ? FormulateLinkPath(Anchor.PlaneX, Anchor.PlaneY, Cursor.PlaneX, Cursor.PlaneY)
        : FormulateLinkPath(Cursor.PlaneX, Cursor.PlaneY, Anchor.PlaneX, Anchor.PlaneY);
    DraftLinkPath.setAttribute('d', PathData);
}

function ConcludePortDrag(PointerRelease)
{
    if (!LinkInFlight) return;

    const Pending = LinkInFlight;
    LinkInFlight = null;
    if (DraftLinkPath) { DraftLinkPath.remove(); DraftLinkPath = null; }

    // 📝 The pointer is captured by the surface, so hit-test the release point explicitly.
    const ReleaseTarget = document.elementFromPoint(PointerRelease.clientX, PointerRelease.clientY);
    const ArrivalDot = ReleaseTarget && ReleaseTarget.closest('[data-porthit]');

    if (ArrivalDot)
    {
        const ArrivalDirection = ArrivalDot.dataset.porthit;
        if (ArrivalDirection !== Pending.PortDirection)
        {
            const SourceEntry = Pending.PortDirection === 'source' ? Pending.Entry : ArrivalDot.dataset.entry;
            const SourcePort  = Pending.PortDirection === 'source' ? Pending.Port  : ArrivalDot.dataset.port;
            const TargetEntry = Pending.PortDirection === 'source' ? ArrivalDot.dataset.entry : Pending.Entry;
            const TargetPort  = Pending.PortDirection === 'source' ? ArrivalDot.dataset.port  : Pending.Port;
            IntegrateLink(SourceEntry, SourcePort, TargetEntry, TargetPort);
        }
        RefreshEditor();
        return;
    }

    // Released over empty canvas — offer the compatible slice of the catalogue.
    const OverSurface = ReleaseTarget === PanSurface || ReleaseTarget === TransformPlane
                     || (ReleaseTarget && ReleaseTarget.closest('.PanSurface') && !ReleaseTarget.closest('.NodeEntry'));

    if (OverSurface)
    {
        RevealCatalogueMenu(PointerRelease.clientX, PointerRelease.clientY, Pending, SpawnCatalogueEntry);
    }
    RefreshEditor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY DRAGGING
//------------------------------------------------------------------------------------------------------------------------

function CommenceEntryDrag(Wrapper, PointerPress)
{
    const Entry = RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry || Entry.Immobilized || RecordStore.CanvasImmobilized) return;

    const AdditiveCondition = PointerPress.shiftKey;
    if (!Entry.Chosen) ChooseEntry(Entry.Token, AdditiveCondition);

    const ChosenEntries = RecordStore.Entries.filter((Candidate) => Candidate.Chosen && !Candidate.Immobilized);

    EntryDragState = {
        PointerId: PointerPress.pointerId,
        OriginClientX: PointerPress.clientX,
        OriginClientY: PointerPress.clientY,
        DragEngaged: false,
        Moving: ChosenEntries.map((Candidate) => ({
            Token: Candidate.Token,
            OriginPlaneX: Candidate.PlaneX,
            OriginPlaneY: Candidate.PlaneY
        }))
    };

    PanSurface.setPointerCapture(PointerPress.pointerId);
}

function AdvanceEntryDrag(PointerMotion)
{
    if (!EntryDragState) return;

    const TravelX = PointerMotion.clientX - EntryDragState.OriginClientX;
    const TravelY = PointerMotion.clientY - EntryDragState.OriginClientY;

    if (!EntryDragState.DragEngaged)
    {
        if (Math.hypot(TravelX, TravelY) < DragThreshold) return;
        EntryDragState.DragEngaged = true;
        for (const Moving of EntryDragState.Moving)
        {
            const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Moving.Token}"]`);
            if (Wrapper) Wrapper.classList.add('Dragging');
        }
    }

    const PlaneTravelX = TravelX / RecordStore.PlaneScale;
    const PlaneTravelY = TravelY / RecordStore.PlaneScale;

    for (const Moving of EntryDragState.Moving)
    {
        const Entry = RetrieveEntry(Moving.Token);
        if (!Entry) continue;
        Entry.PlaneX = Moving.OriginPlaneX + PlaneTravelX;
        Entry.PlaneY = Moving.OriginPlaneY + PlaneTravelY;

        const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Moving.Token}"]`);
        if (Wrapper) Wrapper.style.transform = `translate(${Entry.PlaneX}px, ${Entry.PlaneY}px)`;
    }

    InscribeLinks();
}

function ConcludeEntryDrag()
{
    if (!EntryDragState) return;
    for (const Moving of EntryDragState.Moving)
    {
        const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Moving.Token}"]`);
        if (Wrapper) Wrapper.classList.remove('Dragging');
    }
    EntryDragState = null;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SURFACE INTERACTION
//------------------------------------------------------------------------------------------------------------------------

PanSurface.addEventListener('pointerdown', (PointerPress) =>
{
    if (PointerPress.button === 2) return;   // Context menu is raised on contextmenu instead

    const PortDot = PointerPress.target.closest('[data-porthit]');
    if (PortDot)
    {
        PointerPress.preventDefault();
        PointerPress.stopPropagation();
        CommencePortDrag(PortDot, PointerPress);
        return;
    }

    // 📝 Interactive controls inside an entry must not start a drag or steal focus.
    if (PointerPress.target.closest('input, textarea, select, button, [data-action="RadialDial"]')) return;

    const Wrapper = PointerPress.target.closest('.NodeEntry');
    if (Wrapper)
    {
        CommenceEntryDrag(Wrapper, PointerPress);
        return;
    }

    // Middle button, or space held, pans; a plain left drag on empty canvas bands a selection.
    const PanCondition = PointerPress.button === 1 || PointerPress.altKey || RecordStore.CanvasImmobilized;

    if (PanCondition)
    {
        // A pan must track the hand exactly, so any glide is abandoned where it stands.
        AbandonGlide();
        PlanePanState = {
            OriginClientX: PointerPress.clientX,
            OriginClientY: PointerPress.clientY,
            OriginOffsetX: RecordStore.PlaneOffsetX,
            OriginOffsetY: RecordStore.PlaneOffsetY
        };
        PanSurface.classList.add('Grabbing');
        PanSurface.setPointerCapture(PointerPress.pointerId);
        return;
    }

    const SurfaceBounds = PanSurface.getBoundingClientRect();
    BandState = {
        OriginSurfaceX: PointerPress.clientX - SurfaceBounds.left,
        OriginSurfaceY: PointerPress.clientY - SurfaceBounds.top,
        AdditiveCondition: PointerPress.shiftKey
    };
    PanSurface.setPointerCapture(PointerPress.pointerId);
});

PanSurface.addEventListener('pointermove', (PointerMotion) =>
{
    if (LinkInFlight)   { AdvancePortDrag(PointerMotion); return; }
    if (EntryDragState) { AdvanceEntryDrag(PointerMotion); return; }

    if (PlanePanState)
    {
        RecordStore.PlaneOffsetX = PlanePanState.OriginOffsetX + (PointerMotion.clientX - PlanePanState.OriginClientX);
        RecordStore.PlaneOffsetY = PlanePanState.OriginOffsetY + (PointerMotion.clientY - PlanePanState.OriginClientY);
        AlignPlaneTransform();
        return;
    }

    if (BandState)
    {
        const SurfaceBounds = PanSurface.getBoundingClientRect();
        const CurrentX = PointerMotion.clientX - SurfaceBounds.left;
        const CurrentY = PointerMotion.clientY - SurfaceBounds.top;

        const BandLeft   = Math.min(BandState.OriginSurfaceX, CurrentX);
        const BandTop    = Math.min(BandState.OriginSurfaceY, CurrentY);
        const BandWidth  = Math.abs(CurrentX - BandState.OriginSurfaceX);
        const BandHeight = Math.abs(CurrentY - BandState.OriginSurfaceY);

        if (BandWidth > DragThreshold || BandHeight > DragThreshold)
        {
            SelectionBand.style.display = 'block';
            SelectionBand.style.left   = `${BandLeft}px`;
            SelectionBand.style.top    = `${BandTop}px`;
            SelectionBand.style.width  = `${BandWidth}px`;
            SelectionBand.style.height = `${BandHeight}px`;
            BandState.Engaged = true;
        }
    }
});

PanSurface.addEventListener('pointerup', (PointerRelease) =>
{
    if (LinkInFlight) { ConcludePortDrag(PointerRelease); return; }

    if (EntryDragState)
    {
        const DragEngaged = EntryDragState.DragEngaged;
        ConcludeEntryDrag();
        if (!DragEngaged)
        {
            const Wrapper = PointerRelease.target.closest('.NodeEntry');
            if (Wrapper) ChooseEntry(Wrapper.dataset.entry, PointerRelease.shiftKey);
        }
        else
        {
            RefreshEditor();
        }
        return;
    }

    if (PlanePanState) { PlanePanState = null; PanSurface.classList.remove('Grabbing'); return; }

    if (BandState)
    {
        if (BandState.Engaged)
        {
            // Collect every entry whose rendered footprint meets the band.
            const BandBounds = SelectionBand.getBoundingClientRect();
            for (const Entry of RecordStore.Entries)
            {
                const Wrapper = EntryPlane.querySelector(`.NodeEntry[data-entry="${Entry.Token}"]`);
                if (!Wrapper) continue;
                const EntryBounds = Wrapper.getBoundingClientRect();
                const OverlapCondition = EntryBounds.left < BandBounds.right
                                      && EntryBounds.right > BandBounds.left
                                      && EntryBounds.top < BandBounds.bottom
                                      && EntryBounds.bottom > BandBounds.top;
                if (OverlapCondition)              Entry.Chosen = true;
                else if (!BandState.AdditiveCondition) Entry.Chosen = false;
            }
            RefreshEditor();
        }
        else
        {
            DiscardChoice();
            ConcealCatalogueMenu();
        }

        SelectionBand.style.display = 'none';
        BandState = null;
    }
});

PanSurface.addEventListener('wheel', (WheelMotion) =>
{
    if (RecordStore.CanvasImmobilized) return;
    WheelMotion.preventDefault();
    const ZoomFactor = WheelMotion.deltaY < 0 ? ZoomIncrement : 1 / ZoomIncrement;

    // Compound from the glide target so a fast spin accumulates instead of fighting the easing.
    const ReferenceScale = GlideTarget ? GlideTarget.Scale : RecordStore.PlaneScale;
    ApplyZoomAboutPoint(ReferenceScale * ZoomFactor, WheelMotion.clientX, WheelMotion.clientY);
}, { passive: false });

PanSurface.addEventListener('contextmenu', (PointerPress) =>
{
    PointerPress.preventDefault();

    const Wrapper = PointerPress.target.closest('.NodeEntry');
    if (Wrapper)
    {
        RevealEntryMenu(PointerPress.clientX, PointerPress.clientY, Wrapper.dataset.entry);
        return;
    }
    RevealCatalogueMenu(PointerPress.clientX, PointerPress.clientY, null, SpawnCatalogueEntry);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY MENU
//------------------------------------------------------------------------------------------------------------------------

let EntryMenuElement = null;
let EntryMenuScreen  = null;

function ConcealEntryMenu()
{
    if (EntryMenuElement) { EntryMenuElement.remove(); EntryMenuElement = null; }
    if (EntryMenuScreen)  { EntryMenuScreen.remove();  EntryMenuScreen = null; }
}

function RevealEntryMenu(ScreenX, ScreenY, EntryToken)
{
    ConcealEntryMenu();
    ConcealCatalogueMenu();

    const Entry = RetrieveEntry(EntryToken);
    if (!Entry) return;

    EntryMenuScreen = document.createElement('div');
    EntryMenuScreen.className = 'MenuScreen';
    EntryMenuScreen.addEventListener('pointerdown', ConcealEntryMenu);
    EntryMenuScreen.addEventListener('contextmenu', (PointerPress) =>
    {
        PointerPress.preventDefault();
        ConcealEntryMenu();
    });
    document.body.appendChild(EntryMenuScreen);

    EntryMenuElement = document.createElement('div');
    EntryMenuElement.className = 'EntryMenu';
    EntryMenuElement.style.top  = `${Math.min(ScreenY, window.innerHeight - 100)}px`;
    EntryMenuElement.style.left = `${Math.min(ScreenX, window.innerWidth - 180)}px`;
    EntryMenuElement.innerHTML =
        `<button class="EntryMenuAction">`
      + `${Entry.Exported ? 'Unmark for export' : 'Mark for export'}</button>`;

    EntryMenuElement.addEventListener('pointerdown', (PointerPress) => PointerPress.stopPropagation());
    EntryMenuElement.addEventListener('click', () =>
    {
        Entry.Exported = !Entry.Exported;
        ConcealEntryMenu();
        RefreshEditor();
    });

    document.body.appendChild(EntryMenuElement);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENTRY ACTION ROUTING
//------------------------------------------------------------------------------------------------------------------------

EntryPlane.addEventListener('click', (PointerRelease) =>
{
    const ActionElement = PointerRelease.target.closest('[data-action]');
    if (!ActionElement) return;

    const Wrapper = ActionElement.closest('.NodeEntry');
    if (!Wrapper) return;
    const Entry = RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    const ActionNaming = ActionElement.dataset.action;
    if (ActionNaming === 'EntryBody') return;

    PointerRelease.stopPropagation();

    switch (ActionNaming)
    {
        case 'Fold':          Entry.Folded = !Entry.Folded; break;
        case 'Tack':          Entry.Tacked = !Entry.Tacked; break;
        case 'Immobilize':    Entry.Immobilized = !Entry.Immobilized; break;
        case 'Reclaim':       ReclaimEntry(Entry.Token); break;
        case 'AttachInbound': AttachInboundPort(Entry.Token); break;
        case 'AttachOutbound':AttachOutboundPort(Entry.Token); break;
        case 'DetachOutbound':DetachOutboundPort(Entry.Token, ActionElement.dataset.port); break;
        case 'Export':        window.alert(`Exporting ${Entry.Title}...`); return;
        case 'Preview':       ChooseEntry(Entry.Token, false); return;

        case 'Annotate':
            Entry.Composing = true;
            if (Entry.Annotation === undefined) Entry.Annotation = '';
            break;

        case 'AnnotationOpen':
            Entry.Composing = true;
            break;

        case 'Notice':
        {
            const NoticeText = window.prompt('Enter tooltip text for this node:', Entry.Notice || '');
            if (NoticeText === null) return;
            Entry.Notice = NoticeText;
            break;
        }

        case 'PortScalarReveal':
            Entry.RevealedScalars[ActionElement.dataset.port] = true;
            break;

        default: return;
    }

    RefreshEditor();

    // 📝 Re-focus the annotation field after the re-render that revealed it.
    if (ActionNaming === 'Annotate' || ActionNaming === 'AnnotationOpen')
    {
        const Field = EntryPlane.querySelector(
            `.NodeEntry[data-entry="${Entry.Token}"] [data-action="AnnotationField"]`);
        if (Field) { Field.focus(); Field.setSelectionRange(Field.value.length, Field.value.length); }
    }
    if (ActionNaming === 'PortScalarReveal')
    {
        const Field = EntryPlane.querySelector(
            `.NodeEntry[data-entry="${Entry.Token}"] [data-port="${ActionElement.dataset.port}"].PinScalarField`);
        if (Field) Field.focus();
    }
});

// 📝 Scalar edits mutate the store in place and never re-render the entry mid-typing — a re-render
//    would destroy the focused field and drop the caret.
EntryPlane.addEventListener('input', (FieldMotion) =>
{
    const Field = FieldMotion.target.closest('[data-action]');
    if (!Field) return;

    const Wrapper = Field.closest('.NodeEntry');
    const Entry = Wrapper && RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    const ActionNaming = Field.dataset.action;

    if (ActionNaming === 'PortScalar')
    {
        Entry.PortScalars[Field.dataset.port] = Field.value;
    }
    else if (ActionNaming === 'RegulatorScalar')
    {
        Entry.PortScalars['val'] = Field.value;
        const Sweep = Wrapper.querySelector('[data-action="RegulatorSweep"]');
        if (Sweep) Sweep.value = parseFloat(Field.value) || 0;
    }
    else if (ActionNaming === 'RegulatorSweep')
    {
        Entry.PortScalars['val'] = Field.value;
        const Scalar = Wrapper.querySelector('[data-action="RegulatorScalar"]');
        if (Scalar) Scalar.value = Field.value;
    }
    else if (ActionNaming === 'AxisScalar')
    {
        Entry.PortScalars[Field.dataset.axis] = Field.value;
    }
    else if (ActionNaming === 'AnnotationField')
    {
        Entry.Annotation = Field.value;
        return;
    }
    else
    {
        return;
    }

    ProvokeRebuild(Entry.Token, () => { RefreshProgressPill(); });
    RefreshProgressPill();
    FeedFluidViewport();
});

// A scalar field closes on double click, restoring the compact affordance.
EntryPlane.addEventListener('dblclick', (PointerRelease) =>
{
    const Field = PointerRelease.target.closest('[data-action="PortScalar"]');
    if (!Field) return;

    const Wrapper = Field.closest('.NodeEntry');
    const Entry = Wrapper && RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    PointerRelease.stopPropagation();
    const PortToken = Field.dataset.port;
    delete Entry.PortScalars[PortToken];
    delete Entry.RevealedScalars[PortToken];
    RefreshEditor();
});

EntryPlane.addEventListener('focusout', (FocusRelease) =>
{
    const Field = FocusRelease.target.closest('[data-action="AnnotationField"]');
    if (!Field) return;

    const Wrapper = Field.closest('.NodeEntry');
    const Entry = Wrapper && RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    Entry.Composing = false;
    RefreshEntries();
});

// 📝 Typing inside an entry must not reach the canvas-level delete shortcut.
EntryPlane.addEventListener('keydown', (KeyPress) =>
{
    if (KeyPress.target.closest('input, textarea')) KeyPress.stopPropagation();
});

//------------------------------------------------------------------------------------------------------------------------
//                                                     RADIAL DIAL
//------------------------------------------------------------------------------------------------------------------------

EntryPlane.addEventListener('pointerdown', (PointerPress) =>
{
    const Dial = PointerPress.target.closest('[data-action="RadialDial"]');
    if (!Dial) return;

    PointerPress.preventDefault();
    PointerPress.stopPropagation();

    const Wrapper = Dial.closest('.NodeEntry');
    const Entry = Wrapper && RetrieveEntry(Wrapper.dataset.entry);
    if (!Entry) return;

    const Needle = Dial.querySelector('.DialNeedle');

    const AlignNeedle = (PointerMotion) =>
    {
        const DialBounds = Dial.getBoundingClientRect();
        const CentreX = DialBounds.left + DialBounds.width / 2;
        const CentreY = DialBounds.top + DialBounds.height / 2;
        let DialAngle = Math.atan2(PointerMotion.clientY - CentreY, PointerMotion.clientX - CentreX) * (180 / Math.PI);
        if (DialAngle < 0) DialAngle += 360;

        Entry.PortScalars['val'] = DialAngle.toFixed(1);
        if (Needle) Needle.style.transform = `translate(0,-50%) rotate(${DialAngle}deg)`;

        const Scalar = Wrapper.querySelector('[data-action="RegulatorScalar"]');
        if (Scalar) Scalar.value = Entry.PortScalars['val'];

        ProvokeRebuild(Entry.Token, () => RefreshProgressPill());
        RefreshProgressPill();
    };

    AlignNeedle(PointerPress);
    Dial.setPointerCapture(PointerPress.pointerId);

    const ReleaseDial = () =>
    {
        Dial.removeEventListener('pointermove', AlignNeedle);
        Dial.removeEventListener('pointerup', ReleaseDial);
    };
    Dial.addEventListener('pointermove', AlignNeedle);
    Dial.addEventListener('pointerup', ReleaseDial);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                   LINK SEVERING
//------------------------------------------------------------------------------------------------------------------------

EdgeCanvas.addEventListener('click', (PointerRelease) =>
{
    const Curve = PointerRelease.target.closest('[data-link]');
    if (!Curve) return;
    PointerRelease.stopPropagation();
    ReclaimLink(Curve.dataset.link);
    RefreshEditor();
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  KEYBOARD SHORTCUTS
//------------------------------------------------------------------------------------------------------------------------

window.addEventListener('keydown', (KeyPress) =>
{
    if (KeyPress.target.closest('input, textarea, select')) return;

    if (KeyPress.key === 'Delete' || KeyPress.key === 'Backspace')
    {
        const Doomed = RecordStore.Entries.filter((Entry) => Entry.Chosen && !Entry.Immobilized);
        if (Doomed.length === 0) return;
        KeyPress.preventDefault();
        for (const Entry of Doomed) ReclaimEntry(Entry.Token);
        RefreshEditor();
        return;
    }

    if (KeyPress.key === 'Escape')
    {
        ConcealCatalogueMenu();
        ConcealEntryMenu();
        DiscardChoice();
        return;
    }

    if (KeyPress.key === 'f' || KeyPress.key === 'F') EncompassEntries();
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOP BAR WIRING
//------------------------------------------------------------------------------------------------------------------------

const SimulateAction       = document.getElementById('SimulateAction');
const SimulateHint         = document.getElementById('SimulateHint');
const ChromeSettingsAction = document.getElementById('ChromeSettingsAction');
const ChromeSettingsPane   = document.getElementById('ChromeSettingsPane');
const ChromeSettingsShell  = document.getElementById('ChromeSettingsShell');
const ViewportToggle       = document.getElementById('ViewportToggle');
const ControlsToggle       = document.getElementById('ControlsToggle');

function RefreshTopBar()
{
    SimulateAction.innerHTML = ComposeGlyph(RecordStore.SimulationRunning ? 'Suspend' : 'Advance', 18);
    SimulateAction.classList.toggle('Running', RecordStore.SimulationRunning);
    SimulateHint.dataset.hint = RecordStore.SimulationRunning ? 'Pause Simulation' : 'Simulate';

    ChromeSettingsAction.innerHTML = ComposeGlyph('Configure', 16);
}

SimulateAction.addEventListener('click', () =>
{
    RecordStore.SimulationRunning = !RecordStore.SimulationRunning;
    RefreshTopBar();
});

function ConcealChromeSettings()
{
    ChromeSettingsPane.classList.remove('Open');
    ChromeSettingsAction.classList.remove('Chosen');
}

ChromeSettingsAction.addEventListener('click', () =>
{
    const Revealed = ChromeSettingsPane.classList.toggle('Open');
    ChromeSettingsAction.classList.toggle('Chosen', Revealed);
});

// 📝 The card closes on leaving the shell that holds the button and the pane together, so it dismisses
//    as soon as the pointer walks off the settings themselves rather than surviving until the pointer
//    crosses into the other canvas.
ChromeSettingsShell.addEventListener('pointerleave', DismissOnGenuineLeave(ConcealChromeSettings));

ViewportToggle.addEventListener('click', () =>
{
    RecordStore.ViewportSide = RecordStore.ViewportSide === 'hidden' ? 'right' : 'hidden';
    RefreshViewportSide();
});

ControlsToggle.addEventListener('click', () =>
{
    RecordStore.ControlsRevealed = !RecordStore.ControlsRevealed;
    ControlsToggle.querySelector('.ToggleTrack').classList.toggle('Engaged', RecordStore.ControlsRevealed);
    RefreshCanvasControls();
});

function RefreshViewportSide()
{
    const ConcealedCondition = RecordStore.ViewportSide === 'hidden';
    document.body.classList.toggle('ViewportHidden', ConcealedCondition);
    ViewportRegion.classList.toggle('Concealed', ConcealedCondition);
    ViewportToggle.querySelector('.ToggleTrack').classList.toggle('Engaged', !ConcealedCondition);
    if (!ConcealedCondition) FeedFluidViewport();
    InscribeLinks();
}

ControlAnchor.addEventListener('click', (PointerRelease) =>
{
    const ControlAction = PointerRelease.target.closest('[data-control]');
    if (!ControlAction) return;

    const SurfaceBounds = PanSurface.getBoundingClientRect();
    const CentreX = SurfaceBounds.left + SurfaceBounds.width / 2;
    const CentreY = SurfaceBounds.top + SurfaceBounds.height / 2;

    switch (ControlAction.dataset.control)
    {
        case 'ZoomIn':
            ApplyZoomAboutPoint(
                (GlideTarget ? GlideTarget.Scale : RecordStore.PlaneScale) * ZoomIncrement, CentreX, CentreY);
            break;
        case 'ZoomOut':
            ApplyZoomAboutPoint(
                (GlideTarget ? GlideTarget.Scale : RecordStore.PlaneScale) / ZoomIncrement, CentreX, CentreY);
            break;
        case 'Encompass':  EncompassEntries(); break;
        case 'Immobilize':
            RecordStore.CanvasImmobilized = !RecordStore.CanvasImmobilized;
            PanSurface.classList.toggle('Locked', RecordStore.CanvasImmobilized);
            RefreshCanvasControls();
            break;
    }
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  VIEWPORT CHROME WIRING
//------------------------------------------------------------------------------------------------------------------------

const ViewportSettingsAction = document.getElementById('ViewportSettingsAction');
const ViewportSettingsPane   = document.getElementById('ViewportSettingsPane');
const ViewportSettingsShell  = document.getElementById('ViewportSettingsShell');

ViewportSettingsAction.innerHTML = ComposeGlyph('Configure', 18);
document.getElementById('DisplayGlyph').innerHTML     = ComposeGlyph('Lattice', 16);
document.getElementById('EnvironmentGlyph').innerHTML = ComposeGlyph('Sun', 16);
document.getElementById('DiagnosticGlyph').innerHTML  = ComposeGlyph('Observe', 16);
document.getElementById('NoticeGlyph').innerHTML      = ComposeGlyph('Notice', 24);

function ConcealViewportSettings()
{
    ViewportSettingsPane.classList.remove('Open');
    ViewportSettingsAction.classList.remove('Engaged');
    ViewportSettingsShell.classList.add('Shut');
}

ViewportSettingsAction.addEventListener('click', () =>
{
    const Revealed = ViewportSettingsPane.classList.toggle('Open');
    ViewportSettingsAction.classList.toggle('Engaged', Revealed);
    ViewportSettingsShell.classList.toggle('Shut', !Revealed);
});

// 📝 Dragging a slider past the pane edge still holds the pointer captive, and dismissing the card
//    underneath the hand would abandon the drag. A held button means the pointer is still committed to
//    the pane, so the leave is ignored until it is released.
function DismissOnGenuineLeave(ConcealAction)
{
    return (PointerExit) =>
    {
        if (PointerExit.buttons !== 0) return;
        ConcealAction();
    };
}

ViewportSettingsShell.addEventListener('pointerleave', DismissOnGenuineLeave(ConcealViewportSettings));

// 📝 A max-height transition needs two real numbers to travel between — `none` is not interpolable, so
//    the open height has to be a measured pixel value. It is measured at fold time rather than seeded
//    once at load: at load the card is shut inside a clamped shell, so scrollHeight comes back
//    clipped, and a value cached then would also go stale as soon as the content reflowed.
for (const FoldCaption of ViewportSettingsPane.querySelectorAll('[data-fold]'))
{
    const Section = FoldCaption.nextElementSibling;
    FoldCaption.querySelector('.FoldChevron').innerHTML = ComposeGlyph('ChevronDown', 14);

    if (!Section || !Section.classList.contains('PaneSection')) continue;

    FoldCaption.addEventListener('click', () =>
    {
        const FoldingCondition = !FoldCaption.classList.contains('Folded');

        if (FoldingCondition)
        {
            // Pin the open height so the collapse eases from a number rather than from `none`, and let
            // that frame land before the 0 — applied in one flush the browser sees no transition to run.
            Section.style.maxHeight = `${Section.scrollHeight}px`;
            requestAnimationFrame(() => FoldCaption.classList.add('Folded'));
            return;
        }

        // 📝 Unfolding runs the other way about: the collapsed rule carries `!important`, so an inline
        //    height set while `Folded` is still on is ignored, then snaps in the instant the class
        //    drops. The class comes off first, pinning 0 inline as the start, and the measured target
        //    goes on the following frame.
        Section.style.maxHeight = '0px';
        FoldCaption.classList.remove('Folded');
        requestAnimationFrame(() =>
        {
            Section.style.maxHeight = `${Section.scrollHeight}px`;
        });
    });

    // Once open, drop the cap so the section can grow with its own content.
    Section.addEventListener('transitionend', (Motion) =>
    {
        if (Motion.propertyName !== 'max-height') return;
        if (!FoldCaption.classList.contains('Folded')) Section.style.maxHeight = 'none';
    });
}

const ShadingActions =
[
    { Identifier: 'ShadeRefractive', Mode: 'refractive', Glyph: 'Droplet' },
    { Identifier: 'ShadeOpaque',     Mode: 'opaque',     Glyph: 'Circle'  },
    { Identifier: 'ShadeThickness',  Mode: 'thickness',  Glyph: 'Vessel'  },
    { Identifier: 'ShadePoints',     Mode: 'points',     Glyph: 'Ripple'  }
];

for (const Specification of ShadingActions)
{
    const ShadingAction = document.getElementById(Specification.Identifier);
    ShadingAction.innerHTML = ComposeGlyph(Specification.Glyph, 18);
    ShadingAction.addEventListener('click', () =>
    {
        for (const Candidate of ShadingActions)
        {
            document.getElementById(Candidate.Identifier)
                .classList.toggle('Chosen', Candidate.Mode === Specification.Mode);
        }
        ReconfigureShading(Specification.Mode);
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                              DIAGNOSTICS & TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

document.getElementById('InspectSelect').addEventListener('change', (SelectionChange) =>
{
    ReconfigureInspection(SelectionChange.target.value);
});

for (const Overlay of [{ Identifier: 'BoundsToggle', Key: 'DomainBounds' },
                       { Identifier: 'ColliderToggle', Key: 'ColliderOutlines' }])
{
    const ToggleAction = document.getElementById(Overlay.Identifier);
    ToggleAction.addEventListener('click', () =>
    {
        const Track = ToggleAction.querySelector('.ToggleTrack');
        const Engaged = Track.classList.toggle('Engaged');
        ReconfigureOverlay({ [Overlay.Key]: Engaged });
    });
}

// 📝 Transport is held here rather than in the viewport so the graph's Simulate action and this bar
//    cannot disagree about whether the sim is running — both read and write the one store field.
const SimulationPlay  = document.getElementById('SimulationPlay');
const SimulationStep  = document.getElementById('SimulationStep');
const SimulationReset = document.getElementById('SimulationReset');

SimulationStep.innerHTML  = ComposeGlyph('Chevron', 18);
SimulationReset.innerHTML = ComposeGlyph('Encompass', 18);

function RefreshTransport()
{
    SimulationPlay.innerHTML = ComposeGlyph(RecordStore.SimulationRunning ? 'Suspend' : 'Advance', 18);
    SimulationPlay.classList.toggle('Running', RecordStore.SimulationRunning);
    RegulateTransport(RecordStore.SimulationRunning ? 'run' : 'halt');
}

SimulationPlay.addEventListener('click', () =>
{
    RecordStore.SimulationRunning = !RecordStore.SimulationRunning;
    RefreshTransport();
    RefreshTopBar();
});

SimulationStep.addEventListener('click', () =>
{
    // Stepping implies pausing: advancing one frame while the loop runs would be indistinguishable.
    RecordStore.SimulationRunning = false;
    RefreshTransport();
    RefreshTopBar();
    RegulateTransport('step');
});

SimulationReset.addEventListener('click', () =>
{
    RegulateTransport('reset');
    RefreshSolverReadout();
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  SOLVER READOUT
//------------------------------------------------------------------------------------------------------------------------

const ReadoutGrid      = document.getElementById('ReadoutGrid');
const ReadoutParticles = document.getElementById('ReadoutParticles');
const ReadoutStep      = document.getElementById('ReadoutStep');
const ReadoutFrame     = document.getElementById('ReadoutFrame');

// 📝 Reports what the solver ACTUALLY holds, queried from it rather than re-derived from the
//    specification — a readout that recomputes its own figures cannot reveal a feed that failed to land.
function RefreshSolverReadout()
{
    const Tally = ResolveSolverTally();
    if (!Tally)
    {
        ReadoutGrid.textContent = ReadoutParticles.textContent = '—';
        ReadoutStep.textContent = ReadoutFrame.textContent     = '—';
        return;
    }

    ReadoutGrid.textContent      = `${Tally.GridExtent}³`;
    ReadoutParticles.textContent = Tally.ParticleTally.toLocaleString();
    ReadoutStep.textContent      = `${Tally.SubstepTally}×`;

    // Frame cost is the one figure that can indicate the budget is blown, so it is marked when it does.
    ReadoutFrame.textContent = `${Tally.FrameMilliseconds.toFixed(1)}ms`;
    ReadoutFrame.classList.toggle('Strained', Tally.FrameMilliseconds > 33.3);
}

const CameraSolid  = document.getElementById('CameraSolid');
const CameraPlanar  = document.getElementById('CameraPlanar');

CameraSolid.addEventListener('click', () =>
{
    CameraSolid.classList.add('Chosen');
    CameraPlanar.classList.remove('Chosen');
    ReconfigureCameraMode('3D');
});

CameraPlanar.addEventListener('click', () =>
{
    CameraPlanar.classList.add('Chosen');
    CameraSolid.classList.remove('Chosen');
    ReconfigureCameraMode('2D');
});

document.getElementById('ResolutionSelect').addEventListener('change', (SelectionChange) =>
{
    ReconfigureResolution(parseInt(SelectionChange.target.value, 10));
});

// The dial openings, matching the viewport profile these sweeps drive.
const EnvironmentOpenings =
{
    SunElevation: 20,
    SunAzimuth:   45,
    Turbidity:    10,
    FogDensity:   0.005
};

// 📝 Ported from ControlsPreview.html: the track is a plain element dragged by pointer rather than an
//    `input[type=range]`, because the range thumb cannot be styled to the component vocabulary and its
//    fill has no cross-engine form at all. Pointer capture is taken on the track so a drag that leaves
//    the pane still steers the dial until the button is released.
for (const SweepGroup of ViewportSettingsPane.querySelectorAll('[data-sweep]'))
{
    const Dial   = SweepGroup.dataset.sweep;
    const Least  = parseFloat(SweepGroup.dataset.least);
    const Most   = parseFloat(SweepGroup.dataset.most);
    const Step   = parseFloat(SweepGroup.dataset.step);
    const Places = parseInt(SweepGroup.dataset.places, 10);

    const Track   = SweepGroup.querySelector('.Sweep');
    const Fill    = SweepGroup.querySelector('.SweepFill');
    const Knob    = SweepGroup.querySelector('.SweepKnob');
    const Readout = SweepGroup.querySelector('.ValueNumber input');

    Readout.min  = Least;
    Readout.max  = Most;
    Readout.step = Step;

    let Magnitude = EnvironmentOpenings[Dial];

    // Paint the track from the held magnitude. Kept separate from the commit so a typed value and a
    // dragged one settle on exactly the same geometry.
    function InscribeSweep(RefreshReadout)
    {
        const Fraction = (Magnitude - Least) / (Most - Least);
        Fill.style.width = `${Fraction * 100}%`;
        Knob.style.left  = `${Fraction * 100}%`;
        if (RefreshReadout) Readout.value = Magnitude.toFixed(Places);
    }

    function CommitMagnitude(Proposal, RefreshReadout)
    {
        // 📝 Quantized to the step before clamping, so the readout can never show a value the dial
        //    cannot actually hold — a 0.0007 fog density displaying as 0.001 while the viewport ran the
        //    unrounded figure would put the two quietly out of agreement.
        const Quantized = Math.round(Proposal / Step) * Step;
        Magnitude = Math.min(Most, Math.max(Least, Quantized));
        InscribeSweep(RefreshReadout);
        ReconfigureEnvironment({ [Dial]: Magnitude });
    }

    function ResolveFromPointer(ClientX)
    {
        const Bounds   = Track.getBoundingClientRect();
        const Fraction = Math.min(1, Math.max(0, (ClientX - Bounds.left) / Bounds.width));
        CommitMagnitude(Least + Fraction * (Most - Least), true);
    }

    Track.addEventListener('pointerdown', (PointerPress) =>
    {
        PointerPress.preventDefault();
        Track.setPointerCapture(PointerPress.pointerId);
        Track.dataset.hauling = 'true';
        ResolveFromPointer(PointerPress.clientX);
    });

    Track.addEventListener('pointermove', (PointerMotion) =>
    {
        if (Track.dataset.hauling !== 'true') return;
        ResolveFromPointer(PointerMotion.clientX);
    });

    const ConcludeHaul = (PointerRelease) =>
    {
        if (Track.dataset.hauling !== 'true') return;
        delete Track.dataset.hauling;
        Track.releasePointerCapture(PointerRelease.pointerId);
    };

    Track.addEventListener('pointerup', ConcludeHaul);
    Track.addEventListener('pointercancel', ConcludeHaul);

    // 📝 The typed path deliberately does not rewrite the field while it is being edited: reformatting
    //    mid-keystroke would fight the caret, so the readout is left alone until focus departs.
    Readout.addEventListener('input', () =>
    {
        const Typed = parseFloat(Readout.value);
        if (Number.isNaN(Typed)) return;
        CommitMagnitude(Typed, false);
    });

    Readout.addEventListener('change', () => InscribeSweep(true));
    Readout.addEventListener('blur',   () => InscribeSweep(true));

    InscribeSweep(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPLIT HANDLE
//------------------------------------------------------------------------------------------------------------------------

const SplitHandle = document.getElementById('SplitHandle');

const SplitFloor   = 0.18;   // [-] - Narrowest either side may be squeezed, as a fraction of the window
const SplitCeiling = 0.82;   // [-] - Widest the graph may grow before the viewport is uselessly thin

// 📝 Committed as a percentage rather than a pixel width so the split survives a window resize at the
//    ratio the user chose, instead of the graph keeping its pixels and the viewport absorbing the change.
function ApplySplitFraction(Fraction)
{
    const Clamped = Math.min(SplitCeiling, Math.max(SplitFloor, Fraction));
    document.body.style.setProperty('--split-fraction', `${(Clamped * 100).toFixed(3)}%`);
    InscribeLinks();
}

SplitHandle.addEventListener('pointerdown', (PointerPress) =>
{
    PointerPress.preventDefault();
    SplitHandle.setPointerCapture(PointerPress.pointerId);
    SplitHandle.classList.add('Hauling');
    document.body.classList.add('Hauling');
});

SplitHandle.addEventListener('pointermove', (PointerMotion) =>
{
    if (!SplitHandle.classList.contains('Hauling')) return;
    ApplySplitFraction(PointerMotion.clientX / window.innerWidth);
});

function ConcludeSplitHaul(PointerRelease)
{
    if (!SplitHandle.classList.contains('Hauling')) return;
    SplitHandle.classList.remove('Hauling');
    document.body.classList.remove('Hauling');
    if (PointerRelease) SplitHandle.releasePointerCapture(PointerRelease.pointerId);
}

SplitHandle.addEventListener('pointerup', ConcludeSplitHaul);
SplitHandle.addEventListener('pointercancel', ConcludeSplitHaul);

// A double click restores the even split, which is otherwise fiddly to hit by hand.
SplitHandle.addEventListener('dblclick', () => ApplySplitFraction(0.5));

//------------------------------------------------------------------------------------------------------------------------
//                                                     INITIALIZE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The terrain editor opened on a single bare "Start" node, which was honest there because one
//    generator node is a complete graph. A liquid graph is a CHAIN, and an unwired canvas would show an
//    empty box with no indication of what to wire — so the editor opens on the smallest graph that
//    actually simulates: a dam break, gravity, the FLIP solve, the projection, and a surface out.
//    This doubles as the reference wiring for anyone reading the prototype for the first time.
function SeedOpeningGraph()
{
    const Spawned = [];

    // Laid out left to right in execution order, the way the graph reads.
    const Layout =
    [
        { Token: 'emit_dambreak',   Category: 'emitter',  PlaneX:   0, PlaneY: 140 },
        { Token: 'force_gravity',   Category: 'force',    PlaneX:   0, PlaneY: 420 },
        { Token: 'force_apply',     Category: 'force',    PlaneX: 300, PlaneY: 300 },
        { Token: 'solve_flip',      Category: 'solver',   PlaneX: 600, PlaneY: 140 },
        { Token: 'solve_pressure',  Category: 'solver',   PlaneX: 900, PlaneY: 140 },
        { Token: 'surf_reconstruct',Category: 'surface',  PlaneX:1200, PlaneY: 140 },
        { Token: 'surf_water',      Category: 'surface',  PlaneX:1500, PlaneY: 140 },
        { Token: 'surf_output',     Category: 'surface',  PlaneX:1800, PlaneY: 200 }
    ];

    for (const Placement of Layout)
    {
        const Category = NodeCatalogue.find((Candidate) => Candidate.Category === Placement.Category);
        const Item = Category && Category.Items.find((Candidate) => Candidate.Token === Placement.Token);
        if (!Item) continue;

        const Entry = ConstructCatalogueEntry(Item, Placement.Category, Placement.PlaneX, Placement.PlaneY);
        RecordStore.Entries.push(Entry);
        Spawned.push(Entry);
    }

    // Ports must exist before links can be routed to them.
    RefreshEntries();

    // 📝 Wire by port LABEL rather than by index, for the same reason the dials resolve by label: an
    //    added port would otherwise re-point these links silently.
    const Locate = (CatalogueToken) => Spawned.find((Entry) => Entry.CatalogueToken === CatalogueToken);
    const Bind = (SourceToken, TargetToken, TargetLabel) =>
    {
        const Source = Locate(SourceToken);
        const Target = Locate(TargetToken);
        if (!Source || !Target || Source.OutboundPorts.length === 0) return;
        const Port = Target.InboundPorts.find((Candidate) => Candidate.Label === TargetLabel);
        if (!Port) return;
        IntegrateLink(Source.Token, Source.OutboundPorts[0].Token, Target.Token, Port.Token);
    };

    Bind('emit_dambreak',   'force_apply',      'Volume');
    Bind('force_gravity',   'force_apply',      'Force');
    Bind('force_apply',     'solve_flip',       'Volume');
    Bind('solve_flip',      'solve_pressure',   'Volume');
    Bind('solve_pressure',  'surf_reconstruct', 'Volume');
    Bind('surf_reconstruct','surf_water',       'Surface');
    Bind('surf_water',      'surf_output',      'Surface');

    // Open with the output chosen, so the viewport previews the finished chain rather than nothing.
    const Output = Locate('surf_output');
    if (Output) Output.Chosen = true;
}

SeedOpeningGraph();

AlignPlaneTransform();
RefreshTopBar();
RefreshTransport();
RefreshViewportSide();
RefreshCanvasControls();
RefreshEditor();

// 📝 WebGPU initialization is asynchronous and can legitimately fail, so the viewport reports its own
//    outcome rather than throwing into the module's top level — an unhandled rejection here would take
//    the whole editor down with it and leave a blank page.
InitializeFluidViewport(document.getElementById('FluidCanvas')).then((Outcome) =>
{
    if (!Outcome.AcquiredCondition)
    {
        const Notice = document.getElementById('AdapterNotice');
        Notice.classList.add('Revealed');
        document.getElementById('NoticeBody').textContent = Outcome.Diagnostic
            || 'This viewport needs a WebGPU adapter. The graph remains fully usable.';
        return;
    }
    FeedFluidViewport();
    RefreshSolverReadout();
});

EncompassEntriesAbruptly();

window.addEventListener('resize', () =>
{
    InscribeLinks();
});

// The readout tracks a live solver, so it is polled rather than pushed from the feed.
setInterval(RefreshSolverReadout, 500);
