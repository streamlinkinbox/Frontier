//============================================================================================================================================
//                                                              RADIANCEPANEL.JS
//============================================================================================================================================
// 📦 Native WebGPU demo orchestration, rigid-instance updates, bounded resources, asynchronous GPU telemetry and a compact non-editor HUD.

import * as THREE from 'three';
import
{
    OrbitControls
}
from '../Ocean/lib/addons/OrbitControls.js';
import
{
    Defaults,
    ConstructScene,
    CascadeLayout
}
from './SceneSpecification.js';
import
{
    Common,
    Geometry,
    Cascade,
    Lighting,
    Temporal,
    Presentation,
    Projection,
    Shadow,
    Links
}
from './TransportIntegrator.js';
const Find = Id => document.getElementById(Id),
    Settings = {
        ...Defaults
    },
    Query = new URLSearchParams(location.search);
const Manual = Query.has('manual');
const State = {
    Ready: false,
    Error: null,
    Frame: 0,
    Completed: 0,
    Time: 0,
    InFlight: 0,
    NeedsResize: true,
    History: false,
    Metrics: null,
    Errors: [],
    Settings
};
const Canvas = Find('View');
const Camera = new THREE.PerspectiveCamera(48, innerWidth / innerHeight, .05, 140);
Camera.coordinateSystem = THREE.WebGPUCoordinateSystem;
const Controls = new OrbitControls(Camera, Canvas);
Controls.enableDamping = true;
Controls.dampingFactor = .1;
Controls.minDistance = 2;
Controls.maxDistance = 65;
Controls.maxPolarAngle = Math.PI * .49;

function ResetView()
{
    Controls.target.set(0, 2.8, -2);
    Camera.position.set(9, 8.4, 22);
    Controls.update();
    State.History = false;
}
ResetView();
const Profiles = {
    Low:
    {
        Scale: .5,
        Spacing: 20,
        Directions: 4,
        Cascades: 3,
        Shadow: 256
    },
    Balanced:
    {
        Scale: .65,
        Spacing: 12,
        Directions: 4,
        Cascades: 4,
        Shadow: 512
    },
    High:
    {
        Scale: .85,
        Spacing: 8,
        Directions: 4,
        Cascades: 5,
        Shadow: 1024
    }
};
const Fields = [
    ['Scale', 'Render scale', .35, 1, .05],
    ['Spacing', 'Probe spacing · px', 8, 24, 4],
    ['Directions', 'Base angular width', 4, 8, 2],
    ['Cascades', 'Cascade count', 2, 5, 1],
    ['Interval', 'First interval · m', .5, 4, .25],
    ['Shadow', 'Point shadow map · px', 256, 1024, 256],
    ['History', 'Temporal weight', 0, .9, .05],
    ['Exposure', 'Exposure', .3, 3, .1],
    ['Power', 'Emitter power', 0, 2, .1],
    ['Sky', 'Sky radiance', 0, .15, .005],
    ['Speed', 'Motion speed', 0, 2, .1],
    ['Level', 'Debug cascade', 0, 4, 1]
];
Find('Sliders').innerHTML = Fields.map(([Name, Label, Min, Max, Step]) =>
    `<label class="Slider"><span>${Label}<output id="${Name}Value"></output></span><input id="${Name}" type="range" min="${Min}" max="${Max}" step="${Step}" aria-label="${Label}"></label>`
    ).join('');

function Refresh()
{
    for (const [Name] of Fields)
    {
        Find(Name).value = Settings[Name];
        Find(`${Name}Value`).textContent = Number(Settings[Name].toFixed(3));
    }
    for (const Name of ['Motion', 'Visibility', 'Specular', 'Counters', 'Analytic']) Find(Name).checked = Settings[
    Name];
    Find('Preset').value = Object.entries(Profiles).find(([, Profile]) => Object.entries(Profile).every(([Name,
        Value]) => Math.abs(Settings[Name] - Value) < 1e-5))?.[0] || 'Custom';
    Find('Debug').value = Settings.Debug;
    Find('Pause').textContent = Settings.Animate ? 'Pause lights' : 'Resume lights';
    Find('Pause').classList.toggle('Active', Settings.Animate);
    Find('Level').max = Settings.Cascades - 1;
}

function Configure(Changes)
{
    for (const [Name, Value] of Object.entries(Changes))
    {
        if (!(Name in Settings)) continue;
        const Field = Fields.find(Field => Field[0] === Name);
        if (Field)
        {
            if (!Number.isFinite(Number(Value))) throw new Error(`${Name} must be finite`);
            Settings[Name] = Math.max(Field[2], Math.min(Field[3], Math.round(Number(Value) / Field[4]) * Field[4]));
        }
        else if (Name === 'Debug') Settings.Debug = Math.max(0, Math.min(9, Math.round(Number(Value) || 0)));
        else Settings[Name] = !!Value;
        if (['Scale', 'Spacing', 'Directions', 'Cascades', 'Interval', 'Shadow'].includes(Name)) State.NeedsResize =
            true;
    }
    Settings.Level = Math.min(Settings.Level, Settings.Cascades - 1);
    State.History = false;
    Refresh();
}
for (const [Name] of Fields) Find(Name).oninput = Event => Configure(
{
    [Name]: Number(Event.target.value)
});
for (const Name of ['Motion', 'Visibility', 'Specular', 'Counters', 'Analytic']) Find(Name).onchange = Event =>
    Configure(
    {
        [Name]: Event.target.checked
    });
