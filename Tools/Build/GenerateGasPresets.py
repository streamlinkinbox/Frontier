#!/usr/bin/env python3
"""Transcribe the browser simulator's settings and presets into the engine header, or verify the transcription.

    python3 Tools/Build/GenerateGasPresets.py            # write Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h
    python3 Tools/Build/GenerateGasPresets.py --check    # exit 1 if the written header is stale
"""
import argparse
import json
import re
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Source = Root / 'Experimental/Fluid/src/presets.js'
Target = Root / 'Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h'

Renamed = {
    'gridResolution':     'LatticeResolution',
    'showVoxelGridLines': 'VoxelLatticeLinesShown',
    'showFloorGrid':      'FloorLatticeShown',
    'showAtlasMinimap':   'TileSheetOverviewShown',
    'atlasMinimapField':  'TileSheetOverviewReading',
}


def Identifier(Key):
    if Key in Renamed:
        return Renamed[Key]
    Name = Key[0].upper() + Key[1:]
    Match = re.match(r'^Show(.+)$', Name)
    if Match:
        return Match.group(1) + 'Shown'
    return Name


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
    'ObstacleX':              '-',      'ObstacleY':            '-',      'ObstacleZ':            '-',
    'ObstacleRadius':         '-',      'ColliderSpeed':        'm/s',  'EmberCount':           '-',
    'EmberSize':              '-',      'EmberIntensity':       '-',    'EmberLifetime':        's',
    'EmberAshiness':          '-',      'RenderScale':          '-',    'RenderChannel':        '-',
    'ColorPalette':           '-',      'VoxelQuantization':    '-',    'TileSheetOverviewReading': '-',
}

Normalised = {
    'EmitterRadius', 'EmitterHeight', 'BlastRadius', 'ObstacleX', 'ObstacleY', 'ObstacleZ',
    'ObstacleRadius', 'SlicePosition',
}


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


Counted = {
    'LatticeResolution', 'PressureIterations', 'BlastLobes', 'RaymarchSteps', 'ShadowSteps', 'EmberCount',
    'SliceAxis', 'ObstacleType', 'RenderChannel', 'ColorPalette', 'VoxelQuantization',
    'TileSheetOverviewReading',
}


def Declare(Name, Readings):
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
    Banner = '//' + '-' * 118

    Add('#pragma once')
    Add('/*' + '=' * 138)
    Add('GASPRESETLIBRARY.H'.center(140))
    Add('=' * 138 + '*/')
    Add('// \U0001f9e9 GENERATED. The browser simulator\'s %d settings and %d presets, transcribed for the engine; '
        'edit presets.js, never this file.' % (len(Fields), len(Presets)))
    Add('//')
    Add('// \U0001f534 DO NOT EDIT. Tools/Build/GenerateGasPresets.py writes this file from')
    Add('//    Experimental/Fluid/src/presets.js, and --check fails the build when the two disagree.')
    Add('//')
    Add('// \u26a0\ufe0f Settings marked [-] normalised are fractions of the domain, not world measurements.')
    Add('')
    Add('#include <cstdint>')
    Add('#include <cstring>')
    Add('')
    Add('namespace Frontier')
    Add('{')
    Add('')
    Add(Banner)
    Add('//' + 'THE SETTINGS'.center(118))
    Add(Banner)
    Add('')
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
    Add(Banner)
    Add('//' + 'THE PRESETS'.center(118))
    Add(Banner)
    Add('')
    Add('struct GasPreset')
    Add('{')
    Add('    const char* Identity       = "";      // [-] - stable key; what a scene stores')
    Add('    const char* Name           = "";      // [-] - what the preset rail shows')
    Add('    const char* Description    = "";      // [-] - the line beneath it')
    Add('    bool        DetonateOnLoad = false;   // [-] - the preset opens mid-explosion rather than idle')
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
    Add(Banner)
    Add('//' + 'CONSTRUCTING A PRESET'.center(118))
    Add(Banner)
    Add('')
    Add('inline GasSettings ConstructPresetSettings(const char* Identity) noexcept')
    Add('{')
    Add('    GasSettings Resolved;')
    Add('    if (Identity == nullptr)')
    Add('        return Resolved;')
    Add('')
    for Order, (Identity, Record) in enumerate(Presets.items()):
        Overrides = Record.get('params', {})
        Keyword = 'if' if Order == 0 else 'else if'
        Add('    %s (std::strcmp(Identity, "%s") == 0)' % (Keyword, Escape(Identity)))
        Add('    {')
        for Key, Reading in Overrides.items():
            Add('        Resolved.%s = %s;' % (Identifier(Key), Spell(Reading, Declared[Key])))
        if not Overrides:
            Add('        // this preset matches the defaults')
        Add('    }')
    Add('')
    Add('    return Resolved;')
    Add('}')
    Add('')
    Add('inline GasSettings ConstructPresetSettingsBySlot(uint32_t Slot) noexcept')
    Add('{')
    Add('    if (Slot >= GasPresetCount)')
    Add('        return GasSettings{};')
    Add('    return ConstructPresetSettings(GasPresetLibrary()[Slot].Identity);')
    Add('}')
    Add('')
    Add('inline const GasPreset* FindGasPreset(const char* Identity) noexcept')
    Add('{')
    Add('    if (Identity == nullptr)')
    Add('        return nullptr;')
    Add('    const GasPreset* Library = GasPresetLibrary();')
    Add('    for (uint32_t Slot = 0u; Slot < GasPresetCount; ++Slot)')
    Add('    {')
    Add('        if (std::strcmp(Library[Slot].Identity, Identity) == 0)')
    Add('            return &Library[Slot];')
    Add('    }')
    Add('    return nullptr;')
    Add('}')
    Add('')
    Add('} // namespace Frontier')
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
