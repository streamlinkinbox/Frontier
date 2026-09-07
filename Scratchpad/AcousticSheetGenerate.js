'use strict';
// ============================================================================================================================================
//                                                   ACOUSTICSHEETGENERATE.JS
// ============================================================================================================================================
// 🧪 Emits the C++ field sheet (AcousticStructure::Fields in Engine/PlatformInterchange/AcousticStructure.cpp) from ACOUSTIC_SCHEMA
//    in Tools/AudioEditor/index.html, row for row in the format the file carries, so the two can be diffed instead of retyped
//    whenever the schema grows. Also prints the struct member lines for a section (--members <section>) in the header's style.
//
//    node Scratchpad/AcousticSheetGenerate.js                 → the sheet rows on stdout (paste between the braces of Fields[])
//    node Scratchpad/AcousticSheetGenerate.js --check         → diffs the rows against the ones in AcousticStructure.cpp; exit 1 on drift
//    node Scratchpad/AcousticSheetGenerate.js --members turbo → member declarations for the [turbo] section
//    node Scratchpad/AcousticSheetGenerate.js --count         → the row count (what AcousticFieldCount must be)
const fs = require('fs'), path = require('path');
const repo = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(repo, 'Tools', 'AudioEditor', 'index.html'), 'utf8');
const dspText = html.slice(html.indexOf('<script id="dsp" type="text/plain">') + '<script id="dsp" type="text/plain">'.length, html.indexOf('</script>', html.indexOf('<script id="dsp"')));
const D = (() => { const module = { exports: {} }; new Function('module', 'exports', dspText)(module, module.exports); return module.exports; })();
const SCHEMA = D.ACOUSTIC_SCHEMA;

const pascal = (key) => key.split('_').map(p => p.charAt(0).toUpperCase() + p.slice(1)).join('');
const sectionMember = (section) => pascal(section);
// C++ literal for a double: always carries a decimal point or exponent (130.0, 0.05, 2.667, 24000.0)
function real(v)
{
    if (Number.isInteger(v)) return v.toFixed(1);
    let text = String(v);
    if (text.indexOf('.') < 0 && text.indexOf('e') < 0) text += '.0';
    return text;
}
const quote = (s) => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';

function rows()
{
    const out = []; let current = null;
    for (const row of SCHEMA)
    {
        const [section, key, def, unit, note, range] = row;
        if (section !== current) { current = section; out.push('    // [' + section + ']'); }
        let category, defReal = '0.0', defText = '""';
        if (Array.isArray(def)) category = 'RealList';
        else if (typeof def === 'boolean') { category = 'Toggle'; defReal = def ? '1.0' : '0.0'; }
        else if (typeof def === 'number') { category = key === 'cylinder_count' ? 'Whole' : 'Real'; defReal = real(def); }
        else { category = 'Text'; defText = quote(def); }
        const ranged = !!(range && typeof def === 'number');
        const r = ranged ? range.map(real) : ['0.0', '0.0', '0.0'];
        out.push('    { ' + [quote(section), quote(key), 'AcousticFieldCategory::' + category, 'offsetof(AcousticStructure, ' + sectionMember(section) + '.' + pascal(key) + ')',
                              defReal, defText, quote(unit), quote(note), r[0], r[1], r[2], ranged ? 'true' : 'false'].join(', ') + ' },');
    }
    return out;
}

const mode = process.argv[2] || '';
if (mode === '--count') { console.log(SCHEMA.length); process.exit(0); }
if (mode === '--members')
{
    const section = process.argv[3];
    for (const row of SCHEMA)
    {
        if (row[0] !== section) continue;
        const [, key, def, unit, note] = row;
        let type, init;
        if (Array.isArray(def)) { type = 'AcousticRealList'; init = '{ ' + def.length + 'u, { ' + def.map(real).join(', ') + ' } }'; }
        else if (typeof def === 'boolean') { type = 'bool'; init = def ? 'true' : 'false'; }
        else if (typeof def === 'number') { type = key === 'cylinder_count' ? 'int32_t' : 'double'; init = key === 'cylinder_count' ? String(def) : real(def); }
        else { type = 'char'; init = quote(def); }
        const decl = type === 'char' ? 'char              ' + pascal(key) + '[AcousticTextCapacity] = ' + init + ';' : type.padEnd(18) + pascal(key) + ' = ' + init + ';';
        const u = '[' + (unit && unit !== '' ? unit : '-') + ']';
        let brief = String(note || '').split(';')[0].split(' — ')[0].split(' (')[0];
        if (brief.length > 96) brief = brief.slice(0, 95) + '…';
        console.log('        ' + decl.padEnd(86) + '// ' + u.padEnd(8) + ' ' + brief);
    }
    process.exit(0);
}
const lines = rows();
if (mode === '--check')
{
    const cpp = fs.readFileSync(path.join(repo, 'Engine', 'PlatformInterchange', 'AcousticStructure.cpp'), 'utf8');
    const start = cpp.indexOf('const AcousticFieldNote AcousticStructure::Fields[AcousticFieldCount] =');
    const open = cpp.indexOf('{', start), close = cpp.indexOf('\n};', open);
    const have = cpp.slice(open + 1, close).split('\n').filter(l => l.trim().length);
    let drift = 0;
    for (let i = 0; i < Math.max(have.length, lines.length); ++i)
        if (have[i] !== lines[i]) { ++drift; if (drift <= 5) console.log('  drift at row ' + i + '\n    file: ' + have[i] + '\n    want: ' + lines[i]); }
    const count = (cpp.match(/AcousticFieldCount\s*=\s*(\d+)u/) || [0, '?'])[1];
    const header = fs.readFileSync(path.join(repo, 'Engine', 'PlatformInterchange', 'AcousticStructure.h'), 'utf8');
    const declared = (header.match(/AcousticFieldCount\s*=\s*(\d+)u/) || [0, '?'])[1];
    console.log('  schema rows ' + SCHEMA.length + ' · sheet rows in the .cpp ' + have.filter(l => l.trim().startsWith('{')).length + ' · AcousticFieldCount ' + declared + ' · drifting lines ' + drift);
    process.exit(drift === 0 && Number(declared) === SCHEMA.length ? 0 : 1);
}
console.log(lines.join('\n'));
