//============================================================================================================================================
//                                                             SPARKSEQUENCE.JS
//============================================================================================================================================
// 🧩 Sparks: hot metal ground off where a rim or the body slides on the road or a wall — thrown, cooling, bouncing, drawn as streaks.

// 📝 The vehicle solver reports each frame's scrapes (VehicleSolver.RecordScrape): the strongest per contact — a hull
//    patch dragged along the asphalt, a rim pinched through a flat or bare once the rubber has gone — with the contact
//    point, the sliding velocity of the metal over the surface, the surface normal and a strength. Each scrape throws
//    sparks in proportion, at 1500–2100 K:
//
//    | Source | Where a spark leaves                          | With what velocity                                    |
//    |--------|-----------------------------------------------|-------------------------------------------------------|
//    | hull   | the scraping point                            | 30–75 % of the slide, fanned, kicked off the surface  |
//    | rim    | the rim's edge a little (α ≤ 0.47 rad) behind | that point's own: Slip + S(1 − cos α)·F + |S| sin α·N |
//    |        | the patch, where the metal leaves the road    | (S = Spin·r) — up off the trailing edge, so the trail |
//    |        |                                               | streams back from a rolling wheel                     |
//
//    In flight a spark falls, feels air drag, cools radiatively (its glow follows the blackbody temperature) and skips
//    off the road, losing most of its speed, until it is too cool to see.
//
//    A few hundred particles: they are integrated here on the CPU and drawn after the temporal antialiasing as thin
//    additive streaks (2.2 mm across, at least a pixel) from where the camera saw them a frame ago to where it sees
//    them now (SparkRaster.wgsl), depth-tested against the scene — thin, bright and fast-moving, they would otherwise
//    be averaged away by the history.

import { ResolveCourseHeight } from "./CourseStructure.js";

const SparkCapacity   = 2048;
const InstanceFloats  = 12;            // head xyz + width, tail xyz + 0, radiance rgb + 0
const Gravity         = 9.81;          // [m/s²]
const AirDrag         = 1.6;           // [1/s]
const CoolingSeconds  = 0.42;          // [s]  e-folding of a spark's excess temperature
const VisibleKelvin   = 950.0;         // [K]  below this a spark is gone
const SparksPerStrength = 700.0;       // [1/s] per unit scrape strength
const SparkWidth      = 0.0022;        // [m]  streak width at Size 1 — a glowing chip a fraction of a millimetre across, blurred

export const SparkSettingsDefaults = Object.freeze({ Enabled: true, Rate: 1.0, Brightness: 1.0, Size: 1.0 });

