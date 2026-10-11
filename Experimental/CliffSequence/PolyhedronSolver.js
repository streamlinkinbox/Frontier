//============================================================================================================================================
//                                                            POLYHEDRONSOLVER.JS
//============================================================================================================================================
// 📦 Convex cell clipping, stitched boundary contours, conforming planar triangulation and topology measurements.

import Triangulate from './PlanarSolver/cdt2d.js';

export const ε = 1e-7;
export const Add = (A, B) => A.map((Value, Index) => Value + B[Index]);
export const Subtract = (A, B) => A.map((Value, Index) => Value - B[Index]);
export const Scale = (A, Value) => A.map(Component => Component * Value);
export const Dot = (A, B) => A.reduce((Sum, Value, Index) => Sum + Value * B[Index], 0);
export const Cross = (A, B) => [A[1]*B[2]-A[2]*B[1], A[2]*B[0]-A[0]*B[2], A[0]*B[1]-A[1]*B[0]];
export const Length = A => Math.hypot(...A);
export const Normalize = A => Scale(A, 1 / Math.max(Length(A), 1e-30));
export const Lerp = (A, B, Fraction) => Add(A, Scale(Subtract(B, A), Fraction));
export const Centre = Points => Scale(Points.reduce(Add, [0,0,0]), 1 / Points.length);
export const PointKey = Point => Point.map(Value => Math.round(Value * 1e7)).join(',');
export const EdgeKey = (A, B) => [PointKey(A), PointKey(B)].sort().join('/');

export function Normal(Points)
{
    let Vector = [0,0,0];
    const Origin = Points[0];
    for (let Index = 1; Index < Points.length-1; ++Index)
        Vector = Add(Vector, Cross(Subtract(Points[Index], Origin), Subtract(Points[Index+1], Origin)));
    return Length(Vector)>1e-10 ? Normalize(Vector) : [0,0,0];
}

export function CleanLoop(Points, Collinear = true)
{
    let Result = Points.filter((Point, Index) => Length(Subtract(Point, Points[(Index+1)%Points.length])) > ε);
    if (!Collinear) return Result;
    let Changed = true;
    while (Changed && Result.length > 3)
    {
        Changed = false;
        Result = Result.filter((Point, Index) =>
        {
            const Before = Subtract(Point, Result[(Index+Result.length-1)%Result.length]);
            const After = Subtract(Result[(Index+1)%Result.length], Point);
            if (Length(Cross(Before, After)) < ε * (Length(Before)+Length(After)) && Dot(Before, After) > 0)
            {
                Changed = true;
                return false;
            }
            return true;
        });
    }
    return Result;
}

export function Face(Points, Tag = 'Cliff', Outward = null)
{
    const Loop = CleanLoop(Points);
    let Direction = Normal(Loop);
    if (Outward && Dot(Direction, Outward) < 0)
    {
        Loop.reverse();
        Direction = Scale(Direction, -1);
    }
    return {Loop, Holes:[], Normal:Direction, Tag};
}

export function Frame(Polygon)
{
    const Origin = Polygon.Loop[0];
    const U = Normalize(Subtract(Polygon.Loop[1], Origin));
    const V = Cross(Polygon.Normal, U);
    return {Origin,U,V, Project:Point => [Dot(Subtract(Point, Origin),U),Dot(Subtract(Point, Origin),V)],
        Unproject:Point => Add(Origin, Add(Scale(U,Point[0]), Scale(V,Point[1])))};
}

