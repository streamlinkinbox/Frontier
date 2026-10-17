//! include "PathTransport.wgsl"
@group(0) @binding(2) var<storage,read> SourcePixels: array<PathPixel>;
@group(0) @binding(3) var<storage,read_write> TargetPixels: array<PathPixel>;
@group(0) @binding(4) var RawPathRadiance: texture_storage_2d<rgba32float,write>;
struct PathEvaluation { Value: vec3f, Target: f32, Proposal: f32, }
fn PathPrimaryHit(S: PathSurface) -> PathHit {
    var Hit: PathHit; Hit.Position=S.Position; Hit.LocalPosition=S.LocalPosition; Hit.Normal=S.Normal;
    Hit.Instance=S.Instance; Hit.Triangle=S.Triangle; Hit.Family=S.Family;
    let T=PathTriangleAt(S.Triangle); let O=Occupants[S.Instance];
    let Ng=normalize((transpose(O.WorldToLocal)*vec4f(cross(T.B-T.A,T.C-T.A),0.0)).xyz);
    Hit.GeometricNormal=select(-Ng,Ng,dot(Ng,S.View)>=0.0); return Hit;
}
fn PathSurfaceAt(Pixel: vec2u) -> PathSurface {
    let Screen=min(Pixel*Frame.TraceScale,vec2u(Frame.ScreenExtent)-vec2u(1u));
    let Point=vec2f(Screen)+0.5; let Far=ReconstructWorldPosition(Point,1.0e-6);
    let Direction=normalize(Far-Frame.CameraPosition);
    let Hit=PathTrace(Frame.CameraPosition,Direction,1.0e30);
    var S: PathSurface;
    S.Triangle=PathInvalid; S.Instance=PathInvalid; S.View=-Direction;
    if (Hit.Instance==PathInvalid) {
        S.Normal=Direction; S.LocalPosition=Direction;
        let Current=Frame.UnjitteredViewProjection*vec4f(Direction,0.0);
        let Prior=Frame.PreviousUnjitteredViewProjection*vec4f(Direction,0.0);
        if (Current.w>0.0 && Prior.w>0.0) { S.Motion=PathScreenPoint(Current)-PathScreenPoint(Prior)+Path.JitterDelta.xy; }
        return S;
    }
    S.Position=Hit.Position; S.Normal=Hit.Normal; S.LocalPosition=Hit.LocalPosition;
    S.Instance=Hit.Instance; S.Triangle=Hit.Triangle; S.Family=Hit.Family; S.Hit=1u;
    S.Depth=ResolveViewDepth(Hit.Position);
    let PriorPosition=Occupants[Hit.Instance].PreviousLocalToWorld*vec4f(PathPreviousLocalPosition(Hit),1.0);
    let Prior=Frame.PreviousUnjitteredViewProjection*PriorPosition;
    S.PriorDepth=Prior.w;
    if (Prior.w>0.0) { S.Motion=PathScreenPoint(Frame.UnjitteredViewProjection*vec4f(Hit.Position,1.0))-PathScreenPoint(Prior)+Path.JitterDelta.xy; }
    else { S.Motion=Frame.ScreenExtent*2.0; }
    return S;
}
fn PathNewCandidate(S: PathSurface, Rng: ptr<function,u32>, Reference: bool) -> PathReservoir {
    var R: PathReservoir; R.Triangle=PathInvalid; R.Instance=PathInvalid;
    R.Seed=PathHash(u32(PathRandom(Rng)*16777216.0)^(*Rng));
    let Choice=PathRandom(Rng);
    if (!Reference && Choice<Path.Options.y) {
        if (PathRandom(Rng)<0.25 || Path.Domain.x==0u) { R.Barycentric=EncodeOctahedral(PathUniformSphere(PathRandom2(Rng))); return R; }
        let Index=min(u32(PathRandom(Rng)*f32(Path.Domain.x)),Path.Domain.x-1u);
        var Low=0u; var High=Path.Scene.z-1u;
        while (Low<High) {
            let Mid=(Low+High)/2u;
            if (Geometry[Path.Geometry.z+Mid*2u+1u]<=Index) { Low=Mid+1u; } else { High=Mid; }
        }
        let Instance=Geometry[Path.Geometry.z+Low*2u]; let IW=PathInstanceWord(Instance);
        let Start=Geometry[Path.Geometry.z+Low*2u+1u]-Geometry[IW+3u];
        R.Instance=Instance; R.Triangle=Geometry[IW+2u]+Index-Start; R.Barycentric=PathBarycentric(Rng); return R;
    }
    let Hit=PathPrimaryHit(S); let Material=PathMaterial(Hit);
    let B=PathSampleBsdf(Material,S.Normal,S.View,false,Rng);
    if (B.Pdf<=0.0 || dot(Hit.GeometricNormal,B.Direction)<=0.0) { R.Instance=PathInvalid-1u; return R; }
    let Secondary=PathTraceRay(PathOffsetOrigin(S.Position,Hit.GeometricNormal),B.Direction,1.0e30,Hit.Instance,Hit.Triangle);
    if (Secondary.Instance==PathInvalid) { R.Barycentric=EncodeOctahedral(B.Direction); return R; }
    R.Instance=Secondary.Instance; R.Triangle=Secondary.Triangle; R.Barycentric=Secondary.Barycentric; return R;
}
// IMPORTANT: area-fraction measure, not solid angle. The factor
//     A(y) * |n_y . -wi| / distance^2
// in BOTH f and the directional proposal is the reconnection Jacobian. Local
// triangle barycentrics stay fixed on rigid/deforming instances. This also handles
// nonuniform scales without an ad-hoc distance-ratio clamp (which biases GI).
//
// A defensive, full-support proposal (20% uniform canonical geometry/environment)
// and a strictly positive target floor are intentional. Reservoirs at different
// pixels/times then share the SAME support, including occluded/black samples.
// This makes M-normalized generalized RIS valid without a biased "valid M" test.
fn PathSupportTarget(Density: f32) -> f32 {
    // Allocate a small importance mass to otherwise-black canonical samples.
    // This is a SAMPLING target, never added to radiance. An absolute 1e-12
    // floor alone technically preserves support but has catastrophic variance
    // when an occluded path becomes illuminated in a moving scene.
    let Power=max(0.01,ResolveLuminance(Frame.SunIlluminance+Frame.NightSkyRadiance)+Frame.SkyScale);
    return max(PathTargetFloor,Density*Power*0.05);
}
fn PathEvaluate(R: PathReservoir, S: PathSurface, Reference: bool) -> PathEvaluation {
    var E=PathEvaluation(vec3f(0.0),PathTargetFloor,0.0);
    if (R.Instance==PathInvalid-1u) { return E; }
    let Primary=PathPrimaryHit(S); let Material=PathMaterial(Primary);
    let Uniform=select(Path.Options.y,0.0,Reference);
    if (R.Instance==PathInvalid) {
        let Direction=DecodeOctahedral(R.Barycentric);
        E.Proposal=Uniform*0.25/(4.0*Pi); E.Target=PathSupportTarget(E.Proposal);
        let B=PathBsdfCos(Material,S.Normal,S.View,Direction);
        if (!any(B>vec3f(0.0)) || dot(Primary.GeometricNormal,Direction)<=0.0) { return E; }
        let Blocker=PathTraceRay(PathOffsetOrigin(S.Position,Primary.GeometricNormal),Direction,1.0e30,Primary.Instance,Primary.Triangle);
        if (Blocker.Instance!=PathInvalid) { return E; }
        let Pdf=PathBsdfPdf(Material,S.Normal,S.View,Direction,false);
        E.Proposal+=(1.0-Uniform)*Pdf;
        E.Value=B*(PathSky(Direction)+PathSun(Direction))*PathMis(Pdf,PathEnvironmentPdf(Direction));
    } else {
        E.Proposal=Uniform*0.75/max(1.0,f32(Path.Domain.x)); E.Target=PathSupportTarget(E.Proposal);
        if (Geometry[PathInstanceWord(R.Instance)+4u]==0u) { return E; }
        let Area=PathArea(R.Triangle,R.Instance); if (Area<=1.0e-20) { return E; }
        var Secondary=PathMakeHit(R.Instance,R.Triangle,R.Barycentric,S.Position-
            (Occupants[R.Instance].LocalToWorld*vec4f(PathTriangleAt(R.Triangle).A,1.0)).xyz);
        let Delta=Secondary.Position-S.Position; let D2=dot(Delta,Delta);
        if (D2<1.0e-12 || PathMasked(Secondary.Instance,Secondary.Position)) { return E; }
        let Direction=Delta/sqrt(D2);
        // Face orientation must use the actual sampled point, not a proxy normal.
        Secondary=PathMakeHit(R.Instance,R.Triangle,R.Barycentric,-Direction); Secondary.Distance=sqrt(D2);
        let B=PathBsdfCos(Material,S.Normal,S.View,Direction);
        if (!any(B>vec3f(0.0)) || dot(Primary.GeometricNormal,Direction)<=0.0) { return E; }
        let GeometryFactor=Area*abs(dot(Secondary.GeometricNormal,-Direction))/D2;
        if (GeometryFactor<=1.0e-20) { return E; }
        let Start=PathOffsetOrigin(S.Position,Primary.GeometricNormal);
        let End=PathOffsetOrigin(Secondary.Position,Secondary.GeometricNormal);
        let Length=length(End-Start);
        let Blocker=PathTraceRay(Start,normalize(End-Start),Length,Primary.Instance,Primary.Triangle);
        if (Blocker.Instance!=PathInvalid && !(Blocker.Instance==R.Instance && Blocker.Triangle==R.Triangle)) { return E; }
        let Pdf=PathBsdfPdf(Material,S.Normal,S.View,Direction,false);
        E.Proposal+=(1.0-Uniform)*Pdf*GeometryFactor;
        E.Value=B*GeometryFactor*PathTraceSuffix(Secondary,-Direction,Pdf,1u,R.Seed);
    }
    E.Value=PathSafeColour(E.Value); E.Target=max(E.Target,ResolveLuminance(E.Value)); return E;
}
fn PathOffer(R: ptr<function,PathReservoir>, Value: ptr<function,vec3f>, Sum: ptr<function,f32>,
             Candidate: PathReservoir, Evaluation: PathEvaluation, Weight: f32, Count: f32, Rng: ptr<function,u32>) {
    let M=(*R).M+Count; (*R).M=M;
    if (Weight<=0.0 || !PathFinite(Weight)) { return; }
    (*Sum)+=Weight;
    if (PathRandom(Rng)*(*Sum)<Weight) { (*R)=Candidate; (*R).M=M; (*R).Target=Evaluation.Target; (*Value)=Evaluation.Value; }
}
fn PathFinalize(R: ptr<function,PathReservoir>, Sum: f32) {
    if ((*R).M>0.0 && (*R).Target>0.0) { (*R).W=Sum/((*R).M*(*R).Target); } else { (*R).W=0.0; }
    if (!PathFinite((*R).W)) { (*R).W=0.0; }
}
fn PathHistoryCompatible(Current: PathSurface, Prior: PathSurface) -> bool {
    if (Current.Hit!=Prior.Hit || Current.Instance!=Prior.Instance || Current.Family!=Prior.Family) { return false; }
    if (Current.Hit==0u) { return dot(Current.Normal,Prior.Normal)>0.999; }
    return dot(Current.Normal,Prior.Normal)>0.9 && Current.PriorDepth>0.0
        && abs(Prior.Depth-Current.PriorDepth)<max(0.015,0.025*Current.PriorDepth)
        && length(Prior.LocalPosition-Current.LocalPosition)<0.04+Current.Depth*0.01;
}
@compute @workgroup_size(8,8)
fn GeneratePathReservoirs(@builtin(global_invocation_id) Id: vec3u) {
    if (any(vec2f(Id.xy)>=Frame.TraceExtent)) { return; }
    let Index=Id.y*u32(Frame.TraceExtent.x)+Id.x;
    var Pixel: PathPixel; Pixel.Surface=PathSurfaceAt(Id.xy);
    let S=Pixel.Surface; var Rng=PathHash(Index^PathHash(Frame.FrameIndex+Path.Counts.w));
    if (S.Hit==0u) {
        Pixel.Direct=vec4f(PathSky(-S.View)+PathSun(-S.View),1.0); TargetPixels[Index]=Pixel; return;
    }
    let Hit=PathPrimaryHit(S); let Material=PathMaterial(Hit);
    Pixel.Direct=vec4f(PathEmission(Hit)+PathNextEvent(Hit,S.View,Material,false,&Rng)
        +PathPrimaryTransmission(Hit,S.View,Material,PathHash(Rng^0xa341316cu)),1.0);
    var Reservoir: PathReservoir; var Selected=vec3f(0.0); var Sum=0.0;
    let Reference=Path.Domain.z!=0u;
    for (var Candidate=0u; Candidate<Path.Counts.y; Candidate++) {
        let R=PathNewCandidate(S,&Rng,Reference); let E=PathEvaluate(R,S,Reference);
        if (Reference) {
            if (E.Proposal>0.0) { Selected+=E.Value/E.Proposal/f32(Path.Counts.y); }
        } else { PathOffer(&Reservoir,&Selected,&Sum,R,E,select(0.0,E.Target/max(E.Proposal,1.0e-30),E.Proposal>0.0),1.0,&Rng); }
    }
    if (Reference) { Reservoir.W=1.0; Reservoir.M=1.0; Reservoir.Target=1.0; }
    else {
        if (FeatureEnabled(Frame,FeatureTemporalReuse) && !FeatureEnabled(Frame,FeatureTemporalRestart)) {
            let Screen=min(Id.xy*Frame.TraceScale,vec2u(Frame.ScreenExtent)-vec2u(1u));
            let PriorPoint=PathPriorTracePixel(Screen,S.Motion);
            let Corner=vec2i(floor(PriorPoint));
            // Nearest compatible of four taps. Acceptance uses PRIMARY guides,
            // never the selected candidate's visibility/weight/radiance.
            var Best=-1; var BestScore=1.0e30;
            for (var Tap=0u; Tap<4u; Tap++) {
                let P=Corner+vec2i(i32(Tap&1u),i32(Tap>>1u));
                if (any(P<vec2i(0)) || any(vec2f(P)>=Frame.TraceExtent)) { continue; }
                let J=u32(P.y)*u32(Frame.TraceExtent.x)+u32(P.x);
                if (!PathHistoryCompatible(S,SourcePixels[J].Surface)) { continue; }
                let Score=length(vec2f(P)-PriorPoint);
                if (Score<BestScore) { Best=i32(J); BestScore=Score; }
            }
            if (Best>=0) {
                let Prior=SourcePixels[u32(Best)].Reservoir; let Count=min(Prior.M,Frame.TemporalTallyLimit);
                if (Count>0.0) {
                    var E=PathEvaluation(vec3f(0.0),PathTargetFloor,0.0);
                    if (Prior.W>0.0) { E=PathEvaluate(Prior,S,false); }
                    PathOffer(&Reservoir,&Selected,&Sum,Prior,E,E.Target*Prior.W*Count,Count,&Rng);
                }
            }
        }
        PathFinalize(&Reservoir,Sum);
    }
    Pixel.Reservoir=Reservoir; Pixel.Contribution=vec4f(Selected,1.0); TargetPixels[Index]=Pixel;
}
@compute @workgroup_size(8,8)
fn SpatialPathReservoirs(@builtin(global_invocation_id) Id: vec3u) {
    if (any(vec2f(Id.xy)>=Frame.TraceExtent)) { return; }
    let Index=Id.y*u32(Frame.TraceExtent.x)+Id.x;
    var Pixel=SourcePixels[Index]; let S=Pixel.Surface;
    if (S.Hit==0u) { TargetPixels[Index]=Pixel; textureStore(RawPathRadiance,vec2i(Id.xy),Pixel.Direct); return; }
    var Rng=PathHash(Index^PathHash(Frame.FrameIndex+Path.Counts.w)^0x9e3779b9u);
    var R: PathReservoir; var Value=vec3f(0.0); var Sum=0.0;
    let Centre=Pixel.Reservoir; let E=PathEvaluation(Pixel.Contribution.rgb,Centre.Target,0.0);
    PathOffer(&R,&Value,&Sum,Centre,E,Centre.Target*Centre.W*Centre.M,Centre.M,&Rng);
    if (FeatureEnabled(Frame,FeatureSpatialReuse) && Path.Domain.z==0u) {
        for (var I=0u; I<min(Frame.SpatialCount,8u); I++) {
            let U=PathRandom2(&Rng); let Angle=U.x*2.0*Pi; let Radius=sqrt(U.y)*Frame.SpatialRadius;
            let P=vec2i(Id.xy)+vec2i(round(vec2f(cos(Angle),sin(Angle))*Radius));
            if (any(P<vec2i(0)) || any(vec2f(P)>=Frame.TraceExtent) || all(P==vec2i(Id.xy))) { continue; }
            let Neighbour=SourcePixels[u32(P.y)*u32(Frame.TraceExtent.x)+u32(P.x)]; let NS=Neighbour.Surface;
            if (NS.Hit==0u || dot(NS.Normal,S.Normal)<0.6 || abs(NS.Depth-S.Depth)>0.15*S.Depth+0.2) { continue; }
            let Other=Neighbour.Reservoir;
            if (Other.M<=0.0) { continue; }
            var Evaluation=PathEvaluation(vec3f(0.0),PathTargetFloor,0.0);
            if (Other.W>0.0) { Evaluation=PathEvaluate(Other,S,false); }
            PathOffer(&R,&Value,&Sum,Other,Evaluation,Evaluation.Target*Other.W*Other.M,Other.M,&Rng);
        }
    }
    if (Path.Domain.z==0u) { PathFinalize(&R,Sum); Pixel.Reservoir=R; Pixel.Contribution=vec4f(Value,1.0); }
    // This SPATIALLY RESAMPLED record, not the initial/temporal one, is the next
    // frame's history. No read/write aliasing of neighbour reservoirs.
    TargetPixels[Index]=Pixel;
    let Radiance=Pixel.Direct.rgb+Pixel.Contribution.rgb*Pixel.Reservoir.W*Frame.IndirectScale;
    textureStore(RawPathRadiance,vec2i(Id.xy),vec4f(PathSafeColour(Radiance),1.0));
}
