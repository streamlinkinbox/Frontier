// Shared density/intersections of the independent v2 renderer. Bind C, ShapeField and FieldSampler.
fn LayerHit(O:vec3f,D:vec3f,Limit:f32)->vec2f{
 let Top=min(C.Control.w,C.Layer.x+C.Layer.y);
 if(C.Control.x<.5||Top<=C.Layer.x){return vec2f(Limit);}
 if(abs(D.z)<1e-5){if(O.z>C.Layer.x&&O.z<Top){return vec2f(0,Limit);}return vec2f(Limit);}
 let A=(C.Layer.x-O.z)/D.z;let B=(Top-O.z)/D.z;
 let R=vec2f(max(0.0,min(A,B)),min(Limit,max(A,B)));return select(vec2f(Limit),R,R.y>R.x);
}
fn PuffHit(O:vec3f,D:vec3f,Limit:f32)->vec2f{
 if(C.Centre.w<.5){return vec2f(Limit);}
 return VolumeHit(O,D,C.Centre.xyz,C.Size.xyz,C.LocalShape.x,Limit);
}
fn VolumeHit(O:vec3f,D:vec3f,Centre:vec3f,Size:vec3f,Shape:f32,Limit:f32)->vec2f{
 let A=(O-Centre)/Size;let B=D/Size;
 if(Shape>.5){let Safe=select(vec3f(1e-7),B,abs(B)>vec3f(1e-7));let T0=(-vec3f(1)-A)/Safe;let T1=(vec3f(1)-A)/Safe;let Near=min(T0,T1);let Far=max(T0,T1);let R=vec2f(max(0.0,max(Near.x,max(Near.y,Near.z))),min(Limit,min(Far.x,min(Far.y,Far.z))));return select(vec2f(Limit),R,R.y>R.x);}
 let BB=dot(B,B);let AB=dot(A,B);let H=AB*AB-BB*(dot(A,A)-1);
 if(H<=0){return vec2f(Limit);}let R=vec2f(max(0.0,(-AB-sqrt(H))/BB),min(Limit,(-AB+sqrt(H))/BB));return select(vec2f(Limit),R,R.y>R.x);
}
fn Field(Q:vec3f)->f32{return textureSampleLevel(ShapeField,FieldSampler,Q,0.0).r;}

// Blend incommensurate, obliquely transformed texture coordinates. No single
// axis-aligned texture period survives; retain the compact baked noise asset.
fn OrganicField(Q:vec3f)->f32{
 let R=vec3f(Q.y+Q.z,Q.z-Q.x,Q.x+Q.y)*.932472+vec3f(.173,.419,.731);
 return .5*(Field(Q)+Field(R));
}
fn LayerDensity(P:vec3f)->f32{
 let Top=min(C.Control.w,C.Layer.x+C.Layer.y);let H=clamp((P.z-C.Layer.x)/max(1.0,Top-C.Layer.x),0.0,1.0);
 var Profile=clamp(H*8.0,0.0,1.0)*clamp((1.0-H)*3.0,0.0,1.0);
 if(C.Shape.z<1.5){Profile=clamp(H*12.0,0.0,1.0)*clamp((1.0-H)*8.0,0.0,1.0);}
 if(C.Shape.z>2.5&&C.Shape.z<3.5){Profile*=1.0+C.Shape.w*H*.5;}
 if(C.Shape.z>3.5){let Q=H*2.0-1.0;Profile*=max(0.0,1.0-Q*Q);}
 let Q=(P-C.Wind.xyz)/max(7200.0*C.Shape.x,1.0);
 let Shape=Field(Q)-C.Formation.x*(1.0-Field(Q*4.1+vec3f(.21,.47,.13)))*.22;
 let Edge=clamp((Shape-(1.0-C.Layer.z))/max(.08,C.Layer.z*.65),0.0,1.0);
 return Edge*Edge*(3.0-2.0*Edge)*Profile*C.Layer.w;
}
// Port of the pinned demo's hash13 / vnoise and lcDensity, converted Y-up -> Z-up.
fn DemoCloudHash(P:vec3f)->f32{var Q=fract(P*.1031);Q+=vec3f(dot(Q,Q.zyx+vec3f(31.32)));return fract((Q.x+Q.y)*Q.z);}
fn DemoCloudNoise(P:vec3f)->f32{let B=floor(P);let F0=fract(P);let F=F0*F0*(vec3f(3)-2.0*F0);var Sum=0.0;
 for(var Z=0u;Z<2u;Z++){for(var Y=0u;Y<2u;Y++){for(var X=0u;X<2u;X++){let W=select(1.0-F.x,F.x,X==1u)*select(1.0-F.y,F.y,Y==1u)*select(1.0-F.z,F.z,Z==1u);Sum+=DemoCloudHash(B+vec3f(f32(X),f32(Y),f32(Z)))*W;}}}return Sum;}
