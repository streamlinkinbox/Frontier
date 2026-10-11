//============================================================================================================================================
//                                                           SCENESPECIFICATION.JS
//============================================================================================================================================
// 📦 Shared triangle coordinates for the raster scene, exact ray intersections and dynamic receiver charts.

export const VolumeMinimum=[-6,0,-6];
export const VolumeExtent=[12,6,12];
export const SceneDefaults={EmitterX:-2.5,Power:18,OccluderX:1.1,Shear:.65,Iterations:4,Visibility:true};
export const ChartColumns=8;
export const ChartSpan=18;
export const ChartInterior=16;

export function ConstructScene(Input=SceneDefaults)
{
    const Settings={...SceneDefaults,...Input};
    const Definitions=[
        ['Floor',[0,-.2,0],[12,.4,12],[.68,.67,.62],0,0],
        ['Back wall',[0,3,-5.95],[12,6,.3],[.65,.64,.60],0,0],
        ['Red wall',[-5.95,3,0],[.3,6,12],[.65,.065,.035],0,0],
        ['Cyan wall',[5.95,3,0],[.3,6,12],[.035,.44,.53],0,0],
        ['Low plinth',[-2.2,.7,1.3],[2.3,1.4,2.3],[.72,.7,.63],.3,0],
        ['Deforming occluder',[Settings.OccluderX,1.5,-1.1],[1.8,3,2.1],[.58,.62,.66],-.3,Settings.Shear],
        ['Area emitter',[Settings.EmitterX,5.25,-.6],[2.3,.12,2.3],[0,0,0],0,0,[Settings.Power,Settings.Power*.83,Settings.Power*.62]]
    ];
    const Corners=[[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1],[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]];
    const Quads=[[1,0,3,2],[4,5,6,7],[0,4,7,3],[5,1,2,6],[0,1,5,4],[7,6,2,3]];
    const Solids=[],Triangles=[],Charts=[],Positions=[],Coordinates=[];
    const ChartRows=Math.ceil(Definitions.length*6/ChartColumns);
    const ChartWidth=ChartColumns*ChartSpan, ChartHeight=ChartRows*ChartSpan;
    for (const [Name,Translation,Size,Reflectance,Rotation,Shear,Emission=[0,0,0]] of Definitions)
    {
        const Points=Corners.map(Corner=>
        {
            const Local=Corner.map((Coordinate,Axis)=>Coordinate*Size[Axis]*.5);
            Local[0]+=Shear*(Corner[1]+1)*.5;
            return [Local[0]*Math.cos(Rotation)+Local[2]*Math.sin(Rotation)+Translation[0],Local[1]+Translation[1],
                -Local[0]*Math.sin(Rotation)+Local[2]*Math.cos(Rotation)+Translation[2]];
        });
        const Minimum=[0,1,2].map(Axis=>Math.min(...Points.map(Point=>Point[Axis])));
        const Maximum=[0,1,2].map(Axis=>Math.max(...Points.map(Point=>Point[Axis])));
        const FirstTriangle=Triangles.length;
        for (const Quad of Quads)
        {
            const Chart=Charts.length;
            Charts.push({Corners:Quad.map(Index=>Points[Index]),Reflectance,Emission});
            for (const Indices of [[0,1,2],[0,2,3]])
            {
                const Triangle=Indices.map(Index=>Points[Quad[Index]]);
                Triangles.push(Triangle);
                Positions.push(...Triangle.flat());
                for (const Index of Indices)
                {
                    const Along=[0,1,1,0][Index], Across=[0,0,1,1][Index];
                    Coordinates.push(((Chart%ChartColumns)*ChartSpan+1.5+Along*(ChartInterior-1))/ChartWidth,
                        (Math.floor(Chart/ChartColumns)*ChartSpan+1.5+Across*(ChartInterior-1))/ChartHeight);
                }
            }
        }
        Solids.push({Name,Minimum,Maximum,Reflectance,Emission,FirstTriangle,TriangleCount:Triangles.length-FirstTriangle});
    }
    return {Solids,Triangles,Charts,Positions,Coordinates,ChartWidth,ChartHeight,Settings};
}

export function ConstructCascades()
{
    return Array.from({length:4},(_,Level)=>
    {
        const Divisor=2**Level;
        const Dimensions=[16,8,16].map(Count=>Count/Divisor);
        const AngularSpan=4*Divisor;
        const Samples=Dimensions.reduce((Product,Count)=>Product*Count,1)*6*AngularSpan**2;
        return {Level,Dimensions,AngularSpan,Samples,Start:1.25*(Divisor-1),End:1.25*(Divisor*2-1),
            Width:Dimensions[0]*Dimensions[2]*AngularSpan,Height:Dimensions[1]*6*AngularSpan};
    });
}
