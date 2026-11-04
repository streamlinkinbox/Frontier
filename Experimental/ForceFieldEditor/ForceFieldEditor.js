/*==========================================================================================================================================
                                                           FORCEFIELDEDITOR.JS
==========================================================================================================================================*/
// 🧩 The Force Field editor prototype: outliner, 100 m – 100 km field viewport with timeline, log stream and the three inspector pages.
'use strict';

(() =>
{
    const C = InspectorControls;
    const V = ForceFieldVisualization;
    const F = ForceFieldCatalogue;
    const StorageKey = 'frontier-force-field-editor-v1';
    const SampleCapacity = 40;

    const Scene = RestoreScene();
    const Clock = { Time: 0, Playing: true, LastTick: 0 };
    const PeakSamples = [], WindSamples = [];
    const LiveRefreshers = [];
    const PersistentRefreshers = [];
    const Sections = [];
    let Selected = 'Field';
    let SceneRevision = 0, PreparedRevision = -1;
    let Prepared = F.Prepare(Scene, 0);
    let Visualization = null, EddyPreview = null;
    let PageScope = null, OutlinerScope = null, InspectorScroller = null;
    let HoverPoint = null, OutlinerQuery = '';

    //------------------------------------------------------------------------------------------------------------------------
    //                                                   SCENE AND STORAGE
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The authored field persists in localStorage, merged over the defaults so a newer prototype still opens an older save.
    function RestoreScene()
    {
        const Defaults = F.ConstructDefaultField();
        try
        {
            const Stored = JSON.parse(localStorage.getItem(StorageKey) || 'null');
            if (!Stored || !Array.isArray(Stored.Sources)) return Defaults;
            return {
                Wind: Object.assign(Defaults.Wind, Stored.Wind),
                Field: Object.assign(Defaults.Field, Stored.Field),
                Sources: Stored.Sources.filter(Source => F.TypeById[Source.Type]).map(Source => F.ConstructSource(Source.Type, Source)),
                Visualization: Object.assign(Defaults.Visualization, Stored.Visualization, { Solo: null }),
                Timeline: Object.assign(Defaults.Timeline, Stored.Timeline)
            };
        }
        catch (Trouble) { return Defaults; }
    }

    let PersistTimer = 0;
    function Persist()
    {
        clearTimeout(PersistTimer);
        PersistTimer = setTimeout(() => { try { localStorage.setItem(StorageKey, JSON.stringify(Scene)); } catch (Trouble) { /* private mode */ } }, 300);
    }

    function ResetScene()
    {
        const Fresh = F.ConstructDefaultField();
        Object.assign(Scene, Fresh);
        Selected = 'Field';
        Clock.Time = 0;
        ConstructOutliner();
        ConstructInspectorPage();
        ConstructTimeline();
        C.Broadcast();
        AppendLog('FORCES', 'Force field reset', 'demo field restored', 'warm');
    }

    const SourceByToken = Token => Scene.Sources.find(Source => Source.Token === Token) || null;

    //------------------------------------------------------------------------------------------------------------------------
    //                                                      FORMATTING
    //------------------------------------------------------------------------------------------------------------------------

    const FormatDistance = V.FormatDistance;
    const FormatCount = Count => Count >= 1e6 ? (Count / 1e6).toFixed(1) + ' M' : Count >= 1e4 ? (Count / 1e3).toFixed(0) + ' k' : Count.toLocaleString('en-US');
    const FormatBytes = Bytes => Bytes >= 1048576 ? (Bytes / 1048576).toFixed(1) + ' MB' : (Bytes / 1024).toFixed(0) + ' KB';
    const Wrap360 = Degrees => ((Degrees % 360) + 360) % 360;

    // 📝 Distance sliders read in m below a kilometre and in km above; typing "2.5 km" or "800 m" works in either unit.
    const DistanceFormat = Quantity => Quantity >= 1000 ? [(Quantity / 1000).toFixed(Quantity >= 10000 ? 1 : 2), 'km'] : [Quantity.toFixed(Quantity < 10 ? 1 : 0), 'm'];
    function DistanceParse(Text, Unit)
    {
        const Match = /(-?[\d.,]+)\s*(km|m)?/i.exec(Text.trim());
        if (!Match) return NaN;
        const Reading = parseFloat(Match[1].replace(',', '.'));
        const Suffix = (Match[2] || Unit || 'm').toLowerCase();
        return Reading * (Suffix === 'km' ? 1000 : 1);
    }

    const ContributionWord = { Flow: 'flow', Accelerate: 'acceleration', Damp: 'damping' };

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       BINDINGS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 One binding shape for every row: read and write a key on a target, and log the edit (consecutive writes to one key
    //    collapse into a single Log Stream row, as a drag writes many times).
    function Bind(Target, Key, Extra, Channel, Owner)
    {
        const Label = Extra.Label || Key;
        return Object.assign({
            Read: () => Target[Key],
            Write: Quantity =>
            {
                Target[Key] = Quantity;
                const Shown = typeof Quantity === 'boolean' ? (Quantity ? 'on' : 'off')
                            : typeof Quantity === 'number' ? (Extra.Distance ? FormatDistance(Quantity) : `${Number(Quantity.toFixed(Extra.Digits ?? 2))}${Extra.Unit ? ' ' + Extra.Unit : ''}`)
                            : Array.isArray(Quantity) ? `[${Quantity.map(Part => typeof Part === 'number' ? Number(Part.toFixed(2)) : Part).join(', ')}]` : String(Quantity);
                AppendLog(Channel || 'FORCES', `${Owner || 'Force Field'}: ${Label} ${Shown}`, null, 'live', `${Owner}.${Key}`);
            }
        }, Extra);
    }

    function DistanceBinding(Target, Key, Extra, Channel, Owner)
    {
        return Bind(Target, Key, Object.assign({ Scale: 'Logarithmic', Format: DistanceFormat, Parse: DistanceParse, Step: 1, Distance: true }, Extra), Channel, Owner);
    }

    function ParameterBinding(Source, Parameter)
    {
        const Extra = { Label: Parameter.Label, Minimum: Parameter.Minimum, Maximum: Parameter.Maximum, Step: Parameter.Step, Unit: Parameter.Unit, Digits: Parameter.Digits ?? 1 };
        if (Parameter.Distance) return DistanceBinding(Source, Parameter.Key, Extra, 'FORCES', Source.Name);
        return Bind(Source, Parameter.Key, Extra, 'FORCES', Source.Name);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    VISUALIZATION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The visualization asks for samples; the scene's time-only work (envelopes, moving centres) is prepared once per time.
    function SampleField(Contribution, X, Y, Z, Time, CellSpan, Out)
    {
        if (Prepared.Time !== Time || PreparedRevision !== SceneRevision) { Prepared = F.Prepare(Scene, Time); PreparedRevision = SceneRevision; }
        return F.EvaluatePrepared(Scene, Prepared, Contribution, X, Y, Z, CellSpan, Out, Scene.Visualization.Solo);
    }

    const Radian = Math.PI / 180;

    // 📝 Outlines for every source, coloured by contribution and dimmed when the view shows another contribution.
    function DescribeSources()
    {
        const Shown = Scene.Visualization.Contribution;
        const Now = F.Prepare(Scene, Clock.Time);
        return Now.Entries.map(Entry =>
        {
            const Source = Entry.Source;
            const Decoration = { Id: Source.Token, X: Entry.Centre[0], Y: Entry.Centre[1], Z: Entry.Centre[2], Contribution: Entry.Contribution,
                                 Label: Source.Name, Active: Entry.Envelope, Selected: Selected === Source.Token, Dim: Entry.Contribution !== Shown,
                                 Shape: 'Circle', Radius: Source.Radius };
            if (Source.Type === 'GustFront')
            {
                const Turn = Source.Bearing * Radian;
                Object.assign(Decoration, { Shape: 'Box', HalfX: Source.HalfWidth, HalfZ: Source.CrestDepth * 1.3, Rotation: Source.Bearing, Bearing: Source.Bearing,
                                            OutlineX: Entry.Centre[0] - Math.sin(Turn) * Source.CrestDepth * 1.05, OutlineZ: Entry.Centre[2] + Math.cos(Turn) * Source.CrestDepth * 1.05 });
            }
            else if (Source.Reach === 'Box')
            {
                const Extents = F.ResolveBoxExtents(Source), Turn = (Source.Bearing || 0) * Radian;
                Object.assign(Decoration, { Shape: 'Box', HalfX: Extents.HalfX, HalfZ: Extents.HalfZ, Rotation: Source.Bearing || 0,
                                            OutlineX: Entry.Centre[0] + Math.sin(Turn) * Extents.OffsetZ, OutlineZ: Entry.Centre[2] - Math.cos(Turn) * Extents.OffsetZ,
                                            Bearing: Source.Type === 'Drag' ? undefined : Source.Bearing });
            }
            else if (Source.Reach === 'Everywhere') Decoration.Shape = 'Everywhere';
            if (Source.Type === 'Blast' && Entry.Envelope > 0.001) Decoration.Radius = Math.min(Source.Radius, Source.InitialRadius + Source.Expansion * Entry.Age);
            if (Source.Type === 'Tornado' || Source.Type === 'Thermal' || Source.Type === 'Microburst') { Decoration.InnerRadius = Source.InnerRadius; Decoration.Height = Source.Height; }
            if (Source.Type === 'Puff') Decoration.Bearing = Source.Bearing;
            if (Source.Type === 'Tornado' && Source.TrackSpeed > 0)
            {
                const Turn = Source.TrackBearing * Radian, Travel = Source.TrackSpeed * Scene.Timeline.Duration;
                Decoration.Track = [[Source.Centre[0], Source.Centre[2]], [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[2] - Math.cos(Turn) * Travel]];
            }
            if (Source.Type === 'Puff' || Source.Type === 'GustFront')
            {
                Decoration.Track = [[Source.Centre[0], Source.Centre[2]], [Entry.Centre[0], Entry.Centre[2]]];
            }
            return Decoration;
        });
    }

    function ResolveCascades()
    {
        return F.ResolveBudget(Scene).Levels.map((Level, Index) => ({ Extent: Level.Extent, Cell: Level.Cell,
                                                                      Label: `C${Index} · ${FormatDistance(Level.Extent)} · ${FormatDistance(Level.Cell)} cells` }));
    }

    function ApplyVisualization()
    {
        const Settings = Scene.Visualization;
        Visualization.Configure({
            Quantity: Settings.Quantity, Contribution: Settings.Contribution, ScaleMode: Settings.ScaleMode, FixedScale: Settings.FixedScale,
            Heatmap: Settings.Heatmap, HeatmapStyle: Settings.HeatmapStyle, Opacity: Settings.Opacity, Arrows: Settings.Arrows,
            ArrowSpacing: Settings.ArrowSpacing, Tracers: Settings.Tracers, TracerCount: Settings.TracerCount, SliceHeight: Settings.SliceHeight,
            CellPixels: Settings.CellPixels, ShowLabels: Settings.Labels, ShowCascades: Settings.Cascades, Cascades: ResolveCascades(),
            CascadeFocus: [Visualization.View.CentreX, Visualization.View.CentreZ], Sources: DescribeSources()
        });
        if (Visualization.View.Mode !== Settings.Mode) Visualization.View.Mode = Settings.Mode;
        if (Settings.Follow)
        {
            const Entry = F.Prepare(Scene, Clock.Time).Entries.find(One => One.Source.Token === Settings.Follow);
            if (Entry) { Visualization.View.CentreX = Entry.Centre[0]; Visualization.View.CentreZ = Entry.Centre[2]; }
        }
    }

    function ConstructVisualization()
    {
        const Canvas = document.getElementById('ViewportCanvas');
        const Extent = Scene.Field.Extent / 2;
        Visualization = V.Create(Canvas, {
            Sample: SampleField, MinimumSpan: 40, MaximumSpan: 2.4e5,
            OnSourcePick: Token => { if (Selected !== Token) { Selected = Token; ConstructInspectorPage(); C.Broadcast(); } },
            OnSourceMove: (Token, X, Z) =>
            {
                const Source = SourceByToken(Token);
                if (!Source) return;
                const Entry = F.PrepareSource(Source, Scene.Wind, Clock.Time);
                const DriftX = Entry.Centre[0] - Source.Centre[0], DriftZ = Entry.Centre[2] - Source.Centre[2];
                Source.Centre = [Number((X - DriftX).toFixed(2)), Source.Centre[1], Number((Z - DriftZ).toFixed(2))];
                AppendLog('FORCES', `${Source.Name}: moved to ${FormatDistance(Source.Centre[0])}, ${FormatDistance(Source.Centre[2])}`, 'dragged in the viewport', 'live', Token + '.Centre');
                C.Broadcast();
            },
            OnHover: Point => { HoverPoint = Point; },
            OnViewChange: () => {}
        });
        Visualization.Configure({ Extent: { MinimumX: -Extent, MinimumZ: -Extent, MaximumX: Extent, MaximumZ: Extent },
                                  Minimap: document.getElementById('MinimapCanvas') });
        Object.assign(Visualization.View, { Mode: Scene.Visualization.Mode, CentreX: 0, CentreZ: 0, Span: 6000, Yaw: Math.PI / 4, Pitch: 0.62 });
        Visualization.AttachNavigation();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        OUTLINER
    //------------------------------------------------------------------------------------------------------------------------

    function DescribeSourceMeta(Source)
    {
        const Type = F.TypeById[Source.Type];
        const Unit = F.Contributions[Type.Contribution].Unit;
        return `${Type.Contribution} · ${Number(Source.Strength.toFixed(Source.Strength < 10 ? 2 : 1))} ${Unit} · ${Source.Reach === 'Everywhere' ? 'everywhere' : FormatDistance(Source.Radius)}`;
    }

    function ResolveStatus(Source)
    {
        const Entry = Prepared.Entries.find(One => One.Source === Source);
        return Entry ? Entry.Status : F.ResolveActivation(Source, Clock.Time).Status;
    }

    function ConstructTreeRow(Host, Row)
    {
        const Meta = C.Construct('div', 'row-meta');
        const Glyph = C.Construct('div', 'row-glyph', C.ConstructGlyph(Row.Glyph));
        Glyph.style.color = Row.Colour;
        Glyph.style.background = Row.Colour + '1f';
        const Status = C.Construct('span', 'row-status');
        const Toggle = C.Construct('button', 'row-toggle', C.ConstructGlyph('Eye'));
        Toggle.type = 'button';
        Toggle.title = 'Enabled';
        const TreeRow = C.Construct('div', 'tree-row' + (Row.Depth === 1 ? ' nested' : Row.Depth === 2 ? ' depth-2' : ''), [
            Glyph, C.Construct('div', 'row-text', [C.Construct('div', 'row-name', Row.Name()), Meta]), Row.Status ? Status : null, Toggle]);
        TreeRow.addEventListener('click', () => { Selected = Row.Id; ConstructInspectorPage(); C.Broadcast(); });
        Toggle.addEventListener('click', Event => { Event.stopPropagation(); Row.Flip(); C.Broadcast(); });
        C.Register(() =>
        {
            TreeRow.querySelector('.row-name').textContent = Row.Name();
            Meta.textContent = Row.Meta();
            const Enabled = Row.Enabled();
            TreeRow.classList.toggle('selected', Selected === Row.Id);
            TreeRow.classList.toggle('muted-row', !Enabled);
            Toggle.textContent = '';
            Toggle.append(C.ConstructGlyph(Enabled ? 'Eye' : 'EyeOff'));
            const Hidden = OutlinerQuery && !Row.Name().toLowerCase().includes(OutlinerQuery) && Row.Depth > 0 && Row.Id !== 'Wind';
            TreeRow.style.display = Hidden ? 'none' : '';
        });
        if (Row.Status) LiveOutlinerRows.push(() => { const Reading = Row.Status(); Status.textContent = Reading; Status.className = 'row-status ' + Reading.toLowerCase(); });
        Host.append(TreeRow);
    }

    const LiveOutlinerRows = [];

    function ConstructOutliner()
    {
        if (OutlinerScope) C.ReleaseScope(OutlinerScope);
        OutlinerScope = C.OpenScope();
        LiveOutlinerRows.length = 0;
        const Content = document.getElementById('OutlinerContent');
        Content.textContent = '';
        const Outliner = C.Construct('div', 'outliner');
        const Total = () => 2 + Scene.Sources.length;
        const ActiveCount = C.Construct('div', 'number');
        const WaitingCount = C.Construct('div', 'number');
        const Library = C.Construct('div', 'round-button', C.ConstructGlyph('Plus'));
        Library.title = 'Add a force';
        Library.addEventListener('click', OpenLibrary);
        const Search = C.Construct('input');
        Search.placeholder = 'Search';
        Search.addEventListener('input', () => { OutlinerQuery = Search.value.trim().toLowerCase(); C.Broadcast(); });
        const RecordCount = C.Construct('div', 'muted');
        Outliner.append(
            C.Construct('div', 'outliner-heading', [C.Construct('div', null, [C.Construct('h1', null, 'Outliner'), RecordCount]), C.Construct('div', 'spacer'), Library]),
            C.Construct('div', 'count-cards', [
                C.Construct('div', 'count-card', [C.Construct('div', 'chip-glyph live', C.ConstructGlyph('Check')), C.Construct('div', 'label', 'Acting now'), ActiveCount]),
                C.Construct('div', 'count-card', [C.Construct('div', 'chip-glyph dim', C.ConstructGlyph('Clock')), C.Construct('div', 'label', 'Waiting or off'), WaitingCount])]),
            C.Construct('div', 'search-row', [
                C.Construct('label', 'search-pill', [C.ConstructGlyph('Search'), Search, C.Construct('kbd', null, 'Ctrl+F')]),
                C.Construct('div', 'filter-pill', [C.ConstructGlyph('Sliders'), C.Construct('span', null, 'Filter')])]));
        C.Register(() => { RecordCount.textContent = `Default.levelspace · ${Total()} records`; });
        LiveOutlinerRows.push(() =>
        {
            const Active = Scene.Sources.filter(Source => ResolveStatus(Source) === 'Active').length;
            ActiveCount.textContent = String(Active + (Scene.Wind.Enabled ? 1 : 0));
            WaitingCount.textContent = String(Scene.Sources.length - Active);
        });
        const Group = C.Construct('div', 'tree-group');
        Group.append(C.Construct('div', 'tree-group-title', [C.ConstructGlyph('Chevron'), C.Construct('span', null, 'Forces'), C.Construct('span', 'count', String(Total()))]));
        ConstructTreeRow(Group, { Id: 'Field', Name: () => 'Force Field', Glyph: 'Wind', Colour: '#8fc7b8', Depth: 0,
                                  Meta: () => `Master · ${Scene.Sources.length} forces · ${Scene.Field.Cascades} cascades`,
                                  Enabled: () => Scene.Field.Enabled, Flip: () => { Scene.Field.Enabled = !Scene.Field.Enabled; AppendLog('FORCES', `Force Field: ${Scene.Field.Enabled ? 'on' : 'off'}`, null, 'warm'); } });
        ConstructTreeRow(Group, { Id: 'Wind', Name: () => 'Wind', Glyph: 'Wind', Colour: '#9fc7e8', Depth: 1,
                                  Meta: () => `Guest · ${Scene.Wind.Speed.toFixed(1)} m/s toward ${Scene.Wind.Bearing.toFixed(0)}° ${C.ResolveCompassPoint(Scene.Wind.Bearing)}`,
                                  Enabled: () => Scene.Wind.Enabled, Flip: () => { Scene.Wind.Enabled = !Scene.Wind.Enabled; AppendLog('WIND', `Wind: ${Scene.Wind.Enabled ? 'on' : 'off'}`, null, 'warm'); } });
        for (const [Contribution, Title] of [['Flow', 'Flow · m/s'], ['Accelerate', 'Accelerate · m/s²'], ['Damp', 'Damp · 1/s']])
        {
            const Members = Scene.Sources.filter(Source => F.TypeById[Source.Type].Contribution === Contribution);
            if (!Members.length) continue;
            Group.append(C.Construct('div', 'group-note', Title));
            for (const Source of Members)
            {
                const Type = F.TypeById[Source.Type];
                ConstructTreeRow(Group, { Id: Source.Token, Name: () => Source.Name, Glyph: Type.Glyph, Colour: F.Contributions[Contribution].Colour, Depth: 1,
                                          Meta: () => DescribeSourceMeta(Source), Status: () => ResolveStatus(Source),
                                          Enabled: () => Source.Enabled,
                                          Flip: () => { Source.Enabled = !Source.Enabled; AppendLog('FORCES', `${Source.Name}: ${Source.Enabled ? 'enabled' : 'disabled'}`, null, 'warm'); } });
            }
        }
        const Add = C.Construct('button', 'add-row', [C.ConstructGlyph('Plus'), C.Construct('span', null, 'Add a localized force')]);
        Add.type = 'button';
        Add.addEventListener('click', OpenLibrary);
        Group.append(Add);
        Outliner.append(Group);
        Outliner.append(C.Construct('div', 'merge-note', [C.Construct('b', null, 'One master component. '),
            document.createTextNode('Wind is the Force Field’s guest; every localized force sits under it, grouped by what it contributes.')]));
        Content.append(Outliner);
        C.CloseScope(OutlinerScope);
        const Status = document.getElementById('OutlinerStatus');
        Status.textContent = '';
        Status.append(C.Construct('div', 'status-cell', [C.Construct('span', null, 'Records'), C.Construct('b', null, String(Total()))]),
                      C.Construct('div', 'status-cell', [C.Construct('span', null, 'Selected'), C.Construct('b', null, '1')]));
        LiveOutlinerRows.forEach(Refresh => Refresh());
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        LIBRARY
    //------------------------------------------------------------------------------------------------------------------------

    function OpenLibrary()
    {
        const Veil = document.getElementById('LibraryVeil');
        const Sheet = document.getElementById('LibrarySheet');
        Sheet.textContent = '';
        const Close = C.Construct('div', 'round-button', C.ConstructGlyph('Close'));
        Close.addEventListener('click', () => Veil.classList.remove('shown'));
        Sheet.append(C.Construct('div', 'library-head', [
            C.Construct('div', null, [C.Construct('h2', null, 'Add a localized force'),
                                      C.Construct('div', 'muted', 'Sixteen types: the native ForceSourceKind set and the eos force fields, sorted by what they contribute.')]),
            C.Construct('div', 'spacer'), Close]));
        for (const Contribution of ['Flow', 'Accelerate', 'Damp'])
        {
            const Facts = F.Contributions[Contribution];
            Sheet.append(C.Construct('div', 'group-note', `${Facts.Label} · ${Facts.Unit}`));
            const Grid = C.Construct('div', 'library-grid');
            for (const Type of F.Types.filter(One => One.Contribution === Contribution))
            {
                const Glyph = C.Construct('div', 'card-glyph', C.ConstructGlyph(Type.Glyph));
                Glyph.style.color = Facts.Colour;
                Glyph.style.background = Facts.Colour + '1f';
                const Card = C.Construct('button', 'library-card', [
                    C.Construct('div', 'card-top', [Glyph, C.Construct('div', null, [C.Construct('div', 'card-name', Type.Name), C.Construct('div', 'card-native', Type.Native)])]),
                    C.Construct('div', 'card-blurb', Type.Blurb)]);
                Card.type = 'button';
                Card.addEventListener('click', () => { Veil.classList.remove('shown'); AddSource(Type.Id); });
                Grid.append(Card);
            }
            Sheet.append(Grid);
        }
        Veil.classList.add('shown');
        Veil.onclick = Event => { if (Event.target === Veil) Veil.classList.remove('shown'); };
    }

    // 📝 A new force lands at the view centre, sized to a quarter of the view so it is visible at once at any scale.
    function AddSource(TypeId)
    {
        const Type = F.TypeById[TypeId];
        const View = Visualization.View;
        const Same = Scene.Sources.filter(Source => Source.Type === TypeId).length;
        const Source = F.ConstructSource(TypeId, { Name: Same ? `${Type.Name} ${Same + 1}` : Type.Name });
        Source.Centre = [Number(View.CentreX.toFixed(1)), Source.Centre[1], Number(View.CentreZ.toFixed(1))];
        if (Source.Reach !== 'Everywhere' && Source.Type !== 'Wake' && Source.Type !== 'Puff')
        {
            const Scale = Math.max(1, (View.Span * 0.18) / Math.max(1, Source.Radius));
            if (Scale > 3 || Scale < 0.2) for (const Key of ['Radius', 'InnerRadius', 'CollarRadius', 'RingRadius', 'HalfX', 'HalfZ', 'HalfWidth', 'Length', 'CrestDepth'])
                if (typeof Source[Key] === 'number') Source[Key] = Number((Source[Key] * Scale).toPrecision(3));
        }
        if (Source.Activation === 'Window' && Source.Begins > 0) Source.Begins = Number((Clock.Time + 1).toFixed(1));
        Scene.Sources.push(Source);
        Selected = Source.Token;
        AppendLog('FORCES', `Added ${Source.Name}`, `${Type.Contribution} · ${Type.Native} · at ${FormatDistance(Source.Centre[0])}, ${FormatDistance(Source.Centre[2])}`, 'live');
        ConstructOutliner();
        ConstructInspectorPage();
        ConstructTimeline();
        C.Broadcast();
    }

    function RemoveSource(Source)
    {
        Scene.Sources.splice(Scene.Sources.indexOf(Source), 1);
        if (Scene.Visualization.Solo === Source.Token) Scene.Visualization.Solo = null;
        if (Scene.Visualization.Follow === Source.Token) Scene.Visualization.Follow = null;
        Selected = 'Field';
        AppendLog('FORCES', `Removed ${Source.Name}`, null, 'warm');
        ConstructOutliner();
        ConstructInspectorPage();
        ConstructTimeline();
        C.Broadcast();
    }

    function DuplicateSource(Source)
    {
        const Copy = F.ConstructSource(Source.Type, JSON.parse(JSON.stringify(Object.assign({}, Source, { Token: undefined }))));
        Copy.Token = F.ConstructSource(Source.Type).Token;
        Copy.Name = Source.Name + ' copy';
        const Offset = Math.max(20, (Source.Radius || 50) * 0.6);
        Copy.Centre = [Source.Centre[0] + Offset, Source.Centre[1], Source.Centre[2] + Offset];
        Scene.Sources.push(Copy);
        Selected = Copy.Token;
        AppendLog('FORCES', `Duplicated ${Source.Name}`, null, 'live');
        ConstructOutliner();
        ConstructInspectorPage();
        ConstructTimeline();
        C.Broadcast();
    }

    function FireSource(Source)
    {
        Source.Activation = 'Trigger';
        Source.TriggeredAt = Clock.Time;
        if (!Source.Lasts) Source.Lasts = Source.Type === 'Blast' ? 14 : 45;
        AppendLog('TRIGGER', `${Source.Name} fired`, `at ${Clock.Time.toFixed(1)} s · lasts ${Source.Lasts.toFixed(0)} s`, 'warm');
        ConstructTimeline();
        C.Broadcast();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        VIEWPORT
    //------------------------------------------------------------------------------------------------------------------------

    const RangePresets = [['100 m', 100], ['1 km', 1000], ['10 km', 10000], ['100 km', 100000]];

    function ConstructViewport()
    {
        const Bar = document.getElementById('ViewportBar');
        const Modes = C.Construct('div', 'bar-choice');
        for (const [Text, Mode, Glyph] of [['Map', 'Map', 'Map'], ['Oblique', 'Oblique', 'Boxes']])
        {
            const Button = C.Construct('button', null, [C.ConstructGlyph(Glyph), C.Construct('span', null, Text)]);
            Button.type = 'button';
            Button.addEventListener('click', () => { Scene.Visualization.Mode = Mode; AppendLog('VIEW', `View: ${Text}`, Mode === 'Oblique' ? 'right-drag turns and tilts' : 'top-down', 'live', 'View.Mode'); C.Broadcast(); });
            C.Register(() => Button.classList.toggle('active', Scene.Visualization.Mode === Mode));
            Modes.append(Button);
        }
        const Ranges = C.Construct('div', 'bar-choice');
        for (const [Text, Span] of RangePresets)
        {
            const Button = C.Construct('button', null, Text);
            Button.type = 'button';
            Button.addEventListener('click', () => JumpToRange(Span));
            Ranges.append(Button);
            PersistentRefreshers.push(() => { const Ratio = Visualization.View.Span / Span; Button.classList.toggle('active', Ratio > 0.7 && Ratio < 1.45); });
        }
        Bar.append(C.Construct('div', 'scene-glyph', C.ConstructGlyph('Wind')),
                   C.Construct('div', null, [C.Construct('div', 'scene-title', 'Force field'),
                                             C.Construct('div', 'scene-meta', 'Everest · Default.levelspace · 100 m – 100 km')]),
                   C.Construct('div', 'spacer'), C.Construct('div', 'renderer-chip', [C.Construct('i'), C.Construct('span', null, 'ANALYTIC · CPU SAMPLED')]),
                   Modes, Ranges);
        document.getElementById('ViewportCaption').textContent = 'Drag to pan · wheel to zoom · drag a source to move it · right-drag tilts the oblique view';
        const Status = document.getElementById('ViewportStatus');
        const Cost = C.Construct('b');
        Status.append(C.Construct('span', null, 'HTML prototype'), C.Construct('span', null, 'Analytic field · the C++ ForceFieldSpace formulas'),
                      C.Construct('span', 'spacer'), C.Construct('span', null, ['Sampling ', Cost]));
        PersistentRefreshers.push(() => { const Statistics = Visualization.Statistics; Cost.textContent = `${FormatCount(Statistics.Samples)} samples · ${Statistics.Milliseconds.toFixed(1)} ms`; });
        const Label = document.getElementById('MinimapLabel');
        Label.append(C.Construct('span', null, 'Extent'), C.Construct('span', null, FormatDistance(Scene.Field.Extent)));
    }

    function JumpToRange(Span)
    {
        Visualization.View.Span = Span;
        AppendLog('VIEW', `Range: ${FormatDistance(Span)}`, `level of detail ${FormatDistance(Visualization.ResolveCellSpan())} cells`, 'live', 'View.Span');
    }

    function RefreshHud()
    {
        const View = Visualization.View, Statistics = Visualization.Statistics;
        const Hud = document.getElementById('ViewportHud');
        Hud.textContent = '';
        const Level = Math.round(Math.log2(Statistics.CellSpan));
        Hud.append(C.Construct('div', null, [C.Construct('span', null, 'VIEW '), document.createTextNode(View.Mode)]),
                   C.Construct('div', null, [C.Construct('span', null, 'SPAN '), document.createTextNode(FormatDistance(View.Span))]),
                   C.Construct('div', null, [C.Construct('span', null, 'CELL '), document.createTextNode(`${FormatDistance(Statistics.CellSpan)} · LOD ${Level}`)]),
                   C.Construct('div', null, [C.Construct('span', null, 'SLICE '), document.createTextNode(FormatDistance(Scene.Visualization.SliceHeight))]),
                   C.Construct('div', null, [C.Construct('span', null, 't '), document.createTextNode(`${Clock.Time.toFixed(1)} s`)]));
        const Readout = document.getElementById('ViewportReadout');
        if (!HoverPoint) { Readout.classList.remove('shown'); return; }
        const Probe = Visualization.ProbeAt(HoverPoint.ScreenX, HoverPoint.ScreenY, Clock.Time);
        if (!Probe) { Readout.classList.remove('shown'); return; }
        const Unit = Probe.Unit;
        const Lines = [`x ${FormatDistance(Probe.X)}   z ${FormatDistance(Probe.Z)}   y ${FormatDistance(Probe.Y)}`];
        if (Scene.Visualization.Contribution === 'Damp') Lines.push(`damping ${Probe.Vector[0].toFixed(3)} 1/s`);
        else
        {
            Lines.push(`|v| ${Probe.Speed.toFixed(2)} ${Unit}   toward ${Probe.Bearing.toFixed(0)}° ${C.ResolveCompassPoint(Probe.Bearing)}`);
            Lines.push(`v  ${Probe.Vector.map(Part => Part.toFixed(2)).join('  ')}`);
            Lines.push(`ω ${V.FormatReading(Probe.Vorticity)}   ∇·v ${V.FormatReading(Probe.Divergence)}   per s`);
            if (Scene.Visualization.Contribution === 'Flow') Lines.push(`Beaufort ${F.ResolveBeaufort(Probe.Horizontal).Force} · ${F.ResolveBeaufort(Probe.Horizontal).Name}`);
        }
        Readout.textContent = Lines.join('\n');
        Readout.classList.add('shown');
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        TIMELINE
    //------------------------------------------------------------------------------------------------------------------------

    let PlayheadMark = null, ClockReading = null, PlayButton = null;

    // 📝 Lanes show the forces with a time window or a trigger; always-on forces are summarised, not drawn.
    function ConstructTimeline()
    {
        const Strip = document.getElementById('TimelineStrip');
        Strip.textContent = '';
        const Duration = Scene.Timeline.Duration;
        PlayButton = C.Construct('div', 'round-button', C.ConstructGlyph(Clock.Playing ? 'Pause' : 'Play'));
        PlayButton.title = 'Play / pause (Space)';
        PlayButton.addEventListener('click', TogglePlay);
        const Restart = C.Construct('div', 'round-button', C.ConstructGlyph('Reset'));
        Restart.title = 'Back to 0 s';
        Restart.addEventListener('click', () => { Clock.Time = 0; for (const Source of Scene.Sources) if (Source.Activation === 'Trigger') Source.TriggeredAt = null; ConstructTimeline(); C.Broadcast(); });
        ClockReading = C.Construct('div', 'timeline-clock');
        const Rates = C.Construct('div', 'target-choice');
        for (const Rate of [1, 10, 60])
        {
            const Button = C.Construct('button', null, `×${Rate}`);
            Button.type = 'button';
            Button.classList.toggle('active', Scene.Timeline.Rate === Rate);
            Button.addEventListener('click', () => { Scene.Timeline.Rate = Rate; AppendLog('TIMELINE', `Playback ×${Rate}`, Rate > 1 ? 'large-scale forces visibly travel' : 'real time', 'live', 'Timeline.Rate'); ConstructTimeline(); C.Broadcast(); });
            Rates.append(Button);
        }
        const Always = Scene.Sources.filter(Source => Source.Activation === 'Always' && Source.Enabled).length;
        Strip.append(C.Construct('div', 'timeline-transport', [
            C.Construct('div', 'transport-row', [PlayButton, Restart, ClockReading]),
            C.Construct('div', 'transport-row', [Rates, C.Construct('span', 'timeline-clock', [C.Construct('small', null, `${Always} always on`)])])]));
        const Tracks = C.Construct('div', 'timeline-tracks');
        const Ruler = C.Construct('div', 'timeline-ruler');
        // 📝 The ruler sits in the lanes' track column (after the name column), so ticks, windows and the playhead share one scale.
        const RulerLane = C.Construct('div', 'timeline-lane ruler-lane', [C.Construct('span', 'lane-name', 'Time'), Ruler, C.Construct('span')]);
        const Step = Duration <= 60 ? 10 : Duration <= 180 ? 20 : 60;
        for (let Second = 0; Second <= Duration; Second += Step)
        {
            const Tick = C.Construct('div', 'tick', C.Construct('span', null, `${Second}s`));
            Tick.style.left = `${Second / Duration * 100}%`;
            Ruler.append(Tick);
        }
        const Scrub = Event =>
        {
            const Bounds = Ruler.getBoundingClientRect();
            Clock.Time = Math.min(Duration, Math.max(0, (Event.clientX - Bounds.left) / Bounds.width * Duration));
            Visualization.Invalidate();
        };
        let Scrubbing = false;
        Ruler.addEventListener('pointerdown', Event => { Scrubbing = true; Ruler.setPointerCapture(Event.pointerId); Scrub(Event); });
        Ruler.addEventListener('pointermove', Event => { if (Scrubbing) Scrub(Event); });
        Ruler.addEventListener('pointerup', () => { Scrubbing = false; });
        Tracks.append(RulerLane);
        const Lanes = C.Construct('div', 'timeline-lanes');
        const Timed = Scene.Sources.filter(Source => Source.Activation !== 'Always');
        for (const Source of Timed)
        {
            const Contribution = F.TypeById[Source.Type].Contribution.toLowerCase();
            const Track = C.Construct('div', 'lane-track');
            const Windows = [];
            if (Source.Activation === 'Window')
            {
                const Period = Source.Repeats > 0 && Source.Lasts > 0 ? Source.Repeats : Infinity;
                for (let Start = Source.Begins; Start < Duration && Windows.length < 60; Start += Period)
                {
                    Windows.push([Start, Source.Lasts > 0 ? Math.min(Duration, Start + Source.Lasts) : Duration]);
                    if (!Number.isFinite(Period)) break;
                }
            }
            else if (Source.TriggeredAt !== null && Source.TriggeredAt !== undefined)
                Windows.push([Source.TriggeredAt, Source.Lasts > 0 ? Math.min(Duration, Source.TriggeredAt + Source.Lasts) : Duration]);
            for (const [Start, End] of Windows)
            {
                const Window = C.Construct('i', 'lane-window ' + Contribution);
                Window.style.left = `${Start / Duration * 100}%`;
                Window.style.width = `${Math.max(0.6, (End - Start) / Duration * 100)}%`;
                Track.append(Window);
            }
            const Fire = C.Construct('button', 'lane-fire', Source.Activation === 'Trigger' ? 'FIRE' : '');
            Fire.type = 'button';
            Fire.addEventListener('click', Event => { Event.stopPropagation(); FireSource(Source); });
            const Lane = C.Construct('div', 'timeline-lane' + (Selected === Source.Token ? ' selected' : ''), [C.Construct('span', 'lane-name', Source.Name), Track, Fire]);
            Lane.addEventListener('click', () => { Selected = Source.Token; ConstructInspectorPage(); ConstructTimeline(); C.Broadcast(); });
            Lanes.append(Lane);
        }
        if (!Timed.length) Lanes.append(C.Construct('div', 'timeline-hint', 'Every force is always on. Give one a time window or a trigger to see it here.'));
        Tracks.append(Lanes);
        PlayheadMark = C.Construct('div', 'timeline-playhead');
        Tracks.append(PlayheadMark);
        Strip.append(Tracks);
        RefreshPlayhead();
    }

    function RefreshPlayhead()
    {
        if (!PlayheadMark) return;
        const Duration = Scene.Timeline.Duration;
        const Ruler = PlayheadMark.parentElement.querySelector('.timeline-ruler');
        PlayheadMark.style.left = `${Ruler.offsetLeft + Clock.Time / Duration * Ruler.clientWidth}px`;
        ClockReading.textContent = '';
        ClockReading.append(document.createTextNode(`${Clock.Time.toFixed(1)} s`), C.Construct('small', null, ` / ${Duration} s`));
    }

    function TogglePlay()
    {
        Clock.Playing = !Clock.Playing;
        PlayButton.textContent = '';
        PlayButton.append(C.ConstructGlyph(Clock.Playing ? 'Pause' : 'Play'));
        AppendLog('TIMELINE', Clock.Playing ? 'Playing' : 'Paused', `at ${Clock.Time.toFixed(1)} s`, Clock.Playing ? 'live' : 'warm', 'Timeline.Playing');
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       LOG STREAM
    //------------------------------------------------------------------------------------------------------------------------

    const LogEntries = [];
    let LogPaused = false;

    function AppendLog(Channel, Message, Detail, Tone, Key)
    {
        if (LogPaused) return;
        const Now = new Date();
        const Last = LogEntries[0];
        if (Key && Last && Last.Key === Key && Now - Last.When < 1500) Object.assign(Last, { Message, Detail, When: Now });
        else LogEntries.unshift({ Channel, Message, Detail, Tone, Key, When: Now });
        if (LogEntries.length > 80) LogEntries.pop();
        RefreshLog();
    }

    function RefreshLog()
    {
        const Content = document.getElementById('LogContent');
        if (!Content) return;
        Content.textContent = '';
        for (const Entry of LogEntries)
        {
            Content.append(C.Construct('div', 'log-row', [
                C.Construct('span', 'severity' + (Entry.Tone === 'warm' ? ' warm' : '')), C.Construct('span', 'when', Entry.When.toTimeString().slice(0, 8)),
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
        const Count = C.Construct('span', 'log-count');
        Count.id = 'LogCount';
        Head.append(C.Construct('div', 'log-title', 'Log Stream'), C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, 'LIVE')]),
                    C.Construct('span', 'spacer'), Count, Pause, Clear);
        C.SmoothScroll(document.getElementById('LogScroll'), document.getElementById('LogContent'));
        AppendLog('EVEREST', "Loaded 'Default.levelspace'", `Force Field · Wind (guest) · ${Scene.Sources.length} localized forces`, 'live');
        AppendLog('FORCES', 'Force Field: master component', 'Wind 6.5 m/s toward 45° from the scene record; localized forces from the native and eos sets', 'live');
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                   INSPECTOR HELPERS
    //------------------------------------------------------------------------------------------------------------------------

    function ConstructSection(Page, Id, Title, Glyph, Tab, Status)
    {
        const Section = C.SectionCard(Page, { Id, Title, Glyph, Status });
        Sections.push({ Id, Tab, Card: Section.Card });
        return Section.Inner;
    }

    const Caption = (Host, Text) => Host.append(C.Construct('p', 'section-caption', Text));
    const SubHeading = (Host, Text) => Host.append(C.Construct('div', 'sub-heading', Text));

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
                    if (Active && !Tab.classList.contains('active')) Bar.scrollTo({ left: Tab.offsetLeft - (Bar.clientWidth - Tab.clientWidth) / 2, behavior: 'smooth' });
                    Tab.classList.toggle('active', Active);
                });
            };
            Scroller.onscroll = Spy;
            Spy();
        };
    }

    function ConstructHeading(Page, Trail, Title, Pill, Subtitle)
    {
        Page.append(C.Construct('div', 'breadcrumb-row', [
            C.Construct('div', 'breadcrumb', [document.createTextNode('Inspector / '), C.Construct('b', null, Trail)]),
            C.Construct('button', 'notes-pill', [C.ConstructGlyph('Plus'), C.Construct('span', null, 'Add notes')])]));
        const Heading = C.Construct('h2', null, Title);
        Page.append(C.Construct('div', 'page-title', [Heading, Pill]), C.Construct('div', 'page-subtitle', Subtitle));
        return Heading;
    }

    function ConstructLevelspaceBlock(Page, Only, Title, CaptionText)
    {
        Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', Title), C.Construct('div', 'caption', CaptionText)]));
        const Block = C.Construct('pre', 'levelspace');
        C.ActionRow(Page, [
            { Label: 'Reset to the demo field', Glyph: 'Reset', Act: () => ResetScene() },
            { Label: 'Copy record', Glyph: 'Copy', Act: () =>
                {
                    const Text = F.EncodeLevelspace(Scene, Only).join('\n');
                    (navigator.clipboard ? navigator.clipboard.writeText(Text) : Promise.reject()).then(() => C.Toast('Levelspace record copied'), () => C.Toast('Copy unavailable here'));
                } }]);
        Page.append(Block);
        C.Register(() =>
        {
            Block.textContent = '';
            for (const Line of F.EncodeLevelspace(Scene, Only))
            {
                const Split = Line.indexOf(' = ');
                if (Split < 0) { Block.append(C.Construct('span', 'comment', Line), '\n'); continue; }
                const Key = Line.slice(0, Split);
                Block.append(C.Construct('span', 'key', Key), Line.slice(Split));
                if (!F.ExistingKeys.has(Key)) Block.append(C.Construct('span', 'comment', '   # proposed'));
                Block.append('\n');
            }
        });
        Page.append(C.Construct('p', 'footer-note', 'Keys without “proposed” are read today by SceneWorld.cpp (ForceFieldRecord, WindRecord); the rest are new '
                                                    + 'for the C++ step. Localized forces become ForceSource records nested under the Force Field, as Wind already is.'));
    }

    function ResolveSeriesRefresh(Series)
    {
        LiveRefreshers.push(Series.Refresh);
    }

    // 📝 A statistics grid whose readings also refresh on the live interval (sampling cost changes without any edit).
    function LiveStatGrid(Host, Cells)
    {
        const Grid = C.Construct('div', 'stat-grid');
        for (const [Label, Resolve] of Cells)
        {
            const Reading = C.Construct('b');
            Grid.append(C.Construct('div', 'stat-cell', [Reading, C.Construct('span', null, Label)]));
            const Refresh = () => { Reading.textContent = Resolve(); };
            C.Register(Refresh);
            LiveRefreshers.push(Refresh);
        }
        Host.append(Grid);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                   FORCE FIELD PAGE
    //------------------------------------------------------------------------------------------------------------------------

    const SeriesTargets = { Flow: [17.2, 'Gale'], Accelerate: [9.81, '1 g ·'], Damp: [1, 'Rate'] };

    function ConstructFieldPage(Page)
    {
        const Settings = Scene.Visualization;
        ConstructHeading(Page, 'Forces', 'Force Field', C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, 'LIVE')]),
                         'The master component: one Wind profile (its guest) and every localized force, sampled from 100 m to 100 km.');
        C.PillTabs(Page, { Options: [['Flow · m/s', 'Flow'], ['Accelerate · m/s²', 'Accelerate'], ['Damp · 1/s', 'Damp']],
                           Read: () => Settings.Contribution,
                           Write: Contribution => { Settings.Contribution = Contribution; if (Contribution === 'Damp') Settings.Quantity = 'Speed'; PeakSamples.length = 0;
                                                    AppendLog('VIEW', `Showing ${ContributionWord[Contribution]}`, F.Contributions[Contribution].Unit, 'live', 'View.Contribution'); } });
        const BudgetMark = C.Construct('div', 'mark live', C.ConstructGlyph('Check'));
        const BudgetText = C.Construct('span');
        const Metric = C.Construct('div', 'metric-big');
        const Rate = C.Construct('div', 'rate');
        const Low = C.Construct('div', 'low');
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [BudgetMark, BudgetText]), C.Construct('div', 'estimate-tag', 'Analytic · in view')]),
            C.Construct('div', 'hero-title', 'Peak in view'),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])]);
        Page.append(Card);
        const Series = C.TimeSeries(Card, { Samples: PeakSamples, Capacity: SampleCapacity, Target: () => SeriesTargets[Settings.Contribution][0], Height: 120,
                                            Unit: () => V.ResolveUnit(Settings.Contribution, Settings.Quantity), TargetLabel: () => SeriesTargets[Settings.Contribution][1], Headroom: 1.25, AxisDigits: 1,
                                            TimeLabels: ['−10 s', '−5 s', 'now'] });
        Card.append(C.Construct('div', 'hero-note', 'The peak of the selected quantity across the view at its level of detail. Zoom out and small forces fade into '
                                                    + 'their cells, as a coarse cascade averages them.'));
        const LiveRefresh = () =>
        {
            const Statistics = Visualization.Statistics;
            const Unit = Visualization.Options.Contribution === Settings.Contribution ? V.ResolveUnit(Settings.Contribution, Settings.Quantity) : '';
            C.ConstructMetric(Metric, Statistics.Peak, Statistics.Peak >= 100 ? 0 : 1, Unit);
            Rate.textContent = FormatDistance(Visualization.View.Span);
            Rate.append(C.Construct('small', null, 'span'));
            Low.textContent = `${FormatDistance(Statistics.CellSpan)} cells · LOD ${Math.round(Math.log2(Statistics.CellSpan))}`;
            if (Settings.Contribution === 'Flow' && Settings.Quantity === 'Speed')
            {
                const Beaufort = F.ResolveBeaufort(Statistics.Peak);
                BudgetMark.className = 'mark ' + (Beaufort.Force >= 10 ? 'danger' : Beaufort.Force >= 8 ? 'warm' : 'live');
                BudgetText.textContent = `Beaufort ${Beaufort.Force} · ${Beaufort.Name}`;
            }
            else
            {
                BudgetMark.className = 'mark live';
                const Shown = Settings.Contribution === 'Damp' ? 'Damping rate' : (V.Quantities[Settings.Quantity] || V.Quantities.Speed).Label;
                BudgetText.textContent = `${Shown} · ${Settings.ScaleMode === 'Automatic' ? 'auto scale' : 'fixed scale'}`;
            }
        };
        LiveRefreshers.push(LiveRefresh);
        ResolveSeriesRefresh(Series);
        C.ChipRow(Page, [
            () => [String(Scene.Sources.filter(Source => F.TypeById[Source.Type].Contribution === 'Flow').length), 'flow'],
            () => [String(Scene.Sources.filter(Source => F.TypeById[Source.Type].Contribution === 'Accelerate').length), 'accelerate'],
            () => [String(Scene.Sources.filter(Source => F.TypeById[Source.Type].Contribution === 'Damp').length), 'damp'],
            () => [`${Scene.Field.Cascades}`, 'cascades']]);
        const Tiles = C.Construct('div', 'tile-grid');
        C.ReadoutTile(Tiles, { Label: 'Baked into the lattice', Glyph: 'Lattice', Resolve: () => { const Budget = F.ResolveBudget(Scene); return [String(Budget.Baked.length), `of ${Budget.GpuSlots} slots`]; } });
        C.ReadoutTile(Tiles, { Label: 'Evaluated per receiver', Glyph: 'Target', Resolve: () => { const Budget = F.ResolveBudget(Scene); return [String(Budget.Live.length + Budget.Damp.length), 'fields']; } });
        C.ReadoutTile(Tiles, { Label: 'Cascade memory · proposed', Glyph: 'Layers', Resolve: () => { const Budget = F.ResolveBudget(Scene); return [FormatBytes(Budget.TotalBytes).split(' ')[0], FormatBytes(Budget.TotalBytes).split(' ')[1]]; } });
        C.ReadoutTile(Tiles, { Label: 'Coarsest cascade', Glyph: 'Globe', Resolve: () => { const Levels = F.ResolveBudget(Scene).Levels; const Last = Levels[Levels.length - 1]; return [FormatDistance(Last.Extent).split(' ')[0], FormatDistance(Last.Extent).split(' ')[1]]; } });
        Page.append(Tiles);
        Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Quick controls'), C.Construct('div', 'caption', 'The field and its view')]));
        const Grid = C.Construct('div', 'toggle-grid');
        C.ToggleTile(Grid, Bind(Scene.Wind, 'Enabled', { Label: 'Wind guest', Glyph: 'Wind' }, 'WIND', 'Wind'));
        C.ToggleTile(Grid, Bind(Scene.Field, 'IncludeTurbulence', { Label: 'Eddies', Glyph: 'Swirl' }, 'FORCES', 'Force Field'));
        C.ToggleTile(Grid, Bind(Settings, 'Heatmap', { Label: 'Heatmap', Glyph: 'Layers' }, 'VIEW', 'View'));
        C.ToggleTile(Grid, Bind(Settings, 'Arrows', { Label: 'Arrows', Glyph: 'Move' }, 'VIEW', 'View'));
        C.ToggleTile(Grid, Bind(Settings, 'Tracers', { Label: 'Tracers', Glyph: 'Activity' }, 'VIEW', 'View'));
        C.ToggleTile(Grid, Bind(Settings, 'Cascades', { Label: 'Cascades', Glyph: 'Lattice' }, 'VIEW', 'View'));
        Page.append(Grid);
        const ConstructTabs = ConstructSectionTabs(Page);
        ConstructFieldView(Page);
        ConstructFieldRange(Page);
        ConstructFieldSources(Page);
        ConstructFieldChannels(Page);
        ConstructFieldTimeline(Page);
        ConstructFieldVectors(Page);
        ConstructFieldBudget(Page);
        ConstructLevelspaceBlock(Page, null, 'Levelspace records', 'Force Field · Wind · forces');
        ConstructTabs();
    }

    function ConstructFieldView(Page)
    {
        const Settings = Scene.Visualization;
        const Inner = ConstructSection(Page, 'view', 'View', 'Eye', 'View', () => `${Settings.Mode} · ${(V.Quantities[Settings.Quantity] || V.Quantities.Speed).Short}`);
        Caption(Inner, 'The slice the viewport shows: which quantity, how it is coloured, and how high above the ground it is cut.');
        const Quantity = { Label: 'Quantity', Availability: () => Settings.Contribution === 'Damp' ? { Enabled: false, Reason: 'damping is a rate' } : { Enabled: true } };
        C.SegmentedRow(Inner, Bind(Settings, 'Quantity', Object.assign({ Options: [['Speed', 'Speed'], ['East', 'AlongX'], ['South', 'AlongZ'], ['Up', 'Vertical']] }, Quantity), 'VIEW', 'View'));
        C.SegmentedRow(Inner, Bind(Settings, 'Quantity', Object.assign({ Label: '', Options: [['Vorticity ω', 'Vorticity'], ['Divergence ∇·v', 'Divergence']] }, Quantity, { Label: '' }), 'VIEW', 'View'));
        C.SegmentedRow(Inner, Bind(Settings, 'Mode', { Label: 'Projection', Options: [['Map · top-down', 'Map'], ['Oblique · 3D slice', 'Oblique']] }, 'VIEW', 'View'));
        C.SliderRow(Inner, DistanceBinding(Settings, 'SliceHeight', { Label: 'Slice height above ground', Minimum: 1, Maximum: 8000, Tags: [['Ekman', 'accent']] }, 'VIEW', 'View'));
        C.SegmentedRow(Inner, Bind(Settings, 'HeatmapStyle', { Label: 'Heatmap', Options: [['Smooth', 'Smooth'], ['Cells (eos)', 'Cells']] }, 'VIEW', 'View'));
        C.SegmentedRow(Inner, Bind(Settings, 'ScaleMode', { Label: 'Colour scale', Options: [['Automatic · 98th percentile', 'Automatic'], ['Fixed', 'Fixed']] }, 'VIEW', 'View'));
        C.SliderRow(Inner, Bind(Settings, 'FixedScale', { Label: 'Fixed full scale', Minimum: 0.5, Maximum: 120, Step: 0.5, Digits: 1, Unit: 'm/s',
                                                         Availability: () => Settings.ScaleMode === 'Fixed' ? { Enabled: true } : { Enabled: false, Reason: 'automatic scale' } }, 'VIEW', 'View'));
        C.SliderRow(Inner, Bind(Settings, 'Opacity', { Label: 'Heatmap opacity', Minimum: 0.1, Maximum: 1, Step: 0.05, Digits: 2 }, 'VIEW', 'View'));
        SubHeading(Inner, 'Arrows and tracers');
        C.SliderRow(Inner, Bind(Settings, 'ArrowSpacing', { Label: 'Arrow spacing on screen', Minimum: 22, Maximum: 90, Step: 1, Digits: 0, Unit: 'px' }, 'VIEW', 'View'));
        C.SliderRow(Inner, Bind(Settings, 'TracerCount', { Label: 'Tracers', Minimum: 0, Maximum: 1200, Step: 20, Digits: 0 }, 'VIEW', 'View'));
        C.SliderRow(Inner, Bind(Settings, 'CellPixels', { Label: 'Sample cell on screen', Minimum: 3, Maximum: 14, Step: 1, Digits: 0, Unit: 'px', Labels: ['finer, slower', 'coarser, faster'] }, 'VIEW', 'View'));
        C.SwitchRow(Inner, Bind(Settings, 'Labels', { Label: 'Source labels', Detail: 'Small forces keep their dot and drop their label when the view is wide.' }, 'VIEW', 'View'));
    }

    // 📝 Cascades: c = 0 … n−1 with cells of FinestCell × 8^c over CellsPerSide cells a side, around the camera (proposed).
    function ConstructCascadeDiagram(Host)
    {
        const Height = 178;
        const Panel = C.Construct('div', 'diagram-panel');
        const Svg = C.ConstructSvg('svg', { class: 'diagram', height: Height });
        Panel.append(Svg);
        Host.append(Panel);
        const Refresh = () =>
        {
            const Levels = F.ResolveBudget(Scene).Levels;
            const Width = Math.max(240, Svg.clientWidth || 360);
            const CentreX = Height / 2 + 4, CentreY = Height / 2;
            Svg.textContent = '';
            const Count = Levels.length;
            for (let Index = Count - 1; Index >= 0; --Index)
            {
                // 📝 Drawn on a log scale (each cascade 8× the last cannot be shown at true proportion); labels carry the true sizes.
                const Side = (Height - 12) * Math.pow(0.58, Count - 1 - Index);
                Svg.append(C.ConstructSvg('rect', { x: CentreX - Side / 2, y: CentreY - Side / 2, width: Side, height: Side, rx: 4,
                                                    fill: `rgba(159,180,214,${0.03 + 0.05 * (Count - 1 - Index)})`, stroke: '#9fb4d6',
                                                    'stroke-opacity': 0.3 + 0.15 * (Count - 1 - Index), 'stroke-dasharray': Index === Count - 1 ? '3 4' : '' }));
            }
            Levels.forEach((Level, Index) =>
            {
                const RowY = 28 + Index * (Height - 30) / Math.max(1, Count);
                Svg.append(C.ConstructSvg('text', { x: Height + 18, y: RowY, fill: '#d8d8d8', 'font-size': 12 }, `C${Index} · ${FormatDistance(Level.Extent)}`),
                           C.ConstructSvg('text', { x: Height + 18, y: RowY + 14, fill: '#6e6e6e', 'font-size': 9.5 }, `${FormatDistance(Level.Cell)} cells · refresh 1/${Level.Refresh}`));
            });
            Svg.append(C.ConstructSvg('circle', { cx: CentreX, cy: CentreY, r: 3, fill: '#ffffff' }),
                       C.ConstructSvg('text', { x: Width - 2, y: Height - 4, fill: '#6e6e6e', 'font-size': 9.5, 'text-anchor': 'end' },
                                      `${Scene.Field.CellsPerSide}² × ${Scene.Field.VerticalCells} each · log scale`));
        };
        new ResizeObserver(Refresh).observe(Svg);
        C.Register(Refresh);
    }

    function ConstructFieldRange(Page)
    {
        const Field = Scene.Field;
        const Inner = ConstructSection(Page, 'range', 'Range and detail', 'Globe', 'Range', () => `${FormatDistance(F.ResolveBudget(Scene).Levels[0].Extent)} – ${FormatDistance(F.ResolveBudget(Scene).Levels.slice(-1)[0].Extent)}`);
        Caption(Inner, 'The field reaches from a street to a weather system. The viewport samples it at a cell size that follows the zoom (a power of '
                     + 'two, so panning never shimmers); the engine would hold the flow kinds in cascaded lattices around the camera.');
        C.ActionRow(Inner, RangePresets.map(([Text, Span]) => ({ Label: Text, Glyph: 'Ruler', Act: () => JumpToRange(Span) })));
        ConstructCascadeDiagram(Inner);
        C.SliderRow(Inner, Bind(Field, 'Cascades', { Label: 'Cascades', Minimum: 2, Maximum: 5, Step: 1, Digits: 0, Tags: [['Proposed', 'warm']] }, 'FORCES', 'Force Field'));
        C.SliderRow(Inner, DistanceBinding(Field, 'FinestCell', { Label: 'Finest cell', Minimum: 0.5, Maximum: 8, Tags: [['Proposed', 'warm']] }, 'FORCES', 'Force Field'));
        C.SegmentedRow(Inner, Bind(Field, 'CellsPerSide', { Label: 'Cells per side', Options: [['64', 64], ['128', 128], ['256', 256]] }, 'FORCES', 'Force Field'));
        C.SegmentedRow(Inner, Bind(Field, 'VerticalCells', { Label: 'Vertical cells', Options: [['16', 16], ['32', 32], ['64', 64]] }, 'FORCES', 'Force Field'));
        C.SwitchRow(Inner, Bind(Scene.Visualization, 'Cascades', { Label: 'Show cascades in the viewport', Detail: 'Nested squares around the view centre, labelled with extent and cell size.' }, 'VIEW', 'View'));
        C.StatGrid(Inner, [
            ['Memory', () => FormatBytes(F.ResolveBudget(Scene).TotalBytes)],
            ['Finest extent', () => FormatDistance(F.ResolveBudget(Scene).Levels[0].Extent)],
            ['Coarsest cell', () => FormatDistance(F.ResolveBudget(Scene).Levels.slice(-1)[0].Cell)]]);
        Caption(Inner, 'Terms shorter than two cells fade out — the 571 m gust bands, 95 m eddies — so a 100 km view shows the weather, not aliasing.');
    }

    function ConstructSourceRow(Host, Source)
    {
        const Type = F.TypeById[Source.Type];
        const Colour = F.Contributions[Type.Contribution].Colour;
        const Glyph = C.Construct('div', 'source-glyph', C.ConstructGlyph(Type.Glyph));
        Glyph.style.color = Colour;
        Glyph.style.background = Colour + '1f';
        const Status = C.Construct('span', 'row-status');
        const Row = C.Construct('div', 'source-row', [Glyph, C.Construct('div', null, [C.Construct('div', 'source-name', Source.Name), C.Construct('div', 'source-meta', `${Type.Name} · ${Type.Native}`)]),
                                                      C.Construct('div', 'source-reading', `${Number(Source.Strength.toFixed(2))} ${F.Contributions[Type.Contribution].Unit}`), Status]);
        Row.addEventListener('click', () => { Selected = Source.Token; ConstructInspectorPage(); C.Broadcast(); });
        LiveRefreshers.push(() => { const Reading = ResolveStatus(Source); Status.textContent = Reading; Status.className = 'row-status ' + Reading.toLowerCase(); });
        Host.append(Row);
    }

    function ConstructFieldSources(Page)
    {
        const Inner = ConstructSection(Page, 'sources', 'Localized forces', 'Target', 'Forces', () => `${Scene.Sources.length} forces`);
        for (const Contribution of ['Flow', 'Accelerate', 'Damp'])
        {
            const Members = Scene.Sources.filter(Source => F.TypeById[Source.Type].Contribution === Contribution);
            if (!Members.length) continue;
            SubHeading(Inner, `${F.Contributions[Contribution].Label} · ${F.Contributions[Contribution].Unit}`);
            Caption(Inner, F.Contributions[Contribution].Text);
            const List = C.Construct('div', 'source-list');
            for (const Source of Members) ConstructSourceRow(List, Source);
            Inner.append(List);
        }
        C.ActionRow(Inner, [{ Label: 'Add a localized force', Glyph: 'Plus', Tone: 'accent', Act: OpenLibrary }]);
    }

    function ConstructFieldChannels(Page)
    {
        const Field = Scene.Field;
        const Inner = ConstructSection(Page, 'channels', 'Receivers', 'Sparkles', 'Receivers', () => `${Field.Channels.length} of ${F.Channels.length}`);
        Caption(Inner, 'Who responds is configuration, not code: the field names its receivers, each force may narrow the list (Acts on), and a '
                     + 'receiver outside it ignores the field entirely.');
        C.ChipToggles(Inner, Bind(Field, 'Channels', { Label: 'Receivers that respond to the field', Options: F.Channels,
                                                       Detail: Keys => Keys.length ? `${Keys.length} receivers respond.` : 'No receiver responds: the field is authored but moves nothing.' }, 'FORCES', 'Force Field'));
        C.StatGrid(Inner, F.Channels.slice(0, 6).map(([Text, Key]) => [Text, () =>
        {
            if (!Field.Channels.includes(Key)) return 'off';
            return `${Scene.Sources.filter(Source => Source.Enabled && (!Source.Acts.length || Source.Acts.includes(Key))).length} forces`;
        }]));
        SubHeading(Inner, 'Wind receivers · WindRecord');
        C.SwitchRow(Inner, Bind(Scene.Wind, 'AdvectClouds', { Label: 'Advect clouds', Detail: 'Cloud density travels with the wind at cloud-base altitude.' }, 'WIND', 'Wind'));
        C.SwitchRow(Inner, Bind(Scene.Wind, 'AdvectPlumes', { Label: 'Advect plumes', Detail: 'Smoke and steam plumes lean and drift.' }, 'WIND', 'Wind'));
        C.SwitchRow(Inner, Bind(Scene.Wind, 'AdvectParticles', { Label: 'Advect particles', Detail: 'Particle effects with wind coupling follow the field.' }, 'WIND', 'Wind'));
    }

    function ConstructFieldTimeline(Page)
    {
        const Timeline = Scene.Timeline;
        const Inner = ConstructSection(Page, 'timeline', 'Timeline', 'Clock', 'Timeline', () => `${Timeline.Duration} s · ×${Timeline.Rate}`);
        Caption(Inner, 'Forces with a time window or a trigger get a lane under the viewport. Begins, Lasts and Repeats follow the eos taxonomy '
                     + '(Lasts 0 = forever, Repeats 0 = once); ramps follow the native trigger envelope.');
        C.SliderRow(Inner, Bind(Timeline, 'Duration', { Label: 'Timeline length', Minimum: 30, Maximum: 600, Step: 10, Digits: 0, Unit: 's' }, 'TIMELINE', 'Timeline'));
        C.SegmentedRow(Inner, Bind(Timeline, 'Rate', { Label: 'Playback rate', Options: [['×1 · real time', 1], ['×10', 10], ['×60 · a minute per second', 60]] }, 'TIMELINE', 'Timeline'));
        C.SwitchRow(Inner, Bind(Timeline, 'Loop', { Label: 'Loop', Detail: 'At the end the clock returns to 0 s; fired triggers keep their firing time.' }, 'TIMELINE', 'Timeline'));
        const Triggers = Scene.Sources.filter(Source => Source.Activation === 'Trigger');
        if (Triggers.length) C.ActionRow(Inner, Triggers.slice(0, 3).map(Source => ({ Label: `Fire ${Source.Name}`, Glyph: 'Zap', Tone: 'accent', Act: () => FireSource(Source) })));
    }

    let TimelineSignature = '';
    function ConstructTimelineIfChanged()
    {
        const Signature = JSON.stringify([Scene.Timeline, Scene.Sources.map(Source => [Source.Token, Source.Name, Source.Activation, Source.Begins, Source.Lasts, Source.Repeats, Source.TriggeredAt, Source.Enabled]), Selected]);
        if (Signature !== TimelineSignature) { TimelineSignature = Signature; ConstructTimeline(); }
    }

    function ConstructFieldVectors(Page)
    {
        const Field = Scene.Field;
        const Inner = ConstructSection(Page, 'vectors', 'World vectors', 'Move', 'Vectors', () => Field.Visualize ? 'shown' : 'hidden');
        Caption(Inner, 'The C++ editor’s arrows in the 3D scene viewport (WorldVectorOverlay): a region of samples evaluated at the camera’s field.');
        C.SwitchRow(Inner, Bind(Field, 'Visualize', { Label: 'Show world vectors', Detail: 'Draws the field as arrows in the scene viewport.' }, 'FORCES', 'Force Field'));
        C.SliderRow(Inner, Bind(Field, 'GridResolution', { Label: 'Samples per axis', Minimum: 3, Maximum: 8, Step: 1, Digits: 0 }, 'FORCES', 'Force Field'));
        C.SliderRow(Inner, Bind(Field, 'ArrowSeconds', { Label: 'Arrow time scale', Minimum: 0.05, Maximum: 2, Step: 0.05, Digits: 2, Unit: 's' }, 'FORCES', 'Force Field'));
        C.VectorRow(Inner, Bind(Field, 'RegionCentre', { Label: 'Region centre', Unit: 'm', Step: 0.25, Digits: 1 }, 'FORCES', 'Force Field'));
        C.VectorRow(Inner, Bind(Field, 'RegionHalfExtents', { Label: 'Region half extents', Unit: 'm', Step: 0.25, Digits: 1, Minimum: 1, Maximum: 1000 }, 'FORCES', 'Force Field'));
    }

    function ConstructFieldBudget(Page)
    {
        const Inner = ConstructSection(Page, 'budget', 'Budget', 'Gauge', 'Budget', () => { const Budget = F.ResolveBudget(Scene); return `${Budget.Baked.length} / ${Budget.GpuSlots} slots`; });
        Caption(Inner, 'Flow forces sum into the lattice and then cost one sample per receiver however many there are; accelerate forces, and any '
                     + 'flow force narrowed to some receivers, are evaluated per receiver per step.');
        LiveStatGrid(Inner, [
            ['Lattice slots', () => { const Budget = F.ResolveBudget(Scene); return `${Budget.Baked.length} / ${Budget.GpuSlots}`; }],
            ['Per receiver', () => String(F.ResolveBudget(Scene).Live.length)],
            ['Damp volumes', () => String(F.ResolveBudget(Scene).Damp.length)],
            ['Prototype samples', () => FormatCount(Visualization.Statistics.Samples)],
            ['Prototype refresh', () => `${Visualization.Statistics.Milliseconds.toFixed(1)} ms`],
            ['Refreshes', () => FormatCount(Visualization.Statistics.Refreshes)]]);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        WIND PAGE
    //------------------------------------------------------------------------------------------------------------------------

    function ResolveReferenceSpeed(Time)
    {
        const View = Visualization.View;
        return F.WindSpeedAt(Scene.Wind, Scene.Field, View.CentreX, Scene.Wind.GroundHeight + 10, View.CentreZ, Time);
    }

    function ConstructWindPage(Page)
    {
        const Wind = Scene.Wind;
        const Owner = 'Wind';
        ConstructHeading(Page, 'Forces / Force Field', 'Wind', C.Construct('span', 'guest-pill', 'GUEST OF FORCE FIELD'),
                         'The planetary profile the field carries: a direction of travel, a pulse of gusts, Ekman shear and veer through altitude, and curl eddies.');
        const BeaufortMark = C.Construct('div', 'mark live', C.ConstructGlyph('Wind'));
        const BeaufortText = C.Construct('span');
        const Metric = C.Construct('div', 'metric-big');
        const Rate = C.Construct('div', 'rate');
        const Low = C.Construct('div', 'low');
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [BeaufortMark, BeaufortText]), C.Construct('div', 'estimate-tag', '10 m · view centre')]),
            C.Construct('div', 'hero-title', 'Wind speed now'),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])]);
        Page.append(Card);
        const Series = C.TimeSeries(Card, { Samples: WindSamples, Capacity: SampleCapacity, Target: () => Wind.Speed, Height: 118, Unit: 'm/s', TargetLabel: 'Base',
                                            Headroom: 1.35, AxisDigits: 1 });
        Card.append(C.Construct('div', 'hero-note', 'Shaded windows are gusts above the base speed: the travelling two-frequency wave of PlanetaryWindProfile, '
                                                    + 'plus eddies where turbulence is on.'));
        LiveRefreshers.push(() =>
        {
            const Speed = ResolveReferenceSpeed(Clock.Time);
            const Beaufort = F.ResolveBeaufort(Speed);
            C.ConstructMetric(Metric, Speed, 1, 'm/s');
            BeaufortText.textContent = `Beaufort ${Beaufort.Force} · ${Beaufort.Name}`;
            BeaufortMark.className = 'mark ' + (Beaufort.Force >= 10 ? 'danger' : Beaufort.Force >= 8 ? 'warm' : 'live');
            Rate.textContent = `${Wrap360(Wind.Bearing + 180).toFixed(0)}°`;
            Rate.append(C.Construct('small', null, 'from'));
            Low.textContent = `gusts to ${(Wind.Speed * (1 + Wind.GustStrength)).toFixed(1)} m/s`;
        });
        ResolveSeriesRefresh(Series);
        C.ChipRow(Page, [
            () => [`${Wind.Bearing.toFixed(0)}°`, `toward ${C.ResolveCompassPoint(Wind.Bearing)}`],
            () => [`${Wrap360(Wind.Bearing + 180).toFixed(0)}°`, `from ${C.ResolveCompassPoint(Wind.Bearing + 180)}`],
            () => [`+${(Wind.Shear * 100).toFixed(0)} %`, 'speed / km'],
            () => [`${Wind.Veer.toFixed(0)}°`, 'veer / km']]);
        Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Quick controls'), C.Construct('div', 'caption', 'WindRecord')]));
        const Grid = C.Construct('div', 'toggle-grid');
        C.ToggleTile(Grid, Bind(Wind, 'Enabled', { Label: 'Wind', Glyph: 'Wind' }, 'WIND', Owner));
        C.ToggleTile(Grid, { Label: 'Gust wave', Glyph: 'Activity', Read: () => Wind.GustStrength > 0,
                             Write: Enabled => { Wind.GustStrength = Enabled ? (Wind.StoredGust || 0.25) : (Wind.StoredGust = Wind.GustStrength, 0); AppendLog('WIND', `Wind: gust wave ${Enabled ? 'on' : 'off'}`, null, 'live'); } });
        C.ToggleTile(Grid, Bind(Scene.Field, 'IncludeTurbulence', { Label: 'Eddies', Glyph: 'Swirl' }, 'WIND', Owner));
        C.ToggleTile(Grid, { Label: 'Mesoscale', Glyph: 'Globe', Read: () => Wind.MesoscaleStrength > 0,
                             Write: Enabled => { Wind.MesoscaleStrength = Enabled ? (Wind.StoredMesoscale || 0.22) : (Wind.StoredMesoscale = Wind.MesoscaleStrength, 0); AppendLog('WIND', `Wind: mesoscale ${Enabled ? 'on' : 'off'}`, 'proposed', 'live'); } });
        C.ToggleTile(Grid, Bind(Wind, 'AdvectClouds', { Label: 'Clouds', Glyph: 'Cloud' }, 'WIND', Owner));
        C.ToggleTile(Grid, Bind(Wind, 'AdvectParticles', { Label: 'Particles', Glyph: 'Sparkles' }, 'WIND', Owner));
        Page.append(Grid);
        const ConstructTabs = ConstructSectionTabs(Page);
        ConstructWindFlow(Page);
        ConstructWindVariability(Page);
        ConstructWindAltitude(Page);
        ConstructWindEddies(Page);
        ConstructWindMesoscale(Page);
        ConstructLevelspaceBlock(Page, 'Wind', 'Levelspace record', 'Nested under rec_force_field');
        ConstructTabs();
    }

    function ConstructWindFlow(Page)
    {
        const Wind = Scene.Wind;
        const Inner = ConstructSection(Page, 'flow', 'Flow', 'Compass', 'Flow', () => `${Wind.Speed.toFixed(1)} m/s`);
        Caption(Inner, 'Where the air goes. The dial is the direction of travel (WindConfiguration::DirectionDegrees); meteorology names the opposite, the bearing it comes from.');
        C.PolarDial(Inner, Bind(Wind, 'Bearing', { Label: 'Direction of travel', Digits: 0, Unit: '°',
                                                   Resolve: () => ({ Caption: `From ${Wrap360(Wind.Bearing + 180).toFixed(0)}° ${C.ResolveCompassPoint(Wind.Bearing + 180)} · native BearingRadians ${((Wind.Bearing - 90) * Radian).toFixed(3)}` }) }, 'WIND', 'Wind'));
        C.SliderRow(Inner, Bind(Wind, 'Speed', { Label: 'Base speed', Minimum: 0, Maximum: 40, Step: 0.1, Digits: 1, Unit: 'm/s', Labels: ['calm', 'storm'] }, 'WIND', 'Wind'));
        C.SliderRow(Inner, Bind(Wind, 'GroundHeight', { Label: 'Ground height', Minimum: -200, Maximum: 3000, Step: 1, Digits: 0, Unit: 'm', Tags: [['Proposed', 'warm']] }, 'WIND', 'Wind'));
    }

    function ConstructWindVariability(Page)
    {
        const Wind = Scene.Wind;
        const Inner = ConstructSection(Page, 'variability', 'Variability', 'Activity', 'Gusts', () => `${(Wind.GustStrength * 100).toFixed(0)} % · ${Wind.GustFrequency.toFixed(3)} Hz`);
        Caption(Inner, 'A coherent gust wave travels downwind: 0.62 sin φ + 0.38 sin(2.17 φ + 1.13), with φ = 2π f t − 0.011 · distance along the wind.');
        C.CurvePlot(Inner, { Height: 128, Resolve: () =>
        {
            const Points = [];
            const Ground = Wind.GroundHeight + 10;
            const Saved = Scene.Field.IncludeTurbulence;
            for (let Second = 0; Second <= 60; Second += 0.25)
            {
                Scene.Field.IncludeTurbulence = false;
                Points.push([Second, F.WindSpeedAt(Wind, Scene.Field, 0, Ground, 0, Second)]);
            }
            Scene.Field.IncludeTurbulence = Saved;
            const Peak = Math.max(...Points.map(Point => Point[1]), Wind.Speed * 1.1, 1);
            const PeakPoint = Points.reduce((Best, Point) => Point[1] > Best[1] ? Point : Best, Points[0]);
            return { XRange: [0, 60], YRange: [0, Peak * 1.15], Curves: [{ Points, Stroke: '#dedede', Fill: 'rgba(255,255,255,0.06)' }],
                     Guides: [{ Y: Wind.Speed, Text: `Base ${Wind.Speed.toFixed(1)} m/s` }], Markers: [{ X: PeakPoint[0], Y: PeakPoint[1], Label: `${PeakPoint[1].toFixed(1)} m/s` }],
                     XTicks: [[0, '0 s'], [15, '15 s'], [30, '30 s'], [45, '45 s'], [60, '60 s']], YTicks: [[0, '0'], [Peak / 2, (Peak / 2).toFixed(0)], [Peak, Peak.toFixed(0)]],
                     Caption: `At the origin, 10 m up, eddies left out · period ${(1 / Math.max(0.001, Wind.GustFrequency)).toFixed(1)} s` };
        } });
        C.SliderRow(Inner, Bind(Wind, 'GustStrength', { Label: 'Gust strength', Minimum: 0, Maximum: 1, Step: 0.01, Digits: 2, Unit: '×' }, 'WIND', 'Wind'));
        C.SliderRow(Inner, Bind(Wind, 'GustFrequency', { Label: 'Gust frequency', Minimum: 0.01, Maximum: 2, Step: 0.005, Digits: 3, Unit: 'Hz', Scale: 'Logarithmic' }, 'WIND', 'Wind'));
    }

    function ConstructWindAltitude(Page)
    {
        const Wind = Scene.Wind;
        const Inner = ConstructSection(Page, 'altitude', 'Altitude', 'Layers', 'Altitude', () => `+${(Wind.Shear * 100).toFixed(0)} %/km · ${Wind.Veer.toFixed(0)}°/km`);
        Caption(Inner, 'Ekman profile: speed gains with height (never below 15 % of the base) and the direction veers. Left: speed through 0–3 km. Right: the hodograph, plan view with north up.');
        const Profile = () =>
        {
            const Points = [];
            for (let Altitude = 0; Altitude <= 3000; Altitude += 100)
            {
                const AltitudeKm = Altitude / 1000;
                const Speed = Wind.Speed * Math.max(0.15, 1 + Wind.Shear * Math.min(AltitudeKm, 12));
                const Bearing = (Wind.Bearing + Wind.Veer * AltitudeKm) * Radian;
                Points.push({ Altitude, Speed, East: Math.sin(Bearing) * Speed, North: Math.cos(Bearing) * Speed });
            }
            return Points;
        };
        C.CurvePlot(Inner, { Height: 150, Resolve: () =>
        {
            const Points = Profile();
            const Peak = Math.max(...Points.map(Point => Point.Speed), 1) * 1.15;
            return { XRange: [0, Peak], YRange: [0, 3000], Curves: [{ Points: Points.map(Point => [Point.Speed, Point.Altitude]), Stroke: '#dfe7f3', Width: 1.8 }],
                     Markers: [0, 10, 20, 30].map(Index => ({ X: Points[Index].Speed, Y: Points[Index].Altitude, Label: `${Points[Index].Speed.toFixed(1)} m/s` })),
                     XTicks: [[0, '0'], [Peak / 2, `${(Peak / 2).toFixed(0)} m/s`], [Peak, `${Peak.toFixed(0)}`]], YTicks: [[0, '0'], [1000, '1 km'], [2000, '2 km'], [3000, '3 km']] };
        } });
        C.CurvePlot(Inner, { Height: 150, Resolve: () =>
        {
            const Points = Profile();
            const Reach = Math.max(...Points.map(Point => Point.Speed), 1) * 1.1;
            return { XRange: [-Reach, Reach], YRange: [-Reach * 0.62, Reach * 0.62], Curves: [
                         { Points: [[-Reach, 0], [Reach, 0]], Stroke: 'rgba(255,255,255,0.12)', Width: 1 },
                         { Points: [[0, -Reach], [0, Reach]], Stroke: 'rgba(255,255,255,0.12)', Width: 1 },
                         { Points: Points.map(Point => [Point.East, Point.North]), Stroke: '#9fb4d6', Width: 1.8 },
                         ...[0, 10, 20, 30].map(Index => ({ Points: [[0, 0], [Points[Index].East, Points[Index].North]], Stroke: 'rgba(223,231,243,0.45)', Width: 1, Dashed: Index > 0 }))],
                     Markers: [0, 10, 20, 30].map(Index => ({ X: Points[Index].East, Y: Points[Index].North, Label: Index ? `${Index / 10} km` : 'ground' })),
                     Caption: `Toward ${Wind.Bearing.toFixed(0)}° at the ground, ${(Wind.Bearing + Wind.Veer * 3).toFixed(0)}° at 3 km` };
        } });
        C.SliderRow(Inner, Bind(Wind, 'Shear', { Label: 'Speed gain per km', Minimum: 0, Maximum: 2, Step: 0.01, Digits: 2, Unit: '/km' }, 'WIND', 'Wind'));
        C.SliderRow(Inner, Bind(Wind, 'Veer', { Label: 'Direction veer per km', Minimum: -30, Maximum: 30, Step: 0.5, Digits: 1, Unit: '°/km' }, 'WIND', 'Wind'));
    }

    // 📝 The eddy preview is a second ForceFieldVisualization over 8 eddy scales of the wind alone, coloured by vorticity.
    function ConstructWindEddies(Page)
    {
        const Wind = Scene.Wind;
        const Inner = ConstructSection(Page, 'eddies', 'Turbulence', 'Swirl', 'Eddies', () => `${Wind.Turbulence.toFixed(2)} · ${FormatDistance(Wind.TurbulenceScale)}`);
        Caption(Inner, 'Curl eddies: divergence-free, drifting downwind at 0.35 × the wind. Strength scales with the wind (0.45 × speed × turbulence).');
        const Canvas = C.Construct('canvas', 'preview-canvas');
        const Panel = C.Construct('div', 'diagram-panel', Canvas);
        Inner.append(Panel);
        EddyPreview = V.Create(Canvas, { Sample: (Contribution, X, Y, Z, Time, CellSpan, Out) => { F.EvaluateWind(Wind, Scene.Field, X, Y, Z, Time, CellSpan, Out); return true; } });
        EddyPreview.Configure({ Quantity: 'Vorticity', Arrows: true, ArrowSpacing: 30, Tracers: true, TracerCount: 140, ShowScaleBar: true, ShowLegend: false,
                                ShowSources: false, ShowGroundGrid: false, CellPixels: 4, SliceHeight: 10, RefreshInterval: 120 });
        C.SliderRow(Inner, Bind(Wind, 'Turbulence', { Label: 'Turbulence', Minimum: 0, Maximum: 1, Step: 0.01, Digits: 2 }, 'WIND', 'Wind'));
        C.SliderRow(Inner, DistanceBinding(Wind, 'TurbulenceScale', { Label: 'Turbulence scale', Minimum: 4, Maximum: 1000 }, 'WIND', 'Wind'));
        C.SliderRow(Inner, Bind(Wind, 'Seed', { Label: 'Seed', Minimum: 1, Maximum: 9999, Step: 1, Digits: 0, Tags: [['Proposed', 'warm']] }, 'WIND', 'Wind'));
    }

    function ConstructWindMesoscale(Page)
    {
        const Wind = Scene.Wind;
        const Inner = ConstructSection(Page, 'mesoscale', 'Mesoscale', 'Globe', 'Mesoscale', () => Wind.MesoscaleStrength > 0 ? FormatDistance(Wind.MesoscaleScale) : 'off');
        Caption(Inner, 'Proposed for the 100 km range: a slow variation of speed (±strength) and direction (±55° × strength) over tens of kilometres, drifting '
                     + 'with the wind, so a weather-scale view is not one uniform arrow.');
        C.SliderRow(Inner, DistanceBinding(Wind, 'MesoscaleScale', { Label: 'Mesoscale length', Minimum: 2000, Maximum: 100000, Tags: [['Proposed', 'warm']] }, 'WIND', 'Wind'));
        C.SliderRow(Inner, Bind(Wind, 'MesoscaleStrength', { Label: 'Mesoscale strength', Minimum: 0, Maximum: 0.6, Step: 0.01, Digits: 2, Tags: [['Proposed', 'warm']] }, 'WIND', 'Wind'));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     SOURCE PAGE
    //------------------------------------------------------------------------------------------------------------------------

    function ConstructSourcePage(Page, Source)
    {
        const Type = F.TypeById[Source.Type];
        const Facts = F.Contributions[Type.Contribution];
        const Pill = C.Construct('span', 'contribution-pill ' + Type.Contribution.toLowerCase(), `${Facts.Label.toUpperCase()} · ${Facts.Unit}`);
        const Heading = ConstructHeading(Page, 'Forces / Force Field', Source.Name, Pill, Type.Blurb);
        C.Register(() => { Heading.textContent = Source.Name; });
        const StatusMark = C.Construct('div', 'mark live', C.ConstructGlyph('Check'));
        const StatusText = C.Construct('span');
        const Metric = C.Construct('div', 'metric-big');
        const Rate = C.Construct('div', 'rate');
        const Low = C.Construct('div', 'low');
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [StatusMark, StatusText]), C.Construct('div', 'estimate-tag', Type.Native)]),
            C.Construct('div', 'hero-title', `Peak ${ContributionWord[Type.Contribution]}`),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])])]);
        Page.append(Card);
        const Plot = C.CurvePlot(Card, { Height: 120, Resolve: () =>
        {
            const Profile = F.ResolveProfile(Scene, Source, Scene.Visualization.SliceHeight);
            const Peak = Math.max(...Profile.Points.map(Point => Point[1]), 1e-6);
            const PeakPoint = Profile.Points.reduce((Best, Point) => Point[1] > Best[1] ? Point : Best, Profile.Points[0]);
            const Ticks = [0, 0.5, 1].map(Share => [Profile.Reach * Share, FormatDistance(Profile.Reach * Share)]);
            const Guides = Source.InnerRadius && Source.InnerRadius < Profile.Reach ? [{ X: Source.InnerRadius, Text: 'inner' }] : [];
            return { XRange: [0, Profile.Reach], YRange: [0, Peak * 1.2], Curves: [{ Points: Profile.Points, Stroke: Facts.Colour, Fill: Facts.Colour + '22', Width: 1.6 }],
                     Markers: [{ X: PeakPoint[0], Y: PeakPoint[1], Label: `${V.FormatReading(PeakPoint[1])} ${Profile.Unit}` }], XTicks: Ticks, Guides,
                     Caption: Source.Type === 'GustFront' ? 'Across the front: nose, surge and wake' : Source.Type === 'Blast' ? 'Shell profile when the front is mid-reach'
                            : Source.Reach === 'Box' ? 'Along the box from its centre' : 'From the centre outward, at the slice height' };
        } });
        LiveRefreshers.push(Plot.Refresh);
        LiveRefreshers.push(() =>
        {
            const Profile = F.ResolveProfile(Scene, Source, Scene.Visualization.SliceHeight);
            const Peak = Math.max(...Profile.Points.map(Point => Point[1]));
            C.ConstructMetric(Metric, Peak, Peak >= 100 ? 0 : Peak >= 10 ? 1 : 2, Profile.Unit);
            const Entry = F.PrepareSource(Source, Scene.Wind, Clock.Time);
            const Status = Entry ? Entry.Status : 'Off';
            StatusMark.className = 'mark ' + (Status === 'Active' ? 'live' : Status === 'Armed' || Status === 'Waiting' ? 'warm' : 'danger');
            StatusMark.textContent = '';
            StatusMark.append(C.ConstructGlyph(Status === 'Active' ? 'Check' : Status === 'Off' ? 'Close' : 'Clock'));
            StatusText.textContent = { Active: 'Acting now', Armed: 'Armed · waiting for its trigger', Waiting: 'Waiting for its window', Spent: 'Window over', Off: 'Disabled' }[Status];
            Rate.textContent = Source.Reach === 'Everywhere' ? '∞' : FormatDistance(Source.Reach === 'Box' ? Math.max(F.ResolveBoxExtents(Source).HalfX, F.ResolveBoxExtents(Source).HalfZ) : Source.Radius);
            Rate.append(C.Construct('small', null, 'reach'));
            Low.textContent = `envelope ${Entry ? (Entry.Envelope * 100).toFixed(0) : 0} %`;
        });
        C.ChipRow(Page, [
            () => [Type.Name.split(' ')[0], 'type'],
            () => [Facts.Unit, 'contribution'],
            () => [Source.Reach, 'reach'],
            () => [Source.Activation, 'activation']]);
        Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Quick controls'), C.Construct('div', 'caption', Type.Native)]));
        const Grid = C.Construct('div', 'toggle-grid');
        C.ToggleTile(Grid, Bind(Source, 'Enabled', { Label: 'Enabled', Glyph: 'Check' }, 'FORCES', Source.Name));
        C.ToggleTile(Grid, { Label: 'Solo in view', Glyph: 'Target', Read: () => Scene.Visualization.Solo === Source.Token,
                             Write: Enabled => { Scene.Visualization.Solo = Enabled ? Source.Token : null; if (Enabled) Scene.Visualization.Contribution = Type.Contribution;
                                                 AppendLog('VIEW', Enabled ? `Solo: ${Source.Name}` : 'Solo off', Enabled ? 'the view shows this force alone' : null, 'live', 'View.Solo'); } });
        C.ToggleTile(Grid, { Label: 'Follow', Glyph: 'Crosshair', Read: () => Scene.Visualization.Follow === Source.Token,
                             Write: Enabled => { Scene.Visualization.Follow = Enabled ? Source.Token : null; AppendLog('VIEW', Enabled ? `Following ${Source.Name}` : 'Follow off', null, 'live', 'View.Follow'); } });
        Page.append(Grid);
        C.ActionRow(Page, [
            { Label: 'Frame in view', Glyph: 'Crosshair', Act: () => FitSourceInView(Source) },
            { Label: 'Duplicate', Glyph: 'Copy', Act: () => DuplicateSource(Source) },
            { Label: 'Remove', Glyph: 'Trash', Tone: 'danger', Act: () => RemoveSource(Source) }]);
        const ConstructTabs = ConstructSectionTabs(Page);
        ConstructSourcePlacement(Page, Source, Type);
        ConstructSourceReach(Page, Source, Type);
        ConstructSourceStrength(Page, Source, Type);
        ConstructSourceTime(Page, Source, Type);
        ConstructSourceChannels(Page, Source, Type);
        ConstructLevelspaceBlock(Page, Source.Token, 'Levelspace record', 'ForceSource · proposed');
        ConstructTabs();
    }

    function FitSourceInView(Source)
    {
        const Entry = F.PrepareSource(Source, Scene.Wind, Clock.Time);
        const View = Visualization.View;
        View.CentreX = Entry.Centre[0];
        View.CentreZ = Entry.Centre[2];
        const Reach = Source.Reach === 'Everywhere' ? 4000 : Source.Reach === 'Box' ? Math.max(F.ResolveBoxExtents(Source).HalfX, F.ResolveBoxExtents(Source).HalfZ) * 2 : Source.Radius * 2;
        View.Span = Math.min(2.4e5, Math.max(60, Reach * 1.6));
        AppendLog('VIEW', `Framed ${Source.Name}`, `span ${FormatDistance(View.Span)}`, 'live', 'View.Frame');
    }

    function ConstructSourcePlacement(Page, Source, Type)
    {
        const Inner = ConstructSection(Page, 'placement', 'Placement', 'Move', 'Placement', () => `${FormatDistance(Source.Centre[0])}, ${FormatDistance(Source.Centre[2])}`);
        C.TextRow(Inner, Bind(Source, 'Name', { Label: 'Name' }, 'FORCES', Source.Name));
        Caption(Inner, Source.Type === 'Microburst' ? 'The centre height is the downdraft core altitude; the outflow spreads along the ground.'
                     : Source.Type === 'GustFront' ? 'The centre is where the front starts; it travels at its front speed from there.'
                     : Source.Type === 'Tornado' ? 'The centre is the touch-down point at the start of the timeline; the column travels its track.'
                     : 'Drag the source’s dot in the viewport, or type a position. Y is height above the field’s ground.');
        const Step = Math.max(0.05, (Source.Radius || 10) / 150);
        C.VectorRow(Inner, Bind(Source, 'Centre', { Label: 'Centre', Unit: 'm', Step, Digits: Source.Radius >= 1000 ? 0 : 1 }, 'FORCES', Source.Name));
        for (const [Key, Label] of Type.Bearings || [])
        {
            C.PolarDial(Inner, Bind(Source, Key, { Label, Digits: 0, Unit: '°', Resolve: () => ({ Caption: `${C.ResolveCompassPoint(Source[Key])} · compass bearing of travel` }) }, 'FORCES', Source.Name));
        }
        if (Type.Spin) C.SegmentedRow(Inner, Bind(Source, 'Spin', { Label: 'Rotation', Options: [['Cyclonic · counter-clockwise', 1], ['Anticyclonic · clockwise', -1]] }, 'FORCES', Source.Name));
    }

    function ConstructSourceReach(Page, Source, Type)
    {
        const Inner = ConstructSection(Page, 'reach', 'Reach and falloff', 'Target', 'Reach', () => `${Source.Reach} · ${Source.Falloff}`);
        Caption(Inner, 'Where the force acts and how it fades toward the edge. Everywhere is not a large sphere: it has no centre and no edge, the case a bounded lattice cannot hold.');
        const Fixed = Source.Type === 'GustFront' || Source.Type === 'Jet' || Source.Type === 'Wake';
        C.SegmentedRow(Inner, Bind(Source, 'Reach', { Label: 'Reach', Options: F.Reaches,
                                                      Availability: () => Fixed ? { Enabled: false, Reason: 'set by the type’s geometry' } : { Enabled: true } }, 'FORCES', Source.Name));
        C.SliderRow(Inner, DistanceBinding(Source, 'Radius', { Label: 'Radius', Minimum: 1, Maximum: 60000,
                                                               Availability: () => Source.Reach === 'Sphere' || Source.Reach === 'Column' ? { Enabled: true } : { Enabled: false, Reason: Source.Reach === 'Box' ? 'box reach' : 'everywhere' } }, 'FORCES', Source.Name));
        C.SliderRow(Inner, DistanceBinding(Source, 'Height', { Label: 'Height', Minimum: 1, Maximum: 15000,
                                                               Availability: () => (Source.Reach === 'Column' || Source.Reach === 'Box') && !Fixed ? { Enabled: true } : { Enabled: false, Reason: 'column and box only' } }, 'FORCES', Source.Name));
        if (Source.Reach === 'Box' && !Fixed)
        {
            C.SliderRow(Inner, DistanceBinding(Source, 'HalfX', { Label: 'Half width', Minimum: 0.5, Maximum: 60000 }, 'FORCES', Source.Name));
            C.SliderRow(Inner, DistanceBinding(Source, 'HalfZ', { Label: 'Half depth', Minimum: 0.5, Maximum: 60000 }, 'FORCES', Source.Name));
        }
        C.SegmentedRow(Inner, Bind(Source, 'Falloff', { Label: 'Falloff', Options: F.Falloffs }, 'FORCES', Source.Name));
        C.CurvePlot(Inner, { Height: 96, Resolve: () =>
        {
            const Points = [];
            for (let Step = 0; Step <= 60; ++Step) Points.push([Step / 60, F.Falls(Source.Falloff, Step / 60, 1)]);
            return { XRange: [0, 1], YRange: [0, 1.1], Curves: [{ Points, Stroke: '#dfe7f3', Fill: 'rgba(223,231,243,0.06)' }],
                     XTicks: [[0, 'centre'], [0.5, 'half'], [1, 'edge']], YTicks: [[0, '0'], [1, '1']],
                     Caption: { None: 'Full strength to the edge, then nothing.', Linear: '1 − q', Smooth: '(1 − q²)², the curve the wind kinds use.', InverseSquare: '1 / (1 + 8q²), gravity-like.' }[Source.Falloff] };
        } });
    }

    function ConstructSourceStrength(Page, Source, Type)
    {
        const Inner = ConstructSection(Page, 'strength', 'Strength', 'Gauge', 'Strength', () => `${Number(Source.Strength.toFixed(2))} ${F.Contributions[Type.Contribution].Unit}`);
        Caption(Inner, Type.Contribution === 'Flow' ? 'Velocities the air carries here, in m/s; receivers follow them at their own coupling.'
                     : Type.Contribution === 'Accelerate' ? 'Accelerations added to every receiver in reach, in m/s², whatever its coupling.' : 'A rate in 1/s: velocity × e^(−rate × step).');
        for (const Parameter of Type.Parameters) C.SliderRow(Inner, ParameterBinding(Source, Parameter));
    }

    function ConstructSourceTime(Page, Source, Type)
    {
        const Duration = () => Scene.Timeline.Duration;
        const Inner = ConstructSection(Page, 'time', 'Time window', 'Clock', 'Time', () => Source.Activation);
        Caption(Inner, 'Always: from the start. Window: Begins, Lasts (0 = forever), Repeats (0 = once). Trigger: armed until fired, then it runs its window.');
        C.SegmentedRow(Inner, Bind(Source, 'Activation', { Label: 'Activation', Options: F.Activations }, 'FORCES', Source.Name));
        const Timed = () => Source.Activation === 'Always' ? { Enabled: false, Reason: 'always on' } : { Enabled: true };
        C.SliderRow(Inner, Bind(Source, 'Begins', { Label: 'Begins', Minimum: 0, Maximum: 600, Step: 0.5, Digits: 1, Unit: 's', Availability: () => Source.Activation === 'Window' ? { Enabled: true } : { Enabled: false, Reason: Source.Activation === 'Trigger' ? 'starts when fired' : 'always on' } }, 'FORCES', Source.Name));
        C.SliderRow(Inner, Bind(Source, 'Lasts', { Label: 'Lasts · 0 = forever', Minimum: 0, Maximum: 600, Step: 0.5, Digits: 1, Unit: 's', Availability: Timed }, 'FORCES', Source.Name));
        C.SliderRow(Inner, Bind(Source, 'Repeats', { Label: 'Repeats every · 0 = once', Minimum: 0, Maximum: 600, Step: 0.5, Digits: 1, Unit: 's', Availability: () => Source.Activation === 'Window' ? { Enabled: true } : { Enabled: false, Reason: 'windows only' } }, 'FORCES', Source.Name));
        C.SliderRow(Inner, Bind(Source, 'RampIn', { Label: 'Ramp in', Minimum: 0, Maximum: 30, Step: 0.05, Digits: 2, Unit: 's', Availability: Timed }, 'FORCES', Source.Name));
        C.SliderRow(Inner, Bind(Source, 'RampOut', { Label: 'Ramp out', Minimum: 0, Maximum: 30, Step: 0.05, Digits: 2, Unit: 's', Availability: Timed }, 'FORCES', Source.Name));
        C.CurvePlot(Inner, { Height: 96, Resolve: () =>
        {
            const Points = [];
            const Span = Duration();
            for (let Step = 0; Step <= 240; ++Step) { const Time = Step / 240 * Span; Points.push([Time, F.ResolveActivation(Source, Time).Envelope]); }
            return { XRange: [0, Span], YRange: [0, 1.15], Curves: [{ Points, Stroke: F.Contributions[Type.Contribution].Colour, Fill: F.Contributions[Type.Contribution].Colour + '22' }],
                     Guides: [{ X: Math.min(Span, Clock.Time), Text: 'now' }], XTicks: [[0, '0 s'], [Span / 2, `${Span / 2} s`], [Span, `${Span} s`]],
                     Caption: 'Envelope over the timeline (the native ramp-in / ramp-out shape).' };
        } });
        if (Source.Activation === 'Trigger' || Source.Type === 'Blast')
            C.ActionRow(Inner, [{ Label: 'Fire now', Glyph: 'Zap', Tone: 'accent', Act: () => FireSource(Source) },
                                { Label: 'Re-arm', Glyph: 'Reset', Act: () => { Source.TriggeredAt = null; AppendLog('TRIGGER', `${Source.Name} re-armed`, null, 'live'); } }]);
    }

    function ConstructSourceChannels(Page, Source, Type)
    {
        const Inner = ConstructSection(Page, 'acts', 'Acts on', 'Sparkles', 'Acts on', () => Source.Acts.length ? `${Source.Acts.length} receivers` : 'every receiver');
        Caption(Inner, 'Leave empty and the force acts on every receiver the field reaches. Name receivers and it acts on those alone.');
        C.ChipToggles(Inner, Bind(Source, 'Acts', { Label: 'Receivers', Options: F.Channels,
                                                    Detail: Keys => !Keys.length ? 'Acts on every receiver.'
                                                                  : Type.Contribution === 'Flow' ? 'Selective flow cannot share the lattice with the forces it excludes: evaluated per receiver.'
                                                                  : `Acts on ${Keys.length} receiver${Keys.length > 1 ? 's' : ''}.` }, 'FORCES', Source.Name));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  INSPECTOR ASSEMBLY
    //------------------------------------------------------------------------------------------------------------------------

    function ConstructInspectorPage()
    {
        if (PageScope) C.ReleaseScope(PageScope);
        PageScope = C.OpenScope();
        LiveRefreshers.length = 0;
        Sections.length = 0;
        EddyPreview = null;
        const Content = document.getElementById('InspectorContent');
        Content.textContent = '';
        const Page = C.Construct('div', 'inspector');
        Content.append(Page);
        const Source = SourceByToken(Selected);
        if (Selected === 'Wind') ConstructWindPage(Page);
        else if (Source) ConstructSourcePage(Page, Source);
        else { Selected = 'Field'; ConstructFieldPage(Page); }
        C.CloseScope(PageScope);
        document.getElementById('InspectorScroll').scrollTop = 0;
        PeakSamples.length = 0;
        WindSamples.length = 0;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     INITIALIZATION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Capture hold (for headless, software-rendered evidence captures): stop the loop and the live interval, render once.
    let Held = false;

    function Tick(Now)
    {
        if (Held) return;
        requestAnimationFrame(Tick);
        const Delta = Clock.LastTick ? Math.min(0.1, (Now - Clock.LastTick) / 1000) : 0;
        Clock.LastTick = Now;
        if (Clock.Playing)
        {
            Clock.Time += Delta * Scene.Timeline.Rate;
            if (Clock.Time > Scene.Timeline.Duration)
            {
                if (Scene.Timeline.Loop) Clock.Time %= Scene.Timeline.Duration;
                else { Clock.Time = Scene.Timeline.Duration; TogglePlay(); }
            }
        }
        ApplyVisualization();
        Visualization.Render(Clock.Time);
        if (EddyPreview)
        {
            const Wind = Scene.Wind;
            Object.assign(EddyPreview.View, { Mode: 'Map', CentreX: 0, CentreZ: 0, Span: Math.max(80, Wind.TurbulenceScale * 8) });
            EddyPreview.Render(Clock.Time);
        }
        RefreshPlayhead();
    }

    // 📝 Live readouts refresh four times a second from one list (as Render settings' LiveRefreshers), never per frame.
    function RefreshLive()
    {
        PeakSamples.push(Visualization.Statistics.Peak);
        if (PeakSamples.length > SampleCapacity) PeakSamples.shift();
        WindSamples.push(ResolveReferenceSpeed(Clock.Time));
        if (WindSamples.length > SampleCapacity) WindSamples.shift();
        LiveRefreshers.forEach(Refresh => Refresh());
        PersistentRefreshers.forEach(Refresh => Refresh());
        LiveOutlinerRows.forEach(Refresh => Refresh());
        RefreshHud();
    }

    function Initialize()
    {
        ConstructVisualization();
        ConstructViewport();
        ConstructOutliner();
        C.SmoothScroll(document.getElementById('OutlinerScroll'), document.getElementById('OutlinerContent'));
        InspectorScroller = C.SmoothScroll(document.getElementById('InspectorScroll'), document.getElementById('InspectorContent'));
        ConstructLogStream();
        ConstructInspectorPage();
        ConstructTimeline();
        const InspectorStatus = document.getElementById('InspectorStatus');
        const Revision = C.Construct('b', null, '0');
        InspectorStatus.append(C.Construct('div', 'status-cell', [C.Construct('span', null, 'Parameters'), C.Construct('b', null, 'Editing')]),
                               C.Construct('span', 'spacer'), C.Construct('div', 'status-cell', [C.Construct('span', null, 'Revision'), Revision]));
        C.Register(() => { ConstructTimelineIfChanged(); });
        // 📝 Every write broadcasts; one refresher turns that into a scene revision (re-prepares the sampler), a redraw and a save.
        let LastEncoding = JSON.stringify(Scene);
        C.Register(() =>
        {
            const Encoding = JSON.stringify(Scene);
            if (Encoding === LastEncoding) return;
            LastEncoding = Encoding;
            SceneRevision++;
            Revision.textContent = String(SceneRevision);
            if (Visualization) Visualization.Invalidate();
            Persist();
        });
        document.addEventListener('keydown', Event =>
        {
            if (Event.target instanceof HTMLInputElement || Event.target instanceof HTMLSelectElement) return;
            const Source = SourceByToken(Selected);
            if (Event.code === 'Space') { TogglePlay(); Event.preventDefault(); }
            else if ((Event.key === 'f' || Event.key === 'F') && Source) FireSource(Source);
            else if ((Event.key === 'Delete' || Event.key === 'Backspace') && Source) RemoveSource(Source);
            else if (Event.key >= '1' && Event.key <= '4') JumpToRange(RangePresets[Number(Event.key) - 1][1]);
            else if (Event.key === 'm' || Event.key === 'M') { Scene.Visualization.Mode = 'Map'; C.Broadcast(); }
            else if (Event.key === 'o' || Event.key === 'O') { Scene.Visualization.Mode = 'Oblique'; C.Broadcast(); }
            else if (Event.key === 'Escape') document.getElementById('LibraryVeil').classList.remove('shown');
        });
        C.Broadcast();
        setInterval(() => { if (!Held) RefreshLive(); }, 250);
        RefreshLive();
        requestAnimationFrame(Tick);
        window.ForceFieldEditorScene = Scene;
        window.ForceFieldEditorView = Visualization;
        window.ForceFieldEditorHold = Enabled =>
        {
            Held = !!Enabled;
            if (Held)
            {
                ApplyVisualization();
                Visualization.Invalidate();
                Visualization.Render(Clock.Time);
                if (EddyPreview) EddyPreview.Render(Clock.Time);
                RefreshLive();
                RefreshPlayhead();
            }
            else { Clock.LastTick = 0; requestAnimationFrame(Tick); }
            return Held;
        };
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', Initialize);
    else Initialize();
})();
