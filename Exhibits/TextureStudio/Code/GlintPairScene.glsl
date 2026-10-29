/*==========================================================================================================================================
                                                         GLINTPAIRSCENE.GLSL
==========================================================================================================================================*/
// 🧩 Shared sand, blue-paint and snow material samples; only the cellular property / Glinty NDF selection differs.

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHARED SCATTERING
//------------------------------------------------------------------------------------------------------------------------

float SmoothDistribution(float Cosine, float Alpha)
{
    float A = Alpha * Alpha, Denominator = Cosine * Cosine * (A - 1.0) + 1.0;
    return A / (3.141592653589793 * Denominator * Denominator);
}
float SmithVisibility(float Cosine, float Alpha)
{
    return 2.0 * Cosine / max(1e-6, Cosine + sqrt(Alpha * Alpha + (1.0 - Alpha * Alpha) * Cosine * Cosine));
}

void main()
{
    vec3 Eye = vec3(0, 0, 6), Ray = normalize(vec3((Uv - .5) * vec2(4.9, 2.45), -6));
    vec3 RayDx = dFdx(Ray), RayDy = dFdy(Ray);
    float Travel = 1e6;
    int Material = -1, NearestIndex = 0;
    float NearestDiscriminant = -1e9, NearestTravel = 0.0;
    vec3 NearestCentre = vec3(0);
    vec3 Centre = vec3(0);
    for (int Index = 0; Index < 3; ++Index)
    {
        vec3 C = vec3((float(Index) - 1.0) * 1.55, 0, 0), Offset = Eye - C;
        float B = dot(Offset, Ray), D = B * B - dot(Offset, Offset) + .68 * .68;
        if (D > NearestDiscriminant)
        {
            NearestDiscriminant = D; NearestIndex = Index; NearestCentre = C; NearestTravel = -B;
        }
        if (D > 0.0)
        {
            float T = -B - sqrt(D);
            if (T > 0.0 && T < Travel)
            {
                Travel = T;
                Material = Index;
                Centre = C;
            }
        }
    }
    bool Hit = Material >= 0;
#ifdef COVERAGE_ONLY
    if (!Hit) discard;
    OutputColour = vec4(0);
    return;
#endif
    if (!Hit) { Centre = NearestCentre; Travel = NearestTravel; }
    int Index = Hit ? Material : NearestIndex;
    vec3 P = Eye + Ray * Travel;
    vec3 N = normalize(P - Centre), V = -Ray;
    vec3 Colour = Index == 0 ? vec3(.64, .38, .13) : Index == 1 ? vec3(.025, .12, .38) : vec3(.83, .90, .96);
    float Alpha = Index == 0 ? .32 : Index == 1 ? .20 : .24;
    float Metallic = Index == 1 ? .9 : 0.0;
    // 📝 Use same-sphere ray differentials, not differences across silhouette/background material IDs.
    // Grazing/polar denominators are bounded; this footprint is a preview approximation, not a general UV solution.
    vec3 Relative = Hit ? (P - Centre) / .68 : vec3(0, 0, 1);
    float Grazing = min(-.03, dot(N, Ray));
    vec3 Dx = Travel * (RayDx - Ray * (dot(N, RayDx) / Grazing)) / .68;
    vec3 Dy = Travel * (RayDy - Ray * (dot(N, RayDy) / Grazing)) / .68;
    float Polar = max(.001, Relative.x * Relative.x + Relative.z * Relative.z);
    float Latitude = 3.141592653589793 * sqrt(max(.001, 1.0 - Relative.y * Relative.y));
    vec2 SurfaceUv = vec2(atan(Relative.z, Relative.x) / 6.283185307179586 + .5,
                          asin(clamp(Relative.y, -1.0, 1.0)) / 3.141592653589793 + .5) * 2.0;
    SurfaceUv += vec2(float(Index) * 4.0);
    mat2 Footprint = 2.0 * mat2(vec2((Relative.x * Dx.z - Relative.z * Dx.x) / (Polar * 6.283185307179586), Dx.y / Latitude),
                               vec2((Relative.x * Dy.z - Relative.z * Dy.x) / (Polar * 6.283185307179586), Dy.y / Latitude));
#if METHOD == 1
    AutomotivePaint Paint;
    Paint.Configuration = uvec4(1, Index == 1 ? 3 : 1, 2, 1);
    Paint.Distribution = vec4(.64, Index == 0 ? 1200.0 : Index == 1 ? 450.0 : 900.0, 1.0, .65);
    Paint.Finish = vec4(max(.08, Alpha * .55), Alpha, Metallic, Metallic);
    Paint.Relief = vec4(.2, .55, .18, 24);
    Paint.Palette[0] = vec4(Colour, 0);
    Paint.AverageTint = Paint.Palette[0];
    PaintSurface Surface = EvaluatePaintSurface(Paint, P - Centre, Colour, Alpha, Metallic);
    Colour = Surface.Albedo;
    Alpha = Surface.Roughness;
    Metallic = Surface.Metallic;
    N = DerivePaintNormal(Paint, Surface, N, P - Centre, false);
#endif
    if (!Hit)
    {
        OutputColour = vec4(mix(vec3(.018, .025, .040), vec3(.055, .070, .090), Uv.y), 1);
        return;
    }
    vec3 Tangent = normalize(cross(N, abs(N.y) < .95 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    vec3 Bitangent = cross(N, Tangent);
    vec3 F0 = mix(vec3(.04), Colour, Metallic);
    vec3 Radiance = Colour * (.035 * (1.0 - Metallic)) + vec3(.006);
    float Nv = max(.001, dot(N, V));
    for (int Light = 0; Light < 3; ++Light)
    {
        if (Light >= LightCount)
            break;
        vec3 Position = Light == 0 ? vec3(-3, 4, 5) : Light == 1 ? vec3(3, 1, 3) : vec3(0, -3, 4);
        Position.x += Phase * .0002;
        vec3 Delta = Position - P, L = normalize(Delta), H = normalize(V + L);
        float Nl = max(0.0, dot(N, L));
        if (Nl <= 0.0)
            continue;
        float Nh = max(0.0, dot(N, H)), Vh = max(0.0, dot(V, H));
        float D = SmoothDistribution(Nh, Alpha);
#if METHOD == 2
        float Population = (Index == 0 ? 200000.0 : Index == 1 ? 800000.0 : 300000.0) * DensityScale;
        vec3 LocalHalf = vec3(dot(Tangent, H), dot(Bitangent, H), Nh);
        D = mix(D, ndf(LocalHalf, Alpha, .012, SurfaceUv, Footprint, Population, .7), .7);
#endif
        vec3 Fresnel = F0 + (1.0 - F0) * pow(1.0 - Vh, 5.0);
        float G = SmithVisibility(Nv, Alpha) * SmithVisibility(Nl, Alpha);
        vec3 Specular = Fresnel * (D * G / (4.0 * Nv));
        vec3 Diffuse = (1.0 - Fresnel) * Colour * ((1.0 - Metallic) * Nl / 3.141592653589793);
        if (Index == 1)
        {
            float CoatD = SmoothDistribution(Nh, .08);
            float CoatF = .04 + .96 * pow(1.0 - Vh, 5.0);
            float CoatG = SmithVisibility(Nv, .08) * SmithVisibility(Nl, .08);
            Specular = Specular * .92 + vec3(CoatF * CoatD * CoatG / (4.0 * Nv));
            Diffuse *= .92;
        }
        vec3 Power = Light == 0 ? vec3(38, 34, 28) : Light == 1 ? vec3(13, 19, 30) : vec3(15);
        Radiance += (Diffuse + Specular) * Power / dot(Delta, Delta);
    }
    Radiance *= 1.6;
    OutputColour = vec4(Radiance / (1.0 + Radiance), 1);
}
