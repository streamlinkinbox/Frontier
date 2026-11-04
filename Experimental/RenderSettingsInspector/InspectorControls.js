/*==========================================================================================================================================
                                                          INSPECTORCONTROLS.JS
==========================================================================================================================================*/
// 🧩 Reusable inspector controls: glyphs, rows (slider, switch, segmented, vector, colour, select), tiles, cards, plots and scrolling.
'use strict';

const InspectorControls = (() =>
{
    const SvgNamespace = 'http://www.w3.org/2000/svg';
    const Refreshers   = new Set();
    const ScopeStack   = [];

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
        Dot:       '<circle cx="12" cy="12" r="3"/>',
        // 📝 Force, particle and transport glyphs (Force Field and Particle editor prototypes).
        Tornado:   '<path d="M3 5h18M5.5 9h13M8 13h8.5M10 17h5M11.5 21h2"/>',
        Attract:   '<circle cx="12" cy="12" r="2.2"/><path d="M3 12h5M16 12h5M12 3v5M12 16v5M6 10l2 2-2 2M18 10l-2 2 2 2M10 6l2 2 2-2M10 18l2-2 2 2"/>',
        Repel:     '<circle cx="12" cy="12" r="2.2"/><path d="M2.5 12h5M16.5 12h5M12 2.5v5M12 16.5v5M4.5 10l-2 2 2 2M19.5 10l2 2-2 2M10 4.5l2-2 2 2M10 19.5l2 2 2-2"/>',
        Orbit:     '<circle cx="12" cy="12" r="2.4"/><ellipse cx="12" cy="12" rx="9.5" ry="4.4" transform="rotate(-24 12 12)"/>',
        Magnet:    '<path d="M5 3.5h4.5v8a2.5 2.5 0 0 0 5 0v-8H19v8a7 7 0 0 1-14 0z"/><path d="M5 7.5h4.5M14.5 7.5H19"/>',
        Thermal:   '<path d="M8 20V8M5 11l3-3 3 3M16 20V4.5M13 7.5l3-3 3 3"/>',
        Downburst: '<path d="M12 3v10M9 10l3 3 3-3"/><path d="M3 20c2.8-3.6 5.6-4.4 9-4.4s6.2.8 9 4.4"/>',
        Front:     '<path d="M14.5 3c-3 3-3 15 0 18"/><path d="M3 8h7.5M3 12h8M3 16h7.5"/><path d="M18 9l2.5 3-2.5 3"/>',
        Jet:       '<path d="M3 8.5h11l6 3.5-6 3.5H3"/><path d="M6.5 12h6"/>',
        Puff:      '<circle cx="9" cy="13" r="4.5"/><circle cx="16" cy="10.5" r="3.5"/><path d="M3 20h18"/>',
        Blast:     '<circle cx="12" cy="12" r="2"/><circle cx="12" cy="12" r="5.6" stroke-dasharray="2 2.6"/><circle cx="12" cy="12" r="9.2"/>',
        Wake:      '<path d="M2.5 15.5h3.5l2.2-4.5h6.6l3.2 4.5h3.5"/><circle cx="7.5" cy="16.8" r="1.7"/><circle cx="16.5" cy="16.8" r="1.7"/><path d="M3 8h4M2.5 11h2"/>',
        Current:   '<rect x="3" y="5" width="18" height="14" rx="2.5"/><path d="M6.5 10h9.5l-2-2M6.5 14h9.5l-2 2"/>',
        Lift:      '<path d="M12 18.5V5M8 9l4-4 4 4"/><path d="M5 20.5h14"/>',
        Gravity:   '<path d="M12 3.5V17M8 13l4 4 4-4"/><path d="M5 20.5h14"/>',
        Damp:      '<path d="M2.5 9c1.6-3.6 3.2-3.6 4.8 0s3.2 2.6 4.8 0 3-1.8 4.6 0 2.4 1.2 4.8 0"/><path d="M2.5 15.5h19"/>',
        Swirl:     '<path d="M12 12.2a2.3 2.3 0 1 1 2.3 2.3A4.8 4.8 0 1 1 12 7.4a7.2 7.2 0 1 1-7.2 7.2"/>',
        Play:      '<path d="M8 5.5v13l10.5-6.5z"/>',
        Pause:     '<path d="M8.5 5v14M15.5 5v14"/>',
        Library:   '<path d="M4 4.5h4v15H4zM10 4.5h4v15h-4z"/><path d="M15.8 5.8l3.4-.9 3.3 13.6-3.4.9z"/>',
        Trash:     '<path d="M4.5 7h15M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13"/>',
        Crosshair: '<circle cx="12" cy="12" r="7"/><path d="M12 2.5v5M12 16.5v5M2.5 12h5M16.5 12h5"/>',
        Map:       '<path d="M3 6.5l6-2.5 6 2.5 6-2.5v13.5l-6 2.5-6-2.5-6 2.5z"/><path d="M9 4v13.5M15 6.5V20"/>',
        Compass:   '<circle cx="12" cy="12" r="9"/><path d="M15.5 8.5l-2 5-5 2 2-5z"/>',
        Leaf:      '<path d="M5 19c0-8 5-14 15-14 0 10-6 15-14 15"/><path d="M5 19l8-8"/>',
        Atom:      '<circle cx="12" cy="12" r="1.5"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8" transform="rotate(60 12 12)"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8" transform="rotate(120 12 12)"/>',
        Flame:     '<path d="M12 21a6.5 6.5 0 0 0 6.5-6.5c0-4.5-4-6.5-4.5-11-3 2-5 4.5-5 7.5-1.2-.7-1.8-2-2-3.5-1.5 1.8-1.5 4.5-1.5 7A6.5 6.5 0 0 0 12 21z"/>',
        Drop:      '<path d="M12 3s6.5 7 6.5 11.5a6.5 6.5 0 0 1-13 0C5.5 10 12 3 12 3z"/>',
        Ruler:     '<path d="M3 15.5 15.5 3 21 8.5 8.5 21z"/><path d="M7 11.5l2 2M10 8.5l1.5 1.5M13 5.5l2 2"/>',
        Timeline:  '<path d="M3 12h18"/><circle cx="7" cy="12" r="2"/><circle cx="15.5" cy="12" r="2"/><path d="M7 5v3M15.5 16v3"/>',
        Move:      '<path d="M12 3v18M3 12h18M9.5 5.5 12 3l2.5 2.5M9.5 18.5 12 21l2.5-2.5M5.5 9.5 3 12l2.5 2.5M18.5 9.5 21 12l-2.5 2.5"/>',
        Globe:     '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18"/>',
        Palette:   '<path d="M12 3a9 9 0 0 0 0 18c1.4 0 2-1 2-2 0-1.4-1.2-1.6-1.2-2.8 0-1 .8-1.7 1.8-1.7H17a4 4 0 0 0 4-4C21 6.6 17 3 12 3z"/><circle cx="7.5" cy="11" r="1.1"/><circle cx="10" cy="7" r="1.1"/><circle cx="14.5" cy="7" r="1.1"/>'
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
    //    A refresher registered while a scope is open also joins that scope, so a page that is rebuilt can release exactly
    //    the refreshers it registered (pages built without a scope keep the original behaviour: they stay registered).
    function Register(Refresher)
    {
        Refreshers.add(Refresher);
        if (ScopeStack.length) ScopeStack[ScopeStack.length - 1].add(Refresher);
        Refresher();
        return Refresher;
    }

    function OpenScope()
    {
        const Scope = new Set();
        ScopeStack.push(Scope);
        return Scope;
    }

    function CloseScope(Scope)
    {
        const Position = ScopeStack.lastIndexOf(Scope);
        if (Position >= 0) ScopeStack.splice(Position, 1);
    }

    // 📝 Releases every refresher the scope collected; later Broadcasts no longer reach the controls of a discarded page.
    function ReleaseScope(Scope)
    {
        if (!Scope) return;
        CloseScope(Scope);
        Scope.forEach(Refresher => Refreshers.delete(Refresher));
        Scope.clear();
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
    //    Optional: Scale 'Logarithmic' (the knob travels in log space, readings keep three significant figures),
    //    Format(Quantity) → [Text, Unit] and Parse(Text, Unit) → Quantity for readings shown in changing units (m / km).
    function SliderRow(Parent, Binding)
    {
        const Digits = Binding.Digits ?? 2;
        const Logarithmic = Binding.Scale === 'Logarithmic' && Binding.Minimum > 0;
        const Row    = Construct('div', 'control-row');
        const Input  = Construct('input');
        Input.type = 'text';
        Input.inputMode = 'decimal';
        Input.setAttribute('aria-label', Binding.Label);
        const UnitLabel = (Binding.Unit || Binding.Format) ? Construct('span', 'unit', Binding.Unit || '') : null;
        const Pill  = Construct('div', 'number-pill', [Input, UnitLabel]);
        const Fill  = Construct('div', 'fill');
        const Knob  = Construct('div', 'knob');
        const Track = Construct('div', 'track', [Fill]);
        Track.tabIndex = 0;
        Track.setAttribute('role', 'slider');
        Track.setAttribute('aria-label', Binding.Label);
        const StepCount = Logarithmic ? Infinity : Math.round((Binding.Maximum - Binding.Minimum) / Binding.Step);
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
            if (Logarithmic) return Math.min(Binding.Maximum, Math.max(Binding.Minimum, Number(Number(Quantity).toPrecision(3))));
            const Snapped = Binding.Minimum + Math.round((Quantity - Binding.Minimum) / Binding.Step) * Binding.Step;
            return Math.min(Binding.Maximum, Math.max(Binding.Minimum, Number(Snapped.toFixed(6))));
        };
        const Share = Quantity => Logarithmic
            ? Math.log(Math.max(Binding.Minimum, Quantity) / Binding.Minimum) / Math.log(Binding.Maximum / Binding.Minimum)
            : (Quantity - Binding.Minimum) / (Binding.Maximum - Binding.Minimum);
        const FromShare = Fraction => Logarithmic
            ? Binding.Minimum * Math.pow(Binding.Maximum / Binding.Minimum, Fraction)
            : Binding.Minimum + Fraction * (Binding.Maximum - Binding.Minimum);
        const Display = Quantity =>
        {
            if (Binding.Format) return Binding.Format(Quantity);
            return [Number(Quantity).toFixed(Digits), Binding.Unit || ''];
        };
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
            WriteQuantity(FromShare(FractionAt(Event.clientX)));
        });
        Track.addEventListener('pointermove', Event =>
        {
            if (!Track.classList.contains('dragging')) return;
            WriteQuantity(FromShare(FractionAt(Event.clientX)));
        });
        const Release = () => Track.classList.remove('dragging');
        Track.addEventListener('pointerup', Release);
        Track.addEventListener('pointercancel', Release);
        Track.addEventListener('keydown', Event =>
        {
            const Steps = { ArrowLeft: -1, ArrowDown: -1, ArrowRight: 1, ArrowUp: 1, PageDown: -10, PageUp: 10 }[Event.key];
            if (Steps !== undefined && Logarithmic) { WriteQuantity(FromShare(Share(Binding.Read()) + Steps / 100)); Event.preventDefault(); }
            else if (Steps !== undefined) { WriteQuantity(Binding.Read() + Steps * Binding.Step); Event.preventDefault(); }
            else if (Event.key === 'Home') { WriteQuantity(Binding.Minimum); Event.preventDefault(); }
            else if (Event.key === 'End') { WriteQuantity(Binding.Maximum); Event.preventDefault(); }
        });
        const ResolveInput = () =>
        {
            const Parsed = Binding.Parse ? Binding.Parse(Input.value, UnitLabel ? UnitLabel.textContent : '')
                                         : parseFloat(Input.value.replace(',', '.'));
            if (Number.isFinite(Parsed)) WriteQuantity(Parsed);
            const [Text, Unit] = Display(Binding.Read());
            Input.value = Text;
            if (UnitLabel) UnitLabel.textContent = Unit;
        };
        Input.addEventListener('change', ResolveInput);
        Input.addEventListener('keydown', Event => { if (Event.key === 'Enter') Input.blur(); });
        new ResizeObserver(() => PlaceKnob(Share(Binding.Read()))).observe(Track);

        Register(() =>
        {
            RefreshAvailability(Row, Binding);
            const Quantity = Binding.Read();
            PlaceKnob(Share(Quantity));
            Track.setAttribute('aria-valuenow', String(Quantity));
            const [Text, Unit] = Display(Quantity);
            if (document.activeElement !== Input) Input.value = Text;
            if (UnitLabel) UnitLabel.textContent = Unit;
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
    //    Optional: Unit ('ms'), TargetLabel ('Target') — either may be a function —, TargetDigits (1), AxisDigits (0), Headroom (1.6),
    //    TimeLabels.
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
            const Unit    = typeof Binding.Unit === 'function' ? Binding.Unit() : (Binding.Unit ?? 'ms');
            const Suffix  = Unit ? ' ' + Unit : '';
            const Peak    = (Math.max(Target * (Binding.Headroom ?? 1.6), ...Samples) * 1.08) || 1;
            const AtY     = Milliseconds => Bottom - (Milliseconds / Peak) * (Bottom - 6);
            const AtX     = Index => (Index / Math.max(1, Binding.Capacity - 1)) * Right;
            Svg.textContent = '';
            Svg.append(Defs);
            for (const Share of [0.25, 0.5, 0.75, 1.0])
            {
                const Milliseconds = Peak * Share / 1.08;
                const Y = AtY(Milliseconds);
                Svg.append(ConstructSvg('line', { x1: 0, x2: Right, y1: Y, y2: Y, stroke: '#ffffff', 'stroke-opacity': 0.07, 'stroke-dasharray': '2 5' }),
                           ConstructSvg('text', { x: Width, y: Y + 3, 'text-anchor': 'end' }, Milliseconds.toFixed(Binding.AxisDigits ?? 0) + Suffix));
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
                       ConstructSvg('text', { x: 0, y: TargetY - 5, fill: '#9a9a9a' },
                                     (typeof Binding.TargetLabel === 'function' ? Binding.TargetLabel() : (Binding.TargetLabel ?? 'Target')) + ' '
                                     + Target.toFixed(Binding.TargetDigits ?? 1) + Suffix));
            const TimeLabels = Binding.TimeLabels || ['−10 s', '−5 s', 'now'];
            for (const [Text, Share] of [[TimeLabels[0], 0], [TimeLabels[1], 0.5], [TimeLabels[2], 1]])
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
    //                                              SELECT, TEXT, VECTOR AND COLOUR ROWS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Specification: { Label, Options: [[Text, Setting], …], Read(), Write(Setting), Tags, Availability() }.
    //    A pill-shaped native list for choices too many for a segmented row.
    function SelectRow(Host, Specification)
    {
        const Row    = Construct('div', 'control-row');
        const Select = Construct('select', 'select-pill');
        Select.setAttribute('aria-label', Specification.Label || 'Choice');
        const Encoded = new Map();
        Specification.Options.forEach(([Text, Setting], Position) =>
        {
            const Choice = Construct('option', null, Text);
            Choice.value = String(Position);
            Encoded.set(String(Position), Setting);
            Select.append(Choice);
        });
        Select.addEventListener('change', () => { Specification.Write(Encoded.get(Select.value)); Broadcast(); });
        if (Specification.Label) Row.append(ConstructRowLabel(Specification));
        Row.append(Construct('div', 'select-line', [Select, Construct('span', 'select-chevron', ConstructGlyph('Chevron'))]));
        Host.append(Row);
        Register(() =>
        {
            RefreshAvailability(Row, Specification);
            const Current = Specification.Read();
            const Position = Specification.Options.findIndex(([, Setting]) => Setting === Current);
            if (Position >= 0 && Select.value !== String(Position)) Select.value = String(Position);
        });
        return Row;
    }

    // 📝 Specification: { Label, Read(), Write(Text), Placeholder, Tags, Availability() }. Writes on every keystroke.
    function TextRow(Host, Specification)
    {
        const Row   = Construct('div', 'control-row');
        const Input = Construct('input', 'text-pill');
        Input.type = 'text';
        Input.spellcheck = false;
        Input.placeholder = Specification.Placeholder || '';
        Input.setAttribute('aria-label', Specification.Label || 'Text');
        Input.addEventListener('input', () => { Specification.Write(Input.value); Broadcast(); });
        Input.addEventListener('keydown', Event => { if (Event.key === 'Enter') Input.blur(); });
        if (Specification.Label) Row.append(ConstructRowLabel(Specification));
        Row.append(Input);
        Host.append(Row);
        Register(() =>
        {
            RefreshAvailability(Row, Specification);
            if (document.activeElement !== Input) Input.value = Specification.Read();
        });
        return Row;
    }

    // 📝 Specification: { Label, Read() → [X, Y, Z], Write(Vector), Step [per px], Digits, Unit, Axes, Minimum, Maximum, Tags,
    //    Availability() }. Three axis pills in the Project Zero editor's axis colours; drag an axis letter to scrub
    //    (Shift for fine steps) or type a reading.
    function VectorRow(Host, Specification)
    {
        const Axes   = Specification.Axes || ['X', 'Y', 'Z'];
        const Digits = Specification.Digits ?? 2;
        const Row    = Construct('div', 'control-row');
        const Line   = Construct('div', 'vector-line');
        const Clamp  = Reading => Math.min(Specification.Maximum ?? Infinity, Math.max(Specification.Minimum ?? -Infinity, Reading));
        const Inputs = Axes.map((Axis, Position) =>
        {
            const Letter = Construct('span', 'axis-letter', Axis);
            const Input  = Construct('input');
            Input.type = 'text';
            Input.inputMode = 'decimal';
            Input.setAttribute('aria-label', `${Specification.Label} ${Axis}`);
            Line.append(Construct('label', 'axis-pill axis-' + Position, [Letter, Input]));
            const WriteAxis = Reading =>
            {
                const Vector = Specification.Read().slice();
                const Next   = Number(Clamp(Reading).toFixed(6));
                if (Vector[Position] === Next) return;
                Vector[Position] = Next;
                Specification.Write(Vector);
                Broadcast();
            };
            let Origin = null;
            Letter.addEventListener('pointerdown', Event =>
            {
                Letter.setPointerCapture(Event.pointerId);
                Origin = { X: Event.clientX, Reading: Specification.Read()[Position] };
                Event.preventDefault();
            });
            Letter.addEventListener('pointermove', Event =>
            {
                if (!Origin) return;
                WriteAxis(Origin.Reading + (Event.clientX - Origin.X) * (Specification.Step ?? 0.01) * (Event.shiftKey ? 0.1 : 1));
            });
            const Release = () => { Origin = null; };
            Letter.addEventListener('pointerup', Release);
            Letter.addEventListener('pointercancel', Release);
            Input.addEventListener('change', () =>
            {
                const Parsed = parseFloat(Input.value.replace(',', '.'));
                if (Number.isFinite(Parsed)) WriteAxis(Parsed);
                Input.value = Number(Specification.Read()[Position]).toFixed(Digits);
            });
            Input.addEventListener('keydown', Event => { if (Event.key === 'Enter') Input.blur(); });
            return Input;
        });
        if (Specification.Unit) Line.append(Construct('span', 'vector-unit', Specification.Unit));
        Row.append(ConstructRowLabel(Specification), Line);
        Host.append(Row);
        Register(() =>
        {
            RefreshAvailability(Row, Specification);
            const Vector = Specification.Read();
            Inputs.forEach((Input, Position) => { if (document.activeElement !== Input) Input.value = Number(Vector[Position]).toFixed(Digits); });
        });
        return Row;
    }

    const LinearToDisplay = Channel => Channel <= 0.0031308 ? 12.92 * Channel : 1.055 * Math.pow(Channel, 1 / 2.4) - 0.055;
    const DisplayToLinear = Channel => Channel <= 0.04045 ? Channel / 12.92 : Math.pow((Channel + 0.055) / 1.055, 2.4);

    // 📝 Specification: { Label, Read() → [R, G, B, A?], Write(Colour), Linear (default true), Alpha, Tags, Availability() }.
    //    Linear colours (the particle engine's) are shown and typed as display hex; readings above 1 keep their intensity.
    function ColourRow(Host, Specification)
    {
        const Linear  = Specification.Linear !== false;
        const Row     = Construct('div', 'control-row');
        const Picker  = Construct('input');
        Picker.type = 'color';
        Picker.setAttribute('aria-label', Specification.Label + ' colour');
        const Swatch  = Construct('label', 'colour-swatch', [Picker]);
        const Hex     = Construct('input');
        Hex.type = 'text';
        Hex.spellcheck = false;
        Hex.setAttribute('aria-label', Specification.Label + ' hex');
        const Intensity = Construct('span', 'unit', '');
        const Line    = Construct('div', 'colour-line', [Swatch, Construct('div', 'number-pill colour-hex', [Hex, Intensity])]);
        let AlphaInput = null;
        if (Specification.Alpha)
        {
            AlphaInput = Construct('input');
            AlphaInput.type = 'text';
            AlphaInput.inputMode = 'decimal';
            AlphaInput.setAttribute('aria-label', Specification.Label + ' alpha');
            Line.append(Construct('div', 'number-pill colour-alpha', [Construct('span', 'alpha-letter', 'α'), AlphaInput]));
        }
        Row.append(ConstructRowLabel(Specification), Line);
        Host.append(Row);
        const Gain = Colour => Math.max(1, Colour[0], Colour[1], Colour[2]);
        const EncodeHex = Colour =>
        {
            const Scale = Gain(Colour);
            return '#' + [0, 1, 2].map(Channel =>
            {
                const Share = Math.min(1, Math.max(0, Colour[Channel] / Scale));
                return Math.round((Linear ? LinearToDisplay(Share) : Share) * 255).toString(16).padStart(2, '0');
            }).join('');
        };
        const WriteHex = Text =>
        {
            const Match = /^#?([0-9a-f]{6})$/i.exec(Text.trim());
            if (!Match) return false;
            const Current = Specification.Read();
            const Scale   = Gain(Current);
            const Next    = Current.slice();
            for (let Channel = 0; Channel < 3; ++Channel)
            {
                const Share = parseInt(Match[1].slice(Channel * 2, Channel * 2 + 2), 16) / 255;
                Next[Channel] = Number(((Linear ? DisplayToLinear(Share) : Share) * Scale).toFixed(5));
            }
            Specification.Write(Next);
            Broadcast();
            return true;
        };
        Picker.addEventListener('input', () => WriteHex(Picker.value));
        Hex.addEventListener('change', () => { WriteHex(Hex.value); Hex.value = EncodeHex(Specification.Read()); });
        Hex.addEventListener('keydown', Event => { if (Event.key === 'Enter') Hex.blur(); });
        if (AlphaInput)
        {
            AlphaInput.addEventListener('change', () =>
            {
                const Parsed = parseFloat(AlphaInput.value.replace(',', '.'));
                if (Number.isFinite(Parsed))
                {
                    const Next = Specification.Read().slice();
                    Next[3] = Math.min(1, Math.max(0, Parsed));
                    Specification.Write(Next);
                    Broadcast();
                }
                AlphaInput.value = Number(Specification.Read()[3] ?? 1).toFixed(2);
            });
            AlphaInput.addEventListener('keydown', Event => { if (Event.key === 'Enter') AlphaInput.blur(); });
        }
        Register(() =>
        {
            RefreshAvailability(Row, Specification);
            const Colour = Specification.Read();
            const Encoded = EncodeHex(Colour);
            Swatch.style.background = Encoded;
            if (document.activeElement !== Picker) Picker.value = Encoded;
            if (document.activeElement !== Hex) Hex.value = Encoded;
            const Scale = Gain(Colour);
            Intensity.textContent = Scale > 1.001 ? '×' + Scale.toFixed(1) : '';
            Intensity.style.display = Scale > 1.001 ? '' : 'none';
            if (AlphaInput && document.activeElement !== AlphaInput) AlphaInput.value = Number(Colour[3] ?? 1).toFixed(2);
        });
        return Row;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                               CHIPS, ACTIONS AND STATISTICS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Specification: { Label, Options: [[Text, Key], …], Read() → [Key, …], Write([Key, …]), Detail(Keys) → caption, Tags }.
    //    Toggle chips for membership lists such as the channels a field acts on.
    function ChipToggles(Host, Specification)
    {
        const Row   = Construct('div', 'control-row');
        const Group = Construct('div', 'chip-toggles');
        const Caption = Construct('div', 'chip-caption');
        const Chips = Specification.Options.map(([Text, Key]) =>
        {
            const Chip = Construct('button', 'chip-toggle', Text);
            Chip.type = 'button';
            Chip.addEventListener('click', () =>
            {
                const Current = Specification.Read().slice();
                const Position = Current.indexOf(Key);
                if (Position >= 0) Current.splice(Position, 1);
                else Current.push(Key);
                Specification.Write(Current);
                Broadcast();
            });
            Group.append(Chip);
            return [Chip, Key];
        });
        if (Specification.Label) Row.append(ConstructRowLabel(Specification));
        Row.append(Group, Caption);
        Host.append(Row);
        Register(() =>
        {
            RefreshAvailability(Row, Specification);
            const Current = Specification.Read();
            Chips.forEach(([Chip, Key]) => Chip.classList.toggle('on', Current.includes(Key)));
            Caption.textContent = Specification.Detail ? Specification.Detail(Current) : '';
        });
        return Row;
    }

    // 📝 Actions: [{ Label, Glyph, Tone ('accent' | 'danger'), Act(Event), Availability() }]. A row of pill buttons.
    function ActionRow(Host, Actions)
    {
        const Row = Construct('div', 'action-row');
        for (const Action of Actions)
        {
            const Button = Construct('button', 'pill-button' + (Action.Tone ? ' ' + Action.Tone : ''),
                                     [Action.Glyph ? ConstructGlyph(Action.Glyph) : null, Construct('span', null, Action.Label)]);
            Button.type = 'button';
            Button.addEventListener('click', Event =>
            {
                if (Action.Availability && !Action.Availability().Enabled) return;
                Action.Act(Event);
                Broadcast();
            });
            if (Action.Availability)
            {
                Register(() =>
                {
                    const Availability = Action.Availability();
                    Button.classList.toggle('unavailable', !Availability.Enabled);
                    Button.title = Availability.Enabled ? '' : (Availability.Reason || '');
                });
            }
            Row.append(Button);
        }
        Host.append(Row);
        return Row;
    }

    // 📝 Cells: [[Label, Resolve() → Text], …]. The small three-column statistics grid.
    function StatGrid(Host, Cells)
    {
        const Grid = Construct('div', 'stat-grid');
        for (const [Label, Resolve] of Cells)
        {
            const Reading = Construct('b');
            Grid.append(Construct('div', 'stat-cell', [Reading, Construct('span', null, Label)]));
            Register(() => { Reading.textContent = typeof Resolve === 'function' ? Resolve() : Resolve; });
        }
        Host.append(Grid);
        return Grid;
    }

    // 📝 Chips: [Resolve() → [Text, Small], …]. The hero's row of round count chips.
    function ChipRow(Host, Chips)
    {
        const Row = Construct('div', 'chip-row');
        for (const Resolve of Chips)
        {
            const Chip = Construct('div', 'count-chip');
            Register(() =>
            {
                const [Text, Small] = Resolve();
                Chip.textContent = '';
                Chip.append(Construct('span', 'tabular', Text), Construct('small', null, Small));
            });
            Row.append(Chip);
        }
        Host.append(Row);
        return Row;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                 CURVE PLOT AND POLAR DIAL
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 A generic plot in the dashboard style. Specification: { Height, Resolve() → { XRange, YRange,
    //    Curves: [{ Points: [[X, Y], …], Stroke, Fill, Dashed, Width }], Markers: [{ X, Y, Label, Anchor }],
    //    XTicks: [[X, Text]], YTicks: [[Y, Text]], Guides: [{ X | Y, Text }], Caption } }.
    function CurvePlot(Host, Specification)
    {
        const Height  = Specification.Height || 132;
        const Panel   = Construct('div', 'diagram-panel curve-plot');
        const Svg     = ConstructSvg('svg', { class: 'diagram', height: Height });
        const Caption = Construct('div', 'plot-caption');
        Panel.append(Svg, Caption);
        Host.append(Panel);
        const Refresh = () =>
        {
            const Plot   = Specification.Resolve();
            const Width  = Math.max(160, Svg.clientWidth || Host.clientWidth || 340);
            const Left   = Plot.YTicks && Plot.YTicks.length ? 34 : 4;
            const Right  = Width - 6;
            const Top    = 8;
            const Bottom = Height - (Plot.XTicks && Plot.XTicks.length ? 18 : 6);
            const [X0, X1] = Plot.XRange;
            const [Y0, Y1] = Plot.YRange;
            const AtX = X => Left + (X - X0) / ((X1 - X0) || 1) * (Right - Left);
            const AtY = Y => Bottom - (Y - Y0) / ((Y1 - Y0) || 1) * (Bottom - Top);
            Svg.textContent = '';
            for (const [Y, Text] of Plot.YTicks || [])
            {
                Svg.append(ConstructSvg('line', { x1: Left, x2: Right, y1: AtY(Y), y2: AtY(Y), stroke: '#ffffff', 'stroke-opacity': 0.07, 'stroke-dasharray': '2 5' }),
                           ConstructSvg('text', { x: Left - 6, y: AtY(Y) + 3, 'text-anchor': 'end', fill: '#6a6a6a', 'font-size': 9.5 }, Text));
            }
            for (const [X, Text] of Plot.XTicks || [])
            {
                Svg.append(ConstructSvg('line', { x1: AtX(X), x2: AtX(X), y1: Top, y2: Bottom, stroke: '#ffffff', 'stroke-opacity': 0.05, 'stroke-dasharray': '2 5' }),
                           ConstructSvg('text', { x: AtX(X), y: Height - 3, 'text-anchor': 'middle', fill: '#6a6a6a', 'font-size': 9.5 }, Text));
            }
            for (const Guide of Plot.Guides || [])
            {
                if (Guide.Y !== undefined)
                {
                    Svg.append(ConstructSvg('line', { x1: Left, x2: Right, y1: AtY(Guide.Y), y2: AtY(Guide.Y), stroke: '#ffffff', 'stroke-opacity': 0.5, 'stroke-dasharray': '7 6' }));
                    if (Guide.Text) Svg.append(ConstructSvg('text', { x: Left + 2, y: AtY(Guide.Y) - 5, fill: '#9a9a9a', 'font-size': 9.5 }, Guide.Text));
                }
                else
                {
                    Svg.append(ConstructSvg('line', { x1: AtX(Guide.X), x2: AtX(Guide.X), y1: Top, y2: Bottom, stroke: '#ffffff', 'stroke-opacity': 0.4, 'stroke-dasharray': '3 4' }));
                    if (Guide.Text) Svg.append(ConstructSvg('text', { x: AtX(Guide.X) + 4, y: Top + 9, fill: '#9a9a9a', 'font-size': 9.5 }, Guide.Text));
                }
            }
            for (const Curve of Plot.Curves || [])
            {
                if (!Curve.Points || Curve.Points.length < 2) continue;
                const Path = Curve.Points.map(([X, Y], Position) => (Position ? 'L' : 'M') + AtX(X).toFixed(1) + ' ' + AtY(Y).toFixed(1)).join(' ');
                if (Curve.Fill)
                {
                    const First = Curve.Points[0], Last = Curve.Points[Curve.Points.length - 1];
                    const BaselineY = AtY(Math.max(Y0, Math.min(Y1, 0)));
                    Svg.append(ConstructSvg('path', { d: `${Path} L${AtX(Last[0]).toFixed(1)} ${BaselineY} L${AtX(First[0]).toFixed(1)} ${BaselineY} Z`, fill: Curve.Fill }));
                }
                Svg.append(ConstructSvg('path', { d: Path, fill: 'none', stroke: Curve.Stroke || '#dedede', 'stroke-width': Curve.Width || 1.4,
                                                  'stroke-linejoin': 'round', 'stroke-dasharray': Curve.Dashed ? '5 5' : '' }));
            }
            for (const Marker of Plot.Markers || [])
            {
                Svg.append(ConstructSvg('circle', { cx: AtX(Marker.X), cy: AtY(Marker.Y), r: Marker.Radius || 3.2, fill: Marker.Fill || '#ffffff' }));
                if (Marker.Label)
                {
                    const Anchor = Marker.Anchor || (AtX(Marker.X) > Width * 0.7 ? 'end' : 'start');
                    Svg.append(ConstructSvg('text', { x: AtX(Marker.X) + (Anchor === 'end' ? -6 : 6), y: AtY(Marker.Y) - 6, 'text-anchor': Anchor,
                                                      fill: '#cfcfcf', 'font-size': 10 }, Marker.Label));
                }
            }
            Caption.textContent = Plot.Caption || '';
            Caption.style.display = Plot.Caption ? '' : 'none';
        };
        new ResizeObserver(Refresh).observe(Svg);
        Register(Refresh);
        return { Panel, Refresh };
    }

    const CompassPoints = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
    const ResolveCompassPoint = Degrees => CompassPoints[Math.round((((Degrees % 360) + 360) % 360) / 22.5) % 16];

    // 📝 Specification: { Label, Read() → bearing [°, clockwise from north], Write(Degrees), Resolve() → { Text, Caption },
    //    Size [px] }. A compass dial: drag the knob (or use the arrow keys) to set the bearing the flow travels toward.
    function PolarDial(Host, Specification)
    {
        const Size   = Specification.Size || 168;
        const Panel  = Construct('div', 'diagram-panel polar-dial');
        const Svg    = ConstructSvg('svg', { class: 'diagram', height: Size, viewBox: `0 0 ${Size} ${Size}` });
        const Side   = Construct('div', 'dial-side');
        Panel.append(Svg, Side);
        Panel.tabIndex = 0;
        Host.append(Panel);
        const Centre = Size / 2, Radius = Size / 2 - 16;
        const Refresh = () =>
        {
            const Bearing = Specification.Read();
            const Turn    = Bearing * Math.PI / 180;
            const Tip     = [Centre + Math.sin(Turn) * Radius, Centre - Math.cos(Turn) * Radius];
            Svg.textContent = '';
            Svg.setAttribute('width', Size);
            Svg.append(ConstructSvg('circle', { cx: Centre, cy: Centre, r: Radius, fill: '#121212', stroke: '#2a2a2a' }),
                       ConstructSvg('circle', { cx: Centre, cy: Centre, r: Radius * 0.62, fill: 'none', stroke: '#ffffff', 'stroke-opacity': 0.06, 'stroke-dasharray': '2 5' }));
            for (let Tick = 0; Tick < 72; ++Tick)
            {
                const Angle = Tick * 5 * Math.PI / 180, Major = Tick % 18 === 0, Mid = Tick % 6 === 0;
                const Inner = Radius - (Major ? 9 : Mid ? 6 : 3);
                Svg.append(ConstructSvg('line', { x1: Centre + Math.sin(Angle) * Inner, y1: Centre - Math.cos(Angle) * Inner,
                                                  x2: Centre + Math.sin(Angle) * Radius, y2: Centre - Math.cos(Angle) * Radius,
                                                  stroke: '#ffffff', 'stroke-opacity': Major ? 0.55 : Mid ? 0.25 : 0.1 }));
            }
            for (const [Text, Angle] of [['N', 0], ['E', 90], ['S', 180], ['W', 270]])
            {
                const A = Angle * Math.PI / 180;
                Svg.append(ConstructSvg('text', { x: Centre + Math.sin(A) * (Radius + 9), y: Centre - Math.cos(A) * (Radius + 9) + 3.5,
                                                  'text-anchor': 'middle', fill: Text === 'N' ? '#d8d8d8' : '#6a6a6a', 'font-size': 10 }, Text));
            }
            const Back = [Centre - Math.sin(Turn) * Radius * 0.55, Centre + Math.cos(Turn) * Radius * 0.55];
            const Lateral = [Math.cos(Turn) * 7, Math.sin(Turn) * 7];
            const ArrowNeck = [Centre + Math.sin(Turn) * (Radius - 22), Centre - Math.cos(Turn) * (Radius - 22)];
            Svg.append(ConstructSvg('line', { x1: Back[0], y1: Back[1], x2: ArrowNeck[0], y2: ArrowNeck[1], stroke: '#dfe7f3', 'stroke-width': 2, 'stroke-linecap': 'round' }),
                       ConstructSvg('path', { d: `M${Tip[0] - Math.sin(Turn) * 8} ${Tip[1] + Math.cos(Turn) * 8} L${ArrowNeck[0] + Lateral[0]} ${ArrowNeck[1] + Lateral[1]} `
                                                + `L${ArrowNeck[0] - Lateral[0]} ${ArrowNeck[1] - Lateral[1]} Z`, fill: '#dfe7f3' }),
                       ConstructSvg('circle', { cx: Tip[0], cy: Tip[1], r: 7, fill: '#e8e8e8', stroke: '#0c0c0c', 'stroke-width': 2 }),
                       ConstructSvg('circle', { cx: Centre, cy: Centre, r: 3, fill: '#ffffff' }));
            const Reading = Specification.Resolve ? Specification.Resolve() : { Text: `${Bearing.toFixed(0)}°`, Caption: '' };
            Side.textContent = '';
            Side.append(Construct('div', 'dial-label', Specification.Label || 'Bearing'),
                        Construct('div', 'dial-reading', [document.createTextNode(Bearing.toFixed(0)), Construct('span', 'unit', '°')]),
                        Construct('div', 'dial-point', ResolveCompassPoint(Bearing)),
                        Reading.Caption ? Construct('div', 'dial-caption', Reading.Caption) : null);
        };
        const BearingAt = Event =>
        {
            const Bounds = Svg.getBoundingClientRect();
            const Scale  = Size / Math.max(1, Bounds.width);
            const X = (Event.clientX - Bounds.left) * Scale - Centre, Y = (Event.clientY - Bounds.top) * Scale - Centre;
            return ((Math.round(Math.atan2(X, -Y) * 180 / Math.PI) % 360) + 360) % 360;
        };
        let Dragging = false;
        Svg.addEventListener('pointerdown', Event => { Dragging = true; Svg.setPointerCapture(Event.pointerId); Specification.Write(BearingAt(Event)); Broadcast(); });
        Svg.addEventListener('pointermove', Event => { if (Dragging) { Specification.Write(BearingAt(Event)); Broadcast(); } });
        Svg.addEventListener('pointerup', () => { Dragging = false; });
        Svg.addEventListener('pointercancel', () => { Dragging = false; });
        Panel.addEventListener('keydown', Event =>
        {
            const Steps = { ArrowLeft: -1, ArrowDown: -1, ArrowRight: 1, ArrowUp: 1, PageDown: -15, PageUp: 15 }[Event.key];
            if (Steps === undefined) return;
            Specification.Write((((Specification.Read() + Steps) % 360) + 360) % 360);
            Broadcast();
            Event.preventDefault();
        });
        Register(Refresh);
        return { Panel, Refresh };
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
        ToggleTile, ReadoutTile, SectionCard, TimeSeries, ParameterMap, SmoothScroll, Toast,
        OpenScope, CloseScope, ReleaseScope, SelectRow, TextRow, VectorRow, ColourRow, ChipToggles, ActionRow, StatGrid, ChipRow,
        CurvePlot, PolarDial, ResolveCompassPoint, LinearToDisplay, DisplayToLinear
    };
})();
