import assert from 'node:assert/strict';
export default {
 BeforeFrame(H,F){const C=H.Surface.Clusters;if(F===1){this.Before=C.CandidateCount;C.TriangleLimit=64;C.Key=null;C.Cache.clear();}if(F===2){assert.ok(C.CandidateCount>this.Before);assert.equal(H.Telemetry.FrameRows.length,2);H.Telemetry.Pause();}if(F===3){const N=H.Telemetry.FrameRows.length;assert.equal(N,2);H.Telemetry.Resume();H.Shell.Select('clusters');}if(F===4){H.Telemetry.Dispose();H.Telemetry=null;H.Shell.Select('diagnostics');}},
 Report(H){assert.equal(H.Telemetry,null);console.log('Recorder capture/pause/resume/remove, cluster leaf rebuild and inspectors passed.');}
};
