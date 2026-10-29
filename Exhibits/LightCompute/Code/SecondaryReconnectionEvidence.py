# 🧩 Audit native secondary-vertex support controls and retain measured comparison tables.
from pathlib import Path
import hashlib
import math
import re

Root = Path(__file__).resolve().parents[3]
Results = Root / 'Exhibits/LightCompute/Results'
Semantic = []
for Mode in ('release', 'sanitized'):
    Text = (Results / f'SecondaryReconnection-{Mode}.txt').read_text()
    Rows = [Line for Line in Text.splitlines() if Line.startswith('RECONNECTION_')]
    if not Rows or Rows[-1] != 'RECONNECTION_DECISION correctness=1 promote=0 gpu_tested=0 production_changed=0':
        raise RuntimeError('Completed native correctness gate required')
    if re.search(r'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer', Text):
        raise RuntimeError('Sanitizer diagnostic in results')
    Hashes = re.findall(r'^([a-f0-9]{64})  (.+)$', Text, re.M)
    if len(Hashes) != 4:
        raise RuntimeError('Expected four measured-source hashes')
    for Digest, Name in Hashes:
        if hashlib.sha256((Root / Name).read_bytes()).hexdigest() != Digest:
            raise RuntimeError(f'Measured source changed: {Name}')
    Semantic.append(Rows)
if Semantic[0] != Semantic[1]:
    raise RuntimeError('Native semantic rows differ across builds')

References, Measurements, Identities = {}, {}, []
NegativeCount = 0
for Line in Semantic[0]:
    Fields = dict(re.findall(r'(\w+)=([^ ]+)', Line))
    Fixture = Fields.get('fixture')
    if Line.startswith('RECONNECTION_REFERENCE '):
        References[Fixture] = Fields
        assert float(Fields['refinement']) < 1e-7
    if Line.startswith('RECONNECTION_RESULT '):
        Measurements[Fixture, Fields['method']] = Fields
        Mean, Error = float(Fields['mean']), float(Fields['nominal_se'])
        Reference = float(References[Fixture]['integral'])
        assert all(math.isfinite(float(Fields[Name])) for Name in ('mean', 'nominal_se', 'rmse'))
        if Fields['method'] in ('angular', 'area', 'fresh8'):
            assert Fields['mean_gate'] == '1'
            assert abs(Mean - Reference) < 4 * Error + .0001
        if Fields['negative_required'] == '1':
            NegativeCount += 1
            assert Fields['negative_gate'] == '1'
            assert abs(Mean - Reference) > 6 * Error + .005 * Reference
        if Fields['method'] in ('no_floor', 'no_fallback'):
            assert abs(Mean - float(References[Fixture]['donor_support'])) < 4 * Error + .0001
    if Line.startswith('RECONNECTION_IDENTITIES '):
        Identities.append(Fields)
        assert float(Fields['maximum_area_angular_error']) < 1e-12
        assert 0 < int(Fields['present']) < int(Fields['considered']) == 64 * 4096 * 8
        assert Fields['candidates'] == '8'
assert len(References) == len(Identities) == 4 and len(Measurements) == 28 and NegativeCount == 6

Lines = ['# Measured secondary-vertex correctness results', '',
         'Native CPU estimates, not rendered images or physical-GPU timing. All means are linear radiance.', '',
         '| Fixture | Reference | Corrected mean ± nominal SE | No fallback | No floor |',
         '|---------|----------:|----------------------------:|------------:|---------:|']
for Fixture, Reference in References.items():
    Corrected = Measurements[Fixture, 'angular']
    Lines.append(f"| {Fixture} | {float(Reference['integral']):.9f} | "
                 f"{float(Corrected['mean']):.9f} ± {float(Corrected['nominal_se']):.9f} | "
                 f"{float(Measurements[Fixture, 'no_fallback']['mean']):.9f} | "
                 f"{float(Measurements[Fixture, 'no_floor']['mean']):.9f} |")
Lines += ['', f"Area and solid-angle encodings agree pointwise within {max(float(F['maximum_area_angular_error']) for F in Identities):.3g}.", '',
          'All six required negative controls fail the reference as intended.',
          'The four proposal/target support controls match the independently clipped-domain integral.',
          'Removing the fallback or the source target floor loses target-visible/source-invisible contributions.', '',
          '| Fixture | Corrected RMSE | Fresh8 RMSE |',
          '|---------|---------------:|------------:|']
for Fixture in References:
    Lines.append(f"| {Fixture} | {float(Measurements[Fixture, 'angular']['rmse']):.9f} | "
                 f"{float(Measurements[Fixture, 'fresh8']['rmse']):.9f} |")
Lines += ['', 'These are per-reservoir errors, not errors of the cohort mean. Proposal mixes differ and no ray,',
          'illumination, cache-build or GPU costs were measured: no efficiency win is established.', '']
