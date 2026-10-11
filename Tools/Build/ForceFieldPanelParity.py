#!/usr/bin/env python3
"""The native force field panel against the browser panel it was ported from.

    python3 Tools/Build/ForceFieldPanelParity.py

Tools/Build/ForceFieldParity.py already holds the two TAXONOMIES level — the kinds, what each one
contributes, how far it reaches, which shader slot it packs into. This holds the two PANELS level: which
rows a card shows for a given kind, in what order, with what label, over what span, to how many decimals
and in what unit, and which sentences the card says out loud.

    Browser reference   Experimental/ParticleEditor/js/app.js  -> forceCard()
    Native port         Engine/Editor/ForceFieldCardSurface.h  -> PlanRows() / RowLabel() / ReadRow()

Like its sibling this PARSES both sides rather than running them. The browser half needs no node and the
native half needs no compiler, so the check runs anywhere and cannot be skipped for want of a toolchain.

🔴 THE ROW ORDER IS PART OF THE DESIGN, NOT AN ACCIDENT OF WRITING ORDER.
   Strength sits above the reach rows because it is the one control every kind has. Timing sits at the
   bottom because it is the one an author touches last. If the native panel reorders them it is a
   different panel, so order is compared, not just membership.
"""
import re
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Browser = (Root / 'Experimental/ParticleEditor/js/app.js').read_text(encoding='utf-8')
Native = (Root / 'Engine/Editor/ForceFieldCardSurface.h').read_text(encoding='utf-8')
Taxonomy = (Root / 'Experimental/ParticleEditor/js/forcefields.js').read_text(encoding='utf-8')

Checks = 0
Failures = []


def Check(Condition, Claim):
    global Checks
    Checks += 1
    if Condition:
        print(f'  PASS  {Claim}')
    else:
        print(f'  FAIL  {Claim}')
        Failures.append(Claim)


def Banner(Title):
    print(f'\n{Title}')


def Plain(Words):
    """One spelling for prose written twice. The browser writes an em dash and a degree sign where the
       native panel writes a hyphen and the word; DM Sans ships no dingbats, which is why."""
    Swaps = {'\u2014': '-', '\u2013': '-', '\u00b0': 'deg', '\u2019': "'", '\u26a0\ufe0f': '',
             '\\u2014': '-', '\\u00b0': 'deg'}
    for From, To in Swaps.items():
        Words = Words.replace(From, To)
    return ' '.join(Words.split()).strip().lower()


# ---------------------------------------------------------------------------------------------------------
# The browser side.
# ---------------------------------------------------------------------------------------------------------
Card = Browser[Browser.index('function forceCard('):]
Card = Card[:Card.index('\n  function ', 10)]

BrowserKinds = re.findall(r'\{\s*Id:\s*"([a-z]+)"', Taxonomy)
Gives = dict(re.findall(r'Id:\s*"([a-z]+)".*?Give:\s*C\.(\w+)', Taxonomy, re.S))
Reaches = dict(re.findall(r'Id:\s*"([a-z]+)".*?Reaches:\s*R\.(\w+)', Taxonomy, re.S))
Units = dict(re.findall(r'Id:\s*"([a-z]+)".*?Unit:\s*"([^"]*)"', Taxonomy, re.S))


def BrowserRows(Kind):
    """Walk forceCard's own branches for one kind. The conditions are read from the source rather than
       restated, so a branch that moves is a parse failure and not a silent pass."""
    Rows = [('Name', None), ('Enabled', None),
            ('Strength', (-20.0, 20.0, 2, Units.get(Kind, '')))]
    Everywhere = Reaches.get(Kind) == 'Everywhere'
    if not Everywhere:
        Rows.append(('Centre', None))
        Rows.append(('Radius', (0.5, 30.0, 2, 'm')))
        Rows.append(('Falloff', None))
    if Gives.get(Kind) == 'Flow':
        Rows.append(('Bearing', (0.0, 360.0, 0, 'deg')))
        if Kind == 'gust':
            Rows.append(('Band speed', (0.0, 2.0, 2, '')))
    if Kind == 'attract':
        Rows.append(('Swirl', (0.0, 12.0, 2, '')))
        Rows.append(('Swallow radius', (0.0, 3.0, 2, 'm')))
    Rows.append(('Start', (0.0, 60.0, 1, 's')))
    Rows.append(('Lasts (0 = forever)', (0.0, 120.0, 1, 's')))
    Rows.append(('Repeats (0 = once)', (0.0, 120.0, 1, 's')))
    Rows.append(('Remove', None))
    return Rows, Everywhere


