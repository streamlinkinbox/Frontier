// Common-origin packet for a distant circular source. Geometry stays instanced.
// Bounding intervals enclose every sample direction under a rigid BLAS transform.
bool capBox(vec3 o, vec3 centre, float delta, vec3 lo, vec3 hi){
  float tn=0.003,tf=48.0;
  for(int k=0;k<3;k++){
    float a=centre[k]-delta,b=centre[k]+delta;
    if(b>0.)tn=max(tn,(lo[k]-o[k])/b);else if(b<0.)tf=min(tf,(lo[k]-o[k])/b);else if(o[k]<lo[k])return false;
    if(a>0.)tf=min(tf,(hi[k]-o[k])/a);else if(a<0.)tn=max(tn,(hi[k]-o[k])/a);else if(o[k]>hi[k])return false;
  }return tf>=tn;
}
const int packetSize=32;
float sunPacket(vec3 origin, vec3 L,float tangentRadius,out float blocker){
  float farT[packetSize];vec3 direction[packetSize];vec3 T,B;buildOrthonormalBasis(L,T,B);
  float cmin=inversesqrt(1.+tangentRadius*tangentRadius);
  float delta=sqrt(2.*(1.-cmin))+0.00001;
  for(int j=0;j<packetSize;j++){vec2 q=sobol2(uint(j));float c=mix(1.,cmin,q.x),a=6.283185307*q.y;direction[j]=c*L+sqrt(max(0.,1.-c*c))*(T*cos(a)+B*sin(a));farT[j]=48.;}
  int stack[64];int depth=1,floorDepth=-1;stack[0]=0;
  vec3 o=origin,axis=L;mat3 normalToWorld=mat3(1.);
  while(depth>0){
    if(depth==floorDepth){floorDepth=-1;o=origin;axis=L;}
    int id=stack[--depth];vec4 lo=fetchNodeTexel(id*2),hi=fetchNodeTexel(id*2+1);
    if(!capBox(o,axis,delta,lo.xyz,hi.xyz))continue;
    int first=int(lo.w+0.5),count=int(round(hi.w));
    if(count<0){int inst=-1-count;mat4 inv=fetchInstanceMat(inst,4);o=(inv*vec4(origin,1.)).xyz;axis=mat3(inv)*L;normalToWorld=transpose(mat3(inv));floorDepth=depth;stack[depth++]=first;}
    else if(count==0){stack[depth++]=first;stack[depth++]=first+1;}
    else for(int ti=0;ti<count;ti++){
      int base=(first+ti)*4;vec4 a=fetchTriTexel(base);if(int(a.w+.5)==7)continue;
      vec3 e=fetchTriTexel(base+1).xyz,f=fetchTriTexel(base+2).xyz,off=o-a.xyz;
      vec3 dv=normalToWorld*cross(f,e),uv=normalToWorld*cross(f,off),vv=normalToWorld*cross(off,e);
      float travelNumer=dot(f,cross(off,e));
      for(int j=0;j<packetSize;j++){vec3 D=direction[j];float det=dot(D,dv);if(abs(det)<1.e-8)continue;
        float u=dot(D,uv)/det,v=dot(D,vv)/det,t=travelNumer/det;
        if(u>=0.&&v>=0.&&u+v<=1.&&t>=.003&&t<farT[j])farT[j]=t;}
    }
  }
  int hits=0;float invSum=0.;for(int j=0;j<packetSize;j++){if(farT[j]<48.){hits++;invSum+=1./max(.005,farT[j]);}}
  blocker=hits>0?float(hits)/invSum:0.;return 1.-float(hits)/float(packetSize);
}
