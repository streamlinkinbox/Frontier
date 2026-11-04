/*==========================================================================================================================================
                                                              GLYPHSYMBOL.CPP
==========================================================================================================================================*/
// 🧩 The demo's glyphs: lucide path data parsed once (path, circle, rect, line; M L H V C S Q T A Z) into stroked
//    polylines with round caps, and the Native* artwork through IconPresentation.
//
// 📝 Path data from lucide-static 1.57.0 (https://lucide.dev), as the demo's EditorIcons.js carries it, used under the ISC License:
//    Copyright (c) 2026 Lucide Icons and Contributors. Permission to use, copy, modify, and/or distribute this software for
//    any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice
//    appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
//    SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. NativeOrbit and NativeBake are the demo's
//    own marks.

#include "Engine/Interface/Controls/GlyphSymbol.h"

#include "Engine/Interface/IconPresentation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier
{

namespace
{

constexpr float GlyphPi = 3.14159265358979f;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PATH DATA
//------------------------------------------------------------------------------------------------------------------------

struct GlyphSource
{
    const char* Name;
    const char* Markup;
};

// 📝 EditorIcons.js EditorGlyphs, then ConstructGlyph's Marks (attribute quotes single, otherwise verbatim).
constexpr GlyphSource GlyphSources[] = {
    {"ScanCorners","<path d='M3 8V5a2 2 0 0 1 2-2h3M16 3h3a2 2 0 0 1 2 2v3M21 16v3a2 2 0 0 1-2 2h-3M8 21H5a2 2 0 0 1-2-2v-3'/>"},
    {"Info", "<circle cx='12' cy='12' r='9'/><path d='M12 11v6M12 7v1'/>"},
    {"TriangleAlert", "<path d='M12 3 22 21H2Z'/><path d='M12 9v5M12 17v1'/>"},
    {"CircleAlert", "<circle cx='12' cy='12' r='9'/><path d='M12 6v8M12 17v1'/>"},
    {"Focus", "<path d='M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5'/><circle cx='12' cy='12' r='3'/>"},
    {"MousePointer", "<path d='m4 3 15 10-7 1-3 7Z'/>"},
    {"Power", "<path d='M12 2v8'/><path d='M6 5a9 9 0 1 0 12 0'/>"},
    {"Layers",
     "<path d='M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 "
     "0-1.83z'/> <path d='M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12'/> "
     "<path d='M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17'/>"},
    {"Box",
     "<path d='M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 "
     "0l7-4A2 2 0 0 0 21 16Z'/> <path d='m3.3 7 8.7 5 8.7-5'/> <path d='M12 22V12'/>"},
    {"Sun",
     "<circle cx='12' cy='12' r='4'/> <path d='M12 2v2'/> <path d='M12 20v2'/> <path d='m4.93 4.93 1.41 1.41'/> "
     "<path d='m17.66 17.66 1.41 1.41'/> <path d='M2 12h2'/> <path d='M20 12h2'/> <path d='m6.34 17.66-1.41 1.41'/> "
     "<path d='m19.07 4.93-1.41 1.41'/>"},
    {"Camera",
     "<path d='M13.997 4a2 2 0 0 1 1.76 1.05l.486.9A2 2 0 0 0 18.003 7H20a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 "
     "1-2-2V9a2 2 0 0 1 2-2h1.997a2 2 0 0 0 1.759-1.048l.489-.904A2 2 0 0 1 10.004 4z'/> "
     "<circle cx='12' cy='13' r='3'/>"},
    {"Car",
     "<path d='M19 17h2c.6 0 1-.4 1-1v-3c0-.9-.7-1.7-1.5-1.9C18.7 10.6 16 10 16 "
     "10s-1.3-1.4-2.2-2.3c-.5-.4-1.1-.7-1.8-.7H5c-.6 0-1.1.4-1.4.9l-1.4 2.9A3.7 3.7 0 0 0 2 12v4c0 .6.4 1 1 1h2'/> "
     "<circle cx='7' cy='17' r='2'/> <path d='M9 17h6'/> <circle cx='17' cy='17' r='2'/>"},
    {"Flame", "<path d='M12 3q1 4 4 6.5t3 5.5a1 1 0 0 1-14 0 5 5 0 0 1 1-3 1 1 0 0 0 5 0c0-2-1.5-3-1.5-5q0-2 2.5-4'/>"},
    {"Wind",
     "<path d='M12.8 19.6A2 2 0 1 0 14 16H2'/> <path d='M17.5 8a2.5 2.5 0 1 1 2 4H2'/> "
     "<path d='M9.8 4.4A2 2 0 1 1 11 8H2'/>"},
    {"Sparkles",
     "<path d='M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l-5.558 "
     "1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 "
     "0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594z'/> <path d='M20 2v4'/> <path d='M22 4h-4'/> "
     "<circle cx='4' cy='20' r='2'/>"},
    {"Hammer",
     "<path d='m15 12-9.373 9.373a1 1 0 0 1-3.001-3L12 9'/> <path d='m18 15 4-4'/> "
     "<path d='m21.5 11.5-1.914-1.914A2 2 0 0 1 19 8.172v-.344a2 2 0 0 0-.586-1.414l-1.657-1.657A6 6 0 0 0 12.516 "
     "3H9l1.243 1.243A6 6 0 0 1 12 8.485V10l2 2h1.172a2 2 0 0 1 1.414.586L18.5 14.5'/>"},
    {"Gauge", "<path d='m12 14 4-4'/> <path d='M3.34 19a10 10 0 1 1 17.32 0'/>"},
    {"SlidersHorizontal",
     "<path d='M10 5H3'/> <path d='M12 19H3'/> <path d='M14 3v4'/> <path d='M16 17v4'/> <path d='M21 12h-9'/> "
     "<path d='M21 19h-5'/> <path d='M21 5h-7'/> <path d='M8 10v4'/> <path d='M8 12H3'/>"},
    {"Eye",
     "<path d='M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 "
     "0'/> <circle cx='12' cy='12' r='3'/>"},
    {"EyeOff",
     "<path d='M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 2.49'/> "
     "<path d='M14.084 14.158a3 3 0 0 1-4.242-4.242'/> "
     "<path d='M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 4.446-5.143'/> "
     "<path d='m2 2 20 20'/>"},
    {"ChevronDown", "<path d='m6 9 6 6 6-6'/>"},
    {"ChevronRight", "<path d='m9 18 6-6-6-6'/>"},
    {"ChevronLeft", "<path d='m15 18-6-6 6-6'/>"},
    {"X", "<path d='M18 6 6 18'/> <path d='m6 6 12 12'/>"},
    {"Search", "<path d='m21 21-4.34-4.34'/> <circle cx='11' cy='11' r='8'/>"},
    {"RotateCcw", "<path d='M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8'/> <path d='M3 3v5h5'/>"},
    {"Cpu",
     "<path d='M12 20v2'/> <path d='M12 2v2'/> <path d='M17 20v2'/> <path d='M17 2v2'/> <path d='M2 12h2'/> "
     "<path d='M2 17h2'/> <path d='M2 7h2'/> <path d='M20 12h2'/> <path d='M20 17h2'/> <path d='M20 7h2'/> "
     "<path d='M7 20v2'/> <path d='M7 2v2'/> <rect x='4' y='4' width='16' height='16' rx='2'/> "
     "<rect x='8' y='8' width='8' height='8' rx='1'/>"},
    {"GridLattice",
     "<rect width='18' height='18' x='3' y='3' rx='2'/> <path d='M3 9h18'/> <path d='M3 15h18'/> <path d='M9 3v18'/> "
     "<path d='M15 3v18'/>"},
    {"CloudFog", "<path d='M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242'/> <path d='M16 17H7'/> <path d='M17 21H9'/>"},
    {"Droplets",
     "<path d='M7 16.3c2.2 0 4-1.83 4-4.05 0-1.16-.57-2.26-1.71-3.19S7.29 6.75 7 5.3c-.29 1.45-1.14 2.84-2.29 3.76S3 "
     "11.1 3 12.25c0 2.22 1.8 4.05 4 4.05z'/> "
     "<path d='M12.56 6.6A10.97 10.97 0 0 0 14 3.02c.5 2.5 2 4.9 4 6.5s3 3.5 3 5.5a6.98 6.98 0 0 1-11.91 4.97'/>"},
    {"Zap",
     "<path d='M15.914 4a1.5 1.5 0 00-2.474-1.561l-9 9A1.5 1.5 0 005.5 14h4.002a.5.5 0 01.471.666L8.086 20a1.5 1.5 0 "
     "002.475 1.56l9-9A1.5 1.5 0 0018.5 10h-3.997a.5.5 0 01-.472-.667z'/>"},
    {"Monitor",
     "<rect width='20' height='14' x='2' y='3' rx='2'/> <line x1='8' x2='16' y1='21' y2='21'/> "
     "<line x1='12' x2='12' y1='17' y2='21'/>"},
    {"Aperture",
     "<circle cx='12' cy='12' r='10'/> <path d='m14.31 8 5.74 9.94'/> <path d='M9.69 8h11.48'/> "
     "<path d='m7.38 12 5.74-9.94'/> <path d='M9.69 16 3.95 6.06'/> <path d='M14.31 16H2.83'/> "
     "<path d='m16.62 12-5.74 9.94'/>"},
    {"Scan",
     "<path d='M3 7V5a2 2 0 0 1 2-2h2'/> <path d='M17 3h2a2 2 0 0 1 2 2v2'/> <path d='M21 17v2a2 2 0 0 1-2 2h-2'/> "
     "<path d='M7 21H5a2 2 0 0 1-2-2v-2'/>"},
    {"LayoutGrid",
     "<rect width='7' height='7' x='3' y='3' rx='1'/> <rect width='7' height='7' x='14' y='3' rx='1'/> "
     "<rect width='7' height='7' x='14' y='14' rx='1'/> <rect width='7' height='7' x='3' y='14' rx='1'/>"},
    {"Map",
     "<path d='M14.106 5.553a2 2 0 0 0 1.788 0l3.659-1.83A1 1 0 0 1 21 4.619v12.764a1 1 0 0 1-.553.894l-4.553 2.277a2 "
     "2 0 0 1-1.788 0l-4.212-2.106a2 2 0 0 0-1.788 0l-3.659 1.83A1 1 0 0 1 3 19.381V6.618a1 1 0 0 1 "
     ".553-.894l4.553-2.277a2 2 0 0 1 1.788 0z'/> <path d='M15 5.764v15'/> <path d='M9 3.236v15'/>"},
    {"Boxes",
     "<path d='M2.97 12.92A2 2 0 0 0 2 14.63v3.24a2 2 0 0 0 .97 1.71l3 1.8a2 2 0 0 0 2.06 0L12 19v-5.5l-5-3-4.03 "
     "2.42Z'/> <path d='m7 16.5-4.74-2.85'/> <path d='m7 16.5 5-3'/> <path d='M7 16.5v5.17'/> "
     "<path d='M12 13.5V19l3.97 2.38a2 2 0 0 0 2.06 0l3-1.8a2 2 0 0 0 .97-1.71v-3.24a2 2 0 0 0-.97-1.71L17 10.5l-5 "
     "3Z'/> <path d='m17 16.5-5-3'/> <path d='m17 16.5 4.74-2.85'/> <path d='M17 16.5v5.17'/> "
     "<path d='M7.97 4.42A2 2 0 0 0 7 6.13v4.37l5 3 5-3V6.13a2 2 0 0 0-.97-1.71l-3-1.8a2 2 0 0 0-2.06 0l-3 1.8Z'/> "
     "<path d='M12 8 7.26 5.15'/> <path d='m12 8 4.74-2.85'/> <path d='M12 13.5V8'/>"},
    {"Activity",
     "<path d='M22 12h-2.48a2 2 0 0 0-1.93 1.46l-2.35 8.36a.25.25 0 0 1-.48 0L9.24 2.18a.25.25 0 0 0-.48 0l-2.35 "
     "8.36A2 2 0 0 1 4.49 12H2'/>"},
    {"CircleDot", "<circle cx='12' cy='12' r='1'/> <circle cx='12' cy='12' r='10'/>"},
    {"Play", "<path d='M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z'/>"},
    {"Pause", "<rect x='14' y='3' width='5' height='18' rx='1'/> <rect x='5' y='3' width='5' height='18' rx='1'/>"},
    {"Bomb",
     "<circle cx='11' cy='13' r='9'/> "
     "<path d='M14.35 4.65 16.3 2.7a2.41 2.41 0 0 1 3.4 0l1.6 1.6a2.4 2.4 0 0 1 0 3.4l-1.95 1.95'/> "
     "<path d='m22 2-1.5 1.5'/>"},
    {"Gem",
     "<path d='M10.5 3 8 9l4 13 4-13-2.5-6'/> "
     "<path d='M17 3a2 2 0 0 1 1.6.8l3 4a2 2 0 0 1 .013 2.382l-7.99 10.986a2 2 0 0 1-3.247 0l-7.99-10.986A2 2 0 0 1 "
     "2.4 7.8l2.998-3.997A2 2 0 0 1 7 3z'/> <path d='M2 9h20'/>"},
    {"Thermometer", "<path d='M14 4v10.54a4 4 0 1 1-4 0V4a2 2 0 0 1 4 0Z'/>"},
    {"Timer", "<line x1='10' x2='14' y1='2' y2='2'/> <line x1='12' x2='15' y1='14' y2='11'/> <circle cx='12' cy='14' r='8'/>"},
    {"Orbit",
     "<path d='M20.341 6.484A10 10 0 0 1 10.266 21.85'/> <path d='M3.659 17.516A10 10 0 0 1 13.74 2.152'/> "
     "<circle cx='12' cy='12' r='3'/> <circle cx='19' cy='5' r='2'/> <circle cx='5' cy='19' r='2'/>"},
    {"Shield",
     "<path d='M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 "
     "6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z'/>"},
    {"Moon",
     "<path d='M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 "
     "8.268c.344-.215.825-.004.803.401'/>"},
    {"Sunrise",
     "<path d='M12 2v8'/> <path d='m4.93 10.93 1.41 1.41'/> <path d='M2 18h2'/> <path d='M20 18h2'/> "
     "<path d='m19.07 10.93-1.41 1.41'/> <path d='M22 22H2'/> <path d='m8 6 4-4 4 4'/> "
     "<path d='M16 18a4 4 0 0 0-8 0'/>"},
    {"Lightbulb",
     "<path d='M15 14c.2-1 .7-1.7 1.5-2.5 1-.9 1.5-2.2 1.5-3.5A6 6 0 0 0 6 8c0 1 .2 2.2 1.5 3.5.7.7 1.3 1.5 1.5 "
     "2.5'/> <path d='M9 18h6'/> <path d='M10 22h4'/>"},
    {"Bug",
     "<path d='M12 20v-9'/> <path d='M14 7a4 4 0 0 1 4 4v3a6 6 0 0 1-12 0v-3a4 4 0 0 1 4-4z'/> "
     "<path d='M14.12 3.88 16 2'/> <path d='M21 21a4 4 0 0 0-3.81-4'/> <path d='M21 5a4 4 0 0 1-3.55 3.97'/> "
     "<path d='M22 13h-4'/> <path d='M3 21a4 4 0 0 1 3.81-4'/> <path d='M3 5a4 4 0 0 0 3.55 3.97'/> "
     "<path d='M6 13H2'/> <path d='m8 2 1.88 1.88'/> <path d='M9 7.13V6a3 3 0 1 1 6 0v1.13'/>"},
    {"Trash2",
     "<path d='M10 11v6'/> <path d='M14 11v6'/> <path d='M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6'/> "
     "<path d='M3 6h18'/> <path d='M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2'/>"},
    {"Plus", "<path d='M5 12h14'/> <path d='M12 5v14'/>"},
    {"Check", "<path d='M20 6 9 17l-5-5'/>"},
    {"Undo2", "<path d='M9 14 4 9l5-5'/> <path d='M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11'/>"},
    {"MoveSpace", "<path d='M5 3v16h16'/> <path d='m5 19 6-6'/> <path d='m2 6 3-3 3 3'/> <path d='m18 16 3 3-3 3'/>"},
    {"Flag", "<path d='M4 15s1-1 4-1 5 2 8 2 4-1 4-1V3s-1 1-4 1-5-2-8-2-4 1-4 1z'/> <line x1='4' x2='4' y1='22' y2='15'/>"},
    {"Target", "<circle cx='12' cy='12' r='10'/> <circle cx='12' cy='12' r='6'/> <circle cx='12' cy='12' r='2'/>"},
    {"Waves",
     "<path d='M2 6c.6.5 1.2 1 2.5 1C7 7 7 5 9.5 5c2.6 0 2.4 2 5 2 2.5 0 2.5-2 5-2 1.3 0 1.9.5 2.5 1'/> "
     "<path d='M2 12c.6.5 1.2 1 2.5 1 2.5 0 2.5-2 5-2 2.6 0 2.4 2 5 2 2.5 0 2.5-2 5-2 1.3 0 1.9.5 2.5 1'/> "
     "<path d='M2 18c.6.5 1.2 1 2.5 1 2.5 0 2.5-2 5-2 2.6 0 2.4 2 5 2 2.5 0 2.5-2 5-2 1.3 0 1.9.5 2.5 1'/>"},
    {"Route",
     "<circle cx='6' cy='19' r='3'/> <path d='M9 19h8.5a3.5 3.5 0 0 0 0-7h-11a3.5 3.5 0 0 1 0-7H15'/> "
     "<circle cx='18' cy='5' r='3'/>"},
    {"TrendingUp", "<path d='M16 7h6v6'/> <path d='m22 7-8.5 8.5-5-5L2 17'/>"},
    {"TrafficCone",
     "<path d='M16.05 10.966a5 2.5 0 0 1-8.1 0'/> "
     "<path d='m16.923 14.049 4.48 2.04a1 1 0 0 1 .001 1.831l-8.574 3.9a2 2 0 0 1-1.66 0l-8.574-3.91a1 1 0 0 1 "
     "0-1.83l4.484-2.04'/> <path d='M16.949 14.14a5 2.5 0 1 1-9.9 0L10.063 3.5a2 2 0 0 1 3.874 0z'/> "
     "<path d='M9.194 6.57a5 2.5 0 0 0 5.61 0'/>"},
    {"Palette",
     "<path d='M12 22a1 1 0 0 1 0-20 10 9 0 0 1 10 9 5 5 0 0 1-5 5h-2.25a1.75 1.75 0 0 0-1.4 2.8l.3.4a1.75 1.75 0 0 "
     "1-1.4 2.8z'/> <circle cx='13.5' cy='6.5' r='.5' fill='currentColor'/> "
     "<circle cx='17.5' cy='10.5' r='.5' fill='currentColor'/> "
     "<circle cx='6.5' cy='12.5' r='.5' fill='currentColor'/> <circle cx='8.5' cy='7.5' r='.5' fill='currentColor'/>"},
    {"NativeOrbit",
     "<path d='M19.1 4.9A10 10 0 1 1 9.4 2.3'/><circle cx='19' cy='5' r='2'/><circle cx='5' cy='19' r='2'/>"
     "<circle cx='12' cy='12' r='3'/>"},
    {"NativeBake",
     "<path d='M12 5h7l2 2v12l-2 2H7l-2-2v-6M2 9h11M9 5l4 4-4 4M3 2v3M1.5 3.5h3'/>"
     "<rect x='14' y='14' width='3' height='3'/>"},
    // 📝 The engine shapes of the Construct dialog and their Outliner rows (Box is the cube), drawn in the 24-unit stroke style
    //    with arcs for the ellipses.
    {"Sphere", "<circle cx='12' cy='12' r='9'/> <path d='M3 12a9 3.5 0 0 0 18 0'/> <path d='M12 3a3.5 9 0 0 0 0 18'/>"},
    {"Cylinder", "<path d='M4 6a8 3 0 1 0 16 0a8 3 0 1 0-16 0'/> <path d='M4 6v12'/> <path d='M20 6v12'/> <path d='M4 18a8 3 0 0 0 16 0'/>"},
    {"Cone", "<path d='M12 3 4 18'/> <path d='m12 3 8 15'/> <path d='M4 18a8 3 0 0 0 16 0'/>"},
    {"Torus", "<path d='M2 12a10 6 0 1 0 20 0a10 6 0 1 0-20 0'/> <path d='M7.5 11.2a4.5 2 0 0 0 9 0'/> <path d='M9 12.4a3 1.3 0 0 1 6 0'/>"},
    {"Square", "<path d='M2 16 8 8h14l-6 8Z'/>"},
    // 📝 The geometry inspector and its Outliner rows (lucide: lock, move, atom, fence, warehouse, tower-control, lamp-floor,
    //    copy, wrench, chevron-up, arrow-up-right; CastShadow and TrafficLight drawn in the same 24-unit stroke style).
    {"Lock", "<rect width='18' height='11' x='3' y='11' rx='2'/> <path d='M7 11V7a5 5 0 0 1 10 0v4'/>"},
    {"Move",
     "<path d='M12 2v20'/> <path d='m15 19-3 3-3-3'/> <path d='m19 9 3 3-3 3'/> <path d='M2 12h20'/> <path d='m5 9-3 3 3 3'/> "
     "<path d='m9 5 3-3 3 3'/>"},
    {"Atom",
     "<circle cx='12' cy='12' r='1'/> <path d='M20.2 20.2c2.04-2.03.02-7.36-4.5-11.9-4.54-4.52-9.87-6.54-11.9-4.5-2.04 2.03-.02 7.36 4.5 11.9 "
     "4.54 4.52 9.87 6.54 11.9 4.5Z'/> <path d='M15.7 15.7c4.52-4.54 6.54-9.87 4.5-11.9-2.03-2.04-7.36-.02-11.9 4.5-4.52 4.54-6.54 9.87-4.5 "
     "11.9 2.03 2.04 7.36.02 11.9-4.5Z'/>"},
    {"CastShadow", "<path d='M4 8 9 5l5 3v6l-5 3-5-3Z'/> <path d='M4 8l5 3 5-3M9 11v6'/> <path d='M14 14l7 3.5-5 3-7-3.5'/>"},
    {"Fence",
     "<path d='M4 3 2 5v15c0 .6.4 1 1 1h2c.6 0 1-.4 1-1V5Z'/> <path d='M6 8h4'/> <path d='M6 18h4'/> "
     "<path d='m12 3-2 2v15c0 .6.4 1 1 1h2c.6 0 1-.4 1-1V5Z'/> <path d='M14 8h4'/> <path d='M14 18h4'/> "
     "<path d='m20 3-2 2v15c0 .6.4 1 1 1h2c.6 0 1-.4 1-1V5Z'/>"},
    {"Warehouse",
     "<path d='M22 8.35V20a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8.35A2 2 0 0 1 3.26 6.5l8-3.2a2 2 0 0 1 1.48 0l8 3.2A2 2 0 0 1 22 8.35Z'/> "
     "<path d='M6 18h12'/> <path d='M6 14h12'/> <rect width='12' height='12' x='6' y='10'/>"},
    {"TowerControl",
     "<path d='M18.2 12.27 20 6H4l1.8 6.27a1 1 0 0 0 .95.73h10.5a1 1 0 0 0 .96-.73Z'/> <path d='M8 13v9'/> <path d='M16 22v-9'/> "
     "<path d='m9 6 1 7'/> <path d='m15 6-1 7'/> <path d='M12 6V2'/> <path d='M13 2h-2'/>"},
    {"LampFloor", "<path d='M9 2h6l3 7H6l3-7Z'/> <path d='M12 9v13'/> <path d='M9 22h6'/>"},
    {"TrafficLight",
     "<rect x='7' y='2' width='10' height='17' rx='3'/> <circle cx='12' cy='6.5' r='1.6'/> <circle cx='12' cy='10.5' r='1.6'/> "
     "<circle cx='12' cy='14.5' r='1.6'/> <path d='M12 19v3'/>"},
    {"Copy", "<rect width='14' height='14' x='8' y='8' rx='2'/> <path d='M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2'/>"},
    {"Wrench",
     "<path d='M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 "
     "6 0 0 1 7.94-7.94l-3.76 3.76z'/>"},
    {"ChevronUp", "<path d='m18 15-6-6-6 6'/>"},
    {"ArrowUpRight", "<path d='M7 7h10v10'/> <path d='M7 17 17 7'/>"},
    // 📝 The force glyphs of the Force Field editor prototype (InspectorControls.js GlyphPaths); its orbit ellipse is drawn as two
    //    arcs (ForceOrbit, beside lucide's Orbit) and the blast's dashed ring as a plain one.
    {"Tornado", "<path d='M3 5h18M5.5 9h13M8 13h8.5M10 17h5M11.5 21h2'/>"},
    {"Attract", "<circle cx='12' cy='12' r='2.2'/><path d='M3 12h5M16 12h5M12 3v5M12 16v5M6 10l2 2-2 2M18 10l-2 2 2 2M10 6l2 2 2-2M10 18l2-2 2 2'/>"},
    {"Repel",
     "<circle cx='12' cy='12' r='2.2'/><path d='M2.5 12h5M16.5 12h5M12 2.5v5M12 16.5v5M4.5 10l-2 2 2 2M19.5 10l2 2-2 2M10 4.5l2-2 2 2M10 19.5l2 2 2-2'/>"},
    {"ForceOrbit", "<circle cx='12' cy='12' r='2.4'/><path d='M20.68 8.14A9.5 4.4 -24 1 0 3.32 15.86A9.5 4.4 -24 1 0 20.68 8.14'/>"},
    {"Magnet", "<path d='M5 3.5h4.5v8a2.5 2.5 0 0 0 5 0v-8H19v8a7 7 0 0 1-14 0z'/><path d='M5 7.5h4.5M14.5 7.5H19'/>"},
    {"Thermal", "<path d='M8 20V8M5 11l3-3 3 3M16 20V4.5M13 7.5l3-3 3 3'/>"},
    {"Downburst", "<path d='M12 3v10M9 10l3 3 3-3'/><path d='M3 20c2.8-3.6 5.6-4.4 9-4.4s6.2.8 9 4.4'/>"},
    {"Front", "<path d='M14.5 3c-3 3-3 15 0 18'/><path d='M3 8h7.5M3 12h8M3 16h7.5'/><path d='M18 9l2.5 3-2.5 3'/>"},
    {"Jet", "<path d='M3 8.5h11l6 3.5-6 3.5H3'/><path d='M6.5 12h6'/>"},
    {"Puff", "<circle cx='9' cy='13' r='4.5'/><circle cx='16' cy='10.5' r='3.5'/><path d='M3 20h18'/>"},
    {"Blast", "<circle cx='12' cy='12' r='2'/><circle cx='12' cy='12' r='5.6'/><circle cx='12' cy='12' r='9.2'/>"},
    {"Wake",
     "<path d='M2.5 15.5h3.5l2.2-4.5h6.6l3.2 4.5h3.5'/><circle cx='7.5' cy='16.8' r='1.7'/><circle cx='16.5' cy='16.8' r='1.7'/>"
     "<path d='M3 8h4M2.5 11h2'/>"},
    {"Current", "<rect x='3' y='5' width='18' height='14' rx='2.5'/><path d='M6.5 10h9.5l-2-2M6.5 14h9.5l-2 2'/>"},
    {"Lift", "<path d='M12 18.5V5M8 9l4-4 4 4'/><path d='M5 20.5h14'/>"},
    {"Gravity", "<path d='M12 3.5V17M8 13l4 4 4-4'/><path d='M5 20.5h14'/>"},
    {"Damp", "<path d='M2.5 9c1.6-3.6 3.2-3.6 4.8 0s3.2 2.6 4.8 0 3-1.8 4.6 0 2.4 1.2 4.8 0'/><path d='M2.5 15.5h19'/>"},
    {"Swirl", "<path d='M12 12.2a2.3 2.3 0 1 1 2.3 2.3A4.8 4.8 0 1 1 12 7.4a7.2 7.2 0 1 1-7.2 7.2'/>"},
    {"Library", "<path d='M4 4.5h4v15H4zM10 4.5h4v15h-4z'/><path d='M15.8 5.8l3.4-.9 3.3 13.6-3.4.9z'/>"},
    {"Crosshair", "<circle cx='12' cy='12' r='7'/><path d='M12 2.5v5M12 16.5v5M2.5 12h5M16.5 12h5'/>"},
    {"Compass", "<circle cx='12' cy='12' r='9'/><path d='M15.5 8.5l-2 5-5 2 2-5z'/>"},
    {"Timeline", "<path d='M3 12h18'/><circle cx='7' cy='12' r='2'/><circle cx='15.5' cy='12' r='2'/><path d='M7 5v3M15.5 16v3'/>"},
    {"Globe", "<circle cx='12' cy='12' r='9'/><path d='M3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18'/>"},
};

// 📝 ConstructGlyph's Native map: the environment artwork each Native* name shows.
struct ArtworkSource
{
    const char* Name;
    IconSymbol  Symbol;
};

constexpr ArtworkSource ArtworkSources[] = {
    {"NativeClouds", IconSymbol::Clouds},        {"NativeLocalCloud", IconSymbol::LocalCloud}, {"NativeLocalFog", IconSymbol::LocalFog},
    {"NativeEnvironment", IconSymbol::FolderEnvironment}, {"NativeSun", IconSymbol::Sun},     {"NativeAtmosphere", IconSymbol::SkyScattering},
    {"NativeStars", IconSymbol::OutlinerStars},  {"NativeMoon", IconSymbol::Moon},            {"NativeFlare", IconSymbol::LensFlare},
    {"NativeFog", IconSymbol::Fog},
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINE PARSING
//------------------------------------------------------------------------------------------------------------------------

// 📝 One stroked run in the 24-unit box; Filled marks fill="currentColor" shapes.
struct GlyphOutline
{
    std::vector<ImVec2> Points{};
    bool                Closed = false;
    bool                Filled = false;
};

class PathReader
{
  public:
    explicit PathReader(std::string_view InText) noexcept : Text(InText) {}

    void SkipSeparators() noexcept
    {
        while (Cursor < Text.size() && (std::isspace(static_cast<unsigned char>(Text[Cursor])) || Text[Cursor] == ',')) ++Cursor;
    }
    [[nodiscard]] bool AtEnd() noexcept
    {
        SkipSeparators();
        return Cursor >= Text.size();
    }
    [[nodiscard]] bool AtNumber() noexcept
    {
        SkipSeparators();
        if (Cursor >= Text.size()) return false;
        const char Lead = Text[Cursor];
        return std::isdigit(static_cast<unsigned char>(Lead)) || Lead == '-' || Lead == '+' || Lead == '.';
    }
    [[nodiscard]] char ReadCommand() noexcept
    {
        SkipSeparators();
        return Cursor < Text.size() ? Text[Cursor++] : '\0';
    }
    // 📝 SVG numbers run together: "1.66-1.66", ".58.91" (a second point starts a new number).
    [[nodiscard]] float ReadNumber() noexcept
    {
        SkipSeparators();
        const size_t Start = Cursor;
        if (Cursor < Text.size() && (Text[Cursor] == '-' || Text[Cursor] == '+')) ++Cursor;
        bool Point = false, Exponent = false;
        while (Cursor < Text.size())
        {
            const char Glyph = Text[Cursor];
            if (std::isdigit(static_cast<unsigned char>(Glyph))) { ++Cursor; continue; }
            if (Glyph == '.' && !Point && !Exponent) { Point = true; ++Cursor; continue; }
            if ((Glyph == 'e' || Glyph == 'E') && !Exponent)
            {
                Exponent = true;
                ++Cursor;
                if (Cursor < Text.size() && (Text[Cursor] == '-' || Text[Cursor] == '+')) ++Cursor;
                continue;
            }
            break;
        }
        return static_cast<float>(std::strtod(std::string(Text.substr(Start, Cursor - Start)).c_str(), nullptr));
    }
    // 📝 Arc flags are single characters and may touch the next number ("a.5.5 0 01.471.666").
    [[nodiscard]] bool ReadFlag() noexcept
    {
        SkipSeparators();
        return Cursor < Text.size() && Text[Cursor++] == '1';
    }

  private:
    std::string_view Text;
    size_t           Cursor = 0;
};

[[nodiscard]] float ReadAttribute(std::string_view Element, const char* Attribute, float Fallback) noexcept
{
    const std::string Key = std::string(" ") + Attribute + "='";
    const size_t      At  = Element.find(Key);
    if (At == std::string_view::npos) return Fallback;
    return static_cast<float>(std::strtod(std::string(Element.substr(At + Key.size())).c_str(), nullptr));
}

[[nodiscard]] std::string_view ReadText(std::string_view Element, const char* Attribute) noexcept
{
    const std::string Key = std::string(" ") + Attribute + "='";
    const size_t      At  = Element.find(Key);
    if (At == std::string_view::npos) return {};
    const size_t Start = At + Key.size();
    const size_t End   = Element.find('\'', Start);
    return End == std::string_view::npos ? std::string_view{} : Element.substr(Start, End - Start);
}

void AppendCubic(std::vector<ImVec2>& Points, ImVec2 P0, ImVec2 P1, ImVec2 P2, ImVec2 P3)
{
    constexpr int Steps = 12;
    for (int Step = 1; Step <= Steps; ++Step)
    {
        const float T = static_cast<float>(Step) / Steps, U = 1.0f - T;
        const float A = U * U * U, B = 3.0f * U * U * T, C = 3.0f * U * T * T, D = T * T * T;
        Points.push_back(ImVec2(A * P0.x + B * P1.x + C * P2.x + D * P3.x, A * P0.y + B * P1.y + C * P2.y + D * P3.y));
    }
}

void AppendQuadratic(std::vector<ImVec2>& Points, ImVec2 P0, ImVec2 P1, ImVec2 P2)
{
    constexpr int Steps = 10;
    for (int Step = 1; Step <= Steps; ++Step)
    {
        const float T = static_cast<float>(Step) / Steps, U = 1.0f - T;
        Points.push_back(ImVec2(U * U * P0.x + 2.0f * U * T * P1.x + T * T * P2.x, U * U * P0.y + 2.0f * U * T * P1.y + T * T * P2.y));
    }
}

// 📝 SVG elliptical arc, endpoint to centre parameterization (SVG 1.1 F.6.5), sampled every ≤ 15°.
void AppendArc(std::vector<ImVec2>& Points, ImVec2 From, float RadiusX, float RadiusY, float RotationDegrees, bool Large, bool Sweep, ImVec2 To)
{
    if (RadiusX == 0.0f || RadiusY == 0.0f || (From.x == To.x && From.y == To.y))
    {
        Points.push_back(To);
        return;
    }
    RadiusX = std::fabs(RadiusX);
    RadiusY = std::fabs(RadiusY);
    const float φ = RotationDegrees * GlyphPi / 180.0f, Cosφ = std::cos(φ), Sinφ = std::sin(φ);
    const float HalfX = 0.5f * (From.x - To.x), HalfY = 0.5f * (From.y - To.y);
    const float PrimeX = Cosφ * HalfX + Sinφ * HalfY, PrimeY = -Sinφ * HalfX + Cosφ * HalfY;
    const float Λ = PrimeX * PrimeX / (RadiusX * RadiusX) + PrimeY * PrimeY / (RadiusY * RadiusY);
    if (Λ > 1.0f)
    {
        RadiusX *= std::sqrt(Λ);
        RadiusY *= std::sqrt(Λ);
    }
    const float Numerator   = RadiusX * RadiusX * RadiusY * RadiusY - RadiusX * RadiusX * PrimeY * PrimeY - RadiusY * RadiusY * PrimeX * PrimeX;
    const float Denominator = RadiusX * RadiusX * PrimeY * PrimeY + RadiusY * RadiusY * PrimeX * PrimeX;
    float       Factor      = Denominator > 0.0f ? std::sqrt(std::max(0.0f, Numerator / Denominator)) : 0.0f;
    if (Large == Sweep) Factor = -Factor;
    const float CentrePrimeX = Factor * RadiusX * PrimeY / RadiusY, CentrePrimeY = -Factor * RadiusY * PrimeX / RadiusX;
    const float CentreX = Cosφ * CentrePrimeX - Sinφ * CentrePrimeY + 0.5f * (From.x + To.x);
    const float CentreY = Sinφ * CentrePrimeX + Cosφ * CentrePrimeY + 0.5f * (From.y + To.y);
    auto Angle = [](float Ux, float Uy, float Vx, float Vy) {
        const float Dot = Ux * Vx + Uy * Vy, Length = std::sqrt((Ux * Ux + Uy * Uy) * (Vx * Vx + Vy * Vy));
        float       θ   = std::acos(std::clamp(Length > 0.0f ? Dot / Length : 1.0f, -1.0f, 1.0f));
        return (Ux * Vy - Uy * Vx) < 0.0f ? -θ : θ;
    };
    const float θStart = Angle(1.0f, 0.0f, (PrimeX - CentrePrimeX) / RadiusX, (PrimeY - CentrePrimeY) / RadiusY);
    float       Δθ     = Angle((PrimeX - CentrePrimeX) / RadiusX, (PrimeY - CentrePrimeY) / RadiusY, (-PrimeX - CentrePrimeX) / RadiusX,
                               (-PrimeY - CentrePrimeY) / RadiusY);
    if (!Sweep && Δθ > 0.0f) Δθ -= 2.0f * GlyphPi;
    if (Sweep && Δθ < 0.0f) Δθ += 2.0f * GlyphPi;
    const int Steps = std::max(2, static_cast<int>(std::ceil(std::fabs(Δθ) / (GlyphPi / 12.0f))));
    for (int Step = 1; Step <= Steps; ++Step)
    {
        const float θ = θStart + Δθ * static_cast<float>(Step) / static_cast<float>(Steps);
        const float X = RadiusX * std::cos(θ), Y = RadiusY * std::sin(θ);
        Points.push_back(ImVec2(Cosφ * X - Sinφ * Y + CentreX, Sinφ * X + Cosφ * Y + CentreY));
    }
}

void ParsePath(std::string_view Data, std::vector<GlyphOutline>& Outlines, bool Filled)
{
    PathReader Reader(Data);
    ImVec2     Pen(0.0f, 0.0f), Start(0.0f, 0.0f), LastControl(0.0f, 0.0f);
    char       Command = '\0', Previous = '\0';
    auto       Open    = [&]() -> GlyphOutline& {
        if (Outlines.empty() || Outlines.back().Closed || Outlines.back().Points.empty()) Outlines.push_back({});
        Outlines.back().Filled = Filled;
        return Outlines.back();
    };
    while (!Reader.AtEnd())
    {
        if (!Reader.AtNumber())
        {
            Command = Reader.ReadCommand();
        }
        else if (Command == 'M') Command = 'L';   // implicit lineto after a moveto
        else if (Command == 'm') Command = 'l';
        const bool Relative = std::islower(static_cast<unsigned char>(Command)) != 0;
        const auto Point    = [&](float X, float Y) { return Relative ? ImVec2(Pen.x + X, Pen.y + Y) : ImVec2(X, Y); };
        switch (std::toupper(static_cast<unsigned char>(Command)))
        {
        case 'M':
        {
            const float X = Reader.ReadNumber(), Y = Reader.ReadNumber();
            Pen = Start = Point(X, Y);
            Outlines.push_back({});
            Outlines.back().Filled = Filled;
            Outlines.back().Points.push_back(Pen);
            break;
        }
        case 'L':
        {
            const float X = Reader.ReadNumber(), Y = Reader.ReadNumber();
            Pen = Point(X, Y);
            Open().Points.push_back(Pen);
            break;
        }
        case 'H':
        {
            const float X = Reader.ReadNumber();
            Pen = ImVec2(Relative ? Pen.x + X : X, Pen.y);
            Open().Points.push_back(Pen);
            break;
        }
        case 'V':
        {
            const float Y = Reader.ReadNumber();
            Pen = ImVec2(Pen.x, Relative ? Pen.y + Y : Y);
            Open().Points.push_back(Pen);
            break;
        }
        case 'C':
        case 'S':
        {
            ImVec2 First;
            if (std::toupper(static_cast<unsigned char>(Command)) == 'C')
            {
                const float X1 = Reader.ReadNumber(), Y1 = Reader.ReadNumber();
                First = Point(X1, Y1);
            }
            else
            {
                const bool Chained = std::strchr("CcSs", Previous) != nullptr;
                First = Chained ? ImVec2(2.0f * Pen.x - LastControl.x, 2.0f * Pen.y - LastControl.y) : Pen;
            }
            const float X2 = Reader.ReadNumber(), Y2 = Reader.ReadNumber(), X = Reader.ReadNumber(), Y = Reader.ReadNumber();
            const ImVec2 Second = Point(X2, Y2), End = Point(X, Y);
            AppendCubic(Open().Points, Pen, First, Second, End);
            LastControl = Second;
            Pen         = End;
            break;
        }
        case 'Q':
        case 'T':
        {
            ImVec2 Control;
            if (std::toupper(static_cast<unsigned char>(Command)) == 'Q')
            {
                const float X1 = Reader.ReadNumber(), Y1 = Reader.ReadNumber();
                Control = Point(X1, Y1);
            }
            else
            {
                const bool Chained = std::strchr("QqTt", Previous) != nullptr;
                Control = Chained ? ImVec2(2.0f * Pen.x - LastControl.x, 2.0f * Pen.y - LastControl.y) : Pen;
            }
            const float  X = Reader.ReadNumber(), Y = Reader.ReadNumber();
            const ImVec2 End = Point(X, Y);
            AppendQuadratic(Open().Points, Pen, Control, End);
            LastControl = Control;
            Pen         = End;
            break;
        }
        case 'A':
        {
            const float RadiusX = Reader.ReadNumber(), RadiusY = Reader.ReadNumber(), Rotation = Reader.ReadNumber();
            const bool  Large = Reader.ReadFlag(), Sweep = Reader.ReadFlag();
            const float X = Reader.ReadNumber(), Y = Reader.ReadNumber();
            const ImVec2 End = Point(X, Y);
            AppendArc(Open().Points, Pen, RadiusX, RadiusY, Rotation, Large, Sweep, End);
            Pen = End;
            break;
        }
        case 'Z':
        {
            if (!Outlines.empty()) Outlines.back().Closed = true;
            Pen = Start;
            break;
        }
        default: return;   // malformed data: keep what was read
        }
        Previous = Command;
    }
}

void AppendEllipse(std::vector<GlyphOutline>& Outlines, ImVec2 Centre, float RadiusX, float RadiusY, bool Filled)
{
    GlyphOutline Outline;
    Outline.Closed = true;
    Outline.Filled = Filled;
    constexpr int Steps = 32;
    for (int Step = 0; Step < Steps; ++Step)
    {
        const float θ = 2.0f * GlyphPi * static_cast<float>(Step) / Steps;
        Outline.Points.push_back(ImVec2(Centre.x + RadiusX * std::cos(θ), Centre.y + RadiusY * std::sin(θ)));
    }
    Outlines.push_back(std::move(Outline));
}

// 📝 <rect> with rounded corners (rx), traced clockwise from the top edge.
void AppendRect(std::vector<GlyphOutline>& Outlines, float X, float Y, float Width, float Height, float Radius, bool Filled)
{
    GlyphOutline Outline;
    Outline.Closed = true;
    Outline.Filled = Filled;
    Radius         = std::clamp(Radius, 0.0f, 0.5f * std::min(Width, Height));
    const ImVec2 Corners[4] = {ImVec2(X + Width - Radius, Y + Radius), ImVec2(X + Width - Radius, Y + Height - Radius),
                               ImVec2(X + Radius, Y + Height - Radius), ImVec2(X + Radius, Y + Radius)};
    for (int Corner = 0; Corner < 4; ++Corner)
    {
        const float From = (-0.5f + 0.5f * static_cast<float>(Corner)) * GlyphPi;
        constexpr int Steps = 6;
        for (int Step = 0; Step <= Steps; ++Step)
        {
            const float θ = From + 0.5f * GlyphPi * static_cast<float>(Step) / Steps;
            Outline.Points.push_back(ImVec2(Corners[Corner].x + Radius * std::cos(θ), Corners[Corner].y + Radius * std::sin(θ)));
            if (Radius <= 0.0f) break;
        }
    }
    Outlines.push_back(std::move(Outline));
}

[[nodiscard]] std::vector<GlyphOutline> ParseMarkup(std::string_view Markup)
{
    std::vector<GlyphOutline> Outlines;
    size_t                    At = 0;
    while ((At = Markup.find('<', At)) != std::string_view::npos)
    {
        const size_t End = Markup.find('>', At);
        if (End == std::string_view::npos) break;
        const std::string_view Element = Markup.substr(At, End - At + 1);
        const bool             Filled  = ReadText(Element, "fill") == "currentColor";
        if (Element.rfind("<path", 0) == 0) ParsePath(ReadText(Element, "d"), Outlines, Filled);
        else if (Element.rfind("<circle", 0) == 0)
        {
            const float Radius = ReadAttribute(Element, "r", 0.0f);
            AppendEllipse(Outlines, ImVec2(ReadAttribute(Element, "cx", 0.0f), ReadAttribute(Element, "cy", 0.0f)), Radius, Radius, Filled);
        }
        else if (Element.rfind("<rect", 0) == 0)
        {
            AppendRect(Outlines, ReadAttribute(Element, "x", 0.0f), ReadAttribute(Element, "y", 0.0f), ReadAttribute(Element, "width", 0.0f),
                       ReadAttribute(Element, "height", 0.0f), ReadAttribute(Element, "rx", 0.0f), Filled);
        }
        else if (Element.rfind("<line", 0) == 0)
        {
            GlyphOutline Outline;
            Outline.Points = {ImVec2(ReadAttribute(Element, "x1", 0.0f), ReadAttribute(Element, "y1", 0.0f)),
                              ImVec2(ReadAttribute(Element, "x2", 0.0f), ReadAttribute(Element, "y2", 0.0f))};
            Outlines.push_back(std::move(Outline));
        }
        At = End + 1;
    }
    return Outlines;
}

// 📝 Each glyph's outlines, parsed on first use and retained for the session.
[[nodiscard]] const std::vector<GlyphOutline>* ResolveOutlines(std::string_view Name)
{
    static std::unordered_map<std::string, std::vector<GlyphOutline>> Retained;
    const auto Found = Retained.find(std::string(Name));
    if (Found != Retained.end()) return &Found->second;
    for (const GlyphSource& Source : GlyphSources)
    {
        if (Name == Source.Name) return &Retained.emplace(std::string(Name), ParseMarkup(Source.Markup)).first->second;
    }
    return nullptr;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLYPH SYMBOL
//------------------------------------------------------------------------------------------------------------------------

bool GlyphSymbol::QueryArtwork(const char* Name) noexcept
{
    if (Name == nullptr) return false;
    for (const ArtworkSource& Source : ArtworkSources)
    {
        if (std::strcmp(Source.Name, Name) == 0) return true;
    }
    return false;
}

bool GlyphSymbol::Construct(ImDrawList* DrawList, const char* Name, ImVec2 Min, float Size, float StrokeWidth, ImU32 Colour)
{
    if (DrawList == nullptr || Name == nullptr || Size <= 0.0f) return false;
    for (const ArtworkSource& Source : ArtworkSources)
    {
        if (std::strcmp(Source.Name, Name) == 0)
        {
            const float Opacity = static_cast<float>((Colour >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
            return IconPresentation::Draw(DrawList, Source.Symbol, Min, Size, Opacity, IconDomain::Editor, IM_COL32_WHITE);
        }
    }
    const std::vector<GlyphOutline>* Outlines = ResolveOutlines(Name);
    if (Outlines == nullptr) return false;
    const float Unit      = Size / 24.0f;
    const float Thickness = std::max(0.5f, StrokeWidth * Unit);
    std::vector<ImVec2> Screen;
    for (const GlyphOutline& Outline : *Outlines)
    {
        if (Outline.Points.empty()) continue;
        Screen.clear();
        for (const ImVec2& Point : Outline.Points) Screen.push_back(ImVec2(Min.x + Point.x * Unit, Min.y + Point.y * Unit));
        if (Outline.Filled)
        {
            DrawList->AddConvexPolyFilled(Screen.data(), static_cast<int>(Screen.size()), Colour);
            continue;
        }
        if (Screen.size() == 1) Screen.push_back(Screen.front());
        DrawList->AddPolyline(Screen.data(), static_cast<int>(Screen.size()), Colour, Outline.Closed ? ImDrawFlags_Closed : ImDrawFlags_None,
                              Thickness);
        if (!Outline.Closed)
        {
            // 📝 stroke-linecap: round.
            DrawList->AddCircleFilled(Screen.front(), 0.5f * Thickness, Colour, 8);
            DrawList->AddCircleFilled(Screen.back(), 0.5f * Thickness, Colour, 8);
        }
    }
    return true;
}

} // namespace Frontier
