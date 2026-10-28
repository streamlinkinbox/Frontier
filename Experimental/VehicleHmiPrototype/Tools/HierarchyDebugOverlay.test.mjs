import test from "node:test";
import assert from "node:assert/strict";
import { HierarchyDebugOverlay } from "../Source/HierarchyDebugOverlay.js";
import { ConstructSphereFacets } from "./GeometryHierarchyCorpus.mjs";

// Minimal DOM stub: just enough surface for the overlay's SVG + panel construction and per-frame draw.
function FakeElement(Tag)
{
    return {
        Tag, style: {}, children: [], textContent: "", value: "", selected: false,
        appendChild(Child) { this.children.push(Child); return Child; },
        replaceChildren(...Children) { this.children = Children; },
        setAttribute(Name, Value) { (this.Attributes ??= {})[Name] = Value; },
        addEventListener() { },
        getBoundingClientRect() { return { width: 800, height: 600 }; }
    };
}

function InstallDom(Search)
{
    const Stage = FakeElement("div");
    globalThis.document = {
        getElementById: (Id) => (Id === "stage" ? Stage : null),
        createElement: (Tag) => FakeElement(Tag),
        createElementNS: (_Ns, Tag) => FakeElement(Tag)
    };
    globalThis.location = { search: Search };
    return Stage;
}

function FakeHost()
{
    const Set = ConstructSphereFacets(3, 9);   // 1280 facets, glass band family 9
    return {
        Scene: {
            DrawRuns: [{ FacetSet: 0, First: 0, Count: 1 }],
            FacetSets: [Set],
            Occupants: [{ Transform: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], Roles: 0 }]
        },
        CameraState: { Position: [0, 0, 5], Forward: [0, 0, -1], Right: [1, 0, 0], Up: [0, 1, 0], TanY: 0.5 }
    };
}

test("overlay is inert without ?hierarchy=1 (default editor path untouched)", () =>
{
    InstallDom("");
    const Overlay = new HierarchyDebugOverlay(FakeHost());
    assert.equal(Overlay.SVG, undefined, "inert overlay must allocate no SVG");
    Overlay.Draw();   // must be a safe no-op
});

test("overlay builds the hierarchy, draws bounds + wireframe and reports stats", () =>
{
    InstallDom("?hierarchy=1&glass=9");
    const Overlay = new HierarchyDebugOverlay(FakeHost());
    assert.ok(Overlay.SVG, "svg created");
    Overlay.Draw();
    assert.ok(Overlay.SVG.children.length > 0, "something was drawn");
    assert.match(Overlay.Stats.textContent, /clusters/);
    assert.match(Overlay.Stats.textContent, /DAG max error/);
    assert.ok(Overlay.LevelRow.children.length >= 3, "level buttons for a multi-level DAG");
    const Build = Overlay.Builds.get(0);
    assert.ok(Build?.Hierarchy, "hierarchy built for facet set 0");
    assert.ok(Build.Hierarchy.Levels.length >= 3, "multi-level hierarchy");
    // Glass family 9 was requested: no cluster may mix classes.
    for (const Node of Build.Hierarchy.Clusters)
        assert.notEqual(Node.ClassMask, 3, "mixed opaque/transmissive cluster");
    // Walk to the root level and draw again; fewer triangles than the leaf level.
    Overlay.SelectedLevel = Build.Hierarchy.Levels.length - 1;
    Overlay.Draw();
    assert.match(Overlay.Stats.textContent, new RegExp("showing L" + Overlay.SelectedLevel));
    // Second frame reuses the cached build (no rebuild for an unchanged facet count).
    const First = Build.Hierarchy;
    Overlay.Draw();
    assert.equal(Overlay.Builds.get(0).Hierarchy, First, "cached build reused");
});

test("cluster debug: runtime-cut view and page colour mode draw and report", () =>
{
    InstallDom("?hierarchy=1&glass=9");
    const Overlay = new HierarchyDebugOverlay(FakeHost());
    Overlay.Draw();
    const Build = Overlay.Builds.get(0);
    assert.ok(Build.Pages?.length > 1, "streaming pages packed for cluster debug");
    assert.ok(Build.Residency, "residency manager created");
    // Page colour mode: resident (pinned) pages colour, unstreamed pages grey out.
    Overlay.ColourMode = "page";
    Overlay.Draw();
    assert.ok(Overlay.SVG.children.length > 0, "page mode draws");
    assert.match(Overlay.Stats.textContent, /pages \d+\/\d+ resident/);
    // Runtime-cut view: the real Phase-2 pipeline decides what is visible from this camera.
    Overlay.ViewMode = "cut";
    Overlay.Draw();
    assert.match(Overlay.Stats.textContent, /runtime cut/);
    assert.match(Overlay.Stats.textContent, /visible/);
    assert.ok(Overlay.RuntimeResult, "runtime frame executed");
    assert.ok(Overlay.RuntimeResult.VisibleClusters.length > 0, "cut selects clusters in front of the camera");
    assert.ok(Overlay.SVG.children.length > 0, "cut bounds/wireframe drawn");
});

test("overlay tolerates a host with no scene yet", () =>
{
    InstallDom("?hierarchy=1");
    const Overlay = new HierarchyDebugOverlay({});
    Overlay.Draw();
    assert.equal(Overlay.SVG.children.length, 0);
});
