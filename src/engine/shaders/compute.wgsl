fn rockStack(p:vec3f,h:f32,r:f32,index:i32,seed:f32) -> f32 {
  let bound=boxSdf(p-vec3f(0,h*.5,0),vec3f(r*2.2+3.,h*.65+3.,r*2.2+3.));
  if(bound>3.){return bound;}
  let phase=hash3(vec3i(index,i32(seed),11))*6.2831853;let progress=clamp(p.y/h,0.,1.);
  let px=p.x-progress*(hash3(vec3i(index,i32(seed),12))-.5)*4.6;
  let pz=p.z-progress*(hash3(vec3i(index,i32(seed),13))-.5)*4.6;
  let angle=phase*.55;let c=cos(angle);let sn=sin(angle);
  let rx=px*c-pz*sn;let rz=px*sn+pz*c;
  let coarse=noise(vec3f(px*.24+seed,p.y*.17,pz*.24));let kind=index%3;
  var wx=r;var wz=r;
  if(kind==0){
    let waist=(progress-.72)/.16;
    wx=r*(1.02-.34*smoothstep(.05,.4,progress)-.19*exp(-waist*waist));wz=wx*.83;
  }else if(kind==1){wx=r*.50;wz=r*1.38;}
  else{wx=r*select(select(1.,.78,progress<.67),1.1,progress<.32);wz=wx*.84;}
  let ledges=sin(p.y*.83+phase)*.24+coarse*.35;
  let qx=rx/max(1.1,wx+ledges);let qz=rz/max(1.1,wz+ledges);
  let polygon=max(abs(qx*.94+qz*.34),max(abs(-qx*.55+qz*.84),abs(qx*.28+qz*.96)));
  let joints=pow(abs(sin(rx*.93+rz*.62+phase+coarse*.6)),14.)*.40;
  let side=(polygon-1.)*min(wx,wz)+joints+coarse*.34;
  let top=p.y-(h-select(0.,2.4,kind==0))+coarse*.8+sin(rx*.5+phase)*.35;
  let v=vec2f(side,top);var d=length(max(v,vec2f(0)))+min(max(v.x,v.y),0.);
  if(kind==0){
    let cap=boxSdf(vec3f(rx-r*.16,p.y-(h-1.8)+rx*.12,rz+r*.09),vec3f(r*1.17,1.65,r*.88))-.85+coarse*.45;
    d=smin(d,cap,.85);
  }
  if(kind==1){
    let notch=ellipsoid(vec3f(rx+r*.4,p.y-h*.61,rz-r*.68),vec3f(r*.64,1.9,r*.72));
    d=max(d,-notch);
  }
  return d;
}
fn organicArch(p:vec3f,seed:f32) -> f32 {
  let warp=noise(p*vec3f(.053,.052,.053)+vec3f(seed,0,0));
  let wx=p.x+warp*1.15+sin(p.y*.14+seed*.01)*.42;
  let wy=p.y+noise(p*vec3f(.079,.04,.078)+vec3f(seed,0,0))*.72;
  let wz=p.z+noise(p*vec3f(.041,.07,.065)+vec3f(seed,0,0))*1.2+p.x*.054;
  let left=boxSdf(vec3f(wx+19.+(wy-14.)*.10,wy-13.,wz+1.),vec3f(8.3,14,5.5))-1.8;
  let right=boxSdf(vec3f(wx-20.5-(wy-13.)*.10,wy-11.5,wz-2.2),vec3f(5.8,11.5,4.7))-1.5;
  let roof=boxSdf(vec3f(wx-1.,wy-27.+wx*.075+noise(vec3f(wx*.18+seed,0,wz*.18))*.9,wz),vec3f(19,4.6,5.7))-1.35;
  var d=smin(smin(left,roof,3.1),right,2.1);
  let opening=smin(ellipsoid(vec3f(wx+2.5,wy-10.5,wz+1.),vec3f(14.7,13.7,17)),ellipsoid(vec3f(wx-6.,wy-8.,wz-2.),vec3f(11.6,11.7,17)),2.3);
  d=max(d,-opening);
  let window=ellipsoid(vec3f(wx+24.5,wy-18.,wz),vec3f(2.25,3.6,13));
  let chip=ellipsoid(vec3f(wx-8.,wy-34.,wz-4.),vec3f(3.8,2.8,7));
  d=max(d,-min(window,chip));
  let weather=noise(vec3f(wx*.23+seed,wy*.28,wz*.24));
  d+=weather*.58+pow(abs(sin(wx*.29+wz*.16+warp)),18.)*.30;
  let t1=rockStack(p-vec3f(-31,0,22),21.,5.5,2,seed);
  let t2=rockStack(p-vec3f(29,0,-22),16.5,4.8,1,seed);
  return smin(d,min(t1,t2),1.4);
}
fn spireField(p:vec3f,seed:f32) -> f32 {
  var rock=50.;
  for(var i=0;i<9;i++){
    let cx=f32(i%3-1)*24.+(hash3(vec3i(i,i32(seed),0))-.5)*10.;
    let cz=f32(i/3-1)*25.+(hash3(vec3i(i,i32(seed),1))-.5)*10.;
    let h=13.+hash3(vec3i(i,i32(seed),2))*18.;let r=4.+hash3(vec3i(i,i32(seed),3))*2.5;
    rock=smin(rock,rockStack(p-vec3f(cx,0,cz),h,r,i,seed),1.4);
  }
  return rock;
}

