//@@HDR: 📦 Frontier/Shaders/SkySpecification.slang — Celestial Sun and Sky Mathematics (dual Slang/C++17)
#ifndef SKY_SPECIFICATION_INCLUDED
#define SKY_SPECIFICATION_INCLUDED

// SkySpecification.slang — the celestial panel's sky + sun + analytic media +
// stars, line by line (moons/clouds/volumes/plane/rainbow/flare/lights are
// separate panel entities and live outside this core).
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
// (buffers, ray queries, entry points) lives in ReSTIRSequence.slang.
#ifndef PROJECT_ZERO_CELESTIAL_CORE_SLANG
#define PROJECT_ZERO_CELESTIAL_CORE_SLANG

// Panel line 770: #define PI 3.14159265
static const float SkyPi = 3.14159265f;
static const float SkyDeg2Rad = 3.14159265f / 180.0f;

// Parameters (filled by the host; the panel's uniforms + visibility = on).
//@@BAN: SKY CONFIGURATION
struct SkyConfiguration
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
    uint fogOn;             // [-] uFogOn (0, hardcoded off by the panel)
    float fogDensity;       // [1/m] uFogDensity (0.011)
    float fogHeight;        // [m] uFogHeight (42)
    float fogScatter;       // [-] uFogScatter (0.7)
    float3 fogColor;        // [-] uFogColor (#8fa4bb)
    uint afOn;              // [-] uAFOn (1)
    float afDensity;        // [1/m] uAFDensity (7e-5)
    float afHeight;         // [m] uAFHeight (1200)
    float afStart;          // [m] uAFStart (0)
    float afMie;            // [-] uAFMie (0.35)
    float afG;              // [-] uAFG (0.7)
    float afSky;            // [x] uAFSky (1)
    float3 afTint;          // [-] uAFTint (#ffffff)
    float3 skyAmb;          // [-] uSkyAmb probe (linear HDR, per-frame)
    uint starsOn;           // [-] uStarsOn (1)
    float starDensity;      // [x] uStarDensity (1)
    float starBright;       // [x] uStarBright (1)
    float starSize;         // [x] uStarSize (1)
    float starGlow;         // [x] uStarGlow (0.8)
    float starTwinkle;      // [-] uStarTwinkle (0.5)
    float starColor;        // [x] uStarColor (1)
    float starLimit;        // [x] uStarLimit (1.5)
    float starMilky;        // [x] uMilky (1)
    float starRot;          // [rad] uStarRot (40 deg)
    float starTilt;         // [rad] uMilkyTilt (-62 deg)
    float starLayers;       // [-] load-time tier (3)
    float starAA;           // [-] load-time tier (1; Ultra+ uses 2)
    float starCeil;         // [-] uStarCeil, from the JS probe line
    float pixAngle;         // [rad] 2*tanHalf/resY, the star footprint
    // Showcase extensions (moon/clouds): defaults keep the mirror bit-exact.
    uint moonOn;            // [-] lunar disc + glow (0)
    float3 moonDir;         // [-] unit, sky frame (+Y up)
    float moonBright;       // [x] lunar radiance scale (2)
    uint cloudOn;           // [-] procedural cirrus layer (0)
    float cloudCoverage;    // [0..1] cirrus cover fraction (0.45)
    float cloudScale;       // [-] cirrus plane scale (2)
};

//@@BAN: SKY PROJECTION
struct SkyProjection
{
    float3 fwd;     // [-] unit, sky frame
    float3 right;   // [-] unit, sky frame
    float3 up;      // [-] unit, sky frame
    float tanHalf;  // [-] tan(fov / 2)
};

//@@BAN: ATMOSPHERE STRUCTURES
struct AtmosphereStructure
{
    float3 sky;     // panel `sky` after tint * brightness
    float3 trans;   // panel `trans`
    float ground;   // panel `ground` flag
    float odR;      // integrated Rayleigh depth (ReSTIR reuse + tests)
    float odM;      // integrated Mie depth
};

struct AtmosphereSpanStructure
{
    float t0;
    float t1;
    float ground;
};

AtmosphereSpanStructure AtmosphereSpanMake(float t0, float t1, float ground)
{
    AtmosphereSpanStructure s;
    s.t0 = t0;
    s.t1 = t1;
    s.ground = ground;
    return s;
}

//@@BAN: STRUCTURE FACTORIES
AtmosphereStructure AtmosphereStructureMake(float3 sky, float3 trans, float ground, float odR, float odM)
{
    AtmosphereStructure o;
    o.sky = sky;
    o.trans = trans;
    o.ground = ground;
    o.odR = odR;
    o.odM = odM;
    return o;
}

// Scalar helpers. C++ bodies live in the prelude; Slang maps to builtins.
#ifdef SLANG_COMPAT_CXX
// (MathFract / MathExp2 / MathPow are declared in SlangCompat.h for the C++ path)
#else
float MathFract(float x) { return frac(x); }
float MathExp2(float x) { return exp2(x); }
float MathPow(float x, float y) { return pow(x, y); }
#endif

