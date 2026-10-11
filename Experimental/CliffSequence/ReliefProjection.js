//============================================================================================================================================
//                                                            RELIEFPROJECTION.JS
//============================================================================================================================================
// 📦 Seeded coherent geological relief: gradient, ridged and cellular fields constrained by cliff cross-sections.

import {CliffProfiles,SectionPlans} from './CliffSpecification.js';

function HashCoordinate(Horizontal,Vertical,Seed)
{
    let Bits=Math.imul(Horizontal,374761393)^Math.imul(Vertical,668265263)^Math.imul(Seed,1442695041);
    Bits=Math.imul(Bits^(Bits>>>13),1274126177);
    return ((Bits^(Bits>>>16))>>>0)/4294967296;
}

function GradientNoise(Horizontal,Vertical,Seed)
{
    const Column=Math.floor(Horizontal),Row=Math.floor(Vertical);
    const Along=Horizontal-Column,Across=Vertical-Row;
    const Smooth=Fraction=>Fraction**3*(Fraction*(Fraction*6-15)+10);
    const Blend=(First,Second,Fraction)=>First+(Second-First)*Fraction;
    const Gradient=(OffsetColumn,OffsetRow)=>
    {
        const Angle=HashCoordinate(Column+OffsetColumn,Row+OffsetRow,Seed)*Math.PI*2;
        return Math.cos(Angle)*(Along-OffsetColumn)+Math.sin(Angle)*(Across-OffsetRow);
    };
    return Blend(Blend(Gradient(0,0),Gradient(1,0),Smooth(Along)),
        Blend(Gradient(0,1),Gradient(1,1),Smooth(Along)),Smooth(Across))*1.5;
}

export function SampleRelief(Horizontal,Vertical,Seed,Mode)
{
    if (Mode==='None') return 0;
    if (Mode==='Cellular')
    {
        let First=Infinity,Second=Infinity;
        for (let Column=Math.floor(Horizontal)-1;Column<=Math.floor(Horizontal)+1;++Column)
            for (let Row=Math.floor(Vertical)-1;Row<=Math.floor(Vertical)+1;++Row)
            {
                const Distance=Math.hypot(Horizontal-Column-.2-HashCoordinate(Column,Row,Seed)*.6,
                    Vertical-Row-.2-HashCoordinate(Column,Row,Seed+19)*.6);
                if (Distance<First) {Second=First;First=Distance;}
                else Second=Math.min(Second,Distance);
            }
        return Math.max(-1,Math.min(1,(Second-First)*2.8-.65));
    }
    let Sum=0,Weight=1,Total=0;
    for (let Octave=0;Octave<3;++Octave)
    {
        const Sample=GradientNoise(Horizontal,Vertical,Seed+Octave*1013);
        Sum+=(Mode==='Ridged'?1-Math.abs(Sample)*2.8:Sample)*Weight;
        Total+=Weight;Weight*=.38;Horizontal*=2.1;Vertical*=2.1;
    }
    return Math.max(-1,Math.min(1,Sum/Total));
}

export function ConstructRelief(Specification)
{
    const {Width,Height,Depth,Relief,Retreat,Seed,NoiseMode,NoiseScale}=Specification;
    const Variation=NoiseMode==='None'?0:Specification.Variation;
    const Profile=CliffProfiles[Specification.Profile];
    const Noise=(Position,Elevation,Channel=0)=>SampleRelief(Position*NoiseScale+.371,Elevation*1.6+.613,Seed+Channel*7907,NoiseMode);
    const Stations=Profile.Stations.map(([Position,Crown,Projection],Index)=>
    {
        const Margin=Index && Index<Profile.Stations.length-1?
            Math.min(Position-Profile.Stations[Index-1][0],Profile.Stations[Index+1][0]-Position):0;
        return [Position+Noise(Position,.3,8)*Margin*.42*Variation,
            Math.max(.20,Crown*(1+Noise(Position,0,3)*Variation*.65)),
            Projection*(1-Variation*.65)+Noise(Position,0,1)*Variation*.48];
    });
    const CrownMaximum=Math.max(...Stations.map(Station=>Station[1]));
    if (Variation) Stations.forEach(Station=>{Station[1]/=CrownMaximum;});
    const MinimumProjection=Math.min(...Stations.flatMap(([, ,Projection])=>SectionPlans.flatMap(Plan=>
        Plan.map(Setback=>Projection*Relief+Setback*Retreat/.48))));
    const DepthScale=Math.min(1,.88/Math.max(.01,-MinimumProjection));
    const Taper=Profile.Taper||[1,.97,1.01,.94,.87];
    let Rows,RearRows;
    // 📝 Keep the loft's projected cells orientation-preserving; no inverted cells at tall serrated crowns.
    for (const TaperFraction of [1,.5,.25,0])
    {
        Rows=Stations.map(([Position,Crown,Projection],Station)=>Profile.Sections.map(([Elevation],Level)=>
            [(Position-.5)*Width*(1+(Taper[Level]-1)*TaperFraction),Elevation*Crown*Height,
                (Projection*Relief+SectionPlans[Profile.SectionKinds[Station]][Level]*Retreat/.48)*Depth*DepthScale+
                Noise(Position,Elevation,2)*Variation*Depth*.11]));
        let Positive=true;
        for (let Column=0;Column<Rows.length-1;++Column) for (let Level=0;Level<Rows[0].length-1;++Level)
        {
            const Points=[Rows[Column][Level],Rows[Column+1][Level],Rows[Column+1][Level+1],Rows[Column][Level+1]];
            for (const [First,Second,Third] of [[0,1,2],[0,2,3]])
            {
                const [Origin,Along,Across]=[Points[First],Points[Second],Points[Third]];
                if ((Along[0]-Origin[0])*(Across[1]-Origin[1])-(Along[1]-Origin[1])*(Across[0]-Origin[0])<1e-5) Positive=false;
            }
        }
        if (Positive) break;
    }
    RearRows=Stations.map(([Position],Station)=>Profile.Sections.map(([Elevation],Level)=>
    {
        const Opposite=Stations[Stations.length-1-Station];
        const Setback=SectionPlans[(Profile.SectionKinds[Station]+1)%SectionPlans.length][Level];
        const Front=Rows[Station][Level];
        const Back=-Depth-Opposite[2]*Relief*Depth*.7-Setback*Depth*Retreat*.75+
            Noise(Position,Elevation,5)*Variation*Depth*.22;
        return [Front[0],Front[1],Math.min(Back,Front[2]-Depth*.24)];
    }));
    return {Rows,RearRows,Sections:Profile.Sections,Stations};
}
