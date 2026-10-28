import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { ChaseCameraSolver } from "../Source/ChaseCameraSolver.js";

const Here = path.dirname(fileURLToPath(import.meta.url));
const Root = path.resolve(Here, "..");

test("ChaseCameraSolver preserves user OrbitYaw and OrbitPitch without decaying to rear", () => {
    const Chase = new ChaseCameraSolver();
    const Position = [0, 0, 0.5];
    const Forward = [1, 0, 0];
    Chase.SnapTo(Position, Forward);
    Chase.OrbitYaw = 1.25;
    Chase.OrbitPitch = 0.22;

    for (let Step = 0; Step < 180; ++Step) {
        Chase.AdvanceChase(Position, Forward, 12.0, 1 / 60);
    }

    assert.equal(Chase.OrbitYaw, 1.25, "OrbitYaw must remain constant after rotating the chase camera");
    assert.equal(Chase.OrbitPitch, 0.22, "OrbitPitch must remain constant after rotating the chase camera");
});

test("DisplayExposure and DisplayRecombination bound cabin auto-exposure and indirect balance", () => {
    const ExposureShader = fs.readFileSync(path.join(Root, "Shader/DisplayExposure.wgsl"), "utf8");
    const RecombShader   = fs.readFileSync(path.join(Root, "Shader/DisplayRecombination.wgsl"), "utf8");
    const PresentShader  = fs.readFileSync(path.join(Root, "Shader/DisplayPresent.wgsl"), "utf8");

    assert.match(ExposureShader, /let Floor\s*=\s*log2\(0\.24\)/, "DisplayExposure should apply dark-cabin adaptation knee");
    assert.match(ExposureShader, /0\.82\)\s*\*\s*exp2\(Frame\.ExposureCompensation\)/, "DisplayExposure should cap maximum auto-exposure boost");
    assert.match(RecombShader, /let TargetCabinShare\s*=\s*select\(CabinIndirectShare,\s*0\.14,/, "DisplayRecombination should bound RTX cabin indirect share");
    assert.match(PresentShader, /\(Frame\.InsetEnabled\s*&\s*1u\)\s*!=\s*0u/, "DisplayPresent should test bit 0 of Frame.InsetEnabled");
});

test("HMI screens are wired into RayTracingSpace and path-traced in RayTraversal and DirectRadiance", () => {
    const RayTracingSource = fs.readFileSync(path.join(Root, "Source/RayTracingSpace.js"), "utf8");
    const RayTraversalWgsl = fs.readFileSync(path.join(Root, "Shader/RayTraversal.wgsl"), "utf8");
    const DirectRadWgsl    = fs.readFileSync(path.join(Root, "Shader/DirectRadiance.wgsl"), "utf8");
    const SceneRecordsWgsl = fs.readFileSync(path.join(Root, "Shader/SceneRecords.wgsl"), "utf8");

    assert.match(RayTracingSource, /AssignHmiTextures\(TabletView,\s*ClusterView\)/, "RayTracingSpace should bind live Tablet and Cluster textures");
    assert.match(RayTraversalWgsl, /@group\(3\)\s*@binding\(4\)\s*var\s+HmiTabletTexture/, "RayTraversal should bind HmiTabletTexture at @group(3) @binding(4)");
    assert.match(RayTraversalWgsl, /@group\(3\)\s*@binding\(5\)\s*var\s+HmiClusterTexture/, "RayTraversal should bind HmiClusterTexture at @group(3) @binding(5)");
    assert.match(RayTraversalWgsl, /fn\s+EvaluateHmiScreenRadiance\(/, "RayTraversal should evaluate live HMI screen emission on ray hits");
    assert.match(RayTraversalWgsl, /fn\s+EvaluateHmiAreaLightNee\(/, "RayTraversal should evaluate rectangular area-light NEE over Tablet and Cluster");
    assert.match(DirectRadWgsl, /EvaluateHmiAreaLightNee\(/, "DirectRadiance should evaluate HMI area-light NEE on primary cabin hits");
    assert.match(SceneRecordsWgsl, /fn\s+ResolveHmiBrightness\(/, "SceneRecords should decode ResolveHmiBrightness from Frame.InsetEnabled");
    assert.match(SceneRecordsWgsl, /fn\s+ResolveHmiScale\(/, "SceneRecords should decode ResolveHmiScale from Frame.InsetEnabled");
});
