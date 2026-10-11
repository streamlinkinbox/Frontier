//============================================================================================================================================
//                                                             TRIANGLESOLVER.JS
//============================================================================================================================================
// 📦 Bounded local edge collapse and cap-diagonal repair for clipped surface triangulations.

import {Subtract, Cross, Dot, Length} from './PolyhedronSolver.js';

function MinimumAngle(Triangle, Vertices)
{
    const Points=Triangle.map(Index=>Vertices[Index]);
    let Angle=180;
    for (let Index=0;Index<3;++Index)
    {
        const A=Subtract(Points[(Index+1)%3],Points[Index]);
        const B=Subtract(Points[(Index+2)%3],Points[Index]);
        Angle=Math.min(Angle,Math.acos(Math.max(-1,Math.min(1,Dot(A,B)/(Length(A)*Length(B)))))*180/Math.PI);
    }
    return Angle;
}

function TriangleOverlap(First, Second, Vertices)
{
    let A=First.map(Index=>Vertices[Index]), B=Second.map(Index=>Vertices[Index]);
    for (let Axis=0;Axis<3;++Axis)
        if (Math.max(...A.map(Point=>Point[Axis]))<Math.min(...B.map(Point=>Point[Axis]))-1e-8 ||
            Math.max(...B.map(Point=>Point[Axis]))<Math.min(...A.map(Point=>Point[Axis]))-1e-8) return false;
    if (First.some(Index=>Second.includes(Index)))
    {
        const Contract=Points=>
        {
            const Middle=[0,1,2].map(Axis=>Points.reduce((Sum,Point)=>Sum+Point[Axis],0)/3);
            return Points.map(Point=>Point.map((Value,Axis)=>Value+(Middle[Axis]-Value)*1e-5));
        };
        A=Contract(A);B=Contract(B);
    }
    // 📝 Unit edge directions retain SAT axes at microscopic clipped corners; relative projections avoid cancellation.
    const RawEdges=Points=>Points.map((Point,Index)=>Subtract(Points[(Index+1)%3],Point));
    const MinimumEdge=Math.min(...[...RawEdges(A),...RawEdges(B)].map(Length));
    const CoordinateScale=Math.max(1,...[...A,...B].flat().map(Math.abs));
    const Tolerance=Math.max(Number.EPSILON*CoordinateScale*8,Math.min(1e-8,MinimumEdge*1e-8));
    const Edges=Points=>RawEdges(Points).map(Edge=>Edge.map(Component=>Component/Math.max(Length(Edge),1e-30)));
    const FirstEdges=Edges(A), SecondEdges=Edges(B);
    const FirstNormal=Cross(FirstEdges[0],FirstEdges[1]), SecondNormal=Cross(SecondEdges[0],SecondEdges[1]);
    const Axes=[FirstNormal,SecondNormal,...FirstEdges.flatMap(Edge=>SecondEdges.map(Other=>Cross(Edge,Other))),
        ...FirstEdges.map(Edge=>Cross(Edge,FirstNormal)),...SecondEdges.map(Edge=>Cross(Edge,SecondNormal))];
    for (const Axis of Axes)
    {
        const Magnitude=Length(Axis);
        if (Magnitude<1e-12) continue;
        const One=A.map(Point=>Dot(Subtract(Point,A[0]),Axis)/Magnitude), Two=B.map(Point=>Dot(Subtract(Point,A[0]),Axis)/Magnitude);
        if (Math.max(...One)<Math.min(...Two)-Tolerance || Math.max(...Two)<Math.min(...One)-Tolerance) return false;
    }
    return true;
}

function CrossesSurface(Mesh, Replaced, Replacements)
{
    const Retained=Mesh.Triangles.filter((Triangle,Index)=>!Replaced.includes(Index));
    return Replacements.some((Triangle,Index)=>[...Retained,...Replacements.slice(Index+1)]
        .some(Other=>TriangleOverlap(Triangle,Other,Mesh.Vertices)));
}

