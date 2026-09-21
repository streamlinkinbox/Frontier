// Lightweight kinematic/buoyant toy coupling, not a rigid-body pressure solve.
export class FloatingBody {
  constructor(){this.reset();}
  reset(level=.65){Object.assign(this,{x:.8,z:.3,y:level,vx:0,vz:0,vy:0,angle:.6,radius:.48,target:null,travel:0,wakes:0,coastWakeTime:0});}
  grab(x,z){this.target={x,z};}
  release(){this.target=null;}
  update(solver,dt,level){
    if(dt<=0)return;
    // Use bounded substeps for consistent buoyancy and obstacle containment.
    let remain=dt;
    while(remain>1e-7){const d=Math.min(remain,1/120);this.step(solver,d,level);remain-=d;}
  }
  step(solver,dt,level){
    this.coastWakeTime=this.target?2:Math.max(0,this.coastWakeTime-dt);
    const g=solver.gradient(this.x,this.z);
    if(this.target){
      const ax=(this.target.x-this.x)*10,az=(this.target.z-this.z)*10;
      const length=Math.hypot(ax,az),cap=length>3?3/length:1;
      const response=1-Math.exp(-12*dt);
      this.vx+=(ax*cap-this.vx)*response;this.vz+=(az*cap-this.vz)*response;
    }else{
      this.vx=(this.vx-g.x*2*dt)*Math.exp(-1.35*dt);
      this.vz=(this.vz-g.z*2*dt)*Math.exp(-1.35*dt);
    }
    const nx=this.x+this.vx*dt,nz=this.z+this.vz*dt;
    if(solver.isWet(nx,nz,this.radius)){
      this.travel+=Math.hypot(nx-this.x,nz-this.z);this.x=nx;this.z=nz;
    }else{this.vx*=-.2;this.vz*=-.2;}
    const speed=Math.hypot(this.vx,this.vz);
    if(speed>.06){
      const a=Math.atan2(this.vx,this.vz),difference=Math.atan2(Math.sin(a-this.angle),Math.cos(a-this.angle));
      this.angle+=difference*(1-Math.exp(-6*dt));
    }
    if(this.travel>.10&&speed>.08&&(this.target||this.coastWakeTime>0)){
      const ux=this.vx/speed,uz=this.vz/speed,s=Math.min(.7,speed*.23);
      // Bow mound + stern depression launch physical solver waves, not painted trails.
      solver.disturb(this.x+ux*.40,this.z+uz*.40,s,.23);
      solver.disturb(this.x-ux*.35,this.z-uz*.35,-s*.75,.28);
      this.travel=0;this.wakes++;
    }
    const targetY=level+solver.sample(this.x,this.z);
    this.vy+=((targetY-this.y)*48-this.vy*10)*dt;this.y+=this.vy*dt;
  }
}
