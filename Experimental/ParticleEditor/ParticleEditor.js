/*==========================================================================================================================================
                                                           PARTICLEEDITOR.JS
==========================================================================================================================================*/
// 🧩 The Particle editor prototype: the eos WebGPU engine and preset library behind the Frontier outliner, inspector and field overlay.
import { PE } from './Engine/pe.js';
import './Engine/fibres.js';
import './Engine/presets.js';
import './Engine/shaders.js';
import './Engine/engine.js';
import './Engine/lightning.js';
import './Engine/forcefields.js';
import './Engine/edits.js';
import './Engine/handoff.js';
import { ConstructLatticeReadback, EvaluateFieldAcceleration } from './ParticleFieldReadback.js';

// 📝 The engine files are the eos Particle editor's, unchanged (Engine/README in ../README.md). The frame loop below is
//    eos app.js's frame(), systems(), camera and packing, ported with Frontier names; the outliner, inspector, library and
//    overlay are new and use InspectorControls and ForceFieldVisualization (classic scripts loaded before this one).
// ⚠️ Top-level const in a classic script is a global binding, not a window property: name them bare, never window.X.
const C = InspectorControls;
const V = ForceFieldVisualization;
const SampleCapacity = 40;

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATRICES
//------------------------------------------------------------------------------------------------------------------------

