/*==========================================================================================================================================
                                                          INSPECTORCONTROLS.JS
==========================================================================================================================================*/
// 🧩 Reusable inspector controls: glyphs, slider, switch and segmented rows, tiles, section cards, charts and inertial scrolling.
'use strict';

const InspectorControls = (() =>
{
    const SvgNamespace = 'http://www.w3.org/2000/svg';
    const Refreshers   = new Set();

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        GLYPHS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 24 × 24 stroke glyphs, drawn for this prototype (the editor's own icon atlas replaces them in C++).
    const GlyphPaths = {
        Monitor:   '<rect x="2.5" y="4" width="19" height="13" rx="2.5"/><path d="M8 21h8M12 17v4"/>',
        Zap:       '<path d="M13 2.5 4.5 14h6.5l-1 7.5L18.5 10H12z"/>',
        Sparkles:  '<path d="M11 3.5l1.7 4.6 4.6 1.7-4.6 1.7L11 16.1l-1.7-4.6-4.6-1.7 4.6-1.7z"/><path d="M18.5 14.5v5M16 17h5"/>',
        Aperture:  '<circle cx="12" cy="12" r="9"/><path d="M14.3 3.3 9.5 11.5M20.7 9.6H11.2M18.4 18.3l-4.8-8.2M9.7 20.7l4.8-8.2M3.3 14.4h9.5M5.6 5.7l4.8 8.2"/>',
        Sun:       '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M4.6 4.6l1.4 1.4M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4 6 18M18 6l1.4-1.4"/>',
        Layers:    '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 12.5l9 5 9-5"/><path d="M3 17l9 5 9-5"/>',
        Scan:      '<path d="M4 8V5.5A1.5 1.5 0 0 1 5.5 4H8M16 4h2.5A1.5 1.5 0 0 1 20 5.5V8M20 16v2.5a1.5 1.5 0 0 1-1.5 1.5H16M8 20H5.5A1.5 1.5 0 0 1 4 18.5V16"/><path d="M7.5 12h9"/>',
        Gauge:     '<path d="M3.6 17.5a9 9 0 1 1 16.8 0"/><path d="M12 13.5l4.2-4.2"/><circle cx="12" cy="13.5" r="1.2"/>',
        Boxes:     '<path d="M12 2.8l8 4.4v9.6l-8 4.4-8-4.4V7.2z"/><path d="M12 12l8-4.6M12 12v9.2M12 12 4 7.4"/>',
        Lattice:   '<rect x="3" y="3" width="18" height="18" rx="2.5"/><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/>',
        Bug:       '<rect x="7.5" y="7" width="9" height="13" rx="4.5"/><path d="M12 7V4.5M9.5 4l1.3 1.6M14.5 4l-1.3 1.6M7.5 12H3.5M20.5 12h-4M7.8 16.5 4.5 19M16.2 16.5l3.3 2.5M7.8 8.8 4.8 6.5M16.2 8.8l3-2.3"/>',
        Activity:  '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
        Chevron:   '<path d="M6 9l6 6 6-6"/>',
        Check:     '<path d="M5 12.5l4.2 4.2L19 7"/>',
        Alert:     '<path d="M12 3.5 2.5 20h19z"/><path d="M12 10v4.5M12 17.4v.2"/>',
        Eye:       '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
        EyeOff:    '<path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.4 0 10 7 10 7a17.6 17.6 0 0 1-2.6 3.5M6.2 6.3C3.6 8.1 2 12 2 12s3.6 7 10 7c1.7 0 3.2-.5 4.5-1.2"/>',
        Search:    '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.8-3.8"/>',
        Sliders:   '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
        Cloud:     '<path d="M7 18.5h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.1 9.6 4.5 4.5 0 0 0 7 18.5z"/>',
        Moon:      '<path d="M20 14.6A8.5 8.5 0 0 1 9.4 4a8.5 8.5 0 1 0 10.6 10.6z"/>',
        Star:      '<path d="M12 3.2l2.6 5.6 6.1.7-4.5 4.2 1.2 6.1L12 16.8l-5.4 3 1.2-6.1-4.5-4.2 6.1-.7z"/>',
        Planet:    '<circle cx="12" cy="12" r="5.2"/><path d="M5.3 15.9C3.1 18 2.6 19.6 3.6 20.3c1.7 1.2 7.2-1.2 12.2-5.4 4.6-3.9 6.8-7.9 5-8.9-.9-.5-2.4-.1-4.3.9"/>',
        Wind:      '<path d="M3 8.5h11a3 3 0 1 0-3-3M3 12.5h15.5a3 3 0 1 1-3 3M3 16.5h7"/>',
        Fog:       '<path d="M6 6.5h12M3.5 10.5h17M3 14.5h18M6 18.5h12"/>',
        Flare:     '<circle cx="8.5" cy="8.5" r="3.5"/><circle cx="15.5" cy="15.5" r="1.6"/><circle cx="19.5" cy="19.5" r="1"/><path d="M8.5 2.5v2M8.5 12.5v2M2.5 8.5h2M12.5 8.5h2"/>',
        Atmosphere:'<circle cx="12" cy="12" r="9"/><path d="M3.5 9.5c3 1.4 6.2 2 8.5 2s5.5-.6 8.5-2M3.6 14.8c2.9 1.2 5.8 1.8 8.4 1.8s5.5-.6 8.4-1.8"/>',
        Copy:      '<rect x="9" y="9" width="11.5" height="11.5" rx="2.5"/><path d="M5.5 15H5a1.5 1.5 0 0 1-1.5-1.5V5A1.5 1.5 0 0 1 5 3.5h8.5A1.5 1.5 0 0 1 15 5v.5"/>',
        Reset:     '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
        Plus:      '<path d="M12 5v14M5 12h14"/>',
        Close:     '<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>',
        Rays:      '<circle cx="5" cy="19" r="1.6"/><path d="M6.3 17.7 20 4M6.6 18.3l13.9-6.8M5.7 17.4l6.8-13.9"/>',
        Clock:     '<circle cx="12" cy="12" r="9"/><path d="M12 7.5V12l3 2"/>',
        Target:    '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r="0.6"/>',
        Folder:    '<path d="M3 7.5A2.5 2.5 0 0 1 5.5 5h3.6l2 2.2h7.4A2.5 2.5 0 0 1 21 9.7v7.8a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z"/>',
        Dot:       '<circle cx="12" cy="12" r="3"/>'
    };

    function ConstructGlyph(Name)
    {
        const Svg = document.createElementNS(SvgNamespace, 'svg');
        Svg.setAttribute('viewBox', '0 0 24 24');
        Svg.setAttribute('class', 'glyph');
        Svg.innerHTML = GlyphPaths[Name] || GlyphPaths.Dot;
        return Svg;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    ELEMENT ASSEMBLY
    //------------------------------------------------------------------------------------------------------------------------

    function Construct(Tag, ClassName, Content)
    {
        const Element = document.createElement(Tag);
        if (ClassName) Element.className = ClassName;
        if (Content === undefined || Content === null) return Element;
        if (typeof Content === 'string' || typeof Content === 'number') Element.textContent = String(Content);
        else if (Array.isArray(Content)) Content.forEach(Part => Part !== null && Part !== undefined && Element.append(Part));
        else Element.append(Content);
        return Element;
    }

    function ConstructSvg(Tag, Attributes, Text)
    {
        const Element = document.createElementNS(SvgNamespace, Tag);
        for (const [Key, Setting] of Object.entries(Attributes || {})) Element.setAttribute(Key, Setting);
        if (Text !== undefined) Element.textContent = Text;
        return Element;
    }

    // 📝 Every control registers one refresher; any write broadcasts to all of them, so dependent rows, readouts and charts
    //    follow one source of truth (the inspector's settings), as InspectorRuntime revisions do in C++.
    function Register(Refresher)
    {
        Refreshers.add(Refresher);
        Refresher();
        return Refresher;
    }

    function Broadcast()
    {
        Refreshers.forEach(Refresher => Refresher());
    }

    function ResolveAvailability(Binding)
    {
        return Binding.Availability ? Binding.Availability() : { Enabled: true, Reason: '' };
    }

    // 📝 A big readout: the whole part bright, the decimals dimmed, the unit small (the dashboard reference's "78.3 %").
    function ConstructMetric(Element, Quantity, Digits, Unit)
    {
        Element.textContent = '';
        const Text = Number(Quantity).toFixed(Digits);
        const Point = Text.indexOf('.');
        Element.append(document.createTextNode(Point < 0 ? Text : Text.slice(0, Point)));
        if (Point >= 0) Element.append(Construct('span', 'fraction', Text.slice(Point)));
        if (Unit) Element.append(Construct('span', 'unit', Unit));
    }

    function ConstructTags(Tags)
    {
        return (Tags || []).map(([Text, Tone]) => Construct('span', 'tag ' + (Tone || 'muted'), Text));
    }

    function ConstructRowLabel(Binding)
    {
        return Construct('div', 'row-label', [
            Construct('span', null, Binding.Label), ...ConstructTags(Binding.Tags), Construct('span', 'spacer'),
            Construct('span', 'row-reason', '')
        ]);
    }

    function RefreshAvailability(Row, Binding)
    {
        const Availability = ResolveAvailability(Binding);
        Row.classList.toggle('disabled', !Availability.Enabled);
        const Reason = Row.querySelector('.row-reason');
        if (Reason) Reason.textContent = Availability.Enabled ? '' : Availability.Reason;
        return Availability.Enabled;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                      CONTROL ROWS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Binding: { Label, Read(), Write(Quantity), Minimum, Maximum, Step, Unit, Digits, Labels, Tags, Availability() }.
    //    The Frontier Editor's field: a numeric pill with its unit beside a pill track with a white knob.
    function SliderRow(Parent, Binding)
    {
        const Digits = Binding.Digits ?? 2;
        const Row    = Construct('div', 'control-row');
        const Input  = Construct('input');
        Input.type = 'text';
        Input.inputMode = 'decimal';
        Input.setAttribute('aria-label', Binding.Label);
        const Pill  = Construct('div', 'number-pill', [Input, Binding.Unit ? Construct('span', 'unit', Binding.Unit) : null]);
        const Fill  = Construct('div', 'fill');
        const Knob  = Construct('div', 'knob');
        const Track = Construct('div', 'track', [Fill]);
        Track.tabIndex = 0;
        Track.setAttribute('role', 'slider');
        Track.setAttribute('aria-label', Binding.Label);
        const StepCount = Math.round((Binding.Maximum - Binding.Minimum) / Binding.Step);
        if (StepCount <= 12)
        {
            for (let Notch = 1; Notch < StepCount; ++Notch)
            {
                const Mark = Construct('div', 'notch');
                Mark.dataset.share = String(Notch / StepCount);
                Track.append(Mark);
            }
        }
        Track.append(Knob);
        Row.append(ConstructRowLabel(Binding), Construct('div', 'slider-line', [Pill, Track]));
        if (Binding.Labels) Row.append(Construct('div', 'end-labels', [Construct('span', null, Binding.Labels[0]), Construct('span', null, Binding.Labels[1])]));
        Parent.append(Row);

        const Quantize = Quantity =>
        {
            const Snapped = Binding.Minimum + Math.round((Quantity - Binding.Minimum) / Binding.Step) * Binding.Step;
            return Math.min(Binding.Maximum, Math.max(Binding.Minimum, Number(Snapped.toFixed(6))));
        };
        const Share = Quantity => (Quantity - Binding.Minimum) / (Binding.Maximum - Binding.Minimum);
        const PlaceKnob = Fraction =>
        {
            // 📝 The knob centre travels inside the track's end caps (18 px each), so it never leaves the pill.
            const Width  = Track.clientWidth || 200;
            const Centre = 18 + Fraction * Math.max(0, Width - 36);
            Knob.style.left  = Centre + 'px';
            Fill.style.width = (Centre + 18) + 'px';
            Track.querySelectorAll('.notch').forEach(Mark => { Mark.style.left = (18 + Number(Mark.dataset.share) * (Width - 36)) + 'px'; });
        };
        const WriteQuantity = Quantity =>
        {
            const Snapped = Quantize(Quantity);
            if (Snapped !== Binding.Read()) { Binding.Write(Snapped); Broadcast(); }
        };
        const FractionAt = ClientX =>
        {
            const Bounds = Track.getBoundingClientRect();
            return Math.min(1, Math.max(0, (ClientX - Bounds.left - 18) / Math.max(1, Bounds.width - 36)));
        };
        Track.addEventListener('pointerdown', Event =>
        {
            Track.setPointerCapture(Event.pointerId);
            Track.classList.add('dragging');
            WriteQuantity(Binding.Minimum + FractionAt(Event.clientX) * (Binding.Maximum - Binding.Minimum));
        });
        Track.addEventListener('pointermove', Event =>
        {
            if (!Track.classList.contains('dragging')) return;
            WriteQuantity(Binding.Minimum + FractionAt(Event.clientX) * (Binding.Maximum - Binding.Minimum));
        });
        const Release = () => Track.classList.remove('dragging');
        Track.addEventListener('pointerup', Release);
        Track.addEventListener('pointercancel', Release);
        Track.addEventListener('keydown', Event =>
        {
            const Steps = { ArrowLeft: -1, ArrowDown: -1, ArrowRight: 1, ArrowUp: 1, PageDown: -10, PageUp: 10 }[Event.key];
            if (Steps !== undefined) { WriteQuantity(Binding.Read() + Steps * Binding.Step); Event.preventDefault(); }
            else if (Event.key === 'Home') { WriteQuantity(Binding.Minimum); Event.preventDefault(); }
            else if (Event.key === 'End') { WriteQuantity(Binding.Maximum); Event.preventDefault(); }
        });
        const CommitInput = () =>
        {
            const Parsed = parseFloat(Input.value.replace(',', '.'));
            if (Number.isFinite(Parsed)) WriteQuantity(Parsed);
            Input.value = Number(Binding.Read()).toFixed(Digits);
        };
        Input.addEventListener('change', CommitInput);
        Input.addEventListener('keydown', Event => { if (Event.key === 'Enter') Input.blur(); });
        new ResizeObserver(() => PlaceKnob(Share(Binding.Read()))).observe(Track);

        Register(() =>
        {
            RefreshAvailability(Row, Binding);
            const Quantity = Binding.Read();
            PlaceKnob(Share(Quantity));
            Track.setAttribute('aria-valuenow', String(Quantity));
            if (document.activeElement !== Input) Input.value = Number(Quantity).toFixed(Digits);
        });
        return Row;
    }

    // 📝 Binding: { Label, Detail, Read(), Write(Enabled), Tags, Availability() }.
    function SwitchRow(Parent, Binding)
    {
        const Row    = Construct('div', 'control-row');
        const Switch = Construct('div', 'switch');
        Switch.setAttribute('role', 'switch');
        const Text = Construct('div', 'switch-text', [
            Construct('div', 'switch-label', [Construct('span', null, Binding.Label), ...ConstructTags(Binding.Tags)]),
            Binding.Detail ? Construct('div', 'switch-detail', Binding.Detail) : null,
            Construct('div', 'switch-detail row-reason', '')
        ]);
        const Line = Construct('div', 'switch-line', [Text, Switch]);
        Line.tabIndex = 0;
        Row.append(Line);
        Parent.append(Row);
        const Flip = () => { Binding.Write(!Binding.Read()); Broadcast(); };
        Line.addEventListener('click', Flip);
        Line.addEventListener('keydown', Event => { if (Event.key === ' ' || Event.key === 'Enter') { Flip(); Event.preventDefault(); } });
        Register(() =>
        {
            RefreshAvailability(Row, Binding);
            const Enabled = !!Binding.Read();
            Switch.classList.toggle('on', Enabled);
            Switch.setAttribute('aria-checked', String(Enabled));
        });
        return Row;
    }

    // 📝 Binding: { Label, Options: [[Text, Setting], …], Read(), Write(Setting), Tags, Availability() }.
    function SegmentedRow(Parent, Binding)
    {
        const Row     = Construct('div', 'control-row');
        const Group   = Construct('div', 'segmented');
        const Buttons = Binding.Options.map(([Text, Setting]) =>
        {
            const Button = Construct('button', null, Text);
            Button.type = 'button';
            Button.addEventListener('click', () => { Binding.Write(Setting); Broadcast(); });
            Group.append(Button);
            return [Button, Setting];
        });
        if (Binding.Label) Row.append(ConstructRowLabel(Binding));
        Row.append(Group);
        Parent.append(Row);
        Register(() =>
        {
            RefreshAvailability(Row, Binding);
            const Current = Binding.Read();
            Buttons.forEach(([Button, Setting]) => Button.classList.toggle('active', Setting === Current));
        });
        return Row;
    }

    // 📝 The dashboard reference's pill tabs: a raised pill marks the active choice.
    function PillTabs(Parent, Binding)
    {
        const Group   = Construct('div', 'pill-tabs');
        const Buttons = Binding.Options.map(([Text, Setting]) =>
        {
            const Button = Construct('button', 'pill-tab', Text);
            Button.type = 'button';
            Button.addEventListener('click', () => { Binding.Write(Setting); Broadcast(); });
            Group.append(Button);
            return [Button, Setting];
        });
        Parent.append(Group);
        Register(() =>
        {
            const Current = Binding.Read();
            Buttons.forEach(([Button, Setting]) => Button.classList.toggle('active', Setting === Current));
        });
        return Group;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                         TILES
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The Frontier Editor's property tile: icon chip, name, ON / OFF / UNAVAILABLE.
    function ToggleTile(Parent, Binding)
    {
        const Status = Construct('div', 'tile-status');
        const Tile   = Construct('div', 'toggle-tile', [
            Construct('div', 'tile-chip', ConstructGlyph(Binding.Glyph)), Construct('div', 'tile-name', Binding.Label), Status
        ]);
        Tile.tabIndex = 0;
        Parent.append(Tile);
        const Flip = () =>
        {
            if (!ResolveAvailability(Binding).Enabled) return;
            Binding.Write(!Binding.Read());
            Broadcast();
        };
        Tile.addEventListener('click', Flip);
        Tile.addEventListener('keydown', Event => { if (Event.key === ' ' || Event.key === 'Enter') { Flip(); Event.preventDefault(); } });
        Register(() =>
        {
            const Availability = ResolveAvailability(Binding);
            const Enabled      = Availability.Enabled && !!Binding.Read();
            Tile.classList.toggle('on', Enabled);
            Tile.classList.toggle('unavailable', !Availability.Enabled);
            Status.textContent = !Availability.Enabled ? 'UNAVAILABLE' : Enabled ? 'ON' : 'OFF';
            Tile.title = Availability.Enabled ? '' : Availability.Reason;
        });
        return Tile;
    }

    // 📝 Binding: { Label, Glyph, Resolve() → [Text, Small] }.
    function ReadoutTile(Parent, Binding)
    {
        const Reading = Construct('div', 'tile-value');
        const Tile    = Construct('div', 'readout-tile', [
            Binding.Glyph ? Construct('div', 'tile-glyph', ConstructGlyph(Binding.Glyph)) : null, Reading,
            Construct('div', 'tile-label', Binding.Label)
        ]);
        Parent.append(Tile);
        Register(() =>
        {
            const [Text, Small] = Binding.Resolve();
            Reading.textContent = Text;
            if (Small) Reading.append(Construct('small', null, Small));
        });
        return Tile;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     SECTION CARDS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Binding: { Id, Title, Glyph, Status() }. Returns the card and its inner column; the head folds the body.
    function SectionCard(Parent, Binding)
    {
        const Status = Construct('div', 'head-status');
        const Head   = Construct('div', 'section-head', [
            Construct('div', 'head-chip', ConstructGlyph(Binding.Glyph)), Construct('div', 'head-title', Binding.Title),
            Construct('div', 'spacer'), Status, Construct('div', 'chevron', ConstructGlyph('Chevron'))
        ]);
        const Inner = Construct('div', 'section-inner');
        const Card  = Construct('section', 'section-card', [Head, Construct('div', 'section-fold', Construct('div', 'section-body', Inner))]);
        Card.id = 'Section-' + Binding.Id;
        Head.addEventListener('click', () => Card.classList.toggle('folded'));
        Parent.append(Card);
        if (Binding.Status) Register(() => { Status.textContent = Binding.Status(); });
        return { Card, Inner };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                         CHARTS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The dashboard reference's chart: dashed guides, a dashed target, shaded windows above the target with white markers
    //    at their edges, a thin bright line. Binding: { Samples (array), Capacity, Target() [ms], Height }.
    function TimeSeries(Parent, Binding)
    {
        const Height = Binding.Height || 132;
        const Svg    = ConstructSvg('svg', { class: 'series', height: Height });
        Parent.append(Svg);
        const Defs = ConstructSvg('defs');
        const Gradient = ConstructSvg('linearGradient', { id: 'SeriesArea', x1: 0, y1: 0, x2: 0, y2: 1 });
        Gradient.append(ConstructSvg('stop', { offset: '0%', 'stop-color': '#ffffff', 'stop-opacity': 0.13 }),
                        ConstructSvg('stop', { offset: '100%', 'stop-color': '#ffffff', 'stop-opacity': 0 }));
        Defs.append(Gradient);
        const Refresh = () =>
        {
            const Width   = Math.max(120, Svg.clientWidth || Parent.clientWidth || 340);
            const Right   = Width - 34;
            const Bottom  = Height - 18;
            const Target  = Binding.Target();
            const Samples = Binding.Samples;
            const Peak    = Math.max(Target * 1.6, ...Samples) * 1.08;
            const AtY     = Milliseconds => Bottom - (Milliseconds / Peak) * (Bottom - 6);
            const AtX     = Index => (Index / Math.max(1, Binding.Capacity - 1)) * Right;
            Svg.textContent = '';
            Svg.append(Defs);
            for (const Share of [0.25, 0.5, 0.75, 1.0])
            {
                const Milliseconds = Peak * Share / 1.08;
                const Y = AtY(Milliseconds);
                Svg.append(ConstructSvg('line', { x1: 0, x2: Right, y1: Y, y2: Y, stroke: '#ffffff', 'stroke-opacity': 0.07, 'stroke-dasharray': '2 5' }),
                           ConstructSvg('text', { x: Width, y: Y + 3, 'text-anchor': 'end' }, Milliseconds.toFixed(0) + ' ms'));
            }
            const Offset = Binding.Capacity - Samples.length;
            // ① windows above the target
            let RunStart = -1;
            const Windows = [];
            Samples.forEach((Milliseconds, Index) =>
            {
                const Above = Milliseconds > Target;
                if (Above && RunStart < 0) RunStart = Index;
                if ((!Above || Index === Samples.length - 1) && RunStart >= 0) { Windows.push([RunStart, Above ? Index : Index - 1]); RunStart = -1; }
            });
            for (const [First, Last] of Windows)
            {
                const X0 = AtX(First + Offset), X1 = AtX(Last + Offset);
                Svg.append(ConstructSvg('rect', { x: X0, y: 6, width: Math.max(2, X1 - X0), height: Bottom - 6, fill: '#ffffff', 'fill-opacity': 0.045 }));
            }
            // ② the line and its area
            if (Samples.length > 1)
            {
                const Points = Samples.map((Milliseconds, Index) => [AtX(Index + Offset), AtY(Milliseconds)]);
                const Line   = Points.map(([X, Y], Index) => (Index ? 'L' : 'M') + X.toFixed(1) + ' ' + Y.toFixed(1)).join(' ');
                Svg.append(ConstructSvg('path', { d: Line + ` L${Points[Points.length - 1][0].toFixed(1)} ${Bottom} L${Points[0][0].toFixed(1)} ${Bottom} Z`,
                                                  fill: 'url(#SeriesArea)' }),
                           ConstructSvg('path', { d: Line, fill: 'none', stroke: '#dedede', 'stroke-width': 1.25, 'stroke-linejoin': 'round' }));
                for (const [First, Last] of Windows)
                    for (const Index of [First, Last])
                        Svg.append(ConstructSvg('circle', { cx: Points[Index][0], cy: Points[Index][1], r: 2.6, fill: '#ffffff' }));
                const [EndX, EndY] = Points[Points.length - 1];
                Svg.append(ConstructSvg('circle', { cx: EndX, cy: EndY, r: 3.4, fill: '#ffffff' }),
                           ConstructSvg('circle', { cx: EndX, cy: EndY, r: 7, fill: '#ffffff', 'fill-opacity': 0.12 }));
            }
            // ③ the target, dashed and labelled
            const TargetY = AtY(Target);
            Svg.append(ConstructSvg('line', { x1: 0, x2: Right, y1: TargetY, y2: TargetY, stroke: '#ffffff', 'stroke-opacity': 0.55,
                                              'stroke-dasharray': '7 6' }),
                       ConstructSvg('text', { x: 0, y: TargetY - 5, fill: '#9a9a9a' }, 'Target ' + Target.toFixed(1) + ' ms'));
            for (const [Text, Share] of [['−10 s', 0], ['−5 s', 0.5], ['now', 1]])
                Svg.append(ConstructSvg('text', { x: Share * Right, y: Height - 2, 'text-anchor': Share === 0 ? 'start' : Share === 1 ? 'end' : 'middle' }, Text));
        };
        new ResizeObserver(Refresh).observe(Svg);
        Register(Refresh);
        return { Refresh };
    }

    // 📝 The Frontier Editor's live parameter map: each column one setting normalised to its control range; drag a column or
    //    use ↑ / ↓. Binding: { Entries: [{ Label, Short, Read, Write, Minimum, Maximum, Step, Digits, Unit, Availability }] }.
    function ParameterMap(Parent, Binding)
    {
        const Height  = 150;
        const MapPanel = Construct('div', 'parameter-map');
        const Svg     = ConstructSvg('svg', { class: 'diagram', height: Height });
        const Caption = Construct('div', 'map-caption');
        MapPanel.append(Construct('div', 'map-head', [Construct('span', null, 'Live parameter map'), Construct('span', null, 'Arrow keys')]), Svg, Caption);
        MapPanel.tabIndex = 0;
        Parent.append(MapPanel);
        let Selected = 0;
        let Dragging = -1;
        const Geometry = () =>
        {
            const Width = Math.max(200, Svg.clientWidth || 320);
            const Left = 30, Right = Width - 4, Top = 8, Bottom = Height - 22;
            const Column = (Right - Left) / Binding.Entries.length;
            return { Width, Left, Right, Top, Bottom, Column };
        };
        const Share = Entry => (Entry.Read() - Entry.Minimum) / (Entry.Maximum - Entry.Minimum);
        const WriteShare = (Entry, Fraction) =>
        {
            const Raw     = Entry.Minimum + Math.min(1, Math.max(0, Fraction)) * (Entry.Maximum - Entry.Minimum);
            const Snapped = Math.min(Entry.Maximum, Math.max(Entry.Minimum, Entry.Minimum + Math.round((Raw - Entry.Minimum) / Entry.Step) * Entry.Step));
            if (Snapped !== Entry.Read()) { Entry.Write(Number(Snapped.toFixed(6))); Broadcast(); }
        };
        const Refresh = () =>
        {
            const Layout = Geometry();
            Svg.textContent = '';
            for (const [Text, Fraction] of [['100%', 1], ['50%', 0.5], ['0%', 0]])
            {
                const Y = Layout.Bottom - Fraction * (Layout.Bottom - Layout.Top);
                Svg.append(ConstructSvg('line', { x1: Layout.Left, x2: Layout.Right, y1: Y, y2: Y, stroke: '#ffffff', 'stroke-opacity': 0.08, 'stroke-dasharray': '2 5' }),
                           ConstructSvg('text', { x: 0, y: Y + 3, fill: '#5e5e5e', 'font-size': 9 }, Text));
            }
            Binding.Entries.forEach((Entry, Index) =>
            {
                const Enabled = !Entry.Availability || Entry.Availability().Enabled;
                const X = Layout.Left + Index * Layout.Column + Layout.Column * 0.18;
                const BarWidth = Layout.Column * 0.64;
                const Y = Layout.Bottom - Share(Entry) * (Layout.Bottom - Layout.Top);
                const Active = Index === Selected;
                Svg.append(ConstructSvg('rect', { x: X, y: Y, width: BarWidth, height: Math.max(1, Layout.Bottom - Y), rx: 3,
                                                  fill: Active ? 'rgba(159,180,214,0.24)' : 'rgba(255,255,255,0.07)', opacity: Enabled ? 1 : 0.35 }),
                           ConstructSvg('line', { x1: X, x2: X + BarWidth, y1: Y, y2: Y, stroke: Active ? '#9fb4d6' : '#8a8a8a', 'stroke-width': 1.4,
                                                  opacity: Enabled ? 1 : 0.35 }),
                           ConstructSvg('circle', { cx: X + BarWidth / 2, cy: Y, r: 3.6, fill: Active ? '#dfe7f3' : '#bdbdbd', opacity: Enabled ? 1 : 0.35 }),
                           ConstructSvg('text', { x: X + BarWidth / 2, y: Height - 6, 'text-anchor': 'middle', fill: Active ? '#d6d6d6' : '#6c6c6c',
                                                  'font-size': 9.5 }, Entry.Short || Entry.Label));
            });
            const Entry = Binding.Entries[Selected];
            Caption.textContent = '';
            Caption.append(Construct('b', null, `${Entry.Label} ${Number(Entry.Read()).toFixed(Entry.Digits ?? 0)}${Entry.Unit ? ' ' + Entry.Unit : ''}`),
                           document.createTextNode(' · authored values normalised to their control ranges; drag a column or use the arrow keys.'));
        };
        const ColumnAt = ClientX =>
        {
            const Layout = Geometry();
            const Bounds = Svg.getBoundingClientRect();
            return Math.min(Binding.Entries.length - 1, Math.max(0, Math.floor((ClientX - Bounds.left - Layout.Left) / Layout.Column)));
        };
        const FractionAt = ClientY =>
        {
            const Layout = Geometry();
            const Bounds = Svg.getBoundingClientRect();
            return (Layout.Bottom - (ClientY - Bounds.top)) / (Layout.Bottom - Layout.Top);
        };
        Svg.addEventListener('pointerdown', Event =>
        {
            Selected = ColumnAt(Event.clientX);
            const Entry = Binding.Entries[Selected];
            if (Entry.Availability && !Entry.Availability().Enabled) { Refresh(); return; }
            Dragging = Selected;
            Svg.setPointerCapture(Event.pointerId);
            WriteShare(Entry, FractionAt(Event.clientY));
            Refresh();
        });
        Svg.addEventListener('pointermove', Event => { if (Dragging >= 0) WriteShare(Binding.Entries[Dragging], FractionAt(Event.clientY)); });
        Svg.addEventListener('pointerup', () => { Dragging = -1; });
        MapPanel.addEventListener('keydown', Event =>
        {
            const Entry = Binding.Entries[Selected];
            if (Event.key === 'ArrowUp' || Event.key === 'ArrowDown')
            {
                const Next = Entry.Read() + (Event.key === 'ArrowUp' ? 1 : -1) * Entry.Step;
                WriteShare(Entry, (Next - Entry.Minimum) / (Entry.Maximum - Entry.Minimum));
                Event.preventDefault();
            }
            else if (Event.key === 'ArrowLeft' || Event.key === 'ArrowRight')
            {
                Selected = (Selected + (Event.key === 'ArrowRight' ? 1 : Binding.Entries.length - 1)) % Binding.Entries.length;
                Refresh();
                Event.preventDefault();
            }
        });
        new ResizeObserver(Refresh).observe(Svg);
        Register(Refresh);
        return MapPanel;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    INERTIAL SCROLLING
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The editor panels' scrolling (no visible scrollbar): the wheel moves a target, the view eases toward it, and past either
    //    end the content stretches a little and springs back once the wheel rests.
    function SmoothScroll(Scroller, Content)
    {
        const Elasticity = 90;   // [px] the farthest the target may run past an end
        let Target = Scroller.scrollTop, Current = Target, Running = false, LastWheel = 0;
        const Limit = () => Math.max(0, Scroller.scrollHeight - Scroller.clientHeight);
        const Advance = Now =>
        {
            const Maximum = Limit();
            Target = Now - LastWheel > 140 ? Math.min(Math.max(Target, 0), Maximum) : Math.min(Math.max(Target, -Elasticity), Maximum + Elasticity);
            Current += (Target - Current) * 0.17;
            if (Math.abs(Target - Current) < 0.25) Current = Target;
            const Clamped = Math.min(Math.max(Current, 0), Maximum);
            Scroller.scrollTop = Clamped;
            const Stretch = Current - Clamped;
            Content.style.transform = Stretch ? `translateY(${(-Stretch * 0.45).toFixed(2)}px)` : '';
            if (Current !== Target || Stretch !== 0) requestAnimationFrame(Advance);
            else Running = false;
        };
        const Start = () => { if (!Running) { Running = true; requestAnimationFrame(Advance); } };
        Scroller.addEventListener('wheel', Event =>
        {
            if (Event.ctrlKey || Math.abs(Event.deltaX) > Math.abs(Event.deltaY)) return;
            Event.preventDefault();
            if (!Running) Current = Target = Scroller.scrollTop;
            Target += Event.deltaMode === 1 ? Event.deltaY * 18 : Event.deltaY;
            LastWheel = performance.now();
            Start();
        }, { passive: false });
        Scroller.addEventListener('scroll', () => { if (!Running) Current = Target = Scroller.scrollTop; });
        return {
            ScrollTo(Position)
            {
                if (!Running) Current = Scroller.scrollTop;
                Target = Math.min(Math.max(Position, 0), Limit());
                LastWheel = 0;
                Start();
            }
        };
    }

    function Toast(Text)
    {
        let Element = document.querySelector('.toast');
        if (!Element) { Element = Construct('div', 'toast'); document.body.append(Element); }
        Element.textContent = Text;
        Element.classList.add('shown');
        clearTimeout(Toast.Timer);
        Toast.Timer = setTimeout(() => Element.classList.remove('shown'), 1800);
    }

    return {
        ConstructGlyph, Construct, ConstructSvg, Register, Broadcast, ConstructMetric, SliderRow, SwitchRow, SegmentedRow, PillTabs,
        ToggleTile, ReadoutTile, SectionCard, TimeSeries, ParameterMap, SmoothScroll, Toast
    };
})();
