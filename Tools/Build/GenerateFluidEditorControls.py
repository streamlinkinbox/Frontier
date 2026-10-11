#!/usr/bin/env python3
"""Transcribe the Fluid app's control specification and preset rail into the editor header, or verify it.

    python3 Tools/Build/GenerateFluidEditorControls.py            # write Engine/Editor/FluidEditorControls.h
    python3 Tools/Build/GenerateFluidEditorControls.py --check    # exit 1 if the written header is stale

Experimental/Fluid/src/SceneSpecification.js stays the single source of truth, the same arrangement
GenerateGasPresets.py uses against presets.js. ControlSpecification is assembled at module load from
CONTROL_GROUPS plus AdditionalControls, so unlike presets.js it cannot be read as JSON -- node resolves it
and prints it, and this file only reshapes what node printed. Eighty-two controls hand-copied into C++ would
be eighty-two chances to mistype a limit that nothing would ever catch.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
App = Root / 'Experimental/Fluid'
Target = Root / 'Engine/Editor/FluidEditorControls.h'
Tuning = Root / 'Engine/VolumetricDynamics/GasPresetLibrary.h'

# FluidPanel.js RenderControl(): the unit cell shown inside a value pill, em dash when the setting is bare.
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
      Kind: Control.type,
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
    """The resolved specification, as the browser itself assembles it."""
    Printed = subprocess.run(['node', '-e', Reader], cwd=App, check=True, text=True,
                             stdout=subprocess.PIPE).stdout
    return json.loads(Printed)


def Fields():
    """Browser key -> the GasSettings member carrying it, read from that header's own trailing comments."""
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
        # Settings written in fractions of the domain carry that caveat where the key would be, so they
        #    are matched on the member name instead -- GenerateGasPresets.py capitalises and nothing else.
        Key = Named.group(1) if Named else Member.group(2)[0].lower() + Member.group(2)[1:]
        Found[Key] = (Member.group(2), Member.group(1))
    return Found


