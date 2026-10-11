//============================================================================================================================================
//                                                            FRACTURESEQUENCE.JS
//============================================================================================================================================
// 📦 Profiled cliff mass, bedding, finite joints, local spalls and shallow polygon fissures.

import {ConstructRelief} from './ReliefProjection.js';
import {FractureMass} from './RuptureSolver.js';
import {CollapseSlivers, FlipCaps, SnapCaps} from './TriangleSolver.js';
import {RecessPlans, BeddingPlans, JointPlans, SpallPlans, CrackPlans, SelectCatalogue, ReadSpecification, EarliestStage} from './CliffSpecification.js';
import {Add, Subtract, Scale, Dot, Cross, Length, Normalize, Lerp, Centre, PointKey, EdgeKey, Face, Frame,
    ClipCells, JoinCells, TriangulateFace, TriangulateBody, InsideMesh, MeshMetrics, PointInLoop, LoopClearance} from './PolyhedronSolver.js';

function ConstructMass(Specification)
{
    const {Rows,RearRows,Sections,Stations}=ConstructRelief(Specification);
    const Cells=[];
    for (let Column=0;Column<Rows.length-1;++Column)
    {
        for (let Level=0;Level<Sections.length-1;++Level)
        {
            const CornerIndices=[[Column,Level],[Column+1,Level],[Column+1,Level+1],[Column,Level+1]];
            const Alternatives=[[[0,1,2],[0,2,3]],[[0,1,3],[1,2,3]]];
            const Quality=Triangles=>Math.min(...Triangles.flatMap(Triangle=>[Rows,RearRows].map(Grid=>
            {
                const Points=Triangle.map(Index=>Grid[CornerIndices[Index][0]][CornerIndices[Index][1]]);
                const Along=Subtract(Points[1],Points[0]),Across=Subtract(Points[2],Points[0]);
                if (Along[0]*Across[1]-Along[1]*Across[0]<=1e-5) return -1;
                const Area=Length(Cross(Along,Across));
                const Edges=Points.map((Point,Index)=>Length(Subtract(Point,Points[(Index+1)%3])));
                return Math.min(...Edges.map((Edge,Index)=>Area/(Edge*Edges[(Index+1)%3])));
            })));
            // 📝 Tall crowns require the short, well-conditioned loft diagonal rather than a fixed strip direction.
            if (Specification.NoiseMode!=='None' && Specification.Variation && Quality(Alternatives[1])>Quality(Alternatives[0])) Alternatives.reverse();
            for (const Triangle of Alternatives[0])
            {
                const Indices=Triangle.map(Index=>CornerIndices[Index]);
                const Front=Indices.map(([Column,Level])=>Rows[Column][Level]);
                const Back=Indices.map(([Column,Level])=>RearRows[Column][Level]);
                const Middle=Centre([...Front,...Back]);
                const Faces=[Face(Front,'Cliff',[0,0,1]),Face(Back,'Cliff',[0,0,-1])];
                for (let Index=0;Index<3;++Index)
                {
                    const Next=(Index+1)%3;
                    const [A,B]=[Indices[Index],Indices[Next]];
                    const Tag=A[1]===B[1] && A[1]===0 ? 'Base' :
                        A[1]===B[1] && A[1]===Sections.length-1 ? 'Crown' :
                        A[0]===B[0] && (A[0]===0 || A[0]===Stations.length-1) ? 'Cliff' : 'Construction';
                    const Loop=[Front[Index],Back[Index],Back[Next],Front[Next]];
                    Faces.push(Face(Loop,Tag,Subtract(Centre(Loop),Middle)));
                }
                Cells.push(Faces);
            }
        }
    }
    return Cells;
}

