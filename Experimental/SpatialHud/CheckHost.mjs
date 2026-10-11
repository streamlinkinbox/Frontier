//===============================================================================================//
// 📦 CheckHost — run js/app.js for real, headless, against a recording stub of WebGPU.
//
// 🔴 WHY THIS EXISTS.
//
//    Three times now a change has passed every check in the repository, rendered four correct
//    stills on the CPU, parsed cleanly as WGSL — and then shown the user a blank canvas with a
//    red box on it. Every one of those was a HOST bug rather than a rendering bug:
//
//      · a uniform buffer referenced one line above the const that creates it, which is a
//        temporal dead zone and therefore a hard ReferenceError, not an undefined
//      · a bind group entry pointing at a binding the pipeline does not declare
//      · a stale module served out of the browser cache
//
//    None of those are visible to a syntax check. `node --check` parses the file and proves
//    nothing about the order things run in, and CheckHud only ever imports the pure modules —
//    figures, layout, fibres, chassis — and never app.js, because app.js touches the DOM and a
//    GPU. So the single file with the most moving parts in it was the one file nothing executed.
//
//    This runs it. document, window, navigator.gpu and requestAnimationFrame are all stubbed, the
//    stub RECORDS every call, and then the recording is asserted against: the passes happen in
//    the right order, into the right targets, with the right bindings, and the error box the page
//    shows on failure must still be empty at the end.
//
//    It is not a renderer and it cannot tell you the picture is right — that is RenderStill's
//    job. It tells you the program RUNS, which turns out to be the part that keeps breaking.
//===============================================================================================//

const Failures = [];
let Passed = 0;

function Claim(what, held) {
    if (held) { Passed++; return; }
    Failures.push(what);
    process.stderr.write(`  FAIL  ${what}\n`);
}

// ── the recording ────────────────────────────────────────────────────────────────────────────────

const Record = {
    passes: [],            // one entry per render pass, in submission order
    buffers: [],
    textures: [],
    pipelines: [],
    bindGroups: [],
    writes: [],
    shader: '',
    announced: null,       // whatever the page would have put in its red box
    frames: 0,
};

const Stub = (name, extra = {}) => ({ __stub: name, ...extra });

function StubTexture(descriptor) {
    const texture = {
        ...descriptor,
        createView: () => ({ __view: descriptor.format, of: texture }),
        destroy() {},
    };
    Record.textures.push(descriptor);
    return texture;
}

function StubPass(descriptor) {
    const entry = {
        target: descriptor.colorAttachments[0].view,
        clear: descriptor.colorAttachments[0].clearValue,
        draws: [],
        pipeline: null,
        bindGroup: null,
    };
    Record.passes.push(entry);
    return {
        setPipeline(pipeline) { entry.pipeline = pipeline; },
        setBindGroup(slot, group) { entry.bindGroup = group; },
        draw(count, instances, first, firstInstance) {
            entry.draws.push({
                count, instances, first, firstInstance,
                pipeline: entry.pipeline, bindGroup: entry.bindGroup,
            });
        },
        end() {},
    };
}

function StubDevice() {
    return {
        features: new Set(),
        limits: {},
        createShaderModule({ code }) {
            Record.shader = code;
            return { getCompilationInfo: async () => ({ messages: [] }) };
        },
        createRenderPipeline(descriptor) {
            // 🔴 Record what the pipeline actually DECLARES, so a bind group can be checked
            //    against it. A real device rejects an entry for a binding the shader never
            //    mentions, and that rejection only ever happens in the browser.
            const name = descriptor.fragment?.entryPoint ?? 'unknown';
            const pipeline = {
                name,
                vertex: descriptor.vertex?.entryPoint,
                format: descriptor.fragment?.targets?.[0]?.format,
                blend: descriptor.fragment?.targets?.[0]?.blend ?? null,
                getBindGroupLayout: (slot) => ({ __layout: name, slot }),
            };
            Record.pipelines.push(pipeline);
            return pipeline;
        },
        createBuffer(descriptor) {
            Record.buffers.push(descriptor);
            return { ...descriptor, __buffer: Record.buffers.length - 1, destroy() {} };
        },
        createTexture: StubTexture,
        createSampler: () => Stub('sampler'),
        createBindGroup({ layout, entries }) {
            const group = { layout, bindings: entries.map((One) => One.binding), entries };
            Record.bindGroups.push(group);
            return group;
        },
        createCommandEncoder: () => ({
            beginRenderPass: StubPass,
            finish: () => Stub('commands'),
        }),
        queue: {
            writeBuffer(buffer, offset, source) {
                Record.writes.push({ buffer, offset, length: source.length ?? 0 });
            },
            submit() {},
        },
        pushErrorScope() {},
        popErrorScope: async () => null,
        addEventListener() {},
    };
}

