//================================================================================
// CelestialCore.slang — the celestial panel's sky + sun, line by line.
//
// Reference: docs/celestial/index.html on branch ref/celestial-html (the panel).
// Panel citations are GLSL line numbers (rsi/atmosphere/dawnGlow/main/post).
//
// DUAL-COMPILE CONTRACT (checked by Diagnostics/CheckCelestialSlang.py):
//   - pure functions of their arguments (no globals, no textures, no ray query)
//   - multi-value returns use small structs, built by field assignment
//   - struct parameters use the `in` modifier (empty in C++, input in Slang)
//   - no `&` outside `&&`, no `out`/`inout` tokens, no `{...}` struct literals,
//     no fixed-width int aliases (use uint/int), no leading-dot float literals
//   - float32 only, matching the panel's WebGL precision
//
// Include path: the Host compiles this file as C++17 after SlangCompat.h; the
// Vulkan engine compiles it as Slang via #include. Slang-only rendering code
// (buffers, ray queries, entry points) lives in CelestialReSTIR.slang.
//================================================================================
#ifndef PROJECT_ZERO_CELESTIAL_CORE_SLANG
#define PROJECT_ZERO_CELESTIAL_CORE_SLANG

// Panel line 770: #define PI 3.14159265
static const float CEL_PI = 3.14159265f;
static const float CEL_D2R = 3.14159265f / 180.0f;

//--------------------------------------------------------------------------------
// Parameters (filled by the host; the panel's uniforms + visibility = on).
//--------------------------------------------------------------------------------
struct CelParams
{
    float3 sunDir;          // [-] unit, sky frame (+Y up)
    float3 sunColor;        // [-] kelvinRGB(sun_temp)
    float sunElevationDeg;  // [deg]
    float sunIntensity;     // [x] uSunIntensity (22)
    float sunAngularRadius; // [rad] uSunAng = sun_ang * D2R / 2
    float sunSoftness;      // [-] uSunSoft (0.25)
    float sunDiscBoost;     // [x] uSunDiscBoost (12)
    float rayleigh;         // [x] uRayleigh (1)
    float mie;              // [x] uMie (1)
    float mieG;             // [-] uMieG (0.78)
    float ozone;            // [x] uOzone (1.2)
    float planetRadius;     // [m] uPlanetR (6371000)
    float atmoHeight;       // [m] uAtmoH (100000)
    float rayleighH;        // [m] uHr (8000)
    float mieH;             // [m] uHm (1200)
    float3 skyTint;         // [-] 1-(1-v)*.35 of sky_tint
    float skyBright;        // [x] uSkyBright (1)
    float3 groundAlbedo;    // [-] sky_ground (#2b2924)
    float groundBright;     // [x] uGroundBright (1)
    float dawnIntensity;    // [x] uDawnInt (1)
    float lineIntensity;    // [x] uHLInt (1)
    uint lineAuto;          // [-] uHLAuto (1)
    float exposureStops;    // [EV] uEV (0.4)
    uint tonemap;           // [-] 1 = ACES
    float bloom;            // [x] uBloom (1)
    float vignette;         // [-] uVignette (0.28)
    float grain;            // [-] uGrain (0.1)
    float camHeight;        // [m] uCamHeight (2)
    float timeSeconds;      // [s] uTime (fract * 100 = the grain seed)
};

struct CelCamera
{
    float3 fwd;     // [-] unit, sky frame
    float3 right;   // [-] unit, sky frame
    float3 up;      // [-] unit, sky frame
    float tanHalf;  // [-] tan(fov / 2)
};

struct CelAtmo
{
    float3 sky;     // panel `sky` after tint * brightness
    float3 trans;   // panel `trans`
    float ground;   // panel `ground` flag
    float odR;      // integrated Rayleigh depth (ReSTIR reuse + tests)
    float odM;      // integrated Mie depth
};

struct CelSpan
{
    float t0;
    float t1;
    float ground;
};

CelSpan celSpanMake(float t0, float t1, float ground)
{
    CelSpan s;
    s.t0 = t0;
    s.t1 = t1;
    s.ground = ground;
    return s;
}

