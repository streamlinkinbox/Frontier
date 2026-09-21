import * as THREE from 'three';
// Bake a periodic microstructure once. No per-fragment Voronoi loop or bubble sprites.
function makeDetail(){
  const n=256,bytes=new Uint8Array(n*n*4);
  const hash=(x,y)=>{const v=Math.sin(x*127.1+y*311.7)*43758.5453;return v-Math.floor(v);};
  const noise=(x,y,period)=>{let i=Math.floor(x),j=Math.floor(y),u=x-i,v=y-j;u=u*u*(3-2*u);v=v*v*(3-2*v);const h=(a,b)=>hash((a+period)%period,(b+period)%period);return (h(i,j)*(1-u)+h(i+1,j)*u)*(1-v)+(h(i,j+1)*(1-u)+h(i+1,j+1)*u)*v;};
  for(let j=0;j<n;j++)for(let i=0;i<n;i++){
    const x=i/n,y=j/n,px=x*40,py=y*40,cx=Math.floor(px),cy=Math.floor(py);let d1=20,d2=20;
    for(let a=-1;a<=1;a++)for(let b=-1;b<=1;b++){
      const ix=(cx+a+40)%40,iy=(cy+b+40)%40;
      const dx=cx+a+.15+.7*hash(ix,iy)-px,dy=cy+b+.15+.7*hash(ix+43,iy+71)-py,d=Math.hypot(dx,dy);
      if(d<d1){d2=d1;d1=d;}else if(d<d2)d2=d;
    }
    const wall=Math.exp(-Math.pow((d2-d1)*10,2));
    const macro=.58*noise(x*4,y*4,4)+.28*noise(x*8,y*8,8)+.14*noise(x*16,y*16,16);
    const meso=.58*noise(x*12,y*12,12)+.28*noise(x*24,y*24,24)+.14*noise(x*48,y*48,48);
    const k=(j*n+i)*4;bytes[k]=Math.round((.57+.36*wall)*255);bytes[k+1]=Math.round(macro*255);bytes[k+2]=Math.round(meso*255);bytes[k+3]=255;
  }
  const tex=new THREE.DataTexture(bytes,n,n,THREE.RGBAFormat);tex.wrapS=tex.wrapT=THREE.RepeatWrapping;tex.minFilter=THREE.LinearMipmapLinearFilter;tex.magFilter=THREE.LinearFilter;tex.generateMipmaps=true;tex.needsUpdate=true;return tex;
}
export function createFoamSurface(field){
  let texture,version=-1;
  const uniforms={uFoamMap:{value:null},uFoamDetail:{value:makeDetail()},uFoamDomain:{value:new THREE.Vector2(field.width,field.depth)},uFoamEnabled:{value:1},uFoamTint:{value:new THREE.Color('#e2ece6')},uUsePlanar:{value:1},uUseRefraction:{value:1}};
  return {uniforms,update(field,enabled,style){
    if(version!==field.version){
      texture?.dispose();texture=new THREE.DataTexture(field.pixels,field.nx,field.nz,THREE.RedFormat,THREE.UnsignedByteType);texture.minFilter=texture.magFilter=THREE.LinearFilter;texture.unpackAlignment=1;texture.needsUpdate=true;
      uniforms.uFoamMap.value=texture;uniforms.uFoamDomain.value.set(field.width,field.depth);version=field.version;
    }
    if(field.dirty){field.uploadPending();texture.needsUpdate=true;field.dirty=false;}
    uniforms.uFoamEnabled.value=enabled&&(field.activeCells>0||field.pending)?1:0;
    uniforms.uFoamTint.value.set(style==='dirty'||style==='swamp'?'#dfdccb':'#e2ece6');
  }};
}
export function withSurfaceFoam(fragment){
  return `uniform sampler2D uFoamMap,uFoamDetail;uniform vec2 uFoamDomain;uniform float uFoamEnabled,uUsePlanar,uUseRefraction;uniform vec3 uFoamTint;\n`+fragment
    .replace('if(all(greaterThan(refrUV', 'if(uUseRefraction>.5&&all(greaterThan(refrUV')
    .replace('reflected=mix(reflected,texture2D(uReflection', 'if(uUsePlanar>.5)reflected=mix(reflected,texture2D(uReflection')
    .replace('gl_FragColor=vec4(color,1.);', `
      if(uFoamEnabled>.5){
        float density=texture2D(uFoamMap,p/uFoamDomain+.5).r;
        if(density>.012){
          vec3 breakup=texture2D(uFoamDetail,p*.64).rgb;
          vec3 cells=texture2D(uFoamDetail,p*1.7).rgb;
          float pores=cells.r;
          // Density erosion exposes connected lacy edges and open water pockets.
          // At high density the cells merge into a matte, light-scattering raft.
          float ragged=density-(.10+breakup.g*.43+breakup.b*.23);
          float aa=max(.035,fwidth(ragged)*1.2);
          float coverage=smoothstep(-aa,aa,ragged);
          float openCells=smoothstep(.30,.57,cells.b);
          coverage*=mix(openCells,1.,smoothstep(.48,.96,density)*.72);
          float thick=smoothstep(.28,.8,density);
          float relief=mix(pores,.88,thick*.35);
          float light=.50+.40*max(dot(normalize(vNormal),normalize(vec3(-.5,1.,.35))),0.);
          vec3 foam=uFoamTint*light*(.59+.41*relief);
          color=mix(color,foam,coverage*.97);
        }
      }
      gl_FragColor=vec4(color,1.);`);
}
