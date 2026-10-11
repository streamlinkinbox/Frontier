#version 450
// Transcription of Engine/DisplayPresentation/VolumeRaymarch.h. That header is authoritative: it is what the
// headless proof marches, and every constant below is carried across from it rather than re-derived. A change
// to the lighting goes in the header first and arrives here second, or the CPU picture and the device picture
// stop agreeing and nothing says so.
//
// Vulkan has real 3D images, so the browser's tiled 2D atlas - a WebGL2 workaround - is not reproduced here.
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler3D uThermo;          // r = smoke density, g = temperature above ambient
layout(binding = 1, rgba16f) uniform writeonly image2D uTarget;

layout(push_constant) uniform Parameters
{
    vec4 CameraOrigin;      // xyz = ray origin in unit-cube coordinates, w = domain diagonal in metres
    vec4 CameraForward;     // xyz, w unused
    vec4 CameraRight;       // xyz = right * tan(halfFov) * aspect, w unused
    vec4 CameraUp;          // xyz = up * tan(halfFov), w unused
    vec4 SunDirection;      // xyz = unit vector toward the sun, w unused
    vec4 Optical;           // x = densityExtinction, y = smokeAlbedo, z = shadowDensity, w = fireIntensity
    vec4 Appearance;        // x = temperatureScale, y = internalScattering, z = phaseAnisotropy, w = exposure
    vec4 Illumination;      // x = ambientIntensity, y = sunIntensity, z = unused, w = unused
    ivec4 Counts;           // x = raymarchSteps, y = shadowSteps, z = targetWidth, w = targetHeight
} p;

const float Pi = 3.14159265358979323846;

float HenyeyGreenstein(float CosineAngle, float Asymmetry)
{
    float Squared = Asymmetry * Asymmetry;
    float Denominator = max(1e-4, pow(1.0 + Squared - 2.0 * Asymmetry * CosineAngle, 1.5));
    return (1.0 - Squared) / (4.0 * Pi * Denominator);
}

// Two lobes, not one. A single forward lobe makes smoke read like glass; the backward lobe at -0.25 mixed in
// at 28 percent is what gives a back-lit plume its silver lining.
float DualHenyeyGreenstein(float CosineAngle, float Asymmetry)
{
    return mix(HenyeyGreenstein(CosineAngle, -0.25), HenyeyGreenstein(CosineAngle, Asymmetry), 0.72);
}

vec3 BlackbodyRadiance(float Temperature, float TemperatureScale)
{
    float Mapped = clamp(Temperature * TemperatureScale * 0.28, 0.0, 1.6);
    if (Mapped <= 0.01) return vec3(0.0);

    const vec3 Stop0 = vec3(0.00, 0.00, 0.000);
    const vec3 Stop1 = vec3(0.55, 0.03, 0.005);
    const vec3 Stop2 = vec3(1.00, 0.24, 0.010);
    const vec3 Stop3 = vec3(1.00, 0.86, 0.520);

    float Scaled = Mapped / 1.6 * 3.0;
    int Low = clamp(int(Scaled), 0, 2);
    float Fraction = Scaled - float(Low);
    vec3 Lower = Low == 0 ? Stop0 : (Low == 1 ? Stop1 : Stop2);
    vec3 Upper = Low == 0 ? Stop1 : (Low == 1 ? Stop2 : Stop3);
    return mix(Lower, Upper, Fraction);
}

bool UnitCubeInterval(vec3 Origin, vec3 Direction, out float Enter, out float Exit)
{
    vec3 Inverse = 1.0 / mix(Direction, vec3(1e-9), equal(Direction, vec3(0.0)));
    vec3 Near = (vec3(0.0) - Origin) * Inverse;
    vec3 Far  = (vec3(1.0) - Origin) * Inverse;
    vec3 Low  = min(Near, Far);
    vec3 High = max(Near, Far);
    Enter = max(0.0, max(Low.x, max(Low.y, Low.z)));
    Exit  = min(High.x, min(High.y, High.z));
    return Exit > Enter;
}

