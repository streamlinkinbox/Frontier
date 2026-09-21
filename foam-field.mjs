// Eulerian coverage field. Fixed 20 Hz updates; no foam particles, no water feedback.
export class FoamField {
  constructor(resolution=128){this.resolution=resolution;this.halfLife=5;this.configureSize(12,8);}
  configureSize(width,depth){
    this.width=width;this.depth=depth;this.nx=this.resolution;this.nz=Math.max(32,Math.round(this.resolution*depth/width));
    const count=this.nx*this.nz;this.a=new Float32Array(count);this.b=new Float32Array(count);this.pixels=new Uint8Array(count);
    this.wet=new Uint8Array(count);this.indices=new Int32Array(count);this.sourceWet=new Uint8Array(count);this.version=(this.version||0)+1;
    this.solverKey='';this.clear();
  }
  configure(solver){
    if(this.width!==solver.width||this.depth!==solver.size)this.configureSize(solver.width,solver.size);
    const key=solver.nx+':'+solver.n;
    if(this.solverKey===key)return;
    this.solverKey=key;
    for(let j=0;j<this.nz;j++)for(let i=0;i<this.nx;i++){
      const k=j*this.nx+i,x=(i+.5)/this.nx*this.width-this.width/2,z=(j+.5)/this.nz*this.depth-this.depth/2;
      this.wet[k]=solver.isWet(x,z,.025)?1:0;
      const ix=Math.max(1,Math.min(solver.nx-2,Math.round((x+this.width/2)/solver.dx)));
      const iz=Math.max(1,Math.min(solver.n-2,Math.round((z+this.depth/2)/solver.dz))),s=iz*solver.nx+ix;
      this.indices[k]=s;this.sourceWet[k]=!solver.solid[s]&&!solver.solid[s-1]&&!solver.solid[s+1]&&!solver.solid[s-solver.nx]&&!solver.solid[s+solver.nx]?1:0;
    }
  }
  clear(){this.a.fill(0);this.b.fill(0);this.pixels.fill(0);this.accumulator=0;this.activeCells=0;this.area=0;this.updates=0;this.dirty=true;this.pending=false;}
  splat(x,z,rx,rz,angle,amount){
    const c=Math.cos(angle),s=Math.sin(angle),r=Math.max(rx,rz)*2;
    const lx=Math.max(0,Math.floor((x-r+this.width/2)/this.width*this.nx)),hx=Math.min(this.nx-1,Math.ceil((x+r+this.width/2)/this.width*this.nx));
    const lz=Math.max(0,Math.floor((z-r+this.depth/2)/this.depth*this.nz)),hz=Math.min(this.nz-1,Math.ceil((z+r+this.depth/2)/this.depth*this.nz));
    for(let j=lz;j<=hz;j++)for(let i=lx;i<=hx;i++){
      const k=j*this.nx+i;if(!this.wet[k])continue;
      const dx=(i+.5)/this.nx*this.width-this.width/2-x,dz=(j+.5)/this.nz*this.depth-this.depth/2-z;
      const u=(c*dx+s*dz)/rx,v=(-s*dx+c*dz)/rz,q=u*u+v*v;
      if(q>4)continue;
      this.a[k]=Math.min(1,this.a[k]+amount*Math.exp(-q*1.8));
    }
    this.pending=true;this.dirty=true;
  }
  sample(x,z){return this.sampleGrid((x/this.width+.5)*this.nx-.5,(z/this.depth+.5)*this.nz-.5);}
  sampleGrid(x,z){
    x=Math.max(0,Math.min(this.nx-1,x));z=Math.max(0,Math.min(this.nz-1,z));
    const i=Math.min(this.nx-2,Math.floor(x)),j=Math.min(this.nz-2,Math.floor(z)),u=x-i,v=z-j,k=j*this.nx+i,a=this.a;
    return (a[k]*(1-u)+a[k+1]*u)*(1-v)+(a[k+this.nx]*(1-u)+a[k+this.nx+1]*u)*v;
  }
  update(solver,dt,emit=true,amount=1){
    this.configure(solver);this.accumulator+=dt;
    // Inputs are on the physics clock; advection/decay run on their own fixed clock.
    while(this.accumulator+1e-12>=.05){this.accumulator-=.05;this.step(solver,.05,emit,amount);}
  }
  step(solver,dt,emit,amount){
    if(!this.pending&&this.activeCells===0&&!emit)return;
    const h=solver.height,v=solver.velocity,n=solver.nx,decay=Math.pow(.5,dt/this.halfLife);
    const sx=this.nx/this.width,sz=this.nz/this.depth;
    let active=0,sum=0,changed=false;const hasCoverage=this.activeCells>0||this.pending;
    for(let j=0;j<this.nz;j++)for(let i=0;i<this.nx;i++){
      const k=j*this.nx+i;
      if(!this.wet[k]){this.b[k]=0;if(this.pixels[k])changed=true;this.pixels[k]=0;continue;}
      const index=this.indices[k],speed=v[index];let gx=0,gz=0,fx=0,fz=0;
      if(Math.abs(speed)>.001){
        gx=(h[index+1]-h[index-1])/(2*solver.dx);gz=(h[index+n]-h[index-n])/(2*solver.dz);
        const d=gx*gx+gz*gz+.16;fx=Math.max(-.45,Math.min(.45,-speed*gx/d))*.45;fz=Math.max(-.45,Math.min(.45,-speed*gz/d))*.45;
      }
      let value=hasCoverage?this.sampleGrid(i-fx*dt*sx,j-fz*dt*sz)*decay:0;
      if(emit&&this.sourceWet[k]&&Math.abs(speed)>.12){
        const slope=Math.hypot(gx,gz),curvature=Math.abs((h[index+1]+h[index-1]-2*h[index])/(solver.dx*solver.dx)+(h[index+n]+h[index-n]-2*h[index])/(solver.dz*solver.dz));
        if(slope>.18&&curvature>.8)value+=dt*amount*Math.min(3,(slope-.18)*Math.abs(speed)*8)*(1-value);
      }
      value=Math.max(0,Math.min(1,value));if(value<.025)value=0;
      this.b[k]=value;const byte=Math.round(value*255);if(byte!==this.pixels[k])changed=true;this.pixels[k]=byte;if(value>0){active++;sum+=value;}
    }
    const temp=this.a;this.a=this.b;this.b=temp;this.activeCells=active;this.area=sum/(sx*sz);this.updates++;this.pending=false;this.dirty=this.dirty||changed;
  }
  uploadPending(){
    if(!this.pending)return;
    let sum=0,active=0;for(let i=0;i<this.a.length;i++){const f=this.a[i];this.pixels[i]=Math.round(f*255);if(f>.025){active++;sum+=f;}}
    this.activeCells=active;this.area=sum*this.width*this.depth/this.a.length;
  }
}
