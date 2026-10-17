import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { ThreadBoundingStructure, ConstructRayScene, RayNodeBytes, RayTriangleBytes } from "../Source/RayTracingScene.js";
import { ConstructIdentityTransform, ConstructRigidTransform, InvertTransform, TransformPoint, TransformDirection } from "../Source/TransformProjection.js";
import { ConstructMaterialSpecification } from "../Source/MaterialSpecification.js";
import { ResolvePathSettings, ResolvePathTraceScale, RendererModes, PathPixelBytes, PathConfigurationBytes } from "../Source/PathTracingSettings.js";
import { RandomStream, Fresh, Combine, ReconnectionFactor } from "./PathEstimatorReference.mjs";

const Near = (A, B, E = 1e-6) => assert.ok(Math.abs(A - B) <= E, `${A} != ${B} (tolerance ${E})`);
function BoxHit(O, D, A, B, Limit) {
    let Low = 0, High = Limit;
    for (let I = 0; I < 3; ++I) {
        if (Math.abs(D[I]) < 1e-30) { if (O[I] < A[I] || O[I] > B[I]) return false; }
        else { const T0 = (A[I] - O[I]) / D[I], T1 = (B[I] - O[I]) / D[I]; Low = Math.max(Low, Math.min(T0, T1)); High = Math.min(High, Math.max(T0, T1)); }
    }
    return Low <= High;
}
const Sub = (A, B) => A.map((V, I) => V - B[I]);
const Dot = (A, B) => A.reduce((Sum, V, I) => Sum + V * B[I], 0);
const Cross = (A, B) => [A[1]*B[2]-A[2]*B[1], A[2]*B[0]-A[0]*B[2], A[0]*B[1]-A[1]*B[0]];
function TriangleHit(O, D, F, Limit) {
    const [A, B, C] = F.Corners, E1 = Sub(B, A), E2 = Sub(C, A);
    const P = Cross(D, E2), Det = Dot(E1, P);
    if (Math.abs(Det) < 1e-20) return Limit;
    const V = Sub(O, A), U = Dot(V, P) / Det, Q = Cross(V, E1), W = Dot(D, Q) / Det;
    const T = Dot(E2, Q) / Det;
    return U >= 0 && W >= 0 && U + W <= 1 && T > 0 && T < Limit ? T : Limit;
}
function Traverse(Tree, O, D) {
    let T = 1e30, I = 0, Steps = 0;
    while (I < Tree.Nodes.length) {
        assert.ok(++Steps <= Tree.Nodes.length, "thread cycle / repeated node");
        const N = Tree.Nodes[I];
        if (!BoxHit(O, D, N.Minimum, N.Maximum, T)) { I = N.Count ? I + 1 : N.Link; continue; }
        if (N.Count) for (let J = 0; J < N.Count; ++J) T = TriangleHit(O, D, Tree.Facets[N.Link + J], T);
        I++;
    }
    return T;
}

test("ray tracing is the default; GTX GI and a genuinely independent reference mode remain explicit", () => {
    assert.equal(ResolvePathSettings(new URLSearchParams()).Renderer, RendererModes.RayTracing);
    assert.equal(ResolvePathSettings(new URLSearchParams("renderer=distance")).Renderer, RendererModes.DistanceField);
    assert.equal(ResolvePathSettings(new URLSearchParams("renderer=reference")).Renderer, RendererModes.Reference);
    const S = ResolvePathSettings(new URLSearchParams("bounces=NaN&candidates=-100&seed=Infinity"));
    assert.equal(S.PathBounces, 6); assert.equal(S.PathCandidates, 1); assert.equal(S.PathSeed, 173);
    assert.equal(ResolvePathSettings(new URLSearchParams("bounces=999")).PathBounces, 16);
});

test("trace targets respect per-binding, max-buffer AND total storage budget, including full-resolution requests", () => {
    const Limits = { maxStorageBufferBindingSize: 128 * 1024**2, maxBufferSize: 256 * 1024**2 };
    assert.equal(ResolvePathTraceScale([1920,1080], 1, Limits), 2);
    assert.equal(ResolvePathTraceScale([1280,720], 1, Limits), 2);
    assert.equal(ResolvePathTraceScale([640,360], 1, Limits), 1);
    assert.equal(ResolvePathTraceScale([1920,1080], 2, Limits, 64), 8);
    assert.throws(() => ResolvePathTraceScale([16384,16384], 1, Limits, 32), /exceed/);
    assert.throws(() => ResolvePathTraceScale([1920,1080], 8, Limits, 32), /exceed/,"full-resolution output is not free at 1/8 trace scale");
    assert.equal(PathPixelBytes, 144); assert.equal(PathConfigurationBytes, 128);
    assert.equal(RayNodeBytes, 32); assert.equal(RayTriangleBytes, 96);
});