// 📝 Clips a convex construction cell. The generated cap is an actual boundary, not a shading mask.
export function ClipCell(Cell, Direction, Offset, Tag)
{
    const Distances = Cell.flatMap(Polygon => Polygon.Loop.map(Point => Dot(Point, Direction)-Offset));
    if (Math.max(...Distances) <= ε) return Cell;
    if (Math.min(...Distances) >= -ε) return null;
    const Faces = [];
    const Intersections = new Map();
    for (const Polygon of Cell)
    {
        const Points = [];
        for (let Index = 0; Index < Polygon.Loop.length; ++Index)
        {
            const A = Polygon.Loop[Index];
            const B = Polygon.Loop[(Index+1)%Polygon.Loop.length];
            const First = Dot(A, Direction)-Offset;
            const Second = Dot(B, Direction)-Offset;
            if (First <= ε) Points.push(A);
            if ((First < -ε && Second > ε) || (First > ε && Second < -ε))
            {
                const Point = Lerp(A,B,First/(First-Second));
                Points.push(Point);
                Intersections.set(PointKey(Point), Point);
            }
            if (Math.abs(First) <= ε) Intersections.set(PointKey(A), A);
        }
        const Loop = CleanLoop(Points);
        if (Loop.length >= 3 && Length(Normal(Loop)) > .5) Faces.push(Face(Loop, Polygon.Tag, Polygon.Normal));
    }
    const Cap = [...Intersections.values()];
    if (Cap.length >= 3)
    {
        const Middle = Centre(Cap);
        const U = Normalize(Cross(Direction, Math.abs(Direction[1]) < .9 ? [0,1,0] : [1,0,0]));
        const V = Cross(Direction,U);
        Cap.sort((A,B) => Math.atan2(Dot(Subtract(A,Middle),V),Dot(Subtract(A,Middle),U)) -
            Math.atan2(Dot(Subtract(B,Middle),V),Dot(Subtract(B,Middle),U)));
        Faces.push(Face(Cap, Tag, Direction));
    }
    return Faces.length >= 4 ? Faces : null;
}

export function ClipCells(Cells, Direction, Offset, Tag)
{
    return Cells.map(Cell => ClipCell(Cell, Direction, Offset, Tag)).filter(Boolean);
}

function SplitEdges(Polygons)
{
    const Points = [...new Map(Polygons.flatMap(Polygon => [Polygon.Loop,...Polygon.Holes].flatMap(Loop=>Loop.map(Point => [PointKey(Point), Point])))).values()];
    const Split=Contour=>
    {
        const Loop = [];
        for (let Index = 0; Index < Contour.length; ++Index)
        {
            const A = Contour[Index];
            const B = Contour[(Index+1)%Contour.length];
            const Δ = Subtract(B,A);
            const Squared = Dot(Δ,Δ);
            const OnEdge = [[0,A]];
            for (const Point of Points)
            {
                const Fraction = Dot(Subtract(Point,A),Δ)/Squared;
                if (Fraction > ε && Fraction < 1-ε && Length(Subtract(Point,Lerp(A,B,Fraction))) < ε*3)
                    OnEdge.push([Fraction,Point]);
            }
            OnEdge.sort((First,Second) => First[0]-Second[0]);
            Loop.push(...OnEdge.map(Entry => Entry[1]));
        }
        return CleanLoop(Loop,false);
    };
    return Polygons.map(Polygon=>({...Polygon,Loop:Split(Polygon.Loop),Holes:Polygon.Holes.map(Split)}));
}

