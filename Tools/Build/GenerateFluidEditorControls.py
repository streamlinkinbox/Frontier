#!/usr/bin/env python3
"""Transcribe the Fluid app's control specification and preset rail into the editor header, or verify it.

    python3 Tools/Build/GenerateFluidEditorControls.py            # write Frontier/Engine/Editor/FluidEditorControls.h
    python3 Tools/Build/GenerateFluidEditorControls.py --check    # exit 1 if the written header is stale
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
App = Root / 'Experimental/Fluid'
Target = Root / 'Frontier/Engine/Editor/FluidEditorControls.h'
Tuning = Root / 'Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h'

Units = {
    'boundsWidth': 'm', 'boundsHeight': 'm', 'emitterRadius': 'm', 'emitterHeight': 'm',
    'obstacleRadius': 'm', 'sunElevation': '\u00b0', 'sunAzimuth': '\u00b0', 'timeScale': '\u00d7',
    'renderScale': '\u00d7', 'dynamicBoundsMax': '\u00d7', 'emitterRate': '\u00d7',
    'temperatureScale': '\u00d7', 'emberLifetime': '\u00d7',
}

Reader = '''
import("./src/SceneSpecification.js").then((Module) => {
  const Rows = [];
  for (const [Key, Control] of Object.entries(Module.ControlSpecification))
    Rows.push({
      Key,
      Heading: Control.label,
      Label: Module.ControlLabels[Key] || Control.label,
      Form: Control.type,
      Low: Control.min ?? 0,
      High: Control.max ?? 0,
      Step: Control.step ?? 0,
      Choices: (Control.options || []).map((Option) => ({
        Ordinal: Option.id ?? Option.value,
        Label: Option.label,
      })),
    });
  const Cards = Object.entries(Module.PresetPresentation).map(([Identity, Presentation]) => ({
    Identity,
    Name: Presentation[0],
    Category: Presentation[1],
    Summary: Presentation[2],
    Glyph: Presentation[3],
  }));
  import("./src/presets.js").then((Presets) => {
    process.stdout.write(
      JSON.stringify({
        Rows,
        Cards,
        Channels: Presets.DEBUG_CHANNELS,
        Revision: Module.SourceRevision,
      }),
    );
  });
});
'''


def Read():
    Printed = subprocess.run(['node', '-e', Reader], cwd=App, check=True, text=True,
                             stdout=subprocess.PIPE).stdout
    return json.loads(Printed)


def Fields():
    Found = {}
    Inside = False
    for Line in Tuning.read_text(encoding='utf-8').splitlines():
        if Line.startswith('struct GasSettings'):
            Inside = True
            continue
        if Inside and Line.startswith('};'):
            break
        if not Inside:
            continue
        Member = re.match(r'\s*(\S+)\s+(\w+)\s*=', Line)
        if not Member:
            continue
        Named = re.search(r'//.*- (\w+)\s*$', Line)
        Key = Named.group(1) if Named else Member.group(2)[0].lower() + Member.group(2)[1:]
        Found[Key] = (Member.group(2), Member.group(1))
    return Found


def Quoted(Body):
    Out = ['"']
    for Letter in Body:
        if Letter == '"' or Letter == '\\':
            Out.append('\\' + Letter)
        elif ' ' <= Letter <= '~':
            Out.append(Letter)
        else:
            Out.extend('\\x%02x' % Byte for Byte in Letter.encode('utf-8'))
    Out.append('"')
    return ''.join(Out)


def Number(Value):
    Written = ('%g' % float(Value))
    return Written + 'f' if ('.' in Written or 'e' in Written) else Written + '.0f'


def Compose(Specification):
    Rows, Cards = Specification['Rows'], Specification['Cards']
    Carried = Fields()
    Bridged = [(Row['Key'], Carried[Row['Key']], Row) for Row in Rows
               if Row['Key'] in Carried and Carried[Row['Key']][1] != 'const']
    # Also include any remaining numeric/bool settings in GasSettings not in ControlSpecification (e.g. paused, autoTurntable)
    BridgedKeys = {Entry[0] for Entry in Bridged}
    for Key, (Member, DeclaredType) in Carried.items():
        if Key not in BridgedKeys and DeclaredType != 'const':
            Bridged.append((Key, (Member, DeclaredType), None))

    Lines = []
    Add = Lines.append
    Thin = '//' + '-' * 118

    Add('#pragma once')
    Add('/*' + '=' * 138)
    Add('FLUIDEDITORCONTROLS.H'.center(140))
    Add('=' * 138 + '*/')
    Add('// \U0001f9e9 GENERATED. The Fluid app\'s %d controls, %d preset cards and %d debug channels, for the native editor.'
        % (len(Rows), len(Cards), len(Specification['Channels'])))
    Add('//')
    Add('// \U0001f534 DO NOT EDIT. Tools/Build/GenerateFluidEditorControls.py writes this file from')
    Add('//    Experimental/Fluid/src/SceneSpecification.js, and --check fails the build when the two disagree.')
    Add('//')
    Add('// Source revision: %s' % Specification['Revision'])
    Add('')
    Add('#include "Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h"')
    Add('')
    Add('#include <algorithm>')
    Add('#include <cmath>')
    Add('#include <cstdint>')
    Add('#include <cstring>')
    Add('')
    Add('namespace Frontier::FluidEditor')
    Add('{')
    Add('')
    Add(Thin)
    Add('//' + 'ONE CONTROL'.center(118))
    Add(Thin)
    Add('')
    Add('enum class ControlForm : uint8_t')
    Add('{')
    Add('    Range  = 0,   // [-] - a number with a pill, a unit cell and a slider')
    Add('    Toggle = 1,   // [-] - a switch')
    Add('    Choice = 2,   // [-] - a select, whose options are listed in order')
    Add('};')
    Add('')
    Add('struct ControlChoice')
    Add('{')
    Add('    int         Ordinal = 0;    // [-] - the value stored for this option')
    Add('    const char* Label   = "";   // [-] - what the select shows')
    Add('};')
    Add('')
    Add('struct ControlRow')
    Add('{')
    Add('    const char*          Key         = "";                   // [-] - the browser key; what a scene stores')
    Add('    const char*          Label       = "";                   // [-] - the inspector\'s short name')
    Add('    const char*          Heading     = "";                   // [-] - the specification\'s long name')
    Add('    const char*          Unit        = "";                   // [-] - the pill\'s unit cell')
    Add('    ControlForm          Form        = ControlForm::Range;   // [-]')
    Add('    float                Low         = 0.0f;                 // [-] - range only')
    Add('    float                High        = 0.0f;                 // [-] - range only')
    Add('    float                Step        = 0.0f;                 // [-] - range only')
    Add('    const ControlChoice* Choices     = nullptr;              // [-] - choice only')
    Add('    uint32_t             ChoiceCount = 0u;                   // [-]')
    Add('};')
    Add('')

    for Row in Rows:
        if not Row['Choices']:
            continue
        Name = Row['Key'][0].upper() + Row['Key'][1:] + 'Choices'
        Add('inline constexpr ControlChoice %s[] = {' % Name)
        for Option in Row['Choices']:
            Add('    { %d, %s },' % (int(Option['Ordinal']), Quoted(Option['Label'])))
        Add('};')
        Add('')

    Add('constexpr uint32_t FluidControlCount = %du;' % len(Rows))
    Add('')
    Add('inline const ControlRow* FluidControls() noexcept')
    Add('{')
    Add('    static const ControlRow Rail[FluidControlCount] = {')
    for Row in Rows:
        Form = {
            'range': 'ControlForm::Range',
            'toggle': 'ControlForm::Toggle',
            'select': 'ControlForm::Choice',
        }[Row['Form']]
        Unit = Units.get(Row['Key'], '\u2014')
        Tail = 'nullptr, 0u'
        if Row['Choices']:
            Name = Row['Key'][0].upper() + Row['Key'][1:] + 'Choices'
            Tail = '%s, %du' % (Name, len(Row['Choices']))
        Add('        { %s, %s, %s,' % (Quoted(Row['Key']), Quoted(Row['Label']), Quoted(Row['Heading'])))
        Add('          %s, %s, %s, %s, %s, %s },'
            % (Quoted(Unit), Form, Number(Row['Low']), Number(Row['High']), Number(Row['Step']), Tail))
    Add('    };')
    Add('    return Rail;')
    Add('}')
    Add('')
    Add('inline const ControlRow* ReadControl(const char* Key) noexcept')
    Add('{')
    Add('    if (!Key)')
    Add('        return nullptr;')
    Add('    const ControlRow* Rail = FluidControls();')
    Add('    for (uint32_t Index = 0u; Index < FluidControlCount; ++Index)')
    Add('    {')
    Add('        if (std::strcmp(Rail[Index].Key, Key) == 0)')
    Add('            return &Rail[Index];')
    Add('    }')
    Add('    return nullptr;')
    Add('}')
    Add('')
    Add(Thin)
    Add('//' + 'THE PRESET RAIL'.center(118))
    Add(Thin)
    Add('')
    Add('struct PresetCard')
    Add('{')
    Add('    const char* Identity = "";   // [-] - the key shared with GasPresetLibrary.h')
    Add('    const char* Name     = "";   // [-] - the card\'s title')
    Add('    const char* Category = "";   // [-] - all / fire / smoke / blast')
    Add('    const char* Summary  = "";   // [-] - the line beneath the title')
    Add('    const char* Glyph    = "";   // [-] - the swatch drawing')
    Add('};')
    Add('')
    Add('constexpr uint32_t FluidPresetCardCount = %du;' % len(Cards))
    Add('')
    Add('inline const PresetCard* FluidPresetCards() noexcept')
    Add('{')
    Add('    static const PresetCard Rail[FluidPresetCardCount] = {')
    for Card in Cards:
        Add('        { %s, %s,' % (Quoted(Card['Identity']), Quoted(Card['Name'])))
        Add('          %s, %s, %s },'
            % (Quoted(Card['Category']), Quoted(Card['Summary']), Quoted(Card['Glyph'])))
    Add('    };')
    Add('    return Rail;')
    Add('}')
    Add('')
    Add(Thin)
    Add('//' + 'READING AND WRITING A SETTING'.center(118))
    Add(Thin)
    Add('')
    Add('inline bool ReadSetting(const GasSettings& Settings, const char* Key, float& Reading) noexcept')
    Add('{')
    Add('    if (!Key)')
    Add('        return false;')
    for Key, (Member, DeclaredType), _ in Bridged:
        Cast = 'Settings.%s ? 1.0f : 0.0f' % Member if DeclaredType == 'bool' else 'float(Settings.%s)' % Member
        Add('    if (std::strcmp(Key, "%s") == 0) { Reading = %s; return true; }' % (Key, Cast))
    Add('    return false;')
    Add('}')
    Add('')
    Add('inline bool WriteSetting(GasSettings& Settings, const char* Key, float Reading) noexcept')
    Add('{')
    Add('    if (!Key)')
    Add('        return false;')
    for Key, (Member, DeclaredType), Row in Bridged:
        if DeclaredType == 'bool':
            Assign = 'Settings.%s = (Reading >= 0.5f);' % Member
        elif DeclaredType == 'int32_t':
            if Row and Row['Form'] == 'range' and Row['High'] > Row['Low']:
                Assign = ('Settings.%s = int32_t(std::lround(std::clamp(Reading, %s, %s)));'
                          % (Member, Number(Row['Low']), Number(Row['High'])))
            else:
                Assign = 'Settings.%s = int32_t(std::lround(Reading));' % Member
        else:
            if Row and Row['Form'] == 'range' and Row['High'] > Row['Low']:
                Assign = 'Settings.%s = std::clamp(Reading, %s, %s);' % (Member, Number(Row['Low']), Number(Row['High']))
            else:
                Assign = 'Settings.%s = Reading;' % Member
        Add('    if (std::strcmp(Key, "%s") == 0) { %s return true; }' % (Key, Assign))
    Add('    return false;')
    Add('}')
    Add('')
    Add(Thin)
    Add('//' + 'THE DEBUG CHANNELS'.center(118))
    Add(Thin)
    Add('')
    Add('constexpr uint32_t DebugChannelCount = %du;' % len(Specification['Channels']))
    Add('')
    Add('inline const ControlChoice* DebugChannels() noexcept')
    Add('{')
    Add('    static const ControlChoice Rail[DebugChannelCount] = {')
    for Channel in Specification['Channels']:
        Add('        { %d, %s },' % (int(Channel['id']), Quoted(Channel['label'])))
    Add('    };')
    Add('    return Rail;')
    Add('}')
    Add('')
    Add('} // namespace Frontier::FluidEditor')
    return '\n'.join(Lines) + '\n'


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--check', action='store_true', help='verify rather than write')
    Arguments = Parser.parse_args()

    Written = Compose(Read())
    if not Arguments.check:
        Target.parent.mkdir(parents=True, exist_ok=True)
        Target.write_text(Written, encoding='utf-8')
        print('Wrote %s' % Target.relative_to(Root))
        return 0

    if not Target.exists() or Target.read_text(encoding='utf-8') != Written:
        print('%s is stale against Experimental/Fluid/src/SceneSpecification.js' % Target.relative_to(Root), file=sys.stderr)
        return 1
    print('%s matches SceneSpecification.js.' % Target.relative_to(Root))
    return 0


if __name__ == '__main__':
    sys.exit(Main())
