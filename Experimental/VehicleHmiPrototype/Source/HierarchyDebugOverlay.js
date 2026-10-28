//------------------------------------------------------------------------------------------------------------------------
// GEOMETRY HIERARCHY DEBUG OVERLAY
//------------------------------------------------------------------------------------------------------------------------
//
// 🧩 Editor debug view for the virtualized-geometry build (Source/GeometryHierarchy.js): per-level cluster bounds
//    and wireframe of the simplified level geometry itself, so the LOD DAG can be inspected against the live scene.
//
// 📝 Off unless the page is opened with ?hierarchy=1 — the default editor path constructs an inert object and
//    allocates nothing. Optional &glass=9,12 lists the material families treated as transmissive (they are built
//    into their own clusters and shown cyan in the Class colour mode). Hierarchies are built lazily, once per
//    eligible facet set (same static-set rule the clustered draw path uses: >= 256 facets, no dynamic vertex
//    capacity), and rebuilt if the facet count changes. Bounds drawn are the error-expanded DAG node bounds, so
//    coarser levels visibly grow by their own error — that padding is the culling-safety margin, not sloppiness.

import { ProjectGizmo } from "./TransformGizmo.js";
import { BuildGeometryHierarchy, CollectHierarchyLevel } from "./GeometryHierarchy.js";
import { PackHierarchyPages, HierarchyResidencyManager, ExecuteHierarchyRuntimeFrame } from "./GeometryHierarchyRuntime.js";

const SvgNamespace = "http://www.w3.org/2000/svg";
const WireframeTriangleCap = 24000;
const BoundsCap = 6000;

function TransformPoint(Matrix, Point)
{
    return [0, 1, 2].map((Axis) =>
        Matrix[Axis] * Point[0] + Matrix[4 + Axis] * Point[1] + Matrix[8 + Axis] * Point[2] + Matrix[12 + Axis]);
}

export class HierarchyDebugOverlay
{
    constructor(Host)
    {
        this.Host = Host;
        this.Builds = new Map();
        this.SelectedLevel = 0;
        this.Mode = "both";          // both | bounds | wireframe
        this.ColourMode = "cluster"; // cluster | error | class | uv | features | page
        this.ViewMode = "level";     // level | cut (live runtime DAG cut for the current camera)
        this.RuntimeResult = null;
        this.RuntimeKey = "";
        this.RuntimeTime = 0;
        this.FrameCounter = 0;
        const Query = new URLSearchParams(location.search);
        if (Query.get("hierarchy") !== "1") return;
        this.CutThreshold = Number.parseFloat(Query.get("px") ?? "") || 1.0;
        this.TransmissiveFamilies = (Query.get("glass") ?? "")
            .split(",").map((Value) => Number.parseInt(Value, 10)).filter(Number.isInteger);
        const Stage = document.getElementById("stage");
        if (!Stage || !document.createElementNS) return;
        this.SVG = document.createElementNS(SvgNamespace, "svg");
        Object.assign(this.SVG.style, { position: "absolute", inset: "0", width: "100%", height: "100%",
            pointerEvents: "none", zIndex: 14, overflow: "hidden" });
        Stage.appendChild(this.SVG);
        this.ConstructPanel(Stage);
    }

