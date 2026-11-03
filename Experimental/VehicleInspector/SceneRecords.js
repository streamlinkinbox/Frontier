/*==========================================================================================================================================
                                        FRONTIER · ENVIRONMENT & 3D OBJECT RECORDS
===========================================================================================================================================*/
/* Data records for Sun, Atmosphere, Clouds and geometry-backed 3D objects. All values carry explicit units; every
   record owns editable point curves. The inspector and outliner read this catalogue through window.FrontierSceneRecords. */
'use strict';

window.FrontierSceneRecords = (() =>
{
    const clone = Value => JSON.parse(JSON.stringify(Value));
    const number = (Value, Digits = 0) => Number.isFinite(Number(Value))
        ? new Intl.NumberFormat('en-US', { minimumFractionDigits: Digits, maximumFractionDigits: Digits }).format(Number(Value))
        : '—';

    function curve(Key, Config)
    {
        const XMin = Config.xMin ?? 0;
        const XMax = Config.xMax ?? 100;
        const YMin = Config.yMin ?? 0;
        const YMax = Config.yMax ?? 100;
        const StepX = Config.stepX ?? Math.max((XMax - XMin) / 50, 0.01);
        const StepY = Config.stepY ?? Math.max((YMax - YMin) / 100, 0.01);
        const Built = {
            key: Key,
            label: Config.label || Key,
            short: Config.short || Config.label || Key,
            tone: Config.tone || 'blue',
            custom: false,
            smooth: true,
            xLabel: Config.xLabel || 'X',
            xUnit: Config.xUnit || '',
            yLabel: Config.yLabel || 'Y',
            yUnit: Config.yUnit || '',
            xMin: XMin, xMax: XMax, yMin: YMin, yMax: YMax,
            xDigits: Config.xDigits ?? 0,
            yDigits: Config.yDigits ?? (StepY < 0.1 ? 2 : StepY < 1 ? 1 : 0),
            stepX: StepX, stepY: StepY,
            points: clone(Config.points || [[XMin, YMin], [XMax, YMax]])
        };
        Built.defaultPoints = clone(Built.points);
        return Built;
    }

    const curves = (...List) => Object.fromEntries(List.map(Entry => [Entry.key, Entry]));

    const card = (label, bind, unit, tone, glyph, trend, digits = 0) =>
        ({ label, bind, unit, tone, glyph, trend, digits, derived: bind });
    const textCard = (label, bind, unit, tone, glyph, trend) =>
        ({ label, textBind: bind, unit, tone, glyph, trend });
    const field = (label, bind, unit, min, max, step, tone, glyph, group = 'general', digits = 1) =>
        ({ label, bind, unit, min, max, step, tone, glyph, group, digits, kind: 'number' });
    const textField = (label, bind, tone, glyph, group = 'general') =>
        ({ label, bind, unit: '', min: 0, max: 0, step: 1, tone, glyph, group, digits: 0, kind: 'string' });

    function hero(label, bind, unit, sideBind, sideUnit, low, digits = 0, sideDigits = 0)
    {
        return { label, bind, unit, sideBind, sideUnit, low, digits, sideDigits };
    }

    function metaText(Record)
    {
        if (!Record || !Record.metaFields || !Record.metaFields.length) return Record ? Record.tag || 'Live record' : '—';
        return Record.metaFields.map(([Key, Unit, Digits = 0]) => `${number(Record[Key], Digits)}${Unit ? ' ' + Unit : ''}`).join(' · ');
    }

    function syncObject(Record)
    {
        if (Record.kind !== 'object') return Record;
        Record.position = { x: Number(Record.posX) || 0, y: Number(Record.posY) || 0, z: Number(Record.posZ) || 0 };
        Record.rotation = { x: Number(Record.rotX) || 0, y: Number(Record.rotY) || 0, z: Number(Record.rotZ) || 0 };
        Record.scale = { x: Number(Record.scaleX) || 1, y: Number(Record.scaleY) || 1, z: Number(Record.scaleZ) || 1 };
        return Record;
    }

    function normaliseRecord(Record)
    {
        if (!Record.curves) Record.curves = {};
        Object.entries(Record.curves).forEach(([Key, Curve]) =>
        {
            Curve.key = Key;
            Curve.xMin = Number.isFinite(Curve.xMin) ? Curve.xMin : 0;
            Curve.xMax = Number.isFinite(Curve.xMax) && Curve.xMax > Curve.xMin ? Curve.xMax : 100;
            Curve.yMin = Number.isFinite(Curve.yMin) ? Curve.yMin : 0;
            Curve.yMax = Number.isFinite(Curve.yMax) && Curve.yMax > Curve.yMin ? Curve.yMax : 100;
            Curve.stepX = Number.isFinite(Curve.stepX) && Curve.stepX > 0 ? Curve.stepX : (Curve.xMax - Curve.xMin) / 50;
            Curve.stepY = Number.isFinite(Curve.stepY) && Curve.stepY > 0 ? Curve.stepY : (Curve.yMax - Curve.yMin) / 100;
            Curve.xDigits = Number.isFinite(Curve.xDigits) ? Curve.xDigits : 0;
            Curve.yDigits = Number.isFinite(Curve.yDigits) ? Curve.yDigits : 0;
            Curve.tone = ['blue', 'amber', 'red', 'green', 'violet', 'cyan', 'white'].includes(Curve.tone) ? Curve.tone : 'violet';
            Curve.smooth = Curve.smooth !== false;
            Curve.custom = !!Curve.custom;
            Curve.label = Curve.label || Key;
            Curve.short = Curve.short || Curve.label;
            if (!Array.isArray(Curve.points) || Curve.points.length < 2)
                Curve.points = [[Curve.xMin, Curve.yMin], [Curve.xMax, Curve.yMax]];
            if (!Array.isArray(Curve.defaultPoints) || Curve.defaultPoints.length < 2)
                Curve.defaultPoints = clone(Curve.points);
        });
        if (!Record.defaultCurves) Record.defaultCurves = clone(Record.curves);
        syncObject(Record);
        return Record;
    }

    function makeSun()
    {
        const Record = {
            id: 'sun', kind: 'environment', group: 'Environment', parentId: '', name: 'Sun', glyph: 'sun', tone: 'amber',
            tag: 'PROCEDURAL', geometryBacked: false, enabled: true, children: [],
            elevationDeg: 38, azimuthDeg: 142, illuminanceKlx: 98, colourK: 5800, discDeg: 0.53,
            irradianceWm2: 1040, shadowCascades: 4, penumbraDeg: 0.8, exposureEv: 0.12,
            metaFields: [['elevationDeg', '°', 0], ['illuminanceKlx', 'klx', 0]],
            hero: hero('Direct illuminance', 'illuminanceKlx', 'klx', 'elevationDeg', '°', 'Solar elevation', 0, 0),
            cards: [
                card('Elevation', 'elevationDeg', '°', 'amber', 'sun', 'Above horizon'),
                card('Azimuth', 'azimuthDeg', '°', 'blue', 'target', 'Compass bearing'),
                card('Illuminance', 'illuminanceKlx', 'klx', 'amber', 'zap', 'Direct beam'),
                card('Colour temp', 'colourK', 'K', 'white', 'droplet', 'Blackbody estimate'),
                card('Solar disc', 'discDeg', '°', 'red', 'target', 'Angular diameter', 2),
                card('Irradiance', 'irradianceWm2', 'W/m²', 'green', 'activity', 'At surface'),
                card('Cascades', 'shadowCascades', 'levels', 'violet', 'layers', 'Clipmap depth'),
                card('Penumbra', 'penumbraDeg', '°', 'cyan', 'sparkles', 'Soft-shadow width', 2)
            ],
            fields: [
                textField('Record name', 'name', 'amber', 'sun'),
                field('Elevation', 'elevationDeg', '°', -12, 90, 1, 'amber', 'sun', 'solar'),
                field('Azimuth', 'azimuthDeg', '°', 0, 360, 1, 'blue', 'target', 'solar'),
                field('Illuminance', 'illuminanceKlx', 'klx', 0, 150, 1, 'amber', 'zap', 'solar'),
                field('Colour temp', 'colourK', 'K', 2000, 10000, 50, 'white', 'droplet', 'solar'),
                field('Solar disc', 'discDeg', '°', 0.1, 3, 0.01, 'red', 'target', 'solar', 2),
                field('Irradiance', 'irradianceWm2', 'W/m²', 0, 1400, 5, 'green', 'activity', 'solar'),
                field('Penumbra', 'penumbraDeg', '°', 0, 4, 0.05, 'cyan', 'sparkles', 'solar', 2),
                field('Exposure bias', 'exposureEv', 'EV', -3, 3, 0.05, 'violet', 'gauge', 'solar', 2)
            ],
            curves: curves(
                curve('solarPath', {
                    label: 'Solar elevation', short: 'Solar path', tone: 'amber', xLabel: 'Local time', xUnit: 'h', yLabel: 'Elevation', yUnit: '°',
                    xMin: 6, xMax: 20, yMin: -10, yMax: 70, stepX: 0.25, stepY: 1,
                    points: [[6, -4], [8, 12], [10, 32], [12, 52], [14, 58], [16, 42], [18, 18], [20, -6]]
                }),
                curve('spectrum', {
                    label: 'Spectral irradiance', short: 'Spectrum', tone: 'violet', xLabel: 'Wavelength', xUnit: 'nm', yLabel: 'Irradiance', yUnit: 'W/m²/nm',
                    xMin: 380, xMax: 780, yMin: 0, yMax: 1.8, stepX: 10, stepY: 0.05, yDigits: 2,
                    points: [[380, 0.45], [440, 1.25], [500, 1.62], [560, 1.72], [620, 1.42], [700, 0.95], [780, 0.48]]
                }),
                curve('shadowDistance', {
                    label: 'Shadow cascade distance', short: 'Cascades', tone: 'blue', xLabel: 'Cascade', xUnit: '#', yLabel: 'End distance', yUnit: 'm',
                    xMin: 0, xMax: 4, yMin: 0, yMax: 500, stepX: 1, stepY: 10,
                    points: [[0, 0], [1, 24], [2, 82], [3, 210], [4, 500]]
                }),
                curve('penumbra', {
                    label: 'Penumbra vs elevation', short: 'Penumbra', tone: 'cyan', xLabel: 'Elevation', xUnit: '°', yLabel: 'Penumbra', yUnit: '°',
                    xMin: 0, xMax: 90, yMin: 0, yMax: 3, stepX: 1, stepY: 0.05, yDigits: 2,
                    points: [[0, 2.6], [15, 1.8], [35, 0.9], [60, 0.45], [90, 0.25]]
                })
            )
        };
        return normaliseRecord(Record);
    }

    function makeAtmosphere()
    {
        const Record = {
            id: 'atmosphere', kind: 'environment', group: 'Environment', parentId: '', name: 'Atmosphere', glyph: 'atmosphere', tone: 'cyan',
            tag: 'PROCEDURAL', geometryBacked: false, enabled: true, children: [],
            rayleigh: 1.0, mie: 2.3, ozone: 1.0, scaleHeightKm: 8.0, turbidity: 2.4,
            airDensity: 1.225, groundAlbedoPct: 18, exposureEv: 0.0, viewDistanceKm: 42,
            metaFields: [['mie', '×', 1], ['ozone', '×', 1]],
            hero: hero('Mie scattering', 'mie', '×', 'rayleigh', '×', 'Rayleigh multiplier', 1, 1),
            cards: [
                card('Mie', 'mie', '×', 'cyan', 'atmosphere', 'Aerosols', 2),
                card('Rayleigh', 'rayleigh', '×', 'blue', 'droplet', 'Molecular', 2),
                card('Ozone', 'ozone', '×', 'violet', 'layers', 'Absorption', 2),
                card('Scale height', 'scaleHeightKm', 'km', 'green', 'ruler', 'Density falloff', 1),
                card('Air density', 'airDensity', 'kg/m³', 'white', 'weight', 'Sea level', 3),
                card('Turbidity', 'turbidity', '×', 'amber', 'wind', 'Aerosol load', 2),
                card('Albedo', 'groundAlbedoPct', '%', 'green', 'target', 'Ground return'),
                card('View distance', 'viewDistanceKm', 'km', 'blue', 'eyeoff', 'Aerial fog')
            ],
            fields: [
                textField('Preset name', 'name', 'cyan', 'atmosphere'),
                field('Rayleigh', 'rayleigh', '×', 0, 3, 0.01, 'blue', 'droplet', 'scattering', 2),
                field('Mie', 'mie', '×', 0, 6, 0.01, 'cyan', 'atmosphere', 'scattering', 2),
                field('Ozone', 'ozone', '×', 0, 3, 0.01, 'violet', 'layers', 'scattering', 2),
                field('Scale height', 'scaleHeightKm', 'km', 1, 20, 0.1, 'green', 'ruler', 'scattering', 1),
                field('Turbidity', 'turbidity', '×', 1, 10, 0.1, 'amber', 'wind', 'scattering', 1),
                field('Air density', 'airDensity', 'kg/m³', 0, 2, 0.001, 'white', 'weight', 'scattering', 3),
                field('Ground albedo', 'groundAlbedoPct', '%', 0, 100, 1, 'green', 'target', 'scattering'),
                field('Exposure', 'exposureEv', 'EV', -3, 3, 0.05, 'violet', 'gauge', 'scattering', 2),
                field('View distance', 'viewDistanceKm', 'km', 1, 200, 1, 'blue', 'eyeoff', 'scattering')
            ],
            curves: curves(
                curve('density', {
                    label: 'Density by altitude', short: 'Density', tone: 'blue', xLabel: 'Altitude', xUnit: 'km', yLabel: 'Density', yUnit: 'kg/m³',
                    xMin: 0, xMax: 80, yMin: 0, yMax: 1.3, stepX: 1, stepY: 0.01, yDigits: 2,
                    points: [[0, 1.225], [8, 0.53], [20, 0.09], [40, 0.004], [80, 0.0001]]
                }),
                curve('rayleighPhase', {
                    label: 'Rayleigh phase', short: 'Rayleigh', tone: 'violet', xLabel: 'Scattering angle', xUnit: '°', yLabel: 'Relative', yUnit: '×',
                    xMin: 0, xMax: 180, yMin: 0, yMax: 1.2, stepX: 2, stepY: 0.01, yDigits: 2,
                    points: [[0, 1.05], [45, 0.83], [90, 0.75], [135, 0.83], [180, 1.05]]
                }),
                curve('miePhase', {
                    label: 'Mie phase', short: 'Mie', tone: 'cyan', xLabel: 'Scattering angle', xUnit: '°', yLabel: 'Relative', yUnit: '×',
                    xMin: 0, xMax: 180, yMin: 0, yMax: 4, stepX: 2, stepY: 0.05, yDigits: 2,
                    points: [[0, 3.8], [20, 2.4], [60, 0.75], [120, 0.28], [180, 0.42]]
                }),
                curve('ozoneAbsorption', {
                    label: 'Ozone absorption', short: 'Ozone', tone: 'green', xLabel: 'Wavelength', xUnit: 'nm', yLabel: 'Absorption', yUnit: '%',
                    xMin: 300, xMax: 750, yMin: 0, yMax: 100, stepX: 10, stepY: 1,
                    points: [[300, 96], [350, 72], [450, 22], [550, 8], [650, 3], [750, 1]]
                })
            )
        };
        return normaliseRecord(Record);
    }

    function makeCloudChild(Parent, Id, Name, Glyph, Tone, Tag, Values, CurveSet)
    {
        const Record = Object.assign({
            id: Id, kind: 'environment-child', group: 'Environment', parentId: Parent.id, name: Name, glyph: Glyph, tone: Tone,
            tag: Tag, geometryBacked: false, enabled: true, children: [],
            metaFields: [['coveragePct', '%', 0], ['baseM', 'm', 0]],
            hero: hero('Layer coverage', 'coveragePct', '%', 'baseM', 'm', 'Cloud base', 0, 0),
            cards: [], fields: [textField('Layer name', 'name', Tone, Glyph)], curves: CurveSet
        }, Values);
        Record.fields = [textField('Layer name', 'name', Tone, Glyph), ...(Values.fields || [])];
        Record.cards = Values.cards || [];
        Record.metaFields = Values.metaFields || Record.metaFields;
        Record.hero = Values.hero || Record.hero;
        delete Record.fieldsExtra;
        return normaliseRecord(Record);
    }

    function makeClouds()
    {
        const Record = {
            id: 'clouds', kind: 'environment', group: 'Environment', parentId: '', name: 'Clouds', glyph: 'cloud', tone: 'blue',
            tag: 'PROCEDURAL', geometryBacked: false, enabled: true, children: [],
            coveragePct: 42, baseM: 1750, topM: 4200, thicknessM: 2450, densityGm3: 0.68,
            windMs: 6.5, dropletUm: 14, layers: 3, precipitationPct: 8,
            metaFields: [['coveragePct', '%', 0], ['baseM', 'm', 0]],
            hero: hero('Cloud coverage', 'coveragePct', '%', 'baseM', 'm', 'Layer base altitude'),
            cards: [
                card('Coverage', 'coveragePct', '%', 'blue', 'cloud', 'Sky fraction'),
                card('Base', 'baseM', 'm', 'cyan', 'ruler', 'Above ground'),
                card('Top', 'topM', 'm', 'violet', 'layers', 'Anvil ceiling'),
                card('Thickness', 'thicknessM', 'm', 'green', 'box', 'Vertical extent'),
                card('Density', 'densityGm3', 'g/m³', 'white', 'weight', 'Liquid water', 2),
                card('Wind', 'windMs', 'm/s', 'cyan', 'wind', 'Layer advection', 1),
                card('Droplets', 'dropletUm', 'µm', 'amber', 'droplet', 'Effective radius'),
                card('Layers', 'layers', 'count', 'blue', 'layers', 'Active strata')
            ],
            fields: [
                textField('Record name', 'name', 'blue', 'cloud'),
                field('Coverage', 'coveragePct', '%', 0, 100, 1, 'blue', 'cloud', 'layer'),
                field('Base altitude', 'baseM', 'm', 0, 8000, 25, 'cyan', 'ruler', 'layer'),
                field('Top altitude', 'topM', 'm', 0, 12000, 25, 'violet', 'layers', 'layer'),
                field('Thickness', 'thicknessM', 'm', 0, 6000, 25, 'green', 'box', 'layer'),
                field('Density', 'densityGm3', 'g/m³', 0, 3, 0.01, 'white', 'weight', 'layer', 2),
                field('Wind speed', 'windMs', 'm/s', 0, 30, 0.1, 'cyan', 'wind', 'layer', 1),
                field('Droplet radius', 'dropletUm', 'µm', 1, 60, 1, 'amber', 'droplet', 'layer'),
                field('Layers', 'layers', 'count', 1, 8, 1, 'blue', 'layers', 'layer'),
                field('Precipitation', 'precipitationPct', '%', 0, 100, 1, 'red', 'droplet', 'layer')
            ],
            curves: curves(
                curve('coverageProfile', {
                    label: 'Coverage by altitude', short: 'Coverage', tone: 'blue', xLabel: 'Altitude', xUnit: 'km', yLabel: 'Coverage', yUnit: '%',
                    xMin: 0, xMax: 12, yMin: 0, yMax: 100, stepX: 0.25, stepY: 1,
                    points: [[0, 0], [1.5, 12], [2.5, 48], [4.2, 68], [7, 34], [10, 12], [12, 0]]
                }),
                curve('densityProfile', {
                    label: 'Liquid water density', short: 'Density', tone: 'white', xLabel: 'Height in layer', xUnit: 'm', yLabel: 'Density', yUnit: 'g/m³',
                    xMin: 0, xMax: 2500, yMin: 0, yMax: 1.5, stepX: 25, stepY: 0.01, yDigits: 2,
                    points: [[0, 0.12], [400, 0.55], [1000, 0.92], [1700, 0.68], [2500, 0.08]]
                }),
                curve('windShear', {
                    label: 'Wind shear', short: 'Wind', tone: 'cyan', xLabel: 'Altitude', xUnit: 'm', yLabel: 'Wind speed', yUnit: 'm/s',
                    xMin: 0, xMax: 3000, yMin: 0, yMax: 20, stepX: 50, stepY: 0.1, yDigits: 1,
                    points: [[0, 3.2], [600, 5.1], [1400, 8.4], [2200, 12.0], [3000, 16.5]]
                }),
                curve('dropletDistribution', {
                    label: 'Droplet size distribution', short: 'Droplets', tone: 'amber', xLabel: 'Radius', xUnit: 'µm', yLabel: 'Population', yUnit: '%',
                    xMin: 1, xMax: 50, yMin: 0, yMax: 100, stepX: 1, stepY: 1,
                    points: [[1, 4], [8, 32], [14, 86], [22, 68], [34, 22], [50, 3]]
                })
            )
        };
        Record.children = [
            makeCloudChild(Record, 'clouds:cumulus', 'Cumulus layer', 'cloud', 'blue', 'LAYER', {
                coveragePct: 62, baseM: 1750, thicknessM: 1450, densityGm3: 0.74, windMs: 6.5, layers: 2,
                metaFields: [['coveragePct', '%', 0], ['baseM', 'm', 0]],
                hero: hero('Layer coverage', 'coveragePct', '%', 'baseM', 'm', 'Cumulus base'),
                cards: [
                    card('Coverage', 'coveragePct', '%', 'blue', 'cloud', 'Within layer'),
                    card('Base', 'baseM', 'm', 'cyan', 'ruler', 'Above ground'),
                    card('Thickness', 'thicknessM', 'm', 'green', 'box', 'Vertical extent'),
                    card('Density', 'densityGm3', 'g/m³', 'white', 'weight', 'Liquid water', 2),
                    card('Wind', 'windMs', 'm/s', 'cyan', 'wind', 'Advection', 1),
                    card('Layers', 'layers', 'count', 'violet', 'layers', 'Sub-strata')
                ],
                fields: [
                    field('Coverage', 'coveragePct', '%', 0, 100, 1, 'blue', 'cloud', 'layer'),
                    field('Base', 'baseM', 'm', 0, 8000, 25, 'cyan', 'ruler', 'layer'),
                    field('Thickness', 'thicknessM', 'm', 0, 6000, 25, 'green', 'box', 'layer'),
                    field('Density', 'densityGm3', 'g/m³', 0, 3, 0.01, 'white', 'weight', 'layer', 2),
                    field('Wind', 'windMs', 'm/s', 0, 30, 0.1, 'cyan', 'wind', 'layer', 1)
                ]
            }, curves(
                curve('cumulusDensity', {
                    label: 'Cumulus density', short: 'Density', tone: 'blue', xLabel: 'Height', xUnit: 'm', yLabel: 'Density', yUnit: 'g/m³',
                    xMin: 0, xMax: 1500, yMin: 0, yMax: 1.2, stepX: 25, stepY: 0.01, yDigits: 2,
                    points: [[0, 0.1], [300, 0.5], [750, 0.9], [1100, 0.7], [1500, 0.12]]
                }),
                curve('cumulusEdge', {
                    label: 'Edge erosion', short: 'Erosion', tone: 'cyan', xLabel: 'Wind', xUnit: 'm/s', yLabel: 'Erosion', yUnit: '%',
                    xMin: 0, xMax: 25, yMin: 0, yMax: 100, stepX: 0.5, stepY: 1,
                    points: [[0, 4], [5, 18], [10, 42], [18, 76], [25, 94]]
                })
            )),
            makeCloudChild(Record, 'clouds:fog', 'Local volumetric fog', 'fog', 'violet', 'VOLUME', {
                coveragePct: 34, baseM: 20, thicknessM: 180, densityGm3: 0.32, windMs: 1.2, falloff: 0.14,
                metaFields: [['densityGm3', 'g/m³', 2], ['falloff', '×', 2]],
                hero: hero('Fog density', 'densityGm3', 'g/m³', 'falloff', '×', 'Height falloff', 2, 2),
                cards: [
                    card('Density', 'densityGm3', 'g/m³', 'violet', 'weight', 'Volume extinction', 2),
                    card('Falloff', 'falloff', '×', 'blue', 'curve', 'Height response', 2),
                    card('Base', 'baseM', 'm', 'cyan', 'ruler', 'Ground offset'),
                    card('Thickness', 'thicknessM', 'm', 'green', 'box', 'Vertical extent'),
                    card('Coverage', 'coveragePct', '%', 'white', 'cloud', 'Local area'),
                    card('Wind', 'windMs', 'm/s', 'cyan', 'wind', 'Drift', 1)
                ],
                fields: [
                    field('Density', 'densityGm3', 'g/m³', 0, 2, 0.01, 'violet', 'weight', 'volume', 2),
                    field('Falloff', 'falloff', '×', 0, 1, 0.01, 'blue', 'curve', 'volume', 2),
                    field('Base', 'baseM', 'm', 0, 1000, 5, 'cyan', 'ruler', 'volume'),
                    field('Thickness', 'thicknessM', 'm', 0, 1000, 5, 'green', 'box', 'volume'),
                    field('Wind', 'windMs', 'm/s', 0, 10, 0.1, 'cyan', 'wind', 'volume', 1)
                ]
            }, curves(
                curve('fogDensity', {
                    label: 'Fog height density', short: 'Height', tone: 'violet', xLabel: 'Height', xUnit: 'm', yLabel: 'Density', yUnit: 'g/m³',
                    xMin: 0, xMax: 200, yMin: 0, yMax: 0.6, stepX: 5, stepY: 0.01, yDigits: 2,
                    points: [[0, 0.42], [40, 0.34], [100, 0.18], [160, 0.06], [200, 0.01]]
                }),
                curve('fogFalloff', {
                    label: 'Fog distance falloff', short: 'Falloff', tone: 'blue', xLabel: 'Distance', xUnit: 'm', yLabel: 'Extinction', yUnit: '%',
                    xMin: 0, xMax: 300, yMin: 0, yMax: 100, stepX: 10, stepY: 1,
                    points: [[0, 100], [60, 72], [140, 38], [220, 14], [300, 2]]
                })
            ))
        ];
        normaliseRecord(Record);
        Record.children.forEach(normaliseRecord);
        return Record;
    }

    function objectCurves(Tone, Seed = 0)
    {
        const Shift = Seed * 6;
        return curves(
            curve('lodRetention', {
                label: 'LOD triangle retention', short: 'LOD', tone: 'blue', xLabel: 'Distance', xUnit: 'm', yLabel: 'Retention', yUnit: '%',
                xMin: 0, xMax: 200, yMin: 0, yMax: 100, stepX: 5, stepY: 1,
                points: [[0, 100], [25, 92 - Shift], [60, 68 - Shift], [110, 34 - Shift], [160, 14], [200, 6]]
            }),
            curve('shadowFade', {
                label: 'Shadow fade', short: 'Shadow', tone: 'violet', xLabel: 'Distance', xUnit: 'm', yLabel: 'Opacity', yUnit: '%',
                xMin: 0, xMax: 160, yMin: 0, yMax: 100, stepX: 5, stepY: 1,
                points: [[0, 100], [30, 86], [70, 52], [110, 20], [160, 0]]
            }),
            curve('windResponse', {
                label: 'Wind response', short: 'Wind', tone: Tone, xLabel: 'Wind speed', xUnit: 'm/s', yLabel: 'Deflection', yUnit: 'cm',
                xMin: 0, xMax: 30, yMin: 0, yMax: 24, stepX: 0.5, stepY: 0.2, yDigits: 1,
                points: [[0, 0], [5, 1.2 + Seed], [10, 4.5 + Seed], [18, 11 + Seed], [30, 19 + Seed]]
            }),
            curve('roughnessAngle', {
                label: 'Roughness by view angle', short: 'Roughness', tone: 'amber', xLabel: 'View angle', xUnit: '°', yLabel: 'Roughness', yUnit: 'µ',
                xMin: 0, xMax: 90, yMin: 0, yMax: 1, stepX: 1, stepY: 0.01, yDigits: 2,
                points: [[0, 0.22], [25, 0.28], [50, 0.42], [70, 0.62], [90, 0.78]]
            })
        );
    }

    function componentCurves(Parent, Component)
    {
        if (Component === 'mesh')
        {
            return curves(
                curve('meshLod', {
                    label: 'Mesh LOD triangles', short: 'LOD', tone: 'blue', xLabel: 'LOD', xUnit: '#', yLabel: 'Triangles', yUnit: 'tri',
                    xMin: 0, xMax: 4, yMin: 0, yMax: Math.max(200, Parent.triangles * 2), stepX: 1, stepY: 5,
                    points: [[0, Parent.triangles], [1, Math.round(Parent.triangles * 0.62)], [2, Math.round(Parent.triangles * 0.34)], [3, Math.round(Parent.triangles * 0.16)], [4, Math.round(Parent.triangles * 0.06)]]
                }),
                curve('streamingCost', {
                    label: 'Streaming cost', short: 'Stream', tone: 'cyan', xLabel: 'Distance', xUnit: 'm', yLabel: 'Pages', yUnit: 'count',
                    xMin: 0, xMax: 200, yMin: 0, yMax: 64, stepX: 5, stepY: 1,
                    points: [[0, 52], [40, 38], [90, 22], [140, 10], [200, 3]]
                })
            );
        }
        if (Component === 'material')
        {
            return curves(
                curve('fresnel', {
                    label: 'Fresnel response', short: 'Fresnel', tone: 'violet', xLabel: 'View angle', xUnit: '°', yLabel: 'Reflectance', yUnit: '×',
                    xMin: 0, xMax: 90, yMin: 0, yMax: 1, stepX: 1, stepY: 0.01, yDigits: 2,
                    points: [[0, 0.04], [30, 0.06], [55, 0.16], [75, 0.46], [90, 0.92]]
                }),
                curve('roughnessAngle', {
                    label: 'Roughness by angle', short: 'Roughness', tone: 'amber', xLabel: 'View angle', xUnit: '°', yLabel: 'Roughness', yUnit: 'µ',
                    xMin: 0, xMax: 90, yMin: 0, yMax: 1, stepX: 1, stepY: 0.01, yDigits: 2,
                    points: [[0, Parent.roughnessPct / 100], [30, Parent.roughnessPct / 100 + 0.05], [60, Parent.roughnessPct / 100 + 0.16], [90, 0.86]]
                })
            );
        }
        return curves(
            curve('contactStiffness', {
                label: 'Contact stiffness', short: 'Contact', tone: 'green', xLabel: 'Penetration', xUnit: 'mm', yLabel: 'Force', yUnit: 'kN',
                xMin: 0, xMax: 20, yMin: 0, yMax: 120, stepX: 0.5, stepY: 1,
                points: [[0, 0], [2, 18], [6, 52], [12, 88], [20, 112]]
            }),
            curve('frictionSlip', {
                label: 'Friction vs slip', short: 'Friction', tone: 'red', xLabel: 'Slip', xUnit: '%', yLabel: 'Friction', yUnit: 'µ',
                xMin: 0, xMax: 100, yMin: 0, yMax: 1.4, stepX: 2, stepY: 0.01, yDigits: 2,
                points: [[0, 0.42], [15, 0.92], [35, 1.14], [65, 0.96], [100, 0.68]]
            })
        );
    }

    function makeObjectChildren(Parent)
    {
        const Templates = [
            {
                suffix: 'mesh', name: 'Mesh', glyph: 'box', tone: 'blue', tag: 'GEOMETRY',
                hero: hero('Triangle count', 'triangles', 'tri', 'vertices', 'vtx', 'Indexed triangle mesh'),
                metaFields: [['triangles', 'tri', 0], ['vertices', 'vtx', 0]],
                cards: [
                    card('Triangles', 'triangles', 'tri', 'blue', 'box', 'Indexed faces'),
                    card('Vertices', 'vertices', 'vtx', 'cyan', 'target', 'Unique points'),
                    card('LODs', 'lods', 'levels', 'violet', 'layers', 'Streaming tiers'),
                    card('Materials', 'materials', 'slots', 'amber', 'layers', 'Surface slots'),
                    card('Bounds', 'boundsM3', 'm³', 'green', 'box', 'Local volume', 2),
                    card('Stream pages', 'streamPages', 'pages', 'white', 'folder', 'Resident budget')
                ],
                fields: [
                    field('Triangles', 'triangles', 'tri', 4, 100000, 1, 'blue', 'box', 'mesh'),
                    field('Vertices', 'vertices', 'vtx', 4, 100000, 1, 'cyan', 'target', 'mesh'),
                    field('LOD levels', 'lods', 'levels', 1, 8, 1, 'violet', 'layers', 'mesh'),
                    field('Stream pages', 'streamPages', 'pages', 1, 128, 1, 'white', 'folder', 'mesh')
                ]
            },
            {
                suffix: 'material', name: 'Material', glyph: 'droplet', tone: 'violet', tag: 'SURFACE',
                hero: hero('Roughness', 'roughnessPct', '%', 'metallicPct', '%', 'Metallic response'),
                metaFields: [['roughnessPct', '%', 0], ['metallicPct', '%', 0]],
                cards: [
                    card('Roughness', 'roughnessPct', '%', 'violet', 'droplet', 'Microfacet spread'),
                    card('Metallic', 'metallicPct', '%', 'blue', 'target', 'Conductor blend'),
                    card('Opacity', 'opacityPct', '%', 'white', 'eyeoff', 'Transmission'),
                    textCard('Base colour', 'baseColour', '', 'amber', 'sparkles', 'Linear albedo'),
                    card('IOR', 'ior', '', 'cyan', 'droplet', 'Index of refraction', 2),
                    card('Flake density', 'flakeDensity', '%', 'green', 'sparkles', 'Paint flakes')
                ],
                fields: [
                    textField('Base colour', 'baseColour', 'amber', 'sparkles', 'surface'),
                    field('Roughness', 'roughnessPct', '%', 0, 100, 1, 'violet', 'droplet', 'surface'),
                    field('Metallic', 'metallicPct', '%', 0, 100, 1, 'blue', 'target', 'surface'),
                    field('Opacity', 'opacityPct', '%', 0, 100, 1, 'white', 'eyeoff', 'surface'),
                    field('IOR', 'ior', '', 1, 2.5, 0.01, 'cyan', 'droplet', 'surface', 2)
                ]
            },
            {
                suffix: 'collider', name: 'Collider', glyph: 'fit', tone: 'green', tag: 'PHYSICS',
                hero: hero('Collision mass', 'massKg', 'kg', 'frictionMu', 'µ', 'Static friction', 0, 2),
                metaFields: [['massKg', 'kg', 0], ['frictionMu', 'µ', 2]],
                cards: [
                    card('Mass', 'massKg', 'kg', 'green', 'weight', 'Rigid body'),
                    card('Friction', 'frictionMu', 'µ', 'red', 'target', 'Static coefficient', 2),
                    card('Restitution', 'restitution', '×', 'amber', 'activity', 'Bounce', 2),
                    card('Hulls', 'hulls', 'count', 'blue', 'box', 'Convex parts'),
                    card('Margin', 'marginMm', 'mm', 'cyan', 'ruler', 'Collision skin', 1),
                    card('Solver', 'solverIterations', 'iters', 'violet', 'gauge', 'Constraint steps')
                ],
                fields: [
                    field('Mass', 'massKg', 'kg', 0, 10000, 5, 'green', 'weight', 'physics'),
                    field('Friction', 'frictionMu', 'µ', 0, 2, 0.01, 'red', 'target', 'physics', 2),
                    field('Restitution', 'restitution', '×', 0, 1, 0.01, 'amber', 'activity', 'physics', 2),
                    field('Margin', 'marginMm', 'mm', 0, 100, 0.5, 'cyan', 'ruler', 'physics', 1)
                ]
            }
        ];
        return Templates.map(Template =>
        {
            const Record = Object.assign({
                id: `${Parent.id}:${Template.suffix}`,
                kind: 'component', suffix: Template.suffix, group: 'Objects', parentId: Parent.id,
                name: `${Parent.name} · ${Template.name}`, glyph: Template.glyph, tone: Template.tone,
                tag: Template.tag, geometryBacked: false, enabled: true, children: [],
                triangles: Parent.triangles, vertices: Parent.vertices, lods: Parent.lods, materials: Parent.materials,
                boundsM3: Parent.boundsM3, streamPages: 12, roughnessPct: Parent.roughnessPct,
                metallicPct: Parent.metallicPct, opacityPct: Parent.opacityPct, baseColour: Parent.baseColour,
                ior: Parent.ior, flakeDensity: 18, massKg: Parent.massKg, frictionMu: 0.86,
                restitution: 0.12, hulls: 2, marginMm: 8, solverIterations: 8,
                metaFields: Template.metaFields, hero: Template.hero, cards: Template.cards,
                fields: [textField('Component name', 'name', Template.tone, Template.glyph), ...Template.fields],
                curves: componentCurves(Parent, Template.suffix)
            });
            return normaliseRecord(Record);
        });
    }

    function makeObject(Config)
    {
        const Record = Object.assign({
            kind: 'object', group: 'Objects', parentId: '', tag: 'GEOMETRY', geometryBacked: true, enabled: true, children: [],
            shape: 'box', rotX: 0, rotZ: 0, baseColour: '#9fb4d6', ior: 1.45, streamPages: 12,
            metaFields: [['triangles', 'tri', 0], ['lodDistanceM', 'm', 0]],
            hero: hero('Triangle count', 'triangles', 'tri', 'boundsM3', 'm³', 'Local bounds volume', 0, 2),
            cards: [
                card('Triangles', 'triangles', 'tri', 'blue', 'box', 'Indexed faces'),
                card('Vertices', 'vertices', 'vtx', 'cyan', 'target', 'Unique points'),
                card('Bounds', 'boundsM3', 'm³', 'green', 'box', 'Local volume', 2),
                card('Mass', 'massKg', 'kg', 'amber', 'weight', 'Rigid body'),
                card('LOD distance', 'lodDistanceM', 'm', 'violet', 'ruler', 'Full detail'),
                card('Materials', 'materials', 'slots', 'red', 'layers', 'Surface slots'),
                card('Roughness', 'roughnessPct', '%', 'white', 'droplet', 'Microfacet spread'),
                card('LODs', 'lods', 'levels', 'blue', 'layers', 'Streaming tiers')
            ],
            fields: [],
            curves: objectCurves(Config.tone, Config.seed || 0)
        }, Config);
        Record.fields = [
            textField('Object name', 'name', Record.tone, Record.glyph, 'transform'),
            field('Position X', 'posX', 'm', -20, 20, 0.1, 'blue', 'ruler', 'transform', 1),
            field('Position Y', 'posY', 'm', -10, 20, 0.1, 'green', 'ruler', 'transform', 1),
            field('Position Z', 'posZ', 'm', -20, 20, 0.1, 'cyan', 'ruler', 'transform', 1),
            field('Rotation Y', 'rotY', '°', -180, 180, 1, 'violet', 'reset', 'transform'),
            field('Scale X', 'scaleX', '×', 0.1, 5, 0.05, 'blue', 'fit', 'transform', 2),
            field('Scale Y', 'scaleY', '×', 0.1, 5, 0.05, 'green', 'fit', 'transform', 2),
            field('Scale Z', 'scaleZ', '×', 0.1, 5, 0.05, 'cyan', 'fit', 'transform', 2),
            field('Triangles', 'triangles', 'tri', 4, 100000, 1, 'blue', 'box', 'mesh'),
            field('Vertices', 'vertices', 'vtx', 4, 100000, 1, 'cyan', 'target', 'mesh'),
            field('Local bounds', 'boundsM3', 'm³', 0, 1000, 0.1, 'green', 'box', 'mesh', 2),
            field('LOD distance', 'lodDistanceM', 'm', 1, 500, 1, 'violet', 'ruler', 'mesh'),
            field('Mass', 'massKg', 'kg', 0, 10000, 5, 'amber', 'weight', 'physics'),
            field('Roughness', 'roughnessPct', '%', 0, 100, 1, 'white', 'droplet', 'surface'),
            field('Metallic', 'metallicPct', '%', 0, 100, 1, 'blue', 'target', 'surface')
        ];
        normaliseRecord(Record);
        Record.children = makeObjectChildren(Record);
        return Record;
    }

    function create()
    {
        const environment = [makeSun(), makeAtmosphere(), makeClouds()];
        const objects = [
            makeObject({
                id: 'obj-plinth', seed: 0, name: 'Proving plinth', glyph: 'box', tone: 'blue', shape: 'box',
                posX: -3.2, posY: 0.8, posZ: -1.2, rotY: 18, scaleX: 1.6, scaleY: 1.4, scaleZ: 1.6,
                vertices: 8, triangles: 12, boundsM3: 3.58, massKg: 820, lodDistanceM: 42, shadowDistanceM: 76,
                materials: 1, lods: 3, roughnessPct: 38, metallicPct: 12, opacityPct: 100
            }),
            makeObject({
                id: 'obj-gantry', seed: 1, name: 'Start gantry', glyph: 'box', tone: 'amber', shape: 'box',
                posX: 2.8, posY: 1.7, posZ: -2.6, rotY: -12, scaleX: 2.2, scaleY: 3.4, scaleZ: 0.75,
                vertices: 8, triangles: 12, boundsM3: 5.61, massKg: 1640, lodDistanceM: 68, shadowDistanceM: 110,
                materials: 2, lods: 4, roughnessPct: 52, metallicPct: 64, opacityPct: 100
            }),
            makeObject({
                id: 'obj-beacon', seed: 2, name: 'Track beacon', glyph: 'target', tone: 'red', shape: 'cone',
                posX: 0.6, posY: 1.0, posZ: 2.2, rotY: 0, scaleX: 0.9, scaleY: 1.8, scaleZ: 0.9,
                vertices: 10, triangles: 16, boundsM3: 1.27, massKg: 145, lodDistanceM: 28, shadowDistanceM: 48,
                materials: 2, lods: 3, roughnessPct: 28, metallicPct: 22, opacityPct: 100
            }),
            makeObject({
                id: 'obj-proxy', seed: 3, name: 'Suzanne proxy', glyph: 'sparkles', tone: 'violet', shape: 'icosahedron',
                posX: -0.6, posY: 1.25, posZ: 1.1, rotY: 28, scaleX: 1.05, scaleY: 1.05, scaleZ: 1.05,
                vertices: 12, triangles: 20, boundsM3: 2.14, massKg: 420, lodDistanceM: 36, shadowDistanceM: 62,
                materials: 1, lods: 4, roughnessPct: 44, metallicPct: 8, opacityPct: 100
            })
        ];
        const all = [
            ...environment,
            ...environment.flatMap(Record => Record.children || []),
            ...objects,
            ...objects.flatMap(Record => Record.children || [])
        ];
        const map = new Map(all.map(Record => [Record.id, Record]));
        return { environment, objects, all, map };
    }

    return { create, makeObject, metaText, normaliseRecord, syncObject, curve, number };
})();
