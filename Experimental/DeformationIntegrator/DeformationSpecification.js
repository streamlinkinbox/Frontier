//============================================================================================================================================
//                                                        DEFORMATIONSPECIFICATION.JS
//============================================================================================================================================
// 📦 Quad tyre connectivity, recorded vertex poses and a fixed-connectivity triangle BVH for deforming-surface transport.

export const RingCount = 64;
export const SectionCount = 24;
export const PoseCount = 17;
export const TyreCount = RingCount * SectionCount;
export const MaximumCompression = .65;
export const Defaults = {
    Compression: .65,
    Animate: true,
    Source: 0,
    Frozen: false,
    View: 0,
    Bounces: 3,
    Samples: 1,
    Scale: .65,
    Exposure: 1,
    Power: 1,
    Wire: false
};

export function DeformPosition(Position, Compression)
{
    const [Horizontal, Vertical, Axial, Attachment] = Position;
    if (Attachment === 0) return [...Position];
    if (Attachment === 2) return [Horizontal, Vertical - Compression, Axial, Attachment];
    // 📝 Strictly increasing vertical warp; the inner bead stays rigid and the lower casing never folds onto itself.
    const Relative = Math.max(0, Vertical - .03);
    const Contact = Math.pow(1 - Math.min(1, Relative / .67), 2);
    const Height = Relative < .67 ? .03 + Math.pow(.67 - Compression, 2) * Relative /
        (.67 * .67 - Compression * Relative) : Vertical - Compression;
    return [Horizontal * (1 + .13 * Compression * Contact), Height,
        Axial * (1 + .65 * Compression * Contact), Attachment
    ];
}

export function ConstructScene()
{
    const Vertices = [],
        Quads = [],
        Facets = [];
    const Colours = [
        [.10, .115, .13],
        [.36, .4, .43],
        [.56, .56, .52],
        [.62, .075, .035],
        [.055, .32, .38],
        [.22, .25, .27],
        [0, 0, 0],
        [0, 0, 0]
    ];
    const Emissions = Colours.map((_, Index) => Index === 6 ? [12, 10.5, 8.5] : Index === 7 ? [5, 8, 12] : [0, 0, 0]);

    function Quad(Corners, Material)
    {
        Quads.push(
        {
            Corners,
            Material
        });
        Facets.push([Corners[0], Corners[1], Corners[2], Material]);
        Facets.push([Corners[0], Corners[2], Corners[3], Material + 128]);
    }
    for (let Ring = 0; Ring < RingCount; Ring++)
    {
        const Circumference = Ring / RingCount * Math.PI * 2;
        for (let Section = 0; Section < SectionCount; Section++)
        {
            const CrossSection = Section / SectionCount * Math.PI * 2;
            const Cosine = Math.cos(CrossSection),
                Sine = Math.sin(CrossSection);
            const Radius = 1.215 + .335 * Math.sign(Cosine) * Math.pow(Math.abs(Cosine), .62);
            const Axial = .56 * Math.sign(Sine) * Math.pow(Math.abs(Sine), .58);
            Vertices.push([Math.sin(Circumference) * Radius, 1.58 + Math.cos(Circumference) * Radius, Axial, 1]);
        }
    }
    for (let Ring = 0; Ring < RingCount; Ring++)
        for (let Section = 0; Section < SectionCount; Section++)
        {
            const NextRing = (Ring + 1) % RingCount,
                NextSection = (Section + 1) % SectionCount;
            Quad([Ring * SectionCount + Section, NextRing * SectionCount + Section,
                NextRing * SectionCount + NextSection, Ring * SectionCount + NextSection
            ], 0);
        }
    // 📝 A rigid open rim follows the axle; it does not fake the tyre's changing local surface.
    const RimStart = Vertices.length;
    const Profile = [
        [.86, -.46],
        [.86, .46],
        [.68, .46],
        [.68, -.46]
    ];
    for (let Ring = 0; Ring < RingCount; Ring++)
        for (const [Radius, Axial] of Profile)
        {
            const Circumference = Ring / RingCount * Math.PI * 2;
            Vertices.push([Math.sin(Circumference) * Radius, 1.58 + Math.cos(Circumference) * Radius, Axial, 2]);
        }
    for (let Ring = 0; Ring < RingCount; Ring++)
        for (let Section = 0; Section < 4; Section++)
        {
            const NextRing = (Ring + 1) % RingCount,
                NextSection = (Section + 1) % 4;
            Quad([RimStart + Ring * 4 + Section, RimStart + NextRing * 4 + Section,
                RimStart + NextRing * 4 + NextSection, RimStart + Ring * 4 + NextSection
            ], 1);
        }

    function Surface(Points, Material)
    {
        const Start = Vertices.length;
        Vertices.push(...Points.map(Point => [...Point, 0]));
        Quad([Start, Start + 1, Start + 2, Start + 3], Material);
    }
    Surface([
        [-4, 0, -3.5],
        [-4, 0, 4.5],
        [4, 0, 4.5],
        [4, 0, -3.5]
    ], 2);
    Surface([
        [-3, 0, -2.5],
        [-3, 2.5, -2.5],
        [-3, 2.5, 2],
        [-3, 0, 2]
    ], 3);
    Surface([
        [-3, 0, -2.5],
        [3, 0, -2.5],
        [3, 3.6, -2.5],
        [-3, 3.6, -2.5]
    ], 4);
    Surface([
        [-2.7, 5.4, -.3],
        [-.5, 5.4, -.3],
        [-.5, 5.4, 1.3],
        [-2.7, 5.4, 1.3]
    ], 6);
    Surface([
        [3, 1.1, -1.3],
        [3, 1.1, .3],
        [3, 3.3, .3],
        [3, 3.3, -1.3]
    ], 7);
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
            const Lengths = Maximum.map((MaximumCoordinate, Axis) => MaximumCoordinate - Minimum[Axis]);
            const Axis = Lengths.indexOf(Math.max(...Lengths));
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
    const BoundsBytes = new ArrayBuffer(Bounds.length * 48),
        BoundsFloats = new Float32Array(BoundsBytes);
    const BoundsIntegers = new Uint32Array(BoundsBytes);
    Bounds.forEach((Extent, Index) =>
    {
        BoundsFloats.set(Extent.Minimum, Index * 12);
        BoundsIntegers[Index * 12 + 3] = Extent.Start;
        BoundsFloats.set(Extent.Maximum, Index * 12 + 4);
        BoundsIntegers[Index * 12 + 7] = Extent.Count;
        BoundsIntegers.set([Extent.Escape, Extent.Left, Extent.Right, Extent.Depth], Index * 12 + 8);
    });
    const FacetBytes = new ArrayBuffer(OrderedFacets.length * 112);
    const FacetFloats = new Float32Array(FacetBytes),
        FacetIntegers = new Uint32Array(FacetBytes);
    OrderedFacets.forEach((Facet, Index) =>
    {
        const Corners = Facet.slice(0, 3).map(Corner => Vertices[Corner]);
        Corners.forEach((Point, Corner) => FacetFloats.set(Point.slice(0, 3), Index * 28 + Corner * 4));
        const Normal = FacetNormal(...Corners);
        FacetFloats.set(Normal, Index * 28 + 12);
        FacetFloats.set(Colours[Facet[3] % 128], Index * 28 + 16);
        FacetFloats.set(Emissions[Facet[3] % 128], Index * 28 + 20);
        FacetIntegers.set([Facet[3] >= 128 ? 1 : 0, Facet[3] % 128, 0, 0], Index * 28 + 24);
    });
    const MaximumDepth = Math.max(...Bounds.map(Extent => Extent.Depth));
    const DepthIndices = [],
        DepthRanges = [];
    for (let Depth = MaximumDepth; Depth >= 0; Depth--)
    {
        const Start = DepthIndices.length;
        Bounds.forEach((Extent, Index) =>
        {
            if (Extent.Depth === Depth) DepthIndices.push(Index);
        });
        DepthRanges.push([Start, DepthIndices.length - Start, Depth, 0]);
    }
    const Poses = new Float32Array(RingCount * SectionCount * PoseCount * 4);
    for (let Pose = 0; Pose < PoseCount; Pose++)
        for (let Index = 0; Index < TyreCount; Index++)
            Poses.set(DeformPosition(Vertices[Index], MaximumCompression * Pose / (PoseCount - 1)), (Pose * TyreCount +
                Index) * 4);
    return {
        Vertices,
        Quads,
        Facets: OrderedFacets,
        Bounds,
        BoundsBytes,
        FacetBytes,
        DepthIndices,
        DepthRanges,
        MaximumDepth,
        Poses,
        Colours,
        Emissions
    };
}