export class SparkSequence
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Settings = { ...SparkSettingsDefaults };
        this.Particles = [];
        this.Instances = new Float32Array(SparkCapacity * InstanceFloats);
        this.InstanceRecords = Exchange.ConstructRecords(SparkCapacity * InstanceFloats * 4, GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, "Spark instances");
        this.Metrics = { Live: 0, Emitted: 0, Scrapes: 0 };
        this.Seed = 0x5EED;
        this.Live = 0;
    }

    async ConstructPrograms(FrameRecords)
    {
        this.Layout = this.Exchange.ConstructDescriptorLayout(["Uniform"], GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, "Spark raster");
        this.Group = this.Exchange.ConstructDescriptorGroup(this.Layout, [FrameRecords]);
        this.Program = await this.Shaders.ConstructRasterProgram("SparkRaster.wgsl", {
            VertexEntry: "SparkVertex", FragmentEntry: "SparkFragment",
            VertexLayouts: [{ arrayStride: InstanceFloats * 4, stepMode: "instance", attributes: [
                { shaderLocation: 0, offset: 0,  format: "float32x4" },
                { shaderLocation: 1, offset: 16, format: "float32x4" },
                { shaderLocation: 2, offset: 32, format: "float32x4" }] }],
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater" },
            Targets: [{ format: "rgba16float", blend: { color: { srcFactor: "one", dstFactor: "one", operation: "add" },
                                                        alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" } } }]
        }, [this.Layout]);
    }

    Random()
    {
        this.Seed = (Math.imul(this.Seed, 1664525) + 1013904223) >>> 0;
        return this.Seed / 4294967296;
    }

    // 📝 Scrapes: [{ Point, Velocity (the metal's slide over the surface), Normal, Strength, Wheel,
    //    Rim: { Forward, SpinSpeed, Radius } for a rim, null for the hull }].
    Emit(Scrapes, Seconds)
    {
        if (!this.Settings.Enabled || !(Seconds > 0.0))
            return;
        this.Metrics.Scrapes = Scrapes.length;
        for (const Scrape of Scrapes)
        {
            const Expected = SparksPerStrength * this.Settings.Rate * Scrape.Strength * Seconds;
            let Count = Math.floor(Expected) + (this.Random() < Expected - Math.floor(Expected) ? 1 : 0);
            Count = Math.min(Count, 96);
            const Normal = Scrape.Normal || [0.0, 0.0, 1.0];
            const Slide = Scrape.Velocity;
            const SlideSpeed = Math.hypot(Slide[0], Slide[1], Slide[2]);
            for (let Spark = 0; Spark < Count && this.Particles.length < SparkCapacity; ++Spark)
            {
                const Spread = [this.Random() * 2.0 - 1.0, this.Random() * 2.0 - 1.0, this.Random() * 2.0 - 1.0];
                let Origin, Velocity;
                if (Scrape.Rim)
                {
                    // 📐 Detached α behind the patch in the sense the rim turns (most right behind it): from the patch at
                    //    R((1 − cos α)·N − σ·sin α·F), moving at Slip + S(1 − cos α)·F + |S|·sin α·N, σ = sign S; it keeps
                    //    75–100 % of that, scattered in a narrow cone and bounced a little off the road.
                    const { Forward, SpinSpeed, Radius } = Scrape.Rim;
                    const Sense = SpinSpeed < 0.0 ? -1.0 : 1.0;
                    const Detach = 0.02 + 0.45 * this.Random() * this.Random();
                    const Cosine = Math.cos(Detach), Sine = Math.sin(Detach);
                    Origin = [0, 1, 2].map((Axis) => Scrape.Point[Axis] + Radius * ((1.0 - Cosine) * Normal[Axis] - Sense * Sine * Forward[Axis]));
                    const Metal = [0, 1, 2].map((Axis) => Slide[Axis] + SpinSpeed * (1.0 - Cosine) * Forward[Axis] + Math.abs(SpinSpeed) * Sine * Normal[Axis]);
                    const Speed = Math.hypot(Metal[0], Metal[1], Metal[2]);
                    const Share = 0.75 + 0.25 * this.Random();
                    const Scatter = 0.12 * Speed + 0.4;
                    const Bounce = 0.3 + 1.2 * this.Random();
                    Velocity = [0, 1, 2].map((Axis) => Metal[Axis] * Share + Spread[Axis] * Scatter + Normal[Axis] * Bounce);
                }
                else
                {
                    // 📝 Along the slide at 30–75 % of its speed, fanned ±35° and kicked 1–4 m/s off the surface.
                    const Share = 0.3 + 0.45 * this.Random();
                    const Fan = 0.6 * Math.max(2.0, SlideSpeed * 0.35);
                    const Kick = 1.0 + 3.0 * this.Random();
                    const Jitter = 0.04;
                    Origin = [Scrape.Point[0] + (this.Random() - 0.5) * Jitter, Scrape.Point[1] + (this.Random() - 0.5) * Jitter,
                              Scrape.Point[2] + 0.01 + this.Random() * 0.02];
                    Velocity = [0, 1, 2].map((Axis) => Slide[Axis] * Share + Spread[Axis] * Fan + Normal[Axis] * Kick);
                }
                // 📝 Born part-way through the frame, with a tail back along its path so even a new spark is a streak.
                const Born = this.Random() * Seconds;
                const Position = [0, 1, 2].map((Axis) => Origin[Axis] + Velocity[Axis] * Born);
                this.Particles.push({
                    Position,
                    Prior: Origin,
                    Velocity,
                    Kelvin: 1500.0 + 600.0 * this.Random(),
                    Age: 0.0,
                    Fresh: true,
                    Size: (0.6 + 0.8 * this.Random()) * this.Settings.Size
                });
                this.Metrics.Emitted++;
            }
        }
    }

    // 📝 Ballistic with linear drag, radiative cooling toward ambient, skips off the course.
    Advance(Seconds)
    {
        if (!(Seconds > 0.0))
            return;
        const Survivors = [];
        for (const Spark of this.Particles)
        {
            // 📝 A spark born this frame already sits where it flew to since; the rest step from where they were.
            if (Spark.Fresh)
            {
                Spark.Fresh = false;
                Survivors.push(Spark);
                continue;
            }
            Spark.Age += Seconds;
            Spark.Prior = Spark.Position.slice();
            const Damping = Math.exp(-AirDrag * Seconds);
            Spark.Velocity = [Spark.Velocity[0] * Damping, Spark.Velocity[1] * Damping, Spark.Velocity[2] * Damping - Gravity * Seconds];
            Spark.Position = [Spark.Position[0] + Spark.Velocity[0] * Seconds, Spark.Position[1] + Spark.Velocity[1] * Seconds,
                              Spark.Position[2] + Spark.Velocity[2] * Seconds];
            const Ground = ResolveCourseHeight(Spark.Position[0], Spark.Position[1]);
            if (Spark.Position[2] < Ground + 0.005)
            {
                Spark.Position[2] = Ground + 0.005;
                if (Spark.Velocity[2] < 0.0)
                    Spark.Velocity = [Spark.Velocity[0] * 0.55, Spark.Velocity[1] * 0.55, -Spark.Velocity[2] * 0.32];
            }
            Spark.Kelvin = 293.0 + (Spark.Kelvin - 293.0) * Math.exp(-Seconds / CoolingSeconds);
            if (Spark.Kelvin > VisibleKelvin && Spark.Age < 2.5)
                Survivors.push(Spark);
        }
        this.Particles = Survivors;
        this.Live = Survivors.length;
        this.Metrics.Live = this.Live;
        if (this.Live === 0)
            return;
        // 📝 Instances: the streak from where the spark was a frame ago (the raster projects it with last frame's camera,
        //    so the streak is the motion the camera saw) to where it is.
        const Brightness = this.Settings.Brightness;
        for (let Index = 0; Index < this.Live; ++Index)
        {
            const Spark = this.Particles[Index];
            const Offset = Index * InstanceFloats;
            const Tail = Spark.Prior || Spark.Position;
            const Radiance = ResolveSparkRadiance(Spark.Kelvin).map((Channel) => Channel * Brightness);
            this.Instances.set([...Spark.Position, SparkWidth * Spark.Size, ...Tail, 0.0, ...Radiance, 0.0], Offset);
        }
        this.Exchange.Device.queue.writeBuffer(this.InstanceRecords, 0, this.Instances, 0, this.Live * InstanceFloats);
    }

    // 📝 Additively over the post radiance, depth-tested against the opaque surface (read only).
    Record(Encoder, TargetView, DepthView)
    {
        if (!this.Program || this.Live === 0)
            return;
        const Pass = Encoder.beginRenderPass({
            label: "Sparks",
            colorAttachments: [{ view: TargetView, loadOp: "load", storeOp: "store" }],
            depthStencilAttachment: { view: DepthView, depthReadOnly: true }
        });
        Pass.setPipeline(this.Program);
        Pass.setBindGroup(0, this.Group);
        Pass.setVertexBuffer(0, this.InstanceRecords);
        Pass.draw(6, this.Live, 0, 0);
        Pass.end();
    }

    Clear()
    {
        this.Particles = [];
        this.Live = 0;
    }

    QueryReadout()
    {
        return [`sparks live: ${this.Metrics.Live} · emitted ${this.Metrics.Emitted} · scrapes this frame ${this.Metrics.Scrapes}`];
    }
}

// 📐 Blackbody colour (Helland's fit, linear) × Stefan–Boltzmann brightness referred to 1500 K, in the scene's radiance
//    units (sunlit white ≈ 5): a fresh spark is a few hundred times brighter than the road it flies over.
export function ResolveSparkRadiance(Kelvin)
{
    const Hecto = Math.max(1000.0, Math.min(6600.0, Kelvin)) / 100.0;
    const Green = Math.min(1.0, Math.max(0.0, (99.4708025861 * Math.log(Hecto) - 161.1195681661) / 255.0));
    const Blue = Hecto <= 19.0 ? 0.0 : Math.min(1.0, Math.max(0.0, (138.5177312231 * Math.log(Math.max(Hecto - 10.0, 1.0)) - 305.0447927307) / 255.0));
    const Ratio = Kelvin / 1500.0;
    const Brightness = 120.0 * Ratio * Ratio * Ratio * Ratio;
    return [1.0, Math.pow(Green, 2.2), Math.pow(Blue, 2.2)].map((Channel) => Channel * Brightness);
}
