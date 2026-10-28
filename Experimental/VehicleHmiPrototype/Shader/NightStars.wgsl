// Rebuilt star rendering: catalogue positions, pixel-filtered cores, brighter bounded
// radiance, compact halos and smooth twilight extinction. No new textures or passes.
struct CatalogueStar { DirectionLuminance:vec4f, Colour:vec4f, }
@group(2) @binding(7) var StarCatalogueTexture:texture_2d<f32>;
fn StarTexel(Index:u32)->vec4f{return textureLoad(StarCatalogueTexture,vec2i(i32(Index%1024u),i32(Index/1024u)),0);}
fn ReadCatalogueStar(Index:u32)->CatalogueStar {var S:CatalogueStar;S.DirectionLuminance=StarTexel(1024u+Index*2u);S.Colour=StarTexel(1025u+Index*2u);return S;}
fn StarCell(D:vec3f)->u32 {
 var P=D.xy/(abs(D.x)+abs(D.y)+abs(D.z));
 if(D.z<0.0){P=(1.0-abs(P.yx))*vec2f(select(-1.0,1.0,P.x>=0.0),select(-1.0,1.0,P.y>=0.0));}
 let Cell=vec2u(clamp(floor((P*.5+.5)*32.0),vec2f(0),vec2f(31)));
 return Cell.y*32u+Cell.x;
}
fn ResolveNightStars(D:vec3f,PixelAngle:f32)->vec3f {
 let Night=(1.0-smoothstep(-14.0,-3.0,Frame.Night.w))*smoothstep(0.0,.06,D.z);
 if(Night<=0.0||Frame.Stars.x<=0.0){return vec3f(0);}
 let Rotation=Frame.StarControl.y+Frame.Stars.w;let Latitude=Frame.StarControl.z;
 let PoleX=D.z*cos(Latitude)-D.y*sin(Latitude);let PoleY=-D.x;let PoleZ=D.z*sin(Latitude)+D.y*cos(Latitude);
 let V=normalize(vec3f(PoleX*cos(Rotation)+PoleY*sin(Rotation),PoleX*sin(Rotation)-PoleY*cos(Rotation),PoleZ));
 let Cell=vec2u(StarTexel(StarCell(V)).xy);let Threshold=pow(10.0,-.4*Frame.Stars.y);
 // A subpixel star is represented by a finite, resolution-aware point spread.
 let Width=max(.00008,PixelAngle*.70*Frame.Stars.z);let W2=Width*Width;var Light=vec3f(0);
 for(var I=0u;I<Cell.y;I++){
  let S=ReadCatalogueStar(Cell.x+I);let Lum=S.DirectionLuminance.w;if(Lum<Threshold){continue;}
  // Difference vector avoids catastrophic 1-dot cancellation at high resolution.
  let Delta=V-S.DirectionLuminance.xyz;let Radius2=dot(Delta,Delta)/W2;if(Radius2>12.0){continue;}
  let Core=exp(-Radius2*1.8);let Halo=exp(-Radius2*.4)*min(.12,sqrt(Lum)*.025)*Frame.StarControl.w;
  let Phase=dot(S.DirectionLuminance.xyz,vec3f(91.7,213.1,57.3));
  let Time=Frame.ElapsedSeconds*Frame.Night.y;let Signal=.6*sin(Time*6.283185+Phase)+.28*sin(Time*11.71+Phase*1.7)+.12*sin(Time*19.37+Phase*2.3);
  let Flicker=1.0+Frame.Night.x*.35*Signal;
  // Compress magnitudes for an authored, readable night sky instead of vanishing faint points.
  let Gain=min(12.0,.04+3.0*pow(max(0.0,Lum),.65));
  Light+=S.Colour.rgb*(Core+Halo)*Gain*Flicker;
 }
 let Plane=dot(V,normalize(vec3f(.3,.83,.47)));
 // Bounded multiscale texture: structured galactic glow and a dark central dust lane.
 let Structure=.55+.25*sin(V.x*27.0+V.z*19.0)*sin(V.y*21.0-V.z*11.0)+.15*sin(V.x*73.0+V.y*49.0)*sin(V.z*57.0);
 let Band=exp(-Plane*Plane/.018)*max(.1,Structure);
 let Dust=1.0-.75*exp(-pow((Plane+.018*sin(V.x*19.0))/.025,2.0))*(.65+.35*sin(V.y*17.0)*sin(V.z*23.0));
 let Airmass=1.0/max(.12,D.z+.08);let Extinction=exp(-.06*(Airmass-1.0));
 return Night*Frame.Stars.x*Extinction*(Light+vec3f(.002,.0024,.0034)*Band*Dust*Frame.Night.z);
}
