/*============================================================================================================================================
                                                           VISUALCONTROLS.JS
============================================================================================================================================*/
// 🧩 Each infographic retains a native, keyboard-accessible range input.
'use strict';

const SvgNamespace = 'http://www.w3.org/2000/svg';
const VisualUpdates = [];
const VisualObserver = new MutationObserver(() => VisualUpdates.forEach(Refresh => Refresh()));
const VisualKinds = {
    SunElevation: 'solar-arc', Rayleigh: 'molecular-bars', Mie: 'aerosol-particles', Ozone: 'ozone-band',
    Exposure: 'exposure-aperture', CloudCoverage: 'cloud-cells', GeometryDistance: 'distance-curve',
    GeometryNear: 'near-boundary', GeometryFar: 'far-boundary', GeometryRetention: 'triangle-retention',
    GeometryCurvePower: 'falloff-family', GeometryMaterial: 'material-pattern'
};
function VisualNode(Name, Attributes)
{
    const Node = document.createElementNS(SvgNamespace, Name);
    for (const [Key, Value] of Object.entries(Attributes)) Node.setAttribute(Key, Value);
    return Node;
}
function DisplayLimit(Input, Value)
{
    const Suffix = Input.id === 'SunElevation' ? '°' : /Distance|Near|Far$/.test(Input.id) ? 'm' : /Retention|Material|Coverage/.test(Input.id) ? '%' : '×';
    return Value + Suffix;
}
function VisualFraction(Input)
{
    return Math.max(0, Math.min(1, (Number(Input.value) - Number(Input.min)) / (Number(Input.max) - Number(Input.min))));
}
function VisualQuality(Distance, Near, Far, Retention, Power)
{
    const Position = Math.max(0, Math.min(1, (Distance - Near) / Math.max(1, Far - Near)));
    return 1 - (1 - Retention / 100) * Math.pow(Position, Power);
}
function VisualPath(Points)
{
    return Points.map(([X, Y], Index) => (Index ? 'L' : 'M') + X.toFixed(2) + ' ' + Y.toFixed(2)).join(' ');
}
function BuildVisualControl(Input)
{
    const Label = Input.closest('.parameter');
    const Chart = document.createElement('span');
    Chart.className = 'visual-control';
    Label.dataset.visual = VisualKinds[Input.id];
    Chart.title = 'Drag horizontally to adjust; use arrow keys for fine changes.';
    const Svg = VisualNode('svg', {viewBox: '0 0 160 48', preserveAspectRatio: 'none', class: 'control-graphic'});
    Svg.setAttribute('aria-hidden', 'true');
    const Scale = document.createElement('span'); Scale.className = 'control-scale';
    Scale.setAttribute('aria-hidden', 'true');
    const Minimum = document.createElement('span'), Maximum = document.createElement('span');
    Minimum.textContent = DisplayLimit(Input, Input.min); Maximum.textContent = DisplayLimit(Input, Input.max);
    const Hint = document.createElement('span'); Hint.className = 'control-hint'; Hint.textContent = 'DRAG ↔';
    Scale.append(Minimum, Hint, Maximum); Chart.append(Svg, Scale);
    Label.insertBefore(Chart, Input); Chart.append(Input);
    Label.classList.add('visual-parameter');
    function Update()
    {
        const Fraction = VisualFraction(Input), X = 5 + 150 * Fraction;
        const P = GeometryParameters;
        let Drawing = '', MarkerY = 41;
        const Rule = '<path class="visual-rule" d="M5 41H155"/>';
        switch (Input.id) {
        case 'SunElevation': {
            // The marker encodes the selected angle; the arc is a schematic, not a time-of-day path.
            const Arc = VisualPath(Array.from({length: 33}, (_, I) => {
                const T = I / 32; return [5 + 150 * T, 35 - 25 * Math.sin(T * Math.PI * .5)];
            }));
            MarkerY = 35 - 25 * Math.sin(Fraction * Math.PI * .5);
            Drawing = `<path class="visual-rule" d="M5 35H155"/><path class="visual-soft" d="${Arc}"/><circle class="visual-halo" cx="${X}" cy="${MarkerY}" r="8"/><path class="visual-rays" d="M${X-10} ${MarkerY}h-4 M${X+10} ${MarkerY}h4 M${X} ${MarkerY-11}v-4"/>`;
            break;
        }
        case 'Rayleigh': {
            // Bars encode the selected normalized scattering multiplier, not a measured spectrum.
            for (let I=0; I<12; ++I) {
                const Height = 7 + I * 2.1, Active = (I+.5)/12 <= Fraction;
                Drawing += `<rect class="${Active ? 'visual-solid' : 'visual-muted'}" x="${6+I*12.5}" y="${38-Height}" width="8" height="${Height}" rx="2"/>`;
            }
            Drawing += Rule; break;
        }
        case 'Mie': {
            // A conceptual particle field; opacity communicates the multiplier.
            for (let I=0; I<17; ++I) {
                const CX=9+I*8.6, CY=11+(I*17%24), R=1.8+(I%4)*.8;
                Drawing += `<circle class="${(I+.5)/17 <= Fraction ? 'visual-solid' : 'visual-muted'}" cx="${CX}" cy="${CY}" r="${R}"/>`;
            }
            Drawing += Rule; break;
        }
        case 'Ozone': {
            // Schematic absorption band, with its intensity linked to the ozone multiplier.
            const Strength=.18+.82*Fraction;
            Drawing = `<path class="visual-ghost" d="M5 38C30 38 39 12 77 10S125 38 155 38"/><path class="visual-fill" opacity="${Strength}" d="M5 38C30 38 39 12 77 10S125 38 155 38L155 41H5Z"/><path class="visual-soft" opacity="${Strength}" d="M5 38C30 38 39 12 77 10S125 38 155 38"/>${Rule}`;
            break;
        }
        case 'Exposure': {
            const A = -Math.PI*.85 + Fraction * Math.PI*1.7, CX=80, CY=22;
            const PX=CX+14*Math.cos(A), PY=CY+14*Math.sin(A);
            Drawing = `<circle class="visual-ring" cx="80" cy="22" r="15"/><circle class="visual-halo" cx="80" cy="22" r="${4+Fraction*7}"/><path class="visual-soft" d="M66 22A14 14 0 ${Fraction>.6?1:0} 1 ${PX.toFixed(2)} ${PY.toFixed(2)}"/><path class="visual-rays" d="M80 1v5 M80 38v4 M59 22h5 M96 22h5"/>${Rule}`;
            break;
        }
        case 'CloudCoverage': {
            for (let I=0; I<9; ++I) {
                const CX=10+I*17.2, Active=(I+.5)/9<=Fraction;
                Drawing += `<path class="${Active?'visual-solid':'visual-muted'}" d="M${CX-5} 30a4 4 0 0 1 2-7 5 5 0 0 1 9 1 3.5 3.5 0 0 1 0 6Z"/>`;
            }
            Drawing += Rule; break;
        }
        case 'GeometryDistance': {
            const Path=VisualPath(Array.from({length: 41},(_,I)=>{
                const T=I/40, D=Number(Input.min)+T*(Number(Input.max)-Number(Input.min));
                return [5+150*T, 38-29*VisualQuality(D,P.GeometryNear,P.GeometryFar,P.GeometryRetention,P.GeometryCurvePower)];
            }));
            MarkerY=38-29*VisualQuality(P.GeometryDistance,P.GeometryNear,P.GeometryFar,P.GeometryRetention,P.GeometryCurvePower);
            Drawing=`<path class="visual-fill" d="${Path}L155 41H5Z"/><path class="control-line visual-soft" d="${Path}"/>${Rule}`; break;
        }
        case 'GeometryNear': {
            const FarX=5+150*Math.min(1,(P.GeometryFar-Number(Input.min))/(Number(Input.max)-Number(Input.min)));
            const DistanceX=5+150*Math.max(0,Math.min(1,(P.GeometryDistance-Number(Input.min))/(Number(Input.max)-Number(Input.min))));
            Drawing=`<rect class="visual-muted" x="5" y="14" width="150" height="14" rx="7"/><rect class="visual-fill" x="5" y="14" width="${Math.max(0,X-5)}" height="14" rx="7"/><path class="control-line visual-soft" d="M${X} 7V35 M${FarX} 11V31 M${DistanceX} 5v6"/><circle class="visual-secondary" cx="${DistanceX}" cy="9" r="2"/>${Rule}`; break;
        }
        case 'GeometryFar': {
            const NearX=5+150*Math.max(0,Math.min(1,(P.GeometryNear-Number(Input.min))/(Number(Input.max)-Number(Input.min))));
            const DistanceX=5+150*Math.max(0,Math.min(1,(P.GeometryDistance-Number(Input.min))/(Number(Input.max)-Number(Input.min))));
            Drawing=`<path class="visual-muted-stroke" d="M5 31H155"/><path class="visual-soft" d="M${NearX} 11L${X} 31"/><path class="visual-rays" d="M${X} 8v26 M${NearX} 6v10"/><circle class="visual-secondary" cx="${DistanceX}" cy="35" r="2"/>${Rule}`; break;
        }
        case 'GeometryRetention': {
            for (let I=0; I<10; ++I) {
                const CX=7+I*15, Active=(I+.5)/10<=Fraction;
                Drawing += `<path class="${Active?'visual-triangle-active':'visual-triangle'}" d="M${CX} 35l7-24 7 24Z"/>`;
            }
            Drawing += Rule; break;
        }
        case 'GeometryCurvePower': {
            for (const Power of [.5, 1, P.GeometryCurvePower, 3]) {
                const Path=VisualPath(Array.from({length: 25},(_,I)=>[5+150*I/24, 7+30*Math.pow(I/24,Power)]));
                Drawing += `<path class="${Power===P.GeometryCurvePower?'visual-soft':'visual-ghost'}" d="${Path}"/>`;
            }
            Drawing += Rule; break;
        }
        case 'GeometryMaterial': {
            // The pattern previews a normalized control level, not a real material-quality metric.
            for (let I=0; I<12; ++I) {
                const Active=(I+.5)/12<=Fraction, CX=6+I*12.5;
                Drawing += `<rect class="${Active?'visual-solid':'visual-muted'}" x="${CX}" y="${I%2?15:8}" width="9" height="${I%2?21:28}" rx="2"/>`;
                if (Active) Drawing += `<path class="visual-cut" d="M${CX+2} ${I%2?20:13}l5 5 M${CX+2} ${I%2?27:20}l5 5"/>`;
            }
            Drawing += Rule; break;
        }
        }
        Svg.innerHTML = Drawing + `<circle class="control-marker" cx="${X}" cy="${MarkerY}" r="3.2"/>`;
        Chart.style.setProperty('--position', (Fraction * 100) + '%');
    }
    VisualUpdates.push(Update);
    Input.addEventListener('input', () => VisualUpdates.forEach(Refresh => Refresh()));
    // Presets and reset update native inputs and --fill without firing input events.
    VisualObserver.observe(Input, {attributes: true, attributeFilter: ['style']});
    Update();
}
for (const Input of document.querySelectorAll('.parameter input[type="range"]')) BuildVisualControl(Input);