// 📝 Remove construction interfaces before triangulation. Co-planar contours are stitched, not concatenated.
export function JoinCells(Cells, Name)
{
    // 📝 Rounded keys alone split a shared corner when roundoff straddles a bucket boundary.
    const Buckets=new Map();
    const Weld=Point=>
    {
        const Coordinate=Point.map(Component=>Math.floor(Component/ε));
        for (let X=-1;X<=1;++X) for (let Y=-1;Y<=1;++Y) for (let Z=-1;Z<=1;++Z)
        {
            const Key=[Coordinate[0]+X,Coordinate[1]+Y,Coordinate[2]+Z].join(',');
            const Match=Buckets.get(Key)?.find(Other=>Length(Subtract(Point,Other))<ε*.25);
            if (Match) return Match;
        }
        const Key=Coordinate.join(',');
        if (!Buckets.has(Key)) Buckets.set(Key,[]);
        Buckets.get(Key).push(Point);
        return Point;
    };
    Cells=Cells.map(Cell=>Cell.map(Polygon=>({...Polygon,Loop:Polygon.Loop.map(Weld)})));
    const Planes = new Map();
    for (const Polygon of Cells.flat())
    {
        let Direction = Polygon.Normal;
        let Offset = Dot(Direction, Polygon.Loop[0]);
        const First = Direction.find(Component => Math.abs(Component) > 1e-6);
        if (First < 0)
        {
            Direction = Scale(Direction,-1);
            Offset *= -1;
        }
        const Key = [...Direction,Offset].map(Value => Math.round(Value*1e5)).join(',');
        if (!Planes.has(Key)) Planes.set(Key, []);
        Planes.get(Key).push(Polygon);
    }
    const Result = [];
    for (const Polygons of Planes.values())
    {
        const Aligned = SplitEdges(Polygons);
        const Edges = new Map();
        for (const Polygon of Aligned)
        {
            Polygon.Loop.forEach((A, Index) =>
            {
                const B = Polygon.Loop[(Index+1)%Polygon.Loop.length];
                const Forward = `${PointKey(A)}/${PointKey(B)}`;
                const Reverse = `${PointKey(B)}/${PointKey(A)}`;
                if (Edges.has(Reverse)) Edges.delete(Reverse);
                else Edges.set(Forward,{A,B,Tag:Polygon.Tag,Normal:Polygon.Normal});
            });
        }
        const Outlines=[],Holes=[];
        while (Edges.size)
        {
            let [Key, Edge] = Edges.entries().next().value;
            const Loop = [];
            const First = PointKey(Edge.A);
            const Tag = Edge.Tag;
            const Outward = Edge.Normal;
            let Closed = false;
            while (Edges.has(Key))
            {
                Loop.push(Edge.A);
                Edges.delete(Key);
                const End = PointKey(Edge.B);
                if (End === First)
                {
                    Closed = true;
                    break;
                }
                const Next = [...Edges.entries()].find(([,Candidate]) => PointKey(Candidate.A) === End);
                if (!Next) break;
                [Key, Edge] = Next;
            }
            if (!Closed) throw new Error(`Unclosed construction interface in ${Name}`);
            if (Loop.length >= 3)
            {
                if (Dot(Normal(Loop),Outward)<0) Holes.push({Loop,Outward});
                else Outlines.push(Face(Loop,Tag,Outward));
            }
        }
        for (const Hole of Holes)
        {
            const Containers=Outlines.filter(Polygon=>
            {
                if (Dot(Polygon.Normal,Hole.Outward)<.99) return false;
                const Basis=Frame(Polygon);
                return PointInLoop(Basis.Project(Hole.Loop[0]),Polygon.Loop.map(Basis.Project));
            });
            if (!Containers.length) throw new Error(`Uncontained planar void in ${Name}`);
            const Area=Polygon=>Polygon.Loop.slice(1,-1).reduce((Sum,Point,Index)=>
                Sum+Dot(Cross(Subtract(Point,Polygon.Loop[0]),Subtract(Polygon.Loop[Index+2],Polygon.Loop[0])),Polygon.Normal),0);
            Containers.sort((First,Second)=>Area(First)-Area(Second));
            Containers[0].Holes.push(Hole.Loop);
        }
        Result.push(...Outlines);
    }
    // 📝 Preserve shared segmentation after coplanar merges; every incident face sees every corner.
    return {Name, Faces:SplitEdges(Result), Spalls:[], Cracks:[]};
}

export function PointInLoop(Point, Loop)
{
    let Inside = false;
    for (let Index=0, Previous=Loop.length-1; Index<Loop.length; Previous=Index++)
    {
        const A=Loop[Index], B=Loop[Previous];
        if ((A[1]>Point[1]) !== (B[1]>Point[1]) && Point[0] < (B[0]-A[0])*(Point[1]-A[1])/(B[1]-A[1])+A[0])
            Inside = !Inside;
    }
    return Inside;
}