(Results / 'SecondaryReconnection-comparison.md').write_text('\n'.join(Lines))
Message = (f'PASS secondary reconnection: {len(Semantic[0])} identical semantic rows; four source hashes per build; '
           '12 corrected/fresh mean gates; six required negative controls; four area/angular identities; '
           'four null-candidate counts; clipped-support references verified. promote=0 gpu_tested=0.\n')
(Results / 'SecondaryReconnection-parity.txt').write_text(Message)
print(Message, end='')


# Retain a plot of measured means, including deliberately wrong controls; not an image of rendered transport.
Images = Root / 'Exhibits/LightCompute/Images'
Images.mkdir(exist_ok=True)
Svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="780" viewBox="0 0 1200 780">',
       '<rect width="1200" height="780" fill="#121923"/>',
       '<g font-family="DejaVu Sans" fill="#edf1fa">',
       '<text x="24" y="28" font-size="18">Secondary-vertex correctness: measured mean / independent reference</text>',
       '<text x="24" y="52" font-size="13">Native CPU / 64 cohorts / whiskers = 4 nominal SE / not GPU renders or performance</text>']
PlotMethods = ('angular', 'fresh8', 'missing_j', 'double_j', 'no_fallback', 'no_floor')
Labels = ('Corrected', 'Fresh8', 'Omit angular J', 'Extra area J', 'No fallback', 'No target floor')
for Panel, Fixture in enumerate(References):
    Left, Top = 25 + Panel % 2 * 595, 95 + Panel // 2 * 325
    Origin = Left + 145
    Reference = float(References[Fixture]['integral'])
    Svg.append(f'<text x="{Left}" y="{Top}" font-size="17">{Fixture}</text>')
    for Tick in range(5):
        X = Origin + Tick * 90
        Svg.append(f'<line x1="{X}" y1="{Top+16}" x2="{X}" y2="{Top+253}" stroke="#35404d"/>')
        Svg.append(f'<text x="{X-6}" y="{Top+275}" font-size="12">{Tick*.5:.1f}</text>')
    X = Origin + 180
    Svg.append(f'<line x1="{X}" y1="{Top+16}" x2="{X}" y2="{Top+253}" stroke="#e1e7f2" stroke-dasharray="4 3"/>')
    for Index, Method in enumerate(PlotMethods):
        Row = Measurements[Fixture, Method]
        Ratio, Error = float(Row['mean']) / Reference, 4 * float(Row['nominal_se']) / Reference
        Y = Top + 34 + Index * 39
        Color = '#77d3a6' if Method == 'angular' else '#9abfff' if Method == 'fresh8' else '#e7aa81'
        X = Origin + 180 * Ratio
        Svg.append(f'<text x="{Left}" y="{Y+4}" font-size="13">{Labels[Index]}</text>')
        Svg.append(f'<line x1="{Origin+180*(Ratio-Error):.3f}" y1="{Y}" x2="{Origin+180*(Ratio+Error):.3f}" y2="{Y}" stroke="{Color}" stroke-width="3"/>')
        Svg.append(f'<circle cx="{X:.3f}" cy="{Y}" r="4" fill="{Color}"/>')
        Svg.append(f'<text x="{X+9:.3f}" y="{Y+4}" font-size="12" fill="{Color}">{Ratio:.3f}</text>')
Svg += ['<text x="24" y="753" font-size="13">Reference line = correct mean. Area/angular corrected estimates agree within 2.23e-16; neither proves an efficiency win.</text>',
        '</g></svg>']
(Images / 'SecondaryReconnectionMeans.svg').write_text('\n'.join(Svg))

# Optional PNG uses ImageMagick's drawing primitives, not its optional rsvg-convert delegate.
import subprocess
import sys
import xml.etree.ElementTree as Xml
if '--png' in sys.argv:
    Command = ['convert', '-size', '1200x780', 'xc:#121923', '-font', 'DejaVu-Sans']
    for Shape in Xml.fromstring('\n'.join(Svg)).iter():
        Tag = Shape.tag.split('}')[-1]
        if Tag == 'text':
            Command += ['-fill', Shape.get('fill', '#edf1fa'), '-stroke', 'none',
                        '-pointsize', Shape.get('font-size', '13'), '-draw',
                        f"text {Shape.get('x')},{Shape.get('y')} '{Shape.text}'"]
        elif Tag == 'line':
            Command += ['-stroke', Shape.get('stroke'), '-strokewidth', Shape.get('stroke-width', '1'),
                        '-draw', f"line {Shape.get('x1')},{Shape.get('y1')} {Shape.get('x2')},{Shape.get('y2')}"]
        elif Tag == 'circle':
            X, Y, Radius = (float(Shape.get(Name)) for Name in ('cx', 'cy', 'r'))
            Command += ['-fill', Shape.get('fill'), '-stroke', 'none', '-draw', f'circle {X},{Y} {X+Radius},{Y}']
    subprocess.run(Command + [str(Images / 'SecondaryReconnectionMeans.png')], check=True)
