import {particleSpacingFor} from './gpu-particle-size.mjs?v=size1';
export const PRESETS={6144:[32,8,24],12000:[40,10,30],24000:[50,12,40],48000:[64,16,48],96000:[80,20,60]};

export class GPUSPH {
  static async create(device,count=6144,dimensions=[5,3.4]){
    const solver=new GPUSPH(device,count,dimensions);try{await solver.init();return solver;}catch(error){solver.destroy();throw error;}
  }
  constructor(device,count,dimensions=[5,3.4]){
    const [width,depth]=dimensions;if(!Number.isFinite(width)||!Number.isFinite(depth)||width<5||depth<3.4)throw new Error('Enter finite dimensions of at least 5 m wide and 3.4 m long. There is no fixed maximum.');
    this.device=device;this.count=count;this.spacing=particleSpacingFor(count,dimensions);this.h=2*this.spacing;
    this.radius=.40*this.spacing;this.mass=1000*this.spacing**3;this.dt=Math.min(.002,this.h/120);
    this.domain=[width/2,depth/2,Math.max(3,4*this.spacing)];this.grid=[Math.ceil(width/this.h),Math.ceil(this.domain[2]/this.h),Math.ceil(depth/this.h)];
    if(!PRESETS[count])throw new Error('Unsupported particle budget.');
    if(![...this.domain,this.mass,this.spacing,...this.grid].every(v=>Number.isFinite(Math.fround(v))&&v>0))throw new Error('These dimensions exceed numeric GPU precision. Choose a smaller footprint.');
    this.cells=this.grid.reduce((a,b)=>a*b,1);this.rock=[-1.05,.32,-.55,.48];this.body=[.9,.68,.25,.40];
    this.time=0;this.steps=0;this.current=0;this.maxSteps=12;this.viscosity=.003;this.velocitySmoothing=.004;this.cohesion=0;this.gravity=9.81;this.stiffness=100;this.useDFSPH=false;this.dfDivergenceIterations=4;this.dfDensityIterations=8;
  }
  async init(){
    const d=this.device;
    if(!Number.isSafeInteger(this.cells)||this.cells*4>Math.min(d.limits.maxStorageBufferBindingSize,d.limits.maxBufferSize)||Math.ceil(this.cells/128)>d.limits.maxComputeWorkgroupsPerDimension)throw new Error('This footprint exceeds this GPU’s grid buffer/dispatch limits. Try a less extreme size or aspect ratio.');
    if(this.count*97*4>Math.min(d.limits.maxStorageBufferBindingSize,d.limits.maxBufferSize)||Math.ceil(this.count/128)>d.limits.maxComputeWorkgroupsPerDimension)throw new Error('This particle size needs more buffer/dispatch capacity than this GPU supports. Choose larger particles.');
    const make=(size,usage,label)=>d.createBuffer({size:Math.ceil(size/4)*4,usage,label});
    this.buffers=[0,1].map(i=>make(this.count*32,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST|GPUBufferUsage.COPY_SRC,'Particle state '+i));
    this.df=make(this.count*16,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_SRC,'DFSPH factors, multipliers and residuals');
    this.heads=make(this.cells*4,GPUBufferUsage.STORAGE,'3D grid atomic heads');
    this.links=make(this.count*4,GPUBufferUsage.STORAGE,'Neighbor linked lists');
    this.density=make(this.count*8,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_SRC,'Density and pressure');
    this.neighbors=make(this.count*97*4,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_SRC,'Transposed accepted-neighbor cache');
    this.counters=make(16,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_SRC|GPUBufferUsage.COPY_DST,'Collision and safety counters');
    this.params=make(256*this.maxSteps,GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST,'Aligned substep parameters');
    const response=await fetch(new URL('./gpu-sph.wgsl?v=size1',import.meta.url));if(!response.ok)throw new Error('Could not load GPU compute shader: '+response.status);
    const shader=d.createShaderModule({code:await response.text(),label:'3D fluid compute: WCSPH / experimental DFSPH'});
    const info=await shader.getCompilationInfo();const errors=info.messages.filter(m=>m.type==='error');if(errors.length)throw new Error(errors.map(m=>`Compute shader ${m.lineNum}:${m.linePos}: ${m.message}`).join('\n'));
    const entries=[{binding:0,visibility:GPUShaderStage.COMPUTE,buffer:{type:'uniform',hasDynamicOffset:true,minBindingSize:144}}];
    for(let i=1;i<=8;i++)entries.push({binding:i,visibility:GPUShaderStage.COMPUTE,buffer:{type:i===1?'read-only-storage':'storage'}});
    this.layout=d.createBindGroupLayout({entries});const pipelineLayout=d.createPipelineLayout({bindGroupLayouts:[this.layout]});this.pipelines={};
    for(const entryPoint of ['clear','build','densities','integrate','dfFactors','dfDivergence','dfDensity','dfCorrect','dfDiagnostics','dfAdvance'])this.pipelines[entryPoint]=await d.createComputePipelineAsync({label:entryPoint,layout:pipelineLayout,compute:{module:shader,entryPoint}});
    this.groups=[0,1].map(i=>d.createBindGroup({layout:this.layout,entries:[{binding:0,resource:{buffer:this.params,offset:0,size:144}},...[[1,this.buffers[i]],[2,this.buffers[1-i]],[3,this.heads],[4,this.links],[5,this.density],[6,this.counters],[7,this.neighbors],[8,this.df]].map(([binding,buffer])=>({binding,resource:{buffer}}))]}));
    this.reset();
  }
  reset(){
    const original=this.domain[0]===2.5&&this.domain[1]===1.7;
    const nx=original?PRESETS[this.count][0]:Math.max(1,Math.floor(this.domain[0]*1.6/this.spacing));
    const nz=original?PRESETS[this.count][2]:Math.max(1,Math.floor(this.domain[1]*1.68/this.spacing));const data=new Float32Array(this.count*8);let index=0;
    for(let layer=0;index<this.count;layer++)for(let z=0;z<nz&&index<this.count;z++)for(let x=0;x<nx&&index<this.count;x++){
      const p=[(x-(nx-1)/2)*this.spacing,(layer+.55)*this.spacing,(z-(nz-1)/2)*this.spacing];
      if(p[1]>this.domain[2]-this.radius)throw new Error('The initial volume exceeds the tub height.');
      if([this.rock,this.body,[this.body[0],this.body[1]+.34,this.body[2]+.17,.24]].some(s=>Math.hypot(p[0]-s[0],p[1]-s[1],p[2]-s[2])<s[3]+this.radius))continue;
      data.set([...p,1,0,0,0,0],index*8);index++;
    }
    for(const buffer of this.buffers)this.device.queue.writeBuffer(buffer,0,data);
    this.device.queue.writeBuffer(this.counters,0,new Uint32Array(4));this.current=0;this.time=0;this.steps=0;
  }
  // CPU only prepares bounded control uniforms. It never integrates the particles.
  encode(encoder,steps,target,poke,timestamps){
    steps=Math.min(steps,this.maxSteps);if(!steps)return;const data=new Float32Array(64*this.maxSteps);const ints=new Uint32Array(data.buffer);
    let body=this.body.slice();
    this.device.queue.writeBuffer(this.counters,0,new Uint32Array(4));
    for(let step=0;step<steps;step++){
      const offset=step*64,previous=body.slice(),delta=target.slice(0,3).map((v,i)=>v-body[i]);
      const fraction=Math.min(1,2.5*this.dt/Math.max(.000001,Math.hypot(...delta)));
      for(let axis=0;axis<3;axis++)body[axis]+=delta[axis]*fraction;
      body[0]=Math.max(-this.domain[0]+.55,Math.min(this.domain[0]-.55,body[0]));body[2]=Math.max(-this.domain[1]+.55,Math.min(this.domain[1]-.55,body[2]));
      const dx=body[0]-this.rock[0],dz=body[2]-this.rock[2],r=this.rock[3]+body[3]+2*this.radius+.025;
      const clearance=Math.sqrt(Math.max(0,r*r-(body[1]-this.rock[1])**2)),distance=Math.hypot(dx,dz);
      if(distance<clearance){body[0]=this.rock[0]+(distance>.00001?dx/distance:1)*clearance;body[2]=this.rock[2]+(distance>.00001?dz/distance:0)*clearance;}
      const velocity=body.slice(0,3).map((v,i)=>(v-previous[i])/this.dt);
      data.set([this.domain[0],this.domain[1],this.domain[2],this.h],offset);
      data.set([this.mass,1000,this.useDFSPH?0:this.stiffness,this.viscosity],offset+4);data.set(body,offset+8);
      data.set([...velocity,this.dt],offset+12);data.set(poke,offset+16);
      ints.set([...this.grid,this.count],offset+20);data.set([this.radius,this.gravity,this.cohesion,.08],offset+24);data.set(this.rock,offset+28);data.set([this.velocitySmoothing,0,0,this.useDFSPH?1:0],offset+32);
    }
    this.device.queue.writeBuffer(this.params,0,data);
    for(let step=0;step<steps;step++){
      const pass=encoder.beginComputePass(step===0&&timestamps?{timestampWrites:{querySet:timestamps,beginningOfPassWriteIndex:0}}:{});
      pass.setBindGroup(0,this.groups[this.current],[step*256]);
      const stages=['clear','build','densities'];
      if(this.useDFSPH){stages.push('dfFactors');for(let i=0;i<this.dfDivergenceIterations;i++)stages.push('dfDivergence','dfCorrect');stages.push('integrate');for(let i=0;i<this.dfDensityIterations;i++)stages.push('dfDensity','dfCorrect');stages.push('dfDiagnostics','dfAdvance');}else stages.push('integrate');
      for(const name of stages){
        pass.setPipeline(this.pipelines[name]);pass.dispatchWorkgroups(Math.ceil((name==='clear'?this.cells:this.count)/128));
      }
      pass.end();this.current=1-this.current;this.time+=this.dt;this.steps++;
    }
    this.body=body;
  }
  async inspect(){
    // Optional asynchronous diagnostics. No readback is required for simulation/rendering.
    const d=this.device,size=this.count*32+16+(this.useDFSPH?this.count*16:0);const staging=d.createBuffer({size,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
    const encoder=d.createCommandEncoder();encoder.copyBufferToBuffer(this.buffers[this.current],0,staging,0,this.count*32);encoder.copyBufferToBuffer(this.counters,0,staging,this.count*32,16);if(this.useDFSPH)encoder.copyBufferToBuffer(this.df,0,staging,this.count*32+16,this.count*16);d.queue.submit([encoder.finish()]);
    await staging.mapAsync(GPUMapMode.READ);const range=staging.getMappedRange();const p=new Float32Array(range,0,this.count*8);const counters=Array.from(new Uint32Array(range,this.count*32,4));
    let finite=true,minY=Infinity,maxY=-Infinity,maxSpeed=0,insideSolids=0,outside=0,meanDensity=0,kineticEnergy=0,penetrations=[],densityRatios=[],compressionSum=0,overOnePercent=0,overFivePercent=0;
    for(let i=0;i<this.count;i++){
      const k=i*8,x=p[k],y=p[k+1],z=p[k+2];for(let j=0;j<8;j++)finite&&=Number.isFinite(p[k+j]);
      const ratio=p[k+3];densityRatios.push(ratio);compressionSum+=Math.max(0,ratio-1);
      if(ratio>1.01)overOnePercent++;if(ratio>1.05)overFivePercent++;
      minY=Math.min(minY,y);maxY=Math.max(maxY,y);maxSpeed=Math.max(maxSpeed,Math.hypot(p[k+4],p[k+5],p[k+6]));meanDensity+=p[k+3];kineticEnergy+=.5*this.mass*(p[k+4]**2+p[k+5]**2+p[k+6]**2);
      if(Math.abs(x)>this.domain[0]-this.radius+.001||Math.abs(z)>this.domain[1]-this.radius+.001||y<this.radius-.001||y>this.domain[2]-this.radius+.001)outside++;
      if(Math.hypot(x-this.rock[0],y-this.rock[1],z-this.rock[2])<this.rock[3]+this.radius-.002||Math.hypot(x-this.body[0],y-this.body[1],z-this.body[2])<this.body[3]+this.radius-.002||Math.hypot(x-this.body[0],y-this.body[1]-.34,z-this.body[2]-.17)<.24+this.radius-.002){insideSolids++;if(penetrations.length<4)penetrations.push({position:[x,y,z],rockGap:Math.hypot(x-this.rock[0],y-this.rock[1],z-this.rock[2])-this.rock[3]-this.radius,bodyGap:Math.hypot(x-this.body[0],y-this.body[1],z-this.body[2])-this.body[3]-this.radius,headGap:Math.hypot(x-this.body[0],y-this.body[1]-.34,z-this.body[2]-.17)-.24-this.radius});}
    }
    densityRatios.sort((a,b)=>a-b);
    const quantile=q=>densityRatios[Math.min(this.count-1,Math.floor(q*(this.count-1)))];
    // p.w records the last density solve BEFORE integration, not current-position density.
    // Reset writes placeholder ones, so callers must honor measured=false until stepped.
    const densityError={measured:this.steps>0,sampleTime:this.steps>0?this.time-this.dt:null,
      minRatio:quantile(0),medianRatio:quantile(.5),p95Ratio:quantile(.95),maxRatio:quantile(1),
      meanPositiveCompressionPercent:100*compressionSum/this.count,
      p95PositiveCompressionPercent:100*Math.max(0,quantile(.95)-1),
      maxPositiveCompressionPercent:100*Math.max(0,quantile(1)-1),
      particlesOverOnePercent:overOnePercent,particlesOverFivePercent:overFivePercent};
    let constraintDiagnostics=null;
    if(this.useDFSPH){const values=new Float32Array(range,this.count*32+16,this.count*4),residuals=[];let divergence=0;
      for(let i=0;i<this.count;i++){residuals.push(values[i*4+2]*100);divergence=Math.max(divergence,values[i*4+3]*100);}
      residuals.sort((a,b)=>a-b);
      constraintDiagnostics={measured:this.steps>0,densityIterations:this.dfDensityIterations,divergenceIterations:this.dfDivergenceIterations,
        meanPredictedCompressionPercent:residuals.reduce((a,b)=>a+b,0)/this.count,
        p95PredictedCompressionPercent:residuals[Math.floor(.95*(this.count-1))],maxPredictedCompressionPercent:residuals.at(-1),
        maxPositiveDivergencePerStepPercent:divergence,
        note:'Linearized pre-contact residual, not post-advection geometric volume error. Fixed iteration budget, no convergence guarantee.'};}
    staging.unmap();staging.destroy();return {algorithm:this.useDFSPH?'DFSPH experimental':'WCSPH',constraintDiagnostics,densityError,count:this.count,time:this.time,steps:this.steps,finite,minY,maxY,maxSpeed,meanDensityRatio:meanDensity/this.count,kineticEnergy,outside,insideSolids,penetrations,neighborTruncations:counters[0],speedCaps:counters[1],contacts:counters[2],invalidResets:counters[3]};
  }
  destroy(){for(const b of [...(this.buffers||[]),this.heads,this.links,this.density,this.counters,this.params,this.neighbors,this.df])b?.destroy();}
}