CelAtmo celAtmoMake(float3 sky, float3 trans, float ground, float odR, float odM)
{
    CelAtmo o;
    o.sky = sky;
    o.trans = trans;
    o.ground = ground;
    o.odR = odR;
    o.odM = odM;
    return o;
}

//--------------------------------------------------------------------------------
// Scalar helpers. C++ bodies live in the prelude; Slang maps to builtins.
//--------------------------------------------------------------------------------
#ifdef SLANG_COMPAT_CXX
// (celFract / celExp2 / celPow are declared in SlangCompat.h for the C++ path)
#else
float celFract(float x) { return frac(x); }
float celExp2(float x) { return exp2(x); }
float celPow(float x, float y) { return pow(x, y); }
#endif

// Panel line 800, exactly:
//   vec2 rsi(vec3 ro,vec3 rd,float r){ float b=dot(ro,rd); float c=dot(ro,ro)-r*r;
//   float d=b*b-c; if(d<0.) return vec2(-1.,-1.); d=sqrt(d); return vec2(-b-d,-b+d); }
float2 celRaySphere(float3 ro, float3 dir, float r)
{
    float b = dot(ro, dir);
    float c = dot(ro, ro) - r * r;
    float d = b * b - c;
    if (d < 0.0f)
    {
        return float2(-1.0f, -1.0f);
    }
    d = sqrt(d);
    return float2(-b - d, -b + d);
}

// Panel lines 848-849:
//   float Ra=uPlanetR+uAtmoH; vec2 ta=rsi(ro,rd,Ra); if(ta.y<0.) return;
//   float t0=max(ta.x,0.), t1=ta.y; vec2 tg=rsi(ro,rd,uPlanetR);
//   if(tg.x>0.){ t1=tg.x; ground=1.; }
CelSpan celAtmosphereSpan(float3 ro, float3 dir, in CelParams p)
{
    float Ra = p.planetRadius + p.atmoHeight;
    float2 ta = celRaySphere(ro, dir, Ra);
    if (ta.y < 0.0f)
    {
        return celSpanMake(0.0f, -1.0f, 0.0f);
    }
    float t0 = max(ta.x, 0.0f);
    float t1 = ta.y;
    float ground = 0.0f;
    float2 tg = celRaySphere(ro, dir, p.planetRadius);
    if (tg.x > 0.0f)
    {
        t1 = tg.x;
        ground = 1.0f;
    }
    return celSpanMake(t0, t1, ground);
}

// Panel lines 861/863 shared extinction shape:
//   exp(-(betaR*odR+betaM*1.1*odM+betaO*odR))
float3 celExpAtt(float3 betaR, float3 betaM, float3 betaO, float odR, float odM)
{
    float ex = betaR.x * odR + betaM.x * 1.1f * odM + betaO.x * odR;
    float ey = betaR.y * odR + betaM.y * 1.1f * odM + betaO.y * odR;
    float ez = betaR.z * odR + betaM.z * 1.1f * odM + betaO.z * odR;
    return float3(exp(-ex), exp(-ey), exp(-ez));
}

