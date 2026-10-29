//============================================================================================================================================
//                                                          VERIFYGLINTPAIR.MJS
//============================================================================================================================================
// 🧩 Time two isolated glint choices against a common smooth baseline and retain exactly two actual material renders.

import {createRequire} from 'node:module';
import {readFile, writeFile, readdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {cpus, platform, release} from 'node:os';
import {execFileSync} from 'node:child_process';
const Require = createRequire(process.cwd() + '/_ClaudeScratch/tmp/GlintTools/package.json');
const Puppeteer = Require('puppeteer-core');
const Chromium = Require('@sparticuz/chromium');
const Directory = '_ClaudeScratch/build/GlintPair';
const Isolate = process.argv.find(A => A.startsWith('--isolate='))?.split('=')[1];
if (Isolate && !['Smooth', 'Cellular', 'Glinty'].includes(Isolate)) throw Error('Invalid isolated method');
const Names = Isolate ? [Isolate] : ['Smooth', 'Cellular', 'Glinty'];
const Sources = Object.fromEntries(await Promise.all([...Names, 'Coverage'].map(async N => [N, await readFile(`${Directory}/${N}.frag`, 'utf8')])));
const Browser = await Puppeteer.launch({executablePath: await Chromium.executablePath(), headless: true,
    args: [...Chromium.args, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    env: {...process.env, LD_LIBRARY_PATH: process.cwd() + '/_ClaudeScratch/tmp/NativeBrowser/lib'}});
try
{
    const Page = await Browser.newPage();
    Page.on('console', Message => console.log(Message.text()));
    const Result = await Page.evaluate(async Sources =>
    {
        const Canvas = document.createElement('canvas');
        Canvas.width = 768; Canvas.height = 384;
        const Gl = Canvas.getContext('webgl2', {antialias: false, depth: false, preserveDrawingBuffer: true});
        if (!Gl || !Gl.getExtension('EXT_color_buffer_float')) throw Error('Float WebGL2 unavailable');
        const Timer = Gl.getExtension('EXT_disjoint_timer_query_webgl2');
        if (!Timer) throw Error('No timer query; do not substitute asynchronous submission timing');
        const Debug = Gl.getExtension('WEBGL_debug_renderer_info');
        const Renderer = Debug ? Gl.getParameter(Debug.UNMASKED_RENDERER_WEBGL) : Gl.getParameter(Gl.RENDERER);
        if (!Renderer.toLowerCase().includes('swiftshader')) throw Error('Expected identified software Vulkan renderer');
        function Compile(Category, Source)
        {
            const Shader = Gl.createShader(Category); Gl.shaderSource(Shader, Source); Gl.compileShader(Shader);
            if (!Gl.getShaderParameter(Shader, Gl.COMPILE_STATUS)) throw Error(Gl.getShaderInfoLog(Shader));
            return Shader;
        }
        const Vertex = Compile(Gl.VERTEX_SHADER, `#version 300 es
precision highp float;
out vec2 Uv;
void main(){vec2 P=vec2((gl_VertexID<<1)&2,gl_VertexID&2);Uv=P;gl_Position=vec4(P*2.0-1.0,0,1);}`);
        const Target = Gl.createTexture(); Gl.bindTexture(Gl.TEXTURE_2D, Target);
        Gl.texStorage2D(Gl.TEXTURE_2D, 1, Gl.RGBA32F, 768, 384);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
        const Framebuffer = Gl.createFramebuffer(); Gl.bindFramebuffer(Gl.FRAMEBUFFER, Framebuffer);
        Gl.framebufferTexture2D(Gl.FRAMEBUFFER, Gl.COLOR_ATTACHMENT0, Gl.TEXTURE_2D, Target, 0);
        if (Gl.checkFramebufferStatus(Gl.FRAMEBUFFER) !== Gl.FRAMEBUFFER_COMPLETE) throw Error('Incomplete target');
        const Stencil = Gl.createRenderbuffer(); Gl.bindRenderbuffer(Gl.RENDERBUFFER, Stencil);
        Gl.renderbufferStorage(Gl.RENDERBUFFER, Gl.DEPTH24_STENCIL8, 768, 384);
        Gl.framebufferRenderbuffer(Gl.FRAMEBUFFER, Gl.DEPTH_STENCIL_ATTACHMENT, Gl.RENDERBUFFER, Stencil);
        if (Gl.checkFramebufferStatus(Gl.FRAMEBUFFER) !== Gl.FRAMEBUFFER_COMPLETE) throw Error('Incomplete stencil target');
        const Programs = {};
        let Coverage;

        for (const [Name, Source] of Object.entries(Sources))
        {
            const Program = Gl.createProgram(); Gl.attachShader(Program, Vertex);
            Gl.attachShader(Program, Compile(Gl.FRAGMENT_SHADER, Source)); Gl.linkProgram(Program);
            if (!Gl.getProgramParameter(Program, Gl.LINK_STATUS)) throw Error(Name + ': ' + Gl.getProgramInfoLog(Program));
            if (Name === 'Coverage') Coverage = Program; else Programs[Name] = Program;
        }
        const Records = [], Images = {};
        let Ordinal = 0;
        for (const [Width, Height, Lights, Density] of [[768,384,3,1], [256,128,1,1], [256,128,3,1], [768,384,3,10]])
        {
            Gl.viewport(0, 0, Width, Height);
            // 📝 Fixed coverage prepass gives both shaders raster-style rejection and derivative helper lanes.
            // It is common setup, excluded from the shaded-draw timestamps.
            Gl.enable(Gl.STENCIL_TEST); Gl.stencilMask(255); Gl.clearStencil(0);
            Gl.clearColor(.035, .045, .060, 1); Gl.colorMask(true, true, true, true);
            Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.STENCIL_BUFFER_BIT);
            Gl.stencilFunc(Gl.ALWAYS, 1, 255); Gl.stencilOp(Gl.KEEP, Gl.KEEP, Gl.REPLACE);
            Gl.colorMask(false, false, false, false); Gl.useProgram(Coverage); Gl.drawArrays(Gl.TRIANGLES, 0, 3);
            Gl.colorMask(true, true, true, true); Gl.stencilMask(0);
            Gl.stencilFunc(Gl.EQUAL, 1, 255); Gl.stencilOp(Gl.KEEP, Gl.KEEP, Gl.KEEP);
            const Samples = Object.fromEntries(Object.keys(Programs).map(N => [N, []]));
            for (let Repetition = -2; Repetition < 9; ++Repetition)
            {
                const Names = Object.keys(Programs), Offset = (Repetition + 2) % Names.length;
                for (let I = 0; I < Names.length; ++I)
                {
                    const Name = Names[(I + Offset) % Names.length], Program = Programs[Name];
                    Gl.useProgram(Program);
                    Gl.uniform1f(Gl.getUniformLocation(Program, 'Phase'), Ordinal++);
                    Gl.uniform1i(Gl.getUniformLocation(Program, 'LightCount'), Lights);
                    Gl.uniform1f(Gl.getUniformLocation(Program, 'DensityScale'), Density);
                    const Query = Gl.createQuery(); Gl.beginQuery(Timer.TIME_ELAPSED_EXT, Query);
                    Gl.drawArrays(Gl.TRIANGLES, 0, 3); Gl.endQuery(Timer.TIME_ELAPSED_EXT); Gl.flush();
                    let Available = false;
                    for (let Attempt = 0; Attempt < 30000; ++Attempt)
                    {
                        await new Promise(Resolve => setTimeout(Resolve, 1));
                        Available = Gl.getQueryParameter(Query, Gl.QUERY_RESULT_AVAILABLE);
                        if (Available) break;
                    }
                    if (!Available || Gl.getParameter(Timer.GPU_DISJOINT_EXT)) throw Error('Invalid timer query');
                    const Ms = Gl.getQueryParameter(Query, Gl.QUERY_RESULT) / 1e6;
                    Gl.deleteQuery(Query);
                    if (!(Ms > 0) || !Number.isFinite(Ms)) throw Error('Invalid elapsed time');
                    if (Repetition >= 0) Samples[Name].push(Ms);
                    if (Gl.getError() !== Gl.NO_ERROR) throw Error('WebGL draw error');
                }
            }
            for (const [Name, Program] of Object.entries(Programs))
            {
                Gl.useProgram(Program); Gl.uniform1f(Gl.getUniformLocation(Program, 'Phase'), 0);
                Gl.drawArrays(Gl.TRIANGLES, 0, 3); Gl.finish();
                const Pixels = new Float32Array(Width * Height * 4);
                Gl.readPixels(0, 0, Width, Height, Gl.RGBA, Gl.FLOAT, Pixels);
                if (Gl.getError() !== Gl.NO_ERROR) throw Error('Render/readback GL error');
                let Sum = 0, Minimum = Infinity, Maximum = -Infinity;
                for (let P = 0; P < Pixels.length; P += 4)
                {
                    if (Pixels[P + 3] !== 1) throw Error(`${Name}: incomplete alpha pixel=${P/4} rgba=${Array.from(Pixels.slice(P,P+4))}`);
                    for (let C = 0; C < 3; ++C)
                    {
                        const R = Pixels[P + C];
                        if (!Number.isFinite(R) || R < 0 || R > 1) throw Error(`${Name}: invalid colour ${R} pixel=${P/4} channel=${C} extent=${Width}x${Height}`);
                        Sum += R; Minimum = Math.min(Minimum, R); Maximum = Math.max(Maximum, R);
                    }
                }
                if (!(Maximum > Minimum && Sum > 0)) throw Error('Degenerate image');
                const Sorted = [...Samples[Name]].sort((A, B) => A - B);
                Records.push({method: Name, width: Width, height: Height, lights: Lights, density_scale: Density,
                    samples_ms: Samples[Name], p50_ms: Sorted[4], p95_ms: Sorted[8], min_rgb: Minimum, max_rgb: Maximum, rgb_sum: Sum});
                if (Width === 768 && Lights === 3 && Density === 1 && Name !== 'Smooth')
                {
                    const Rgb = new Uint8Array(Width * Height * 3);
                    for (let Y = 0; Y < Height; ++Y) for (let X = 0; X < Width; ++X) for (let C = 0; C < 3; ++C)
                        Rgb[(Y * Width + X) * 3 + C] = Math.round(255 * Math.pow(Pixels[((Height - 1 - Y) * Width + X) * 4 + C], 1 / 2.2));
                    Images[Name] = Array.from(Rgb);
                }
            }
            console.log(`PAIR width=${Width} lights=${Lights} density=${Density} completed`);
        }
        return {renderer: Renderer, version: Gl.getParameter(Gl.VERSION), records: Records, images: Images,
            resources: {feature_textures: 0, feature_buffers: 0, feature_texture_buffer_bytes: 0,
                shared_float_target_bytes: 768 * 384 * 16, shared_depth_stencil_bytes: 768 * 384 * 4, shared_readback_bytes: 768 * 384 * 16,
                particle_list_bytes: 0, dictionary_bytes: 0, programs_in_comparison: Object.keys(Programs).length + 1,
                driver_pipeline_register_spill_bytes: null, physical_vram_peak_bytes: null}};
    }, Sources);
    const CaptureHashes = {};
    const Repeat = process.argv.includes('--repeat') || Boolean(Isolate);
    for (const [Name, Pixels] of Object.entries(Result.images))
    {
        const Payload = Buffer.from(Pixels);
        CaptureHashes[Name] = createHash('sha256').update(Payload).digest('hex');
        const Ppm = `${Directory}/${Name}${Repeat ? 'Repeat' : ''}.ppm`;
        if (Repeat) await writeFile(Ppm, Buffer.concat([Buffer.from('P6\n768 384\n255\n'), Payload]));
        if (!Repeat)
        {
            await writeFile(Ppm, Buffer.concat([Buffer.from('P6\n768 384\n255\n'), Payload]));
            execFileSync('python3', ['Exhibits/UIFoundation/Code/EncodeEvidence.py', Ppm, `Exhibits/TextureStudio/Images/GlintPair${Name}.png`]);
        }
    }
    // 📝 Host memory snapshot outside all measured draw intervals; process-tree PSS is NOT GPU VRAM.
    const Processes = [];
    for (const Entry of await readdir('/proc'))
    {
        if (!/^\d+$/.test(Entry)) continue;
        try
        {
            const Status = await readFile(`/proc/${Entry}/status`, 'utf8');
            Processes.push({pid: Number(Entry), up: Number(Status.match(/^PPid:\s+(\d+)/m)[1]),
                rss_kib: Number(Status.match(/^VmRSS:\s+(\d+)/m)?.[1] || 0)});
        }
        catch {}
    }
    const Included = new Set([Browser.process().pid]);
    for (let I = 0; I < Processes.length; ++I)
        for (const P of Processes) if (Included.has(P.up)) Included.add(P.pid);
    const Memory = [];
    for (const P of Processes.filter(P => Included.has(P.pid)))
    {
        let Pss = null;
        try { Pss = Number((await readFile(`/proc/${P.pid}/smaps_rollup`, 'utf8')).match(/^Pss:\s+(\d+)/m)[1]); } catch {}
        Memory.push({rss_kib: P.rss_kib, pss_kib: Pss});
    }
    Result.resources.host_browser_process_tree = {processes: Memory.length,
        rss_sum_kib: Memory.reduce((S, P) => S + P.rss_kib, 0),
        pss_sum_kib: Memory.every(P => P.pss_kib !== null) ? Memory.reduce((S, P) => S + P.pss_kib, 0) : null,
        meaning: 'post-run host snapshot including browser/driver/JS/output; not peak allocation, not GPU VRAM; RSS double-counts shared pages'};
    delete Result.images;
    const Report = {result: 'passed', browser: await Browser.version(), platform: `${platform()} ${release()}`,
        cpu: cpus()[0].model, hardware_execution: false, windows_execution: false, native_slang_execution: false,
        authors_demo_full_file_identity_verified: false, full_material_integration: false,
        timing_scope: 'software ANGLE/Vulkan elapsed query around one shaded draw after a common stencil prepass; prepass/compile/readback excluded; no hardware GPU / FPS prediction',
        warmups: 2, measured_repetitions: 9, p95_definition: 'nearest rank; maximum of only nine samples',
        caveat: 'common scene but different material models; not equal-error/equal-quality results',
        inputs: JSON.parse(await readFile(`${Directory}/Inputs.json`, 'utf8')), ...Result,
        capture_rgb_sha256: CaptureHashes, source_sha256: {}};
    for (const File of ['Exhibits/TextureStudio/Code/PrepareGlintPair.py', 'Exhibits/TextureStudio/Code/VerifyGlintPair.mjs',
        'Exhibits/TextureStudio/Code/GlintPairScene.glsl', 'Engine/Render/Shader/AutomotivePaint.slang'])
        Report.source_sha256[File] = createHash('sha256').update(await readFile(File)).digest('hex');
    await writeFile(`Exhibits/TextureStudio/Results/GlintPair${Isolate ? 'Memory' + Isolate : Repeat ? 'Repeat' : ''}.json`, JSON.stringify(Report, null, 2) + '\n');
    console.log(JSON.stringify(Report.records.map(({method, width, lights, density_scale, p50_ms}) =>
        ({method, width, lights, density_scale, p50_ms})), null, 2));
}
finally { await Browser.close(); }
