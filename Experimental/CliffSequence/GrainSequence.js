//============================================================================================================================================
//                                                              GRAINSEQUENCE.JS
//============================================================================================================================================
// 📦 Packed material columns, conservative surface-water transport, cement loss and explicit occupied-grain boundary extraction.

import {ReadGrainSpecification,MineralPresets,PackingProperties} from './GrainSpecification.js';
import {Subtract,Cross,Dot,Normalize,Length,MeshMetrics,Face,TriangulateBody} from './PolyhedronSolver.js';

const Clamp=Amount=>Math.max(0,Math.min(1,Amount));
const Key=Point=>Point.map(Coordinate=>Math.round(Coordinate*1e9)).join('/');
const EdgeKey=(First,Second)=>[Key(First),Key(Second)].sort().join('|');
const Area=Loop=>Loop.reduce((Sum,Point,Index)=>
    Sum+Point[0]*Loop[(Index+1)%Loop.length][1]-Point[1]*Loop[(Index+1)%Loop.length][0],0)*.5;
function ClipPolygon(Loop,Direction,Offset)
{
    const Result=[];
    for (let Index=0;Index<Loop.length;++Index)
    {
        const First=Loop[Index],Second=Loop[(Index+1)%Loop.length];
        const A=Dot(First,Direction)-Offset,B=Dot(Second,Direction)-Offset;
        if (A<=0) Result.push(First);
        if ((A<0&&B>0)||(A>0&&B<0)) Result.push(First.map((Coordinate,Axis)=>Coordinate+(Second[Axis]-Coordinate)*A/(A-B)));
    }
    return Result;
}

export function CaptureGrainSource(Triangle,BodyName,Stage,TriangleIndex=null)
{
    if (!Array.isArray(Triangle)||Triangle.length!==3||Triangle.some(Point=>!Array.isArray(Point)||Point.length!==3||!Point.every(Number.isFinite)))
        throw new Error('Source requires three finite triangle coordinates');
    const U=Normalize(Subtract(Triangle[1],Triangle[0]));
    const Normal=Normalize(Cross(Subtract(Triangle[1],Triangle[0]),Subtract(Triangle[2],Triangle[0])));
    if (Length(Cross(Subtract(Triangle[1],Triangle[0]),Subtract(Triangle[2],Triangle[0])))<1e-8) throw new Error('Source face is too small');
    // 📝 Orient the displayed patch with projected world-up, retaining the exact source triangle attachment.
    let V=Subtract([0,1,0],Normal.map(Component=>Component*Normal[1]));
    if (Length(V)<.01) V=Normalize(Cross(Normal,U)); else V=Normalize(V);
    const Along=Normalize(Cross(V,Normal));
    const Origin=[0,1,2].map(Axis=>Triangle.reduce((Sum,Point)=>Sum+Point[Axis],0)/3);
    const Clearance=Math.min(...Triangle.map((Point,Index)=>Length(Cross(Subtract(Origin,Point),Subtract(Triangle[(Index+1)%3],Point)))/Length(Subtract(Triangle[(Index+1)%3],Point))));
    return {Triangle:Triangle.map(Point=>Point.slice()),BodyName:String(BodyName).slice(0,100),Stage,TriangleIndex:Number.isInteger(TriangleIndex)&&TriangleIndex>=0?TriangleIndex:null,Origin,U:Along,V,Normal,
        MaximumSize:Clearance*Math.SQRT2*.94};
}

function Barycentric(Point,Triangle)
{
    const A=Subtract(Triangle[1],Triangle[0]),B=Subtract(Triangle[2],Triangle[0]),P=Subtract(Point,Triangle[0]);
    const AA=Dot(A,A),AB=Dot(A,B),BB=Dot(B,B),AP=Dot(A,P),BP=Dot(B,P),Denominator=AA*BB-AB*AB;
    const Second=(BB*AP-AB*BP)/Denominator,Third=(AA*BP-AB*AP)/Denominator;
    return [1-Second-Third,Second,Third];
}