    ConstructPanel(Stage)
    {
        const Panel = document.createElement("div");
        Object.assign(Panel.style, { position: "absolute", top: "12px", right: "12px", zIndex: 15, width: "300px",
            background: "#171d23e6", color: "#d5dce6", padding: "10px 12px", borderRadius: "8px",
            fontSize: "11px", fontFamily: "inherit", pointerEvents: "auto", lineHeight: "1.5" });
        const Title = document.createElement("div");
        Title.textContent = "Geometry hierarchy — virtualized geometry debug";
        Title.style.fontWeight = "600";
        Panel.appendChild(Title);
        this.Stats = document.createElement("div");
        this.Stats.textContent = "Waiting for scene…";
        Panel.appendChild(this.Stats);
        this.LevelRow = document.createElement("div");
        Object.assign(this.LevelRow.style, { display: "flex", flexWrap: "wrap", gap: "4px", margin: "6px 0" });
        Panel.appendChild(this.LevelRow);
        const MakeSelect = (Label, Options, Current, OnChange) =>
        {
            const Row = document.createElement("label");
            Row.textContent = Label + " ";
            const Select = document.createElement("select");
            for (const [Value, Text] of Options)
            {
                const Option = document.createElement("option");
                Option.value = Value; Option.textContent = Text; Option.selected = Value === Current;
                Select.appendChild(Option);
            }
            Select.addEventListener("change", () => OnChange(Select.value));
            Row.appendChild(Select);
            Panel.appendChild(Row);
            Panel.appendChild(document.createElement("br"));
        };
        MakeSelect("Show", [["both", "Bounds + wireframe"], ["bounds", "Bounds"], ["wireframe", "Wireframe"]],
            this.Mode, (Value) => { this.Mode = Value; });
        MakeSelect("View", [["level", "DAG level (L buttons)"], ["cut", "Runtime cut (live camera)"]],
            this.ViewMode, (Value) => { this.ViewMode = Value; this.RuntimeKey = ""; });
        MakeSelect("Colour", [["cluster", "Cluster identity"], ["error", "Error heat"], ["class", "Opaque / transmissive"],
            ["uv", "UV checker (per-triangle)"], ["features", "Sharp edges / UV seams"], ["page", "Streaming page / residency"]],
            this.ColourMode, (Value) => { this.ColourMode = Value; });
        const Note = document.createElement("div");
        Note.textContent = "Bounds are error-expanded. Wireframe is the simplified level geometry. " +
            "Class: grey opaque · cyan transmissive · magenta = mixed (must never appear). " +
            "Features: red = triangle touches a marked sharp edge · cyan = touches only a UV seam. " +
            "Page: hue = streaming page, grey = page not resident yet (pages stream in root-first). " +
            "Runtime cut: green = phase-1 visible · cyan = phase-2 disoccluded · red = Hi-Z occluded.";
        Note.style.opacity = "0.75";
        Panel.appendChild(Note);
        Stage.appendChild(Panel);
        this.Panel = Panel;
    }

    ColourFor(Node, MaxError, Record = null)
    {
        if (this.ColourMode === "page")
        {
            if (Node.PageIndex === undefined) return "#9aa7b8";
            if (Record?.Residency && !Record.Residency.IsPageResident(Node.PageIndex)) return "#414c5c";
            return "hsl(" + ((Node.PageIndex * 97.3) % 360).toFixed(1) + " 65% 58%)";
        }
        if (this.ColourMode === "error")
        {
            const T = MaxError > 0 ? Math.min(1, Node.Error / MaxError) : 0;
            return "hsl(" + Math.round(240 - 240 * T) + " 85% 60%)";
        }
        if (this.ColourMode === "class")
            return Node.ClassMask === 2 ? "#46dcf0" : Node.ClassMask === 1 ? "#9aa7b8" : "#ff4fd8";
        return "hsl(" + ((Node.Identity * 137.508) % 360).toFixed(1) + " 62% 58%)";
    }

    EnsureBuild(FacetSetIndex, Set)
    {
        const Existing = this.Builds.get(FacetSetIndex);
        if (Existing && Existing.FacetCount === Set.Facets.length) return Existing;
        const Record = { FacetCount: Set.Facets.length, Hierarchy: null, Failure: null, BuildMs: 0, MaxError: 0 };
        try
        {
            const Begin = performance.now();
            Record.Hierarchy = BuildGeometryHierarchy(Set, { TransmissiveFamilies: this.TransmissiveFamilies });
            Record.BuildMs = performance.now() - Begin;
            for (const Node of Record.Hierarchy.Clusters) Record.MaxError = Math.max(Record.MaxError, Node.Error);
            // 📝 Cluster debug extras: pack streaming pages and start a cold residency so the Page colour
            //    mode and the runtime-cut view can show pages streaming in (root pages pinned + resident).
            try
            {
                Record.Pages = PackHierarchyPages(Record.Hierarchy, {});
                Record.Residency = new HierarchyResidencyManager(Record.Pages, {});
                for (const Page of Record.Pages)
                    if (!Page.Pinned) Record.Residency.RequestPage(Page.PageIndex, 1);
            }
            catch { Record.Pages = null; Record.Residency = null; }
        }
        catch (Failure)
        {
            Record.Failure = String(Failure.message ?? Failure);
        }
        this.Builds.set(FacetSetIndex, Record);
        return Record;
    }

