//==============================================================================================================================================
//                                                           GASSCENECODEC.H
//==============================================================================================================================================
// 📦 Reads and writes a gas scene as TOML — the format the engine uses, and the one the browser authoring tool exports for it.
//
// 🔴 TOML, NOT JSON, AND THE REASON IS NOT TASTE.
//    Frontier already carries toml++ and already configures itself from TOML; `Slate.config.toml` is read by
//    ConfigurationRegistry.cpp with the same include this file uses. A gas scene arriving as JSON would be the
//    only JSON in the build and would need a second parser brought in to read it. The browser keeps writing
//    .fluid.json because a web page already has JSON.parse and has no other consumer — but that file does not
//    cross. `Experimental/Fluid/src/SceneTomlCodec.js` writes the TOML this reads, from the same scene object
//    the JSON path writes, so the two can never describe different scenes.
//
// ⚠️ TOML DISTINGUISHES AN INTEGER FROM A FLOAT, AND SO DOES GasSettings.
//    `gridResolution = 64` is an integer and `exposure = 1.0` is a float, and toml++ will hand back exactly
//    what was written. A writer that emits `1` for an exposure produces a file this refuses — correctly, and
//    avoidably, which is why the browser emitter is checked on its own side as well. Reading is tolerant in
//    one direction only: an integer is accepted where a float is wanted, because 1 and 1.0 denote the same
//    quantity, but a float is never accepted where a count is wanted, because 64.5 voxels is not a lattice.
//
// 💡 UNKNOWN KEYS ARE REPORTED, NOT REFUSED.
//    A scene written by a newer authoring tool carries settings this build has never heard of. Refusing it
//    makes the corpus unopenable the moment anyone adds a setting; ignoring it silently loses work without
//    saying so. Both are worse than reading what is understood and reporting the rest by name.
//
// The scene is the unit that crosses: settings, the names the outliner shows, and a camera. A preset is
//    tuning; a scene is tuning plus a framing plus names, which is what actually has to survive the crossing.

#pragma once

#include "GasPresetLibrary.h"

#include <toml++/toml.hpp>

#include <charconv>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE SCENE
//------------------------------------------------------------------------------------------------------------------------

constexpr std::string_view GasSceneFormat    = "frontier-fluid-scene";
constexpr int64_t          GasSceneVersion   = 1;
constexpr std::string_view GasSceneExtension = ".gasscene.toml";


// Where the viewport sits. Spherical about a centre, matching the authoring tool's orbit camera, because a
//    matrix would not survive a human editing the file and the whole point of TOML is that one can.
struct GasSceneCamera
{
    double Theta    = 0.0;                      // [rad] - azimuth
    double Phi      = 1.3;                      // [rad] - polar, clamped away from the poles by the tool
    double Distance = 3.0;                      // [-]   - orbit radius, in domain widths
    double Centre[3] = { 0.0, 0.0, 0.0 };       // [-]   - what it orbits, in domain-relative units
};


// The names the outliner shows. Four, because the authoring tool names exactly four objects; a scene with
//    more objects is a later version of this format and will say so in its version number.
struct GasSceneNames
{
    std::string Domain   = "Gas domain";
    std::string Emitter  = "Fire emitter";
    std::string Collider = "Sphere collider";
    std::string Sun      = "Directional light";
};


struct GasScene
{
    std::string    Name = "Untitled scene";
    GasSettings    Settings;
    GasSceneNames  Names;
    GasSceneCamera Camera;
    bool           CameraPresent = false;       // [-] - a scene may decline to frame itself
};


// Why a read failed, or what it survived. A refusal carries a sentence a human can act on; a success may
//    still carry names, which is the unknown-key case from the file header.
struct GasSceneReading
{
    bool                     Accepted = false;
    std::string              Refusal;           // [-] - empty when accepted
    std::vector<std::string> UnknownSettings;   // [-] - present in the file, absent from this build
    std::vector<std::string> RefusedSettings;   // [-] - present and understood, but of the wrong type
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     READING SETTINGS
//------------------------------------------------------------------------------------------------------------------------

namespace GasSceneDetail {

// An integer is accepted where a float is wanted: 1 and 1.0 denote the same quantity and a writer emitting
//    the former is being terse, not wrong.
inline bool ReadFloat(const toml::node& Node, float& Target) noexcept
{
    if (const auto Floating = Node.value<double>(); Floating && Node.is_number())
    {
        Target = static_cast<float>(*Floating);
        return true;
    }
    return false;
}

// A float is never accepted where a count is wanted. 64.5 voxels is not a lattice, and silently truncating it
//    hides an authoring mistake behind a plausible result.
inline bool ReadCount(const toml::node& Node, int32_t& Target) noexcept
{
    if (!Node.is_integer()) return false;
    const auto Whole = Node.value<int64_t>();
    if (!Whole) return false;
    Target = static_cast<int32_t>(*Whole);
    return true;
}

inline bool ReadSwitch(const toml::node& Node, bool& Target) noexcept
{
    if (!Node.is_boolean()) return false;
    Target = *Node.value<bool>();
    return true;
}

}   // namespace GasSceneDetail

// 📝 Generated. Tools/Build/GenerateGasSceneCodec.py writes the two bodies below from presets.js, so the 85
//    settings are named in one place and crossed in another without either being hand-maintained.
#include "GasSceneFields.inl"

//------------------------------------------------------------------------------------------------------------------------
//                                                        READING
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Parses a gas scene from TOML text.
/// in    Text       [-]  the whole file
/// in    OutScene   [-]  filled on acceptance; untouched otherwise, so a failed read cannot half-apply
/// out   GasSceneReading  [-]  acceptance, a refusal sentence, and any settings skipped or refused
/// err   a parse error, a wrong format marker, a wrong version or a missing name are all refusals; an
///       unrecognised setting is not, and is reported by name instead
/// note  settings absent from the file keep their default, which is what lets a scene written against an
///       older build still open
/// cost  🚩  one parse of the text
/// tag   api, allocating, nonthrowing
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
        for (const auto& [Key, Node] : *Settings)
        {
            const GasSceneDetail::Crossing Crossed =
                GasSceneDetail::CrossSetting(Scene.Settings, std::string_view(Key.str()), Node);
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

//------------------------------------------------------------------------------------------------------------------------
//                                                        WRITING
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Writes a gas scene as the TOML the authoring tool also writes — byte for byte, which is what makes the
///    round-trip a real check rather than a formality.
/// in    Scene        [-]  the scene
/// out   std::string  [-]  the whole file, ending in one newline
/// note  settings are emitted sorted by name, for the reason the browser emitter sorts them: a file whose
///       line order follows a traversal diffs differently depending on who wrote it, and the corpus is
///       version controlled
/// cost  🚩
/// tag   api, allocating, nonthrowing
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
    // 📝 No blank line after the settings. SpellSettings already terminates its last line, and the browser
    //    emitter's trailing empty element contributes nothing after it, so a second newline here is one more
    //    than the authoring tool writes — which the byte comparison catches and nothing else would.
    GasSceneDetail::SpellSettings(Scene.Settings, Out);
    return Out;
}

}   // namespace Frontier
