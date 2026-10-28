// Renderer v2: independent bounded cloud renderer. No native cloud kernels/history/cache.
export const CloudDefaults={CloudRendererVersion:2,CloudEnabled:true,WindEnabled:true,WindGust:.25,WindShear:.15,WindVeer:8,WindTurbulence:.15,CloudDetail:.3,CloudLightSteps:2,CloudAbsorption:.15,CloudPowder:.45,LocalCloudShape:0,LocalCloudForm:1,LocalCloudSoftness:.5,LocalCloudDetail:.55,FogModelVersion:1,LocalFogFalloff:true,LocalFogSelfShadow:true,LocalFogSpeed:.8,LocalFogEnabled:false,LocalFogFollowWind:true,LocalFogX:30,LocalFogY:0,LocalFogZ:2.5,LocalFogHalfX:18,LocalFogHalfY:18,LocalFogHalfZ:4,LocalFogShape:0,LocalFogDensity:.35,LocalFogNoise:.75,LocalFogScale:5,LocalFogSteps:28,LocalFogAbsorption:.08,LocalFogAnisotropy:.6,LocalFogSoftness:.45,LocalFogLightGain:1,LocalFogAlbedo:[.72,.8,.9],CloudShadows:true,CloudShadowStrength:.65,CloudShadowSoftness:35,CloudShadowDistance:6000,CloudShadowSamples:6,CloudFollowWind:true,CloudCoverage:.55,CloudDensity:1,CloudScale:1,CloudAnisotropy:.35,CloudType:2,CloudAnvil:.5,CloudBase:1500,CloudThickness:1200,CloudCeiling:14000,LocalCloudEnabled:false,LocalCloudFollowWind:true,LocalCloudX:0,LocalCloudY:0,LocalCloudZ:400,LocalCloudHalfX:300,LocalCloudHalfY:300,LocalCloudHalfZ:150,LocalCloudDensity:1,LocalCloudCoverage:.6,LocalCloudScale:30,LocalCloudAnisotropy:.35,CloudResolution:35,CloudSteps:12,CloudLocalSteps:16,CloudWindSpeed:7,CloudWindBearing:90};
export const CloudRanges={WindGust:[0,1],WindShear:[0,2],WindVeer:[-60,60],WindTurbulence:[0,1],CloudDetail:[0,1],CloudLightSteps:[0,4],CloudAbsorption:[0,1],CloudPowder:[0,1],LocalCloudShape:[0,1],LocalCloudForm:[0,2],LocalCloudSoftness:[.05,1],LocalCloudDetail:[0,1],LocalFogX:[-100000,100000],LocalFogY:[-100000,100000],LocalFogZ:[-10000,20000],LocalFogHalfX:[1,1000],LocalFogHalfY:[1,1000],LocalFogHalfZ:[1,1000],LocalFogSpeed:[0,5],LocalFogShape:[0,3],LocalFogDensity:[0,2],LocalFogNoise:[0,1],LocalFogScale:[.5,30],LocalFogSteps:[8,48],LocalFogAbsorption:[0,1],LocalFogAnisotropy:[0,.95],LocalFogSoftness:[.02,1],LocalFogLightGain:[0,3],CloudShadowStrength:[0,1],CloudShadowSoftness:[0,200],CloudShadowDistance:[1000,16000],CloudShadowSamples:[4,12],CloudCoverage:[0,1],CloudDensity:[0,4],CloudScale:[.2,3],CloudAnisotropy:[-.8,.8],CloudType:[0,5],CloudAnvil:[0,1],CloudBase:[100,20000],CloudThickness:[100,6000],CloudCeiling:[4000,20000],LocalCloudX:[-100000,100000],LocalCloudY:[-100000,100000],LocalCloudZ:[-10000,20000],LocalCloudHalfX:[1,10000],LocalCloudHalfY:[1,10000],LocalCloudHalfZ:[1,10000],LocalCloudDensity:[0,4],LocalCloudCoverage:[0,1],LocalCloudScale:[2,200],LocalCloudAnisotropy:[-.8,.8],CloudResolution:[25,100],CloudSteps:[8,64],CloudLocalSteps:[8,64],CloudWindSpeed:[0,40],CloudWindBearing:[0,360]};
export function CloudExtent(W,H,Percent=CloudDefaults.CloudResolution){const S=Math.max(.25,Math.min(1,Percent/100));return [Math.max(1,Math.ceil(W*S)),Math.max(1,Math.ceil(H*S))];}
// Small periodic value fields baked once at startup. All octaves are already in the texture.
export function BuildCloudField(Size=64){
 let Seed=0x31a59e27;const Rand=()=>{Seed^=Seed<<13;Seed^=Seed>>>17;Seed^=Seed<<5;return (Seed>>>0)/4294967296;};
 const Grids=[8,16,32].map(N=>({N,A:Float32Array.from({length:N*N*N},Rand)}));
 const Out=new Uint8Array(Size**3);
 for(let Z=0;Z<Size;Z++)for(let Y=0;Y<Size;Y++)for(let X=0;X<Size;X++){
  let V=0,Weight=0;
  for(let O=0;O<3;O++){const {N,A}=Grids[O],Scale=N/Size;const PX=X*Scale,PY=Y*Scale,PZ=Z*Scale,IX=Math.floor(PX),IY=Math.floor(PY),IZ=Math.floor(PZ);
   const Ease=T=>T*T*(3-2*T),FX=Ease(PX-IX),FY=Ease(PY-IY),FZ=Ease(PZ-IZ);let S=0;
   for(let K=0;K<2;K++)for(let J=0;J<2;J++)for(let I=0;I<2;I++)S+=A[((((IZ+K)%N)*N+(IY+J)%N)*N+(IX+I)%N)]*(I?FX:1-FX)*(J?FY:1-FY)*(K?FZ:1-FZ);
   const W=1/(1<<O);V+=S*W;Weight+=W;
  }Out[(Z*Size+Y)*Size+X]=Math.round(V/Weight*255);
 }return Out;
}

// Preserve the base march density as the editable layer grows; bound worst-case work.
export function CloudSkySamples(S){const Depth=Math.max(0,Math.min(S.CloudCeiling,S.CloudBase+S.CloudThickness)-S.CloudBase);return Math.min(96,Math.ceil(S.CloudSteps*Math.max(1,Depth/1200)));}

export function CloudLocalSamples(S){const Span=2*Math.max(S.LocalCloudHalfX,S.LocalCloudHalfY,S.LocalCloudHalfZ);return Math.min(96,Math.max(S.CloudLocalSteps,Math.ceil(Span/Math.max(1,S.LocalCloudScale*1.5))));}
