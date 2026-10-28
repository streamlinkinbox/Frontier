import assert from 'node:assert/strict';
export default{
 BeforeFrame(H,F){Object.assign(H.Settings,{ClusteredGeometryEnabled:F!==1,CloudEnabled:false,LocalCloudEnabled:false,LocalFogEnabled:true,LocalFogX:-20,LocalFogY:0,LocalFogZ:2.5,LocalFogHalfX:30,LocalFogHalfY:20,LocalFogHalfZ:5,LocalFogDensity:.12});},
 Report(H){assert.ok(H.Surface.Clusters.CandidateCount>0);console.log('Live scene clustered / legacy / clustered routing with local fog passed.',{candidates:H.Surface.Clusters.CandidateCount,indexBytes:H.Surface.Clusters.IndexCount*4});}
};