export class GrainSequence
{
    constructor(Input={},Source=null)
    {
        this.Specification=ReadGrainSpecification(Input);
        this.InitialSpecification={...this.Specification};
        this.Source=Source?CaptureGrainSource(Source.Triangle,Source.BodyName,Source.Stage,Source.TriangleIndex):null;
        if (this.Source&&this.Source.MaximumSize<.02) throw new Error('Choose a larger cliff face for the material patch');
        this.Size=Math.min(this.Specification.Size,this.Source?.MaximumSize??Infinity);
        this.Pitch=this.Size/this.Specification.Resolution;
        this.Depth=this.Pitch*.24;
        this.Crown=this.Pitch*.008;
        this.Cycle=0;this.Removed=0;this.Events=[];
        this.WaterLedger={Rain:0,Evaporation:0,Drainage:0};
        this.TracerLedger={Released:0,Exported:0,Deposited:0};
        let Sequence=this.Specification.Seed>>>0;
        const Pick=()=>{Sequence=(Math.imul(1664525,Sequence)+1013904223)>>>0;return Sequence/4294967296;};
        const Sites=[];
        const Count=this.Specification.Resolution;
        // 📝 Hard-core point packing gives irregular grain sizes without a noise field or a displaced surface.
        const Separation=this.Pitch*.62;
        const Buckets=new Map();
        for (let Attempt=0;Sites.length<Count*Count&&Attempt<Count*Count*100;++Attempt)
        {
            const Point=[0,1].map(()=>((.12+Pick()*(Count-.24))/Count-.5)*this.Size);
            const Coordinate=Point.map(Component=>Math.floor(Component/Separation));
            let Clear=true;
            for (let X=-1;X<=1;++X) for (let Y=-1;Y<=1;++Y)
                for (const Other of Buckets.get(`${Coordinate[0]+X}/${Coordinate[1]+Y}`)||[])
                    if (Length(Subtract(Point,Other))<Separation) Clear=false;
            if (!Clear) continue;
            Sites.push(Point);
            const Address=Coordinate.join('/');
            if (!Buckets.has(Address)) Buckets.set(Address,[]);
            Buckets.get(Address).push(Point);
        }
        if (Sites.length!==Count*Count) throw new Error('Grain packing did not reach the requested density');
        const Half=this.Size*.5,Interned=new Map();
        const Intern=Point=>
        {
            const Code=Key(Point);
            if (!Interned.has(Code)) Interned.set(Code,Point.map(Coordinate=>Math.round(Coordinate*1e9)/1e9));
            return Interned.get(Code);
        };
        const Preset=MineralPresets[this.Specification.Preset];
        this.Columns=Sites.map((Site,Index)=>
        {
            let Loop=[[-Half,-Half],[Half,-Half],[Half,Half],[-Half,Half]];
            const Candidates=Sites.map((Point,Other)=>({Point,Other,Distance:Dot(Subtract(Point,Site),Subtract(Point,Site))}))
                .filter(Candidate=>Candidate.Other!==Index).sort((First,Second)=>First.Distance-Second.Distance);
            for (const Candidate of Candidates)
            {
                const Radius=Math.max(...Loop.map(Point=>Dot(Subtract(Point,Site),Subtract(Point,Site))));
                if (Candidate.Distance>4*Radius) break;
                const Direction=Subtract(Candidate.Point,Site);
                Loop=ClipPolygon(Loop,Direction,(Dot(Candidate.Point,Candidate.Point)-Dot(Site,Site))*.5);
            }
            Loop=Loop.map(Intern);
            if (Loop.length<3||Area(Loop)<=0) throw new Error('Invalid packed grain cell');
            const Centre=[0,1].map(Axis=>Loop.reduce((Sum,Point)=>Sum+Point[Axis],0)/Loop.length);
            const Weak=Math.max(Math.exp(-(((Centre[0]+Centre[1]*.18)/(.065*this.Size))**2)),
                Math.exp(-(((Centre[1]+this.Size*.12)/(.055*this.Size))**2))) * this.Specification.WeakBand;
            const Contact=Loop.map(()=>-1);
            const CellArea=Area(Loop);
            const Grains=Array.from({length:this.Specification.Layers},(_,Layer)=>
            {
                let Choice=Pick(),Mineral=0;
                while (Mineral<3&&Choice>Preset.Fractions[Mineral]) Choice-=Preset.Fractions[Mineral++];
                const Lamination=this.Specification.Preset==='Laminated' ? (Math.floor((Centre[1]/this.Size+.5)*8)%2?.55:1) : 1;
                const Bond=this.Specification.BondStrength*Preset.Bond*(1-Weak*.7)*Lamination*(.9+Pick()*.1);
                return {Layer,Mineral,Bond,InitialBond:Bond,Water:0,Solute:0,Oxide:0,Alive:true,
                    Capacity:CellArea*this.Depth*(.12+.1*Mineral),Reactivity:Preset.Reactivity[Mineral],Iron:Preset.Iron[Mineral]};
            });
            const World=this.Source?this.Source.Origin.map((Coordinate,Axis)=>Coordinate+Centre[0]*this.Source.U[Axis]+Centre[1]*this.Source.V[Axis]):null;
            return {Index,Loop,Centre,Area:CellArea,Contact,Grains,Remaining:Grains.length,Weak,Deposit:0,
                Attachment:World?{BodyName:this.Source.BodyName,Stage:this.Source.Stage,TriangleIndex:this.Source.TriangleIndex,Barycentric:Barycentric(World,this.Source.Triangle)}:null};
        });
        const Edges=new Map();
        for (const Column of this.Columns) Column.Loop.forEach((Point,Side)=>
        {
            const Code=EdgeKey(Point,Column.Loop[(Side+1)%Column.Loop.length]);
            if (Edges.has(Code))
            {
                const [Other,OtherSide]=Edges.get(Code);
                Column.Contact[Side]=Other.Index;Other.Contact[OtherSide]=Column.Index;
                Edges.delete(Code);
            }
            else Edges.set(Code,[Column,Side]);
        });
        for (const [Column,Side] of Edges.values())
        {
            const A=Column.Loop[Side],B=Column.Loop[(Side+1)%Column.Loop.length];
            if (![0,1].some(Axis=>Math.abs(Math.abs(A[Axis])-Half)<1e-8&&Math.abs(A[Axis]-B[Axis])<1e-8))
                throw new Error('Unmatched internal grain contact');
        }
        this.InitialVolume=this.Volume();
    }