function SeparateCutFromCorners(Cells, Direction, Desired, Clearance, MaximumShift=.6)
{
    const Coordinates=[...new Set(Cells.flatMap(Cell=>Cell.flatMap(Polygon=>Polygon.Loop.map(Point=>Dot(Point,Direction)))))];
    const Candidates=[Desired,...Coordinates.flatMap(Value=>[Value-Clearance-1e-5,Value+Clearance+1e-5])];
    Candidates.sort((A,B)=>Math.abs(A-Desired)-Math.abs(B-Desired));
    return Candidates.find(Value=>Math.abs(Value-Desired)<MaximumShift && Coordinates.every(Corner=>Math.abs(Value-Corner)>=Clearance)) ?? Desired;
}

// 📝 Authored flexure and step-over traces, expressed as continuous piecewise planes.
// 📝 These cut surfaces, rather than displacing the cliff vertices or sampling a noise field.
const Flexures=[
    [[-.8,0],[-.36,.05],[-.28,.7],[-.06,.65],[.06,-.55],[.29,-.48],[.38,.25],[.8,.1]],
    [[-.8,.2],[-.43,.2],[-.23,-.6],[.02,-.48],[.12,.65],[.34,.55],[.43,-.3],[.8,-.2]],
    [[-.8,-.1],[-.4,-.35],[-.3,.4],[-.12,.6],[.14,-.45],[.25,-.35],[.46,.5],[.8,.2]]];

function ClipFlexure(Cells, Level, BedIndex, Side, Specification)
{
    if (!Number.isFinite(Level)) return Cells;
    const {Width,Height,Beds,Dip,Aperture,FractureBend}=Specification;
    const Knots=Flexures[BedIndex%Flexures.length];
    const Axis=[1,0,[.28,-.19,.38][BedIndex%3]];
    const Amplitude=Math.min(Height/Beds*.30,1)*FractureBend;
    const Result=[];
    for (let Segment=0;Segment<Knots.length-1;++Segment)
    {
        const [U,A]=Knots[Segment], [V,B]=Knots[Segment+1];
        let Pieces=Cells;
        if (Segment>0) Pieces=ClipCells(Pieces,Scale(Axis,-1),-U*Width,'Construction');
        if (Segment<Knots.length-2) Pieces=ClipCells(Pieces,Axis,V*Width,'Construction');
        const Slope=(B-A)*Amplitude/((V-U)*Width);
        const Intercept=A*Amplitude-Slope*U*Width;
        // 📝 Seams open at a step-over and pinch along intact portions; they are not uniform saw slots.
        const OpenA=.22+Math.abs(A)*1.65, OpenB=.22+Math.abs(B)*1.65;
        const GapSlope=(OpenB-OpenA)*Aperture*.5/((V-U)*Width);
        const GapIntercept=OpenA*Aperture*.5-GapSlope*U*Width;
        const Direction=Subtract([-Math.tan(Dip*Math.PI/180),1,.035],Scale(Axis,Slope-Side*GapSlope));
        const Offset=Level+Intercept-Side*GapIntercept;
        Pieces=ClipCells(Pieces,Scale(Direction,Side),Offset*Side,'Bedding');
        Result.push(...Pieces);
    }
    return Result;
}

function SliceBeds(Cells, Specification, Select)
{
    const {Height,Beds}=Specification;
    const Sequence=BeddingPlans[Select(BeddingPlans.length)];
    const Weights=Array.from({length:Beds},(_,Index)=>Sequence[Index%Sequence.length]);
    const Total=Weights.reduce((A,B)=>A+B);
    const Levels=[-Infinity];
    let Sum=0;
    for (let Index=0;Index<Beds-1;++Index)
    {
        Sum+=Weights[Index];
        Levels.push(Sum/Total*Height*.99);
    }
    Levels.push(Infinity);
    return Weights.map((Weight,Index)=>
    {
        let Pieces=ClipFlexure(Cells,Levels[Index],Index,-1,Specification);
        Pieces=ClipFlexure(Pieces,Levels[Index+1],Index+1,1,Specification);
        return {Name:`Bed ${String(Index+1).padStart(2,'0')}`,Cells:Pieces,Layer:Index,
            Middle:(Number.isFinite(Levels[Index])?Levels[Index]:0)+Weight/Total*Height*.5};
    }).filter(Bed=>Bed.Cells.length);
}