const Dot3 = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Subtract3 = (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
const Cross3 = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Normalize3 = A => { const Length = Math.hypot(A[0], A[1], A[2]) || 1; return [A[0] / Length, A[1] / Length, A[2] / Length]; };
const Clamp = (Reading, Lower, Upper) => Math.min(Upper, Math.max(Lower, Reading));
const FormatInteger = Count => Math.round(Count).toLocaleString('en-US');
const Wrap360 = Degrees => ((Degrees % 360) + 360) % 360;

function MultiplyProjection(A, B)
{
    const Out = new Float32Array(16);
    for (let Column = 0; Column < 4; Column++)
        for (let Row = 0; Row < 4; Row++)
        {
            let Sum = 0;
            for (let Inner = 0; Inner < 4; Inner++) Sum += A[Inner * 4 + Row] * B[Column * 4 + Inner];
            Out[Column * 4 + Row] = Sum;
        }
    return Out;
}

function ConstructLookAt(Eye, Centre, Up)
{
    const Forward = Normalize3(Subtract3(Centre, Eye));
    const Side = Normalize3(Cross3(Forward, Up));
    const Upward = Cross3(Side, Forward);
    return Float32Array.of(Side[0], Upward[0], -Forward[0], 0, Side[1], Upward[1], -Forward[1], 0, Side[2], Upward[2], -Forward[2], 0,
                           -Dot3(Side, Eye), -Dot3(Upward, Eye), Dot3(Forward, Eye), 1);
}

function ConstructPerspective(FieldOfView, Aspect, Near, Far)
{
    const Focal = 1 / Math.tan(FieldOfView / 2);
    return Float32Array.of(Focal / Aspect, 0, 0, 0, 0, Focal, 0, 0, 0, 0, Far / (Near - Far), -1, 0, 0, (Near * Far) / (Near - Far), 0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE
//------------------------------------------------------------------------------------------------------------------------

const Scene = {
    Engine: null, Lightning: null, Readback: null, Effects: [], NextNumber: 1,
    Forces: PE.Forces.DefaultForces(),
    Wind: Object.assign(PE.defaultWind(), { showArrows: false }),
    Camera: { Yaw: 0.6, Pitch: 0.3, Distance: 12.5, Target: [0, 1.6, 0] },
    Playing: true, TimeScale: 1, Time: 0, Step: 0, Rate: 0, CpuMilliseconds: 0,
    ProbeEnabled: true, Probe: [0, 1, 0],
    Web: { Enabled: false, Interval: 0.6, NextAt: 0, Links: 2 },
    Overlay: { Enabled: true, Contribution: 'Flow', Quantity: 'Speed', Heatmap: true, Arrows: true, Tracers: false, SliceHeight: 1.2,
               Opacity: 0.38, ArrowSpacing: 34, Minimap: true, Labels: true },
    PackedFields: null, AdapterName: '—'
};
const Domain = { Minimum: PE.WIND.min, Size: PE.WIND.size, Dimensions: PE.WIND.dim };
const ClearColour = [0.035, 0.037, 0.045];
const LiveRefreshers = [];
const PersistentRefreshers = [];
const Sections = [];
const ProbeSamples = [];
let Selected = 'Field';
let PageScope = null, OutlinerScope = null, InspectorScroller = null;
let Visualization = null, OutlinerQuery = '', LibraryGroup = 'All', LibraryQuery = '';

const CategoryLabels = { 0: 'Streak', 1: 'Wind-driven', 3: 'Molecular · LJ', 4: 'Molecular · reactive', 5: 'VFX', 6: 'Swarm · flocking', 7: 'Light fibres' };
const GroupGlyphs = { Transitions: 'Boxes', VFX: 'Flame', 'Wind-blown': 'Leaf', 'Light fibres': 'Zap', Water: 'Drop', Ambient: 'Sparkles', Effects: 'Sparkles',
                      Weather: 'Cloud', Fields: 'Magnet', Ground: 'Layers', Physics: 'Atom', Storms: 'Tornado', Aircraft: 'Rays', Space: 'Planet' };
const ForceGlyphs = { prevailing: 'Wind', gust: 'Front', tornado: 'Tornado', outflow: 'Blast', attract: 'Attract', repel: 'Repel', lift: 'Lift', magnetic: 'Magnet',
                      gravity: 'Gravity', orbit: 'Orbit', drag: 'Damp', current: 'Current' };
const Contributions = { flow: 'Flow', accelerate: 'Accelerate', damp: 'Damp' };
const ContributionColours = { Flow: '#8fd3c4', Accelerate: '#e8c27a', Damp: '#b9a3e8' };
const ContributionUnits = { Flow: 'm/s', Accelerate: 'm/s²', Damp: '1/s' };

// 📝 Colour of a linear RGBA as display hex (for outliner glyphs and library swatches).
function DisplayColour(Linear)
{
    return '#' + [0, 1, 2].map(Channel => Math.round(Clamp(C.LinearToDisplay(Clamp(Linear[Channel], 0, 1)), 0, 1) * 255).toString(16).padStart(2, '0')).join('');
}

// 📝 eos stores a flow field's direction as atan2(x, z) (0° = +Z). The editor shows compass bearings like the Force Field editor
//    (0° = −Z, up in the top view; 90° = +X), which is 180° − the eos reading; the data itself is not converted.
const CompassFromField = Field => Wrap360(180 - PE.Forces.Bearing(Field));
const FieldFromCompass = Degrees => PE.Forces.Along(Wrap360(180 - Degrees));

//------------------------------------------------------------------------------------------------------------------------
//                                                  EFFECTS (eos systems)
//------------------------------------------------------------------------------------------------------------------------

const FlowKinds = ['prevailing', 'gust', 'tornado', 'outflow'];

// 📝 eos applyWindLink: a preset that needs its wind (a tornado its column, a sandstorm its prevailing wind) switches it on.
function ApplyWindLink(Preset)
{
    const Link = Preset.windLink;
    if (!Link) return;
    const Kind = FlowKinds[Link.type];
    if (!Kind) return;
    let Field = Scene.Forces.find(One => One.Kind === Kind);
    if (!Field)
    {
        if (FlowFields().length >= PE.WIND.maxComps) return;
        Field = { ...PE.Forces.BaseField(Kind), Name: Link.name, Enabled: false, Centre: [0, 0, 0], Radius: 2, Strength: 4, Rate: 0, Direction: PE.Forces.Along(0) };
        Scene.Forces.push(Field);
    }
    const Changes = Link.set || {};
    if (Changes.radius !== undefined) Field.Radius = Changes.radius;
    if (Changes.strength !== undefined) Field.Strength = Changes.strength;
    if (Changes.bearing !== undefined) Field.Direction = PE.Forces.Along(Changes.bearing);
    if (Changes.freq !== undefined) Field.Rate = Changes.freq;
    if (Link.local) { Field.Centre[0] = Preset.p.origin[0]; Field.Centre[2] = Preset.p.origin[2]; }
    Field.Enabled = true;
}

function FlowFields()
{
    return Scene.Forces.filter(One => { const Kind = PE.Forces.KindById(One.Kind); return Kind && Kind.Give === PE.Forces.Contribution.Flow; });
}

function ConstructEffect(PresetId, Name)
{
    const Preset = PE.presetById(PresetId);
    ApplyWindLink(Preset);
    const Effect = { Token: 'fx_' + Scene.NextNumber, Number: Scene.NextNumber++, PresetId, Name: Name || Preset.name, P: JSON.parse(JSON.stringify(Preset.p)),
                     Gpu: null, Accumulated: 0, PendingBurst: 0, OverrideOrigin: null, NeedFill: true, Head: 0, BurstTimer: undefined, AliveSamples: [] };
    Effect.Gpu = Scene.Engine.createSystem(Effect.P);
    return Effect;
}

function RebuildGpu(Effect)
{
    if (Effect.Gpu) Scene.Engine.destroySystem(Effect.Gpu);
    Effect.Gpu = Scene.Engine.createSystem(Effect.P);
    Object.assign(Effect, { NeedFill: true, Head: 0, Accumulated: 0, PendingBurst: 0 });
}

function AddEffect(PresetId)
{
    const Preset = PE.presetById(PresetId);
    const Same = Scene.Effects.filter(Effect => Effect.PresetId === PresetId).length;
    const Effect = ConstructEffect(PresetId, Same ? `${Preset.name} ${Same + 1}` : Preset.name);
    Scene.Effects.push(Effect);
    Selected = Effect.Token;
    const Kind = CategoryLabels[Effect.P.kind] || 'Particles';
    AppendLog('PARTICLES', `Added ${Effect.Name}`, `${Preset.group === Kind ? Kind : `${Preset.group} · ${Kind}`} · `
              + (Effect.P.kind === 7 ? `${FormatInteger(Effect.P.fibre.strands)} strands` : `${FormatInteger(Effect.Gpu.cap)} capacity`), 'live');
    ConstructOutliner();
    ConstructInspectorPage();
    C.Broadcast();
    return Effect;
}

function RemoveEffect(Effect)
{
    Scene.Engine.destroySystem(Effect.Gpu);
    Scene.Effects = Scene.Effects.filter(One => One !== Effect);
    Selected = 'Field';
    AppendLog('PARTICLES', `Removed ${Effect.Name}`, null, 'warm');
    ConstructOutliner();
    ConstructInspectorPage();
    C.Broadcast();
}

function DuplicateEffect(Effect)
{
    const Copy = ConstructEffect(Effect.PresetId, `${Effect.Name} copy`);
    Copy.P = JSON.parse(JSON.stringify(Effect.P));
    Copy.P.origin = [Copy.P.origin[0] + 1, Copy.P.origin[1], Copy.P.origin[2] + 1];
    RebuildGpu(Copy);
    Scene.Effects.push(Copy);
    Selected = Copy.Token;
    AppendLog('PARTICLES', `Duplicated ${Effect.Name}`, null, 'live');
    ConstructOutliner();
    ConstructInspectorPage();
    C.Broadcast();
}

function ResetAll()
{
    for (const Effect of Scene.Effects) RebuildGpu(Effect);
    Scene.Lightning.bolts = [];
    Scene.Lightning.flash = 0;
    AppendLog('PARTICLES', 'Every effect cleared and re-seeded', null, 'warm');
}

// 📝 Strikes place a burst on any Strike sparks effect, at the ground point.
function ReceiveStrike(Point)
{
    for (const Effect of Scene.Effects)
        if (Effect.PresetId === 'strikeSparks') { Effect.PendingBurst += 160; Effect.OverrideOrigin = Point; }
}

function StrikeNow()
{
    const Hit = Scene.Lightning.strike(Scene.Time);
    ReceiveStrike(Hit);
    AppendLog('LIGHTNING', 'Strike', `at (${Hit.map(Part => Part.toFixed(1)).join(', ')}) m · thunder after ${(Scene.Lightning.distance / 343).toFixed(1)} s`, 'warm', 'Strike');
}

const EffectByToken = Token => Scene.Effects.find(Effect => Effect.Token === Token) || null;
const ForceByToken = Token => Token && Token.startsWith('force:') ? Scene.Forces[Number(Token.slice(6))] || null : null;

//------------------------------------------------------------------------------------------------------------------------
//                                                   CAMERA AND PACKING
//------------------------------------------------------------------------------------------------------------------------

function ResolveCamera(Aspect)
{
    const Camera = Scene.Camera;
    const Cosine = Math.cos(Camera.Pitch), Sine = Math.sin(Camera.Pitch);
    const Eye = [Camera.Target[0] + Camera.Distance * Cosine * Math.sin(Camera.Yaw), Camera.Target[1] + Camera.Distance * Sine,
                 Camera.Target[2] + Camera.Distance * Cosine * Math.cos(Camera.Yaw)];
    const View = ConstructLookAt(Eye, Camera.Target, [0, 1, 0]);
    const Projection = ConstructPerspective((45 * Math.PI) / 180, Aspect, 0.05, 400);
    const Forward = Normalize3(Subtract3(Camera.Target, Eye));
    const Right = Normalize3(Cross3(Forward, [0, 1, 0]));
    return { ViewProjection: MultiplyProjection(Projection, View), Eye, Right, Up: Cross3(Right, Forward) };
}

// 📝 eos buildLines: the domain box, each visible effect's origin cross (amber when selected), molecular boxes, the probe.
function ConstructLines()
{
    const Out = [];
    const Line = (A, B, Colour) => Out.push(A[0], A[1], A[2], ...Colour, B[0], B[1], B[2], ...Colour);
    const Cross = (Centre, Size, Colour) =>
    {
        Line([Centre[0] - Size, Centre[1], Centre[2]], [Centre[0] + Size, Centre[1], Centre[2]], Colour);
        Line([Centre[0], Centre[1] - Size, Centre[2]], [Centre[0], Centre[1] + Size, Centre[2]], Colour);
        Line([Centre[0], Centre[1], Centre[2] - Size], [Centre[0], Centre[1], Centre[2] + Size], Colour);
    };
    const Box = (Low, High, Colour) =>
    {
        const Corners = [[Low[0], Low[1], Low[2]], [High[0], Low[1], Low[2]], [High[0], Low[1], High[2]], [Low[0], Low[1], High[2]],
                         [Low[0], High[1], Low[2]], [High[0], High[1], Low[2]], [High[0], High[1], High[2]], [Low[0], High[1], High[2]]];
        for (const [A, B] of [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]]) Line(Corners[A], Corners[B], Colour);
    };
    if (Scene.Wind.showDomain)
        Box(Domain.Minimum, [Domain.Minimum[0] + Domain.Size[0], Domain.Minimum[1] + Domain.Size[1], Domain.Minimum[2] + Domain.Size[2]], [0.45, 0.6, 0.85, 0.55]);
    for (const Effect of Scene.Effects)
    {
        if (!Effect.P.visible) continue;
        const Origin = Effect.P.origin, Chosen = Selected === Effect.Token;
        Cross(Origin, 0.12, Chosen ? [1, 0.75, 0.25, 1] : [0.6, 0.65, 0.75, 0.6]);
        if (Effect.P.kind === 3 || Effect.P.kind === 4)
        {
            const Half = Effect.P.boxHalf[0];
            Box([Origin[0] - Half, Origin[1] - Half, Origin[2] - Half], [Origin[0] + Half, Origin[1] + Half, Origin[2] + Half], Chosen ? [1, 0.75, 0.25, 0.9] : [0.5, 0.9, 1, 0.35]);
        }
        if (Effect.P.emitShape === 3 && Effect.P.kind < 3)
        {
            const Half = Effect.P.boxHalf;
            Box([Origin[0] - Half[0], Origin[1] - Half[1], Origin[2] - Half[2]], [Origin[0] + Half[0], Origin[1] + Half[1], Origin[2] + Half[2]], [0.6, 0.7, 0.9, 0.3]);
        }
    }
    if (Scene.ProbeEnabled) Cross(Scene.Probe, 0.18, [1, 0.9, 0.3, 1]);
    return new Float32Array(Out);
}

function ResolveProbeVoxel()
{
    const Cell = Domain.Size.map((Size, Axis) => Size / Domain.Dimensions[Axis]);
    return {
        ix: Clamp(Math.floor((Scene.Probe[0] - Domain.Minimum[0]) / Cell[0]), 0, Domain.Dimensions[0] - 1),
        iy: Clamp(Math.floor((Scene.Probe[1] - Domain.Minimum[1]) / Cell[1]), 0, Domain.Dimensions[1] - 1),
        iz: Clamp(Math.floor((Scene.Probe[2] - Domain.Minimum[2]) / Cell[2]), 0, Domain.Dimensions[2] - 1)
    };
}

// 📝 eos packFields: per-effect attractors and magnetic dipoles, then the authored acceleration fields (at most 8).
function PackFields()
{
    const Out = new Float32Array(128);
    let Count = 0;
    const Put = (Type, Position, Field) =>
    {
        if (Count >= 8) return;
        const Offset = (1 + 3 * Count) * 4;
        Out.set([Position[0], Position[1], Position[2], Field.radius], Offset);
        Out.set([Type, Field.strength, Field.swirl || 0, Field.swallow || 0], Offset + 4);
        Out.set([Field.t0 || 0, Field.duration, Field.period || 0, 0], Offset + 8);
        Count++;
    };
    for (const Effect of Scene.Effects)
    {
        const Attractor = Effect.P.attractor;
        if (!Effect.P.visible || !Attractor || !Attractor.enabled) continue;
        Put(0, Effect.P.origin, { radius: Attractor.radius, strength: Attractor.strength, swirl: Attractor.swirl, swallow: Attractor.swallow, t0: 0, duration: 1e9, period: 0 });
    }
    for (const Effect of Scene.Effects)
    {
        const Dipole = Effect.P.magneticField;
        if (!Effect.P.visible || !Dipole) continue;
        Put(3, Effect.P.origin, { radius: Dipole.radius, strength: Dipole.strength, swirl: Dipole.guide, swallow: 0, t0: 0, duration: 1e9, period: 0 });
    }
    for (const Entry of PE.Forces.ForGpu(Scene.Forces, Scene.Time).Forces) Put(Entry.type, Entry.pos, Entry);
    Out[0] = Count;
    Out[1] = Scene.Time;
    return Out;
}

// 📝 eos packLens: black hole lensing and heat shimmer inputs in screen space.
function PackLens(Camera, Aspect)
{
    const Out = new Float32Array(32);
    let Holes = 0, Shimmers = 0;
    const Projection = Camera.ViewProjection;
    const Scale = 0.5 / Math.tan(Math.PI / 8);
    const Project = Origin =>
    {
        const Clip = [0, 1, 2, 3].map(Row => Projection[Row] * Origin[0] + Projection[4 + Row] * Origin[1] + Projection[8 + Row] * Origin[2] + Projection[12 + Row]);
        if (Clip[3] <= 0.01) return null;
        const Separation = Math.hypot(Origin[0] - Camera.Eye[0], Origin[1] - Camera.Eye[1], Origin[2] - Camera.Eye[2]);
        return { U: (Clip[0] / Clip[3]) * 0.5 + 0.5, V: 0.5 - (Clip[1] / Clip[3]) * 0.5, K: Scale / Math.max(Separation, 0.01) };
    };
    for (const Effect of Scene.Effects)
    {
        const Hole = Effect.P.blackHole;
        if (!Effect.P.visible || !Hole || Holes >= 2) continue;
        const Point = Project(Effect.P.origin);
        if (!Point) continue;
        Out.set([Point.U, Point.V, Hole.horizon * (Hole.einstein || 0) * Point.K, Hole.horizon * Point.K], Holes * 4);
        Holes++;
    }
    for (const Effect of Scene.Effects)
    {
        const Shimmer = Effect.P.heatShimmer;
        if (!Effect.P.visible || !Shimmer || Shimmers >= 2) continue;
        const Point = Project(Effect.P.origin);
        if (!Point) continue;
        Out.set([Point.U, Point.V, Shimmer.radius * Point.K, Shimmer.strength], 12 + Shimmers * 8);
        Out.set([Scene.Time * Shimmer.rate, Shimmer.freq, 0, 0], 16 + Shimmers * 8);
        Shimmers++;
    }
    Out[8] = Holes;
    Out[9] = Aspect;
    Out[10] = 0.35;
    Out[11] = Shimmers;
    return { Count: Holes, ShimmerCount: Shimmers, Packed: Out };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE TICK (eos frame)
//------------------------------------------------------------------------------------------------------------------------

let LastTick = 0, LiveTimer = 0, CpuAccumulated = 0, CpuTicks = 0, Held = false, ReadbackTimer = 0;

function Tick(Now)
{
    if (Held) return;
    requestAnimationFrame(Tick);
    if (document.hidden) { LastTick = 0; return; }
    Advance(Now);
}

function Advance(Now)
{
    const Started = performance.now();
    const RealDelta = LastTick ? Math.min(0.05, (Now - LastTick) / 1000) : 0;
    LastTick = Now;
    if (RealDelta > 0) Scene.Rate = Scene.Rate * 0.9 + (1 / RealDelta) * 0.1;
    const Delta = Scene.Playing ? RealDelta * Scene.TimeScale : 0;
    Scene.Time += Delta;
    Scene.Step++;

    for (const Hit of Scene.Lightning.update(Delta, Scene.Time)) ReceiveStrike(Hit);
    if (Scene.Web.Enabled && Scene.Time >= Scene.Web.NextAt)
    {
        const Origins = Scene.Effects.filter(Effect => Effect.P.visible).map(Effect => [Effect.P.origin[0], Effect.P.origin[1], Effect.P.origin[2]]);
        if (Origins.length >= 2) Scene.Lightning.web(Scene.Time, Origins, { k: Scene.Web.Links, life: Scene.Web.Interval * 0.9 });
        Scene.Web.NextAt = Scene.Time + Scene.Web.Interval;
    }

    const Engine = Scene.Engine;
    const Canvas = Engine.canvas;
    const Aspect = Canvas.width / Math.max(1, Canvas.height);
    const Camera = ResolveCamera(Aspect);
    const Global = new Float32Array(52);
    const Offsets = PE.GLOB_OFF;
    Global.set(Camera.ViewProjection, Offsets.viewProj);
    Global.set([Camera.Right[0], Camera.Right[1], Camera.Right[2], 0], Offsets.camRight);
    Global.set([Camera.Up[0], Camera.Up[1], Camera.Up[2], 0], Offsets.camUp);
    Global.set([Camera.Eye[0], Camera.Eye[1], Camera.Eye[2], 1], Offsets.camPos);
    Global.set([Domain.Minimum[0], Domain.Minimum[1], Domain.Minimum[2], 0], Offsets.windMin);
    Global.set([Domain.Size[0], Domain.Size[1], Domain.Size[2], 0], Offsets.windSize);
    const Flow = PE.Forces.ForGpu(Scene.Forces).Lattice;
    Global.set([Domain.Dimensions[0], Domain.Dimensions[1], Domain.Dimensions[2], Flow.length], Offsets.windDim);
    Global.set([Scene.Time, Delta, Scene.Step, Scene.Wind.windScale], Offsets.timing);
    Global.set([Scene.Wind.arrowRef, 0.012, Scene.Lightning.flash, Scene.Wind.turbulence], Offsets.viz);
    Global.set([Scene.Wind.swirl, 0.45, 0.22, Scene.Wind.arrowStride || 3], Offsets.swirl);
    const Components = new Float32Array(PE.WIND.maxComps * 8);
    Flow.slice(0, PE.WIND.maxComps).forEach((Component, Index) =>
    {
        Components.set([Component.x, Component.z, Math.max(0.5, Component.radius), Component.type, Component.strength, (Component.bearing * Math.PI) / 180,
                        Component.freq, Component.enabled ? 1 : 0], Index * 8);
    });

    const Jobs = [];
    for (const Effect of Scene.Effects)
    {
        if (!Effect.P.visible || !Effect.Gpu) continue;
        const P = Effect.P, Gpu = Effect.Gpu;
        if (P.kind === 7)
        {
            // 📝 Light fibres are analytic: a uniform and (for trails) a path table, no simulation.
            const Fibre = P.fibre;
            const Key = Fibre.pathShape + '|' + Fibre.pathSize;
            let Path = null;
            if (Gpu.pathKey !== Key) { Gpu.pathKey = Key; Path = PE.samplePath(Fibre.pathShape, Fibre.pathSize, PE.PATH_SAMPLES); }
            PE.fillFibre(Gpu.fibreCpu, P, { lf: (Scene.Time / Fibre.loopSeconds) % 1, projScale: (0.5 * Canvas.height) / Math.tan(Math.PI / 8), viewW: Canvas.width, viewH: Canvas.height });
            Jobs.push({ gpu: Gpu, cpu: Gpu.fibreCpu, path: Path, fibre: true, simulate: false, draw: false, strands: Fibre.strands, segments: Fibre.segments,
                        sparks: Fibre.sparks > 0, alpha: false, mol: false, emitN: 0, steps: 0, gd: 1 });
            continue;
        }
        const Molecular = P.kind === 3 || P.kind === 4;
        const Simulate = Scene.Playing && Delta > 0;
        let Release = 0, Steps = 0, StepDelta = Delta, Head = Effect.Head, Origin = P.origin;
        if (Simulate)
        {
            if (P.burstEvery > 0)
            {
                Effect.BurstTimer = (Effect.BurstTimer ?? 0) - Delta;
                if (Effect.BurstTimer <= 0)
                {
                    Effect.BurstTimer = P.burstEvery * (0.7 + 0.6 * Math.random());
                    Effect.PendingBurst += P.burstCount;
                    Effect.OverrideOrigin = P.burstAtOrigin ? null : [-3 + 6 * Math.random(), 3.5 + 2.5 * Math.random(), -3 + 6 * Math.random()];
                }
            }
            if (Effect.OverrideOrigin) { Origin = Effect.OverrideOrigin; Effect.OverrideOrigin = null; }
            if (Molecular)
            {
                // 📝 Keep the step at or below ~1.25 × simDt so the Lennard-Jones forces stay stable when the rate drops.
                Steps = Clamp(Math.round(Delta / P.simDt), 1, 8);
                StepDelta = Math.min(Delta / Steps, P.simDt * 1.25);
                if (Effect.NeedFill) { Release = Gpu.cap; Effect.NeedFill = false; Head = 0; }
            }
            else
            {
                Effect.Accumulated += P.rate * Delta;
                Release = Math.floor(Effect.Accumulated);
                Effect.Accumulated -= Release;
                Release = Math.min(Gpu.cap, Release + Effect.PendingBurst);
                Effect.PendingBurst = 0;
                Effect.Head = (Head + Release) % Gpu.cap;
            }
        }
        const Half = P.boxHalf[0];
        const Cutoff = 2.5 * P.sigma;
        const GridDimension = Molecular ? Clamp(Math.floor((2 * Half) / Cutoff), 1, PE.MAX_GD) : 1;
        PE.fillSys(Gpu.cpu, P, { origin: Origin, dt: StepDelta, time: Scene.Time, frame: Scene.Step, cap: Gpu.cap, emitN: Release, head: Head,
                                 seed: (Scene.Step * 7919 + Effect.Number * 104729) >>> 0, mol: Molecular, gd: GridDimension, cellSize: Molecular ? (2 * Half) / GridDimension : 1 });
        Jobs.push({ gpu: Gpu, cpu: Gpu.cpu, simulate: Simulate, mol: Molecular, emitN: Release, steps: Steps, gd: GridDimension, swarm: P.kind === 6,
                    readStats: Simulate && Scene.Step % 3 === 0, draw: true, alpha: P.blend === 'alpha', cube: P.shape === 4,
                    shatter: !!(P.transition && P.transition.fragment), fly: !!(P.transition && P.transition.after === 'butterfly') });
    }

    const Segments = Scene.Lightning.pack(Scene.Time);
    const Lines = ConstructLines();
    const Probe = Scene.ProbeEnabled && !Engine.probePending && Scene.Step % 4 === 0 ? ResolveProbeVoxel() : null;
    const Lens = PackLens(Camera, Aspect);
    Scene.PackedFields = PackFields();
    Engine.render({
        glob: Global, fields: Scene.PackedFields, lensCount: Lens.Count + Lens.ShimmerCount, lens: Lens.Packed, comps: Components,
        clear: ClearColour.map(Channel => Channel + Scene.Lightning.flash * 0.22), floor: Scene.Wind.showFloor, arrows: Scene.Wind.showArrows,
        lines: Lines, lineCount: Lines.length / 7, segCount: Segments, segData: Scene.Lightning.dataBuf, jobs: Jobs, probe: Probe
    });

    // 📝 The overlay: the shared visualization in Camera mode with this frame's view-projection, over the WebGPU canvas.
    if (Now - ReadbackTimer > 150 && Scene.Overlay.Enabled) { ReadbackTimer = Now; Scene.Readback.Request(); }
    RenderOverlay(Camera);

    CpuAccumulated += performance.now() - Started;
    CpuTicks++;
    if (Now - LiveTimer > 250)
    {
        LiveTimer = Now;
        Scene.CpuMilliseconds = CpuTicks ? CpuAccumulated / CpuTicks : 0;
        CpuAccumulated = 0;
        CpuTicks = 0;
        RefreshLive();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FIELD OVERLAY
//------------------------------------------------------------------------------------------------------------------------

const InsideDomain = (X, Y, Z) => X >= Domain.Minimum[0] && X <= Domain.Minimum[0] + Domain.Size[0] && Z >= Domain.Minimum[2] && Z <= Domain.Minimum[2] + Domain.Size[2];

function SampleParticleField(Contribution, X, Y, Z, Time, CellSpan, Out)
{
    if (Contribution === 'Flow') return Scene.Readback ? Scene.Readback.Sample(X, Y, Z, Out) : InsideDomain(X, Y, Z);
    if (Contribution === 'Accelerate') { EvaluateFieldAcceleration(Scene.PackedFields, X, Y, Z, Out); return InsideDomain(X, Y, Z); }
    for (const Field of Scene.Forces)
        if (Field.Kind === 'drag' && Field.Enabled) Out[0] += PE.Forces.SampleField(Field, [X, Y, Z], Time)[0];
    return InsideDomain(X, Y, Z);
}

function DescribeForces()
{
    const List = [];
    Scene.Forces.forEach((Field, Index) =>
    {
        const Kind = PE.Forces.KindById(Field.Kind);
        if (!Kind) return;
        const Contribution = Contributions[Kind.Give];
        const Token = 'force:' + Index;
        const Shape = Field.Reaches === 'everywhere' ? 'Everywhere' : Field.Reaches === 'box' ? 'Box' : 'Circle';
        List.push({ Id: Token, X: Field.Centre[0], Y: Field.Centre[1], Z: Field.Centre[2], Radius: Field.Radius, Shape, HalfX: Field.Radius, HalfZ: Field.Radius,
                    Contribution, Label: Field.Name, Active: Field.Enabled && PE.Forces.Awake(Field, Scene.Time) ? 1 : 0, Selected: Selected === Token,
                    Dim: Contribution !== Scene.Overlay.Contribution, Bearing: Kind.Give === 'flow' && Field.Kind !== 'outflow' && Field.Kind !== 'tornado' ? CompassFromField(Field) : undefined });
    });
    for (const Effect of Scene.Effects)
    {
        if (!Effect.P.visible) continue;
        if (Effect.P.attractor && Effect.P.attractor.enabled)
            List.push({ Id: Effect.Token, X: Effect.P.origin[0], Y: Effect.P.origin[1], Z: Effect.P.origin[2], Radius: Effect.P.attractor.radius, Shape: 'Circle',
                        Contribution: 'Accelerate', Label: `${Effect.Name} · pull`, Active: 1, Selected: Selected === Effect.Token, Dim: Scene.Overlay.Contribution !== 'Accelerate' });
        if (Effect.P.magneticField)
            List.push({ Id: Effect.Token, X: Effect.P.origin[0], Y: Effect.P.origin[1], Z: Effect.P.origin[2], Radius: Effect.P.magneticField.radius, Shape: 'Circle',
                        Contribution: 'Accelerate', Label: `${Effect.Name} · dipole`, Active: 1, Selected: Selected === Effect.Token, Dim: Scene.Overlay.Contribution !== 'Accelerate' });
    }
    return List;
}

function ConstructOverlay()
{
    const Canvas = document.getElementById('OverlayCanvas');
    Visualization = V.Create(Canvas, { Sample: SampleParticleField, OnMinimapPick: (X, Z) =>
    {
        Scene.Camera.Target[0] = Clamp(X, -6, 6);
        Scene.Camera.Target[2] = Clamp(Z, -6, 6);
        AppendLog('VIEW', 'Camera target moved from the minimap', `(${X.toFixed(1)}, ${Z.toFixed(1)}) m`, 'live', 'Camera.Target');
    } });
    Visualization.Configure({
        Extent: { MinimumX: Domain.Minimum[0], MinimumZ: Domain.Minimum[2], MaximumX: Domain.Minimum[0] + Domain.Size[0], MaximumZ: Domain.Minimum[2] + Domain.Size[2] },
        ClipToExtent: true, CameraCells: 40, ShowScaleBar: false, ShowGroundGrid: false, Minimap: document.getElementById('MinimapCanvas'), RefreshInterval: 100
    });
    Visualization.View.Mode = 'Camera';
}

function RenderOverlay(Camera)
{
    const Overlay = Scene.Overlay;
    const Canvas = document.getElementById('OverlayCanvas');
    document.getElementById('MinimapInset').classList.toggle('shown', Overlay.Enabled && Overlay.Minimap);
    if (!Overlay.Enabled)
    {
        if (Canvas.dataset.cleared !== 'yes') { Canvas.getContext('2d').clearRect(0, 0, Canvas.width, Canvas.height); Canvas.dataset.cleared = 'yes'; }
        return;
    }
    Canvas.dataset.cleared = '';
    Visualization.View.ViewProjection = Camera.ViewProjection;
    Visualization.Configure({ Contribution: Overlay.Contribution, Quantity: Overlay.Quantity, Heatmap: Overlay.Heatmap, Arrows: Overlay.Arrows, Tracers: Overlay.Tracers,
                              TracerCount: 220, SliceHeight: Overlay.SliceHeight, Opacity: Overlay.Opacity, ArrowSpacing: Overlay.ArrowSpacing, ShowLabels: Overlay.Labels,
                              Sources: DescribeForces(), ProbePoint: Scene.ProbeEnabled ? [Scene.Probe[0], Scene.Probe[2]] : null,
                              Minimap: Overlay.Minimap ? document.getElementById('MinimapCanvas') : null });
    Visualization.Render(Scene.Time);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LOG STREAM
//------------------------------------------------------------------------------------------------------------------------

const LogEntries = [];
let LogPaused = false;

function AppendLog(Channel, Message, Detail, Tone, Key)
{
    if (LogPaused) return;
    const Now = new Date();
    const Last = LogEntries[0];
    if (Key && Last && Last.Key === Key && Now - Last.When < 1500) Object.assign(Last, { Message, Detail, When: Now });
    else LogEntries.unshift({ Channel, Message, Detail, Tone, Key, When: Now });
    if (LogEntries.length > 80) LogEntries.pop();
    RefreshLog();
}

function RefreshLog()
{
    const Content = document.getElementById('LogContent');
    if (!Content) return;
    Content.textContent = '';
    for (const Entry of LogEntries)
    {
        Content.append(C.Construct('div', 'log-row', [
            C.Construct('span', 'severity' + (Entry.Tone === 'warm' ? ' warm' : '')), C.Construct('span', 'when', Entry.When.toTimeString().slice(0, 8)),
            C.Construct('span', 'channel', Entry.Channel),
            C.Construct('span', 'message', [C.Construct('b', null, Entry.Message), Entry.Detail ? C.Construct('span', null, ' · ' + Entry.Detail) : null])]));
    }
    const Count = document.getElementById('LogCount');
    if (Count) Count.textContent = `${LogEntries.length} entries`;
}

function ConstructLogStream()
{
    const Head = document.getElementById('LogHead');
    const Pause = C.Construct('div', 'round-button', C.ConstructGlyph('Clock'));
    const Clear = C.Construct('div', 'round-button', C.ConstructGlyph('Close'));
    Pause.title = 'Pause';
    Clear.title = 'Clear';
    Pause.addEventListener('click', () => { LogPaused = !LogPaused; Pause.style.color = LogPaused ? 'var(--warm)' : ''; });
    Clear.addEventListener('click', () => { LogEntries.length = 0; RefreshLog(); });
    const Count = C.Construct('span', 'log-count');
    Count.id = 'LogCount';
    Head.append(C.Construct('div', 'log-title', 'Log Stream'), C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, 'LIVE')]),
                C.Construct('span', 'spacer'), Count, Pause, Clear);
    C.SmoothScroll(document.getElementById('LogScroll'), document.getElementById('LogContent'));
    // 📝 Every effect row is funnelled through eos PE.Edits; the log hears the same announcements the host handoff does.
    PE.Edits.Subscribe(Edit =>
    {
        const Effect = Edit.System;
        const Shown = typeof Edit.Value === 'number' ? Number(Edit.Value.toFixed(3)) : Array.isArray(Edit.Value) ? `[${Edit.Value.map(Part => Number(Number(Part).toFixed(2))).join(', ')}]` : String(Edit.Value);
        AppendLog('PARTICLES', `${Effect ? Effect.Name : 'Scene'}: ${Edit.What} ${Shown}`, null, 'live', `${Effect ? Effect.Token : 'scene'}.${Edit.What}`);
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     BINDINGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 An effect parameter row: reads and writes P[Key] (or a nested object's key) and is announced through PE.Edits.
function BindEffect(Effect, Target, Key, Extra)
{
    const Label = Extra.Label || Key;
    const Write = PE.Edits.Through(Label, Quantity => { Target[Key] = Array.isArray(Quantity) ? Quantity.slice() : Quantity; if (Extra.Rebuild) RebuildGpu(Effect); });
    return Object.assign({ Read: () => Target[Key], Write }, Extra);
}

// 📝 A scene row (forces, wind lattice, overlay, lightning): logged directly.
function BindScene(Target, Key, Extra, Channel, Owner)
{
    const Label = Extra.Label || Key;
    return Object.assign({
        Read: () => Target[Key],
        Write: Quantity =>
        {
            Target[Key] = Quantity;
            const Shown = typeof Quantity === 'boolean' ? (Quantity ? 'on' : 'off') : typeof Quantity === 'number' ? `${Number(Quantity.toFixed(Extra.Digits ?? 2))}${Extra.Unit ? ' ' + Extra.Unit : ''}`
                        : Array.isArray(Quantity) ? `[${Quantity.map(Part => Number(Number(Part).toFixed(2))).join(', ')}]` : String(Quantity);
            AppendLog(Channel, `${Owner}: ${Label} ${Shown}`, null, 'live', `${Owner}.${Key}`);
        }
    }, Extra);
}

const Range = (Host, Binding, Minimum, Maximum, Step, Digits, Unit, Extra) => C.SliderRow(Host, Object.assign(Binding, { Minimum, Maximum, Step, Digits, Unit }, Extra || {}));

//------------------------------------------------------------------------------------------------------------------------
//                                                       OUTLINER
//------------------------------------------------------------------------------------------------------------------------

const LiveOutlinerRows = [];

function ConstructTreeRow(Host, Row)
{
    const Meta = C.Construct('div', 'row-meta');
    const Glyph = C.Construct('div', 'row-glyph', C.ConstructGlyph(Row.Glyph));
    const Toggle = C.Construct('button', 'row-toggle', C.ConstructGlyph('Eye'));
    Toggle.type = 'button';
    const Name = C.Construct('div', 'row-name');
    const TreeRow = C.Construct('div', 'tree-row' + (Row.Depth === 1 ? ' nested' : ''), [Glyph, C.Construct('div', 'row-text', [Name, Meta]), Toggle]);
    TreeRow.addEventListener('click', () => { Selected = Row.Id; ConstructInspectorPage(); C.Broadcast(); });
    Toggle.addEventListener('click', Event => { Event.stopPropagation(); Row.Flip(); C.Broadcast(); });
    const Paint = () =>
    {
        const Colour = Row.Colour();
        Glyph.style.color = Colour;
        Glyph.style.background = Colour + '1f';
        Name.textContent = Row.Name();
        const Enabled = Row.Enabled();
        TreeRow.classList.toggle('selected', Selected === Row.Id);
        TreeRow.classList.toggle('muted-row', !Enabled);
        Toggle.textContent = '';
        Toggle.append(C.ConstructGlyph(Enabled ? 'Eye' : 'EyeOff'));
        TreeRow.style.display = OutlinerQuery && Row.Searchable && !Row.Name().toLowerCase().includes(OutlinerQuery) ? 'none' : '';
    };
    C.Register(Paint);
    LiveOutlinerRows.push(() => { Meta.textContent = Row.Meta(); });
    Meta.textContent = Row.Meta();
    Host.append(TreeRow);
}

function DescribeEffectMeta(Effect)
{
    const Kind = CategoryLabels[Effect.P.kind] || 'Particles';
    if (Effect.P.kind === 7) return `${Kind} · ${FormatInteger(Effect.P.fibre.strands)} strands`;
    const Alive = Effect.Gpu && Effect.Gpu.stats_ ? Effect.Gpu.stats_.alive : 0;
    return `${Kind} · ${FormatInteger(Alive)} alive`;
}

function DescribeForceMeta(Field)
{
    const Kind = PE.Forces.KindById(Field.Kind);
    const Contribution = Contributions[Kind.Give];
    return `${Contribution} · ${Number(Field.Strength.toFixed(2))} ${ContributionUnits[Contribution]} · ${Field.Reaches === 'everywhere' ? 'everywhere' : Field.Radius.toFixed(1) + ' m'}`;
}

function ConstructOutliner()
{
    if (OutlinerScope) C.ReleaseScope(OutlinerScope);
    OutlinerScope = C.OpenScope();
    LiveOutlinerRows.length = 0;
    const Content = document.getElementById('OutlinerContent');
    Content.textContent = '';
    const Outliner = C.Construct('div', 'outliner');
    const VisibleCount = C.Construct('div', 'number'), HiddenCount = C.Construct('div', 'number');
    const LibraryButton = C.Construct('div', 'round-button', C.ConstructGlyph('Library'));
    LibraryButton.title = 'Library';
    LibraryButton.addEventListener('click', OpenLibrary);
    const Search = C.Construct('input');
    Search.placeholder = 'Search';
    Search.value = OutlinerQuery;
    Search.addEventListener('input', () => { OutlinerQuery = Search.value.trim().toLowerCase(); C.Broadcast(); });
    const Library = C.Construct('button', 'filter-pill', [C.ConstructGlyph('Library'), C.Construct('span', null, 'Library')]);
    Library.addEventListener('click', OpenLibrary);
    const Records = C.Construct('div', 'muted');
    Outliner.append(
        C.Construct('div', 'outliner-heading', [C.Construct('div', null, [C.Construct('h1', null, 'Outliner'), Records]), C.Construct('div', 'spacer'), LibraryButton]),
        C.Construct('div', 'count-cards', [
            C.Construct('div', 'count-card', [C.Construct('div', 'chip-glyph live', C.ConstructGlyph('Eye')), C.Construct('div', 'label', 'Visible effects'), VisibleCount]),
            C.Construct('div', 'count-card', [C.Construct('div', 'chip-glyph dim', C.ConstructGlyph('EyeOff')), C.Construct('div', 'label', 'Hidden'), HiddenCount])]),
        C.Construct('div', 'search-row', [C.Construct('label', 'search-pill', [C.ConstructGlyph('Search'), Search, C.Construct('kbd', null, 'Ctrl+F')]), Library]));
    C.Register(() =>
    {
        const Visible = Scene.Effects.filter(Effect => Effect.P.visible).length;
        VisibleCount.textContent = String(Visible);
        HiddenCount.textContent = String(Scene.Effects.length - Visible);
        Records.textContent = `Particle scene · ${Scene.Effects.length + Scene.Forces.length + 3} records`;
    });
    const Environment = C.Construct('div', 'tree-group');
    Environment.append(C.Construct('div', 'tree-group-title', [C.ConstructGlyph('Chevron'), C.Construct('span', null, 'Environment'), C.Construct('span', 'count', String(Scene.Forces.length + 3))]));
    ConstructTreeRow(Environment, { Id: 'Field', Name: () => 'Force Field', Glyph: 'Wind', Colour: () => '#8fc7b8', Depth: 0,
                                    Meta: () => `Master · ${Scene.Forces.length} fields · overlay ${Scene.Overlay.Enabled ? 'on' : 'off'}`,
                                    Enabled: () => Scene.Overlay.Enabled, Flip: () => { Scene.Overlay.Enabled = !Scene.Overlay.Enabled; AppendLog('VIEW', `Field overlay ${Scene.Overlay.Enabled ? 'on' : 'off'}`, null, 'live'); } });
    ConstructTreeRow(Environment, { Id: 'Wind', Name: () => 'Wind', Glyph: 'Wind', Colour: () => '#9fc7e8', Depth: 1,
                                    Meta: () => `Guest · lattice 24 × 12 × 24 · ×${Scene.Wind.windScale.toFixed(2)}`,
                                    Enabled: () => Scene.Wind.windScale > 0, Flip: () => { Scene.Wind.windScale = Scene.Wind.windScale > 0 ? 0 : 1; AppendLog('WIND', `Wind scale ×${Scene.Wind.windScale}`, null, 'live'); } });
    Scene.Forces.forEach((Field, Index) =>
    {
        const Kind = PE.Forces.KindById(Field.Kind);
        if (!Kind) return;
        ConstructTreeRow(Environment, { Id: 'force:' + Index, Name: () => Field.Name, Glyph: ForceGlyphs[Field.Kind] || 'Target', Colour: () => ContributionColours[Contributions[Kind.Give]],
                                        Depth: 1, Searchable: true, Meta: () => DescribeForceMeta(Field), Enabled: () => Field.Enabled,
                                        Flip: () => { Field.Enabled = !Field.Enabled; AppendLog('FORCES', `${Field.Name}: ${Field.Enabled ? 'enabled' : 'disabled'}`, null, 'live'); } });
    });
    ConstructTreeRow(Environment, { Id: 'Lightning', Name: () => 'Lightning', Glyph: 'Zap', Colour: () => '#c9b8f0', Depth: 0,
                                    Meta: () => `${FormatInteger(Scene.Lightning.strikes)} strikes · ${Scene.Lightning.auto ? 'automatic' : 'manual'}`,
                                    Enabled: () => Scene.Lightning.auto, Flip: () => { Scene.Lightning.auto = !Scene.Lightning.auto; AppendLog('LIGHTNING', `Automatic strikes ${Scene.Lightning.auto ? 'on' : 'off'}`, null, 'live'); } });
    Outliner.append(Environment);
    const Effects = C.Construct('div', 'tree-group');
    Effects.append(C.Construct('div', 'tree-group-title', [C.ConstructGlyph('Chevron'), C.Construct('span', null, 'Particle effects'), C.Construct('span', 'count', String(Scene.Effects.length))]));
    for (const Effect of Scene.Effects)
    {
        const Preset = PE.presetById(Effect.PresetId);
        ConstructTreeRow(Effects, { Id: Effect.Token, Name: () => Effect.Name, Glyph: GroupGlyphs[Preset.group] || 'Sparkles', Colour: () => DisplayColour(Effect.P.colA), Depth: 0,
                                    Searchable: true, Meta: () => DescribeEffectMeta(Effect), Enabled: () => Effect.P.visible,
                                    Flip: () => { Effect.P.visible = !Effect.P.visible; AppendLog('PARTICLES', `${Effect.Name}: ${Effect.P.visible ? 'shown' : 'hidden'}`, null, 'live'); } });
    }
    const Add = C.Construct('button', 'add-row', [C.ConstructGlyph('Plus'), C.Construct('span', null, 'Add from the library')]);
    Add.type = 'button';
    Add.style.marginLeft = '0';
    Add.style.width = '100%';
    Add.addEventListener('click', OpenLibrary);
    Effects.append(Add);
    Outliner.append(Effects);
    Content.append(Outliner);
    C.CloseScope(OutlinerScope);
    const Status = document.getElementById('OutlinerStatus');
    Status.textContent = '';
    Status.append(C.Construct('div', 'status-cell', [C.Construct('span', null, 'Records'), C.Construct('b', null, String(Scene.Effects.length + Scene.Forces.length + 3))]),
                  C.Construct('div', 'status-cell', [C.Construct('span', null, 'Selected'), C.Construct('b', null, '1')]));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        LIBRARY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The same 52 presets as the eos editor (PE.Presets), browsed by group; a card adds an effect and selects it.
function OpenLibrary()
{
    const Veil = document.getElementById('LibraryVeil');
    const Sheet = document.getElementById('LibrarySheet');
    const Groups = [...new Set(PE.Presets.map(Preset => Preset.group))];
    const Paint = () =>
    {
        Sheet.textContent = '';
        const Close = C.Construct('div', 'round-button', C.ConstructGlyph('Close'));
        Close.addEventListener('click', () => Veil.classList.remove('shown'));
        const Search = C.Construct('input');
        Search.placeholder = 'Search the library';
        Search.value = LibraryQuery;
        Search.addEventListener('input', () => { LibraryQuery = Search.value.trim().toLowerCase(); PaintGrid(); });
        Sheet.append(C.Construct('div', 'library-head', [
            C.Construct('div', null, [C.Construct('h2', null, 'Library'), C.Construct('div', 'muted', `${PE.Presets.length} presets in ${Groups.length} groups · the eos Particle editor’s set`)]),
            C.Construct('div', 'spacer'), C.Construct('label', 'search-pill library-search', [C.ConstructGlyph('Search'), Search]), Close]));
        const Tabs = C.Construct('div', 'library-tabs');
        for (const Group of ['All', ...Groups])
        {
            const Count = Group === 'All' ? PE.Presets.length : PE.Presets.filter(Preset => Preset.group === Group).length;
            const Tab = C.Construct('button', 'library-tab' + (LibraryGroup === Group ? ' active' : ''), [C.Construct('span', null, Group), C.Construct('small', null, String(Count))]);
            Tab.type = 'button';
            Tab.addEventListener('click', () => { LibraryGroup = Group; Paint(); });
            Tabs.append(Tab);
        }
        Sheet.append(Tabs);
        const Grid = C.Construct('div', 'library-grid');
        Sheet.append(Grid);
        const PaintGrid = () =>
        {
            Grid.textContent = '';
            const Shown = PE.Presets.filter(Preset => (LibraryGroup === 'All' || Preset.group === LibraryGroup)
                                                      && (!LibraryQuery || (Preset.name + ' ' + Preset.blurb + ' ' + Preset.group).toLowerCase().includes(LibraryQuery)));
            for (const Preset of Shown)
            {
                const Colour = DisplayColour(Preset.p.colA), Ending = DisplayColour(Preset.p.colB);
                const Glyph = C.Construct('div', 'card-glyph', C.ConstructGlyph(GroupGlyphs[Preset.group] || 'Sparkles'));
                Glyph.style.color = Colour;
                Glyph.style.background = Colour + '22';
                const InScene = Scene.Effects.filter(Effect => Effect.PresetId === Preset.id).length;
                const Swatch = C.Construct('div', 'card-swatch');
                Swatch.style.background = `linear-gradient(90deg, ${Colour}, ${Ending})`;
                const Card = C.Construct('button', 'library-card', [
                    C.Construct('div', 'card-top', [Glyph, C.Construct('div', null, [C.Construct('div', 'card-name', Preset.name),
                        C.Construct('div', 'card-native', Preset.group === (CategoryLabels[Preset.p.kind] || '') ? Preset.group.toUpperCase()
                                                                  : `${Preset.group.toUpperCase()} · ${(CategoryLabels[Preset.p.kind] || 'Particles').toUpperCase()}`)])]),
                    C.Construct('div', 'card-blurb', Preset.blurb), Swatch,
                    InScene ? C.Construct('span', 'card-badge', `${InScene} in scene`) : null]);
                Card.type = 'button';
                Card.addEventListener('click', () => { Veil.classList.remove('shown'); AddEffect(Preset.id); });
                Grid.append(Card);
            }
            if (!Shown.length) Grid.append(C.Construct('div', 'timeline-hint', 'Nothing matches.'));
        };
        PaintGrid();
    };
    Paint();
    Veil.classList.add('shown');
    Veil.onclick = Event => { if (Event.target === Veil) Veil.classList.remove('shown'); };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

let PlayButton = null;

function ConstructViewport()
{
    const Bar = document.getElementById('ViewportBar');
    const Chip = C.Construct('span');
    const Transport = C.Construct('div', 'bar-choice');
    PlayButton = C.Construct('button', null, [C.ConstructGlyph('Pause'), C.Construct('span', null, 'Pause')]);
    PlayButton.type = 'button';
    PlayButton.addEventListener('click', TogglePlay);
    const Reset = C.Construct('button', null, [C.ConstructGlyph('Reset'), C.Construct('span', null, 'Reset')]);
    Reset.type = 'button';
    Reset.addEventListener('click', ResetAll);
    const Strike = C.Construct('button', null, [C.ConstructGlyph('Zap'), C.Construct('span', null, 'Strike')]);
    Strike.type = 'button';
    Strike.addEventListener('click', StrikeNow);
    Transport.append(PlayButton, Reset, Strike);
    const Rates = C.Construct('div', 'bar-choice');
    for (const Rate of [0.25, 0.5, 1, 2])
    {
        const Button = C.Construct('button', null, `×${Rate}`);
        Button.type = 'button';
        Button.addEventListener('click', () => { Scene.TimeScale = Rate; AppendLog('VIEW', `Time ×${Rate}`, null, 'live', 'TimeScale'); C.Broadcast(); });
        C.Register(() => Button.classList.toggle('active', Scene.TimeScale === Rate));
        Rates.append(Button);
    }
    const Views = C.Construct('div', 'bar-choice');
    const Overlay = C.Construct('button', null, [C.ConstructGlyph('Layers'), C.Construct('span', null, 'Field')]);
    Overlay.type = 'button';
    Overlay.title = 'Force-field overlay (V)';
    Overlay.addEventListener('click', () => { Scene.Overlay.Enabled = !Scene.Overlay.Enabled; AppendLog('VIEW', `Field overlay ${Scene.Overlay.Enabled ? 'on' : 'off'}`, null, 'live', 'Overlay'); C.Broadcast(); });
    C.Register(() => Overlay.classList.toggle('active', Scene.Overlay.Enabled));
    const Top = C.Construct('button', null, [C.ConstructGlyph('Map'), C.Construct('span', null, 'Top')]);
    Top.type = 'button';
    Top.addEventListener('click', ToggleTopView);
    PersistentRefreshers.push(() => Top.classList.toggle('active', Scene.Camera.Pitch > 1.2));
    Views.append(Overlay, Top);
    Bar.append(C.Construct('div', 'scene-glyph', C.ConstructGlyph('Sparkles')),
               C.Construct('div', null, [C.Construct('div', 'scene-title', 'Particle scene'), C.Construct('div', 'scene-meta', 'eos WebGPU engine')]),
               C.Construct('div', 'spacer'), C.Construct('div', 'renderer-chip', [C.Construct('i'), Chip]), Transport, Rates, Views);
    PersistentRefreshers.push(() => { Chip.textContent = `WEBGPU · ${Scene.Playing ? 'LIVE' : 'PAUSED'}`; });
    document.getElementById('ViewportCaption').textContent = 'Drag to orbit · right-drag pans · wheel zooms · Space pauses · L strikes · V field overlay';
    const Status = document.getElementById('ViewportStatus');
    const Adapter = C.Construct('b'), Readback = C.Construct('b');
    Status.append(C.Construct('span', null, 'HTML prototype'), C.Construct('span', null, ['Adapter ', Adapter]), C.Construct('span', 'spacer'),
                  C.Construct('span', null, ['Lattice readback ', Readback]));
    PersistentRefreshers.push(() =>
    {
        Adapter.textContent = Scene.AdapterName;
        const Transfer = Scene.Readback ? Scene.Readback.Status : null;
        Readback.textContent = Transfer && Transfer.Count ? `${(Transfer.Bytes / 1024).toFixed(0)} KB · ${Transfer.Milliseconds.toFixed(1)} ms · ${FormatInteger(Transfer.Count)}×` : '—';
    });
    document.getElementById('MinimapLabel').append(C.Construct('span', null, 'Lattice domain'), C.Construct('span', null, '12 × 12 m'));
}

function ToggleTopView()
{
    const Camera = Scene.Camera;
    if (Camera.Pitch > 1.2) { Camera.Pitch = 0.3; Camera.Yaw = 0.6; } else { Camera.Pitch = 1.45; Camera.Yaw = 0; }
    AppendLog('VIEW', Camera.Pitch > 1.2 ? 'Top view' : 'Orbit view', null, 'live', 'Camera.View');
}

function TogglePlay()
{
    Scene.Playing = !Scene.Playing;
    PlayButton.textContent = '';
    PlayButton.append(C.ConstructGlyph(Scene.Playing ? 'Pause' : 'Play'), C.Construct('span', null, Scene.Playing ? 'Pause' : 'Play'));
    AppendLog('VIEW', Scene.Playing ? 'Playing' : 'Paused', `t ${Scene.Time.toFixed(1)} s`, Scene.Playing ? 'live' : 'warm', 'Playing');
}

function RefreshHud()
{
    const Hud = document.getElementById('ViewportHud');
    const Total = Scene.Effects.filter(Effect => Effect.P.visible).reduce((Sum, Effect) => Sum + (Effect.Gpu.stats_ ? Effect.Gpu.stats_.alive : 0), 0);
    const Capacity = Scene.Effects.filter(Effect => Effect.P.visible && Effect.P.kind !== 7).reduce((Sum, Effect) => Sum + Effect.Gpu.cap, 0);
    Hud.textContent = '';
    Hud.append(C.Construct('div', null, [C.Construct('span', null, 'PARTICLES '), document.createTextNode(`${FormatInteger(Total)} / ${FormatInteger(Capacity)}`)]),
               C.Construct('div', null, [C.Construct('span', null, 'RATE '), document.createTextNode(`${Math.round(Scene.Rate)} fps`)]),
               C.Construct('div', null, [C.Construct('span', null, 'CPU '), document.createTextNode(`${Scene.CpuMilliseconds.toFixed(2)} ms`)]),
               C.Construct('div', null, [C.Construct('span', null, 't '), document.createTextNode(`${Scene.Time.toFixed(1)} s`)]));
    const Readout = document.getElementById('ViewportReadout');
    const Result = Scene.Engine.probeResult;
    Readout.classList.toggle('shown', Scene.ProbeEnabled && !!Result);
    if (Result)
    {
        const Speed = Math.hypot(Result.value[0], Result.value[2]);
        Readout.textContent = [`PROBE (${Scene.Probe.map(Part => Part.toFixed(1)).join(', ')}) m · voxel ${Result.at.join(', ')}`,
                               `wind ${Result.value.map(Part => Part.toFixed(2)).join('  ')} m/s`,
                               `|v| ${Result.mag.toFixed(2)} · horizontal ${Speed.toFixed(2)} m/s`,
                               `readback ${Result.ms.toFixed(2)} ms · ${Result.bytes} B`].join('\n');
    }
}

// 📝 eos setupViewport: drag orbits, right-drag (or Shift) pans, the wheel zooms.
function AttachOrbit()
{
    const Canvas = document.getElementById('ViewportCanvas');
    let Drag = null;
    Canvas.addEventListener('contextmenu', Event => Event.preventDefault());
    Canvas.addEventListener('pointerdown', Event => { Drag = { X: Event.clientX, Y: Event.clientY, Pan: Event.button === 2 || Event.shiftKey || Event.button === 1 }; Canvas.setPointerCapture(Event.pointerId); });
    Canvas.addEventListener('pointermove', Event =>
    {
        if (!Drag) return;
        const DeltaX = Event.clientX - Drag.X, DeltaY = Event.clientY - Drag.Y;
        Drag.X = Event.clientX;
        Drag.Y = Event.clientY;
        const Camera = Scene.Camera;
        if (Drag.Pan)
        {
            const Scale = Camera.Distance * 0.0015;
            Camera.Target[0] += -DeltaX * Math.cos(Camera.Yaw) * Scale;
            Camera.Target[2] += DeltaX * Math.sin(Camera.Yaw) * Scale;
            Camera.Target[1] += DeltaY * Scale;
        }
        else
        {
            Camera.Yaw -= DeltaX * 0.005;
            Camera.Pitch = Clamp(Camera.Pitch + DeltaY * 0.005, -1.45, 1.45);
        }
    });
    const Release = () => { Drag = null; };
    Canvas.addEventListener('pointerup', Release);
    Canvas.addEventListener('pointercancel', Release);
    Canvas.addEventListener('wheel', Event => { Event.preventDefault(); Scene.Camera.Distance = Clamp(Scene.Camera.Distance * Math.exp(Event.deltaY * 0.001), 2, 60); }, { passive: false });
}

function ResizeCanvas()
{
    const Canvas = document.getElementById('ViewportCanvas');
    const Bounds = Canvas.parentElement.getBoundingClientRect();
    const Ratio = Math.min(window.devicePixelRatio || 1, 2);
    const Width = Math.max(64, Math.floor(Bounds.width * Ratio)), Height = Math.max(64, Math.floor(Bounds.height * Ratio));
    if (Canvas.width !== Width || Canvas.height !== Height)
    {
        Canvas.width = Width;
        Canvas.height = Height;
        if (Scene.Engine) Scene.Engine.resize(Width, Height);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INSPECTOR HELPERS
//------------------------------------------------------------------------------------------------------------------------

function ConstructSection(Page, Id, Title, Glyph, Tab, Status)
{
    const Section = C.SectionCard(Page, { Id, Title, Glyph, Status });
    Sections.push({ Id, Tab, Card: Section.Card });
    return Section.Inner;
}

const Caption = (Host, Text) => Host.append(C.Construct('p', 'section-caption', Text));
const SubHeading = (Host, Text) => Host.append(C.Construct('div', 'sub-heading', Text));

function RevealSection(Id)
{
    const Entry = Sections.find(Section => Section.Id === Id);
    if (!Entry || !InspectorScroller) return;
    Entry.Card.classList.remove('folded');
    InspectorScroller.ScrollTo(Entry.Card.offsetTop - 52);
}

function ConstructSectionTabs(Page)
{
    const Bar = C.Construct('div', 'section-tabs');
    Page.append(Bar);
    return () =>
    {
        for (const Entry of Sections)
        {
            const Tab = C.Construct('button', 'section-tab', Entry.Tab);
            Tab.type = 'button';
            Tab.dataset.section = Entry.Id;
            Tab.addEventListener('click', () => RevealSection(Entry.Id));
            Bar.append(Tab);
        }
        const Scroller = document.getElementById('InspectorScroll');
        const Spy = () =>
        {
            let Current = Sections[0]?.Id;
            for (const Entry of Sections) if (Entry.Card.offsetTop - 70 <= Scroller.scrollTop) Current = Entry.Id;
            Bar.querySelectorAll('.section-tab').forEach(Tab =>
            {
                const Active = Tab.dataset.section === Current;
                if (Active && !Tab.classList.contains('active')) Bar.scrollTo({ left: Tab.offsetLeft - (Bar.clientWidth - Tab.clientWidth) / 2, behavior: 'smooth' });
                Tab.classList.toggle('active', Active);
            });
        };
        Scroller.onscroll = Spy;
        Spy();
    };
}

function ConstructHeading(Page, Trail, Title, Pill, Subtitle)
{
    Page.append(C.Construct('div', 'breadcrumb-row', [
        C.Construct('div', 'breadcrumb', [document.createTextNode('Inspector / '), C.Construct('b', null, Trail)]),
        C.Construct('button', 'notes-pill', [C.ConstructGlyph('Plus'), C.Construct('span', null, 'Add notes')])]));
    const Heading = C.Construct('h2', null, Title);
    Page.append(C.Construct('div', 'page-title', [Heading, Pill]), C.Construct('div', 'page-subtitle', Subtitle));
    return Heading;
}

// 📝 InspectorControls.ReadoutTile's markup, refreshed on the live interval as well as on broadcasts.
function LiveReadoutTile(Host, Binding)
{
    const Reading = C.Construct('div', 'tile-value');
    Host.append(C.Construct('div', 'readout-tile', [C.Construct('div', 'tile-glyph', C.ConstructGlyph(Binding.Glyph)), Reading, C.Construct('div', 'tile-label', Binding.Label)]));
    const Refresh = () => { const [Text, Small] = Binding.Resolve(); Reading.textContent = Text; if (Small) Reading.append(C.Construct('small', null, Small)); };
    C.Register(Refresh);
    LiveRefreshers.push(Refresh);
}

function LiveStatGrid(Host, Cells)
{
    const Grid = C.Construct('div', 'stat-grid');
    for (const [Label, Resolve] of Cells)
    {
        const Reading = C.Construct('b');
        Grid.append(C.Construct('div', 'stat-cell', [Reading, C.Construct('span', null, Label)]));
        const Refresh = () => { Reading.textContent = Resolve(); };
        C.Register(Refresh);
        LiveRefreshers.push(Refresh);
    }
    Host.append(Grid);
}

function ConstructRecordBlock(Page, Title, CaptionText, Lines, Existing)
{
    Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', Title), C.Construct('div', 'caption', CaptionText)]));
    const Block = C.Construct('pre', 'levelspace');
    C.ActionRow(Page, [{ Label: 'Copy record', Glyph: 'Copy', Act: () =>
    {
        const Text = Lines().join('\n');
        (navigator.clipboard ? navigator.clipboard.writeText(Text) : Promise.reject()).then(() => C.Toast('Record copied'), () => C.Toast('Copy unavailable here'));
    } }]);
    Page.append(Block);
    C.Register(() =>
    {
        Block.textContent = '';
        for (const Line of Lines())
        {
            const Split = Line.indexOf(' = ');
            if (Split < 0) { Block.append(C.Construct('span', 'comment', Line), '\n'); continue; }
            const Key = Line.slice(0, Split);
            Block.append(C.Construct('span', 'key', Key), Line.slice(Split));
            if (!Existing.has(Key)) Block.append(C.Construct('span', 'comment', '   # proposed'));
            Block.append('\n');
        }
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     EFFECT PAGE
//------------------------------------------------------------------------------------------------------------------------

function ConstructEffectPage(Page, Effect)
{
    const P = Effect.P;
    const Preset = PE.presetById(Effect.PresetId);
    const Molecular = P.kind === 3 || P.kind === 4, Fibre = P.kind === 7;
    PE.Edits.For(Effect);
    const Heading = ConstructHeading(Page, `Particle effects / ${Preset.group}`, Effect.Name, C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, (CategoryLabels[P.kind] || 'Particles').toUpperCase())]),
                                     Preset.blurb);
    C.Register(() => { Heading.textContent = Effect.Name; });
    const Mark = C.Construct('div', 'mark live', C.ConstructGlyph('Check'));
    const MarkText = C.Construct('span');
    const Metric = C.Construct('div', 'metric-big');
    const Rate = C.Construct('div', 'rate');
    const Low = C.Construct('div', 'low');
    const Card = C.Construct('div', 'hero-card', [
        C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [Mark, MarkText]), C.Construct('div', 'estimate-tag', Fibre ? 'Analytic · no readback' : 'GPU readback · 32 B')]),
        C.Construct('div', 'hero-title', Fibre ? 'Strands drawn' : 'Particles alive'),
        C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])]);
    Page.append(Card);
    const Series = C.TimeSeries(Card, { Samples: Effect.AliveSamples, Capacity: SampleCapacity, Target: () => Fibre ? P.fibre.strands : Effect.Gpu.cap, Height: 112, Unit: '',
                                        TargetLabel: Fibre ? 'Strands' : 'Capacity', TargetDigits: 0, Headroom: 1.12 });
    LiveRefreshers.push(Series.Refresh);
    LiveRefreshers.push(() =>
    {
        const Alive = Fibre ? P.fibre.strands : Effect.Gpu.stats_ ? Effect.Gpu.stats_.alive : 0;
        Metric.textContent = FormatInteger(Alive);
        Rate.textContent = Fibre ? `${P.fibre.loopSeconds.toFixed(0)} s` : Molecular ? 'fill' : `${FormatInteger(P.rate)}`;
        Rate.append(C.Construct('small', null, Fibre ? 'loop' : Molecular ? 'kept alive' : '/s'));
        Low.textContent = Fibre ? `${FormatInteger(P.fibre.segments)} segments` : `of ${FormatInteger(Effect.Gpu.cap)} capacity`;
        const Share = Fibre ? 0 : Alive / Math.max(1, Effect.Gpu.cap);
        const Full = !Molecular && !Fibre && Share >= 0.98;
        Mark.className = 'mark ' + (!P.visible ? 'warm' : Full ? 'warm' : 'live');
        Mark.textContent = '';
        Mark.append(C.ConstructGlyph(!P.visible ? 'EyeOff' : Full ? 'Alert' : 'Check'));
        MarkText.textContent = !P.visible ? 'Hidden' : !Scene.Playing ? 'Paused' : Full ? 'At capacity' : Molecular ? 'Every particle alive' : Fibre ? 'Drawn analytically' : 'Within capacity';
    });
    C.ChipRow(Page, [
        () => [(CategoryLabels[P.kind] || 'Particles').split(' · ')[0], 'kind'],
        () => [P.blend === 'alpha' ? 'Alpha' : 'Additive', 'blend'],
        () => [['Streak', 'Sphere', 'Leaf', 'Glow', 'Cube', 'Coin', 'Bubble'][P.shape] || '—', 'shape'],
        () => [FormatInteger(Fibre ? P.fibre.strands : Effect.Gpu.cap), Fibre ? 'strands' : 'capacity']]);
    const Tiles = C.Construct('div', 'tile-grid');
    LiveReadoutTile(Tiles, { Label: 'Readback latency', Glyph: 'Clock', Resolve: () => [Effect.Gpu.readbackMs ? Effect.Gpu.readbackMs.toFixed(2) : '—', 'ms'] });
    LiveReadoutTile(Tiles, { Label: 'Readbacks', Glyph: 'Activity', Resolve: () => [FormatInteger(Effect.Gpu.readbackCount), '× 32 B'] });
    if (Molecular) LiveReadoutTile(Tiles, { Label: P.kind === 4 ? 'Species A · B · C' : 'Temperature ⟨v²⟩ / 3', Glyph: 'Atom', Resolve: () =>
    {
        const Stats = Effect.Gpu.stats_;
        if (!Stats) return ['—', ''];
        return P.kind === 4 ? [`${FormatInteger(Stats.A)} · ${FormatInteger(Stats.B)} · ${FormatInteger(Stats.C)}`, ''] : [(Stats.alive ? Stats.energy / Stats.alive / 3 : 0).toFixed(3), 'ε'];
    } });
    else C.ReadoutTile(Tiles, { Label: 'Wind coupling', Glyph: 'Wind', Resolve: () => [P.windCoupling.toFixed(2), '1/s'] });
    C.ReadoutTile(Tiles, { Label: Molecular ? 'Box half extent' : 'Gravity', Glyph: Molecular ? 'Boxes' : 'Gravity', Resolve: () => Molecular ? [P.boxHalf[0].toFixed(2), 'm'] : [P.gravity.toFixed(2), 'g'] });
    Page.append(Tiles);
    Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Quick controls'), C.Construct('div', 'caption', 'This effect')]));
    const Grid = C.Construct('div', 'toggle-grid');
    C.ToggleTile(Grid, BindEffect(Effect, P, 'visible', { Label: 'Visible', Glyph: 'Eye' }));
    C.ToggleTile(Grid, { Label: 'Wind-driven', Glyph: 'Wind', Read: () => P.windCoupling > 0,
                         Write: PE.Edits.Through('Wind coupling', Enabled => { P.windCoupling = Enabled ? (Effect.StoredCoupling || 1) : (Effect.StoredCoupling = P.windCoupling, 0); }),
                         Availability: () => Molecular || Fibre ? { Enabled: false, Reason: Fibre ? 'fibres ignore wind' : 'atoms ignore wind' } : { Enabled: true } });
    C.ToggleTile(Grid, { Label: 'Additive glow', Glyph: 'Sparkles', Read: () => P.blend === 'add', Write: PE.Edits.Through('Blend', Enabled => { P.blend = Enabled ? 'add' : 'alpha'; }),
                         Availability: () => Fibre ? { Enabled: false, Reason: 'fibres glow' } : { Enabled: true } });
    C.ToggleTile(Grid, { Label: 'Floor bounce', Glyph: 'Activity', Read: () => P.bounce > 0, Write: PE.Edits.Through('Bounce', Enabled => { P.bounce = Enabled ? (Effect.StoredBounce || 0.4) : (Effect.StoredBounce = P.bounce, 0); }),
                         Availability: () => Molecular || Fibre ? { Enabled: false, Reason: 'not simulated against the floor' } : { Enabled: true } });
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Enabled', { Label: 'Field overlay', Glyph: 'Layers' }, 'VIEW', 'Overlay'));
    C.ToggleTile(Grid, BindScene(Scene.Wind, 'showFloor', { Label: 'Floor', Glyph: 'Lattice' }, 'VIEW', 'Floor'));
    Page.append(Grid);
    C.ActionRow(Page, [
        { Label: 'Burst', Glyph: 'Sparkles', Tone: 'accent', Act: () => { Effect.PendingBurst += P.transition ? Effect.Gpu.cap : Math.max(60, Math.round(Effect.Gpu.cap * 0.4)); Effect.OverrideOrigin = null; AppendLog('PARTICLES', `${Effect.Name}: burst`, null, 'live'); },
          Availability: () => Fibre ? { Enabled: false, Reason: 'fibres are analytic' } : { Enabled: true } },
        { Label: Molecular ? 'Re-seed' : 'Clear', Glyph: 'Reset', Act: () => { RebuildGpu(Effect); AppendLog('PARTICLES', `${Effect.Name}: ${Molecular ? 're-seeded' : 'cleared'}`, null, 'live'); } },
        { Label: 'Duplicate', Glyph: 'Copy', Act: () => DuplicateEffect(Effect) },
        { Label: 'Delete', Glyph: 'Trash', Tone: 'danger', Act: () => RemoveEffect(Effect) }]);
    const ConstructTabs = ConstructSectionTabs(Page);
    if (Fibre) ConstructFibreSections(Page, Effect);
    else
    {
        ConstructSourceSection(Page, Effect, Molecular);
        ConstructAppearanceSection(Page, Effect, Molecular);
        if (!Molecular) ConstructMotionSection(Page, Effect);
        if (P.transition) ConstructTransitionSection(Page, Effect);
        if (P.blackHole && P.attractor) ConstructBlackHoleSection(Page, Effect);
        if (P.heatShimmer) ConstructShimmerSection(Page, Effect);
        if (P.magneticField) ConstructDipoleSection(Page, Effect);
        if (Molecular) ConstructPhysicsSection(Page, Effect);
        ConstructReadbackSection(Page, Effect, Molecular);
    }
    ConstructRecordBlock(Page, 'Levelspace record', 'ParticleEffect · proposed', () => EncodeEffect(Effect), new Set(['identity', 'type', 'name', 'enabled']));
    ConstructTabs();
}

function ConstructSourceSection(Page, Effect, Molecular)
{
    const P = Effect.P;
    const Inner = ConstructSection(Page, 'source', 'Source', 'Target', 'Source', () => Molecular ? `fill ${FormatInteger(P.capacity)}` : `${FormatInteger(P.rate)} /s`);
    Caption(Inner, Molecular ? 'Molecular effects fill once and keep every particle alive; the box is the reflecting container.' : 'Where particles start, how many each second, and how they leave the source.');
    C.TextRow(Inner, BindEffect(Effect, Effect, 'Name', { Label: 'Name' }));
    const Capacities = [384, 512, 1024, 1536, 2048, 4096, 8192];
    if (!Capacities.includes(P.capacity)) Capacities.push(P.capacity);
    Capacities.sort((A, B) => A - B);
    C.SelectRow(Inner, BindEffect(Effect, P, 'capacity', { Label: 'Capacity', Options: Capacities.map(Count => [FormatInteger(Count), Count]), Rebuild: true }));
    if (!Molecular) Range(Inner, BindEffect(Effect, P, 'rate', { Label: 'Rate' }), 0, 2000, 1, 0, '/s');
    C.SelectRow(Inner, BindEffect(Effect, P, 'emitShape', { Label: 'Shape', Options: [['Point', 0], ['Sphere volume', 1], ['Disc', 2], ['Box', 3], ['Box surface grid', 4]] }));
    C.VectorRow(Inner, BindEffect(Effect, P, 'origin', { Label: 'Origin', Unit: 'm', Step: 0.02, Digits: 2, Minimum: -20, Maximum: 20 }));
    Range(Inner, BindEffect(Effect, P, 'radius', { Label: 'Radius' }), 0, 8, 0.01, 2, 'm');
    if (Molecular) C.SliderRow(Inner, Object.assign(BindEffect(Effect, P, 'boxHalf', { Label: 'Box half extent' }), {
        Read: () => P.boxHalf[0], Write: PE.Edits.Through('Box half extent', Reading => { P.boxHalf = [Reading, Reading, Reading]; }), Minimum: 0.2, Maximum: 4, Step: 0.01, Digits: 2, Unit: 'm' }));
    else
    {
        C.VectorRow(Inner, BindEffect(Effect, P, 'boxHalf', { Label: 'Box half extents', Unit: 'm', Step: 0.02, Digits: 1, Minimum: 0, Maximum: 10 }));
        C.VectorRow(Inner, BindEffect(Effect, P, 'dir', { Label: 'Direction', Step: 0.01, Digits: 2, Minimum: -1, Maximum: 1 }));
        Range(Inner, BindEffect(Effect, P, 'spread', { Label: 'Spread' }), 0, 3.1416, 0.01, 2, 'rad');
        Range(Inner, Object.assign(BindEffect(Effect, P, 'speedMin', { Label: 'Speed min' }), { Write: PE.Edits.Through('Speed min', Reading => { P.speedMin = Math.min(Reading, P.speedMax); }) }), 0, 20, 0.05, 2, 'm/s');
        Range(Inner, Object.assign(BindEffect(Effect, P, 'speedMax', { Label: 'Speed max' }), { Write: PE.Edits.Through('Speed max', Reading => { P.speedMax = Math.max(Reading, P.speedMin); }) }), 0, 20, 0.05, 2, 'm/s');
        Range(Inner, Object.assign(BindEffect(Effect, P, 'lifeMin', { Label: 'Life min' }), { Write: PE.Edits.Through('Life min', Reading => { P.lifeMin = Math.min(Reading, P.lifeMax); }) }), 0.05, 20, 0.05, 2, 's');
        Range(Inner, Object.assign(BindEffect(Effect, P, 'lifeMax', { Label: 'Life max' }), { Write: PE.Edits.Through('Life max', Reading => { P.lifeMax = Math.max(Reading, P.lifeMin); }) }), 0.05, 20, 0.05, 2, 's');
        if (P.burstEvery > 0 || P.burstCount)
        {
            SubHeading(Inner, 'Timed bursts');
            Range(Inner, BindEffect(Effect, P, 'burstEvery', { Label: 'Burst every · 0 = off' }), 0, 30, 0.1, 1, 's');
            Range(Inner, BindEffect(Effect, P, 'burstCount', { Label: 'Particles per burst' }), 0, 4096, 1, 0, '');
            C.SwitchRow(Inner, BindEffect(Effect, P, 'burstAtOrigin', { Label: 'Burst at the origin', Detail: 'Off: each burst fires from a random point in the sky (fireworks).' }));
        }
    }
}

function ConstructAppearanceSection(Page, Effect, Molecular)
{
    const P = Effect.P;
    const Inner = ConstructSection(Page, 'appearance', 'Particle', 'Palette', 'Particle', () => P.blend === 'alpha' ? 'alpha' : 'additive');
    if (!Molecular)
    {
        C.SelectRow(Inner, BindEffect(Effect, P, 'shape', { Label: 'Shape', Options: [['Streak', 0], ['Sphere', 1], ['Leaf / paper', 2], ['Soft glow', 3], ['Chip cube', 4], ['Coin', 5], ['Bubble', 6]] }));
        if (P.kind === 1) C.SegmentedRow(Inner, BindEffect(Effect, P, 'leafMode', { Label: 'Leaf mode', Options: [['Leaf', 0], ['Paper', 1]] }));
    }
    C.SegmentedRow(Inner, BindEffect(Effect, P, 'blend', { Label: 'Blend', Options: [['Additive glow', 'add'], ['Alpha · premultiplied', 'alpha']] }));
    if (!Molecular)
    {
        Range(Inner, BindEffect(Effect, P, 'sizeStart', { Label: 'Size at birth' }), 0.002, 2, 0.001, 3, 'm');
        Range(Inner, BindEffect(Effect, P, 'sizeEnd', { Label: 'Size at death' }), 0.002, 3, 0.001, 3, 'm');
    }
    Range(Inner, BindEffect(Effect, P, 'sizeScale', { Label: 'Size scale' }), 0.1, 4, 0.01, 2, '×');
    C.ColourRow(Inner, BindEffect(Effect, P, 'colA', { Label: Molecular ? 'Colour A' : 'Colour at birth', Alpha: true }));
    C.ColourRow(Inner, BindEffect(Effect, P, 'colB', { Label: Molecular ? 'Colour B' : 'Colour at death', Alpha: true }));
    if (P.kind === 4) C.ColourRow(Inner, BindEffect(Effect, P, 'colC', { Label: 'Colour C (product)', Alpha: true }));
}

function ConstructMotionSection(Page, Effect)
{
    const P = Effect.P;
    const Inner = ConstructSection(Page, 'motion', 'Motion', 'Wind', 'Motion', () => `${P.windCoupling.toFixed(2)} /s coupling`);
    Caption(Inner, 'How the effect answers the Force Field: wind coupling is how fast a particle relaxes to the flow (Flow fields); gravity, buoyancy and the scene’s acceleration fields are added straight to velocity.');
    Range(Inner, BindEffect(Effect, P, 'windCoupling', { Label: 'Wind coupling' }), 0, 5, 0.01, 2, '1/s');
    Range(Inner, BindEffect(Effect, P, 'drag', { Label: 'Drag' }), 0, 5, 0.01, 2, '1/s');
    Range(Inner, BindEffect(Effect, P, 'gravity', { Label: 'Gravity' }), -1, 2, 0.01, 2, 'g');
    Range(Inner, BindEffect(Effect, P, 'buoyancy', { Label: 'Buoyancy' }), -4, 6, 0.05, 2, 'm/s²');
    Range(Inner, BindEffect(Effect, P, 'bounce', { Label: 'Bounce' }), 0, 1, 0.01, 2, '');
    Range(Inner, BindEffect(Effect, P, 'flutter', { Label: 'Flutter' }), 0, 5, 0.05, 2, 'm/s²');
    if (P.kind === 6)
    {
        SubHeading(Inner, 'Swarm');
        Range(Inner, BindEffect(Effect, P, 'swarmRadius', { Label: 'Neighbour radius' }), 0.1, 4, 0.05, 2, 'm');
        Range(Inner, BindEffect(Effect, P, 'pulseHz', { Label: 'Glow pulse' }), 0, 4, 0.05, 2, 'Hz');
        Range(Inner, BindEffect(Effect, P, 'pulseDepth', { Label: 'Pulse depth' }), 0, 1, 0.01, 2, '');
    }
}

function ConstructTransitionSection(Page, Effect)
{
    const Transition = Effect.P.transition;
    const Inner = ConstructSection(Page, 'transition', 'Transition', 'Boxes', 'Transition', () => Transition.wave || 'corner');
    C.SelectRow(Inner, BindEffect(Effect, Transition, 'assemble', { Label: 'Flight', Options: [['Burst or fall', 0], ['Rebuild from outside', 1], ['Stack in column', 2]] }));
    C.SelectRow(Inner, BindEffect(Effect, Transition, 'after', { Label: 'After release', Options: [['Cube (fades out)', 'none'], ['Becomes a butterfly', 'butterfly']] }));
    C.SelectRow(Inner, BindEffect(Effect, Transition, 'wave', { Label: 'Release wave', Options: [['Corner (derez)', 'corner'], ['Melt (patchy)', 'melt'], ['Glitch (blocky)', 'glitch']] }));
    Range(Inner, BindEffect(Effect, Transition, 'holdBase', { Label: 'Hold base' }), 0, 10, 0.05, 2, 's');
    Range(Inner, BindEffect(Effect, Transition, 'holdSpread', { Label: 'Release spread' }), 0, 10, 0.05, 2, 's');
    Range(Inner, BindEffect(Effect, Transition, 'burstShare', { Label: 'Burst share' }), 0, 1, 0.01, 2, '');
    if (Transition.assemble > 0) Range(Inner, BindEffect(Effect, Transition, 'shell', { Label: Transition.assemble === 2 ? 'Drop height' : 'Start distance' }), 0.5, 8, 0.05, 2, 'm');
    if (Effect.P.shape === 4)
    {
        C.SwitchRow(Inner, BindEffect(Effect, Transition, 'fragment', { Label: 'Break on floor impact', Detail: 'Each cube that hits the floor breaks once into eight smaller cubes.' }));
        Range(Inner, BindEffect(Effect, Transition, 'childLife', { Label: 'Child cube life' }), 0.1, 6, 0.05, 2, 's');
    }
}

function ConstructBlackHoleSection(Page, Effect)
{
    const Hole = Effect.P.blackHole, Pull = Effect.P.attractor;
    const Inner = ConstructSection(Page, 'blackhole', 'Black hole', 'Planet', 'Black hole', () => `${Pull.strength.toFixed(1)} m/s²`);
    Caption(Inner, 'A per-effect attractor (it moves with the effect) plus screen-space lensing. Shown in the field overlay under Accelerate.');
    Range(Inner, BindEffect(Effect, Hole, 'horizon', { Label: 'Shadow radius' }), 0.1, 2, 0.01, 2, 'm');
    Range(Inner, BindEffect(Effect, Hole, 'einstein', { Label: 'Lensing strength' }), 0, 6, 0.05, 2, '× shadow');
    Range(Inner, BindEffect(Effect, Pull, 'strength', { Label: 'Pull strength' }), 0, 30, 0.1, 1, 'm/s²');
    Range(Inner, BindEffect(Effect, Pull, 'radius', { Label: 'Pull radius' }), 1, 40, 0.1, 1, 'm');
    Range(Inner, BindEffect(Effect, Pull, 'swirl', { Label: 'Swirl' }), 0, 12, 0.05, 2, 'm/s²');
    Range(Inner, BindEffect(Effect, Pull, 'swallow', { Label: 'Swallow radius' }), 0, 3, 0.01, 2, 'm');
}

function ConstructShimmerSection(Page, Effect)
{
    const Shimmer = Effect.P.heatShimmer;
    const Inner = ConstructSection(Page, 'shimmer', 'Heat shimmer', 'Flame', 'Shimmer', () => `${Shimmer.radius.toFixed(1)} m`);
    Range(Inner, BindEffect(Effect, Shimmer, 'radius', { Label: 'Radius' }), 0.2, 6, 0.05, 2, 'm');
    Range(Inner, BindEffect(Effect, Shimmer, 'strength', { Label: 'Strength' }), 0, 0.05, 0.001, 3, '');
    Range(Inner, BindEffect(Effect, Shimmer, 'freq', { Label: 'Frequency' }), 2, 40, 0.5, 1, '');
    Range(Inner, BindEffect(Effect, Shimmer, 'rate', { Label: 'Rise speed' }), 0, 5, 0.05, 2, '×');
}

function ConstructDipoleSection(Page, Effect)
{
    const Dipole = Effect.P.magneticField;
    const Inner = ConstructSection(Page, 'dipole', 'Magnetic dipole', 'Magnet', 'Dipole', () => `${Dipole.strength.toFixed(1)} m/s²`);
    Caption(Inner, 'A per-effect dipole (axis world Z): particles are driven along the field lines and damped across them.');
    Range(Inner, BindEffect(Effect, Dipole, 'strength', { Label: 'Drive strength' }), 0, 10, 0.05, 2, 'm/s²');
    Range(Inner, BindEffect(Effect, Dipole, 'guide', { Label: 'Guide rate' }), 0, 12, 0.05, 2, '1/s');
    Range(Inner, BindEffect(Effect, Dipole, 'radius', { Label: 'Radius' }), 1, 20, 0.1, 1, 'm');
}

function ConstructPhysicsSection(Page, Effect)
{
    const P = Effect.P;
    const Inner = ConstructSection(Page, 'physics', 'Physics', 'Atom', 'Physics', () => `kT ${P.temperature.toFixed(2)}`);
    Caption(Inner, P.kind === 3 ? 'Lennard-Jones pairs on a spatial hash grid, a Langevin thermostat and reflecting walls. The wind does not act on atoms.'
                                : 'A + B → C on contact (a probability per second) and C → A or B on its own. Species counts come back as a 32-byte readback.');
    Range(Inner, BindEffect(Effect, P, 'temperature', { Label: 'Temperature kT' }), 0.02, 3, 0.01, 2, 'ε');
    Range(Inner, BindEffect(Effect, P, 'damping', { Label: 'Thermostat γ' }), 0, 10, 0.05, 2, '1/s');
    Range(Inner, BindEffect(Effect, P, 'simDt', { Label: 'Integration step' }), 0.001, 0.01, 0.0005, 4, 's');
    Range(Inner, BindEffect(Effect, P, 'sigma', { Label: 'Diameter σ' }), 0.03, 0.2, 0.001, 3, 'm');
    if (P.kind === 3) Range(Inner, BindEffect(Effect, P, 'epsilon', { Label: 'Well depth ε' }), 0, 1, 0.01, 2, '');
    else
    {
        Range(Inner, BindEffect(Effect, P, 'reactRate', { Label: 'Reaction rate' }), 0, 30, 0.1, 1, '1/s');
        Range(Inner, BindEffect(Effect, P, 'dissociation', { Label: 'Dissociation' }), 0, 5, 0.05, 2, '1/s');
        Range(Inner, BindEffect(Effect, P, 'reactRadius', { Label: 'Reaction radius' }), 0.02, 0.2, 0.005, 3, 'm');
        Range(Inner, BindEffect(Effect, P, 'fracA', { Label: 'Initial share of A' }), 0, 1, 0.01, 2, '');
    }
}

function ConstructReadbackSection(Page, Effect, Molecular)
{
    const Inner = ConstructSection(Page, 'readback', 'Readback', 'Activity', 'Readback', () => `${FormatInteger(Effect.Gpu.readbackCount)}×`);
    Caption(Inner, 'Each readback copies 32 bytes (counts and energy) from a GPU reduction; the map is asynchronous, so a frame never waits for it.');
    LiveStatGrid(Inner, [
        ['Alive', () => Effect.Gpu.stats_ ? FormatInteger(Effect.Gpu.stats_.alive) : '—'],
        ['Latency', () => Effect.Gpu.readbackMs ? `${Effect.Gpu.readbackMs.toFixed(2)} ms` : '—'],
        [Molecular ? 'Energy' : 'Readbacks', () => Molecular && Effect.Gpu.stats_ ? Effect.Gpu.stats_.energy.toFixed(1) : FormatInteger(Effect.Gpu.readbackCount)]]);
    const Result = C.Construct('p', 'section-caption', 'Not measured yet.');
    C.ActionRow(Inner, [{ Label: 'Benchmark full readback', Glyph: 'Gauge', Act: async () =>
    {
        Result.textContent = 'Measuring…';
        const Measured = await Scene.Engine.benchmarkFullReadback(Effect.Gpu);
        Result.textContent = `${FormatInteger(Measured.bytes / 1024)} KiB (${FormatInteger(Effect.Gpu.cap)} × 80 B) in ${Measured.ms.toFixed(2)} ms ≈ ${Measured.mbPerSecond.toFixed(0)} MiB/s · `
                           + `${FormatInteger(Measured.alive)} alive. Measured on this browser’s adapter; the editor never needs the full buffer.`;
        AppendLog('PARTICLES', `${Effect.Name}: full readback ${Measured.ms.toFixed(2)} ms`, `${FormatInteger(Measured.bytes / 1024)} KiB`, 'live');
    } }]);
    Inner.append(Result);
}

function ConstructFibreSections(Page, Effect)
{
    const P = Effect.P, Fibre = P.fibre;
    const Trail = Fibre.shape === 'trail', Ribbon = Fibre.shape === 'ribbon';
    const Rebuild = () => { ConstructInspectorPage(); C.Broadcast(); };
    const Form = ConstructSection(Page, 'form', 'Form', 'Zap', 'Form', () => `${Fibre.shape} · ${FormatInteger(Fibre.strands)}`);
    Caption(Form, 'Fibres are analytic: the GPU evaluates each curve from the loop phase, so they cost no simulation and the wind does not move them.');
    C.TextRow(Form, BindEffect(Effect, Effect, 'Name', { Label: 'Name' }));
    C.SelectRow(Form, Object.assign(BindEffect(Effect, Fibre, 'shape', { Label: 'Form', Options: [['Streak (light head)', 'streak'], ['Ribbon (rippling sheet)', 'ribbon'], ['Trail (rides a path)', 'trail']] }),
                                    { Write: PE.Edits.Through('Form', Shape => { Fibre.shape = Shape; setTimeout(Rebuild); }) }));
    Range(Form, BindEffect(Effect, Fibre, 'strands', { Label: 'Strands' }), 1, 1200, 1, 0, '');
    Range(Form, BindEffect(Effect, Fibre, 'segments', { Label: 'Segments' }), 4, 160, 1, 0, '');
    Range(Form, BindEffect(Effect, Fibre, 'seed', { Label: 'Seed' }), 1, 999, 1, 0, '');
    if (!Trail) Range(Form, BindEffect(Effect, Fibre, 'length', { Label: 'Length' }), 0.5, 30, 0.01, 2, 'm');
    Range(Form, BindEffect(Effect, Fibre, 'spread', { Label: 'Spread' }), 0, 10, 0.01, 2, 'm');
    Range(Form, BindEffect(Effect, Fibre, 'amplitude', { Label: 'Wobble' }), 0, 4, 0.01, 2, 'm');
    Range(Form, BindEffect(Effect, Fibre, 'frequency', { Label: 'Waves per loop' }), 0, 8, 0.01, 2, '');
    if (Ribbon)
    {
        Range(Form, BindEffect(Effect, Fibre, 'sheetWidth', { Label: 'Sheet width' }), 0.5, 30, 0.01, 2, 'm');
        Range(Form, BindEffect(Effect, Fibre, 'ripple', { Label: 'Ripple' }), 0, 4, 0.01, 2, 'm');
        Range(Form, BindEffect(Effect, Fibre, 'waves', { Label: 'Sheet waves' }), 0, 6, 0.01, 2, '');
    }
    if (Ribbon || Trail) Range(Form, BindEffect(Effect, Fibre, 'phaseSpread', { Label: 'Phase spread' }), 0, 6.283, 0.01, 2, 'rad');
    if (Trail)
    {
        C.SelectRow(Form, BindEffect(Effect, Fibre, 'pathShape', { Label: 'Path', Options: PE.FIBRE_PATHS.map(Name => [Name, Name]) }));
        Range(Form, BindEffect(Effect, Fibre, 'pathSize', { Label: 'Path size' }), 1, 10, 0.1, 1, 'm');
        Range(Form, BindEffect(Effect, Fibre, 'trailLength', { Label: 'Trail length' }), 0.02, 1, 0.01, 2, 'share');
    }
    const Motion = ConstructSection(Page, 'loop', 'Motion', 'Clock', 'Motion', () => `${Fibre.loopSeconds.toFixed(1)} s loop`);
    Range(Motion, BindEffect(Effect, Fibre, 'loopSeconds', { Label: 'Loop period' }), 2, 60, 0.5, 1, 's');
    Range(Motion, BindEffect(Effect, Fibre, 'harmonic', { Label: 'Speed multiple' }), 1, 4, 1, 0, '');
    Range(Motion, BindEffect(Effect, Fibre, 'windowCycles', { Label: 'Light heads per loop' }), 1, 6, 1, 0, '');
    Range(Motion, BindEffect(Effect, Fibre, 'window', { Label: 'Light window' }), 0.02, 1, 0.01, 2, '');
    C.SelectRow(Motion, BindEffect(Effect, Fibre, 'pulseShape', { Label: 'Pulse shape', Options: PE.PULSE_SHAPES.map(Name => [Name, Name]) }));
    Range(Motion, BindEffect(Effect, Fibre, 'pulseRate', { Label: 'Pulses per loop' }), 0, 4, 1, 0, '');
    Range(Motion, BindEffect(Effect, Fibre, 'pulseDepth', { Label: 'Pulse depth' }), 0, 1, 0.01, 2, '');
    const Placement = ConstructSection(Page, 'placement', 'Placement', 'Move', 'Placement', () => `×${Fibre.scale.toFixed(2)}`);
    C.VectorRow(Placement, BindEffect(Effect, P, 'origin', { Label: 'Origin', Unit: 'm', Step: 0.02, Digits: 2, Minimum: -20, Maximum: 20 }));
    Range(Placement, BindEffect(Effect, Fibre, 'scale', { Label: 'Scale' }), 0.05, 10, 0.01, 2, '×');
    if (!Trail && !Ribbon) C.VectorRow(Placement, BindEffect(Effect, P, 'dir', { Label: 'Direction', Step: 0.01, Digits: 2, Minimum: -1, Maximum: 1 }));
    const Look = ConstructSection(Page, 'look', 'Look', 'Sparkles', 'Look', () => `${Fibre.intensity.toFixed(1)} intensity`);
    Range(Look, BindEffect(Effect, Fibre, 'thickness', { Label: 'Thickness' }), 0.5, 8, 0.05, 2, 'px');
    Range(Look, BindEffect(Effect, Fibre, 'taper', { Label: 'Taper' }), 0, 1, 0.01, 2, '');
    Range(Look, BindEffect(Effect, Fibre, 'intensity', { Label: 'Intensity' }), 0, 12, 0.01, 2, '');
    Range(Look, BindEffect(Effect, Fibre, 'halo', { Label: 'Halo' }), 0, 2, 0.01, 2, '');
    Range(Look, BindEffect(Effect, Fibre, 'baseline', { Label: 'Idle brightness' }), 0, 1, 0.01, 2, '');
    Range(Look, BindEffect(Effect, Fibre, 'accentMix', { Label: 'Accent amount' }), 0, 1, 0.01, 2, '');
    const Colour = ConstructSection(Page, 'colour', 'Colour', 'Palette', 'Colour', () => Fibre.colourMode);
    C.SelectRow(Colour, Object.assign(BindEffect(Effect, Fibre, 'colourMode', { Label: 'Mode', Options: [['Solid', 'solid'], ['Ramp along the fibre', 'ramp'], ['Palette per strand', 'palette']] }),
                                      { Write: PE.Edits.Through('Colour mode', Mode => { Fibre.colourMode = Mode; setTimeout(Rebuild); }) }));
    C.ColourRow(Colour, BindEffect(Effect, Fibre, 'colC', { Label: 'Accent colour' }));
    Fibre.stops.forEach((Stop, Index) =>
    {
        C.ColourRow(Colour, BindEffect(Effect, Stop, 'col', { Label: `Stop ${Index + 1}` }));
        if (Fibre.colourMode === 'ramp') Range(Colour, BindEffect(Effect, Stop, 'pos', { Label: `Stop ${Index + 1} position` }), 0, 1, 0.01, 2, '');
    });
    const StopActions = [];
    if (Fibre.stops.length < PE.MAX_STOPS) StopActions.push({ Label: 'Add stop', Glyph: 'Plus', Act: () => { const Last = Fibre.stops[Fibre.stops.length - 1]; Fibre.stops.push({ pos: Math.min(1, Last.pos + 0.2), col: Last.col.slice() }); setTimeout(Rebuild); } });
    if (Fibre.stops.length > 1) StopActions.push({ Label: 'Remove last stop', Glyph: 'Trash', Tone: 'danger', Act: () => { Fibre.stops.pop(); setTimeout(Rebuild); } });
    if (StopActions.length) C.ActionRow(Colour, StopActions);
    const Sparks = ConstructSection(Page, 'sparks', 'Head sparks', 'Sparkles', 'Sparks', () => Fibre.sparks > 0 ? `${(Fibre.sparks * 100).toFixed(0)} %` : 'off');
    Range(Sparks, BindEffect(Effect, Fibre, 'sparks', { Label: 'Sparks' }), 0, 1, 0.01, 2, '');
    Range(Sparks, BindEffect(Effect, Fibre, 'sparkSize', { Label: 'Spark size' }), 0, 0.4, 0.001, 3, 'm');
    Range(Sparks, BindEffect(Effect, Fibre, 'sparkBrightness', { Label: 'Spark brightness' }), 0, 12, 0.01, 2, '');
}

// 📝 A proposed ParticleEffect record: the preset it came from and only the parameters that differ from it.
function EncodeEffect(Effect)
{
    const Preset = PE.presetById(Effect.PresetId);
    const Lines = ['[[records]]', `identity = "${Effect.Token}"`, 'type = "ParticleEffect"', `name = "${Effect.Name}"`, '[records.configuration]',
                   `enabled = ${Effect.P.visible}`, `preset = "${Effect.PresetId}"`];
    const Format = Reading => Array.isArray(Reading) ? `[${Reading.map(Part => Number(Number(Part).toFixed(4))).join(', ')}]` : typeof Reading === 'string' ? `"${Reading}"` : String(Number(Number(Reading).toFixed(5)));
    let Changes = 0;
    for (const [Key, Reading] of Object.entries(Effect.P))
    {
        if (Key === 'visible' || typeof Reading === 'object' && !Array.isArray(Reading)) continue;
        if (JSON.stringify(Reading) === JSON.stringify(Preset.p[Key])) continue;
        Lines.push(`${Key.replace(/([a-z])([A-Z])/g, '$1_$2').toLowerCase()} = ${Format(Reading)}`);
        Changes++;
    }
    if (!Changes) Lines.push('# no overrides: the preset as shipped');
    return Lines;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FORCE FIELD AND WIND PAGES
//------------------------------------------------------------------------------------------------------------------------

function ConstructOverlaySection(Page)
{
    const Overlay = Scene.Overlay;
    const Inner = ConstructSection(Page, 'overlay', 'Field overlay', 'Layers', 'Overlay', () => Overlay.Enabled ? `${Overlay.SliceHeight.toFixed(1)} m slice` : 'off');
    Caption(Inner, 'The Force Field editor’s visualization over the 3D view: a horizontal slice through the lattice domain, coloured and arrowed. Flow comes '
                 + 'from the GPU lattice itself (read back); Accelerate is fieldAccel on the same packed fields the particles use.');
    C.SwitchRow(Inner, BindScene(Overlay, 'Enabled', { Label: 'Show the field overlay', Detail: 'V toggles it from the viewport.' }, 'VIEW', 'Overlay'));
    C.SegmentedRow(Inner, BindScene(Overlay, 'Quantity', { Label: 'Quantity', Options: [['Speed', 'Speed'], ['Up', 'Vertical'], ['Vorticity', 'Vorticity'], ['Divergence', 'Divergence']] }, 'VIEW', 'Overlay'));
    Range(Inner, BindScene(Overlay, 'SliceHeight', { Label: 'Slice height' }, 'VIEW', 'Overlay'), 0.1, 7.9, 0.05, 2, 'm');
    Range(Inner, BindScene(Overlay, 'Opacity', { Label: 'Heatmap opacity' }, 'VIEW', 'Overlay'), 0.1, 1, 0.05, 2, '');
    C.SwitchRow(Inner, BindScene(Overlay, 'Heatmap', { Label: 'Heatmap' }, 'VIEW', 'Overlay'));
    C.SwitchRow(Inner, BindScene(Overlay, 'Arrows', { Label: 'Arrows on the slice' }, 'VIEW', 'Overlay'));
    C.SwitchRow(Inner, BindScene(Overlay, 'Tracers', { Label: 'Tracers', Detail: 'Streak particles riding the slice (display rate, not real time).' }, 'VIEW', 'Overlay'));
    C.SwitchRow(Inner, BindScene(Overlay, 'Minimap', { Label: 'Minimap', Detail: 'The domain from above with the camera’s ground trace; click to move the camera target.' }, 'VIEW', 'Overlay'));
    C.SwitchRow(Inner, BindScene(Overlay, 'Labels', { Label: 'Field labels' }, 'VIEW', 'Overlay'));
}

function ConstructProbeSection(Page)
{
    const Inner = ConstructSection(Page, 'probe', 'Probe', 'Crosshair', 'Probe', () => Scene.ProbeEnabled ? `(${Scene.Probe.map(Part => Part.toFixed(1)).join(', ')})` : 'off');
    Caption(Inner, 'One texel (8 bytes, rgba16float) of the wind lattice is copied every few frames and mapped asynchronously — the cheap direction of readback.');
    C.SwitchRow(Inner, BindScene(Scene, 'ProbeEnabled', { Label: 'Probe on' }, 'WIND', 'Probe'));
    C.VectorRow(Inner, BindScene(Scene, 'Probe', { Label: 'Position', Unit: 'm', Step: 0.02, Digits: 2, Minimum: -6, Maximum: 8 }, 'WIND', 'Probe'));
    LiveStatGrid(Inner, [
        ['Wind at probe', () => Scene.Engine.probeResult ? `${Scene.Engine.probeResult.mag.toFixed(2)} m/s` : '—'],
        ['Readback', () => Scene.Engine.probeResult ? `${Scene.Engine.probeResult.ms.toFixed(2)} ms` : '—'],
        ['Overlay at probe', () => { const Out = new Float64Array(3); if (!Scene.Readback) return '—'; Scene.Readback.Sample(Scene.Probe[0], Scene.Probe[1], Scene.Probe[2], Out); return `${Math.hypot(Out[0], Out[1], Out[2]).toFixed(2)} m/s`; }]]);
}

function ConstructFieldPage(Page)
{
    ConstructHeading(Page, 'Environment', 'Force Field', C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, 'LIVE')]),
                     'The same master component as the Force Field editor: the wind lattice (its guest) and the scene’s localized forces, drawn by the shared visualization.');
    C.PillTabs(Page, { Options: [['Flow · m/s', 'Flow'], ['Accelerate · m/s²', 'Accelerate'], ['Damp · 1/s', 'Damp']], Read: () => Scene.Overlay.Contribution,
                       Write: Contribution => { Scene.Overlay.Contribution = Contribution; Scene.Overlay.Enabled = true; AppendLog('VIEW', `Overlay shows ${Contribution.toLowerCase()}`, ContributionUnits[Contribution], 'live', 'Overlay.Contribution'); } });
    ConstructProbeHero(Page);
    C.ChipRow(Page, [
        () => [String(FlowFields().length), 'flow'],
        () => [String(Scene.Forces.filter(Field => (PE.Forces.KindById(Field.Kind) || {}).Give === 'accelerate').length), 'accelerate'],
        () => [String(Scene.Forces.filter(Field => (PE.Forces.KindById(Field.Kind) || {}).Give === 'damp').length), 'damp'],
        () => ['24·12·24', 'lattice']]);
    const Tiles = C.Construct('div', 'tile-grid');
    C.ReadoutTile(Tiles, { Label: 'Summed into the lattice', Glyph: 'Lattice', Resolve: () => [String(PE.Forces.Bakeable(Scene.Forces).Baked.length), `of ${PE.WIND.maxComps}`] });
    C.ReadoutTile(Tiles, { Label: 'Evaluated per particle', Glyph: 'Target', Resolve: () => [String(PE.Forces.Bakeable(Scene.Forces).Live.length), 'fields'] });
    LiveReadoutTile(Tiles, { Label: 'Packed for fieldAccel', Glyph: 'Boxes', Resolve: () => [String(Scene.PackedFields ? Scene.PackedFields[0] : 0), 'of 8'] });
    C.ReadoutTile(Tiles, { Label: 'No GPU path yet', Glyph: 'Alert', Resolve: () => [String(Scene.Forces.filter(Field => !PE.Forces.Packable(Field)).length), 'authored only'] });
    Page.append(Tiles);
    Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Quick controls'), C.Construct('div', 'caption', 'Overlay and lattice')]));
    const Grid = C.Construct('div', 'toggle-grid');
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Enabled', { Label: 'Field overlay', Glyph: 'Layers' }, 'VIEW', 'Overlay'));
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Heatmap', { Label: 'Heatmap', Glyph: 'Palette' }, 'VIEW', 'Overlay'));
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Arrows', { Label: 'Slice arrows', Glyph: 'Move' }, 'VIEW', 'Overlay'));
    C.ToggleTile(Grid, BindScene(Scene.Wind, 'showArrows', { Label: '3D lattice arrows', Glyph: 'Boxes' }, 'VIEW', 'Lattice'));
    C.ToggleTile(Grid, BindScene(Scene.Wind, 'showDomain', { Label: 'Domain box', Glyph: 'Scan' }, 'VIEW', 'Lattice'));
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Minimap', { Label: 'Minimap', Glyph: 'Map' }, 'VIEW', 'Overlay'));
    Page.append(Grid);
    const ConstructTabs = ConstructSectionTabs(Page);
    ConstructOverlaySection(Page);
    ConstructForceListSection(Page);
    ConstructProbeSection(Page);
    ConstructRecordBlock(Page, 'Levelspace records', 'Force fields · proposed', EncodeForces, new Set(['identity', 'type', 'name', 'parent', 'enabled']));
    ConstructTabs();
}

function ConstructProbeHero(Page)
{
    const Metric = C.Construct('div', 'metric-big');
    const Rate = C.Construct('div', 'rate');
    const Low = C.Construct('div', 'low');
    const MarkText = C.Construct('span');
    const Card = C.Construct('div', 'hero-card', [
        C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [C.Construct('div', 'mark live', C.ConstructGlyph('Check')), MarkText]),
                                         C.Construct('div', 'estimate-tag', 'GPU lattice · probe')]),
        C.Construct('div', 'hero-title', 'Wind at the probe'),
        C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])]);
    Page.append(Card);
    const Series = C.TimeSeries(Card, { Samples: ProbeSamples, Capacity: SampleCapacity, Target: () => Scene.Wind.arrowRef, Height: 110, Unit: 'm/s', TargetLabel: 'Arrow full scale', Headroom: 1.2, AxisDigits: 1 });
    LiveRefreshers.push(Series.Refresh);
    LiveRefreshers.push(() =>
    {
        const Result = Scene.Engine.probeResult;
        C.ConstructMetric(Metric, Result ? Result.mag : 0, 2, 'm/s');
        const Bearing = Result ? Wrap360(Math.atan2(Result.value[0], -Result.value[2]) * 180 / Math.PI) : 0;
        Rate.textContent = `${Bearing.toFixed(0)}°`;
        Rate.append(C.Construct('small', null, C.ResolveCompassPoint(Bearing)));
        Low.textContent = Result ? `vertical ${Result.value[1].toFixed(2)} m/s` : 'waiting for readback';
        const Status = Scene.Readback ? Scene.Readback.Status : null;
        MarkText.textContent = Status && Status.Count ? `Overlay reads the GPU lattice · ${(Status.Bytes / 1024).toFixed(0)} KB every 150 ms` : 'Overlay waiting for the first lattice readback';
    });
}

