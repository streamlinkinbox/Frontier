import {El, Diagram} from './NativeEnvironmentControls.js';
import {ObservatorySlider} from './ObservatorySlider.js';

export function AtmosphereProbeAltitude(Value) {
    return Number.isFinite(Value) ? Math.max(0, Math.min(30000, Math.round(Value / 500) * 500)) : 0;
}

// An inspectable density diagram, not an extra sky renderer. The shell spacing is
// exaggerated; every density readout comes from the existing exponential model.
export function BuildAtmosphereColumn(C, Parent, Settings, DensityAt) {
    const Probe = {Altitude: 8000};
    const Root = El('section', 'at-column-scan');
    const Heading = El('div', 'at-scan-heading');
    Heading.append(El('span', '', 'Atmospheric column'), El('span', 'at-scan-id', '01 / DENSITY SCAN'));
    Root.appendChild(Heading);
    Parent.prepend(Root);

    const Result = El('div', 'at-scan-result');
    const Density = El('strong');
    Result.append(El('span', '', 'Relative molecular density'), Density, El('small', '', '% of sea level'));
    Root.appendChild(Result);

    const Height = El('span', 'at-probe-tag');
    Root.appendChild(Height);
    const Aerosol = El('div', 'at-aerosol-result');
    const AerosolValue = El('strong');
    Aerosol.append(El('span', '', 'Aerosol density'), AerosolValue);
    Root.appendChild(Aerosol);

    Diagram(C, Root, 640, 520, Pen => {
        const Context = Pen.X;
        const Centre = [320, 453];
        const Radius = 240;
        for (let Km = 30; Km >= 0; Km -= .5) {
            const D = DensityAt(Settings, Km * 1000);
            const Alpha = Math.min(.6, .025 + D.Rayleigh * Settings.Rayleigh * .19 + D.Mie * Settings.Mie * .08);
            Pen.circle(...Centre, Radius + Km * 4, null, `rgba(173,207,175,${Alpha})`);
        }
        for (const Km of [0, 5, 10, 20, 30]) {
            const R = Radius + Km * 4;
            Pen.path(Array.from({length: 65}, (_, I) => {
                const A = -Math.PI + I / 64 * Math.PI;
                return [Centre[0] + Math.cos(A) * R, Centre[1] + Math.sin(A) * R];
            }), '#d9e4c12a', 1, [2, 7]);
        }
        const Ground = Context.createRadialGradient(250, 300, 10, 320, 453, 270);
        Ground.addColorStop(0, '#3b4239');
        Ground.addColorStop(.5, '#232922');
        Ground.addColorStop(1, '#101511');
        Pen.circle(...Centre, Radius, Ground, '#b4c59955');
        for (const R of [65, 120, 180]) Pen.ellipse(320, 453, R, 240, null, '#acbaa315');
        for (const R of [70, 140, 210]) Pen.ellipse(320, 453, 240, R, null, '#acbaa315');

        const R = Radius + Probe.Altitude / 1000 * 4;
        const Point = [320 + Math.cos(-1.17) * R, 453 + Math.sin(-1.17) * R];
        Pen.path(Array.from({length: 65}, (_, I) => {
            const A = -2.4 + I / 64 * 1.8;
            return [320 + Math.cos(A) * R, 453 + Math.sin(A) * R];
        }), '#d4e99b99', 1.5);
        Pen.line([Point[0], Point[1]], [555, Point[1] - 40], '#d4e99b90', 1);
        Pen.line([555, Point[1] - 40], [595, Point[1] - 40], '#d4e99b90', 1);
        Pen.circle(...Point, 17, '#d4e99b0d', '#d4e99b80');
        Pen.circle(...Point, 5, '#e3efc5');
        Pen.line([Point[0] - 25, Point[1]], [Point[0] + 25, Point[1]], '#d4e99b55');
        Pen.line([Point[0], Point[1] - 25], [Point[0], Point[1] + 25], '#d4e99b55');
        Pen.text(595, Point[1] - 52, (Probe.Altitude / 1000).toFixed(1) + ' km', '#dce9c5', 21, 'right');

        Pen.text(36, 480, 'GROUND / Z = 0', '#b1baa5', 19, 'left');

    }, Event => {
        const Value = Event.Key
            ? Event.Key === 'Home' ? 0 : Probe.Altitude + (['ArrowUp', 'ArrowRight'].includes(Event.Key) ? 500 : -500) * Event.Step
            : (Math.hypot(Event.Point[0] - 320, Event.Point[1] - 453) - 240) / 4 * 1000;
        Probe.Altitude = AtmosphereProbeAltitude(Value);
        C.Synchronise();
    }, 'Atmosphere altitude probe · drag shell or use arrow keys');

    // Animation time does not change a density column. Retain the scan pixels
    // unless its medium or diagnostic altitude changed; do not redraw for the day cycle.
    const DrawScan = C.Live[C.Live.length - 1];
    let LastScanKey;
    const RefreshScan = () => {
        const Key = JSON.stringify([Probe.Altitude, Settings.Rayleigh, Settings.Mie, Settings.RayleighHeight, Settings.MieHeight]);
        if (Key === LastScanKey) return;
        LastScanKey = Key;
        DrawScan();
    };
    RefreshScan.Static = true;
    C.Live[C.Live.length - 1] = RefreshScan;
    C.Bindings[C.Bindings.indexOf(DrawScan)] = RefreshScan;

    const Update = () => {
        const D = DensityAt(Settings, Probe.Altitude);
        Density.textContent = (D.Rayleigh * 100).toFixed(1);
        Height.textContent = '↗ ' + (Probe.Altitude / 1000).toFixed(1) + ' km probe';
        AerosolValue.textContent = (D.Mie * 100).toFixed(2) + '%';
    };
    Update.Static = true;
    C.Bindings.push(Update);
    C.Live.push(Update);
    Update();
    const Input = El('div', 'at-probe-input');
    Root.appendChild(Input);
    ObservatorySlider(C, Input, Probe, 'Altitude', 'Probe altitude', [0, 30000], () => C.Synchronise(), 'km', .5, .001);
    Root.appendChild(El('p', 'at-scan-note', 'Exaggerated shell diagram · relative density, not a rendered planet. Probe changes this diagnostic only.'));
    return Root;
}
