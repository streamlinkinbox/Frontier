// Two-level triangle acceleration for the actual vehicle game, NOT the distance
// field or the surface cache. BLASes are shared by static instances; each wheel
// and the articulated body own a deformable BLAS. Threaded preorder nodes avoid
// both fixed traversal stacks and a step-count cutoff.
import { ConstructBoundingStructure } from "./BoundingStructure.js";
import { OccupantRole } from "./FacetStructure.js";
import { CrossVector, SubtractVector, NormalizeVector, TransformPoint, TransformBounds } from "./TransformProjection.js";

export const RayNodeBytes = 32;
export const RayTriangleBytes = 96;
export const RayInstanceWords = 8;
export const RaySceneSources = ["PathScene.wgsl", "PathRefit.wgsl"];
const Invalid = 0xFFFFFFFF;
const Luminance = (V) => V[0] * 0.2126 + V[1] * 0.7152 + V[2] * 0.0722;

// Convert the existing binned-SAH tree to depth-first threading. Interior Link is
// the first node AFTER its subtree; leaf Link is the first ordered primitive.
// Nodes and triangles have separate global index spaces.
export function ThreadBoundingStructure(Facets, NodeBase = 0, TriangleBase = 0)
{
    if (!Facets.length) return { Nodes: [], Facets: [], Levels: [] };
    const Corners = Float32Array.from(Facets.flatMap((F) => F.Corners.flat()));
    const Tree = ConstructBoundingStructure(Corners);
    const F = Tree.NodeScalars;
    const U = new Uint32Array(F.buffer, F.byteOffset, F.length);
    const Nodes = [], Levels = [];
    const Visit = (Old) => {
        const Index = Nodes.length;
        const Count = U[Old * 8 + 7], First = U[Old * 8 + 3];
        const Node = { Minimum: Array.from(F.slice(Old * 8, Old * 8 + 3)),
                       Maximum: Array.from(F.slice(Old * 8 + 4, Old * 8 + 7)), Count, Link: 0, Height: 0 };
        Nodes.push(Node);
        if (Count) Node.Link = TriangleBase + First;
        else {
            const Left = Visit(First), Right = Visit(First + 1);
            Node.Height = 1 + Math.max(Nodes[Left].Height, Nodes[Right].Height);
            Node.Link = NodeBase + Nodes.length;
        }
        (Levels[Node.Height] ||= []).push(NodeBase + Index);
        return Index;
    };
    Visit(0);
    return { Nodes, Facets: Array.from(Tree.FacetOrder, (Index) => Facets[Index]), Levels };
}

function TriangleArea(Facet, Transform)
{
    const P = Facet.Corners.map((Point) => TransformPoint(Transform, Point));
    return 0.5 * Math.hypot(...CrossVector(SubtractVector(P[1], P[0]), SubtractVector(P[2], P[0])));
}

function PackTriangle(F, U, Word, Facet, Owner)
{
    const FlatNormal = NormalizeVector(CrossVector(SubtractVector(Facet.Corners[1], Facet.Corners[0]),
                                                  SubtractVector(Facet.Corners[2], Facet.Corners[0])));
    for (let Corner = 0; Corner < 3; ++Corner)
    {
        F.set(Facet.Corners[Corner], Word + Corner * 4);
        F.set(Facet.Normals?.[Corner] || FlatNormal, Word + 12 + Corner * 4);
    }
    U[Word + 3] = Facet.Family;
    U[Word + 7] = Owner;
}

