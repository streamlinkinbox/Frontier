/*==========================================================================================================================================
                                                         FORCEFIELDCATALOGUE.JS
==========================================================================================================================================*/
// 🧩 The Force Field editor's field: the Wind guest profile, sixteen localized force types, their evaluation and the levelspace encoding.
'use strict';

// 📝 Units and conventions, shared with the C++ field (Engine/PhysicalDynamics/ForceField, AtmosphericFlow):
//      world      X east, Y up, Z south (the map shows north up); metres, seconds
//      bearing    compass degrees, clockwise from north, the direction of TRAVEL (WindConfiguration::DirectionDegrees);
//                 the native PlanetaryWindProfile reads BearingRadians = (Bearing − 90°) · π / 180
//      flow       m/s · accelerate m/s² · damp 1/s (the eos taxonomy: the three never share a bucket)
//    The flow types are the native ForceSourceKind set evaluated as ForceFieldSpace::EvaluateStepSample does; the
//    accelerate and damp types are the eos Particle editor's force fields (forcefields.js) with the same falloffs.
const ForceFieldCatalogue = (() =>
{
    const Radian   = Math.PI / 180;
    const Smooth01 = Share => { const Clamped = Math.min(1, Math.max(0, Share)); return Clamped * Clamped * (3 - 2 * Clamped); };
    const Clamp    = (Reading, Lower, Upper) => Math.min(Upper, Math.max(Lower, Reading));

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    TAXONOMY
    //------------------------------------------------------------------------------------------------------------------------

    const Contributions = {
        Flow:       { Label: 'Flow', Unit: 'm/s', Colour: '#8fd3c4',
                      Text: 'Velocity the air carries. A receiver relaxes toward it at its own coupling, so one gust moves a leaf and a hailstone differently. Flow sums into one lattice however many fields there are.' },
        Accelerate: { Label: 'Accelerate', Unit: 'm/s²', Colour: '#e8c27a',
                      Text: 'Added straight to velocity, whatever the receiver’s coupling: gravity, attraction and repulsion. Evaluated per receiver, so each costs more than a flow field.' },
        Damp:       { Label: 'Damp', Unit: '1/s', Colour: '#b9a3e8',
                      Text: 'Scales velocity down. Removes energy without adding a direction, so it can never start anything moving.' }
    };

    const Channels  = [['Particles', 'Particles'], ['Debris', 'Debris'], ['Rain & hail', 'Precipitation'], ['Clouds', 'Clouds'],
                       ['Plumes', 'Plumes'], ['Foliage', 'Foliage'], ['Vehicles', 'Vehicles']];
    const Reaches   = [['Everywhere', 'Everywhere'], ['Sphere', 'Sphere'], ['Column', 'Column'], ['Box', 'Box']];
    const Falloffs  = [['None', 'None'], ['Linear', 'Linear'], ['Smooth', 'Smooth'], ['Inverse²', 'InverseSquare']];
    const Activations = [['Always', 'Always'], ['Window', 'Window'], ['Trigger', 'Trigger']];

    // 📝 eos Falls(): how much of a field reaches a point, 1 at the centre and 0 at the edge.
    function Falls(Shape, Distance, Radius)
    {
        if (Shape === 'None') return Distance < Radius ? 1 : 0;
        const Share = Clamp(Distance / Math.max(Radius, 1e-3), 0, 1);
        if (Shape === 'Linear') return 1 - Share;
        if (Shape === 'InverseSquare') return (1 / (1 + 8 * Share * Share)) * (Share < 1 ? 1 : 0);
        const Soft = 1 - Share * Share;
        return Soft * Soft;
    }

    const Distance = (Minimum, Maximum, Step, Unit) => ({ Minimum, Maximum, Step, Unit: Unit || 'm', Scale: 'Logarithmic', Distance: true });

    //------------------------------------------------------------------------------------------------------------------------
    //                                                   FORCE TYPES
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Parameters list the type's own controls (Strength section); Native names the C++ ForceSourceKind (or the eos field
    //    id) that the type stands for, so the record maps one to one.
    const Types = [
        { Id: 'Tornado', Name: 'Tornado column', Contribution: 'Flow', Glyph: 'Tornado', Native: 'VortexColumn', Reach: 'Column',
          Blurb: 'A Burgers–Rott column: tangential speed peaks at the inner radius, boundary-layer inflow near the ground, a helical updraft inside, and a collar that widens toward cloud base. It travels along its track.',
          Defaults: { Strength: 68, InnerRadius: 20, CollarRadius: 96, Inflow: 26, Updraft: 42, Height: 950, Spin: 1, TrackBearing: 109, TrackSpeed: 3.7, Radius: 600, Falloff: 'Smooth' },
          Parameters: [
              Object.assign({ Key: 'Strength', Label: 'Peak tangential speed', Digits: 1 }, { Minimum: 1, Maximum: 120, Step: 0.5, Unit: 'm/s' }),
              Object.assign({ Key: 'InnerRadius', Label: 'Inner radius (ground)' }, Distance(2, 20000, 1)),
              Object.assign({ Key: 'CollarRadius', Label: 'Collar radius (cloud base)' }, Distance(2, 40000, 1)),
              { Key: 'Inflow', Label: 'Radial inflow', Minimum: 0, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
              { Key: 'Updraft', Label: 'Axial updraft', Minimum: 0, Maximum: 80, Step: 0.5, Unit: 'm/s', Digits: 1 },
              Object.assign({ Key: 'Height', Label: 'Column height' }, Distance(20, 15000, 10)),
              { Key: 'TrackSpeed', Label: 'Track speed', Minimum: 0, Maximum: 30, Step: 0.1, Unit: 'm/s', Digits: 1 }],
          Bearings: [['TrackBearing', 'Track toward']], Spin: true },
        { Id: 'GustFront', Name: 'Gust front', Contribution: 'Flow', Glyph: 'Front', Native: 'TravelingGustFront', Reach: 'Everywhere',
          Blurb: 'A travelling surge: the nose lifts the air, the wake behind it decays over a few crest depths. At kilometre scale it is a squall line or a haboob wall.',
          Defaults: { Strength: 29, FrontSpeed: 18, CrestDepth: 85, Height: 140, HalfWidth: 900, Bearing: 113, Radius: 900, Falloff: 'Smooth' },
          Parameters: [
              { Key: 'Strength', Label: 'Peak surge speed', Minimum: 1, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
              { Key: 'FrontSpeed', Label: 'Front speed', Minimum: 0, Maximum: 40, Step: 0.5, Unit: 'm/s', Digits: 1 },
              Object.assign({ Key: 'CrestDepth', Label: 'Crest depth' }, Distance(5, 10000, 1)),
              Object.assign({ Key: 'HalfWidth', Label: 'Half width along the front' }, Distance(10, 60000, 10)),
              Object.assign({ Key: 'Height', Label: 'Gust head height' }, Distance(5, 5000, 5))],
          Bearings: [['Bearing', 'Travels toward']] },
        { Id: 'Microburst', Name: 'Microburst', Contribution: 'Flow', Glyph: 'Downburst', Native: 'RadialMicroburst', Reach: 'Column',
          Blurb: 'A convective downdraft column that hits the ground and spreads as a ring of outflow. The centre height is the core altitude.',
          Defaults: { Strength: 31, Downdraft: 24, InnerRadius: 65, RingRadius: 150, Height: 364, Radius: 600, Falloff: 'Smooth', CentreY: 260 },
          Parameters: [
              { Key: 'Strength', Label: 'Peak outflow speed', Minimum: 1, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
              { Key: 'Downdraft', Label: 'Downdraft speed', Minimum: 0, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
              Object.assign({ Key: 'InnerRadius', Label: 'Column radius' }, Distance(5, 10000, 1)),
              Object.assign({ Key: 'RingRadius', Label: 'Outflow ring radius' }, Distance(10, 20000, 1))] },
        { Id: 'Thermal', Name: 'Thermal updraft', Contribution: 'Flow', Glyph: 'Thermal', Native: 'ThermalUpdraftPlume', Reach: 'Column',
          Blurb: 'A buoyant chimney of rising air: entrainment inflow in the lower third, divergence near the top where a cumulus turret forms.',
          Defaults: { Strength: 14.5, Inflow: 4.8, InnerRadius: 48, Height: 620, Radius: 170, Falloff: 'Smooth' },
          Parameters: [
              { Key: 'Strength', Label: 'Peak updraft', Minimum: 0.5, Maximum: 40, Step: 0.1, Unit: 'm/s', Digits: 1 },
              { Key: 'Inflow', Label: 'Entrainment inflow', Minimum: 0, Maximum: 20, Step: 0.1, Unit: 'm/s', Digits: 1 },
              Object.assign({ Key: 'InnerRadius', Label: 'Warm column radius' }, Distance(2, 5000, 1)),
              Object.assign({ Key: 'Height', Label: 'Chimney height' }, Distance(15, 12000, 5))] },
        { Id: 'Jet', Name: 'Jet corridor', Contribution: 'Flow', Glyph: 'Jet', Native: 'DirectionalJetCorridor', Reach: 'Box',
          Blurb: 'A Venturi corridor: flow funnelled along an axis, widening downstream — a canyon, a tunnel draft, a street between towers.',
          Defaults: { Strength: 24, Length: 95, InnerRadius: 12, Bearing: 90, Radius: 120, Falloff: 'Smooth', CentreY: 4 },
          Parameters: [
              { Key: 'Strength', Label: 'Axial speed', Minimum: 0.5, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
              Object.assign({ Key: 'Length', Label: 'Corridor length' }, Distance(2, 30000, 1)),
              Object.assign({ Key: 'InnerRadius', Label: 'Corridor radius' }, Distance(0.5, 5000, 0.5))],
          Bearings: [['Bearing', 'Axis toward']] },
        { Id: 'Puff', Name: 'Gust puff', Contribution: 'Flow', Glyph: 'Puff', Native: 'LocalizedGustPuff', Reach: 'Sphere',
          Blurb: 'A Gaussian packet of faster air that drifts downstream with the wind at its altitude.',
          Defaults: { Strength: 12, Lift: 0.8, Radius: 28, RadiusY: 14, Drift: 0.92, Bearing: 112, Falloff: 'Smooth', CentreY: 12 },
          Parameters: [
              { Key: 'Strength', Label: 'Surge speed', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s', Digits: 1 },
              { Key: 'Lift', Label: 'Vertical surge', Minimum: -10, Maximum: 10, Step: 0.1, Unit: 'm/s', Digits: 1 },
              Object.assign({ Key: 'RadiusY', Label: 'Vertical radius' }, Distance(1, 2000, 0.5)),
              { Key: 'Drift', Label: 'Drift with the wind', Minimum: 0, Maximum: 1.5, Step: 0.01, Unit: '×', Digits: 2 }],
          Bearings: [['Bearing', 'Surge toward']] },
        { Id: 'Blast', Name: 'Blast wave', Contribution: 'Flow', Glyph: 'Blast', Native: 'SphericalImpulseShell', Reach: 'Sphere',
          Blurb: 'An expanding shell r(t) = r₀ + c·t pushing air outward, weakening with distance. Fire it from the timeline or the Fire button.',
          Defaults: { Strength: 42, Expansion: 110, Thickness: 14, InitialRadius: 3, Radius: 1500, Falloff: 'None', Activation: 'Window', Begins: 6, Lasts: 14, Repeats: 30, RampIn: 0, RampOut: 2, CentreY: 2 },
          Parameters: [
              { Key: 'Strength', Label: 'Peak radial speed', Minimum: 1, Maximum: 200, Step: 1, Unit: 'm/s', Digits: 0 },
              { Key: 'Expansion', Label: 'Wavefront speed', Minimum: 5, Maximum: 400, Step: 1, Unit: 'm/s', Digits: 0 },
              Object.assign({ Key: 'Thickness', Label: 'Shell thickness' }, Distance(0.5, 2000, 0.5)),
              Object.assign({ Key: 'InitialRadius', Label: 'Initial radius' }, Distance(0.5, 1000, 0.5))] },
        { Id: 'Wake', Name: 'Vehicle wake', Contribution: 'Flow', Glyph: 'Wake', Native: 'AerodynamicWake', Reach: 'Box',
          Blurb: 'The slipstream behind a moving body: a forward tow and a counter-rotating pair that lifts spray and dust. Ready for the vehicle step.',
          Defaults: { Strength: 13.5, Circulation: 7.5, Speed: 22, HalfWidth: 1.15, Length: 36, Height: 3.4, Bearing: 0, Radius: 80, Falloff: 'Smooth', CentreY: 0.65 },
          Parameters: [
              { Key: 'Strength', Label: 'Slipstream tow', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s', Digits: 1 },
              { Key: 'Circulation', Label: 'Trailing circulation', Minimum: 0, Maximum: 30, Step: 0.1, Unit: 'm/s', Digits: 1 },
              { Key: 'Speed', Label: 'Body speed', Minimum: 0, Maximum: 90, Step: 0.5, Unit: 'm/s', Digits: 1 },
              Object.assign({ Key: 'HalfWidth', Label: 'Half track' }, Distance(0.2, 40, 0.05)),
              Object.assign({ Key: 'Length', Label: 'Wake length' }, Distance(2, 600, 0.5)),
              Object.assign({ Key: 'Height', Label: 'Wake height' }, Distance(0.3, 60, 0.1))],
          Bearings: [['Bearing', 'Body travels toward']] },
        { Id: 'Current', Name: 'Current', Contribution: 'Flow', Glyph: 'Current', Native: 'current', Reach: 'Box',
          Blurb: 'A steady flow inside a box: a river, a conveyor of air, an HVAC duct. The eos Current field.',
          Defaults: { Strength: 4, Bearing: 90, HalfX: 60, HalfZ: 20, Height: 30, Radius: 60, Falloff: 'Smooth' },
          Parameters: [{ Key: 'Strength', Label: 'Flow speed', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s', Digits: 1 }],
          Bearings: [['Bearing', 'Flows toward']] },
        { Id: 'Attractor', Name: 'Attractor', Contribution: 'Accelerate', Glyph: 'Attract', Native: 'attract', Reach: 'Sphere',
          Blurb: 'Pulls toward its centre. With swirl it behaves like a drain; with a swallow radius it removes what reaches it (the black hole preset).',
          Defaults: { Strength: 6, Swirl: 2, Swallow: 0, Radius: 260, Falloff: 'Smooth', CentreY: 10 },
          Parameters: [
              { Key: 'Strength', Label: 'Pull', Minimum: 0, Maximum: 60, Step: 0.1, Unit: 'm/s²', Digits: 1 },
              { Key: 'Swirl', Label: 'Swirl', Minimum: 0, Maximum: 30, Step: 0.1, Unit: 'm/s²', Digits: 1 },
              Object.assign({ Key: 'Swallow', Label: 'Swallow radius' }, { Minimum: 0, Maximum: 500, Step: 0.5, Unit: 'm', Digits: 1 })] },
        { Id: 'Repulsor', Name: 'Repulsor', Contribution: 'Accelerate', Glyph: 'Repel', Native: 'repel', Reach: 'Sphere',
          Blurb: 'Pushes away from its centre — a fan, a shock-free blast zone, a keep-out field.',
          Defaults: { Strength: 8, Radius: 180, Falloff: 'Smooth', CentreY: 10 },
          Parameters: [{ Key: 'Strength', Label: 'Push', Minimum: 0, Maximum: 60, Step: 0.1, Unit: 'm/s²', Digits: 1 }] },
        { Id: 'Orbit', Name: 'Orbit', Contribution: 'Accelerate', Glyph: 'Orbit', Native: 'orbit', Reach: 'Sphere',
          Blurb: 'Tangential acceleration around world up, with no inward part: receivers circle the centre.',
          Defaults: { Strength: 4, Radius: 220, Falloff: 'Smooth', CentreY: 10 },
          Parameters: [{ Key: 'Strength', Label: 'Tangential pull', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s²', Digits: 1 }] },
        { Id: 'Lift', Name: 'Lift', Contribution: 'Accelerate', Glyph: 'Lift', Native: 'lift', Reach: 'Sphere',
          Blurb: 'Straight up, fading toward the edge — a column of buoyancy for particles that ignore the wind.',
          Defaults: { Strength: 5, Radius: 120, Falloff: 'Smooth', CentreY: 10 },
          Parameters: [{ Key: 'Strength', Label: 'Upward acceleration', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s²', Digits: 1 }] },
        { Id: 'Magnetic', Name: 'Magnetic dipole', Contribution: 'Accelerate', Glyph: 'Magnet', Native: 'magnetic', Reach: 'Sphere',
          Blurb: 'Drives receivers along the field lines of a dipole whose axis is world Z.',
          Defaults: { Strength: 3, Radius: 160, Falloff: 'Smooth', CentreY: 10 },
          Parameters: [{ Key: 'Strength', Label: 'Drive', Minimum: 0, Maximum: 30, Step: 0.1, Unit: 'm/s²', Digits: 1 }] },
        { Id: 'Gravity', Name: 'Gravity', Contribution: 'Accelerate', Glyph: 'Gravity', Native: 'gravity', Reach: 'Everywhere',
          Blurb: 'Unbounded and an acceleration, so no coupling can soften it. Author “this room is on the Moon” with a sphere reach.',
          Defaults: { Strength: 9.81, Radius: 500, Falloff: 'None', CentreY: 0 },
          Parameters: [{ Key: 'Strength', Label: 'Acceleration', Minimum: 0, Maximum: 30, Step: 0.01, Unit: 'm/s²', Digits: 2 }] },
        { Id: 'Drag', Name: 'Drag volume', Contribution: 'Damp', Glyph: 'Damp', Native: 'drag', Reach: 'Box',
          Blurb: 'Bleeds speed away inside its volume — water, mud, a slow-motion zone.',
          Defaults: { Strength: 0.8, HalfX: 150, HalfZ: 80, Height: 40, Radius: 150, Falloff: 'Smooth', Bearing: 0 },
          Parameters: [{ Key: 'Strength', Label: 'Damping rate', Minimum: 0, Maximum: 10, Step: 0.01, Unit: '1/s', Digits: 2 }] }
    ];
    const TypeById = Object.fromEntries(Types.map(Type => [Type.Id, Type]));

    //------------------------------------------------------------------------------------------------------------------------
    //                                                 FIELD CONSTRUCTION
    //------------------------------------------------------------------------------------------------------------------------

    let TokenCounter = 1;

    function ConstructSource(TypeId, Overrides)
    {
        const Type = TypeById[TypeId];
        const Defaults = Type.Defaults;
        const Source = Object.assign({
            Token: 'src_' + (TokenCounter++).toString(36) + Math.floor(Math.random() * 1e6).toString(36),
            Type: TypeId, Name: Type.Name, Enabled: true,
            Centre: [0, Defaults.CentreY ?? 0, 0], Reach: Type.Reach, Radius: 100, Height: 200, HalfX: 50, HalfZ: 50, Falloff: 'Smooth',
            Activation: 'Always', Begins: 0, Lasts: 0, Repeats: 0, RampIn: 1.25, RampOut: 2, TriggeredAt: null, Acts: []
        }, JSON.parse(JSON.stringify(Defaults)));
        delete Source.CentreY;
        return Object.assign(Source, Overrides || {});
    }

    // 📝 The scene wind is Default.levelspace's Wind record (45°, 6.5 m/s, turbulence 0.45) with WindConfiguration's other
    //    defaults; the localized forces span 100 m to 100 km so every level of detail has something to show.
    function ConstructDefaultField()
    {
        return {
            Wind: { Enabled: true, Bearing: 45, Speed: 6.5, GustStrength: 0.25, GustFrequency: 0.085, Shear: 0.15, Veer: 8,
                    Turbulence: 0.45, TurbulenceScale: 95, Seed: 7331, GroundHeight: 0, MesoscaleScale: 18000, MesoscaleStrength: 0.22,
                    AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true },
            Field: { Enabled: true, IncludeTurbulence: true, Visualize: false, GridResolution: 5, ArrowSeconds: 0.5,
                     RegionCentre: [-20, 0, 8], RegionHalfExtents: [20, 20, 8],
                     Cascades: 4, FinestCell: 2, CellsPerSide: 128, VerticalCells: 32, Extent: 100000, Channels: Channels.map(([, Key]) => Key) },
            Sources: [
                ConstructSource('Tornado', { Name: 'Mesocyclone', Centre: [-21000, 0, 9000], Strength: 24, InnerRadius: 2600, CollarRadius: 5200, Inflow: 8,
                                             Updraft: 12, Height: 9000, Radius: 26000, TrackBearing: 62, TrackSpeed: 12 }),
                ConstructSource('GustFront', { Name: 'Squall line', Centre: [-30000, 0, -14000], Strength: 22, FrontSpeed: 16, CrestDepth: 1800,
                                               HalfWidth: 24000, Height: 1500, Bearing: 118 }),
                ConstructSource('Tornado', { Name: 'Tornado', Centre: [0, 0, 220] }),
                ConstructSource('Microburst', { Name: 'Microburst', Centre: [1800, 260, -900] }),
                ConstructSource('Thermal', { Name: 'Thermal', Centre: [-950, 0, 650] }),
                ConstructSource('Jet', { Name: 'Canyon jet', Centre: [700, 4, 1050], Bearing: 80 }),
                ConstructSource('Puff', { Name: 'Gust puff', Centre: [-420, 12, -480] }),
                ConstructSource('Blast', { Name: 'Blast wave', Centre: [320, 2, -320] }),
                ConstructSource('Wake', { Name: 'Vehicle wake', Centre: [-60, 0.65, -40], Bearing: 20 }),
                ConstructSource('Attractor', { Name: 'Attractor', Centre: [520, 10, 380] }),
                ConstructSource('Repulsor', { Name: 'Repulsor', Centre: [-560, 10, -150] }),
                ConstructSource('Gravity', { Name: 'Gravity' }),
                ConstructSource('Drag', { Name: 'Drag volume', Centre: [1100, 0, 300] }),
                ConstructSource('Microburst', { Name: 'Storm outflow', Centre: [26000, 1800, 24000], Strength: 26, Downdraft: 18, InnerRadius: 2400,
                                                RingRadius: 7000, Radius: 22000, Activation: 'Trigger' })
            ],
            Visualization: { Contribution: 'Flow', Quantity: 'Speed', Mode: 'Map', ScaleMode: 'Automatic', FixedScale: 30, Heatmap: true,
                             HeatmapStyle: 'Smooth', Arrows: true, Tracers: true, Cascades: false, Labels: true, SliceHeight: 10, Opacity: 1,
                             ArrowSpacing: 40, TracerCount: 360, CellPixels: 6, Solo: null },
            Timeline: { Duration: 120, Rate: 1, Loop: true }
        };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  WIND EVALUATION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 2D gradient (Perlin) noise with its analytic gradient and a quintic fade: smooth, free of the value-noise grid
    //    blockiness, so the curl of it reads as round eddies. Returns about [−1, 1] and writes ∂/∂u, ∂/∂v into Gradient.
    function Hash(ColumnIndex, RowIndex, Seed)
    {
        let Mixed = Math.imul(ColumnIndex, 374761393) ^ Math.imul(RowIndex, 668265263) ^ Math.imul(Seed, 1274126177);
        Mixed = Math.imul(Mixed ^ (Mixed >>> 13), 1274126177);
        return (Mixed ^ (Mixed >>> 16)) >>> 0;
    }

    const GradientDirections = new Float64Array(32);
    for (let Index = 0; Index < 16; ++Index)
    {
        GradientDirections[Index * 2] = Math.cos((Index + 0.5) / 16 * Math.PI * 2);
        GradientDirections[Index * 2 + 1] = Math.sin((Index + 0.5) / 16 * Math.PI * 2);
    }

    function NoiseGradient(U, V, Seed, Gradient)
    {
        const ColumnIndex = Math.floor(U), RowIndex = Math.floor(V);
        const X = U - ColumnIndex, Y = V - RowIndex;
        const FadeX = X * X * X * (X * (X * 6 - 15) + 10), FadeY = Y * Y * Y * (Y * (Y * 6 - 15) + 10);
        const SlopeX = 30 * X * X * (X * (X - 2) + 1), SlopeY = 30 * Y * Y * (Y * (Y - 2) + 1);
        const A = (Hash(ColumnIndex, RowIndex, Seed) & 15) * 2, B = (Hash(ColumnIndex + 1, RowIndex, Seed) & 15) * 2;
        const C = (Hash(ColumnIndex, RowIndex + 1, Seed) & 15) * 2, D = (Hash(ColumnIndex + 1, RowIndex + 1, Seed) & 15) * 2;
        const AX = GradientDirections[A], AY = GradientDirections[A + 1], BX = GradientDirections[B], BY = GradientDirections[B + 1];
        const CX = GradientDirections[C], CY = GradientDirections[C + 1], DX = GradientDirections[D], DY = GradientDirections[D + 1];
        const N00 = AX * X + AY * Y, N10 = BX * (X - 1) + BY * Y, N01 = CX * X + CY * (Y - 1), N11 = DX * (X - 1) + DY * (Y - 1);
        const Cross = N00 - N10 - N01 + N11;
        const Reading = N00 + FadeX * (N10 - N00) + FadeY * (N01 - N00) + FadeX * FadeY * Cross;
        Gradient[0] = (AX + FadeX * (BX - AX) + FadeY * (CX - AX) + FadeX * FadeY * (AX - BX - CX + DX) + SlopeX * (N10 - N00 + FadeY * Cross)) * 1.41;
        Gradient[1] = (AY + FadeX * (BY - AY) + FadeY * (CY - AY) + FadeX * FadeY * (AY - BY - CY + DY) + SlopeY * (N01 - N00 + FadeX * Cross)) * 1.41;
        return Reading * 1.41;
    }

    // 📝 Level-of-detail filter: a term whose wavelength is under two cells is faded out (by six cells it is whole), as a
    //    coarse cascade would average it away; without it, gust bands and eddies alias into noise at 100 km.
    const Resolvable = (Wavelength, CellSpan) => Smooth01((Wavelength / Math.max(1e-6, CellSpan) - 2) / 4);

    const NoiseGradientStore = new Float64Array(2);

    // 📝 ForceFieldSpace::EvaluateStepSample's wind: Ekman speed gain and veer with altitude, the coherent two-frequency gust
    //    wave travelling downwind, plus curl eddies (a 2D stream function, divergence-free) drifting at 0.35 × the wind.
    //    MesoscaleScale / MesoscaleStrength are proposed: a slow speed and direction variation over tens of kilometres.
    function EvaluateWind(Wind, Settings, X, Y, Z, Time, CellSpan, Out)
    {
        if (!Wind.Enabled || Wind.Speed <= 0) return;
        const AltitudeKm = Math.max(0, Y - Wind.GroundHeight) / 1000;
        let Bearing = Wind.Bearing + Wind.Veer * AltitudeKm;
        let SpeedGain = Math.max(0.15, 1 + Wind.Shear * Math.min(AltitudeKm, 12));
        const Drift = 0.35 * Wind.Speed * Time;
        const HeadingX = Math.sin(Wind.Bearing * Radian), HeadingZ = -Math.cos(Wind.Bearing * Radian);
        if (Wind.MesoscaleStrength > 0 && Wind.MesoscaleScale > 0)
        {
            const Visible = Resolvable(Wind.MesoscaleScale * 2, CellSpan);
            if (Visible > 0)
            {
                const U = (X - HeadingX * Drift) / Wind.MesoscaleScale, V = (Z - HeadingZ * Drift) / Wind.MesoscaleScale;
                const SpeedShift = NoiseGradient(U, V, Wind.Seed + 11, NoiseGradientStore);
                const TurnShift = NoiseGradient(U + 37.1, V - 19.7, Wind.Seed + 23, NoiseGradientStore);
                SpeedGain *= 1 + Wind.MesoscaleStrength * SpeedShift * Visible;
                Bearing += Wind.MesoscaleStrength * 55 * TurnShift * Visible;
            }
        }
        const DirectionX = Math.sin(Bearing * Radian), DirectionZ = -Math.cos(Bearing * Radian);
        const Along = X * DirectionX + Z * DirectionZ;
        const Phase = 2 * Math.PI * Wind.GustFrequency * Time - 0.011 * Along;
        const GustWave = 0.62 * Math.sin(Phase) * Resolvable(2 * Math.PI / 0.011, CellSpan)
                       + 0.38 * Math.sin(2.17 * Phase + 1.13) * Resolvable(2 * Math.PI / (0.011 * 2.17), CellSpan);
        const Speed = Wind.Speed * SpeedGain * (1 + Wind.GustStrength * GustWave);
        Out[0] += DirectionX * Speed;
        Out[2] += DirectionZ * Speed;
        if (Settings.IncludeTurbulence && Wind.Turbulence > 0)
        {
            const Visible = Resolvable(Wind.TurbulenceScale * 2, CellSpan);
            if (Visible > 0)
            {
                const Scale = Math.max(4, Wind.TurbulenceScale);
                const U = (X - HeadingX * Drift) / Scale, V = (Z - HeadingZ * Drift) / Scale;
                const Gain = Wind.Turbulence * Wind.Speed * 0.45 * Visible;
                NoiseGradient(U, V + Y / Scale * 0.37, Wind.Seed, NoiseGradientStore);
                Out[0] += NoiseGradientStore[1] * Gain;
                Out[2] -= NoiseGradientStore[0] * Gain;
                Out[1] += NoiseGradient(U + 71.3, V + 13.9, Wind.Seed + 5, NoiseGradientStore) * Gain * 0.2;
            }
        }
    }

    // 📝 The wind speed a receiver at the reference point sees over time (the Variability preview and the gust chart).
    function WindSpeedAt(Wind, Settings, X, Y, Z, Time)
    {
        const Out = new Float64Array(3);
        EvaluateWind(Wind, Settings, X, Y, Z, Time, 0.5, Out);
        return Math.hypot(Out[0], Out[1], Out[2]);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                 ACTIVATION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Begins / Lasts / Repeats (eos: Lasts 0 = forever, Repeats 0 = once) shaped by the native ramp-in / ramp-out envelope.
    //    Trigger sources start at their firing time. Returns { Envelope 0–1, Age [s] since the window opened }.
    function ResolveActivation(Source, Time)
    {
        if (!Source.Enabled) return { Envelope: 0, Age: 0, Status: 'Off' };
        if (Source.Activation === 'Always') return { Envelope: 1, Age: Time, Status: 'Active' };
        let Start = Source.Begins;
        if (Source.Activation === 'Trigger')
        {
            if (Source.TriggeredAt === null || Source.TriggeredAt === undefined) return { Envelope: 0, Age: 0, Status: 'Armed' };
            Start = Source.TriggeredAt;
        }
        let Local = Time - Start;
        if (Local < 0) return { Envelope: 0, Age: 0, Status: 'Waiting' };
        if (Source.Activation === 'Window' && Source.Repeats > 0 && Source.Lasts > 0) Local %= Source.Repeats;
        if (Source.Lasts > 0 && Local > Source.Lasts) return { Envelope: 0, Age: Local, Status: Source.Activation === 'Window' && Source.Repeats > 0 ? 'Waiting' : 'Spent' };
        let Envelope = Source.RampIn > 1e-4 ? Smooth01(Local / Source.RampIn) : 1;
        if (Source.Lasts > 0 && Source.RampOut > 1e-4) Envelope *= 1 - Smooth01((Local - (Source.Lasts - Source.RampOut)) / Source.RampOut);
        return { Envelope, Age: Local, Status: Envelope > 1e-3 ? 'Active' : 'Waiting' };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                 SOURCE EVALUATION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Where a moving source is now: tornado columns travel their track, gust puffs drift with the wind at their height.
    function ResolveCentre(Source, Wind, Age)
    {
        if (Source.Type === 'Tornado' && Source.TrackSpeed > 0)
        {
            const Turn = Source.TrackBearing * Radian, Travel = Source.TrackSpeed * Age;
            return [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[1], Source.Centre[2] - Math.cos(Turn) * Travel];
        }
        if (Source.Type === 'Puff' && Source.Drift > 0 && Wind.Enabled)
        {
            const AltitudeKm = Math.max(0, Source.Centre[1] - Wind.GroundHeight) / 1000;
            const Turn = (Wind.Bearing + Wind.Veer * AltitudeKm) * Radian;
            const Travel = Wind.Speed * Math.max(0.15, 1 + Wind.Shear * Math.min(AltitudeKm, 12)) * Source.Drift * Age;
            return [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[1], Source.Centre[2] - Math.cos(Turn) * Travel];
        }
        if (Source.Type === 'GustFront')
        {
            const Turn = Source.Bearing * Radian, Travel = Source.FrontSpeed * Age;
            return [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[1], Source.Centre[2] - Math.cos(Turn) * Travel];
        }
        return Source.Centre;
    }

    // 📝 Reach share at a point: Everywhere 1; Sphere by 3D distance; Column by horizontal distance between the base and the
    //    top (+30 % taper); Box in the source's own frame turned to its bearing. Falloff shapes the edge (eos Falls).
    function ResolveReach(Source, Centre, X, Y, Z, Ground)
    {
        if (Source.Reach === 'Everywhere') return 1;
        const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2];
        // 📝 A microburst's centre is its core altitude; its column stands on the ground and reaches 1.4 × that altitude.
        const ColumnFoot = Source.Type === 'Microburst' ? (Ground ?? 0) : Centre[1];
        const DeltaY = Y - ColumnFoot;
        if (Source.Reach === 'Sphere')
        {
            const Separation = Math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
            return Separation >= Source.Radius ? 0 : Falls(Source.Falloff, Separation, Source.Radius);
        }
        if (Source.Reach === 'Column')
        {
            const Height = Math.max(1, Source.Type === 'Microburst' ? Math.max(Source.Height, (Centre[1] - ColumnFoot) * 1.4) : Source.Height);
            if (DeltaY < -15 || DeltaY > Height * 1.3) return 0;
            const Separation = Math.hypot(DeltaX, DeltaZ);
            if (Separation >= Source.Radius) return 0;
            const Taper = DeltaY <= Height ? 1 : 1 - Smooth01((DeltaY - Height) / (0.3 * Height));
            return Falls(Source.Falloff, Separation, Source.Radius) * Taper;
        }
        const Turn = (Source.Bearing || 0) * Radian;
        const Cosine = Math.cos(Turn), Sine = Math.sin(Turn);
        const Extents = ResolveBoxExtents(Source);
        const LocalX = DeltaX * Cosine + DeltaZ * Sine, LocalZ = -DeltaX * Sine + DeltaZ * Cosine + Extents.OffsetZ;
        const Share = Math.max(Math.abs(LocalX) / Extents.HalfX, Math.abs(LocalZ) / Extents.HalfZ, DeltaY < 0 ? -DeltaY / 5 : DeltaY / Math.max(1, Extents.Height));
        return Share >= 1 ? 0 : Falls(Source.Falloff === 'None' ? 'None' : Source.Falloff, Share, 1);
    }

    // 📝 Boxes follow the type's geometry: a jet's corridor, a wake's trail, otherwise the authored half extents.
    function ResolveBoxExtents(Source)
    {
        if (Source.Type === 'Jet') return { HalfX: Source.InnerRadius * 3.2, HalfZ: Source.Length * 0.75, Height: Source.Centre[1] + Source.InnerRadius * 3.2, OffsetZ: Source.Length * 0.5 };
        if (Source.Type === 'Wake') return { HalfX: Source.HalfWidth * 4, HalfZ: Source.Length * 1.2, Height: Source.Height * 2.4, OffsetZ: -Source.Length };
        return { HalfX: Math.max(0.5, Source.HalfX), HalfZ: Math.max(0.5, Source.HalfZ), Height: Math.max(1, Source.Height), OffsetZ: 0 };
    }

    function EvaluateFlowSource(Source, Wind, X, Y, Z, Age, Envelope, Centre, Out)
    {
        const Ground = Wind.GroundHeight;
        switch (Source.Type)
        {
        case 'Tornado':
        {
            const RelativeY = Y - Centre[1];
            const SpanH = Math.max(10, Source.Height);
            if (RelativeY < -15 || RelativeY > SpanH * 1.3) return;
            const HeightShare = Clamp(RelativeY / SpanH, 0, 1);
            const TopTaper = RelativeY <= SpanH ? 1 : 1 - Smooth01((RelativeY - SpanH) / (0.3 * SpanH));
            const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2];
            const Separation = Math.hypot(DeltaX, DeltaZ);
            const InnerRadius = Math.max(1, Source.InnerRadius + (Source.CollarRadius - Source.InnerRadius) * Smooth01(HeightShare));
            const ρ = Separation / InnerRadius;
            const Profile = (2 * ρ) / (1 + ρ * ρ);
            const Tangential = Envelope * TopTaper * Source.Strength * Profile * (Source.Spin < 0 ? -1 : 1);
            const InflowBand = Math.exp(-Math.max(0, RelativeY) / Math.max(12, 0.16 * SpanH)) - 0.35 * Smooth01((HeightShare - 0.72) / 0.28);
            const Radial = -Envelope * TopTaper * Source.Inflow * InflowBand * Profile;
            const Updraft = Envelope * TopTaper * Source.Updraft * Math.exp(-0.42 * ρ * ρ) * Smooth01(RelativeY / 8 + 0.15);
            const UnitX = Separation > 1e-5 ? DeltaX / Separation : 1, UnitZ = Separation > 1e-5 ? DeltaZ / Separation : 0;
            // 📝 Spin +1 is cyclonic: counter-clockwise seen from above (east of the centre the air moves north, −Z).
            const Turn = Source.TrackBearing * Radian, Carry = Envelope * 0.25 * Source.TrackSpeed;
            Out[0] += UnitZ * Tangential + UnitX * Radial + Math.sin(Turn) * Carry;
            Out[1] += Updraft;
            Out[2] += -UnitX * Tangential + UnitZ * Radial - Math.cos(Turn) * Carry;
            return;
        }
        case 'GustFront':
        {
            const Turn = Source.Bearing * Radian, PropagationX = Math.sin(Turn), PropagationZ = -Math.cos(Turn);
            const Behind = (Centre[0] - X) * PropagationX + (Centre[2] - Z) * PropagationZ;
            const Lateral = Math.abs((X - Centre[0]) * -PropagationZ + (Z - Centre[2]) * PropagationX);
            const Depth = Math.max(5, Source.CrestDepth), RelativeY = Y - Centre[1];
            if (Behind < -0.25 * Depth || Behind > 2.4 * Depth || RelativeY < 0 || RelativeY > Source.Height * 1.25 || Lateral > Source.HalfWidth * 1.2) return;
            const NoseRise = Smooth01((Behind + 0.25 * Depth) / (0.35 * Depth));
            const WakeDecay = Math.exp(-Math.max(0, Behind - 0.1 * Depth) / Depth);
            const HeightEnvelope = 1 - Smooth01(RelativeY / Math.max(5, Source.Height));
            const LateralEnvelope = 1 - Smooth01((Lateral - Source.HalfWidth * 0.8) / (Source.HalfWidth * 0.4));
            const Surge = Envelope * NoseRise * WakeDecay * HeightEnvelope * LateralEnvelope;
            const NoseLift = Envelope * NoseRise * Math.exp(-Math.abs(Behind) / (0.28 * Depth)) * HeightEnvelope * LateralEnvelope * 0.28 * Source.Strength;
            Out[0] += PropagationX * Source.Strength * Surge;
            Out[1] += NoseLift;
            Out[2] += PropagationZ * Source.Strength * Surge;
            return;
        }
        case 'Microburst':
        {
            const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2];
            const Separation = Math.hypot(DeltaX, DeltaZ), RelativeY = Y - Ground;
            const ColumnRadius = Math.max(5, Source.InnerRadius), Ring = Math.max(10, Source.RingRadius);
            if (RelativeY < 0 || RelativeY > Math.max(40, Centre[1] * 1.4)) return;
            const DowndraftColumn = Math.exp(-(Separation * Separation) / (ColumnRadius * ColumnRadius)) * Smooth01(RelativeY / 35);
            const OutRing = (Separation / Ring) * Math.exp(-0.5 * Math.pow(Separation / Ring, 2)) * Math.exp(-RelativeY / Math.max(42, Ring * 0.28));
            const Inverse = Separation > 1e-4 ? 1 / Separation : 0;
            Out[0] += Envelope * Source.Strength * OutRing * DeltaX * Inverse;
            Out[1] -= Envelope * Source.Downdraft * DowndraftColumn;
            Out[2] += Envelope * Source.Strength * OutRing * DeltaZ * Inverse;
            return;
        }
        case 'Thermal':
        {
            const RelativeY = Y - Centre[1], ColumnHeight = Math.max(15, Source.Height);
            if (RelativeY < 0 || RelativeY > ColumnHeight * 1.35) return;
            const HeightShare = Clamp(RelativeY / ColumnHeight, 0, 1.35);
            const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2], Separation = Math.hypot(DeltaX, DeltaZ);
            const PlumeRadius = Math.max(2, Source.InnerRadius * (1 + 0.45 * HeightShare));
            const ρ = Separation / PlumeRadius;
            if (ρ >= 3.5) return;
            const ChimneyEnvelope = Envelope * Math.exp(-0.5 * ρ * ρ) * Smooth01(RelativeY / 10 + 0.15) * (1 - Smooth01((HeightShare - 1) / 0.35));
            const RadialMode = -Smooth01(0.35 - HeightShare) + 0.55 * Smooth01((HeightShare - 0.72) / 0.28);
            const Inverse = Separation > 1e-4 ? 1 / Separation : 0;
            const Radial = Envelope * Source.Inflow * RadialMode * (2 * ρ / (1 + ρ * ρ));
            Out[0] += DeltaX * Inverse * Radial;
            Out[1] += Source.Strength * ChimneyEnvelope;
            Out[2] += DeltaZ * Inverse * Radial;
            return;
        }
        case 'Jet':
        {
            const Turn = Source.Bearing * Radian, AxisX = Math.sin(Turn), AxisZ = -Math.cos(Turn);
            const RelativeX = X - Centre[0], RelativeY = Y - Centre[1], RelativeZ = Z - Centre[2];
            const Axial = RelativeX * AxisX + RelativeZ * AxisZ;
            const Length = Math.max(2, Source.Length), CorridorRadius = Math.max(0.5, Source.InnerRadius);
            if (Axial < -0.2 * CorridorRadius || Axial > 1.25 * Length) return;
            const PerpendicularX = RelativeX - AxisX * Axial, PerpendicularZ = RelativeZ - AxisZ * Axial;
            const RadialSquared = PerpendicularX * PerpendicularX + RelativeY * RelativeY + PerpendicularZ * PerpendicularZ;
            const JetRadius = CorridorRadius * (1 + 0.35 * Math.max(0, Axial) / Length);
            if (RadialSquared >= 9 * JetRadius * JetRadius) return;
            const AxialEnvelope = Smooth01((Axial + 0.2 * CorridorRadius) / (0.4 * CorridorRadius)) * (1 - Smooth01((Axial - 0.85 * Length) / (0.4 * Length)));
            const Gain = Envelope * AxialEnvelope * Math.exp(-0.5 * RadialSquared / (JetRadius * JetRadius)) * Source.Strength;
            Out[0] += AxisX * Gain;
            Out[2] += AxisZ * Gain;
            return;
        }
        case 'Puff':
        {
            const RadiusXZ = Math.max(1.5, Source.Radius), RadiusY = Math.max(1, Source.RadiusY);
            const U = (X - Centre[0]) / RadiusXZ, V = (Y - Centre[1]) / RadiusY, W = (Z - Centre[2]) / RadiusXZ;
            const Squared = U * U + V * V + W * W;
            if (Squared >= 9) return;
            const Gauss = Envelope * Math.exp(-0.5 * Squared);
            const Turn = Source.Bearing * Radian;
            Out[0] += Math.sin(Turn) * Source.Strength * Gauss;
            Out[1] += Source.Lift * Gauss;
            Out[2] += -Math.cos(Turn) * Source.Strength * Gauss;
            return;
        }
        case 'Blast':
        {
            const InitialRadius = Math.max(0.5, Source.InitialRadius);
            const FrontRadius = InitialRadius + Math.max(0, Source.Expansion) * Age;
            const Thickness = Math.max(0.5, Source.Thickness);
            const DeltaX = X - Centre[0], DeltaY = Y - Centre[1], DeltaZ = Z - Centre[2];
            const Separation = Math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
            const FrontShare = (Separation - FrontRadius) / Thickness;
            if (Math.abs(FrontShare) >= 3.2 || Separation < 1e-4) return;
            const Attenuation = Math.pow(InitialRadius / Math.max(InitialRadius, FrontRadius), 0.65);
            const Outward = Source.Strength * Envelope * Attenuation * Math.exp(-0.5 * FrontShare * FrontShare) / Separation;
            Out[0] += DeltaX * Outward;
            Out[1] += DeltaY * Outward;
            Out[2] += DeltaZ * Outward;
            return;
        }
        case 'Wake':
        {
            const Turn = Source.Bearing * Radian, ForwardX = Math.sin(Turn), ForwardZ = -Math.cos(Turn);
            const RightX = -ForwardZ, RightZ = ForwardX;
            const RelativeX = X - Centre[0], RelativeY = Y - Ground, RelativeZ = Z - Centre[2];
            const Trail = -(RelativeX * ForwardX + RelativeZ * ForwardZ);
            const LateralDistance = RelativeX * RightX + RelativeZ * RightZ;
            const WakeLength = Math.max(4, Source.Length), HalfWidth = Math.max(0.4, Source.HalfWidth), WakeHeight = Math.max(0.8, Source.Height);
            if (Trail < -0.5 * HalfWidth || Trail > 2.2 * WakeLength || RelativeY < 0 || RelativeY > 2.4 * WakeHeight) return;
            const Spread = 1 + 0.85 * Math.max(0, Trail) / WakeLength;
            const LongEnvelope = Smooth01((Trail + 0.5 * HalfWidth) / HalfWidth) * Math.exp(-Math.max(0, Trail) / WakeLength);
            const LateralShare = LateralDistance / (HalfWidth * Spread);
            const Weight = Envelope * LongEnvelope * Math.exp(-0.5 * LateralShare * LateralShare) * (1 - Smooth01(RelativeY / (1.8 * WakeHeight * Spread)));
            const Upwash = Math.max(0, (1 - 0.8 * LateralShare * LateralShare) * Source.Circulation);
            const InwardRoll = -LateralShare * 0.65 * Source.Circulation;
            const Tow = Source.Strength * Math.min(1, Source.Speed / 22);
            Out[0] += Weight * (ForwardX * Tow + RightX * InwardRoll);
            Out[1] += Weight * Upwash;
            Out[2] += Weight * (ForwardZ * Tow + RightZ * InwardRoll);
            return;
        }
        case 'Current':
        {
            const Turn = Source.Bearing * Radian;
            Out[0] += Math.sin(Turn) * Source.Strength * Envelope;
            Out[2] += -Math.cos(Turn) * Source.Strength * Envelope;
            return;
        }
        default:
            return;
        }
    }

    // 📝 The eos forcefields.js SampleField for the accelerate types, and the GPU's dipole for the magnetic one.
    function EvaluateAccelerateSource(Source, X, Y, Z, Envelope, Share, Centre, Out)
    {
        const Scale = Source.Strength * Share * Envelope;
        if (Source.Type === 'Gravity') { Out[1] -= Scale; return; }
        if (Source.Type === 'Lift') { Out[1] += Scale; return; }
        const DeltaX = X - Centre[0], DeltaY = Y - Centre[1], DeltaZ = Z - Centre[2];
        const Separation = Math.max(1e-3, Math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ));
        const OutX = DeltaX / Separation, OutY = DeltaY / Separation, OutZ = DeltaZ / Separation;
        if (Source.Type === 'Repulsor') { Out[0] += OutX * Scale; Out[1] += OutY * Scale; Out[2] += OutZ * Scale; return; }
        if (Source.Type === 'Magnetic')
        {
            const Clipped = Math.max(Separation, 0.4);
            const UnitX = DeltaX / Clipped, UnitY = DeltaY / Clipped, UnitZ = DeltaZ / Clipped;
            const Along = UnitZ * 3;
            const FieldX = Along * UnitX, FieldY = Along * UnitY, FieldZ = Along * UnitZ - 1;
            const Length = Math.hypot(FieldX, FieldY, FieldZ) || 1;
            Out[0] += FieldX / Length * Scale; Out[1] += FieldY / Length * Scale; Out[2] += FieldZ / Length * Scale;
            return;
        }
        const TangentLength = Math.hypot(OutZ, OutX) || 1;
        const TangentX = -OutZ / TangentLength, TangentZ = OutX / TangentLength;
        if (Source.Type === 'Orbit') { Out[0] += TangentX * Scale; Out[2] += TangentZ * Scale; return; }
        const Turn = (Source.Swirl || 0) * Share * Envelope;
        Out[0] += -OutX * Scale + TangentX * Turn;
        Out[1] += -OutY * Scale;
        Out[2] += -OutZ * Scale + TangentZ * Turn;
    }

    // 📝 Everything about a source that depends on time only — envelope, age, where it is now and a horizontal bound for
    //    cheap rejection — is resolved once per refresh, not once per sample (a 150 × 95 lattice is 14 000 samples).
    function PrepareSource(Source, Wind, Time)
    {
        const Type = TypeById[Source.Type];
        if (!Type) return null;
        const Activation = ResolveActivation(Source, Time);
        const Centre = ResolveCentre(Source, Wind, Activation.Age);
        let BoundX = Centre[0], BoundZ = Centre[2], Bound = Infinity;
        if (Source.Type === 'GustFront')
        {
            const Turn = Source.Bearing * Radian;
            BoundX -= Math.sin(Turn) * Source.CrestDepth * 1.1;
            BoundZ += Math.cos(Turn) * Source.CrestDepth * 1.1;
            Bound = Math.hypot(Source.HalfWidth * 1.2, Source.CrestDepth * 1.5);
        }
        else if (Source.Type === 'Blast') Bound = Math.min(Source.Radius, Source.InitialRadius + Source.Expansion * Activation.Age + 3.2 * Source.Thickness);
        else if (Source.Type === 'Puff') Bound = Math.min(Source.Radius * 3, Source.Reach === 'Sphere' ? Math.max(Source.Radius, Source.Radius * 3) : Infinity);
        else if (Source.Reach === 'Box')
        {
            const Extents = ResolveBoxExtents(Source), Turn = (Source.Bearing || 0) * Radian;
            BoundX += Math.sin(Turn) * Extents.OffsetZ;
            BoundZ -= Math.cos(Turn) * Extents.OffsetZ;
            Bound = Math.hypot(Extents.HalfX, Extents.HalfZ);
        }
        else if (Source.Reach !== 'Everywhere') Bound = Source.Radius;
        return { Source, Type, Contribution: Type.Contribution, Envelope: Activation.Envelope, Age: Activation.Age, Status: Activation.Status,
                 Centre, BoundX, BoundZ, BoundSquared: Bound * Bound };
    }

    function Prepare(Scene, Time)
    {
        const Entries = [];
        for (const Source of Scene.Sources)
        {
            const Entry = PrepareSource(Source, Scene.Wind, Time);
            if (Entry) Entries.push(Entry);
        }
        return { Time, Entries };
    }

    const FlowScratch = new Float64Array(3);

    // 📝 One prepared source's contribution at a point, in its own units (callers keep the three contributions apart).
    function EvaluateEntry(Entry, Wind, X, Y, Z, Out)
    {
        if (Entry.Envelope <= 1e-5) return false;
        const DeltaX = X - Entry.BoundX, DeltaZ = Z - Entry.BoundZ;
        if (DeltaX * DeltaX + DeltaZ * DeltaZ > Entry.BoundSquared) return false;
        const { Source, Envelope, Age, Centre } = Entry;
        if (Entry.Contribution === 'Flow')
        {
            const Share = Source.Reach === 'Everywhere' || Source.Type === 'GustFront' ? 1 : ResolveReach(Source, Centre, X, Y, Z, Wind.GroundHeight);
            if (Share <= 0) return false;
            if (Share >= 0.999) { EvaluateFlowSource(Source, Wind, X, Y, Z, Age, Envelope, Centre, Out); return true; }
            FlowScratch[0] = FlowScratch[1] = FlowScratch[2] = 0;
            EvaluateFlowSource(Source, Wind, X, Y, Z, Age, Envelope, Centre, FlowScratch);
            Out[0] += FlowScratch[0] * Share; Out[1] += FlowScratch[1] * Share; Out[2] += FlowScratch[2] * Share;
            return true;
        }
        const Share = ResolveReach(Source, Centre, X, Y, Z, Wind.GroundHeight);
        if (Share <= 0) return false;
        if (Entry.Contribution === 'Accelerate') EvaluateAccelerateSource(Source, X, Y, Z, Envelope, Share, Centre, Out);
        else Out[0] += Source.Strength * Share * Envelope;
        return true;
    }

    function EvaluateSource(Source, Wind, X, Y, Z, Time, Out)
    {
        const Entry = PrepareSource(Source, Wind, Time);
        return Entry ? EvaluateEntry(Entry, Wind, X, Y, Z, Out) : false;
    }

    // 📝 The whole field at a point for one contribution (Damp writes its scalar rate into Out[0]). Prepared comes from
    //    Prepare(Scene, Time); Solo restricts the sum to one source token, or to 'Wind'.
    function EvaluatePrepared(Scene, Prepared, Contribution, X, Y, Z, CellSpan, Out, Solo)
    {
        if (!Scene.Field.Enabled) return true;
        if (Contribution === 'Flow' && (!Solo || Solo === 'Wind')) EvaluateWind(Scene.Wind, Scene.Field, X, Y, Z, Prepared.Time, CellSpan, Out);
        if (Solo === 'Wind') return true;
        for (const Entry of Prepared.Entries)
        {
            if (Entry.Contribution !== Contribution) continue;
            if (Solo && Entry.Source.Token !== Solo) continue;
            EvaluateEntry(Entry, Scene.Wind, X, Y, Z, Out);
        }
        return true;
    }

    function Evaluate(Scene, Contribution, X, Y, Z, Time, CellSpan, Out, Solo)
    {
        return EvaluatePrepared(Scene, Prepare(Scene, Time), Contribution, X, Y, Z, CellSpan, Out, Solo);
    }

    // 📝 The type's radial profile for the inspector's hero plot: reading against distance from the centre at the slice height.
    function ResolveProfile(Scene, Source, SliceHeight)
    {
        const Type = TypeById[Source.Type];
        const Reach = Source.Type === 'GustFront' ? Source.CrestDepth * 3 : Source.Type === 'Blast' ? Math.max(Source.Radius * 0.4, 60) :
                      Source.Reach === 'Box' ? Math.max(ResolveBoxExtents(Source).HalfX, ResolveBoxExtents(Source).HalfZ) * 1.2 : Source.Radius * 1.15;
        const Points = [];
        const Probe = Object.assign({}, Source, { Enabled: true, Activation: 'Always', TrackSpeed: 0, Drift: 0, FrontSpeed: 0 });
        const Steps = 72;
        for (let Step = 0; Step <= Steps; ++Step)
        {
            const Separation = Step / Steps * Reach;
            const Out = new Float64Array(3);
            let X = Source.Centre[0] + Separation, Z = Source.Centre[2];
            if (Source.Type === 'GustFront') { X = Source.Centre[0] - Math.sin(Source.Bearing * Radian) * (Separation - Reach * 0.2); Z = Source.Centre[2] + Math.cos(Source.Bearing * Radian) * (Separation - Reach * 0.2); }
            else if (Source.Type === 'Jet' || Source.Type === 'Current' || Source.Type === 'Wake' || Source.Type === 'Drag')
            {
                const Axis = Source.Type === 'Wake' ? Source.Bearing + 180 : Source.Type === 'Jet' ? Source.Bearing : Source.Bearing + 90;
                X = Source.Centre[0] + Math.sin(Axis * Radian) * Separation;
                Z = Source.Centre[2] - Math.cos(Axis * Radian) * Separation;
            }
            const Height = Source.Type === 'Blast' || Type.Contribution !== 'Flow' ? Source.Centre[1] : Source.Type === 'Puff' || Source.Type === 'Jet' ? Source.Centre[1] : SliceHeight;
            const BlastTime = Source.Type === 'Blast' ? Math.max(0, (Reach * 0.5 - Source.InitialRadius) / Math.max(1, Source.Expansion)) : 0;
            EvaluateSource(Probe, Scene.Wind, X, Height, Z, BlastTime, Out);
            const Reading = Type.Contribution === 'Damp' ? Out[0] : Math.hypot(Out[0], Out[1], Out[2]);
            Points.push([Separation, Reading]);
        }
        return { Points, Reach, Unit: Contributions[Type.Contribution].Unit };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    LEVELSPACE
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Keys ForceFieldRecord and WindRecord already read in SceneWorld.cpp; everything else is proposed for the C++ step.
    const ExistingKeys = new Set(['identity', 'type', 'name', 'parent', 'enabled', 'visualize', 'include_turbulence', 'centre_metres',
        'half_extents_metres', 'grid_resolution', 'arrow_seconds', 'direction_deg', 'speed_mps', 'turbulence', 'gust_frequency_hz',
        'turbulence_scale_metres', 'gust_strength', 'shear_per_kilometre', 'veer_degrees_per_kilometre', 'advect_clouds', 'advect_plumes',
        'advect_particles']);

    const FormatNumber = Reading => Number.isInteger(Reading) ? Reading.toFixed(1) : String(Number(Reading.toFixed(4)));
    const FormatTriple = Triple => `[${Triple.map(FormatNumber).join(', ')}]`;

    function EncodeSource(Source, Lines)
    {
        const Type = TypeById[Source.Type];
        Lines.push('', '[[records]]', `identity = "${Source.Token}"`, 'type = "ForceSource"', `name = "${Source.Name}"`, 'parent = "rec_force_field"',
                   '[records.configuration]', `enabled = ${Source.Enabled}`, `source = "${Type.Native}"`, `contribution = "${Type.Contribution.toLowerCase()}"`,
                   `centre_metres = ${FormatTriple(Source.Centre)}`, `reach = "${Source.Reach.toLowerCase()}"`, `reach_metres = ${FormatNumber(Source.Radius)}`,
                   `falloff = "${Source.Falloff.toLowerCase()}"`, `strength = ${FormatNumber(Source.Strength)}`);
        for (const Parameter of Type.Parameters)
        {
            if (Parameter.Key === 'Strength') continue;
            Lines.push(`${Parameter.Key.replace(/([a-z])([A-Z])/g, '$1_$2').toLowerCase()} = ${FormatNumber(Source[Parameter.Key])}`);
        }
        for (const [Key] of Type.Bearings || []) Lines.push(`${Key.replace(/([a-z])([A-Z])/g, '$1_$2').toLowerCase()}_deg = ${FormatNumber(Source[Key])}`);
        if (Type.Spin) Lines.push(`spin = ${Source.Spin < 0 ? '"anticyclonic"' : '"cyclonic"'}`);
        if (Source.Reach === 'Column' || Source.Reach === 'Box') Lines.push(`height_metres = ${FormatNumber(Source.Height)}`);
        if (Source.Reach === 'Box' && Source.Type !== 'Jet' && Source.Type !== 'Wake') Lines.push(`half_extents_metres = [${FormatNumber(Source.HalfX)}, ${FormatNumber(Source.HalfZ)}]`);
        Lines.push(`activation = "${Source.Activation.toLowerCase()}"`, `begins_seconds = ${FormatNumber(Source.Begins)}`, `lasts_seconds = ${FormatNumber(Source.Lasts)}`,
                   `repeats_seconds = ${FormatNumber(Source.Repeats)}`, `ramp_in_seconds = ${FormatNumber(Source.RampIn)}`, `ramp_out_seconds = ${FormatNumber(Source.RampOut)}`,
                   `acts_on = [${Source.Acts.map(Key => `"${Key.toLowerCase()}"`).join(', ')}]`);
    }

    function EncodeLevelspace(Scene, Only)
    {
        const Lines = [];
        const { Field, Wind } = Scene;
        if (!Only || Only === 'Field')
        {
            Lines.push('[[records]]', 'identity = "rec_force_field"', 'type = "ForceField"', 'name = "Force Field"', '[records.configuration]',
                       `enabled = ${Field.Enabled}`, `visualize = ${Field.Visualize}`, `include_turbulence = ${Field.IncludeTurbulence}`,
                       `centre_metres = ${FormatTriple(Field.RegionCentre)}`, `half_extents_metres = ${FormatTriple(Field.RegionHalfExtents)}`,
                       `grid_resolution = ${Field.GridResolution}`, `arrow_seconds = ${FormatNumber(Field.ArrowSeconds)}`,
                       `cascades = ${Field.Cascades}`, `finest_cell_metres = ${FormatNumber(Field.FinestCell)}`, `cells_per_side = ${Field.CellsPerSide}`,
                       `vertical_cells = ${Field.VerticalCells}`, `extent_metres = ${FormatNumber(Field.Extent)}`,
                       `channels = [${Field.Channels.map(Key => `"${Key.toLowerCase()}"`).join(', ')}]`);
        }
        if (!Only || Only === 'Wind')
        {
            if (Lines.length) Lines.push('');
            Lines.push('[[records]]', 'identity = "rec_wind"', 'type = "Wind"', 'name = "Wind"', 'parent = "rec_force_field"', '[records.configuration]',
                       `enabled = ${Wind.Enabled}`, `direction_deg = ${FormatNumber(Wind.Bearing)}`, `speed_mps = ${FormatNumber(Wind.Speed)}`,
                       `turbulence = ${FormatNumber(Wind.Turbulence)}`, `turbulence_scale_metres = ${FormatNumber(Wind.TurbulenceScale)}`,
                       `gust_strength = ${FormatNumber(Wind.GustStrength)}`, `gust_frequency_hz = ${FormatNumber(Wind.GustFrequency)}`,
                       `shear_per_kilometre = ${FormatNumber(Wind.Shear)}`, `veer_degrees_per_kilometre = ${FormatNumber(Wind.Veer)}`,
                       `advect_clouds = ${Wind.AdvectClouds}`, `advect_plumes = ${Wind.AdvectPlumes}`, `advect_particles = ${Wind.AdvectParticles}`,
                       `ground_height_metres = ${FormatNumber(Wind.GroundHeight)}`, `seed = ${Wind.Seed}`,
                       `mesoscale_scale_metres = ${FormatNumber(Wind.MesoscaleScale)}`, `mesoscale_strength = ${FormatNumber(Wind.MesoscaleStrength)}`);
        }
        if (!Only) for (const Source of Scene.Sources) EncodeSource(Source, Lines);
        else if (Only !== 'Field' && Only !== 'Wind') { const Source = Scene.Sources.find(One => One.Token === Only); if (Source) { EncodeSource(Source, Lines); Lines.shift(); } }
        return Lines;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  DERIVED READINGS
    //------------------------------------------------------------------------------------------------------------------------

    const BeaufortLimits = [0.5, 1.6, 3.4, 5.5, 8.0, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7];
    const BeaufortNames = ['Calm', 'Light air', 'Light breeze', 'Gentle breeze', 'Moderate breeze', 'Fresh breeze', 'Strong breeze', 'Near gale', 'Gale',
                           'Strong gale', 'Storm', 'Violent storm', 'Hurricane force'];

    function ResolveBeaufort(Speed)
    {
        let Force = 0;
        while (Force < BeaufortLimits.length && Speed >= BeaufortLimits[Force]) Force++;
        return { Force, Name: BeaufortNames[Force] };
    }

    // 📝 Which sources the lattice can hold and which each receiver must evaluate (eos Bakeable): selective flow fields
    //    (an Acts-on list) cannot share one texture with the fields they exclude.
    function ResolveBudget(Scene)
    {
        const Baked = [], Live = [], Damp = [];
        for (const Source of Scene.Sources)
        {
            const Type = TypeById[Source.Type];
            if (!Type) continue;
            if (Type.Contribution === 'Damp') Damp.push(Source);
            else if (Type.Contribution === 'Flow' && !Source.Acts.length) Baked.push(Source);
            else Live.push(Source);
        }
        const Field = Scene.Field;
        const CascadeBytes = Field.CellsPerSide * Field.CellsPerSide * Field.VerticalCells * 8;
        const Levels = [];
        for (let Level = 0; Level < Field.Cascades; ++Level)
        {
            const Cell = Field.FinestCell * Math.pow(8, Level);
            Levels.push({ Cell, Extent: Cell * Field.CellsPerSide, Refresh: Math.pow(4, Level) });
        }
        return { Baked, Live, Damp, CascadeBytes, TotalBytes: CascadeBytes * Field.Cascades, Levels, GpuSlots: 16 };
    }

    return {
        Contributions, Channels, Reaches, Falloffs, Activations, Types, TypeById, Falls, ConstructSource, ConstructDefaultField,
        EvaluateWind, WindSpeedAt, ResolveActivation, ResolveCentre, ResolveReach, ResolveBoxExtents, EvaluateSource, Evaluate,
        Prepare, PrepareSource, EvaluatePrepared, EvaluateEntry,
        ResolveProfile, EncodeLevelspace, ExistingKeys, ResolveBeaufort, ResolveBudget, Smooth01, Radian
    };
})();
