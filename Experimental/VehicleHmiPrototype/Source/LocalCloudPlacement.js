export const LocalCloudPresets={
 Puff:{LocalCloudHalfX:90,LocalCloudHalfY:90,LocalCloudHalfZ:55,LocalCloudScale:80,LocalCloudCoverage:.72,LocalCloudDensity:1},
 Bank:{LocalCloudHalfX:300,LocalCloudHalfY:160,LocalCloudHalfZ:70,LocalCloudScale:120,LocalCloudCoverage:.8,LocalCloudDensity:.8},
 Wisp:{LocalCloudHalfX:140,LocalCloudHalfY:65,LocalCloudHalfZ:25,LocalCloudScale:65,LocalCloudCoverage:.58,LocalCloudDensity:.5}
};
export function PlaceLocalCloud(S,Eye=[0,0,2],Forward=[1,0,0]){
 const Length=Math.hypot(...Forward),D=Length>1e-6?Forward.map(V=>V/Length):[1,0,0];
 const Distance=Math.max(80,S.LocalCloudHalfX,S.LocalCloudHalfY,S.LocalCloudHalfZ)*2.4;
 for(let I=0;I<3;I++)S['LocalCloud'+['X','Y','Z'][I]]=Math.max(-100000,Math.min(100000,Math.round(Eye[I]+D[I]*Distance)));
 // Aim along the view instead of lifting the volume out of a downward-looking chase camera.
 S.LocalCloudZ=Math.max(5,Math.min(20000,S.LocalCloudZ));S.LocalCloudEnabled=true;
 if(S.LocalCloudCoverage<=0)S.LocalCloudCoverage=.72;if(S.LocalCloudDensity<=0)S.LocalCloudDensity=1;
 return [S.LocalCloudX,S.LocalCloudY,S.LocalCloudZ];
}
