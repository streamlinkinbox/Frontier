import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ScanSequence, ScanSpecification, EaseInOutCubic, DampValue, ResolveAcesFilmic, ResolveDisplayColour, ConvertLinearToSrgb } from '../Source/ScanSequence.js';

const Near = (Actual, Expected, Tolerance = 1.0e-6) => assert.ok(Math.abs(Actual - Expected) <= Tolerance, `${Actual} is not within ${Tolerance} of ${Expected}`);
const Run = (Scan, Steps) => { for (let Step = 0; Step < Steps; ++Step) Scan.Advance(0.05); };

test('easing and damping are the reference\'s', () => {
    assert.equal(EaseInOutCubic(0), 0);
    assert.equal(EaseInOutCubic(0.5), 0.5);
    assert.equal(EaseInOutCubic(1), 1);
    Near(EaseInOutCubic(0.25), 4 * 0.25 ** 3);
    Near(DampValue(0, 1, 3, 0.05), 1 - Math.exp(-0.15));
    assert.equal(DampValue(1, 1, 3, 0.05), 1);
});

test('three.js\'s ACES filmic and the sRGB encode carry the scan colours to the display', () => {
    assert.deepEqual(ResolveAcesFilmic([0, 0, 0]), [0, 0, 0]);
    Near(ResolveAcesFilmic([1, 1, 1])[0], 0.7634, 1.0e-3);          // white sits at 0.76, not 1
    assert.deepEqual(ResolveAcesFilmic([50, 50, 50]), [1, 1, 1]);   // saturated
    const [Low, High] = [ResolveAcesFilmic([0.2, 0.2, 0.2])[1], ResolveAcesFilmic([0.4, 0.4, 0.4])[1]];
    assert.ok(Low < High);
    Near(ConvertLinearToSrgb(0.5), 0.7354, 1.0e-3);
    Near(ConvertLinearToSrgb(0.001), 0.01292, 1.0e-6);
    const Accent = ResolveDisplayColour(ScanSpecification.Accent), Edge = ResolveDisplayColour(ScanSpecification.EdgeColour);
    [0.43296, 0.77718, 0.87498].forEach((Value, Index) => Near(Accent[Index], Value, 1.0e-4));
    [0.69290, 0.83306, 0.86843].forEach((Value, Index) => Near(Edge[Index], Value, 1.0e-4));
});

test('toggling on ramps the scan in and makes the solids transparent; off ramps out and stops drawing', () => {
    const Scan = new ScanSequence();
    assert.equal(Scan.Visible, false);
    assert.equal(Scan.Transparent, false);
    Run(Scan, 5);
    assert.equal(Scan.Visible, false);                              // not enabled: nothing moves
    Scan.Assign(true);
    Run(Scan, 1);
    Near(Scan.Ramp, 1 - Math.exp(-0.15));
    assert.equal(Scan.Transparent, true);
    assert.equal(Scan.Visible, true);
    Run(Scan, 19);                                                  // one second
    Near(Scan.Ramp, 1 - Math.exp(-3), 1.0e-6);
    Scan.Assign(false);
    Run(Scan, 60);                                                  // three seconds: e^-9 < 0.002, snapped
    assert.equal(Scan.Ramp, 0);
    assert.equal(Scan.Amount, 0);
    assert.equal(Scan.Visible, false);
    assert.equal(Scan.Transparent, false);
});

test('the front sweeps 3.25 m ahead of the nose to 3.35 m behind the tail in 4.6 s, holds a second, fades and repeats', () => {
    const Scan = new ScanSequence();
    Scan.Assign(true);
    Run(Scan, 46);                                                  // 2.3 s: half the travel, the middle of the ease
    Near(Scan.Front, 3.25 + (-3.35 - 3.25) * 0.5);
    assert.equal(Scan.Sweeping, true);
    Run(Scan, 47);                                                  // 2.35 s further: 4.65 s, the travel is done
    Near(Scan.Front, -3.35);
    assert.equal(Scan.Sweeping, false);
    Run(Scan, 10);                                                  // 5.15 s: held
    assert.equal(Scan.CycleAmount, 1);
    Run(Scan, 20);                                                  // 6.15 s: halfway through the fade (5.6 s … 6.6 s)
    Near(Scan.CycleAmount, 0.45, 1.0e-6);
    Run(Scan, 10);                                                  // 6.65 s: a new pass has begun at the nose
    assert.equal(Scan.Sweeping, true);
    Near(Scan.Front, 3.25, 1.0e-3);
    Near(Scan.CycleAmount, 1, 1.0e-6);
});

test('a stalled frame moves the scan 0.05 s; switching on again restarts the sweep, off keeps the front where it was', () => {
    const Scan = new ScanSequence();
    Scan.Assign(true);
    Scan.Advance(2.0);
    Near(Scan.SweepSeconds, 0.05);
    Scan.Assign(true);                                              // already on: not a restart
    Near(Scan.SweepSeconds, 0.05);
    Run(Scan, 40);
    const Front = Scan.Front;
    Scan.Assign(false);
    Run(Scan, 5);
    assert.equal(Scan.Front, Front);
    Scan.Assign(true);
    assert.equal(Scan.SweepSeconds, 0);
    assert.equal(Scan.Clock > 2.0, true);                           // the clock itself is the raw time
});

test('the curtain shows 0.075 and 0.30 while the front moves and a quarter of that while it rests; the wireframe opacity follows the amount', () => {
    const Scan = new ScanSequence();
    Scan.Assign(true);
    Run(Scan, 40);                                                  // 2 s, mid-sweep, the ramp nearly full
    assert.equal(Scan.Sweeping, true);
    const Moving = Scan.ResolveCurtain();
    Near(Moving[0], 0.075 * Scan.Ramp);
    Near(Moving[1], 0.30 * Scan.Ramp);
    Near(Scan.ResolveEdgeOpacity(), 0.55 * Scan.Amount);
    Run(Scan, 80);                                                  // 6.0 s: the sweep is over, the fade has begun
    assert.equal(Scan.Sweeping, false);
    const Resting = Scan.ResolveCurtain();
    Near(Resting[1], 0.30 * Scan.Ramp * Scan.CycleAmount * 0.25);
});
