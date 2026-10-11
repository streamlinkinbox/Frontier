//============================================================================================================================================
//                                                            DEFORMATIONPANEL.JS
//============================================================================================================================================
// 📦 Standalone WebGPU deformation demonstration, synchronized geometry/refit/lighting and explicit accumulation invalidation.

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
    TyreCount,
    RingCount,
    SectionCount,
    PoseCount
}
from './DeformationSpecification.js';
import
{
    Deformation,
    Raster,
    Integration,
    Presentation
}
from './TransportIntegrator.js';

const Find = Identifier => document.getElementById(Identifier);
const Configuration = {
    ...Defaults
};
const Query = new URLSearchParams(location.search);
const Manual = Query.has('manual');
const Metrics = {
    Ready: false,
    Error: null,
    Completed: 0,
    Samples: 0,
    Revision: 0,
    Busy: false,
    Compression: 0,
    GeometryMilliseconds: 0,
    Width: 0,
    Height: 0,
    Errors: []
};
const Canvas = Find('View');
const Camera = new THREE.PerspectiveCamera(42, innerWidth / innerHeight, .05, 60);
Camera.coordinateSystem = THREE.WebGPUCoordinateSystem;
const Controls = new OrbitControls(Camera, Canvas);
Controls.enableDamping = true;
Controls.minDistance = 3;
Controls.maxDistance = 15;
Controls.maxPolarAngle = Math.PI * .49;

function ResetView()
{
    Camera.position.set(4.7, 3.05, 7.6);
    Controls.target.set(0, 1.25, 0);
    Controls.update();
}
ResetView();

function Refresh()
{
    for (const Name of ['Compression', 'Samples', 'Bounces', 'Scale', 'Exposure', 'Power'])
    {
        Find(Name).value = Configuration[Name];
        Find(Name + 'Reading').textContent = Configuration[Name];
    }
    Find('Source').value = Configuration.Source;
    Find('Frozen').checked = Configuration.Frozen;
    Find('Wire').checked = Configuration.Wire;
    Find('Diagnostic').value = Configuration.View;
    Find('Pause').disabled = Configuration.Source === 2;
    Find('Pause').textContent = Configuration.Animate ? 'Pause deformation' : 'Animate deformation';
    Find('GeometryMode').textContent = Configuration.Frozen ? 'FROZEN REST GEOMETRY · WRONG WHEN DEFORMED' :
        'CURRENT TRIANGLES · REFITTED BVH';
    Find('GeometryMode').classList.toggle('Warning', Configuration.Frozen);
}

function Configure(Changes)
{
    const Limits = {
        Compression: [0, .65],
        Samples: [1, 16],
        Bounces: [1, 4],
        Scale: [.3, 1],
        Exposure: [.2, 3],
        Power: [0, 2]
    };
    for (const [Name, Reading] of Object.entries(Changes))
    {
        if (!(Name in Configuration)) continue;
        if (Limits[Name])
        {
            if (!Number.isFinite(Number(Reading))) throw new Error('Control must be finite');
            Configuration[Name] = Math.max(Limits[Name][0], Math.min(Limits[Name][1], Number(Reading)));
        }
        else if (['Source', 'View'].includes(Name)) Configuration[Name] = Math.max(0, Math.min(Name === 'Source' ? 2 :
            3, Math.round(Number(Reading) || 0)));
        else Configuration[Name] = !!Reading;
    }
    Configuration.Samples = Math.round(Configuration.Samples);
    Configuration.Bounces = Math.round(Configuration.Bounces);
    Refresh();
}
for (const Name of ['Compression', 'Samples', 'Bounces', 'Scale', 'Exposure', 'Power'])
    Find(Name).oninput = Event => Configure(
    {
        [Name]: Number(Event.target.value)
    });