// Panel line 800, exactly:
//   vec2 rsi(vec3 ro,vec3 rd,float r){ float b=dot(ro,rd); float c=dot(ro,ro)-r*r;
//   float d=b*b-c; if(d<0.) return vec2(-1.,-1.); d=sqrt(d); return vec2(-b-d,-b+d); }
float2 RaySphereIntersect(float3 ro, float3 dir, float r)
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
AtmosphereSpanStructure AtmosphereSpanCompute(float3 ro, float3 dir, in SkyConfiguration p)
{
    float Ra = p.planetRadius + p.atmoHeight;
    float2 ta = RaySphereIntersect(ro, dir, Ra);
    if (ta.y < 0.0f)
    {
        return AtmosphereSpanMake(0.0f, -1.0f, 0.0f);
    }
    float t0 = max(ta.x, 0.0f);
    float t1 = ta.y;
    float ground = 0.0f;
    float2 tg = RaySphereIntersect(ro, dir, p.planetRadius);
    if (tg.x > 0.0f)
    {
        t1 = tg.x;
        ground = 1.0f;
    }
    return AtmosphereSpanMake(t0, t1, ground);
}

// Panel lines 861/863 shared extinction shape:
//   exp(-(betaR*odR+betaM*1.1*odM+betaO*odR))
//@@BAN: ATMOSPHERE INTEGRATION
float3 ExtinctionAttenuation(float3 betaR, float3 betaM, float3 betaO, float odR, float odM)
{
    float ex = betaR.x * odR + betaM.x * 1.1f * odM + betaO.x * odR;
    float ey = betaR.y * odR + betaM.y * 1.1f * odM + betaO.y * odR;
    float ez = betaR.z * odR + betaM.z * 1.1f * odM + betaO.z * odR;
    return float3(exp(-ex), exp(-ey), exp(-ez));
}

// Panel lines 847-863 (N=20 primary segs, NL=8 light segs, quadratic spacing).
AtmosphereStructure AtmosphereCompute(float3 ro, float3 dir, in SkyConfiguration p)
{
    AtmosphereSpanStructure span = AtmosphereSpanCompute(ro, dir, p);
    if (span.t1 < 0.0f)
    {
        return AtmosphereStructureMake(float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f),
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
    float pR = 3.0f / (16.0f * SkyPi) * (1.0f + mu * mu);
    float pM = 3.0f / (8.0f * SkyPi) * ((1.0f - g * g) * (1.0f + mu * mu))
             / ((2.0f + g * g) * MathPow(1.0f + g * g - 2.0f * g * mu, 1.5f));
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
        float2 tl = RaySphereIntersect(pp, p.sunDir, p.planetRadius + p.atmoHeight);
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
            float3 att = ExtinctionAttenuation(betaR, betaM, betaO, odR + olR, odM + olM);
            sumR += att * hr;
            sumM += att * hm;
        }
    }
    float3 trans = ExtinctionAttenuation(betaR, betaM, betaO, odR, odM);
    float3 sky = (sumR * betaR * pR + sumM * betaM * pM) * p.sunIntensity * p.sunColor;
    return AtmosphereStructureMake(sky, trans, ground, odR, odM);
}

// Panel line 1148: normalize(dir.xz + 1e-5) — the epsilon pads the components.
float2 HorizonDirectionCompute(float3 v)
{
    float2 h = float2(v.x + 1e-5f, v.z + 1e-5f);
    float l = sqrt(h.x * h.x + h.y * h.y);
    return float2(h.x / l, h.y / l);
}

float SunFacingCompute(float3 dir, float3 sunDir)
{
    float2 h = HorizonDirectionCompute(dir);
    float2 s = HorizonDirectionCompute(sunDir);
    return 0.5f + 0.5f * (h.x * s.x + h.y * s.y);
}

