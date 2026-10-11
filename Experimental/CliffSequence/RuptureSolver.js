//============================================================================================================================================
//                                                             RUPTURESOLVER.JS
//============================================================================================================================================
// 📦 Sequential finite fracture partitions with persistent orientation families and rough, matching polygon cut surfaces.

import {ClipCells,Dot,Scale,Subtract,Normalize,Cross} from './PolyhedronSolver.js';
import {SampleRelief} from './ReliefProjection.js';

function MeasureCells(Cells)
{
    let Volume=0;
    const Moment=[0,0,0];
    for (const Cell of Cells) for (const Face of Cell)
        for (let Index=1;Index<Face.Loop.length-1;++Index)
        {
            const [First,Second,Third]=[Face.Loop[0],Face.Loop[Index],Face.Loop[Index+1]];
            const Signed=Dot(First,Cross(Second,Third))/6;
            Volume+=Signed;
            for (let Axis=0;Axis<3;++Axis) Moment[Axis]+=(First[Axis]+Second[Axis]+Third[Axis])*Signed*.25;
        }
    return {Volume:Math.abs(Volume),Centre:Moment.map(Component=>Component/Volume)};
}

function ClipRupture(Cells,Surface,Side,Aperture)
{
    const {Knots,Direction,Along,Offset}=Surface;
    const Result=[];
    for (let Segment=0;Segment<Knots.length-1;++Segment)
    {
        const [Start,StartRelief]=Knots[Segment], [End,EndRelief]=Knots[Segment+1];
        let Parts=Cells;
        if (Segment>0) Parts=ClipCells(Parts,Scale(Along,-1),-Start,'Construction');
        if (Segment<Knots.length-2) Parts=ClipCells(Parts,Along,End,'Construction');
        const Slope=(EndRelief-StartRelief)/(End-Start);
        const Normal=Subtract(Direction,Scale(Along,Slope));
        const Constant=Offset+StartRelief-Slope*Start;
        Result.push(...ClipCells(Parts,Scale(Normal,Side),Side*Constant-Aperture*.5,'Fracture'));
    }
    return Result;
}

export function FractureMass(Cells,Specification,Select)
{
    const {FractureStyle,Beds,FractureSeed,FractureBend,Aperture,Dip}=Specification;
    const Families=FractureStyle==='Conjugate'?[[.866,.5,.12],[.866,-.5,-.12],[.08,.18,1]]:
        FractureStyle==='Orthogonal'?[[1,.08,.14],[-.14,.08,1],[.1,1,-.05]]:
        [[1,.08,.14],[.18,.10,1]];
    const Angle=Dip*Math.PI/180;
    const Parts=[{Cells,...MeasureCells(Cells)}];
    for (let Cut=0;Cut<Beds;++Cut)
    {
        Parts.sort((First,Second)=>Second.Volume-First.Volume);
        const Part=Parts[0];
        const Family=Families[Cut%Families.length];
        const Direction=Normalize([Family[0]*Math.cos(Angle)-Family[1]*Math.sin(Angle),
            Family[0]*Math.sin(Angle)+Family[1]*Math.cos(Angle),Family[2]]);
        const Reference=Math.abs(Direction[1])<.85?[0,1,0]:[1,0,0];
        const Along=Normalize(Subtract(Reference,Scale(Direction,Dot(Direction,Reference))));
        const Points=Part.Cells.flatMap(Cell=>Cell.flatMap(Polygon=>Polygon.Loop));
        const Coordinates=Points.map(Point=>Dot(Point,Along));
        const Minimum=Math.min(...Coordinates),Maximum=Math.max(...Coordinates),Span=Maximum-Minimum;
        const NormalCoordinates=Points.map(Point=>Dot(Point,Direction));
        const Thickness=Math.max(...NormalCoordinates)-Math.min(...NormalCoordinates);
        const Amplitude=Math.min(Span*.06,Thickness*.10,1.05)*FractureBend;
        const Knots=Array.from({length:6},(_,Index)=>[Minimum+Span*Index/5,
            SampleRelief(Index*.79+.13,Cut*.31+.57,FractureSeed+Cut*181,'Gradient')*Amplitude]);
        const Offset=Dot(Part.Centre,Direction)+(Select(101)/100-.5)*Thickness*.18;
        const Surface={Knots,Direction,Along,Offset};
        const Divided=[-1,1].map(Side=>
        {
            const Pieces=ClipRupture(Part.Cells,Surface,Side,Aperture);
            return {Cells:Pieces,...MeasureCells(Pieces)};
        });
        // 📝 A fracture that only shaves off a tiny tip is not a usable primary partition.
        if (Divided.some(Piece=>!Number.isFinite(Piece.Volume)||Piece.Volume<Part.Volume*.035)) continue;
        Parts.splice(0,1,...Divided);
    }
    return Parts.map((Part,Index)=>({Cells:Part.Cells,Name:`Fracture ${String(Index+1).padStart(2,'0')}`,
        Layer:Index,Middle:Part.Centre[1]}));
}