function SplitJoints(Beds, Specification, Select)
{
    const {Depth,Width,JointSpacing,Penetration,Aperture,FractureBend}=Specification;
    const Family=JointPlans[Select(JointPlans.length)];
    const Result=[];
    const Middle=-Depth*.48, Half=Depth*(1-Penetration)*.5;
    for (const Bed of Beds)
    {
        const CentreCells=ClipCells(ClipCells(Bed.Cells,[0,0,1],Middle+Half-Aperture*.5,'Termination'),[0,0,-1],-Middle+Half-Aperture*.5,'Termination');
        const Core=ClipCells(ClipCells(CentreCells,[1,0,0],Width*.27-Aperture*.5,'Termination'),[-1,0,0],Width*.27-Aperture*.5,'Termination');
        if (Core.length) Result.push({Name:`${Bed.Name} · core`,Cells:Core,Layer:Bed.Layer,Rear:true});
        const Exposures=[
            {Name:'front',Cells:ClipCells(Bed.Cells,[0,0,-1],-Middle-Half-Aperture*.5,'Termination'),Out:[0,0,1]},
            {Name:'rear',Cells:ClipCells(Bed.Cells,[0,0,1],Middle-Half-Aperture*.5,'Termination'),Out:[0,0,-1]},
            {Name:'left',Cells:ClipCells(CentreCells,[1,0,0],-Width*.27-Aperture*.5,'Termination'),Out:[-1,0,0]},
            {Name:'right',Cells:ClipCells(CentreCells,[-1,0,0],-Width*.27-Aperture*.5,'Termination'),Out:[1,0,0]}];
        for (const Exposure of Exposures)
        {
            if (!Exposure.Cells.length) continue;
            const AlongEnd=Math.abs(Exposure.Out[0])>.5;
            const Span=AlongEnd?Depth:Width;
            const CentreCoordinate=AlongEnd?Middle:0;
            const Positions=[-Infinity];
            const Stagger=Family.Stagger[Bed.Layer%4]*JointSpacing;
            if (!AlongEnd) for (let U=CentreCoordinate-Span*.5+JointSpacing+Stagger;U<CentreCoordinate+Span*.5;U+=JointSpacing) Positions.push(U);
            Positions.push(Infinity);
            const JointDirection=AlongEnd?[Family.Obliquity,Family.Lean,1]:[1,Family.Lean,Family.Obliquity];
            const BendAmount=FractureBend*[.21,-.26,.16][Bed.Layer%3];
            const Boundary=JoinCells(Exposure.Cells,Exposure.Name).Faces;
            const Corners=[...new Set(Boundary.flatMap(Polygon=>Polygon.Loop.map(Point=>
                Dot(Point,JointDirection)-BendAmount*Math.max(0,Point[1]-Bed.Middle))))];
            for (let Cut=1;Cut<Positions.length-1;++Cut)
            {
                const Desired=Positions[Cut],Clearance=.13;
                const Candidates=[Desired,...Corners.flatMap(Corner=>[Corner-Clearance-1e-5,Corner+Clearance+1e-5])];
                Candidates.sort((First,Second)=>Math.abs(First-Desired)-Math.abs(Second-Desired));
                Positions[Cut]=Candidates.find(Candidate=>Math.abs(Candidate-Desired)<.5 &&
                    Corners.every(Corner=>Math.abs(Candidate-Corner)>=Clearance))??Desired;
            }
            for (let Index=0;Index<Positions.length-1;++Index)
            {
                const Pieces=[];
                for (const Upper of [false,true])
                {
                    let Parts=ClipCells(Exposure.Cells,[0,Upper?-1:1,0],Upper?-Bed.Middle:Bed.Middle,'Construction');
                    const Bend=Upper ? FractureBend*[.21,-.26,.16][Bed.Layer%3] : 0;
                    const Direction=AlongEnd?[Family.Obliquity,Family.Lean-Bend,1]:[1,Family.Lean-Bend,Family.Obliquity];
                    const Shift=-Bend*Bed.Middle;
                    if (Number.isFinite(Positions[Index])) Parts=ClipCells(Parts,Scale(Direction,-1),-Positions[Index]-Shift-Aperture*.38,'Joint');
                    if (Number.isFinite(Positions[Index+1])) Parts=ClipCells(Parts,Direction,Positions[Index+1]+Shift-Aperture*.38,'Joint');
                    Pieces.push(...Parts);
                }
                let Carved=Pieces;
                if (Carved.length && Specification.FaceRecess>0)
                {
                    const Feature=RecessPlans[Select(RecessPlans.length)];
                    const Outward=Normalize(Add(Exposure.Out,[Feature.Normal[0]*.2,Feature.Normal[1],0]));
                    const Support=Math.max(...Carved.flatMap(Cell=>Cell.flatMap(Face=>Face.Loop.map(Point=>Dot(Point,Outward)))));
                    const Recession=Specification.FaceRecess*Feature.Depth*[.35,1,.25,.8,.15,.65,.3][Bed.Layer%7];
                    const Offset=SeparateCutFromCorners(Carved,Outward,Support-Recession,.10,.25);
                    Carved=ClipCells(Carved,Outward,Math.min(Support,Offset),'Cliff');
                }
                if (Carved.length) Result.push({Name:`${Bed.Name} · ${Exposure.Name} ${Index+1}`,Cells:Carved,Layer:Bed.Layer});
            }
        }
    }
    return Result;
}