export function SegmentDistance(Point, A, B)
{
    const Δ = B.map((Value,Index) => Value-A[Index]);
    const Squared = Dot(Δ,Δ);
    const Fraction = Math.max(0, Math.min(1, Dot(Point.map((Value,Index) => Value-A[Index]),Δ)/Squared));
    return Math.hypot(...Point.map((Value,Index) => Value-A[Index]-Δ[Index]*Fraction));
}

export function LoopClearance(Point, Loop)
{
    return Math.min(...Loop.map((A,Index) => SegmentDistance(Point,A,Loop[(Index+1)%Loop.length])));
}

function SubdivideLoop(Loop, Span, EdgeSpans)
{
    const Points = [];
    Loop.forEach((First,Index) =>
    {
        const Second = Loop[(Index+1)%Loop.length];
        if (!Number.isFinite(Span))
        {
            Points.push(First);
            return;
        }
        const Forward=PointKey(First)<PointKey(Second);
        const [A,B]=Forward?[First,Second]:[Second,First];
        const Distance=Length(Subtract(A,B));
        const StartSpan=Math.min(Span,Math.max(.025,(EdgeSpans?.get(PointKey(A))||Span)*.8));
        const EndSpan=Math.min(Span,Math.max(.025,(EdgeSpans?.get(PointKey(B))||Span)*.8));
        const March=Initial=>
        {
            const Values=[0];
            let Position=0, Step=Initial;
            while (Position+Step<Distance*.5-Step*.3)
            {
                Position+=Step;
                Values.push(Position);
                Step=Math.min(Span,Step*1.7);
            }
            return Values;
        };
        const FromStart=March(StartSpan), FromEnd=March(EndSpan);
        const Start=FromStart.at(-1), End=Distance-FromEnd.at(-1);
        const Count=Math.max(1,Math.ceil((End-Start)/Span));
        const Distances=[...FromStart];
        for (let Part=1;Part<Count;++Part) Distances.push(Start+(End-Start)*Part/Count);
        Distances.push(...FromEnd.slice().reverse().map(Value=>Distance-Value));
        const Vertices=Distances.map(Value=>Lerp(A,B,Value/Distance));
        if (!Forward) Vertices.reverse();
        Points.push(...Vertices.slice(0,-1));
    });
    return Points;
}