function ConstructForceListSection(Page)
{
    const Inner = ConstructSection(Page, 'forces', 'Localized forces', 'Target', 'Forces', () => `${Scene.Forces.length} fields`);
    Caption(Inner, 'The eos taxonomy: flow fields sum into the lattice; acceleration fields are evaluated per particle by fieldAccel; damp has no GPU path yet.');
    for (const [Give, Title] of [['flow', 'Flow · m/s'], ['accelerate', 'Accelerate · m/s²'], ['damp', 'Damp · 1/s']])
    {
        const Members = Scene.Forces.map((Field, Index) => [Field, Index]).filter(([Field]) => (PE.Forces.KindById(Field.Kind) || {}).Give === Give);
        if (!Members.length) continue;
        SubHeading(Inner, Title);
        const List = C.Construct('div', 'source-list');
        for (const [Field, Index] of Members)
        {
            const Contribution = Contributions[Give];
            const Glyph = C.Construct('div', 'source-glyph', C.ConstructGlyph(ForceGlyphs[Field.Kind] || 'Target'));
            Glyph.style.color = ContributionColours[Contribution];
            Glyph.style.background = ContributionColours[Contribution] + '1f';
            const Status = C.Construct('span', 'row-status ' + (Field.Enabled ? 'active' : 'off'), Field.Enabled ? (PE.Forces.Packable(Field) ? 'GPU' : 'Authored') : 'Off');
            const Row = C.Construct('div', 'source-row', [Glyph, C.Construct('div', null, [C.Construct('div', 'source-name', Field.Name), C.Construct('div', 'source-meta', PE.Forces.KindById(Field.Kind).Name)]),
                                                          C.Construct('div', 'source-reading', `${Number(Field.Strength.toFixed(2))} ${ContributionUnits[Contribution]}`), Status]);
            Row.addEventListener('click', () => { Selected = 'force:' + Index; ConstructInspectorPage(); C.Broadcast(); });
            List.append(Row);
        }
        Inner.append(List);
    }
    SubHeading(Inner, 'Add a field');
    const Adders = C.Construct('div', 'chip-toggles');
    for (const Kind of PE.Forces.ForceFieldKinds)
    {
        const Chip = C.Construct('button', 'chip-toggle', `+ ${Kind.Name}`);
        Chip.type = 'button';
        Chip.addEventListener('click', () => AddForce(Kind.Id));
        Adders.append(Chip);
    }
    Inner.append(Adders);
}