    Surface(Column) {return Column.Grains[Column.Remaining-1]??null;}

    Volume()
    {
        // 📝 Identical fixed exposed caps cancel between successive occupied layers; the backing is never eroded.
        return this.Columns.reduce((Sum,Column)=>Sum+Column.Area*((Column.Remaining+1)*this.Depth+
            (Column.Remaining?this.Crown*(1+.72+.72*.72)/3:0)),0);
    }

    Step(Input=this.Specification)
    {
        const Specification=ReadGrainSpecification(Input);
        if (PackingProperties.some(Name=>Specification[Name]!==this.InitialSpecification[Name])) throw new Error('Rebuild grains before changing packing parameters');
        if (JSON.stringify(Specification)!==JSON.stringify(this.Specification)) this.Events.push({Cycle:this.Cycle,Specification:{...Specification}});
        this.Specification=Specification;
        if (this.Cycle>=2000) throw new Error('Study limit reached: reset before continuing');
        const {Rain,Drying,Runoff,Solvent,Oxidation}=Specification;
        const Exposure=this.Source?.Normal? .3+.7*Math.max(0,this.Source.Normal[1]):.5;
        for (const Column of this.Columns)
        {
            const Grain=this.Surface(Column);
            if (!Grain) continue;
            const Incoming=Rain*Exposure*Grain.Capacity*.22;
            const Absorbed=Math.min(Incoming,Grain.Capacity-Grain.Water);
            Grain.Water+=Absorbed;this.WaterLedger.Rain+=Incoming;this.WaterLedger.Drainage+=Incoming-Absorbed;
        }
        // 📝 Each contact exchanges bounded water and dissolved tracer. Receiver capacity and donor content are respected.
        for (const Column of this.Columns)
        {
            const Grain=this.Surface(Column);
            if (!Grain) continue;
            for (const Index of Column.Contact)
            {
                if (Index<=Column.Index) continue;
                const Other=this.Columns[Index],Adjacent=this.Surface(Other);
                if (!Adjacent) continue;
                const Down=(Column.Centre[1]-Other.Centre[1])/this.Pitch*(this.Source?Math.sqrt(Math.max(0,1-this.Source.Normal[1]**2)):1);
                const Driving=Grain.Water/Grain.Capacity-Adjacent.Water/Adjacent.Capacity+Down*.45;
                const [Donor,Receiver]=Driving>=0?[Grain,Adjacent]:[Adjacent,Grain];
                const Transfer=Math.min(Donor.Water,Receiver.Capacity-Receiver.Water,
                    Math.abs(Driving)*Runoff*.12*Math.min(Donor.Capacity,Receiver.Capacity));
                const Tracer=Donor.Water>0?Donor.Solute*Transfer/Donor.Water:0;
                Donor.Water-=Transfer;Receiver.Water+=Transfer;Donor.Solute-=Tracer;Receiver.Solute+=Tracer;
            }
        }
        for (const Column of this.Columns)
        {
            const Grain=this.Surface(Column);
            if (!Grain) continue;
            const Saturation=Grain.Water/Grain.Capacity;
            const Exposed=Column.Contact.filter(Index=>Index<0||this.Columns[Index].Remaining<Column.Remaining).length;
            const Loss=Solvent*Saturation*.016*Grain.Reactivity*(1+Exposed*.16);
            Grain.Bond=Math.max(0,Grain.Bond-Loss);
            Grain.Oxide=Clamp(Grain.Oxide+Oxidation*Grain.Iron*Saturation*(1-Saturation*.55)*.045*(1-Grain.Oxide));
            const Released=Loss*Grain.Iron*Grain.Capacity*.12;
            Grain.Solute+=Released;this.TracerLedger.Released+=Released;
            const Evaporated=Grain.Water*Drying*.16;
            Grain.Water-=Evaporated;this.WaterLedger.Evaporation+=Evaporated;
            const Deposited=Grain.Solute*Drying*.12;
            Grain.Solute-=Deposited;Column.Deposit+=Deposited;this.TracerLedger.Deposited+=Deposited;
            if (Column.Contact.includes(-1))
            {
                const Fraction=Runoff*.08;
                this.WaterLedger.Drainage+=Grain.Water*Fraction;this.TracerLedger.Exported+=Grain.Solute*Fraction;
                Grain.Water*=1-Fraction;Grain.Solute*=1-Fraction;
            }
            if (Grain.Bond<Grain.InitialBond*(.25+Math.min(3,Exposed)*.035))
            {
                // 📝 Only an exposed grain can detach; buried grains and the permanent backing cannot float or disappear.
                this.WaterLedger.Drainage+=Grain.Water;this.TracerLedger.Exported+=Grain.Solute;
                Grain.Water=0;Grain.Solute=0;Grain.Alive=false;
                --Column.Remaining;++this.Removed;
            }
        }
        ++this.Cycle;
        return this.Measure();
    }

