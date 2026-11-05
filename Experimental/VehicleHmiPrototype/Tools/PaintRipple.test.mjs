import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ConvertHexToLinear, ResolvePaintTarget, ResolveRippleRadius, PaintRipple, PaintRippleSpecification } from '../Source/PaintRipple.js';

const Near = (Actual, Expected, Tolerance = 1.0e-9) => assert.ok(Math.abs(Actual - Expected) <= Tolerance, `${Actual} is not within ${Tolerance} of ${Expected}`);

test('hex colours convert to linear the way THREE.Color.set does, and anything else is refused', () => {
    assert.deepEqual(ConvertHexToLinear('#ffffff'), [1, 1, 1]);
    assert.deepEqual(ConvertHexToLinear('#000000'), [0, 0, 0]);
    const Silver = ConvertHexToLinear('#c2c8d0');
    Near(Silver[0], 0.5395, 1.0e-3);                                 // (0xc2 / 255 + 0.055) / 1.055, to the power 2.4
    Near(ConvertHexToLinear('#040404')[0], 4 / 255 / 12.92, 1.0e-12);   // the linear toe below 0.04045
    for (const Bad of ['red', '#fff', '#12345g', 'c2c8d0']) assert.throws(() => ConvertHexToLinear(Bad), TypeError);
});

test('a finish resolves to the reference\'s roughness, metalness and clear coat', () => {
    const Metallic = { Hex: '#c2c8d0', Metallic: true }, Solid = { Hex: '#b2946a', Metallic: false };
    const Pick = (Option, Finish) => { const { Roughness, Metalness, CoatWeight, CoatRoughness } = ResolvePaintTarget(Option, Finish); return [Roughness, Metalness, CoatWeight, CoatRoughness]; };
    assert.deepEqual(Pick(Metallic, 'gloss'), [0.26, 0.9, 1, 0.12]);
    assert.deepEqual(Pick(Solid, 'gloss'), [0.45, 0.35, 1, 0.12]);
    assert.deepEqual(Pick(Metallic, 'matte'), [0.72, 0.9, 0.1, 0.65]);
    assert.deepEqual(Pick(Solid, 'matte'), [0.72, 0.35, 0.1, 0.65]);
    assert.deepEqual(ResolvePaintTarget(Solid, 'gloss').Albedo, ConvertHexToLinear('#b2946a'));
});

test('the wave grows on easeOutCubic to 6.4 m in 1.35 s', () => {
    const { Seconds, Radius } = PaintRippleSpecification;
    assert.equal([Seconds, Radius].join(' '), '1.35 6.4');
    assert.equal(ResolveRippleRadius(0, Seconds, Radius), 0);
    Near(ResolveRippleRadius(Seconds / 2, Seconds, Radius), (1 - 0.125) * 6.4);
    assert.equal(ResolveRippleRadius(Seconds, Seconds, Radius), 6.4);
    assert.equal(ResolveRippleRadius(10, Seconds, Radius), 6.4);
});

test('a ripple spends at most 0.05 s a step, commits once at the end, and commits a run it interrupts', () => {
    const Committed = [];
    const Ripple = new PaintRipple((Target) => Committed.push(Target));
    const First = { Albedo: [1, 0, 0], Roughness: 0.5, Metalness: 0.5 }, Second = { Albedo: [0, 1, 0], Roughness: 0.2, Metalness: 0.9 };
    Ripple.Advance(1.0);                                              // no run: nothing happens
    assert.equal(Ripple.Active, false);
    Ripple.Begin([1, 2, 3], First);
    Ripple.Advance(1.0);                                              // a stalled frame still moves only 0.05 s
    Near(Ripple.Elapsed, 0.05);
    Near(Ripple.Reached, ResolveRippleRadius(0.05, 1.35, 6.4));
    assert.deepEqual(Committed, []);
    Ripple.Begin([4, 5, 6], Second);                                  // an in-flight run is committed at once, the new one starts at 0
    assert.deepEqual(Committed, [First]);
    assert.equal(Ripple.Reached, 0);
    assert.deepEqual(Ripple.Centre, [4, 5, 6]);
    for (let Step = 0; Step < 26; ++Step) Ripple.Advance(0.05);
    assert.equal(Ripple.Active, true);
    Ripple.Advance(0.05);
    assert.equal(Ripple.Active, false);
    assert.deepEqual(Committed, [First, Second]);
    assert.equal(Ripple.Finished, 2);
});

test('twelve floats reach the frame record: centre, radius, next finish, softness', () => {
    const Ripple = new PaintRipple(() => {});
    const Scalars = new Float32Array(16).fill(-1);
    Ripple.Encode(Scalars, 2);
    assert.deepEqual([...Scalars.slice(2, 14)], [0, 0, 0, 0, 0, 0, 0, 0, 0, 0.55, 0, 0].map(Math.fround));   // nothing to show: radius 0
    assert.equal(Scalars[0], -1);
    assert.equal(Scalars[14], -1);
    Ripple.Begin([1, 2, 3], { Albedo: [0.25, 0.5, 0.75], Roughness: 0.5, Metalness: 0.125 });
    Ripple.Advance(0.05);
    Ripple.Encode(Scalars, 2);
    assert.deepEqual([...Scalars.slice(2, 5)], [1, 2, 3]);
    assert.equal(Scalars[5], Math.fround(Ripple.Reached));
    assert.deepEqual([...Scalars.slice(6, 11)], [0.25, 0.5, 0.75, 0.5, 0.125]);
    assert.equal(Scalars[11], Math.fround(0.55));
});