// 📝 eos addForce: any kind in the taxonomy can be added, including the two with no GPU path (they say so).
function AddForce(Id)
{
    const Fresh = PE.Forces.BaseField(Id);
    const Same = Scene.Forces.filter(One => One.Kind === Id).length;
    if (Same) Fresh.Name = `${Fresh.Name} ${Same + 1}`;
    if (Fresh.Reaches !== PE.Forces.Reach.Everywhere) { Fresh.Centre = [0, 2, 0]; Fresh.Radius = 4; }
    Scene.Forces.push(Fresh);
    Selected = 'force:' + (Scene.Forces.length - 1);
    AppendLog('FORCES', `Added ${Fresh.Name}`, PE.Forces.Packable(Fresh) ? 'reaches the GPU' : 'authored only: no GPU path yet', PE.Forces.Packable(Fresh) ? 'live' : 'warm');
    ConstructOutliner();
    ConstructInspectorPage();
    C.Broadcast();
}

function EncodeForces()
{
    const Lines = [];
    Scene.Forces.forEach((Field, Index) =>
    {
        if (Index) Lines.push('');
        Lines.push('[[records]]', `identity = "rec_particle_force_${Index}"`, 'type = "ForceSource"', `name = "${Field.Name}"`, 'parent = "rec_force_field"', '[records.configuration]',
                   `enabled = ${Field.Enabled}`, `source = "${Field.Kind}"`, `centre_metres = [${Field.Centre.join(', ')}]`, `reach = "${Field.Reaches}"`, `reach_metres = ${Field.Radius}`,
                   `falloff = "${Field.Fades}"`, `strength = ${Field.Strength}`, `bearing_deg = ${CompassFromField(Field).toFixed(1)}`,
                   `begins_seconds = ${Field.Begins}`, `lasts_seconds = ${Field.Lasts}`, `repeats_seconds = ${Field.Repeats}`);
    });
    return Lines;
}

