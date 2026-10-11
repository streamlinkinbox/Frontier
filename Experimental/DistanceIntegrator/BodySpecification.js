//============================================================================================================================================
//                                                            BODYSPECIFICATION.JS
//============================================================================================================================================
// 📦 Closed quad bodywork, wheel arches, cavities, thin cooling fins and a non-rigid deformation for sampled-field transport.

import
{
    FacetNormal,
    IntersectFacet
}
from '../DeformationIntegrator/DeformationSpecification.js';
export
{
    FacetNormal,
    IntersectFacet
};
export const DomainMinimum = [-3, -.3, -3.6];
export const DomainMaximum = [3, 3.3, 3.6];
export const Colours = [
    [.18, .48, .56],
    [.27, .3, .32],
    [.72, .23, .07],
    [.52, .53, .5],
    [.065, .19, .25],
    [.36, .12, .055],
    [.055, .065, .075]
];
export const Defaults = {
    Amount: .55,
    Animate: false,
    Resolution: 64,
    View: 1,
    Rays: 4,
    Scale: .6,
    Exposure: 1.15,
    Power: 1,
    Slice: .48,
    Axis: 1,
    Frozen: false,
    Counters: false
};

export function DeformPosition(Position, Amount)
{
    if (Position[3] === 0) return [...Position];
    const Fraction = Math.max(0, Math.min(1, (Position[2] + .6) / 3.4));
    const Weight = Fraction * Fraction * (3 - 2 * Fraction);
    return [Position[0] * (1 + .14 * Amount * Weight) + .14 * Amount * Weight * Math.sin(Position[2] * 3.2),
        Position[1] + Amount * Weight * (.15 * Math.sin(Position[2] * 4.2) + .11 * Math.cos(Position[0] * 2.7)),
        Position[2] - Amount * Weight, Position[3]
    ];
}

