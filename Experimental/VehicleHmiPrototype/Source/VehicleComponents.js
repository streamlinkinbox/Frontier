import {CourseMaterial as M} from './MaterialSpecification.js';
export const VehicleWheelNames=['Front left','Front right','Rear left','Rear right'];
export const VehicleSystemNames=[['overview','Overview'],['chassis','Chassis & collision'],['engine','Engine'],['tyres','Tyres'],['suspension','Suspension'],['lights','Lights'],['hmi','HMI & cabin'],['mirrors','Mirrors']];
export const TyreComponentNames=[['tyre','Tyre & rim'],['marks','Tyre marks'],['smoke','Smoke & fire'],['sparks','Rim sparks'],['burnout','Burnout']];
export const VehicleInspectorAliases={cabin:['hmi','tyre'],marks:['tyres','marks'],gas:['tyres','smoke']};
export function VehicleInspectorState(H){return H.VehicleInspector??=( {System:'overview',TyreComponent:'tyre',Wheel:0,ShowCollision:false} );}
export function BodyComponentFamily(F){if([M.Headlight,M.Taillight].includes(F))return 'lights';if([M.MirrorGlass,M.MirrorTrim].includes(F))return 'mirrors';if([M.DisplayGlass,M.TabletHousing,M.SteeringWheel].includes(F))return 'hmi';if([M.CabinLeather,M.CabinTrim,M.CabinAccent,M.SeatFabric,M.CabinCarpet].includes(F))return 'cabin';return 'chassis';}
// Ownership groups reference original facet ranges. No copied vertices, transforms or draw occupants.
export function ConstructVehicleComponents(Scene,TyreCount,RimCount){
 const Body=Scene.VehicleOccupants.Body,BodySet=Scene.Occupants[Body].FacetSet,Groups={};
 Scene.FacetSets[BodySet].Facets.forEach((F,I)=>(Groups[BodyComponentFamily(F.Family)]??=[]).push(I));
 const BodyParts=Object.entries(Groups).map(([Id,FacetIndices])=>({Id,Kind:'mesh group',Mesh:{Occupant:Body,FacetSet:BodySet,FacetIndices}}));
 const Chassis=BodyParts.find(N=>N.Id==='chassis');if(Chassis)Chassis.Children=[{Id:'chassis-sparks',Kind:'body scrape sparks',Service:'Sparks',SharedRenderer:true}];
 const Wheels=Scene.VehicleOccupants.Wheels.map((Occupant,I)=>({Id:'wheel-'+I,Name:VehicleWheelNames[I],Wheel:I,Kind:'wheel assembly',Children:[
  {Id:`wheel-${I}-tyre`,Kind:'mesh range',Mesh:{Occupant,FacetSet:Scene.Occupants[Occupant].FacetSet,FirstFacet:0,FacetCount:TyreCount},Children:[{Id:`wheel-${I}-marks`,Kind:'rubber contact deposit',Service:'Marks'},{Id:`wheel-${I}-smoke`,Kind:'thermal smoke / combustion source',Service:'Smoke',SharedDomain:true}]},
  {Id:`wheel-${I}-rim`,Kind:'mesh range',Mesh:{Occupant,FacetSet:Scene.Occupants[Occupant].FacetSet,FirstFacet:TyreCount,FacetCount:RimCount},Children:[{Id:`wheel-${I}-sparks`,Kind:'rim contact sparks',Service:'Sparks',SharedRenderer:true}]}
 ]}));
 return {Id:'vehicle',Kind:'assembly',Children:[...BodyParts,{Id:'engine',Kind:'solver component',DedicatedMesh:false},{Id:'suspension',Kind:'four solver struts',DedicatedMesh:false},{Id:'wheels',Kind:'assembly group',Children:Wheels}],Wheels,BodyParts};
}
export function ResolveVehicleComponentHit(Scene,Occupant,FacetIndex,Family){const V=Scene.VehicleComponents;if(!V)return null;const Wheel=V.Wheels.find(W=>W.Children[0].Mesh.Occupant===Occupant);if(Wheel)return Wheel.Children.find(N=>FacetIndex>=N.Mesh.FirstFacet&&FacetIndex<N.Mesh.FirstFacet+N.Mesh.FacetCount)?.Id??null;return Occupant===Scene.VehicleOccupants.Body?BodyComponentFamily(Family):null;}
