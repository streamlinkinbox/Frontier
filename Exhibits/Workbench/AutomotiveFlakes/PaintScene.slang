// Shared CPU/GLSL review scene: a 140 x 90 mm curved, ellipsoidal paint swatch.
struct APPaintHit { float Hit; vec3 Position; vec3 Normal; vec3 View; vec2 UV; };
APPaintHit APIntersect(vec2 pixel,vec2 resolution,float yaw,float distance) {
    APPaintHit h;h.Hit=0.0;h.Position=vec3(0.0);h.Normal=vec3(0.0,0.0,1.0);h.View=h.Normal;h.UV=vec2(0.0);
    vec2 ndc=vec2((pixel.x/resolution.x*2.0-1.0)*resolution.x/resolution.y,pixel.y/resolution.y*2.0-1.0);
    vec3 eye=distance*vec3(sin(yaw),0.15,cos(yaw));
    vec3 forward=normalize(-eye),right=normalize(cross(forward,vec3(0.0,1.0,0.0))),up=cross(right,forward);
    vec3 ray=normalize(forward+right*(ndc.x*0.34)+up*(ndc.y*0.34));
    vec3 radius=vec3(0.070,0.045,0.025),o=eye/radius,d=ray/radius;
    float a=dot(d,d),b=dot(o,d),c=dot(o,o)-1.0,disc=b*b-a*c;
    if(disc<0.0)return h;
    float t=(-b-sqrt(disc))/a;if(t<=0.0)return h;
    h.Hit=1.0;h.Position=eye+ray*t;h.Normal=normalize(h.Position/(radius*radius));h.View=-ray;
    h.UV=vec2(h.Position.x,h.Position.y);return h;
}
vec3 APSceneLocal(vec3 x,vec3 n,vec3 t,vec3 b) { return vec3(dot(x,t),dot(x,b),dot(x,n)); }
vec3 APStudio(vec2 pixel,vec2 resolution,AP_INPUT(AutomotivePaintParameters) p,float phase,float yaw,float distance,AP_INPUT(AutomotiveFlakePalette) palette) {
    APPaintHit hit=APIntersect(pixel,resolution,yaw,distance);
    if(hit.Hit==0.0){float vignette=1.0-length(vec2(pixel.x/resolution.x-0.5,pixel.y/resolution.y-0.5));return vec3(0.014,0.018,0.024)*max(0.3,vignette);}
    APPaintHit hx=APIntersect(pixel+vec2(1.0,0.0),resolution,yaw,distance),hy=APIntersect(pixel+vec2(0.0,1.0),resolution,yaw,distance);
    vec2 dx=hx.Hit>0.0?hx.UV-hit.UV:vec2(0.01),dy=hy.Hit>0.0?hy.UV-hit.UV:vec2(0.01);
    AutomotiveFlakeSurface s=AutomotiveApplyFlakePalette(AutomotivePrepareFlakes(p,hit.UV,dx,dy),palette);
    vec3 n=hit.Normal,t=normalize(vec3(1.0,0.0,0.0)-n*n.x),b=cross(n,t);
    vec3 v=APSceneLocal(hit.View,n,t,b);
    float coverage=mix(s.Weight,s.MeanWeight,s.Unresolved);
    // A labelled diffuse ambient fill; glossy illumination below comes from the studio lights.
    vec3 colour=p.Pigment*(0.16*(1.0-coverage))*(1.0-FresnelDielectric(v.z,1.5))*mix(vec3(1.0),AutomotiveCoatTint(p),APRange(p.CoatWeight,0.0,1.0));
    for(int box=0;box<2;++box)for(int y=0;y<12;++y)for(int x=0;x<8;++x){
        float u=(float(x)+0.5)/8.0-0.5,w=(float(y)+0.5)/12.0-0.5;
        vec3 light=box==0?vec3(-0.38+u*1.05,0.52+w*0.30,1.15):vec3(0.8+u*0.25,-0.15+w*1.05,0.85);
        float angle=phase*(box==0?1.0:-0.4);
        light=vec3(cos(angle)*light.x+sin(angle)*light.z,light.y,-sin(angle)*light.x+cos(angle)*light.z);
        vec3 direction=normalize(light);
        vec3 l=APSceneLocal(direction,n,t,b);
        vec3 radiance=box==0?vec3(46.0,40.0,32.0):vec3(12.0,17.0,26.0);
        // Quadrature over the rectangular source's projected solid angle.
        float area=box==0?0.315:0.2625;
        float omega=area*abs(direction.z)/(dot(light,light)*96.0);
        colour+=AutomotiveEvaluatePaint(p,s,v,l)*radiance*(max(0.0,l.z)*omega);
    }
    return colour;
}
vec3 APTonemap(vec3 x) {
    x=x*1.6;
    x=clamp((x*(2.51*x+vec3(0.03)))/(x*(2.43*x+vec3(0.59))+vec3(0.14)),vec3(0.0),vec3(1.0));
    return pow(x,vec3(1.0/2.2));
}
