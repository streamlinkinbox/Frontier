// Damped wave equation: h_tt = c² ∇²h - damping h_t.
// Fixed XZ domain, reflecting walls/obstacles, fixed-step velocity-Verlet integration with symmetric damping.
export class SurfaceSolver {
  constructor(n=96, size=8, width=size) {
    this.size=size;this.width=width;this.base=n;
    this.maxN=Math.floor(Math.sqrt(160000*size/width));
    while((Math.round((this.maxN-1)*width/size)+1)*this.maxN>160000)this.maxN--;this.speed=1.4;this.damping=.45;
    this.time=0;this.lastDt=0;this.accumulator=0;this.droppedTime=0;this.clampHits=0;this.adaptive=true;this.refinements=0;
    this.obstacles=[{x:-2.4,z:-1.65,r:.65},{x:2.7,z:1.65,r:.46}];
    this.allocate(n);
  }
  allocate(n) {
    this.n=n;this.nx=Math.round((n-1)*this.width/this.size)+1;this.dx=this.width/(this.nx-1);this.dz=this.size/(n-1);
    this.height=new Float32Array(this.nx*n);this.velocity=new Float32Array(this.nx*n);
    this.accel=new Float32Array(this.nx*n);this.solid=new Uint8Array(this.nx*n);
    for(let j=0;j<n;j++)for(let i=0;i<this.nx;i++) {
      const x=i*this.dx-this.width/2,z=j*this.dz-this.size/2;
      this.solid[j*this.nx+i]=this.obstacles.some(o=>(x-o.x)**2+(z-o.z)**2<o.r**2)?1:0;
    }
  }
  sampleField(field,x,z,n=this.n) {
    const nx=Math.round((n-1)*this.width/this.size)+1;
    const gx=Math.max(0,Math.min(nx-1,(x/this.width+.5)*(nx-1)));
    const gz=Math.max(0,Math.min(n-1,(z/this.size+.5)*(n-1)));
    const i=Math.min(nx-2,Math.floor(gx)),j=Math.min(n-2,Math.floor(gz));
    const a=gx-i,b=gz-j,k=j*nx+i;
    return (field[k]*(1-a)+field[k+1]*a)*(1-b)+(field[k+nx]*(1-a)+field[k+nx+1]*a)*b;
  }
  sample(x,z){return this.sampleField(this.height,x,z);}
  gradient(x,z) {
    const d=this.dx,e=this.dz;
    return {x:(this.sample(x+d,z)-this.sample(x-d,z))/(2*d),z:(this.sample(x,z+e)-this.sample(x,z-e))/(2*e)};
  }
  isWet(x,z,margin=0) {
    return Math.abs(x)<this.width/2-margin&&Math.abs(z)<this.size/2-margin&&!this.obstacles.some(o=>(x-o.x)**2+(z-o.z)**2<(o.r+margin)**2);
  }
  disturb(x,z,strength=.65,radius=.3) {
    if(!this.isWet(x,z))return;
    const n=this.n,nx=this.nx,d=this.dx,e=this.dz;
    const loX=Math.max(0,Math.floor((x-radius*3+this.width/2)/d)),hiX=Math.min(nx-1,Math.ceil((x+radius*3+this.width/2)/d));
    const loZ=Math.max(0,Math.floor((z-radius*3+this.size/2)/e)),hiZ=Math.min(n-1,Math.ceil((z+radius*3+this.size/2)/e));
    for(let j=loZ;j<=hiZ;j++)for(let i=loX;i<=hiX;i++) {
      const k=j*nx+i;if(this.solid[k])continue;
      const q=((i*d-this.width/2-x)**2+(j*e-this.size/2-z)**2)/(2*radius*radius);
      this.height[k]=Math.max(-.55,Math.min(.55,this.height[k]+strength*.32*(1-q)*Math.exp(-q)));
    }
  }
  maxSlope() {
    let max=0;const n=this.n,nx=this.nx,h=this.height,d=2*this.dx,e=2*this.dz;
    for(let j=1;j<n-1;j++)for(let i=1;i<nx-1;i++) {
      const k=j*nx+i;if(this.solid[k]||this.solid[k-1]||this.solid[k+1]||this.solid[k-nx]||this.solid[k+nx])continue;
      max=Math.max(max,Math.hypot((h[k+1]-h[k-1])/d,(h[k+nx]-h[k-nx])/e));
    }
    return max;
  }
  refineIfNeeded() {
    if(!this.adaptive||this.n>=Math.min(this.base*2,this.maxN))return false;
    const slope=this.maxSlope();
    const desired=slope>.85?this.base*2:slope>.4?Math.round(this.base*1.5):this.n;
    if(desired<=this.n)return false;
    const oldH=this.height,oldV=this.velocity,oldN=this.n;
    this.allocate(Math.min(this.maxN,desired));
    for(let j=0;j<this.n;j++)for(let i=0;i<this.nx;i++) {
      const k=j*this.nx+i;if(this.solid[k])continue;
      const x=i*this.dx-this.width/2,z=j*this.dz-this.size/2;
      this.height[k]=this.sampleField(oldH,x,z,oldN);
      this.velocity[k]=this.sampleField(oldV,x,z,oldN);
    }
    this.refinements++;return true;
  }
  stableDt(){return Math.min(1/90,.35/(this.speed*Math.sqrt(1/(this.dx*this.dx)+1/(this.dz*this.dz))));}
  acceleration() {
    const n=this.n,nx=this.nx,h=this.height,a=this.accel,solid=this.solid;
    const cx=this.speed*this.speed/(this.dx*this.dx),cz=this.speed*this.speed/(this.dz*this.dz);
    for(let j=0;j<n;j++)for(let i=0;i<nx;i++){
      const k=j*nx+i;if(solid[k]){a[k]=0;continue;}
      const center=h[k];
      const l=i>0&&!solid[k-1]?h[k-1]:center,r=i<nx-1&&!solid[k+1]?h[k+1]:center;
      const b=j>0&&!solid[k-nx]?h[k-nx]:center,t=j<n-1&&!solid[k+nx]?h[k+nx]:center;
      a[k]=cx*(l+r-2*center)+cz*(b+t-2*center);
    }
  }
  step(dt) {
    if(!Number.isFinite(dt)||!(dt>0)||dt>this.stableDt()*1.00001)throw new RangeError('Time step exceeds CFL safety bound');
    const h=this.height,v=this.velocity,a=this.accel,solid=this.solid;
    const decay=Math.exp(-this.damping*dt*.5);
    // Strang-split drag and velocity-Verlet. Never take frame-remainder steps.
    this.acceleration();
    for(let k=0;k<h.length;k++){
      if(solid[k])continue;
      v[k]=v[k]*decay+.5*dt*a[k];
      h[k]+=dt*v[k];
    }
    this.acceleration();
    for(let k=0;k<h.length;k++){
      if(solid[k])continue;
      v[k]=(v[k]+.5*dt*a[k])*decay;
      if(Math.abs(h[k])>.55){h[k]=Math.sign(h[k])*.55;v[k]=0;this.clampHits++;}
    }
    this.time+=dt;this.lastDt=dt;
  }
  advance(seconds,maxSteps=32,beforeStep=null) {
    if(!Number.isFinite(seconds)||seconds<0)throw new RangeError('Invalid frame duration');
    const startTime=this.time,accepted=Math.min(seconds,.1);
    this.droppedTime+=seconds-accepted;this.accumulator+=accepted;
    const dt=this.stableDt();let steps=0;
    while(this.accumulator+1e-12>=dt&&steps<maxSteps){
      if(beforeStep)beforeStep(dt);
      this.step(dt);this.accumulator=Math.max(0,this.accumulator-dt);steps++;
    }
    // Bound backlog rather than speeding the physics up after a stall.
    if(this.accumulator>=dt){const keep=this.accumulator%dt;this.droppedTime+=this.accumulator-keep;this.accumulator=keep;}
    return this.time-startTime;
  }
  energy(){
    let e=0;const n=this.n,nx=this.nx,h=this.height,v=this.velocity,c2=this.speed*this.speed;
    for(let j=0;j<n;j++)for(let i=0;i<nx;i++){
      const k=j*nx+i;if(this.solid[k])continue;e+=v[k]*v[k];
      if(i+1<nx&&!this.solid[k+1])e+=c2*((h[k+1]-h[k])/this.dx)**2;
      if(j+1<n&&!this.solid[k+nx])e+=c2*((h[k+nx]-h[k])/this.dz)**2;
    }
    return .5*e*this.dx*this.dz;
  }
}
