import { test } from "node:test";
import assert from "node:assert/strict";
import { EaseCinematic, InterpolateAngle, InterpolateOrbitPose, ResolveLagBlend, BlendVector, ClampStudioEye } from "../Source/LobbyCameraMotion.js";

test("cinematic easing is still at both ends and monotonic between", () => {
    assert.equal(EaseCinematic(0), 0);
    assert.equal(EaseCinematic(1), 1);
    assert.equal(EaseCinematic(-1), 0);
    assert.equal(EaseCinematic(2), 1);
    let Previous = 0;
    for (let Step = 1; Step <= 20; ++Step) {
        const Value = EaseCinematic(Step / 20);
        assert.ok(Value >= Previous && Value <= 1);
        Previous = Value;
    }
});

test("angle interpolation takes the shortest way around", () => {
    const Wrapped = InterpolateAngle(6.1, 0.3, 1.0);
    assert.ok(Math.abs(((Wrapped - 0.3 + Math.PI * 3) % (Math.PI * 2)) - Math.PI) < 1e-9);
    assert.ok(InterpolateAngle(6.1, 0.3, 0.5) > 6.1);   // crosses zero going forward, not backwards through pi
});

test("lag blend is frame-rate independent in form and bounded", () => {
    assert.equal(ResolveLagBlend(0, 0.3), 0);
    const OneStep = ResolveLagBlend(1 / 30, 0.3);
    const TwoSteps = 1 - (1 - ResolveLagBlend(1 / 60, 0.3)) ** 2;
    assert.ok(Math.abs(OneStep - TwoSteps) < 1e-12);
    assert.ok(ResolveLagBlend(10, 0.3) < 1);
    assert.deepEqual(BlendVector([0, 0, 0], [2, 4, 6], 0.5), [1, 2, 3]);
});

test("studio containment keeps the eye under the softbox and inside the cove", () => {
    assert.deepEqual(ClampStudioEye([0, 0, 9]), [0, 0, 5.25]);
    assert.deepEqual(ClampStudioEye([0, 0, -2]), [0, 0, 0.35]);
    const Wide = ClampStudioEye([20, 0, 2]);
    assert.ok(Math.hypot(Wide[0], Wide[1]) <= 18.4 + 1e-9);
    assert.deepEqual(ClampStudioEye([3, 2, 1.5]), [3, 2, 1.5]);   // an ordinary pose is untouched
});

test("orbit pose interpolation blends yaw, pitch and distance", () => {
    const Pose = InterpolateOrbitPose({ Yaw: 0, Pitch: 0, Distance: 10 }, { Yaw: 1, Pitch: 0.4, Distance: 6 }, 0.5);
    assert.deepEqual(Pose, { Yaw: 0.5, Pitch: 0.2, Distance: 8 });
});
