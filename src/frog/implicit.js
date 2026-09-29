import * as THREE from 'three';
const clamp=THREE.MathUtils.clamp;

/** Smooth-union anatomical volumes, meshed as a single weighted skin surface. */
export function volume(center,radii,bone,rotation=new THREE.Quaternion(),region='body') {
  const m=new THREE.Matrix4().makeRotationFromQuaternion(rotation).transpose().elements;
  return {center:center.toArray(),r:radii.toArray(),m,bone,region,minR:Math.min(radii.x,radii.y,radii.z)};
}
function distance(shape,x,y,z){
  x-=shape.center[0];y-=shape.center[1];z-=shape.center[2];const m=shape.m;
  const a=(m[0]*x+m[4]*y+m[8]*z)/shape.r[0],b=(m[1]*x+m[5]*y+m[9]*z)/shape.r[1],c=(m[2]*x+m[6]*y+m[10]*z)/shape.r[2];
  return (Math.sqrt(a*a+b*b+c*c)-1)*shape.minR;
}
const smoothMin=(a,b,k)=>{const h=clamp(.5+.5*(b-a)/k,0,1);return b+(a-b)*h-k*h*(1-h);};

export function fieldValue(shapes,x,y,z){let d=100;for(const s of shapes)if(s.region!=='socket')d=smoothMin(d,distance(s,x,y,z),s.region==='body'?.29:.20);for(const s of shapes)if(s.region==='socket')d=-smoothMin(-d,distance(s,x,y,z),.035);return d;}

export function buildSkin(builder,shapes,paint,{step=.075}={}){
  const min=[-3.7,-.08,-3.9],max=[3.7,3.7,3.75];
  const nx=Math.ceil((max[0]-min[0])/step)+1,ny=Math.ceil((max[1]-min[1])/step)+1,nz=Math.ceil((max[2]-min[2])/step)+1;
  const sy=nx,sz=nx*ny,field=new Float32Array(nx*ny*nz);
  const evaluate=(x,y,z)=>fieldValue(shapes,x,y,z);
  for(let k=0;k<nz;k++)for(let j=0;j<ny;j++)for(let i=0;i<nx;i++)field[i+j*sy+k*sz]=evaluate(min[0]+i*step,min[1]+j*step,min[2]+k*step);
  const p=builder.part('Skin');p.n=[];const cache=new Map();
  const xyz=id=>{const k=Math.floor(id/sz),r=id-k*sz,j=Math.floor(r/sy),i=r-j*sy;return [min[0]+i*step,min[1]+j*step,min[2]+k*step];};
  const gradient=id=>{
    const k=Math.floor(id/sz),r=id-k*sz,j=Math.floor(r/sy),i=r-j*sy;
    return [field[id+(i<nx-1?1:0)]-field[id-(i>0?1:0)],field[id+(j<ny-1?sy:0)]-field[id-(j>0?sy:0)],field[id+(k<nz-1?sz:0)]-field[id-(k>0?sz:0)]];
  };
  function vertex(a,b){
    const lo=Math.min(a,b),hi=Math.max(a,b),key=lo*field.length+hi;
    if(cache.has(key))return cache.get(key);
    const t=field[a]/(field[a]-field[b]),aa=xyz(a),bb=xyz(b),point=aa.map((v,i)=>v+(bb[i]-v)*t);
    const ga=gradient(a),gb=gradient(b),normal=new THREE.Vector3(...ga.map((v,i)=>v+(gb[i]-v)*t)).normalize();
    const ds=shapes.map(s=>s.region==='socket'?1000:distance(s,...point)),dmin=Math.min(...ds),weights=new Map();let region='body',nearest=ds.indexOf(dmin);region=shapes[nearest].region;
    shapes.forEach((s,i)=>{const w=Math.exp(-(ds[i]-dmin)*17);weights.set(s.bone,(weights.get(s.bone)||0)+w);});
    const influence=[...weights].filter(x=>x[1]>1e-7).sort((a,b)=>b[1]-a[1]).slice(0,4),total=influence.reduce((n,x)=>n+x[1],0);
    const i=p.p.length/3;p.p.push(...point);p.n.push(normal.x,normal.y,normal.z);
    p.uv.push((point[0]+3.7)/7.4,(point[2]+3.9)/7.65);
    const color=paint(...point,region,normal);p.c.push(color.r,color.g,color.b);
    for(let j=0;j<4;j++){const e=influence[j];p.si.push(e?e[0]:0);p.sw.push(e?e[1]/total:0);}
    cache.set(key,i);return i;
  }
  function triangle(a,b,c){
    const ax=p.p[a*3],ay=p.p[a*3+1],az=p.p[a*3+2];
    const ux=p.p[b*3]-ax,uy=p.p[b*3+1]-ay,uz=p.p[b*3+2]-az;
    const vx=p.p[c*3]-ax,vy=p.p[c*3+1]-ay,vz=p.p[c*3+2]-az;
    const dot=(uy*vz-uz*vy)*p.n[a*3]+(uz*vx-ux*vz)*p.n[a*3+1]+(ux*vy-uy*vx)*p.n[a*3+2];
    if(dot>=0)p.idx.push(a,b,c);else p.idx.push(a,c,b);
  }
  const tetra=[[0,5,1,6],[0,1,2,6],[0,2,3,6],[0,3,7,6],[0,7,4,6],[0,4,5,6]];
  for(let k=0;k<nz-1;k++)for(let j=0;j<ny-1;j++)for(let i=0;i<nx-1;i++){
    const a=i+j*sy+k*sz,cube=[a,a+1,a+1+sy,a+sy,a+sz,a+sz+1,a+sz+1+sy,a+sz+sy];
    const signs=cube.map(id=>field[id]<0);if(signs.every(Boolean)||!signs.some(Boolean))continue;
    for(const tet of tetra){const inside=tet.filter(n=>signs[n]).map(n=>cube[n]),outside=tet.filter(n=>!signs[n]).map(n=>cube[n]);
      if(inside.length===1){triangle(...outside.map(b=>vertex(inside[0],b)));}
      else if(inside.length===3){triangle(...inside.map(b=>vertex(outside[0],b)));}
      else if(inside.length===2){const [a,b]=inside,[c,d]=outside;const ac=vertex(a,c),ad=vertex(a,d),bc=vertex(b,c),bd=vertex(b,d);triangle(ac,ad,bc);triangle(bc,ad,bd);}
    }
  }
  return {vertices:p.p.length/3,triangles:p.idx.length/3};
}
