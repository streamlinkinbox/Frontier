/*====================================================================================================================================
                                                     GLYPHOUTLINES.JS
====================================================================================================================================*/
// 🧩 Stroked 24x24 outline paths for every editor affordance, sized on demand

// 📝 Each entry is the inner markup of a 24x24 stroked icon. Geometry is transcribed from the
//    reference editor's icon set so the prototype matches it glyph for glyph.
const OutlinePaths =
{
    Box:            '<path d="M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"/><path d="m3.3 7 8.7 5 8.7-5"/><path d="M12 22V12"/>',
    Pointer:        '<path d="M12.586 12.586 19 19"/><path d="M3.688 3.037a.497.497 0 0 0-.651.651l6.5 15.999a.501.501 0 0 0 .947-.062l1.569-6.083a2 2 0 0 1 1.448-1.479l6.124-1.579a.5.5 0 0 0 .063-.947z"/>',
    Sparkles:       '<path d="M9.937 15.5A2 2 0 0 0 8.5 14.063l-6.135-1.582a.5.5 0 0 1 0-.962L8.5 9.936A2 2 0 0 0 9.937 8.5l1.582-6.135a.5.5 0 0 1 .963 0L14.063 8.5A2 2 0 0 0 15.5 9.937l6.135 1.581a.5.5 0 0 1 0 .964L15.5 14.063a2 2 0 0 0-1.437 1.437l-1.582 6.135a.5.5 0 0 1-.963 0z"/>',
    Database:       '<ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M3 5V19A9 3 0 0 0 21 19V5"/><path d="M3 12A9 3 0 0 0 21 12"/>',
    Calculator:     '<rect width="16" height="20" x="4" y="2" rx="2"/><line x1="8" x2="16" y1="6" y2="6"/><line x1="16" x2="16" y1="14" y2="18"/><path d="M16 10h.01"/><path d="M12 10h.01"/><path d="M8 10h.01"/><path d="M12 14h.01"/><path d="M8 14h.01"/><path d="M12 18h.01"/><path d="M8 18h.01"/>',
    Branching:      '<circle cx="18" cy="18" r="3"/><circle cx="6" cy="6" r="3"/><path d="M6 21V9a9 9 0 0 0 9 9"/>',
    Strata:         '<path d="m12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83z"/><path d="M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12"/><path d="M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17"/>',
    Configure:      '<path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/>',

    Plus:           '<path d="M5 12h14"/><path d="M12 5v14"/>',
    Minus:          '<path d="M5 12h14"/>',
    Trash:          '<path d="M3 6h18"/><path d="M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6"/><path d="M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2"/><line x1="10" x2="10" y1="11" y2="17"/><line x1="14" x2="14" y1="11" y2="17"/>',
    Contract:       '<path d="m14 10 7-7"/><path d="M20 10h-6V4"/><path d="m3 21 7-7"/><path d="M4 14h6v6"/>',
    Expand:         '<path d="M15 3h6v6"/><path d="m21 3-7 7"/><path d="m3 21 7-7"/><path d="M9 21H3v-6"/>',
    Observe:        '<path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"/><circle cx="12" cy="12" r="3"/>',
    Tack:           '<path d="M12 17v5"/><path d="M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-.76a2 2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 10.76V7a1 1 0 0 1 1-1 2 2 0 0 0 0-4H8a2 2 0 0 0 0 4 1 1 0 0 1 1 1z"/>',
    Fastened:       '<rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>',
    Unfastened:     '<rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 9.9-1"/>',
    Annotation:     '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>',
    Notice:         '<circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/>',

    Chevron:        '<path d="m9 18 6-6-6-6"/>',
    ChevronDown:    '<path d="m6 9 6 6 6-6"/>',
    Magnifier:      '<path d="m21 21-4.34-4.34"/><circle cx="11" cy="11" r="8"/>',
    Lattice:        '<rect width="7" height="7" x="3" y="3" rx="1"/><rect width="7" height="7" x="14" y="3" rx="1"/><rect width="7" height="7" x="14" y="14" rx="1"/><rect width="7" height="7" x="3" y="14" rx="1"/>',
    Braces:         '<path d="M8 3H7a2 2 0 0 0-2 2v5a2 2 0 0 1-2 2 2 2 0 0 1 2 2v5c0 1.1.9 2 2 2h1"/><path d="M16 21h1a2 2 0 0 0 2-2v-5c0-1.1.9-2 2-2a2 2 0 0 1-2-2V5a2 2 0 0 0-2-2h-1"/>',
    Advance:        '<polygon points="6 3 20 12 6 21 6 3"/>',
    Suspend:        '<rect x="14" y="4" width="4" height="16" rx="1"/><rect x="6" y="4" width="4" height="16" rx="1"/>',
    Encompass:      '<path d="M8 3H5a2 2 0 0 0-2 2v3"/><path d="M21 8V5a2 2 0 0 0-2-2h-3"/><path d="M3 16v3a2 2 0 0 0 2 2h3"/><path d="M16 21h3a2 2 0 0 0 2-2v-3"/>',
    Raster:         '<rect width="18" height="18" x="3" y="3" rx="2" ry="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"/>',
    Circle:         '<circle cx="12" cy="12" r="10"/>',
    Square:         '<rect width="18" height="18" x="3" y="3" rx="2"/>',
    Sun:            '<circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="m4.93 4.93 1.41 1.41"/><path d="m17.66 17.66 1.41 1.41"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="m6.34 17.66-1.41 1.41"/><path d="m19.07 4.93-1.41 1.41"/>',

    // 📝 The liquid categories, drawn to the same 24x24 stroked convention as the set above so they sit
    //    beside the inherited glyphs without reading as a second icon family.
    Droplet:        '<path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/>',
    Spout:          '<path d="M3 6h7a4 4 0 0 1 4 4v2"/><path d="M18 4v4"/><path d="M21 6h-6"/><path d="M14 16a4 4 0 0 1-8 0c0-2 4-6 4-6s4 4 4 6"/>',
    Obstacle:       '<rect width="8" height="8" x="8" y="8" rx="1"/><path d="M3 3v4"/><path d="M3 3h4"/><path d="M21 21v-4"/><path d="M21 21h-4"/><path d="M3 21v-4"/><path d="M3 21h4"/><path d="M21 3v4"/><path d="M21 3h-4"/>',
    Current:        '<path d="M2 7c2.5-2 5-2 7.5 0S17 9 19.5 7"/><path d="M2 13c2.5-2 5-2 7.5 0s5 2 7.5 0"/><path d="M2 19c2.5-2 5-2 7.5 0s5 2 7.5 0"/><path d="M19 13h3"/><path d="M19 19h3"/>',
    Ripple:         '<path d="M2 12c2-2.5 4-2.5 6 0s4 2.5 6 0 4-2.5 6 0"/><path d="M2 17c2-2.5 4-2.5 6 0s4 2.5 6 0 4-2.5 6 0"/><circle cx="12" cy="5" r="2"/>',
    Vessel:         '<path d="M5 4h14l-1.5 15a2 2 0 0 1-2 1.8H8.5a2 2 0 0 1-2-1.8z"/><path d="M6.2 11c2-1.6 3.9-1.6 5.8 0s3.8 1.6 5.7 0"/>'
};

// Compose one stroked SVG at the requested pixel size.
export function ComposeGlyph(OutlineNaming, PixelSize = 16, StrokeWeight = 2)
{
    const Outline = OutlinePaths[OutlineNaming];
    if (!Outline) return '';

    return `<svg xmlns="http://www.w3.org/2000/svg" width="${PixelSize}" height="${PixelSize}" viewBox="0 0 24 24" `
         + `fill="none" stroke="currentColor" stroke-width="${StrokeWeight}" stroke-linecap="round" `
         + `stroke-linejoin="round" style="display:block">${Outline}</svg>`;
}

// The two hand-drawn trigonometric curves shown inside circular math entries.
export const TrigonometricCurves =
{
    Sine:   '<svg xmlns="http://www.w3.org/2000/svg" width="32" height="20" viewBox="0 0 70 40" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><path d="M5 20 C 15 0, 25 40, 35 20 C 45 0, 55 40, 65 20"/></svg>',
    Cosine: '<svg xmlns="http://www.w3.org/2000/svg" width="32" height="20" viewBox="0 0 70 40" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><path d="M5 5 C 15 5, 20 35, 30 35 C 40 35, 45 5, 55 5 C 60 5, 66 20, 66 20"/></svg>'
};
