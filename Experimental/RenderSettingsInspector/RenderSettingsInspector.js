/*==========================================================================================================================================
                                                       RENDERSETTINGSINSPECTOR.JS
==========================================================================================================================================*/
// 🧩 The Render settings prototype: one outliner component, its inspector, and a viewport showing the selected debug view.
'use strict';

(() =>
{
    const C       = InspectorControls;
    const Catalog = RenderSettingsCatalogue;
    const Settings = JSON.parse(JSON.stringify(Catalog.DemoDefaults));
    const Samples  = [];
    const SampleCapacity = 100;
    const ViewFolder     = '../../Exhibits/SceneViewport/Images/Inspection/';
    let   SelectedEntry = 'render';
    let   InspectorScroller = null;
    const LiveRefreshers = [];

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       BINDINGS
    //------------------------------------------------------------------------------------------------------------------------

    const KeyLabels = { Renderer: 'Renderer', Inspection: 'Debug view', TargetRate: 'Budget' };

    function Bind(Key, Extra)
    {
        if (Extra && Extra.Label && !KeyLabels[Key]) KeyLabels[Key] = Extra.Label;
        return Object.assign({
            Read: () => Settings[Key],
            Write: Quantity => { Settings[Key] = Quantity; },
            Availability: () => Catalog.ResolveAvailability(Settings, Key)
        }, Extra);
    }

    function ResolveViewportExtents()
    {
        const Surface = document.getElementById('ViewportSurface');
        const Width = Math.max(320, Surface ? Surface.clientWidth : 1200), Height = Math.max(180, Surface ? Surface.clientHeight : 680);
        return Catalog.ResolveExtents(Settings, Width, Height, window.devicePixelRatio || 1);
    }

    const FormatExtent = Extent => `${Extent[0]}×${Extent[1]}`;
    const FormatCount  = Count => Count >= 1e6 ? (Count / 1e6).toFixed(1) + ' M' : Count >= 1e3 ? (Count / 1e3).toFixed(0) + ' k' : String(Count);
    const TraceNames   = { 1: 'Full', 2: 'Half', 4: 'Quarter' };
    const TraceShares  = { 1: '1', 2: '½', 4: '¼' };
    const RendererNames = { Rtx: 'RTX · ReSTIR path tracer', Gtx: 'GTX · distance-field GI', Analytic: 'Analytic illumination' };

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       OUTLINER
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Default.levelspace after the merge: Diagnostics, Geometry clusters, BVH & distance field and Surface cache become
    //    sections of the one Render settings component.
    const OutlinerGroups = [
        { Title: 'Rendering', Rows: [
            { Id: 'render', Name: 'Render settings', Glyph: 'Monitor', Colour: '#9fb4d6',
              Meta: () => `${{ Rtx: 'RTX', Gtx: 'GTX', Analytic: 'Analytic' }[Settings.Renderer]} · ${FormatExtent(ResolveViewportExtents().Render)}` }] },
        { Title: 'Environment', Rows: [
            { Id: 'sun', Name: 'Sun', Glyph: 'Sun', Colour: '#e8c27a', Meta: () => '38° · 98 klx' },
            { Id: 'flare', Name: 'Lens Flare', Glyph: 'Flare', Colour: '#e8c27a', Meta: () => '0.62 × · 7 blades', Nested: true },
            { Id: 'clouds', Name: 'Clouds', Glyph: 'Cloud', Colour: '#b9c6d6', Meta: () => '42 % cover · 1750 m' },
            { Id: 'localcloud', Name: 'Local Cloud', Glyph: 'Cloud', Colour: '#b9c6d6', Meta: () => 'Local volume' },
            { Id: 'localfog', Name: 'Local Volumetric Fog', Glyph: 'Fog', Colour: '#a7b5c4', Meta: () => 'Local volume' },
            { Id: 'atmosphere', Name: 'Atmosphere', Glyph: 'Atmosphere', Colour: '#8fb4e8', Meta: () => 'Mie 2.3 · ozone 1.0' },
            { Id: 'stars', Name: 'Stars', Glyph: 'Star', Colour: '#c9b8f0', Meta: () => '5000 stars · ×1.15' },
            { Id: 'moons', Name: 'Moons', Glyph: 'Moon', Colour: '#d8d8d8', Meta: () => 'Luna · Ember' },
            { Id: 'venus', Name: 'Venus', Glyph: 'Planet', Colour: '#e6cf9c', Meta: () => 'Orbital solver' },
            { Id: 'jupiter', Name: 'Jupiter', Glyph: 'Planet', Colour: '#d9b38c', Meta: () => 'Orbital solver' },
            { Id: 'mars', Name: 'Mars', Glyph: 'Planet', Colour: '#d98f6c', Meta: () => 'Orbital solver' },
            { Id: 'saturn', Name: 'Saturn', Glyph: 'Planet', Colour: '#e3c98f', Meta: () => 'Orbital solver' },
            { Id: 'heightfog', Name: 'Height Fog', Glyph: 'Fog', Colour: '#a7b5c4', Meta: () => '0.012 · falloff 0.14' }] },
        { Title: 'Forces', Rows: [
            { Id: 'force', Name: 'Force Field', Glyph: 'Wind', Colour: '#8fc7b8', Meta: () => 'Ambient flow' },
            { Id: 'wind', Name: 'Wind', Glyph: 'Wind', Colour: '#8fc7b8', Meta: () => '6.5 m/s · 45°', Nested: true }] }
    ];

    function ConstructOutliner(Content)
    {
        const Total = OutlinerGroups.reduce((Sum, Group) => Sum + Group.Rows.length, 0);
        const Outliner = C.Construct('div', 'outliner');
        Outliner.append(
            C.Construct('div', 'outliner-heading', [
                C.Construct('div', null, [C.Construct('h1', null, 'Outliner'), C.Construct('div', 'muted', `Default.levelspace · ${Total} records`)]),
                C.Construct('div', 'spacer'), C.Construct('div', 'round-button', C.ConstructGlyph('Sliders'))]),
            C.Construct('div', 'count-cards', [
                C.Construct('div', 'count-card', [C.Construct('div', 'chip-glyph live', C.ConstructGlyph('Check')), C.Construct('div', 'label', 'Enabled'),
                                                 C.Construct('div', 'number', String(Total))]),
                C.Construct('div', 'count-card', [C.Construct('div', 'chip-glyph dim', C.ConstructGlyph('EyeOff')), C.Construct('div', 'label', 'Hidden'),
                                                 C.Construct('div', 'number', '0')])]),
            C.Construct('div', 'search-row', [
                C.Construct('div', 'search-pill', [C.ConstructGlyph('Search'), C.Construct('span', null, 'Search'), C.Construct('kbd', null, 'Ctrl+F')]),
                C.Construct('div', 'filter-pill', [C.ConstructGlyph('Sliders'), C.Construct('span', null, 'Filter')])]));
        for (const Group of OutlinerGroups)
        {
            const Section = C.Construct('div', 'tree-group');
            Section.append(C.Construct('div', 'tree-group-title', [C.ConstructGlyph('Chevron'), C.Construct('span', null, Group.Title),
                                                                    C.Construct('span', 'count', String(Group.Rows.length))]));
            for (const Row of Group.Rows)
            {
                const Meta = C.Construct('div', 'row-meta');
                const Glyph = C.Construct('div', 'row-glyph', C.ConstructGlyph(Row.Glyph));
                Glyph.style.color = Row.Colour;
                Glyph.style.background = Row.Colour + '1f';
                const Element = C.Construct('div', 'tree-row' + (Row.Nested ? ' nested' : ''), [
                    Glyph, C.Construct('div', 'row-text', [C.Construct('div', 'row-name', Row.Name), Meta]),
                    C.Construct('div', 'row-state', C.ConstructGlyph('Check'))]);
                Element.addEventListener('click', () => { SelectedEntry = Row.Id; ConstructInspectorPage(); C.Broadcast(); });
                C.Register(() => { Meta.textContent = Row.Meta(); Element.classList.toggle('selected', SelectedEntry === Row.Id); });
                Section.append(Element);
            }
            if (Group.Title === 'Rendering')
            {
                Section.append(C.Construct('div', 'merge-note', [
                    C.Construct('b', null, 'One component. '),
                    document.createTextNode('Diagnostics, Geometry clusters, BVH & distance field and Surface cache are now sections of Render settings.')]));
            }
            Outliner.append(Section);
        }
        Content.append(Outliner);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       VIEWPORT
    //------------------------------------------------------------------------------------------------------------------------

    const ViewImages = new Map();

    // 📝 The cluster views (10–14) were captured twice: as the default level shows them and with virtualized geometry on.
    function ResolveViewPath(Index)
    {
        const Virtualized = Index >= 10 && Settings.VirtualizedGeometry;
        return ViewFolder + `Inspection-${String(Index).padStart(2, '0')}${Virtualized ? '-Virtualized' : ''}.jpg`;
    }

    function ResolveViewImage(Index)
    {
        const Path = ResolveViewPath(Index);
        if (ViewImages.has(Path)) return ViewImages.get(Path);
        const Picture = new Image();
        Picture.decoding = 'async';
        Picture.src = Path;
        Picture.addEventListener('load', () => { RefreshViewport(); C.Broadcast(); });
        ViewImages.set(Path, Picture);
        return Picture;
    }

    function ResolveViewNote(View)
    {
        if (View.Missing) return 'Not ported';
        if (View.NeedsVirtualized && !Settings.VirtualizedGeometry) return 'VG off';
        return '';
    }

    // 📝 The captured view, resampled at the render extent's share of the screen: a lower viewport scale reads softer.
    const Downsampled = document.createElement('canvas');
    function RefreshViewport()
    {
        const Canvas = document.getElementById('ViewportCanvas');
        if (!Canvas) return;
        const Surface = Canvas.parentElement;
        const Ratio = Math.min(window.devicePixelRatio || 1, 2);
        const Width = Math.max(1, Math.round(Surface.clientWidth * Ratio)), Height = Math.max(1, Math.round(Surface.clientHeight * Ratio));
        if (Canvas.width !== Width || Canvas.height !== Height) { Canvas.width = Width; Canvas.height = Height; }
        const Context = Canvas.getContext('2d');
        Context.fillStyle = '#060606';
        Context.fillRect(0, 0, Width, Height);
        const Picture = ResolveViewImage(Settings.Inspection);
        const Fallback = ResolveViewImage(0);
        const Source = Picture.complete && Picture.naturalWidth ? Picture : Fallback.complete && Fallback.naturalWidth ? Fallback : null;
        const Caption = document.getElementById('ViewportCaption');
        if (!Source)
        {
            Context.fillStyle = '#3a3a3a';
            Context.font = `${13 * Ratio}px DM Sans, sans-serif`;
            Context.textAlign = 'center';
            Context.fillText('Capture not found: run InspectionViewCapture (Exhibits/SceneViewport)', Width / 2, Height / 2);
            if (Caption) Caption.textContent = 'No capture';
            return;
        }
        const Extents = ResolveViewportExtents();
        const Sharpness = Math.min(1, Extents.Render[0] / Extents.Screen[0]);
        const Cover = Math.max(Width / Source.naturalWidth, Height / Source.naturalHeight);
        const CoverWidth = Source.naturalWidth * Cover, CoverHeight = Source.naturalHeight * Cover;
        Downsampled.width = Math.max(16, Math.round(Width * Sharpness));
        Downsampled.height = Math.max(16, Math.round(Height * Sharpness));
        const Small = Downsampled.getContext('2d');
        Small.imageSmoothingQuality = 'high';
        Small.drawImage(Source, (Width - CoverWidth) / 2 * Sharpness, (Height - CoverHeight) / 2 * Sharpness, CoverWidth * Sharpness, CoverHeight * Sharpness);
        Context.imageSmoothingEnabled = true;
        Context.imageSmoothingQuality = Sharpness < 1 ? 'low' : 'high';
        Context.drawImage(Downsampled, 0, 0, Width, Height);
        const View = Catalog.InspectionViews[Settings.Inspection];
        if (Caption)
            Caption.textContent = View.Missing ? 'Black in the C++ port: the demo’s InspectionMarch is not ported yet'
                                : View.NeedsVirtualized && !Settings.VirtualizedGeometry ? 'Needs virtualized geometry: the demo’s fallback shows'
                                : Source === Picture ? `C++ viewport capture · ${Source.naturalWidth}×${Source.naturalHeight} · SwiftShader`
                                : 'View not captured · showing the final image';
    }

    function ConstructViewport()
    {
        const Bar = document.getElementById('ViewportBar');
        const ChipText = C.Construct('span');
        const ViewName = C.Construct('span');
        const ViewButton = C.Construct('div', 'view-select', [C.ConstructGlyph('Eye'), ViewName, C.ConstructGlyph('Chevron')]);
        ViewButton.addEventListener('click', () => RevealSection('views'));
        Bar.append(C.Construct('div', 'scene-glyph', C.ConstructGlyph('Folder')),
                   C.Construct('div', null, [C.Construct('div', 'scene-title', 'Scene viewport'), C.Construct('div', 'scene-meta', 'Everest · Default.levelspace')]),
                   C.Construct('div', 'spacer'), C.Construct('div', 'renderer-chip', [C.Construct('i'), ChipText]), ViewButton);
        const Hud = document.getElementById('ViewportHud');
        const Readout = document.getElementById('ViewportReadout');
        const Inset = document.getElementById('ViewportInset');
        C.Register(() =>
        {
            const Extents = ResolveViewportExtents();
            const View = Catalog.InspectionViews[Settings.Inspection];
            ChipText.textContent = RendererNames[Settings.Renderer].toUpperCase();
            ViewName.textContent = View.Name;
            Hud.innerHTML = '';
            Hud.append(C.Construct('div', null, [C.Construct('span', null, 'VIEW '), document.createTextNode(View.Name)]),
                       C.Construct('div', null, [C.Construct('span', null, 'RENDER '), document.createTextNode(FormatExtent(Extents.Render))]),
                       C.Construct('div', null, [C.Construct('span', null, 'TRACE '), document.createTextNode(FormatExtent(Extents.Trace))]));
            const Rays = Catalog.CountRays(Settings, Extents);
            Readout.classList.toggle('shown', Settings.HudVisible);
            Readout.textContent = [
                Settings.Renderer === 'Rtx' ? `RTX ReSTIR · ${Settings.MaxBounces}B GI · ${Settings.ReflectionBounces}B Refl · M=${Settings.InitialCandidates}`
                                            : RendererNames[Settings.Renderer],
                `Screen ${FormatExtent(Extents.Screen)} · render ${FormatExtent(Extents.Render)} · trace ${FormatExtent(Extents.Trace)}`,
                `TLAS 504 instances (1007 entries) · BLAS 20878 triangles`,
                `Rays ≤ ${FormatCount(Rays.Total)} per refresh · ${Extents.Grade.ClipResolution}³ clipmaps`].join('\n');
            Inset.classList.toggle('shown', Settings.InsetEnabled);
            Inset.querySelector('.inset-label').textContent = `Distance slice · ${Settings.SliceHeight.toFixed(2)} m`;
            RefreshViewport();
        });
        new ResizeObserver(() => C.Broadcast()).observe(document.getElementById('ViewportSurface'));
        const Status = document.getElementById('ViewportStatus');
        Status.append(C.Construct('span', null, 'HTML prototype'), C.Construct('span', null, 'Viewport images from the C++ renderer'),
                      C.Construct('span', 'spacer'), C.Construct('span', null, 'Timings are estimates · no engine connection'));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                      LOG STREAM
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 What the editor will log for each change: the setting, its new reading, and what the renderer must rebuild for it.
    const Consequences = {
        RenderScale: 'render targets rebuilt', TraceScale: 'trace targets rebuilt', QualityTier: 'scene reloads at the new grade',
        Renderer: 'refresh history cleared', Inspection: 'display only', VirtualizedGeometry: 'static sets re-recorded',
        ClusterTriangles: 'clusters rebuilt', LiveRefresh: 'the viewport holds its last image while paused'
    };
    const LogEntries = [];
    let   LogPaused  = false;

    function DescribeSetting(Key, Reading)
    {
        if (Key === 'Renderer') return RendererNames[Reading];
        if (Key === 'Inspection') return Catalog.InspectionViews[Reading].Name;
        if (Key === 'TraceScale') return TraceNames[Reading];
        if (Key === 'QualityTier') return Catalog.QualityGrades[Reading].Name;
        if (Key === 'TargetRate') return `${Reading} fps`;
        if (typeof Reading === 'boolean') return Reading ? 'on' : 'off';
        return Number.isInteger(Reading) ? String(Reading) : Number(Reading).toFixed(2);
    }

    function AppendLog(Channel, Message, Detail, Tone, Key)
    {
        if (LogPaused) return;
        const Now = new Date();
        const Last = LogEntries[0];
        // 📝 A drag writes many times: consecutive writes to one setting within 1.5 s update a single row.
        if (Key && Last && Last.Key === Key && Now - Last.When < 1500) Object.assign(Last, { Message, Detail, When: Now });
        else LogEntries.unshift({ Channel, Message, Detail, Tone, Key, When: Now });
        if (LogEntries.length > 60) LogEntries.pop();
        RefreshLog();
    }

    function RefreshLog()
    {
        const Content = document.getElementById('LogContent');
        if (!Content) return;
        Content.textContent = '';
        for (const Entry of LogEntries)
        {
            const When = Entry.When.toTimeString().slice(0, 8);
            Content.append(C.Construct('div', 'log-row', [
                C.Construct('span', 'severity' + (Entry.Tone === 'warm' ? ' warm' : '')), C.Construct('span', 'when', When),
                C.Construct('span', 'channel', Entry.Channel),
                C.Construct('span', 'message', [C.Construct('b', null, Entry.Message), Entry.Detail ? C.Construct('span', null, ' · ' + Entry.Detail) : null])]));
        }
        const Count = document.getElementById('LogCount');
        if (Count) Count.textContent = `${LogEntries.length} entries`;
    }

    function ConstructLogStream()
    {
        const Head = document.getElementById('LogHead');
        const Pause = C.Construct('div', 'round-button', C.ConstructGlyph('Clock'));
        const Clear = C.Construct('div', 'round-button', C.ConstructGlyph('Close'));
        Pause.title = 'Pause';
        Clear.title = 'Clear';
        Pause.addEventListener('click', () => { LogPaused = !LogPaused; Pause.style.color = LogPaused ? 'var(--warm)' : ''; });
        Clear.addEventListener('click', () => { LogEntries.length = 0; RefreshLog(); });
        const CountLabel = C.Construct('span', 'log-count');
        CountLabel.id = 'LogCount';
        Head.append(C.Construct('div', 'log-title', 'Log Stream'), C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, 'LIVE')]),
                    C.Construct('span', 'spacer'), CountLabel, Pause, Clear);
        C.SmoothScroll(document.getElementById('LogScroll'), document.getElementById('LogContent'));
        AppendLog('EVEREST', "Loaded 'Default.levelspace'", '16 scene records', 'live');
        AppendLog('RENDERING', 'Render settings: one component', 'replaces Diagnostics, Geometry clusters, BVH & distance field, Surface cache', 'live');
        let Previous = JSON.parse(JSON.stringify(Settings));
        C.Register(() =>
        {
            for (const Key of Object.keys(Settings))
            {
                if (Settings[Key] === Previous[Key]) continue;
                const Consequence = Consequences[Key];
                AppendLog('RENDERING', `${(KeyLabels[Key] || Key).split(' · ')[0]}: ${DescribeSetting(Key, Settings[Key])}`, Consequence,
                          Key === 'QualityTier' || Key === 'TraceScale' || Key === 'RenderScale' ? 'warm' : 'live', Key);
            }
            Previous = JSON.parse(JSON.stringify(Settings));
        });
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    INSPECTOR HEADING
    //------------------------------------------------------------------------------------------------------------------------

    function ConstructHero(Page)
    {
        C.PillTabs(Page, { Options: [['RTX · ReSTIR', 'Rtx'], ['GTX · Distance field', 'Gtx'], ['Analytic', 'Analytic']],
                           Read: () => Settings.Renderer, Write: Renderer => { Settings.Renderer = Renderer; } });
        const BudgetMark = C.Construct('div', 'mark');
        const BudgetText = C.Construct('span');
        const Metric = C.Construct('div', 'metric-big');
        const Rate = C.Construct('div', 'rate');
        const Low = C.Construct('div', 'low');
        const TargetChoice = C.Construct('div', 'target-choice');
        for (const TargetRate of [30, 60, 120])
        {
            const Button = C.Construct('button', null, `${TargetRate} fps`);
            Button.type = 'button';
            Button.addEventListener('click', () => { Settings.TargetRate = TargetRate; C.Broadcast(); });
            C.Register(() => Button.classList.toggle('active', Settings.TargetRate === TargetRate));
            TargetChoice.append(Button);
        }
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [BudgetMark, BudgetText]),
                                             C.Construct('div', 'estimate-tag', 'Estimated · prototype')]),
            C.Construct('div', 'hero-title', 'GPU time per refresh'),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])]),
            C.Construct('div', 'target-row', [C.ConstructGlyph('Target'), C.Construct('span', null, 'Budget per refresh'), C.Construct('span', 'spacer'), TargetChoice])
        ]);
        Page.append(Card);
        const Series = C.TimeSeries(Card, { Samples, Capacity: SampleCapacity, Target: () => 1000 / Settings.TargetRate, Height: 128 });
        // 📝 The owner's observation in the demo: more rays per pixel did not lower the frame rate. The rate sits at the display's
        //    cap while the GPU has headroom, so the cost shows first in GPU time, which is what this card leads with.
        Card.append(C.Construct('div', 'hero-note', 'Why GPU time, not FPS: the frame rate holds at the display’s cap until the GPU runs out '
                                                    + 'of headroom, so extra rays show here first.'));
        const LiveRefresh = () =>
        {
            const Latest = Samples.length ? Samples[Samples.length - 1] : 0;
            // 📝 The rate over the last two seconds; the 1 % low over the whole ten-second window.
            const Recent = Samples.slice(-20);
            const Mean = Recent.length ? Recent.reduce((Sum, Sample) => Sum + Sample, 0) / Recent.length : 0;
            const Sorted = [...Samples].sort((A, B) => B - A);
            const Worst = Sorted.length ? Sorted[Math.max(0, Math.floor(Sorted.length * 0.01))] : 0;
            const Budget = 1000 / Settings.TargetRate;
            C.ConstructMetric(Metric, Latest, 1, 'ms');
            Rate.textContent = (Mean > 0 ? 1000 / Mean : 0).toFixed(0);
            Rate.append(C.Construct('small', null, 'fps'));
            Low.textContent = `1 % low ${(Worst > 0 ? 1000 / Worst : 0).toFixed(0)} fps`;
            const Within = Mean <= Budget;
            const Close = Mean <= Budget * 1.15;
            BudgetMark.className = 'mark ' + (Within ? 'live' : Close ? 'warm' : 'danger');
            BudgetMark.textContent = '';
            BudgetMark.append(C.ConstructGlyph(Within ? 'Check' : 'Alert'));
            BudgetText.textContent = Within ? 'Within budget' : Close ? 'At the budget' : 'Over budget';
        };
        C.Register(LiveRefresh);
        LiveRefreshers.length = 0;
        LiveRefreshers.push(LiveRefresh, Series.Refresh);
    }

    function ConstructOverview(Page)
    {
        const Chips = C.Construct('div', 'chip-row');
        const Chip = (Resolve) =>
        {
            const Element = C.Construct('div', 'count-chip');
            C.Register(() => { const [Text, Small] = Resolve(); Element.textContent = ''; Element.append(C.Construct('span', 'tabular', Text), C.Construct('small', null, Small)); });
            Chips.append(Element);
        };
        Chip(() => [FormatExtent(ResolveViewportExtents().Render), 'render']);
        Chip(() => [FormatExtent(ResolveViewportExtents().Trace), `trace · ${TraceShares[Settings.TraceScale]}`]);
        Chip(() => [Settings.Renderer === 'Rtx' ? String(Settings.InitialCandidates) : '—', 'paths / px']);
        Chip(() => [Settings.Renderer === 'Rtx' ? `${Settings.MaxBounces} · ${Settings.ReflectionBounces}` : '—', 'bounces']);
        Page.append(Chips);
        const Tiles = C.Construct('div', 'tile-grid');
        C.ReadoutTile(Tiles, { Label: 'Rays per refresh · at most', Glyph: 'Rays',
                               Resolve: () => { const Count = Catalog.CountRays(Settings, ResolveViewportExtents()).Total;
                                                return Count ? [FormatCount(Count).replace(' ', ''), 'rays'] : ['—', Settings.Renderer === 'Rtx' ? '' : 'no BVH rays']; } });
        C.ReadoutTile(Tiles, { Label: 'Render pixels', Glyph: 'Monitor',
                               Resolve: () => { const Extents = ResolveViewportExtents();
                                                return [((Extents.Render[0] * Extents.Render[1]) / 1e6).toFixed(2), Extents.Capped ? 'M · at ceiling' : 'M px']; } });
        C.ReadoutTile(Tiles, { Label: 'Quality grade', Glyph: 'Gauge',
                               Resolve: () => { const Grade = Catalog.QualityGrades[Settings.QualityTier]; return [Grade.Name, `${Grade.ClipResolution}³`]; } });
        C.ReadoutTile(Tiles, { Label: 'Viewport scale', Glyph: 'Scan', Resolve: () => [Settings.RenderScale.toFixed(2), '×'] });
        Page.append(Tiles);
        Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Quick controls'),
                                                            C.Construct('div', 'caption', 'The demo’s switches')]));
        const Grid = C.Construct('div', 'toggle-grid');
        C.ToggleTile(Grid, { Label: 'Ray tracing', Glyph: 'Zap', Read: () => Settings.Renderer === 'Rtx',
                             Write: Enabled => { Settings.Renderer = Enabled ? 'Rtx' : 'Gtx'; },
                             Availability: () => Settings.Renderer === 'Analytic' ? { Enabled: false, Reason: 'analytic renderer' } : { Enabled: true } });
        C.ToggleTile(Grid, Bind('GlobalIllumination', { Label: 'Indirect GI', Glyph: 'Sparkles' }));
        C.ToggleTile(Grid, Bind('Reflection', { Label: 'Reflections', Glyph: 'Aperture' }));
        C.ToggleTile(Grid, Bind('RayTracedShadows', { Label: 'RT shadows', Glyph: 'Sun' }));
        C.ToggleTile(Grid, Bind('VarianceFilter', { Label: 'Denoiser', Glyph: 'Layers' }));
        C.ToggleTile(Grid, Bind('Antialias', { Label: 'Temporal AA', Glyph: 'Scan' }));
        Page.append(Grid);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       DIAGRAMS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Viewport, render and trace extents nested at true proportion: the trace grid shows one cell per trace pixel block.
    function ConstructResolutionDiagram(Parent)
    {
        const Height = 168;
        const Panel = C.Construct('div', 'diagram-panel');
        const Svg = C.ConstructSvg('svg', { class: 'diagram', height: Height });
        Panel.append(Svg);
        Parent.append(Panel);
        const Refresh = () =>
        {
            const Extents = ResolveViewportExtents();
            const Width = Math.max(220, Svg.clientWidth || 340);
            const Largest = Math.max(Extents.Screen[0], Extents.Render[0]);
            const Fit = Math.min((Width - 4) / Largest, (Height - 30) / Math.max(Extents.Screen[1], Extents.Render[1]));
            Svg.textContent = '';
            const Box = (Extent, Stroke, Fill, Dash) => C.ConstructSvg('rect', { x: 1, y: Height - 22 - Extent[1] * Fit, width: Extent[0] * Fit,
                                                                                  height: Extent[1] * Fit, rx: 6, fill: Fill, stroke: Stroke,
                                                                                  'stroke-width': 1.2, 'stroke-dasharray': Dash || '' });
            Svg.append(Box(Extents.Screen, '#4a4a4a', 'rgba(255,255,255,0.02)', '3 4'), Box(Extents.Render, '#8a8a8a', 'rgba(255,255,255,0.035)'));
            const TraceBox = Box(Extents.Trace, '#9fb4d6', 'rgba(159,180,214,0.14)');
            Svg.append(TraceBox);
            // ① the trace pixel lattice, coarsened so a cell stays visible: each cell stands for TraceScale² render pixels × n
            const Cell = Math.max(5, Extents.Trace[0] * Fit / 24);
            const Top = Height - 22 - Extents.Trace[1] * Fit;
            for (let X = Cell; X < Extents.Trace[0] * Fit - 1; X += Cell)
                Svg.append(C.ConstructSvg('line', { x1: 1 + X, x2: 1 + X, y1: Top, y2: Height - 22, stroke: '#9fb4d6', 'stroke-opacity': 0.16 }));
            for (let Y = Cell; Y < Extents.Trace[1] * Fit - 1; Y += Cell)
                Svg.append(C.ConstructSvg('line', { x1: 1, x2: 1 + Extents.Trace[0] * Fit, y1: Top + Y, y2: Top + Y, stroke: '#9fb4d6', 'stroke-opacity': 0.16 }));
            const Label = (X, Y, Text, Fill, Anchor) => C.ConstructSvg('text', { x: X, y: Y, fill: Fill, 'font-size': 10, 'text-anchor': Anchor || 'start' }, Text);
            Svg.append(Label(8, Height - 22 - Extents.Trace[1] * Fit + 15, `Trace ${FormatExtent(Extents.Trace)}`, '#cfdcef'),
                       Label(Extents.Render[0] * Fit - 6, Height - 22 - Extents.Render[1] * Fit + 15, `Render ${FormatExtent(Extents.Render)}`, '#bcbcbc', 'end'),
                       Label(1, Height - 6, `Viewport ${FormatExtent(Extents.Screen)}`, '#6e6e6e'),
                       Label(Width - 2, Height - 6, `${TraceNames[Settings.TraceScale]} trace · ${(Settings.TraceScale * Settings.TraceScale)} render px per trace px`,
                             '#6e6e6e', 'end'));
        };
        new ResizeObserver(Refresh).observe(Svg);
        C.Register(Refresh);
    }

    // 📝 Four toroidal clipmaps: cascade c has voxels of FinestVoxel × 2^c over ClipResolution voxels a side.
    function ConstructCascadeDiagram(Parent)
    {
        const Height = 176;
        const Panel = C.Construct('div', 'diagram-panel');
        const Svg = C.ConstructSvg('svg', { class: 'diagram', height: Height });
        Panel.append(Svg);
        Parent.append(Panel);
        const Refresh = () =>
        {
            const Grade = Catalog.QualityGrades[Settings.QualityTier];
            const Width = Math.max(220, Svg.clientWidth || 340);
            const CentreX = Height / 2 + 6, CentreY = Height / 2;
            Svg.textContent = '';
            for (let Cascade = 3; Cascade >= 0; --Cascade)
            {
                const Side = (Height - 12) * Math.pow(2, Cascade - 3);
                const Extent = Grade.ClipResolution * Grade.FinestVoxel * Math.pow(2, Cascade);
                Svg.append(C.ConstructSvg('rect', { x: CentreX - Side / 2, y: CentreY - Side / 2, width: Side, height: Side, rx: 4,
                                                    fill: `rgba(159,180,214,${0.03 + 0.05 * (3 - Cascade)})`, stroke: '#9fb4d6',
                                                    'stroke-opacity': 0.25 + 0.18 * (3 - Cascade), 'stroke-dasharray': Cascade === 3 ? '3 4' : '' }));
                const RowY = 34 + (3 - Cascade) * 34;
                Svg.append(C.ConstructSvg('text', { x: Height + 26, y: RowY, fill: '#d8d8d8', 'font-size': 12 }, `${Extent.toFixed(0)} m`),
                           C.ConstructSvg('text', { x: Height + 26, y: RowY + 14, fill: '#6e6e6e', 'font-size': 9.5 },
                                          `cascade ${Cascade} · ${(Grade.FinestVoxel * Math.pow(2, Cascade)).toFixed(3).replace(/0+$/, '').replace(/\.$/, '')} m voxels`));
            }
            Svg.append(C.ConstructSvg('circle', { cx: CentreX, cy: CentreY, r: 3, fill: '#ffffff' }),
                       C.ConstructSvg('text', { x: Width - 2, y: Height - 4, fill: '#6e6e6e', 'font-size': 9.5, 'text-anchor': 'end' },
                                      `${Grade.ClipResolution}³ each · camera at the centre`));
        };
        new ResizeObserver(Refresh).observe(Svg);
        C.Register(Refresh);
    }

    // 📝 The proving ground's glossy materials on the roughness axis, against the cutoff (Exhibits/RayTracedLighting views).
    function ConstructRoughnessDiagram(Parent)
    {
        const Height = 74;
        const Svg = C.ConstructSvg('svg', { class: 'diagram', height: Height });
        const Panel = C.Construct('div', 'diagram-panel', Svg);
        Parent.append(Panel);
        const Materials = [['Glass', 0.04], ['Tempered pane', 0.06], ['Traffic cone', 0.22]];
        const Refresh = () =>
        {
            const Width = Math.max(220, Svg.clientWidth || 340);
            const AtX = Roughness => 4 + Roughness * (Width - 8);
            Svg.textContent = '';
            const Defs = C.ConstructSvg('defs');
            const Ramp = C.ConstructSvg('linearGradient', { id: 'RoughnessRamp', x1: 0, x2: 1, y1: 0, y2: 0 });
            Ramp.append(C.ConstructSvg('stop', { offset: '0%', 'stop-color': '#dfe7f3', 'stop-opacity': 0.75 }),
                        C.ConstructSvg('stop', { offset: '100%', 'stop-color': '#3a3a3a', 'stop-opacity': 0.6 }));
            Defs.append(Ramp);
            Svg.append(Defs, C.ConstructSvg('rect', { x: 4, y: 30, width: Width - 8, height: 8, rx: 4, fill: 'url(#RoughnessRamp)' }));
            const Cutoff = AtX(Settings.ReflectionRoughness);
            Svg.append(C.ConstructSvg('rect', { x: Cutoff, y: 26, width: Math.max(0, Width - 4 - Cutoff), height: 16, fill: '#161616', 'fill-opacity': 0.72 }),
                       C.ConstructSvg('line', { x1: Cutoff, x2: Cutoff, y1: 18, y2: 50, stroke: '#ffffff', 'stroke-dasharray': '3 3' }),
                       C.ConstructSvg('text', { x: Cutoff, y: 12, fill: '#d8d8d8', 'font-size': 10, 'text-anchor': Settings.ReflectionRoughness > 0.8 ? 'end' : 'middle' },
                                      `cutoff ${Settings.ReflectionRoughness.toFixed(2)}`));
            // 📝 Glass and the tempered pane sit 0.02 apart: one label for both, the cone on a second row.
            for (const [, Roughness] of Materials) Svg.append(C.ConstructSvg('circle', { cx: AtX(Roughness), cy: 34, r: 3.5, fill: '#ffffff' }));
            Svg.append(C.ConstructSvg('text', { x: AtX(0.04) - 3, y: 58, fill: '#8a8a8a', 'font-size': 9.5 }, 'Glass 0.04 · tempered pane 0.06'),
                       C.ConstructSvg('line', { x1: AtX(0.22), x2: AtX(0.22), y1: 39, y2: 62, stroke: '#ffffff', 'stroke-opacity': 0.25 }),
                       C.ConstructSvg('text', { x: AtX(0.22) + 5, y: 71, fill: '#8a8a8a', 'font-size': 9.5 }, 'Traffic cone 0.22'),
                       C.ConstructSvg('text', { x: Width - 4, y: 71, fill: '#5e5e5e', 'font-size': 9.5, 'text-anchor': 'end' }, 'rougher: no reflection rays'));
        };
        new ResizeObserver(Refresh).observe(Svg);
        C.Register(Refresh);
    }

    function ConstructStatGrid(Parent, Cells)
    {
        const Grid = C.Construct('div', 'stat-grid');
        for (const [Label, Resolve] of Cells)
        {
            const Reading = C.Construct('b');
            Grid.append(C.Construct('div', 'stat-cell', [Reading, C.Construct('span', null, Label)]));
            C.Register(() => { Reading.textContent = typeof Resolve === 'function' ? Resolve() : Resolve; });
        }
        Parent.append(Grid);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       SECTIONS
    //------------------------------------------------------------------------------------------------------------------------

    const Sections = [];

    function ConstructSection(Page, Id, Title, Glyph, Tab, Status)
    {
        const Section = C.SectionCard(Page, { Id, Title, Glyph, Status });
        Sections.push({ Id, Tab, Card: Section.Card });
        return Section.Inner;
    }

    const Caption = (Parent, Text) => Parent.append(C.Construct('p', 'section-caption', Text));
    const SubHeading = (Parent, Text) => Parent.append(C.Construct('div', 'sub-heading', Text));
    const Recommended = [['Recommended', 'live']];

    function ConstructResolution(Page)
    {
        const Body = ConstructSection(Page, 'resolution', 'Resolution & sampling', 'Monitor', 'Resolution',
                                      () => `${FormatExtent(ResolveViewportExtents().Render)} · ${TraceNames[Settings.TraceScale]} trace`);
        Caption(Body, 'The render target is the viewport times the viewport scale, under the grade’s pixel ceiling. ReSTIR GI and the '
                    + 'reflections trace at the trace resolution: Half (the demo) traces one pixel in four; Full traces every pixel, four times '
                    + 'the rays, for sharper bounce light and reflections.');
        ConstructResolutionDiagram(Body);
        C.SliderRow(Body, Bind('RenderScale', { Label: 'Viewport scale', Minimum: 0.25, Maximum: 2, Step: 0.05, Unit: '×', Digits: 2,
                                                Labels: ['Faster', 'Sharper'], Tags: [['Rebuilds targets', 'muted']] }));
        C.SegmentedRow(Body, Bind('TraceScale', { Label: 'Trace resolution · GI and reflections', Options: [['Full', 1], ['Half', 2], ['Quarter', 4]],
                                                  Tags: [['Rebuilds targets', 'muted']] }));
        C.SliderRow(Body, Bind('InitialCandidates', { Label: 'Paths per pixel (M)', Minimum: 1, Maximum: 4, Step: 1, Unit: 'M', Digits: 0,
                                                      Labels: ['Fast', 'Low variance'] }));
        C.SegmentedRow(Body, Bind('QualityTier', { Label: 'Quality grade', Options: [['Low', 0], ['Medium', 1], ['High', 2]], Tags: [['Reloads scene', 'warm']] }));
        const Grade = () => Catalog.QualityGrades[Settings.QualityTier];
        ConstructStatGrid(Body, [['Cascades', () => `${Grade().ClipResolution}³`], ['Finest voxel', () => `${Grade().FinestVoxel} m`],
                                 ['March steps', () => String(Grade().Steps)], ['Shadow map', () => String(Grade().ShadowExtent)],
                                 ['Card atlas', () => String(Grade().CardExtent)],
                                 ['Pixel ceiling', () => ({ 0: '1280×720', 1: '1600×900', 2: '1920×1080' })[Settings.QualityTier]]]);
    }

    function ConstructIllumination(Page)
    {
        const Body = ConstructSection(Page, 'illumination', 'Global illumination', 'Sparkles', 'GI',
                                      () => !Settings.GlobalIllumination || Settings.Renderer === 'Analytic' ? 'Off'
                                            : Settings.Renderer === 'Rtx' ? `${Settings.MaxBounces} B · M ${Settings.InitialCandidates}` : 'Distance field');
        C.SwitchRow(Body, Bind('GlobalIllumination', { Label: 'Indirect global illumination',
                                                       Detail: 'ReSTIR GI: path-traced bounce light (RTX) or the distance-field gather (GTX)' }));
        C.SliderRow(Body, Bind('MaxBounces', { Label: 'Multi-bounce · path bounces', Minimum: 1, Maximum: 4, Step: 1, Unit: 'B', Digits: 0,
                                               Labels: ['1 bounce', '4 bounces'] }));
        C.SwitchRow(Body, Bind('MultiBounceNee', { Label: 'Multi-bounce NEE + RIS', Detail: 'Sun, fire and lamp light sampled at every path vertex' }));
        C.SwitchRow(Body, Bind('RussianRoulette', { Label: 'Russian roulette', Detail: 'Throughput-weighted, unbiased path termination' }));
        SubHeading(Body, 'ReSTIR reuse');
        C.SwitchRow(Body, Bind('TemporalReuse', { Label: 'Temporal reuse', Detail: 'Reservoirs carried through motion vectors' }));
        C.SwitchRow(Body, Bind('SpatialReuse', { Label: 'Spatial reuse', Detail: 'Neighbouring reservoirs merged per pixel' }));
        C.SwitchRow(Body, Bind('VisibilityReuse', { Label: 'Visibility validation', Detail: 'Reused samples re-traced for occlusion' }));
        C.SwitchRow(Body, Bind('RadianceRefresh', { Label: 'Refresh reused radiance', Detail: 'Reused samples re-shaded under this refresh’s light' }));
        C.SliderRow(Body, Bind('SpatialCount', { Label: 'Spatial candidates', Minimum: 0, Maximum: 8, Step: 1, Digits: 0 }));
        C.SliderRow(Body, Bind('SpatialRadius', { Label: 'Spatial radius', Minimum: 2, Maximum: 48, Step: 1, Unit: 'px', Digits: 0 }));
        C.SliderRow(Body, Bind('TemporalTallyLimit', { Label: 'Temporal M limit', Minimum: 1, Maximum: 40, Step: 1, Digits: 0, Labels: ['Responsive', 'Stable'] }));
        SubHeading(Body, 'Intensity');
        C.SliderRow(Body, Bind('IndirectScale', { Label: 'Indirect intensity', Minimum: 0, Maximum: 3, Step: 0.05, Unit: '×', Digits: 2 }));
        C.SliderRow(Body, Bind('SkyScale', { Label: 'Sky light', Minimum: 0, Maximum: 3, Step: 0.05, Unit: '×', Digits: 2 }));
        C.SliderRow(Body, Bind('IncandescenceScale', { Label: 'Emissive surfaces', Minimum: 0, Maximum: 3, Step: 0.05, Unit: '×', Digits: 2 }));
        SubHeading(Body, 'Surface cache');
        C.SwitchRow(Body, Bind('CardRadiosity', { Label: 'Card radiosity', Detail: 'Multi-bounce light inside the surface cache (8,554 cards)' }));
        C.SliderRow(Body, Bind('CardTileBudget', { Label: 'Static tiles per refresh · 0 = the grade’s', Minimum: 0, Maximum: 8192, Step: 64, Digits: 0,
                                                   Labels: ['Grade budget', 'Eager'] }));
        const Entry = (Key, Label, Short, Minimum, Maximum, Step, Digits, Unit) =>
            Object.assign(Bind(Key), { Label, Short, Minimum, Maximum, Step, Digits, Unit });
        C.ParameterMap(Body, { Entries: [
            Entry('MaxBounces', 'Path bounces', 'Bounces', 1, 4, 1, 0, 'B'), Entry('InitialCandidates', 'Paths per pixel', 'M', 1, 4, 1, 0, ''),
            Entry('SpatialCount', 'Spatial candidates', 'Spatial', 0, 8, 1, 0, ''), Entry('SpatialRadius', 'Spatial radius', 'Radius', 2, 48, 1, 0, 'px'),
            Entry('TemporalTallyLimit', 'Temporal M limit', 'M limit', 1, 40, 1, 0, ''), Entry('IndirectScale', 'Indirect intensity', 'Indirect', 0, 3, 0.05, 2, '×')] });
    }

    function ConstructReflections(Page)
    {
        const Body = ConstructSection(Page, 'reflections', 'Reflections', 'Aperture', 'Reflections',
                                      () => !Settings.Reflection || Settings.Renderer === 'Analytic' ? 'Off'
                                            : Settings.Renderer === 'Rtx' && Settings.RayTracedReflections ? `RTX · ${Settings.ReflectionBounces} B` : 'Distance field');
        C.SwitchRow(Body, Bind('Reflection', { Label: 'Glossy & mirror reflections', Detail: 'The reflection target and the exterior mirror glass' }));
        C.SwitchRow(Body, Bind('RayTracedReflections', { Label: 'RTX reflection rays', Detail: 'Exact BVH triangle hits instead of distance-field sphere tracing' }));
        C.SliderRow(Body, Bind('ReflectionBounces', { Label: 'Reflection bounces', Minimum: 1, Maximum: 4, Step: 1, Unit: 'B', Digits: 0,
                                                      Labels: ['Single bounce', '4 bounces'] }));
        C.SliderRow(Body, Bind('ReflectionScale', { Label: 'Reflection intensity', Minimum: 0, Maximum: 3, Step: 0.05, Unit: '×', Digits: 2 }));
        C.SliderRow(Body, Bind('ReflectionRoughness', { Label: 'Roughness cutoff', Minimum: 0.15, Maximum: 1, Step: 0.05, Digits: 2,
                                                        Labels: ['Mirror only', 'All surfaces'] }));
        ConstructRoughnessDiagram(Body);
        SubHeading(Body, 'Measured refinements');
        Caption(Body, 'Beyond the demo, measured in Exhibits/RayTracedLighting. The half-resolution trace and the glossy filter’s ±16 px '
                    + 'reach soften every reflection; the mirror filter keeps smooth surfaces sharp.');
        C.SwitchRow(Body, Bind('MirrorFilter', { Label: 'Mirror filter', Tags: Recommended,
                                                 Detail: 'Smooth surfaces take the unfiltered reflection · pane noise from 2.70 to 1.58 RMS' }));
        C.SwitchRow(Body, Bind('MirrorTrace', { Label: 'Mirror trace', Tags: [['Costly', 'warm']],
                                                Detail: 'Smooth surfaces traced at screen resolution · then 1.36 RMS' }));
    }

    function ConstructShadows(Page)
    {
        const Body = ConstructSection(Page, 'shadows', 'Sun & shadows', 'Sun', 'Shadows',
                                      () => !Settings.DirectShadow ? 'Off' : Settings.Renderer === 'Rtx' && Settings.RayTracedShadows ? 'Ray-traced' : 'Cascades');
        C.SwitchRow(Body, Bind('DirectShadow', { Label: 'Sun shadows', Detail: 'Ray-traced BVH cone or cascaded shadow depth' }));
        C.SwitchRow(Body, Bind('RayTracedShadows', { Label: 'Ray-traced shadows', Detail: 'Exact BVH visibility for the sun cone, street lamps and headlights' }));
        C.SwitchRow(Body, Bind('FireflyFilter', { Label: 'Firefly & boiling suppression', Detail: 'Jacobian and luminance outlier clamp' }));
        SubHeading(Body, 'Measured refinements');
        C.SwitchRow(Body, Bind('SolarDisc', { Label: 'Solar disc', Tags: Recommended,
                                              Detail: 'The sun shaded as a disc, never doubled in reflection rays · glint errors from 572 to 56' }));
        C.SwitchRow(Body, Bind('StratifiedSun', { Label: 'Stratified sun', Tags: Recommended, Detail: 'Sun-disc samples stratified over 4 × 4 pixel blocks' }));
        C.SwitchRow(Body, Bind('PenumbraFilter', { Label: 'Penumbra filter', Tags: Recommended,
                                                   Detail: 'Ray-traced verdicts filtered across the penumbra · visible errors from 928 to 615, flicker −30 %' }));
        const Apply = C.Construct('button', 'pill-button accent', [C.ConstructGlyph('Check'), C.Construct('span', null, 'Apply the measured recommendation')]);
        Apply.type = 'button';
        Apply.addEventListener('click', () => { Object.assign(Settings, Catalog.MeasuredRecommendation); C.Broadcast(); C.Toast('Solar disc, stratified sun, penumbra and mirror filters on'); });
        Body.append(C.Construct('div', 'action-row', Apply));
    }

    function ConstructDenoising(Page)
    {
        const Body = ConstructSection(Page, 'denoising', 'Denoising & antialiasing', 'Layers', 'Denoise',
                                      () => Settings.VarianceFilter && Settings.Renderer !== 'Analytic' ? `SVGF ${Settings.DiffuseWaveletLevels} · ${Settings.GlossyWaveletLevels} levels` : 'Off');
        C.SwitchRow(Body, Bind('VarianceFilter', { Label: 'Denoiser (SVGF)', Detail: 'Temporal moments, then à-trous wavelet iterations per lobe' }));
        // 📝 VarianceIntegrator dispatches min(4, WaveletIterations + 1) 5 × 5 à-trous levels; the last reaches 2 · 2^(n−1) trace
        //    pixels, on screen × the trace scale (the glossy filter's ±16 px at the demo's 3 levels and half-resolution trace).
        const Reach = Levels => 2 * Math.pow(2, Levels - 1) * Settings.TraceScale;
        for (const [Lobe, Title] of [['Diffuse', 'Diffuse filter'], ['Glossy', 'Glossy filter']])
        {
            const Card = C.Construct('div', 'sub-card');
            const Small = C.Construct('small');
            Card.append(C.Construct('div', 'sub-card-title', [C.Construct('span', null, Title), Small]));
            C.Register(() => { Small.textContent = `reach ±${Reach(Settings[Lobe + 'WaveletLevels'])} screen px`; });
            C.SliderRow(Card, Bind(Lobe + 'WaveletLevels', { Label: 'À-trous wavelet levels', Minimum: 1, Maximum: 4, Step: 1, Digits: 0 }));
            C.SliderRow(Card, Bind(Lobe + 'TallyLimit', { Label: 'History limit', Minimum: 1, Maximum: 64, Step: 1, Digits: 0, Labels: ['Responsive', 'Stable'] }));
            C.SliderRow(Card, Bind(Lobe + 'MinimumWeight', { Label: 'Minimum blend weight', Minimum: 0.01, Maximum: 0.5, Step: 0.01, Digits: 2 }));
            C.SliderRow(Card, Bind(Lobe + 'ColourSigma', { Label: 'Colour edge stop', Minimum: 0.5, Maximum: 16, Step: 0.5, Unit: 'σ', Digits: 1 }));
            Body.append(Card);
        }
        C.SwitchRow(Body, Bind('Antialias', { Label: 'Temporal antialiasing', Detail: 'Sub-pixel jitter resolved over 16 refreshes; also resolves dithered tyre marks' }));
    }

    function ConstructExposure(Page)
    {
        const Body = ConstructSection(Page, 'exposure', 'Exposure', 'Gauge', 'Exposure',
                                      () => `${Settings.AutoExposure ? 'Auto · ' : ''}${Settings.ExposureCompensation >= 0 ? '+' : ''}${Settings.ExposureCompensation.toFixed(2)} EV`);
        C.SwitchRow(Body, Bind('AutoExposure', { Label: 'Auto exposure', Detail: 'Eye adaptation to the scene’s luminance' }));
        C.SliderRow(Body, Bind('ExposureCompensation', { Label: 'Exposure compensation', Minimum: -4, Maximum: 4, Step: 0.01, Unit: 'EV', Digits: 2,
                                                         Labels: ['Darker', 'Brighter'] }));
    }

    function ConstructGeometry(Page)
    {
        const Body = ConstructSection(Page, 'geometry', 'Geometry', 'Boxes', 'Geometry',
                                      () => Settings.VirtualizedGeometry ? 'Virtualized' : Settings.ClusteredGeometry ? 'Clusters' : 'Direct');
        C.SwitchRow(Body, Bind('VirtualizedGeometry', { Label: 'Virtualized geometry', Detail: 'Cluster nesting of eligible static sets · recording calls from 6,452 to 10' }));
        C.SliderRow(Body, Bind('VirtualizedStreamSlots', { Label: 'Stream slots · 0 = one per page', Minimum: 0, Maximum: 64, Step: 1, Digits: 0 }));
        C.SwitchRow(Body, Bind('ClusteredGeometry', { Label: 'GPU cluster culling', Detail: 'Lossless clusters culled on the GPU into indirect draws' }));
        C.SegmentedRow(Body, Bind('ClusterTriangles', { Label: 'Maximum triangles per leaf', Options: [['64', 64], ['128', 128], ['256', 256]],
                                                        Tags: [['Rebuilds clusters', 'muted']] }));
        C.SwitchRow(Body, Bind('ClusterBounds', { Label: 'Cluster bounds overlay', Detail: 'Green: GPU-visible · red: GPU-culled · grey: pending' }));
        const Rebuild = C.Construct('button', 'pill-button', [C.ConstructGlyph('Reset'), C.Construct('span', null, 'Rebuild clusters')]);
        Rebuild.type = 'button';
        Rebuild.addEventListener('click', () => { AppendLog('RENDERING', 'Clusters rebuilt', `${Settings.ClusterTriangles} triangles per leaf`, 'warm'); C.Toast('Clusters rebuilt'); });
        Body.append(C.Construct('div', 'action-row', Rebuild));
        ConstructStatGrid(Body, [['TLAS instances', '504'], ['TLAS entries', '1,007'], ['BLAS triangles', '20,878'],
                                 ['Occupants', '2,094'], ['Surface cards', '8,554'], ['Lamps', '30']]);
    }

    function ConstructDistanceField(Page)
    {
        const Body = ConstructSection(Page, 'field', 'Distance field & BVH', 'Lattice', 'Field',
                                      () => `${Catalog.QualityGrades[Settings.QualityTier].ClipResolution}³ · 4 cascades`);
        Caption(Body, 'Four toroidal clipmaps follow the camera; the quality grade sets their resolution and finest voxel. The two-level '
                    + 'BVH (TLAS over the occupants, a binned-SAH BLAS per shape) carries the RTX rays.');
        ConstructCascadeDiagram(Body);
        C.SwitchRow(Body, Bind('FreezeDistance', { Label: 'Freeze field updates', Detail: 'Stop rewriting bricks to inspect one fixed field' }));
    }

    function ConstructViews(Page)
    {
        const Body = ConstructSection(Page, 'views', 'Debug views', 'Bug', 'Views', () => Catalog.InspectionViews[Settings.Inspection].Short);
        for (const Group of ['Lighting', 'Scene', 'Clusters'])
        {
            Body.append(C.Construct('div', 'view-group-title', Group));
            const Grid = C.Construct('div', 'view-grid');
            for (const View of Catalog.InspectionViews.filter(Entry => Entry.Group === Group))
            {
                const Badge = C.Construct('span', 'badge');
                const Thumb = C.Construct('div', 'thumb', [C.Construct('div', 'placeholder', C.ConstructGlyph('Eye')), C.Construct('span', 'index', String(View.Index)), Badge]);
                const Picture = C.Construct('img');
                Picture.alt = View.Name;
                Picture.addEventListener('load', () => { Thumb.querySelector('.placeholder')?.remove(); Picture.style.display = ''; });
                Picture.addEventListener('error', () => { Picture.style.display = 'none'; });
                Thumb.prepend(Picture);
                const Tile = C.Construct('div', 'view-tile', [Thumb, C.Construct('div', 'view-name', View.Name)]);
                Tile.addEventListener('click', () => { Settings.Inspection = View.Index; C.Broadcast(); });
                C.Register(() =>
                {
                    Tile.classList.toggle('active', Settings.Inspection === View.Index);
                    const Path = ResolveViewPath(View.Index);
                    if (Picture.getAttribute('src') !== Path) Picture.setAttribute('src', Path);
                    const Note = ResolveViewNote(View);
                    Badge.textContent = Note;
                    Badge.style.display = Note ? '' : 'none';
                });
                Grid.append(Tile);
            }
            Body.append(Grid);
        }
        C.SegmentedRow(Body, Bind('DistanceInspection', { Label: 'BVH / SDF view', Options: Catalog.DistanceViews }));
        C.SwitchRow(Body, Bind('HudVisible', { Label: 'Readout overlay', Detail: 'Renderer, extents and BVH counts over the viewport · H' }));
        C.SwitchRow(Body, Bind('InsetEnabled', { Label: 'Distance slice inset', Detail: 'A horizontal slice of the field · J' }));
        C.SliderRow(Body, Bind('SliceHeight', { Label: 'Slice height', Minimum: 0.05, Maximum: 4, Step: 0.05, Unit: 'm', Digits: 2 }));
    }

    function ConstructTiming(Page)
    {
        const Body = ConstructSection(Page, 'timing', 'Refresh timing', 'Activity', 'Timing',
                                      () => `${Catalog.EstimateSubmissions(Settings, ResolveViewportExtents()).Total.toFixed(1)} ms`);
        Caption(Body, 'Prototype estimate from the settings. In the editor these rows are the measured submission times of the last refresh.');
        C.SwitchRow(Body, Bind('LiveRefresh', { Label: 'Live refresh', Detail: 'Pause to compare one still image against a change (A/B)' }));
        // 📝 The demo's session recorder: the refresh times while recording, exported as CSV (refresh, milliseconds).
        const Recorded = [];
        let   Recording = false;
        const RecordLabel = C.Construct('span', null, 'Record session');
        const Record = C.Construct('button', 'pill-button', [C.ConstructGlyph('Activity'), RecordLabel]);
        const Export = C.Construct('button', 'pill-button', [C.ConstructGlyph('Copy'), C.Construct('span', null, 'Copy CSV')]);
        Record.type = Export.type = 'button';
        Record.addEventListener('click', () =>
        {
            Recording = !Recording;
            RecordLabel.textContent = Recording ? 'Pause recording' : `Record session · ${Recorded.length}`;
            Record.classList.toggle('accent', Recording);
        });
        Export.addEventListener('click', () =>
        {
            const Text = ['refresh,milliseconds', ...Recorded.map((Milliseconds, Index) => `${Index},${Milliseconds.toFixed(3)}`)].join('\n');
            (navigator.clipboard ? navigator.clipboard.writeText(Text) : Promise.reject()).then(() => C.Toast(`${Recorded.length} refreshes copied as CSV`),
                                                                                              () => C.Toast('Copy unavailable here'));
        });
        Body.append(C.Construct('div', 'action-row', [Record, Export]));
        LiveRefreshers.push(() => { if (Recording && Samples.length) { Recorded.push(Samples[Samples.length - 1]); RecordLabel.textContent = `Pause recording · ${Recorded.length}`; } });
        const List = C.Construct('div', 'timing-list');
        Body.append(List);
        C.Register(() =>
        {
            const Estimate = Catalog.EstimateSubmissions(Settings, ResolveViewportExtents());
            const Largest = Math.max(...Estimate.Submissions.map(Submission => Submission.Milliseconds));
            List.textContent = '';
            for (const Submission of [...Estimate.Submissions].sort((A, B) => B.Milliseconds - A.Milliseconds))
            {
                const Colour = Catalog.GroupColours[Submission.Group];
                const Dot = C.Construct('span', 'dot');
                Dot.style.background = Colour;
                const Bar = C.Construct('i');
                Bar.style.width = (100 * Submission.Milliseconds / Largest).toFixed(1) + '%';
                Bar.style.background = Colour;
                List.append(C.Construct('div', 'timing-row', [
                    C.Construct('div', 'timing-top', [Dot, C.Construct('span', null, Submission.Name), C.Construct('span', 'spacer'),
                                                     C.Construct('span', 'ms', Submission.Milliseconds.toFixed(2) + ' ms'),
                                                     C.Construct('span', 'share', (100 * Submission.Milliseconds / Estimate.Total).toFixed(0) + ' %')]),
                    C.Construct('div', 'timing-bar', Bar)]));
            }
        });
    }

    function ConstructLevelspaceBlock(Page)
    {
        Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Levelspace record'),
                                                            C.Construct('div', 'caption', 'Replaces five records')]));
        const Block = C.Construct('pre', 'levelspace');
        const Reset = C.Construct('button', 'pill-button', [C.ConstructGlyph('Reset'), C.Construct('span', null, 'Reset to the demo')]);
        const Copy = C.Construct('button', 'pill-button', [C.ConstructGlyph('Copy'), C.Construct('span', null, 'Copy record')]);
        Reset.type = Copy.type = 'button';
        Reset.addEventListener('click', () =>
        {
            const TargetRate = Settings.TargetRate;
            Object.assign(Settings, JSON.parse(JSON.stringify(Catalog.DemoDefaults)), { TargetRate });
            C.Broadcast();
            C.Toast('Demo defaults restored');
        });
        Copy.addEventListener('click', () =>
        {
            const Text = Catalog.EncodeLevelspace(Settings).join('\n');
            (navigator.clipboard ? navigator.clipboard.writeText(Text) : Promise.reject()).then(() => C.Toast('Levelspace entry copied'), () => C.Toast('Copy unavailable here'));
        });
        Page.append(C.Construct('div', 'action-row', [Reset, Copy]), Block);
        C.Register(() =>
        {
            Block.textContent = '';
            for (const Line of Catalog.EncodeLevelspace(Settings))
            {
                const Split = Line.indexOf(' = ');
                if (Split < 0 || Line.startsWith('identity') || Line.startsWith('type') || Line.startsWith('name')) { Block.append(Line + '\n'); continue; }
                const Key = Line.slice(0, Split);
                Block.append(C.Construct('span', 'key', Key), Line.slice(Split));
                if (!Catalog.ExistingKeys.has(Key) && Key !== 'enabled') Block.append(C.Construct('span', 'comment', '   # proposed'));
                Block.append('\n');
            }
        });
        Page.append(C.Construct('p', 'footer-note', 'Keys marked “proposed” are new for the C++ step; the others are already read by SceneWorld.cpp. '
                                                    + 'Ray counts are exact upper bounds from the settings; timings are estimates until the editor measures them.'));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                   INSPECTOR ASSEMBLY
    //------------------------------------------------------------------------------------------------------------------------

    function RevealSection(Id)
    {
        const Entry = Sections.find(Section => Section.Id === Id);
        if (!Entry || !InspectorScroller) return;
        Entry.Card.classList.remove('folded');
        InspectorScroller.ScrollTo(Entry.Card.offsetTop - 52);
    }

    function ConstructSectionTabs(Page)
    {
        const Bar = C.Construct('div', 'section-tabs');
        Page.append(Bar);
        return () =>
        {
            for (const Entry of Sections)
            {
                const Tab = C.Construct('button', 'section-tab', Entry.Tab);
                Tab.type = 'button';
                Tab.dataset.section = Entry.Id;
                Tab.addEventListener('click', () => RevealSection(Entry.Id));
                Bar.append(Tab);
            }
            const Scroller = document.getElementById('InspectorScroll');
            const Spy = () =>
            {
                let Current = Sections[0]?.Id;
                for (const Entry of Sections) if (Entry.Card.offsetTop - 70 <= Scroller.scrollTop) Current = Entry.Id;
                Bar.querySelectorAll('.section-tab').forEach(Tab =>
                {
                    const Active = Tab.dataset.section === Current;
                    // 📝 Only the strip scrolls sideways; scrollIntoView would also move the inspector itself.
                    if (Active && !Tab.classList.contains('active'))
                        Bar.scrollTo({ left: Tab.offsetLeft - (Bar.clientWidth - Tab.clientWidth) / 2, behavior: 'smooth' });
                    Tab.classList.toggle('active', Active);
                });
            };
            Scroller.addEventListener('scroll', Spy);
            Spy();
        };
    }

    function ConstructInspectorPage()
    {
        const Content = document.getElementById('InspectorContent');
        Content.textContent = '';
        Sections.length = 0;
        const Page = C.Construct('div', 'inspector');
        Content.append(Page);
        Page.append(C.Construct('div', 'breadcrumb-row', [C.Construct('div', 'breadcrumb', [document.createTextNode('Inspector / '), C.Construct('b', null, 'Rendering')]),
                                                           C.Construct('button', 'notes-pill', [C.ConstructGlyph('Plus'), C.Construct('span', null, 'Add notes')])]));
        if (SelectedEntry !== 'render')
        {
            Page.append(C.Construct('div', 'empty-inspector', [C.Construct('h3', null, 'Outside this prototype'),
                document.createTextNode('This record keeps its existing C++ inspector. The prototype covers the one Render settings component; select it in the Outliner.')]));
            return;
        }
        Page.append(C.Construct('div', 'page-title', [C.Construct('h2', null, 'Render settings'), C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, 'LIVE')])]),
                    C.Construct('div', 'page-subtitle', 'The renderer as one component: global illumination, ray tracing, reflections, resolution, geometry and debug views.'));
        ConstructHero(Page);
        ConstructOverview(Page);
        const ConstructTabs = ConstructSectionTabs(Page);
        ConstructResolution(Page);
        ConstructIllumination(Page);
        ConstructReflections(Page);
        ConstructShadows(Page);
        ConstructDenoising(Page);
        ConstructExposure(Page);
        ConstructGeometry(Page);
        ConstructDistanceField(Page);
        ConstructViews(Page);
        ConstructTiming(Page);
        ConstructLevelspaceBlock(Page);
        ConstructTabs();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     INITIALIZATION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 A refresh-time sample every 100 ms around the estimate (±3 %, rare 1.5× spikes), so the chart moves as the editor's will.
    function AdvanceSamples()
    {
        const Estimate = Catalog.EstimateSubmissions(Settings, ResolveViewportExtents()).Total;
        const Gaussian = (Math.random() + Math.random() + Math.random() - 1.5) / 1.5;
        const Spike = Math.random() < 0.02 ? 1.35 + Math.random() * 0.3 : 1;
        Samples.push(Math.max(0.5, Estimate * (1 + 0.03 * Gaussian) * Spike));
        if (Samples.length > SampleCapacity) Samples.shift();
    }

    function Initialize()
    {
        for (let Index = 0; Index < SampleCapacity; ++Index) AdvanceSamples();
        ConstructOutliner(document.getElementById('OutlinerContent'));
        C.SmoothScroll(document.getElementById('OutlinerScroll'), document.getElementById('OutlinerContent'));
        InspectorScroller = C.SmoothScroll(document.getElementById('InspectorScroll'), document.getElementById('InspectorContent'));
        ConstructViewport();
        ConstructInspectorPage();
        ConstructLogStream();
        document.getElementById('OutlinerStatus').append(
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Records'), C.Construct('b', null, '16')]),
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Selected'), C.Construct('b', null, '1')]));
        const InspectorStatus = document.getElementById('InspectorStatus');
        const Revision = C.Construct('b', null, '0');
        let RevisionCount = 0, LastEncoding = JSON.stringify(Settings);
        InspectorStatus.append(C.Construct('div', 'status-cell', [C.Construct('span', null, 'Parameters'), C.Construct('b', null, 'Editing')]),
                               C.Construct('span', 'spacer'), C.Construct('div', 'status-cell', [C.Construct('span', null, 'Revision'), Revision]));
        // 📝 One revision per settings change, as RenderPipelineConfiguration::AdvanceRevision in C++.
        C.Register(() =>
        {
            const Encoding = JSON.stringify(Settings);
            if (Encoding !== LastEncoding) { LastEncoding = Encoding; Revision.textContent = String(++RevisionCount); }
        });
        document.addEventListener('keydown', Event =>
        {
            if (Event.target instanceof HTMLInputElement) return;
            if (Event.key === 'h' || Event.key === 'H') { Settings.HudVisible = !Settings.HudVisible; C.Broadcast(); }
            if (Event.key === 'j' || Event.key === 'J') { Settings.InsetEnabled = !Settings.InsetEnabled; C.Broadcast(); }
        });
        C.Broadcast();
        setInterval(() => { if (!Settings.LiveRefresh) return; AdvanceSamples(); LiveRefreshers.forEach(Refresh => Refresh()); }, 100);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', Initialize);
    else Initialize();
})();