// Pure construction is deliberately separate from WebGPU for numerical/unit tests.
export function ConstructRayScene(Scene, Materials, MeshCache = new WeakMap())
{
    const Meshes = [], Nodes = [], Triangles = [], DynamicLevels = [];
    const MeshForSet = new Map(), MeshForOccupant = new Map();
    const AddMesh = (Facets, Owner = Invalid) => {
        const Root = Nodes.length, First = Triangles.length;
        let Cached = MeshCache.get(Facets);
        if (!Cached) { Cached = ThreadBoundingStructure(Facets); MeshCache.set(Facets, Cached); }
        const Tree = {
            Nodes: Cached.Nodes.map((Node) => ({ ...Node, Link: Node.Link + (Node.Count ? First : Root) })),
            Facets: Cached.Facets,
            Levels: Cached.Levels.map((List) => List.map((Node) => Node + Root))
        };
        for (const Node of Tree.Nodes) Nodes.push(Node);
        for (const Facet of Tree.Facets) Triangles.push({ Facet, Owner });
        const Mesh = { Root, End: Nodes.length, First, Count: Tree.Facets.length, Facets: Tree.Facets };
        Meshes.push(Mesh);
        if (Owner !== Invalid)
            Tree.Levels.forEach((List, Level) => (DynamicLevels[Level] ||= []).push(...List));
        return Mesh;
    };
    // These are contiguous so the deformation kernel touches only the first range.
    Scene.Occupants.slice(0, 5).forEach((Occupant, Index) => {
        MeshForOccupant.set(Index, AddMesh(Scene.FacetSets[Occupant.FacetSet].Facets, Index));
    });
    const DynamicTriangles = Triangles.length;
    const SharedSets = new Set(Scene.Occupants.filter((O, Index) => Index >= 5 && !O.Fragment && (O.Roles & OccupantRole.Raster)).map((O) => O.FacetSet));
    Scene.FacetSets.forEach((Set, Index) => {
        if (!Set.Composite && Set.Facets.length && SharedSets.has(Index)) MeshForSet.set(Index, AddMesh(Set.Facets));
    });
    // Composite raster draws hide per-fragment transforms in OccupantOffset. The
    // RT scene has one real instance/BLAS per fragment, including non-Raster slots.
    Scene.Occupants.forEach((Occupant, Index) => {
        if (Occupant.Fragment && Occupant.RayFacets?.length)
            MeshForOccupant.set(Index, AddMesh(Occupant.RayFacets));
    });
    const Active = [];
    Scene.Occupants.forEach((Occupant, Index) => {
        if (Occupant.Inactive || (!(Occupant.Roles & OccupantRole.Raster) && !Occupant.Fragment)) return;
        const Mesh = MeshForOccupant.get(Index) || MeshForSet.get(Occupant.FacetSet);
        if (!Mesh?.Count) return;
        const Bounds = TransformBounds(Occupant.Transform, Nodes[Mesh.Root].Minimum, Nodes[Mesh.Root].Maximum);
        Active.push({ Index, Mesh, Bounds });
    });
    // Build a TLAS over boxes using the same tested SAH builder. These three
    // points have precisely the desired bounds and centroid; they are not traced.
    const BoxFacets = Active.map((Instance) => ({ Family: 0, Instance,
        Corners: [Instance.Bounds.Minimum, Instance.Bounds.Maximum,
            Instance.Bounds.Minimum.map((V, Axis) => (V + Instance.Bounds.Maximum[Axis]) * 0.5)] }));
    const TlasRoot = Nodes.length;
    const Tlas = ThreadBoundingStructure(BoxFacets, TlasRoot, 0);
    Nodes.push(...Tlas.Nodes);
    const Domain = Tlas.Facets.map((Facet) => Facet.Instance);
    let DomainTriangleCount = 0;
    for (const Instance of Domain) {
        DomainTriangleCount += Instance.Mesh.Count;
        Instance.Prefix = DomainTriangleCount;
    }
    const Emitters = [];
    let EmissionTotal = 0;
    for (const Instance of Active) {
        const Occupant = Scene.Occupants[Instance.Index];
        Instance.Mesh.Facets.forEach((Facet, Local) => {
            const Family = Occupant.MaterialOverride >= 0 ? Occupant.MaterialOverride : Facet.Family;
            const Power = Math.max(0, Luminance(Materials[Family].Emission));
            const Area = TriangleArea(Facet, Occupant.Transform);
            if (Power <= 0 || Area < 1e-16) return;
            const Weight = Power * Area;
            EmissionTotal += Weight;
            Emitters.push({ Triangle: Instance.Mesh.First + Local, Instance: Instance.Index, Weight, Cdf: EmissionTotal });
        });
    }
    // Emission lookup is binary-searchable by (instance, triangle). The CDF uses
    // this same order. Dynamic areas need not update selection probabilities: the
    // shader divides by CURRENT world area, not by this construction-time area.
    Emitters.sort((A, B) => A.Instance - B.Instance || A.Triangle - B.Triangle);
    let Cdf = 0;
    for (const E of Emitters) { E.Probability = E.Weight / EmissionTotal; Cdf += E.Probability; E.Cdf = Cdf; }
    if (Emitters.length) Emitters[Emitters.length - 1].Cdf = 1;

    const TriangleOffset = Nodes.length * 8;
    const InstanceOffset = TriangleOffset + Triangles.length * 24;
    const DomainOffset = InstanceOffset + Scene.Occupants.length * RayInstanceWords;
    const EmitterOffset = DomainOffset + Domain.length * 2;
    const RefitOffset = EmitterOffset + Emitters.length * 4;
    const Refit = [], Stages = [];
    const AddStages = (Levels, Kind) => Levels.forEach((List, Level) => {
        if (!List?.length) return;
        Stages.push({ First: RefitOffset + Refit.length, Count: List.length, Kind: Level === 0 ? Kind : 2 });
        Refit.push(...List);
    });
    AddStages(DynamicLevels, 0);
    AddStages(Tlas.Levels, 1);
    const Bytes = new ArrayBuffer(Math.max(16, (RefitOffset + Refit.length) * 4));
    const F = new Float32Array(Bytes), U = new Uint32Array(Bytes);
    Nodes.forEach((Node, Index) => {
        F.set(Node.Minimum, Index * 8); U[Index * 8 + 3] = Node.Link;
        F.set(Node.Maximum, Index * 8 + 4); U[Index * 8 + 7] = Node.Count;
    });
    Triangles.forEach(({ Facet, Owner }, Index) => PackTriangle(F, U, TriangleOffset + Index * 24, Facet, Owner));
    Active.forEach(({ Index, Mesh }) => U.set([Mesh.Root, Mesh.End, Mesh.First, Mesh.Count, 1, 0, 0, 0], InstanceOffset + Index * RayInstanceWords));
    Domain.forEach((I, Index) => U.set([I.Index, I.Prefix], DomainOffset + Index * 2));
    Emitters.forEach((E, Index) => {
        const Word = EmitterOffset + Index * 4;
        U[Word] = E.Triangle; U[Word + 1] = E.Instance; F[Word + 2] = E.Probability; F[Word + 3] = E.Cdf;
    });
    U.set(Refit, RefitOffset);
    return { Bytes, Nodes, Triangles, Meshes, Active, Domain, Emitters, Stages,
        TriangleOffset, InstanceOffset, DomainOffset, EmitterOffset, TlasRoot, TlasEnd: Nodes.length,
        DomainTriangleCount, DynamicTriangles,
        BaseTriangles: new Float32Array(Bytes.slice(TriangleOffset * 4, (TriangleOffset + DynamicTriangles * 24) * 4)) };
}

