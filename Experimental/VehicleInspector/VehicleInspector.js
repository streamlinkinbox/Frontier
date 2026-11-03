/*==========================================================================================================================================
                                                       FRONTIER · VEHICLE INSPECTOR
===========================================================================================================================================*/
/* A data-only Vehicle record in the same Outliner/Inspector language as Render settings, Sun and Atmosphere.
   Tyres are parented child sockets; pit-stop actions detach or replace them. Curves are editable point splines. */
'use strict';

(() =>
{
    const PRESET_KEY = 'frontier.vehicle-inspector.presets.v1';
    const $ = Id => document.getElementById(Id);
    const clone = Value => JSON.parse(JSON.stringify(Value));
    const esc = Value => String(Value ?? '').replace(/[&<>"']/g, Character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[Character]));
    const clamp = (Value, Minimum, Maximum) => Math.min(Maximum, Math.max(Minimum, Value));
    const round = (Value, Places = 4) => Number(Number(Value).toFixed(Places));
    const fmt = (Value, Digits = 0) => Number.isFinite(Number(Value)) ? new Intl.NumberFormat('en-US', { minimumFractionDigits: Digits, maximumFractionDigits: Digits }).format(Number(Value)) : '—';
    const time = () => new Date().toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false });

    const GlyphPaths = {
        monitor: '<rect x="2.5" y="4" width="19" height="13" rx="2.5"/><path d="M8 21h8M12 17v4"/>',
        sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M4.6 4.6l1.4 1.4M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4 6 18M18 6l1.4-1.4"/>',
        atmosphere: '<circle cx="12" cy="12" r="9"/><path d="M3.5 9.5c3 1.4 6.2 2 8.5 2s5.5-.6 8.5-2M3.6 14.8c2.9 1.2 5.8 1.8 8.4 1.8s5.5-.6 8.4-1.8"/>',
        car: '<path d="M3.4 13.5 5.6 8.3A3 3 0 0 1 8.4 6.3h7.2a3 3 0 0 1 2.8 2l2.2 5.2"/><path d="M3.4 13.5h17.2v4.1h-3.1v-1.4H6.5v1.4H3.4z"/><circle cx="7" cy="14.6" r="1.5"/><circle cx="17" cy="14.6" r="1.5"/><path d="M7.5 9.5h9"/>',
        tyre: '<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="4"/><path d="M12 3.5v4M12 16.5v4M3.5 12h4M16.5 12h4M6 6l2.6 2.6M15.4 15.4 18 18M18 6l-2.6 2.6M8.6 15.4 6 18"/>',
        chevron: '<path d="m6 9 6 6 6-6"/>',
        check: '<path d="m5 12.5 4.2 4.2L19 7"/>',
        eyeoff: '<path d="m3 3 18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.4 0 10 7 10 7a17.6 17.6 0 0 1-2.6 3.5M6.2 6.3C3.6 8.1 2 12 2 12s3.6 7 10 7c1.7 0 3.2-.5 4.5-1.2"/>',
        search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.8-3.8"/>',
        sliders: '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
        plus: '<path d="M12 5v14M5 12h14"/>',
        zap: '<path d="M13 2.5 4.5 14h6.5l-1 7.5L18.5 10H12z"/>',
        gauge: '<path d="M3.6 17.5a9 9 0 1 1 16.8 0"/><path d="m12 13.5 4.2-4.2"/><circle cx="12" cy="13.5" r="1.2"/>',
        ruler: '<path d="m3 15 12-12 6 6L9 21z"/><path d="m7 11 2 2M10 8l2 2M13 5l2 2"/>',
        wind: '<path d="M3 8.5h11a3 3 0 1 0-3-3M3 12.5h15.5a3 3 0 1 1-3 3M3 16.5h7"/>',
        weight: '<path d="M6 8h12l2 12H4z"/><circle cx="12" cy="7" r="3"/><path d="M9.5 7h5"/>',
        thermometer: '<path d="M14 14.8V5a2 2 0 1 0-4 0v9.8a4 4 0 1 0 4 0z"/><path d="M12 9v7"/>',
        droplet: '<path d="M12 2.5s6 6.4 6 10.5a6 6 0 0 1-12 0c0-4.1 6-10.5 6-10.5z"/>',
        layers: '<path d="m12 3 9 5-9 5-9-5z"/><path d="m3 12.5 9 5 9-5M3 17l9 5 9-5"/>',
        curve: '<path d="M3 19V5M3 19h18"/><path d="M4 16c4 0 4-9 8-9s4 6 8 6"/>',
        copy: '<rect x="9" y="9" width="11.5" height="11.5" rx="2.5"/><path d="M5.5 15H5a1.5 1.5 0 0 1-1.5-1.5V5A1.5 1.5 0 0 1 5 3.5h8.5A1.5 1.5 0 0 1 15 5v.5"/>',
        download: '<path d="M12 3v12m0 0 4.5-4.5M12 15l-4.5-4.5M4 19h16"/>',
        save: '<path d="M4 4h13l3 3v13H4z"/><path d="M8 4v6h8V4M8 20v-6h8v6"/>',
        reset: '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
        trash: '<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v5M14 11v5"/>',
        fit: '<path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5"/><circle cx="12" cy="12" r="3"/>',
        wrench: '<path d="M14.5 6.5a4 4 0 0 0-5 5L3 18l3 3 6.5-6.5a4 4 0 0 0 5-5l-3 3-3-3z"/>',
        clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7.5V12l3 2"/>',
        folder: '<path d="M3 7.5A2.5 2.5 0 0 1 5.5 5h3.6l2 2.2h7.4A2.5 2.5 0 0 1 21 9.7v7.8a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z"/>',
        activity: '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
        target: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r=".6"/>',
        box: '<path d="m12 2.8 8 4.4v9.6l-8 4.4-8-4.4V7.2z"/><path d="m12 12 8-4.6M12 12v9.2M12 12 4 7.4"/>',
        sparkles: '<path d="m11 3.5 1.7 4.6 4.6 1.7-4.6 1.7L11 16.1l-1.7-4.6-4.6-1.7 4.6-1.7z"/><path d="M18.5 14.5v5M16 17h5"/>',
        fuel: '<path d="M4 21V5a2 2 0 0 1 2-2h6a2 2 0 0 1 2 2v16M3 21h12M6 8h6"/><path d="M14 8h3l2 3v7a2 2 0 0 1-4 0v-3h-1"/>',
        drive: '<rect x="3" y="7" width="18" height="10" rx="3"/><circle cx="7" cy="12" r="1.5"/><path d="M11 9h6M11 15h6"/>',
        snowflake: '<path d="M12 2v20M4 6l16 12M20 6 4 18M9 4l3 3 3-3M9 20l3-3 3 3"/>',
        alert: '<path d="M12 3.5 2.5 20h19z"/><path d="M12 10v4.5M12 17.4v.2"/>',
        dot: '<circle cx="12" cy="12" r="3"/>'
    };

    function icon(Name)
    {
        return `<svg class="glyph" viewBox="0 0 24 24" aria-hidden="true">${GlyphPaths[Name] || GlyphPaths.dot}</svg>`;
    }

    const Compounds = {
        Soft:   { tone: 'red',    pressure: 168, temp: 94,  grip: 1.18 },
        Medium: { tone: 'amber',  pressure: 172, temp: 88,  grip: 1.08 },
        Hard:   { tone: 'white',  pressure: 176, temp: 82,  grip: 0.98 },
        Wet:    { tone: 'cyan',   pressure: 158, temp: 62,  grip: 0.86 },
        Slick:  { tone: 'green',  pressure: 164, temp: 102, grip: 1.26 }
    };
    const CompoundNames = Object.keys(Compounds);
    const compound = Name => Compounds[Name] || Compounds.Soft;
    const compoundTone = Name => compound(Name).tone;

    const SceneRows = [
        {
            id: 'render', group: 'Rendering', name: 'Render settings', glyph: 'monitor', tone: 'blue',
            meta: 'RTX · 900×662 · 4.3 ms',
            hero: { label: 'GPU time per refresh', value: 4.3, digits: 1, unit: 'ms', side: '228', sideUnit: 'fps', low: '1 % low 159 fps' },
            stats: [
                { label: 'Render', text: '900×662', unit: 'px', tone: 'blue', glyph: 'monitor', trend: 'Viewport target' },
                { label: 'Trace', text: '450×331', unit: 'px', tone: 'violet', glyph: 'target', trend: '½ resolution' },
                { label: 'Bounces', text: '3 · 2', unit: 'paths', tone: 'amber', glyph: 'layers', trend: 'GI / reflection' }
            ]
        },
        {
            id: 'sun', group: 'Environment', name: 'Sun', glyph: 'sun', tone: 'amber',
            meta: '38° · 98 klx',
            hero: { label: 'Direct illuminance', value: 98, digits: 0, unit: 'klx', side: '38', sideUnit: '°', low: 'Solar elevation' },
            stats: [
                { label: 'Elevation', text: '38', unit: '°', tone: 'amber', glyph: 'sun', trend: 'Stratified disc' },
                { label: 'Colour', text: '5 800', unit: 'K', tone: 'white', glyph: 'droplet', trend: 'Blackbody estimate' },
                { label: 'Shadow', text: '4', unit: 'cascades', tone: 'blue', glyph: 'layers', trend: 'Clipmap depth' }
            ]
        },
        {
            id: 'atmosphere', group: 'Environment', name: 'Atmosphere', glyph: 'atmosphere', tone: 'cyan',
            meta: 'Mie 2.3 · ozone 1.0',
            hero: { label: 'Mie scattering', value: 2.3, digits: 1, unit: '×', side: '1.0', sideUnit: 'ozone', low: 'Earth preset' },
            stats: [
                { label: 'Mie', text: '2.3', unit: '×', tone: 'cyan', glyph: 'atmosphere', trend: 'Aerosols' },
                { label: 'Ozone', text: '1.0', unit: '×', tone: 'violet', glyph: 'droplet', trend: 'Absorption' },
                { label: 'Scale height', text: '8.0', unit: 'km', tone: 'green', glyph: 'ruler', trend: 'Density profile' }
            ]
        }
    ];

    function defaultCurves()
    {
        const Curves = {
            torque: {
                key: 'torque', label: 'Engine torque', short: 'Torque', tone: 'amber', custom: false, smooth: true,
                xLabel: 'Engine speed', xUnit: 'rpm', yLabel: 'Crank torque', yUnit: 'N·m',
                xMin: 1000, xMax: 8000, yMin: 0, yMax: 700, xDigits: 0, yDigits: 0, stepX: 100, stepY: 5,
                points: [[1000, 320], [2000, 475], [3500, 610], [5000, 590], [6500, 480], [8000, 355]]
            },
            thermal: {
                key: 'thermal', label: 'Thermal derate', short: 'Thermal', tone: 'red', custom: false, smooth: true,
                xLabel: 'Coolant temperature', xUnit: '°C', yLabel: 'Power retention', yUnit: '%',
                xMin: 40, xMax: 130, yMin: 0, yMax: 110, xDigits: 0, yDigits: 0, stepX: 1, stepY: 1,
                points: [[40, 100], [75, 100], [95, 96], [110, 84], [130, 58]]
            },
            boost: {
                key: 'boost', label: 'Boost pressure', short: 'Boost', tone: 'cyan', custom: false, smooth: true,
                xLabel: 'Engine speed', xUnit: 'rpm', yLabel: 'Manifold pressure', yUnit: 'bar',
                xMin: 1000, xMax: 8000, yMin: 0, yMax: 2.2, xDigits: 0, yDigits: 2, stepX: 100, stepY: 0.05,
                points: [[1000, 0.45], [2000, 1.25], [3200, 1.80], [5200, 1.75], [6700, 1.35], [8000, 0.90]]
            },
            grip: {
                key: 'grip', label: 'Tyre grip window', short: 'Grip', tone: 'green', custom: false, smooth: true,
                xLabel: 'Core temperature', xUnit: '°C', yLabel: 'Available grip', yUnit: 'µ',
                xMin: 20, xMax: 140, yMin: 0, yMax: 1.4, xDigits: 0, yDigits: 2, stepX: 1, stepY: 0.01,
                points: [[20, 0.66], [55, 1.02], [90, 1.18], [112, 1.10], [140, 0.68]]
            }
        };
        Object.values(Curves).forEach(Curve => { Curve.defaultPoints = clone(Curve.points); });
        return Curves;
    }

    function makeTyre(VehicleId, Code, Name, Compound, Pressure, Temperature, Wear, Grip, Laps)
    {
        return {
            id: `${VehicleId}:${Code.toLowerCase()}`, code: Code, name: Name, mounted: true, compound: Compound,
            pressureKpa: Pressure, coreTempC: Temperature, wearPct: Wear, gripMu: Grip, stintLaps: Laps
        };
    }

    function createVehicle(Ordinal)
    {
        const Id = `v${Ordinal}`;
        const Serial = String(Ordinal).padStart(2, '0');
        return {
            id: Id, ordinal: Ordinal, name: `FR-V / ${Serial}`, category: 'GT3 prototype', drive: 'RWD',
            massKg: 1420, powerKw: 428, wheelbaseMm: 2720, topSpeedKph: 318, dragAreaM2: 0.62,
            downforceN: 1240, fuelKg: 92, sprintS: 3.2, maxRpm: 8000, pitCompound: 'Soft',
            curves: defaultCurves(),
            tyres: [
                makeTyre(Id, 'FL', 'Front left',  'Soft',   168, 94,  8, 1.18, 12),
                makeTyre(Id, 'FR', 'Front right', 'Soft',   168, 96,  9, 1.17, 12),
                makeTyre(Id, 'RL', 'Rear left',   'Medium', 172, 88, 13, 1.08, 18),
                makeTyre(Id, 'RR', 'Rear right',  'Medium', 172, 90, 14, 1.07, 18)
            ]
        };
    }

    function loadPresets()
    {
        try { return JSON.parse(localStorage.getItem(PRESET_KEY) || '{}') || {}; }
        catch { return {}; }
    }

    const state = {
        vehicles: [createVehicle(1)],
        vehicleSerial: 1,
        selected: 'v1',
        lastVehicleId: 'v1',
        expanded: new Set(['v1']),
        folded: new Set(),
        showEmptySockets: true,
        search: '',
        curveKey: 'torque',
        tab: 'overview',
        revision: 0,
        presets: loadPresets(),
        logs: [
            { time: time(), tag: 'SCENE',   tone: 'blue',  message: 'Default.levelspace · Vehicle record added' },
            { time: time(), tag: 'VEHICLE', tone: 'green', message: '4 tyre sockets parented to FR-V / 01' },
            { time: time(), tag: 'NOTE',    tone: 'amber', message: 'No geometry attached — inspector-only step' }
        ]
    };

    let toastTimer = 0;

    function toast(Text)
    {
        const Element = $('Toast');
        Element.textContent = Text;
        Element.classList.add('shown');
        clearTimeout(toastTimer);
        toastTimer = setTimeout(() => Element.classList.remove('shown'), 1800);
    }

    function addLog(Tag, Message, Tone = 'blue')
    {
        state.logs.push({ time: time(), tag: Tag, tone: Tone, message: Message });
        if (state.logs.length > 40) state.logs.shift();
        renderLog();
    }

    function savePresets()
    {
        try { localStorage.setItem(PRESET_KEY, JSON.stringify(state.presets)); }
        catch { toast('Preset storage is unavailable in this browser'); }
    }

    function commit(Tag, Message, Tone = 'blue', ToastText = '')
    {
        state.revision += 1;
        addLog(Tag, Message, Tone);
        renderAll();
        if (ToastText) toast(ToastText);
    }

    function softCommit(Tag, Message, Tone = 'blue')
    {
        state.revision += 1;
        addLog(Tag, Message, Tone);
        renderOutliner();
        renderStatus();
        refreshDerivedLabels(activeVehicle());
    }

    function resolveSelection()
    {
        const Row = SceneRows.find(Entry => Entry.id === state.selected);
        if (Row) return { kind: 'scene', row: Row };
        for (const Vehicle of state.vehicles)
        {
            if (Vehicle.id === state.selected) return { kind: 'vehicle', vehicle: Vehicle };
            const Tyre = Vehicle.tyres.find(Entry => Entry.id === state.selected);
            if (Tyre) return { kind: 'tyre', vehicle: Vehicle, tyre: Tyre };
        }
        return { kind: 'vehicle', vehicle: state.vehicles[0] };
    }

    function activeVehicle()
    {
        return state.vehicles.find(Vehicle => Vehicle.id === state.lastVehicleId) || state.vehicles[0];
    }

    function findTyreOwner(Id)
    {
        for (const Vehicle of state.vehicles)
        {
            const Tyre = Vehicle.tyres.find(Entry => Entry.id === Id);
            if (Tyre) return { vehicle: Vehicle, tyre: Tyre };
        }
        return null;
    }

    function selectionLabel()
    {
        const Selection = resolveSelection();
        if (Selection.kind === 'scene') return Selection.row.name;
        if (Selection.kind === 'tyre') return `${Selection.tyre.code} · ${Selection.tyre.name}`;
        return Selection.vehicle.name;
    }

    function activeCurve(Vehicle)
    {
        const Keys = Object.keys(Vehicle.curves);
        if (!Vehicle.curves[state.curveKey]) state.curveKey = Keys[0] || 'torque';
        return Vehicle.curves[state.curveKey];
    }

    function peakTorque(Vehicle)
    {
        const Curve = Vehicle.curves.torque;
        return Curve ? Math.max(...Curve.points.map(Point => Point[1])) : 0;
    }

    function powerWeight(Vehicle)
    {
        return Vehicle.massKg > 0 ? Vehicle.powerKw / (Vehicle.massKg / 1000) : 0;
    }

    function mountedCount(Vehicle)
    {
        return Vehicle.tyres.filter(Tyre => Tyre.mounted).length;
    }

    function metricHtml(Value, Digits = 0, Unit = '', Derived = '')
    {
        const Text = fmt(Value, Digits);
        const Point = Text.indexOf('.');
        const Whole = Point < 0 ? Text : Text.slice(0, Point);
        const Fraction = Point < 0 ? '' : Text.slice(Point);
        return `${Derived ? `<span data-derived="${esc(Derived)}">${esc(Whole)}</span>` : esc(Whole)}${Fraction ? `<span class="fraction">${esc(Fraction)}</span>` : ''}${Unit ? `<span class="unit">${esc(Unit)}</span>` : ''}`;
    }

    function statCard(Options)
    {
        const ValueHtml = Options.text !== undefined
            ? `${esc(Options.text)}${Options.unit ? `<span class="unit">${esc(Options.unit)}</span>` : ''}`
            : metricHtml(Options.value, Options.digits ?? 0, Options.unit || '', Options.derived || '');
        return `<article class="stat-card" data-tone="${esc(Options.tone || 'blue')}">
            <div class="stat-icon">${icon(Options.glyph || 'dot')}</div>
            <div class="stat-value">${ValueHtml}</div>
            <div class="stat-label">${esc(Options.label)}</div>
            ${Options.trend ? `<div class="stat-trend">${esc(Options.trend)}</div>` : ''}
        </article>`;
    }

    function heroCard(Options)
    {
        return `<section class="hero-card" data-tone="${esc(Options.tone || 'blue')}">
            <div class="hero-top"><div class="budget-chip"><span class="mark">${icon(Options.glyph || 'check')}</span>${esc(Options.chip)}</div><span class="estimate-tag">${esc(Options.tag)}</span></div>
            <div class="hero-title">${esc(Options.title)}</div>
            <div class="hero-metrics"><div class="big-metric">${metricHtml(Options.value, Options.digits ?? 0, Options.unit || '')}</div>
                <div class="hero-side"><div class="rate">${Options.sideHtml}<small>${esc(Options.sideUnit || '')}</small></div><div class="low">${esc(Options.low || '')}</div></div>
            </div>
            <div class="hero-note">${Options.noteHtml}</div>
        </section>`;
    }

    function sectionCard(Id, Tone, Glyph, Title, Status, Body)
    {
        const Folded = state.folded.has(Id) ? ' folded' : '';
        return `<section class="section-card${Folded}" id="${esc(Id)}" data-tone="${esc(Tone)}">
            <div class="section-head" data-action="fold" data-target="${esc(Id)}">
                <span class="head-chip">${icon(Glyph)}</span><span class="head-title">${esc(Title)}</span><span class="spacer"></span>
                <span class="head-status">${esc(Status)}</span><span class="chevron">${icon('chevron')}</span>
            </div>
            <div class="section-fold"><div class="section-inner">${Body}</div></div>
        </section>`;
    }

    function pageTabs(Tabs, Active)
    {
        return `<nav class="page-tabs" aria-label="Inspector sections">${Tabs.map(Tab => `<button type="button" class="page-tab${Tab.key === Active ? ' active' : ''}" data-action="jump" data-target="${esc(Tab.target)}" data-tab="${esc(Tab.key)}">${esc(Tab.label)}</button>`).join('')}</nav>`;
    }

    function fieldHtml(Options)
    {
        const Kind = Options.kind || 'number';
        const Value = Kind === 'string' ? esc(Options.value) : esc(Options.value);
        const Attributes = Kind === 'number' ? `min="${esc(Options.min ?? 0)}" step="${esc(Options.step ?? 1)}" inputmode="decimal"` : `maxlength="40"`;
        return `<label class="field" data-tone="${esc(Options.tone || 'blue')}">
            <span class="field-head">${icon(Options.glyph || 'dot')}<span>${esc(Options.label)}</span></span>
            <span class="field-input"><input type="${Kind === 'string' ? 'text' : 'number'}" value="${Value}" ${Attributes}
                data-bind="${esc(Options.bind)}" data-scope="${esc(Options.scope || 'vehicle')}" data-kind="${Kind}" data-unit="${esc(Options.unit || '')}" aria-label="${esc(Options.label)}">
                ${Options.unit ? `<span class="field-unit">${esc(Options.unit)}</span>` : ''}
            </span>
        </label>`;
    }

    function rangeRow(Options)
    {
        return `<div class="range-row" data-tone="${esc(Options.tone || 'blue')}">
            <div class="range-head"><span>${esc(Options.label)}</span><span class="spacer"></span><output>${fmt(Options.value, Options.digits ?? 0)} ${esc(Options.unit)}</output></div>
            <input type="range" min="${esc(Options.min)}" max="${esc(Options.max)}" step="${esc(Options.step)}" value="${esc(Options.value)}"
                data-bind="${esc(Options.bind)}" data-scope="${esc(Options.scope || 'tyre')}" data-kind="number" data-unit="${esc(Options.unit)}" data-digits="${esc(Options.digits ?? 0)}"
                aria-label="${esc(Options.label)}">
        </div>`;
    }

    function compoundButtons(Selected, Action, Extra = '')
    {
        return `<div class="compound-row">${CompoundNames.map(Name => {
            const Tone = compoundTone(Name);
            return `<button type="button" class="compound-button${Name === Selected ? ' active' : ''}" data-tone="${Tone}" data-action="${Action}" data-value="${esc(Name)}" ${Extra}>${esc(Name)}</button>`;
        }).join('')}</div>`;
    }

    function statusCell(Label, Value)
    {
        return `<div class="status-cell"><span>${esc(Label)}</span><b>${esc(Value)}</b></div>`;
    }

    function treeRow(Options)
    {
        const Tone = Options.tone || 'blue';
        const Nested = Options.nested ? ' nested' : '';
        const Empty = Options.empty ? ' is-empty' : '';
        const Selected = Options.selected ? ' selected' : '';
        return `<div class="tree-row${Nested}${Empty}${Selected}" data-tone="${Tone}" data-select="${esc(Options.id)}" role="button" tabindex="0">
            <span class="row-marker"></span>
            ${Options.toggle ? `<button type="button" class="tree-toggle${Options.expanded ? '' : ' collapsed'}" data-action="toggle-expand" data-id="${esc(Options.id)}" aria-label="${Options.expanded ? 'Collapse' : 'Expand'}">${icon('chevron')}</button>` : ''}
            <span class="row-glyph">${icon(Options.glyph)}</span>
            <span class="row-text"><span class="row-name">${esc(Options.name)}${Options.tag ? `<span class="mini-tag">${esc(Options.tag)}</span>` : ''}</span><span class="row-meta">${esc(Options.meta)}</span></span>
            ${Options.fitButton ? `<button type="button" class="fit-mini" data-action="fit-tyre" data-id="${esc(Options.id)}">Fit</button>` : ''}
            <span class="row-state ${Options.enabled === false ? 'off' : 'on'}">${icon(Options.enabled === false ? 'eyeoff' : 'check')}</span>
        </div>`;
    }

    function renderOutliner()
    {
        const Query = state.search.trim().toLowerCase();
        const TotalTyres = state.vehicles.reduce((Sum, Vehicle) => Sum + Vehicle.tyres.length, 0);
        const Total = SceneRows.length + state.vehicles.length + TotalTyres;
        const Mounted = state.vehicles.reduce((Sum, Vehicle) => Sum + mountedCount(Vehicle), 0);
        const Empty = TotalTyres - Mounted;
        let Html = `<div class="outliner">
            <div class="outliner-heading">
                <div><h1>Outliner</h1><div class="muted">Default.levelspace · ${Total} records</div></div><div class="spacer"></div>
                <button type="button" class="round-button" data-action="add-vehicle" title="Add vehicle from current spec" aria-label="Add vehicle from current spec">${icon('plus')}</button>
            </div>
            <div class="count-cards">
                <div class="count-card" data-tone="green"><div class="chip-glyph live">${icon('check')}</div><div class="label">Enabled</div><div class="number">${SceneRows.length + state.vehicles.length + Mounted}</div></div>
                <div class="count-card" data-tone="amber"><div class="chip-glyph ${Empty ? 'warm' : 'dim'}">${icon(Empty ? 'alert' : 'eyeoff')}</div><div class="label">Empty sockets</div><div class="number">${Empty}</div></div>
            </div>
            <div class="search-row">
                <label class="search-pill">${icon('search')}<input id="OutlinerSearch" type="search" value="${esc(state.search)}" placeholder="Search records" autocomplete="off"><kbd>Ctrl+F</kbd></label>
                <button type="button" class="filter-pill${state.showEmptySockets ? '' : ' on'}" data-action="toggle-empty">${icon('sliders')}<span>${state.showEmptySockets ? 'Hide empty' : 'Show empty'}</span></button>
            </div>`;

        const StaticGroups = [
            { title: 'Rendering', rows: SceneRows.filter(Row => Row.group === 'Rendering') },
            { title: 'Environment', rows: SceneRows.filter(Row => Row.group === 'Environment') }
        ];
        for (const Group of StaticGroups)
        {
            const Rows = Group.rows.filter(Row => !Query || `${Row.name} ${Row.meta}`.toLowerCase().includes(Query));
            if (!Rows.length) continue;
            Html += `<div class="tree-group"><div class="tree-group-title">${icon('chevron')}<span>${Group.title}</span><span class="count">${Rows.length}</span></div>`;
            for (const Row of Rows)
            {
                Html += treeRow({ id: Row.id, name: Row.name, glyph: Row.glyph, tone: Row.tone, meta: Row.meta, selected: state.selected === Row.id, enabled: true });
            }
            Html += '</div>';
        }

        let VehicleRows = 0;
        let VehicleHtml = '';
        for (const Vehicle of state.vehicles)
        {
            const VehicleMatch = !Query || `${Vehicle.name} ${Vehicle.category} vehicle`.toLowerCase().includes(Query);
            const Expanded = state.expanded.has(Vehicle.id) || !!Query;
            const VisibleTyres = Vehicle.tyres.filter(Tyre =>
            {
                if (!Expanded) return false;
                if (!Tyre.mounted && !state.showEmptySockets) return false;
                if (!Query) return true;
                return `${Tyre.code} ${Tyre.name} ${Tyre.compound}`.toLowerCase().includes(Query);
            });
            if (Query && !VehicleMatch && !VisibleTyres.length) continue;
            VehicleRows += 1;
            VehicleHtml += treeRow({
                id: Vehicle.id, name: Vehicle.name, glyph: 'car', tone: 'blue', tag: 'Parent',
                meta: `${fmt(Vehicle.powerKw, 0)} kW · ${fmt(Vehicle.massKg, 0)} kg · ${mountedCount(Vehicle)}/4 fitted`,
                selected: state.selected === Vehicle.id, enabled: true, toggle: true, expanded: Expanded
            });
            for (const Tyre of VisibleTyres)
            {
                VehicleHtml += treeRow({
                    id: Tyre.id, name: `${Tyre.code} · ${Tyre.name} tyre`, glyph: 'tyre', tone: compoundTone(Tyre.compound),
                    meta: Tyre.mounted ? `${fmt(Tyre.pressureKpa, 0)} kPa · ${fmt(Tyre.coreTempC, 0)} °C · ${fmt(Tyre.wearPct, 0)} % wear` : 'Empty socket · fit at pit stop',
                    selected: state.selected === Tyre.id, enabled: Tyre.mounted, empty: !Tyre.mounted, nested: true,
                    fitButton: !Tyre.mounted
                });
            }
        }
        if (VehicleRows)
        {
            Html += `<div class="tree-group"><div class="tree-group-title">${icon('chevron')}<span>Vehicles</span><span class="count">${VehicleRows}</span></div>${VehicleHtml}
                <div class="tree-note"><b>Data-only record.</b> Vehicle has no mesh. Four tyre sockets stay parented; detach or replace them at a pit stop.</div>
            </div>`;
        }
        Html += '</div>';
        $('OutlinerContent').innerHTML = Html;
    }

    function socketCard(Vehicle, Tyre)
    {
        const Tone = compoundTone(Tyre.compound);
        const Metrics = Tyre.mounted
            ? `<span><b>${fmt(Tyre.pressureKpa, 0)}</b> kPa</span><span><b>${fmt(Tyre.coreTempC, 0)}</b> °C</span><span><b>${fmt(Tyre.wearPct, 0)}</b> % wear</span>`
            : '<span>Empty socket</span><span>Fit at pit stop</span>';
        return `<article class="socket-card${Tyre.mounted ? '' : ' empty'}" data-tone="${Tone}" data-select="${esc(Tyre.id)}" role="button" tabindex="0">
            <div class="socket-code"><b>${esc(Tyre.code)}</b><span>${esc(Tyre.compound)}</span></div>
            <div class="socket-name">${esc(Tyre.name)}</div><div class="socket-metrics">${Metrics}</div>
        </article>`;
    }

    function renderComposition()
    {
        const Vehicle = activeVehicle();
        $('SceneGlyph').innerHTML = icon('car');
        $('SceneMeta').textContent = `${Vehicle.name} · ${Vehicle.category} · data-only`;
        $('NodeDiagram').innerHTML = `<div class="node-root" data-tone="blue">
                <div class="node-root-top">${icon('car')}<span>Vehicle record</span><span class="spacer"></span><span>${esc(Vehicle.drive)}</span></div>
                <div class="node-root-name">${esc(Vehicle.name)}</div>
                <div class="node-root-meta"><b>${fmt(Vehicle.powerKw, 0)}</b><span class="unit">kW</span><span>·</span><b>${fmt(Vehicle.massKg, 0)}</b><span>kg</span><span>·</span><b data-derived="curveCount">${Object.keys(Vehicle.curves).length}</b><span>curves</span></div>
            </div>
            <div class="branch">${Vehicle.tyres.map(Tyre => socketCard(Vehicle, Tyre)).join('')}</div>`;
        $('CompositionReadout').innerHTML = `<b>${mountedCount(Vehicle)}/4</b> mounted · <b>0</b> meshes · <b>${Object.keys(Vehicle.curves).length}</b> curves`;
    }

    function renderLog()
    {
        const Lines = state.logs.slice(-9);
        $('LogCount').textContent = `${state.logs.length} entries`;
        $('LogContent').innerHTML = Lines.map(Entry => `<div class="log-line" data-tone="${esc(Entry.tone)}"><span class="log-time">${esc(Entry.time)}</span><span class="log-tag">${esc(Entry.tag)}</span><span class="log-message">${esc(Entry.message)}</span></div>`).join('');
    }

    function renderStatus()
    {
        const TotalTyres = state.vehicles.reduce((Sum, Vehicle) => Sum + Vehicle.tyres.length, 0);
        const Total = SceneRows.length + state.vehicles.length + TotalTyres;
        $('OutlinerStatus').innerHTML = `${statusCell('Records', Total)}${statusCell('Selected', selectionLabel())}${statusCell('Vehicles', state.vehicles.length)}`;
        $('SceneStatus').innerHTML = `${statusCell('Scene', 'Default.levelspace')}${statusCell('Meshes', 0)}${statusCell('Geometry', 'None')}`;
        $('InspectorStatus').innerHTML = `${statusCell('Parameters', 'Editing')}<span class="spacer"></span>${statusCell('Revision', String(state.revision).padStart(2, '0'))}`;
    }

    function buildRecord(Vehicle)
    {
        return {
            Schema: 'Frontier.VehicleRecord/1',
            Name: Vehicle.name,
            Class: Vehicle.category,
            Parent: 'Default.levelspace',
            Geometry: 'None',
            Spec: {
                Drive: Vehicle.drive, MassKg: Vehicle.massKg, PowerKw: Vehicle.powerKw,
                PeakTorqueNm: round(peakTorque(Vehicle), 1), WheelbaseMm: Vehicle.wheelbaseMm,
                TopSpeedKph: Vehicle.topSpeedKph, DragAreaM2: Vehicle.dragAreaM2,
                DownforceN: Vehicle.downforceN, FuelKg: Vehicle.fuelKg, SprintSeconds: Vehicle.sprintS, RedlineRpm: Vehicle.maxRpm
            },
            TyreSockets: Vehicle.tyres.map(Tyre => ({
                Code: Tyre.code, Mounted: Tyre.mounted, Compound: Tyre.compound,
                PressureKpa: Tyre.pressureKpa, CoreTempC: Tyre.coreTempC, WearPct: Tyre.wearPct,
                GripMu: Tyre.gripMu, StintLaps: Tyre.stintLaps, ReplaceableAtPitStop: true
            })),
            Curves: Object.fromEntries(Object.values(Vehicle.curves).map(Curve => [Curve.key, {
                Label: Curve.label, X: { Label: Curve.xLabel, Unit: Curve.xUnit, Minimum: Curve.xMin, Maximum: Curve.xMax },
                Y: { Label: Curve.yLabel, Unit: Curve.yUnit, Minimum: Curve.yMin, Maximum: Curve.yMax },
                Smooth: Curve.smooth, Custom: Curve.custom,
                Points: Curve.points.map(Point => ({ x: round(Point[0], 3), y: round(Point[1], 3) }))
            }]))
        };
    }

    function levelspaceText(Vehicle)
    {
        return `Vehicle "${Vehicle.name}" {\n  Geometry = "None"                 # data-only record\n  Class = "${Vehicle.category}"\n  Drive = "${Vehicle.drive}"\n  MassKg = ${Vehicle.massKg}\n  PowerKw = ${Vehicle.powerKw}\n  PeakTorqueNm = ${round(peakTorque(Vehicle), 1)}   # from torque curve\n  WheelbaseMm = ${Vehicle.wheelbaseMm}\n  RedlineRpm = ${Vehicle.maxRpm}\n  TyreSockets = [ ${Vehicle.tyres.map(T => `${T.code}:${T.mounted ? T.compound : 'EMPTY'}`).join(', ')} ]\n  Curves = [ ${Object.keys(Vehicle.curves).join(', ')} ]        # editable\n  ReplaceableAtPitStop = true\n}`;
    }

    function tyreLevelspaceText(Vehicle, Tyre)
    {
        return `Tyre "${Tyre.code}" {\n  Parent = "${Vehicle.name}"\n  Mounted = ${Tyre.mounted}\n  Compound = "${Tyre.compound}"\n  PressureKpa = ${Tyre.pressureKpa}\n  CoreTempC = ${Tyre.coreTempC}\n  WearPct = ${Tyre.wearPct}\n  GripMu = ${Tyre.gripMu}\n  Geometry = "None"\n}`;
    }

    function refreshDerivedLabels(Vehicle)
    {
        const Values = {
            peakTorque: fmt(peakTorque(Vehicle), 0),
            powerWeight: fmt(powerWeight(Vehicle), 1),
            curveCount: String(Object.keys(Vehicle.curves).length),
            fitted: `${mountedCount(Vehicle)}/4`
        };
        Object.entries(Values).forEach(([Key, Value]) => document.querySelectorAll(`[data-derived="${Key}"]`).forEach(Element => { Element.textContent = Value; }));
        const Levelspace = $('VehicleLevelspace');
        if (Levelspace) Levelspace.textContent = levelspaceText(Vehicle);
        const TyreLevelspace = $('TyreLevelspace');
        if (TyreLevelspace)
        {
            const Selection = resolveSelection();
            if (Selection.kind === 'tyre') TyreLevelspace.textContent = tyreLevelspaceText(Selection.vehicle, Selection.tyre);
        }
    }

    function renderStaticInspector(Row)
    {
        return `<div class="inspector">
            <div class="breadcrumb-row"><div class="breadcrumb">Inspector / <b>${esc(Row.group)}</b></div><button type="button" class="notes-pill">${icon('plus')} Add notes</button></div>
            <div class="page-title"><h2>${esc(Row.name)}</h2><span class="live-pill"><i></i>LIVE</span></div>
            <div class="page-subtitle">The existing ${esc(Row.name.toLowerCase())} card stays in the same Outliner. Vehicle has been added beside it as a data-only record with parented tyre sockets.</div>
            ${heroCard({ tone: Row.tone, glyph: Row.glyph, chip: 'Existing scene record', tag: 'C++ INSPECTOR', title: Row.hero.label, value: Row.hero.value, digits: Row.hero.digits, unit: Row.hero.unit, sideHtml: esc(Row.hero.side), sideUnit: Row.hero.sideUnit, low: Row.hero.low, noteHtml: `<b>No change to this record.</b> The Vehicle inspector is the new HTML surface requested for this step.` })}
            <div class="stat-grid">${Row.stats.map(Stat => statCard({ ...Stat, text: Stat.text })).join('')}</div>
            <div class="empty-inspector"><h3>Vehicle is now in the Outliner</h3>Select <b>${esc(state.vehicles[0].name)}</b> or one of its four tyre sockets to inspect the new data-only hierarchy. The full tyre/vehicle editor remains the next step.</div>
            <div class="action-row" data-tone="blue"><button type="button" class="btn primary" data-action="go-vehicle">${icon('car')} Go to Vehicle</button></div>
        </div>`;
    }

    function tyreCard(Tyre)
    {
        const Tone = compoundTone(Tyre.compound);
        const Values = Tyre.mounted
            ? `<div class="tyre-values"><div class="tyre-value"><b>${fmt(Tyre.pressureKpa, 0)}<small> kPa</small></b><span>Pressure</span></div><div class="tyre-value"><b>${fmt(Tyre.coreTempC, 0)}<small> °C</small></b><span>Core</span></div><div class="tyre-value"><b>${fmt(Tyre.wearPct, 0)}<small> %</small></b><span>Wear</span></div></div><div class="wear-track"><i style="width:${clamp(Tyre.wearPct, 0, 100)}%"></i></div>`
            : `<div class="tyre-values"><div class="tyre-value"><b>—<small> kPa</small></b><span>Pressure</span></div><div class="tyre-value"><b>—<small> °C</small></b><span>Core</span></div><div class="tyre-value"><b>—<small> %</small></b><span>Wear</span></div></div><div class="wear-track"><i style="width:0%"></i></div>`;
        return `<article class="tyre-card${Tyre.mounted ? '' : ' empty'}" data-tone="${Tone}" data-select="${esc(Tyre.id)}" role="button" tabindex="0">
            <div class="tyre-top"><span class="tyre-code">${esc(Tyre.code)}</span><span class="tyre-name">${esc(Tyre.name)}</span><span class="compound-pill">${Tyre.mounted ? esc(Tyre.compound) : 'Empty'}</span></div>
            ${Values}
        </article>`;
    }

    function renderVehicleInspector(Vehicle)
    {
        const Torque = peakTorque(Vehicle);
        const Pwr = powerWeight(Vehicle);
        const Curve = activeCurve(Vehicle);
        const CurveTabs = Object.values(Vehicle.curves).map(Entry => `<button type="button" class="curve-tab${Entry.key === Curve.key ? ' active' : ''}" data-tone="${Entry.tone}" data-action="select-curve" data-key="${esc(Entry.key)}"><i></i>${esc(Entry.short || Entry.label)}</button>`).join('');
        const PresetNames = Object.keys(state.presets);
        const PresetOptions = PresetNames.length ? PresetNames.map(Name => `<option value="${esc(Name)}">${esc(Name)}</option>`).join('') : '<option value="">No saved presets</option>';

        const StatCards = [
            statCard({ tone: 'blue', glyph: 'weight', label: 'Mass', value: Vehicle.massKg, unit: 'kg', trend: 'Incl. driver & fluids' }),
            statCard({ tone: 'amber', glyph: 'gauge', label: 'Peak torque', value: Torque, unit: 'N·m', derived: 'peakTorque', trend: 'From torque curve' }),
            statCard({ tone: 'violet', glyph: 'ruler', label: 'Wheelbase', value: Vehicle.wheelbaseMm, unit: 'mm', trend: 'Chassis datum' }),
            statCard({ tone: 'cyan', glyph: 'wind', label: 'Drag area', value: Vehicle.dragAreaM2, digits: 2, unit: 'm²', trend: 'Cd · frontal area' }),
            statCard({ tone: 'green', glyph: 'layers', label: 'Downforce', value: Vehicle.downforceN, unit: 'N', trend: 'At 250 km/h' }),
            statCard({ tone: 'red', glyph: 'zap', label: 'Top speed', value: Vehicle.topSpeedKph, unit: 'km/h', trend: `${fmt(Vehicle.sprintS, 1)} s · 0–100` })
        ].join('');

        const Fields = [
            fieldHtml({ label: 'Vehicle name', bind: 'name', value: Vehicle.name, kind: 'string', tone: 'blue', glyph: 'car' }),
            fieldHtml({ label: 'Class', bind: 'category', value: Vehicle.category, kind: 'string', tone: 'violet', glyph: 'folder' }),
            fieldHtml({ label: 'Mass', bind: 'massKg', value: Vehicle.massKg, unit: 'kg', min: 400, step: 5, tone: 'blue', glyph: 'weight' }),
            fieldHtml({ label: 'Peak power', bind: 'powerKw', value: Vehicle.powerKw, unit: 'kW', min: 20, step: 1, tone: 'amber', glyph: 'zap' }),
            fieldHtml({ label: 'Wheelbase', bind: 'wheelbaseMm', value: Vehicle.wheelbaseMm, unit: 'mm', min: 1200, step: 10, tone: 'violet', glyph: 'ruler' }),
            fieldHtml({ label: 'Top speed', bind: 'topSpeedKph', value: Vehicle.topSpeedKph, unit: 'km/h', min: 80, step: 1, tone: 'red', glyph: 'gauge' }),
            fieldHtml({ label: 'Drag area', bind: 'dragAreaM2', value: Vehicle.dragAreaM2, unit: 'm²', min: 0.1, step: 0.01, tone: 'cyan', glyph: 'wind' }),
            fieldHtml({ label: 'Downforce', bind: 'downforceN', value: Vehicle.downforceN, unit: 'N', min: 0, step: 10, tone: 'green', glyph: 'layers' }),
            fieldHtml({ label: 'Fuel load', bind: 'fuelKg', value: Vehicle.fuelKg, unit: 'kg', min: 0, step: 1, tone: 'amber', glyph: 'fuel' }),
            fieldHtml({ label: '0–100', bind: 'sprintS', value: Vehicle.sprintS, unit: 's', min: 1, step: 0.1, tone: 'red', glyph: 'clock' }),
            fieldHtml({ label: 'Redline', bind: 'maxRpm', value: Vehicle.maxRpm, unit: 'rpm', min: 3000, step: 100, tone: 'white', glyph: 'activity' })
        ].join('');

        const TyresBody = `<p class="section-caption">Four child sockets are parented to <b>${esc(Vehicle.name)}</b>. Detached sockets stay visible in the Outliner until a pit stop fits a replacement.</p>
            <div class="tyre-grid">${Vehicle.tyres.map(tyreCard).join('')}</div>
            <div class="pitstop-panel" data-tone="amber">
                <div class="pitstop-head">${icon('wrench')}<span>Pit stop compound</span><small>${mountedCount(Vehicle)} / 4 fitted</small></div>
                ${compoundButtons(Vehicle.pitCompound, 'set-pit-compound')}
                <div class="action-row" data-tone="amber">
                    <button type="button" class="btn primary" data-action="replace-set" data-id="${esc(Vehicle.id)}">${icon('fit')} Replace set</button>
                    <button type="button" class="btn" data-action="fit-empty" data-id="${esc(Vehicle.id)}">${icon('plus')} Fit empty sockets</button>
                    <button type="button" class="btn ghost" data-action="go-vehicle">${icon('car')} Vehicle record</button>
                </div>
            </div>`;

        const CurvesBody = `<p class="section-caption">Editable vehicle curves. Drag a point, click the plot to insert one, double-click a point to remove it, or use the arrow keys for fine edits.</p>
            <div class="curve-tabs">${CurveTabs}<button type="button" class="curve-tab add" data-tone="white" data-action="new-curve">${icon('plus')} New curve</button></div>
            <div class="curve-panel" id="CurveEditorHost" data-tone="${esc(Curve.tone)}"></div>
            <div class="action-row" style="margin-top:9px" data-tone="blue">
                <button type="button" class="btn ghost" data-action="reset-builtins">${icon('reset')} Reset built-ins</button>
                <button type="button" class="btn" data-action="export-json">${icon('download')} Export curves</button>
            </div>`;

        const CustomBody = `<p class="section-caption">Author a custom vehicle from the record fields and curves. Saving stores a portable preset in this browser; adding creates another data-only scene record.</p>
            <div class="field-grid">${Fields}</div>
            <div style="margin-top:11px" data-tone="violet"><div class="field-head">${icon('drive')}<span>Driven axle</span></div>
                <div class="segmented">${['RWD', 'AWD', 'FWD'].map(Drive => `<button type="button" class="segment${Vehicle.drive === Drive ? ' active' : ''}" data-action="set-drive" data-value="${Drive}">${Drive}</button>`).join('')}</div>
            </div>
            <div class="action-row" style="margin-top:11px" data-tone="blue">
                <button type="button" class="btn primary" data-action="save-preset">${icon('save')} Save preset</button>
                <button type="button" class="btn" data-action="add-vehicle">${icon('plus')} Add vehicle from spec</button>
                <button type="button" class="btn" data-action="export-json">${icon('download')} Export JSON</button>
            </div>
            <div class="preset-row"><select id="PresetSelect" aria-label="Saved vehicle presets">${PresetOptions}</select></div>
            <pre class="levelspace" id="VehicleLevelspace">${esc(levelspaceText(Vehicle))}</pre>`;

        return `<div class="inspector">
            <div class="breadcrumb-row"><div class="breadcrumb">Inspector / <b>Vehicles</b> / <b>${esc(Vehicle.name)}</b></div><button type="button" class="notes-pill">${icon('plus')} Add notes</button></div>
            <div class="page-title"><h2>${esc(Vehicle.name)}</h2><span class="live-pill"><i></i>DATA RECORD</span></div>
            <div class="page-subtitle">${esc(Vehicle.category)} · no geometry attached. Specification, parented tyre sockets and editable curves live in one inspector.</div>
            ${pageTabs([{ key: 'overview', label: 'Overview', target: 'Section-Overview' }, { key: 'tyres', label: 'Tyres', target: 'Section-Tyres' }, { key: 'curves', label: 'Curves', target: 'Section-Curves' }, { key: 'custom', label: 'Custom', target: 'Section-Custom' }], state.tab)}
            <div id="Section-Overview">
                ${heroCard({ tone: 'amber', glyph: 'zap', chip: 'Custom vehicle record', tag: 'NO GEOMETRY', title: 'Peak power', value: Vehicle.powerKw, digits: 0, unit: 'kW', sideHtml: `<span data-derived="powerWeight">${fmt(Pwr, 1)}</span>`, sideUnit: 'kW/t', low: `${fmt(Torque, 0)} N·m peak torque`, noteHtml: `<b>${mountedCount(Vehicle)} of 4 tyre sockets fitted.</b> Tyres are parented child records and can be removed or replaced at pit stops; the Vehicle itself carries no mesh.` })}
                ${sectionCard('Card-Specification', 'blue', 'gauge', 'Chassis & power', 'UNITS', `<div class="stat-grid">${StatCards}</div>`)}
            </div>
            <div id="Section-Tyres">${sectionCard('Card-Tyres', 'red', 'tyre', 'Tyre set', 'PARENTED · 4', TyresBody)}</div>
            <div id="Section-Curves">${sectionCard('Card-Curves', Curve.tone, 'curve', 'Engine & thermal curves', 'EDITABLE', CurvesBody)}</div>
            <div id="Section-Custom">${sectionCard('Card-Custom', 'violet', 'sparkles', 'Custom vehicle', 'PROPOSED', CustomBody)}</div>
            <p class="footer-note">Inspector-first step: no additional geometry and no full vehicle/tyre editor yet. Curve points, tyre sockets and the record fields are live prototype controls.</p>
        </div>`;
    }

    function renderTyreInspector(Vehicle, Tyre)
    {
        const Tone = compoundTone(Tyre.compound);
        const StatCards = [
            statCard({ tone: 'blue', glyph: 'gauge', label: 'Pressure', value: Tyre.pressureKpa, unit: 'kPa', trend: Tyre.mounted ? 'Cold baseline' : 'Socket empty' }),
            statCard({ tone: 'amber', glyph: 'thermometer', label: 'Core temp', value: Tyre.coreTempC, unit: '°C', trend: 'Live estimate' }),
            statCard({ tone: 'red', glyph: 'activity', label: 'Wear', value: Tyre.wearPct, unit: '%', trend: `${fmt(100 - Tyre.wearPct, 0)} % remaining` }),
            statCard({ tone: 'green', glyph: 'target', label: 'Grip', value: Tyre.gripMu, digits: 2, unit: 'µ', trend: 'Current compound' }),
            statCard({ tone: 'violet', glyph: 'clock', label: 'Stint', value: Tyre.stintLaps, unit: 'laps', trend: 'Since last pit stop' }),
            statCard({ tone: Tone, glyph: 'droplet', label: 'Compound', text: Tyre.compound, unit: 'class', trend: 'Replaceable' })
        ].join('');
        const ConditionBody = `<p class="section-caption">Condition fields use explicit units. Values are prototype data; no tyre mesh or wheel geometry is attached.</p>
            <div class="stat-grid">${StatCards}</div>
            ${rangeRow({ label: 'Pressure', bind: 'pressureKpa', scope: 'tyre', value: Tyre.pressureKpa, min: 80, max: 240, step: 1, digits: 0, unit: 'kPa', tone: 'blue' })}
            ${rangeRow({ label: 'Core temperature', bind: 'coreTempC', scope: 'tyre', value: Tyre.coreTempC, min: 20, max: 150, step: 1, digits: 0, unit: '°C', tone: 'amber' })}
            ${rangeRow({ label: 'Wear', bind: 'wearPct', scope: 'tyre', value: Tyre.wearPct, min: 0, max: 100, step: 1, digits: 0, unit: '%', tone: 'red' })}
            ${rangeRow({ label: 'Available grip', bind: 'gripMu', scope: 'tyre', value: Tyre.gripMu, min: 0, max: 1.5, step: 0.01, digits: 2, unit: 'µ', tone: 'green' })}`;
        const PitstopBody = `<p class="section-caption">A tyre is a removable child of <b>${esc(Vehicle.name)}</b>. Detach leaves an empty socket in the Outliner; fitting or replacing mounts a fresh tyre record.</p>
            <div class="pitstop-head">${icon('wrench')}<span>Pit stop</span><small>${Tyre.mounted ? 'MOUNTED' : 'EMPTY SOCKET'}</small></div>
            ${compoundButtons(Tyre.compound, 'set-compound', `data-id="${esc(Tyre.id)}"`)}
            <div class="action-row" data-tone="amber">
                ${Tyre.mounted ? `<button type="button" class="btn primary" data-action="replace-tyre" data-id="${esc(Tyre.id)}">${icon('fit')} Replace tyre</button><button type="button" class="btn danger" data-action="detach-tyre" data-id="${esc(Tyre.id)}">${icon('trash')} Detach tyre</button>` : `<button type="button" class="btn primary" data-action="fit-tyre" data-id="${esc(Tyre.id)}">${icon('plus')} Fit tyre</button>`}
                <button type="button" class="btn ghost" data-action="go-vehicle">${icon('car')} Parent vehicle</button>
            </div>`;
        const ParentBody = `<p class="section-caption">Parent record and hierarchy state.</p>
            <div class="stat-grid">
                ${statCard({ tone: 'blue', glyph: 'car', label: 'Parent', text: Vehicle.name.split('/').pop().trim(), unit: 'id', trend: Vehicle.category })}
                ${statCard({ tone: 'green', glyph: 'layers', label: 'Fitted', text: `${mountedCount(Vehicle)}/4`, unit: 'sockets', trend: 'Child records' })}
                ${statCard({ tone: 'white', glyph: 'box', label: 'Meshes', text: '0', unit: 'count', trend: 'Data-only' })}
            </div>
            <pre class="levelspace" id="TyreLevelspace">${esc(tyreLevelspaceText(Vehicle, Tyre))}</pre>`;
        return `<div class="inspector">
            <div class="breadcrumb-row"><div class="breadcrumb">Inspector / <b>${esc(Vehicle.name)}</b> / <b>${esc(Tyre.code)}</b></div><button type="button" class="notes-pill">${icon('plus')} Add notes</button></div>
            <div class="page-title"><h2>${esc(Tyre.name)} tyre</h2><span class="live-pill"><i></i>PARENTED</span></div>
            <div class="page-subtitle">${esc(Tyre.code)} socket on ${esc(Vehicle.name)} · ${Tyre.mounted ? `${esc(Tyre.compound)} compound` : 'empty socket'}. Detachable and replaceable at pit stops.</div>
            ${heroCard({ tone: Tone, glyph: 'tyre', chip: Tyre.mounted ? `${Tyre.compound} compound` : 'Empty socket', tag: Tyre.mounted ? 'MOUNTED' : 'PIT STOP', title: Tyre.mounted ? 'Cold pressure' : 'Socket state', value: Tyre.mounted ? Tyre.pressureKpa : 0, digits: 0, unit: 'kPa', sideHtml: Tyre.mounted ? fmt(Tyre.coreTempC, 0) : '—', sideUnit: '°C', low: Tyre.mounted ? `${fmt(Tyre.wearPct, 0)} % wear · ${fmt(Tyre.gripMu, 2)} µ grip` : 'Fit a replacement compound', noteHtml: `<b>Parented child:</b> ${esc(Tyre.id)}. No geometry is spawned for this tyre record.` })}
            ${sectionCard('Card-Condition', Tone, 'gauge', 'Tyre condition', 'EDITABLE', ConditionBody)}
            ${sectionCard('Card-Pitstop', 'amber', 'wrench', 'Pit stop', 'REMOVE / REPLACE', PitstopBody)}
            ${sectionCard('Card-Parent', 'blue', 'car', 'Parent record', 'NO GEOMETRY', ParentBody)}
            <p class="footer-note">Tyre editor is the next step; this page shows the inspector surface and the removable parented socket first.</p>
        </div>`;
    }

    function renderInspector()
    {
        const Scroller = $('InspectorScroll');
        const PreviousScroll = Scroller.scrollTop;
        const Selection = resolveSelection();
        let Html;
        let VehicleForCurve = null;
        let CurveForEditor = null;
        if (Selection.kind === 'scene') Html = renderStaticInspector(Selection.row);
        else if (Selection.kind === 'tyre') Html = renderTyreInspector(Selection.vehicle, Selection.tyre);
        else
        {
            VehicleForCurve = Selection.vehicle;
            CurveForEditor = activeCurve(VehicleForCurve);
            Html = renderVehicleInspector(VehicleForCurve);
        }
        $('InspectorContent').innerHTML = Html;
        if (VehicleForCurve && CurveForEditor)
        {
            createCurveEditor($('CurveEditorHost'), CurveForEditor, {
                onChange: (CommitChange, Reason) => handleCurveChange(VehicleForCurve, CurveForEditor, CommitChange, Reason),
                onDelete: Key => deleteCurve(VehicleForCurve, Key)
            });
        }
        Scroller.scrollTop = PreviousScroll;
    }

    function handleCurveChange(Vehicle, Curve, IsCommit, Reason)
    {
        if (!IsCommit) return;
        refreshDerivedLabels(Vehicle);
        softCommit('CURVE', `${Curve.label} · ${Reason === 'drag' ? 'points edited' : Reason}`, Curve.tone);
        if (Reason !== 'drag' && Reason !== 'keyboard') toast(`${Curve.short || Curve.label} updated`);
    }

    function deleteCurve(Vehicle, Key)
    {
        const Curve = Vehicle.curves[Key];
        if (!Curve || !Curve.custom) return;
        delete Vehicle.curves[Key];
        state.curveKey = Object.keys(Vehicle.curves)[0] || 'torque';
        commit('CURVE', `${Curve.label} removed`, 'red', 'Custom curve removed');
    }

    function createCurveEditor(Host, Curve, Callbacks)
    {
        const Width = 600, Height = 210;
        const Pad = { left: 48, right: 18, top: 15, bottom: 34 };
        const PlotWidth = Width - Pad.left - Pad.right;
        const PlotHeight = Height - Pad.top - Pad.bottom;
        const Bottom = Pad.top + PlotHeight;
        let Active = Math.min(2, Math.max(0, Curve.points.length - 1));
        let Dragging = -1;

        Host.innerHTML = `<div class="curve-header"><strong>${esc(Curve.label)}</strong><span>${esc(Curve.yLabel)} / ${esc(Curve.xLabel)}</span><span class="spacer"></span><span class="curve-accent">${Curve.custom ? 'CUSTOM' : 'EDITABLE'}</span></div>
            <svg class="curve-svg" viewBox="0 0 ${Width} ${Height}" preserveAspectRatio="none" tabindex="0" role="application" aria-label="${esc(Curve.label)} editable curve"></svg>
            <div class="curve-readout"></div>
            <div class="curve-toolbar">
                <button type="button" class="btn tiny" data-curve-action="add">${icon('plus')} Point</button>
                <button type="button" class="btn tiny" data-curve-action="remove">${icon('trash')} Remove</button>
                <button type="button" class="btn tiny" data-curve-action="smooth">Smooth</button>
                <button type="button" class="btn tiny ghost" data-curve-action="reset">${icon('reset')} Reset</button>
                <span class="spacer"></span>
                ${Curve.custom ? `<button type="button" class="btn tiny danger" data-curve-action="delete">${icon('trash')} Curve</button>` : ''}
            </div>
            <div class="curve-help"><kbd>Drag</kbd> move · <kbd>Click</kbd> add · <kbd>Double-click</kbd> remove · <kbd>← → ↑ ↓</kbd> fine edit · <kbd>Shift</kbd> ×5</div>`;
        const Svg = Host.querySelector('.curve-svg');
        const Readout = Host.querySelector('.curve-readout');
        const Toolbar = Host.querySelector('.curve-toolbar');

        const screen = Point =>
        {
            const X = Pad.left + (Point[0] - Curve.xMin) / (Curve.xMax - Curve.xMin) * PlotWidth;
            const Y = Pad.top + (1 - (Point[1] - Curve.yMin) / (Curve.yMax - Curve.yMin)) * PlotHeight;
            return [X, Y];
        };
        const dataAt = Event =>
        {
            const Bounds = Svg.getBoundingClientRect();
            const X = clamp((Event.clientX - Bounds.left) / Bounds.width * Width, Pad.left, Pad.left + PlotWidth);
            const Y = clamp((Event.clientY - Bounds.top) / Bounds.height * Height, Pad.top, Bottom);
            return [
                Curve.xMin + (X - Pad.left) / PlotWidth * (Curve.xMax - Curve.xMin),
                Curve.yMin + (Bottom - Y) / PlotHeight * (Curve.yMax - Curve.yMin)
            ];
        };
        const snap = (Value, Step, Minimum, Maximum) => round(clamp(Minimum + Math.round((Value - Minimum) / Step) * Step, Minimum, Maximum));
        const axis = (Value, Digits, Unit) => `${fmt(Value, Digits)}${Unit ? ' ' + Unit : ''}`;

        function pathData()
        {
            const Points = Curve.points.map(screen);
            if (!Points.length) return '';
            let Data = `M ${Points[0][0].toFixed(2)} ${Points[0][1].toFixed(2)}`;
            for (let Index = 0; Index < Points.length - 1; ++Index)
            {
                const Previous = Points[Math.max(0, Index - 1)];
                const Current = Points[Index];
                const Next = Points[Index + 1];
                const Following = Points[Math.min(Points.length - 1, Index + 2)];
                if (!Curve.smooth)
                {
                    Data += ` L ${Next[0].toFixed(2)} ${Next[1].toFixed(2)}`;
                    continue;
                }
                const Tension = 0.82;
                const C1 = [Current[0] + (Next[0] - Previous[0]) * Tension / 6, Current[1] + (Next[1] - Previous[1]) * Tension / 6];
                const C2 = [Next[0] - (Following[0] - Current[0]) * Tension / 6, Next[1] - (Following[1] - Current[1]) * Tension / 6];
                Data += ` C ${C1[0].toFixed(2)} ${C1[1].toFixed(2)}, ${C2[0].toFixed(2)} ${C2[1].toFixed(2)}, ${Next[0].toFixed(2)} ${Next[1].toFixed(2)}`;
            }
            return Data;
        }

        function setPoint(Index, RawPoint)
        {
            if (Index < 0 || Index >= Curve.points.length) return;
            const Last = Curve.points.length - 1;
            let X = snap(RawPoint[0], Curve.stepX, Curve.xMin, Curve.xMax);
            const Y = snap(RawPoint[1], Curve.stepY, Curve.yMin, Curve.yMax);
            if (Index === 0) X = Curve.xMin;
            else if (Index === Last) X = Curve.xMax;
            else
            {
                const Previous = Curve.points[Index - 1][0];
                const Next = Curve.points[Index + 1][0];
                const Lower = Previous + Curve.stepX;
                const Upper = Next - Curve.stepX;
                X = Lower > Upper ? round((Previous + Next) / 2) : clamp(X, Lower, Upper);
            }
            Curve.points[Index] = [X, Y];
        }

        function insertPoint(RawPoint)
        {
            let Index = Curve.points.findIndex(Point => Point[0] > RawPoint[0]);
            if (Index < 0) Index = Curve.points.length - 1;
            if (Index < 1) Index = 1;
            const Left = Curve.points[Index - 1];
            const Right = Curve.points[Index];
            const Lower = Left[0] + Curve.stepX;
            const Upper = Right[0] - Curve.stepX;
            const X = Lower > Upper ? round((Left[0] + Right[0]) / 2)
                : clamp(snap(RawPoint[0], Curve.stepX, Curve.xMin, Curve.xMax), Lower, Upper);
            const Share = (X - Left[0]) / Math.max(0.0001, Right[0] - Left[0]);
            const Y = snap(Left[1] + (Right[1] - Left[1]) * Share, Curve.stepY, Curve.yMin, Curve.yMax);
            Curve.points.splice(Index, 0, [X, Y]);
            return Index;
        }

        function removePoint(Index)
        {
            if (Curve.points.length <= 2 || Index === 0 || Index === Curve.points.length - 1) return false;
            Curve.points.splice(Index, 1);
            Active = clamp(Index - 1, 0, Curve.points.length - 1);
            return true;
        }

        function refresh()
        {
            const Points = Curve.points.map(screen);
            const Line = pathData();
            const First = Points[0] || [Pad.left, Bottom];
            const Last = Points[Points.length - 1] || [Pad.left + PlotWidth, Bottom];
            const Area = Line ? `${Line} L ${Last[0].toFixed(2)} ${Bottom} L ${First[0].toFixed(2)} ${Bottom} Z` : '';
            const ActivePoint = Points[Active] || [Pad.left, Bottom];
            let Grid = `<line class="curve-axis" x1="${Pad.left}" y1="${Pad.top}" x2="${Pad.left}" y2="${Bottom}"/><line class="curve-axis" x1="${Pad.left}" y1="${Bottom}" x2="${Pad.left + PlotWidth}" y2="${Bottom}"/>`;
            for (let Index = 1; Index <= 3; ++Index)
            {
                const X = Pad.left + PlotWidth * Index / 4;
                const Y = Pad.top + PlotHeight * Index / 4;
                Grid += `<line class="curve-grid" x1="${X}" y1="${Pad.top}" x2="${X}" y2="${Bottom}"/><line class="curve-grid" x1="${Pad.left}" y1="${Y}" x2="${Pad.left + PlotWidth}" y2="${Y}"/>`;
            }
            Grid += `<text class="curve-label right" x="${Pad.left - 7}" y="${Pad.top + 4}">${esc(axis(Curve.yMax, Curve.yDigits, Curve.yUnit))}</text>
                <text class="curve-label right" x="${Pad.left - 7}" y="${Pad.top + PlotHeight / 2 + 3}">${esc(axis((Curve.yMax + Curve.yMin) / 2, Curve.yDigits, ''))}</text>
                <text class="curve-label right" x="${Pad.left - 7}" y="${Bottom + 3}">${esc(axis(Curve.yMin, Curve.yDigits, ''))}</text>
                <text class="curve-label" x="${Pad.left}" y="${Height - 8}">${esc(axis(Curve.xMin, Curve.xDigits, Curve.xUnit))}</text>
                <text class="curve-label middle" x="${Pad.left + PlotWidth / 2}" y="${Height - 8}">${esc(axis((Curve.xMin + Curve.xMax) / 2, Curve.xDigits, ''))}</text>
                <text class="curve-label right" x="${Pad.left + PlotWidth}" y="${Height - 8}">${esc(axis(Curve.xMax, Curve.xDigits, ''))}</text>`;
            const Guides = `<line class="curve-guide" x1="${ActivePoint[0]}" y1="${Pad.top}" x2="${ActivePoint[0]}" y2="${Bottom}"/><line class="curve-guide" x1="${Pad.left}" y1="${ActivePoint[1]}" x2="${Pad.left + PlotWidth}" y2="${ActivePoint[1]}"/>`;
            const Circles = Points.map((Point, Index) => `<circle class="curve-point${Index === Active ? ' active' : ''}" cx="${Point[0].toFixed(2)}" cy="${Point[1].toFixed(2)}" r="4.5" data-point="${Index}"/><circle class="curve-hit" cx="${Point[0].toFixed(2)}" cy="${Point[1].toFixed(2)}" r="11" data-point="${Index}"/>`).join('');
            Svg.innerHTML = `${Grid}<path class="curve-area" d="${Area}"/><path class="curve-line" d="${Line}"/>${Guides}${Circles}`;
            const Point = Curve.points[Active];
            Readout.innerHTML = `<span>Point <b>${Active + 1}</b> / ${Curve.points.length}</span><span class="spacer"></span><b>${esc(axis(Point[0], Curve.xDigits, Curve.xUnit))}</b><span>·</span><b>${esc(axis(Point[1], Curve.yDigits, Curve.yUnit))}</b><small>${Curve.smooth ? 'spline' : 'linear'}</small>`;
            const SmoothButton = Toolbar.querySelector('[data-curve-action="smooth"]');
            const RemoveButton = Toolbar.querySelector('[data-curve-action="remove"]');
            if (SmoothButton) SmoothButton.textContent = Curve.smooth ? 'Smooth' : 'Linear';
            if (RemoveButton) RemoveButton.disabled = Curve.points.length <= 2 || Active === 0 || Active === Curve.points.length - 1;
        }

        Svg.addEventListener('pointerdown', Event =>
        {
            const Hit = Event.target.closest('[data-point]');
            if (Hit)
            {
                Active = Number(Hit.dataset.point);
                Dragging = Active;
            }
            else
            {
                Active = insertPoint(dataAt(Event));
                Dragging = Active;
            }
            Svg.setPointerCapture(Event.pointerId);
            refresh();
        });
        Svg.addEventListener('pointermove', Event =>
        {
            if (Dragging < 0) return;
            setPoint(Dragging, dataAt(Event));
            refresh();
            Callbacks.onChange(false, 'drag');
        });
        const endDrag = Event =>
        {
            if (Dragging < 0) return;
            Dragging = -1;
            try { Svg.releasePointerCapture(Event.pointerId); } catch { /* capture may already be gone */ }
            Callbacks.onChange(true, 'drag');
        };
        Svg.addEventListener('pointerup', endDrag);
        Svg.addEventListener('pointercancel', endDrag);
        Svg.addEventListener('dblclick', Event =>
        {
            const Hit = Event.target.closest('[data-point]');
            if (!Hit) return;
            Event.preventDefault();
            if (removePoint(Number(Hit.dataset.point)))
            {
                refresh();
                Callbacks.onChange(true, 'point removed');
            }
        });
        Svg.addEventListener('keydown', Event =>
        {
            const Multiplier = Event.shiftKey ? 5 : 1;
            let Handled = true;
            if (Event.key === 'ArrowLeft') setPoint(Active, [Curve.points[Active][0] - Curve.stepX * Multiplier, Curve.points[Active][1]]);
            else if (Event.key === 'ArrowRight') setPoint(Active, [Curve.points[Active][0] + Curve.stepX * Multiplier, Curve.points[Active][1]]);
            else if (Event.key === 'ArrowUp') setPoint(Active, [Curve.points[Active][0], Curve.points[Active][1] + Curve.stepY * Multiplier]);
            else if (Event.key === 'ArrowDown') setPoint(Active, [Curve.points[Active][0], Curve.points[Active][1] - Curve.stepY * Multiplier]);
            else if (Event.key === '+' || Event.key === '=') Active = insertPoint([(Curve.xMin + Curve.xMax) / 2, (Curve.yMin + Curve.yMax) / 2]);
            else if (Event.key === '-' || Event.key === 'Delete' || Event.key === 'Backspace') removePoint(Active);
            else Handled = false;
            if (Handled) { Event.preventDefault(); refresh(); Callbacks.onChange(true, 'keyboard'); }
        });
        Toolbar.addEventListener('click', Event =>
        {
            const Button = Event.target.closest('[data-curve-action]');
            if (!Button) return;
            const Action = Button.dataset.curveAction;
            if (Action === 'add') Active = insertPoint([(Curve.xMin + Curve.xMax) / 2, Curve.points[Math.floor(Curve.points.length / 2)][1]]);
            else if (Action === 'remove') removePoint(Active);
            else if (Action === 'smooth') Curve.smooth = !Curve.smooth;
            else if (Action === 'reset') { Curve.points = clone(Curve.defaultPoints); Curve.smooth = true; Active = Math.min(2, Curve.points.length - 1); }
            else if (Action === 'delete') { Callbacks.onDelete(Curve.key); return; }
            refresh();
            Callbacks.onChange(true, Action === 'smooth' ? 'interpolation changed' : Action === 'reset' ? 'reset' : 'points edited');
        });
        refresh();
    }

    function fitTyre(Vehicle, Tyre, CompoundName)
    {
        const Preset = compound(CompoundName);
        Object.assign(Tyre, {
            mounted: true, compound: CompoundName, pressureKpa: Preset.pressure, coreTempC: Preset.temp,
            wearPct: 0, gripMu: Preset.grip, stintLaps: 0
        });
    }

    function detachTyre(Tyre)
    {
        Object.assign(Tyre, { mounted: false, pressureKpa: 0, coreTempC: 24, wearPct: 0, gripMu: 0, stintLaps: 0 });
    }

    function vehicleSnapshot(Vehicle)
    {
        return clone(Vehicle);
    }

    function applyPreset(Vehicle, Preset)
    {
        const Fields = ['name', 'category', 'drive', 'massKg', 'powerKw', 'wheelbaseMm', 'topSpeedKph', 'dragAreaM2', 'downforceN', 'fuelKg', 'sprintS', 'maxRpm', 'pitCompound'];
        Fields.forEach(Field => { if (Preset[Field] !== undefined) Vehicle[Field] = Preset[Field]; });
        if (Preset.curves) Vehicle.curves = clone(Preset.curves);
        Object.entries(Vehicle.curves).forEach(([Key, Curve]) =>
        {
            const Tones = ['blue', 'amber', 'red', 'green', 'violet', 'cyan', 'white'];
            Curve.key = Key;
            Curve.xMin = Number.isFinite(Curve.xMin) ? Curve.xMin : 0;
            Curve.xMax = Number.isFinite(Curve.xMax) && Curve.xMax > Curve.xMin ? Curve.xMax : 100;
            Curve.yMin = Number.isFinite(Curve.yMin) ? Curve.yMin : 0;
            Curve.yMax = Number.isFinite(Curve.yMax) && Curve.yMax > Curve.yMin ? Curve.yMax : 100;
            Curve.stepX = Number.isFinite(Curve.stepX) && Curve.stepX > 0 ? Curve.stepX : (Curve.xMax - Curve.xMin) / 50;
            Curve.stepY = Number.isFinite(Curve.stepY) && Curve.stepY > 0 ? Curve.stepY : (Curve.yMax - Curve.yMin) / 100;
            Curve.xDigits = Number.isFinite(Curve.xDigits) ? Curve.xDigits : 0;
            Curve.yDigits = Number.isFinite(Curve.yDigits) ? Curve.yDigits : 0;
            Curve.tone = Tones.includes(Curve.tone) ? Curve.tone : 'violet';
            Curve.smooth = Curve.smooth !== false;
            Curve.custom = !!Curve.custom;
            Curve.label = Curve.label || Key;
            Curve.short = Curve.short || Curve.label;
            if (!Array.isArray(Curve.points) || Curve.points.length < 2)
                Curve.points = [[Curve.xMin, Curve.yMin], [Curve.xMax, Curve.yMax]];
            if (!Array.isArray(Curve.defaultPoints) || Curve.defaultPoints.length < 2)
                Curve.defaultPoints = clone(Curve.points);
        });
        if (Array.isArray(Preset.tyres))
        {
            Preset.tyres.forEach((Source, Index) =>
            {
                if (!Vehicle.tyres[Index]) return;
                ['mounted', 'compound', 'pressureKpa', 'coreTempC', 'wearPct', 'gripMu', 'stintLaps'].forEach(Field => { Vehicle.tyres[Index][Field] = Source[Field]; });
            });
        }
        Vehicle.tyres.forEach(Tyre => { Tyre.id = `${Vehicle.id}:${Tyre.code.toLowerCase()}`; });
        state.curveKey = Vehicle.curves[state.curveKey] ? state.curveKey : Object.keys(Vehicle.curves)[0];
    }

    function addVehicleFromSpec()
    {
        const Source = activeVehicle();
        const Ordinal = ++state.vehicleSerial;
        const Vehicle = vehicleSnapshot(Source);
        Vehicle.id = `v${Ordinal}`;
        Vehicle.ordinal = Ordinal;
        Vehicle.name = /\d+$/.test(Source.name) ? Source.name.replace(/\d+$/, String(Ordinal).padStart(2, '0')) : `${Source.name} ${String(Ordinal).padStart(2, '0')}`;
        Vehicle.tyres.forEach(Tyre => { Tyre.id = `${Vehicle.id}:${Tyre.code.toLowerCase()}`; });
        state.vehicles.push(Vehicle);
        state.expanded.add(Vehicle.id);
        state.lastVehicleId = Vehicle.id;
        state.selected = Vehicle.id;
        state.tab = 'overview';
        commit('VEHICLE', `${Vehicle.name} added from current spec · 4 parented sockets`, 'green', `${Vehicle.name} added · no geometry`);
    }

    function addCustomCurve(Values)
    {
        const Vehicle = activeVehicle();
        const Label = String(Values.get('label') || '').trim() || 'Custom curve';
        const ToneValue = String(Values.get('tone') || 'violet');
        const Tone = ['violet', 'cyan', 'green', 'amber', 'red', 'blue'].includes(ToneValue) ? ToneValue : 'violet';
        const XMax = Math.max(1, Number(Values.get('xMax')) || 100);
        const YMax = Math.max(1, Number(Values.get('yMax')) || 100);
        const Key = `custom-${Date.now().toString(36)}`;
        const Points = [[0, 0], [XMax * 0.25, YMax * 0.62], [XMax * 0.5, YMax * 0.88], [XMax * 0.75, YMax * 0.68], [XMax, YMax * 0.35]];
        const Curve = {
            key: Key, label: Label, short: Label.length > 11 ? Label.slice(0, 10) + '…' : Label, tone: Tone, custom: true, smooth: true,
            xLabel: String(Values.get('xLabel') || 'X').trim(), xUnit: String(Values.get('xUnit') || '').trim(),
            yLabel: String(Values.get('yLabel') || 'Y').trim(), yUnit: String(Values.get('yUnit') || '').trim(),
            xMin: 0, xMax: XMax, yMin: 0, yMax: YMax, xDigits: 0, yDigits: YMax <= 10 ? 2 : 0,
            stepX: Math.max(XMax / 100, 0.01), stepY: Math.max(YMax / 100, 0.01), points: Points
        };
        Curve.defaultPoints = clone(Points);
        Vehicle.curves[Key] = Curve;
        state.curveKey = Key;
        state.tab = 'curves';
        commit('CURVE', `${Label} added to ${Vehicle.name}`, Tone, `${Label} curve added`);
        scrollToSection('Section-Curves');
    }

    function resetBuiltInCurves()
    {
        const Vehicle = activeVehicle();
        const Defaults = defaultCurves();
        Object.entries(Defaults).forEach(([Key, Curve]) => { Vehicle.curves[Key] = Curve; });
        state.curveKey = 'torque';
        commit('CURVE', 'Built-in torque, thermal, boost and grip curves reset', 'blue', 'Built-in curves reset');
    }

    function showJsonDialog()
    {
        const Vehicle = activeVehicle();
        $('JsonArea').value = JSON.stringify(buildRecord(Vehicle), null, 2);
        $('JsonDialogTitle').textContent = `${Vehicle.name} · custom vehicle record`;
        try { $('JsonDialog').showModal(); } catch { $('JsonDialog').setAttribute('open', ''); }
    }

    function copyJson()
    {
        const Area = $('JsonArea');
        Area.select();
        const Text = Area.value;
        if (navigator.clipboard && navigator.clipboard.writeText)
        {
            navigator.clipboard.writeText(Text).then(() => toast('Vehicle JSON copied')).catch(() => { document.execCommand('copy'); toast('Vehicle JSON copied'); });
        }
        else { document.execCommand('copy'); toast('Vehicle JSON copied'); }
    }

    function downloadJson()
    {
        const Vehicle = activeVehicle();
        const Text = JSON.stringify(buildRecord(Vehicle), null, 2);
        const Blob = new Blob([Text], { type: 'application/json' });
        const Url = URL.createObjectURL(Blob);
        const Anchor = document.createElement('a');
        Anchor.href = Url;
        Anchor.download = `${Vehicle.name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'vehicle'}.json`;
        document.body.append(Anchor);
        Anchor.click();
        Anchor.remove();
        setTimeout(() => URL.revokeObjectURL(Url), 1000);
        toast('Vehicle JSON downloaded');
    }

    function select(Id)
    {
        state.selected = Id;
        const Selection = resolveSelection();
        if (Selection.kind === 'vehicle') state.lastVehicleId = Selection.vehicle.id;
        if (Selection.kind === 'tyre') state.lastVehicleId = Selection.vehicle.id;
        state.tab = Selection.kind === 'tyre' ? 'condition' : 'overview';
        state.curveKey = Selection.kind === 'vehicle' && Selection.vehicle.curves[state.curveKey] ? state.curveKey : 'torque';
        renderAll();
    }

    function scrollToSection(Target)
    {
        const Scroller = $('InspectorScroll');
        const Element = $(Target);
        if (!Scroller || !Element) return;
        const Position = Element.getBoundingClientRect().top - Scroller.getBoundingClientRect().top + Scroller.scrollTop - 46;
        Scroller.scrollTo({ top: Math.max(0, Position), behavior: 'smooth' });
    }

    function handleAction(Action, Element)
    {
        const Id = Element.dataset.id;
        const Value = Element.dataset.value;
        const Target = Element.dataset.target;
        const Vehicle = activeVehicle();
        switch (Action)
        {
            case 'add-vehicle': addVehicleFromSpec(); break;
            case 'toggle-expand':
                if (state.expanded.has(Id)) state.expanded.delete(Id); else state.expanded.add(Id);
                renderOutliner();
                break;
            case 'toggle-empty': state.showEmptySockets = !state.showEmptySockets; renderOutliner(); break;
            case 'go-vehicle': select(state.lastVehicleId); break;
            case 'jump':
                state.tab = Element.dataset.tab;
                document.querySelectorAll('.page-tab').forEach(Tab => Tab.classList.toggle('active', Tab.dataset.tab === state.tab));
                scrollToSection(Target);
                break;
            case 'fold':
                if (state.folded.has(Target)) state.folded.delete(Target); else state.folded.add(Target);
                $(Target).classList.toggle('folded');
                break;
            case 'select-curve':
                state.curveKey = Element.dataset.key;
                state.tab = 'curves';
                renderInspector();
                scrollToSection('Section-Curves');
                break;
            case 'new-curve':
                try { $('CurveDialog').showModal(); } catch { $('CurveDialog').setAttribute('open', ''); }
                break;
            case 'close-dialog':
                try { $(Element.dataset.dialog).close(); } catch { $(Element.dataset.dialog).removeAttribute('open'); }
                break;
            case 'set-pit-compound':
                Vehicle.pitCompound = Value;
                commit('PIT STOP', `Pit-stop compound set to ${Value}`, compoundTone(Value), `${Value} selected`);
                break;
            case 'replace-set':
                Vehicle.tyres.forEach(Tyre => fitTyre(Vehicle, Tyre, Vehicle.pitCompound));
                commit('PIT STOP', `All four tyres replaced with ${Vehicle.pitCompound}`, 'green', 'Fresh set fitted');
                break;
            case 'fit-empty':
            {
                const Count = Vehicle.tyres.filter(Tyre => !Tyre.mounted).length;
                Vehicle.tyres.forEach(Tyre => { if (!Tyre.mounted) fitTyre(Vehicle, Tyre, Vehicle.pitCompound); });
                commit('PIT STOP', `${Count} empty socket${Count === 1 ? '' : 's'} fitted with ${Vehicle.pitCompound}`, 'green', `${Count} tyre${Count === 1 ? '' : 's'} fitted`);
                break;
            }
            case 'fit-tyre':
            {
                const Owner = findTyreOwner(Id);
                if (Owner) { fitTyre(Owner.vehicle, Owner.tyre, Owner.vehicle.pitCompound); commit('PIT STOP', `${Owner.tyre.code} fitted with ${Owner.vehicle.pitCompound}`, 'green', `${Owner.tyre.code} tyre fitted`); }
                break;
            }
            case 'replace-tyre':
            {
                const Owner = findTyreOwner(Id);
                if (Owner) { fitTyre(Owner.vehicle, Owner.tyre, Owner.tyre.compound); commit('PIT STOP', `${Owner.tyre.code} tyre replaced · wear reset`, 'amber', `${Owner.tyre.code} replaced`); }
                break;
            }
            case 'detach-tyre':
            {
                const Owner = findTyreOwner(Id);
                if (Owner) { detachTyre(Owner.tyre); commit('PIT STOP', `${Owner.tyre.code} detached · empty socket retained`, 'red', `${Owner.tyre.code} detached`); }
                break;
            }
            case 'set-compound':
            {
                const Owner = findTyreOwner(Id);
                if (Owner) { fitTyre(Owner.vehicle, Owner.tyre, Value); commit('PIT STOP', `${Owner.tyre.code} compound changed to ${Value}`, compoundTone(Value), `${Value} fitted`); }
                break;
            }
            case 'set-drive':
                Vehicle.drive = Value;
                commit('VEHICLE', `Driven axle set to ${Value}`, 'violet', `${Value} selected`);
                break;
            case 'save-preset':
            {
                const Name = Vehicle.name.trim() || 'Untitled vehicle';
                state.presets[Name] = vehicleSnapshot(Vehicle);
                savePresets();
                commit('PRESET', `${Name} saved to browser presets`, 'green', 'Vehicle preset saved');
                break;
            }
            case 'export-json': showJsonDialog(); break;
            case 'copy-json': copyJson(); break;
            case 'download-json': downloadJson(); break;
            case 'reset-builtins': resetBuiltInCurves(); break;
            default: break;
        }
    }

    function handleChange(Event)
    {
        const Input = Event.target;
        if (Input.id === 'PresetSelect')
        {
            if (!Input.value || !state.presets[Input.value]) return;
            const Vehicle = activeVehicle();
            applyPreset(Vehicle, state.presets[Input.value]);
            commit('PRESET', `${Input.value} loaded into ${Vehicle.name}`, 'violet', 'Preset loaded');
            return;
        }
        const Bind = Input.dataset.bind;
        if (!Bind) return;
        const Scope = Input.dataset.scope;
        const Kind = Input.dataset.kind;
        const Selection = resolveSelection();
        const Target = Scope === 'tyre' && Selection.kind === 'tyre' ? Selection.tyre : activeVehicle();
        let Value;
        if (Kind === 'string') Value = Input.value.trim() || 'Untitled';
        else
        {
            Value = Number(Input.value);
            if (!Number.isFinite(Value)) return;
            const Minimum = Number(Input.min);
            if (Number.isFinite(Minimum)) Value = Math.max(Minimum, Value);
            Value = round(Value, 4);
        }
        Target[Bind] = Value;
        if (Scope === 'vehicle' && Bind === 'maxRpm')
        {
            for (const Key of ['torque', 'boost'])
            {
                const Curve = Target.curves[Key];
                if (!Curve) continue;
                Curve.xMax = Math.max(1000, Value);
                Curve.stepX = Math.max(50, Curve.xMax / 80);
                Curve.points.forEach(Point => { Point[0] = Math.min(Point[0], Curve.xMax); });
                Curve.points[Curve.points.length - 1][0] = Curve.xMax;
            }
        }
        commit('RECORD', `${Bind} set to ${Kind === 'string' ? Value : fmt(Value, 2)}${Input.dataset.unit ? ' ' + Input.dataset.unit : ''}`, 'blue');
    }

    function renderAll()
    {
        renderOutliner();
        renderComposition();
        renderInspector();
        renderLog();
        renderStatus();
    }

    document.addEventListener('click', Event =>
    {
        const ActionElement = Event.target.closest('[data-action]');
        if (ActionElement)
        {
            Event.preventDefault();
            Event.stopPropagation();
            handleAction(ActionElement.dataset.action, ActionElement);
            return;
        }
        const SelectElement = Event.target.closest('[data-select]');
        if (SelectElement)
        {
            Event.preventDefault();
            select(SelectElement.dataset.select);
        }
    });

    document.addEventListener('input', Event =>
    {
        const Input = Event.target;
        if (Input.id === 'OutlinerSearch')
        {
            const Caret = Input.selectionStart;
            state.search = Input.value;
            renderOutliner();
            const Replacement = $('OutlinerSearch');
            if (Replacement) { Replacement.focus(); Replacement.setSelectionRange(Caret, Caret); }
            return;
        }
        if (Input.matches('input[type="range"][data-bind]'))
        {
            const Output = Input.closest('.range-row').querySelector('output');
            if (Output) Output.textContent = `${fmt(Input.value, Number(Input.dataset.digits || 0))} ${Input.dataset.unit || ''}`.trim();
        }
    });

    document.addEventListener('change', handleChange);

    document.addEventListener('submit', Event =>
    {
        if (Event.target.id !== 'CurveForm') return;
        Event.preventDefault();
        const CurveValues = new FormData(Event.target);
        addCustomCurve(CurveValues);
        Event.target.reset();
        try { $('CurveDialog').close(); } catch { $('CurveDialog').removeAttribute('open'); }
    });

    document.addEventListener('keydown', Event =>
    {
        if ((Event.ctrlKey || Event.metaKey) && Event.key.toLowerCase() === 'f')
        {
            Event.preventDefault();
            const Search = $('OutlinerSearch');
            if (Search) Search.focus();
        }
        if (Event.key === 'Enter' && Event.target.matches('[data-select]')) { Event.preventDefault(); select(Event.target.dataset.select); }
        if (Event.key === ' ' && Event.target.matches('[data-select]')) { Event.preventDefault(); select(Event.target.dataset.select); }
    });

    renderAll();
})();
