#ifndef FRONTIER_PRESENTATION_DITHER
#define FRONTIER_PRESENTATION_DITHER
// Shared by denoise ON/OFF: fixed in screen space, bounded to half an rgba8 LSB.
float PresentationDither(ivec2 Pixel)
{
    uint H = uint(Pixel.x) * 1664525u + uint(Pixel.y) * 1013904223u;
    H = (H ^ (H >> 16u)) * 2246822519u;
    H = H ^ (H >> 13u);
    return (float(H & 1023u) / 1023.0 - 0.5) / 255.0;
}
#endif