vec2 ReadVolume(vec3 Coordinate)
{
    return texture(uThermo, Coordinate).rg;
}

// Leaves the cube early rather than clamping: a clamped sample repeated at the wall darkens everything near
// the boundary by an amount that depends only on where the box happens to be.
float SunTransmittance(vec3 Coordinate, vec3 SunDirection)
{
    int Steps = max(1, p.Counts.y);
    float StepSize = 0.6 / float(Steps);
    float Depth = 0.0;
    for (int Step = 0; Step < Steps; ++Step)
    {
        vec3 Point = Coordinate + SunDirection * ((float(Step) + 0.5) * StepSize);
        if (any(lessThan(Point, vec3(0.0))) || any(greaterThan(Point, vec3(1.0)))) break;
        Depth += ReadVolume(Point).r * p.Optical.z * StepSize;
    }
    return exp(-Depth);
}

void main()
{
    ivec2 Pixel = ivec2(gl_GlobalInvocationID.xy);
    if (Pixel.x >= p.Counts.z || Pixel.y >= p.Counts.w) return;

    vec2 Screen = (vec2(Pixel) + 0.5) / vec2(p.Counts.zw) * 2.0 - 1.0;
    vec3 Origin = p.CameraOrigin.xyz;
    vec3 Direction = normalize(p.CameraForward.xyz + p.CameraRight.xyz * Screen.x + p.CameraUp.xyz * Screen.y);

    vec3 Radiance = vec3(0.0);
    float Transmittance = 1.0;

    float Enter, Exit;
    if (UnitCubeInterval(Origin, Direction, Enter, Exit))
    {
        int Steps = max(1, p.Counts.x);
        float StepSize = (Exit - Enter) / float(Steps);

        // Normalised by the domain diagonal, so a plume keeps its opacity while dynamic bounds expand the box.
        float OpticalScale = 1.75 / max(1.0, p.CameraOrigin.w);

        vec3 SunDirection = p.SunDirection.xyz;
        float Phase = DualHenyeyGreenstein(dot(Direction, SunDirection), p.Appearance.z);
        vec3 Sunlight = vec3(1.00, 0.93, 0.82) * p.Illumination.y;
        vec3 Skylight = vec3(0.30, 0.38, 0.52) * p.Illumination.x;

        for (int Step = 0; Step < Steps; ++Step)
        {
            if (Transmittance < 0.003) break;

            vec3 Point = Origin + Direction * (Enter + (float(Step) + 0.5) * StepSize);
            vec2 Sample = ReadVolume(Point);
            float Smoke = Sample.r;
            float Temperature = Sample.g;
            if (Smoke <= 0.004 && Temperature <= 0.015) continue;

            float Interval = StepSize * OpticalScale;
            float Extinction = max(0.001, Smoke * p.Optical.x + Temperature * 0.65);
            float StepTransmittance = exp(-Extinction * Interval);

            // A plume is darker where it is dense, because light is scattered away before it can leave.
            float Powder = 1.0 - 0.45 * exp(-Smoke * p.Optical.x * 2.2);
            float Direct = SunTransmittance(Point, SunDirection) * Phase * Powder * 2.4;
            float Height = mix(0.45, 1.15, Point.z);

            vec3 Emission = BlackbodyRadiance(Temperature, p.Appearance.x);
            float Internal = p.Appearance.y * (0.45 + 0.55 * exp(-Smoke * 0.9));

            vec3 Scattered = (Sunlight * Direct + Skylight * Height) * p.Optical.y * Smoke;
            vec3 Emitted = Emission * p.Optical.w * Internal;
            Radiance += Transmittance * (Scattered + Emitted) * Interval;
            Transmittance *= StepTransmittance;
        }
    }

    imageStore(uTarget, Pixel, vec4(Radiance * p.Appearance.w, 1.0 - Transmittance));
}