// 📝 Deterministic, bounded-error topology cleanup, not vertex displacement or a shape-generation function.
// 📝 A collapse keeps an existing endpoint, obeys the manifold link condition, and cannot flip a triangle.
export function CollapseSlivers(Mesh, Tolerance)
{
    const {Vertices}=Mesh;
    let Collapses=0;
    for (let Pass=0;Pass<160;++Pass)
    {
        const Angles=Mesh.Triangles.map(Triangle=>MinimumAngle(Triangle,Vertices));
        const Thin=Angles.map((Angle,Index)=>({Angle,Index})).filter(Entry=>Entry.Angle<5).sort((A,B)=>A.Angle-B.Angle);
        if (!Thin.length) break;
        const Neighbours=new Map(), Incident=new Map();
        Mesh.Triangles.forEach((Triangle,Index)=>Triangle.forEach(A=>
        {
            if (!Neighbours.has(A)) Neighbours.set(A,new Set());
            if (!Incident.has(A)) Incident.set(A,[]);
            Incident.get(A).push(Index);
            Triangle.forEach(B=>{if (A!==B) Neighbours.get(A).add(B);});
        }));
        let Applied=false;
        for (const Entry of Thin)
        {
            const Triangle=Mesh.Triangles[Entry.Index];
            const Edges=Triangle.map((A,Index)=>[A,Triangle[(Index+1)%3]]).sort((A,B)=>
                Length(Subtract(Vertices[A[0]],Vertices[A[1]]))-Length(Subtract(Vertices[B[0]],Vertices[B[1]])));
            for (const [A,B] of Edges)
            {
                if (Length(Subtract(Vertices[A],Vertices[B]))>Tolerance) continue;
                const Common=[...Neighbours.get(A)].filter(Vertex=>Neighbours.get(B).has(Vertex));
                if (Common.length!==2) continue;
                for (const [Keep,Drop] of [[A,B],[B,A]])
                {
                    const Affected=[...new Set([...Incident.get(Keep),...Incident.get(Drop)])];
                    const Before=Affected.filter(Index=>Angles[Index]<5).length;
                    let Valid=true, After=0, NewMinimum=180;
                    for (const Index of Affected)
                    {
                        const Old=Mesh.Triangles[Index];
                        if (Old.includes(Keep) && Old.includes(Drop)) continue;
                        const Next=Old.map(Vertex=>Vertex===Drop?Keep:Vertex);
                        const [P,Q,R]=Old.map(Vertex=>Vertices[Vertex]);
                        const [S,T,U]=Next.map(Vertex=>Vertices[Vertex]);
                        const First=Cross(Subtract(Q,P),Subtract(R,P));
                        const Second=Cross(Subtract(T,S),Subtract(U,S));
                        if (Length(Second)<1e-9 || Dot(First,Second)<Length(First)*Length(Second)*.8)
                        {
                            Valid=false;
                            break;
                        }
                        const Angle=MinimumAngle(Next,Vertices);
                        if (Angle<5) ++After;
                        NewMinimum=Math.min(NewMinimum,Angle);
                    }
                    const PreviousMinimum=Math.min(...Affected.map(Index=>Angles[Index]));
                    if (!Valid || After>Before || (After===Before && NewMinimum<=PreviousMinimum+1e-5)) continue;
                    const Replacements=Affected.map(Index=>Mesh.Triangles[Index].map(Vertex=>Vertex===Drop?Keep:Vertex))
                        .filter(Triangle=>new Set(Triangle).size===3);
                    if (CrossesSurface(Mesh,Affected,Replacements)) continue;
                    const Triangles=[],Tags=[];
                    Mesh.Triangles.forEach((Triangle,Index)=>
                    {
                        const Next=Triangle.map(Vertex=>Vertex===Drop?Keep:Vertex);
                        if (new Set(Next).size===3)
                        {
                            Triangles.push(Next);
                            Tags.push(Mesh.Tags[Index]);
                        }
                    });
                    Mesh.Triangles=Triangles;
                    Mesh.Tags=Tags;
                    ++Collapses;
                    Applied=true;
                    break;
                }
                if (Applied) break;
            }
            if (Applied) break;
        }
        if (!Applied) break;
    }
    const Remap=new Map();
    const Compact=[];
    Mesh.Triangles=Mesh.Triangles.map(Triangle=>Triangle.map(Index=>
    {
        if (!Remap.has(Index))
        {
            Remap.set(Index,Compact.length);
            Compact.push(Vertices[Index]);
        }
        return Remap.get(Index);
    }));
    Mesh.Vertices=Compact;
    Mesh.Collapses=Collapses;
    return Mesh;
}

