//============================================================================================================================================
//                                                           SCENESPECIFICATION.JS
//============================================================================================================================================
// 📦 Original triangle atrium, rigid-instance BLAS construction and screen-cascade resource budgets; no external scene assets.

export const Defaults = {
    Scale: .65,
    Spacing: 12,
    Directions: 4,
    Cascades: 4,
    Interval: 1.5,
    History: .65,
    Exposure: 1.1,
    Shadow: 512,
    Power: 1,
    Sky: .025,
    Speed: 1,
    Animate: true,
    Motion: true,
    Visibility: true,
    Specular: false,
    Counters: false,
    Analytic: true,
    Debug: 0,
    Level: 0
};
export function CascadeLayout(Width, Height, Settings)
{
    return Array.from(
    {
        length: Settings.Cascades
    }, (_, Level) =>
    {
        const Spacing = Settings.Spacing * 2 ** Level,
            Directions = Settings.Directions * 2 ** Level;
        const X = Math.ceil(Width / Spacing),
            Y = Math.ceil(Height / Spacing);
        return {
            Level,
            Spacing,
            Directions,
            X,
            Y,
            Count: X * Y * Directions ** 2,
            Near: Level ? Settings.Interval * 2 ** (Level - 1) : 0,
            Far: Level === Settings.Cascades - 1 ? 80 : Settings.Interval * 2 ** Level
        };
    });
}
const Minus = (A, B) => A.map((V, I) => V - B[I]);
const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Dot = (A, B) => A.reduce((Sum, V, I) => Sum + V * B[I], 0);
const Normal = V => V.map(X => X / Math.hypot(...V));
export function ConstructScene()
{
    const Groups = [
        [],
        [],
        [],
        []
    ];
    const Stone = [.44, .46, .43, .8],
        Dark = [.16, .19, .21, .7],
        Red = [.64, .15, .075, .8],
        Blue = [.065, .34, .4, .7];

    function Triangle(A, B, C, Colour = Stone, Emission = [0, 0, 0, 0], Instance = 0)
    {
        const N = Normal(Cross(Minus(B, A), Minus(C, A)));
        if (!N.every(Number.isFinite)) throw new Error('Degenerate scene triangle');
        Groups[Instance].push(
        {
            A,
            B,
            C,
            N,
            Colour,
            Emission,
            Instance
        });
    }

    function Quad(A, B, C, D, Colour, Emission, Instance)
    {
        Triangle(A, B, C, Colour, Emission, Instance);
        Triangle(A, C, D, Colour, Emission, Instance);
    }

    function Box(Centre, Size, Colour = Stone, Emission = [0, 0, 0, 0], Instance = 0)
    {
        const P = Array.from(
        {
            length: 8
        }, (_, I) => Centre.map((V, A) => V + Size[A] * ((I >> A & 1) - .5)));
        for (const [A, B, C, D] of [
                [0, 4, 6, 2],
                [1, 3, 7, 5],
                [0, 1, 5, 4],
                [2, 6, 7, 3],
                [0, 2, 3, 1],
                [4, 5, 7, 6]
            ]) Quad(P[A], P[B], P[C], P[D], Colour, Emission, Instance);
    }

    function Cylinder(X, Y, Z, Radius, Height, Sides = 16, Colour = Stone)
    {
        for (let I = 0; I < Sides; ++I)
        {
            const A = I / Sides * Math.PI * 2,
                B = (I + 1) / Sides * Math.PI * 2;
            const P = [X + Math.cos(A) * Radius, Y, Z + Math.sin(A) * Radius],
                Q = [X + Math.cos(B) * Radius, Y, Z + Math.sin(B) * Radius];
            const R = [Q[0], Y + Height, Q[2]],
                S = [P[0], Y + Height, P[2]];
            Quad(P, S, R, Q, Colour);
            Triangle([X, Y, Z], P, Q, Colour);
            Triangle([X, Y + Height, Z], R, S, Colour);
        }
    }
    Box([0, -.3, -1], [20, .6, 22], [.36, .37, .33, .76]);
    Box([0, 3.4, -11], [20, 6.8, .4], Stone);
    Box([-10, 3.4, -1], [.4, 6.8, 20], Red);
    Box([10, 3.4, -1], [.4, 6.8, 20], Blue);
    for (const X of [-6.8, 6.8])
    {
        for (const Z of [-8, -4, 0, 4, 8])
        {
            Box([X, .16, Z], [1, .32, 1], Dark);
            Cylinder(X, .32, Z, .36, 3.8, 16);
            Box([X, 4.12, Z], [.94, .3, .94], Stone);
        }
        for (const Z of [-6, -2, 2, 6])
            for (let I = 0; I < 16; ++I)
            {
                const A = I / 16 * Math.PI,
                    B = (I + 1) / 16 * Math.PI;
                const Point = (R, Angle, Offset) => [X + Offset, 4.23 + Math.sin(Angle) * R, Z + Math.cos(Angle) * R];
                const P = [Point(1.62, A, -.3), Point(2, A, -.3), Point(2, B, -.3), Point(1.62, B, -.3),
                    Point(1.62, A, .3), Point(2, A, .3), Point(2, B, .3), Point(1.62, B, .3)
                ];
                for (const [J, K, L, M] of [
                        [0, 3, 2, 1],
                        [4, 5, 6, 7],
                        [0, 4, 7, 3],
                        [1, 2, 6, 5]
                    ]) Quad(P[J], P[K], P[L], P[M], Stone);
            }
        Box([X, 6.4, 0], [1.1, .35, 18], Dark);
        Box([X, 6.15, 0], [.08, .12, 17], [.7, .65, .5, .5], [2.5, 1.8, .9, 0]);
    }
    for (let Step = 0; Step < 4; ++Step) Box([0, .1 + Step * .16, -2], [5 - Step * .65, .2, 5 - Step * .65], [.25, .27,
        .26, .65
    ]);
    Cylinder(0, .72, -2, 1.05, .35, 32, Dark);
    for (let Ring = 0; Ring < 12; ++Ring)
        for (let Side = 0; Side < 10; ++Side)
        {
            const Point = (R, S) =>
            {
                const Angle = S / 10 * Math.PI * 2 + R * .12,
                    Radius = .8 + .27 * Math.sin(R * .7);
                return [Math.cos(Angle) * Radius, R * .25, Math.sin(Angle) * Radius];
            };
            Quad(Point(Ring, Side), Point(Ring + 1, Side), Point(Ring + 1, Side + 1), Point(Ring, Side + 1), [.55, .57,
                .52, .22
            ], [0, 0, 0, 0], 1);
        }
    for (const X of [-3.8, 3.8])
        for (const Z of [-7, 3, 7])
        {
            Box([X, .4, Z], [1.7, .25, .8], Dark);
            Box([X - .6, .18, Z], [.18, .36, .6], Dark);
            Box([X + .6, .18, Z], [.18, .36, .6], Dark);
        }
    for (let I = 0; I < 9; ++I) Box([-8 + I * 2, .015, -1], [.08, .03, 19.2], [.19, .21, .22, .8]);
    for (const [Id, Colour] of [
            [2, [1, .39, .09]],
            [3, [.06, .62, 1]]
        ])
    {
        Box([0, 0, 0], [.6, .6, .6], [...Colour, .2], [...Colour.map(V => V * 10), 1], Id);
        Box([0, -.4, 0], [.1, .2, .1], Dark, [0, 0, 0, 0], Id);
    }
    const Triangles = [],
        Nodes = [],
        Instances = [];
    const Bounds = List =>
    {
        const Minimum = [Infinity, Infinity, Infinity],
            Maximum = [-Infinity, -Infinity, -Infinity];
        for (const T of List)
            for (const P of [T.A, T.B, T.C])
                for (let A = 0; A < 3; ++A)
                {
                    Minimum[A] = Math.min(Minimum[A], P[A] - .0001);
                    Maximum[A] = Math.max(Maximum[A], P[A] + .0001);
                }
        return {
            Minimum,
            Maximum
        };
    };
    const Area = B =>
    {
        const S = Minus(B.Maximum, B.Minimum);
        return 2 * (S[0] * S[1] + S[1] * S[2] + S[2] * S[0]);
    };

    function Build(List, Depth = 0)
    {
        const Index = Nodes.length,
            B = Bounds(List),
            Node = {
                ...B,
                Start: 0,
                Count: 0,
                Escape: 0,
                Depth
            };
        Nodes.push(Node);
        if (List.length <= 4)
        {
            Node.Start = Triangles.length;
            Node.Count = List.length;
            Triangles.push(...List);
        }
        else
        {
            const Extent = Minus(B.Maximum, B.Minimum),
                Axis = Extent.indexOf(Math.max(...Extent));
            List.sort((A, B) => (A.A[Axis] + A.B[Axis] + A.C[Axis]) - (B.A[Axis] + B.B[Axis] + B.C[Axis]));
            let Split = Math.floor(List.length / 2),
                Cost = Infinity;
            for (let Bin = 1; Bin < 8; ++Bin)
            {
                const Candidate = Math.max(1, Math.min(List.length - 1, Math.floor(List.length * Bin / 8)));
                const Score = Area(Bounds(List.slice(0, Candidate))) * Candidate + Area(Bounds(List.slice(Candidate))) *
                    (List.length - Candidate);
                if (Score < Cost)
                {
                    Cost = Score;
                    Split = Candidate;
                }
            }
            Build(List.slice(0, Split), Depth + 1);
            Build(List.slice(Split), Depth + 1);
        }
        Node.Escape = Nodes.length;
        return Index;
    }
    for (const Group of Groups)
    {
        const Root = Build(Group);
        Instances.push(
        {
            Root,
            End: Nodes.length
        });
    }
    const TriangleData = new ArrayBuffer(Triangles.length * 112),
        TFloat = new Float32Array(TriangleData),
        TUint = new Uint32Array(TriangleData);
    Triangles.forEach((T, I) =>
    {
        for (const [Slot, Data] of [
                [0, T.A],
                [4, T.B],
                [8, T.C],
                [12, T.N],
                [16, T.Colour],
                [20, T.Emission]
            ]) TFloat.set(Data, I * 28 + Slot);
        TUint[I * 28 + 24] = T.Instance;
    });
    const NodeData = new ArrayBuffer(Nodes.length * 48),
        NFloat = new Float32Array(NodeData),
        NUint = new Uint32Array(NodeData);
    Nodes.forEach((N, I) =>
    {
        NFloat.set(N.Minimum, I * 12);
        NFloat.set(N.Maximum, I * 12 + 4);
        NUint[I * 12 + 3] = N.Start;
        NUint[I * 12 + 7] = N.Count;
        NUint[I * 12 + 8] = N.Escape;
    });
    return {
        Triangles,
        Nodes,
        Instances,
        TriangleData,
        NodeData,
        MaximumDepth: Math.max(...Nodes.map(N => N.Depth))
    };
}
export function IntersectTriangle(Origin, Direction, T, Minimum = 0, Maximum = Infinity)
{
    const E = Minus(T.B, T.A),
        F = Minus(T.C, T.A),
        P = Cross(Direction, F),
        D = Dot(E, P);
    if (Math.abs(D) < 1e-8) return Maximum;
    const S = Minus(Origin, T.A),
        U = Dot(S, P) / D;
    if (U < 0 || U > 1) return Maximum;
    const Q = Cross(S, E),
        V = Dot(Direction, Q) / D;
    if (V < 0 || U + V > 1) return Maximum;
    const Distance = Dot(F, Q) / D;
    return Distance > Minimum && Distance < Maximum ? Distance : Maximum;
}
