//============================================================================================================================================
//                                                              DISTANCEPANEL.JS
//============================================================================================================================================
// 📦 Native WebGPU field construction and visible SDF diagnostics; preparation and lighting timings remain separate.

import * as THREE from 'three';
import
{
    OrbitControls
}
from '../Ocean/lib/addons/OrbitControls.js';
import
{
    ConstructScene,
    Defaults,
    DomainMinimum,
    DomainMaximum
}
from './BodySpecification.js';
import
{
    Deformation,
    Construction,
    Raster,
    Shading,
    Presentation
}
from './DistanceIntegrator.js';

const Find = Identifier => document.getElementById(Identifier);
const Configuration = {
    ...Defaults
};
const Query = new URLSearchParams(location.search),
    Manual = Query.has('manual');
const Metrics = {
    Ready: false,
    Busy: false,
    Error: null,
    Completed: 0,
    Revision: 0,
    FieldRevision: 0,
    FieldBuilds: 0,
    Errors: []
};
const Canvas = Find('View');
const Camera = new THREE.PerspectiveCamera(43, innerWidth / innerHeight, .05, 60);
Camera.coordinateSystem = THREE.WebGPUCoordinateSystem;
const Controls = new OrbitControls(Camera, Canvas);
Controls.enableDamping = true;
Controls.minDistance = 4;
Controls.maxDistance = 18;
Controls.maxPolarAngle = Math.PI * .48;

function ResetView()
{
    Camera.position.set(6.3, 4.3, 8.9);
    Controls.target.set(0, 1.1, 0);
    Controls.update();
}
ResetView();
const Captions = [
    'RASTER SURFACE · SDF SHADOWS + ONE-BOUNCE GI', 'SDF ONLY · TRIANGLE RASTER HIDDEN',
    'SDF GRADIENT NORMALS · TRIANGLE RASTER HIDDEN', 'SIGNED FIELD SLICE · BLUE OUTSIDE / ORANGE INSIDE',
    'PRIMARY HIT ERROR · GREEN CLOSE / YELLOW DEPTH / RED MISSING / BLUE EXTRA',
    'MARCH STEPS · BLUE LOW / RED HIGH / MAGENTA EXHAUSTED', 'INDIRECT ONLY · SDF BOUNCE + SHADOW RAYS',
    'TRIANGLE REFERENCE · SAME ONE-BOUNCE ESTIMATOR', 'TRIANGLE NORMALS + ACTUAL QUAD EDGES',
    'RASTER DIRECT ONLY · SDF SHADOW RAYS'
];

function Refresh()
{
    for (const Name of ['Amount', 'Resolution', 'Rays', 'Scale', 'Exposure', 'Power', 'Slice'])
    {
        Find(Name).value = Configuration[Name];
        Find(Name + 'Reading').textContent = Configuration[Name];
    }
    Find('Diagnostic').value = Configuration.View;
    Find('Axis').value = Configuration.Axis;
    Find('Frozen').checked = Configuration.Frozen;
    Find('Counters').checked = Configuration.Counters;
    Find('Pause').textContent = Configuration.Animate ? 'Pause deformation' : 'Animate deformation';
    Find('Caption').textContent = Captions[Configuration.View];
    Find('SliceControls').hidden = Configuration.View !== 3;
    Find('Warning').textContent = Configuration.Frozen ? 'SDF FROZEN: move the geometry to expose stale-field errors.' :
        'Sampled approximation: thin features can disappear. Magenta marks an exhausted march.';
}

function Configure(Changes)
{
    const Limits = {
        Amount: [0, 1],
        Resolution: [32, 128],
        Rays: [1, 16],
        Scale: [.25, 1],
        Exposure: [.2, 3],
        Power: [0, 2],
        Slice: [0, 1],
        View: [0, 9],
        Axis: [0, 2]
    };
    for (const [Name, Reading] of Object.entries(Changes))
    {
        if (!(Name in Configuration)) continue;
        if (Limits[Name])
        {
            if (!Number.isFinite(Number(Reading))) throw new Error('A finite control is required');
            Configuration[Name] = Math.max(Limits[Name][0], Math.min(Limits[Name][1], Number(Reading)));
        }
        else Configuration[Name] = !!Reading;
    }
    Configuration.Resolution = Math.round(Configuration.Resolution / 16) * 16;
    for (const Name of ['Rays', 'View', 'Axis']) Configuration[Name] = Math.round(Configuration[Name]);
    Refresh();
}
for (const Name of ['Amount', 'Resolution', 'Rays', 'Scale', 'Exposure', 'Power', 'Slice'])
    Find(Name).oninput = Event => Configure(
    {
        [Name]: Number(Event.target.value)
    });
