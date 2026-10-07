import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {buildMountain} from '../src/mountain.js';
import {disposeQuarry} from '../src/quarry.js';
import {createRockMaterial,updateRockSurface,surfaceSettings,referenceStoneFinish,fieldGLSL,surfaceGLSL} from '../src/surface-material.js';
import {stoneReliefBands} from '../src/surface-height.js';
import {rebuildSurfaceDetails} from '../src/surface-details.js';
const settings={geology:'sandstone',seed:123,mountainHeight:45,mountainRadius:40,meshStyle:'lowpoly',detail:0,debris:false};
test('surface shader uses no texture samples, supports instancing and shades all families',()=>{
  for(const geology of ['limestone','sandstone','slate']){
    const m=createRockMaterial({geology,seed:42});
    for(const name of ['map','normalMap','bumpMap','roughnessMap','metalnessMap','displacementMap'])assert.equal(m[name],null);
    const shader={vertexShader:THREE.ShaderLib.standard.vertexShader,fragmentShader:THREE.ShaderLib.standard.fragmentShader,uniforms:{}};
    m.onBeforeCompile(shader);
    assert.ok(shader.vertexShader.includes('rockWorld=instanceMatrix*rockWorld'));
    assert.ok(shader.fragmentShader.includes('if(uSurfaceEnabled>.5)'));
    assert.ok(shader.fragmentShader.includes('roughnessFactor=clamp'));
    assert.equal(shader.uniforms.uSurfaceEnabled.value,1);
    assert.doesNotMatch(fieldGLSL+surfaceGLSL,/sampler|texture\s*\(/);
    m.dispose();
  }
});
test('live material controls change uniforms without rebuilding base or rock matrices',()=>{
  const q=buildMountain(settings),before=q.children[0].geometry.attributes.position.array.slice();
  const material=q.children[0].material;
  updateRockSurface(q,{surfaceEnabled:false,oxidation:100,weathering:0,grain:0,grainSize:48,crystals:0,peeling:0});
  assert.equal(material.userData.surfaceUniforms.uOxidation.value,1);assert.equal(material.userData.surfaceUniforms.uSurfaceEnabled.value,0);
  assert.equal(material.userData.surfaceUniforms.uGrainSize.value,48);
  assert.deepEqual(before,q.children[0].geometry.attributes.position.array);
  assert.equal(surfaceSettings({grain:999}).grain,100);assert.equal(surfaceSettings({grain:-1}).grain,0);
  disposeQuarry(q);
});
test('small detail geometry is bounded, repeatable and independently controlled',()=>{
  const q=buildMountain(settings);
  let root=rebuildSurfaceDetails(q,{peeling:100,crystals:100});
  assert.equal(root.children[0].count,240);
  assert.ok(root.children[1].count>1000&&root.children[1].count<=2400);
  assert.equal(root.userData.clusters.length,120);
  assert.ok(root.children[1].instanceColor);
  assert.equal(root.children[1].receiveShadow,false);
  const crystalMatrix=root.children[1].instanceMatrix.array.slice();
  let disposed=false;root.children[0].geometry.addEventListener('dispose',()=>disposed=true);
  root=rebuildSurfaceDetails(q,{peeling:0,crystals:100});
  assert.ok(disposed);assert.equal(root.children.length,1);assert.deepEqual(crystalMatrix,root.children[0].instanceMatrix.array);
  root.children.forEach(o=>{assert.ok(o.instanceMatrix.array.every(Number.isFinite));assert.ok(o.geometry.attributes.position.array.every(Number.isFinite));});
  root=rebuildSurfaceDetails(q,{peeling:0,crystals:0,surfaceEnabled:false});assert.equal(root.children.length,0);assert.equal(root.visible,false);
  disposeQuarry(q);
});

test('grain size is bounded separately from strength and detail is clustered and exposed',()=>{
  assert.equal(surfaceSettings({grainSize:0}).grainSize,2);
  assert.equal(surfaceSettings({grainSize:9000}).grainSize,2000);
  assert.equal(surfaceSettings({grain:0,grainSize:36}).grainSize,36);
  assert.ok(surfaceGLSL.includes('rockAggregate(grainDomain)'));
  assert.ok(surfaceGLSL.includes('rockRunoff(p,seed,footprint)'));
  assert.doesNotMatch(surfaceGLSL,/fract\(plane\)/);
  const q=buildMountain(settings),root=rebuildSurfaceDetails(q,{peeling:0,crystals:50});
  assert.equal(root.userData.clusters.length,60);
  const mesh=root.getObjectByName('Mineral_crystals'),matrix=new THREE.Matrix4(),point=new THREE.Vector3();
  for(let i=0;i<mesh.count;i++){
    mesh.getMatrixAt(i,matrix);point.setFromMatrixPosition(matrix);
    assert.ok(root.userData.clusters.some(c=>c.position.distanceTo(point)<.35));
  }
  for(const cluster of root.userData.clusters){
    const origin=cluster.position.clone().addScaledVector(cluster.normal,.006);
    const ray=new THREE.Raycaster(origin,cluster.normal,0,2000);
    assert.equal(ray.intersectObjects(q.children.filter(o=>o.isMesh),false).length,0);
  }
  // Even without dressing, continuous base surfaces receive actual crystals.
  const base=buildMountain(settings),sources=base.children.filter(o=>o.isInstancedMesh);
  sources.forEach(o=>base.remove(o));
  const baseRoot=rebuildSurfaceDetails(base,{peeling:0,crystals:20});
  assert.ok(baseRoot.getObjectByName('Mineral_crystals').count>0);
  sources.forEach(o=>base.add(o));disposeQuarry(base);disposeQuarry(q);
});

test('layered mineral flakes are live, mapless and independent of prism geometry',()=>{
  const material=createRockMaterial({mineralFlakes:75,grainSize:700});
  const shader={vertexShader:THREE.ShaderLib.standard.vertexShader,fragmentShader:THREE.ShaderLib.standard.fragmentShader,uniforms:{}};
  material.onBeforeCompile(shader);
  assert.equal(shader.uniforms.uMineralFlakes.value,.75);
  assert.equal(shader.uniforms.uGrainSize.value,700);
  assert.ok(fieldGLSL.includes('layer<3'));
  assert.ok(fieldGLSL.includes('priority>front'));
  assert.ok(fieldGLSL.includes('rockFilter(scale,footprint)'));
  assert.ok(fieldGLSL.includes('weights=pow(abs(n)'));
  assert.doesNotMatch(fieldGLSL,/sampler|texture\s*\(/);
  assert.equal(material.metalness,0);
  const group=buildMountain(settings),root=rebuildSurfaceDetails(group,{peeling:0,crystals:50});
  const crystals=root.getObjectByName('Mineral_crystals').instanceMatrix.array.slice();
  updateRockSurface(group,{mineralFlakes:100,grainSize:2000});
  assert.equal(group.children[0].material.userData.surfaceUniforms.uMineralFlakes.value,1);
  updateRockSurface(group,{mineralFlakes:0,grainSize:60});
  assert.equal(group.children[0].material.userData.surfaceUniforms.uMineralFlakes.value,0);
  assert.deepEqual(root.getObjectByName('Mineral_crystals').instanceMatrix.array,crystals);
  assert.equal(surfaceSettings({mineralFlakes:999}).mineralFlakes,100);
  assert.equal(surfaceSettings({mineralFlakes:-2}).mineralFlakes,0);
  assert.equal(surfaceSettings({}).grainSize,240);
  disposeQuarry(group);material.dispose();
});

test('flake physical finish and selective relief use live per-cell shader parameters',()=>{
  const group=buildMountain(settings),material=group.children[0].material;
  assert.ok(material.isMeshPhysicalMaterial);
  const shader={vertexShader:THREE.ShaderLib.physical.vertexShader,fragmentShader:THREE.ShaderLib.physical.fragmentShader,uniforms:{}};
  material.onBeforeCompile(shader);
  updateRockSurface(group,{flakeMetallic:80,flakeRoughness:12,flakeSpecular:90,flakeClearcoat:70,flakeCoatRoughness:15,flakeIOR:2.2,flakeColor:100,flakeRelief:0});
  for(const [name,value] of [['uFlakeMetallic',.8],['uFlakeRoughness',.12],['uFlakeSpecular',.9],['uFlakeClearcoat',.7],['uFlakeCoatRoughness',.15],['uFlakeIOR',2.2],['uFlakeColor',1],['uFlakeRelief',0]])assert.equal(shader.uniforms[name].value,value);
  assert.ok(shader.fragmentShader.includes('material.clearcoat = uFlakeClearcoat*rockFlakeMask'));
  assert.ok(shader.fragmentShader.includes('material.ior = mix(1.5,uFlakeIOR,rockFlakeMask)'));
  assert.ok(surfaceGLSL.includes('metalnessFactor=uFlakeMetallic*flakeAmount'));
  assert.ok(fieldGLSL.includes('result.relief=step(1.0-uFlakeRelief'));
  assert.ok(fieldGLSL.includes('result.slope=(result.slope+'));
  assert.ok(fieldGLSL.includes('result.color=vec3(rockHash'));
  assert.equal(surfaceSettings({flakeIOR:99}).flakeIOR,2.5);
  assert.equal(surfaceSettings({flakeIOR:0}).flakeIOR,1);
  updateRockSurface(group,{flakeRelief:100});assert.equal(shader.uniforms.uFlakeRelief.value,1);
  disposeQuarry(group);
});

test('layered stone and grain height controls derive normals from mapless scalar relief',()=>{
  const group=buildMountain(settings);
  updateRockSurface(group,{layerDepth:95,grainHeight:100});
  const u=group.children[0].material.userData.surfaceUniforms;
  assert.equal(u.uLayerDepth.value,.95);assert.equal(u.uGrainHeight.value,1);
  assert.ok(fieldGLSL.includes('rockStoneHeight'));
  assert.ok(fieldGLSL.includes('dFdx(height)'));
  assert.ok(surfaceGLSL.includes('rockHeightNormal(p,stoneFaceNormal,totalHeight)'));
  assert.ok(surfaceGLSL.includes('grainHeight*raisedCoverage'));
  assert.doesNotMatch(fieldGLSL,/sampler|texture\s*\(/);
  updateRockSurface(group,{layerDepth:0,grainHeight:0});
  assert.equal(u.uLayerDepth.value,0);assert.equal(u.uGrainHeight.value,0);
  assert.equal(surfaceSettings({layerDepth:200,grainHeight:-1}).layerDepth,100);
  disposeQuarry(group);
});


test('reference stone removes isocontour whorls and keeps height on a geometric normal frame',()=>{
  assert.ok(fieldGLSL.includes('rockSegmentDistance(q,start,end)'));
  assert.ok(fieldGLSL.includes('rockBrokenFractures'));
  assert.doesNotMatch(fieldGLSL,/abs\(joint-\.5\)|abs\(branch-\.48\)/);
  assert.ok(surfaceGLSL.includes('rockStoneHeight(p,stoneFaceNormal,seed,footprint)'));
  assert.ok(surfaceGLSL.includes('rockHeightNormal(p,stoneFaceNormal,totalHeight)'));
  assert.ok(surfaceGLSL.includes('continuousGrain'));
  const group=buildMountain(settings),positions=group.children[0].geometry.attributes.position.array.slice();
  updateRockSurface(group,referenceStoneFinish);
  assert.equal(group.children[0].material.userData.surfaceUniforms.uGrain.value,.65);
  assert.equal(group.children[0].material.userData.surfaceUniforms.uFlakeMetallic.value,0);
  assert.equal(group.children[0].material.userData.surfaceUniforms.uLayerDepth.value,.85);
  assert.deepEqual(group.children[0].geometry.attributes.position.array,positions);
  disposeQuarry(group);
});


test('restored relief retains explicit multi-scale amplitude and independent grain height',()=>{
  assert.equal(stoneReliefBands.length,4);
  for(const band of stoneReliefBands){
    assert.ok(band.amplitude*band.frequency>.075,'Each resolved relief band must retain a readable slope budget');
    assert.ok(fieldGLSL.includes(band.amplitude.toFixed(4)));
  }
  assert.equal(referenceStoneFinish.grainHeight,85);
  assert.ok(referenceStoneFinish.grainSize>=100);
  assert.ok(surfaceGLSL.includes('*uGrainHeight*grainVisibility'));
  assert.doesNotMatch(surfaceGLSL,/uGrainHeight\*uGrain\b/);
  const group=buildMountain(settings),before=group.children[0].geometry.attributes.position.array.slice();
  updateRockSurface(group,{grain:0,grainHeight:100,layerDepth:100});
  const u=group.children[0].material.userData.surfaceUniforms;
  assert.equal(u.uGrain.value,0);assert.equal(u.uGrainHeight.value,1);assert.equal(u.uLayerDepth.value,1);
  assert.deepEqual(group.children[0].geometry.attributes.position.array,before);
  disposeQuarry(group);
});