Find('Debug').onchange = Event => Configure(
{
    Debug: Number(Event.target.value)
});
Find('Preset').onchange = Event => Configure(Profiles[Event.target.value] ||
{});
Find('Pause').onclick = () => Configure(
{
    Animate: !Settings.Animate
});
Find('Reset').onclick = ResetView;
Find('ToggleControls').onclick = () =>
{
    Find('Controls').hidden = !Find('Controls').hidden;
    Find('ToggleControls').setAttribute('aria-expanded', String(!Find('Controls').hidden));
};
if (innerWidth < 650) Find('ToggleControls').click();
window.addEventListener('keydown', Event =>
{
    if (['INPUT', 'SELECT', 'BUTTON'].includes(document.activeElement.tagName)) return;
    if (Event.code === 'Space')
    {
        Event.preventDefault();
        Find('Pause').click();
    }
    if (Event.key.toLowerCase() === 'h') Find('ToggleControls').click();
});
window.addEventListener('resize', () =>
{
    State.NeedsResize = true;
    State.History = false;
});
Refresh();

function Fail(Error)
{
    Find('Failure').querySelector('b').textContent = State.Ready ? 'Renderer stopped' : 'WebGPU initialisation failed';
    State.Error = Error.message || String(Error);
    State.Errors.push(State.Error);
    Find('Failure').hidden = false;
    Find('Failure').querySelector('p').textContent = State.Error;
    Find('Status').textContent = 'Rendering stopped · no alternate renderer substituted';
}
window.TransportApp = {
    State,
    Settings,
    Camera,
    Controls,
    Configure,
    ResetView
};
async function Start()
{
    if (!navigator.gpu) throw new Error(
        'navigator.gpu is unavailable. Open this HTTPS page in a WebGPU-enabled browser.');
    let Adapter = await navigator.gpu.requestAdapter(
    {
        powerPreference: 'high-performance'
    });
    if (!Adapter) Adapter = await navigator.gpu.requestAdapter(
    {
        forceFallbackAdapter: true
    });
    if (!Adapter) throw new Error(
        'No WebGPU adapter was granted. Check browser support, hardware acceleration and driver availability.'
        );
    const Info = Adapter.info ||
        {},
        Timestamp = Adapter.features.has('timestamp-query');
    State.Adapter = {
        Vendor: Info.vendor,
        Architecture: Info.architecture,
        Device: Info.device,
        Description: Info.description,
        Timestamp
    };
    State.Software = /swiftshader|software|llvmpipe/i.test(Object.values(State.Adapter).join(' ')) || !!Info
        .isFallbackAdapter;
    Find('Adapter').textContent =
        `${Info.description||Info.architecture||Info.vendor||'WebGPU adapter'}${State.Software?' · software renderer':''}`;
    const Device = await Adapter.requestDevice(
    {
        requiredFeatures: Timestamp ? ['timestamp-query'] : []
    });
    Device.addEventListener('uncapturederror', Event => Fail(Event.error));
    Device.lost.then(Info => Fail(new Error(`Device lost: ${Info.message||Info.reason}. Reload to recover.`)));
    const Context = Canvas.getContext('webgpu'),
        Format = navigator.gpu.getPreferredCanvasFormat();
    Context.configure(
    {
        device: Device,
        format: Format,
        alphaMode: 'opaque'
    });
    const Scene = ConstructScene();
    State.Triangles = Scene.Triangles.length;
    State.Nodes = Scene.Nodes.length;
    Find('TriangleCount').textContent = `${Scene.Triangles.length.toLocaleString()} triangles`;
    const Buffer = (Label, Size, Usage, Data = null) =>
    {
        const B = Device.createBuffer(
        {
            label: Label,
            size: Math.max(16, Math.ceil(Size / 4) * 4),
            usage: Usage
        });
        if (Data) Device.queue.writeBuffer(B, 0, Data);
        return B;
    };
    const Storage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
    const TriangleBuffer = Buffer('Triangle geometry', Scene.TriangleData.byteLength, Storage, Scene.TriangleData);
    const NodeBuffer = Buffer('Static local-space BLAS nodes', Scene.NodeData.byteLength, Storage, Scene.NodeData);
    const InstanceBuffer = Buffer('Rigid transforms and BLAS roots', 4 * 144, Storage);
    const Uniform = Buffer('Frame parameters', 288, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST);
    const Statistics = Buffer('Ray traversal counters', 16, Storage | GPUBufferUsage.COPY_SRC);
    const BaseEntries = [
    {
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT | GPUShaderStage.COMPUTE,
        buffer:
        {
            type: 'uniform'
        }
    },
    {
        binding: 1,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT | GPUShaderStage.COMPUTE,
        buffer:
        {
            type: 'read-only-storage'
        }
    },
    {
        binding: 2,
        visibility: GPUShaderStage.COMPUTE,
        buffer:
        {
            type: 'read-only-storage'
        }
    },
    {
        binding: 3,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.COMPUTE,
        buffer:
        {
            type: 'read-only-storage'
        }
    },
    {
        binding: 4,
        visibility: GPUShaderStage.COMPUTE,
        buffer:
        {
            type: 'storage'
        }
    }];
    const BaseLayout = Device.createBindGroupLayout(
    {
        entries: BaseEntries
    });
    const SharedLayout = Device.createBindGroupLayout(
    {
        entries: [...BaseEntries,
        {
            binding: 5,
            visibility: GPUShaderStage.COMPUTE,
            buffer:
            {
                type: 'uniform'
            }
        },
        {
            binding: 6,
            visibility: GPUShaderStage.COMPUTE,
            texture:
            {
                sampleType: 'depth',
                viewDimension: '2d-array'
            }
        },
        {
            binding: 7,
            visibility: GPUShaderStage.COMPUTE,
            sampler:
            {
                type: 'comparison'
            }
        }]
    });
    const Bind = (Layout, Resources) => Device.createBindGroup(
    {
        layout: Layout,
        entries: Resources.map((Resource, Index) => (
        {
            binding: Index,
            resource: Resource
        }))
    });
    const BaseResources = [Uniform, TriangleBuffer, NodeBuffer, InstanceBuffer, Statistics].map(buffer => (
    {
        buffer
    }));
    const BaseBind = Bind(BaseLayout, BaseResources);
    let Shared = null;
    const ShadowMatrixBuffer = Buffer('Point shadow view-projection matrices', 12 * 64, GPUBufferUsage.UNIFORM |
        GPUBufferUsage.COPY_DST);
    const ShadowSampler = Device.createSampler(
    {
        compare: 'less-equal',
        minFilter: 'linear',
        magFilter: 'linear'
    });
    const ShadowFaceLayout = Device.createBindGroupLayout(
    {
        entries: [
        {
            binding: 0,
            visibility: GPUShaderStage.VERTEX,
            buffer:
            {
                type: 'uniform'
            }
        }]
    });
    const Entry = (Slot, Kind, Stage = GPUShaderStage.COMPUTE) => (
    {
        binding: Slot,
        visibility: Stage,
        ...Kind
    });
    const ReadTexture = {
            texture:
            {
                sampleType: 'unfilterable-float'
            }
        },
        ReadBuffer = {
            buffer:
            {
                type: 'read-only-storage'
            }
        },
        WriteBuffer = {
            buffer:
            {
                type: 'storage'
            }
        },
        UniformEntry = {
            buffer:
            {
                type: 'uniform'
            }
        };
    const WriteTexture = {
        storageTexture:
        {
            access: 'write-only',
            format: 'rgba16float'
        }
    };
    const CascadeLayoutGpu = Device.createBindGroupLayout(
    {
        entries: [ReadTexture, ReadTexture, ReadBuffer, WriteBuffer, UniformEntry, ReadBuffer].map((T, I) =>
            Entry(I, T))
    });
    const LinkLayout = Device.createBindGroupLayout(
    {
        entries: [ReadTexture, ReadTexture, WriteBuffer, UniformEntry].map((T, I) => Entry(I, T))
    });
    const LightingLayout = Device.createBindGroupLayout(
    {
        entries: [ReadTexture, ReadTexture, ReadTexture, ReadTexture, ReadBuffer, WriteTexture,
            UniformEntry].map((T, I) => Entry(I, T))
    });
    const TemporalLayout = Device.createBindGroupLayout(
    {
        entries: [ReadTexture, ReadTexture, ReadTexture, ReadTexture, ReadTexture, ReadTexture,
            WriteTexture].map((T, I) => Entry(I, T))
    });
    const ProjectionLayout = Device.createBindGroupLayout(
    {
        entries: [ReadBuffer, WriteBuffer, UniformEntry].map((T, I) => Entry(I, T))
    });
    const PresentLayout = Device.createBindGroupLayout(
    {
        entries: [
        {
            texture:
            {
                sampleType: 'float'
            }
        },
        {
            sampler:
            {
                type: 'filtering'
            }
        }, ReadBuffer, UniformEntry].map((T, I) => Entry(I, T, GPUShaderStage.FRAGMENT))
    });
    async function Module(Label, Source)
    {
        const Module = Device.createShaderModule(
        {
            label: Label,
            code: Common + Source
        });
        const Info = await Module.getCompilationInfo();
        const Errors = Info.messages.filter(Message => Message.type === 'error');
        if (Errors.length) throw new Error(
            `${Label}: ${Errors.map(E=>`${E.lineNum}:${E.linePos} ${E.message}`).join('\n')}`);
        return Module;
    }
    const Layout = Second => Device.createPipelineLayout(
    {
        bindGroupLayouts: Second ? [SharedLayout, Second] : [SharedLayout]
    });
    const [GeometryModule, CascadeModule, LightingModule, TemporalModule, PresentModule, ProjectionModule,
        ShadowModule, LinkModule
    ] = await Promise.all([
        Module('Triangle raster', Geometry), Module('Cascade transport', Cascade), Module('Surface gather',
            Lighting), Module('History rejection', Temporal), Module('Presentation', Presentation), Module(
            'SH projection', Projection), Module('Point shadow raster', Shadow), Module(
            'Probe visibility links', Links)
    ]);
    const Raster = await Device.createRenderPipelineAsync(
    {
        label: 'Triangle G-buffer',
        layout: Device.createPipelineLayout(
        {
            bindGroupLayouts: [BaseLayout]
        }),
        vertex:
        {
            module: GeometryModule,
            entryPoint: 'VertexMain'
        },
        fragment:
        {
            module: GeometryModule,
            entryPoint: 'FragmentMain',
            targets: ['rgba16float', 'rgba16float', 'rgba8unorm', 'rgba16float'].map(format => (
            {
                format
            }))
        },
        primitive:
        {
            topology: 'triangle-list',
            cullMode: 'none'
        },
        depthStencil:
        {
            format: 'depth24plus',
            depthWriteEnabled: true,
            depthCompare: 'less'
        }
    });
    const ShadowPipeline = await Device.createRenderPipelineAsync(
    {
        label: 'Dynamic point shadow faces',
        layout: Device.createPipelineLayout(
        {
            bindGroupLayouts: [BaseLayout, ShadowFaceLayout]
        }),
        vertex:
        {
            module: ShadowModule,
            entryPoint: 'ShadowVertex'
        },
        primitive:
        {
            topology: 'triangle-list',
            cullMode: 'none'
        },
        depthStencil:
        {
            format: 'depth32float',
            depthWriteEnabled: true,
            depthCompare: 'less',
            depthBias: 2,
            depthBiasSlopeScale: 2
        }
    });
    const Compute = async (Label, Module, Entry, Second) => Device.createComputePipelineAsync(
    {
        label: Label,
        layout: Layout(Second),
        compute:
        {
            module: Module,
            entryPoint: Entry
        }
    });
    const [CascadePipeline, LightingPipeline, TemporalPipeline, ProjectionPipeline, LinkPipeline] = await Promise
        .all([Compute('Trace and merge', CascadeModule, 'CascadeMain', CascadeLayoutGpu),
            Compute('Lighting', LightingModule, 'LightingMain', LightingLayout), Compute('Temporal',
                TemporalModule, 'TemporalMain', TemporalLayout), Compute('Irradiance', ProjectionModule,
                'ProjectionMain', ProjectionLayout), Compute('Probe links', LinkModule, 'LinkMain', LinkLayout)
        ]);
    const Present = await Device.createRenderPipelineAsync(
    {
        label: 'Tonemap and display',
        layout: Layout(PresentLayout),
        vertex:
        {
            module: PresentModule,
            entryPoint: 'PresentVertex'
        },
        fragment:
        {
            module: PresentModule,
            entryPoint: 'PresentFragment',
            targets: [
            {
                format: Format
            }]
        },
        primitive:
        {
            topology: 'triangle-list'
        }
    });
    const Sampler = Device.createSampler(
        {
            minFilter: 'linear',
            magFilter: 'linear'
        }),
        Dummy = Buffer('Terminal cascade sentinel', 16, Storage);
    const QuerySet = Timestamp ? Device.createQuerySet(
    {
        type: 'timestamp',
        count: 32
    }) : null;
    const Resolve = Timestamp ? Buffer('GPU timestamp resolve', 256, GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage
        .COPY_SRC) : null;
    const Telemetry = Array.from(
    {
        length: 3
    }, () => (
    {
        Busy: false,
        Buffer: Buffer('Asynchronous telemetry', 272, GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ)
    }));
    let Resources = [],
        Width = 0,
        Height = 0,
        Position, Normal, Albedo, Emission, Depth, Raw, PreviousPosition, PreviousNormal, History, Levels,
        LightingBind, TemporalBind, PresentBind, ProjectionBind, ShadowTexture, ShadowFaces;
    let Memory = 0,
        PreviousVP = new THREE.Matrix4(),
        Parity = 0,
        LastTick = performance.now(),
        CompletedTick = performance.now(),
        CompletedCount = 0;

    function Allocate()
    {
        for (const Resource of Resources) Resource.destroy();
        Resources = [];
        const Ratio = Math.min(devicePixelRatio || 1, 1.5);
        Canvas.width = Math.max(1, Math.round(innerWidth * Ratio));
        Canvas.height = Math.max(1, Math.round(innerHeight * Ratio));
        const TestWidth = Number(Query.get('width'));
        const RequestedWidth = TestWidth > 0 ? Math.min(960, TestWidth) : Math.min(960, Canvas.width * Settings
            .Scale);
        Width = Math.max(128, Math.floor(Math.min(RequestedWidth, 720 * Canvas.width / Canvas.height) / 8) * 8);
        Height = Math.max(80, Math.round(Math.min(720, Width * Canvas.height / Canvas.width) / 8) * 8);
        Camera.aspect = Canvas.width / Canvas.height;
        Camera.updateProjectionMatrix();
        Memory = Scene.TriangleData.byteLength + Scene.NodeData.byteLength + 4 * 144 + 288 + 16 + 768;
        ShadowTexture = Device.createTexture(
        {
            label: 'Dynamic point-light depth maps',
            size: [Settings.Shadow, Settings.Shadow, 12],
            format: 'depth32float',
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
        });
        Resources.push(ShadowTexture);
        Memory += Settings.Shadow ** 2 * 12 * 4;
        Shared = Bind(SharedLayout, [...BaseResources,
        {
            buffer: ShadowMatrixBuffer
        }, ShadowTexture.createView(
        {
            dimension: '2d-array'
        }), ShadowSampler]);
        window.TransportApp.Shared = Shared;
        ShadowFaces = Array.from(
        {
            length: 12
        }, (_, I) =>
        {
            const Parameters = Buffer(`Shadow face ${I}`, 80, GPUBufferUsage.UNIFORM | GPUBufferUsage
                .COPY_DST);
            Resources.push(Parameters);
            Memory += 80;
            return {
                Parameters,
                View: ShadowTexture.createView(
                {
                    dimension: '2d',
                    baseArrayLayer: I,
                    arrayLayerCount: 1
                }),
                Bind: Bind(ShadowFaceLayout, [
                {
                    buffer: Parameters
                }])
            };
        });

        function Texture(Label, Format = 'rgba16float')
        {
            const T = Device.createTexture(
            {
                label: Label,
                size: [Width, Height],
                format: Format,
                usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                    (Format === 'depth24plus' ? 0 : GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST) | (
                        Format === 'rgba16float' ? GPUTextureUsage.STORAGE_BINDING : 0)
            });
            Resources.push(T);
            Memory += Width * Height * (Format === 'rgba16float' ? 8 : 4);
            return T;
        }
        Position = Texture('World position and instance');
        Normal = Texture('World normal and roughness');
        Albedo = Texture('Albedo', 'rgba8unorm');
        Emission = Texture('Emissive surfaces');
        Depth = Texture('Raster visibility', 'depth24plus');
        Raw = Texture('Current shading');
        PreviousPosition = Texture('Previous world positions');
        PreviousNormal = Texture('Previous normals');
        History = [Texture('History even'), Texture('History odd')];
        Levels = CascadeLayout(Width, Height, Settings).map(Spec =>
        {
            const Data = Buffer(`Cascade ${Spec.Level} intervals`, Spec.Count * 16, Storage | GPUBufferUsage
                    .COPY_SRC),
                Parameters = Buffer(`Cascade ${Spec.Level} parameters`, 48, GPUBufferUsage.UNIFORM |
                    GPUBufferUsage.COPY_DST);
            const Weights = Buffer(`Cascade ${Spec.Level} spatial links`, Spec.X * Spec.Y * 16, Storage);
            Resources.push(Data, Parameters, Weights);
            Memory += Spec.Count * 16 + 48 + Spec.X * Spec.Y * 16;
            return {
                ...Spec,
                Data,
                Parameters,
                Weights
            };
        });
        for (const L of Levels)
        {
            const Next = Levels[L.Level + 1],
                Data = new ArrayBuffer(48),
                U = new Uint32Array(Data),
                F = new Float32Array(Data);
            U.set([L.X, L.Y, L.Directions, L.Level]);
            F.set([L.Near, L.Far, L.Spacing, 0], 4);
            U.set(Next ? [Next.X, Next.Y, Next.Directions, 0] : [0, 0, 0, 0], 8);
            Device.queue.writeBuffer(L.Parameters, 0, Data);
            L.Bind = Bind(CascadeLayoutGpu, [Position.createView(), Normal.createView(),
            {
                buffer: Next?.Data || Dummy
            },
            {
                buffer: L.Data
            },
            {
                buffer: L.Parameters
            },
            {
                buffer: L.Weights
            }]);
            L.LinkBind = Bind(LinkLayout, [Position.createView(), Normal.createView(),
            {
                buffer: L.Weights
            },
            {
                buffer: L.Parameters
            }]);
        }
        const Coefficients = Buffer('Diffuse SH9 coefficients', Levels[0].X * Levels[0].Y * 9 * 16, Storage);
        Resources.push(Coefficients);
        Memory += Coefficients.size;
        ProjectionBind = Bind(ProjectionLayout, [
        {
            buffer: Levels[0].Data
        },
        {
            buffer: Coefficients
        },
        {
            buffer: Levels[0].Parameters
        }]);
        LightingBind = Bind(LightingLayout, [Position.createView(), Normal.createView(), Albedo.createView(),
            Emission.createView(),
            {
                buffer: Coefficients
            },
            Raw.createView(),
            {
                buffer: Levels[0].Parameters
            }
        ]);
        TemporalBind = [0, 1].map(I => Bind(TemporalLayout, [Raw.createView(), Position.createView(), Normal
            .createView(), History[1 - I].createView(), PreviousPosition.createView(), PreviousNormal
            .createView(), History[I].createView()
        ]));
        PresentBind = [0, 1].map(I => Levels.map(L => Bind(PresentLayout, [History[I].createView(), Sampler,
        {
            buffer: L.Data
        },
        {
            buffer: L.Parameters
        }])));
        State.Width = Width;
        State.Height = Height;
        State.SampleSlots = Levels.reduce((S, L) => S + L.Count, 0);
        State.Memory = Memory;
        State.Levels = Levels.map((
        {
            Level,
            X,
            Y,
            Directions,
            Count,
            Near,
            Far
        }) => (
        {
            Level,
            X,
            Y,
            Directions,
            Count,
            Near,
            Far
        }));
        Find('Memory').innerHTML = `${(Memory/1048576).toFixed(1)}<i> MB</i>`;
        Find('CascadeBudget').innerHTML = Levels.map(L =>
                `<span>C${L.Level} · ${L.X}×${L.Y} probes</span><b>${L.Directions**2} directions</b>`).join('') +
            `<span>Interval slots / frame</span><b>${State.SampleSlots.toLocaleString()}</b><span>Shading resolution</span><b>${Width} × ${Height}</b>`;
        State.NeedsResize = false;
        State.History = false;
        State.ShadowValid = false;
    }
    const Models = Array.from(
        {
            length: 4
        }, () => new THREE.Matrix4()),
        InstanceData = new ArrayBuffer(4 * 144),
        IFloat = new Float32Array(InstanceData),
        IUint = new Uint32Array(InstanceData);
    const VP = new THREE.Matrix4(),
        Data = new Float32Array(72),
        ShadowData = new Float32Array(12 * 16);
    const ShadowCamera = new THREE.PerspectiveCamera(90, 1, .08, 65);
    ShadowCamera.coordinateSystem = THREE.WebGPUCoordinateSystem;
    ShadowCamera.updateProjectionMatrix();
    const Directions = [
        [1, 0, 0],
        [-1, 0, 0],
        [0, 1, 0],
        [0, -1, 0],
        [0, 0, 1],
        [0, 0, -1]
    ];
    const ShadowFaceData = new ArrayBuffer(80),
        ShadowFaceFloat = new Float32Array(ShadowFaceData),
        ShadowFaceUint = new Uint32Array(ShadowFaceData);

    function Upload()
    {
        const T = State.Time,
            Light0 = [-3.4 + Math.sin(T * .67) * 2.2, 3.1 + Math.sin(T * .4) * .7, 1 + Math.cos(T * .5) * 3.4];
        const Light1 = [3.6 + Math.cos(T * .53) * 1.6, 4.3, -4 + Math.sin(T * .62) * 3.2];
        Models[0].identity();
        Models[1].makeRotationY(Settings.Motion ? T * .45 : 0);
        Models[1].setPosition(0, 1.07, -2);
        Models[2].makeTranslation(...Light0);
        Models[3].makeTranslation(...Light1);
        for (let I = 0; I < 12; ++I)
        {
            ShadowCamera.position.fromArray(I < 6 ? Light0 : Light1);
            const Face = I % 6;
            ShadowCamera.up.set(0, Face === 2 || Face === 3 ? 0 : 1, Face === 2 ? -1 : Face === 3 ? 1 : 0);
            ShadowCamera.lookAt(ShadowCamera.position.clone().add(new THREE.Vector3(...Directions[Face])));
            ShadowCamera.updateMatrixWorld();
            const Matrix = ShadowCamera.projectionMatrix.clone().multiply(ShadowCamera.matrixWorldInverse);
            ShadowData.set(Matrix.elements, I * 16);
            ShadowFaceFloat.set(Matrix.elements);
            ShadowFaceUint.set([Math.floor(I / 6), 0, 0, 0], 16);
            Device.queue.writeBuffer(ShadowFaces[I].Parameters, 0, ShadowFaceData);
        }
        Device.queue.writeBuffer(ShadowMatrixBuffer, 0, ShadowData);
        Models.forEach((Model, I) =>
        {
            IFloat.set(Model.elements, I * 36);
            IFloat.set(Model.clone().invert().elements, I * 36 + 16);
            IUint.set([Scene.Instances[I].Root, Scene.Instances[I].End, I, 0], I * 36 + 32);
        });
        Device.queue.writeBuffer(InstanceBuffer, 0, InstanceData);
        Camera.updateMatrixWorld();
        VP.multiplyMatrices(Camera.projectionMatrix, Camera.matrixWorldInverse);
        Data.set(VP.elements, 0);
        Data.set(PreviousVP.elements, 16);
        Data.set([...Camera.position.toArray(), T], 32);
        Data.set([Width, Height, 1 / Width, 1 / Height], 36);
        Data.set([Settings.Interval, Settings.History, Settings.Exposure, State.Frame], 40);
        Data.set([Settings.Power, Settings.Sky, +Settings.Specular, +Settings.Visibility], 44);
        Data.set([+State.History, +(Settings.Animate && Settings.Speed > 0), Settings.Debug, Settings.Level], 48);
        Data.set([...Light0, Settings.Analytic ? 125 : 0], 52);
        Data.set([1, .47, .16, 0], 56);
        Data.set([...Light1, Settings.Analytic ? 145 : 0], 60);
        Data.set([.13, .63, 1, 0], 64);
        Data.set([+Settings.Counters, 0, 0, 0], 68);
        Device.queue.writeBuffer(Uniform, 0, Data);
        PreviousVP.copy(VP);
    }

    function RecordTelemetry(Slot, Names, Number)
    {
        Slot.Buffer.mapAsync(GPUMapMode.READ).then(() =>
        {
            const Copy = Slot.Buffer.getMappedRange().slice(0),
                Ticks = new BigUint64Array(Copy, 0, 32),
                Counts = new Uint32Array(Copy, 256, 4);
            const Times = Timestamp ? Names.map((Name, I) => (
            {
                Name,
                Milliseconds: NumberSafe(Ticks[I * 2 + 1] - Ticks[I * 2]) / 1e6
            })) : [];
            const Total = Times.reduce((Sum, T) => Sum + T.Milliseconds, 0);
            State.Metrics = {
                Frame: Number,
                Times,
                Total: Timestamp ? Total : null,
                Rays: Counts[0],
                NodeVisits: Counts[1],
                TriangleTests: Counts[2],
                Hits: Counts[3]
            };
            Find('GpuTime').innerHTML = Timestamp ? `${Total.toFixed(1)}<i> ms</i>` : 'n/a';
            Find('RayLabel').textContent = Settings.Counters ? 'MEASURED RAYS / FRAME' :
                'INTERVAL SLOTS / FRAME';
            Find('Rays').textContent = (Settings.Counters ? Counts[0] : State.TraceSlots).toLocaleString();
            Find('PassTimes').textContent = Times.length ? Times.map(T =>
                    `${T.Name} ${T.Milliseconds.toFixed(2)}`).join(' · ') + ' ms' :
                'GPU timestamp queries unsupported; completed-frame rate is not GPU execution time.';
            Slot.Buffer.unmap();
            Slot.Busy = false;
        }).catch(Error =>
        {
            Slot.Busy = false;
            if (!State.Error) Fail(Error);
        });
    }
    const NumberSafe = Value => Number(Value);

    function RenderFrame()
    {
        if (State.Error) return Promise.reject(new Error(State.Error));
        if (State.InFlight >= 2 || State.NeedsResize && State.InFlight > 0) return null;
        if (State.NeedsResize) Allocate();
        Controls.update();
        Upload();
        const Encoder = Device.createCommandEncoder(
            {
                label: `Radiance frame ${State.Frame}`
            }),
            Names = [];
        const Stamp = Name =>
        {
            const Index = Names.length;
            Names.push(Name);
            return Timestamp ?
            {
                querySet: QuerySet,
                beginningOfPassWriteIndex: Index * 2,
                endOfPassWriteIndex: Index * 2 + 1
            } : undefined;
        };
        Encoder.clearBuffer(Statistics);
        const NeedShadows = Settings.Analytic && Settings.Power > 0 && ![3, 4, 5, 8].includes(Settings.Debug);
        if (NeedShadows && (!State.ShadowValid || State.ShadowTime !== State.Time || State.ShadowMotion !== Settings
                .Motion))
        {
            State.ShadowValid = true;
            State.ShadowTime = State.Time;
            State.ShadowMotion = Settings.Motion;
            State.ShadowUpdates = (State.ShadowUpdates || 0) + 1;
            const ShadowIndex = Names.length;
            Names.push('Shadows');
            for (let I = 0; I < 12; ++I)
            {
                const Writes = !Timestamp ? undefined : I === 0 ?
                {
                    querySet: QuerySet,
                    beginningOfPassWriteIndex: ShadowIndex * 2
                } : I === 11 ?
                {
                    querySet: QuerySet,
                    endOfPassWriteIndex: ShadowIndex * 2 + 1
                } : undefined;
                const Pass = Encoder.beginRenderPass(
                {
                    label: `Point shadow ${I}`,
                    colorAttachments: [],
                    depthStencilAttachment:
                    {
                        view: ShadowFaces[I].View,
                        depthClearValue: 1,
                        depthLoadOp: 'clear',
                        depthStoreOp: 'store'
                    },
                    timestampWrites: Writes
                });
                Pass.setPipeline(ShadowPipeline);
                Pass.setBindGroup(0, BaseBind);
                Pass.setBindGroup(1, ShadowFaces[I].Bind);
                Pass.draw(Scene.Triangles.length * 3);
                Pass.end();
            }
        }
        const RasterPass = Encoder.beginRenderPass(
        {
            label: 'Raster visibility',
            colorAttachments: [Position, Normal, Albedo, Emission].map(T => (
            {
                view: T.createView(),
                clearValue: [0, 0, 0, 0],
                loadOp: 'clear',
                storeOp: 'store'
            })),
            depthStencilAttachment:
            {
                view: Depth.createView(),
                depthClearValue: 1,
                depthLoadOp: 'clear',
                depthStoreOp: 'store'
            },
            timestampWrites: Stamp('Raster')
        });
        RasterPass.setPipeline(Raster);
        RasterPass.setBindGroup(0, BaseBind);
        RasterPass.draw(Scene.Triangles.length * 3);
        RasterPass.end();
        const NeedTransport = ![1, 3, 4, 5, 8].includes(Settings.Debug);
        State.TraceSlots = NeedTransport ? State.SampleSlots : 0;
        if (NeedTransport)
        {
            const LinkPass = Encoder.beginComputePass(
            {
                label: 'Shared spatial visibility',
                timestampWrites: Stamp('Links')
            });
            LinkPass.setPipeline(LinkPipeline);
            LinkPass.setBindGroup(0, Shared);
            for (const L of Levels.slice(0, -1))
            {
                LinkPass.setBindGroup(1, L.LinkBind);
                LinkPass.dispatchWorkgroups(Math.ceil(L.X * L.Y / 64));
            }
            LinkPass.end();
            for (let I = Levels.length - 1; I >= 0; --I)
            {
                const Pass = Encoder.beginComputePass(
                {
                    label: `Cascade ${I}`,
                    timestampWrites: Stamp(`C${I}`)
                });
                Pass.setPipeline(CascadePipeline);
                Pass.setBindGroup(0, Shared);
                Pass.setBindGroup(1, Levels[I].Bind);
                Pass.dispatchWorkgroups(Math.ceil(Levels[I].Count / 64));
                Pass.end();
            }
            const Project = Encoder.beginComputePass(
            {
                label: 'Diffuse SH9 projection',
                timestampWrites: Stamp('SH9')
            });
            Project.setPipeline(ProjectionPipeline);
            Project.setBindGroup(0, Shared);
            Project.setBindGroup(1, ProjectionBind);
            Project.dispatchWorkgroups(Math.ceil(Levels[0].X * Levels[0].Y / 64));
            Project.end();
        }
        for (const [Name, Pipeline, Group] of [
                ['Shade', LightingPipeline, LightingBind],
                ['Temporal', TemporalPipeline, TemporalBind[Parity]]
            ])
        {
            const Pass = Encoder.beginComputePass(
            {
                label: Name,
                timestampWrites: Stamp(Name)
            });
            Pass.setPipeline(Pipeline);
            Pass.setBindGroup(0, Shared);
            Pass.setBindGroup(1, Group);
            Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
            Pass.end();
        }
        const Final = Encoder.beginRenderPass(
        {
            label: 'Presentation',
            colorAttachments: [
            {
                view: Context.getCurrentTexture().createView(),
                clearValue: [0, 0, 0, 1],
                loadOp: 'clear',
                storeOp: 'store'
            }],
            timestampWrites: Stamp('Display')
        });
        Final.setPipeline(Present);
        Final.setBindGroup(0, Shared);
        Final.setBindGroup(1, PresentBind[Parity][Settings.Level]);
        Final.draw(3);
        Final.end();
        Encoder.copyTextureToTexture(
        {
            texture: Position
        },
        {
            texture: PreviousPosition
        }, [Width, Height]);
        Encoder.copyTextureToTexture(
        {
            texture: Normal
        },
        {
            texture: PreviousNormal
        }, [Width, Height]);
        const Slot = Telemetry.find(Slot => !Slot.Busy);
        if (Slot)
        {
            Slot.Busy = true;
            if (Timestamp)
            {
                Encoder.resolveQuerySet(QuerySet, 0, Names.length * 2, Resolve, 0);
                Encoder.copyBufferToBuffer(Resolve, 0, Slot.Buffer, 0, Names.length * 16);
            }
            Encoder.copyBufferToBuffer(Statistics, 0, Slot.Buffer, 256, 16);
        }
        Device.queue.submit([Encoder.finish()]);
        if (Slot) RecordTelemetry(Slot, Names, State.Frame);
        State.InFlight++;
        State.Frame++;
        State.History = true;
        Parity = 1 - Parity;
        return Device.queue.onSubmittedWorkDone().then(() =>
        {
            State.InFlight--;
            State.Completed++;
            CompletedCount++;
            const Now = performance.now();
            if (Now - CompletedTick > 1000)
            {
                State.Fps = CompletedCount * 1000 / (Now - CompletedTick);
                Find('Fps').innerHTML = `${State.Fps.toFixed(1)}<i> fps</i>`;
                CompletedCount = 0;
                CompletedTick = Now;
            }
            Find('Status').textContent =
                `${State.Software?'Software WebGPU · not a hardware performance benchmark':'Native WebGPU compute + raster'} · ${Width}×${Height} · frame ${State.Completed} · bounded 20 m atrium`;
        });
    }
    async function Readback(Name = 'History')
    {
        await Device.queue.onSubmittedWorkDone();
        const Pitch = Math.ceil(Width * 8 / 256) * 256;
        const B = Buffer('Validation HDR readback', Pitch * Height, GPUBufferUsage.COPY_DST | GPUBufferUsage
                .MAP_READ),
            E = Device.createCommandEncoder();
        E.copyTextureToBuffer(
        {
            texture: Name === 'Position' ? Position : History[1 - Parity]
        },
        {
            buffer: B,
            bytesPerRow: Pitch
        }, [Width, Height]);
        Device.queue.submit([E.finish()]);
        await B.mapAsync(GPUMapMode.READ);
        const Values = Array.from(new Uint16Array(B.getMappedRange()));
        B.unmap();
        B.destroy();
        return {
            Width,
            Height,
            Pitch,
            Values
        };
    }
    async function ReadCascade(Level)
    {
        await Device.queue.onSubmittedWorkDone();
        const L = Levels[Level];
        if (!L) throw new Error('Invalid cascade');
        const B = Buffer('Validation cascade readback', L.Count * 16, GPUBufferUsage.COPY_DST | GPUBufferUsage
                .MAP_READ),
            E = Device.createCommandEncoder();
        E.copyBufferToBuffer(L.Data, 0, B, 0, L.Count * 16);
        Device.queue.submit([E.finish()]);
        await B.mapAsync(GPUMapMode.READ);
        const Values = Array.from(new Float32Array(B.getMappedRange()));
        B.unmap();
        B.destroy();
        return Values;
    }
    Object.assign(window.TransportApp,
    {
        Device,
        Scene,
        Shared,
        SharedLayout,
        Models,
        RenderFrame,
        Readback,
        ReadCascade,
        Step: async Time =>
        {
            if (!Number.isFinite(Time)) throw new Error('Frame time must be finite');
            await Device.queue.onSubmittedWorkDone();
            State.Time = Time;
            const Result = RenderFrame();
            if (Result) await Result;
            return State.Metrics;
        }
    });
    State.Ready = true;
    Find('Status').textContent = 'WebGPU ready';

    function Animate(Now)
    {
        requestAnimationFrame(Animate);
        const Delta = Math.min(.05, (Now - LastTick) / 1000);
        LastTick = Now;
        if (document.hidden || State.Error) return;
        if (Settings.Animate) State.Time += Delta * Settings.Speed;
        try
        {
            const Pending = RenderFrame();
            Pending?.catch(Fail);
        }
        catch (Error)
        {
            Fail(Error);
        }
    }
    if (Manual) await RenderFrame();
    else requestAnimationFrame(Animate);
}
Start().catch(Fail);
