#!/usr/bin/env python3
"""Engine/Shaders/InterfaceSignedDistance.slang -> WGSL, for the browser HUD reference.

    python3 Tools/Build/GenerateHudShader.py            # write
    python3 Tools/Build/GenerateHudShader.py --check    # fail if the written file is stale

🔴 THE BROWSER REFERENCE IS GENERATED, NOT TRANSCRIBED.

   Experimental/SpatialHud draws the same shapes the native spatial interface draws. Writing them a
   second time in WGSL would have produced two files that agree on the day they were written and drift
   forever after — which is exactly the failure the gas card's parity script exists to catch after the
   fact. This converts instead, so there is no second authority: an arc in the browser IS the engine's
   arc, because it is the engine's text with its types rewritten.

   The conversion is mechanical and deliberately NARROW. It understands the subset of GLSL this one file
   is written in and nothing more, and it fails loudly on anything it does not recognise rather than
   emitting WGSL that merely looks plausible. Every rule below exists because this file uses it.

What is NOT converted, on purpose: the streak field. That is a new category with no native counterpart
yet, so it is hand-written in js/streaks.js. When it crosses to C++ it goes into the .slang and starts
arriving through here like everything else.
"""
import argparse
import re
import sys
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Source = Root / 'Engine/Shaders/InterfaceSignedDistance.slang'
Target = Root / 'Experimental/SpatialHud/js/sdf.generated.js'

Scalars = {'float': 'f32', 'uint': 'u32', 'int': 'i32', 'bool': 'bool'}
Vectors = {'vec2': 'vec2f', 'vec3': 'vec3f', 'vec4': 'vec4f'}
Types = {**Scalars, **Vectors}


def ConvertConstructors(Line):
    """vec2(...) -> vec2f(...), uint(x) -> u32(x). Word-bounded so `vec2 Inner` is untouched here."""
    for From, To in Vectors.items():
        Line = re.sub(r'\b' + From + r'\s*\(', To + '(', Line)
    Line = re.sub(r'\bfloat\s*\(', 'f32(', Line)
    Line = re.sub(r'\buint\s*\(', 'u32(', Line)
    return Line


def ConvertCalls(Line):
    """Two-argument atan is atan2 in WGSL; the one-argument form keeps its name."""
    return re.sub(r'\batan\s*\(([^(),]+),', r'atan2(\1,', Line)


def ConvertTernary(Line):
    """WGSL has no ternary operator. select(falseValue, trueValue, condition) is the replacement.

    🔴 THIS IS SCANNED, NOT PATTERN-MATCHED, AND THE FIRST ATTEMPT WAS WRONG IN A WAY THAT COMPILED.
       A regex that reached for the nearest parenthesised group turned

           (Offset - Span) < (kInterfaceTau - Offset) ? Span : 0.0

       into select(0.0, Span, kInterfaceTau - Offset) — valid WGSL, wrong arc, and nothing downstream
       could have noticed. So the condition's start is found by walking LEFT with a parenthesis depth
       and stopping at the real boundary: an unmatched open bracket, or a top-level = or , or return.
       SelfCheck() below pins all three conversions this file needs.
    """
    while '?' in Line:
        Mark = Line.rindex('?')

        Depth, Start = 0, 0
        for At in range(Mark - 1, -1, -1):
            Letter = Line[At]
            if Letter in ')]':
                Depth += 1
            elif Letter in '([':
                if Depth == 0:
                    Start = At + 1
                    break
                Depth -= 1
            elif Depth == 0 and Letter in '=,':
                Start = At + 1
                break
        else:
            Start = 0
        Region = Line[Start:Mark]
        Lead = Region[:len(Region) - len(Region.lstrip())]
        Condition = Region.strip()
        if Condition.startswith('return '):
            Start += len(Lead) + len('return ')
            Lead = ' '
            Condition = Condition[len('return '):].strip()

        Depth, Colon = 0, -1
        for At in range(Mark + 1, len(Line)):
            Letter = Line[At]
            if Letter in '([':
                Depth += 1
            elif Letter in ')]':
                Depth -= 1
            elif Letter == ':' and Depth == 0:
                Colon = At
                break
        if Colon < 0:
            raise SystemExit(f'GenerateHudShader: a ternary with no colon: {Line.strip()}')

        Depth, Finish = 0, len(Line)
        for At in range(Colon + 1, len(Line)):
            Letter = Line[At]
            if Letter in '([':
                Depth += 1
            elif Letter in ')]':
                if Depth == 0:
                    Finish = At
                    break
                Depth -= 1
            elif Depth == 0 and Letter in ';,':
                Finish = At
                break

        Yes = Line[Mark + 1:Colon].strip()
        No  = Line[Colon + 1:Finish].strip()
        Line = Line[:Start] + Lead + f'select({No}, {Yes}, {Condition})' + Line[Finish:]
    return Line