function ConstructWindPage(Page)
{
    const Wind = Scene.Wind;
    ConstructHeading(Page, 'Environment / Force Field', 'Wind', C.Construct('span', 'guest-pill', 'GUEST OF FORCE FIELD'),
                     'A 3D lattice of velocities (24 × 12 × 24 voxels over 12 × 8 × 12 m) rebuilt every frame from the flow fields; every effect samples it each step.');
    ConstructProbeHero(Page);
    C.ChipRow(Page, [() => [`×${Wind.windScale.toFixed(2)}`, 'scale'], () => [Wind.turbulence.toFixed(2), 'turbulence m/s'], () => [Wind.swirl.toFixed(2), 'swirl m/s'], () => [String(FlowFields().length), 'flow fields']]);
    const ConstructTabs = ConstructSectionTabs(Page);
    const Lattice = ConstructSection(Page, 'lattice', 'Lattice', 'Lattice', 'Lattice', () => `×${Wind.windScale.toFixed(2)}`);
    Caption(Lattice, 'Global terms of the sampling grid, added to every voxel: a scale on the whole lattice, value-noise turbulence and divergence-free curl swirl.');
    Range(Lattice, BindScene(Wind, 'windScale', { Label: 'Wind scale' }, 'WIND', 'Wind'), 0, 3, 0.01, 2, '×');
    Range(Lattice, BindScene(Wind, 'turbulence', { Label: 'Turbulence' }, 'WIND', 'Wind'), 0, 2, 0.01, 2, 'm/s');
    Range(Lattice, BindScene(Wind, 'swirl', { Label: 'Swirl' }, 'WIND', 'Wind'), 0, 4, 0.05, 2, 'm/s');
    const Display = ConstructSection(Page, 'display', 'Display', 'Eye', 'Display', () => Wind.showArrows ? '3D arrows' : 'overlay');
    Caption(Display, 'The eos 3D lattice arrows (coloured by speed) and the floor and domain box. The field overlay replaces the arrows by default.');
    C.SwitchRow(Display, BindScene(Wind, 'showArrows', { Label: '3D lattice arrows' }, 'VIEW', 'Lattice'));
    Range(Display, BindScene(Wind, 'arrowRef', { Label: 'Arrow full scale' }, 'VIEW', 'Lattice'), 1, 15, 0.1, 1, 'm/s');
    Range(Display, BindScene(Wind, 'arrowStride', { Label: 'Arrow density' }, 'VIEW', 'Lattice'), 1, 6, 1, 0, 'voxel step');
    C.SwitchRow(Display, BindScene(Wind, 'showFloor', { Label: 'Floor' }, 'VIEW', 'Lattice'));
    C.SwitchRow(Display, BindScene(Wind, 'showDomain', { Label: 'Domain box' }, 'VIEW', 'Lattice'));
    const Fields = ConstructSection(Page, 'fields', 'Fields in this lattice', 'Wind', 'Fields', () => `${FlowFields().length} flow`);
    const Flowing = FlowFields();
    if (!Flowing.length) Caption(Fields, 'No flow fields: the lattice is empty and every effect coasts.');
    C.StatGrid(Fields, Flowing.slice(0, 9).map(Field => [Field.Name, () => Field.Enabled ? `${Field.Strength.toFixed(2)} m/s` : 'off']));
    C.ActionRow(Fields, [{ Label: 'Edit force fields', Glyph: 'Target', Tone: 'accent', Act: () => { Selected = 'Field'; ConstructInspectorPage(); setTimeout(() => RevealSection('forces'), 50); } }]);
    ConstructProbeSection(Page);
    ConstructTabs();
}