test("threaded SAH traversal agrees with brute-force actual triangles; no fixed stack or step cutoff", () => {
    const Random = RandomStream(391), Facets = [];
    for (let I = 0; I < 600; ++I) {
        const Centre = [0,0,0].map(() => Random()*30-15);
        Facets.push({ Family: I % 3, Corners: [0,1,2].map(() => Centre.map((V) => V + Random()*3-1.5)) });
    }
    Facets.push({ Family: 0, Corners: [[-20,-20,0],[20,-20,0],[0,20,0]] });
    const Tree = ThreadBoundingStructure(Facets);
    assert.equal(Tree.Facets.length, Facets.length);
    assert.equal(new Set(Tree.Facets).size, Facets.length);
    for (let I = 0; I < Tree.Nodes.length; ++I) {
        const N = Tree.Nodes[I];
        if (!N.Count) { assert.ok(N.Link > I + 1 && N.Link <= Tree.Nodes.length); }
        else assert.ok(N.Link + N.Count <= Facets.length);
    }
    for (let I = 0; I < 1200; ++I) {
        const O = [0,0,0].map(() => Random()*40-20), D = [0,0,0].map(() => Random()*2-1);
        if (I % 8 === 0) D[I % 3] = 0;
        const Expected = Facets.reduce((T, F) => TriangleHit(O,D,F,T), 1e30);
        Near(Traverse(Tree,O,D), Expected, Expected === 1e30 ? 0 : 3e-4);
    }
    Near(Traverse(ThreadBoundingStructure([Facets.at(-1)]), [0,0,2], [0,0,-1]), 2);
    assert.equal(Traverse(ThreadBoundingStructure([Facets.at(-1)]), [40,0,0], [0,0,1]), 1e30);
});

test("unnormalized local ray directions preserve world t under nonuniform and negative instance scales", () => {
    const M = ConstructIdentityTransform(); M[0] = -3; M[5] = 0.5; M[10] = 7; M[12] = 10;
    const Inverse = InvertTransform(M);
    const O = [10,0,20], D = [0,0,-1];
    const LocalO = TransformPoint(Inverse, O), LocalD = TransformDirection(Inverse,D);
    const F = { Corners: [[-1,-1,0],[1,-1,0],[0,1,0]] };
    Near(TriangleHit(LocalO,LocalD,F,1e30),20,1e-5);
});

function FixtureScene() {
    const Facets = [{ Family: 0, Corners: [[-1,-1,0],[1,-1,0],[0,1,0]] }];
    const Scene = { FacetSets: [{ Facets }], Occupants: [] };
    for (let I = 0; I < 7; ++I) Scene.Occupants.push({ FacetSet: 0, Roles: 1, MaterialOverride: -1,
        Transform: ConstructRigidTransform([I*4,0,0],[0,0,0,1]), Inactive: I === 6 });
    Scene.FacetSets.push({ Facets: [], Composite: true });
    Scene.Occupants.push({ FacetSet: 1, Roles: 8, Fragment: true, RayFacets: Facets,
        RayGeometryVersion: 1, MaterialOverride: -1, Transform: ConstructIdentityTransform() });
    return Scene;
}
test("real fragment slots and raster-only level geometry enter RT; inactive slots/card-only ground tiles do not", () => {
    const Scene = FixtureScene(), Materials = ConstructMaterialSpecification(), Cache = new WeakMap();
    Scene.Occupants.push({ ...Scene.Occupants[5], Roles: 4 });
    const Data = ConstructRayScene(Scene,Materials,Cache);
    assert.deepEqual(Data.Active.map((I) => I.Index),[0,1,2,3,4,5,7]);
    assert.equal(Data.DomainTriangleCount,7); assert.equal(Data.DynamicTriangles,5);
    assert.equal(Data.Domain.at(-1).Prefix,7);
    for (const Instance of Data.Active) {
        const W = Data.InstanceOffset+Instance.Index*8, U = new Uint32Array(Data.Bytes);
        assert.equal(U[W],Instance.Mesh.Root); assert.equal(U[W+3],Instance.Mesh.Count);
    }
    Scene.Occupants[6].Inactive = false;
    const Updated = ConstructRayScene(Scene,Materials,Cache);
    assert.equal(Updated.DomainTriangleCount,8);
    assert.equal(Updated.Triangles.length,Data.Triangles.length);
    assert.deepEqual(Updated.Triangles,Data.Triangles,"stable canonical triangle identities across activity changes");
});

