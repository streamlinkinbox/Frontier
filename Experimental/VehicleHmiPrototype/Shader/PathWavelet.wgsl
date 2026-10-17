//! include "PathRecords.wgsl"
@group(0) @binding(2) var<storage,read> Pixels: array<PathPixel>;
@group(0) @binding(3) var InputSignal: texture_2d<f32>;
@group(0) @binding(4) var Moments: texture_2d<f32>;
@group(0) @binding(5) var<uniform> Filter: vec4u;
@group(0) @binding(6) var FilteredTarget: texture_storage_2d<rgba32float,write>;
@compute @workgroup_size(8,8)
fn FilterPathWavelet(@builtin(global_invocation_id) Id: vec3u) {
    let Pixel=vec2i(Id.xy); let Extent=vec2i(Frame.TraceExtent);
    if (any(Pixel>=Extent)) { return; }
    let S=Pixels[Id.y*u32(Extent.x)+Id.x].Surface; let Centre=textureLoad(InputSignal,Pixel,0);
    let Lum=ResolveLuminance(Centre.rgb);
    // Stable 3x3 variance estimate while the moment history is short.
    var Variance=0.0; var VarianceWeight=0.0; var LocalMean=0.0; var LocalSquare=0.0;
    for (var Y=-1; Y<=1; Y++) { for (var X=-1; X<=1; X++) {
        let P=clamp(Pixel+vec2i(X,Y),vec2i(0),Extent-vec2i(1));
        let Q=Pixels[u32(P.y)*u32(Extent.x)+u32(P.x)].Surface;
        if (Q.Instance!=S.Instance || Q.Family!=S.Family || dot(Q.Normal,S.Normal)<0.8) { continue; }
        let Sample=textureLoad(InputSignal,P,0); let L=ResolveLuminance(Sample.rgb);
        Variance+=Sample.a; LocalMean+=L; LocalSquare+=L*L; VarianceWeight+=1.0;
    } }
    let Count=max(VarianceWeight,1.0); Variance/=Count;
    let HistoryLength=textureLoad(Moments,Pixel,0).z;
    if (HistoryLength<4.0) {
        // With one sample mu2 - mu1^2 is exactly zero. Without spatial
        // bootstrapping the very first frame would reject all noisy neighbours.
        let SpatialVariance=max(0.0,LocalSquare/Count-(LocalMean/Count)*(LocalMean/Count));
        Variance=max(Variance,SpatialVariance*4.0/max(1.0,HistoryLength));
    }
    let Sigma=4.0*sqrt(max(Variance,0.0))+0.001;
    let Kernel=array<f32,3>(0.375,0.25,0.0625);
    var Sum=vec3f(0.0); var SumVariance=0.0; var Total=0.0;
    for (var Y=-2; Y<=2; Y++) { for (var X=-2; X<=2; X++) {
        let P=Pixel+vec2i(X,Y)*i32(Filter.x);
        if (any(P<vec2i(0)) || any(P>=Extent)) { continue; }
        let Q=Pixels[u32(P.y)*u32(Extent.x)+u32(P.x)].Surface;
        if (Q.Hit!=S.Hit || Q.Family!=S.Family) { continue; }
        let C=textureLoad(InputSignal,P,0); let Normal=pow(max(0.0,dot(Q.Normal,S.Normal)),64.0);
        // Plane distance, not raw view-z difference: a receding flat road has
        // a steep depth gradient but must still filter across adjacent rows.
        // Normals/materials and point-to-plane error stop at actual boundaries.
        let PlaneError=abs(dot(Q.Position-S.Position,S.Normal));
        let Depth=exp(-PlaneError/max(0.003,0.0025*S.Depth*f32(Filter.x)));
        let Colour=exp(-abs(ResolveLuminance(C.rgb)-Lum)/Sigma);
        let W=Kernel[u32(abs(X))]*Kernel[u32(abs(Y))]*Normal*Depth*Colour;
        Sum+=C.rgb*W; SumVariance+=C.a*W*W; Total+=W;
    } }
    textureStore(FilteredTarget,Pixel,vec4f(Sum/max(Total,1.0e-20),SumVariance/max(Total*Total,1.0e-20)));
}