def SelfCheck():
    """The three ternaries this file contains, and what each must become. A fourth one appearing is a
       generator change, not a silent conversion."""
    Expected = [
        ('    float Nearest = (Offset - Span) < (kInterfaceTau - Offset) ? Span : 0.0;',
         '    float Nearest = select(0.0, Span, (Offset - Span) < (kInterfaceTau - Offset));'),
        ('    float CapAngle = Start + (Extent < 0.0 ? -Nearest : Nearest);',
         '    float CapAngle = Start + (select(Nearest, -Nearest, Extent < 0.0));'),
        ('    float Snapped = Start + clamp(Index, 0.0, Count - 1.0) * Step * (Sweep < 0.0 ? -1.0 : 1.0);',
         '    float Snapped = Start + clamp(Index, 0.0, Count - 1.0) * Step * (select(1.0, -1.0, Sweep < 0.0));'),
    ]
    for Source_, Wanted in Expected:
        Got = ConvertTernary(Source_)
        if Got != Wanted:
            raise SystemExit(f'GenerateHudShader: ternary self-check failed\n  from {Source_.strip()}\n'
                             f'  want {Wanted.strip()}\n  got  {Got.strip()}')


def ConvertBraces(Line):
    """WGSL has no single-statement if. GLSL does, and this file uses it forty times.

    `if (Lit == 0u) return 1.0e9;` is a parse error there, not a style preference, so the body is
    wrapped. The condition's closing parenthesis is found by matching, not by regex: several of these
    conditions contain parentheses of their own, as in `if ((Lit & 0x01u) != 0u) Best = ...`.
    """
    Stripped = Line.strip()
    Indent = Line[:len(Line) - len(Line.lstrip())]

    Head = None
    for Opener in ('} else if', 'else if', 'if', 'else'):
        if Stripped.startswith(Opener) and (len(Stripped) == len(Opener)
                                            or not Stripped[len(Opener)].isalnum()):
            Head = Opener
            break
    if Head is None:
        return Line

    At = len(Head)
    if Head != 'else':
        while At < len(Stripped) and Stripped[At] != '(':
            At += 1
        if At >= len(Stripped):
            return Line
        Depth = 0
        while At < len(Stripped):
            if Stripped[At] == '(':
                Depth += 1
            elif Stripped[At] == ')':
                Depth -= 1
                if Depth == 0:
                    At += 1
                    break
            At += 1

    Condition = Stripped[:At]
    Body = Stripped[At:].strip()
    if not Body or Body.startswith('{'):
        return Line                                  # already a block, or the block opens next line
    return f'{Indent}{Condition} {{ {Body} }}'


FunctionHead = re.compile(r'^(?P<ret>float|uint|int|vec[234]|void)\s+(?P<name>\w+)\s*\((?P<args>[^)]*)\)\s*$')
Declaration = re.compile(r'^(?P<indent>\s*)(?P<const>const\s+)?(?P<type>float|uint|int|vec[234])\s+(?P<rest>.+;)\s*$')
TopConstant = re.compile(r'^const\s+(?P<type>float|uint|int)\s+(?P<name>\w+)\s*=\s*(?P<value>[^;]+);$')