// Panel lines 847-863 (N=20 primary segs, NL=8 light segs, quadratic spacing).
CelAtmo celAtmosphere(float3 ro, float3 dir, in CelParams p)
{
    CelSpan span = celAtmosphereSpan(ro, dir, p);
    if (span.t1 < 0.0f)
    {
        return celAtmoMake(float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f),
                           0.0f, 0.0f, 0.0f);
    }
    float t0 = span.t0;
    float t1 = span.t1;
    float ground = span.ground;
    const int N = 20;
    const int NL = 8;
    float len = t1 - t0;
    float3 betaR = float3(5.8e-6f, 13.5e-6f, 33.1e-6f) * p.rayleigh;
    float3 betaM = float3(21e-6f, 21e-6f, 21e-6f) * p.mie;
    float3 betaO = float3(0.65e-6f, 1.881e-6f, 0.085e-6f) * p.ozone;
    float3 sumR = float3(0.0f, 0.0f, 0.0f);
    float3 sumM = float3(0.0f, 0.0f, 0.0f);
    float odR = 0.0f;
    float odM = 0.0f;
    float mu = dot(dir, p.sunDir);
    float g = p.mieG;
    float pR = 3.0f / (16.0f * CEL_PI) * (1.0f + mu * mu);
    float pM = 3.0f / (8.0f * CEL_PI) * ((1.0f - g * g) * (1.0f + mu * mu))
             / ((2.0f + g * g) * celPow(1.0f + g * g - 2.0f * g * mu, 1.5f));
    for (int i = 0; i < N; i++)
    {
        float s0 = float(i) / float(N);
        float s1 = float(i + 1) / float(N);
        s0 *= s0;
        s1 *= s1;
        float ta = t0 + len * s0;
        float tb = t0 + len * s1;
        float seg = tb - ta;
        float tm = 0.5f * (ta + tb);
        float3 pp = ro + dir * tm;
        float h = length(pp) - p.planetRadius;
        float hr = exp(-h / p.rayleighH) * seg;
        float hm = exp(-h / p.mieH) * seg;
        odR += hr;
        odM += hm;
        float2 tl = celRaySphere(pp, p.sunDir, p.planetRadius + p.atmoHeight);
        float lenL = tl.y;
        float olR = 0.0f;
        float olM = 0.0f;
        bool ok = true;
        for (int j = 0; j < NL; j++)
        {
            float q0 = float(j) / float(NL);
            float q1 = float(j + 1) / float(NL);
            q0 *= q0;
            q1 *= q1;
            float segL = lenL * (q1 - q0);
            float3 qq = pp + p.sunDir * (lenL * 0.5f * (q0 + q1));
            float hq = length(qq) - p.planetRadius;
            if (hq < 0.0f)
            {
                ok = false;
                break;
            }
            olR += exp(-hq / p.rayleighH) * segL;
            olM += exp(-hq / p.mieH) * segL;
        }
        if (ok)
        {
            float3 att = celExpAtt(betaR, betaM, betaO, odR + olR, odM + olM);
            sumR += att * hr;
            sumM += att * hm;
        }
    }
    float3 trans = celExpAtt(betaR, betaM, betaO, odR, odM);
    float3 sky = (sumR * betaR * pR + sumM * betaM * pM) * p.sunIntensity * p.sunColor;
    return celAtmoMake(sky, trans, ground, odR, odM);
}

// Panel line 1148: normalize(dir.xz + 1e-5) — the epsilon pads the components.
float2 celHorizOf(float3 v)
{
    float2 h = float2(v.x + 1e-5f, v.z + 1e-5f);
    float l = sqrt(h.x * h.x + h.y * h.y);
    return float2(h.x / l, h.y / l);
}

float celFacing(float3 dir, float3 sunDir)
{
    float2 h = celHorizOf(dir);
    float2 s = celHorizOf(sunDir);
    return 0.5f + 0.5f * (h.x * s.x + h.y * s.y);
}

