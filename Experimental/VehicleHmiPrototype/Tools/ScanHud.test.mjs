import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ScanHud, ScanTags, HudSpecification, EvaluateCubicBezier, ResolvePing, ResolvePulse, ResolveScanLineTop, ResolveFrameBox, ProjectToStage, ResolveTagLayout } from '../Source/ScanHud.js';

const Near = (Actual, Expected, Tolerance = 1.0e-6) => assert.ok(Math.abs(Actual - Expected) <= Tolerance, `${Actual} is not within ${Tolerance} of ${Expected}`);
// A 2D context that records what is drawn and measures every character 5 px wide.
function ConstructRecordingCanvas(Width, Height) {
    const Texts = [], Calls = [];
    const Context = new Proxy({ fillText: (Text, X, Y) => Texts.push([Text, X, Y]), measureText: (Text) => ({ width: 5 * Text.length }),
        createLinearGradient: () => ({ addColorStop() {} }), createRadialGradient: () => ({ addColorStop() {} }) },
        { get: (Target, Name) => Name in Target ? Target[Name] : (...Arguments) => Calls.push([Name, ...Arguments]), set: (Target, Name, Value) => { Target[Name] = Value; return true; } });
    return { width: Width, height: Height, getContext: () => Context, Texts, Calls };
}

test('CSS timing: cubic-bezier curves, ping, pulse and the 7 s scan line', () => {
    assert.equal(EvaluateCubicBezier(0.42, 0, 0.58, 1, 0), 0);
    assert.equal(EvaluateCubicBezier(0.42, 0, 0.58, 1, 1), 1);
    Near(EvaluateCubicBezier(0.42, 0, 0.58, 1, 0.5), 0.5, 1.0e-9);   // ease-in-out is symmetric
    assert.ok(EvaluateCubicBezier(0, 0, 0.2, 1, 0.1) > 0.1);          // ping starts fast
    assert.deepEqual(ResolvePing(0), { Scale: 1, Opacity: 1 });
    assert.deepEqual(ResolvePing(0.8), { Scale: 2, Opacity: 0 });
    assert.deepEqual(ResolvePing(1.8), { Scale: 2, Opacity: 0 });     // every second again
    Near(ResolvePulse(0), 1);
    Near(ResolvePulse(1), 0.5, 1.0e-9);
    Near(ResolvePulse(2), 1, 1.0e-9);
    Near(ResolveScanLineTop(0), 0.08);
    Near(ResolveScanLineTop(3.5), 0.92, 1.0e-9);
    Near(ResolveScanLineTop(7), 0.08, 1.0e-9);
    Near(ResolveScanLineTop(1.75), ResolveScanLineTop(5.25), 1.0e-9);
    Near(ResolveScanLineTop(-1.0), ResolveScanLineTop(6.0), 1.0e-9);
});

test('the corner frame, the projection of a tag anchor and the tag boxes are laid out as the reference\'s CSS does', () => {
    assert.deepEqual(ResolveFrameBox(640, 400), { X: 40, Y: 64, Width: 560, Height: 224 });
    assert.deepEqual(ResolveFrameBox(50, 50), { X: 40, Y: 64, Width: 0, Height: 0 });
    const Identity = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
    assert.deepEqual(ProjectToStage(Identity, [0, 0, 0.5], 640, 400), [320, 200]);
    assert.deepEqual(ProjectToStage(Identity, [1, 1, 0.5], 640, 400), [640, 0]);
    const Behind = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0]);   // w = z
    assert.equal(ProjectToStage(Behind, [0, 0, -1], 640, 400), null);
    const Layout = ResolveTagLayout([100, 200], ScanTags[0], (Text, Size, Spacing) => Text.length * 5 + Spacing * Text.length);
    assert.equal(Layout.Dot.X, 108);                                  // translate-x-2 from the anchor
    assert.equal(Layout.Line.X, 108 + 8 + 8);
    assert.equal(Layout.Card.X, Layout.Line.X + 28 + 8);
    Near(Layout.Card.Y + Layout.Card.Height / 2, 200);                // -translate-y-1/2: centred on the anchor
    assert.equal(Layout.Card.Height, 8 * 1.5 + 10 * 1.5 + 12);
    assert.ok(Layout.LabelMiddle < Layout.ValueMiddle);
});

test('the tags are the reference\'s four, worded as it words them', () => {
    assert.deepEqual(ScanTags.map(Tag => Tag.Label), ['Front Motor', 'Ultium Battery Pack', '35-in All-Terrain', 'Cabin']);
    assert.deepEqual(ScanTags.map(Tag => Tag.Value), ['500 kW · e-diff lock', '212 kWh · 329 mi range', 'TPMS 38 PSI · OK', '5 seats · panoramic glass']);
    assert.equal(HudSpecification.Inset.Top, 64);
});

test('the HUD draws two layers into a canvas of twice the stage\'s height, the captions and every projected tag', () => {
    const Canvases = [];
    const Hud = new ScanHud((Width, Height) => { const Canvas = ConstructRecordingCanvas(Width, Height); Canvases.push(Canvas); return Canvas; });
    assert.equal(Hud.Capable, false);                                  // no canvas yet
    assert.equal(Hud.Draw(0, [null, null, null, null]), false);
    Hud.Resize(640, 400);
    assert.deepEqual([Canvases[0].width, Canvases[0].height], [640, 800]);
    Hud.Resize(640, 400);
    assert.equal(Canvases.length, 1);                                  // the same extent keeps the canvas
    assert.equal(Hud.Capable, true);
    assert.equal(Hud.Draw(1.0, [[500, 100], null, [400, 250], [300, 90]]), true);
    assert.equal(Hud.Layouts.length, 3);                               // one anchor behind the camera: no tag
    const Written = Canvases[0].Texts.map(([Text]) => Text).join('');
    for (const Caption of ['PERCEPTION SCAN · ACTIVE', 'LIDAR · 360°'])
        assert.ok(Written.includes(Caption), `the HUD says ${Caption}`);
    for (const Tag of [ScanTags[0], ScanTags[2], ScanTags[3]]) {
        assert.ok(Written.includes(Tag.Label.toUpperCase()), Tag.Label);
        assert.ok(Written.includes(Tag.Value), Tag.Value);
    }
    assert.ok(!Written.includes(ScanTags[1].Label.toUpperCase()));
    // the frame layer sits in the lower half of the canvas
    assert.ok(Canvases[0].Calls.some(([Name, ...Arguments]) => Name === 'translate' && Arguments[1] === 400));
});

test('a host whose 2D canvas cannot draw text leaves the HUD out instead of failing', () => {
    const Hud = new ScanHud(() => ({ getContext: () => ({ clearRect() {} }) }));
    Hud.Resize(64, 40);
    assert.equal(Hud.Capable, false);
    assert.equal(Hud.Draw(0, []), false);
});