function EdgeIncidence(Body)
{
    const Edges=new Map();
    Body.Faces.forEach((Polygon,FaceIndex)=>Polygon.Loop.forEach((A,Index)=>
    {
        const B=Polygon.Loop[(Index+1)%Polygon.Loop.length];
        const Key=EdgeKey(A,B);
        if (!Edges.has(Key)) Edges.set(Key,[]);
        Edges.get(Key).push({A,B,FaceIndex,Index});
    }));
    return [...Edges.values()].filter(Edges=>Edges.length===2);
}

function InteriorTriangle(Polygon, Points)
{
    const Basis=Frame(Polygon);
    const Loop=Polygon.Loop.map(Basis.Project);
    // 📝 Sample the complete proposed triangular patch, not just its deepest corner.
    for (let First=1;First<8;++First)
    {
        for (let Second=1;Second<8-First;++Second)
        {
            const Point=Add(Scale(Points[0],First/8),Add(Scale(Points[1],Second/8),Scale(Points[2],1-(First+Second)/8)));
            if (!PointInLoop(Basis.Project(Point),Loop)) return false;
        }
    }
    return true;
}

function CarveSpalls(Body, Specification, Select)
{
    if (Body.Rear || !Specification.SpallDensity) return Body;
    const Source=TriangulateBody(Body,Infinity);
    const Busy=new Set();
    const Original=Body.Faces.slice();
    const Edges=EdgeIncidence(Body).sort((First,Second)=>Length(Subtract(Second[0].A,Second[0].B))-Length(Subtract(First[0].A,First[0].B)));
    for (const Pair of Edges)
    {
        const [First,Second]=Pair;
        const FirstFace=Original[First.FaceIndex], SecondFace=Original[Second.FaceIndex];
        if (Busy.has(First.FaceIndex) || Busy.has(Second.FaceIndex)) continue;
        if (FirstFace.Holes.length || SecondFace.Holes.length) continue;
        if (![FirstFace.Tag,SecondFace.Tag].some(Tag=>Tag==='Cliff' || Tag==='Crown')) continue;
        const Cosine=Dot(FirstFace.Normal,SecondFace.Normal);
        if (Cosine>.90 || Cosine<-.7) continue;
        if (Dot(FirstFace.Normal,Subtract(Centre(SecondFace.Loop),First.A))>1e-6) continue;
        const EdgeLength=Length(Subtract(First.B,First.A));
        if (EdgeLength<.7 || Select(100)>=Specification.SpallDensity*100) continue;
        const Plan=SpallPlans[Select(SpallPlans.length)];
        const Tangent=Normalize(Subtract(First.B,First.A));
        const IntoFirst=Normalize(Cross(FirstFace.Normal,Tangent));
        const IntoSecond=Normalize(Cross(SecondFace.Normal,Scale(Tangent,-1)));
        const Middle=Lerp(First.A,First.B,Plan.Position);
        let Size=Math.min(Specification.SpallSize,EdgeLength*.30,
            EdgeLength*Math.min(Plan.Position-.10,.90-Plan.Position)/Plan.Length);
        for (let Attempt=0;Attempt<5;++Attempt,Size*=.7)
        {
            if (Size<.12) break;
            const Start=Add(Middle,Scale(Tangent,-Size*Plan.Length));
            const End=Add(Middle,Scale(Tangent,Size*Plan.Length));
            const FirstLip=Add(Middle,Add(Scale(IntoFirst,Size*Plan.FirstDepth),Scale(Tangent,Size*Plan.Shear)));
            const SecondLip=Add(Middle,Add(Scale(IntoSecond,Size*Plan.SecondDepth),Scale(Tangent,-Size*Plan.Shear)));
            const FirstPath=Plan.Shape==='Flake' ? [Add(FirstLip,Add(Scale(Tangent,-Size*.35),Scale(IntoFirst,Size*.12))),
                Add(FirstLip,Add(Scale(Tangent,Size*.50),Scale(IntoFirst,-Size*.05)))] : [FirstLip];
            const SecondPath=Plan.Shape==='Flake' ? [Add(SecondLip,Add(Scale(Tangent,-Size*.28),Scale(IntoSecond,Size*.08))),
                Add(SecondLip,Add(Scale(Tangent,Size*.40),Scale(IntoSecond,-Size*.09)))] : [SecondLip];
            const LipInside=(Polygon,Lip)=>
            {
                const Basis=Frame(Polygon), Loop=Polygon.Loop.map(Basis.Project), Point=Basis.Project(Lip);
                return PointInLoop(Point,Loop) && LoopClearance(Point,Loop)>Math.min(.025,Size*.15);
            };
            if (!FirstPath.every(Point=>LipInside(FirstFace,Point) && InteriorTriangle(FirstFace,[Start,End,Point])) ||
                !SecondPath.every(Point=>LipInside(SecondFace,Point) && InteriorTriangle(SecondFace,[End,Start,Point]))) continue;
            const Root=Subtract(Centre([Start,End,...FirstPath,...SecondPath]),Scale(Normalize(Add(FirstFace.Normal,SecondFace.Normal)),Size*Plan.Root));
            const Rim=[Start,...SecondPath,End,...FirstPath.slice().reverse()];
            if (!InsideMesh(Root,Source) || Rim.some(Point=>!InsideMesh(Lerp(Point,Root,.5),Source))) continue;
            const Replace=(Record,Path,Reverse)=>
            {
                const Polygon=Body.Faces[Record.FaceIndex];
                const Loop=Polygon.Loop.slice();
                Loop.splice(Record.Index+1,0,...(Reverse ? [End,...Path.slice().reverse(),Start] : [Start,...Path,End]));
                Body.Faces[Record.FaceIndex]={...Polygon,Loop};
            };
            Replace(First,FirstPath,false);
            Replace(Second,SecondPath,true);
            // 📝 Bounded fracture facets replace a local edge patch; both original edge ends survive.
            const Feature=Body.Spalls.length;
            Rim.forEach((Point,Index)=>Body.Faces.push({...Face([Point,Rim[(Index+1)%Rim.length],Root],'Spall'),Feature}));
            Body.Spalls.push({Centre:Middle,Root,Rim,Size,Shape:Plan.Shape,Feature,AffectedFaces:[First.FaceIndex,Second.FaceIndex],
                OriginalFaces:[FirstFace,SecondFace],OriginalEdge:[First.A,First.B]});
            Busy.add(First.FaceIndex);
            Busy.add(Second.FaceIndex);
            break;
        }
    }
    return Body;
}