def Quoted(Body):
    """A C++ string literal. The source is UTF-8 prose; escapes keep the header seven-bit."""
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
    # A control whose member is a string -- interactionMode -- has no numeric reading and is left out.
    Bridged = [(Row['Key'], Carried[Row['Key']]) for Row in Rows
               if Row['Key'] in Carried and Carried[Row['Key']][1] != 'const']
    Lines = []
    Add = Lines.append
    Rule = '//' + '=' * 142
    Thin = '//' + '-' * 118

    Add(Rule)
    Add('//' + 'FLUIDEDITORCONTROLS.H'.center(142))
    Add(Rule)
    Add('// \U0001f4e6 GENERATED. The Fluid app\'s %d controls, %d preset cards and %d debug channels, for the native editor.'
        % (len(Rows), len(Cards), len(Specification['Channels'])))
    Add('//')
    Add('// \U0001f534 DO NOT EDIT. Tools/Build/GenerateFluidEditorControls.py writes this file from')
    Add('//    Experimental/Fluid/src/SceneSpecification.js, and --check fails the build when the two')
    Add('//    disagree. Limits, steps, labels and the order of the choice lists are authored in the browser,')
    Add('//    against a live picture; the native page only draws them.')
    Add('//')
    Add('// The short Label is what the inspector prints beside the pill and the long Heading is the')
    Add('//    specification\'s own name for the setting, kept because it is what the browser\'s tooltip and')
    Add('//    its exported scene both carry.')
    Add('//')
    Add('// Source revision: %s' % Specification['Revision'])
    Add('')
    Add('#pragma once')
    Add('')
    Add('#include "GasPresetLibrary.h"')
    Add('')
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
    Add('enum class ControlKind : uint8_t')
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
    Add('    ControlKind          Kind        = ControlKind::Range;   // [-]')
    Add('    float                Low         = 0.0f;                 // [-] - range only')
    Add('    float                High        = 0.0f;                 // [-] - range only')
    Add('    float                Step        = 0.0f;                 // [-] - range only')
    Add('    const ControlChoice* Choices     = nullptr;              // [-] - choice only')
    Add('    uint32_t             ChoiceCount = 0u;                   // [-]')
    Add('};')
    Add('')

    # Each choice list becomes its own array so the rows stay one line of figures each.
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
    Add('/// \U0001f4e6 Every control the Fluid app offers, in the specification\'s own order.')
    Add('/// out   const ControlRow*   [-]  FluidControlCount entries, never null')
    Add('/// cost  \u2714\ufe0f')
    Add('inline const ControlRow* FluidControls() noexcept')
    Add('{')
    Add('    static const ControlRow Rail[FluidControlCount] = {')
    for Row in Rows:
        Kind = {'range': 'ControlKind::Range', 'toggle': 'ControlKind::Toggle',
                'select': 'ControlKind::Choice'}[Row['Kind']]
        Unit = Units.get(Row['Key'], '\u2014')
        Tail = 'nullptr, 0u'
        if Row['Choices']:
            Name = Row['Key'][0].upper() + Row['Key'][1:] + 'Choices'
            Tail = '%s, %du' % (Name, len(Row['Choices']))
        Add('        { %s, %s, %s,' % (Quoted(Row['Key']), Quoted(Row['Label']), Quoted(Row['Heading'])))
        Add('          %s, %s, %s, %s, %s, %s },'
            % (Quoted(Unit), Kind, Number(Row['Low']), Number(Row['High']), Number(Row['Step']), Tail))
    Add('    };')
    Add('    return Rail;')
    Add('}')
    Add('')
    Add('/// \U0001f4e6 The control a key names.')
    Add('/// in    Key    [-]  a browser key')
    Add('/// out   const ControlRow*   [-]  null when nothing carries that key')
    Add('/// cost  \u2714\ufe0f  linear over %d entries' % len(Rows))
    Add('inline const ControlRow* ReadControl(const char* Key) noexcept')
    Add('{')
    Add('    if (!Key) return nullptr;')
    Add('    const ControlRow* Rail = FluidControls();')
    Add('    for (uint32_t Index = 0u; Index < FluidControlCount; ++Index)')
    Add('        if (std::strcmp(Rail[Index].Key, Key) == 0) return &Rail[Index];')
    Add('    return nullptr;')
    Add('}')
    Add('')
    Add(Thin)
    Add('//' + 'THE PRESET RAIL'.center(118))
    Add(Thin)
    Add('// PresetPresentation: the short name, the filter it answers to, the line beneath it and the glyph.')
    Add('//    GasPresetLibrary.h carries the same 18 identities with their tuning; this carries how they read.')
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
    Add('/// \U0001f4e6 The preset rail, in the order the browser lists it.')
    Add('/// out   const PresetCard*   [-]  FluidPresetCardCount entries, never null')
    Add('/// cost  \u2714\ufe0f')
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
    Add('//' + 'READING A SETTING'.center(118))
    Add(Thin)
    Add('// The inspector paints whatever the host\'s GasSettings holds, so the page needs one bridge from a')
    Add('//    browser key to the member that carries it. Both sides of that bridge are generated from the')
    Add('//    same two browser files, which is the only reason it can be trusted to stay complete.')
    Add('')
    Add('/// \U0001f4e6 The value a control is currently showing.')
    Add('/// in    Settings   [-]  the effect being edited')
    Add('/// in    Key        [-]  a browser key')
    Add('/// out   Reading    [-]  written only when the key is known; booleans read 0 or 1')
    Add('/// out   bool       [-]  false when no member carries that key')
    Add('/// cost  \u2714\ufe0f  linear over the %d bridged keys' % len(Bridged))
    Add('inline bool ReadSetting(const GasSettings& Settings, const char* Key, float& Reading) noexcept')
    Add('{')
    Add('    if (!Key) return false;')
    for Key, (Member, Kind) in Bridged:
        Cast = 'Settings.%s ? 1.0f : 0.0f' % Member if Kind == 'bool' else 'float(Settings.%s)' % Member
        Add('    if (std::strcmp(Key, "%s") == 0) { Reading = %s; return true; }' % (Key, Cast))
    Add('    return false;')
    Add('}')
    Add('')
    Add(Thin)
    Add('//' + 'THE DEBUG CHANNELS'.center(118))
    Add(Thin)
    Add('// DEBUG_CHANNELS (presets.js). ConnectInterface() fills both the viewport bar\'s select and the')
    Add('//    diagnostics overlay\'s from this list, which is why these read shorter than the renderChannel')
    Add('//    control\'s own option labels -- the list, not the control, is what a user actually sees.')
    Add('')
    Add('constexpr uint32_t DebugChannelCount = %du;' % len(Specification['Channels']))
    Add('')
    Add('/// \U0001f4e6 The channels the viewport can show, in order.')
    Add('/// out   const ControlChoice*   [-]  DebugChannelCount entries, never null')
    Add('/// cost  \u2714\ufe0f')
    Add('inline const ControlChoice* DebugChannels() noexcept')
    Add('{')
    Add('    static const ControlChoice Rail[DebugChannelCount] = {')
    for Channel in Specification['Channels']:
        Add('        { %d, %s },' % (int(Channel['id']), Quoted(Channel['label'])))
    Add('    };')
    Add('    return Rail;')
    Add('}')
    Add('')
    Add('}   // namespace Frontier::FluidEditor')
    return '\n'.join(Lines) + '\n'


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--check', action='store_true', help='verify rather than write')
    Arguments = Parser.parse_args()

    Written = Compose(Read())
    if not Arguments.check:
        Target.write_text(Written, encoding='utf-8')
        print('Wrote %s' % Target.relative_to(Root))
        return 0

    if not Target.exists():
        print('%s is missing; run without --check' % Target.relative_to(Root), file=sys.stderr)
        return 1
    if Target.read_text(encoding='utf-8') != Written:
        print('%s is stale against Experimental/Fluid/src/SceneSpecification.js; '
              'run Tools/Build/GenerateFluidEditorControls.py' % Target.relative_to(Root), file=sys.stderr)
        return 1
    print('%s matches SceneSpecification.js.' % Target.relative_to(Root))
    return 0


if __name__ == '__main__':
    sys.exit(Main())