def ConvertSignature(Line):
    Match = FunctionHead.match(Line.strip())
    if Match is None:
        return None
    Arguments = []
    for One in Match.group('args').split(','):
        One = One.strip()
        if not One:
            continue
        Kind, _, Name = One.partition(' ')
        if Kind not in Types:
            raise SystemExit(f'GenerateHudShader: unknown parameter type "{Kind}" in: {Line.strip()}')
        Arguments.append(f'{Name.strip()}: {Types[Kind]}')
    Returns = '' if Match.group('ret') == 'void' else f" -> {Types.get(Match.group('ret'), Vectors.get(Match.group('ret')))}"
    return f"fn {Match.group('name')}({', '.join(Arguments)}){Returns}"


def ConvertDeclaration(Line):
    """A local declaration becomes var (mutable) or let (a const in the source).

    Everything is var by default rather than let, because this file reassigns most of what it declares
    and a wrong let is a compile error rather than a wrong picture. A source `const` becomes `let`:
    WGSL's own `const` demands a compile-time constant, which `const float T = HalfThickness;` is not.
    """
    Match = Declaration.match(Line)
    if Match is None:
        return None
    Keyword = 'let' if Match.group('const') else 'var'
    Body = Match.group('rest').rstrip()
    assert Body.endswith(';')
    Body = Body[:-1]

    # `const float B = 0.06, Tp = 0.94;` — one declaration per statement in WGSL.
    Parts, Depth, Current = [], 0, ''
    for Letter in Body:
        if Letter in '([':
            Depth += 1
        elif Letter in ')]':
            Depth -= 1
        if Letter == ',' and Depth == 0:
            Parts.append(Current)
            Current = ''
        else:
            Current += Letter
    Parts.append(Current)
    return '; '.join(f'{Keyword} {One.strip()}' for One in Parts) + ';'


def WrapDanglingBlocks(Lines):
    """The other half of the braces problem: an if whose single statement is on the NEXT line.

        if (Delta <= abs(Sweep) || Sweep >= 6.28318530718)
            return abs(length(Local) - Radius) - HalfThickness;

    ConvertBraces only sees one line at a time and cannot know a body is coming, so this pass runs
    afterwards over the whole file and brackets the statement in Allman braces of its own.
    """
    Out, Index = [], 0
    while Index < len(Lines):
        Line = Lines[Index]
        Code = Line.split('//')[0].rstrip()
        Stripped = Code.strip()
        Opens = any(Stripped.startswith(One) for One in ('if ', 'if(', '} else if', 'else if', 'else'))

        if Opens and not Stripped.endswith(('{', '}', ';')) and Stripped.endswith(')'):
            Indent = Line[:len(Line) - len(Line.lstrip())]
            Follow = Index + 1
            while Follow < len(Lines) and (not Lines[Follow].strip()
                                           or Lines[Follow].strip().startswith('//')):
                Follow += 1
            if Follow < len(Lines) and not Lines[Follow].strip().startswith('{'):
                Finish, Depth = Follow, 0
                while Finish < len(Lines):
                    Body = Lines[Finish].split('//')[0]
                    Depth += Body.count('(') - Body.count(')')
                    if Depth <= 0 and Body.rstrip().endswith(';'):
                        break
                    Finish += 1
                Out.append(Line)
                Out.append(Indent + '{')
                Out.extend(Lines[Index + 1:Finish + 1])
                Out.append(Indent + '}')
                Index = Finish + 1
                continue
        Out.append(Line)
        Index += 1
    return Out


