//============================================================================================================================================
//                                                         RENDERERNAMES.TEST.MJS
//============================================================================================================================================
// 🧩 The naming correction, enforced: there is no hardware ray tracing in this app, so nothing user-facing may call the BVH path tracer "RTX" (or the distance-field renderer "GTX").
//    The old query values still work as aliases, and that is the only place the old names may appear.

import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { ResolveRendererName, RendererAliases, RendererLabels } from "../Source/RendererNames.js";

const Root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

test("?renderer= accepts the new names and the old ones as aliases, and defaults to the BVH path tracer", () =>
{
    assert.equal(ResolveRendererName("bvh"), "bvh");
    assert.equal(ResolveRendererName("sdf"), "sdf");
    assert.equal(ResolveRendererName("rtx"), "bvh", "rtx is kept only as a compatibility alias of the software BVH tracer");
    assert.equal(ResolveRendererName("gtx"), "sdf", "gtx is kept only as a compatibility alias of the distance-field renderer");
    assert.equal(ResolveRendererName("  RTX "), "bvh");
    assert.equal(ResolveRendererName("GTX"), "sdf");
    assert.equal(ResolveRendererName(null), "bvh");
    assert.equal(ResolveRendererName(undefined), "bvh");
    assert.equal(ResolveRendererName(""), "bvh");
    assert.equal(ResolveRendererName("vulkan"), "bvh", "an unknown value is the default, not an error");
    assert.deepEqual(Object.keys(RendererAliases).sort(), ["bvh", "gtx", "rtx", "sdf"]);
    assert.ok(Object.isFrozen(RendererAliases) && Object.isFrozen(RendererLabels));
});

test("the host turns the resolved name into the RayTracing setting (default: BVH on; sdf, gtx: off)", () =>
{
    const Host = fs.readFileSync(path.join(Root, "Source/IlluminationHost.js"), "utf8");
    assert.match(Host, /ResolveRendererName\(Query\.get\("renderer"\)\)/);
    assert.match(Host, /RayTracing: RendererParam === "bvh"/);
    for (const [Query, Expected] of [["bvh", true], ["rtx", true], ["sdf", false], ["gtx", false], [null, true]])
        assert.equal(ResolveRendererName(Query) === "bvh", Expected, `?renderer=${Query}`);
});

// Where "RTX" or "GTX" may still be written: this file's siblings that DEFINE the aliases, and any line that says it is the old name or an alias.
const Allowed = /alias|old name|OLD name|compatib|formerly|renamed|RendererNames|no RTX|not RTX|never RT cores|hardware ray tracing/i;
const Scan = (Directory, Extensions) => fs.readdirSync(path.join(Root, Directory), { withFileTypes: true })
    .filter((Entry) => Entry.isFile() && Extensions.some((Extension) => Entry.name.endsWith(Extension))).map((Entry) => path.join(Directory, Entry.name));

test("no user-facing label, comment or document calls the software BVH tracer 'RTX' (or the SDF renderer 'GTX') except to say it is the old name", () =>
{
    const Files = [...Scan("Source", [".js"]), ...Scan("Shader", [".wgsl"]), ...Scan("Tools", [".mjs", ".py", ".sh"]).filter((File) => !/RendererNames\.test\.mjs$/.test(File)), "README.md", "COMBINED_PLAN.md", "Profiles/README.md", "index.html",
                   ...(fs.existsSync(path.join(Root, "Documentation")) ? Scan("Documentation", [".md"]) : []), ...(fs.existsSync(path.join(Root, "PLAN.md")) ? ["PLAN.md"] : [])]
        .filter((File) => fs.existsSync(path.join(Root, File)));
    const Offenders = [];
    for (const File of Files)
        fs.readFileSync(path.join(Root, File), "utf8").split("\n").forEach((Line, Index) =>
        {
            if (/\b(RTX|GTX)\b/.test(Line) && !Allowed.test(Line) && !/renderer=(rtx|gtx)|"rtx"|"gtx"|`rtx`|`gtx`|\brtx\b.*\bgtx\b/.test(Line)) Offenders.push(`${File}:${Index + 1}: ${Line.trim().slice(0, 140)}`);
        });
    assert.deepEqual(Offenders, [], "the renderer is the software BVH path tracer (no hardware RT) and the other one is the SDF renderer");
});