function CarveCracks(Body, Specification, Select)
{
    if (Body.Rear || !Specification.CrackDensity || Select(100)>=Specification.CrackDensity*100) return Body;
    const Source=TriangulateBody(Body,Infinity);
    const Faces=Body.Faces.filter(Polygon=>Polygon.Tag==='Cliff' && !Polygon.Holes.length);
    Faces.sort((A,B)=>
    {
        const Area=Polygon=>Length(Polygon.Loop.slice(1,-1).reduce((Sum,Point,Index)=>
            Add(Sum,Cross(Subtract(Point,Polygon.Loop[0]),Subtract(Polygon.Loop[Index+2],Polygon.Loop[0]))),[0,0,0]));
        return Area(B)-Area(A);
    });
    const Plan=CrackPlans[Select(CrackPlans.length)];
    for (const Polygon of Faces.slice(0,3))
    {
        const Basis=Frame(Polygon);
        const Loop=Polygon.Loop.map(Basis.Project);
        const Patch=TriangulateFace(Polygon);
        const Candidates=Patch.Triangles.map(Triangle=>Centre(Triangle.map(Index=>Patch.Vertices[Index])));
        Candidates.push(Centre(Polygon.Loop));
        Candidates.sort((A,B)=>LoopClearance(Basis.Project(B),Loop)-LoopClearance(Basis.Project(A),Loop));
        const Direction=Normalize(Subtract([Plan.Tilt,1,0],Scale(Polygon.Normal,Dot([Plan.Tilt,1,0],Polygon.Normal))));
        const Across=Normalize(Cross(Polygon.Normal,Direction));
        for (let Attempt=0;Attempt<4;++Attempt)
        {
            const Size=Specification.CrackLength*Math.pow(.8,Attempt);
            const Width=Specification.CrackWidth;
            const Middle=Add(Candidates[0],Scale(Across,Plan.Offset*Size));
            const Path=Plan.Path.map(([Along,Side])=>Add(Middle,Add(Scale(Direction,Along*Size),Scale(Across,Side*Size))));
            // 📝 The V-groove is shallow and finite. It does not split the block or use a signed-distance field.
            const Root=Path.map((Point,Index)=>Subtract(Point,Scale(Polygon.Normal,Specification.CrackDepth*[.5,1,.8,.45][Index])));
            const Left=Path.map((Point,Index)=>Add(Point,Scale(Across,Width*.5*[.35,1,1,.3][Index])));
            const Right=Path.map((Point,Index)=>Subtract(Point,Scale(Across,Width*.5*[.35,1,1,.3][Index])));
            let Rim=[...Left,...Right.slice().reverse()];
            let RootIndices=[0,1,2,3,3,2,1,0];
            if (!Rim.every(Point=>PointInLoop(Basis.Project(Point),Loop) && LoopClearance(Basis.Project(Point),Loop)>Width*1.6)) continue;
            if (!Root.every(Point=>InsideMesh(Point,Source))) continue;
            if (PolygonArea(Rim.map(Basis.Project))<0)
            {
                Rim.reverse();
                RootIndices.reverse();
            }
            Polygon.CrackOriginalHoles=Polygon.Holes.slice();
            Polygon.Holes.push(Rim.slice().reverse());
            Rim.forEach((A,Index)=>
            {
                const Next=(Index+1)%Rim.length;
                const B=Rim[Next], First=Root[RootIndices[Index]], Second=Root[RootIndices[Next]];
                if (RootIndices[Index]===RootIndices[Next]) Body.Faces.push(Face([A,B,First],'Crack'));
                else
                {
                    Body.Faces.push(Face([A,B,Second],'Crack'));
                    Body.Faces.push(Face([A,Second,First],'Crack'));
                }
            });
            Body.Cracks.push({Centre:Middle,Root,Rim,Depth:Specification.CrackDepth,Length:Size});
            return Body;
        }
    }
    return Body;
}

