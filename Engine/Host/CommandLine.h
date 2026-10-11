//============================================================================================================================================
//                                   📦 Projects/Project-Zero/Source/CommandLine.h — P4
//============================================================================================================================================
// 🧩 The Unreal-shaped launch line the plan asked for, parsed once and shared by both hosts:
//
//      SlateEditor -Project=Project-Zero -Level=Showroom -Build=Editor -Config=Development +Location=(0,-6.0,2.6)
//      SlateGame   -Project=Project-Zero -Level=Showroom -Config=Development -game -silent
//      PackProject -Project=Project-Zero -Pack -Embed=Fonts
//
// 🔑 It is a PARSER LAYER, not a new app (plan §6). What it produces is a value type the hosts read; the compatibility
//    aliases (`--scene file.gltf`, `--scale n`) are parsed into the SAME fields, which is what keeps the CPU proofs in
//    Exhibits/ running unchanged while the format lands.
//
// ⚠️ The `+`-prefixed trailing arguments take no `-` name and stay LAST, as written — `+Location=(0,-6.0,2.6)` is a
//    positional override applied after the level loads, so it is stored as a transform override rather than as a flag.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{
    enum class CommandBuild : uint8_t { Editor = 0u, Game = 1u };
    enum class CommandConfig : uint8_t { Debug = 0u, Development = 1u, Shipping = 2u };

    struct CommandOptions
    {
        // ── the project ─────────────────────────────────────────────────────────────────────────────────────────────
        std::string Project;        // -Project=<Name|path.projectspace>   (empty = the host's default)
        std::string ProjectPath;    // resolved: Projects/<Name>/<Name>.projectspace, or the path as given
        std::string Level;          // -Level=<Name>  (empty = the level the project marks default)
        uint32_t    LevelExplicit = 0u;   // [-]  1 when -Level was given: the host then refuses an unknown level BY NAME

        // ── how it runs ─────────────────────────────────────────────────────────────────────────────────────────────
        CommandBuild  Build  = CommandBuild::Game;
        CommandConfig Config = CommandConfig::Development;
        bool          Silent = false;      // -silent
        bool          Game   = false;      // -game   (explicit, for a tool that launches the game host)

        // ── the family tools (plan §5) ──────────────────────────────────────────────────────────────────────────────
        bool          Pack = false;        // -Pack
        bool          Explode = false;     // -Explode
        bool          BakeSky = false;     // -Bake=Sky
        std::string   Export;              // -Export[=All|<Level>]  (empty = not requested; "*" = All)
        std::vector<std::string> Embed;    // -Embed=Fonts,Assets,Engine (repeatable)

        // ── the camera override: applied AFTER the level loads, so "play from here" is reproducible ─────────────────
        float    Location[3] = { 0.0f, 0.0f, 0.0f };   // [m]
        float    Rotation[3] = { 0.0f, 0.0f, 0.0f };   // [deg] pitch, yaw, roll
        bool     HasLocation = false;
        bool     HasRotation = false;

        // ── compatibility, for one phase (plan §6's last paragraph) ────────────────────────────────────────────────
        std::string SceneAlias;     // --scene <file.gltf>
        float       Scale = 1.0f;   // --scale <n>
        bool        HasScale = false;

        // ── what the host needs to know about the parse ────────────────────────────────────────────────────────────
        bool        WantsHelp = false;      // -h / --help / -?
        bool        Recognised = true;      // false when an unknown flag was seen (the host decides whether that is fatal)
        std::vector<std::string> Unknown;   // the flags that were not recognised, in order

        // The pack policy bits this command line asks for, as SpaceFormat.h's SpacePackPolicy.
        [[nodiscard]] uint32_t PackPolicy() const noexcept;
        [[nodiscard]] bool     WantsProject() const noexcept { return !Project.empty() || !ProjectPath.empty(); }
        [[nodiscard]] std::string Describe() const noexcept;   // one line, for the log
    };

    // Parses argv[1..]. `DefaultProject` is the host's fallback when -Project is absent ("" = none), and
    //    `ContentRoot` is where `Projects/<Name>/` lives. Never throws; never exits — the hosts own that decision.
    [[nodiscard]] bool ParseCommandLine(int ArgumentCount, char** Arguments, const std::string& DefaultProject,
                                        const std::string& ContentRoot, CommandOptions& Out,
                                        std::string& OutError) noexcept;

    // "+Location=(0,-6.0,2.6)" / "+Rotation=(0,220,0)" — exposed because the gate parses them directly.
    [[nodiscard]] bool ParsePlusVector(const std::string& Argument, const char* Name, float Out[3]) noexcept;
}
