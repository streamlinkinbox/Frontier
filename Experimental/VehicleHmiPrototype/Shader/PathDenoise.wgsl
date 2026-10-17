//! include "PathRecords.wgsl"
@group(0) @binding(2) var<storage,read> CurrentPixels: array<PathPixel>;
@group(0) @binding(3) var<storage,read> PreviousPixels: array<PathPixel>;
@group(0) @binding(4) var RawSignal: texture_2d<f32>;
@group(0) @binding(5) var PriorSignal: texture_2d<f32>;
@group(0) @binding(6) var PriorMoments: texture_2d<f32>;
@group(0) @binding(7) var IntegratedTarget: texture_storage_2d<rgba32float,write>;
@group(0) @binding(8) var MomentTarget: texture_storage_2d<rgba32float,write>;
fn PathDenoiseCompatible(S: PathSurface, P: PathSurface) -> bool {
    if (S.Hit!=P.Hit || S.Instance!=P.Instance || S.Family!=P.Family) { return false; }
    if (S.Hit==0u) { return dot(S.Normal,P.Normal)>0.999; }
    return S.PriorDepth>0.0 && dot(S.Normal,P.Normal)>0.9
        && abs(P.Depth-S.PriorDepth)<max(0.015,0.025*S.PriorDepth)
        && length(P.LocalPosition-S.LocalPosition)<0.04+S.Depth*0.01;
}
@compute @workgroup_size(8,8)
fn IntegratePathHistory(@builtin(global_invocation_id) Id: vec3u) {
    let Pixel=vec2i(Id.xy); let Extent=vec2i(Frame.TraceExtent);
    if (any(Pixel>=Extent)) { return; }
    let Index=Id.y*u32(Extent.x)+Id.x; let S=CurrentPixels[Index].Surface;
    let Signal=textureLoad(RawSignal,Pixel,0).rgb; let Lum=ResolveLuminance(Signal);
    let Screen=min(Id.xy*Frame.TraceScale,vec2u(Frame.ScreenExtent)-vec2u(1u));
    let Prior=PathPriorTracePixel(Screen,S.Motion);
    let Corner=vec2i(floor(Prior)); let Fraction=fract(Prior);
    var Weight=0.0; var Colour=vec3f(0.0); var Moments=vec3f(0.0);
    if (!FeatureEnabled(Frame,FeatureTemporalRestart)) {
        for (var Tap=0u; Tap<4u; Tap++) {
            let Offset=vec2i(i32(Tap&1u),i32(Tap>>1u)); let P=Corner+Offset;
            if (any(P<vec2i(0)) || any(P>=Extent)) { continue; }
            if (!PathDenoiseCompatible(S,PreviousPixels[u32(P.y)*u32(Extent.x)+u32(P.x)].Surface)) { continue; }
            let W=select(1.0-Fraction.x,Fraction.x,Offset.x==1)*select(1.0-Fraction.y,Fraction.y,Offset.y==1);
            Colour+=textureLoad(PriorSignal,P,0).rgb*W; Moments+=textureLoad(PriorMoments,P,0).xyz*W; Weight+=W;
        }
    }
    var N=1.0; var Result=Signal; var Moment=vec2f(Lum,Lum*Lum);
    if (Weight>0.001) {
        Colour/=Weight; Moments/=Weight;
        N=min(Moments.z+1.0,32.0); let Alpha=max(1.0/N,Path.Options.z);
        // Current-neighbourhood clipping is reconstruction only. The raw Monte
        // Carlo signal and all reservoir weights are NEVER firefly-clamped.
        var Low=Signal; var High=Signal;
        for (var Y=-1; Y<=1; Y++) { for (var X=-1; X<=1; X++) {
            let P=clamp(Pixel+vec2i(X,Y),vec2i(0),Extent-vec2i(1));
            let PS=CurrentPixels[u32(P.y)*u32(Extent.x)+u32(P.x)].Surface;
            if (PS.Instance!=S.Instance || dot(PS.Normal,S.Normal)<0.8) { continue; }
            let C=textureLoad(RawSignal,P,0).rgb; Low=min(Low,C); High=max(High,C);
        } }
        let Spread=max((High-Low)*0.25,vec3f(0.01)); Colour=clamp(Colour,Low-Spread,High+Spread);
        Result=mix(Colour,Signal,Alpha); Moment=mix(Moments.xy,Moment,max(Alpha,0.2));
    }
    let Variance=max(0.0,Moment.y-Moment.x*Moment.x)*4.0/min(N,4.0);
    textureStore(IntegratedTarget,Pixel,vec4f(PathSafeColour(Result),Variance));
    textureStore(MomentTarget,Pixel,vec4f(Moment,N,0.0));
}