function PolygonArea(Points)
{
    return Points.reduce((Sum,A,Index)=>
    {
        const B=Points[(Index+1)%Points.length];
        return Sum+A[0]*B[1]-B[0]*A[1];
    },0)*.5;
}

function EmitStage(Bodies, Number, Specification, Previous)
{
    const Defective=Mesh=>
    {
        const Metrics=MeshMetrics(Mesh);
        return Metrics.OpenEdges+Metrics.NonmanifoldEdges+Metrics.NonmanifoldVertices+
            Metrics.DuplicateTriangles+Metrics.WindingErrors+Metrics.ZeroArea;
    };
    const Compile=Body=>SnapCaps(FlipCaps(CollapseSlivers(FlipCaps(TriangulateBody(Body,Specification.TriangleSpan*(Number===1 && Specification.Height<=Specification.Width*.8?3:1)),.12),.12),.12),.12);
    const Meshes=Bodies.map(Body=>
    {
        let Mesh=Compile(Body);
        const PreviousCount=Previous?.Records.find(Record=>Record.Name===Body.Name)?.ThinTriangles||0;
        // 📝 Damage is transactional: reject a local feature if it worsens the narrow-triangle budget.
        if (Number===4)
        {
            while ((Defective(Mesh) || MeshMetrics(Mesh).ThinTriangles>PreviousCount) && Body.Spalls.length)
            {
                const Narrow=Mesh.Triangles.find(Triangle=>
                {
                    const Points=Triangle.map(Index=>Mesh.Vertices[Index]);
                    return Points.some((Point,Index)=>
                    {
                        const A=Normalize(Subtract(Points[(Index+1)%3],Point));
                        const B=Normalize(Subtract(Points[(Index+2)%3],Point));
                        return Dot(A,B)>Math.cos(5*Math.PI/180);
                    });
                });
                const Position=Narrow ? Centre(Narrow.map(Index=>Mesh.Vertices[Index])) : Body.Spalls.at(-1).Centre;
                const Ordered=Body.Spalls.slice().sort((A,B)=>Length(Subtract(A.Centre,Position))-Length(Subtract(B.Centre,Position)));
                const Rejected=Ordered[0];
                Rejected.AffectedFaces.forEach((FaceIndex,Index)=>{Body.Faces[FaceIndex]=Rejected.OriginalFaces[Index];});
                Body.Faces=Body.Faces.filter(Polygon=>!(Polygon.Tag==='Spall' && Polygon.Feature===Rejected.Feature));
                Body.Spalls=Body.Spalls.filter(Spall=>Spall!==Rejected);
                Body.RejectedSpalls=(Body.RejectedSpalls||0)+1;
                Mesh=Compile(Body);
            }
        }
        if (Number===5 && (Defective(Mesh) || MeshMetrics(Mesh).ThinTriangles>PreviousCount) && Body.Cracks.length)
        {
            Body.Faces=Body.Faces.filter(Polygon=>Polygon.Tag!=='Crack').map(Polygon=>({...Polygon,Holes:Polygon.CrackOriginalHoles||Polygon.Holes}));
            Body.RejectedCracks=Body.Cracks.length;
            Body.Cracks=[];
            Mesh=Compile(Body);
        }
        Mesh.Spalls=Mesh.Spalls.map(({OriginalFaces,...Record})=>Record);
        Mesh.RejectedSpalls=Body.RejectedSpalls||0;
        Mesh.RejectedCracks=Body.RejectedCracks||0;
        return Mesh;
    });
    const Records=Meshes.map(Mesh=>({...MeshMetrics(Mesh),Name:Mesh.Name}));
    const Sum=Key=>Records.reduce((Total,Record)=>Total+Record[Key],0);
    const Metrics={Stage:Number,Bodies:Meshes.length,Vertices:Sum('Vertices'),Triangles:Sum('Triangles'),
        OpenEdges:Sum('OpenEdges'),NonmanifoldEdges:Sum('NonmanifoldEdges'),WindingErrors:Sum('WindingErrors'),
        ZeroArea:Sum('ZeroArea'),NonmanifoldVertices:Sum('NonmanifoldVertices'),DuplicateTriangles:Sum('DuplicateTriangles'),ThinTriangles:Sum('ThinTriangles'),MinimumAngle:Math.min(...Records.map(Record=>Record.MinimumAngle)),
        RejectedSpalls:Meshes.reduce((Total,Mesh)=>Total+Mesh.RejectedSpalls,0),RejectedCracks:Meshes.reduce((Total,Mesh)=>Total+Mesh.RejectedCracks,0),
        Volume:Sum('Volume'),Spalls:Meshes.reduce((Total,Mesh)=>Total+Mesh.Spalls.length,0),Cracks:Meshes.reduce((Total,Mesh)=>Total+Mesh.Cracks.length,0)};
    return {Number,Meshes,Metrics,Records};
}

