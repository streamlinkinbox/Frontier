import { create, globals } from '../../../Experimental/VehicleHmiPrototype/Tools/node_modules/webgpu/index.js';
import * as Solver from '../../../Experimental/FluidEditor/Fluid/FluidKernels.js';
import * as Surface from '../../../Experimental/FluidEditor/Fluid/FluidRenderKernels.js';

Object.assign(globalThis, globals);
const Gpu = create(['backend=null']);
const Adapter = await Gpu.requestAdapter();
if (!Adapter) throw new Error('Dawn null adapter unavailable');
const Device = await Adapter.requestDevice();
let Count = 0;
for (const [Name, Source] of Object.entries({ ...Solver, ...Surface })) {
    if (!Name.endsWith('Kernel') || typeof Source !== 'string') continue;
    const Module = Device.createShaderModule({ label: Name, code: Source });
    const Report = await Module.getCompilationInfo();
    const Errors = Report.messages.filter(Message => Message.type === 'error');
    if (Errors.length) throw new Error(`${Name}: ${Errors.map(Error => Error.message).join('; ')}`);
    Device.pushErrorScope('validation');
    if (Name === 'SurfaceRenderKernel') {
        Device.createRenderPipeline({ layout: 'auto', vertex: { module: Module, entryPoint: 'VertexStage' },
            fragment: { module: Module, entryPoint: 'FragmentStage', targets: [{ format: 'bgra8unorm' }] },
            primitive: { topology: 'triangle-list' } });
    } else {
        Device.createComputePipeline({ layout: 'auto', compute: { module: Module, entryPoint: 'main' } });
    }
    const Failure = await Device.popErrorScope();
    if (Failure) throw new Error(`${Name}: ${Failure.message}`);
    Count++;
}
console.log(`PASS Dawn null WGSL compilation and pipeline creation: ${Count} liquid shader modules`);
Device.destroy();