// Panel lines 910-937. Multiplication order follows the panel term by term.
float3 celDawnGlow(float3 dir, float elevDeg, float facing, in CelParams p)
{
    float alt = asin(clamp(dir.y, -1.0f, 1.0f)) / CEL_D2R;
    if (alt < -2.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float altp = max(alt, 0.0f);
    float dAz = acos(clamp(facing * 2.0f - 1.0f, -1.0f, 1.0f));
    float az = exp(-celPow(dAz / 0.95f, 2.0f));
    float azW = exp(-celPow(dAz / 1.8f, 2.0f));
    float tw = smoothstep(-16.0f, -5.0f, elevDeg)
             * (1.0f - smoothstep(0.5f, 6.0f, elevDeg));
    float depth = clamp(-elevDeg / 10.0f, 0.0f, 1.0f);
    float3 c0 = float3(1.0f, 0.88f, 0.62f);
    float3 c1 = float3(1.0f, 0.62f, 0.28f);
    float3 c2 = float3(0.95f, 0.42f, 0.30f);
    float3 c3 = float3(0.62f, 0.36f, 0.48f);
    float3 c4 = float3(0.25f, 0.30f, 0.58f);
    float u = log2(1.0f + altp * 2.0f);
    float3 c = lerp(c0, c1, smoothstep(0.0f, 1.6f, u));
    c = lerp(c, c2, smoothstep(1.6f, 2.9f, u));
    c = lerp(c, c3, smoothstep(2.9f, 4.0f, u));
    c = lerp(c, c4, smoothstep(4.0f, 5.2f, u));
    c = lerp(c, lerp(c2, c4, 0.6f), depth * 0.6f);
    float H = lerp(1.9f, 3.8f, depth);
    float env = exp(-altp / H) * (1.0f - depth * 0.35f);
    float rim = exp(-altp / 0.45f) * (1.0f - depth);
    float3 glow = (c * env * 0.30f + c0 * rim * 0.25f)
                * (az * 0.85f + azW * 0.15f) * tw;
    float wlWin = lerp(1.0f, smoothstep(-5.5f, -2.5f, elevDeg)
                      * (1.0f - smoothstep(-0.6f, 0.3f, elevDeg)),
                      p.lineAuto > 0u ? 1.0f : 0.0f);
    float lineAz = exp(-celPow(dAz / 0.55f, 2.0f));
    float line = exp(-celPow(alt / 0.11f, 2.0f))
               * (0.7f + 0.3f * smoothstep(-0.4f, 0.0f, alt));
    glow = glow + float3(1.0f, 0.98f, 0.92f) * line * lineAz * wlWin
         * p.lineIntensity * 0.45f;
    float domeWin = smoothstep(-16.0f, -8.0f, elevDeg)
                  * (1.0f - smoothstep(-2.0f, 4.0f, elevDeg));
    glow = glow + float3(0.10f, 0.15f, 0.30f) * 0.035f * domeWin
         * (1.0f - exp(-altp / 6.0f)) * (1.0f - 0.5f * az);
    return glow * p.dawnIntensity;
}

// Panel airMassOf: z = 90-e; z>=96 -> 40; else min(40, 1/(cos z + k))).
float celAirMassOf(float e)
{
    float z = 90.0f - e;
    if (z >= 96.0f)
    {
        return 40.0f;
    }
    return min(40.0f, 1.0f / (cos(z * CEL_D2R)
           + 0.50572f * celPow(96.07995f - z, -1.6364f)));
}

// Panel lines 1240-1241:
//   vec3 ext=exp(-(vec3(5.8e-6,13.5e-6,33.1e-6)*uRayleigh*uHr
//                +vec3(21e-6)*1.1*uMie*uHm)*airMassOf(elevDeg));
//   vec3 sunCol=uSunColor*max(ext,trans*trans);
float3 celSunSpectral(float3 trans, in CelParams p)
{
    float m = celAirMassOf(p.sunElevationDeg);
    float c0 = 5.8e-6f * p.rayleigh * p.rayleighH
             + 21e-6f * 1.1f * p.mie * p.mieH;
    float c1 = 13.5e-6f * p.rayleigh * p.rayleighH
             + 21e-6f * 1.1f * p.mie * p.mieH;
    float c2 = 33.1e-6f * p.rayleigh * p.rayleighH
             + 21e-6f * 1.1f * p.mie * p.mieH;
    float3 att = float3(c0 * m, c1 * m, c2 * m);
    float3 ext = float3(exp(-att.x), exp(-att.y), exp(-att.z));
    float3 trans2 = float3(trans.x * trans.x, trans.y * trans.y, trans.z * trans.z);
    return p.sunColor * max(ext, trans2);
}

// Panel lines 1237-1242. The first disc assignment is overwritten by the
// soft-edged one, so only the second is evaluated:
//   float soft=mix(1.,2.2,1.-smoothstep(0.,4.,elevDeg));
//   disc=1.-smoothstep(uSunAng*(1.-uSunSoft*.9*soft),uSunAng,ang);
//   float limb=mix(1.,.55,smoothstep(0.,uSunAng,ang));
//   col+=disc*limb*sunCol*uSunIntensity*uSunDiscBoost*mix(.35,1.,...);
float3 celSunDisc(float3 dir, float3 trans, in CelParams p)
{
    float ang = acos(clamp(dot(dir, p.sunDir), -1.0f, 1.0f));
    float edge = lerp(1.0f, 2.2f,
                      1.0f - smoothstep(0.0f, 4.0f, p.sunElevationDeg));
    float disc = 1.0f - smoothstep(p.sunAngularRadius
                             * (1.0f - p.sunSoftness * 0.9f * edge),
                             p.sunAngularRadius, ang);
    float limb = lerp(1.0f, 0.55f, smoothstep(0.0f, p.sunAngularRadius, ang));
    float3 sunCol = celSunSpectral(trans, p);
    return disc * limb * sunCol * p.sunIntensity * p.sunDiscBoost
         * lerp(0.35f, 1.0f, smoothstep(-1.0f, 8.0f, p.sunElevationDeg));
}

// Panel line 1243:
//   float sg=exp(-ang*40.)*.35+exp(-ang*9.)*.03+exp(-ang*2.5)*.004;
//   col+=sg*uBloom*sunCol*uSunIntensity*.6;
float3 celSunAureole(float3 dir, float3 trans, in CelParams p)
{
    float ang = acos(clamp(dot(dir, p.sunDir), -1.0f, 1.0f));
    float3 sunCol = celSunSpectral(trans, p);
    float sg = exp(-ang * 40.0f) * 0.35f + exp(-ang * 9.0f) * 0.03f
             + exp(-ang * 2.5f) * 0.004f;
    return sg * p.bloom * sunCol * p.sunIntensity * 0.6f;
}

// Panel line 1211 (planet ground, plane off):
//   vec3 sunP=trans*uSunColor*uSunIntensity*.09*nd;
float3 celSunGround(float3 dir, float3 trans, in CelParams p)
{
    float3 ro = float3(0.0f, p.planetRadius + p.camHeight, 0.0f);
    float2 t = celRaySphere(ro, dir, p.planetRadius);
    float3 hp = ro + dir * t.x;
    float3 n = normalize(hp);
    float nd = max(dot(n, p.sunDir), 0.0f);
    return trans * p.sunColor * p.sunIntensity * 0.09f * nd;
}

// Panel lines 1141-1143 with sky/atmosphere visibility on:
//   atmosphere(ro,dir,sky,trans,ground); sky*=uSkyTint*uSkyBright;
CelAtmo celSkyField(float3 dir, in CelParams p)
{
    float3 ro = float3(0.0f, p.planetRadius + p.camHeight, 0.0f);
    CelAtmo f = celAtmosphere(ro, dir, p);
    f.sky = f.sky * p.skyTint * p.skyBright;
    return f;
}

// Full pixel radiance before post: sky + glow + disc + aureole over the sky,
// the lit planet disc over the ground (panel lines 1197-1244, plane/fog/
// clouds/stars/moons/flare off).
float3 celSkyFull(float3 dir, in CelParams p)
{
    CelAtmo f = celSkyField(dir, p);
    if (f.ground <= 0.5f)
    {
        float facing = celFacing(dir, p.sunDir);
        float hdrScale = (p.sunIntensity / 22.0f) * p.skyBright;
        float groundObs = 1.0f - smoothstep(1500.0f, 12000.0f, p.camHeight);
        float3 glow = celDawnGlow(dir, p.sunElevationDeg, facing, p)
                    * hdrScale * groundObs;
        return f.sky + glow + celSunDisc(dir, f.trans, p)
             + celSunAureole(dir, f.trans, p);
    }
    float3 sunP = celSunGround(dir, f.trans, p);
    float3 amb = f.sky * 0.35f
               + float3(0.002f, 0.003f, 0.006f) * p.skyBright;
    return p.groundAlbedo * p.groundBright * (sunP + amb) + f.sky;
}

// Panel line 1128, exactly:
//   vec3 aces(vec3 x){ return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.); }
float3 celToneAces(float3 v)
{
    float3 num = v * (2.51f * v + 0.03f);
    float3 den = v * (2.43f * v + 0.59f) + 0.14f;
    return clamp(num / den, 0.0f, 1.0f);
}

// Panel hash13, line 794:
//   float hash13(vec3 p){ p=fract(p*.1031); p+=dot(p,p.zyx+31.32);
//   return fract((p.x+p.y)*p.z); }
// The dot is expanded (dot(q, q.zyx+31.32) = q.x*(q.z+31.32)+...) so the dual
// region needs no swizzle.
float celHash13(float3 p)
{
    float3 q = float3(celFract(p.x * 0.1031f), celFract(p.y * 0.1031f),
                      celFract(p.z * 0.1031f));
    float d = q.x * (q.z + 31.32f) + q.y * (q.y + 31.32f) + q.z * (q.x + 31.32f);
    q = q + d;
    return celFract((q.x + q.y) * q.z);
}

// Panel lines 1257-1261 (tonemap 1 = ACES):
//   float autoEV=-.35*smoothstep(-8.,-1.,elevDeg)-1.0*smoothstep(-1.,6.,elevDeg)
//                -.6*smoothstep(6.,30.,elevDeg);
//   col*=exp2(uEV+autoEV);
//   if(uTonemap==1.) col=aces(col); ...
//   float vig=1.-uVignette*pow(length(uv*vec2(uRes.y/uRes.x,1.)*.9),2.2);
//   col*=clamp(vig,0.,1.);
//   col=pow(max(col,0.),vec3(1./2.2));
//   col+=(hash13(vec3(gl_FragCoord.xy,fract(uTime)*100.))-.5)*uGrain*.12;
// (px, py) are top-down framebuffer rows; the grain seed pattern matches the
// oracle and the C++ port's per-pixel call, which use the same convention.
float3 celApplyPost(float3 hdr, float uu, float vv, float w, float h,
                    uint px, uint py, in CelParams p)
{
    float e = p.sunElevationDeg;
    float autoEV = -0.35f * smoothstep(-8.0f, -1.0f, e)
                 - 1.0f * smoothstep(-1.0f, 6.0f, e)
                 - 0.6f * smoothstep(6.0f, 30.0f, e);
    float3 col = hdr * celExp2(p.exposureStops + autoEV);
    if (p.tonemap == 1u)
    {
        col = celToneAces(col);
    }
    else
    {
        col = clamp(col, 0.0f, 1.0f);
    }
    float2 vk = float2(uu * (h / w), vv) * 0.9f;
    float r = sqrt(vk.x * vk.x + vk.y * vk.y);
    float vig = 1.0f - p.vignette * celPow(r, 2.2f);
    col = col * clamp(vig, 0.0f, 1.0f);
    col = float3(celPow(max(col.x, 0.0f), 1.0f / 2.2f),
                 celPow(max(col.y, 0.0f), 1.0f / 2.2f),
                 celPow(max(col.z, 0.0f), 1.0f / 2.2f));
    float seed = celFract(p.timeSeconds) * 100.0f;
    float gr = celHash13(float3(float(px) + 0.5f, float(py) + 0.5f, seed));
    return col + (gr - 0.5f) * p.grain * 0.12f;
}

// Panel line 1147: dot(sky, vec3(.2126,.7152,.0722)).
float celLuminance(float3 v)
{
    return v.x * 0.2126f + v.y * 0.7152f + v.z * 0.0722f;
}

// D65 relative luminance for ReSTIR target functions (display physics, not
// the panel; kept next to celLuminance so the two never get confused).
float celLuminanceDW(float3 v)
{
    return v.x * 0.212671f + v.y * 0.715160f + v.z * 0.072169f;
}

// Panel line 1139: vec2 uv=(gl_FragCoord.xy*2.-uRes)/uRes.y — with fragY
// mapped from the top-down row. Returns (uu, vv).
float2 celViewportUV(uint x, uint y, uint w, uint h)
{
    float uu = ((float(x) + 0.5f) * 2.0f - float(w)) / float(h);
    float fragY = float(h) - (float(y) + 0.5f);
    float vv = (fragY * 2.0f - float(h)) / float(h);
    float2 uv;
    uv.x = uu;
    uv.y = vv;
    return uv;
}

// Panel line 1140: dir=normalize(uCamFwd+uCamRight*uv.x*uTanHalf
//                               +uCamUp*uv.y*uTanHalf), evaluated left to right.
float3 celViewportDir(in CelCamera cam, float2 uv)
{
    return normalize(cam.fwd + cam.right * uv.x * cam.tanHalf
                     + cam.up * uv.y * cam.tanHalf);
}

#endif // PROJECT_ZERO_CELESTIAL_CORE_SLANG