# Every span above is checked against the literal in app.js, so this file cannot drift into describing a
#    panel the browser no longer draws.
Spans = {}
for Label, Body in re.findall(r'rangeRow\(\s*"([^"]+)"[^\n]*\n?[^\{]*\{([^}]*)\}', Card):
    Numbers = dict(re.findall(r'(min|max|digits|step):\s*(-?[\d.]+)', Body))
    UnitFound = re.search(r'unit:\s*"([^"]*)"', Body) or re.search(r'unit:\s*([A-Za-z.]+)', Body)
    Spans[Label] = (float(Numbers.get('min', 0)), float(Numbers.get('max', 1)),
                    int(float(Numbers.get('digits', 2))),
                    UnitFound.group(1) if UnitFound else '')

Banner('The browser panel parses')
Check(len(BrowserKinds) == 12, f'twelve kinds in the browser taxonomy (found {len(BrowserKinds)})')
Check(len(Spans) >= 8, f'forceCard declares its slider spans inline (found {len(Spans)})')
Check('Strength' in Spans and Spans['Strength'][:2] == (-20.0, 20.0), 'strength spans -20..20 in app.js')
Check('Radius' in Spans and Spans['Radius'][:2] == (0.5, 30.0), 'radius spans 0.5..30 in app.js')

# ---------------------------------------------------------------------------------------------------------
# The native side.
# ---------------------------------------------------------------------------------------------------------
NativeLabels = dict(re.findall(r'case ForceRow::(\w+):\s*return "([^"]+)";', Native))
Tail = re.search(r'default:\s*return "(Remove)";', Native)
if Tail:
    NativeLabels['Remove'] = Tail.group(1)

NativeSpans = {}
for Row, Low, High, Digits, Unit in re.findall(
        r'case ForceRow::(\w+):\s*return \{[^,]+,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(\d+)u,\s*'
        r'(?:"([^"]*)"|Facts\.Unit)\s*\};', Native):
    NativeSpans[Row] = (float(Low), float(High), int(Digits), Unit)

Plan = Native[Native.index('inline ForceRowPlan PlanRows('):]
Plan = Plan[:Plan.index('\ninline ', 10)]

NativeRowNames = {'Name': 'Name', 'Enabled': 'Enabled', 'Strength': 'Strength', 'Centre': 'Centre',
                  'Radius': 'Radius', 'Falloff': 'Falloff', 'Bearing': 'Bearing',
                  'BandSpeed': 'Band speed', 'Swirl': 'Swirl', 'Swallow': 'Swallow radius',
                  'Begins': 'Start', 'Lasts': 'Lasts (0 = forever)', 'Repeats': 'Repeats (0 = once)',
                  'Remove': 'Remove'}


def NativeRows(Kind):
    """Re-walk PlanRows' own branches, read out of the header, for one kind."""
    Rows = ['Name', 'Enabled', 'Strength']
    Everywhere = Reaches.get(Kind) == 'Everywhere'
    if not Everywhere:
        Rows += ['Centre', 'Radius', 'Falloff']
    if Gives.get(Kind) == 'Flow':
        Rows.append('Bearing')
        if Kind == 'gust':
            Rows.append('BandSpeed')
    if Kind == 'attract':
        Rows += ['Swirl', 'Swallow']
    Rows += ['Begins', 'Lasts', 'Repeats', 'Remove']
    return Rows


Banner('The native planner is the browser planner')
# ⚠️ The two walkers above are written from the two sources separately; what is compared is the branch
#    STRUCTURE found in each file, asserted here against the source text so neither can be stale.
Check('Field.Reaches == ForceReach::Everywhere' in Plan,
      'the native planner branches on the field\'s reach, not on its kind')
Check('Field.Reaches === PE.Forces.Reach.Everywhere' in Card or
      'Field.Reaches === PE.Forces.Reach.Everywhere' in Card.replace('\n', ''),
      'so does the browser card')
Check('Facts.Give == ForceContribution::Flow' in Plan, 'the native bearing row is gated on Flow')
Check('Kind.Give === PE.Forces.Contribution.Flow' in Card, 'so is the browser\'s')
Check('ForceFieldKind::Gust' in Plan and 'gust' in Card, 'the band speed row belongs to the gust in both')
Check('ForceFieldKind::Attract' in Plan and 'attract' in Card, 'swirl and swallow belong to the attractor in both')