// Panel lines 910-937. Multiplication order follows the panel term by term.
//@@BAN: DAWN GLOW AND WHITE LINE
float3 DawnGlowCompute(float3 dir, float elevDeg, float facing, in SkyConfiguration p)
{
    float alt = asin(clamp(dir.y, -1.0f, 1.0f)) / SkyDeg2Rad;
    if (alt < -2.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float altp = max(alt, 0.0f);
    float dAz = acos(clamp(facing * 2.0f - 1.0f, -1.0f, 1.0f));
    float az = exp(-MathPow(dAz / 0.95f, 2.0f));
    float azW = exp(-MathPow(dAz / 1.8f, 2.0f));
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
    float lineAz = exp(-MathPow(dAz / 0.55f, 2.0f));
    float line = exp(-MathPow(alt / 0.11f, 2.0f))
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
float AirMassCompute(float e)
{
    float z = 90.0f - e;
    if (z >= 96.0f)
    {
        return 40.0f;
    }
    return min(40.0f, 1.0f / (cos(z * SkyDeg2Rad)
           + 0.50572f * MathPow(96.07995f - z, -1.6364f)));
}

// Panel lines 1240-1241:
//   vec3 ext=exp(-(vec3(5.8e-6,13.5e-6,33.1e-6)*uRayleigh*uHr
//                +vec3(21e-6)*1.1*uMie*uHm)*airMassOf(elevDeg));
//   vec3 sunCol=uSunColor*max(ext,trans*trans);
float3 SunSpectralCompute(float3 trans, in SkyConfiguration p)
{
    float m = AirMassCompute(p.sunElevationDeg);
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
float3 SunDiscCompute(float3 dir, float3 trans, in SkyConfiguration p)
{
    float ang = acos(clamp(dot(dir, p.sunDir), -1.0f, 1.0f));
    float edge = lerp(1.0f, 2.2f,
                      1.0f - smoothstep(0.0f, 4.0f, p.sunElevationDeg));
    float disc = 1.0f - smoothstep(p.sunAngularRadius
                             * (1.0f - p.sunSoftness * 0.9f * edge),
                             p.sunAngularRadius, ang);
    float limb = lerp(1.0f, 0.55f, smoothstep(0.0f, p.sunAngularRadius, ang));
    float3 sunCol = SunSpectralCompute(trans, p);
    return disc * limb * sunCol * p.sunIntensity * p.sunDiscBoost
         * lerp(0.35f, 1.0f, smoothstep(-1.0f, 8.0f, p.sunElevationDeg));
}

// Panel line 1243:
//   float sg=exp(-ang*40.)*.35+exp(-ang*9.)*.03+exp(-ang*2.5)*.004;
//   col+=sg*uBloom*sunCol*uSunIntensity*.6;
float3 SunAureoleCompute(float3 dir, float3 trans, in SkyConfiguration p)
{
    float ang = acos(clamp(dot(dir, p.sunDir), -1.0f, 1.0f));
    float3 sunCol = SunSpectralCompute(trans, p);
    float sg = exp(-ang * 40.0f) * 0.35f + exp(-ang * 9.0f) * 0.03f
             + exp(-ang * 2.5f) * 0.004f;
    return sg * p.bloom * sunCol * p.sunIntensity * 0.6f;
}

// Panel line 1211 (planet ground, plane off):
//   vec3 sunP=trans*uSunColor*uSunIntensity*.09*nd;
float3 SunGroundCompute(float3 dir, float3 trans, in SkyConfiguration p)
{
    float3 ro = float3(0.0f, p.planetRadius + p.camHeight, 0.0f);
    float2 t = RaySphereIntersect(ro, dir, p.planetRadius);
    float3 hp = ro + dir * t.x;
    float3 n = normalize(hp);
    float nd = max(dot(n, p.sunDir), 0.0f);
    return trans * p.sunColor * p.sunIntensity * 0.09f * nd;
}

// Panel lines 1141-1143 with sky/atmosphere visibility on:
//   atmosphere(ro,dir,sky,trans,ground); sky*=uSkyTint*uSkyBright;
AtmosphereStructure SkyFieldCompute(float3 dir, in SkyConfiguration p)
{
    float3 ro = float3(0.0f, p.planetRadius + p.camHeight, 0.0f);
    AtmosphereStructure f = AtmosphereCompute(ro, dir, p);
    f.sky = f.sky * p.skyTint * p.skyBright;
    return f;
}

// Panel hash13, line 794:
//   float hash13(vec3 p){ p=fract(p*.1031); p+=dot(p,p.zyx+31.32);
//   return fract((p.x+p.y)*p.z); }
// The dot is expanded (dot(q, q.zyx+31.32) = q.x*(q.z+31.32)+...) so the dual
// region needs no swizzle.
float HashCompute13(float3 p)
{
    float3 q = float3(MathFract(p.x * 0.1031f), MathFract(p.y * 0.1031f),
                      MathFract(p.z * 0.1031f));
    float d = q.x * (q.z + 31.32f) + q.y * (q.y + 31.32f) + q.z * (q.x + 31.32f);
    q = q + d;
    return MathFract((q.x + q.y) * q.z);
}

// Panel line 793, swizzles expanded (the dual region has no .yxz):
//   vec3 hash33(vec3 p){ p=fract(p*vec3(.1031,.1030,.0973));
//   p+=dot(p,p.yxz+33.33); return fract((p.xxy+p.yxx)*p.zyx); }
// dot(p,p.yxz+33.33) = px*(py+33.33)+py*(px+33.33)+pz*(pz+33.33) and
// (p.xxy+p.yxx)*p.zyx = ((px+py)*pz, 2*px*py, (px+py)*px).
float3 HashCompute33(float3 p)
{
    float3 q = float3(MathFract(p.x * 0.1031f), MathFract(p.y * 0.1030f),
                      MathFract(p.z * 0.0973f));
    float d = q.x * (q.y + 33.33f) + q.y * (q.x + 33.33f)
            + q.z * (q.z + 33.33f);
    q = q + d;
    float s = q.x + q.y;
    return float3(MathFract(s * q.z), MathFract(2.0f * q.x * q.y),
                  MathFract(s * q.x));
}

// Panel lines 795-797: trilinear value noise over hash13, corner order kept.
float NoiseValueCompute(float3 p)
{
    float3 ii = float3(floor(p.x), floor(p.y), floor(p.z));
    float3 f = float3(MathFract(p.x), MathFract(p.y), MathFract(p.z));
    f = f * f * (3.0f - 2.0f * f);
    float n000 = HashCompute13(ii);
    float n100 = HashCompute13(ii + float3(1.0f, 0.0f, 0.0f));
    float n010 = HashCompute13(ii + float3(0.0f, 1.0f, 0.0f));
    float n110 = HashCompute13(ii + float3(1.0f, 1.0f, 0.0f));
    float n001 = HashCompute13(ii + float3(0.0f, 0.0f, 1.0f));
    float n101 = HashCompute13(ii + float3(1.0f, 0.0f, 1.0f));
    float n011 = HashCompute13(ii + float3(0.0f, 1.0f, 1.0f));
    float n111 = HashCompute13(ii + float3(1.0f, 1.0f, 1.0f));
    return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
                lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}

// Showcase moon (no panel counterpart; same disc-plus-glow structure as the
// sun): a pale full disc at the moon radius plus a tight halo, attenuated
// by the view-path transmittance like any sky emitter.
float3 MoonDiscCompute(float3 dir, float3 trans, in SkyConfiguration p)
{
    float ang = acos(clamp(dot(dir, p.moonDir), -1.0f, 1.0f));
    float radius = p.sunAngularRadius * 0.98f;
    float disc = 1.0f - smoothstep(radius * 0.90f, radius, ang);
    float limb = lerp(1.0f, 0.72f, smoothstep(0.0f, radius, ang));
    float halo = exp(-ang * 22.0f) * 0.30f + exp(-ang * 5.0f) * 0.04f;
    float3 moonCol = float3(0.90f, 0.94f, 1.0f) * trans;
    return (disc * limb + halo) * moonCol * p.moonBright;
}

// Showcase cirrus (no panel counterpart): the view direction projected onto
// a wind-drifted plane, 3-octave value-noise fbm, coverage gate, horizon
// fade. Returns the layer density in [0..1].
float CloudFieldCompute(float3 dir, in SkyConfiguration p)
{
    if (dir.y <= 0.01f)
    {
        return 0.0f;
    }
    float2 cuv = float2(dir.x, dir.z) / dir.y * p.cloudScale
               + float2(p.timeSeconds * 0.004f, p.timeSeconds * 0.0016f);
    float3 cell = float3(cuv.x, cuv.y, 7.31f);
    float fbm = NoiseValueCompute(cell) * 0.55f
              + NoiseValueCompute(cell * 2.13f + 7.7f) * 0.27f
              + NoiseValueCompute(cell * 4.41f + 3.1f) * 0.18f;
    float gate = smoothstep(1.0f - p.cloudCoverage - 0.25f,
                            1.0f - p.cloudCoverage + 0.35f, fbm);
    return gate * smoothstep(0.01f, 0.12f, dir.y);
}

// Cirrus shading: atmosphere-reddened sunlight (silver lining toward the
// sun) over sky-ambient shade.
float3 CloudShadeCompute(float3 dir, float density, float3 trans,
                         in SkyConfiguration p)
{
    float toward = max(dot(dir, p.sunDir), 0.0f);
    float glow = pow(toward * 0.5f + 0.5f, 3.0f);
    float3 lit = trans * p.sunColor * (0.6f + 2.5f * glow)
               + float3(0.10f, 0.12f, 0.16f);
    float mOn = p.moonOn > 0u ? 1.0f : 0.0f;
    float mGlow = pow(max(dot(dir, p.moonDir), 0.0f) * 0.5f + 0.5f, 2.0f);
    lit = lit + float3(0.55f, 0.65f, 0.85f) * (mGlow * p.moonBright * 0.12f)
                * mOn;
    float3 shade = p.skyAmb * 0.55f + float3(0.04f, 0.05f, 0.08f);
    return lerp(shade, lit, clamp(glow + pow(toward, 8.0f), 0.0f, 1.0f))
         * (0.35f + 0.65f * density);
}


// Panel lines 805-806.
float3 RotateYCompute(float3 v, float a)
{
    float c = cos(a);
    float s = sin(a);
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

float3 RotateXCompute(float3 v, float a)
{
    float c = cos(a);
    float s = sin(a);
    return float3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
}

// Panel lines 808-809 (ternaries stand in for the sign steps).
float2 OctahedralEncode(float3 n)
{
    float l1 = abs(n.x) + abs(n.y) + abs(n.z);
    n = n / l1;
    float2 p = float2(n.x, n.z);
    if (n.y < 0.0f)
    {
        float ax = 1.0f - abs(p.y);
        float ay = 1.0f - abs(p.x);
        float sx = p.x >= 0.0f ? 1.0f : -1.0f;
        float sy = p.y >= 0.0f ? 1.0f : -1.0f;
        p = float2(ax * sx, ay * sy);
    }
    return p * 0.5f + 0.5f;
}

float3 OctahedralDecode(float2 f)
{
    f = f * 2.0f - 1.0f;
    float3 n = float3(f.x, 1.0f - abs(f.x) - abs(f.y), f.y);
    float t = max(-n.y, 0.0f);
    n.x += n.x >= 0.0f ? -t : t;
    n.z += n.z >= 0.0f ? -t : t;
    return normalize(n);
}

// Panel lines 802-804: the shader-side blackbody ramp (star colours).
float3 StarKelvinCompute(float t)
{
    float x = clamp((t - 2000.0f) / 10000.0f, 0.0f, 1.0f);
    float3 c = lerp(float3(1.0f, 0.55f, 0.25f), float3(1.0f, 0.93f, 0.86f),
                    smoothstep(0.0f, 0.4f, x));
    c = lerp(c, float3(0.72f, 0.82f, 1.0f), smoothstep(0.4f, 1.0f, x));
    return c;
}

// Panel line 945: the exact exp(-y/H) segment integral both fogs share.
float ExpHeightKernel(float hh, float y0, float y1)
{
    float ya = max(0.0f, min(y0, y1));
    float yb = max(0.0f, max(y0, y1));
    if ((yb - ya) < 1e-3f)
    {
        return exp(-ya / hh);
    }
    return hh * (exp(-ya / hh) - exp(-yb / hh)) / (yb - ya);
}

// Panel line 946.
float FogTransmittance(float d, float y0, float y1, in SkyConfiguration p)
{
    float od = p.fogDensity * d * ExpHeightKernel(max(1.0f, p.fogHeight), y0, y1);
    return exp(-od * od);
}

// Panel lines 947-962: both analytic fogs in one shot. NOTE the panel uses
// the literal 3.14159 (not PI) in the HG lobes and the AF sun term.
//@@BAN: SKY MEDIA (FOG AND ATMOSPHERE-FOG)
float3 SkyMediaApply(float3 col, float3 dir, float d, float3 hzGlow,
                     float3 trans, in SkyConfiguration p)
{
    bool hf = p.fogOn > 0u && p.camHeight < 3000.0f;
    bool af = p.afOn > 0u && p.camHeight < 6000.0f;
    if (!hf && !af)
    {
        return col;
    }
    float y0 = p.camHeight;
    float y1 = p.camHeight + dir.y * d;
    float cosS = clamp(dot(dir, p.sunDir), -1.0f, 1.0f);
    float3 sunL = trans * p.sunColor * p.sunIntensity * 0.02f;
    float Tf = 1.0f;
    float3 Lf = float3(0.0f, 0.0f, 0.0f);
    if (hf)
    {
        Tf = FogTransmittance(d, y0, y1, p);
        float g = 0.55f;
        float hg = (1.0f - g * g)
                 / (4.0f * 3.14159f
                    * MathPow(1.0f + g * g - 2.0f * g * cosS, 1.5f));
        Lf = p.fogColor * (p.skyAmb * 0.9f + hzGlow * 0.35f)
           + p.fogScatter * hg * sunL * max(p.sunDir.y + 0.1f, 0.0f);
    }
    float3 Ta = float3(1.0f, 1.0f, 1.0f);
    float3 La = float3(0.0f, 0.0f, 0.0f);
    if (af)
    {
        float od = p.afDensity * max(0.0f, d - p.afStart)
                 * ExpHeightKernel(max(1.0f, p.afHeight), y0, y1);
        if (od > 1e-6f)
        {
            float3 beta = lerp(float3(5.8e-6f, 13.5e-6f, 33.1e-6f) / 13.5e-6f,
                               float3(1.0f, 1.0f, 1.0f), p.afMie);
            Ta = float3(exp(-beta.x * od), exp(-beta.y * od), exp(-beta.z * od));
            float g = p.afG;
            float hg = (1.0f - g * g)
                     / (4.0f * 3.14159f
                        * MathPow(1.0f + g * g - 2.0f * g * cosS, 1.5f));
            La = ((p.skyAmb * 0.9f + hzGlow * 0.25f) * p.afSky
                  + sunL * hg * 4.0f * 3.14159f
                  * max(p.sunDir.y + 0.08f, 0.0f)
                  * lerp(1.0f, 0.35f, p.afMie)) * p.afTint;
        }
    }
    float3 T = Ta * Tf;
    float3 Lin = La * (1.0f - Ta) * lerp(1.0f, Tf, 0.5f)
               + Lf * (1.0f - Tf) * lerp(float3(1.0f, 1.0f, 1.0f), Ta, 0.5f);
    return col * T + Lin;
}

// Panel lines 1133-1138 + the JS probe combine (amb = px0*.4+px1*.35+px2*.25):
// zenith, sun-side horizon and anti-sun horizon through the same atmosphere.
//@@BAN: SKY AMBIENT PROBE
float3 SkyAmbientCompute(in SkyConfiguration p)
{
    // Panel ro = (0, uPlanetR + uCamHeight, 0), built inside SkyFieldCompute.
    float3 hd = normalize(float3(p.sunDir.x + 1e-4f, 0.0f, p.sunDir.z));
    AtmosphereStructure a0 = SkyFieldCompute(float3(0.0f, 1.0f, 0.0f), p);
    AtmosphereStructure a1 = SkyFieldCompute(normalize(hd + float3(0.0f, 0.08f, 0.0f)), p);
    AtmosphereStructure a2 = SkyFieldCompute(normalize(float3(0.0f, 0.0f, 0.0f) - hd
                                       + float3(0.0f, 0.08f, 0.0f)), p);
    return a0.sky * 0.4f + a1.sky * 0.35f + a2.sky * 0.25f;
}

// Panel lines 811-846: Milky Way band + 4 octahedral point-star layers.
//@@BAN: STARS AND MILKY WAY
float3 StarFieldCompute(float3 d, float pixAng, float am, in SkyConfiguration p)
{
    float3 sd = RotateXCompute(RotateYCompute(d, p.starRot), p.starTilt);
    float3 col = float3(0.0f, 0.0f, 0.0f);
    float band = exp(-MathPow(sd.y / 0.15f, 2.0f));
    float n = NoiseValueCompute(sd * 6.0f) * 0.5f
            + NoiseValueCompute(sd * 13.0f + 3.1f) * 0.3f
            + NoiseValueCompute(sd * 29.0f + 7.3f) * 0.2f;
    float dust = smoothstep(0.35f, 0.7f,
                            NoiseValueCompute(sd * 9.0f + float3(5.2f, 1.1f, 8.8f)))
               * exp(-MathPow(sd.y / 0.06f, 2.0f)) * 0.8f;
    float mw = band * (0.35f + 0.65f * n) * (1.0f - dust) * p.starMilky;
    col = col + lerp(float3(0.55f, 0.62f, 0.9f), float3(0.95f, 0.85f, 0.7f),
                     dust * 0.6f) * mw * 3.2e-4f;
    float2 ouv = OctahedralEncode(sd);
    for (int o = 0; o < 4; o++)
    {
        if (float(o) >= p.starLayers)
        {
            break;
        }
        float freq = o == 0 ? 18.0f : (o == 1 ? 46.0f : (o == 2 ? 110.0f : 230.0f));
        float density = o == 0 ? 0.28f : (o == 1 ? 0.42f : (o == 2 ? 0.6f : 0.7f));
        float layerGain = o == 0 ? 1.0f : (o == 1 ? 0.4f : (o == 2 ? 0.15f : 0.06f));
        float2 gp = ouv * freq;
        float2 base = float2(floor(gp.x), floor(gp.y));
        for (int j = -1; j <= 1; j++)
        {
            for (int i = -1; i <= 1; i++)
            {
                float2 cell = base + float2(float(i), float(j));
                float3 h = HashCompute33(float3(cell.x, cell.y, float(o) * 17.0f));
                if (h.x < 1.0f - density * p.starDensity)
                {
                    continue;
                }
                float2 suv = (cell + 0.5f + (float2(h.y, h.z) - 0.5f) * 0.9f)
                           / freq;
                float3 sdir = OctahedralDecode(float2(clamp(suv.x, 0.0f, 1.0f),
                                                  clamp(suv.y, 0.0f, 1.0f)));
                float ang = acos(clamp(dot(sd, sdir), -1.0f, 1.0f));
                float bright = (MathPow(HashCompute13(float3(cell.x, cell.y,
                                                        3.3f + float(o))),
                                       8.0f) * 8.0f + 0.15f) * layerGain;
                bright = bright * lerp(1.0f, band * 2.2f + 0.35f,
                                       p.starMilky * 0.6f);
                float ph = HashCompute13(float3(cell.x, cell.y, 5.7f)) * 6.283f;
                float tw = 1.0f - p.starTwinkle
                         * (0.3f + 0.5f * clamp(am / 6.0f, 0.0f, 1.0f))
                         * (0.5f + 0.5f * sin(p.timeSeconds
                           * (3.0f + HashCompute13(float3(cell.x, cell.y, 2.2f))
                             * 8.0f) + ph));
                float T = lerp(2800.0f, 11000.0f,
                               MathPow(HashCompute13(float3(cell.x, cell.y, 9.1f)),
                                      1.6f));
                float3 sc = lerp(float3(1.0f, 1.0f, 1.0f), StarKelvinCompute(T),
                                 p.starColor);
                float ps = max(p.starSize * (0.0006f + bright * 0.00028f),
                               pixAng * 0.9f);
                float core = (1.0f - smoothstep(ps * 0.55f, ps, ang))
                           * (1.0f + 0.6f * (bright >= 2.5f ? 1.0f : 0.0f));
                core = core * 3.2f;
                float halo = exp(-MathPow(ang / (ps * (2.0f + 6.0f * p.starGlow)),
                                         1.5f)) * 0.045f * p.starGlow
                           * (bright / 8.0f + 0.03f);
                float spikes = 0.0f;
                if (o == 0 && bright > 2.5f)
                {
                    float3 t1 = normalize(cross(sdir,
                                                float3(0.001f, 1.0f, 0.0f)));
                    float3 t2 = cross(sdir, t1);
                    float3 dd = sd - sdir;
                    float2 q = float2(dot(dd, t1), dot(dd, t2));
                    float w = max(pixAng * 0.6f, ps * 0.35f);
                    spikes = (exp(-abs(q.x) / w * 0.5f) * exp(-abs(q.y) * 70.0f)
                            + exp(-abs(q.y) / w * 0.5f) * exp(-abs(q.x) * 70.0f))
                           * 0.3f * p.starGlow * (bright / 8.0f);
                }
                col = col + sc * (core + halo + spikes) * bright * tw * 2.6e-3f;
            }
        }
    }
    return col * p.starBright;
}

// Panel lines 1221-1225: the 2x2 rotated-grid star supersample.
float3 StarPixelCompute(float3 dir, float3 trans, in SkyProjection cam, in SkyConfiguration p)
{
    float am = AirMassCompute(asin(clamp(dir.y, -1.0f, 1.0f)) / SkyDeg2Rad);
    float3 s0 = float3(0.0f, 0.0f, 0.0f);
    for (int q = 0; q < 4; q++)
    {
        float2 o = q == 0 ? float2(-0.375f, -0.125f)
                 : (q == 1 ? float2(0.125f, -0.375f)
                 : (q == 2 ? float2(0.375f, 0.125f)
                           : float2(-0.125f, 0.375f)));
        float3 d2 = normalize(dir + (cam.right * o.x + cam.up * o.y)
                                    * p.pixAngle);
        s0 = s0 + StarFieldCompute(d2, p.pixAngle * 0.7f, am, p);
    }
    return s0 * 0.25f * trans;
}

// Panel line 1147: dot(sky, vec3(.2126,.7152,.0722)).
float LuminanceCompute(float3 v)
{
    return v.x * 0.2126f + v.y * 0.7152f + v.z * 0.0722f;
}

// D65 relative luminance for ReSTIR target functions (display physics, not
// the panel; kept next to LuminanceCompute so the two never get confused).
float LuminanceDWCompute(float3 v)
{
    return v.x * 0.212671f + v.y * 0.715160f + v.z * 0.072169f;
}

// Full sky radiance before post: sky + glow + single-sample stars + disc +
// aureole over the sky, then the shared analytic-media path length; the lit
// planet disc over the ground (panel lines 1197-1245, plane/clouds/moons/
// flare off). ReSTIR lighting calls this — the star AA branch is a primary-
// ray reconstruction filter and lives in SkyPixelCompute instead.
float3 SkyShadedCompute(in AtmosphereStructure f, float3 dir, in SkyConfiguration p)
{
    if (f.ground <= 0.5f)
    {
        float facing = SunFacingCompute(dir, p.sunDir);
        float hdrScale = (p.sunIntensity / 22.0f) * p.skyBright;
        float groundObs = 1.0f - smoothstep(1500.0f, 12000.0f, p.camHeight);
        float3 glow = DawnGlowCompute(dir, p.sunElevationDeg, facing, p)
                    * hdrScale * groundObs;
        float3 hdir = normalize(float3(dir.x + 1e-5f, 1e-5f, dir.z + 1e-5f));
        float3 hzGlow = DawnGlowCompute(hdir, p.sunElevationDeg, facing, p)
                      * hdrScale * groundObs;
        float3 col = f.sky + glow;
        float skyLum = LuminanceCompute(f.sky);
        float hzDip = -acos(clamp(p.planetRadius
                                  / (p.planetRadius + p.camHeight),
                                  0.0f, 1.0f));
        float ext = smoothstep(hzDip - 0.01f,
                               hzDip + 0.12f * groundObs + 0.002f, dir.y);
        if (p.starsOn > 0u
            && p.starCeil > max(skyLum, 1e-7f) * p.starLimit * 0.6f)
        {
            float am = AirMassCompute(asin(clamp(dir.y, -1.0f, 1.0f)) / SkyDeg2Rad);
            float3 st = StarFieldCompute(dir, p.pixAngle, am, p) * f.trans;
            float skyL = max(skyLum, 1e-7f);
            float thresh = skyL * p.starLimit;
            float l = LuminanceCompute(st);
            float vis = smoothstep(thresh * 0.6f, thresh * 2.5f, l + 1e-9f);
            col = col + st * vis * ext;
        }
        col = col + SunDiscCompute(dir, f.trans, p) + SunAureoleCompute(dir, f.trans, p);
        if (p.moonOn > 0u)
        {
            col = col + MoonDiscCompute(dir, f.trans, p);
        }
        float hRef = max(p.fogHeight * 4.0f, p.afHeight * 5.0f);
        float dS = dir.y > 0.002f ? min(60000.0f, hRef / dir.y) : 60000.0f;
        col = SkyMediaApply(col, dir, dS, hzGlow, f.trans, p);
        if (p.cloudOn > 0u)
        {
            float density = CloudFieldCompute(dir, p);
            if (density > 0.0f)
            {
                col = lerp(col,
                           CloudShadeCompute(dir, density, f.trans, p),
                           clamp(density * 1.15f, 0.0f, 0.92f));
            }
        }
        return col;
    }
    float3 sunP = SunGroundCompute(dir, f.trans, p);
    float3 amb = f.sky * 0.35f
               + float3(0.002f, 0.003f, 0.006f) * p.skyBright;
    return p.groundAlbedo * p.groundBright * (sunP + amb) + f.sky;
}

//@@BAN: SKY RADIANCE ASSEMBLY
float3 SkyRadianceCompute(float3 dir, in SkyConfiguration p)
{
    AtmosphereStructure f = SkyFieldCompute(dir, p);
    return SkyShadedCompute(f, dir, p);
}

// Full primary-ray pixel: SkyRadianceCompute plus the panel's star-AA branch
// (lines 1219-1225). The single-sample star term is removed and the
// supersampled one added back; the add-then-subtract is exact to the ulp,
// far below the post chain's half-LSB budget.
float3 SkyPixelCompute(float3 dir, in SkyProjection cam, in SkyConfiguration p)
{
    AtmosphereStructure f = SkyFieldCompute(dir, p);
    float3 col = SkyShadedCompute(f, dir, p);
    if (f.ground > 0.5f)
    {
        return col;
    }
    float skyLum = LuminanceCompute(f.sky);
    if (p.starsOn > 0u && p.starCeil > max(skyLum, 1e-7f) * p.starLimit * 0.6f)
    {
        float aaNeed = p.starCeil >= max(skyLum, 1e-7f) * p.starLimit * 8.0f
                     ? 1.0f : 0.0f;
        if (p.starAA > 1.5f && aaNeed > 0.5f)
        {
            float am = AirMassCompute(asin(clamp(dir.y, -1.0f, 1.0f)) / SkyDeg2Rad);
            float3 stS = StarFieldCompute(dir, p.pixAngle, am, p) * f.trans;
            float3 stA = StarPixelCompute(dir, f.trans, cam, p);
            float skyL = max(skyLum, 1e-7f);
            float thresh = skyL * p.starLimit;
            float hzDip = -acos(clamp(p.planetRadius
                                      / (p.planetRadius + p.camHeight),
                                      0.0f, 1.0f));
            float groundObs = 1.0f - smoothstep(1500.0f, 12000.0f, p.camHeight);
            float ext = smoothstep(hzDip - 0.01f,
                                   hzDip + 0.12f * groundObs + 0.002f, dir.y);
            float visS = smoothstep(thresh * 0.6f, thresh * 2.5f,
                                    LuminanceCompute(stS) + 1e-9f);
            float visA = smoothstep(thresh * 0.6f, thresh * 2.5f,
                                    LuminanceCompute(stA) + 1e-9f);
            col = col - stS * visS * ext + stA * visA * ext;
        }
    }
    return col;
}

// Panel line 1128, exactly:
//   vec3 aces(vec3 x){ return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.); }
float3 ToneAcesCompute(float3 v)
{
    float3 num = v * (2.51f * v + 0.03f);
    float3 den = v * (2.43f * v + 0.59f) + 0.14f;
    return clamp(num / den, 0.0f, 1.0f);
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
//@@BAN: POST CHAIN AND VIEWPORT
float3 SkyPostApply(float3 hdr, float uu, float vv, float w, float h,
                    uint px, uint py, in SkyConfiguration p)
{
    float e = p.sunElevationDeg;
    float autoEV = -0.35f * smoothstep(-8.0f, -1.0f, e)
                 - 1.0f * smoothstep(-1.0f, 6.0f, e)
                 - 0.6f * smoothstep(6.0f, 30.0f, e);
    float3 col = hdr * MathExp2(p.exposureStops + autoEV);
    if (p.tonemap == 1u)
    {
        col = ToneAcesCompute(col);
    }
    else
    {
        col = clamp(col, 0.0f, 1.0f);
    }
    float2 vk = float2(uu * (h / w), vv) * 0.9f;
    float r = sqrt(vk.x * vk.x + vk.y * vk.y);
    float vig = 1.0f - p.vignette * MathPow(r, 2.2f);
    col = col * clamp(vig, 0.0f, 1.0f);
    col = float3(MathPow(max(col.x, 0.0f), 1.0f / 2.2f),
                 MathPow(max(col.y, 0.0f), 1.0f / 2.2f),
                 MathPow(max(col.z, 0.0f), 1.0f / 2.2f));
    float seed = MathFract(p.timeSeconds) * 100.0f;
    float gr = HashCompute13(float3(float(px) + 0.5f, float(py) + 0.5f, seed));
    return col + (gr - 0.5f) * p.grain * 0.12f;
}

// Panel line 1139: vec2 uv=(gl_FragCoord.xy*2.-uRes)/uRes.y — with fragY
// mapped from the top-down row. Returns (uu, vv).
float2 ViewportUVCompute(uint x, uint y, uint w, uint h)
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
float3 ViewportDirectionCompute(in SkyProjection cam, float2 uv)
{
    return normalize(cam.fwd + cam.right * uv.x * cam.tanHalf
                     + cam.up * uv.y * cam.tanHalf);
}

#endif // PROJECT_ZERO_CELESTIAL_CORE_SLANG

#endif // SKY_SPECIFICATION_INCLUDED
