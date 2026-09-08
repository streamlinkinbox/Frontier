import { describe, expect, it } from "vitest";
import { EditorCamera } from "../EditorCamera";
import { dot, sub } from "../math";
const aspect = 1.5;

describe("Unreal-style inspection camera", () => {
  it("enters fly mode without moving the eye, and RMB-look rotates in place", () => {
    const c = new EditorCamera(),
      original = c.basis(aspect);
    c.enterFly(aspect);
    expect(c.basis(aspect).eye).toEqual(original.eye);
    c.look(80, -30, aspect);
    expect(c.basis(aspect).eye).toEqual(original.eye);
    expect(c.basis(aspect).forward).not.toEqual(original.forward);
  });
  it("moves forward/right and uses world-Y for Q/E, not an orbit or heightmap", () => {
    const c = new EditorCamera();
    c.enterFly(aspect);
    const start = c.basis(aspect);
    c.key("KeyW", true, aspect);
    c.update(0.1, aspect);
    c.clearKeys();
    expect(dot(sub(c.position, start.eye), start.forward)).toBeCloseTo(1, 6);
    const before = [...c.position] as [number, number, number];
    c.key("KeyD", true, aspect);
    c.update(0.1, aspect);
    c.clearKeys();
    expect(dot(sub(c.position, before), start.right)).toBeCloseTo(1, 6);
    const y = c.position[1];
    c.key("KeyE", true, aspect);
    c.update(0.1, aspect);
    c.clearKeys();
    expect(c.position[1]).toBeCloseTo(y + 1, 6);
    c.key("KeyQ", true, aspect);
    c.update(0.1, aspect);
    c.clearKeys();
    expect(c.position[1]).toBeCloseTo(y, 6);
  });
  it("normalizes diagonal motion, boosts with Shift and is frame-rate independent", () => {
    const run = (frames: number, boost = false, diagonal = false) => {
      const c = new EditorCamera();
      c.enterFly(aspect);
      const start = c.position;
      c.key("KeyW", true, aspect);
      if (diagonal) c.key("KeyD", true, aspect);
      if (boost) c.key("ShiftLeft", true, aspect);
      for (let i = 0; i < frames; i++) c.update(1 / frames, aspect);
      return Math.hypot(...sub(c.position, start));
    };
    expect(run(60)).toBeCloseTo(10, 6);
    expect(run(30)).toBeCloseTo(10, 6);
    expect(run(60, false, true)).toBeCloseTo(10, 6);
    expect(run(60, true)).toBeCloseTo(30, 6);
  });
  it("stops on focus loss and can return to the overview from any location", () => {
    const c = new EditorCamera();
    c.key("KeyW", true, aspect);
    c.update(0.1, aspect);
    c.clearKeys();
    const position = [...c.position];
    c.update(0.1, aspect);
    expect(c.position).toEqual(position);
    c.lookAt([0, 2, 0], [0, 2, -20]);
    expect(c.mode).toBe("fly");
    expect(c.basis(aspect).forward[2]).toBeCloseTo(-1);
    c.frame();
    expect(c.mode).toBe("fly");
    expect(c.distance).toBe(151);
    expect(c.target).toEqual([0, 7, 0]);
  });
  it("the wheel changes fly speed without dollying or changing camera mode", () => {
    const c = new EditorCamera();
    c.enterFly(aspect);
    const position = c.position;
    c.wheel(-200, true, aspect);
    expect(c.speed).toBeGreaterThan(10);
    expect(c.position).toEqual(position);
    c.wheel(-200, false, aspect);
    expect(c.position).toEqual(position);
    expect(c.mode).toBe("fly");
  });
});

it("never switches modes as a side effect of look, orbit, or movement keys", () => {
  const c = new EditorCamera(),
    before = c.basis(aspect);
  c.key("KeyW", true, aspect);
  c.update(0.1, aspect);
  c.look(100, 40, aspect);
  expect(c.mode).toBe("orbit");
  expect(c.basis(aspect)).toEqual(before);
  c.enterFly(aspect);
  const eye = [...c.position],
    yaw = c.yaw;
  c.orbit(100, 40, aspect);
  expect(c.mode).toBe("fly");
  expect(c.position).toEqual(eye);
  expect(c.yaw).toBe(yaw);
  c.enterOrbit(aspect);
  expect(c.basis(aspect).eye).toEqual(eye);
});