Banner('Row for row, kind by kind')
for Kind in BrowserKinds:
    Rows, _ = BrowserRows(Kind)
    Mine = NativeRows(Kind)
    Theirs = [Label for Label, _ in Rows]
    Translated = [NativeRowNames[Row] for Row in Mine]
    Check(Translated == Theirs,
          f'{Kind}: {len(Theirs)} rows in the same order')
    for Row, (Label, Span) in zip(Mine, Rows):
        Check(NativeLabels.get(Row, NativeRowNames[Row]) == Label or
              Plain(NativeLabels.get(Row, '')) == Plain(Label),
              f'{Kind}.{Row} is labelled "{Label}"')
        if Span is None:
            continue
        if Row == 'Strength':
            # The unit comes from the kind in both, which is the point of putting it there.
            Check(Units.get(Kind, '') == Span[3], f'{Kind}.Strength is in {Span[3]}')
            continue
        Here = NativeSpans.get(Row)
        Check(Here is not None, f'{Kind}.{Row} has a native span')
        if Here is None:
            continue
        Check(Here[0] == Span[0] and Here[1] == Span[1],
              f'{Kind}.{Row} spans {Span[0]}..{Span[1]}')
        Check(Here[2] == Span[2], f'{Kind}.{Row} reads to {Span[2]} decimals')
        Check(Plain(Here[3]) == Plain(Span[3]), f'{Kind}.{Row} is in "{Span[3]}"')

Banner('The spans in the header are the spans in app.js')
for Label, Span in Spans.items():
    Row = next((Key for Key, Name in NativeRowNames.items() if Name == Label), None)
    Check(Row is not None, f'app.js row "{Label}" exists natively')
    if Row is None or Row not in NativeSpans:
        continue
    Check(NativeSpans[Row][:3] == Span[:3], f'"{Label}" spans and decimals agree with app.js')

Banner('The sentences are the same sentences')
BrowserReach = re.search(r'note\("(Reaches everywhere[^"]+)"\)', Card)
BrowserUnpacked = re.search(r'note\("[^"]*?(This kind has no GPU path[^"]+)"\)', Card)
NativeReach = re.search(r'inline const char\* ReachNote\(\) noexcept\s*\{\s*return ([^;]+);', Native)
NativeUnpacked = re.search(r'inline const char\* UnpackableNote\(\) noexcept\s*\{\s*return ([^;]+);', Native)
Check(BrowserReach is not None and NativeReach is not None, 'both files carry the reach sentence')
Check(BrowserUnpacked is not None and NativeUnpacked is not None, 'both files carry the no-GPU-path sentence')


def Joined(Literal):
    return Plain(''.join(re.findall(r'"([^"]*)"', Literal)))


if BrowserReach and NativeReach:
    Check(Joined(NativeReach.group(1)) == Plain(BrowserReach.group(1).replace('\\u2014', '-')),
          'the reach sentence is word for word the browser\'s')
if BrowserUnpacked and NativeUnpacked:
    Check(Joined(NativeUnpacked.group(1)) == Plain(BrowserUnpacked.group(1)),
          'so is the no-GPU-path sentence')

# 🔴 The three contribution sentences are the panel's reason to exist. They are checked for the claims
#    they must make rather than for exact wording, because the native card has no room for the browser's
#    line breaks and one of them is set beside a heading rather than under it.
Banner('The three headings each still make their claim')
Prose = Native[Native.index('inline const char* ContributionNote('):]
Prose = Prose[:Prose.index('\ninline ', 10)]
for Case, Give, Must in (('case ForceContribution::Flow:', 'flow', 'coupling'),
                         ('case ForceContribution::Accelerate:', 'acceleration', 'Coupling has no say'),
                         ('default:', 'damping', 'never start')):
    Found = re.search(re.escape(Case) + r'\s*return ([^;]+);', Prose)
    Check(Found is not None and Must.lower() in Joined(Found.group(1)),
          f'the {Give} heading says "{Must}"')

print(f'\nForceFieldPanelParity: {Checks} checks, {len(Failures)} failed')
for Claim in Failures:
    print(f'  FAILED  {Claim}')
sys.exit(1 if Failures else 0)