function ConstructForcePage(Page, Field, Index)
{
    const Kind = PE.Forces.KindById(Field.Kind);
    const Contribution = Contributions[Kind.Give];
    const Owner = Field.Name;
    const Pill = C.Construct('span', 'contribution-pill ' + Contribution.toLowerCase(), `${Contribution.toUpperCase()} · ${Kind.Unit}`);
    const Heading = ConstructHeading(Page, 'Environment / Force Field', Field.Name, Pill,
                                     `${Kind.Name}${Kind.Was ? ' · was ' + Kind.Was : ' · new with the taxonomy'}. ${Kind.Give === 'flow' ? 'Summed into the wind lattice.' : Kind.Give === 'accelerate' ? 'Evaluated per particle by fieldAccel.' : 'Authored; no GPU path yet.'}`);
    C.Register(() => { Heading.textContent = Field.Name; });
    const Metric = C.Construct('div', 'metric-big');
    const Rate = C.Construct('div', 'rate');
    const Low = C.Construct('div', 'low');
    const MarkText = C.Construct('span');
    const Mark = C.Construct('div', 'mark live', C.ConstructGlyph('Check'));
    const Card = C.Construct('div', 'hero-card', [
        C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [Mark, MarkText]), C.Construct('div', 'estimate-tag', Kind.Id)]),
        C.Construct('div', 'hero-title', 'Strength at the centre'),
        C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])]);
    Page.append(Card);
    C.CurvePlot(Card, { Height: 110, Resolve: () =>
    {
        const Reach = Field.Reaches === 'everywhere' ? 10 : Field.Radius;
        const Points = [];
        for (let Step = 0; Step <= 60; ++Step) Points.push([Step / 60 * Reach * 1.1, Field.Reaches === 'everywhere' ? Math.abs(Field.Strength) : Math.abs(Field.Strength) * (Step / 60 * 1.1 < 1 ? PE.Forces.Falls(Field.Fades, Step / 60 * Reach * 1.1, Reach) : 0)]);
        const Peak = Math.max(1e-6, ...Points.map(Point => Point[1]));
        return { XRange: [0, Reach * 1.1], YRange: [0, Peak * 1.2], Curves: [{ Points, Stroke: ContributionColours[Contribution], Fill: ContributionColours[Contribution] + '22' }],
                 XTicks: [[0, 'centre'], [Reach / 2, `${(Reach / 2).toFixed(1)} m`], [Reach, `${Reach.toFixed(1)} m`]],
                 Caption: Field.Reaches === 'everywhere' ? 'Everywhere: no centre, no edge.' : `Falloff ${Field.Fades} to the ${Field.Reaches} edge.` };
    } });
    LiveRefreshers.push(() =>
    {
        C.ConstructMetric(Metric, Math.abs(Field.Strength), 2, Kind.Unit);
        const Awake = PE.Forces.Awake(Field, Scene.Time);
        Mark.className = 'mark ' + (!Field.Enabled ? 'danger' : Awake ? 'live' : 'warm');
        MarkText.textContent = !Field.Enabled ? 'Disabled' : !PE.Forces.Packable(Field) ? 'Authored · no GPU path yet' : Awake ? 'Acting now' : 'Outside its time window';
        Rate.textContent = Field.Reaches === 'everywhere' ? '∞' : `${Field.Radius.toFixed(1)} m`;
        Rate.append(C.Construct('small', null, 'reach'));
        Low.textContent = Kind.Give === 'flow' ? `toward ${CompassFromField(Field).toFixed(0)}° ${C.ResolveCompassPoint(CompassFromField(Field))}` : Field.Reaches;
    });
    Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Quick controls'), C.Construct('div', 'caption', Kind.Id)]));
    const Grid = C.Construct('div', 'toggle-grid');
    C.ToggleTile(Grid, BindScene(Field, 'Enabled', { Label: 'Enabled', Glyph: 'Check' }, 'FORCES', Owner));
    C.ToggleTile(Grid, { Label: `Show ${Contribution.toLowerCase()}`, Glyph: 'Layers', Read: () => Scene.Overlay.Enabled && Scene.Overlay.Contribution === Contribution,
                         Write: Enabled => { Scene.Overlay.Enabled = Enabled || Scene.Overlay.Enabled; Scene.Overlay.Contribution = Contribution; } });
    C.ToggleTile(Grid, BindScene(Scene.Overlay, 'Labels', { Label: 'Field labels', Glyph: 'Target' }, 'VIEW', 'Overlay'));
    Page.append(Grid);
    C.ActionRow(Page, [{ Label: 'Remove field', Glyph: 'Trash', Tone: 'danger', Act: () => { Scene.Forces.splice(Index, 1); Selected = 'Field'; AppendLog('FORCES', `Removed ${Owner}`, null, 'warm'); ConstructOutliner(); ConstructInspectorPage(); } }]);
    const ConstructTabs = ConstructSectionTabs(Page);
    const FieldSection = ConstructSection(Page, 'field', 'Field', 'Gauge', 'Field', () => `${Field.Strength.toFixed(2)} ${Kind.Unit}`);
    C.TextRow(FieldSection, BindScene(Field, 'Name', { Label: 'Name' }, 'FORCES', Owner));
    Range(FieldSection, BindScene(Field, 'Strength', { Label: 'Strength' }, 'FORCES', Owner), -20, 20, 0.05, 2, Kind.Unit);
    if (Kind.Give === 'flow' && Field.Kind !== 'tornado' && Field.Kind !== 'outflow')
        C.PolarDial(FieldSection, { Label: 'Direction of travel', Read: () => CompassFromField(Field), Write: Degrees => { Field.Direction = FieldFromCompass(Degrees); AppendLog('FORCES', `${Owner}: toward ${Degrees}°`, null, 'live', `${Owner}.Direction`); },
                                    Resolve: () => ({ Caption: `Compass bearing (0° = −Z, up in the top view). eos stores ${PE.Forces.Bearing(Field).toFixed(0)}° (0° = +Z).` }) });
    if (Field.Kind === 'gust') Range(FieldSection, BindScene(Field, 'Rate', { Label: 'Band speed' }, 'FORCES', Owner), 0, 2, 0.01, 2, 'Hz');
    if (Field.Kind === 'attract')
    {
        Range(FieldSection, BindScene(Field, 'Swirl', { Label: 'Swirl' }, 'FORCES', Owner), 0, 12, 0.05, 2, 'm/s²');
        Range(FieldSection, BindScene(Field, 'Swallow', { Label: 'Swallow radius' }, 'FORCES', Owner), 0, 3, 0.01, 2, 'm');
    }
    if (!PE.Forces.Packable(Field)) Caption(FieldSection, '⚠️ This kind has no GPU path yet: it is authored and described but moves nothing. It is listed rather than hidden, as the eos editor does.');
    const Reach = ConstructSection(Page, 'reach', 'Reach and falloff', 'Target', 'Reach', () => Field.Reaches === 'everywhere' ? 'everywhere' : `${Field.Radius.toFixed(1)} m`);
    if (Field.Reaches === 'everywhere') Caption(Reach, 'Reaches everywhere: not a very large sphere — it has no centre and no edge, the case a bounded lattice could not hold.');
    else
    {
        C.VectorRow(Reach, BindScene(Field, 'Centre', { Label: 'Centre', Unit: 'm', Step: 0.02, Digits: 2, Minimum: -20, Maximum: 20 }, 'FORCES', Owner));
        Range(Reach, BindScene(Field, 'Radius', { Label: 'Radius' }, 'FORCES', Owner), 0.5, 30, 0.05, 2, 'm');
        C.SegmentedRow(Reach, BindScene(Field, 'Fades', { Label: 'Falloff', Options: Object.values(PE.Forces.Falloff).map(Name => [Name === 'inverse-square' ? 'Inverse²' : Name[0].toUpperCase() + Name.slice(1), Name]) }, 'FORCES', Owner));
    }
    const Time = ConstructSection(Page, 'time', 'Time window', 'Clock', 'Time', () => Field.Lasts > 0 ? `${Field.Lasts.toFixed(1)} s` : 'forever');
    Range(Time, BindScene(Field, 'Begins', { Label: 'Begins' }, 'FORCES', Owner), 0, 60, 0.1, 1, 's');
    Range(Time, BindScene(Field, 'Lasts', { Label: 'Lasts · 0 = forever' }, 'FORCES', Owner), 0, 120, 0.1, 1, 's');
    Range(Time, BindScene(Field, 'Repeats', { Label: 'Repeats every · 0 = once' }, 'FORCES', Owner), 0, 120, 0.1, 1, 's');
    ConstructRecordBlock(Page, 'Levelspace record', 'ForceSource · proposed', () => EncodeForces().join('\n').split('\n\n')[Index].split('\n'), new Set(['identity', 'type', 'name', 'parent', 'enabled']));
    ConstructTabs();
}

