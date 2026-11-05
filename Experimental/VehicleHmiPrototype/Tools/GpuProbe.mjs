//============================================================================================================================================
//                                                              GPUPROBE.MJS
//============================================================================================================================================
// 🧩 Reads GPU textures and buffers back to the CPU under Node, for tests and diagnostics that need real numbers (needs an executing backend:
//    RunOnSoftwareGpu.mjs). Half-float and float textures are returned as Float32Array in RGBA order.

export function HalfToFloat(Half)
{
    const Sign = (Half & 0x8000) ? -1 : 1, Exponent = (Half >> 10) & 0x1F, Fraction = Half & 0x3FF;
    if (Exponent === 0) return Sign * Math.pow(2, -14) * (Fraction / 1024);
    if (Exponent === 31) return Fraction ? NaN : Sign * Infinity;
    return Sign * Math.pow(2, Exponent - 15) * (1 + Fraction / 1024);
}

const BytesPerTexel = { rgba16float: 8, rg16float: 4, r16float: 2, r32float: 4, rgba32float: 16, rgba8unorm: 4, bgra8unorm: 4 };

export async function ReadTexture(Device, Texture, Width, Height, Format = Texture.format, MipLevel = 0)
{
    const Texel = BytesPerTexel[Format];
    if (!Texel) throw new Error("ReadTexture: unsupported format " + Format);
    const RowBytes = Math.ceil(Width * Texel / 256) * 256;
    const Buffer = Device.createBuffer({ size: RowBytes * Height, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyTextureToBuffer({ texture: Texture, mipLevel: MipLevel }, { buffer: Buffer, bytesPerRow: RowBytes }, [Width, Height]);
    Device.queue.submit([Encoder.finish()]);
    await Buffer.mapAsync(GPUMapMode.READ);
    const Bytes = new Uint8Array(Buffer.getMappedRange().slice(0));
    Buffer.unmap(); Buffer.destroy();
    const Channels = Texel / (Format.includes("16") ? 2 : Format.includes("32") ? 4 : 1);
    const Out = new Float32Array(Width * Height * 4);
    const View = new DataView(Bytes.buffer);
    for (let Y = 0; Y < Height; ++Y)
        for (let X = 0; X < Width; ++X)
            for (let C = 0; C < 4; ++C)
            {
                let Value = C === 3 && Channels < 4 ? 1.0 : 0.0;
                if (C < Channels)
                {
                    const At = Y * RowBytes + X * Texel;
                    if (Format.includes("16float")) Value = HalfToFloat(View.getUint16(At + C * 2, true));
                    else if (Format.includes("32float")) Value = View.getFloat32(At + C * 4, true);
                    else Value = View.getUint8(At + C) / 255.0;
                }
                Out[(Y * Width + X) * 4 + C] = Value;
            }
    return Out;
}

export async function ReadBuffer(Device, Buffer, Size = Buffer.size)
{
    const Read = Device.createBuffer({ size: Size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const Encoder = Device.createCommandEncoder();
    Encoder.copyBufferToBuffer(Buffer, 0, Read, 0, Size);
    Device.queue.submit([Encoder.finish()]);
    await Read.mapAsync(GPUMapMode.READ);
    const Bytes = Read.getMappedRange().slice(0);
    Read.unmap(); Read.destroy();
    return Bytes;
}

// 📝 Per-channel mean/min/max over a rectangle of an RGBA float image; counts non-finite values.
export function Summarise(Pixels, Width, Height, Region = [0, 0, Width, Height])
{
    const [X0, Y0, X1, Y1] = Region;
    const Sum = [0, 0, 0, 0], Min = [Infinity, Infinity, Infinity, Infinity], Max = [-Infinity, -Infinity, -Infinity, -Infinity];
    let Count = 0, NonFinite = 0;
    for (let Y = Y0; Y < Y1; ++Y)
        for (let X = X0; X < X1; ++X, ++Count)
            for (let C = 0; C < 4; ++C)
            {
                const Value = Pixels[(Y * Width + X) * 4 + C];
                if (!Number.isFinite(Value)) { NonFinite++; continue; }
                Sum[C] += Value; Min[C] = Math.min(Min[C], Value); Max[C] = Math.max(Max[C], Value);
            }
    return { Mean: Sum.map((S) => S / Math.max(1, Count)), Min, Max, NonFinite, Count };
}

// 📝 Every texture the host creates afterwards also gets COPY_SRC, so ReadTexture can read any target. Call it at import time of a hooks
//    module (the harness imports hooks before the host). Test-only: the production host never does this.
export async function EnableTextureReadback()
{
    // 📝 APP_ROOT (an app directory, as given to the harness) lets the probe patch another checkout's DeviceExchange, e.g. a baseline.
    const Root = process.env.APP_ROOT ? new URL("file://" + process.env.APP_ROOT.replace(/\/?$/, "/")) : new URL("../", import.meta.url);
    const { DeviceExchange } = await import(new URL("Source/DeviceExchange.js", Root).href);
    if (DeviceExchange.prototype.ReadbackEnabled) return;
    const Original = DeviceExchange.prototype.ConstructTexture;
    DeviceExchange.prototype.ConstructTexture = function (Descriptor)
    {
        return Original.call(this, { ...Descriptor, usage: Descriptor.usage | GPUTextureUsage.COPY_SRC });
    };
    DeviceExchange.prototype.ReadbackEnabled = true;
}
