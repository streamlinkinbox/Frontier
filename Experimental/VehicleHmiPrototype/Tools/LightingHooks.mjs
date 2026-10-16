import assert from 'node:assert/strict';
import {ParseIES} from '../Source/Photometry.js';
const IES='IESNA:LM-63-2002\nTILT=NONE\n1 1000 1 4 1 1 2 0.2 0.2 0.1 1 1 12\n0 30 60 90\n0\n100 80 40 0';
let Saw=new Set();
export default {
    BeforeFrame(Host,Frame) {
        Host.FixedStep=1/60;Host.Settings.SunElevation=-8;
        const Stage=Math.floor(Frame/8)%7;
        Host.Settings.LightMode=['Off','Low','High','Fog','Low','High','Off'][Stage];
        Host.Settings.RoofProfile=Stage===4?'Spot':Stage===5?'IES':'Automotive';
        Host.Settings.StreetLights=Stage!==6;
        if(Stage===5&&!Host.LocalLighting.IES) Host.LocalLighting.IES=ParseIES(IES);
        if(Frame>0){Saw.add(Host.PriorLocalLightKey);assert.ok(Host.LocalLighting.Metrics.Count<=32);}
        const E=Host.Exchange.Device.createCommandEncoder();
        Host.Cabin.RecordMirror(E,Host.Scene,Host.VertexRecords,Host.Vehicle.QueryBodyTransform(),0);
        Host.Exchange.Device.queue.submit([E.finish()]);
    },
    Report(Host) {
        assert.ok(Saw.size>=6);
        assert.equal(Host.LocalLighting.Lights.length,0);
        assert.deepEqual(Host.Materials[84].Emission,[0,0,0]);assert.deepEqual(Host.Materials[85].Emission,[0,0,0]);
        assert.deepEqual(Host.Materials[86].Emission,[0,0,0]);
        assert.deepEqual(Host.Materials[13].Emission,[0,0,0]);
        assert.equal(Host.Cards.LightingTiles.length,0);
        console.log('lighting: Off/Low/High/Fog/Spot/IES, mirrors, clustered masks and source/GI off states exercised');
    }
};
