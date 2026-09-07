import { describe, it, expect, vi } from "vitest";
import {
  DiagnosticLog,
  diagnosticJSON,
  summarizePixels,
  summarizeVolume,
} from "../diagnostics";

describe("bounded, shareable GPU diagnostics", () => {
  it("keeps the newest events and notifies/unsubscribes UI listeners", () => {
    const log = new DiagnosticLog(2);
    const listener = vi.fn();
    const unsubscribe = log.subscribe(listener);
    log.log("GPU", "old event");
    log.log("GPU", "shader compiled");
    unsubscribe();
    log.log("GPU", "frame complete");
    const report = log.report();
    expect(report).not.toContain("old event");
    expect(report).toContain("shader compiled");
    expect(report).toContain("frame complete");
    expect(listener).toHaveBeenCalledTimes(2);
    expect(log.getRevision()).toBe(3);
  });
  it("coalesces repeated errors instead of growing without bound", () => {
    const log = new DiagnosticLog();
    for (let i = 0; i < 100; i++)
      log.log("WebGPU", "Device lost", { reason: "unknown" }, "error");
    expect(log.report()).toContain("repeated 100 times");
    expect(log.report().match(/Device lost/g)).toHaveLength(1);
  });
  it("retains error messages/stacks while stripping credentials, URL queries and raw volumes", () => {
    const cyclic: any = {
      error: new Error(
        "GPU failure at https://user:password@example.test/app.js?token=private#secret",
      ),
      accessToken: "private-token",
      samples: new Float32Array(10000),
      invalid: NaN,
    };
    cyclic.self = cyclic;
    const text = diagnosticJSON(cyclic);
    expect(text).toContain("GPU failure at https://example.test/app.js");
    expect(text).toContain("stack");
    expect(text).not.toContain("user:password");
    expect(text).not.toContain("token=private");
    expect(text).not.toContain("#secret");
    expect(text).not.toContain("private-token");
    expect(text).toContain("40000 bytes omitted");
    expect(text).toContain("circular");
    expect(text).toContain("NaN");
  });
  it("does not claim that GPU readback proves on-screen presentation", () => {
    expect(new DiagnosticLog().report()).toContain(
      "NOT proof that the browser compositor displayed the canvas",
    );
  });
});

describe("diagnostic pixel and volume summaries", () => {
  it("distinguishes a uniform background from opaque spatial variation", () => {
    const blank = new Uint8Array([120, 130, 123, 255, 120, 130, 123, 255]);
    expect(summarizePixels(blank).nonUniformOpaque).toBe(false);
    blank[0] = 240;
    expect(summarizePixels(blank).nonUniformOpaque).toBe(true);
    expect(summarizePixels(new Uint8Array(8)).opaquePercent).toBe(0);
  });
  it("reports RGBA statistics from a BGRA canvas correctly", () => {
    const summary = summarizePixels(
      new Uint8Array([10, 20, 210, 255, 20, 40, 150, 255]),
      true,
    );
    expect(summary.meanRGBA).toEqual([180, 30, 15, 255]);
    expect(summary.opaquePercent).toBe(100);
    expect(summary.sandstoneColorPercent).toBe(100);
  });
  it("reports empty/broken fields without embedding any voxel values", () => {
    const data = new Float32Array([
      -2,
      0,
      0,
      0,
      4,
      0,
      0,
      0,
      NaN,
      0,
      0,
      0,
      Infinity,
      0,
      0,
      0,
    ]);
    expect(summarizeVolume(data)).toEqual({
      voxels: 4,
      minSDF: -2,
      maxSDF: 4,
      solidVoxels: 1,
      nonFiniteSDF: 2,
    });
  });
});