    Measure()
    {
        const Exposed=this.Columns.map(Column=>this.Surface(Column)).filter(Boolean);
        const Water=Exposed.reduce((Sum,Grain)=>Sum+Grain.Water,0),Solute=Exposed.reduce((Sum,Grain)=>Sum+Grain.Solute,0);
        return {Cycle:this.Cycle,Grains:this.Columns.length*this.Specification.Layers,Exposed:Exposed.length,Removed:this.Removed,
            Moisture:Exposed.reduce((Sum,Grain)=>Sum+Grain.Water/Grain.Capacity,0)/Math.max(1,Exposed.length),
            Oxidation:Exposed.reduce((Sum,Grain)=>Sum+Grain.Oxide,0)/Math.max(1,Exposed.length),
            Volume:this.Volume(),RemovedVolume:this.InitialVolume-this.Volume(),Water,
            WaterResidual:this.WaterLedger.Rain-this.WaterLedger.Evaporation-this.WaterLedger.Drainage-Water,
            TracerResidual:this.TracerLedger.Released-this.TracerLedger.Exported-this.TracerLedger.Deposited-Solute,
            ...this.WaterLedger,Staining:this.TracerLedger.Deposited};
    }

    Boundary()
    {
        const Faces=[];
        const Emit=(Points,Owner,Layer)=>Faces.push(Face(Points.map(Point=>Point.map(Coordinate=>Coordinate/this.Pitch)),`${Owner}:${Layer}`));
        for (const Column of this.Columns)
        {
            const {Loop,Centre,Remaining}=Column,Top=(Remaining-this.Specification.Layers)*this.Depth;
            const Bottom=-(this.Specification.Layers+1)*this.Depth;
            const Inner=Loop.map(Point=>Point.map((Coordinate,Axis)=>Centre[Axis]+(Coordinate-Centre[Axis])*.72));
            const Rise=Remaining?this.Crown:0;
            for (let Side=0;Side<Loop.length;++Side)
            {
                const Next=(Side+1)%Loop.length,A=Loop[Side],B=Loop[Next],C=Inner[Side],D=Inner[Next];
                if (Side===0)
                {
                    Emit(Inner.map(Point=>[...Point,Top+Rise]),Column.Index,Remaining-1);
                    Emit(Loop.slice().reverse().map(Point=>[...Point,Bottom]),Column.Index,-1);
                }
                Emit([[...A,Top],[...B,Top],[...D,Top+Rise],[...C,Top+Rise]],Column.Index,Remaining-1);
                const Other=Column.Contact[Side],Lower=Other<0?-1:this.Columns[Other].Remaining;
                for (let Level=Lower;Level<Remaining;++Level)
                {
                    const Low=(Level-this.Specification.Layers)*this.Depth,High=Low+this.Depth;
                    Emit([[...A,Low],[...B,Low],[...B,High],[...A,High]],Column.Index,Level);
                }
            }
        }
        const Result=TriangulateBody({Name:'Grain material patch',Faces,Spalls:[],Cracks:[]},.6);
        // 📝 Conforming face projections can straddle a rounded coordinate key; weld only numerical roundoff in grain units.
        const Welded=[],Buckets=new Map();
        const Remap=Result.Vertices.map(Point=>
        {
            const Coordinate=Point.map(Component=>Math.floor(Component/1e-7));
            for (let X=-1;X<=1;++X) for (let Y=-1;Y<=1;++Y) for (let Z=-1;Z<=1;++Z)
                for (const Index of Buckets.get([Coordinate[0]+X,Coordinate[1]+Y,Coordinate[2]+Z].join('/'))||[])
                    if (Length(Subtract(Point,Welded[Index]))<1e-8) return Index;
            const Address=Coordinate.join('/'),Index=Welded.length;
            if (!Buckets.has(Address)) Buckets.set(Address,[]);
            Buckets.get(Address).push(Index);Welded.push(Point);return Index;
        });
        Result.Triangles=Result.Triangles.map(Triangle=>Triangle.map(Index=>Remap[Index]));
        Result.Vertices=Welded.map(Point=>Point.map(Coordinate=>Coordinate*this.Pitch));
        Result.Owners=Result.Tags.map(Tag=>Number(Tag.split(':')[0]));
        Result.Layers=Result.Tags.map(Tag=>Number(Tag.split(':')[1]));
        // 📝 Degeneracy uses the same grain units as meshing, not the metre-scale cliff cutoff. Angles are scale invariant.
        Result.Metrics=MeshMetrics({...Result,Vertices:Welded});
        Result.Metrics.Volume*=this.Pitch**3;
        Result.Metrics.AreaTolerance=1e-10*this.Pitch**2;
        Result.Metrics.MinimumArea=Result.Triangles.reduce((Minimum,Triangle)=>
        {
            const [A,B,C]=Triangle.map(Index=>Result.Vertices[Index]);
            return Math.min(Minimum,Length(Cross(Subtract(B,A),Subtract(C,A)))*.5);
        },Infinity);
        if (Result.Metrics.OpenEdges+Result.Metrics.NonmanifoldEdges+Result.Metrics.NonmanifoldVertices+
            Result.Metrics.WindingErrors+Result.Metrics.ZeroArea+Result.Metrics.DuplicateTriangles) throw new Error('Grain boundary topology rejected. Try a larger source face or lower grain resolution.');
        return Result;
    }