    RefreshLevelButtons(MaxLevel)
    {
        const Signature = "levels:" + MaxLevel + ":sel:" + this.SelectedLevel;
        if (Signature === this.LevelSignature) return;
        this.LevelSignature = Signature;
        this.LevelRow.replaceChildren();
        for (let Level = 0; Level <= MaxLevel; ++Level)
        {
            const Button = document.createElement("button");
            Button.textContent = "L" + Level;
            Object.assign(Button.style, { padding: "2px 8px", borderRadius: "6px", cursor: "pointer",
                border: "1px solid " + (Level === this.SelectedLevel ? "#61eaa0" : "#3a4654"),
                background: Level === this.SelectedLevel ? "#1e3a2c" : "#222c36", color: "#d5dce6" });
            Button.addEventListener("click", () => { this.SelectedLevel = Level; this.LevelSignature = ""; });
            this.LevelRow.appendChild(Button);
        }
    }

    AppendBounds(Node, Matrix, Camera, Width, Height, Colour, Opacity = "0.85", StrokeWidth = "1.2")
    {
        const S = this.SVG;
        const Points = Array.from({ length: 8 }, (_, K) =>
        {
            const P = Node.Minimum.map((V, A) => (K & (1 << A) ? Node.Maximum[A] : V));
            return ProjectGizmo(TransformPoint(Matrix, P), Camera, Width, Height);
        });
        let Path = "";
        for (let K = 0; K < 8; ++K) for (let A = 0; A < 3; ++A)
        {
            const J = K ^ (1 << A);
            if (J < K || !Points[K] || !Points[J]) continue;
            Path += "M" + Points[K][0].toFixed(1) + "," + Points[K][1].toFixed(1) +
                    "L" + Points[J][0].toFixed(1) + "," + Points[J][1].toFixed(1);
        }
        if (!Path) return false;
        const Element = document.createElementNS(SvgNamespace, "path");
        Element.setAttribute("d", Path);
        Element.setAttribute("fill", "none");
        Element.setAttribute("stroke", Colour);
        Element.setAttribute("stroke-width", StrokeWidth);
        Element.setAttribute("opacity", Opacity);
        S.appendChild(Element);
        return true;
    }

