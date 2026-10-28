/*====================================================================================================================================
                                                 CATALOGUESPECIFICATIONS.JS
====================================================================================================================================*/
// 🧩 The spawnable liquid-node catalogue, port colour table, and per-category glyph assignment

//------------------------------------------------------------------------------------------------------------------------
//                                                    PORT CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 A connection is admissible when either side is 'any', otherwise the classifications must match.
//    The liquid graph carries four payloads the terrain graph had no use for: a `volume` is the live
//    simulation state handed from stage to stage, a `region` is a bounded shape used as an emitter or a
//    collider, a `force` is an acceleration field, and a `surface` is the reconstructed water boundary
//    ready for shading. Keeping them distinct is what stops a collider being wired into a shader input.
export const PortDotClass =
{
    float:   'DotFloat',
    vec2:    'DotVec2',
    vec3:    'DotVec3',
    color:   'DotColor',
    volume:  'DotVolume',
    region:  'DotRegion',
    force:   'DotForce',
    surface: 'DotSurface',
    any:     'DotAny'
};

export function ResolvePortDotClass(PortClassification)
{
    return PortDotClass[PortClassification] || PortDotClass.any;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CATEGORY PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

export const CategoryPresentation =
{
    input:     { Glyph: 'Pointer',    GlyphClass: 'GlyphInput' },
    emitter:   { Glyph: 'Spout',      GlyphClass: 'GlyphEmitter' },
    collider:  { Glyph: 'Obstacle',   GlyphClass: 'GlyphCollider' },
    force:     { Glyph: 'Current',    GlyphClass: 'GlyphForce' },
    solver:    { Glyph: 'Lattice',    GlyphClass: 'GlyphSolver' },
    surface:   { Glyph: 'Ripple',     GlyphClass: 'GlyphSurface' },
    math:      { Glyph: 'Calculator', GlyphClass: 'GlyphMath' },
    utility:   { Glyph: 'Configure',  GlyphClass: 'GlyphUtility' }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    NODE CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The graph reads left to right as the simulation itself runs: sources fill a domain, forces and
//    colliders perturb it, the solver advances it, and the surface stage turns the result into something
//    renderable. A node's inbound ports are its dials, in the order the panel lists them.
export const NodeCatalogue =
[
    {
        Category: 'input', Glyph: 'Pointer', Naming: 'Inputs & Controls',
        Items:
        [
            { Token: 'input_value',  Naming: 'Value (Float)', Summary: 'Constant float value', OutboundPort: 'float', InboundPorts: [] },
            { Token: 'input_slider', Naming: 'Slider',        Summary: '0-1 value slider',     OutboundPort: 'float', InboundPorts: [] },
            { Token: 'input_vec2',   Naming: 'Vector 2D',     Summary: 'X, Y values',          OutboundPort: 'vec2',  InboundPorts: [] },
            { Token: 'input_vec3',   Naming: 'Vector 3D',     Summary: 'X, Y, Z values',       OutboundPort: 'vec3',  InboundPorts: [] },
            { Token: 'input_radial', Naming: 'Radial Angles', Summary: 'Radial slider',        OutboundPort: 'vec2',  InboundPorts: [] },
            { Token: 'input_box',    Naming: 'Box Control',   Summary: '2D coordinate box',    OutboundPort: 'vec2',  InboundPorts: [] }
        ]
    },
    {
        Category: 'emitter', Glyph: 'Spout', Naming: 'Sources & Emitters',
        Items:
        [
            // 📝 A dam break is the canonical liquid test: a resting block of water released at t=0. It
            //    seeds the domain once rather than emitting continuously, so it carries no rate dial.
            { Token: 'emit_dambreak', Naming: 'Dam Break',   Summary: 'Released block of water',  OutboundPort: 'volume', InboundPorts: ['vec3','vec3','float'] },
            { Token: 'emit_volume',   Naming: 'Volume Fill', Summary: 'Prefill a region',         OutboundPort: 'volume', InboundPorts: ['region','float'] },
            { Token: 'emit_inflow',   Naming: 'Inflow Jet',  Summary: 'Continuous stream',        OutboundPort: 'volume', InboundPorts: ['vec3','vec3','float','float'] },
            { Token: 'emit_drop',     Naming: 'Droplet',     Summary: 'Falling sphere of water',  OutboundPort: 'volume', InboundPorts: ['vec3','float','float'] },
            { Token: 'emit_pool',     Naming: 'Still Pool',  Summary: 'Flat resting level',       OutboundPort: 'volume', InboundPorts: ['float'] },
            { Token: 'emit_drain',    Naming: 'Drain',       Summary: 'Removes liquid in region', OutboundPort: 'volume', InboundPorts: ['volume','region','float'] }
        ]
    },
    {
        Category: 'collider', Glyph: 'Obstacle', Naming: 'Colliders & Regions',
        Items:
        [
            { Token: 'coll_box',     Naming: 'Box Collider',    Summary: 'Axis-aligned solid',    OutboundPort: 'region', InboundPorts: ['vec3','vec3'] },
            { Token: 'coll_sphere',  Naming: 'Sphere Collider', Summary: 'Spherical solid',       OutboundPort: 'region', InboundPorts: ['vec3','float'] },
            { Token: 'coll_terrain', Naming: 'Terrain Bed',     Summary: 'Height-field floor',    OutboundPort: 'region', InboundPorts: ['float','float','float'] },
            { Token: 'coll_paddle',  Naming: 'Moving Paddle',   Summary: 'Animated stirrer',      OutboundPort: 'region', InboundPorts: ['vec3','vec3','float'] },
            { Token: 'coll_apply',   Naming: 'Apply Collider',  Summary: 'Bind solid to the sim', OutboundPort: 'volume', InboundPorts: ['volume','region','float'] }
        ]
    },
    {
        Category: 'force', Glyph: 'Current', Naming: 'Forces',
        Items:
        [
            { Token: 'force_gravity',    Naming: 'Gravity',    Summary: 'Uniform acceleration',     OutboundPort: 'force',  InboundPorts: ['vec3'] },
            { Token: 'force_wind',       Naming: 'Wind',       Summary: 'Directional surface drag', OutboundPort: 'force',  InboundPorts: ['vec3','float'] },
            { Token: 'force_vortex',     Naming: 'Vortex',     Summary: 'Swirl about an axis',      OutboundPort: 'force',  InboundPorts: ['vec3','vec3','float'] },
            { Token: 'force_turbulence', Naming: 'Turbulence', Summary: 'Curl-noise agitation',     OutboundPort: 'force',  InboundPorts: ['float','float','float'] },
            { Token: 'force_apply',      Naming: 'Apply Force',Summary: 'Bind force to the sim',    OutboundPort: 'volume', InboundPorts: ['volume','force','float'] }
        ]
    },
    {
        Category: 'solver', Glyph: 'Lattice', Naming: 'Solver',
        Items:
        [
            // 📝 The blend dial is the PIC/FLIP ratio: 0 is pure PIC (heavily damped, reads as viscous),
            //    1 is pure FLIP (energetic but noisy). Production values sit near 0.95.
            { Token: 'solve_flip',      Naming: 'FLIP Solver',    Summary: 'PIC/FLIP advection',              OutboundPort: 'volume', InboundPorts: ['volume','force','float','float'] },
            { Token: 'solve_pressure',  Naming: 'Pressure Solve', Summary: 'Make the field divergence-free',  OutboundPort: 'volume', InboundPorts: ['volume','float','float'] },
            { Token: 'solve_viscosity', Naming: 'Viscosity',      Summary: 'Momentum diffusion',              OutboundPort: 'volume', InboundPorts: ['volume','float'] },
            { Token: 'solve_domain',    Naming: 'Domain',         Summary: 'Grid extent and cell size',       OutboundPort: 'volume', InboundPorts: ['volume','float','float'] },
            { Token: 'solve_timestep',  Naming: 'Time Step',      Summary: 'CFL and substepping',             OutboundPort: 'volume', InboundPorts: ['volume','float','float'] }
        ]
    },
    {
        Category: 'surface', Glyph: 'Ripple', Naming: 'Surfacing & Look',
        Items:
        [
            { Token: 'surf_reconstruct', Naming: 'Reconstruct',   Summary: 'Particles to a surface',     OutboundPort: 'surface', InboundPorts: ['volume','float','float'] },
            { Token: 'surf_water',       Naming: 'Water Shading', Summary: 'Refraction and absorption',  OutboundPort: 'surface', InboundPorts: ['surface','color','float','float'] },
            { Token: 'surf_foam',        Naming: 'Foam & Spray',  Summary: 'Whitewater from curvature',  OutboundPort: 'surface', InboundPorts: ['surface','float','float'] },
            { Token: 'surf_output',      Naming: 'Output',        Summary: 'Final rendered surface',     OutboundPort: 'any',     InboundPorts: ['surface'] }
        ]
    },
    {
        Category: 'math', Glyph: 'Calculator', Naming: 'Math & Logic',
        Items:
        [
            { Token: 'math_add',   Naming: 'Add',      Summary: 'Sum two inputs',          OutboundPort: 'float', InboundPorts: ['float','float'] },
            { Token: 'math_sub',   Naming: 'Subtract', Summary: 'Subtract inputs',         OutboundPort: 'float', InboundPorts: ['float','float'] },
            { Token: 'math_mult',  Naming: 'Multiply', Summary: 'Multiply two inputs',     OutboundPort: 'float', InboundPorts: ['float','float'] },
            { Token: 'math_div',   Naming: 'Divide',   Summary: 'Divide inputs',           OutboundPort: 'float', InboundPorts: ['float','float'] },
            { Token: 'math_clamp', Naming: 'Clamp',    Summary: 'Constrain value range',   OutboundPort: 'float', InboundPorts: ['float','float','float'] },
            { Token: 'math_sine',  Naming: 'Sine',     Summary: 'Trigonometric sine wave', OutboundPort: 'float', InboundPorts: ['float'] },
            { Token: 'math_cos',   Naming: 'Cosine',   Summary: 'Trigonometric cos wave',  OutboundPort: 'float', InboundPorts: ['float'] },
            { Token: 'math_log',   Naming: 'Log',      Summary: 'Logarithm base 10',       OutboundPort: 'float', InboundPorts: ['float'] }
        ]
    },
    {
        Category: 'utility', Glyph: 'Configure', Naming: 'Utilities',
        Items:
        [
            { Token: 'util_timer',  Naming: 'Timer',       Summary: 'Simulation clock',  OutboundPort: 'float', InboundPorts: [] },
            { Token: 'util_log',    Naming: 'Console Log', Summary: 'Debug values',      OutboundPort: 'any',   InboundPorts: ['any'] },
            { Token: 'util_viewer', Naming: 'Preview',     Summary: 'Value visualizer',  OutboundPort: 'any',   InboundPorts: ['any'] }
        ]
    }
];

//------------------------------------------------------------------------------------------------------------------------
//                                                 PORT LABEL OVERRIDES
//------------------------------------------------------------------------------------------------------------------------

// 🔴 These labels are load-bearing, not decoration. The viewport feed resolves every dial BY LABEL
//    (`ResolveDial(Entry, 'Blend', ...)`), so renaming one here silently unbinds it from the solver —
//    the node keeps its port, the sim keeps its default, and nothing reports a fault. Rename in both
//    places or neither.
const InboundLabelOverrides =
{
    math_add:   ['A', 'B'],
    math_sub:   ['A', 'B'],
    math_mult:  ['A', 'B'],
    math_div:   ['A', 'B'],
    math_clamp: ['Value', 'Min', 'Max'],
    math_sine:  ['Value'],
    math_cos:   ['Value'],
    math_log:   ['Value'],

    emit_dambreak: ['Origin', 'Extent', 'Fill'],
    emit_volume:   ['Region', 'Fill'],
    emit_inflow:   ['Origin', 'Direction', 'Speed', 'Radius'],
    emit_drop:     ['Centre', 'Radius', 'Speed'],
    emit_pool:     ['Level'],
    emit_drain:    ['Volume', 'Region', 'Rate'],

    coll_box:     ['Origin', 'Extent'],
    coll_sphere:  ['Centre', 'Radius'],
    coll_terrain: ['Height', 'Roughness', 'Seed'],
    coll_paddle:  ['Centre', 'Extent', 'Rate'],
    coll_apply:   ['Volume', 'Region', 'Friction'],

    force_gravity:    ['Acceleration'],
    force_wind:       ['Direction', 'Strength'],
    force_vortex:     ['Centre', 'Axis', 'Strength'],
    force_turbulence: ['Strength', 'Scale', 'Seed'],
    force_apply:      ['Volume', 'Force', 'Scale'],

    solve_flip:      ['Volume', 'Force', 'Blend', 'Substeps'],
    solve_pressure:  ['Volume', 'Iterations', 'Tolerance'],
    solve_viscosity: ['Volume', 'Viscosity'],
    solve_domain:    ['Volume', 'Extent', 'Resolution'],
    solve_timestep:  ['Volume', 'Step', 'CFL'],

    surf_reconstruct: ['Volume', 'Radius', 'Smoothing'],
    surf_water:       ['Surface', 'Absorption', 'Depth', 'Roughness'],
    surf_foam:        ['Surface', 'Threshold', 'Lifetime'],
    surf_output:      ['Surface']
};

export function ResolveInboundLabels(CatalogueToken, InboundTally)
{
    const Overrides = InboundLabelOverrides[CatalogueToken];

    const Labels = [];
    for (let PortIndex = 0; PortIndex < InboundTally; PortIndex++)
    {
        Labels.push((Overrides && Overrides[PortIndex]) || `In ${PortIndex + 1}`);
    }
    return Labels;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENTRY CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

export const OperatorTokens  = ['math_add', 'math_sub', 'math_mult', 'math_div'];
export const RegulatorTokens = ['input_value', 'input_slider', 'input_vec2', 'input_vec3', 'input_radial', 'input_box'];
export const AxisTokens      = ['input_vec2', 'input_vec3', 'input_box'];
export const SweepTokens     = ['input_slider', 'input_value'];

// 📝 Every node the viewport can preview. The terrain editor called this TerrainTokens and keyed it to
//    noise generators; here it is any node yielding a simulatable volume or a surface, because choosing
//    any stage of the chain should show the sim rather than blank the frame.
export const LiquidTokens =
[
    'emit_dambreak', 'emit_volume', 'emit_inflow', 'emit_drop', 'emit_pool', 'emit_drain',
    'coll_apply', 'force_apply',
    'solve_flip', 'solve_pressure', 'solve_viscosity', 'solve_domain', 'solve_timestep',
    'surf_reconstruct', 'surf_water', 'surf_foam', 'surf_output'
];

// The emitter tokens that seed the domain — one must be present for anything to be simulated.
export const SourceTokens = ['emit_dambreak', 'emit_volume', 'emit_inflow', 'emit_drop', 'emit_pool'];

// A compact (math-shaped) entry is any math operation or any input regulator.
export function CompactEntryCondition(CatalogueToken)
{
    if (!CatalogueToken) return false;
    return CatalogueToken.startsWith('math_') || CatalogueToken.startsWith('input_');
}