export class RayTracingScene
{
    constructor(Exchange, Shaders, Scene, Materials, OccupantRecords, VehicleRecords)
    {
        Object.assign(this, { Exchange, Shaders, Scene, Materials, OccupantRecords, VehicleRecords });
        this.Generation = 0;
        this.MeshCache = new WeakMap();
    }

    async ConstructPrograms()
    {
        this.Layout = this.Exchange.ConstructDescriptorLayout(["Uniform", "Storage", "StorageRead", "StorageRead", "StorageRead"],
            GPUShaderStage.COMPUTE, "Triangle BVH deformation/refit");
        this.DeformProgram = await this.Shaders.ConstructComputeProgram("PathRefit.wgsl", "DeformPathTriangles", [this.Layout]);
        this.RefitProgram = await this.Shaders.ConstructComputeProgram("PathRefit.wgsl", "RefitPathBounds", [this.Layout]);
    }

    Update(Frame = 0, Force = false)
    {
        const Signature = this.Scene.Occupants.map((O) => `${O.Inactive ? 0 : 1}:${O.FacetSet}:${O.Roles}:${O.MaterialOverride}:${O.RayGeometryVersion || 0}`).join(",")
            + this.Materials.map((M) => M.Emission.join(",")).join(";");
        // Periodic SAH reconstruction prevents refitted moving instances from
        // degenerating into an arbitrarily expensive tree. No triangle LOD/proxy.
        if (!Force && Signature === this.Signature && Frame % 120 !== 0) return false;
        const TopologyChanged = Signature !== this.Signature;
        const Started = performance.now();
        const Data = ConstructRayScene(this.Scene, this.Materials, this.MeshCache);
        this.LastRebuildMilliseconds = performance.now() - Started;
        const Limits = this.Exchange.Device.limits;
        if (Data.Bytes.byteLength > Math.min(Limits.maxStorageBufferBindingSize, Limits.maxBufferSize))
            throw new Error(`Triangle scene (${(Data.Bytes.byteLength / 1048576).toFixed(1)} MiB) exceeds the WebGPU storage binding limit.`);
        const Old = this.Resources || [], OldGeometry = this.Geometry, OldData = this.Data;
        this.Data = Data;
        this.Geometry = this.Exchange.ConstructRecordsFrom(new Uint32Array(Data.Bytes), GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC, "Threaded triangle BLAS/TLAS");
        this.BaseTriangles = this.Exchange.ConstructRecordsFrom(Data.BaseTriangles, GPUBufferUsage.STORAGE, "Undeformed vehicle triangles");
        // Previous DEFORMED local vertices are needed for exact tyre/contact,
        // steering-column and tablet-scale motion, not just rigid transforms.
        this.PreviousTriangles = this.Exchange.ConstructRecordsFrom(Data.BaseTriangles,
            GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC, "Previous deformed vehicle triangles");
        this.Resources = [this.Geometry, this.BaseTriangles, this.PreviousTriangles];
        this.PreviousLoaded = true;
        if (!TopologyChanged && OldGeometry && OldData.DynamicTriangles === Data.DynamicTriangles) {
            // A periodic SAH repack starts from authored geometry. Preserve the
            // last GPU-deformed pose BEFORE retiring that old allocation.
            const Copy = this.Exchange.Device.createCommandEncoder({ label: "Retain vehicle pose across BVH repack" });
            Copy.copyBufferToBuffer(OldGeometry, OldData.TriangleOffset * 4, this.PreviousTriangles, 0, Data.BaseTriangles.byteLength);
            this.Exchange.Device.queue.submit([Copy.finish()]);
        }
        const Group = (First, Count, Kind) => {
            const Uniform = this.Exchange.ConstructRecordsFrom(Uint32Array.of(Data.TriangleOffset, Data.InstanceOffset,
                Data.DomainOffset, First, Count, Kind, 0, 0), GPUBufferUsage.UNIFORM, "Refit stage");
            this.Resources.push(Uniform);
            return this.Exchange.ConstructDescriptorGroup(this.Layout,
                [Uniform, this.Geometry, this.BaseTriangles, this.OccupantRecords, this.VehicleRecords]);
        };
        this.DeformGroup = Group(0, Data.DynamicTriangles, 0);
        this.RefitStages = Data.Stages.map((Stage) => ({ ...Stage, Group: Group(Stage.First, Stage.Count, Stage.Kind) }));
        this.Signature = Signature;
        this.Generation++;
        // WebGPU destruction is deferred until already-submitted GPU uses finish.
        for (const Resource of Old) Resource.destroy();
        return TopologyChanged;
    }

