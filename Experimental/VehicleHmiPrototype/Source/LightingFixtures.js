// Rigid, body-attached LED flood box and shielded scene luminaires. +X is beam-forward.
import { EmitBoxFacets } from './CourseStructure.js';
import { CourseMaterial as M } from './MaterialSpecification.js';
import { ResolveCoMAuthoredZ } from './VehicleSpecification.js';
export const RoofLightMount = Object.freeze([0.20, 0, 1.49 - ResolveCoMAuthoredZ()]);
export function ConstructRoofLightFacets() {
    const Result = [];
    const Box = (P,S,Family) => Result.push(...EmitBoxFacets(...S,Family).map(F => ({...F,Corners:F.Corners.map(V=>V.map((C,I)=>C+P[I]))})));
    for (const Y of [-0.50,0.50]) Box([-0.02,Y,1.34-ResolveCoMAuthoredZ()],[0.22,0.08,0.15],M.LightHousing);
    Box([0.12,0,RoofLightMount[2]-0.10],[0.40,1.40,0.23],M.LightHousing);
    // Extruded rear heat-sink fins, raised lip and eight recessed reflector/LED cells.
    for (let I=0;I<9;I++) Box([-0.11,-0.62+I*0.155,RoofLightMount[2]-0.075],[0.10,0.025,0.17],M.LightHousing);
    for (let I=0;I<8;I++) {
        const Y=-0.58+I*0.166;
        Box([0.327,Y,RoofLightMount[2]-0.065],[0.026,0.145,0.14],M.LightReflector);
        Box([0.344,Y,RoofLightMount[2]-0.043],[0.018,0.094,0.096],M.RoofLed);
    }
    return Result;
}
export function ConstructStreetHeadFacets() {
    const Result=[];
    const Box=(P,S,F)=>Result.push(...EmitBoxFacets(...S,F).map(T=>({...T,Corners:T.Corners.map(V=>V.map((C,I)=>C+P[I]))})));
    Box([0,0,0.04],[1.8,0.6,0.22],M.LightHousing);
    // LED board faces down; opaque top/side shielding prevents an emissive white box.
    Box([0,0,0.025],[1.56,0.44,0.018],M.LightReflector);
    for(let X=0;X<6;X++) for(let Y=0;Y<2;Y++)
        Box([-0.65+X*0.26,-0.115+Y*0.23,0.014],[0.19,0.16,0.012],M.StreetLed);
    return Result;
}
export function ConstructTunnelHeadFacets() {
    const Result=[];
    const Box=(P,S,F)=>Result.push(...EmitBoxFacets(...S,F).map(T=>({...T,Corners:T.Corners.map(V=>V.map((C,I)=>C+P[I]))})));
    Box([0,0,0.02],[3.0,0.12,0.14],M.LightHousing);
    Box([0,0,0.012],[2.86,0.09,0.01],M.LightReflector);
    for(let I=0;I<14;I++)Box([-1.3+I*0.2,0,0.005],[0.16,0.065,0.009],M.TunnelLed);
    return Result;
}
