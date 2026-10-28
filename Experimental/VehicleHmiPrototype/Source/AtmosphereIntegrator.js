//============================================================================================================================================
//                                                         ATMOSPHEREINTEGRATOR.JS
//============================================================================================================================================
// 🧩 Light direction from the time of day, sun transmittance at the observer, moonlight below the horizon, sky-view refresh.

import { NormalizeVector, ClampScalar } from "./TransformProjection.js";

const SkyViewExtent = [192, 108];   // [texel] - azimuth × elevation

// 📝 Same constants as AtmosphereScattering.wgsl, so the light the shading uses is the light the sky was integrated for.
const GroundRadius     = 6360000.0;
const AtmosphereRadius = 6460000.0;
const RayleighScatter  = [5.802e-6, 13.558e-6, 33.1e-6];
const MieExtinction    = 4.40e-6;
const OzoneAbsorption  = [0.650e-6, 1.881e-6, 0.085e-6];
const ObserverAltitude = 100.0;

export class AtmosphereIntegrator
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.LastKey  = "";
        this.Light    = { Direction: [0, 0, 1], Illuminance: [1, 1, 1], SkyScale: 1.0, NightSky: [0, 0, 0], SunVisible: 1.0 };
    }

    async ConstructResources()
    {
        this.SkyTexture = this.Exchange.ConstructTexture({ label: "Sky view", size: SkyViewExtent, format: "rgba16float",
                                                           usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
        this.SkyView = this.SkyTexture.createView();
        this.SkySampler = this.Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear",
                                                           addressModeU: "repeat", addressModeV: "clamp-to-edge" });
        const Texture = (Label, Size) => this.Exchange.ConstructTexture({label:Label,size:Size,format:'rgba16float',usage:GPUTextureUsage.STORAGE_BINDING|GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_SRC});
        this.TransmittanceTexture=Texture('Atmosphere transmittance 256×64',[256,64]);
        this.MultipleTexture=Texture('Atmosphere multiple scattering 32×32',[32,32]);
        this.SkyTransmissionTexture=Texture('Sky-view transmission',SkyViewExtent);
        this.LutSampler=this.Exchange.ConstructSampler({magFilter:'linear',minFilter:'linear',addressModeU:'clamp-to-edge',addressModeV:'clamp-to-edge'});
        this.UniformRecords = this.Exchange.ConstructRecords(96, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, 'Atmosphere record');
        const Compute=GPUShaderStage.COMPUTE;
        this.TransmittanceLayout=this.Exchange.ConstructDescriptorLayout(['Uniform','StorageTexture2D:rgba16float'],Compute,'Transmittance LUT');
        this.MultipleLayout=this.Exchange.ConstructDescriptorLayout(['Uniform','StorageTexture2D:rgba16float','Texture2D','Sampler'],Compute,'Multiple scattering LUT');
        this.Layout=this.Exchange.ConstructDescriptorLayout(['Uniform','StorageTexture2D:rgba16float','Texture2D','Texture2D','Sampler','StorageTexture2D:rgba16float'],Compute,'Sky view LUT');
        const Trans=this.TransmittanceTexture.createView(),Multi=this.MultipleTexture.createView();
        this.TransmittanceGroup=this.Exchange.ConstructDescriptorGroup(this.TransmittanceLayout,[this.UniformRecords,Trans]);
        this.MultipleGroup=this.Exchange.ConstructDescriptorGroup(this.MultipleLayout,[this.UniformRecords,Multi,Trans,this.LutSampler]);
        this.Group=this.Exchange.ConstructDescriptorGroup(this.Layout,[this.UniformRecords,this.SkyView,Trans,Multi,this.LutSampler,this.SkyTransmissionTexture.createView()]);
        this.TransmittanceProgram=await this.Shaders.ConstructComputeProgram('AtmosphereTransmittance.wgsl','IntegrateTransmittance',[this.TransmittanceLayout]);
        this.MultipleProgram=await this.Shaders.ConstructComputeProgram('AtmosphereMultiple.wgsl','IntegrateMultiple',[this.MultipleLayout]);
        this.Program=await this.Shaders.ConstructComputeProgram('AtmosphereScattering.wgsl','IntegrateSkyView',[this.Layout]);
        this.Revision=0;this.MediumRevision=0;

    }

    // 📝 Elevation and azimuth in degrees. Below about −4° the key light hands over to a cool, dim moon opposite the
    //    sun, and a faint night-sky floor keeps escaped rays from returning pure black.
    ResolveLight(ElevationDegrees, AzimuthDegrees, SolarIlluminance, Settings = {})
    {
        this.Settings = Settings;
        const Elevation = ElevationDegrees * Math.PI / 180.0;
        const Azimuth   = AzimuthDegrees * Math.PI / 180.0;
        const Sun = NormalizeVector([Math.cos(Elevation) * Math.cos(Azimuth), Math.cos(Elevation) * Math.sin(Azimuth), Math.sin(Elevation)]);
        const Transmittance = Settings.AirMass===false?[1,1,1]:ResolveSunTransmittance(Sun, Settings);
        const Temperature = Math.max(20, Math.min(100, (Settings.SunTemperature ?? 5778) / 100));
        const TemperatureRGB = Settings.SunColourSource==='RGB'?(Settings.SunTint??[1,1,1]):[Temperature <= 66 ? 255 : 329.698727 * Math.pow(Temperature-60,-0.13320476),
            Temperature <= 66 ? 99.470802 * Math.log(Temperature)-161.119568 : 288.12217 * Math.pow(Temperature-60,-0.07551485),
            Temperature >= 66 ? 255 : Temperature <= 19 ? 0 : 138.517731 * Math.log(Temperature-10)-305.044793]
            .map(C => Math.pow(Math.max(0, Math.min(255,C))/255,2.2));
        this.SolarTint = TemperatureRGB;
        const SunFade  = (Settings.SunEnabled===false?0:1)*ClampScalar((ElevationDegrees + 1.0) / 3.0, 0.0, 1.0);
        const MoonFade = Settings.SunEnabled===false?1:ClampScalar((-ElevationDegrees - 4.0) / 6.0, 0.0, 1.0);
        const MoonElevation = (Settings.MoonElevation ?? 38) * Math.PI / 180.0;
        const MoonAzimuth = (Settings.MoonAzimuth ?? 0) * Math.PI / 180.0;
        const Moon = NormalizeVector([Math.cos(MoonElevation) * Math.cos(MoonAzimuth),
                                      Math.cos(MoonElevation) * Math.sin(MoonAzimuth), Math.sin(MoonElevation)]);
        const MoonKey=JSON.stringify([Moon,Settings.Rayleigh,Settings.Mie,Settings.Ozone,Settings.RayleighHeight,Settings.MieHeight]);
        if(this.MoonTransmissionKey!==MoonKey){this.MoonTransmissionKey=MoonKey;this.MoonTransmission=ResolveSunTransmittance(Moon,Settings);}
        const MoonTransmission = this.MoonTransmission;
        const MoonHorizon = ClampScalar(((Settings.MoonElevation ?? 38) + 0.5) / 2, 0, 1);
        // Independent moonlight power; zero solar power must not switch off the Moon.
        // Artistic disc diameter is deliberately NOT squared into illumination.
        const MoonIlluminance = [0.08, 0.10, 0.14].map(Component=>Component*(Settings.MoonLightEnabled===false?0:(Settings.MoonLightIntensity??1)));
        if (MoonFade > SunFade)
        {
            this.Light.Source = 'Moon';
            this.Light.Direction = Moon;
            this.Light.Illuminance = MoonIlluminance.map((Component, I) => Component * MoonTransmission[I] * MoonHorizon * MoonFade * (Settings.MoonEnabled === false ? 0 : (Settings.MoonBrightness ?? 1)) * (0.5 - 0.5 * Math.cos((Settings.MoonPhase ?? 14.765) / 29.53 * 2 * Math.PI)));
        }
        else
        {
            this.Light.Source = 'Sun';
            this.Light.Direction = Sun;
            this.Light.Illuminance = Transmittance.map((Component, I) => Component * TemperatureRGB[I] * SolarIlluminance * SunFade);
        }
        this.Light.SolarIlluminance = Transmittance.map((V, I) => V * TemperatureRGB[I] * SolarIlluminance * SunFade);
        this.Light.SunDirection = Sun;
        this.Light.SunVisible = SunFade;
        this.Light.NightSky = [0.0012, 0.0016, 0.0030].map((Component) => Component * (1.0 - SunFade) * SolarIlluminance / 10.0);
        return this.Light;
    }

    RecordSkyView(Encoder, SolarIlluminance)
    {
        const Sun = this.Light.SunDirection;
        const S = this.Settings;
        const Medium = [S.Rayleigh ?? 1, S.Mie ?? 1, S.Ozone ?? 1, S.MieG ?? 0.8, S.RayleighHeight ?? 8000, S.MieHeight ?? 1200];
        const MediumKey = JSON.stringify([Medium, S.GroundAlbedo ?? [0.3,0.3,0.3]]);
        const Key = JSON.stringify([MediumKey, this.SolarTint, S.MultipleScattering ?? true,S.ForceAtmosphereBake??0,S.SkyTint,S.HorizonGlow,S.AtmosphereEnabled,S.TierSamples]) + `${Sun.map((Component) => Component.toFixed(4)).join(",")}|${SolarIlluminance.toFixed(3)}`;
        if (Key === this.LastKey && S.UseBakedAtmosphere!==false)
            return false;
        this.LastKey = Key;
        const Record = new Float32Array(24);
        Record.set(Sun, 0);
        Record[3] = SolarIlluminance;
        Record[4] = ObserverAltitude;
        Record[5] = S.HorizonGlow??1;
        Record.set(Medium.slice(0, 4), 8);
        Record.set(Medium.slice(4), 12);
        Record.set(this.SolarTint.map((V,I)=>V*(S.SkyTint?.[I]??1)), 16);
        Record[14] = S.MultipleScattering === false ? 0 : 1;
        Record[15] = S.AtmosphereEnabled===false?0:1;
        Record.set(S.GroundAlbedo ?? [0.3,0.3,0.3],20);
        Record[23] = S.TierSamples??40;
        this.Exchange.Device.queue.writeBuffer(this.UniformRecords, 0, Record);
        if (MediumKey !== this.LastMediumKey) {
            for(const [Label,Program,Group,Width,Height] of [
                ['Atmosphere transmittance',this.TransmittanceProgram,this.TransmittanceGroup,256,64],
                ['Atmosphere multiple scattering',this.MultipleProgram,this.MultipleGroup,32,32]]) {
                const LutPass=Encoder.beginComputePass({label:Label});LutPass.setPipeline(Program);LutPass.setBindGroup(0,Group);
                LutPass.dispatchWorkgroups(Math.ceil(Width/8),Math.ceil(Height/8));LutPass.end();
            }
            this.LastMediumKey=MediumKey;this.MediumRevision++;
        }
        this.Revision++;
        const Pass = Encoder.beginComputePass({ label: "Sky view" });
        Pass.setPipeline(this.Program);
        Pass.setBindGroup(0, this.Group);
        Pass.dispatchWorkgroups(Math.ceil(SkyViewExtent[0] / 8), Math.ceil(SkyViewExtent[1] / 8));
        Pass.end();
        return true;
    }
}