Find('Source').onchange = Event => Configure(
{
    Source: Number(Event.target.value)
});
Find('Diagnostic').onchange = Event => Configure(
{
    View: Number(Event.target.value)
});
for (const Name of ['Frozen', 'Wire']) Find(Name).onchange = Event => Configure(
{
    [Name]: Event.target.checked
});
let AnimationLimit = Defaults.Compression;
Find('Pause').onclick = () =>
{
    if (Configuration.Animate)
    {
        AnimationLimit = Configuration.Compression;
        Configure(
        {
            Animate: false,
            Compression: Metrics.Compression
        });
    }
    else Configure(
    {
        Animate: true,
        Compression: Math.max(Configuration.Compression, AnimationLimit) || Defaults.Compression
    });
};
Find('Reset').onclick = ResetView;
Find('Rest').onclick = () => Configure(
{
    Animate: false,
    Compression: 0
});
Find('Loaded').onclick = () => Configure(
{
    Animate: false,
    Compression: .65
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
window.DeformationApp = {
    Configuration,
    Metrics,
    Camera,
    Controls,
    Configure,
    ResetView
};

async function Start()
{
    if (!navigator.gpu) throw new Error(
        'WebGPU is required. Open this page in a secure context using a supported browser. There is no WebGL fallback.'
    );
    const Adapter = await navigator.gpu.requestAdapter(
    {
        powerPreference: 'high-performance'
    });
    if (!Adapter) throw new Error('No WebGPU adapter is available.');
    const Information = Adapter.info || await Adapter.requestAdapterInfo?.() ||
    {};
    const Timestamp = Adapter.features.has('timestamp-query');
    const Device = await Adapter.requestDevice(
    {
        requiredFeatures: Timestamp ? ['timestamp-query'] : []
    });
    Device.addEventListener('uncapturederror', Event => Refuse(Event.error));
    Device.lost.then(Reason => Refuse(new Error('WebGPU device lost: ' + Reason.message)));
    const Software = /swiftshader|software|llvmpipe/i.test(Object.values(Information).join(' ') + Information
        .architecture);
    Metrics.Adapter = {
        Vendor: Information.vendor,
        Architecture: Information.architecture,
        Description: Information.description,
        Timestamp,
        Software
    };
    Find('Adapter').textContent = (Information.architecture || Information.vendor || 'WebGPU adapter') + (Software ?
        ' · software, not a hardware benchmark' : ' · native WebGPU');
    const Context = Canvas.getContext('webgpu');
    const Format = navigator.gpu.getPreferredCanvasFormat();
    Context.configure(
    {
        device: Device,
        format: Format,
        alphaMode: 'opaque'
    });
    const Scene = ConstructScene();
    Find('SceneFacts').textContent =
        `${Scene.Quads.length.toLocaleString()} quads → ${Scene.Facets.length.toLocaleString()} traced triangles`;
    const Storage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC;

    function Allocate(Label, Content, Usage = Storage)
    {
        const Bytes = typeof Content === 'number' ? Content : Content.byteLength;
        const Resource = Device.createBuffer(
        {
            label: Label,
            size: Math.max(16, Bytes),
            usage: Usage
        });
        if (typeof Content !== 'number') Device.queue.writeBuffer(Resource, 0, Content);
        return Resource;
    }
    const Parameters = Allocate('View and deformation parameters', 144, GPUBufferUsage.UNIFORM | GPUBufferUsage
        .COPY_DST);
    const Rest = Allocate('Rest vertices', new Float32Array(Scene.Vertices.flat()));
    const Vertices = Allocate('Current deformed vertices', new Float32Array(Scene.Vertices.flat()));
    const Connectivity = Allocate('Fixed quad triangulation', new Uint32Array(Scene.Facets.flat()));
    const Facets = Allocate('Current expanded triangles', Scene.FacetBytes);
    const Bounds = Allocate('Refitted bounds', Scene.BoundsBytes);
    const FrozenFacets = Allocate('Deliberately stale rest triangles', Scene.FacetBytes);
    const FrozenBounds = Allocate('Deliberately stale rest bounds', Scene.BoundsBytes);
    const DepthIndices = Allocate('Bottom-up depth permutation', new Uint32Array(Scene.DepthIndices));
    const DepthRanges = Allocate('Refit dispatch ranges', Scene.DepthRanges.length * 256, GPUBufferUsage.UNIFORM |
        GPUBufferUsage.COPY_DST);
    Scene.DepthRanges.forEach((Range, Index) => Device.queue.writeBuffer(DepthRanges, Index * 256, new Uint32Array(
        Range)));
    const Poses = Device.createTexture(
    {
        label: 'Recorded vertex animation texture',
        size: [RingCount, SectionCount * PoseCount],
        format: 'rgba32float',
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
    });
    Device.queue.writeTexture(
    {
        texture: Poses
    }, Scene.Poses,
    {
        bytesPerRow: RingCount * 16
    }, [RingCount, SectionCount * PoseCount]);
    const AllStages = GPUShaderStage.COMPUTE | GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
    const Uniform = (Slot, Dynamic = false) => (
    {
        binding: Slot,
        visibility: AllStages,
        buffer:
        {
            type: 'uniform',
            hasDynamicOffset: Dynamic
        }
    });
    const StorageEntry = (Slot, Write = false) => (
    {
        binding: Slot,
        visibility: Write ? GPUShaderStage.COMPUTE : AllStages,
        buffer:
        {
            type: Write ? 'storage' : 'read-only-storage'
        }
    });
    const TextureEntry = Slot => (
    {
        binding: Slot,
        visibility: AllStages,
        texture:
        {
            sampleType: 'unfilterable-float'
        }
    });
    const GeometryLayout = Device.createBindGroupLayout(
    {
        entries: [Uniform(0), StorageEntry(1), StorageEntry(2, true),
            StorageEntry(3), StorageEntry(4, true), StorageEntry(5, true), StorageEntry(6), Uniform(7,
                true), TextureEntry(8)
        ]
    });
    const TraceLayout = Device.createBindGroupLayout(
    {
        entries: [Uniform(0), StorageEntry(1), StorageEntry(2)]
    });
    const IntegrationLayout = Device.createBindGroupLayout(
    {
        entries: [0, 1, 2, 3].map(TextureEntry).concat([
        {
            binding: 4,
            visibility: GPUShaderStage.COMPUTE,
            storageTexture:
            {
                access: 'write-only',
                format: 'rgba32float'
            }
        }])
    });
    const DisplayLayout = Device.createBindGroupLayout(
    {
        entries: [TextureEntry(0), TextureEntry(1)]
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
    }, Poses.createView()]);
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
    const FrozenBind = Bind(TraceLayout, [
    {
        buffer: Parameters
    },
    {
        buffer: FrozenFacets
    },
    {
        buffer: FrozenBounds
    }]);
    async function Compile(Label, Code)
    {
        const Program = Device.createShaderModule(
        {
            label: Label,
            code: Code
        });
        const Compilation = await Program.getCompilationInfo();
        const Errors = Compilation.messages.filter(Message => Message.type === 'error');
        if (Errors.length) throw new Error(Label + ': ' + Errors.map(Message =>
            `${Message.lineNum}: ${Message.message}`).join('\n'));
        return Program;
    }
    const DeformationProgram = await Compile('Deformation and refit', Deformation);
    const GeometryProjection = Device.createPipelineLayout(
    {
        bindGroupLayouts: [GeometryLayout]
    });
    const Deform = await Device.createComputePipelineAsync(
    {
        layout: GeometryProjection,
        compute:
        {
            module: DeformationProgram,
            entryPoint: 'Deform'
        }
    });
    const Expand = await Device.createComputePipelineAsync(
    {
        layout: GeometryProjection,
        compute:
        {
            module: DeformationProgram,
            entryPoint: 'Expand'
        }
    });
    const Refit = await Device.createComputePipelineAsync(
    {
        layout: GeometryProjection,
        compute:
        {
            module: DeformationProgram,
            entryPoint: 'Refit'
        }
    });
    const RasterProgram = await Compile('Quad raster visibility', Raster);
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
    const Integrator = await Device.createComputePipelineAsync(
    {
        layout: Device.createPipelineLayout(
        {
            bindGroupLayouts: [TraceLayout, IntegrationLayout]
        }),
        compute:
        {
            module: await Compile('Diffuse path integration', Integration),
            entryPoint: 'Integrate'
        }
    });
    const DisplayProgram = await Compile('Radiance display', Presentation);
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
    let Textures = [],
        Position, Normal, Colour, Depth, Radiance, IntegratorBind, DisplayBind, Parity = 0;
    let GeometryKey = '',
        AccumulationKey = '',
        LastSize = '',
        External = null,
        ExternalRevision = 0;
    let SimulationTime = 0,
        LastTick = performance.now(),
        Stopped = false;
    const UniformBytes = new ArrayBuffer(144),
        UniformFloats = new Float32Array(UniformBytes),
        UniformIntegers = new Uint32Array(UniformBytes);
    const Projection = new THREE.Matrix4();
    const QuerySet = Timestamp ? Device.createQuerySet(
    {
        type: 'timestamp',
        count: 12
    }) : null;
    const QueryResolve = Timestamp ? Allocate('Timestamp resolve', 96, GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage
        .COPY_SRC) : null;
    const QueryRead = Timestamp ? Allocate('Timestamp readback', 96, GPUBufferUsage.MAP_READ | GPUBufferUsage
        .COPY_DST) : null;

    function Resize()
    {
        const Requested = Math.min(800, Number(Query.get('width')) || innerWidth * Configuration.Scale);
        const Width = Math.max(96, Math.floor(Math.min(Requested, 600 * innerWidth / innerHeight) / 8) * 8);
        const Height = Math.max(64, Math.floor(Width * innerHeight / innerWidth / 8) * 8);
        const Key = `${Width}/${Height}/${innerWidth}/${innerHeight}`;
        if (Key === LastSize) return;
        LastSize = Key;
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
        const Sampled = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC;
        Position = Texture('Current raster world positions', 'rgba32float', Sampled | GPUTextureUsage
            .RENDER_ATTACHMENT);
        Normal = Texture('Current raster normals', 'rgba16float', Sampled | GPUTextureUsage.RENDER_ATTACHMENT);
        Colour = Texture('Reflectance and actual quad edges', 'rgba16float', Sampled | GPUTextureUsage
            .RENDER_ATTACHMENT);
        Depth = Texture('Raster depth', 'depth32float', GPUTextureUsage.RENDER_ATTACHMENT);
        Radiance = [0, 1].map(Index => Texture(`Progressive radiance ${Index}`, 'rgba32float', Sampled |
            GPUTextureUsage.STORAGE_BINDING));
        IntegratorBind = [0, 1].map(Index => Bind(IntegrationLayout, [Position.createView(), Normal.createView(),
            Colour.createView(),
            Radiance[1 - Index].createView(), Radiance[Index].createView()
        ]));
        DisplayBind = [0, 1].map(Index => Bind(DisplayLayout, [Radiance[Index].createView(), Colour.createView()]));
        Canvas.width = Math.round(innerWidth * Math.min(devicePixelRatio, 1.5));
        Canvas.height = Math.round(innerHeight * Math.min(devicePixelRatio, 1.5));
        Camera.aspect = innerWidth / innerHeight;
        Camera.updateProjectionMatrix();
        Metrics.Width = Width;
        Metrics.Height = Height;
        Metrics.Samples = 0;
        AccumulationKey = '';
    }
    async function Render(Compression)
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
            Controls.update();
            Camera.updateMatrixWorld();
            Projection.multiplyMatrices(Camera.projectionMatrix, Camera.matrixWorldInverse);
            const NewGeometryKey = `${Compression}/${Active.Source}/${ExternalRevision}`;
            const GeometryChanged = GeometryKey !== NewGeometryKey;
            const NewAccumulationKey = NewGeometryKey +
                `/${Active.Frozen}/${Active.Bounces}/${Active.View}/${Active.Power}/` + Projection.elements
                .join(',');
            if (AccumulationKey !== NewAccumulationKey) Metrics.Samples = 0;
            AccumulationKey = NewAccumulationKey;
            UniformFloats.set(Projection.elements, 0);
            UniformFloats.set([...Camera.position.toArray(), 0], 16);
            UniformIntegers.set([Metrics.Width, Metrics.Height, Metrics.Samples, Active.Bounces], 20);
            UniformFloats.set([Compression, Active.Power, Active.Exposure, 0], 24);
            UniformIntegers.set([Active.Source, +Active.Frozen, Active.View, Active.Samples], 28);
            UniformIntegers.set([TyreCount, Scene.Vertices.length, Scene.Facets.length, +Active.Wire], 32);
            Device.queue.writeBuffer(Parameters, 0, UniformBytes);
            if (GeometryChanged && Active.Source === 2)
            {
                if (!External) throw new Error(
                    'Supply live solver positions through DeformationApp.SupplyPositions first.');
                const Full = new Float32Array(Scene.Vertices.flat());
                Full.set(External);
                // 📝 External tyre vertices are already in world space; the rigid rim follows the requested axle compression.
                for (let Index = TyreCount; Index < Scene.Vertices.length; Index++)
                    if (Full[Index * 4 + 3] === 2) Full[Index * 4 + 1] -= Compression;
                Device.queue.writeBuffer(Vertices, 0, Full);
            }
            const Encoder = Device.createCommandEncoder();
            const Names = [];
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
                if (Active.Source !== 2)
                {
                    Expansion.setPipeline(Deform);
                    Expansion.dispatchWorkgroups(Math.ceil(Scene.Vertices.length / 64));
                }
                Expansion.setPipeline(Expand);
                Expansion.dispatchWorkgroups(Math.ceil(Scene.Facets.length / 64));
                Expansion.end();
                const Refitting = Encoder.beginComputePass(
                {
                    timestampWrites: Measure('BVH refit')
                });
                Refitting.setPipeline(Refit);
                Scene.DepthRanges.forEach((Range, Index) =>
                {
                    Refitting.setBindGroup(0, GeometryBind, [Index * 256]);
                    Refitting.dispatchWorkgroups(Math.ceil(Range[1] / 64));
                });
                Refitting.end();
                GeometryKey = NewGeometryKey;
                Metrics.Revision++;
            }
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
            const Transport = Encoder.beginComputePass(
            {
                timestampWrites: Measure('Diffuse paths')
            });
            Transport.setPipeline(Integrator);
            Transport.setBindGroup(0, Active.Frozen ? FrozenBind : CurrentBind);
            Transport.setBindGroup(1, IntegratorBind[Parity]);
            Transport.dispatchWorkgroups(Math.ceil(Metrics.Width / 8), Math.ceil(Metrics.Height / 8));
            Transport.end();
            const Presentation = Encoder.beginRenderPass(
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
            Presentation.setPipeline(Display);
            Presentation.setBindGroup(0, CurrentBind);
            Presentation.setBindGroup(1, DisplayBind[Parity]);
            Presentation.draw(3);
            Presentation.end();
            if (Timestamp)
            {
                Encoder.resolveQuerySet(QuerySet, 0, Names.length * 2, QueryResolve, 0);
                Encoder.copyBufferToBuffer(QueryResolve, 0, QueryRead, 0, Names.length * 16);
            }
            Device.queue.submit([Encoder.finish()]);
            await Device.queue.onSubmittedWorkDone();
            if (Timestamp)
            {
                await QueryRead.mapAsync(GPUMapMode.READ);
                const Readings = new BigUint64Array(QueryRead.getMappedRange());
                Metrics.Timings = Names.map((Name, Index) => (
                {
                    Name,
                    Milliseconds: Number(Readings[Index * 2 + 1] - Readings[Index * 2]) / 1e6
                }));
                Metrics.GpuMilliseconds = Metrics.Timings.reduce((Sum, Entry) => Sum + Entry.Milliseconds, 0);
                QueryRead.unmap();
            }
            Metrics.Samples += Active.Samples;
            Metrics.Completed++;
            Metrics.Compression = Compression;
            Metrics.TraceRevision = Active.Frozen ? 0 : Metrics.Revision;
            Metrics.GeometryChanged = GeometryChanged;
            Metrics.Configuration = Active;
            Parity = 1 - Parity;
            Find('SamplesReadingTotal').textContent = Metrics.Samples.toLocaleString();
            Find('Revision').textContent = `${Metrics.Revision} / ${Metrics.TraceRevision}`;
            Find('Gpu').textContent = Timestamp ? Metrics.GpuMilliseconds.toFixed(1) + ' ms' : 'n/a';
            Find('Contact').textContent = Compression.toFixed(3) + ' m';
            Find('Timing').textContent = (Metrics.Timings || []).map(Entry =>
                `${Entry.Name} ${Entry.Milliseconds.toFixed(2)}`).join(' · ');
            Find('Status').textContent =
                `${Metrics.Width} × ${Metrics.Height} · ${Active.Animate ? 'Moving surface: accumulation resets' : 'Paused surface: samples converge'} · ${Software ? 'Software execution, not a hardware speed result' : 'Native WebGPU'}`;
        }
        finally
        {
            Metrics.Busy = false;
        }
    }
    async function ReadResource(Resource, Bytes)
    {
        await Device.queue.onSubmittedWorkDone();
        const Reading = Allocate('Geometry verification', Bytes, GPUBufferUsage.COPY_DST | GPUBufferUsage
            .MAP_READ);
        const Encoder = Device.createCommandEncoder();
        Encoder.copyBufferToBuffer(Resource, 0, Reading, 0, Bytes);
        Device.queue.submit([Encoder.finish()]);
        await Reading.mapAsync(GPUMapMode.READ);
        const Copy = Reading.getMappedRange().slice(0);
        Reading.unmap();
        Reading.destroy();
        return Copy;
    }
    async function Readback(Name = 'Radiance')
    {
        await Device.queue.onSubmittedWorkDone();
        const Pitch = Math.ceil(Metrics.Width * 16 / 256) * 256;
        const Reading = Allocate('Linear image verification', Pitch * Metrics.Height, GPUBufferUsage.COPY_DST |
            GPUBufferUsage.MAP_READ);
        const Encoder = Device.createCommandEncoder();
        Encoder.copyTextureToBuffer(
        {
            texture: Name === 'Position' ? Position : Radiance[1 - Parity]
        },
        {
            buffer: Reading,
            bytesPerRow: Pitch
        }, [Metrics.Width, Metrics.Height]);
        Device.queue.submit([Encoder.finish()]);
        await Reading.mapAsync(GPUMapMode.READ);
        const Pixels = Array.from(new Float32Array(Reading.getMappedRange()));
        Reading.unmap();
        Reading.destroy();
        return {
            Width: Metrics.Width,
            Height: Metrics.Height,
            Pitch,
            Pixels
        };
    }
    Object.assign(window.DeformationApp,
    {
        Device,
        Scene,
        TraceLayout,
        CurrentBind,
        FrozenBind,
        Parameters,
        Facets,
        Bounds,
        Readback,
        ReadResource,
        Vertices,
        Metrics,
        Render,
        SupplyPositions: Positions =>
        {
            if (Positions.length !== TyreCount * 3 && Positions.length !== TyreCount * 4)
                throw new Error('Expected one XYZ or XYZW position per tyre vertex');
            if (!Array.from(Positions).every(Number.isFinite)) throw new Error(
                'All solver positions must be finite');
            const Stride = Positions.length / TyreCount;
            const Incoming = new Float32Array(TyreCount * 4);
            for (let Index = 0; Index < TyreCount; Index++) Incoming.set([Positions[Index * Stride],
                Positions[Index * Stride + 1], Positions[Index * Stride + 2], 1
            ], Index * 4);
            if (!Incoming.every(Number.isFinite)) throw new Error(
                'Solver positions must fit finite float32 coordinates');
            External = Incoming;
            ExternalRevision++;
            Configure(
            {
                Source: 2,
                Animate: false
            });
        },
        Step: async (Compression = Configuration.Compression, Samples = 1) =>
        {
            if (!Manual || Metrics.Busy) throw new Error(
                'Use an idle manual view for deterministic stepping');
            if (!Number.isInteger(Samples) || Samples < 1 || Samples > 1024) throw new Error(
                'Render count must be an integer in [1, 1024]');
            if (!Number.isFinite(Compression) || Compression < 0 || Compression > .65) throw new Error(
                'Compression must be in [0, .65]');
            for (let Index = 0; Index < Samples; Index++) await Render(Compression);
            return Metrics;
        },
        ResetAccumulation: () =>
        {
            AccumulationKey = '';
        },
        Stop: () =>
        {
            Stopped = true;
        }
    });
    Metrics.Ready = true;
    if (Manual) await Render(Configuration.Compression);
    else
    {
        function Animate(Now)
        {
            requestAnimationFrame(Animate);
            const Delta = Math.min(.1, (Now - LastTick) / 1000);
            LastTick = Now;
            if (Configuration.Animate) SimulationTime += Delta;
            if (document.hidden || Metrics.Error || Metrics.Busy || Stopped) return;
            const Compression = Configuration.Animate ? Configuration.Compression * (.5 - .5 * Math.cos(
                SimulationTime * 1.4)) : Configuration.Compression;
            Render(Compression).catch(Refuse);
        }
        requestAnimationFrame(Animate);
    }
}
Start().catch(Refuse);
