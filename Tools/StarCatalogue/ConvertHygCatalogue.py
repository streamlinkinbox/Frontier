#!/usr/bin/env python3
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  ConvertHygCatalogue.py — HYG astronomical database → Slate's compact star catalogue
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
#  The HYG database is 119 000 stars and about 35 MB of CSV. Slate needs four numbers from ~9 000 of them, so the
#  conversion is a 240× reduction and the result is a reasonable thing to keep in a repository.
#
#  🔴 The converter is committed, not just its output. An opaque binary blob in a source tree is a liability: no
#  reader can tell what produced it, whether it is current, or how to regenerate it after a bug. This script plus
#  the documented source URL means the asset is always reproducible.
#
#  Usage:
#      curl -L -o hygdata_v41.csv \
#          https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/CURRENT/hygdata_v41.csv
#      python3 Tools/StarCatalogue/ConvertHygCatalogue.py hygdata_v41.csv \
#          EngineContent/StarCatalogue/BrightStars.bin
#
#  HYG is CC BY-SA 4.0 (astronexus/HYG-Database). It aggregates Hipparcos, Yale Bright Star and Gliese.
# ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

import csv
import math
import struct
import sys

# 6.5 is the naked-eye limit under a dark sky, and it is where the Bright Star Catalogue itself stops. Fainter
#    stars cannot be seen by definition, so carrying them would be paying for data that can never be rendered.
MAGNITUDE_LIMIT = 6.5

# 'SLAT' + version. A header rather than a bare array so a future format change is detectable rather than being
#    silently misread as coordinates.
MAGIC = 0x54414C53
VERSION = 1


def ColourIndexToRgb(ColourIndex):
    """B−V colour index → linear RGB.

    B−V is a temperature measurement: negative is hot and blue, positive is cool and red. Converting through
    temperature rather than assigning colours by taste is what makes Betelgeuse red and Rigel blue for the
    reason they actually are.

    Ballesteros' formula for the temperature, then a standard blackbody fit. Both are approximations, but the
    error is far below what a point source a pixel or two across can show.
    """
    Bv = max(-0.4, min(2.0, ColourIndex))
    Temperature = 4600.0 * (1.0 / (0.92 * Bv + 1.7) + 1.0 / (0.92 * Bv + 0.62))

    T = Temperature / 100.0

    if T <= 66.0:
        Red = 255.0
    else:
        Red = 329.698727446 * ((T - 60.0) ** -0.1332047592)

    if T <= 66.0:
        Green = 99.4708025861 * math.log(T) - 161.1195681661
    else:
        Green = 288.1221695283 * ((T - 60.0) ** -0.0755148492)

    if T >= 66.0:
        Blue = 255.0
    elif T <= 19.0:
        Blue = 0.0
    else:
        Blue = 138.5177312231 * math.log(T - 10.0) - 305.0447927307

    Clamp = lambda V: max(0.0, min(255.0, V)) / 255.0
    return Clamp(Red), Clamp(Green), Clamp(Blue)


def Convert(SourcePath, TargetPath):
    Stars = []

    # Slate's own BrightStars.csv carries a comment block above the header; HYG's does not. Skipping to the
    #    header line lets one converter read both, rather than having two that can drift apart.
    with open(SourcePath, newline='', encoding='utf-8') as Handle:
        Text = Handle.read()
    # ⚠️ HYG quotes its header row as of v41 ("id","hip",...), where earlier revisions wrote it bare. Searching
    #    only for `proper,` / `id,` matched neither, and because the fallback was a find() returning -1 the reader
    #    started one character before the end of the file and produced a catalogue of ZERO stars — silently, with
    #    a cheerful "0 stars to magnitude 6.5" and a 16-byte asset. Both spellings are accepted now, and a zero
    #    count is a hard error rather than a successful-looking no-op.
    HeaderAt = -1
    for Candidate in ('proper,', '"proper"', 'id,', '"id"'):
        HeaderAt = Text.find(Candidate)
        if HeaderAt >= 0:
            HeaderAt = Text.rfind('\n', 0, HeaderAt) + 1   # rewind to the start of that header LINE
            break
    if HeaderAt < 0:
        raise SystemExit(f'{SourcePath}: no recognisable CSV header')

    import io
    for Row in csv.DictReader(io.StringIO(Text[HeaderAt:])):
        if True:
            # The Sun is row 0 of HYG and would otherwise be rendered as a star at the origin.
            if Row.get('proper', '').strip() == 'Sol':
                continue
            try:
                Magnitude = float(Row['mag'])
            except (KeyError, ValueError):
                continue
            if Magnitude > MAGNITUDE_LIMIT:
                continue
            try:
                # HYG stores right ascension in HOURS, not degrees. Reading it as degrees puts every star in the
                #    wrong place by a factor of fifteen — the sky would still look like a sky, which is why this
                #    is worth stating rather than assuming.
                RightAscension = float(Row['ra']) * 15.0 * math.pi / 180.0
                Declination    = float(Row['dec']) * math.pi / 180.0
            except (KeyError, ValueError):
                continue

            try:
                ColourIndex = float(Row['ci'])
            except (KeyError, ValueError):
                ColourIndex = 0.65   # roughly solar, for the few rows with no photometry

            Red, Green, Blue = ColourIndexToRgb(ColourIndex)

            # Store a unit vector rather than the two angles: the shader needs a direction, and converting here
            #    means it is done 9 000 times at build rather than per pixel per frame.
            CosDeclination = math.cos(Declination)
            X = CosDeclination * math.cos(RightAscension)
            Y = CosDeclination * math.sin(RightAscension)
            Z = math.sin(Declination)

            # Magnitude → relative luminance. Each step of 1 is a factor of 2.512, by definition of the scale.
            #    Normalised so magnitude 0 is 1.0, which keeps the numbers in a sane range for a float.
            Luminance = 10.0 ** (-0.4 * Magnitude)

            Stars.append((X, Y, Z, Luminance, Red, Green, Blue, Magnitude))

    # Brightest first. The renderer can then stop early when it only wants the recognisable stars, and a
    #    truncated file is still a sensible sky rather than a random sample.
    Stars.sort(key=lambda S: S[7])

    with open(TargetPath, 'wb') as Handle:
        Handle.write(struct.pack('<III', MAGIC, VERSION, len(Stars)))
        Handle.write(struct.pack('<I', 0))   # reserved, keeps the header 16 bytes
        for X, Y, Z, Luminance, Red, Green, Blue, _ in Stars:
            Handle.write(struct.pack('<7f', X, Y, Z, Luminance, Red, Green, Blue))

    if not Stars:
        raise SystemExit(f'{SourcePath}: parsed 0 stars — the header was found but no row survived '
                         f'(check the ra/dec/mag/ci column names). Refusing to write an empty catalogue.')
    print(f'{len(Stars)} stars to magnitude {MAGNITUDE_LIMIT} → {TargetPath}')
    print(f'{16 + len(Stars) * 28} bytes')
    if Stars:
        Brightest = Stars[0]
        print(f'brightest: magnitude {Brightest[7]:+.2f}, colour ({Brightest[4]:.2f}, {Brightest[5]:.2f}, {Brightest[6]:.2f})')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        print('usage: ConvertHygCatalogue.py <hygdata_v41.csv> <output.bin>')
        sys.exit(1)
    Convert(sys.argv[1], sys.argv[2])
