// Read the actual rgba16float GPU sky texture, never synthesize a substitute chart.
// One small asynchronous copy per changed LUT, only while the inspector is drawn.
export function DecodeHalf(Value) {
    const Sign = Value & 0x8000 ? -1 : 1;
    const Exponent = (Value >> 10) & 31, Mantissa = Value & 1023;
    if (Exponent === 31) return Mantissa ? NaN : Sign * Infinity;
    return Sign * (Exponent ? (1 + Mantissa/1024) * 2 ** (Exponent-15) : Mantissa * 2 ** -24);
}
export class SkyLutPreview {
    constructor(Atmosphere, Options={}) { this.Mode=Options.Mode??'Raw'; this.Atmosphere=Atmosphere; this.Key=''; this.Status='Waiting for sky LUT'; this.Busy=false; this.Exposure=1; }
    async Refresh() {
        const A=this.Atmosphere;
        const IsTrans=this.Mode==='TransLut', IsMulti=this.Mode==='MultiLut', IsTransmission=this.Mode==='Transmission';
        const Texture=IsTrans?A.TransmittanceTexture:IsMulti?A.MultipleTexture:IsTransmission?A.SkyTransmissionTexture:A.SkyTexture;
        const Width=IsTrans?256:IsMulti?32:192, Height=IsTrans?64:IsMulti?32:108;
        const Stride=Math.ceil(Width*8/256)*256;
        const Key=(IsTrans||IsMulti?A.LastMediumKey:A.LastKey)+this.Mode;
        if (this.Busy || !Key || Key===this.Key || this.FailedKey===Key) return;
        this.Busy=true;
        let Buffer;
        try {
            const Device=A.Exchange.Device;
            Buffer=Device.createBuffer({label:'Sky LUT inspector readback',size:Stride*Height,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
            const Encoder=Device.createCommandEncoder({label:'Sky LUT inspector copy'});
            Encoder.copyTextureToBuffer({texture:Texture},{buffer:Buffer,bytesPerRow:Stride,rowsPerImage:Height},[Width,Height,1]);
            Device.queue.submit([Encoder.finish()]);
            await Buffer.mapAsync(GPUMapMode.READ);
            const Half=new Uint16Array(Buffer.getMappedRange());
            const Pixels=new Float32Array(Width*Height*3);
            for(let Y=0;Y<Height;Y++) for(let X=0;X<Width;X++) for(let C=0;C<3;C++) Pixels[(Y*Width+X)*3+C]=Math.max(0,DecodeHalf(Half[Y*Stride/2+X*4+C]));
            this.Pixels=Pixels; this.Width=Width;this.Height=Height;this.Key=Key;this.Status=`GPU ${this.Mode} · ${Width} × ${Height} · RGBA16F`;
        } catch(Error) { this.FailedKey=Key; this.Status=`Preview unavailable: ${Error.message}`; }
        finally { if(Buffer) Buffer.destroy(); this.Busy=false; }
    }
    Draw(Context,Width,Height) {
        void this.Refresh();
        if(this.Pixels && (this.ImageKey!==this.Key || this.ImageExposure!==this.Exposure)) {
            this.Canvas ??= document.createElement('canvas');
            const Panorama=['Profile','Panorama','Sphere'].includes(this.Mode);
            const W=Panorama?640:this.Width,H=Panorama?320:this.Height;
            this.Canvas.width=W;this.Canvas.height=H;
            const C=this.Canvas.getContext('2d'), Image=C.createImageData(W,H);
            for(let Y=0;Y<H;Y++)for(let X=0;X<W;X++) {
                let SX=X,SY=Y;
                if(Panorama){
                    const Elevation=this.Mode==='Sphere'?Math.PI/2-(Y+.5)/H*Math.PI:Math.PI/2-(Y+.5)/H*Math.PI/2;
                    const Sun=this.Atmosphere.Light.SunDirection;
                    const U=this.Mode==='Profile'?(Math.atan2(Sun[1],Sun[0])/(Math.PI*2)+1)%1:(X+.5)/W;
                    const V=.5-.5*Math.sign(Elevation)*Math.sqrt(Math.abs(Elevation)/(Math.PI/2));
                    SX=Math.min(this.Width-1,Math.floor(U*this.Width));SY=Math.min(this.Height-1,Math.floor(V*this.Height));
                }
                for(let K=0;K<3;K++) {
                    const Value=this.Pixels[(SY*this.Width+SX)*3+K];
                    const Linear=['Transmission','TransLut'].includes(this.Mode)?Math.min(1,Value):Math.max(0,Value)*2**this.Exposure;
                    const Mapped=['Transmission','TransLut'].includes(this.Mode)?Linear:Linear/(1+Linear);
                    Image.data[(Y*W+X)*4+K]=Math.round(255*Mapped**(1/2.2));
                }
                Image.data[(Y*W+X)*4+3]=255;
            }
            C.putImageData(Image,0,0);this.ImageKey=this.Key;this.ImageExposure=this.Exposure;
        }
        if(this.Canvas) Context.drawImage(this.Canvas,0,0,Width,Height-32);
        Context.fillStyle='#b7bfba';Context.font='14px sans-serif';Context.fillText(this.Status,10,Height-10);
    }
}
