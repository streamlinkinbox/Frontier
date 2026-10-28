import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";

// 📝 Regression guard for the black cluster-debug-views bug: VirtualizedGeometry referenced five WGSL
//    files that were never added to the host's ShaderSources manifest, so they were never fetched —
//    program construction threw with ?hierarchy=1 and the debug views silently no-opped without it.
//    Every shader file named anywhere in Source must be retrievable: listed in the manifest itself, or
//    pulled in by a manifest shader's //! include chain.

const Host = readFileSync(new URL("../Source/IlluminationHost.js", import.meta.url), "utf8");
const Manifest = new Set([...Host.matchAll(/"([A-Za-z0-9_]+\.wgsl)"/g)].map((Match) => Match[1]));

function ExpandIncludes(Name, Into)
{
    if (Into.has(Name)) return;
    Into.add(Name);
    const Text = readFileSync(new URL(`../Shader/${Name}`, import.meta.url), "utf8");
    for (const Match of Text.matchAll(/\/\/!\s*include\s+"([^"]+)"/g)) ExpandIncludes(Match[1], Into);
}
const Retrievable = new Set();
for (const Name of Manifest) ExpandIncludes(Name, Retrievable);

test("every WGSL file referenced from Source is in the host shader manifest", () =>
{
    const Missing = [];
    for (const File of readdirSync(new URL("../Source/", import.meta.url)))
    {
        if (!File.endsWith(".js")) continue;
        const Text = readFileSync(new URL(`../Source/${File}`, import.meta.url), "utf8");
        for (const Match of Text.matchAll(/"([A-Za-z0-9_]+\.wgsl)"/g))
            if (!Retrievable.has(Match[1])) Missing.push(`${File} -> ${Match[1]}`);
    }
    assert.deepEqual(Missing, [], "shader files referenced but never fetched (add them to ShaderSources in IlluminationHost.js)");
});

test("the virtualized-geometry shaders are all retrievable", () =>
{
    for (const Name of ["VirtualizedGeometryCull.wgsl", "HiZPyramid.wgsl", "HiZDownsample.wgsl", "VisibilityRaster.wgsl", "MaterialResolve.wgsl", "ClusterDebugFallback.wgsl"])
        assert.ok(Retrievable.has(Name), `${Name} must be fetched at startup`);
});
