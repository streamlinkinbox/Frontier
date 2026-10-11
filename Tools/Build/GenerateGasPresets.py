#!/usr/bin/env python3
"""Transcribe the browser simulator's settings and presets into the engine header, or verify the transcription.

    python3 Tools/Build/GenerateGasPresets.py            # write Engine/VolumetricDynamics/GasPresetLibrary.h
    python3 Tools/Build/GenerateGasPresets.py --check    # exit 1 if the written header is stale

Experimental/Fluid/src/presets.js stays the single source of truth for tuning, exactly as CMakeLists.txt stays
the single source of truth for the shader list. The engine does not get a second copy that drifts: it gets a
generated one, and --check fails the build when it no longer matches. That is the same arrangement
CompileShaders.py uses against the two shader lists, and for the same reason -- two hand-maintained copies of
the same numbers have never once stayed equal.

The two blocks in presets.js are strict JSON, so this reads them without evaluating any JavaScript.
"""
import argparse
import json
import re
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Source = Root / 'Experimental/Fluid/src/presets.js'
Target = Root / 'Engine/VolumetricDynamics/GasPresetLibrary.h'

# ── Identifiers ────────────────────────────────────────────────────────────────────────────────────
# SKILL-Naming bars Grid, Atlas and Map as structural words, so the six settings carrying them are
# renamed rather than transliterated. Turntable keeps its spelling: the ban is on Table as a lookup
# structure, and a turntable is a physical mechanism, which the mathematical vocabulary clause admits.
Renamed = {
    'gridResolution':     'LatticeResolution',
    'showVoxelGridLines': 'VoxelLatticeLinesShown',
    'showFloorGrid':      'FloorLatticeShown',
    'showAtlasMinimap':   'TileSheetOverviewShown',
    'atlasMinimapField':  'TileSheetOverviewReading',
}

# Booleans are never prefixed with is/has/can; a JavaScript show* becomes a noun phrase ending in Shown.
def Identifier(Key):
    if Key in Renamed:
        return Renamed[Key]
    Name = Key[0].upper() + Key[1:]
    Match = re.match(r'^Show(.+)$', Name)
    if Match:
        return Match.group(1) + 'Shown'
    return Name


# ── Units ──────────────────────────────────────────────────────────────────────────────────────────
# Every inline comment must carry a bracketed unit. Most are dimensionless tuning weights; the ones that
# are not are named here, because a wrong unit in a comment is worse than no comment.
Units = {
    'LatticeResolution':      'voxels', 'PressureIterations':   '-',    'TimeScale':            '-',
    'BoundsWidth':            'm',      'BoundsHeight':         'm',    'DynamicBoundsMax':     'm',
    'VorticityConfinement':   '-',      'Buoyancy':             'm/s2', 'SmokeWeight':          'm/s2',
    'BurnRate':               '1/s',    'BurnHeat':             'K',    'SootGeneration':       '1/s',
    'CombustionExpansion':    '-',      'CoolingRate':          '1/s',  'SmokeDissipation':     '1/s',
    'VelocityDamping':        '1/s',    'TurbulenceStrength':   'm/s',  'TurbulenceScale':      '1/m',
    'WindX':                  'm/s',    'WindZ':                'm/s',  'EmitterRate':          '1/s',
    'EmitterRadius':          '-',      'EmitterHeight':        '-',    'EmitterUpwardVelocity': 'm/s',
    'EmitterSwirl':           '-',      'EmitterTemperature':   'K',    'EmitterFuel':          '-',
    'EmitterSmoke':           '-',      'BlastStrength':        'm/s',  'BlastRadius':          '-',
    'BlastTemperature':       'K',      'BlastFuel':            '-',    'BlastSmoke':           '-',
    'BlastLobes':             '-',      'RaymarchSteps':        '-',    'ShadowSteps':          '-',
    'DensityExtinction':      '1/m',    'SmokeAlbedo':          '-',    'ShadowDensity':        '1/m',
    'FireIntensity':          '-',      'TemperatureScale':     '-',    'InternalScattering':   '-',
    'PhaseAnisotropy':        '-',      'AmbientIntensity':     '-',    'SunIntensity':         '-',
    'SunAzimuth':             'deg',    'SunElevation':         'deg',  'Exposure':             '-',
    'BloomIntensity':         '-',      'GodRaysIntensity':     '-',    'ShockwaveStrength':    '-',
    'SliceAxis':              '-',      'SlicePosition':        '-',    'ObstacleType':         '-',
    'ObstacleX':              '-',      'ObstacleY':            '-',    'ObstacleZ':            '-',
    'ObstacleRadius':         '-',      'ColliderSpeed':        'm/s',  'EmberCount':           '-',
    'EmberSize':              '-',      'EmberIntensity':       '-',    'EmberLifetime':        's',
    'EmberAshiness':          '-',      'RenderScale':          '-',    'RenderChannel':        '-',
    'ColorPalette':           '-',      'VoxelQuantization':    '-',    'TileSheetOverviewReading': '-',
}

