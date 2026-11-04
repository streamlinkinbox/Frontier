/*==========================================================================================================================================
                                                         FORCEFIELDVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 CPU validation of the Force Field editor's field, the shared visualization's helpers and the Particle editor's fieldAccel port.
//
//     node Exhibits/ForceFieldEditor/ForceFieldValidation.mjs
//
// 📝 The prototypes are browser scripts: the classic ones (catalogue, visualization) are evaluated here in this realm and their
//    top-level constants exposed; the Particle editor's readback file is an ES module and is imported directly.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const Root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
globalThis.window = globalThis;
globalThis.document = { createElement: () => ({ getContext: () => ({}) }) };

function LoadClassic(Relative)
{
    const Source = readFileSync(join(Root, Relative), 'utf8');
    const Names = [...Source.matchAll(/^const ([A-Za-z]+) = /gm)].map(Match => Match[1]);
    (0, eval)(Source + '\n;' + Names.map(Name => `globalThis.${Name} = ${Name};`).join(''));
}
LoadClassic('Experimental/ForceFieldVisualization/ForceFieldVisualization.js');
LoadClassic('Experimental/ForceFieldEditor/ForceFieldCatalogue.js');
const F = globalThis.ForceFieldCatalogue, V = globalThis.ForceFieldVisualization;
const { EvaluateFieldAcceleration, DecodeHalf } = await import(join(Root, 'Experimental/ParticleEditor/ParticleFieldReadback.js'));

let Passed = 0;
function Assert(Condition, Claim)
{
    if (!Condition) { console.error('  FAIL  ' + Claim); process.exit(1); }
    Passed++;
    console.log('  PASS  ' + Claim);
}
const Banner = Title => console.log('\n' + Title);
const Near = (A, B, Slack) => Math.abs(A - B) <= Slack;
let Seed = 12345;
const Random = () => ((Seed = (Seed * 1103515245 + 12345) % 2147483648) / 2147483648);

//------------------------------------------------------------------------------------------------------------------------
Banner('Wind: the native PlanetaryWindProfile formulas (ForceField.cpp EvaluateStepSample), eddies and mesoscale off');
{
    const Scene = F.ConstructDefaultField();
    Object.assign(Scene.Wind, { Turbulence: 0, MesoscaleStrength: 0, Bearing: 63, Speed: 9.5, GustStrength: 0.28, GustFrequency: 0.085, Shear: 0.35, Veer: 12 });
    let Worst = 0;
    for (let Trial = 0; Trial < 400; ++Trial)
    {
        const X = (Random() - 0.5) * 40000, Y = Random() * 6000, Z = (Random() - 0.5) * 40000, Time = Random() * 300;
        const Out = new Float64Array(3);
        F.EvaluateWind(Scene.Wind, Scene.Field, X, Y, Z, Time, 0.5, Out);
        // Independent transcription of the native step sample (BearingRadians = (Direction − 90°)·π/180, veer in rad/km).
        const AltitudeKm = Math.max(0, Y) * 1e-3;
        const β = (Scene.Wind.Bearing - 90) * Math.PI / 180 + Scene.Wind.Veer * Math.PI / 180 * AltitudeKm;
        const Gain = Math.max(0.15, 1 + Scene.Wind.Shear * Math.min(AltitudeKm, 12));
        const Along = X * Math.cos(β) + Z * Math.sin(β);
        const Phase = 2 * Math.PI * Scene.Wind.GustFrequency * Time - 0.011 * Along;
        const Wave = 0.62 * Math.sin(Phase) + 0.38 * Math.sin(2.17 * Phase + 1.13);
        const Speed = Scene.Wind.Speed * Gain * (1 + Scene.Wind.GustStrength * Wave);
        Worst = Math.max(Worst, Math.abs(Out[0] - Math.cos(β) * Speed), Math.abs(Out[2] - Math.sin(β) * Speed), Math.abs(Out[1]));
    }
    Assert(Worst < 1e-9, `400 random points from 0 to 6 km up match the native profile (worst difference ${Worst.toExponential(1)} m/s)`);
    const Coarse = new Float64Array(3);
    F.EvaluateWind(Scene.Wind, Scene.Field, 1234, 10, -987, 17, 2000, Coarse);
    const Expected = Scene.Wind.Speed * Math.max(0.15, 1 + Scene.Wind.Shear * 0.01);
    Assert(Near(Math.hypot(Coarse[0], Coarse[2]), Expected, 1e-9), 'at 2 km cells the 571 m and 263 m gust bands are filtered out (level of detail), leaving the sheared mean');
}

//------------------------------------------------------------------------------------------------------------------------
Banner('Wind: curl eddies are divergence-free and bounded');
{
    const Scene = F.ConstructDefaultField();
    Object.assign(Scene.Wind, { GustStrength: 0, MesoscaleStrength: 0, Shear: 0 });
    const At = (X, Z) => { const Out = new Float64Array(3); F.EvaluateWind(Scene.Wind, Scene.Field, X, 10, Z, 3, 0.25, Out); return Out; };
    let Divergence = 0, Eddy = 0;
    const Step = 0.25;
    for (let Trial = 0; Trial < 400; ++Trial)
    {
        const X = Random() * 3000, Z = Random() * 3000;
        Divergence = Math.max(Divergence, Math.abs((At(X + Step, Z)[0] - At(X - Step, Z)[0] + At(X, Z + Step)[2] - At(X, Z - Step)[2]) / (2 * Step)));
        const Here = At(X, Z);
        Eddy = Math.max(Eddy, Math.hypot(Here[0] - Math.sin(Math.PI / 4) * Scene.Wind.Speed, Here[2] + Math.cos(Math.PI / 4) * Scene.Wind.Speed));
    }
    Assert(Divergence < 1e-3, `horizontal divergence stays at finite-difference noise (${Divergence.toExponential(1)} 1/s)`);
    Assert(Eddy < Scene.Wind.Speed, `eddies stay below the base speed (${Eddy.toFixed(2)} of ${Scene.Wind.Speed} m/s at turbulence ${Scene.Wind.Turbulence})`);
}

//------------------------------------------------------------------------------------------------------------------------
Banner('Activation: Begins / Lasts / Repeats with the native ramps, and triggers');
{
    const Source = F.ConstructSource('Blast', { Activation: 'Window', Begins: 6, Lasts: 14, Repeats: 30, RampIn: 0, RampOut: 2 });
    const At = Time => F.ResolveActivation(Source, Time).Envelope;
    Assert(At(5) === 0 && At(10) === 1, 'closed before Begins (5 s), full inside the window (10 s)');
    Assert(At(19.5) > 0 && At(19.5) < 1 && At(21) === 0, 'ramps out over the last 2 s and is closed after Lasts');
    Assert(At(40) === 1, 'opens again one Repeats period later (36 s + 4 s)');
    const Trigger = F.ConstructSource('Microburst', { Activation: 'Trigger', Lasts: 45, RampIn: 1.25, RampOut: 2 });
    Assert(F.ResolveActivation(Trigger, 30).Status === 'Armed', 'a trigger is armed until fired');
    Trigger.TriggeredAt = 12;
    Assert(F.ResolveActivation(Trigger, 20).Envelope === 1 && F.ResolveActivation(Trigger, 60).Envelope === 0, 'fired at 12 s it acts at 20 s and is spent after 57 s');
}

//------------------------------------------------------------------------------------------------------------------------
Banner('Flow types: the native source shapes');
{
    const Scene = F.ConstructDefaultField();
    const Probe = (Source, DX, Y, DZ, Time = 0) => { const Out = new Float64Array(3); F.EvaluateSource(Source, Scene.Wind, Source.Centre[0] + DX, Y, Source.Centre[2] + DZ, Time, Out); return Out; };
    const Tornado = F.ConstructSource('Tornado', { Centre: [0, 0, 0], TrackSpeed: 0 });
    let PeakRadius = 0, Peak = 0;
    for (let Radius = 1; Radius < 120; Radius += 0.5)
    {
        const Out = Probe(Tornado, Radius, 10, 0);
        if (Math.abs(Out[2]) > Peak) { Peak = Math.abs(Out[2]); PeakRadius = Radius; }
    }
    Assert(Near(PeakRadius, 20.3, 3), `Burgers–Rott tangential speed peaks at the inner radius (${PeakRadius} m for a 20 m core at 10 m height)`);
    Assert(Probe(Tornado, 20, 10, 0)[2] < 0, 'cyclonic spin: east of the centre the air moves north (−Z)');
    Assert(Probe(Tornado, 5, 10, 0)[1] > 0, 'helical updraft inside the core');
    const Burst = F.ConstructSource('Microburst', { Centre: [0, 260, 0] });
    Assert(Probe(Burst, 0, 10, 0)[1] < 0, 'microburst: downdraft at the centre near the ground');
    Assert(Probe(Burst, 150, 10, 0)[0] > 0 && Probe(Burst, -150, 10, 0)[0] < 0, 'microburst: outflow runs outward along the ground');
    const Thermal = F.ConstructSource('Thermal', { Centre: [0, 0, 0] });
    Assert(Probe(Thermal, 0, 100, 0)[1] > 10, 'thermal: buoyant updraft in the chimney');
    const Front = F.ConstructSource('GustFront', { Centre: [0, 0, 0], Bearing: 90, FrontSpeed: 18 });
    Assert(Math.hypot(...Probe(Front, 300, 10, 0, 10)) === 0, 'gust front: still air ahead of the nose (nose at 180 m after 10 s)');
    Assert(Probe(Front, 150, 10, 0, 10)[0] > 5, 'gust front: surge behind the nose along its bearing');
    const Blast = F.ConstructSource('Blast', { Centre: [0, 2, 0], Activation: 'Always' });
    const Front2 = 3 + 110 * 2;
    Assert(Probe(Blast, Front2, 2, 0, 2)[0] > Probe(Blast, Front2 + 60, 2, 0, 2)[0], 'blast wave: the shell is at r₀ + c·t');
}

//------------------------------------------------------------------------------------------------------------------------
Banner('Accelerate and damp types: the eos taxonomy');
{
    const Scene = F.ConstructDefaultField();
    const Probe = (Source, X, Y, Z) => { const Out = new Float64Array(3); F.EvaluateSource(Source, Scene.Wind, X, Y, Z, 0, Out); return Out; };
    const Pull = F.ConstructSource('Attractor', { Centre: [0, 10, 0], Swirl: 2 });
    const Inward = Probe(Pull, 100, 10, 0);
    Assert(Inward[0] < 0 && Math.abs(Inward[2]) > 0, 'attractor pulls inward (−X east of it) with a swirl component');
    Assert(Probe(F.ConstructSource('Repulsor', { Centre: [0, 10, 0] }), 50, 10, 0)[0] > 0, 'repulsor pushes outward');
    const Gravity = Probe(F.ConstructSource('Gravity'), 5000, 123, -7000);
    Assert(Gravity[1] === -9.81 && Gravity[0] === 0 && Gravity[2] === 0, 'gravity is −9.81 m/s² everywhere');
    const Drag = F.ConstructSource('Drag', { Centre: [0, 0, 0] });
    Assert(Probe(Drag, 0, 5, 0)[0] > 0 && Probe(Drag, 400, 5, 0)[0] === 0, 'drag volume damps inside its box only');
    const Prepared = F.Prepare(Scene, 7);
    let Same = true;
    for (let Trial = 0; Trial < 200; ++Trial)
    {
        const X = (Random() - 0.5) * 8000, Z = (Random() - 0.5) * 8000;
        for (const Contribution of ['Flow', 'Accelerate', 'Damp'])
        {
            const A = new Float64Array(3), B = new Float64Array(3);
            F.Evaluate(Scene, Contribution, X, 10, Z, 7, 4, A);
            F.EvaluatePrepared(Scene, Prepared, Contribution, X, 10, Z, 4, B);
            Same = Same && A.every((Reading, Index) => Near(Reading, B[Index], 1e-12));
        }
    }
    Assert(Same, 'the prepared fast path (bounds, once-per-time envelopes) equals the direct evaluation at 200 points × 3 contributions');
}

//------------------------------------------------------------------------------------------------------------------------
Banner('Levelspace: keys marked existing are the ones SceneWorld.cpp reads today');
{
    const SceneWorld = readFileSync(join(Root, 'Frontier/Engine/Scene/SceneWorld.cpp'), 'utf8');
    const Body = Name => { const Start = SceneWorld.indexOf(`void ${Name}::DeserializeConfiguration`); return SceneWorld.slice(Start, SceneWorld.indexOf('\n}\n', Start)); };
    const Readable = new Set([...Body('WindRecord').matchAll(/"([a-z_]+)"/g), ...Body('ForceFieldRecord').matchAll(/"([a-z_]+)"/g)].map(Match => Match[1]));
    const Structural = new Set(['identity', 'type', 'name', 'parent']);
    const Claimed = [...F.ExistingKeys].filter(Key => !Structural.has(Key));
    const Missing = Claimed.filter(Key => !Readable.has(Key));
    Assert(Missing.length === 0, `all ${Claimed.length} keys shown without “proposed” are read by WindRecord / ForceFieldRecord${Missing.length ? ' (missing: ' + Missing.join(', ') + ')' : ''}`);
    const Lines = F.EncodeLevelspace(F.ConstructDefaultField(), 'Wind');
    Assert(Lines.includes('direction_deg = 45.0') && Lines.includes('speed_mps = 6.5') && Lines.includes('turbulence = 0.45'),
           'the Wind record opens with Default.levelspace’s values (45°, 6.5 m/s, turbulence 0.45)');
}

//------------------------------------------------------------------------------------------------------------------------
Banner('Visualization helpers');
{
    Assert(V.FormatDistance(0) === '0 m' && V.FormatDistance(850) === '850 m' && V.FormatDistance(2400) === '2.4 km' && V.FormatDistance(100000) === '100 km',
           'distances read in m below 1 km and in km above');
    Assert(V.NiceLength(1800) === 1000 && V.NiceLength(2600) === 2000 && V.NiceLength(6000) === 5000, 'scale bars use 1 · 2 · 5 × 10ᵏ');
    const Focal = 1 / Math.tan(Math.PI / 8), Near_ = 0.05, Far = 400;
    const Projection = [Focal / 1.6, 0, 0, 0, 0, Focal, 0, 0, 0, 0, Far / (Near_ - Far), -1, 0, 0, (Near_ * Far) / (Near_ - Far), 0];
    const Inverse = V.InvertProjection(Projection);
    let Error = 0;
    for (let Row = 0; Row < 4; ++Row) for (let Column = 0; Column < 4; ++Column)
    {
        let Sum = 0;
        for (let Inner = 0; Inner < 4; ++Inner) Sum += Projection[Inner * 4 + Row] * Inverse[Column * 4 + Inner];
        Error = Math.max(Error, Math.abs(Sum - (Row === Column ? 1 : 0)));
    }
    Assert(Error < 1e-9, `the camera-mode unprojection inverts the WebGPU perspective (error ${Error.toExponential(1)})`);
    Assert(V.ResolveColour(0).join() === V.ColourStops[0].join() && V.ResolveColour(1).join() === V.ColourStops[4].join(), 'the colour map ends on the eos stops');
}

//------------------------------------------------------------------------------------------------------------------------
Banner('Particle editor: fieldAccel port and half-float decoding');
{
    const Fields = new Float32Array(128);
    Fields[0] = 2; Fields[1] = 5;
    Fields.set([0, 1, 0, 4, 0, 6, 0, 0, 0, 1e9, 0, 0], 4);          // attractor at (0, 1, 0), radius 4, strength 6
    Fields.set([0, 0, 0, 0, 2, -9.81, 0, 0, 0, 1e9, 0, 0], 16);     // gravity: lift type with a negative strength, radius 0 = everywhere
    const Out = new Float64Array(3);
    EvaluateFieldAcceleration(Fields, 2, 1, 0, Out);
    Assert(Near(Out[0], -6 * Math.pow(1 - 2 / 4, 2), 1e-6) && Near(Out[1], -9.81, 1e-5), 'attractor −strength·(1 − r/R)² toward its centre plus gravity everywhere, as shaders.js fieldAccel');
    const Outside = new Float64Array(3);
    EvaluateFieldAcceleration(Fields, 9, 1, 0, Outside);
    Assert(Outside[0] === 0 && Near(Outside[1], -9.81, 1e-5), 'outside the radius only the unbounded field acts');
    Fields.set([0, 1, 0, 4, 0, 6, 0, 0, 10, 2, 0, 0], 4);           // the attractor now waits for 10 s
    const Waiting = new Float64Array(3);
    EvaluateFieldAcceleration(Fields, 2, 1, 0, Waiting);
    Assert(Waiting[0] === 0, 'a field outside its time window adds nothing (fieldOn)');
    Assert(DecodeHalf(0x3c00) === 1 && DecodeHalf(0xc000) === -2 && Near(DecodeHalf(0x3555), 1 / 3, 1e-3) && DecodeHalf(0) === 0, 'rgba16float texels decode exactly');
}

//------------------------------------------------------------------------------------------------------------------------
Banner('Sampling cost (reported, not asserted)');
{
    const Scene = F.ConstructDefaultField();
    const Out = new Float64Array(3);
    for (const Span of [600, 6000, 100000])
    {
        const Prepared = F.Prepare(Scene, 12), Cell = Span / 150;
        for (let Warm = 0; Warm < 2; ++Warm) for (let Index = 0; Index < 14250; ++Index) { Out.fill(0); F.EvaluatePrepared(Scene, Prepared, 'Flow', (Index % 150) * Cell - Span / 2, 10, Math.floor(Index / 150) * Cell - Span / 3, Cell, Out); }
        const Started = process.hrtime.bigint();
        for (let Index = 0; Index < 14250; ++Index) { Out.fill(0); F.EvaluatePrepared(Scene, Prepared, 'Flow', (Index % 150) * Cell - Span / 2, 10, Math.floor(Index / 150) * Cell - Span / 3, Cell, Out); }
        const Milliseconds = Number(process.hrtime.bigint() - Started) / 1e6;
        console.log(`  ${V.FormatDistance(Span).padStart(7)} view · 150 × 95 lattice · ${Milliseconds.toFixed(1)} ms · ${(Milliseconds * 1000 / 14250).toFixed(2)} µs per sample`);
    }
}

console.log(`\nPASS ${Passed}`);