    Recipe() {return {Format:'Frontier.GrainWeathering',Version:1,Specification:this.InitialSpecification,Source:this.Source,Cycles:this.Cycle,Events:this.Events};}
}

export function RestoreGrainRecipe(Recipe)
{
    if (Recipe?.Format!=='Frontier.GrainWeathering'||Recipe.Version!==1||!Number.isInteger(Recipe.Cycles)||Recipe.Cycles<0||Recipe.Cycles>2000)
        throw new Error('Unsupported or oversized grain study');
    const Sequence=new GrainSequence(Recipe.Specification,Recipe.Source);
    const Events=Recipe.Events??[];
    if (!Array.isArray(Events)||Events.length>2000||Events.some((Event,Index)=>!Number.isInteger(Event.Cycle)||Event.Cycle<0||
        Event.Cycle>=Recipe.Cycles||(Index&&Event.Cycle<=Events[Index-1].Cycle))) throw new Error('Invalid weathering event sequence');
    let Index=0,Specification=Sequence.Specification;
    for (let Cycle=0;Cycle<Recipe.Cycles;++Cycle)
    {
        if (Events[Index]?.Cycle===Cycle) Specification=ReadGrainSpecification(Events[Index++].Specification);
        Sequence.Step(Specification);
    }
    return Sequence;
}