export function TriangulateFace(Polygon, Span = Infinity, EdgeSpans = null)
{
    const Basis = Frame(Polygon);
    const Loops = [Polygon.Loop,...Polygon.Holes].map(Loop => SubdivideLoop(Loop,Span,EdgeSpans));
    const Vertices = Loops.flat();
    const Points = Vertices.map(Basis.Project);
    const Constraints = [];
    let Offset = 0;
    for (const Loop of Loops)
    {
        Loop.forEach((Point,Index) => Constraints.push([Offset+Index,Offset+(Index+1)%Loop.length]));
        Offset += Loop.length;
    }
    const PlanarLoops = Loops.map(Loop => Loop.map(Basis.Project));
    const Inside = Point => PointInLoop(Point,PlanarLoops[0]) && !PlanarLoops.slice(1).some(Loop => PointInLoop(Point,Loop));
    const Distance = Point => Math.min(...PlanarLoops.map(Loop => LoopClearance(Point,Loop)));
    if (Number.isFinite(Span))
    {
        const Minimum = [0,1].map(Axis => Math.min(...Points.map(Point => Point[Axis])));
        const Maximum = [0,1].map(Axis => Math.max(...Points.map(Point => Point[Axis])));
        let Row = 0;
        for (let Y=Minimum[1]+Span*.65; Y<Maximum[1]; Y+=Span*.866, ++Row)
        {
            for (let X=Minimum[0]+Span*(Row%2 ? .75 : .25); X<Maximum[0]; X+=Span)
            {
                const Point = [X,Y];
                if (Inside(Point) && Distance(Point) > Span*.28)
                {
                    Points.push(Point);
                    Vertices.push(Basis.Unproject(Point));
                }
            }
        }
        for (const Loop of [Polygon.Loop,...Polygon.Holes])
        {
            for (const Vertex of Loop)
            {
                const LocalSpan=EdgeSpans?.get(PointKey(Vertex))||Span;
                if (LocalSpan>Span*.35) continue;
                const Corner=Basis.Project(Vertex);
                for (let Radius=Math.max(.04,LocalSpan*.8);Radius<Span*.8;Radius*=2)
                {
                    for (let Index=0;Index<12;++Index)
                    {
                        const Angle=Index*Math.PI/6;
                        const Point=[Corner[0]+Math.cos(Angle)*Radius,Corner[1]+Math.sin(Angle)*Radius];
                        if (Inside(Point) && Distance(Point)>Radius*.28 && Points.every(Other=>Math.hypot(Point[0]-Other[0],Point[1]-Other[1])>Radius*.6))
                        {
                            Points.push(Point);
                            Vertices.push(Basis.Unproject(Point));
                        }
                    }
                }
            }
        }
        // 📝 Graduated rings around a narrow incision avoid spanning straight from a long edge to its lip.
        for (const Hole of PlanarLoops.slice(1))
        {
            for (const Radius of [Span*.15,Span*.33,Span*.6])
            {
                for (const Corner of Hole)
                {
                    for (let Index=0; Index<8; ++Index)
                    {
                        const Angle=Index*Math.PI/4;
                        const Point=[Corner[0]+Math.cos(Angle)*Radius,Corner[1]+Math.sin(Angle)*Radius];
                        if (Inside(Point) && Distance(Point)>Radius*.45 && Points.every(Other => Math.hypot(Point[0]-Other[0],Point[1]-Other[1])>Radius*.65))
                        {
                            Points.push(Point);
                            Vertices.push(Basis.Unproject(Point));
                        }
                    }
                }
            }
        }
    }
    const Triangles = Triangulate(Points, Constraints, {exterior:false});
    for (const Triangle of Triangles)
    {
        const [A,B,C]=Triangle.map(Index => Vertices[Index]);
        if (Dot(Cross(Subtract(B,A),Subtract(C,A)),Polygon.Normal) < 0) [Triangle[1],Triangle[2]]=[Triangle[2],Triangle[1]];
    }
    return {Vertices,Triangles};
}

export function TriangulateBody(Body, Span)
{
    const Vertices=[];
    const Triangles=[];
    const Tags=[];
    const EdgeSpans=new Map();
    for (const Polygon of Body.Faces)
    {
        for (const Loop of [Polygon.Loop,...Polygon.Holes])
        {
            Loop.forEach((A,Index)=>
            {
                const B=Loop[(Index+1)%Loop.length];
                const Distance=Length(Subtract(B,A));
                for (const Point of [A,B]) EdgeSpans.set(PointKey(Point),Math.min(EdgeSpans.get(PointKey(Point))||Infinity,Distance));
            });
        }
    }
    const IndexByPoint=new Map();
    const Intern=Point =>
    {
        const Key=PointKey(Point);
        if (!IndexByPoint.has(Key))
        {
            IndexByPoint.set(Key,Vertices.length);
            Vertices.push(Point);
        }
        return IndexByPoint.get(Key);
    };
    for (const Polygon of Body.Faces)
    {
        const Mesh=TriangulateFace(Polygon,Span,EdgeSpans);
        const Indices=Mesh.Vertices.map(Intern);
        for (const Triangle of Mesh.Triangles)
        {
            Triangles.push(Triangle.map(Index => Indices[Index]));
            Tags.push(Polygon.Tag);
        }
    }
    return {Name:Body.Name,Vertices,Triangles,Tags,Spalls:Body.Spalls,Cracks:Body.Cracks};
}