Find('Diagnostic').onchange = Event => Configure(
{
    View: Number(Event.target.value)
});
Find('Axis').onchange = Event => Configure(
{
    Axis: Number(Event.target.value)
});
for (const Name of ['Frozen', 'Counters']) Find(Name).onchange = Event => Configure(
{
    [Name]: Event.target.checked
});
let AnimationLimit = Defaults.Amount;
Find('Pause').onclick = () =>
{
    if (Configuration.Animate)
    {
        AnimationLimit = Configuration.Amount;
        Configure(
        {
            Animate: false,
            Amount: Metrics.Amount || 0
        });
    }
    else Configure(
    {
        Animate: true,
        Amount: Math.max(Configuration.Amount, AnimationLimit)
    });
};
Find('Reset').onclick = ResetView;
Find('Rest').onclick = () => Configure(
{
    Animate: false,
    Amount: 0
});
Find('Loaded').onclick = () => Configure(
{
    Animate: false,
    Amount: 1
});
Find('Toggle').onclick = () =>
{
    Find('Controls').hidden = !Find('Controls').hidden;
    Find('Toggle').setAttribute('aria-expanded', String(!Find('Controls').hidden));
};
addEventListener('keydown', Event =>
{
    if (/INPUT|SELECT|BUTTON/.test(Event.target.tagName)) return;
    if (Event.code === 'Space')
    {
        Event.preventDefault();
        Find('Pause').click();
    }
    if (Event.code === 'KeyH') Find('Toggle').click();
});
if (innerWidth < 700) Find('Toggle').click();
Refresh();

