#pragma once
/*==========================================================================================================================================
                                                            GASSCENECODEC.H
==========================================================================================================================================*/
// 🧩 Reads and writes a gas scene as TOML — the format the engine uses, and the one Experimental/Fluid/src/SceneTomlCodec.js exports.
//
// ⚠️ TOML distinguishes an integer from a float, and so does GasSettings. Reading accepts an integer where a float is
//    wanted, because 1 and 1.0 denote the same quantity, while refusing a float where an integer voxel count is wanted.

#include "Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h"

#include <toml++/toml.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                      THE SCENE
//----------------------------------------------------------------------------------------------------------------------

constexpr std::string_view GasSceneFormat    = "frontier-fluid-scene";
constexpr int64_t          GasSceneVersion   = 1;
constexpr std::string_view GasSceneExtension = ".gasscene.toml";

struct GasSceneCamera
{
    double Theta     = 0.0;                      // [rad] - azimuth
    double Phi       = 1.3;                      // [rad] - polar, clamped away from the poles by the tool
    double Distance  = 3.0;                      // [-]   - orbit radius, in domain widths
    double Centre[3] = { 0.0, 0.0, 0.0 };        // [-]   - what it orbits, in domain-relative units
};

struct GasSceneNames
{
    std::string Domain   = "Gas domain";
    std::string Emitter  = "Fire emitter";
    std::string Collider = "Sphere collider";
    std::string Sun      = "Directional light";
};

struct GasScene
{
    std::string    Name          = "Untitled scene";
    GasSettings    Settings{};
    GasSceneNames  Names{};
    GasSceneCamera Camera{};
    bool           CameraPresent = false;        // [-] - a scene may decline to frame itself
};

struct GasSceneReading
{
    bool                     Accepted = false;
    std::string              Refusal{};          // [-] - empty when accepted
    std::vector<std::string> UnknownSettings{};  // [-] - present in the file, absent from this build
    std::vector<std::string> RefusedSettings{};  // [-] - present and understood, but of the wrong type
};

//----------------------------------------------------------------------------------------------------------------------
//                                                   READING SETTINGS
//----------------------------------------------------------------------------------------------------------------------

namespace GasSceneDetail
{

inline bool ReadFloat(const toml::node& Entry, float& Target) noexcept
{
    if (const auto Floating = Entry.value<double>(); Floating && Entry.is_number())
    {
        Target = static_cast<float>(*Floating);
        return true;
    }
    return false;
}

inline bool ReadCount(const toml::node& Entry, int32_t& Target) noexcept
{
    if (!Entry.is_integer())
        return false;
    const auto Whole = Entry.value<int64_t>();
    if (!Whole)
        return false;
    Target = static_cast<int32_t>(*Whole);
    return true;
}

inline bool ReadSwitch(const toml::node& Entry, bool& Target) noexcept
{
    if (!Entry.is_boolean())
        return false;
    Target = *Entry.value<bool>();
    return true;
}

} // namespace GasSceneDetail

// 📝 Generated from Experimental/Fluid/src/presets.js by Tools/Build/GenerateGasSceneCodec.py.
#include "Frontier/Engine/VolumetricDynamics/GasSceneFields.inl"

//----------------------------------------------------------------------------------------------------------------------
//                                                       READING
//----------------------------------------------------------------------------------------------------------------------

