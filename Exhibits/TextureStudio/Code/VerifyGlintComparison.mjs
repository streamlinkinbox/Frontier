//============================================================================================================================================
//                                                      VERIFYGLINTCOMPARISON.MJS
//============================================================================================================================================
// 🧩 Execute isolated reference-adapter fragment shaders with explicit software-renderer identity and readback correctness checks.

import {createRequire} from 'node:module';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {cpus, platform, arch, release} from 'node:os';
import {execFileSync} from 'node:child_process';
const Require = createRequire(process.cwd() + '/_ClaudeScratch/tmp/PatternTools/package.json');
const Puppeteer = Require('puppeteer-core');
const Chromium = Require('@sparticuz/chromium');
const Directory = process.argv[2] || '_ClaudeScratch/build/GlintComparison';
const Names = ['GlintyAdapter', 'ChermainReference', 'CellularAdapter', 'RasterFloor'];
const Sources = Object.fromEntries(await Promise.all(Names.map(async Name => [Name, await readFile(`${Directory}/${Name}.frag`, 'utf8')])));
const Dictionary = (await readFile(`${Directory}/Dictionary.bin`)).toString('base64');
const Inputs = JSON.parse(await readFile(`${Directory}/Inputs.json`, 'utf8'));
const Browser = await Puppeteer.launch({executablePath: '/tmp/chromium', headless: true,
    args: [...Chromium.args, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    env: {...process.env, LD_LIBRARY_PATH: process.cwd() + '/_ClaudeScratch/tmp/NativeBrowser/lib'}});
try
{
    const Page = await Browser.newPage();
    const Result = await Page.evaluate(async ({Sources, Dictionary}) =>
    {
        const Canvas = document.createElement('canvas');
        Canvas.width = Canvas.height = 256;
        const Gl = Canvas.getContext('webgl2', {antialias: false, depth: false, preserveDrawingBuffer: true});
        if (!Gl || !Gl.getExtension('EXT_color_buffer_float')) throw Error('Float WebGL2 rendering unavailable');
        const Timer = Gl.getExtension('EXT_disjoint_timer_query_webgl2');
        if (!Timer) throw Error('Timestamp queries unavailable; do not rank asynchronous CPU submission timings');
        const Debug = Gl.getExtension('WEBGL_debug_renderer_info');
        const Renderer = Debug ? Gl.getParameter(Debug.UNMASKED_RENDERER_WEBGL) : Gl.getParameter(Gl.RENDERER);
        if (!Renderer.toLowerCase().includes('swiftshader')) throw Error('Expected explicitly identified software renderer');
        const Compile = (Category, Source) =>
        {
            const Shader = Gl.createShader(Category); Gl.shaderSource(Shader, Source); Gl.compileShader(Shader);
            if (!Gl.getShaderParameter(Shader, Gl.COMPILE_STATUS)) throw Error(Gl.getShaderInfoLog(Shader));
            return Shader;
        };
        const Vertex = Compile(Gl.VERTEX_SHADER, `#version 300 es
precision highp float;
out vec2 Uv;
void main(){vec2 P=vec2((gl_VertexID<<1)&2,gl_VertexID&2);Uv=P;gl_Position=vec4(P*2.0-1.0,0,1);}`);
        const Texture = Gl.createTexture(); Gl.bindTexture(Gl.TEXTURE_2D, Texture);
        const Bytes = Uint8Array.from(atob(Dictionary), Character => Character.charCodeAt(0));
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGB16F, 64, 1024, 0, Gl.RGB, Gl.FLOAT, new Float32Array(Bytes.buffer));
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
        const Target = Gl.createTexture(); Gl.bindTexture(Gl.TEXTURE_2D, Target);
        Gl.texStorage2D(Gl.TEXTURE_2D, 1, Gl.RGBA32F, 256, 256);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
        const Framebuffer = Gl.createFramebuffer(); Gl.bindFramebuffer(Gl.FRAMEBUFFER, Framebuffer);
        Gl.framebufferTexture2D(Gl.FRAMEBUFFER, Gl.COLOR_ATTACHMENT0, Gl.TEXTURE_2D, Target, 0);
        if (Gl.checkFramebufferStatus(Gl.FRAMEBUFFER) !== Gl.FRAMEBUFFER_COMPLETE) throw Error('Incomplete float framebuffer');
        Gl.bindTexture(Gl.TEXTURE_2D, Texture);
        const Programs = {};
        for (const [Name, Source] of Object.entries(Sources))
        {
            const Program = Gl.createProgram(); Gl.attachShader(Program, Vertex);
            Gl.attachShader(Program, Compile(Gl.FRAGMENT_SHADER, Source)); Gl.linkProgram(Program);
            if (!Gl.getProgramParameter(Program, Gl.LINK_STATUS)) throw Error(Name + ': ' + Gl.getProgramInfoLog(Program));
            Programs[Name] = Program;
        }
        const Measurements = [];
        const Images = {};
        let SubmissionOrdinal = 0;
        for (const Extent of [128, 256]) for (const Density of [20, 26])
        {
            const Cases = Object.keys(Programs);
            // Rotate order between repetitions; one draw and finish per measured interval, after shader warmup.
            const Samples = Object.fromEntries(Cases.map(Name => [Name, []]));
            for (let Repetition = -2; Repetition < 7; ++Repetition)
            {
                const Offset = (Repetition + 2) % Cases.length;
                for (let Index = 0; Index < Cases.length; ++Index)
                {
                    const Name = Cases[(Index + Offset) % Cases.length], Program = Programs[Name];
                    Gl.useProgram(Program); Gl.viewport(0, 0, Extent, Extent);
                    Gl.uniform1i(Gl.getUniformLocation(Program, 'DictionaryTex'), 0);
                    Gl.uniform1f(Gl.getUniformLocation(Program, 'Density'), Density);
                    Gl.uniform1f(Gl.getUniformLocation(Program, 'Roughness'), .3);
                    Gl.uniform1f(Gl.getUniformLocation(Program, 'Phase'), SubmissionOrdinal++);
                    const Query = Gl.createQuery(); Gl.beginQuery(Timer.TIME_ELAPSED_EXT, Query);
                    Gl.drawArrays(Gl.TRIANGLES, 0, 3); Gl.endQuery(Timer.TIME_ELAPSED_EXT); Gl.flush();
                    let Available = false;
                    for (let Attempt = 0; Attempt < 10000; ++Attempt)
                    {
                        await new Promise(Resolve => setTimeout(Resolve, 1));
                        Available = Gl.getQueryParameter(Query, Gl.QUERY_RESULT_AVAILABLE);
                        if (Available) break;
                    }
                    if (!Available || Gl.getParameter(Timer.GPU_DISJOINT_EXT)) throw Error('Invalid/disjoint timestamp query');
                    const Elapsed = Gl.getQueryParameter(Query, Gl.QUERY_RESULT) / 1e6;
                    Gl.deleteQuery(Query);
                    if (!(Elapsed > 0)) throw Error('Zero/nonpositive timestamp interval; cannot rank');
                    if (Repetition >= 0) Samples[Name].push(Elapsed);
                    const ErrorCode = Gl.getError();
                    if (ErrorCode !== Gl.NO_ERROR) throw Error(`${Name}: GL error ${ErrorCode}`);
                }
            }
            for (const Name of Cases)
            {
                const Program = Programs[Name]; Gl.useProgram(Program);
                Gl.uniform1f(Gl.getUniformLocation(Program, 'Phase'), 0);
                Gl.drawArrays(Gl.TRIANGLES, 0, 3); Gl.finish();
                const Pixels = new Float32Array(Extent * Extent * 4);
                Gl.readPixels(0, 0, Extent, Extent, Gl.RGBA, Gl.FLOAT, Pixels);
                let Minimum = Infinity, Maximum = -Infinity, Sum = 0;
                for (let Index = 0; Index < Pixels.length; Index += 4)
                {
                    for (let Channel = 0; Channel < 3; ++Channel)
                    {
                        const Radiance = Pixels[Index + Channel];
                        if (!Number.isFinite(Radiance) || Radiance < 0) throw Error(Name + ': nonfinite/negative output');
                        Minimum = Math.min(Minimum, Radiance); Maximum = Math.max(Maximum, Radiance); Sum += Radiance;
                    }
                    if (Math.abs(Pixels[Index + 3] - 1) > .001) throw Error(Name + ': missing output pixels');
                }
                if (!(Maximum > Minimum && Sum > 0)) throw Error(Name + ': degenerate output');
                const Sorted = [...Samples[Name]].sort((A, B) => A - B);
                Measurements.push({method: Name, extent: Extent, log_density: Density, roughness: .3,
                    software_timestamp_ms: Samples[Name], p50_ms: Sorted[3], p95_ms: Sorted[6],
                    minimum: Minimum, maximum: Maximum, sum: Sum});
                if (Extent === 256 && Density === 20)
                {
                    const Rgb = new Uint8Array(Extent * Extent * 3);
                    for (let Y = 0; Y < Extent; ++Y) for (let X = 0; X < Extent; ++X)
                        for (let Channel = 0; Channel < 3; ++Channel)
                            Rgb[(Y * Extent + X) * 3 + Channel] = Math.round(255 * Math.pow(Math.min(1,
                                Pixels[((Extent - 1 - Y) * Extent + X) * 4 + Channel]), 1 / 2.2));
                    Images[Name] = Array.from(Rgb);
                }
            }
        }
        return {renderer: Renderer, vendor: Gl.getParameter(Gl.VENDOR), version: Gl.getParameter(Gl.VERSION),
            extensions: Gl.getSupportedExtensions(), records: Measurements, images: Images};
    }, {Sources, Dictionary});
    await mkdir('Exhibits/TextureStudio/Results', {recursive: true});
    const CaptureDigests = {};
    for (const [Name, Pixels] of Object.entries(Result.images))
    {
        await writeFile(`${Directory}/${Name}.ppm`, Buffer.concat([Buffer.from('P6\n256 256\n255\n'), Buffer.from(Pixels)]));
        if (Name !== 'RasterFloor')
        {
            const Capture = `Exhibits/TextureStudio/Images/Glint${Name}.png`;
            execFileSync('python3', ['Exhibits/UIFoundation/Code/EncodeEvidence.py', `${Directory}/${Name}.ppm`, Capture]);
            CaptureDigests[Capture] = createHash('sha256').update(await readFile(Capture)).digest('hex');
        }
    }
    delete Result.images;
    const Report = {result: 'passed', browser: await Browser.version(),
        operating_system: `${platform()} ${release()} ${arch()}`, cpu: cpus()[0].model, logical_cpus: cpus().length, hardware_execution: false, windows_execution: false,
        full_material_integration: false, official_glinty_reference: false,
        timing_scope: 'EXT_disjoint_timer_query_webgl2 around one draw; software-renderer device timestamp duration, not hardware GPU performance',
        repetitions: 7, warmups: 2, fixed_point_light: true, inputs: Inputs, ...Result, capture_sha256: CaptureDigests,
        source_sha256: {}};
    for (const File of ['Exhibits/TextureStudio/Code/PrepareGlintComparison.py',
        'Exhibits/TextureStudio/Code/VerifyGlintComparison.mjs', 'Engine/Render/Shader/AutomotivePaint.slang'])
        Report.source_sha256[File] = createHash('sha256').update(await readFile(File)).digest('hex');
    await writeFile(process.argv[3] || 'Exhibits/TextureStudio/Results/GlintComparisonChecks.json', JSON.stringify(Report, null, 2) + '\n');
    console.log(JSON.stringify({renderer: Result.renderer, timings: Result.records.map(({method, extent, log_density, p50_ms}) =>
        ({method, extent, log_density, p50_ms}))}, null, 2));
}
finally { await Browser.close(); }
