// GPU clustered forward+/deferred light lists. One conservative sphere/frustum
// test per light per 32×32×log-depth cluster, not one light loop per screen pixel.
// GI cards traverse the same light records without a camera-dependent cluster.
import { RetrievePhotometricAtlas, PhotometrySize } from "./Photometry.js";

export const ClusterConfiguration = Object.freeze({
    Tile: 32, Slices: 16, Capacity: 128, PerCluster: 32, Near: 0.02, Far: 512
});
const LightStride = 64;  // four vec4f, also in LightRecords.wgsl
const ClusterStride = 1 + ClusterConfiguration.PerCluster; // count followed by light indices

export class ClusteredLighting
{
    constructor(Exchange, Shaders, FrameRecords)
    {
        this.Exchange = Exchange;
        this.Shaders = Shaders;
        this.FrameRecords = FrameRecords;
        this.Count = 0;
        this.Extent = [0, 0];
        this.TileCount = [1, 1];
        this.ClusterCount = 0;
    }

    async ConstructResources()
    {
        const E = this.Exchange, Usage = GPUBufferUsage;
        this.LightRecords = E.ConstructRecords(LightStride * ClusterConfiguration.Capacity, Usage.STORAGE | Usage.COPY_DST, "Local lights");
        this.PhotometryRecords = E.ConstructRecordsFrom(await RetrievePhotometricAtlas(), Usage.STORAGE, "IES photometry");
        this.ConfigurationRecords = E.ConstructRecords(48, Usage.UNIFORM | Usage.COPY_DST, "Cluster configuration");
        this.Layout = E.ConstructDescriptorLayout(["StorageRead", "StorageRead", "Uniform", "StorageRead"],
                                                  GPUShaderStage.COMPUTE | GPUShaderStage.FRAGMENT, "Local light transport");
        this.BuildLayout = E.ConstructDescriptorLayout(["Uniform", "StorageRead", "Storage", "Uniform"],
                                                       GPUShaderStage.COMPUTE, "Build clustered light lists");
        this.EmptyLayout = E.ConstructDescriptorLayout([], GPUShaderStage.COMPUTE | GPUShaderStage.FRAGMENT, "Unused distance field");
        this.BuildProgram = await this.Shaders.ConstructComputeProgram("ClusterBuild.wgsl", "BuildClusters", [this.BuildLayout]);
    }

    Resize(Width, Height)
    {
        if (this.Extent[0] === Width && this.Extent[1] === Height) return;
        const C = ClusterConfiguration;
        this.Extent = [Width, Height];
        this.TileCount = [Math.ceil(Width / C.Tile), Math.ceil(Height / C.Tile)];
        this.ClusterCount = this.TileCount[0] * this.TileCount[1] * C.Slices;
        this.ClusterRecords?.destroy();
        this.ClusterRecords = this.Exchange.ConstructRecords(this.ClusterCount * ClusterStride * 4,
            GPUBufferUsage.STORAGE, "Cluster members");
        this.Group = this.Exchange.ConstructDescriptorGroup(this.Layout,
            [this.LightRecords, this.ClusterRecords, this.ConfigurationRecords, this.PhotometryRecords]);
        this.BuildGroup = this.Exchange.ConstructDescriptorGroup(this.BuildLayout,
            [this.FrameRecords, this.LightRecords, this.ClusterRecords, this.ConfigurationRecords]);
    }

    Upload(Lights, Camera)
    {
        const C = ClusterConfiguration;
        this.Count = Math.min(C.Capacity, Lights.length);
        const Data = new Float32Array(Math.max(1, this.Count) * LightStride / 4);
        for (let Index = 0; Index < this.Count; Index++)
        {
            const Light = Lights[Index], At = Index * (LightStride / 4);
            Data.set([...Light.Position, Light.Reach, ...Light.Direction, Light.Inner,
                      ...Light.Up, Light.Outer, ...Light.Colour, Light.Profile], At);
        }
        if (this.Count) this.Exchange.Device.queue.writeBuffer(this.LightRecords, 0, Data);
        const Record = new ArrayBuffer(48);
        const U = new Uint32Array(Record), F = new Float32Array(Record);
        U.set([...this.TileCount, this.Count, C.Slices], 0);
        const HalfY = Math.tan(Camera.VerticalFieldOfView * 0.5);
        F.set([...this.Extent, HalfY * Camera.Aspect, HalfY], 4);
        F.set([C.Near, C.Far, C.Slices / Math.log(C.Far / C.Near), 0], 8);
        this.Exchange.Device.queue.writeBuffer(this.ConfigurationRecords, 0, Record);
    }

    RecordClusters(Encoder)
    {
        const Pass = Encoder.beginComputePass({ label: "Build light clusters" });
        Pass.setPipeline(this.BuildProgram);
        Pass.setBindGroup(0, this.BuildGroup);
        Pass.dispatchWorkgroups(Math.ceil(this.ClusterCount / 64));
        Pass.end();
    }

    QueryReadout()
    {
        return [`${this.Count} local lights / ${ClusterConfiguration.Capacity} capacity`,
                `${this.TileCount[0]}×${this.TileCount[1]}×${ClusterConfiguration.Slices} clusters · ${ClusterConfiguration.PerCluster} max per cluster`,
                `${PhotometrySize.Azimuth}×${PhotometrySize.Polar} samples per IES profile`];
    }
}
