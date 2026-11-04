#!/usr/bin/env python3
# ==================================================================================================================================
#                                                      RENDERDRAWINGSVG.PY
# ==================================================================================================================================
# 🧩 Renders drawing SVGs written by WriteDrawingSvg / DrawingValidation --svg to PNG through ImageMagick's own vector format (MVG),
#    for hosts whose ImageMagick has no SVG delegate. Standard library only; reads only the subset the kernel writes (layer groups
#    with polylines, polygons and texts).
#
#    python3 RenderDrawingSvg.py [--font DejaVuSansMono.ttf] [--pixels-per-millimetre 4] file.svg ...

import argparse
import re
import subprocess
import sys

# ------------------------------------------------------------------------------------------------------------------------
#                                                    CONVERSION
# ------------------------------------------------------------------------------------------------------------------------

def ScalePoints(Points, Scale):
    return ' '.join('%.2f,%.2f' % (float(First) * Scale, float(Second) * Scale) for First, Second in (Pair.split(',') for Pair in Points.split()))

def RenderSheet(Path, Font, Scale):
    Source = open(Path, encoding='utf-8').read()
    Box = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', Source)
    if not Box:
        return 'no millimetre viewBox'
    Width, Height = float(Box.group(1)), float(Box.group(2))
    Lines = ['viewbox 0 0 %d %d' % (Width * Scale, Height * Scale), 'fill white', 'rectangle 0,0 %d,%d' % (Width * Scale, Height * Scale), 'stroke-linecap round', 'stroke-linejoin round']
    for Layer in re.finditer(r'<g id="(\w+)"[^>]*stroke-width="([\d.]+)"(?: stroke-dasharray="([\d. ]+)")?>(.*?)</g>', Source, re.S):
        Name, Weight, Dashes, Body = Layer.groups()
        for Polyline in re.finditer(r'<polyline points="([^"]+)"', Body):
            Lines += ['push graphic-context', 'fill none', 'stroke black', 'stroke-width %.2f' % max(float(Weight) * Scale, 1.0)]
            if Dashes:
                Lines.append('stroke-dasharray ' + ' '.join('%.1f' % (float(Dash) * Scale) for Dash in Dashes.split()))
            Lines += ['polyline ' + ScalePoints(Polyline.group(1), Scale), 'pop graphic-context']
        for Polygon in re.finditer(r'<polygon fill="#000000" stroke="none" points="([^"]+)"', Body):
            Lines += ['push graphic-context', 'fill black', 'stroke none', 'polygon ' + ScalePoints(Polygon.group(1), Scale), 'pop graphic-context']
        for Text in re.finditer(r'<text[^>]*x="([\d.-]+)" y="([\d.-]+)" font-size="([\d.]+)" text-anchor="(\w+)"(?: transform="rotate\(([-\d.]+) [\d.-]+ [\d.-]+\)")?>([^<]*)</text>', Body):
            X, Y, Size, Anchor, Rotation, Content = Text.groups()
            Content = Content.replace('&lt;', '<').replace('&gt;', '>').replace('&quot;', '"').replace('&amp;', '&').replace("'", "\\'")
            Lines += ['push graphic-context', 'translate %.2f,%.2f' % (float(X) * Scale, float(Y) * Scale)]
            if Rotation:
                Lines.append('rotate %s' % Rotation)
            Lines += ['fill black', 'stroke none', 'font-size %.2f' % (float(Size) * Scale), 'text-anchor %s' % Anchor]
            if Font:
                Lines.append('font "%s"' % Font)
            Lines += ["text 0,0 '%s'" % Content, 'pop graphic-context']
    Vector = Path[:-4] + '.mvg'
    open(Vector, 'w', encoding='utf-8').write('\n'.join(Lines) + '\n')
    Result = subprocess.run(['convert', '-size', '%dx%d' % (Width * Scale, Height * Scale), 'mvg:' + Vector, Path[:-4] + '.png'], capture_output=True, text=True)
    return Result.stderr.strip()

# ------------------------------------------------------------------------------------------------------------------------
#                                                    ENTRY POINT
# ------------------------------------------------------------------------------------------------------------------------

if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description='Render kernel drawing SVGs to PNG through ImageMagick MVG')
    Parser.add_argument('--font', default='', help='TrueType face for texts (a monospace face matches the layout estimate)')
    Parser.add_argument('--pixels-per-millimetre', type=float, default=4.0)
    Parser.add_argument('Files', nargs='+')
    Options = Parser.parse_args()
    Failed = False
    for File in Options.Files:
        Message = RenderSheet(File, Options.font, Options.pixels_per_millimetre)
        if Message:
            print('%s: %s' % (File, Message[:300]))
            Failed = Failed or 'error' in Message.lower()
    sys.exit(1 if Failed else 0)
