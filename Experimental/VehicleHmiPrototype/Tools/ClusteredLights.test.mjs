import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {ClusteredLights,BuildLightClusters,LightDefaults,LocalLightingFloats} from '../Source/ClusteredLights.js';
import {ParseIES} from '../Source/Photometry.js';
import {ConstructRoofLightFacets,ConstructStreetHeadFacets} from '../Source/LightingFixtures.js';
import {CourseMaterial} from '../Source/MaterialSpecification.js';
import {ConstructRigidTransform} from '../Source/TransformProjection.js';
const Vehicle={Hidden:false,QueryBodyTransform:()=>ConstructRigidTransform([10,20,0.53],[0,0,0,1])};
export const TestIES='IESNA:LM-63-2002\n[LUMINAIRE] Synthetic test distribution, not measured\nTILT=NONE\n1 1000 1 4 1 1 2 0.2 0.2 0.1\n1 1 12\n0 30 60 90\n0\n100 80 40 0';
test('LM-63 Type C parser handles axisymmetry, interpolation and rejects unsupported/invalid files',()=>{
    const P=ParseIES(TestIES);assert.equal(P.PeakCandela,100);assert.equal(P.Table.length,256);
    assert.equal(P.Sample(0,0),100);assert.equal(P.Sample(270,45),60);assert.equal(P.Sample(30,120),0);
    assert.throws(()=>ParseIES(TestIES.replace('TILT=NONE','TILT=INCLUDE')),/TILT/);
    assert.throws(()=>ParseIES(TestIES.replace('4 1 1 2','4 1 2 2')),/Type C/);
    assert.throws(()=>ParseIES(TestIES.replace('100 80 40 0','100 -80 40 0')),/nonnegative/);
    assert.throws(()=>ParseIES(TestIES.replace('100 80 40 0','100')),/Incomplete/);
});
test('cluster masks include every sphere overlapping a cell and preserve slot 31 bits',()=>{
    const L=Array.from({length:32},(_,I)=>({Position:[8,8,4],Reach:I===31?5:0}));
    const M=BuildLightClusters(L,[0,0,0]);assert.equal(M[0],0x80000000);assert.equal(M[4*7],0);
    const All=BuildLightClusters(L.map(A=>({...A,Reach:1000})),[0,0,0]);assert.equal(All[0],0xffffffff);
});
test('all automotive modes follow the car, are bounded, and off/disabled remove lights',()=>{
    const System=new ClusteredLights();
    for(const Mode of ['Low','High','Fog']) {
        const Data=System.Build(Vehicle,{...LightDefaults,LightMode:Mode},[0,0,2]);
        assert.equal(Data.length,LocalLightingFloats);assert.equal(System.Lights.length,32);
        const A=System.Lights[0];assert.ok(A.Position[0]>10);assert.ok(A.Position[1]>19);
        assert.ok(A.Direction[0]>0.9);assert.ok(A.Reach<=110);assert.ok(A.Colour.every(Number.isFinite));
        assert.ok(System.Metrics.Clusters>0);
    }
    System.Build(Vehicle,{...LightDefaults,LocalLights:false},[0,0,2]);assert.equal(System.Lights.length,0);
    System.Build(Vehicle,{...LightDefaults,StreetLights:false},[0,0,2]);assert.equal(System.Lights.length,0);
    System.Build({...Vehicle,Hidden:true},{...LightDefaults,StreetLights:false,LightMode:'High'},[0,0,2]);assert.equal(System.Lights.length,0);
    System.IES=ParseIES(TestIES);
    System.Build(Vehicle,{...LightDefaults,RoofProfile:'IES',LightMode:'Low'},[0,0,2]);assert.equal(System.Lights[0].Type,6);
});
test('fixture meshes distinguish housing, reflectors, source faces and stay attached',()=>{
    const R=ConstructRoofLightFacets(),S=ConstructStreetHeadFacets();
    for(const F of [CourseMaterial.LightHousing,CourseMaterial.LightReflector,CourseMaterial.RoofLed])assert.ok(R.some(T=>T.Family===F));
    assert.equal(R.filter(T=>T.Family===CourseMaterial.RoofLed).length,8*12);
    assert.ok(S.some(T=>T.Family===CourseMaterial.StreetLed));assert.ok(S.some(T=>T.Family===CourseMaterial.LightHousing));
});
test('cluster lighting is shared by screen/cards and GI uses live material emission',()=>{
    for(const Shader of ['DirectRadiance','CardDirect']) {
        const S=fs.readFileSync(new URL(`../Shader/${Shader}.wgsl`,import.meta.url),'utf8');
        assert.match(S,/LocalLightMask\(Position\)/);assert.match(S,/ResolveShadowedLocalArrival/);
    }
    const Final=fs.readFileSync(new URL('../Shader/CardFinal.wgsl',import.meta.url),'utf8');
    assert.match(Final,/LiveMaterials\[Family\]/);assert.match(Final,/Material.Emission/);
});
