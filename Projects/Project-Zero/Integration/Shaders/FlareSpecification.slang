//============================================================================================================================================
// FlareSpecification.slang — camera lens artifacts (halo, ghosts, anamorphic streak) over an HDR frame
//
// Dual-compile core (Slang + C++17 through SlangInterchange.h), same contract as the sky/fog cores: pure functions,
// no file-scope state, no engine types. All images are interleaved RGB float planes; the C++ driver owns the buffers
// (the engine's Vector3 frame store is 16-byte aligned, so it is packed in/out at the boundary, never aliased here).
// Included by exactly ONE translation unit (RendererHost.cpp); the header never names these routines.
//============================================================================================================================================

#ifndef FLARE_SPECIFICATION_INCLUDED
#define FLARE_SPECIFICATION_INCLUDED

// Rec.709 luminance of one HDR pixel.
float FlareLuminance(float3 c)
{
    return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f;
}

// Soft-knee bright-pass: keeps HDR highlights, kills everything below threshold.
float3 FlareBrightPixel(float3 c, float threshold, float softness)
{
    float lum = FlareLuminance(c);
    float w = smoothstep(threshold, threshold + max(softness, 1e-6f), lum);
    return c * w;
}

// Clamped bilinear sample of an interleaved RGB float plane.
float3 FlareSampleBilinear(const float* plane, int w, int h, float x, float y)
{
    float cx = clamp(x, 0.0f, float(w) - 1.0f);
    float cy = clamp(y, 0.0f, float(h) - 1.0f);
    int x0 = int(cx);
    int y0 = int(cy);
    int x1 = (x0 + 1 < w) ? (x0 + 1) : (w - 1);
    int y1 = (y0 + 1 < h) ? (y0 + 1) : (h - 1);
    float fx = cx - float(x0);
    float fy = cy - float(y0);
    int i00 = (y0 * w + x0) * 3;
    int i10 = (y0 * w + x1) * 3;
    int i01 = (y1 * w + x0) * 3;
    int i11 = (y1 * w + x1) * 3;
    float3 c00 = float3(plane[i00], plane[i00 + 1], plane[i00 + 2]);
    float3 c10 = float3(plane[i10], plane[i10 + 1], plane[i10 + 2]);
    float3 c01 = float3(plane[i01], plane[i01 + 1], plane[i01 + 2]);
    float3 c11 = float3(plane[i11], plane[i11 + 1], plane[i11 + 2]);
    return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
}

// One horizontal gaussian blur pass (radius <= 64).
void FlareBlurH(const float* src, float* dst, int w, int h, int radius, float sigma)
{
    float kernel[129];
    float total = 0.0f;
    for (int o = -radius; o <= radius; o++)
    {
        float g = exp(-0.5f * float(o * o) / (sigma * sigma));
        kernel[o + radius] = g;
        total = total + g;
    }
    float inv = 1.0f / total;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            float3 acc = float3(0.0f, 0.0f, 0.0f);
            for (int o = -radius; o <= radius; o++)
            {
                int sx = x + o;
                sx = (sx < 0) ? 0 : ((sx > w - 1) ? (w - 1) : sx);
                int si = (y * w + sx) * 3;
                acc = acc + float3(src[si], src[si + 1], src[si + 2]) * kernel[o + radius];
            }
            int di = (y * w + x) * 3;
            dst[di] = acc.x * inv;
            dst[di + 1] = acc.y * inv;
            dst[di + 2] = acc.z * inv;
        }
    }
}

// One vertical gaussian blur pass (radius <= 64).
void FlareBlurV(const float* src, float* dst, int w, int h, int radius, float sigma)
{
    float kernel[129];
    float total = 0.0f;
    for (int o = -radius; o <= radius; o++)
    {
        float g = exp(-0.5f * float(o * o) / (sigma * sigma));
        kernel[o + radius] = g;
        total = total + g;
    }
    float inv = 1.0f / total;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            float3 acc = float3(0.0f, 0.0f, 0.0f);
            for (int o = -radius; o <= radius; o++)
            {
                int sy = y + o;
                sy = (sy < 0) ? 0 : ((sy > h - 1) ? (h - 1) : sy);
                int si = (sy * w + x) * 3;
                acc = acc + float3(src[si], src[si + 1], src[si + 2]) * kernel[o + radius];
            }
            int di = (y * w + x) * 3;
            dst[di] = acc.x * inv;
            dst[di + 1] = acc.y * inv;
            dst[di + 2] = acc.z * inv;
        }
    }
}