export function InsideMesh(Point, Mesh)
{
    const Direction=Normalize([.9123,.2317,.3379]);
    const Hits=[];
    for (const Triangle of Mesh.Triangles)
    {
        const [A,B,C]=Triangle.map(Index => Mesh.Vertices[Index]);
        const First=Subtract(B,A), Second=Subtract(C,A);
        const P=Cross(Direction,Second);
        const Determinant=Dot(First,P);
        if (Math.abs(Determinant)<1e-10) continue;
        const From=Subtract(Point,A);
        const U=Dot(From,P)/Determinant;
        const Q=Cross(From,First);
        const V=Dot(Direction,Q)/Determinant;
        const Distance=Dot(Second,Q)/Determinant;
        if (U>=-ε && V>=-ε && U+V<=1+ε && Distance>ε) Hits.push(Distance);
    }
    Hits.sort((A,B)=>A-B);
    return Hits.filter((Value,Index)=>Index===0 || Value-Hits[Index-1]>1e-6).length%2===1;
}

export function MeshMetrics(Mesh)
{
    const Edges=new Map();
    const VertexLinks=new Map();
    const TriangleKeys=new Set();
    let DuplicateTriangles=0;
    let ZeroArea=0, Volume=0, MinimumAngle=180, ThinTriangles=0;
    for (const Triangle of Mesh.Triangles)
    {
        const Key=Triangle.slice().sort((A,B)=>A-B).join('/');
        if (TriangleKeys.has(Key)) ++DuplicateTriangles;
        TriangleKeys.add(Key);
        Triangle.forEach((Vertex,Index)=>
        {
            if (!VertexLinks.has(Vertex)) VertexLinks.set(Vertex,new Map());
            const Links=VertexLinks.get(Vertex);
            const A=Triangle[(Index+1)%3], B=Triangle[(Index+2)%3];
            if (!Links.has(A)) Links.set(A,new Set());
            if (!Links.has(B)) Links.set(B,new Set());
            Links.get(A).add(B);
            Links.get(B).add(A);
        });
        const [A,B,C]=Triangle.map(Index=>Mesh.Vertices[Index]);
        const Area=Length(Cross(Subtract(B,A),Subtract(C,A)))*.5;
        if (Area<1e-10) ++ZeroArea;
        Volume+=Dot(A,Cross(B,C))/6;
        const Sides=[Length(Subtract(B,A)),Length(Subtract(C,B)),Length(Subtract(A,C))];
        let Angle=180;
        for (let Index=0;Index<3;++Index)
        {
            const [A,B,C]=[Sides[Index],Sides[(Index+1)%3],Sides[(Index+2)%3]];
            Angle=Math.min(Angle,Math.acos(Math.max(-1,Math.min(1,(A*A+B*B-C*C)/(2*A*B))))*180/Math.PI);
        }
        MinimumAngle=Math.min(MinimumAngle,Angle);
        if (Angle<5) ++ThinTriangles;
        Triangle.forEach((A,Index)=>
        {
            const B=Triangle[(Index+1)%3];
            const Key=[A,B].sort((First,Second)=>First-Second).join('/');
            const Record=Edges.get(Key)||{Count:0,Orientation:0};
            ++Record.Count;
            Record.Orientation+=A<B?1:-1;
            Edges.set(Key,Record);
        });
    }
    const NonmanifoldVertices=[...VertexLinks.values()].filter(Links=>
    {
        if ([...Links.values()].some(Neighbours=>Neighbours.size!==2)) return true;
        const Seen=new Set(), Pending=[Links.keys().next().value];
        while (Pending.length)
        {
            const Vertex=Pending.pop();
            if (Seen.has(Vertex)) continue;
            Seen.add(Vertex);
            Pending.push(...Links.get(Vertex));
        }
        return Seen.size!==Links.size;
    }).length;
    return {Vertices:Mesh.Vertices.length,Triangles:Mesh.Triangles.length,ZeroArea,Volume,MinimumAngle,ThinTriangles,NonmanifoldVertices,DuplicateTriangles,
        OpenEdges:[...Edges.values()].filter(Edge=>Edge.Count===1).length,
        NonmanifoldEdges:[...Edges.values()].filter(Edge=>Edge.Count>2).length,
        WindingErrors:[...Edges.values()].filter(Edge=>Edge.Count===2 && Edge.Orientation!==0).length};
}
