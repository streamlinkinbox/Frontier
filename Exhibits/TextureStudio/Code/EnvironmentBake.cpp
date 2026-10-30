/*============================================================================================================================================
                                                          ENVIRONMENTBAKE.CPP
============================================================================================================================================*/
// 🧩 Offline baker for the viewport studio environment archives: evaluate neutral area-light rigs into
// equirect PNGs under EngineContent/EnvironmentArchives. Regenerate with:
// EnvironmentBake [OutputDirectory] (default EngineContent/EnvironmentArchives).
// 📝 Rigs are deliberately colour-neutral (equal RGB) after the tinted analytic environments were
// retired; the runtime derives each archive's key light and strength from its own pixels.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>
#include <stb_image_write.h>

namespace
{
constexpr uint32_t Width = 1024, Height = 512;

std::array<float, 3> Normalize(std::array<float, 3> Value)
{
    const float Length = std::sqrt(Value[0] * Value[0] + Value[1] * Value[1] + Value[2] * Value[2]);
    return Length > 1e-12f ? std::array<float, 3>{Value[0] / Length, Value[1] / Length, Value[2] / Length}
                            : std::array<float, 3>{0, 1, 0};
}
float Dot(std::array<float, 3> A, std::array<float, 3> B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
std::array<float, 3> Cross(std::array<float, 3> A, std::array<float, 3> B)
{
    return {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
}
// 📝 Azimuth 0 faces the default camera (+Z, toward the viewer), positive toward +X (screen
// right); elevation from the horizon plane. Matches the shader's atan2(x, z) / acos(y) mapping.
std::array<float, 3> DirectionFromAngles(float AzimuthDegrees, float ElevationDegrees)
{
    const float Azimuth = AzimuthDegrees * std::numbers::pi_v<float> / 180;
    const float Elevation = ElevationDegrees * std::numbers::pi_v<float> / 180;
    return {std::cos(Elevation) * std::sin(Azimuth), std::sin(Elevation), std::cos(Elevation) * std::cos(Azimuth)};
}
std::array<float, 3> DirectionFromTexel(uint32_t X, uint32_t Y)
{
    const float Polar = (Y + .5f) / Height * std::numbers::pi_v<float>;
    const float Azimuth = ((X + .5f) / Width - .5f) * 2 * std::numbers::pi_v<float>;
    const float Sine = std::sin(Polar);
    return {Sine * std::sin(Azimuth), std::cos(Polar), Sine * std::cos(Azimuth)};
}
float Smoothstep(float Edge0, float Edge1, float Value)
{
    const float T = std::clamp((Value - Edge0) / (Edge1 - Edge0), 0.f, 1.f);
    return T * T * (3 - 2 * T);
}

struct Panel
{
    float Azimuth, Elevation;   // degrees
    float HalfWidth, HalfHeight; // tangent-space half extents at the centre direction
    float Radiance;
    float Softness = .12f;      // edge feather relative to the half extents
    float Roll = 0;             // degrees about the centre direction
};

struct StudioRig
{
    const char *FileName;
    float Zenith, Horizon, Floor;
    std::vector<Panel> Panels;
};

float EvaluatePanel(const Panel &Panel, std::array<float, 3> Direction)
{
    const auto Centre = DirectionFromAngles(Panel.Azimuth, Panel.Elevation);
    const float Facing = Dot(Direction, Centre);
    if (Facing <= .05f) return 0;
    // 📝 Gnomonic projection onto the panel plane keeps rectangles rectangular instead of
    // bending them along lines of latitude.
    std::array<float, 3> Up{0, 1, 0};
    if (std::abs(Dot(Centre, Up)) > .98f) Up = {0, 0, -1};
    auto Right = Normalize(Cross(Up, Centre));
    auto PanelUp = Cross(Centre, Right);
    if (Panel.Roll != 0)
    {
        const float Angle = Panel.Roll * std::numbers::pi_v<float> / 180;
        const float Cosine = std::cos(Angle), Sine = std::sin(Angle);
        const auto RolledRight = std::array<float, 3>{Right[0] * Cosine + PanelUp[0] * Sine,
                                                      Right[1] * Cosine + PanelUp[1] * Sine,
                                                      Right[2] * Cosine + PanelUp[2] * Sine};
        PanelUp = {-Right[0] * Sine + PanelUp[0] * Cosine, -Right[1] * Sine + PanelUp[1] * Cosine,
                   -Right[2] * Sine + PanelUp[2] * Cosine};
        Right = RolledRight;
    }
    const float LocalX = Dot(Direction, Right) / Facing, LocalY = Dot(Direction, PanelUp) / Facing;
    const float EdgeX = 1 - Smoothstep(Panel.HalfWidth * (1 - Panel.Softness), Panel.HalfWidth * (1 + Panel.Softness), std::abs(LocalX));
    const float EdgeY = 1 - Smoothstep(Panel.HalfHeight * (1 - Panel.Softness), Panel.HalfHeight * (1 + Panel.Softness), std::abs(LocalY));
    // 📝 Softbox faces read slightly brighter at the centre than at the rim.
    const float CentreFalloff = 1 / (1 + .35f * (LocalX * LocalX + LocalY * LocalY));
    return Panel.Radiance * EdgeX * EdgeY * CentreFalloff;
}

float EvaluateRig(const StudioRig &Rig, std::array<float, 3> Direction)
{
    const float Height = Direction[1];
    float Radiance = Height >= 0 ? Rig.Horizon + (Rig.Zenith - Rig.Horizon) * std::pow(Height, .65f)
                                 : Rig.Horizon + (Rig.Floor - Rig.Horizon) * std::pow(-Height, .6f);
    for (const auto &Panel : Rig.Panels) Radiance += EvaluatePanel(Panel, Direction);
    return Radiance;
}

uint8_t EncodeSrgbChannel(float Value)
{
    const float Clamped = std::clamp(Value, 0.f, 1.f);
    const float Encoded = Clamped <= .0031308f ? Clamped * 12.92f : 1.055f * std::pow(Clamped, 1.f / 2.4f) - .055f;
    return uint8_t(std::lround(Encoded * 255.f));
}

void BakeRig(const StudioRig &Rig, const std::filesystem::path &Directory)
{
    std::vector<float> Field(size_t(Width) * Height, 0);
    float Peak = 0;
    double WeightSum = 0, LuminanceSum = 0;
    for (uint32_t Y = 0; Y < Height; ++Y)
    {
        const float Weight = std::sin((Y + .5f) / Height * std::numbers::pi_v<float>);
        for (uint32_t X = 0; X < Width; ++X)
        {
            // 📝 2x2 supersampling keeps panel edges smooth after the runtime's 512x256 resample.
            float Radiance = 0;
            for (int SubY = 0; SubY < 2; ++SubY)
                for (int SubX = 0; SubX < 2; ++SubX)
                {
                    const float Polar = (Y + (SubY + .5f) / 2) / Height * std::numbers::pi_v<float>;
                    const float Azimuth = ((X + (SubX + .5f) / 2) / Width - .5f) * 2 * std::numbers::pi_v<float>;
                    const float Sine = std::sin(Polar);
                    Radiance += EvaluateRig(Rig, {Sine * std::sin(Azimuth), std::cos(Polar), Sine * std::cos(Azimuth)}) / 4;
                }
            Field[size_t(Y) * Width + X] = Radiance;
            Peak = std::max(Peak, Radiance);
            LuminanceSum += Radiance * Weight;
            WeightSum += Weight;
        }
    }
    if (Peak <= 0) throw std::runtime_error(std::string("Environment rig is black: ") + Rig.FileName);
    // 📝 The runtime re-normalizes strength from each archive's own mean, so the byte encoding
    // only needs a consistent peak; dividing by the peak preserves emitter-to-ambient ratios.
    std::vector<uint8_t> Pixels(size_t(Width) * Height * 3);
    for (size_t Texel = 0; Texel < Field.size(); ++Texel)
    {
        const uint8_t Encoded = EncodeSrgbChannel(Field[Texel] / Peak);
        Pixels[Texel * 3] = Pixels[Texel * 3 + 1] = Pixels[Texel * 3 + 2] = Encoded;
    }
    const auto Path = Directory / Rig.FileName;
    if (!stbi_write_png(Path.string().c_str(), int(Width), int(Height), 3, Pixels.data(), int(Width) * 3))
        throw std::runtime_error("Environment PNG write failed: " + Path.string());
    const double Mean = LuminanceSum / WeightSum / Peak;
    const double Strength = std::clamp(.2 / std::max(Mean, 1e-4), .15, 4.0);
    std::cout << Rig.FileName << " mean=" << Mean << " runtimeStrength=" << Strength
              << " effectivePeak=" << Strength << " effectiveAmbient=" << .2 << '\n';
}

const std::vector<StudioRig> Rigs{
    // 📝 The default studio: large key softbox upper-left, right fill, overhead strip and a
    // back-left kick over a dim neutral gradient (the retired analytic rig, minus its tint).
    {"StudioSoftbox.png", .34f, .12f, .045f, {
        {-38, 42, .55f, .42f, 5.f, .18f, -12},
        {52, 18, .4f, .55f, 1.6f, .2f, 0},
        {175, 58, .9f, .18f, 2.4f, .25f, 0},
        {-125, 12, .16f, .5f, 1.8f, .2f, 0}}},
    // 📝 Dark surround with two vertical rim strips and a top softbox, for glossy products.
    {"StudioStrip.png", .1f, .035f, .012f, {
        {-115, 25, .1f, .8f, 5.f, .3f, 0},
        {115, 25, .1f, .8f, 5.f, .3f, 0},
        {0, 75, .7f, .3f, 2.2f, .25f, 0},
        {0, 8, 1.2f, .5f, .35f, .4f, 0}}},
    // 📝 One large window camera-left with a weak opposite bounce.
    {"WindowLight.png", .3f, .16f, .05f, {
        {-55, 22, .75f, .65f, 4.2f, .1f, 0},
        {120, 5, 1.f, .4f, .5f, .4f, 0}}},
    // 📝 Shadowless overcast dome: bright zenith falling to a dim floor, one huge soft glow.
    {"Overcast.png", .95f, .42f, .14f, {
        {-30, 60, 1.4f, .8f, .5f, .6f, 0}}},
    // 📝 Near-black studio: single overhead spot, thin horizon glow behind, faint front fill.
    {"DarkStudio.png", .03f, .012f, .004f, {
        {-15, 68, .3f, .3f, 6.f, .35f, 0},
        {180, 2, 2.2f, .07f, .8f, .5f, 0},
        {20, 10, .8f, .3f, .25f, .4f, 0}}},
};
} // namespace

int main(int ArgumentCount, char *Arguments[])
{
    try
    {
        const std::filesystem::path Directory = ArgumentCount > 1 ? Arguments[1] : "EngineContent/EnvironmentArchives";
        std::filesystem::create_directories(Directory);
        for (const auto &Rig : Rigs) BakeRig(Rig, Directory);
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "EnvironmentBake failed: " << Failure.what() << '\n';
        return 1;
    }
}
