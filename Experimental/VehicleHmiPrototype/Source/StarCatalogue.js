// Native SLAT v1 catalogue, octahedral direction bins, boundary duplication.
export function StarCell(D){const L=Math.abs(D[0])+Math.abs(D[1])+Math.abs(D[2]);let X=D[0]/L,Y=D[1]/L;if(D[2]<0){const U=(1-Math.abs(Y))*(X>=0?1:-1);Y=(1-Math.abs(X))*(Y>=0?1:-1);X=U;}return Math.min(31,Math.max(0,Math.floor((Y*.5+.5)*32)))*32+Math.min(31,Math.max(0,Math.floor((X*.5+.5)*32)));}
export function DecodeStarCatalogue(Buffer){const H=new DataView(Buffer);if(Buffer.byteLength<16||H.getUint32(0,true)!==0x54414c53||H.getUint32(4,true)!==1)throw new Error('Invalid native SLAT star catalogue');const Count=H.getUint32(8,true);if(!Count||Count>200000||Buffer.byteLength!==16+Count*28)throw new Error('Invalid star catalogue count');const F=new Float32Array(Buffer,16),Records=Array.from({length:Count},(_,I)=>Array.from(F.subarray(I*7,I*7+7)));if(Records.some(R=>R.some(V=>!Number.isFinite(V))))throw new Error('Non-finite star catalogue');return Records;}
export async function ConstructStarCatalogue(Exchange){const Response=await fetch('Assets/Celestial/BrightStars.bin');if(!Response.ok)throw new Error('Native star catalogue could not be loaded');const Records=DecodeStarCatalogue(await Response.arrayBuffer()),Bins=Array.from({length:1024},()=>[]);
 for(const R of Records){const D=R.slice(0,3),Axis=Math.abs(D[2])>.9?[0,1,0]:[0,0,1];let U=[D[1]*Axis[2]-D[2]*Axis[1],D[2]*Axis[0]-D[0]*Axis[2],D[0]*Axis[1]-D[1]*Axis[0]];const N=Math.hypot(...U);U=U.map(V=>V/N);const V=[D[1]*U[2]-D[2]*U[1],D[2]*U[0]-D[0]*U[2],D[0]*U[1]-D[1]*U[0]],Touched=new Set([StarCell(D)]);
  // Conservative footprint overlap, including octahedral seams and large editor pixels.
  for(let I=0;I<12;I++){const A=I*Math.PI/6;Touched.add(StarCell(D.map((X,J)=>X+.04*(U[J]*Math.cos(A)+V[J]*Math.sin(A)))));}
  for(const Cell of Touched)Bins[Cell].push(R);
 }
 const Cells=new Uint32Array(2048),Data=[];Bins.forEach((B,I)=>{Cells[I*2]=Data.length/8;Cells[I*2+1]=B.length;for(const R of B)Data.push(...R,0);});
 const Width=1024,Height=Math.ceil((1024+Data.length/4)/1024),Texels=new Float32Array(Width*Height*4);
 for(let I=0;I<1024;I++){Texels[I*4]=Cells[I*2];Texels[I*4+1]=Cells[I*2+1];}
 Texels.set(Data,1024*4);
 const Texture=Exchange.ConstructTexture({label:'Native star catalogue cells + records',size:[Width,Height],format:'rgba32float',usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST});
 Exchange.Device.queue.writeTexture({texture:Texture},Texels,{bytesPerRow:Width*16,rowsPerImage:Height},[Width,Height]);
 return {Records,Texture,View:Texture.createView(),BinnedCount:Data.length/8};
}
