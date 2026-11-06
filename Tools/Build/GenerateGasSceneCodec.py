#!/usr/bin/env python3
"""Write the per-setting crossing between TOML and GasSettings, from presets.js.

    python3 Tools/Build/GenerateGasSceneCodec.py            # write Frontier/Engine/VolumetricDynamics/GasSceneFields.inl
    python3 Tools/Build/GenerateGasSceneCodec.py --check    # exit 1 if the written file is stale
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from GenerateGasPresets import Declare, Identifier, ReadBlock, Source  # noqa: E402

Root = Path(__file__).resolve().parents[2]
Target = Root / 'Frontier/Engine/VolumetricDynamics/GasSceneFields.inl'


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
    Banner = '//' + '-' * 118

    Add('/*' + '=' * 138)
    Add('GASSCENEFIELDS.INL'.center(140))
    Add('=' * 138 + '*/')
    Add('// \U0001f9e9 GENERATED. Crosses the %d settings between TOML and GasSettings; edit presets.js, never this file.'
        % len(Defaults))
    Add('//')
    Add('// \U0001f534 DO NOT EDIT. Tools/Build/GenerateGasSceneCodec.py writes this from presets.js, and --check')
    Add('//    fails the build when the two disagree. Included by GasSceneCodec.h inside namespace Frontier.')
    Add('')
    Add('// clang-format off')
    Add('')
    Add('namespace GasSceneDetail')
    Add('{')
    Add('')
    Add(Banner)
    Add('//' + 'SPELLING A NUMBER'.center(118))
    Add(Banner)
    Add('')
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
    Add(Banner)
    Add('//' + 'CROSSING ONE SETTING IN'.center(118))
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
            Add('        return Node.is_string() ? Crossing::Read : Crossing::WrongType;')
        else:
            Add('        return ReadFloat(Node, Target.%s) ? Crossing::Read : Crossing::WrongType;' % Name)
    Add('    return Crossing::Unknown;')
    Add('}')
    Add('')
    Add(Banner)
    Add('//' + 'CROSSING EVERY SETTING OUT'.center(118))
    Add(Banner)
    Add('')
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
    Add('} // namespace GasSceneDetail')
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
            print('STALE: %s no longer matches %s' % (Target.relative_to(Root), Source.relative_to(Root)), file=sys.stderr)
            return 1
        print('%s matches %s.' % (Target.relative_to(Root), Source.relative_to(Root)))
        return 0

    Target.parent.mkdir(parents=True, exist_ok=True)
    Target.write_text(Composed, encoding='utf-8')
    print('Wrote %s' % Target.relative_to(Root))
    return 0


if __name__ == '__main__':
    sys.exit(Main())
