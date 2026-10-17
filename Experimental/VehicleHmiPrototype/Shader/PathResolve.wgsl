//! include "PathRecords.wgsl"
//! include "PackingCodec.wgsl"
@group(0) @binding(2) var<storage,read> Pixels: array<PathPixel>;
@group(0) @binding(3) var FilteredSignal: texture_2d<f32>;
@group(0) @binding(4) var RawSignal: texture_2d<f32>;
@group(0) @binding(5) var RasterAttributes: texture_2d<f32>;
@group(0) @binding(6) var RasterDepth: texture_depth_2d;
@group(0) @binding(7) var RasterDirect: texture_2d<f32>;
@group(0) @binding(8) var<storage,read> Materials: array<MaterialRecord>;
@group(0) @binding(9) var ScreenTarget: texture_storage_2d<rgba16float,write>;
fn PathHeat(X: f32) -> vec3f { return clamp(vec3f(1.5-abs(4.0*X-3.0),1.5-abs(4.0*X-2.0),1.5-abs(4.0*X-1.0)),vec3f(0.0),vec3f(1.0)); }
@compute @workgroup_size(8,8)
fn ResolvePathScreen(@builtin(global_invocation_id) Id: vec3u) {
    let Pixel=vec2i(Id.xy); if (any(vec2f(Pixel)>=Frame.ScreenExtent)) { return; }
    let Extent=vec2i(Frame.TraceExtent); let Position=vec2f(Pixel)/f32(Frame.TraceScale);
    let Corner=vec2i(floor(Position)); let Fraction=fract(Position);
    let Depth=textureLoad(RasterDepth,Pixel,0); let Attr=textureLoad(RasterAttributes,Pixel,0);
    let RasterNormal=DecodeOctahedral(Attr.xy);
    let ViewDepth=select(0.0,ResolveViewDepth(ReconstructWorldPosition(vec2f(Pixel)+0.5,max(Depth,1.0e-9))),Depth>0.0);
    var Sum=vec3f(0.0); var Total=0.0; var Nearest=vec3f(0.0); var NearestDistance=1.0e30;
    var Selected: PathPixel; var SelectedScore=1.0e30;
    for (var Tap=0u; Tap<4u; Tap++) {
        let Offset=vec2i(i32(Tap&1u),i32(Tap>>1u)); let P=clamp(Corner+Offset,vec2i(0),Extent-vec2i(1));
        let Record=Pixels[u32(P.y)*u32(Extent.x)+u32(P.x)]; let S=Record.Surface;
        let C=textureLoad(FilteredSignal,P,0).rgb;
        let Bilinear=select(1.0-Fraction.x,Fraction.x,Offset.x==1)*select(1.0-Fraction.y,Fraction.y,Offset.y==1);
        let Difference=abs(S.Depth-ViewDepth)/max(ViewDepth,0.01);
        var Edge=exp(-Difference*60.0)*pow(max(0.0,dot(S.Normal,RasterNormal)),32.0);
        if (S.Hit==0u && Depth<=0.0) { Edge=1.0; }
        // Raster guides deliberately omit glazing; transport does NOT. Do not
        // reject a correctly traced glass surface using the opaque guide behind.
        if (S.Family==VehicleGlassFamily && S.Hit!=0u) { Edge=1.0; }
        if (Frame.TraceScale==1u) { Edge=1.0; }
        let Weight=(Bilinear+0.00001)*Edge; Sum+=C*Weight; Total+=Weight;
        let Distance=length(vec2f(P)-Position);
        if (Distance<NearestDistance) { NearestDistance=Distance; Nearest=C; }
        let Score=Distance+select(0.0,Difference,Depth>0.0 && S.Family!=VehicleGlassFamily);
        if (Score<SelectedScore) { SelectedScore=Score; Selected=Record; }
    }
    var Colour=Nearest; if (Total>0.00001) { Colour=Sum/Total; }
    let S=Selected.Surface; let TracePixel=clamp(vec2i(round(Position)),vec2i(0),Extent-vec2i(1));
    // GPU particle cubes are an intentional game/VFX overlay, not proxy scene
    // geometry for GI. Keep their existing lighting/emission and read-only depth.
    if (Path.Domain.w!=0u && Depth>0.0 && (S.Hit==0u || ViewDepth<S.Depth-max(0.03,0.005*S.Depth))) {
        let Material=Materials[u32(Attr.z+0.5)];
        Colour=textureLoad(RasterDirect,Pixel,0).rgb+(Material.Emission+Material.Albedo*max(0.0,Attr.w-1.0))*Frame.EmissionScale;
    }
    switch (Frame.InspectionMode) {
        case 1u: { Colour=Selected.Direct.rgb; }
        case 2u: { Colour=max(vec3f(0.0),Colour-Selected.Direct.rgb); }
        case 3u: { }
        case 4u: { Colour=textureLoad(RawSignal,TracePixel,0).rgb; }
        case 5u: { Colour=max(vec3f(0.0),Colour-Selected.Direct.rgb); }
        case 8u: { Colour=S.Normal*0.5+0.5; }
        case 9u: { Colour=select(vec3f(0.0),Materials[S.Family].Albedo,S.Hit!=0u); }
        case 10u: { Colour=PathHeat(log2(1.0+Selected.Reservoir.M)/8.0); }
        case 11u: { Colour=PathHeat(clamp(log2(1.0+Selected.Reservoir.W)/12.0,0.0,1.0)); }
        case 12u: { Colour=vec3f(fract(f32(S.Instance)*0.6180339),fract(f32(S.Instance)*0.381966),fract(f32(S.Instance)*0.2113248)); }
        default: { }
    }
    // Display attachment is float16. Keep raw radiance/moments in float32 and
    // saturate only at the display boundary, rather than emitting half infinities.
    textureStore(ScreenTarget,Pixel,vec4f(min(PathSafeColour(Colour),vec3f(65000.0)),1.0));
}