inline GasSceneReading ReadGasScene(std::string_view Text, GasScene& OutScene) noexcept
{
    GasSceneReading Result;

    toml::table Parsed;
    try
    {
        Parsed = toml::parse(Text);
    }
    catch (const toml::parse_error& Failure)
    {
        Result.Refusal = std::string("not readable as TOML: ") + Failure.description().data();
        return Result;
    }
    catch (...)
    {
        Result.Refusal = "not readable as TOML";
        return Result;
    }

    const auto Format = Parsed["format"].value<std::string>();
    if (!Format || *Format != GasSceneFormat)
    {
        Result.Refusal = "not a Frontier gas scene; the format marker is missing or wrong";
        return Result;
    }
    const auto Version = Parsed["version"].value<int64_t>();
    if (!Version || *Version != GasSceneVersion)
    {
        Result.Refusal = "a gas scene of a version this build does not read";
        return Result;
    }

    GasScene Scene;
    const auto Name = Parsed["name"].value<std::string>();
    if (!Name || Name->empty() || Name->size() > 64u)
    {
        Result.Refusal = "a scene must carry a name of 1 to 64 characters";
        return Result;
    }
    Scene.Name = *Name;

    if (const toml::table* Names = Parsed["names"].as_table())
    {
        if (const auto Entry = (*Names)["domain"].value<std::string>())   Scene.Names.Domain   = *Entry;
        if (const auto Entry = (*Names)["emitter"].value<std::string>())  Scene.Names.Emitter  = *Entry;
        if (const auto Entry = (*Names)["collider"].value<std::string>()) Scene.Names.Collider = *Entry;
        if (const auto Entry = (*Names)["sun"].value<std::string>())      Scene.Names.Sun      = *Entry;
    }

    if (const toml::table* Camera = Parsed["camera"].as_table())
    {
        Scene.CameraPresent = true;
        if (const auto Entry = (*Camera)["theta"].value<double>())    Scene.Camera.Theta    = *Entry;
        if (const auto Entry = (*Camera)["phi"].value<double>())      Scene.Camera.Phi      = *Entry;
        if (const auto Entry = (*Camera)["distance"].value<double>()) Scene.Camera.Distance = *Entry;
        if (const toml::array* Centre = (*Camera)["centre"].as_array())
        {
            if (Centre->size() != 3u)
            {
                Result.Refusal = "a camera centre must carry exactly three numbers";
                return Result;
            }
            for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
                Scene.Camera.Centre[Axis] = Centre->get(Axis)->value_or(0.0);
        }
    }

    if (const toml::table* Settings = Parsed["settings"].as_table())
    {
        for (const auto& [Key, Entry] : *Settings)
        {
            const GasSceneDetail::Crossing Crossed =
                GasSceneDetail::CrossSetting(Scene.Settings, std::string_view(Key.str()), Entry);
            if (Crossed == GasSceneDetail::Crossing::Unknown)
                Result.UnknownSettings.emplace_back(Key.str());
            else if (Crossed == GasSceneDetail::Crossing::WrongType)
                Result.RefusedSettings.emplace_back(Key.str());
        }
    }

    if (!Result.RefusedSettings.empty())
    {
        Result.Refusal = "settings of the wrong type: " + Result.RefusedSettings.front();
        if (Result.RefusedSettings.size() > 1u)
            Result.Refusal += " and " + std::to_string(Result.RefusedSettings.size() - 1u) + " more";
        return Result;
    }

    OutScene = std::move(Scene);
    Result.Accepted = true;
    return Result;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                       WRITING
//----------------------------------------------------------------------------------------------------------------------

inline std::string WriteGasScene(const GasScene& Scene) noexcept
{
    std::string Out;
    Out.reserve(4096u);
    Out += "# Frontier gas scene. Written by Experimental/Fluid; read by Engine/VolumetricDynamics.\n";
    Out += "# Settings are sorted by name so the file diffs cleanly; order carries no meaning.\n\n";
    Out += "format = \"";
    Out += GasSceneFormat;
    Out += "\"\nversion = 1\nname = \"";
    Out += Scene.Name;
    Out += "\"\n\n[names]\n";
    Out += "collider = \"" + Scene.Names.Collider + "\"\n";
    Out += "domain = \"" + Scene.Names.Domain + "\"\n";
    Out += "emitter = \"" + Scene.Names.Emitter + "\"\n";
    Out += "sun = \"" + Scene.Names.Sun + "\"\n\n";

    if (Scene.CameraPresent)
    {
        Out += "[camera]\n";
        Out += "theta = "    + GasSceneDetail::SpellFloat(Scene.Camera.Theta)    + "\n";
        Out += "phi = "      + GasSceneDetail::SpellFloat(Scene.Camera.Phi)      + "\n";
        Out += "distance = " + GasSceneDetail::SpellFloat(Scene.Camera.Distance) + "\n";
        Out += "centre = [" + GasSceneDetail::SpellFloat(Scene.Camera.Centre[0]) + ", "
                            + GasSceneDetail::SpellFloat(Scene.Camera.Centre[1]) + ", "
                            + GasSceneDetail::SpellFloat(Scene.Camera.Centre[2]) + "]\n\n";
    }

    Out += "[settings]\n";
    GasSceneDetail::SpellSettings(Scene.Settings, Out);
    return Out;
}

} // namespace Frontier
