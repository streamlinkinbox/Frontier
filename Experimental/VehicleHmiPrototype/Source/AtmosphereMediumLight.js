import {El, Diagram} from './NativeEnvironmentControls.js';

// Normalised Henyey–Greenstein phase shape. This is a directional diagnostic,
// not a sky render or measured radiance. Radius squared represents relative phase.
export function AtmospherePhaseShape(G, Angle) {
    const Bounded = Math.max(-.95, Math.min(.95, G));
    return ((1 - Math.abs(Bounded)) ** 2 / (1 + Bounded ** 2 - 2 * Bounded * Math.cos(Angle))) ** 1.5;
}

export function NeutralLightTransmission(RGB) {
    return RGB[0] * .2126 + RGB[1] * .7152 + RGB[2] * .0722;
}

export function BuildAtmosphereMediumLight(C, S, Panes, UI, DensityAt, TransmissionAt) {
    const {Bind, Field, Toggle, Caption, Change} = UI;
    function Section(Parent, Name, Subtitle, Class) {
        const Root = El('section', Class);
        const Head = El('header', 'at-section-head');
        Head.append(El('h2', '', Name), El('span', '', Subtitle));
        Root.appendChild(Head);
        Parent.appendChild(Root);
        return Root;
    }
    function Instrument(Parent, W, H, Keys, Draw, Drag, Label) {
        const Canvas = Diagram(C, Parent, W, H, Draw, Drag, Label);
        const Redraw = C.Live[C.Live.length - 1];
        let LastKey;
        const Refresh = () => {
            const Key = JSON.stringify(Keys());
            if (Key === LastKey) return;
            LastKey = Key;
            Redraw();
        };
        Refresh.Static = true;
        C.Live[C.Live.length - 1] = Refresh;
        C.Bindings[C.Bindings.indexOf(Redraw)] = Refresh;
        return Canvas;
    }

    // Overview: an open channel rack instead of the rejected XY composition pad.
    const Recipe = Section(Panes.overview, 'Medium recipe', 'LIVE COEFFICIENTS', 'at-recipe');
    for (const [Key, Label, Mark, Description] of [
        ['Rayleigh', 'Molecular scattering', 'R', 'Air molecules'],
        ['Mie', 'Aerosol haze', 'M', 'Suspended particles'],
        ['Ozone', 'Ozone absorption', 'O₃', 'Selective absorption']
    ]) {
        const Channel = El('div', 'at-recipe-channel');
        const Icon = El('span', 'at-channel-mark', Mark);
        const Control = El('div', 'at-channel-control');
        Channel.append(Icon, Control);
        Recipe.appendChild(Channel);
        Field(Control, Key, Label, '×');
        Control.appendChild(El('small', 'at-channel-note', Description));
    }

    // Medium: two density ribbons share one altitude ruler. The handles edit
    // scale height, where that medium's relative density is exactly 1/e.
    const Falloff = Section(Panes.medium, 'How the air thins', 'SCALE-HEIGHT COMPARATOR', 'at-falloff');
    const Legend = El('div', 'at-ribbon-legend');
    Legend.append(El('span', '', 'Rayleigh / molecules'), El('span', '', 'Mie / aerosols'));
    Falloff.appendChild(Legend);
    let Selected = 'RayleighHeight';
    Instrument(Falloff, 640, 430, () => [S.RayleighHeight, S.MieHeight], X => {
        const Bottom = 350, Top = 55, Height = Bottom - Top;
        const Y = Metres => Bottom - Metres / 20000 * Height;
        for (const Km of [0, 5, 10, 15, 20]) {
            const PY = Y(Km * 1000);
            X.line([28, PY], [280, PY], '#b0c6aa15', 1, [2, 7]);
            X.line([360, PY], [612, PY], '#b0c6aa15', 1, [2, 7]);
            X.text(320, PY + 6, String(Km), '#a9b59b', 20);
        }
        X.text(320, 25, 'km', '#7c8c70', 20);
        for (const [Key, Centre, Sign, Colour, Fill] of [
            ['Rayleigh', 280, -1, '#b8d3bd', '#92bba324'],
            ['Mie', 360, 1, '#decfa8', '#d4b77f20']
        ]) {
            const Points = Array.from({length: 101}, (_, I) => {
                const Altitude = I / 100 * 20000;
                return [Centre + Sign * DensityAt(S, Altitude)[Key] * 235, Y(Altitude)];
            });
            const Context = X.X;
            Context.beginPath();
            Context.moveTo(Centre, Bottom);
            Points.forEach(P => Context.lineTo(...P));
            Context.lineTo(Centre, Top);
            Context.closePath();
            Context.fillStyle = Fill;
            Context.fill();
            X.path(Points, Colour, 2.5);
            X.line([Centre, Top], [Centre, Bottom], '#b0c6aa35');
            const PY = Y(S[Key + 'Height']);
            const PX = Centre + Sign * Math.exp(-1) * 235;
            X.line([Centre, PY], [PX, PY], Colour, 1, [3, 6]);
            X.circle(PX, PY, 12, '#141b12', Colour);
            X.circle(PX, PY, 4, Colour);
            X.text(Key === 'Rayleigh' ? 40 : 600, 402, (S[Key + 'Height'] / 1000).toFixed(1) + ' km', Colour, 30, Key === 'Rayleigh' ? 'left' : 'right');
        }
        X.text(320, 402, '1/e HEIGHT', '#7f9071', 17);
    }, E => {
        if (!E.Key) {
            Selected = E.Point[0] < 320 ? 'RayleighHeight' : 'MieHeight';
            Change(Selected, Math.round((350 - E.Point[1]) / 295 * 20000 / 100) * 100);
        } else {
            if (E.Key === 'ArrowLeft') Selected = 'RayleighHeight';
            else if (E.Key === 'ArrowRight') Selected = 'MieHeight';
            else Change(Selected, E.Key === 'Home' ? Selected === 'RayleighHeight' ? 8000 : 1200 : S[Selected] + (E.Key === 'ArrowUp' ? 100 : -100) * E.Step);
        }
    }, 'Scale height comparator · drag left for Rayleigh or right for Mie');
    Field(Falloff, 'RayleighHeight', 'Rayleigh scale height', 'm', 100);
    Field(Falloff, 'MieHeight', 'Mie scale height', 'm', 100);
    Caption(Falloff, 'Drag either ribbon to edit its scale height. Markers sit at 36.8% of sea-level density. Each ribbon is normalised separately.');

    const Coefficients = Section(Panes.medium, 'Constituents', 'SEA-LEVEL MULTIPLIERS', 'at-constituents');
    Field(Coefficients, 'Rayleigh', 'Rayleigh multiplier', '×');
    Field(Coefficients, 'Mie', 'Mie multiplier', '×');
    Field(Coefficients, 'Ozone', 'Ozone multiplier', '×');
    const Phase = Section(Panes.medium, 'Where light scatters', 'DIRECTIONAL RESPONSE', 'at-phase');
    Instrument(Phase, 640, 340, () => [S.MieG], X => {
        const Origin = [320, 165];
        for (const Radius of [45, 90, 135]) X.circle(...Origin, Radius, null, '#aebc9819');
        X.line([40, 165], [600, 165], '#aebc9830', 1, [3, 6]);
        const Points = Array.from({length: 361}, (_, I) => {
            const Angle = I / 360 * Math.PI * 2;
            const Radius = Math.sqrt(AtmospherePhaseShape(S.MieG, Angle)) * 135;
            return [320 + Math.cos(Angle) * Radius, 165 + Math.sin(Angle) * Radius];
        });
        X.path(Points, '#d6dcb0', 2.5);
        X.line([40, 165], [175, 165], '#bac5a7', 2);
        X.path([[162, 157], [175, 165], [162, 173]], '#bac5a7', 2);
        X.circle(...Origin, 5, '#e0e9c8');
        X.text(40, 320, 'INCIDENT LIGHT →', '#9aa98c', 20, 'left');
        X.text(600, 320, 'FORWARD →', '#9aa98c', 20, 'right');
    }, null, 'Normalised Henyey–Greenstein directional scattering shape');
    Field(Phase, 'MieG', 'Mie anisotropy', 'g');
    Caption(Phase, 'Normalised phase reference, not radiance. Squared plot radius represents relative phase strength; positive g favours forward scattering.');

    // A single compact transmission dial. RGB sweeps encode the exact channel
    // fractions; the central number is the linear-luminance transmission of white RGB.
    const Bench = Section(Panes.light, 'Light through air', 'TRANSMISSION', 'at-optical-bench');
    const Status = El('span', 'at-optical-status');
    Bench.appendChild(Status);
    Bind(() => Status.textContent = S.AtmosphereEnabled === false ? 'Atmosphere off · reference only' : S.AirMass === false ? 'Attenuation bypassed · reference only' : 'Direct sunlight · linked');
    const Dial = El('div', 'at-transmission-dial');
    Bench.appendChild(Dial);
    Instrument(Dial, 640, 330, () => [S.SunElevation, S.SunAzimuth, S.Rayleigh, S.Mie, S.Ozone, S.RayleighHeight, S.MieHeight], X => {
        const T = TransmissionAt(S), Centre = [320, 250];
        const Arc = (Radius, Fraction) => Array.from({length: 97}, (_, I) => {
            const Angle = Math.PI + I / 96 * Math.PI * Fraction;
            return [Centre[0] + Math.cos(Angle) * Radius, Centre[1] + Math.sin(Angle) * Radius];
        });
        for (const [I, Colour] of ['#d2a49b', '#b6cc9c', '#a4c6d7'].entries()) {
            const Radius = 209 - I * 31;
            X.path(Arc(Radius, 1), '#c5cfb913', 8);
            if (T[I] > 0) {
                X.path(Arc(Radius, T[I]), Colour, 8);
                const Angle = Math.PI + T[I] * Math.PI;
                X.circle(Centre[0] + Math.cos(Angle) * Radius, Centre[1] + Math.sin(Angle) * Radius, 5, Colour);
            }
        }
        X.text(96, 290, '0%', '#7f8c73', 20);
        X.text(544, 290, '100%', '#7f8c73', 20);
    }, null, 'RGB transmission sweeps · zero to one hundred percent');
    const Neutral = El('div', 'at-neutral-readout'), NeutralValue = El('strong', 'at-neutral-value');
    Neutral.append(NeutralValue, El('span', '', 'neutral-light transmission'));
    Dial.appendChild(Neutral);
    Bind(() => NeutralValue.textContent = (NeutralLightTransmission(TransmissionAt(S)) * 100).toFixed(1) + '%');
    const Outputs = El('div', 'at-output-strip');
    Bench.appendChild(Outputs);
    for (const [Index, Name] of ['Red', 'Green', 'Blue'].entries()) {
        const Channel = El('div', 'at-trans-row'), Value = El('strong');
        Channel.append(El('span', '', Name), Value);
        Outputs.appendChild(Channel);
        Bind(() => Value.textContent = (TransmissionAt(S)[Index] * 100).toFixed(1) + '%');
    }
    const Observer = El('div', 'at-transmission-context'), Elevation = El('span');
    Observer.append(Elevation, El('span', '', 'Observer / 100 m'));
    Bench.appendChild(Observer);
    Bind(() => Elevation.textContent = 'Sun / ' + S.SunElevation.toFixed(1) + '°');
    Caption(Bench, 'Isolated atmosphere, neutral RGB source. Centre value is luminance-weighted transmission; excludes clouds and source tint.');
    const Switches = El('div', 'at-path-switches');
    Bench.appendChild(Switches);
    Toggle(Switches, 'AirMass', 'Apply air-mass attenuation');
    Toggle(Switches, 'MultipleScattering', 'Multiple scattering');

    // Paint-like colour wells keep the two distinct renderer bindings visible.
    const Finish = Section(Panes.light, 'Sky & surface', 'LINEAR RGB TINTS', 'at-finish');
    const Wells = El('div', 'at-colour-wells');
    Finish.appendChild(Wells);
    for (const [Key, Name, CaptionText] of [['SkyTint', 'Sky Tint', 'Scattered sky'], ['GroundAlbedo', 'Ground Albedo', 'Ground bounce']]) {
        const Well = El('label', 'at-colour-well');
        const Swatch = El('i');
        const Input = El('input');
        Input.type = 'color';
        Input.setAttribute('aria-label', Name);
        const Hex = El('span', 'at-well-hex');
        Well.append(Swatch, El('strong', '', Name), El('small', '', CaptionText), Hex, Input);
        Wells.appendChild(Well);
        Bind(() => {
            const Value = '#' + S[Key].map(V => Math.round(Math.max(0, Math.min(1, V)) ** (1 / 2.2) * 255).toString(16).padStart(2, '0')).join('');
            Input.value = Value;
            Hex.textContent = Value.toUpperCase();
            Swatch.style.background = Value;
        });
        Input.addEventListener('input', () => {
            S[Key] = [1, 3, 5].map(N => (parseInt(Input.value.slice(N, N + 2), 16) / 255) ** 2.2);
            C.Notify(Key, S);
            C.Synchronise();
        });
    }
    Field(Finish, 'SkyScale', 'Sky brightness', '×');
    Caption(Finish, 'Ground albedo feeds the existing multiple-scattering closure; these wells are source colours, not a camera preview.');

    const Horizon = El('details', 'at-horizon-drawer');
    Horizon.appendChild(El('summary', '', 'Horizon shaping  ↗'));
    Panes.light.appendChild(Horizon);
    Field(Horizon, 'HorizonGlow', 'Horizon glow', '×');
    Field(Horizon, 'WhiteLine', 'White line', '×');
    Toggle(Horizon, 'LineAtCivilOnly', 'Civil twilight only');
    const Budget = Section(Panes.light, 'Integration', 'GPU LOOKUP QUALITY', 'at-integration');
    Field(Budget, 'TierSamples', 'Sky LUT samples', 'samples', 1);
}