fn DemoPuffDensity(P:vec3f,Light:bool)->f32{
 let Q=(P-C.Centre.xyz)/C.Size.xyz;let Margin=select(1.0-length(Q),1.0-max(abs(Q.x),max(abs(Q.y),abs(Q.z))),C.LocalShape.x>.5);
 let Mask=smoothstep(0.0,max(.05,C.LocalShape.z),Margin);if(Mask<=0.0){return 0.0;}
 let H=clamp(Q.z*.5+.5,0.0,1.0);var Profile=smoothstep(0.0,.1,H)*(1.0-smoothstep(.75,1.0,H));
 if(C.LocalShape.y>.5&&C.LocalShape.y<1.5){Profile=smoothstep(0.0,.08,H)*(1.0-smoothstep(.4,1.0,H))*1.15;}
 if(C.LocalShape.y>1.5){Profile=smoothstep(0.0,.3,H)*(1.0-smoothstep(.6,1.0,H))*.6;}
 // Noise is evaluated in reference Y-up axes; world placements remain Z-up.
 let W=(P+C.Advection.xyz*.6)/max(1.0,C.Size.w);let S=W.xzy;
 let Shape=(DemoCloudNoise(S)*.5+DemoCloudNoise(S*2.02+vec3f(3.1,1.7,9.2))*.25+DemoCloudNoise(S*4.1+vec3f(7.7,2.2,1.1))*.125+DemoCloudNoise(S*8.3+vec3f(1.3,8.8,4.4))*.0625)/.9375;
 let Base=clamp((Shape-(1.0-C.Local.y))/max(.001,C.Local.y),0.0,1.0)*Profile*Mask;if(Base<=0.0){return 0.0;}if(Light){return Base*C.Local.x;}
 let Detail=DemoCloudNoise(S*9.0)*.6+DemoCloudNoise(S*19.0+vec3f(5))*.4;
 let Erode=mix(Detail,1.0-Detail,clamp(H*3.0,0.0,1.0))*C.LocalShape.w*.45;
 return clamp(Base-Erode*(1.0-Base),0.0,1.0)*C.Local.x;
}
fn PuffDensity(P:vec3f)->f32{return DemoPuffDensity(P,false);}
fn PuffLightingDensity(P:vec3f)->f32{return DemoPuffDensity(P,true);}
fn FogHit(O:vec3f,D:vec3f,Limit:f32)->vec2f{
 if(C.FogCentre.w<.5){return vec2f(Limit);}return VolumeHit(O,D,C.FogCentre.xyz,C.FogSize.xyz,select(0.0,1.0,C.FogSize.w==1.0||C.FogSize.w==3.0),Limit);
}
fn FogDensity(P:vec3f)->f32{
 let Q=(P-C.FogCentre.xyz)/C.FogSize.xyz;var Margin=1.0-length(Q);
 if(C.FogSize.w==1.0){Margin=1.0-max(abs(Q.x),max(abs(Q.y),abs(Q.z)));}
 if(C.FogSize.w==3.0){let H=(Q.z+1.0)*.5;Margin=min(1.0-length(vec3f(Q.xy,H)),H*4.0);}
 if(Margin<=0.0){return 0.0;}
 var Mask=smoothstep(0.0,max(.02,C.FogLighting.y*.5),Margin);
 if(C.FogControl.y>.5){Mask=pow(smoothstep(0.0,1.0,clamp(Margin,0.0,1.0)),mix(.35,3.0,C.FogLighting.y));}
 let UV=(P+C.FogWind.xyz*.35)/max(.5,C.FogMedium.z);
 let Noise=OrganicField(UV)*.55+OrganicField(UV*2.3+vec3f(1.7))*.3+OrganicField(UV*5.1+vec3f(4.2))*.15;
 let Het=mix(1.0,smoothstep(.28,.85,Noise)*1.6,C.FogMedium.y);
 let Gravity=1.0-.35*clamp(Q.z*.5+.5,0.0,1.0);
 return C.FogMedium.x*Mask*Het*Gravity;
}