fn initialSdf(p:vec3f) -> f32 {
  let seed=f32(u32(u.flags.z)%8192u);
  let n=noise(p*vec3f(.065,.038,.065)+vec3f(seed,0,0));
  let fine=noise(p*vec3f(.26,.3,.26)+vec3f(seed,0,0));
  let bedding=sin(p.y*1.38+n*1.4)*.23+sin(p.y*3.6+n)*.075;
  let ground=p.y+.6-noise(vec3f(p.x*.1+seed,0,p.z*.1))*.23;
  var d=ground;
  if(u.geology.w<.5) {
    let curve=p.x+9.5*sin(p.z*.058)+3.3*sin(p.z*.117+seed*.01);
    let floorHeight=-.8+smoothstep(3.,17.,abs(curve))*4.4+noise(vec3f(p.x*.13+seed,0,p.z*.13))*.35;
    var rock=50.;
    for(var i=0;i<6;i++) {
      let cx=select(24.,-25.,i%2==0)+(hash3(vec3i(i,i32(seed),4))-.5)*5.;
      let cz=-28.+f32(i/2)*28.+(hash3(vec3i(i,i32(seed),5))-.5)*4.;
      let height=19.+hash3(vec3i(i,i32(seed),6))*14.;
      let rx=14.5+hash3(vec3i(i,i32(seed),7))*4.;let rz=17.5+hash3(vec3i(i,i32(seed),8))*3.;
      let ledge=sin(p.y*.67+n*1.1)*.72+sin(p.y*1.7)*.24;
      let taper=max(p.y-2.,0.)*.095;
      let dx=(p.x-cx+n*1.8)/(rx-taper+ledge);let dz=(p.z-cz+n)/(rz-taper+ledge);
      let poly=max(abs(dx*.94+dz*.34),max(abs(-dx*.55+dz*.84),abs(dx*.28+dz*.96)));
      let fractures=pow(abs(sin((p.x-cx)*.66+(p.z-cz)*.51+n*2.4)),16.)*.85;
      let footprint=(poly-1.)*min(rx,rz)+fractures;
      let crown=p.y-height+abs(noise(p*vec3f(.17,.018,.17)+vec3f(seed,0,0)))*.85-fine*.30;
      let v=vec2f(footprint,crown);
      let mesa=length(max(v,vec2f(0)))+min(max(v.x,v.y),0.);
      rock=smin(rock,mesa,2.2);
    }
    let uy=(p.y-6.)/2.8;let undercut=exp(-uy*uy)*1.7;
    let width=5.4+max(p.y,0.)*.24+sin(p.y*.66+n*.9)*.75+n*1.3+undercut;
    let flutes=pow(abs(sin(p.z*.41+noise(vec3f(p.x*.08+seed,2,p.z*.12))*2.)),10.)*1.8;
    let cut=(abs(curve)-width-flutes)*.78;
    rock=max(rock,-cut);
    d=smin(p.y-floorHeight,rock,2.5);
    let c1=ellipsoid(p-vec3f(-12,6,12),vec3f(8.2,4.6,8));
    let c2=ellipsoid(p-vec3f(12,7,-23),vec3f(7.7,4.1,6.4));
    d=max(d,-min(c1,c2));
    let gx=i32(floor(p.x/6.));let gz=i32(floor(p.z/6.));let scatter=hash3(vec3i(gx,i32(seed),gz));
    if(scatter>.53&&abs(curve)>8.) {
      let r=.6+hash3(vec3i(gx,i32(seed)+1,gz))*1.2;
      let bx=(f32(gx)+.25+hash3(vec3i(gx,i32(seed)+2,gz))*.5)*6.;
      let bz=(f32(gz)+.25+hash3(vec3i(gx,i32(seed)+3,gz))*.5)*6.;
      d=min(d,ellipsoid(p-vec3f(bx,floorHeight+r*.12,bz),vec3f(r,r*.68,r*.8)));
    }
  }else if(u.geology.w<1.5){
    d=smin(ground,organicArch(p,seed),2.2);
  }else{
    d=smin(ground,spireField(p,seed),2.1);
  }
  d+=bedding*.58+fine*.52+n*.26;
  let bound=boxSdf(p-vec3f(0,16.7,0),vec3f(44.6,18.1,44.6))-1.1;
  return clamp(max(d,bound),-24.,32.);
}
@compute @workgroup_size(4,4,4)
fn initialize(@builtin(global_invocation_id) id:vec3u) {
  if(any(id>=vec3u(u.dims.xyz))){return;}
  let p=WORLD_MIN+(vec3f(id)+.5)/u.dims.xyz*WORLD_SIZE;
  textureStore(outputField,vec3i(id),vec4f(initialSdf(p),0,0,0));
}
fn permeability(d:f32) -> f32 {return smoothstep(-.45*u.dims.w,.65*u.dims.w,d);}
fn runoffPotential(p:vec3f) -> f32 {
  return noise(p*vec3f(.46,.035,.46)+vec3f(u.flags.z*.013,0,0))*.72+noise(p*vec3f(.18,.06,.18)+vec3f(0,11.2,u.flags.z*.007))*.28;
}
fn runoffGeometry(p:vec3i) -> vec4f {
  let a=loadAt(p);let h=u.dims.w;
  if(abs(a.x)>h*2.2){return vec4f(0,-1,0,0);}
  let g=vec3f(loadAt(p+vec3i(1,0,0)).x-loadAt(p-vec3i(1,0,0)).x,loadAt(p+vec3i(0,1,0)).x-loadAt(p-vec3i(0,1,0)).x,loadAt(p+vec3i(0,0,1)).x-loadAt(p-vec3i(0,0,1)).x);
  let n=g/max(length(g),.000001);
  let surface=(1.-smoothstep(h*.5,h*2.2,abs(a.x)))*smoothstep(-.15,.25,n.y);
  let potential=runoffPotential(WORLD_MIN+(vec3f(p)+.5)*h)*u.processes.x*.07*surface;
  return vec4f(n.x*n.y*surface,-1.+n.y*n.y*surface,n.z*n.y*surface,potential);
}
fn faceFlux(p:vec3i,a:vec4f,geometry:vec4f,offset:vec3i,axis:i32) -> f32 {
  let b=loadAt(p+offset);
  if(a.y==0.&&b.y==0.){return 0.;}
  let gb=runoffGeometry(p+offset);
  let direction=(geometry[axis]+gb[axis])*.5;
  let donor=select(b.y,a.y,direction>=0.);
  let raw=(a.y+geometry.w-b.y-gb.w)*.16+direction*donor*.72;
  return clamp(raw,-b.y/6.,a.y/6.)*min(permeability(a.x),permeability(b.x));
}
@compute @workgroup_size(4,4,4)
fn flux(@builtin(global_invocation_id) id:vec3u) {
  if(any(id>=vec3u(u.dims.xyz))){return;}
  let p=vec3i(id);let a=loadAt(p);let g=runoffGeometry(p);
  var f=vec3f(faceFlux(p,a,g,vec3i(1,0,0),0),faceFlux(p,a,g,vec3i(0,1,0),1),faceFlux(p,a,g,vec3i(0,0,1),2));
  if(id.x==u32(u.dims.x)-1u){f.x=0.;}if(id.y==u32(u.dims.y)-1u){f.y=0.;}if(id.z==u32(u.dims.z)-1u){f.z=0.;}
  textureStore(outputFlux,p,vec4f(f,0));
}
fn fluxAt(p:vec3i) -> vec3f {if(any(p<vec3i(0))||any(p>=vec3i(u.dims.xyz))){return vec3f(0);}return textureLoad(fluxField,p,0).xyz;}
fn concentration(v:vec4f) -> f32 {return min(v.z/max(v.y,.00001),3.);}
fn sedimentFlux(f:f32,a:vec4f,b:vec4f,axis:i32) -> f32 {
  let settling=u.brushParams.w*.06;let dustBudget=u.processes.y*.18;
  let advected=(1.-settling-dustBudget)*f*select(concentration(b),concentration(a),f>=0.);
  let wind=vec3f(u.processes.z,0,u.processes.w);let windNorm=max(.001,abs(wind.x)+abs(wind.z));
  let donor=select(b,a,wind[axis]>=0.);
  let dust=dustBudget*wind[axis]/windNorm*donor.z*(1.-clamp(donor.y*3.,0.,1.))*min(permeability(a.x),permeability(b.x));
  return advected+dust-select(0.,settling*b.z*min(permeability(a.x),permeability(b.x)),axis==1);
}
fn abrasiveBand(p:vec3f) -> f32 {
  let along=p.x*u.processes.z+p.z*u.processes.w;let across=-p.x*u.processes.w+p.z*u.processes.z;
  let phase=p.y*1.55+noise(vec3f(across*.14+u.flags.z*.003,p.y*.09,along*.025))*1.65;
  return smoothstep(.25,.85,.5+.5*sin(phase));
}
fn windExposure(p:vec3i,n:vec3f) -> f32 {
  var visibility=1.;let direction=vec3f(u.processes.z,0,u.processes.w);
  for(var step=0;step<288;step++){
    let q=vec3i(floor(vec3f(p)+n*.8-direction*(1.+f32(step)*.75)+.5));
    if(any(q<vec3i(0))||any(q>=vec3i(u.dims.xyz))){break;}
    visibility*=smoothstep(-u.dims.w*.25,u.dims.w*.25,loadAt(q).x);
    if(visibility<.001){break;}
  }
  return visibility;
}
@compute @workgroup_size(4,4,4)
fn evolve(@builtin(global_invocation_id) id:vec3u) {
  if(any(id>=vec3u(u.dims.xyz))){return;}
  let p=vec3i(id);let a=loadAt(p);
  if(any(id<vec3u(2))||any(id>=vec3u(u.dims.xyz)-2u)){textureStore(outputField,p,vec4f(a.x,0,0,a.w));return;}
  let xp=loadAt(p+vec3i(1,0,0));let xm=loadAt(p-vec3i(1,0,0));
  let yp=loadAt(p+vec3i(0,1,0));let ym=loadAt(p-vec3i(0,1,0));
  let zp=loadAt(p+vec3i(0,0,1));let zm=loadAt(p-vec3i(0,0,1));
  let f=fluxAt(p);let fm=vec3f(fluxAt(p-vec3i(1,0,0)).x,fluxAt(p-vec3i(0,1,0)).y,fluxAt(p-vec3i(0,0,1)).z);
  var water=a.y+dot(fm-f,vec3f(1));
  var sediment=max(0.,a.z+sedimentFlux(fm.x,xm,a,0)+sedimentFlux(fm.y,ym,a,1)+sedimentFlux(fm.z,zm,a,2)-sedimentFlux(f.x,a,xp,0)-sedimentFlux(f.y,a,yp,1)-sedimentFlux(f.z,a,zp,2));
  let cell=u.dims.w;
  let grad=vec3f(xp.x-xm.x,yp.x-ym.x,zp.x-zm.x);
  let normal=grad/max(length(grad),.000001);
  var rain=0.;
  if(a.x>0.&&a.x<cell*1.7&&normal.y>.08&&u.erosion.x>0.) {
    var exposed=1.;
    // Test every voxel above the surface: thin cave roofs shelter the floor.
    for(var y=p.y+1;y<i32(u.dims.y);y++) {
      exposed*=smoothstep(-cell*.2,cell*.3,loadAt(vec3i(p.x,y,p.z)).x);
      if(exposed<.001){break;}
    }
    rain=u.erosion.x*.04*normal.y*exposed;
  }
  water=max(0.,water+rain)*(1.-u.erosion.w*.035);
  let narrow=1.-smoothstep(cell*.75,cell,abs(a.x));
  var delta=0.;
  if(narrow>0.) {
    var front=vec3i(0,0,select(-1,1,normal.z>=0.));
    if(abs(normal.x)>abs(normal.y)&&abs(normal.x)>abs(normal.z)){front=vec3i(select(-1,1,normal.x>=0.),0,0);}
    else if(abs(normal.y)>abs(normal.z)){front=vec3i(0,select(-1,1,normal.y>=0.),0);}
    let wet=max(water,loadAt(p+front).y*.85);
    let flowCell=select(p+front,p,a.y>=loadAt(p+front).y);
    let lateral=select(vec3i(1,0,0),vec3i(0,0,1),abs(normal.x)>abs(normal.z));
    let streamWet=loadAt(flowCell).y;
    let flankWet=(loadAt(flowCell-lateral*2).y+loadAt(flowCell-lateral).y+streamWet+loadAt(flowCell+lateral).y+loadAt(flowCell+lateral*2).y)/5.;
    let concentration=smoothstep(1.01,1.3,streamWet/max(flankWet,.002));
    let focus=1.+u.processes.x*2.*concentration;
    let channelMask=1.-u.processes.x+u.processes.x*(.04+.96*concentration);
    let q=((f+fm)*.5+fluxAt(p+front))*.5;
    let tangential=length(q-normal*dot(q,normal));
    let slope=min(2.5,sqrt(max(0.,1.-normal.y*normal.y))/max(abs(normal.y),.25));
    let velocity=min(3.,tangential/max(wet,.015));
    let wp=WORLD_MIN+(vec3f(id)+.5)*cell;
    let weak=erodibility(wp);
    let shear=wet*velocity*(1.+slope*2.4)*focus;
    let threshold=(.001+u.geology.z*.025)*(1.1-weak*.7);
    let capacity=u.erosion.z*wet*velocity*(1.+slope*.8)*6.*focus;
    let solid=solidFraction(a.x);
    let detach=min(min(solid,max(0.,capacity-sediment)),min(.045,u.erosion.y*weak*max(0.,shear-threshold)*.95*narrow*channelMask));
    let deposit=min(min(1.-solid,sediment),min(.02,max(0.,sediment-capacity)*(.08+u.brushParams.w*.35)*smoothstep(-.15,.75,normal.y)*narrow));
    var windDetach=0.;let facing=max(0.,-normal.x*u.processes.z-normal.z*u.processes.w);
    if(u.processes.y>0.&&facing>.08&&wet<.34){
      let exposure=windExposure(p,normal);let dry=1.-clamp(wet*3.,0.,1.);
      let aboveWater=select(0.,1.,u.water.z<.5||wp.y>u.water.x+.25);
      let rate=u.processes.y*u.processes.y*facing*exposure*dry*aboveWater*abrasiveBand(wp)*exp(-max(wp.y+2.,0.)/22.);
      windDetach=min(max(0.,solid-detach),min(.04,rate*weak*.08*narrow));
    }
    let exchange=detach+windDetach-deposit;
    delta=exchange*2.*cell;
    sediment+=exchange;
  }
  textureStore(outputField,p,vec4f(clamp(a.x+delta,-24.,32.),min(water,2.),max(0.,sediment),clamp(a.w+delta,-6.,6.)));
}
// Four downward diagonal grains transfers. Source and destination each grant a
// quarter of their material/void budget to a shared link. No global blur term.
fn talusLink(p:vec3i,offset:vec3i,solid:f32,rate:f32) -> f32 {
  let dest=p+offset;
  if(any(dest<vec3i(2))||any(dest>=vec3i(u.dims.xyz)-2)){return 0.;}
  return min(solid,1.-solidFraction(loadAt(dest).x))*.25*rate;
}
@compute @workgroup_size(4,4,4)
fn talusFlux(@builtin(global_invocation_id) id:vec3u) {
  if(any(id>=vec3u(u.dims.xyz))){return;}
  let p=vec3i(id);let a=loadAt(p);var f=vec4f(0);
  if(all(id>=vec3u(2))&&all(id<vec3u(u.dims.xyz)-2u)&&id.y>2u&&abs(a.x)<u.dims.w&&u.geology.x>0.) {
    let g=vec3f(loadAt(p+vec3i(1,0,0)).x-loadAt(p-vec3i(1,0,0)).x,loadAt(p+vec3i(0,1,0)).x-loadAt(p-vec3i(0,1,0)).x,loadAt(p+vec3i(0,0,1)).x-loadAt(p-vec3i(0,0,1)).x);
    let ny=g.y/max(length(g),.000001);let repose=cos(u.pick.z);
    let unstable=clamp((repose-ny)/repose,0.,1.);
    let loose=clamp(-a.w/u.dims.w,0.,1.);
    let weak=erodibility(WORLD_MIN+(vec3f(id)+.5)*u.dims.w);
    let mobility=mix((.7-u.geology.z*.35)*weak,1.,loose);
    let rate=min(.4,u.geology.x*.45*unstable*mobility);let solid=solidFraction(a.x);
    f=vec4f(talusLink(p,vec3i(1,-1,0),solid,rate),talusLink(p,vec3i(-1,-1,0),solid,rate),talusLink(p,vec3i(0,-1,1),solid,rate),talusLink(p,vec3i(0,-1,-1),solid,rate));
  }
  textureStore(outputFlux,p,f);
}
@compute @workgroup_size(4,4,4)
fn talusSettle(@builtin(global_invocation_id) id:vec3u) {
  if(any(id>=vec3u(u.dims.xyz))){return;}
  let p=vec3i(id);var a=loadAt(p);
  if(all(id>=vec3u(2))&&all(id<vec3u(u.dims.xyz)-2u)) {
    let outflow=dot(textureLoad(fluxField,p,0),vec4f(1));
    let incoming=textureLoad(fluxField,p+vec3i(-1,1,0),0).x+textureLoad(fluxField,p+vec3i(1,1,0),0).y+textureLoad(fluxField,p+vec3i(0,1,-1),0).z+textureLoad(fluxField,p+vec3i(0,1,1),0).w;
    let change=incoming-outflow;
    if(abs(change)>.000000000001){a.x=(.5-clamp(solidFraction(a.x)+change,0.,1.))*2.*u.dims.w;a.w=clamp(a.w-change*2.*u.dims.w,-6.,6.);}
  }
  textureStore(outputField,p,a);
}
@compute @workgroup_size(4,4,4)
fn sculpt(@builtin(global_invocation_id) id:vec3u) {
  if(any(id>=vec3u(u.dims.xyz))){return;}
  let p=vec3i(id);var a=loadAt(p);
  if(any(id<vec3u(2))||any(id>=vec3u(u.dims.xyz)-2u)){textureStore(outputField,p,a);return;}
  let wp=WORLD_MIN+(vec3f(id)+.5)*u.dims.w;
  let r=u.brushParams.x;let v=wp-u.sculpt.xyz;let distance=length(v);
  let n=u.planeNormal.xyz;let tangent=u.strokeTangent.xyz;let bitangent=cross(n,tangent);
  let tool=i32(u.brushParams.z);var value=a.x;
  let extent=select(r,r*1.65,tool>=5);
  if(any(abs(v)>vec3f(extent))){textureStore(outputField,p,a);return;}
  if(tool==5){
    let plane=dot(wp-u.planeOrigin.xyz,n);let radial=length(v-n*dot(v,n));
    let edge=1.-smoothstep(r*(1.-u.brushParams.y*.55),r,radial);
    let depth=1.-smoothstep(r*.95,r*1.6,abs(plane));
    value=mix(a.x,plane,edge*depth*u.sculpt.w*.62);
  }else if(tool==6||tool==7){
    let along=dot(v,tangent);let side=dot(v,bitangent);let height=dot(v,n);let phase=u.strokeTangent.w*.017;
    let width=max(u.dims.w*.75,r*u.planeNormal.w)*select(1.,2.2,tool==7);
    let depth=r*u.planeOrigin.w*select(1.15,1.9,tool==7);
    let curve=sin(along*.9+phase)*r*.10+sin(along*2.3+phase)*r*.04;
    let opening=width*clamp(1.+height/depth,.12,1.);
    var cut=max(max(abs(side-curve)-opening,abs(along)-r),max(-height-depth,height-u.dims.w*.35));
    if(tool==6){
      let ax=(along-r*.12)*.65+side*.76;let az=-(along-r*.12)*.76+side*.65;
      let fork=max(max(abs(az)-opening*.55,abs(ax)-r*.65),max(-height-depth*.68,height-u.dims.w*.3));
      cut=min(cut,fork);
    }
    value=mix(a.x,max(a.x,-cut),u.sculpt.w*.72);
  }else if(tool==8){
    let local=vec3f(dot(v,tangent),dot(v,n)-r*.22,dot(v,bitangent));
    let phase=u.strokeTangent.w*.013;
    let stone=ellipsoid(local,vec3f(r*.91,r*clamp(u.planeOrigin.w,.3,1.2),r*.78))+noise(local/r*2.3+vec3f(phase,0,0))*r*.11;
    value=mix(a.x,min(a.x,stone),min(1.,u.sculpt.w*1.7));
  }else if(distance<r){
    let w=(1.-smoothstep(r*(1.-u.brushParams.y),r,distance))*u.sculpt.w*.38;
    if(tool==1){value-=w*u.dims.w*1.5;}
    else if(tool==2){value+=w*u.dims.w*1.5;}
    else if(tool==3){value=mix(a.x,(loadAt(p-vec3i(1,0,0)).x+loadAt(p+vec3i(1,0,0)).x+loadAt(p-vec3i(0,1,0)).x+loadAt(p+vec3i(0,1,0)).x+loadAt(p-vec3i(0,0,1)).x+loadAt(p+vec3i(0,0,1)).x)/6.,w);}
    else if(tool==4){value=mix(a.x,wp.y-u.sculpt.y,w);}
  }
  a.x=clamp(value,-24.,32.);textureStore(outputField,p,a);
}
@compute @workgroup_size(1)
fn pickSurface() {
  let rd=normalize(u.forward.xyz+u.right.xyz*u.pick.x*u.forward.w*u.right.w+u.up.xyz*u.pick.y*u.right.w);
  let t=trace(u.eye.xyz,rd,280);
  let hit=u.eye.xyz+rd*t;
  pickResult[0]=vec4f(hit,select(0.,1.,t<9999.));
  let e=max(u.dims.w*.8,u.brushParams.x*.22);
  let g=vec3f(map(hit+vec3f(e,0,0))-map(hit-vec3f(e,0,0)),map(hit+vec3f(0,e,0))-map(hit-vec3f(0,e,0)),map(hit+vec3f(0,0,e))-map(hit-vec3f(0,0,e)));
  pickResult[1]=vec4f(safeNormalize(g),0.);
}
// Eikonal relaxation extends surface displacement into neighboring voxels.
// Without redistancing, a narrow-band level set eventually runs out of valid
// distances and sphere tracing becomes unsafe. Godunov upwinding and a 0.2 CFL
// step keep the update stable; the limiter prevents changing any voxel's sign.
@compute @workgroup_size(4,4,4)
fn redistance(@builtin(global_invocation_id) id:vec3u) {
  if(any(id>=vec3u(u.dims.xyz))){return;}
  let p=vec3i(id);var a=loadAt(p);let h=u.dims.w;
  if(abs(a.x)<h*8.&&all(id>vec3u(1))&&all(id<vec3u(u.dims.xyz)-2u)) {
    let backward=(vec3f(a.x)-vec3f(loadAt(p-vec3i(1,0,0)).x,loadAt(p-vec3i(0,1,0)).x,loadAt(p-vec3i(0,0,1)).x))/h;
    let ahead=(vec3f(loadAt(p+vec3i(1,0,0)).x,loadAt(p+vec3i(0,1,0)).x,loadAt(p+vec3i(0,0,1)).x)-vec3f(a.x))/h;
    let b=select(min(backward,vec3f(0)),max(backward,vec3f(0)),a.x>=0.);
    let f=select(max(ahead,vec3f(0)),min(ahead,vec3f(0)),a.x>=0.);
    let gradient=sqrt(dot(max(b*b,f*f),vec3f(1)));
    let sign=a.x/sqrt(a.x*a.x+h*h);
    let correction=clamp(.2*h*sign*(gradient-1.),-.45*abs(a.x),.45*abs(a.x));
    a.x-=correction;
  }
  textureStore(outputField,p,a);
}