# Settings whose spelled range is a fraction of the domain rather than a world measurement. The browser
# splats these in normalised coordinates, so widening the bounds requires shrinking them proportionally --
# the trap that was found and fixed while enlarging the explosion bounds.
Normalised = {'EmitterRadius', 'EmitterHeight', 'BlastRadius', 'ObstacleX', 'ObstacleY', 'ObstacleZ',
              'ObstacleRadius', 'SlicePosition'}


def ReadBlock(Text, Name):
    Marker = 'export const %s = ' % Name
    Start = Text.index(Marker) + len(Marker)
    Depth = 0
    for Cursor in range(Start, len(Text)):
        if Text[Cursor] in '{[':
            Depth += 1
        elif Text[Cursor] in '}]':
            Depth -= 1
            if Depth == 0:
                return json.loads(Text[Start:Cursor + 1])
    raise SystemExit('unterminated %s block in %s' % (Name, Source))


def Spell(Reading, Declared):
    if Declared == 'bool':
        return 'true' if Reading else 'false'
    if Declared == 'const char*':
        return '"%s"' % Reading
    if Declared == 'int32_t':
        return '%d' % int(Reading)
    Text = repr(float(Reading))
    return Text + 'f' if ('.' in Text or 'e' in Text) else Text + '.0f'


# Settings that are counts or enumerated selections, and are therefore integers even though every reading
# of them happens to be whole. Everything else that is numeric is a float, because a setting an artist drags
# is continuous whether or not a preset currently lands on a round number.
Counted = {
    'LatticeResolution', 'PressureIterations', 'BlastLobes', 'RaymarchSteps', 'ShadowSteps', 'EmberCount',
    'SliceAxis', 'ObstacleType', 'RenderChannel', 'ColorPalette', 'VoxelQuantization',
    'TileSheetOverviewReading',
}


def Declare(Name, Readings):
    """The type covering every reading of this setting, across the defaults and all eighteen presets.

    Inferring from the default alone is the trap here: timeScale defaults to 1 and would be typed as an
    integer, and the presets that set it to 0.55 would then be silently truncated to zero.
    """
    if any(isinstance(Reading, bool) for Reading in Readings):
        return 'bool'
    if any(isinstance(Reading, str) for Reading in Readings):
        return 'const char*'
    return 'int32_t' if Name in Counted else 'float'


def Escape(Text):
    return Text.replace('\\', '\\\\').replace('"', '\\"')