// 📝 Flip nearly coplanar cap diagonals only within the positional tolerance. This removes long flat
// 📝 triangles at clipped profile seams without moving vertices or rounding whole rock edges.
export function FlipCaps(Mesh, Tolerance)
{
    let Flips=0;
    for (let Pass=0;Pass<200;++Pass)
    {
        const Edges=new Map();
        Mesh.Triangles.forEach((Triangle,Index)=>Triangle.forEach((A,Side)=>
        {
            const B=Triangle[(Side+1)%3], C=Triangle[(Side+2)%3];
            const Key=[A,B].sort((A,B)=>A-B).join('/');
            if (!Edges.has(Key)) Edges.set(Key,[]);
            Edges.get(Key).push({A,B,C,Index});
        }));
        let Applied=false;
        for (const Pair of Edges.values())
        {
            if (Pair.length!==2) continue;
            const [First,Second]=Pair;
            const {A,B,C}=First, D=Second.C;
            if (Edges.has([C,D].sort((A,B)=>A-B).join('/'))) continue;
            const Before=Math.min(MinimumAngle(Mesh.Triangles[First.Index],Mesh.Vertices),MinimumAngle(Mesh.Triangles[Second.Index],Mesh.Vertices));
            if (Before>=5) continue;
            const [P,Q,R,S]=[A,B,C,D].map(Index=>Mesh.Vertices[Index]);
            const AB=Subtract(Q,P);
            const Distance=Point=>Length(Cross(Subtract(Point,P),AB))/Length(AB);
            if (Math.min(Distance(R),Distance(S))>Tolerance) continue;
            const OldNormal=Cross(AB,Subtract(R,P));
            const OtherNormal=Cross(Subtract(P,Q),Subtract(S,Q));
            if (Dot(OldNormal,OtherNormal)<Length(OldNormal)*Length(OtherNormal)*.8) continue;
            const Next=[[C,A,D],[C,D,B]];
            const After=Math.min(...Next.map(Triangle=>MinimumAngle(Triangle,Mesh.Vertices)));
            if (After<Before+.1) continue;
            if (Next.some(Triangle=>
            {
                const [A,B,C]=Triangle.map(Index=>Mesh.Vertices[Index]);
                const Normal=Cross(Subtract(B,A),Subtract(C,A));
                return Dot(Normal,OldNormal)<=0 || Dot(Normal,OtherNormal)<=0;
            })) continue;
            if (CrossesSurface(Mesh,[First.Index,Second.Index],Next)) continue;
            Mesh.Triangles[First.Index]=Next[0];
            Mesh.Triangles[Second.Index]=Next[1];
            ++Flips;
            Applied=true;
            break;
        }
        if (!Applied) break;
    }
    Mesh.Flips=Flips;
    return Mesh;
}

export function SnapCaps(Mesh, Tolerance)
{
    let Repairs=0;
    const Attempted=new Set();
    for (let Pass=0;Pass<40;++Pass)
    {
        let Applied=false;
        for (let Index=0;Index<Mesh.Triangles.length;++Index)
        {
            const Triangle=Mesh.Triangles[Index];
            if (MinimumAngle(Triangle,Mesh.Vertices)>=5) continue;
            const Key=Triangle.join('/');
            if (Attempted.has(Key)) continue;
            Attempted.add(Key);
            const Sides=Triangle.map((A,Side)=>({A,B:Triangle[(Side+1)%3],C:Triangle[(Side+2)%3]}));
            Sides.sort((First,Second)=>Length(Subtract(Mesh.Vertices[Second.A],Mesh.Vertices[Second.B]))-Length(Subtract(Mesh.Vertices[First.A],Mesh.Vertices[First.B])));
            const {A,B,C}=Sides[0];
            const [P,Q,R]=[A,B,C].map(Vertex=>Mesh.Vertices[Vertex]);
            const Δ=Subtract(Q,P);
            const Fraction=Dot(Subtract(R,P),Δ)/Dot(Δ,Δ);
            const Point=P.map((Value,Axis)=>Value+Δ[Axis]*Fraction);
            if (Fraction<.001 || Fraction>.999 || Length(Subtract(Point,R))>Tolerance) continue;
            const Split=Mesh.Vertices.length;
            const Candidate={...Mesh,Vertices:[...Mesh.Vertices,Point],Triangles:[],Tags:[]};
            Mesh.Triangles.forEach((Triangle,Index)=>
            {
                if (Triangle.includes(A) && Triangle.includes(B))
                {
                    for (let Side=0;Side<3;++Side)
                    {
                        const First=Triangle[Side],Second=Triangle[(Side+1)%3],Third=Triangle[(Side+2)%3];
                        if ((First===A && Second===B) || (First===B && Second===A))
                        {
                            Candidate.Triangles.push([First,Split,Third],[Split,Second,Third]);
                            Candidate.Tags.push(Mesh.Tags[Index],Mesh.Tags[Index]);
                            break;
                        }
                    }
                }
                else
                {
                    Candidate.Triangles.push(Triangle);
                    Candidate.Tags.push(Mesh.Tags[Index]);
                }
            });
            const Before=Mesh.Triangles.filter(Triangle=>MinimumAngle(Triangle,Mesh.Vertices)<5).length;
            CollapseSlivers(Candidate,Tolerance);
            const After=Candidate.Triangles.filter(Triangle=>MinimumAngle(Triangle,Candidate.Vertices)<5).length;
            if (After>=Before) continue;
            Mesh=Candidate;
            ++Repairs;
            Applied=true;
            Attempted.clear();
            break;
        }
        if (!Applied) break;
    }
    Mesh.CapRepairs=Repairs;
    return Mesh;
}
