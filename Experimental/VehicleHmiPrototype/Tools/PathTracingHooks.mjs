import assert from "node:assert/strict";
import { CourseMaterial } from "../Source/MaterialSpecification.js";
import { PathPixelBytes } from "../Source/PathTracingSettings.js";
let LastFrame = -1, InitialGeneration = 0, Fragments = false, Resized = false;
async function ReadBuffer(Device, Buffer) {
    const Read = Device.createBuffer({ size: Buffer.size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const Encoder = Device.createCommandEncoder(); Encoder.copyBufferToBuffer(Buffer,0,Read,0,Buffer.size);
    Device.queue.submit([Encoder.finish()]); await Read.mapAsync(GPUMapMode.READ);
    const Bytes = Read.getMappedRange().slice(0); Read.unmap(); Read.destroy(); return Bytes;
}
export default {
    BeforeFrame(Host, Frame) {
        if (Frame === LastFrame) return; LastFrame = Frame;
        assert.ok(Host.PathTracer, "default renderer must really construct the triangle path tracer");
        Host.FixedStep = 1/60;
        if (Frame === 0) InitialGeneration = Host.PathTracer.Acceleration.Generation;
        if (Frame === 1) Host.Stress.Assign(2);
        if (Frame === 2) Host.Vehicle.ShredTyre(0);
        if (Frame === 3) { Host.Fracture.BreakFromEditor(Host.Fracture.Props[0]); Fragments = true; }
        if (Frame === 4) { Host.Settings.HmiScale = 2; Host.AssignCameraMode("HMI"); }
        if (Frame === 5) { Host.AssignCameraMode("Chase"); Host.AfterVehicleMoved(); }
        if (Frame === 6) {
            Host.Settings.TraceScale = Host.Settings.TraceScale === 4 ? 2 : 4;
            Host.ReconstructScreenTargets(); Resized = true;
        }
        if (Frame === 7) { Host.Settings.SpatialReuse = false; Host.ApplySetting("SpatialReuse"); }
        if (Frame === 8) { Host.Settings.SpatialReuse = true; Host.ApplySetting("SpatialReuse"); }
        if (Frame >= 9) Host.Settings.HmiScale = Frame & 1 ? 1 : 2;
    },
    async Report(Host) {
        assert.ok(Host.FrameIndex>=10,"run at least 10 frames to test previous tablet deformation");
        assert.ok(Host.PathTracer.Acceleration.Generation > InitialGeneration);
        assert.ok(Fragments && Resized);
        assert.ok(Host.PathTracer.Acceleration.Data.Active.some(({ Index }) => Host.Scene.Occupants[Index].Fragment));
        assert.ok(Host.PathTracer.TraceExtent[0] === Host.TraceExtent[0]);
        assert.ok(Host.Vehicle.Wheels[0].Shredded);
        if (process.env.BACKEND && process.env.BACKEND !== "null") {
            const Device = Host.Exchange.Device, P = Host.PathTracer;
            const Bytes = await ReadBuffer(Device,P.Histories[(Host.FrameIndex-1)&1]);
            const F = new Float32Array(Bytes), U = new Uint32Array(Bytes);
            let Hits = 0, Reused = 0;
            for (let I = 0; I < F.length; I += PathPixelBytes/4) {
                if (!U[I+19]) continue; Hits++;
                assert.ok(F[I+23] >= 1 && Number.isFinite(F[I+26]), "finite positive M and finite reservoir W");
                if (F[I+23] > 1.5) Reused++;
                for (const Offset of [28,29,30,32,33,34]) assert.ok(F[I+Offset] >= 0 && Number.isFinite(F[I+Offset]), "finite nonnegative path radiance");
            }
            assert.ok(Hits > 10, "real triangle rays must hit the scene");
            assert.ok(Reused > 0, "reservoir reuse must actually execute");
            const Geometry = new Float32Array(await ReadBuffer(Device,P.Acceleration.Geometry));
            const Previous = new Float32Array(await ReadBuffer(Device,P.Acceleration.PreviousTriangles));
            let Shredded = 0, TabletMoved = 0;
            P.Acceleration.Data.Triangles.forEach(({ Facet, Owner }, I) => {
                const Offset = P.Acceleration.Data.TriangleOffset + I*24;
                if (Owner === 0 && Facet.Family === CourseMaterial.TabletHousing) {
                    if (Math.hypot(...[0,1,2].map((A) => Previous[I*24+A]-Geometry[Offset+A]))>0.001) TabletMoved++;
                }
                if (Owner !== 1 || Facet.Family !== CourseMaterial.Tyre) return;
                for (const C of [0,4,8]) for (let Axis=0; Axis<3; ++Axis) assert.equal(Geometry[Offset+C+Axis],0,"RT rubber deforms exactly like shredded raster rubber");
                Shredded++;
            });
            assert.ok(Shredded > 100);
            assert.ok(TabletMoved > 0,"previous deformed tablet vertices, not current vertices, must drive temporal reprojection");
            console.log(`executed GPU checks: ${Hits} primary hits, ${Reused} reused reservoirs, ${Shredded} collapsed rubber triangles, ${TabletMoved} tablet facets retain previous deformation`);
        }
        console.log("triangle path tracing integration: stress activation, actual fracture triangles, tyre shredding, HMI, camera restart and resize exercised");
    }
};