function Refuse(Error)
{
    Metrics.Error = Error.message || String(Error);
    Metrics.Errors.push(Metrics.Error);
    Find('Failure').hidden = false;
    Find('FailureText').textContent = Metrics.Error;
}
window.DistanceApp = {
    Configuration,
    Metrics,
    Camera,
    Controls,
    Configure,
    ResetView
};
async function Start()
{
    if (!navigator.gpu) throw new Error('WebGPU required. There is no WebGL or CPU renderer fallback.');
    const Adapter = await navigator.gpu.requestAdapter(
    {
        powerPreference: 'high-performance'
    });
    if (!Adapter) throw new Error('No WebGPU adapter available');
    const Information = Adapter.info || await Adapter.requestAdapterInfo?.() ||
    {};
    const Timestamp = Adapter.features.has('timestamp-query');
    const Device = await Adapter.requestDevice(
    {
        requiredFeatures: Timestamp ? ['timestamp-query'] : []
    });
    Device.addEventListener('uncapturederror', Event => Refuse(Event.error));
    Device.lost.then(Reason => Refuse(new Error('Device lost: ' + Reason.message)));
    const Software = /swiftshader|software|llvmpipe/i.test(Information.architecture + ' ' + Information
    .description);
    Metrics.Adapter = {
        Vendor: Information.vendor,
        Architecture: Information.architecture,
        Software,
        Timestamp
    };
    Find('Adapter').textContent = (Information.architecture || Information.vendor || 'WebGPU') + (Software ?
        ' · software adapter' : ' · native WebGPU');
    const Context = Canvas.getContext('webgpu'),
        Format = navigator.gpu.getPreferredCanvasFormat();
    Context.configure(
    {
        device: Device,
        format: Format,
        alphaMode: 'opaque'
    });
    const Scene = ConstructScene();
    Find('SceneFacts').textContent =
        `${Scene.Facets.length.toLocaleString()} triangles · ${Scene.Parts.length} closed parts · 25–160 mm thin features`;
    const Storage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST;

    function Allocate(Label, Content, Usage = Storage)
    {
        const Resource = Device.createBuffer(
        {
            label: Label,
            size: Math.max(16, typeof Content === 'number' ? Content : Content.byteLength),
            usage: Usage
        });
        if (typeof Content !== 'number') Device.queue.writeBuffer(Resource, 0, Content);
        return Resource;
    }
    const Parameters = Allocate('Field and camera parameters', 240, GPUBufferUsage.UNIFORM | GPUBufferUsage
        .COPY_DST);
    const Rest = Allocate('Rest bodywork vertices', new Float32Array(Scene.Vertices.flat()));
    const Vertices = Allocate('Current deformed vertices', new Float32Array(Scene.Vertices.flat()));
    const Connectivity = Allocate('Stable quad triangles', new Uint32Array(Scene.Facets.flatMap(Facet => Facet
        .slice(0, 4))));
    const Facets = Allocate('Current triangles and materials', Scene.FacetBytes);
    const Bounds = Allocate('Refitted construction/reference BVH', Scene.BoundsBytes);
    const DepthIndices = Allocate('Refit depth permutation', new Uint32Array(Scene.DepthIndices));
    const DepthRanges = Allocate('Refit dispatch ranges', Scene.DepthRanges.length * 256, GPUBufferUsage.UNIFORM |
        GPUBufferUsage.COPY_DST);
    Scene.DepthRanges.forEach((Range, Index) => Device.queue.writeBuffer(DepthRanges, Index * 256, new Uint32Array(
        Range)));
    const Counters = Allocate('Lighting-only traversal counters', 16);
    const ComponentBounds = Allocate('Closed-component extents for sign pruning', 32 * 32);
    const All = GPUShaderStage.COMPUTE | GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
    const Uniform = (Slot, Dynamic = false) => (
    {
        binding: Slot,
        visibility: All,
        buffer:
        {
            type: 'uniform',
            hasDynamicOffset: Dynamic
        }
    });
    const Stored = (Slot, Write = false) => (
    {
        binding: Slot,
        visibility: Write ? GPUShaderStage.COMPUTE : All,
        buffer:
        {
            type: Write ? 'storage' : 'read-only-storage'
        }
    });
    const Sampled = Slot => (
    {
        binding: Slot,
        visibility: All,
        texture:
        {
            sampleType: 'unfilterable-float'
        }
    });
    const GeometryLayout = Device.createBindGroupLayout(
    {
        entries: [Uniform(0), Stored(1), Stored(2, true), Stored(3), Stored(4, true), Stored(5, true),
            Stored(6), Uniform(7, true)
        ]
    });
    const TraceLayout = Device.createBindGroupLayout(
    {
        entries: [Uniform(0), Stored(1), Stored(2)]
    });
    const ConstructLayout = Device.createBindGroupLayout(
    {
        entries: [
        {
            binding: 0,
            visibility: GPUShaderStage.COMPUTE,
            storageTexture:
            {
                access: 'write-only',
                format: 'rgba16float',
                viewDimension: '3d'
            }
        }, Stored(1, true)]
    });
    const FieldLayout = Device.createBindGroupLayout(
    {
        entries: [
        {
            binding: 0,
            visibility: GPUShaderStage.COMPUTE,
            texture:
            {
                sampleType: 'float',
                viewDimension: '3d'
            }
        },
        {
            binding: 1,
            visibility: GPUShaderStage.COMPUTE,
            sampler:
            {
                type: 'filtering'
            }
        }, Stored(2, true)]
    });
    const ShadeLayout = Device.createBindGroupLayout(
    {
        entries: [Sampled(0), Sampled(1), Sampled(2),
        {
            binding: 3,
            visibility: GPUShaderStage.COMPUTE,
            storageTexture:
            {
                access: 'write-only',
                format: 'rgba16float'
            }
        }]
    });
    const DisplayLayout = Device.createBindGroupLayout(
    {
        entries: [Sampled(0)]
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
    const GeometryBind = Bind(GeometryLayout, [
    {
        buffer: Parameters
    },
    {
        buffer: Rest
    },
    {
        buffer: Vertices
    },
    {
        buffer: Connectivity
    },
    {
        buffer: Facets
    },
    {
        buffer: Bounds
    },
    {
        buffer: DepthIndices
    },
    {
        buffer: DepthRanges,
        size: 16
    }]);
    const CurrentBind = Bind(TraceLayout, [
    {
        buffer: Parameters
    },
    {
        buffer: Facets
    },
    {
        buffer: Bounds
    }]);
    const Sampler = Device.createSampler(
    {
        minFilter: 'linear',
        magFilter: 'linear',
        addressModeU: 'clamp-to-edge',
        addressModeV: 'clamp-to-edge',
        addressModeW: 'clamp-to-edge'
    });
    async function Compile(Label, Code)
    {
        const Program = Device.createShaderModule(
        {
            label: Label,
            code: Code
        });
        const Reading = await Program.getCompilationInfo();
        const Errors = Reading.messages.filter(Message => Message.type === 'error');
        if (Errors.length) throw new Error(Label + ': ' + Errors.map(Message =>
            `${Message.lineNum}: ${Message.message}`).join('\n'));
        return Program;
    }
    const GeometryProgram = await Compile('Vertex expansion and refit', Deformation);
    const GeometryProjection = Device.createPipelineLayout(
    {
        bindGroupLayouts: [GeometryLayout]
    });
    const Compute = (Program, Entry, Layout) => Device.createComputePipelineAsync(
    {
        layout: Layout,
        compute:
        {
            module: Program,
            entryPoint: Entry
        }
    });
    const Deform = await Compute(GeometryProgram, 'Deform', GeometryProjection);
    const Expand = await Compute(GeometryProgram, 'Expand', GeometryProjection);
    const Refit = await Compute(GeometryProgram, 'Refit', GeometryProjection);
    const ConstructionProgram = await Compile('Signed field construction', Construction);
    const ConstructionLayout = Device.createPipelineLayout(
    {
        bindGroupLayouts: [TraceLayout, ConstructLayout]
    });
    const Construct = await Compute(ConstructionProgram, 'Construct', ConstructionLayout);
    const Components = await Compute(ConstructionProgram, 'ComponentExtents', ConstructionLayout);
    const Shade = await Compute(await Compile('SDF sphere tracing and single bounce', Shading), 'Shade', Device
        .createPipelineLayout(
        {
            bindGroupLayouts: [TraceLayout, ShadeLayout, FieldLayout]
        }));
    const RasterProgram = await Compile('Current triangle visibility', Raster);
    const RasterProjection = await Device.createRenderPipelineAsync(
    {
        layout: Device.createPipelineLayout(
        {
            bindGroupLayouts: [TraceLayout]
        }),
        vertex:
        {
            module: RasterProgram,
            entryPoint: 'Project'
        },
        fragment:
        {
            module: RasterProgram,
            entryPoint: 'Resolve',
            targets: [
            {
                format: 'rgba32float'
            },
            {
                format: 'rgba16float'
            },
            {
                format: 'rgba16float'
            }]
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
            depthCompare: 'less'
        }
    });
    const DisplayProgram = await Compile('Display', Presentation);
    const Display = await Device.createRenderPipelineAsync(
    {
        layout: Device.createPipelineLayout(
        {
            bindGroupLayouts: [TraceLayout, DisplayLayout]
        }),
        vertex:
        {
            module: DisplayProgram,
            entryPoint: 'Project'
        },
        fragment:
        {
            module: DisplayProgram,
            entryPoint: 'Resolve',
            targets: [
            {
                format: Format
            }]
        }
    });
    let Volume = null,
        ConstructBind, FieldBind, Resolution = 0,
        FieldKey = '',
        GeometryKey = '',
        SizeKey = '';
    let Textures = [],
        Position, Normal, Colour, Depth, Image, ShadeBind, DisplayBind;
    let External = null,
        ExternalRevision = 0,
        Stopped = false,
        SimulationTime = 0,
        LastTick = performance.now();
    const UniformBytes = new ArrayBuffer(240),
        Floats = new Float32Array(UniformBytes),
        Integers = new Uint32Array(UniformBytes);
    const Projection = new THREE.Matrix4(),
        InverseProjection = new THREE.Matrix4();
    const QuerySet = Timestamp ? Device.createQuerySet(
    {
        type: 'timestamp',
        count: 16
    }) : null;
    const Resolve = Timestamp ? Allocate('Timestamp resolve', 128, GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage
        .COPY_SRC) : null;
    const Readings = Allocate('Timestamp and traversal readback', 144, GPUBufferUsage.COPY_DST | GPUBufferUsage
        .MAP_READ);

    function Resize()
    {
        const Requested = Math.min(720, Number(Query.get('width')) || innerWidth * Configuration.Scale);
        const Width = Math.max(96, Math.floor(Math.min(Requested, 560 * innerWidth / innerHeight) / 8) * 8);
        const Height = Math.max(64, Math.floor(Width * innerHeight / innerWidth / 8) * 8);
        const Key = `${Width}/${Height}/${innerWidth}/${innerHeight}`;
        if (SizeKey === Key) return;
        SizeKey = Key;
        for (const Texture of Textures) Texture.destroy();
        Textures = [];
        const Texture = (Label, Format, Usage) =>
        {
            const Resource = Device.createTexture(
            {
                label: Label,
                size: [Width, Height],
                format: Format,
                usage: Usage
            });
            Textures.push(Resource);
            return Resource;
        };
        const SampleUsage = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC;
        Position = Texture('Raster world positions', 'rgba32float', SampleUsage | GPUTextureUsage
        .RENDER_ATTACHMENT);
        Normal = Texture('Raster normals', 'rgba16float', SampleUsage | GPUTextureUsage.RENDER_ATTACHMENT);
        Colour = Texture('Raster reflectance and quad edges', 'rgba16float', SampleUsage | GPUTextureUsage
            .RENDER_ATTACHMENT);
        Depth = Texture('Raster depth', 'depth32float', GPUTextureUsage.RENDER_ATTACHMENT);
        Image = Texture('SDF lighting or diagnostic', 'rgba16float', SampleUsage | GPUTextureUsage.STORAGE_BINDING);
        ShadeBind = Bind(ShadeLayout, [Position.createView(), Normal.createView(), Colour.createView(), Image
            .createView()
        ]);
        DisplayBind = Bind(DisplayLayout, [Image.createView()]);
        Canvas.width = Math.round(innerWidth * Math.min(devicePixelRatio, 1.5));
        Canvas.height = Math.round(innerHeight * Math.min(devicePixelRatio, 1.5));
        Camera.aspect = innerWidth / innerHeight;
        Camera.updateProjectionMatrix();
        Metrics.Width = Width;
        Metrics.Height = Height;
    }

    function AllocateField(Count)
    {
        if (Resolution === Count) return;
        Volume?.destroy();
        Resolution = Count;
        FieldKey = '';
        Volume = Device.createTexture(
        {
            label: 'Dynamic signed mesh distance and material',
            size: [Count, Count, Count],
            dimension: '3d',
            format: 'rgba16float',
            usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage
                .COPY_SRC
        });
        ConstructBind = Bind(ConstructLayout, [Volume.createView(),
        {
            buffer: ComponentBounds
        }]);
        FieldBind = Bind(FieldLayout, [Volume.createView(), Sampler,
        {
            buffer: Counters
        }]);
        Metrics.Resolution = Count;
        Metrics.VoxelBytes = Count ** 3 * 8;
        Metrics.CellWidths = DomainMaximum.map((Coordinate, Axis) => (Coordinate - DomainMinimum[Axis]) / (Count -
            1));
        Find('CellSize').textContent = Metrics.CellWidths.map(Length => (Length * 1000).toFixed(0)).join(' × ') +
            ' mm';
        Find('VolumeSize').textContent = `${Count}³ · ${(Metrics.VoxelBytes / 1048576).toFixed(1)} MiB`;
        Object.assign(window.DistanceApp,
        {
            Volume,
            FieldBind
        });
    }
    async function Render(Amount)
    {
        if (Metrics.Busy || Stopped) return;
        if (Metrics.Error) throw new Error(Metrics.Error);
        const Active = {
            ...Configuration
        };
        Metrics.Busy = true;
        try
        {
            Resize();
            AllocateField(Active.Resolution);
            Controls.update();
            Camera.updateMatrixWorld();
            Projection.multiplyMatrices(Camera.projectionMatrix, Camera.matrixWorldInverse);
            InverseProjection.copy(Projection).invert();
            const CurrentGeometryKey = `${Amount}/${ExternalRevision}`;
            const GeometryChanged = GeometryKey !== CurrentGeometryKey;
            const FieldChanged = !FieldKey || (!Active.Frozen && FieldKey !== CurrentGeometryKey);
            Floats.set(Projection.elements, 0);
            Floats.set(InverseProjection.elements, 16);
            Floats.set([...Camera.position.toArray(), 0], 32);
            Integers.set([Metrics.Width, Metrics.Height, Active.View, Active.Rays], 36);
            Floats.set([Amount, Active.Power, Active.Exposure, Active.Slice], 40);
            Integers.set([Resolution, Active.Axis, +Active.Counters, Metrics.FieldRevision], 44);
            Floats.set([...DomainMinimum, .7], 48);
            Floats.set([...DomainMaximum, 0], 52);
            Integers.set([256, 0, 0, 0], 56);
            Device.queue.writeBuffer(Parameters, 0, UniformBytes);
            if (GeometryChanged && External) Device.queue.writeBuffer(Vertices, 0, External);
            const Encoder = Device.createCommandEncoder(),
                Names = [];
            const Measure = Name =>
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
            if (GeometryChanged)
            {
                const Expansion = Encoder.beginComputePass(
                {
                    timestampWrites: Measure('Vertices + triangles')
                });
                Expansion.setBindGroup(0, GeometryBind, [0]);
                if (!External)
                {
                    Expansion.setPipeline(Deform);
                    Expansion.dispatchWorkgroups(Math.ceil(Scene.Vertices.length / 64));
                }
                Expansion.setPipeline(Expand);
                Expansion.dispatchWorkgroups(Math.ceil(Scene.Facets.length / 64));
                Expansion.end();
                const Refitting = Encoder.beginComputePass(
                {
                    timestampWrites: Measure('Construction BVH refit')
                });
                Refitting.setPipeline(Refit);
                Scene.DepthRanges.forEach((Range, Index) =>
                {
                    Refitting.setBindGroup(0, GeometryBind, [Index * 256]);
                    Refitting.dispatchWorkgroups(Math.ceil(Range[1] / 64));
                });
                Refitting.end();
                GeometryKey = CurrentGeometryKey;
                Metrics.Revision++;
            }
            if (FieldChanged)
            {
                Find('Status').textContent = `Building ${Resolution}³ signed volume from current triangles…`;
                const Building = Encoder.beginComputePass(
                {
                    timestampWrites: Measure('Signed volume rebuild')
                });
                Building.setBindGroup(0, CurrentBind);
                Building.setBindGroup(1, ConstructBind);
                Building.setPipeline(Components);
                Building.dispatchWorkgroups(1);
                Building.setPipeline(Construct);
                Building.dispatchWorkgroups(Math.ceil(Resolution / 4), Math.ceil(Resolution / 4), Math.ceil(
                    Resolution / 4));
                Building.end();
                FieldKey = CurrentGeometryKey;
                Metrics.FieldRevision = Metrics.Revision;
                Metrics.FieldBuilds++;
            }
            Encoder.clearBuffer(Counters);
            // 📝 SDF-only, slice and error views do not even issue a triangle raster pass.
            const NeedsRaster = [0, 6, 7, 8, 9].includes(Active.View);
            if (NeedsRaster)
            {
                const Visibility = Encoder.beginRenderPass(
                {
                    timestampWrites: Measure('Raster'),
                    colorAttachments: [Position, Normal, Colour].map(Texture => (
                    {
                        view: Texture.createView(),
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
                    }
                });
                Visibility.setPipeline(RasterProjection);
                Visibility.setBindGroup(0, CurrentBind);
                Visibility.draw(Scene.Facets.length * 3);
                Visibility.end();
            }
            const Lighting = Encoder.beginComputePass(
            {
                timestampWrites: Measure(Active.View === 7 ? 'Triangle reference GI' :
                    'Field shading / diagnostic')
            });
            Lighting.setPipeline(Shade);
            Lighting.setBindGroup(0, CurrentBind);
            Lighting.setBindGroup(1, ShadeBind);
            Lighting.setBindGroup(2, FieldBind);
            Lighting.dispatchWorkgroups(Math.ceil(Metrics.Width / 8), Math.ceil(Metrics.Height / 8));
            Lighting.end();
            const Displaying = Encoder.beginRenderPass(
            {
                timestampWrites: Measure('Display'),
                colorAttachments: [
                {
                    view: Context.getCurrentTexture().createView(),
                    clearValue: [0, 0, 0, 1],
                    loadOp: 'clear',
                    storeOp: 'store'
                }]
            });
            Displaying.setPipeline(Display);
            Displaying.setBindGroup(0, CurrentBind);
            Displaying.setBindGroup(1, DisplayBind);
            Displaying.draw(3);
            Displaying.end();
            if (Timestamp)
            {
                Encoder.resolveQuerySet(QuerySet, 0, Names.length * 2, Resolve, 0);
                Encoder.copyBufferToBuffer(Resolve, 0, Readings, 0, Names.length * 16);
            }
            Encoder.copyBufferToBuffer(Counters, 0, Readings, 128, 16);
            Device.queue.submit([Encoder.finish()]);
            await Device.queue.onSubmittedWorkDone();
            await Readings.mapAsync(GPUMapMode.READ);
            const Bytes = Readings.getMappedRange();
            if (Timestamp)
            {
                const Times = new BigUint64Array(Bytes, 0, 16);
                Metrics.Timings = Names.map((Name, Index) => (
                {
                    Name,
                    Milliseconds: Number(Times[Index * 2 + 1] - Times[Index * 2]) / 1e6
                }));
                if (FieldChanged) Metrics.LastBuildMilliseconds = Metrics.Timings.find(Entry => Entry.Name ===
                    'Signed volume rebuild').Milliseconds;
                Metrics.GpuMilliseconds = Metrics.Timings.reduce((Sum, Entry) => Sum + Entry.Milliseconds, 0);
            }
            Metrics.Work = Array.from(new Uint32Array(Bytes, 128, 4));
            Readings.unmap();
            Metrics.Completed++;
            Metrics.Amount = Amount;
            Metrics.Configuration = Active;
            Metrics.GeometryChanged = GeometryChanged;
            Metrics.FieldChanged = FieldChanged;
            Metrics.Rasterized = NeedsRaster;
            Find('Revision').textContent = `${Metrics.Revision} / ${Metrics.FieldRevision}`;
            Find('BuildTime').textContent = Timestamp ? Metrics.LastBuildMilliseconds.toFixed(1) + ' ms' :
            'n/a';
            Find('Gpu').textContent = Timestamp ? Metrics.GpuMilliseconds.toFixed(1) + ' ms' : 'n/a';
            Find('Work').textContent = Active.Counters ?
                `${Metrics.Work[0].toLocaleString()} marches · ${Metrics.Work[2].toLocaleString()} triangle queries · ${Metrics.Work[3]} exhausted` :
                'Lighting counters off · no progressive accumulation';
            Find('Timing').textContent = (Metrics.Timings || []).map(Entry =>
                `${Entry.Name} ${Entry.Milliseconds.toFixed(2)}`).join(' · ');
            Find('Status').textContent =
                `${Metrics.Width} × ${Metrics.Height} · ${Software ? 'Software WebGPU, not a gaming-GPU benchmark' : 'Native WebGPU'} · geometry ${Metrics.Revision} / field ${Metrics.FieldRevision}`;
        }
        finally
        {
            Metrics.Busy = false;
        }
    }

    function DecodeHalf(Reading)
    {
        const Sign = Reading & 32768 ? -1 : 1,
            Exponent = Reading >> 10 & 31,
            Fraction = Reading & 1023;
        return Sign * (Exponent === 0 ? Fraction * 2 ** -24 : Exponent === 31 ? Infinity : (1 + Fraction / 1024) *
            2 ** (Exponent - 15));
    }
    async function ReadTexture(Texture, Width, Height, Layers = 1, Float32 = false)
    {
        await Device.queue.onSubmittedWorkDone();
        const PixelBytes = Float32 ? 16 : 8,
            Pitch = Math.ceil(Width * PixelBytes / 256) * 256;
        const Reading = Allocate('Texture verification readback', Pitch * Height * Layers, GPUBufferUsage
            .COPY_DST | GPUBufferUsage.MAP_READ);
        const Encoder = Device.createCommandEncoder();
        Encoder.copyTextureToBuffer(
        {
            texture: Texture
        },
        {
            buffer: Reading,
            bytesPerRow: Pitch,
            rowsPerImage: Height
        }, [Width, Height, Layers]);
        Device.queue.submit([Encoder.finish()]);
        await Reading.mapAsync(GPUMapMode.READ);
        const Pixels = Float32 ? Array.from(new Float32Array(Reading.getMappedRange())) : Array.from(
            new Uint16Array(Reading.getMappedRange()), DecodeHalf);
        Reading.unmap();
        Reading.destroy();
        return {
            Width,
            Height,
            Layers,
            Pitch: Pitch / (Float32 ? 4 : 2),
            Pixels
        };
    }
    async function ReadResource(Resource, Bytes)
    {
        await Device.queue.onSubmittedWorkDone();
        const Reading = Allocate('Geometry verification readback', Bytes, GPUBufferUsage.COPY_DST |
            GPUBufferUsage.MAP_READ);
        const Encoder = Device.createCommandEncoder();
        Encoder.copyBufferToBuffer(Resource, 0, Reading, 0, Bytes);
        Device.queue.submit([Encoder.finish()]);
        await Reading.mapAsync(GPUMapMode.READ);
        const Copy = Reading.getMappedRange().slice(0);
        Reading.unmap();
        Reading.destroy();
        return Copy;
    }
    Object.assign(window.DistanceApp,
    {
        Device,
        Scene,
        TraceLayout,
        FieldLayout,
        CurrentBind,
        Parameters,
        Facets,
        Bounds,
        Vertices,
        Counters,
        ReadResource,
        Readback: Name => Name === 'Field' ? ReadTexture(Volume, Resolution, Resolution, Resolution) :
            Name === 'Position' ? ReadTexture(Position, Metrics.Width, Metrics.Height, 1, true) :
            ReadTexture(Image, Metrics.Width, Metrics.Height),
        SupplyPositions: Positions =>
        {
            const Stride = Positions.length / Scene.Vertices.length;
            if (Stride !== 3 && Stride !== 4) throw new Error(
                'Supply XYZ or XYZW for every scene vertex');
            const Incoming = new Float32Array(Scene.Vertices.length * 4);
            for (let Index = 0; Index < Scene.Vertices.length; Index++) Incoming.set([Positions[Index *
                    Stride], Positions[Index * Stride + 1], Positions[Index * Stride + 2], Scene
                .Vertices[Index][3]
            ], Index * 4);
            if (!Incoming.every(Number.isFinite)) throw new Error('Positions must be finite float32');
            for (let Index = 0; Index < Scene.Vertices.length; Index++)
                if (Scene.Vertices[Index][3])
                    for (let Axis = 0; Axis < 3; Axis++)
                        if (Incoming[Index * 4 + Axis] <= DomainMinimum[Axis] || Incoming[Index * 4 +
                                Axis] >= DomainMaximum[Axis]) throw new Error(
                            'Dynamic vertices must stay within the field domain');
            for (let Index = 0; Index < Scene.Vertices.length; Index++)
                if (!Scene.Vertices[Index][3])
                    for (let Axis = 0; Axis < 3; Axis++)
                        if (Math.abs(Incoming[Index * 4 + Axis] - Scene.Vertices[Index][Axis]) > 1e-6)
                            throw new Error(
                                'The analytic static room cannot be changed through this input');
            External = Incoming;
            ExternalRevision++;
            Configure(
            {
                Animate: false
            });
        },
        ResumeProcedural: () =>
        {
            External = null;
            ExternalRevision++;
        },
        Rebuild: () =>
        {
            FieldKey = '';
        },
        Stop: () =>
        {
            Stopped = true;
        },
        Step: async (Amount = Configuration.Amount) =>
        {
            if (!Manual || Metrics.Busy) throw new Error(
                'Use an idle manual view for deterministic stepping');
            if (!Number.isFinite(Amount) || Amount < 0 || Amount > 1) throw new Error(
                'Deformation must be in [0, 1]');
            await Render(Amount);
            return Metrics;
        }
    });
    Find('Rebuild').onclick = () =>
    {
        FieldKey = '';
    };
    Metrics.Ready = true;
    if (Manual) await Render(Configuration.Amount);
    else
    {
        function Animate(Now)
        {
            requestAnimationFrame(Animate);
            const Delta = Math.min(.1, (Now - LastTick) / 1000);
            LastTick = Now;
            if (Configuration.Animate) SimulationTime += Delta;
            if (document.hidden || Metrics.Error || Metrics.Busy || Stopped) return;
            const Amount = Configuration.Animate ? Configuration.Amount * (.5 - .5 * Math.cos(SimulationTime *
                .9)) : Configuration.Amount;
            Render(Amount).catch(Refuse);
        }
        requestAnimationFrame(Animate);
    }
}
Start().catch(Refuse);