export function FacetNormal(First, Second, Third)
{
    const Edge = Second.map((Coordinate, Axis) => Coordinate - First[Axis]);
    const Other = Third.map((Coordinate, Axis) => Coordinate - First[Axis]);
    const Cross = [Edge[1] * Other[2] - Edge[2] * Other[1], Edge[2] * Other[0] - Edge[0] * Other[2],
        Edge[0] * Other[1] - Edge[1] * Other[0]
    ];
    const Length = Math.hypot(...Cross);
    return Cross.map(Coordinate => Coordinate / Length);
}

export function IntersectFacet(Origin, Direction, First, Second, Third, Minimum = .001)
{
    const Subtract = (Left, Right) => Left.slice(0, 3).map((Coordinate, Axis) => Coordinate - Right[Axis]);
    const Cross = (Left, Right) => [Left[1] * Right[2] - Left[2] * Right[1],
        Left[2] * Right[0] - Left[0] * Right[2], Left[0] * Right[1] - Left[1] * Right[0]
    ];
    const Dot = (Left, Right) => Left.reduce((Sum, Coordinate, Axis) => Sum + Coordinate * Right[Axis], 0);
    const Edge = Subtract(Second, First),
        Other = Subtract(Third, First),
        Perpendicular = Cross(Direction, Other);
    const Determinant = Dot(Edge, Perpendicular);
    if (Math.abs(Determinant) < 1e-8) return Infinity;
    const Relative = Subtract(Origin, First),
        FirstWeight = Dot(Relative, Perpendicular) / Determinant;
    if (FirstWeight < 0 || FirstWeight > 1) return Infinity;
    const Tangent = Cross(Relative, Edge),
        SecondWeight = Dot(Direction, Tangent) / Determinant;
    if (SecondWeight < 0 || FirstWeight + SecondWeight > 1) return Infinity;
    const Distance = Dot(Other, Tangent) / Determinant;
    return Distance > Minimum ? Distance : Infinity;
}