export function ConstructScene()
{
    const Vertices = [],
        Quads = [],
        Facets = [],
        Parts = [];

    function Part(Name, Material, Thickness)
    {
        const Component = Parts.length + 1;
        if (Component > 32) throw new Error('Parity classification supports at most 32 closed components');
        Parts.push(
        {
            Name,
            Component,
            Material,
            Thickness
        });
        return Component;
    }

    function Quad(Corners, Material, Component)
    {
        Quads.push(
        {
            Corners,
            Material,
            Component
        });
        Facets.push([Corners[0], Corners[1], Corners[2], Material, Component]);
        Facets.push([Corners[0], Corners[2], Corners[3], Material + 128, Component]);
    }

    function Patch(Name, Surface, Thickness, Columns, Rows, Material)
    {
        const Component = Part(Name, Material, Math.hypot(...Thickness)),
            Start = Vertices.length;
        const LayerSize = (Columns + 1) * (Rows + 1);
        const Address = (Column, Row, Layer) => Start + Layer * LayerSize + Row * (Columns + 1) + Column;
        for (let Layer = 0; Layer < 2; Layer++)
            for (let Row = 0; Row <= Rows; Row++)
                for (let Column = 0; Column <= Columns; Column++)
                {
                    const Position = Surface(Column / Columns, Row / Rows);
                    Vertices.push([...Position.map((Coordinate, Axis) => Coordinate - Layer * Thickness[Axis]), 1]);
                }
        for (let Row = 0; Row < Rows; Row++)
            for (let Column = 0; Column < Columns; Column++)
            {
                Quad([Address(Column, Row, 0), Address(Column, Row + 1, 0), Address(Column + 1, Row + 1, 0), Address(
                    Column + 1, Row, 0)], Material, Component);
                Quad([Address(Column, Row, 1), Address(Column + 1, Row, 1), Address(Column + 1, Row + 1, 1), Address(
                    Column, Row + 1, 1)], Material, Component);
            }
        const Perimeter = [];
        for (let Column = 0; Column < Columns; Column++) Perimeter.push([Column, 0]);
        for (let Row = 0; Row < Rows; Row++) Perimeter.push([Columns, Row]);
        for (let Column = Columns; Column > 0; Column--) Perimeter.push([Column, Rows]);
        for (let Row = Rows; Row > 0; Row--) Perimeter.push([0, Row]);
        for (let Index = 0; Index < Perimeter.length; Index++)
        {
            const First = Perimeter[Index],
                Second = Perimeter[(Index + 1) % Perimeter.length];
            Quad([Address(...First, 0), Address(...Second, 0), Address(...Second, 1), Address(...First, 1)], Material,
                Component);
        }
    }

    function Box(Name, Centre, Half, Material, Component = null)
    {
        if (Component === null) Component = Part(Name, Material, Math.min(...Half) * 2);
        const Start = Vertices.length;
        for (let Corner = 0; Corner < 8; Corner++) Vertices.push([...Centre.map((Coordinate, Axis) => Coordinate + Half[
            Axis] * ((Corner >> Axis & 1) ? 1 : -1)), Component ? 1 : 0]);
        for (const Corners of [
                [0, 4, 6, 2],
                [1, 3, 7, 5],
                [0, 1, 5, 4],
                [2, 6, 7, 3],
                [0, 2, 3, 1],
                [4, 5, 7, 6]
            ])
            Quad(Corners.map(Index => Start + Index), Material, Component);
    }
    // 📝 Four individually closed panels form a real central opening, not a painted recess.
    const Hood = (MinimumHorizontal, MaximumHorizontal, MinimumDepth, MaximumDepth, Name, Columns, Rows) =>
        Patch(Name, (Horizontal, Depth) =>
        {
            const Across = MinimumHorizontal + (MaximumHorizontal - MinimumHorizontal) * Horizontal;
            const Along = MinimumDepth + (MaximumDepth - MinimumDepth) * Depth;
            return [Across, 1.52 + .18 * (1 - Across * Across / 2.1) + .045 * Math.cos(Along * 2), Along];
        }, [0, .16, 0], Columns, Rows, 0);
    Hood(-1.38, -.48, .1, 2.55, 'Left hood panel', 8, 24);
    Hood(.48, 1.38, .1, 2.55, 'Right hood panel', 8, 24);
    Hood(-.48, .48, .1, .6, 'Hood rear cross panel', 12, 6);
    Hood(-.48, .48, 1.95, 2.55, 'Hood nose cross panel', 12, 8);
    for (const Side of [-1, 1])
        for (const Depth of [-1.65, 1.65])
        {
            const Component = Part(`${Side < 0 ? 'Left' : 'Right'} ${Depth > 0 ? 'front' : 'rear'} curved wheel arch`,
                0, .14);
            const Start = Vertices.length,
                Segments = 48,
                Section = 8;
            for (let Along = 0; Along <= Segments; Along++)
                for (let Around = 0; Around < Section; Around++)
                {
                    const Angle = Along / Segments * Math.PI;
                    const CrossAngle = Around / Section * Math.PI * 2;
                    const Radius = .92 + .105 * Math.cos(CrossAngle);
                    Vertices.push([Side * (1.43 + .22 * Math.sin(CrossAngle)), .48 + Math.sin(Angle) * Radius, Depth +
                        Math.cos(Angle) * Radius, 1
                    ]);
                }
            for (let Along = 0; Along < Segments; Along++)
                for (let Around = 0; Around < Section; Around++)
                    Quad([Start + Along * Section + Around, Start + (Along + 1) * Section + Around,
                        Start + (Along + 1) * Section + (Around + 1) % Section, Start + Along * Section + (Around +
                            1) % Section
                    ], 0, Component);
            // 📝 Quad fan caps close each sweep; they are not hidden openings in the sign classifier.
            for (const Along of [0, Segments])
            {
                const Centre = Vertices.length;
                const Ring = Array.from(
                {
                    length: Section
                }, (_, Index) => Start + Along * Section + Index);
                const Position = [0, 1, 2].map(Axis => Ring.reduce((Sum, Index) => Sum + Vertices[Index][Axis], 0) /
                    Section);
                Vertices.push([...Position, 1]);
                for (let Index = 0; Index < Section; Index += 2)
                {
                    const Corners = [Centre, Ring[Index], Ring[(Index + 1) % Section], Ring[(Index + 2) % Section]];
                    Quad(Along ? Corners : Corners.reverse(), 0, Component);
                }
            }
        }
    Patch('Crowned cabin roof', (Across, Along) => [(Across * 2 - 1) * 1.14,
        2.4 + .18 * Math.sin(Across * Math.PI), -2.1 + Along * 1.35
    ], [0, .16, 0], 24, 16, 0);
    for (const Side of [-1, 1])
    {
        Patch('Door lower panel ' + Side, (Along, Vertical) => [Side * (1.23 + .08 * Math.sin(Vertical * Math.PI)),
            .55 + Vertical * .7, -1.95 + Along * 1.95
        ], [Side * .13, 0, 0], 22, 10, 0);
        for (const Depth of [-2.02, -.78])
            Patch('Cabin pillar ' + Side + '/' + Depth, (Across, Height) => [Side * (1.28 - .15 * Height) + (Across -
                    .5) * .16,
                1.15 + Height * 1.3, Depth + .12 * Height
            ], [0, 0, .15], 2, 12, 1);
        Box('Rocker rail ' + Side, [Side * 1.23, .44, -.6], [.13, .13, 1.6], 1);
    }
    // 📝 A rectangular annular grille has a true through cavity and near-coplanar front/back sheets.
    const Grille = Part('Open grille surround', 2, .15),
        GrilleStart = Vertices.length;
    const Outer = [
        [-1.25, .65],
        [1.25, .65],
        [1.25, 1.25],
        [-1.25, 1.25]
    ];
    const Inner = [
        [-1.05, .82],
        [1.05, .82],
        [1.05, 1.08],
        [-1.05, 1.08]
    ];
    for (const Depth of [2.68, 2.53])
        for (const Loop of [Outer, Inner])
            for (const [Horizontal, Height] of Loop)
                Vertices.push([Horizontal, Height, Depth, 1]);
    for (let Edge = 0; Edge < 4; Edge++)
    {
        const Next = (Edge + 1) % 4;
        for (const Corners of [
                [Edge, Next, Next + 4, Edge + 4],
                [Edge + 8, Edge + 12, Next + 12, Next + 8],
                [Edge, Edge + 8, Next + 8, Next],
                [Edge + 4, Next + 4, Next + 12, Edge + 12]
            ]) Quad(Corners.map(Index => Index + GrilleStart), 2, Grille);
    }
    Patch('Rear bulkhead', (Across, Height) => [(Across * 2 - 1) * 1.22, .5 + Height * .78, -2.45], [0, 0, .14], 16, 8,
        0);
    Box('Engine casing under open hood', [0, 1.08, 1.22], [.43, .31, .53], 1);
    for (let Index = 0; Index < 6; Index++)
        Box('Thin cooling fin ' + Index, [0, 1.45, .8 + Index * .15], [.40, .16, .0175], 2);
    for (const Side of [-1, 1])
        for (const Depth of [-.35, .25])
            Patch('Inclined inner strut ' + Side + '/' + Depth, (Across, Height) => [Side * (.25 + Height * .8) + (
                    Across - .5) * .085,
                .6 + Height * .76, Depth + Height * .26
            ], [0, 0, .085], 2, 14, 2);
    Box('Thin undertray', [0, .35, -.2], [1.05, .0125, 2.35], 6);
    Box('Floor', [0, -.15, 0], [5, .15, 5], 3, 0);
    Box('Backdrop', [0, 1.7, -4], [5, 1.7, .1], 4, 0);
    Box('Side reflector', [-4, 1.25, 0], [.1, 1.25, 4], 5, 0);
    const Bounds = [],
        OrderedFacets = [];

    function Partition(Indices, Depth)
    {
        const Address = Bounds.length;
        const Minimum = [Infinity, Infinity, Infinity],
            Maximum = [-Infinity, -Infinity, -Infinity];
        for (const Index of Indices)
            for (const Corner of Facets[Index].slice(0, 3))
                for (let Axis = 0; Axis < 3; Axis++)
                {
                    Minimum[Axis] = Math.min(Minimum[Axis], Vertices[Corner][Axis] - .00001);
                    Maximum[Axis] = Math.max(Maximum[Axis], Vertices[Corner][Axis] + .00001);
                }
        const Extent = {
            Minimum,
            Maximum,
            Start: 0,
            Count: 0,
            Escape: 0,
            Left: 0,
            Right: 0,
            Depth
        };
        Bounds.push(Extent);
        if (Indices.length <= 4)
        {
            Extent.Start = OrderedFacets.length;
            Extent.Count = Indices.length;
            OrderedFacets.push(...Indices.map(Index => Facets[Index]));
        }
        else
        {
            const Widths = Maximum.map((Coordinate, Axis) => Coordinate - Minimum[Axis]);
            const Axis = Widths.indexOf(Math.max(...Widths));
            const Centre = Index => Facets[Index].slice(0, 3).reduce((Sum, Corner) => Sum + Vertices[Corner][Axis], 0);
            Indices.sort((First, Second) => Centre(First) - Centre(Second));
            const Split = Math.floor(Indices.length / 2);
            Extent.Left = Partition(Indices.slice(0, Split), Depth + 1);
            Extent.Right = Partition(Indices.slice(Split), Depth + 1);
        }
        Extent.Escape = Bounds.length;
        return Address;
    }
    Partition(Facets.map((_, Index) => Index), 0);
    for (let Index = Bounds.length - 1; Index >= 0; Index--)
    {
        const Extent = Bounds[Index];
        let Membership = 0;
        if (Extent.Count)
            for (let Offset = 0; Offset < Extent.Count; Offset++)
            {
                const Component = OrderedFacets[Extent.Start + Offset][4];
                if (Component) Membership |= 1 << (Component - 1);
            }
        else Membership = Bounds[Extent.Left].Membership | Bounds[Extent.Right].Membership;
        Extent.Membership = Membership >>> 0;
    }
    const BoundsBytes = new ArrayBuffer(Bounds.length * 48),
        BoundsFloats = new Float32Array(BoundsBytes),
        BoundsIntegers = new Uint32Array(BoundsBytes);
    Bounds.forEach((Extent, Index) =>
    {
        BoundsFloats.set(Extent.Minimum, Index * 12);
        BoundsIntegers[Index * 12 + 3] = Extent.Start;
        BoundsFloats.set(Extent.Maximum, Index * 12 + 4);
        BoundsIntegers[Index * 12 + 7] = Extent.Count;
        BoundsIntegers.set([Extent.Escape, Extent.Left, Extent.Right, Extent.Membership], Index * 12 + 8);
    });
    const FacetBytes = new ArrayBuffer(OrderedFacets.length * 112),
        FacetFloats = new Float32Array(FacetBytes),
        FacetIntegers = new Uint32Array(FacetBytes);
    OrderedFacets.forEach((Facet, Index) =>
    {
        const Corners = Facet.slice(0, 3).map(Corner => Vertices[Corner]);
        Corners.forEach((Point, Corner) => FacetFloats.set(Point.slice(0, 3), Index * 28 + Corner * 4));
        FacetFloats.set(FacetNormal(...Corners), Index * 28 + 12);
        FacetFloats.set(Colours[Facet[3] % 128], Index * 28 + 16);
        FacetIntegers.set([Facet[3] >= 128 ? 1 : 0, Facet[3] % 128, Facet[4], 0], Index * 28 + 24);
    });
    const MaximumDepth = Math.max(...Bounds.map(Extent => Extent.Depth)),
        DepthIndices = [],
        DepthRanges = [];
    if (MaximumDepth >= 31) throw new Error('Nearest-distance traversal stack is too small for this topology');
    for (let Depth = MaximumDepth; Depth >= 0; Depth--)
    {
        const Start = DepthIndices.length;
        Bounds.forEach((Extent, Index) =>
        {
            if (Extent.Depth === Depth) DepthIndices.push(Index);
        });
        DepthRanges.push([Start, DepthIndices.length - Start, Depth, 0]);
    }
    return {
        Vertices,
        Quads,
        Facets: OrderedFacets,
        Parts,
        Bounds,
        BoundsBytes,
        FacetBytes,
        DepthIndices,
        DepthRanges,
        MaximumDepth
    };
}