def Compose():
    Text = Source.read_text(encoding='utf-8')
    Defaults = ReadBlock(Text, 'DEFAULT_PARAMS')
    Presets = ReadBlock(Text, 'PRESETS')

    # Every reading each setting ever takes, so the declared type covers all of them.
    Seen = {Key: [Reading] for Key, Reading in Defaults.items()}
    for Record in Presets.values():
        for Key, Reading in Record.get('params', {}).items():
            if Key not in Seen:
                raise SystemExit('a preset sets %s, which is not a declared setting' % Key)
            Seen[Key].append(Reading)

    Declared = {Key: Declare(Identifier(Key), Readings) for Key, Readings in Seen.items()}
    Fields = [(Identifier(Key), Key, Reading, Declared[Key]) for Key, Reading in Defaults.items()]
    NameWidth = max(len(Name) for Name, _, _, _ in Fields)
    TypeWidth = max(len(Type) for _, _, _, Type in Fields)
    SpellWidth = max(len(Spell(Reading, Type)) for _, _, Reading, Type in Fields)

    Out = []
    Add = Out.append
    Ruler = '//' + '=' * 142
    Banner = '//' + '-' * 120

    Add(Ruler)
    Add('//' + 'GASPRESETLIBRARY.H'.center(142))
    Add(Ruler)
    Add('// 📦 GENERATED. The browser simulator\'s %d settings and %d presets, transcribed for the engine; '
        'edit presets.js, never this file.' % (len(Fields), len(Presets)))
    Add('//')
    Add('// 🔴 DO NOT EDIT. Tools/Build/GenerateGasPresets.py writes this file from')
    Add('//    Experimental/Fluid/src/presets.js, and --check fails the build when the two disagree. A hand')
    Add('//    edit here is erased by the next generation and, worse, silently diverges the native plume from')
    Add('//    the browser one it was tuned against until someone notices the two no longer look alike.')
    Add('//')
    Add('// The browser file stays authoritative because that is where the tuning is actually done -- with a')
    Add('//    viewport, sliders and an immediate picture. Transcribing rather than re-authoring is what makes')
    Add('//    the scene round-trip in step 4 of the port a real check instead of a formality.')
    Add('//')
    Add('// ⚠️ Settings marked [-] normalised are fractions of the domain, not world measurements. The browser')
    Add('//    splats them in normalised coordinates, so enlarging the bounds without shrinking these shrinks')
    Add('//    the effect in world terms -- the defect found and fixed while widening the explosion bounds.')
    Add('')
    Add('#pragma once')
    Add('')
    Add('#include <cstdint>')
    Add('#include <cstring>')
    Add('')
    Add('namespace Frontier {')
    Add('')
    Add(Banner)
    Add('//' + 'THE SETTINGS'.center(120))
    Add(Banner)
    Add('')
    Add('// One gas effect, fully described. Field order follows presets.js so the two read side by side.')
    Add('struct GasSettings')
    Add('{')
    for Name, Key, Reading, Type in Fields:
        Unit = Units.get(Name, '-')
        Note = 'normalised fraction of the domain' if Name in Normalised else Key
        Add('    %-*s %-*s = %-*s   // [%s]%s- %s'
            % (TypeWidth, Type, NameWidth, Name, SpellWidth + 1, Spell(Reading, Type) + ';',
               Unit, ' ' * max(1, 8 - len(Unit)), Note))
    Add('};')
    Add('')
    Add('constexpr uint32_t GasSettingCount = %du;' % len(Fields))
    Add('')
    Add('')
    Add(Banner)
    Add('//' + 'THE PRESETS'.center(120))
    Add(Banner)
    Add('')
    Add('// A preset is an identity, two lines of presentation, and the settings it differs from the default')
    Add('//    in. The differences are applied over a default-constructed GasSettings, which is what lets a')
    Add('//    new setting arrive with a default without touching all %d presets.' % len(Presets))
    Add('struct GasPreset')
    Add('{')
    Add('    const char* Identity        = "";      // [-] - stable key; what a scene stores')
    Add('    const char* Name            = "";      // [-] - what the preset rail shows')
    Add('    const char* Description     = "";      // [-] - the line beneath it')
    Add('    bool        DetonateOnLoad  = false;   // [-] - the preset opens mid-explosion rather than idle')
    Add('};')
    Add('')
    Add('constexpr uint32_t GasPresetCount = %du;' % len(Presets))
    Add('')
    Add('inline const GasPreset* GasPresetLibrary() noexcept')
    Add('{')
    Add('    static const GasPreset Library[GasPresetCount] = {')
    for Identity, Record in Presets.items():
        Add('        { "%s", "%s",' % (Escape(Identity), Escape(Record.get('name', ''))))
        Add('          "%s",' % Escape(Record.get('description', '')))
        Add('          %s },' % ('true' if Record.get('triggerExplosionOnLoad') else 'false'))
    Add('    };')
    Add('    return Library;')
    Add('}')
    Add('')
    Add('')
    Add(Banner)
    Add('//' + 'CONSTRUCTING A PRESET'.center(120))
    Add(Banner)
    Add('')
    Add('/// 📦 The settings for one preset: the defaults above, with that preset\'s differences applied.')
    Add('/// in    Identity      [-]  stable preset key')
    Add('/// out   GasSettings   [-]  fully resolved; the defaults unchanged when the key is unknown')
    Add('/// err   an unknown key yields the defaults rather than refusing, so a scene naming a preset that has')
    Add('///       since been renamed still opens and still looks like something')
    Add('/// note  the differences are applied in the order presets.js lists them, which matters only for the')
    Add('///       reader, since no key is written twice')
    Add('/// cost  ✔️')
    Add('/// tag   api, generated, nonallocating, nonthrowing')
    Add('inline GasSettings ConstructPresetSettings(const char* Identity) noexcept')
    Add('{')
    Add('    GasSettings Resolved;')
    Add('    if (Identity == nullptr) return Resolved;')
    Add('')
    for Order, (Identity, Record) in enumerate(Presets.items()):
        Overrides = Record.get('params', {})
        Keyword = 'if' if Order == 0 else 'else if'
        Add('    %s (std::strcmp(Identity, "%s") == 0)' % (Keyword, Escape(Identity)))
        Add('    {')
        for Key, Reading in Overrides.items():
            Add('        Resolved.%s = %s;' % (Identifier(Key), Spell(Reading, Declared[Key])))
        if not Overrides:
            Add('        // this preset is the defaults exactly')
        Add('    }')
    Add('')
    Add('    return Resolved;')
    Add('}')
    Add('')
    Add('')
    Add('/// 📦 Settings for the preset at a position in the library, for a caller walking all of them.')
    Add('/// in    Slot          [-]  0 to GasPresetCount - 1')
    Add('/// out   GasSettings   [-]  the defaults when the slot is out of range')
    Add('/// cost  ✔️')
    Add('/// tag   api, generated, nonallocating, nonthrowing')
    Add('inline GasSettings ConstructPresetSettings(uint32_t Slot) noexcept')
    Add('{')
    Add('    if (Slot >= GasPresetCount) return GasSettings{};')
    Add('    return ConstructPresetSettings(GasPresetLibrary()[Slot].Identity);')
    Add('}')
    Add('')
    Add('}   // namespace Frontier')
    return '\n'.join(Out) + '\n'


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--check', action='store_true',
                        help='verify the written header matches presets.js; write nothing')
    Arguments = Parser.parse_args()

    Composed = Compose()
    if Arguments.check:
        if not Target.exists():
            print('STALE: %s has never been generated' % Target.relative_to(Root))
            return 1
        if Target.read_text(encoding='utf-8') != Composed:
            print('STALE: %s no longer matches %s' % (Target.relative_to(Root), Source.relative_to(Root)))
            print('       run: python3 Tools/Build/GenerateGasPresets.py')
            return 1
        print('%s is current against %s' % (Target.relative_to(Root), Source.relative_to(Root)))
        return 0

    Target.parent.mkdir(parents=True, exist_ok=True)
    Target.write_text(Composed, encoding='utf-8')
    print('wrote %s' % Target.relative_to(Root))
    return 0


if __name__ == '__main__':
    sys.exit(Main())
