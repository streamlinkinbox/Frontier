#!/usr/bin/env python3
"""Write the per-setting crossing between TOML and GasSettings, from presets.js.

    python3 Tools/Build/GenerateGasSceneCodec.py            # write Engine/VolumetricDynamics/GasSceneFields.inl
    python3 Tools/Build/GenerateGasSceneCodec.py --check    # exit 1 if the written file is stale

The 85 settings are named in presets.js, declared in GasPresetLibrary.h and crossed here. Only the first of
those is hand-maintained; the other two are generated from it by this script and GenerateGasPresets.py, so a
setting is added in one place and arrives everywhere.

The emitted writer must agree with Experimental/Fluid/src/SceneTomlCodec.js byte for byte, because the
round-trip check compares the two files directly rather than comparing parsed structures. Comparing
structures would pass while the two emitters disagreed about how to spell a number, and the disagreement
would only surface as a corpus that churns every time a different tool saves it.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from GenerateGasPresets import Counted, Declare, Identifier, ReadBlock, Source  # noqa: E402

Root = Path(__file__).resolve().parents[2]
Target = Root / 'Engine/VolumetricDynamics/GasSceneFields.inl'


def Compose():
    Text = Source.read_text(encoding='utf-8')
    Defaults = ReadBlock(Text, 'DEFAULT_PARAMS')
    Presets = ReadBlock(Text, 'PRESETS')

    Seen = {Key: [Reading] for Key, Reading in Defaults.items()}
    for Record in Presets.values():
        for Key, Reading in Record.get('params', {}).items():
            Seen[Key].append(Reading)
    Declared = {Key: Declare(Identifier(Key), Readings) for Key, Readings in Seen.items()}

    Out = []
    Add = Out.append
    Ruler = '//' + '=' * 142
    Banner = '//' + '-' * 120

    Add(Ruler)
    Add('//' + 'GASSCENEFIELDS.INL'.center(142))
    Add(Ruler)
    Add('// 📦 GENERATED. Crosses the %d settings between TOML and GasSettings; edit presets.js, never this file.'
        % len(Defaults))
    Add('//')
    Add('// 🔴 DO NOT EDIT. Tools/Build/GenerateGasSceneCodec.py writes this from presets.js, and --check')
    Add('//    fails the build when the two disagree. Included by GasSceneCodec.h inside namespace Frontier.')
    Add('//')
    Add('// The spelling of a number here must match SceneTomlCodec.js exactly, because the round-trip check')
    Add('//    compares the two emitted files rather than their parsed contents. Comparing contents would pass')
    Add('//    while the two disagreed about how to write a number, and the corpus would churn in version')
    Add('//    control every time a different tool saved it.')
    Add('')
    Add('// clang-format off')
    Add('')
    Add('namespace GasSceneDetail {')
    Add('')
    Add(Banner)
    Add('//' + 'SPELLING A NUMBER'.center(120))
    Add(Banner)
    Add('')
    Add('// Shortest representation that reads back as the same number — the same rule JavaScript\'s Number')
    Add('//    printing follows, which is why the two emitters agree. A trailing ".0" is appended when the')
    Add('//    shortest form has no point in it, because TOML would otherwise read a float back as an integer.')
    Add('template <typename Number>')
    Add('inline std::string SpellShortest(Number Reading, bool AsFloat) noexcept')
    Add('{')
    Add('    char Buffer[64];')
    Add('    const auto Written = std::to_chars(Buffer, Buffer + sizeof(Buffer), Reading);')
    Add('    if (Written.ec != std::errc{}) return AsFloat ? "0.0" : "0";')
    Add('    std::string Spelt(Buffer, Written.ptr);')
    Add('    if (AsFloat && Spelt.find(\'.\') == std::string::npos && Spelt.find(\'e\') == std::string::npos)')
    Add('        Spelt += ".0";')
    Add('    return Spelt;')
    Add('}')
    Add('')
    Add('inline std::string SpellFloat(double Reading) noexcept  { return SpellShortest(Reading, true); }')
    Add('inline std::string SpellFloat(float Reading) noexcept   { return SpellShortest(Reading, true); }')
    Add('inline std::string SpellCount(int32_t Reading) noexcept  { return std::to_string(Reading); }')
    Add('')
    Add('')
    Add(Banner)
    Add('//' + 'CROSSING ONE SETTING IN'.center(120))
    Add(Banner)
    Add('')
    Add('enum class Crossing : uint32_t')
    Add('{')
    Add('    Read      = 0u,   // [-] - understood and applied')
    Add('    Unknown   = 1u,   // [-] - this build has never heard of it; reported, not refused')
    Add('    WrongType = 2u,   // [-] - understood, but the file spells it as something it is not')
    Add('};')
    Add('')
    Add('inline Crossing CrossSetting(GasSettings& Target, std::string_view Key, const toml::node& Node) noexcept')
    Add('{')

    First = True
    for Key in Defaults:
        Name = Identifier(Key)
        Type = Declared[Key]
        Keyword = 'if' if First else 'else if'
        First = False
        Add('    %s (Key == "%s")' % (Keyword, Key))
        if Type == 'bool':
            Add('        return ReadSwitch(Node, Target.%s) ? Crossing::Read : Crossing::WrongType;' % Name)
        elif Type == 'int32_t':
            Add('        return ReadCount(Node, Target.%s) ? Crossing::Read : Crossing::WrongType;' % Name)
        elif Type == 'const char*':
            # A string setting is carried but not owned: GasSettings holds a pointer into the preset library,
            # and a scene's own string would have to outlive the read. Accepted and ignored, reported as read.
            Add('        return Node.is_string() ? Crossing::Read : Crossing::WrongType;')
        else:
            Add('        return ReadFloat(Node, Target.%s) ? Crossing::Read : Crossing::WrongType;' % Name)
    Add('    return Crossing::Unknown;')
    Add('}')
    Add('')
    Add('')
    Add(Banner)
    Add('//' + 'CROSSING EVERY SETTING OUT'.center(120))
    Add(Banner)
    Add('')
    Add('// Sorted by name, matching the browser emitter. Not the declaration order, deliberately.')
    Add('inline void SpellSettings(const GasSettings& Settings, std::string& Out) noexcept')
    Add('{')
    for Key in sorted(Defaults):
        Name = Identifier(Key)
        Type = Declared[Key]
        if Type == 'bool':
            Add('    Out += "%s = "; Out += Settings.%s ? "true" : "false"; Out += "\\n";' % (Key, Name))
        elif Type == 'int32_t':
            Add('    Out += "%s = " + SpellCount(Settings.%s) + "\\n";' % (Key, Name))
        elif Type == 'const char*':
            Add('    Out += "%s = \\""; Out += Settings.%s; Out += "\\"\\n";' % (Key, Name))
        else:
            Add('    Out += "%s = " + SpellFloat(Settings.%s) + "\\n";' % (Key, Name))
    Add('}')
    Add('')
    Add('}   // namespace GasSceneDetail')
    Add('')
    Add('// clang-format on')
    return '\n'.join(Out) + '\n'


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--check', action='store_true', help='verify the written file; write nothing')
    Arguments = Parser.parse_args()

    Composed = Compose()
    if Arguments.check:
        if not Target.exists() or Target.read_text(encoding='utf-8') != Composed:
            print('STALE: %s no longer matches %s' % (Target.relative_to(Root), Source.relative_to(Root)))
            print('       run: python3 Tools/Build/GenerateGasSceneCodec.py')
            return 1
        print('%s is current against %s' % (Target.relative_to(Root), Source.relative_to(Root)))
        return 0

    Target.write_text(Composed, encoding='utf-8')
    print('wrote %s' % Target.relative_to(Root))
    return 0


if __name__ == '__main__':
    sys.exit(Main())