// ── the page ─────────────────────────────────────────────────────────────────────────────────────
// Every element the control rail reaches for, answering plausibly rather than throwing. The point
// is to let start() get all the way to a submitted frame.

function StubElement(id) {
    const element = {
        id,
        value: '0.5',
        checked: false,
        hidden: true,
        textContent: '',
        dataset: {},
        style: {},
        clientWidth: 1280,
        clientHeight: 760,
        width: 1280,
        height: 760,
        classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
        addEventListener() {},
        appendChild() {},
        querySelector: () => StubElement('child'),
        querySelectorAll: () => [],
        getBoundingClientRect: () => ({ left: 0, top: 0, width: 1280, height: 760 }),
        getContext(kind) {
            if (kind !== 'webgpu') return null;
            return {
                configure(descriptor) { Record.configured = descriptor; },
                getCurrentTexture: () => ({ createView: () => ({ __view: 'swapchain' }) }),
            };
        },
    };
    if (id === 'notice') {
        // The red box. Anything the page would have shown the user is a failure here.
        Object.defineProperty(element, 'textContent', {
            get: () => Record.announced ?? '',
            set: (text) => { if (text) Record.announced = text; },
        });
    }
    return element;
}

export function InstallStubs(frameLimit = 2) {
    const document = {
        readyState: 'complete',
        getElementById: (id) => StubElement(id),
        querySelector: (what) => StubElement(what),
        querySelectorAll: () => [],
        addEventListener() {},
        createElement: (tag) => StubElement(tag),
        body: StubElement('body'),
    };

    globalThis.document = document;
    globalThis.window = globalThis;
    globalThis.devicePixelRatio = 1;

    // The usage enums are globals the browser provides, and they are plain bit flags. Without
    // them every createBuffer call is a ReferenceError — which is itself the first thing this
    // harness found, so they are clearly worth having right.
    globalThis.GPUBufferUsage = {
        MAP_READ: 1, MAP_WRITE: 2, COPY_SRC: 4, COPY_DST: 8, INDEX: 16, VERTEX: 32,
        UNIFORM: 64, STORAGE: 128, INDIRECT: 256, QUERY_RESOLVE: 512,
    };
    globalThis.GPUTextureUsage = {
        COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, STORAGE_BINDING: 8, RENDER_ATTACHMENT: 16,
    };
    globalThis.GPUShaderStage = { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 };
    globalThis.GPUMapMode = { READ: 1, WRITE: 2 };

    // 🔴 globalThis.navigator is a getter-only accessor in node 22, so a plain assignment throws.
    //    defineProperty replaces the whole property and does work — the earlier note in the docs
    //    that said "shim window and document only" was about assignment, not about this.
    Object.defineProperty(globalThis, 'navigator', {
        configurable: true,
        value: {
            gpu: {
                getPreferredCanvasFormat: () => 'bgra8unorm',
                requestAdapter: async () => ({
                    features: new Set(),
                    limits: {},
                    requestDevice: async () => StubDevice(),
                }),
            },
        },
    });

    // Run a bounded number of frames and then stop, so the check terminates.
    globalThis.requestAnimationFrame = (callback) => {
        if (Record.frames >= frameLimit) return 0;
        Record.frames++;
        setTimeout(() => callback(performance.now()), 0);
        return Record.frames;
    };
    globalThis.cancelAnimationFrame = () => {};
    return Record;
}

// ── the run ──────────────────────────────────────────────────────────────────────────────────────

