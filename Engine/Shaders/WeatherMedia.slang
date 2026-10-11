#ifndef FRONTIER_WEATHER_MEDIA
#define FRONTIER_WEATHER_MEDIA
// Live camera-ray media. WeatherConstantRecord supplies binding-24 rows.
// Density matches VolumetricMedia; overlapping media share extinction/source terms.
// Reference altitude (km) each medium advects its BULK translation at: mid-slab for the deck, centre for a
//    local puff. Sliding the whole medium at ONE altitude keeps its shape rigid; the optional shear term below
//    adds the height-dependent lean on top of that.
float WeatherRefKm(int medium)
{
    if(medium==0){float base,top;CloudShadowExtent(Weather[0].x,Weather[0].y,Weather[1].z,base,top);return (base+top)*0.5*.001;}
    int b=medium==1?4:9;return max(Weather[b].z,0.0)*.001;
}
// Wind advection. Weather[3].z is the mode: 0 = TRANSLATE (rigid slide, shape preserved), 1 = SHEAR (bounded
//    lean). The bulk is the wind at the reference altitude integrated over the clock — uniform across height, so
//    it can only move the field, never distort it. Shear adds the height-relative velocity difference, but its
//    time growth is CAPPED at 45 s so the medium leans to a steady tilt instead of being stretched without limit
//    into decorrelated noise (the "wind ruins the shape" report: unbounded shear x clock tears a cloud's top and
//    bottom apart until adjacent heights sample unrelated noise). Twin: VolumetricMedia::CloudDensity.
vec2 WeatherDrift(int medium, float z, float factor)
{
    vec4 wind=Weather[18+medium];
    float refKm=WeatherRefKm(medium);
    float aR=radians(wind.y+wind.w*refKm);
    vec2  bulk=vec2(sin(aR),cos(aR))*(wind.x*(1.0+wind.z*refKm));
    vec2  drift=bulk*(Weather[3].x*factor);
    if(Weather[3].z>0.5){
        float km=max(z,0.0)*.001;
        float aZ=radians(wind.y+wind.w*km);
        vec2  local=vec2(sin(aZ),cos(aZ))*(wind.x*(1.0+wind.z*km));
        drift+=(local-bulk)*(min(Weather[3].x,45.0)*factor);
    }
    return drift;
}
// Seconds the weather clock has run, and how far that has walked each medium through its noise. Weather[3].x
//    is the clock (packed whenever any volume is enabled, not only when something drifts), and the rate is the
//    shared one so the shader and the CPU mirror boil at the same speed.
float WeatherEvolve(int medium)
{
    return Weather[3].x * CloudEvolutionRate(Weather[18+medium].x);
}
float WeatherDensity(int medium, vec3 p)
{
    if(medium==0){
        if(Weather[3].w==0.0)return 0.0;
        float base,top;
        if(!CloudShadowExtent(Weather[0].x,Weather[0].y,Weather[1].z,base,top)||p.z<base||p.z>top)return 0.0;
        vec2 drift=Weather[3].y>0.0?WeatherDrift(0,p.z,.8):vec2(0);
        return max(0.0,CloudShadowDensity(uint(Weather[1].w),base,top,Weather[0].z,Weather[0].w,Weather[1].x,Weather[1].y,drift.x,drift.y,WeatherEvolve(0),p));
    }
    int b=medium==1?4:9;
    if(Weather[b].w==0.0)return 0.0;
    float r=length((p-Weather[b].xyz)/max(Weather[b+1].xyz,vec3(.001)));
    if(r>=1.0)return 0.0;
    vec2 drift=Weather[b+4].x>0.0?WeatherDrift(medium,p.z,.6):vec2(0);
    vec3 s=(p+vec3(drift,0))/max(Weather[b+1].w,1.0);
    // Same two octaves as before, but the second is rotated off the first's axes and both walk with the clock:
    //    a local puff that only slides is a decal, and its lattice was as visible as the layer's.
    float evolve=WeatherEvolve(medium);
    vec3 o1=CloudShadowTurn1(s)*2.02+vec3(3.1,1.7,9.2+evolve*1.7);
    float shape=(CloudShadowNoise(s.x,s.y,s.z+evolve)*.5+CloudShadowNoise(o1.x,o1.y,o1.z)*.25)/.75;
    float threshold=1.0-clamp(Weather[b+2].y,0.0,1.0);
    float raw=clamp((shape*.5+.5-threshold)/max(1.0-threshold,.001),0.0,1.0);
    return smoothstep(0.0,1.0,raw)*(1.0-smoothstep(.55,1.0,r))*max(Weather[b+2].x,0.0);
}
float WeatherPhase(float cosine,float g)
{
    g=clamp(g,-.9,.9);
    return (1.0-g*g)/(12.56637061436*pow(max(1.0+g*g-2.0*g*cosine,.0001),1.5));
}
bool WeatherBox(int b,vec3 origin,vec3 direction,float maximum,out float lo,out float hi)
{
    lo=0.0;hi=maximum;
    if(Weather[b].w==0.0)return false;
    for(int c=0;c<3;++c){
        float lower=Weather[b][c]-max(Weather[b+1][c],.001);
        float upper=Weather[b][c]+max(Weather[b+1][c],.001);
        if(abs(direction[c])<1e-6){if(origin[c]<lower||origin[c]>upper)return false;}
        else{float a=(lower-origin[c])/direction[c],z=(upper-origin[c])/direction[c];lo=max(lo,min(a,z));hi=min(hi,max(a,z));}
    }
    return hi>lo;
}
float WeatherShadow(vec3 p,float stepSize,float jitter)
{
    float optical=0.0;
    for(int i=1;i<=int(Weather[17].z);++i){
        vec3 q=p+SkySunDirection.xyz*(stepSize*(float(i)-1.0+jitter)*.5);
        optical+=(WeatherDensity(0,q)+WeatherDensity(1,q)+WeatherDensity(2,q))*stepSize*.005;
        if(optical>4.0)break;
    }
    return exp(-optical);
}
float WeatherColumn(float z0,float z1,float distance,float height)
{
    z0=max(z0,0.0);z1=max(z1,0.0);height=max(height,1.0);
    float rise=z1-z0;
    if(abs(rise)<.001)return exp(-z0/height)*distance;
    return max(0.0,height*(exp(-z0/height)-exp(-z1/height))*distance/rise);
}
// ⚠️ `jitter` in [0,1) offsets the first sample of every march by a fraction of a step. Without it a fixed
//    step size draws its own aliasing into the image: the sample planes are parallel surfaces in the volume, so
//    the frame shows evenly spaced bands wherever the medium is thick (the corduroy in the owner's captures)
//    and the banding MOVES WITH THE CAMERA, which no amount of accumulation removes. Offsetting each pixel's
//    ray by a hashed fraction turns that structure into per-pixel noise, and per-pixel noise is exactly what
//    the accumulator and the denoiser were built to remove. Cost: one extra multiply-add per march.
vec3 ApplyWeather(vec3 origin,vec3 direction,float maximum,vec3 background,float jitter)
{
    if(Weather[17].w==0.0)return background;
    maximum=clamp(maximum,0.0,100000.0);
    vec3 ambient=max(Weather[8].yzw,vec3(0));
    vec3 sunlight=SkySunRadiance.xyz*smoothstep(-12.0,0.0,SkySunDirection.w);
    float nearT[3],farT[3];bool hit[3];
    float base,top;nearT[0]=0.0;farT[0]=0.0;
    hit[0]=false;
    if(Weather[3].w>0.0&&CloudShadowExtent(Weather[0].x,Weather[0].y,Weather[1].z,base,top))
        hit[0]=CloudShadowInterval(origin.z,direction.z,base,top,maximum,nearT[0],farT[0]);
    hit[1]=WeatherBox(4,origin,direction,maximum,nearT[1],farT[1]);
    hit[2]=WeatherBox(9,origin,direction,maximum,nearT[2],farT[2]);
    // Split at all entry/exit boundaries. Empty gaps cost no samples; overlapping
    // boxes use a single mixture rather than ordering entire volumes incorrectly.
    float edges[6];
    for(int m=0;m<3;++m){edges[m*2]=hit[m]?nearT[m]:maximum;edges[m*2+1]=hit[m]?farT[m]:maximum;}
    for(int a=1;a<6;++a)for(int b=a;b>0&&edges[b]<edges[b-1];--b){float v=edges[b];edges[b]=edges[b-1];edges[b-1]=v;}
    vec3 scatter=vec3(0);float transmission=1.0;
    float cosine=dot(direction,SkySunDirection.xyz);
    for(int segment=0;segment<5;++segment){
        float lo=edges[segment],hi=edges[segment+1],mid=(lo+hi)*.5;
        if(hi<=lo||transmission<.005)continue;
        float spacing=1e30;int cap=1;bool occupied=false;
        for(int m=0;m<3;++m)if(hit[m]&&mid>=nearT[m]&&mid<=farT[m]){
            occupied=true;
            spacing=min(spacing,m==0?4000.0/max(Weather[17].x,1.0):max(Weather[m==1?5:10].w*.5,1.0));
            cap=max(cap,m==0?int(Weather[17].x)*4:int(Weather[17].y));
        }
        if(!occupied)continue;
        // Finite GPU budget, unlike the CPU reference's unbounded distant march.
        int count=int(clamp(ceil((hi-lo)/spacing),1.0,float(min(cap,512))));
        float ds=(hi-lo)/float(count);
        for(int i=0;i<count;++i){
            vec3 p=origin+direction*(lo+(float(i)+jitter)*ds);
            vec3 density=vec3(WeatherDensity(0,p),WeatherDensity(1,p),WeatherDensity(2,p));
            float total=density.x+density.y+density.z;
            if(total<=1e-5)continue;
            float shadow=WeatherShadow(p,ds,jitter);
            vec3 source=density.x*Weather[2].xyz*(ambient+sunlight*shadow*WeatherPhase(cosine,Weather[2].w));
            source+=density.y*Weather[7].xyz*(ambient+sunlight*shadow*WeatherPhase(cosine,Weather[6].z));
            source+=density.z*vec3(.88)*(ambient+sunlight*shadow*WeatherPhase(cosine,Weather[11].z));
            float stepT=exp(-total*ds*.01);
            scatter+=transmission*(1.0-stepT)*source/total;
            transmission*=stepT;
            if(transmission<.005)break;
        }
    }
    vec3 rgb=background*transmission+scatter;
    float endZ=origin.z+direction.z*maximum;
    if(Weather[14].w>0.0){
        float od=max(Weather[14].x,0.0)*WeatherColumn(origin.z,endZ,maximum,Weather[14].y);
        float t=exp(-min(od*od,80.0));
        vec3 source=Weather[15].xyz*ambient+.02*Weather[14].z*WeatherPhase(cosine,.55)*clamp(SkySunDirection.z+.1,0.0,1.0)*SkySunRadiance.xyz;
        rgb=rgb*t+source*(1.0-t);
    }
    if(Weather[15].w>0.0){
        float start=clamp(Weather[16].y,0.0,maximum),distance=maximum-start;
        float z=origin.z+direction.z*start;
        float rayleigh=WeatherColumn(z,endZ,distance,SkyRayleigh.w),mie=WeatherColumn(z,endZ,distance,SkyMie.y);
        float mixMie=clamp(Weather[16].z,0.0,1.0);
        vec3 tau=max(Weather[16].x,0.0)*((1.0-mixMie)*max(SkyRayleigh.xyz,vec3(0))*rayleigh+mixMie*max(SkyMie.x,0.0)*mie);
        vec3 t=exp(-min(tau,vec3(80)));
        rgb=rgb*t+ambient*(1.0-t);
    }
    return rgb;
}
#endif