function ConstructLightningPage(Page)
{
    const Lightning = Scene.Lightning;
    ConstructHeading(Page, 'Environment', 'Lightning', C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, Lightning.auto ? 'AUTO' : 'MANUAL')]),
                     'Branching fractal bolts built on the CPU for each strike and drawn as glowing GPU ribbons. The flash lights the frame; thunder follows after distance ÷ 343 m/s.');
    C.ActionRow(Page, [{ Label: 'Strike now', Glyph: 'Zap', Tone: 'accent', Act: StrikeNow },
                       { Label: 'Sound', Glyph: 'Activity', Act: () => { if (!Lightning.thunder.enabled) Lightning.thunder.enable(); else Lightning.thunder.disable(); AppendLog('LIGHTNING', `Thunder ${Lightning.thunder.enabled ? 'on' : 'off'}`, null, 'live'); } }]);
    LiveStatGrid(Page, [['Strikes', () => FormatInteger(Lightning.strikes)], ['Last strike', () => Lightning.lastStrike ? `(${Lightning.lastStrike.at.map(Part => Part.toFixed(1)).join(', ')})` : '—'],
                        ['Thunder', () => Lightning.thunder.enabled ? 'on' : 'off']]);
    const ConstructTabs = ConstructSectionTabs(Page);
    const Strikes = ConstructSection(Page, 'strikes', 'Strikes', 'Zap', 'Strikes', () => Lightning.auto ? 'automatic' : 'manual');
    Caption(Strikes, 'Strikes fire the Strike sparks effect. Sound needs one click (the browser’s autoplay rule), then plays after distance ÷ 343 s.');
    C.SwitchRow(Strikes, BindScene(Lightning, 'auto', { Label: 'Automatic strikes' }, 'LIGHTNING', 'Lightning'));
    Range(Strikes, Object.assign(BindScene(Lightning, 'intervalMin', { Label: 'Interval min' }, 'LIGHTNING', 'Lightning'), { Write: Reading => { Lightning.intervalMin = Math.min(Reading, Lightning.intervalMax); } }), 0.2, 30, 0.1, 1, 's');
    Range(Strikes, Object.assign(BindScene(Lightning, 'intervalMax', { Label: 'Interval max' }, 'LIGHTNING', 'Lightning'), { Write: Reading => { Lightning.intervalMax = Math.max(Reading, Lightning.intervalMin); } }), 0.2, 60, 0.1, 1, 's');
    Range(Strikes, BindScene(Lightning, 'distance', { Label: 'Distance' }, 'LIGHTNING', 'Lightning'), 50, 6000, 10, 0, 'm');
    const Shape = ConstructSection(Page, 'bolt', 'Bolt shape', 'Activity', 'Bolt', () => `${Lightning.levels} levels`);
    Range(Shape, BindScene(Lightning, 'amp', { Label: 'Roughness' }, 'LIGHTNING', 'Lightning'), 0.2, 5, 0.05, 2, 'm');
    Range(Shape, Object.assign(BindScene(Lightning, 'levels', { Label: 'Detail levels' }, 'LIGHTNING', 'Lightning'), { Write: Reading => { Lightning.levels = Math.round(Reading); } }), 3, 7, 1, 0, '');
    Range(Shape, BindScene(Lightning, 'branchChance', { Label: 'Branch chance' }, 'LIGHTNING', 'Lightning'), 0, 1, 0.01, 2, '');
    const Web = ConstructSection(Page, 'web', 'Lightning web', 'Lattice', 'Web', () => Scene.Web.Enabled ? 'on' : 'off');
    Caption(Web, 'Each visible effect’s origin is a node linked to its nearest neighbours by a branching bolt, redrawn each interval. Drawn only: they do not strike particles.');
    C.SwitchRow(Web, Object.assign(BindScene(Scene.Web, 'Enabled', { Label: 'Arcs between effects' }, 'LIGHTNING', 'Web'), { Write: Enabled => { Scene.Web.Enabled = Enabled; Scene.Web.NextAt = 0; } }));
    Range(Web, BindScene(Scene.Web, 'Interval', { Label: 'Re-strike every' }, 'LIGHTNING', 'Web'), 0.1, 3, 0.05, 2, 's');
    Range(Web, Object.assign(BindScene(Scene.Web, 'Links', { Label: 'Links per effect' }, 'LIGHTNING', 'Web'), { Write: Reading => { Scene.Web.Links = Math.round(Reading); } }), 1, 4, 1, 0, '');
    ConstructTabs();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 INSPECTOR ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