    RecordRefit(Encoder, TimestampWrites)
    {
        if (!this.PreviousLoaded)
            Encoder.copyBufferToBuffer(this.Geometry, this.Data.TriangleOffset * 4,
                this.PreviousTriangles, 0, this.Data.BaseTriangles.byteLength);
        this.PreviousLoaded = false;
        const Pass = Encoder.beginComputePass({ label: "Triangle deformation and BLAS/TLAS refit", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.DeformProgram); Pass.setBindGroup(0, this.DeformGroup);
        Pass.dispatchWorkgroups(Math.ceil(this.Data.DynamicTriangles / 64));
        Pass.setPipeline(this.RefitProgram);
        // Dispatch boundaries provide storage visibility; never spin-wait for
        // children in a single dispatch (a GPU scheduling deadlock).
        for (const Stage of this.RefitStages) {
            Pass.setBindGroup(0, Stage.Group); Pass.dispatchWorkgroups(Math.ceil(Stage.Count / 64));
        }
        Pass.end();
    }

    Describe()
    {
        const D = this.Data;
        return [`${D.Active.length} triangle instances · ${D.DomainTriangleCount.toLocaleString()} instanced triangles`,
            `${D.Triangles.length.toLocaleString()} stored triangles · ${D.Nodes.length.toLocaleString()} threaded BVH nodes`,
            `${D.Emitters.length} emissive triangles · ${(D.Bytes.byteLength / 1048576).toFixed(1)} MiB acceleration data`];
    }
}