// 📐 Transmittance from the observer toward the sun, 64-step quadrature of the extinction along the ray.
export function ResolveSunTransmittance(Direction, S = {})
{
    const Origin = [0.0, 0.0, GroundRadius + ObserverAltitude];
    const Dot = Origin[2] * Direction[2];
    const Square = Origin[2] * Origin[2];
    const GroundDiscriminant = Dot * Dot - (Square - GroundRadius * GroundRadius);
    if (GroundDiscriminant > 0.0 && (-Dot - Math.sqrt(GroundDiscriminant)) > 0.0)
        return [0.0, 0.0, 0.0];
    const Exit = -Dot + Math.sqrt(Dot * Dot - (Square - AtmosphereRadius * AtmosphereRadius));
    const Samples = 64;
    const Step = Exit / Samples;
    const Depth = [0.0, 0.0, 0.0];
    for (let Sample = 0; Sample < Samples; ++Sample)
    {
        const Travel = (Sample + 0.5) * Step;
        const Point = [Direction[0] * Travel, Direction[1] * Travel, Origin[2] + Direction[2] * Travel];
        const Altitude = Math.hypot(Point[0], Point[1], Point[2]) - GroundRadius;
        const Rayleigh = Math.exp(-Altitude / (S.RayleighHeight ?? 8000));
        const Mie = Math.exp(-Altitude / (S.MieHeight ?? 1200));
        const Ozone = Math.max(0.0, 1.0 - Math.abs(Altitude - 25000.0) / 15000.0);
        for (let Channel = 0; Channel < 3; ++Channel)
            Depth[Channel] += (RayleighScatter[Channel] * Rayleigh * (S.Rayleigh ?? 1) + MieExtinction * Mie * (S.Mie ?? 1) + OzoneAbsorption[Channel] * Ozone * (S.Ozone ?? 1)) * Step;
    }
    return Depth.map((Component) => Math.exp(-Component));
}