function ConstructInspectorPage()
{
    if (PageScope) C.ReleaseScope(PageScope);
    PageScope = C.OpenScope();
    LiveRefreshers.length = 0;
    Sections.length = 0;
    PE.Edits.For(null);
    const Content = document.getElementById('InspectorContent');
    Content.textContent = '';
    const Page = C.Construct('div', 'inspector');
    Content.append(Page);
    const Effect = EffectByToken(Selected), Field = ForceByToken(Selected);
    if (Effect) ConstructEffectPage(Page, Effect);
    else if (Field) ConstructForcePage(Page, Field, Number(Selected.slice(6)));
    else if (Selected === 'Wind') ConstructWindPage(Page);
    else if (Selected === 'Lightning') ConstructLightningPage(Page);
    else { Selected = 'Field'; ConstructFieldPage(Page); }
    C.CloseScope(PageScope);
    document.getElementById('InspectorScroll').scrollTop = 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Readouts refresh four times a second from one list, as the Render settings inspector does; nothing broadcasts per frame.
function RefreshLive()
{
    for (const Effect of Scene.Effects)
    {
        Effect.AliveSamples.push(Effect.P.kind === 7 ? Effect.P.fibre.strands : Effect.Gpu.stats_ ? Effect.Gpu.stats_.alive : 0);
        if (Effect.AliveSamples.length > SampleCapacity) Effect.AliveSamples.shift();
    }
    ProbeSamples.push(Scene.Engine.probeResult ? Scene.Engine.probeResult.mag : 0);
    if (ProbeSamples.length > SampleCapacity) ProbeSamples.shift();
    LiveRefreshers.forEach(Refresh => Refresh());
    PersistentRefreshers.forEach(Refresh => Refresh());
    LiveOutlinerRows.forEach(Refresh => Refresh());
    RefreshHud();
}

async function Boot()
{
    const Canvas = document.getElementById('ViewportCanvas');
    const Banner = document.getElementById('BootError');
    try
    {
        ResizeCanvas();
        Scene.Engine = new PE.Engine(Canvas);
        const Facts = await Scene.Engine.init();
        Scene.AdapterName = Facts.adapter || 'WebGPU';
        Scene.Lightning = new PE.Lightning();
        Scene.Readback = ConstructLatticeReadback(Scene.Engine);
        Scene.Engine.onLost = Lost => { Banner.hidden = false; Banner.textContent = `WebGPU device lost: ${Lost.message || Lost.reason}. Reload the page.`; AppendLog('PARTICLES', 'WebGPU device lost', Lost.message || Lost.reason, 'warm'); };
        // 📝 ?scene=none|id,id,… overrides the default scene, as in the eos editor.
        const Query = new URLSearchParams(location.search).get('scene');
        const Identities = Query === null ? PE.defaultScene : Query === 'none' || Query === '' ? [] : Query.split(',');
        for (const Identity of Identities) if (PE.presetById(Identity)) Scene.Effects.push(ConstructEffect(Identity));
        ConstructOverlay();
        ConstructViewport();
        ConstructOutliner();
        C.SmoothScroll(document.getElementById('OutlinerScroll'), document.getElementById('OutlinerContent'));
        InspectorScroller = C.SmoothScroll(document.getElementById('InspectorScroll'), document.getElementById('InspectorContent'));
        ConstructLogStream();
        ConstructInspectorPage();
        AppendLog('PARTICLES', `WebGPU ready · ${Scene.AdapterName}`, `${Scene.Effects.length} effects · ${PE.Presets.length} presets in the library`, 'live');
        AppendLog('FORCES', 'Field overlay: the Force Field editor’s visualization', 'flow read back from the GPU lattice; acceleration from fieldAccel', 'live');
        const InspectorStatus = document.getElementById('InspectorStatus');
        const Revision = C.Construct('b', null, '0');
        let Revisions = 0;
        InspectorStatus.append(C.Construct('div', 'status-cell', [C.Construct('span', null, 'Parameters'), C.Construct('b', null, 'Editing')]), C.Construct('span', 'spacer'),
                               C.Construct('div', 'status-cell', [C.Construct('span', null, 'Revision'), Revision]));
        PE.Edits.Subscribe(() => { Revision.textContent = String(++Revisions); });
        AttachOrbit();
        window.addEventListener('resize', ResizeCanvas);
        new ResizeObserver(ResizeCanvas).observe(Canvas.parentElement);
        window.addEventListener('keydown', Event =>
        {
            if (Event.target.matches('input, select, textarea')) return;
            if (Event.code === 'Space') { Event.preventDefault(); TogglePlay(); }
            else if (Event.key === 'l' || Event.key === 'L') StrikeNow();
            else if (Event.key === 'v' || Event.key === 'V') { Scene.Overlay.Enabled = !Scene.Overlay.Enabled; C.Broadcast(); }
            else if (Event.key === 'Escape') document.getElementById('LibraryVeil').classList.remove('shown');
        });
        // 📝 Framed by a host editor (the gas emitter card's Open ParticleEditor), eos handoff speaks one message each way.
        const HandoffView = {
            get selection() { const Effect = EffectByToken(Selected); return Effect ? { type: 'system', id: Effect.Number } : { type: 'other' }; },
            get systems() { return Scene.Effects.map(Effect => ({ id: Effect.Number, presetId: Effect.PresetId, name: Effect.Name, p: Effect.P })); }
        };
        PE.Handoff.Install(HandoffView, {
            Open: (PresetId, Name) =>
            {
                const Existing = Scene.Effects.find(Effect => Effect.PresetId === PresetId);
                const Effect = Existing || (PE.presetById(PresetId) ? AddEffect(PresetId) : null);
                if (!Effect) return null;
                if (Name) Effect.Name = Name;
                Selected = Effect.Token;
                return { p: Effect.P, Effect };
            },
            Rebuild: Opened => RebuildGpu(Opened.Effect),
            Refresh: () => { ConstructOutliner(); ConstructInspectorPage(); C.Broadcast(); }
        });
        C.Broadcast();
        window.ParticleEditorScene = Scene;
        window.ParticleEditorView = Visualization;
        window.ParticleEditorHold = Enabled =>
        {
            Held = !!Enabled;
            if (!Held) { LastTick = 0; requestAnimationFrame(Tick); }
            else { Advance(performance.now()); RefreshLive(); }
            return Held;
        };
        requestAnimationFrame(Tick);
    }
    catch (Trouble)
    {
        console.error(Trouble);
        Banner.hidden = false;
        Banner.textContent = `Particle editor could not start: ${Trouble && Trouble.message ? Trouble.message : Trouble}`;
    }
}

// 🔴 A module script is deferred: DOMContentLoaded has usually fired by the time this runs, so boot now unless parsing continues.
if (document.readyState === 'loading') window.addEventListener('DOMContentLoaded', Boot);
else Boot();
