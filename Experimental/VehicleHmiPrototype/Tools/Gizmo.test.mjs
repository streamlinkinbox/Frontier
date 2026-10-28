import {test} from 'node:test';import assert from 'node:assert/strict';import {ResolveEditorTransform} from '../Source/EditorTransforms.js';import {ProjectGizmo} from '../Source/TransformGizmo.js';
test('gizmo projection uses Z-up camera, rejects points behind viewer',()=>{const C={Position:[0,0,0],Forward:[1,0,0],Right:[0,1,0],Up:[0,0,1],TanY:1};assert.deepEqual(ProjectGizmo([10,0,0],C,200,100),[100,50,10]);assert.equal(ProjectGizmo([-10,0,0],C,200,100),null);assert.ok(ProjectGizmo([10,0,2],C,200,100)[1]<50);});
test('volume translation and scale mutate the actual renderer settings',()=>{const H={Settings:{LocalCloudX:1,LocalCloudY:2,LocalCloudZ:3,LocalCloudHalfX:10,LocalCloudHalfY:20,LocalCloudHalfZ:30}};const T=ResolveEditorTransform(H,'local-cloud');T.Set([5,6,7]);assert.deepEqual(T.Get(),[5,6,7]);T.SetScale([12,24,36]);assert.deepEqual(T.GetScale(),[12,24,36]);assert.equal(ResolveEditorTransform(H,'render'),null);});
test('cone translation calls the physics world rather than modifying a dummy transform',()=>{let Placed;const Cone={Body:{Position:[1,2,3],Orientation:[0,0,0,1]}},H={Settings:{},Cones:{Cones:[Cone]},Bodies:{PlaceBody:(B,P)=>{Placed=P;B.Position=P;}}};ResolveEditorTransform(H,'cone-0').Set([4,5,6]);assert.deepEqual(Placed,[4,5,6]);});
test('baked-coordinate static shapes use their bounds centre, not world origin',()=>{
 const O={Transform:new Float32Array([1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1])};let Upload=0,Invalidated=0,Committed=0;
 const H={Settings:{},Scene:{Occupants:[O],QueryWorldBounds:()=>{const Points=Array.from({length:8},(_,K)=>[K&1?110:100,K&2?30:20,K&4?10:0].map((_,A,Local)=>[0,1,2].reduce((N,J)=>N+O.Transform[J*4+A]*Local[J],O.Transform[12+A])));return {Minimum:[0,1,2].map(A=>Math.min(...Points.map(P=>P[A]))),Maximum:[0,1,2].map(A=>Math.max(...Points.map(P=>P[A])))};},AssignOccupantTransform:(_,T)=>O.Transform=T,EncodeOccupantRange:()=>new Float32Array(84),QueryObstacleBounds:()=>[]},Bodies:{Bodies:[],AssignObstacles:()=>Committed++},Vehicle:{AssignObstacles:()=>Committed++},Exchange:{Device:{queue:{writeBuffer:()=>Upload++}}},Distance:{InvalidateBounds:()=>Invalidated++},Cards:{InvalidateLighting:()=>{}},Cones:{Cones:[]},Fracture:{Props:[]}};
 const T=ResolveEditorTransform(H,'object-0');assert.deepEqual(T.Get(),[105,25,5]);T.Set([108,25,5]);assert.deepEqual(T.Get(),[108,25,5]);assert.equal(O.Transform[12],3);assert.equal(Upload,1);assert.equal(Invalidated,1);T.Commit();assert.equal(Committed,2);T.Set([105,25,5]);assert.equal(O.Transform[12],0);
 const Snapshot=T.GetRotationState(),Pivot=T.Get();T.Rotate([0,0,1],Math.PI/2,Snapshot,Pivot);const World=Pivot.map((_,A)=>[0,1,2].reduce((N,K)=>N+O.Transform[K*4+A]*Pivot[K],O.Transform[12+A]));World.forEach((V,A)=>assert.ok(Math.abs(V-Pivot[A])<1e-4));T.RestoreRotation(Snapshot);assert.deepEqual(O.Transform,Snapshot);T.SetScale([2,2,2]);assert.deepEqual(T.GetScale(),[2,2,2]);T.SetScale([1,1,1]);assert.deepEqual(O.Transform,Snapshot);
});
test('cone rotation updates the physics quaternion and restores it exactly',()=>{
 const Body={Position:[1,2,3],Orientation:[0,0,0,1]},H={Settings:{},Cones:{Cones:[{Body}]},Bodies:{PlaceBody:(B,P,Q)=>{B.Position=P.slice();B.Orientation=Q.slice();}}};
 const T=ResolveEditorTransform(H,'cone-0'),Q=T.GetRotationState();T.Rotate([0,0,1],Math.PI/2,Q);assert.ok(Math.abs(Body.Orientation[2]-Math.SQRT1_2)<1e-8);T.RestoreRotation(Q);assert.deepEqual(Body.Orientation,[0,0,0,1]);
});
test('celestial geometry is ported with exact reference dimensions and material palette',async()=>{
 const {readFile}=await import('node:fs/promises'),S=await readFile(new URL('../Source/CelestialGizmoRenderer.js',import.meta.url),'utf8');
 for(const Token of ['0xe01414','0x12d40a','0x1560e0','ConeGeometry(.06,.18,24)','CylinderGeometry(.06,.06,.14,24)','degToRad(31)','arcBand=.038','TorusGeometry(.16,.008,12,48)','roughness:.45,metalness:.1','ray.intersectObjects','depthTest:false'])assert.ok(S.includes(Token),Token);
 assert.ok(!S.includes('https://unpkg.com'));
});
test('gizmo requires Free camera plus an explicit matching Outliner selection',async()=>{
 const {CanUseEditorGizmo}=await import('../Source/TransformGizmo.js');const H={FreeCamera:false},S={Selected:'vehicle',GizmoSelection:'vehicle'};
 assert.equal(CanUseEditorGizmo(H,S),false);H.FreeCamera=true;assert.equal(CanUseEditorGizmo(H,S),true);S.GizmoSelection=null;assert.equal(CanUseEditorGizmo(H,S),false);S.GizmoSelection='cone-0';assert.equal(CanUseEditorGizmo(H,S),false);S.GizmoSelection='vehicle';H.ParticleWorkspace={Active:true};assert.equal(CanUseEditorGizmo(H,S),false);
});
test('gizmo basis follows quaternion and matrix orientation, excluding nonuniform scale',async()=>{
 const {QuaternionGizmoBasis,MatrixGizmoBasis}=await import('../Source/EditorTransforms.js');const {ConstructAxisQuaternion,ConstructRigidTransform}=await import('../Source/TransformProjection.js');const Q=ConstructAxisQuaternion([0,0,1],Math.PI/2),B=QuaternionGizmoBasis(Q),M=MatrixGizmoBasis(ConstructRigidTransform([1,2,3],Q,[2,3,4]));
 assert.ok(Math.abs(B[0][0])<1e-8);assert.ok(Math.abs(B[0][1]-1)<1e-8);assert.ok(Math.abs(B[1][0]+1)<1e-8);for(let I=0;I<3;I++)for(let J=0;J<3;J++)assert.ok(Math.abs(B[I][J]-M[I][J])<1e-6);
});
test('local-axis translation moves along the rotated object axis, not a world axis',async()=>{
 const {TransformGizmo}=await import('../Source/TransformGizmo.js'),N=Math.SQRT1_2;let Position=[0,10,0];const T={Get:()=>Position,Set:P=>Position=P};const G=Object.create(TransformGizmo.prototype);
 Object.assign(G,{Shell:{Selected:'object-1',Controls:{Synchronise(){}}},Stage:{getBoundingClientRect:()=>({width:200,height:200,left:0,top:0})},Target:()=>T,Draw(){},Drag:{T,Id:'object-1',Mode:'g',Axis:'x',Basis:[[N,N,0],[-N,N,0],[0,0,1]],Start:[0,10,0],Screen:[100,100,10],Mouse:[100,100],Camera:{Position:[0,0,0],Forward:[0,1,0],Right:[1,0,0],Up:[0,0,1],TanY:1}}});
 G.Move({clientX:120,clientY:100});assert.ok(Position[0]>.1);assert.ok(Math.abs(Position[0]-(Position[1]-10))<1e-8);assert.equal(Position[2],0);
});