    // 📝 Live cluster debug: run the actual Phase-2 runtime (DAG cut -> frustum/cone -> two-phase Hi-Z) on
    //    the CPU at reduced resolution, throttled, and draw what it decided: phase-1 visible clusters green,
    //    phase-2 disoccluded cyan, Hi-Z occluded red. Culled-by-frustum/cone clusters are counted, not drawn.
    DrawCut(PerSet, Scene, CameraState, Width, Height)
    {
        const Now = performance.now();
        const Key = [CameraState.Position, CameraState.Forward].flat().map((V) => V.toFixed(2)).join(",") +
            "|" + this.CutThreshold;
        if ((!this.RuntimeResult || Key !== this.RuntimeKey) && Now - this.RuntimeTime > 350)
        {
            this.RuntimeTime = Now;
            this.RuntimeKey = Key;
            const Instances = [];
            for (const { Record, Occupants } of PerSet.values())
                for (const Occupant of Occupants)
                {
                    const Matrix = Scene.Occupants[Occupant]?.Transform;
                    if (!Matrix || Matrix.length < 16) continue;
                    Instances.push({ Hierarchy: Record.Hierarchy, LocalToWorld: Float64Array.from(Matrix),
                                     PreviousLocalToWorld: Float64Array.from(Matrix), Occupant, Residency: Record.Residency });
                }
            if (Instances.length)
            {
                const Eye = CameraState.Position;
                const RuntimeCamera = { Eye, Target: Eye.map((V, A) => V + CameraState.Forward[A]), Up: CameraState.Up,
                    FovY: 2 * Math.atan(CameraState.TanY ?? 0.5), ScreenWidth: 320, ScreenHeight: 240,
                    Near: 0.1, Far: 2000, PreviousViewProjection: this.RuntimeResult?.ViewProjection };
                try
                {
                    this.RuntimeResult = ExecuteHierarchyRuntimeFrame(Instances, RuntimeCamera,
                        { PixelErrorThreshold: this.CutThreshold, PreviousHiZPyramid: this.RuntimeResult?.CurrentHiZPyramid ?? null });
                }
                catch { this.RuntimeResult = null; }
            }
        }
        const Result = this.RuntimeResult;
        if (!Result) { this.CutStats = "runtime cut unavailable"; return; }
        const MatrixOf = (Item) => Scene.Occupants[Item.Occupant]?.Transform;
        let TrianglesDrawn = 0;
        const WireframePaths = new Map();
        for (const Item of Result.VisibleClusters)
        {
            const Matrix = MatrixOf(Item);
            if (!Matrix) continue;
            const Colour = Item.Phase === 2 ? "#3fd2ff" : "#61eaa0";
            this.AppendBounds(Item.Node, Matrix, CameraState, Width, Height, Colour);
            if (this.Mode === "bounds") continue;
            const Level = Item.Hierarchy.Levels[Item.Node.Level];
            for (const Triangle of Item.Node.Triangles)
            {
                if (TrianglesDrawn >= WireframeTriangleCap) break;
                const Screen = [];
                for (let Corner = 0; Corner < 3; ++Corner)
                {
                    const Vertex = Level.Triangles[Triangle * 3 + Corner];
                    const P = [Level.Positions[Vertex * 3], Level.Positions[Vertex * 3 + 1], Level.Positions[Vertex * 3 + 2]];
                    Screen.push(ProjectGizmo(TransformPoint(Matrix, P), CameraState, Width, Height));
                }
                if (Screen.some((P) => !P)) continue;
                let Segment = "";
                for (let Edge = 0; Edge < 3; ++Edge)
                {
                    const P = Screen[Edge], Q = Screen[(Edge + 1) % 3];
                    Segment += "M" + P[0].toFixed(1) + "," + P[1].toFixed(1) + "L" + Q[0].toFixed(1) + "," + Q[1].toFixed(1);
                }
                WireframePaths.set(Colour, (WireframePaths.get(Colour) ?? "") + Segment);
                ++TrianglesDrawn;
            }
        }
        for (const Item of Result.OccludedCulled)
        {
            const Matrix = MatrixOf(Item);
            if (Matrix) this.AppendBounds(Item.Node, Matrix, CameraState, Width, Height, "#ff5340", "0.5", "1");
        }
        for (const [Colour, Path] of WireframePaths)
        {
            const Element = document.createElementNS(SvgNamespace, "path");
            Element.setAttribute("d", Path);
            Element.setAttribute("fill", "none");
            Element.setAttribute("stroke", Colour);
            Element.setAttribute("stroke-width", "0.7");
            Element.setAttribute("opacity", "0.9");
            this.SVG.appendChild(Element);
        }
        const St = Result.Stats;
        let Resident = 0, PageTotal = 0, Queued = 0;
        for (const { Record } of PerSet.values())
            if (Record.Pages)
            {
                PageTotal += Record.Pages.length;
                Resident += Record.Pages.filter((Page) => Record.Residency.IsPageResident(Page.PageIndex)).length;
                Queued += Record.Residency.StreamingQueue.length;
            }
        this.CutStats = "runtime cut · " + St.CutClusterCount + " cut → " + Result.VisibleClusters.length +
            " visible (" + St.Phase2DisoccludedCount + " disoccluded) · " + St.VisibleTriangles + " of " +
            St.SourceTriangles + " tris · frustum " + St.FrustumCulledCount + " · cone " + St.ConeCulledCount +
            " · occluded " + St.OccludedCulledCount + " · glass draws " + St.TransmissiveDrawCount +
            " · pages " + Resident + "/" + PageTotal + " resident, " + Queued + " queued" +
            " · threshold " + this.CutThreshold + " px (?px= to change)";
    }

