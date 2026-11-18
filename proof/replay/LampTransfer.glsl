// Source term only. Account for emitter sampling separately from secondary GI.
uniform sampler2D uLampFaces; // 4 vec4 per triangle: a, edge1, edge2, Le; authored world geometry
void main(){
 vec4 guide=texelFetch(uGNormMat,ivec2(gl_FragCoord.xy),0);
 if(guide.w<0.5){outShadow=vec4(0);return;}
 vec3 p=texelFetch(uGPosRough,ivec2(gl_FragCoord.xy),0).xyz,n=normalize(guide.xyz),Lo=vec3(0);
 int count=textureSize(uLampFaces,0).y;
 for(int j=0;j<count;j++){
   vec3 a=texelFetch(uLampFaces,ivec2(0,j),0).xyz;
   vec3 e=texelFetch(uLampFaces,ivec2(1,j),0).xyz,f=texelFetch(uLampFaces,ivec2(2,j),0).xyz;
   vec3 areaVector=0.5*cross(e,f),Le=texelFetch(uLampFaces,ivec2(3,j),0).rgb;
   if(dot(areaVector,p-a)<=0.)continue;
   vec3 x=a+(e+f)/3., d=x-p;float r2=dot(d,d);if(r2<1.e-12)continue;
   // cos(receiver)*cos(emitter)*area / (pi*r^2); both cosines use normalized d.
   float F=max(dot(n,d),0.)*max(dot(areaVector,-d),0.)/(3.14159265*r2*r2);
   if(F<=0.)continue;
   vec3 o=p+0.004*n;vec3 ray=x-o;float len=length(ray);
   if(len>0.001 && traceSceneShadowBlockerBvh(o,ray/len,0.,len-.001)<0.)Lo+=Le*F;
 }
 outShadow=vec4(Lo,1.);
}
