/*==========================================================================================================================================
                                                        LUNARSURFACEREFERENCE.CPP
==========================================================================================================================================*/
// 🧩 NativeMoonPreview.js sampled from the same six registered 2K maps, with its phase and surface-orientation conventions.

#include "Engine/Interface/Inspectors/LunarSurfaceReference.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    REGISTERED MAP DECODING
//------------------------------------------------------------------------------------------------------------------------

LunarSurfaceReference ReadLunarSurface(const std::filesystem::path &Path)
{
    LunarSurfaceReference Result;
    std::ifstream         Input(Path, std::ios::binary | std::ios::ate);
    if (!Input)
    {
        Result.Diagnostic = "Registered lunar surface unavailable";
        return Result;
    }
    const auto Length = Input.tellg();
    if (Length <= 0 || Length > 16 * 1024 * 1024)
    {
        Result.Diagnostic = "Lunar image exceeds the encoded size limit";
        return Result;
    }
    std::vector<uint8_t> Encoded(static_cast<size_t>(Length));
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Encoded.data()), Length))
    {
        Result.Diagnostic = "Lunar image read failed";
        return Result;
    }
    int Channels = 0;
    if (!stbi_info_from_memory(Encoded.data(), static_cast<int>(Encoded.size()), &Result.Width, &Result.Height, &Channels) ||
        Result.Width <= 0 || Result.Height <= 0 || Result.Width > 4096 || Result.Height > 4096)
    {
        Result.Diagnostic = "Invalid or oversized lunar image";
        return Result;
    }
    uint8_t *Pixels = stbi_load_from_memory(Encoded.data(), static_cast<int>(Encoded.size()), &Result.Width, &Result.Height, &Channels, 3);
    if (Pixels == nullptr)
    {
        Result.Diagnostic = "Lunar image decode failed";
        return Result;
    }
    Result.Rgb.assign(Pixels, Pixels + static_cast<size_t>(Result.Width) * Result.Height * 3);
    stbi_image_free(Pixels);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPHERICAL SURFACE LIGHT
//------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> GenerateLunarReference(const LunarSurfaceReference &Surface, const MoonInstanceConfiguration &Moon)
{
    if (!std::isfinite(Moon.PhaseDays) || !std::isfinite(Moon.RollDegrees) || !std::isfinite(Moon.PitchDegrees) ||
        !std::isfinite(Moon.SpinDegrees) || !std::isfinite(Moon.Brightness) || Surface.Width <= 0 || Surface.Height <= 0 ||
        Surface.Width > 4096 || Surface.Height > 4096 || Surface.Rgb.size() != static_cast<size_t>(Surface.Width) * Surface.Height * 3)
        return {};
    const auto          &Preset = LunarSurfacePresets[std::min<uint32_t>(Moon.Body, 5)];
    std::vector<uint8_t> Pixels(256 * 256 * 4, 0);
    const double         Roll  = Moon.RollDegrees * EnvironmentReadings::Pi / 180;
    const double         Pitch = (Preset.Tilt + Moon.PitchDegrees) * EnvironmentReadings::Pi / 180;
    const double         Spin  = Moon.SpinDegrees * EnvironmentReadings::Pi / 180;
    const double         Phase = Moon.PhaseDays / 29.53 * EnvironmentReadings::Pi * 2;
    for (int Row = 0; Row < 256; ++Row)
    {
        for (int Column = 0; Column < 256; ++Column)
        {
            const double NormalAlong = (Column + 0.5 - 128) / 126, NormalAcross = (128 - Row - 0.5) / 126;
            const double RadiusSquared = NormalAlong * NormalAlong + NormalAcross * NormalAcross;
            if (RadiusSquared > 1) continue;
            const double NormalForward = std::sqrt(1 - RadiusSquared);
            const double RolledAlong   = NormalAlong * std::cos(Roll) - NormalAcross * std::sin(Roll);
            const double RolledAcross  = NormalAlong * std::sin(Roll) + NormalAcross * std::cos(Roll);
            const double TurnedAcross  = RolledAcross * std::cos(Pitch) - NormalForward * std::sin(Pitch);
            const double TurnedForward = RolledAcross * std::sin(Pitch) + NormalForward * std::cos(Pitch);
            const double U = std::fmod((std::atan2(RolledAlong, TurnedForward) + Spin) / (2 * EnvironmentReadings::Pi) + 2.5, 1);
            const double V = 0.5 - std::asin(std::clamp(TurnedAcross, -1.0, 1.0)) / EnvironmentReadings::Pi;
            const size_t Sample =
                (static_cast<size_t>(std::clamp(static_cast<int>(V * Surface.Height), 0, Surface.Height - 1)) * Surface.Width +
                 static_cast<size_t>(std::clamp(static_cast<int>(U * Surface.Width), 0, Surface.Width - 1))) *
                3;
            const double Wrap        = Preset.Haze * 0.3;
            const double Cosine      = std::max(0.0, (NormalAlong * std::sin(Phase) - NormalForward * std::cos(Phase) + Wrap) / (1 + Wrap));
            const double Limb        = 1 - 0.5 * std::pow(1 - NormalForward, 2) * std::max<double>(Preset.Haze, 0.35);
            const double Light       = (std::pow(Cosine, 0.8) * Limb + 0.012) * Moon.Brightness;
            const size_t Destination = (static_cast<size_t>(Row) * 256 + Column) * 4;
            for (size_t Channel = 0; Channel < 3; ++Channel)
            {
                const double Albedo = std::pow(Surface.Rgb[Sample + Channel] / 255.0, 2.2 * Preset.Gamma);
                Pixels[Destination + Channel] =
                    static_cast<uint8_t>(std::lround(255 * std::pow(std::clamp(Albedo * Light, 0.0, 1.0), 1 / 2.2)));
            }
            Pixels[Destination + 3] = 255;
        }
    }
    return Pixels;
}

const char *ResolveLunarPhaseName(double Days) noexcept
{
    constexpr const char *Names[] = {"New moon",  "Waxing crescent", "First quarter", "Waxing gibbous",
                                     "Full moon", "Waning gibbous",  "Last quarter",  "Waning crescent"};
    if (!std::isfinite(Days)) return "Unknown phase";
    const double Age = std::fmod(std::fmod(Days, 29.53) + 29.53, 29.53);
    return Names[static_cast<size_t>(std::floor(Age / 29.53 * 8 + 0.5)) % 8];
}

} // namespace Frontier