    Draw()
    {
        const S = this.SVG, H = this.Host;
        if (!S) return;
        const Scene = H.Scene, Camera = H.CameraState;
        if (!Scene?.DrawRuns || !Camera)
        {
            if (this.Visible) S.replaceChildren();
            this.Visible = false;
            return;
        }
        this.Visible = true;
        const Bounds = S.getBoundingClientRect();
        const Width = Bounds.width, Height = Bounds.height;
        // Group draw runs per built facet set: each run contributes occupants to draw the hierarchy through.
        const PerSet = new Map();
        for (const Run of Scene.DrawRuns)
        {
            const Set = Scene.FacetSets[Run.FacetSet];
            if (!Set?.Facets || Set.Facets.length < 256 || Set.VertexCapacity) continue;
            const Record = this.EnsureBuild(Run.FacetSet, Set);
            if (!Record.Hierarchy) continue;
            if (!PerSet.has(Run.FacetSet)) PerSet.set(Run.FacetSet, { Record, Occupants: [] });
            for (let O = Run.First; O < Run.First + Run.Count; ++O) PerSet.get(Run.FacetSet).Occupants.push(O);
        }
        let MaxLevel = 0;
        for (const { Record } of PerSet.values()) MaxLevel = Math.max(MaxLevel, Record.Hierarchy.Levels.length - 1);
        this.SelectedLevel = Math.min(this.SelectedLevel, MaxLevel);
        this.RefreshLevelButtons(MaxLevel);

        // 📝 Pages stream in a couple per draw so Page colour / runtime-cut residency visibly fills in.
        ++this.FrameCounter;
        for (const { Record } of PerSet.values())
        {
            Record.Residency?.ServiceStreamingQueue(this.FrameCounter, 2);
        }

        S.replaceChildren();
        if (this.ViewMode === "cut")
        {
            this.DrawCut(PerSet, Scene, Camera, Width, Height);
            if (this.Stats) this.Stats.textContent = this.CutStats ?? "runtime cut…";
            return;
        }
        let ClusterTotal = 0, TriangleTotal = 0, RootMaxError = 0, BuildMsTotal = 0, Failures = 0, Skipped = 0;
        let BoundsDrawn = 0, TrianglesDrawn = 0, SharpTotal = 0, SeamTotal = 0;
        const WireframePaths = new Map();
        for (const { Record, Occupants } of PerSet.values())
        {
            const Hierarchy = Record.Hierarchy;
            BuildMsTotal += Record.BuildMs;
            RootMaxError = Math.max(RootMaxError, Record.MaxError);
            const Level = Math.min(this.SelectedLevel, Hierarchy.Levels.length - 1);
            const Entry = Hierarchy.Levels[Level];
            ClusterTotal += Entry.Clusters.length;
            TriangleTotal += Entry.TriangleCount;
            SharpTotal += Entry.SharpEdgeCount ?? 0;
            SeamTotal += Entry.UvSeamEdgeCount ?? 0;
            const SoupTriangles = this.Mode !== "bounds" ? CollectHierarchyLevel(Hierarchy, Level) : null;
            for (const Occupant of Occupants)
            {
                const Matrix = Scene.Occupants[Occupant]?.Transform;
                if (!Matrix || Matrix.length < 16) continue;
                if (this.Mode !== "wireframe")
                    for (const Identity of Entry.Clusters)
                    {
                        if (BoundsDrawn >= BoundsCap) { ++Skipped; continue; }
                        const Node = Hierarchy.Clusters[Identity];
                        const Points = Array.from({ length: 8 }, (_, K) =>
                        {
                            const P = Node.Minimum.map((V, A) => (K & (1 << A) ? Node.Maximum[A] : V));
                            return ProjectGizmo(TransformPoint(Matrix, P), Camera, Width, Height);
                        });
                        let Path = "";
                        for (let K = 0; K < 8; ++K) for (let A = 0; A < 3; ++A)
                        {
                            const J = K ^ (1 << A);
                            if (J < K || !Points[K] || !Points[J]) continue;
                            Path += "M" + Points[K][0].toFixed(1) + "," + Points[K][1].toFixed(1) +
                                    "L" + Points[J][0].toFixed(1) + "," + Points[J][1].toFixed(1);
                        }
                        if (!Path) continue;
                        ++BoundsDrawn;
                        const Element = document.createElementNS(SvgNamespace, "path");
                        Element.setAttribute("d", Path);
                        Element.setAttribute("fill", "none");
                        Element.setAttribute("stroke", this.ColourFor(Node, Record.MaxError, Record));
                        Element.setAttribute("stroke-width", "1.2");
                        Element.setAttribute("opacity", "0.85");
                        S.appendChild(Element);
                    }
                if (SoupTriangles)
                    for (let Triangle = 0; Triangle < SoupTriangles.TriangleCount; ++Triangle)
                    {
                        if (TrianglesDrawn >= WireframeTriangleCap) { ++Skipped; continue; }
                        const Node = Hierarchy.Clusters[SoupTriangles.Owners[Triangle]];
                        let Colour = this.ColourFor(Node, Record.MaxError, Record);
                        if (this.ColourMode === "uv" && SoupTriangles.CornerUVs)
                        {
                            const U = (SoupTriangles.CornerUVs[Triangle * 6] + SoupTriangles.CornerUVs[Triangle * 6 + 2] +
                                       SoupTriangles.CornerUVs[Triangle * 6 + 4]) / 3;
                            const V = (SoupTriangles.CornerUVs[Triangle * 6 + 1] + SoupTriangles.CornerUVs[Triangle * 6 + 3] +
                                       SoupTriangles.CornerUVs[Triangle * 6 + 5]) / 3;
                            Colour = (Math.floor(U * 24) + Math.floor(V * 24)) % 2 === 0 ? "#e8ecf2" : "#3460a8";
                        }
                        else if (this.ColourMode === "features")
                        {
                            if (SoupTriangles.SharpMask?.[Triangle]) Colour = "#ff4632";
                            else if (SoupTriangles.UvSeamMask?.[Triangle]) Colour = "#37d7eb";
                        }
                        let Segment = "";
                        const Screen = [];
                        for (let Corner = 0; Corner < 3; ++Corner)
                        {
                            const P = [0, 1, 2].map((A) => SoupTriangles.Positions[Triangle * 9 + Corner * 3 + A]);
                            Screen.push(ProjectGizmo(TransformPoint(Matrix, P), Camera, Width, Height));
                        }
                        if (Screen.some((P) => !P)) continue;
                        for (let Edge = 0; Edge < 3; ++Edge)
                        {
                            const P = Screen[Edge], Q = Screen[(Edge + 1) % 3];
                            Segment += "M" + P[0].toFixed(1) + "," + P[1].toFixed(1) +
                                       "L" + Q[0].toFixed(1) + "," + Q[1].toFixed(1);
                        }
                        WireframePaths.set(Colour, (WireframePaths.get(Colour) ?? "") + Segment);
                        ++TrianglesDrawn;
                    }
            }
        }
        for (const [Colour, Path] of WireframePaths)
        {
            const Element = document.createElementNS(SvgNamespace, "path");
            Element.setAttribute("d", Path);
            Element.setAttribute("fill", "none");
            Element.setAttribute("stroke", Colour);
            Element.setAttribute("stroke-width", "0.7");
            Element.setAttribute("opacity", "0.9");
            S.appendChild(Element);
        }
        for (const Record of this.Builds.values()) if (Record.Failure) ++Failures;
        if (this.Stats)
            this.Stats.textContent =
                PerSet.size + " facet set(s) · showing L" + this.SelectedLevel + " of " + MaxLevel +
                " · " + ClusterTotal + " clusters · " + TriangleTotal + " triangles at this level" +
                " · DAG max error " + RootMaxError.toFixed(4) +
                " · sharp edges " + SharpTotal + " · UV seams " + SeamTotal +
                (this.ColourMode === "page" ? " · pages " +
                    [...PerSet.values()].reduce((N, { Record }) => N + (Record.Pages?.filter((P) => Record.Residency.IsPageResident(P.PageIndex)).length ?? 0), 0) +
                    "/" + [...PerSet.values()].reduce((N, { Record }) => N + (Record.Pages?.length ?? 0), 0) + " resident" : "") +
                " · build " + BuildMsTotal.toFixed(0) + " ms" +
                (Skipped ? " · " + Skipped + " items over draw cap" : "") +
                (Failures ? " · " + Failures + " build failure(s): " +
                    [...this.Builds.values()].find((R) => R.Failure).Failure : "") +
                (PerSet.size === 0 ? " · no eligible static facet sets (>=256 facets) in view of the draw runs" : "");
    }
}