if (import.meta.url === (await import('node:url')).pathToFileURL(process.argv[1]).href) {
    InstallStubs(2);

    // Importing app.js boots it: a module script is deferred, so it calls start() itself when the
    // document is already complete. This is exactly the path the browser takes.
    await import('./js/app.js');

    // Let the promise chain and the two frames drain.
    for (let spin = 0; spin < 24; spin++) await new Promise((done) => setTimeout(done, 4));

    // 🔴 The one that would have caught the temporal dead zone, the stale binding and every other
    //    "it works for me" the CPU renderer cannot see.
    Claim(`the page reports no error (${Record.announced ?? 'none'})`, Record.announced === null);
    Claim('a frame was actually submitted', Record.passes.length > 0);

    if (Record.announced !== null || Record.passes.length < 5) {
        process.stderr.write(`\nCheckHost: ${Failures.length} FAILED, ${Passed} passed`
                           + ` — the host did not reach a complete frame, so nothing after this`
                           + ` could be checked.\n`);
        process.exit(1);
    }

    const named = (entry) => entry.draws[0]?.pipeline?.name ?? 'empty';
    const order = Record.passes.map(named);

    Claim(`four passes per frame, got ${Record.passes.length / Math.max(Record.frames, 1)}`,
          Record.passes.length >= 4);

    const frame = Record.passes.slice(0, 4);
    Claim(`the display is filled first (${order[0]})`, named(frame[0]) === 'FragmentMain');
    Claim(`then the bright pass (${order[1]})`, named(frame[1]) === 'fsBloomCut');
    Claim(`then one blur per axis (${order[2]}, ${order[3]})`,
          named(frame[2]) === 'fsBloomAcross' && named(frame[3]) === 'fsBloomDown');

    const window = Record.passes[4];
    Claim('the window pass comes last and starts with the body',
          window && window.draws[0]?.pipeline?.name === 'fsRoom');
    Claim('and ends with the glass',
          window && window.draws[window.draws.length - 1]?.pipeline?.name === 'fsGlass');

    // The display pass is the interface split in two with the world in between — the whole reason
    // the submission order matters at all.
    const interfaceDraws = frame[0].draws;
    Claim(`the interface is split in two around the world (${interfaceDraws.length} draws)`,
          interfaceDraws.length >= 3
          && interfaceDraws[0].pipeline.name === 'FragmentMain'
          && interfaceDraws[interfaceDraws.length - 1].pipeline.name === 'FragmentMain'
          && interfaceDraws.some((One) => One.pipeline.name === 'fsFibre'));
    Claim('the two interface halves are one buffer, drawn by offset',
          interfaceDraws[0].first === 0
          && interfaceDraws[interfaceDraws.length - 1].firstInstance > 0);

    // Targets and formats.
    const floatTargets = Record.textures.filter((One) => One.format === 'rgba16float');
    Claim(`the display and both glow targets are float (${floatTargets.length})`,
          floatTargets.length === 3);
    Claim('everything drawn into them is declared float',
          Record.pipelines.filter((One) => ['FragmentMain', 'fsFibre', 'fsSpark', 'fsBloomCut',
                                            'fsBloomAcross', 'fsBloomDown'].includes(One.name))
                          .every((One) => One.format === 'rgba16float'));
    Claim('the glass draws to the swapchain and is not additive',
          Record.pipelines.some((One) => One.name === 'fsGlass'
                                && One.format === 'bgra8unorm'
                                && One.blend?.color?.dstFactor === 'one-minus-src-alpha'));
    Claim('the body is opaque',
          Record.pipelines.some((One) => One.name === 'fsRoom' && One.blend === null));

    // 🔴 Bind groups against the bindings their shader stage actually declares. A real device
    //    rejects a group that carries an entry the pipeline never mentions.
    const glassGroup = Record.bindGroups.find((One) => One.layout.__layout === 'fsGlass');
    Claim('the glass is bound to the display, the sampler and the glow',
          glassGroup && [0, 3, 4, 5, 6].every((One) => glassGroup.bindings.includes(One)));
    const roomGroup = Record.bindGroups.find((One) => One.layout.__layout === 'fsRoom');
    Claim('the body is bound only to what it reads',
          roomGroup && roomGroup.bindings.join() === '0,3');

    // Two cameras: the interface must read the panel's uniform, never the window's.
    const globals = Record.buffers.filter((One) => One.size === 128);
    Claim('there are two camera uniforms of the same shape', globals.length === 2);
    const figureGroup = Record.bindGroups.find((One) => One.layout.__layout === 'FragmentMain');
    const glassCamera = glassGroup.entries.find((One) => One.binding === 0).resource.buffer;
    const panelCamera = figureGroup.entries.find((One) => One.binding === 0).resource.buffer;
    Claim('the interface and the glass look through DIFFERENT cameras',
          glassCamera.__buffer !== panelCamera.__buffer);

    Claim('both camera uniforms are written every frame',
          Record.writes.filter((One) => One.length === 32).length >= 2);

    if (Failures.length) {
        process.stderr.write(`\nCheckHost: ${Failures.length} FAILED, ${Passed} passed\n`);
        process.exit(1);
    }
    process.stdout.write(`CheckHost: PASS ${Passed}  (${Record.frames} frames, `
                       + `${Record.passes.length} passes, ${Record.pipelines.length} pipelines)\n`);
}