export class CliffSequence
{
    constructor()
    {
        this.Specification=null;
        this.Stages=[];
        this.Construction=[];
    }

    Generate(Input, Progress=()=>{}, Through=5)
    {
        const Specification=ReadSpecification(Input);
        Through=Math.max(1,Math.min(5,Math.round(Through)));
        const Changed=EarliestStage(this.Specification,Specification);
        this.Stages.length=Math.min(this.Stages.length,Changed-1);
        this.Construction.length=this.Stages.length;
        this.Specification=Specification;
        const ReusedStages=this.Stages.slice(0,Through).map(Stage=>Stage.Number);
        const ExecutedStages=[],Timings={};
        for (let Number=this.Stages.length+1;Number<=Through;++Number)
        {
            const Started=performance.now();
            Progress(Number);
            const Select=SelectCatalogue(Specification.FractureSeed+Number*104729);
            const Previous=this.Construction[Number-2];
            let Content,Bodies;
            if (Number===1)
            {
                Content=ConstructMass(Specification);
                Bodies=[JoinCells(Content,'Cliff mass')];
            }
            else if (Number===2)
            {
                Content=Specification.FractureStyle==='Bedding'?SliceBeds(Previous,Specification,Select):FractureMass(Previous,Specification,Select);
                Bodies=Content.map(Bed=>JoinCells(Bed.Cells,Bed.Name));
            }
            else if (Number===3)
            {
                Content=SplitJoints(Previous,Specification,Select).map(Part=>({...JoinCells(Part.Cells,Part.Name),Rear:Part.Rear}));
                Bodies=Content;
            }
            else
            {
                Content=Previous.map(Body=>(Number===4?CarveSpalls:CarveCracks)(structuredClone(Body),Specification,Select));
                Bodies=Content;
            }
            const Stage=EmitStage(Bodies,Number,Specification,this.Stages.at(-1));
            this.Construction.push(Content);
            this.Stages.push(Stage);
            ExecutedStages.push(Number);
            Timings[Number]=performance.now()-Started;
        }
        return {Specification,Stages:this.Stages.slice(),Through,ExecutedStages,ReusedStages,Timings};
    }
}

export function GenerateCliff(Input, Progress=()=>{}, Through=5)
{
    return new CliffSequence().Generate(Input,Progress,Through);
}