def Convert(Text):
    Out, InFunction = [], False
    for Raw in Text.splitlines():
        Line = Raw.rstrip('\n')

        if Line.startswith('#'):
            continue                                            # include guards have no WGSL meaning

        Stripped = Line.strip()
        if Stripped.startswith('//') or Stripped == '':
            Out.append(Line)
            continue

        # Trailing comments are carried, not parsed: the declaration and signature patterns below
        #    anchor on the end of the statement, and a comment there would hide it from both.
        Code, Marker, Trailer = Line.partition('//')
        Trailer = (Marker + Trailer) if Marker else ''
        Pad = ''
        if Trailer:
            Bare = Code.rstrip()
            Pad = Code[len(Bare):]
            Code = Bare
        Line = Code

        Line = ConvertConstructors(Line)
        Line = ConvertCalls(Line)
        Line = ConvertTernary(Line)

        def Finish(Text):
            return Text + Pad + Trailer

        Top = TopConstant.match(Line.strip())
        if Top is not None and not InFunction:
            Out.append(Finish(f"const {Top.group('name')}: {Scalars[Top.group('type')]} = "
                              f"{Top.group('value').strip()};"))
            continue

        Line = ConvertBraces(Line)

        Signature = ConvertSignature(Line)
        if Signature is not None:
            Out.append(Finish(Signature))
            InFunction = True
            continue

        if InFunction:
            Declared = ConvertDeclaration(Line)
            if Declared is not None:
                Indent = Line[:len(Line) - len(Line.lstrip())]
                Out.append(Finish(Indent + Declared.strip()))
                continue
            if Line.startswith('}'):
                InFunction = False

        Out.append(Finish(Line))
    return '\n'.join(WrapDanglingBlocks(Out))


def Verify(Wgsl):
    """Loud failures. Each of these means the converter met something it does not understand."""
    Trouble = []
    for Number, Line in enumerate(Wgsl.splitlines(), 1):
        Bare = Line.split('//')[0]
        if '?' in Bare:
            Trouble.append(f'{Number}: an unconverted ternary: {Line.strip()}')
        for Word in ('vec2 ', 'vec3 ', 'vec4 ', 'float ', 'uint '):
            if re.search(r'(^|[(,\s])' + Word.strip() + r'\s+\w', Bare) and 'fn ' not in Bare:
                Trouble.append(f'{Number}: a GLSL type survived: {Line.strip()}')
                break
    if Trouble:
        for One in Trouble:
            print(f'  GenerateHudShader: {One}', file=sys.stderr)
        raise SystemExit('GenerateHudShader: conversion is incomplete, refusing to write')


Preamble = '''// GENERATED - DO NOT EDIT.
//
//     python3 Tools/Build/GenerateHudShader.py
//
// Source: Engine/Shaders/InterfaceSignedDistance.slang
//
// Every shape the native spatial interface can draw, converted to WGSL so the browser reference draws
// the engine's own arcs, ticks, needles, digits and glyphs rather than lookalikes of them. The streak
// field is NOT here: it has no native counterpart yet and lives in js/streaks.js until it does.

export const SDF_WGSL = String.raw`
'''


def Compose():
    Wgsl = Convert(Source.read_text(encoding='utf-8'))
    Verify(Wgsl)
    return Preamble + Wgsl + '\n`;\n'


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--check', action='store_true', help='fail if the written file is out of date')
    Arguments = Parser.parse_args()

    SelfCheck()
    Written = Compose()
    if Arguments.check:
        Existing = Target.read_text(encoding='utf-8') if Target.exists() else ''
        if Existing != Written:
            raise SystemExit(f'GenerateHudShader: {Target.relative_to(Root)} is stale - re-run the generator')
        print(f'GenerateHudShader: {Target.relative_to(Root)} is current')
    else:
        Target.parent.mkdir(parents=True, exist_ok=True)
        Target.write_text(Written, encoding='utf-8')
        print(f'GenerateHudShader: wrote {Target.relative_to(Root)} '
              f'({len(Written.splitlines())} lines from {Source.relative_to(Root)})')