// Anamorphic streak: long horizontal gaussian over the hard-thresholded highlights.
void FlareStreakH(const float* src, float* dst, int w, int h, int radius, float sigma, float threshold, float3 tint)
{
    float kernel[129];
    float total = 0.0f;
    for (int o = -radius; o <= radius; o++)
    {
        float g = exp(-0.5f * float(o * o) / (sigma * sigma));
        kernel[o + radius] = g;
        total = total + g;
    }
    float inv = 1.0f / total;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            float3 acc = float3(0.0f, 0.0f, 0.0f);
            for (int o = -radius; o <= radius; o++)
            {
                int sx = x + o;
                sx = (sx < 0) ? 0 : ((sx > w - 1) ? (w - 1) : sx);
                int si = (y * w + sx) * 3;
                float3 c = float3(src[si], src[si + 1], src[si + 2]);
                float lum = FlareLuminance(c);
                float k = (lum > threshold) ? (lum - threshold) : 0.0f;
                acc = acc + c * (k * kernel[o + radius]);
            }
            int di = (y * w + x) * 3;
            dst[di] = acc.x * inv * tint.x;
            dst[di + 1] = acc.y * inv * tint.y;
            dst[di + 2] = acc.z * inv * tint.z;
        }
    }
}

// One lens ghost: a minified whole-frame copy on the source/center optical axis.
// axisT places the ghost: 0 = on the source, 1 = mirrored past the center, >1 beyond, <0 past the source.
void FlareGhostAccum(const float* src, float* dst, int w, int h,
    float srcX, float srcY, float axisT, float size, float3 tint, float intensity)
{
    float cx = float(w) * 0.5f;
    float cy = float(h) * 0.5f;
    float dx = srcX - cx;
    float dy = srcY - cy;
    float dist = sqrt(dx * dx + dy * dy);
    float ux = (dist > 1e-4f) ? (dx / dist) : 1.0f;
    float uy = (dist > 1e-4f) ? (dy / dist) : 0.0f;
    float gx = cx - ux * dist * axisT;
    float gy = cy - uy * dist * axisT;
    float minDim = (w < h) ? float(w) : float(h);
    float rad = size * minDim * 0.5f;
    float invSize = 1.0f / max(size, 1e-4f);
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            float rx = float(x) - gx;
            float ry = float(y) - gy;
            float r = sqrt(rx * rx + ry * ry) / max(rad, 1e-4f);
            float mask = 1.0f - smoothstep(0.65f, 1.0f, r);
            if (mask > 0.0f)
            {
                float3 c = FlareSampleBilinear(src, w, h, srcX + rx * invSize, srcY + ry * invSize);
                int di = (y * w + x) * 3;
                float k = mask * intensity;
                dst[di] = dst[di] + c.x * tint.x * k;
                dst[di + 1] = dst[di + 1] + c.y * tint.y * k;
                dst[di + 2] = dst[di + 2] + c.z * tint.z * k;
            }
        }
    }
}

// Composite: bilinear-upsample the half-res flare and add it into the full-res packed frame.
void FlareCompositeAdd(float* fullPacked, int fw, int fh, const float* flareHalf, int hw, int hh, float strength)
{
    for (int y = 0; y < fh; y++)
    {
        for (int x = 0; x < fw; x++)
        {
            float hx = (float(x) + 0.5f) * float(hw) / float(fw) - 0.5f;
            float hy = (float(y) + 0.5f) * float(hh) / float(fh) - 0.5f;
            float3 f = FlareSampleBilinear(flareHalf, hw, hh, hx, hy) * strength;
            int di = (y * fw + x) * 3;
            fullPacked[di] = fullPacked[di] + f.x;
            fullPacked[di + 1] = fullPacked[di + 1] + f.y;
            fullPacked[di + 2] = fullPacked[di + 2] + f.z;
        }
    }
}

#endif // FLARE_SPECIFICATION_INCLUDED
