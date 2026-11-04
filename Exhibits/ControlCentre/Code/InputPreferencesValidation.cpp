/*==========================================================================================================================================
                                                      INPUTPREFERENCESVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify portable input maps, response mathematics, strict TOML decoding and non-destructive replacement.

#include "Engine/Interface/InputPreferencesCodec.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void EnforceCondition(bool Condition, const char *Description)
{
    if (!Condition)
        throw std::runtime_error(Description);
}

std::string DecodeText(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        EnforceCondition(ArgumentCount == 2, "Expected an evidence directory");
        const std::filesystem::path Directory(Arguments[1]);
        std::filesystem::create_directories(Directory);
        const auto       Path = Directory / std::filesystem::path(u8"入力設定.toml");
        InputPreferences Preferences;
        EnforceCondition(Preferences.EvaluatePointerDelta({10, 20}).X == 10, "Default response is not unity");
        Preferences.MouseSensitivity = 100;
        EnforceCondition(Preferences.EvaluatePointerDelta({10, 20}).Y == 40, "Maximum sensitivity differs");
        Preferences.InvertPitch = true;
        EnforceCondition(Preferences.EvaluatePointerDelta({10, 20}).Y == 40, "Disabled advanced controls changed inversion");
        Preferences.AdvancedControls = true;
        EnforceCondition(Preferences.EvaluatePointerDelta({10, 20}).Y == -40, "Applied inversion differs");
        Preferences.MouseSensitivity = 0;
        EnforceCondition(Preferences.EvaluatePointerDelta({10, 20}).X == 5, "Minimum sensitivity differs");
        for (uint32_t Index = 1; Index < static_cast<uint32_t>(InputKeyCategory::Count); ++Index)
        {
            const auto Key = static_cast<InputKeyCategory>(Index);
            EnforceCondition(InputChordTable::DecodeKeyName(InputChordTable::QueryKeyName(Key)) == Key,
                    "Unstable keyboard token");
        }
        Preferences.CustomShortcuts = true;
        Preferences.Shortcuts[0]    = {InputKeyCategory::K, true};
        std::string Diagnostic;
        EnforceCondition(InputPreferencesCodec::Encode(Path, Preferences, Diagnostic), "Input TOML save failed");
        const auto       Original = DecodeText(Path);
        InputPreferences Restored;
        EnforceCondition(InputPreferencesCodec::Decode(Path, Restored, Diagnostic) && Restored == Preferences,
                "Unicode input preference round-trip failed");
        auto Invalid             = Preferences;
        Invalid.MouseSensitivity = std::numeric_limits<float>::quiet_NaN();
        EnforceCondition(!InputPreferencesCodec::Encode(Path, Invalid, Diagnostic) && DecodeText(Path) == Original,
                "Invalid sensitivity destroyed saved preferences");
        Invalid              = Preferences;
        Invalid.Shortcuts[1] = Invalid.Shortcuts[0];
        EnforceCondition(!InputPreferencesCodec::Encode(Path, Invalid, Diagnostic) && DecodeText(Path) == Original,
                "Duplicate chord overwrote saved preferences");
        Invalid.Shortcuts[1] = {InputKeyCategory::Escape};
        EnforceCondition(!InputChordTable::Validate(Invalid, Diagnostic), "Reserved Escape command was accepted");
        Invalid.Shortcuts[1] = {InputKeyCategory::Count};
        EnforceCondition(!InputChordTable::Validate(Invalid, Diagnostic), "Unsupported keyboard ordinal accepted");
        const auto Malformed = Directory / "Malformed.toml";
        const auto Refuse    = [&](std::string Text)
        {
            std::ofstream(Malformed) << Text;
            auto Candidate = Preferences;
            EnforceCondition(!InputPreferencesCodec::Decode(Malformed, Candidate, Diagnostic) && Candidate == Preferences,
                    "Malformed input mutated live preferences");
        };
        auto Replaced = Original;
        Replaced.replace(Replaced.find("format = 1"), 10, "format = 2");
        Refuse(Replaced);
        Replaced = Original;
        Replaced.replace(Replaced.find("custom_shortcuts = true"), 23, "custom_shortcuts = 'yes'");
        Refuse(Replaced);
        Replaced = Original;
        Replaced.replace(Replaced.find("ControlCentre"), 13, "UnknownCommand");
        Refuse(Replaced);
        Replaced = Original;
        Replaced.replace(Replaced.find("Typography"), 10, "ControlCentre");
        Refuse(Replaced);
        Replaced = Original;
        Replaced.replace(Replaced.find("key = 'K'"), 9, "key = 'Unrecognised'");
        Refuse(Replaced);
        Refuse("format = 1\nshortcuts = []\n");
        const auto Occupied = Directory / "Occupied.toml";
        std::filesystem::create_directories(Occupied);
        EnforceCondition(!InputPreferencesCodec::Encode(Occupied, Preferences, Diagnostic) &&
                    std::filesystem::is_directory(Occupied) && !std::filesystem::exists(Occupied.string() + ".tmp"),
                "Failed replacement damaged the destination or leaked its temporary file");
        EnforceCondition(DecodeText(Path) == Original, "Failure tests damaged the valid preference file");
        std::cout << "PASS: portable tokens, pointer gain/inversion, Unicode TOML round-trip, strict schema, "
                  << "duplicate/reserved chords, invalid-save preservation and replacement failure cleanup\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
