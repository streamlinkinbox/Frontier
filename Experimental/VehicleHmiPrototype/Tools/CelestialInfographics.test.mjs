import {test} from 'node:test';
import assert from 'node:assert/strict';
import {EnvironmentDefaults} from '../Source/EnvironmentSettings.js';
import {StarChartPoint, StarsCatalogueData, StarScintillation, SetStarsInspectorValue} from '../Source/StarsInspectorInfographics.js';
import {LunarPhaseData, LunarCycle, MoonAngularRuler, MoonInspectorTargets, SetMoonInspectorValue} from '../Source/MoonInspectorInfographics.js';

test('star planisphere is equal-area about the north pole and rotates actual catalogue coordinates',()=>{
 assert.deepEqual(StarChartPoint([0,0,1,1]),[0,-0]);
 const Equator=StarChartPoint([1,0,0,1]);assert.ok(Math.abs(Math.hypot(...Equator)-Math.SQRT1_2)<1e-12);
 const Turn=StarChartPoint([1,0,0,1],90);assert.ok(Math.abs(Turn[0])<1e-12);assert.ok(Math.abs(Turn[1]-Math.SQRT1_2)<1e-12);
 assert.ok(Math.abs(Math.hypot(...StarChartPoint([0,0,-1,1]))-1)<1e-12);
});
test('star population uses real flux cutoffs and each catalogue record is counted once',()=>{
 const R=[-1,0,2,4,6,8].map(M=>[1,0,0,10**(-.4*M),1,1,1]);
 const Data=StarsCatalogueData(R,4);assert.equal(Data.Total,6);assert.equal(Data.Eligible,4);assert.equal(Data.Bins.reduce((A,B)=>A+B,0),6);assert.equal(Data.EligibleBins.reduce((A,B)=>A+B,0),4);
 assert.equal(StarsCatalogueData([],6).Eligible,0);
});
test('star editing bounds fields, rejects unrelated/nonfinite data and wraps catalogue rotation',()=>{
 const S={...EnvironmentDefaults};SetStarsInspectorValue(S,'StarRotation',-10);assert.equal(S.StarRotation,350);SetStarsInspectorValue(S,'StarRotation',361);assert.equal(S.StarRotation,1);
 SetStarsInspectorValue(S,'StarLimit',19);assert.equal(S.StarLimit,8);SetStarsInspectorValue(S,'TwinkleDepth',-2);assert.equal(S.TwinkleDepth,0);
 assert.equal(SetStarsInspectorValue(S,'MoonPhase',9),false);assert.equal(SetStarsInspectorValue(S,'StarSize',Infinity),false);
});
test('scintillation reference exactly follows the renderer frequencies, phase and depth scaling',()=>{
 const S={...EnvironmentDefaults,TwinkleDepth:.7,TwinkleRate:1.3},R=[.5,.4,Math.sqrt(.59),2,1,1,1],Time=2.5,P=R[0]*91.7+R[1]*213.1+R[2]*57.3,T=Time*S.TwinkleRate;
 const Expected=1+.7*.35*(.6*Math.sin(T*6.283185+P)+.28*Math.sin(T*11.71+P*1.7)+.12*Math.sin(T*19.37+P*2.3));assert.equal(StarScintillation(Time,S,R),Expected);
 for(let I=0;I<100;I++){const V=StarScintillation(I/10,S,R);assert.ok(V>=1-.7*.35&&V<=1+.7*.35);}
 assert.equal(StarScintillation(Time,{...S,TwinkleEnabled:false},R),1);
});
test('Moon phase data retains renderer conventions and the 29.53-day cycle',()=>{
 assert.equal(LunarPhaseData(0).Fraction,0);assert.equal(LunarPhaseData(LunarCycle/2).Fraction,1);assert.equal(LunarPhaseData(LunarCycle/2).Name,'Full moon');
 assert.ok(Math.abs(LunarPhaseData(LunarCycle/4).Fraction-.5)<1e-12);assert.equal(LunarPhaseData(LunarCycle).Name,'New moon');assert.equal(LunarPhaseData(-LunarCycle/4).Name,'Last quarter');
});
test('Moon manual edits unlink phase/placement only; size and orientation retain solver follow',()=>{
 const M={...EnvironmentDefaults,MoonFollowSky:true};SetMoonInspectorValue(M,'MoonSize',300);assert.equal(M.MoonSize,180);assert.equal(M.MoonFollowSky,true);
 SetMoonInspectorValue(M,'MoonSpin',250);assert.equal(M.MoonSpin,180);assert.equal(M.MoonFollowSky,true);SetMoonInspectorValue(M,'MoonElevation',-100);assert.equal(M.MoonElevation,-90);assert.equal(M.MoonFollowSky,false);
 M.MoonFollowSky=true;SetMoonInspectorValue(M,'MoonPhase',10);assert.equal(M.MoonFollowSky,false);assert.equal(SetMoonInspectorValue(M,'CloudDensity',2),false);assert.equal(SetMoonInspectorValue(M,'MoonGlow',NaN),false);
});
test('four Moon targets preserve primary defaults and do not edit another instance',()=>{
 const S={...EnvironmentDefaults,MoonInstances:[]},Targets=MoonInspectorTargets(S);assert.equal(Targets.length,4);assert.equal(Targets[0],S);assert.equal(Targets[0].MoonSize,25);assert.ok(Targets.slice(1).every(M=>!M.MoonEnabled));
 SetMoonInspectorValue(Targets[2],'MoonPhase',7);assert.equal(S.MoonPhase,14.765);assert.equal(Targets[1].MoonPhase,14.765);assert.equal(MoonInspectorTargets(S)[2],Targets[2]);
});
test('Moon angular ruler is finite and monotonic across oversized artistic diameters',()=>{
 const Values=[.1,.52,1,5,25,90,180].map(MoonAngularRuler);assert.ok(Values.every(V=>Number.isFinite(V)&&V>0&&V<=1));assert.ok(Values.every((V,I)=>I===0||V>Values[I-1]));assert.equal(Values.at(-1),1);
});