test("canonical area-fraction reconnection includes inverse square, endpoint cosine AND changing world area", () => {
    Near(ReconnectionFactor([0,0,0],[0,0,2],[0,0,-1],3),0.75);
    Near(ReconnectionFactor([0,0,0],[0,0,4],[0,0,-1],3),0.1875);
    Near(ReconnectionFactor([0,0,0],[0,0,2],[0,0,-1],6),1.5);
    Near(ReconnectionFactor([0,0,0],[0,0,2],[1,0,0],3),0);
});

test("generalized RIS remains correct when different source PDFs, zero-radiance visibility, temporal M caps and spatial reuse are combined", () => {
    const Random = RandomStream(71), N = 100000;
    const PdfA = [0.70,0.10,0.15,0.05], PdfB = [0.05,0.55,0.15,0.25];
    const A = [2,0.1,0.1,0.4], B = [0.1,1,0.3,0.1]; // positive support, INCLUDING black samples
    const F = [0,1.5,0,0.5], Dest = F.map((X) => Math.max(X,0.1));
    let Sum = 0, Sum2 = 0;
    for (let I = 0; I < N; ++I) {
        const Old = Combine([Fresh(PdfA,A,3,Random),Fresh(PdfB,B,2,Random)],A,Random);
        const Temporal = Combine([Fresh(PdfB,Dest,1,Random),Old],Dest,Random,2);
        const Spatial = Combine([Temporal,Fresh(PdfA,A,2,Random)],Dest,Random);
        const Value = F[Spatial.Y]*Spatial.W;
        Sum += Value; Sum2 += Value*Value;
    }
    const Mean = Sum/N, StandardError = Math.sqrt(Math.max(0,Sum2/N-Mean*Mean)/N);
    Near(Mean,2,Math.max(0.025,6*StandardError));
});

test("trace reprojection inverts the actual sample grid and includes camera jitter and previous deformation", () => {
    const Records=fs.readFileSync(new URL("../Shader/PathRecords.wgsl",import.meta.url),"utf8");
    const Sampling=fs.readFileSync(new URL("../Shader/PathResampling.wgsl",import.meta.url),"utf8");
    const Scene=fs.readFileSync(new URL("../Shader/PathScene.wgsl",import.meta.url),"utf8");
    const Temporal=fs.readFileSync(new URL("../Shader/PathDenoise.wgsl",import.meta.url),"utf8");
    for (const Scale of [1,2,4,8]) for (const Index of [0,3,19,72]) {
        const Screen=Index*Scale, CurrentJitter=0.375, PriorJitter=-0.25, RigidMotion=2.3;
        const CurrentUnjittered=Screen+0.5-CurrentJitter;
        const PriorScreen=CurrentUnjittered-RigidMotion+PriorJitter;
        const Motion=RigidMotion+CurrentJitter-PriorJitter;
        Near((Screen-Motion)/Scale,(PriorScreen-0.5)/Scale);
        Near((Screen-0)/Scale,Index); // stationary, unjittered point cannot drift
    }
    assert.match(Records,/return \(vec2f\(Screen\)-Motion\)\/f32\(Frame.TraceScale\)/);
    assert.match(Sampling,/PathPreviousLocalPosition\(Hit\)/);
    assert.match(Sampling,/Path\.JitterDelta\.xy/);
    assert.match(Sampling,/PathPriorTracePixel\(Screen,S.Motion\)/);
    assert.match(Temporal,/PathPriorTracePixel\(Screen,S.Motion\)/);
    assert.match(Scene,/PreviousVehicleTriangles\[Word/);
});

test("WGSL and host preserve spatial histories and keep transport separate from legacy cabin/split-sum approximations", () => {
    const Load = (Name) => fs.readFileSync(new URL(Name,import.meta.url),"utf8");
    const Shader = Load("../Shader/PathResampling.wgsl"), Transport = Load("../Shader/PathTransport.wgsl");
    assert.match(Shader,/PathTraceSuffix\(Secondary/); assert.match(Shader,/TargetPixels\[Index\]=Pixel/);
    assert.match(Shader,/Area\*abs\(dot\(Secondary.GeometricNormal,-Direction\)\)\/D2/);
    assert.match(Transport,/PathSampleVndf/); assert.match(Transport,/Survival/); assert.match(Transport,/PathMis/);
    assert.doesNotMatch(Transport,/ResolveEnvironmentResponse|CabinIndirectShare|SampleCard|DistanceTraversal/);
    const Host = Load("../Source/ReSTIRPathTracer.js");
    assert.match(Host,/this\.Scratch, this\.Histories\[P\]/);
    assert.match(Host,/this\.Histories\[1 - P\], this\.Scratch/);
});
