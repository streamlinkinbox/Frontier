/*============================================================================================================================================
                                                         ATMOSPHEREEDITOR.JS
============================================================================================================================================*/
// 🧩 Interactive single-scattering atmosphere preview and authoring controls.

'use strict';

const AtmosphereCanvas = document.getElementById('AtmosphereCanvas');
const RenderNotice = document.getElementById('RenderNotice');
const Fields = ['SunElevation', 'Rayleigh', 'Mie', 'Ozone', 'Exposure', 'CloudCoverage'];
// 📝 Presets are visually guided approximations, not measured planetary atmospheres.
const PlanetProfiles = {
    Earth: {Radius: 6360, AirDepth: 100, MolecularHeight: 8, AerosolHeight: 1.2,
        BetaRayleigh: [.005802,.013558,.0331], BetaMie: [.0044,.0044,.0044], BetaOzone: [.000650,.001881,.000085],
        Sea: [.012,.032,.036], Land: [.047,.074,.056], CloudTint: [1,.99,.96], CloudHeight: 7,
        Defaults: {SunElevation: 25, Rayleigh: 1, Mie: 1, Ozone: 1, Exposure: 1, CloudCoverage: 55}, Clouds: true},
    Mars: {Radius: 3390, AirDepth: 110, MolecularHeight: 11.1, AerosolHeight: 10,
        BetaRayleigh: [.000035,.000081,.000199], BetaMie: [.0036,.0022,.0012], BetaOzone: [.000650,.001881,.000085],
        Sea: [.10,.031,.012], Land: [.17,.065,.026], CloudTint: [.98,.82,.70], CloudHeight: 18,
        Defaults: {SunElevation: 25, Rayleigh: 1, Mie: 1, Ozone: 0, Exposure: 1.1, CloudCoverage: 20}, Clouds: false},
    Venus: {Radius: 6052, AirDepth: 200, MolecularHeight: 15.9, AerosolHeight: 22,
        BetaRayleigh: [.011,.020,.036], BetaMie: [.012,.010,.007], BetaOzone: [.000650,.001881,.000085],
        Sea: [.12,.075,.027], Land: [.16,.11,.045], CloudTint: [1,.78,.45], CloudHeight: 60,
        Defaults: {SunElevation: 25, Rayleigh: 1, Mie: 1.5, Ozone: 0, Exposure: 1, CloudCoverage: 95}, Clouds: true},
};
const Parameters = {...PlanetProfiles.Earth.Defaults, Planet: 'Earth', Enabled: true, Clouds: true, Limb: true, View: 'orbit', Yaw: 0, Pitch: 0};
const OrbitAim = {Yaw: 0, Pitch: 0};
let OrbitTimestamp = 0;
const VertexSource = `#version 300 es
in vec2 Position;
void main() { gl_Position = vec4(Position, 0.0, 1.0); }
`;

// 📝 Distances are kilometres. The view and solar optical depths integrate exponential
// molecular/aerosol profiles and a triangular ozone layer through a spherical atmosphere.
const FragmentSource = `#version 300 es
precision highp float;
out vec4 Fragment;
uniform vec2 Resolution;
uniform float SunElevation;
uniform float Rayleigh;
uniform float Mie;
uniform float Ozone;
uniform float Exposure;
uniform float Enabled;
uniform float SurfaceView;
uniform vec2 Orbit;
uniform float ValidationMode;
uniform float GroundRadius;
uniform float AirRadius;
uniform float MolecularHeight;
uniform float AerosolHeight;
uniform vec3 BetaRayleigh;
uniform vec3 BetaMie;
uniform vec3 BetaOzone;
uniform vec3 Sea;
uniform vec3 Land;
uniform sampler2D CloudAtlas;
uniform float CloudHeight;
uniform float CloudCoverage;
uniform float CloudsEnabled;
uniform vec3 CloudTint;
uniform float LimbEnabled;
const float PI = 3.14159265359;

vec2 SphereIntersection(vec3 Origin, vec3 Direction, float Radius) {
    float B = dot(Origin, Direction);
    float D = B * B - dot(Origin, Origin) + Radius * Radius;
    if (D < 0.0) return vec2(1e20, -1e20);
    D = sqrt(D);
    return vec2(-B - D, -B + D);
}
vec3 Density(float Altitude) {
    float H = max(Altitude, 0.0);
    return vec3(exp(-H / MolecularHeight), exp(-H / AerosolHeight), max(0.0, 1.0 - abs(H - 25.0) / 15.0));
}
vec3 Extinction(vec3 OpticalDepth) {
    return BetaRayleigh * Rayleigh * OpticalDepth.x + BetaMie * Mie * OpticalDepth.y + BetaOzone * Ozone * OpticalDepth.z;
}
vec3 SunDepth(vec3 Point, vec3 Light, out bool Occluded) {
    vec2 Ground = SphereIntersection(Point, Light, GroundRadius);
    Occluded = Ground.x > 0.001 && Ground.x < 1e19;
    if (Occluded) return vec3(0.0);
    float Distance = max(0.0, SphereIntersection(Point, Light, AirRadius).y);
    vec3 Depth = vec3(0.0);
    for (int I = 0; I < 8; ++I) {
        float A = float(I) / 8.0, B = float(I + 1) / 8.0;
        float T0 = Distance * A * A, T1 = Distance * B * B;
        vec3 P = Point + Light * ((T0 + T1) * 0.5);
        Depth += Density(length(P) - GroundRadius) * (T1 - T0);
    }
    return Depth;
}
float Hash(vec3 P) { return fract(sin(dot(P, vec3(127.1,311.7,74.7))) * 43758.5453); }
float Noise(vec3 P) {
    vec3 I = floor(P), F = fract(P); F = F * F * (3.0 - 2.0 * F);
    return mix(mix(mix(Hash(I), Hash(I+vec3(1,0,0)), F.x), mix(Hash(I+vec3(0,1,0)), Hash(I+vec3(1,1,0)), F.x), F.y),
               mix(mix(Hash(I+vec3(0,0,1)), Hash(I+vec3(1,0,1)), F.x), mix(Hash(I+vec3(0,1,1)), Hash(I+vec3(1,1,1)), F.x), F.y), F.z);
}
void main() {
    if (ValidationMode > 0.5) {
        bool Occluded;
        vec3 Direction = ValidationMode < 1.5 ? vec3(0.0,0.0,1.0) : vec3(0.0,0.0,-1.0);
        vec3 Depth = SunDepth(vec3(0.0,0.0,GroundRadius+0.1), Direction, Occluded);
        Fragment = ValidationMode < 1.5 ? vec4(exp(-Extinction(Depth)),1.0) : vec4(Occluded ? 1.0 : 0.0,0.0,0.0,1.0);
        return;
    }
    vec2 P = (gl_FragCoord.xy * 2.0 - Resolution) / Resolution.y;
    vec3 Origin, Direction;
    if (SurfaceView > 0.5) {
        Origin = vec3(0.0, 0.0, GroundRadius + 0.1);
        Direction = normalize(vec3(1.0, P.x * 0.72, P.y * 0.72 + 0.10));
    } else {
        float Yaw = Orbit.x, Pitch = Orbit.y;
        vec3 Normal = vec3(sin(Yaw) * cos(Pitch), sin(Pitch), cos(Yaw) * cos(Pitch));
        Origin = Normal * GroundRadius * 3.1;
        vec3 Forward = -Normal;
        vec3 Right = normalize(cross(Forward, vec3(0.0, 1.0, 0.0)));
        vec3 Up = normalize(cross(Right, Forward));
        float Aspect = Resolution.x / Resolution.y;
        float Fit = min(1.0, Aspect / 1.2);
        P /= Fit;
        P.x -= Aspect < 0.9 ? 0.5 : 0.39;
        Direction = normalize(Forward * 2.0 + Right * P.x + Up * P.y);
    }
    float Elevation = radians(SunElevation);
    float Azimuth = radians(22.0);
    vec3 Light = normalize(vec3(cos(Elevation)*cos(Azimuth), cos(Elevation)*sin(Azimuth), sin(Elevation)));
    vec2 Air = SphereIntersection(Origin, Direction, AirRadius);
    vec2 Ground = SphereIntersection(Origin, Direction, GroundRadius);
    bool HitsGround = Ground.x > 0.0 && Ground.x < 1e19;
    float Start = max(0.0, Air.x);
    float End = Air.y;
    if (HitsGround) End = min(End, Ground.x);
    vec3 Radiance = vec3(0.0);
    vec3 ViewDepth = vec3(0.0);
    if (End > Start && Enabled > 0.5) {
        float Nearest = clamp(-dot(Origin, Direction), Start, End);
        float Mu = dot(Direction, Light);
        float PhaseR = 3.0 / (16.0 * PI) * (1.0 + Mu * Mu);
        float G = 0.8;
        float PhaseM = 3.0 / (8.0 * PI) * ((1.0-G*G)*(1.0+Mu*Mu)) /
                       ((2.0+G*G)*pow(max(0.001,1.0+G*G-2.0*G*Mu),1.5));
        for (int I = 0; I < 24; ++I) {
            float A = float(I) / 24.0, B = float(I + 1) / 24.0;
            float T0 = A < 0.5 ? Nearest - (Nearest-Start)*pow(1.0-2.0*A, 2.0) : Nearest + (End-Nearest)*pow(2.0*A-1.0, 2.0);
            float T1 = B < 0.5 ? Nearest - (Nearest-Start)*pow(1.0-2.0*B, 2.0) : Nearest + (End-Nearest)*pow(2.0*B-1.0, 2.0);
            float Step = T1 - T0;
            vec3 Point = Origin + Direction * ((T0 + T1) * 0.5);
            vec3 LocalDensity = Density(length(Point) - GroundRadius);
            vec3 MidpointDepth = ViewDepth + LocalDensity * Step * 0.5;
            ViewDepth += LocalDensity * Step;
            bool Occluded;
            vec3 SolarDepth = SunDepth(Point, Light, Occluded);
            if (!Occluded) {
                vec3 Transmission = exp(-Extinction(MidpointDepth + SolarDepth));
                Radiance += Transmission * (BetaRayleigh * Rayleigh * LocalDensity.x * PhaseR +
                            BetaMie * Mie * LocalDensity.y * PhaseM) * Step * 20.0;
            }
        }
    }
    if (SurfaceView < 0.5 && !HitsGround && Air.y > Air.x && LimbEnabled > 0.5) Radiance *= 3.0;
    if (HitsGround) {
        vec3 Point = Origin + Direction * Ground.x;
        vec3 Normal = normalize(Point);
        float NdotL = max(0.0, dot(Normal, Light));
        bool Occluded;
        vec3 SolarDepth = SunDepth(Point + Normal * 0.01, Light, Occluded);
        vec3 SunTransmission = Enabled > 0.5 ? exp(-Extinction(SolarDepth)) : vec3(1.0);
        float LandFraction = smoothstep(0.48, 0.66, Noise(Normal*8.0) * 0.7 + Noise(Normal*19.0) * 0.3);
        vec3 Albedo = mix(Sea, Land, LandFraction);
        vec3 Surface = Albedo * (0.014 + NdotL * SunTransmission * 2.0);
        Radiance += Surface * (Enabled > 0.5 ? exp(-Extinction(ViewDepth)) : vec3(1.0));
    } else {
        float Stars = step(0.9987, Hash(floor(Direction * 1100.0))) * 0.035;
        Radiance += vec3(0.0012, 0.0024, 0.0021) + vec3(Stars);
        float Cosine = dot(Direction, Light);
        float SolarDisc = smoothstep(cos(radians(0.34)), cos(radians(0.24)), Cosine);
        vec3 Transmission = Enabled > 0.5 ? exp(-Extinction(ViewDepth)) : vec3(1.0);
        Radiance += SolarDisc * 6.0 * Transmission;
    }
    // 📝 A precomputed low-cost cloud coverage atlas on a spherical layer, not volumetric weather.
    if (CloudsEnabled > 0.5 && CloudCoverage > 0.001) {
        vec2 CloudHit = SphereIntersection(Origin, Direction, GroundRadius + CloudHeight);
        float CloudT = CloudHit.x > 0.001 ? CloudHit.x : CloudHit.y;
        if (CloudT > 0.001 && CloudT < 1e19 && (!HitsGround || CloudT < Ground.x)) {
            vec3 Point = Origin + Direction * CloudT;
            vec3 Normal = normalize(Point);
            vec2 UV = vec2(atan(Normal.z, Normal.x)/(2.0*PI)+0.5, acos(clamp(Normal.y,-1.0,1.0))/PI);
            // 📝 Surface view uses a local tiled projection: the orbital atlas cannot resolve
            // individual clouds at ground scale. This is a visual approximation.
            if (SurfaceView > 0.5) UV = fract(Point.xy / 140.0 + 0.5);
            float Pattern = texture(CloudAtlas, UV).r;
            float Threshold = 0.64 - CloudCoverage * 0.27;
            float Opacity = smoothstep(Threshold - 0.06, Threshold + 0.18, Pattern) * min(1.0, CloudCoverage * 3.0) * 0.78;
            Opacity = mix(Opacity, 0.94, smoothstep(0.80, 1.0, CloudCoverage));
            float Sunlight = max(dot(Normal, Light), 0.0);
            bool Shadowed;
            vec3 SolarDepth = SunDepth(Point, Light, Shadowed);
            vec3 SolarTransmission = Enabled > 0.5 ? exp(-Extinction(SolarDepth)) : vec3(1.0);
            vec3 CloudRadiance = CloudTint * (0.008 + (Shadowed ? 0.0 : Sunlight) * SolarTransmission * 0.85);
            vec3 ForegroundDepth = vec3(0.0);
            if (Enabled > 0.5 && Air.y > Air.x) {
                float CloudStart = max(0.0, Air.x);
                float CloudLength = max(0.0, CloudT - CloudStart);
                for (int I = 0; I < 6; ++I) {
                    float T = CloudStart + CloudLength * (float(I) + 0.5) / 6.0;
                    ForegroundDepth += Density(length(Origin + Direction*T) - GroundRadius) * CloudLength / 6.0;
                }
            }
            vec3 ForegroundTransmission = Enabled > 0.5 ? exp(-Extinction(ForegroundDepth)) : vec3(1.0);
            vec3 CloudSeen = CloudRadiance * ForegroundTransmission + Radiance * (1.0-ForegroundTransmission) * 0.6;
            Radiance = mix(Radiance, CloudSeen, Opacity);
        }
    }
    vec3 Colour = pow(vec3(1.0) - exp(-max(Radiance,vec3(0.0)) * Exposure), vec3(1.0/2.2));
    Fragment = vec4(Colour, 1.0);
}
`;

let Gl = null;
let Program = null;
let Locations = {};
let Pending = false;

function AtlasHash(X, Y, Z)
{
    let Bits = Math.imul(X, 374761393) + Math.imul(Y, 668265263) + Math.imul(Z, 2147483647);
    Bits = Math.imul(Bits ^ (Bits >>> 13), 1274126177);
    return ((Bits ^ (Bits >>> 16)) >>> 0) / 4294967295;
}

function AtlasNoise(X, Y, Z)
{
    const IX = Math.floor(X), IY = Math.floor(Y), IZ = Math.floor(Z);
    let FX = X - IX, FY = Y - IY, FZ = Z - IZ;
    FX = FX*FX*(3-2*FX); FY = FY*FY*(3-2*FY); FZ = FZ*FZ*(3-2*FZ);
    const Blend = (A, B, T) => A + (B - A) * T;
    const A = Blend(Blend(AtlasHash(IX,IY,IZ),AtlasHash(IX+1,IY,IZ),FX), Blend(AtlasHash(IX,IY+1,IZ),AtlasHash(IX+1,IY+1,IZ),FX), FY);
    const B = Blend(Blend(AtlasHash(IX,IY,IZ+1),AtlasHash(IX+1,IY,IZ+1),FX), Blend(AtlasHash(IX,IY+1,IZ+1),AtlasHash(IX+1,IY+1,IZ+1),FX), FY);
    return Blend(A, B, FZ);
}

function BuildCloudAtlas()
{
    const Width = 512, Height = 256, Pixels = new Uint8Array(Width * Height);
    for (let Y = 0; Y < Height; ++Y) {
        const Latitude = Math.PI * (Y + .5) / Height;
        const SinLatitude = Math.sin(Latitude), CosLatitude = Math.cos(Latitude);
        for (let X = 0; X < Width; ++X) {
            const Longitude = (X + .5) / Width * Math.PI * 2 - Math.PI;
            const NX = Math.cos(Longitude) * SinLatitude, NY = CosLatitude, NZ = Math.sin(Longitude) * SinLatitude;
            const Broad = AtlasNoise(NX*11+31, NY*22+17, NZ*11+9);
            const Medium = AtlasNoise(NX*25+7, NY*48+29, NZ*25+13);
            const Detail = AtlasNoise(NX*55+19, NY*100+11, NZ*55+37);
            const Fine = AtlasNoise(NX*110+5, NY*175+23, NZ*110+3);
            Pixels[Y * Width + X] = Math.round(255 * Math.max(0, Math.min(1, Broad*.48 + Medium*.30 + Detail*.16 + Fine*.06)));
        }
    }
    const Texture = Gl.createTexture();
    Gl.activeTexture(Gl.TEXTURE0);
    Gl.bindTexture(Gl.TEXTURE_2D, Texture);
    Gl.pixelStorei(Gl.UNPACK_ALIGNMENT, 1);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR_MIPMAP_LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.REPEAT);
    Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
    Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.R8, Width, Height, 0, Gl.RED, Gl.UNSIGNED_BYTE, Pixels);
    Gl.generateMipmap(Gl.TEXTURE_2D);
}

function UpdateDensityPlot(Profile)
{
    const Points = [];
    for (let I = 0; I <= 40; ++I) {
        const Fraction = I / 40;
        const Density = Math.exp(-4 * (1 - Fraction));
        Points.push([10 + Density * 100, 12 + Fraction * 48]);
    }
    const Path = Points.map((Point, Index) => (Index ? 'L' : 'M') + Point[0].toFixed(2) + ' ' + Point[1].toFixed(2)).join(' ');
    document.getElementById('DensityCurve').setAttribute('d', Path);
    document.getElementById('DensityArea').setAttribute('d', Path + ' L10 60 L10 12 Z');
    document.getElementById('DensityMarker').setAttribute('cx', (10 + Math.exp(-1) * 100).toFixed(2));
    document.getElementById('DensityMarker').setAttribute('cy', '48');
    document.querySelector('.density-instrument').setAttribute('aria-label', 'Molecular density profile, scale height ' + Profile.MolecularHeight + ' kilometres');
}

function CompileShader(Type, Source)
{
    const Shader = Gl.createShader(Type);
    Gl.shaderSource(Shader, Source);
    Gl.compileShader(Shader);
    if (!Gl.getShaderParameter(Shader, Gl.COMPILE_STATUS)) throw new Error(Gl.getShaderInfoLog(Shader));
    return Shader;
}

function InitialiseAtmosphere()
{
    Gl = AtmosphereCanvas.getContext('webgl2', {alpha: false, antialias: false, preserveDrawingBuffer: true});
    if (!Gl) throw new Error('WebGL 2 is not available in this browser.');
    Program = Gl.createProgram();
    Gl.attachShader(Program, CompileShader(Gl.VERTEX_SHADER, VertexSource));
    Gl.attachShader(Program, CompileShader(Gl.FRAGMENT_SHADER, FragmentSource));
    Gl.linkProgram(Program);
    if (!Gl.getProgramParameter(Program, Gl.LINK_STATUS)) throw new Error(Gl.getProgramInfoLog(Program));
    Gl.useProgram(Program);
    const Buffer = Gl.createBuffer();
    Gl.bindBuffer(Gl.ARRAY_BUFFER, Buffer);
    Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array([-1,-1, 3,-1, -1,3]), Gl.STATIC_DRAW);
    const Position = Gl.getAttribLocation(Program, 'Position');
    Gl.enableVertexAttribArray(Position);
    Gl.vertexAttribPointer(Position, 2, Gl.FLOAT, false, 0, 0);
    for (const Name of ['Resolution', 'SunElevation', 'Rayleigh', 'Mie', 'Ozone', 'Exposure', 'Enabled', 'SurfaceView', 'Orbit', 'ValidationMode', 'GroundRadius', 'AirRadius', 'MolecularHeight', 'AerosolHeight', 'BetaRayleigh', 'BetaMie', 'BetaOzone', 'Sea', 'Land', 'CloudAtlas', 'CloudHeight', 'CloudCoverage', 'CloudsEnabled', 'CloudTint', 'LimbEnabled']) {
        Locations[Name] = Gl.getUniformLocation(Program, Name);
    }
    BuildCloudAtlas();
    RenderAtmosphere();
}

function RenderAtmosphere(Timestamp)
{
    Pending = false;
    if (!Gl || !Program || Gl.isContextLost()) return;
    let Moving = false;
    if (Number.isFinite(Timestamp)) {
        const Delta = OrbitTimestamp ? Math.min(0.1, Math.max(0, (Timestamp - OrbitTimestamp) / 1000)) : 1 / 60;
        const Blend = 1 - Math.exp(-Delta / 0.20);
        for (const Axis of ['Yaw', 'Pitch']) {
            Parameters[Axis] += (OrbitAim[Axis] - Parameters[Axis]) * Blend;
            if (Math.abs(OrbitAim[Axis] - Parameters[Axis]) < 0.0001) Parameters[Axis] = OrbitAim[Axis];
            else Moving = true;
        }
        OrbitTimestamp = Moving ? Timestamp : 0;
    }
    // 📝 Cap the drawing buffer for interactive software rendering; the physical formulation is unchanged.
    const Scale = Math.min(window.devicePixelRatio || 1, 1);
    const Width = Math.max(1, Math.round(AtmosphereCanvas.clientWidth * Scale));
    const Height = Math.max(1, Math.round(AtmosphereCanvas.clientHeight * Scale));
    if (AtmosphereCanvas.width !== Width || AtmosphereCanvas.height !== Height) {
        AtmosphereCanvas.width = Width;
        AtmosphereCanvas.height = Height;
    }
    Gl.viewport(0, 0, Width, Height);
    Gl.useProgram(Program);
    Gl.uniform2f(Locations.Resolution, Width, Height);
    for (const Name of ['SunElevation', 'Rayleigh', 'Mie', 'Ozone', 'Exposure']) Gl.uniform1f(Locations[Name], Parameters[Name]);
    const Profile = PlanetProfiles[Parameters.Planet];
    Gl.uniform1f(Locations.GroundRadius, Profile.Radius);
    Gl.uniform1f(Locations.AirRadius, Profile.Radius + Profile.AirDepth);
    Gl.uniform1f(Locations.MolecularHeight, Profile.MolecularHeight);
    Gl.uniform1f(Locations.AerosolHeight, Profile.AerosolHeight);
    for (const Name of ['BetaRayleigh', 'BetaMie', 'BetaOzone', 'Sea', 'Land', 'CloudTint']) Gl.uniform3fv(Locations[Name], Profile[Name]);
    Gl.uniform1f(Locations.CloudHeight, Profile.CloudHeight);
    Gl.uniform1f(Locations.CloudCoverage, Parameters.CloudCoverage / 100);
    Gl.uniform1f(Locations.CloudsEnabled, Parameters.Clouds ? 1 : 0);
    Gl.uniform1f(Locations.LimbEnabled, Parameters.Limb ? 1 : 0);
    Gl.uniform1i(Locations.CloudAtlas, 0);
    Gl.uniform1f(Locations.Enabled, Parameters.Enabled ? 1 : 0);
    Gl.uniform1f(Locations.SurfaceView, Parameters.View === 'surface' ? 1 : 0);
    Gl.uniform2f(Locations.Orbit, Parameters.Yaw, Parameters.Pitch);
    Gl.uniform1f(Locations.ValidationMode, 0);
    Gl.drawArrays(Gl.TRIANGLES, 0, 3);
    AtmosphereCanvas.dataset.rendered = 'true';
    if (Moving) RequestRender();
}

function RequestRender()
{
    if (!Pending) { Pending = true; requestAnimationFrame(RenderAtmosphere); }
}

function UpdateField(Name)
{
    const Input = document.getElementById(Name);
    const Output = document.getElementById(Name + 'Readout');
    const Amount = Number(Input.value);
    Parameters[Name] = Amount;
    Output.innerHTML = (Name === 'SunElevation' || Name === 'CloudCoverage' ? Amount.toFixed(0) : Amount.toFixed(2)) + '<span>' + (Name === 'SunElevation' ? '°' : Name === 'CloudCoverage' ? '%' : '×') + '</span>';
    Input.style.setProperty('--fill', ((Amount - Number(Input.min)) / (Number(Input.max) - Number(Input.min)) * 100) + '%');
}

for (const Name of Fields) {
    const Input = document.getElementById(Name);
    Input.addEventListener('input', () => { UpdateField(Name); RequestRender(); });
    UpdateField(Name);
}
document.getElementById('AtmosphereEnabled').addEventListener('change', Event => {
    Parameters.Enabled = Event.currentTarget.checked;
    RequestRender();
});
function ApplyPlanet(Name)
{
    if (!PlanetProfiles[Name]) return;
    Parameters.Planet = Name;
    const Profile = PlanetProfiles[Name];
    for (const Field of Fields) { document.getElementById(Field).value = Profile.Defaults[Field]; UpdateField(Field); }
    Parameters.Enabled = true;
    Parameters.Clouds = Profile.Clouds;
    Parameters.Limb = true;
    Parameters.Yaw = Parameters.Pitch = OrbitAim.Yaw = OrbitAim.Pitch = 0;
    OrbitTimestamp = 0;
    document.getElementById('AtmosphereEnabled').checked = true;
    document.getElementById('CloudsEnabled').checked = Profile.Clouds;
    document.getElementById('LimbEnabled').checked = true;
    document.getElementById('LimbReadout').textContent = Parameters.View === 'surface' ? 'SINGLE-SCATTER SKY' : 'RIM ENHANCED ×3';
    document.getElementById('PlanetPreset').value = Name;
    document.getElementById('PlanetReadout').textContent = Name.toUpperCase();
    document.getElementById('HeightReadout').innerHTML = Profile.MolecularHeight.toFixed(1) + ' <small>km</small>';
    UpdateDensityPlot(Profile);
    RequestRender();
}
document.getElementById('PlanetPreset').addEventListener('change', Event => ApplyPlanet(Event.currentTarget.value));
document.getElementById('ResetAtmosphere').addEventListener('click', () => ApplyPlanet(Parameters.Planet));
document.getElementById('CloudsEnabled').addEventListener('change', Event => { Parameters.Clouds = Event.currentTarget.checked; RequestRender(); });
document.getElementById('LimbEnabled').addEventListener('change', Event => {
    Parameters.Limb = Event.currentTarget.checked;
    document.getElementById('LimbReadout').textContent = Parameters.View === 'surface' ? 'SINGLE-SCATTER SKY' : Parameters.Limb ? 'RIM ENHANCED ×3' : 'PHYSICAL RIM';
    RequestRender();
});
for (const Button of document.querySelectorAll('.view-button')) {
    Button.addEventListener('click', () => {
        Parameters.View = Button.dataset.view;
        for (const Choice of document.querySelectorAll('.view-button')) {
            const Selected = Choice === Button;
            Choice.classList.toggle('selected', Selected);
            Choice.setAttribute('aria-pressed', String(Selected));
        }
        document.querySelector('.viewport').dataset.view = Parameters.View;
        document.getElementById('LimbReadout').textContent = Parameters.View === 'surface' ? 'SINGLE-SCATTER SKY' : Parameters.Limb ? 'RIM ENHANCED ×3' : 'PHYSICAL RIM';
        document.getElementById('ViewDescription').textContent = Parameters.View === 'orbit' ? 'ORBITAL PERSPECTIVE' : 'SURFACE PERSPECTIVE';
        document.getElementById('ViewHint').textContent = Parameters.View === 'orbit' ? 'DRAG TO ORBIT' : 'SURFACE / 100 M';
        AtmosphereCanvas.style.cursor = Parameters.View === 'orbit' ? 'grab' : 'default';
        RequestRender();
    });
}
let Pointer = null;
AtmosphereCanvas.addEventListener('pointerdown', Event => {
    if (Parameters.View !== 'orbit') return;
    Pointer = {X: Event.clientX, Y: Event.clientY};
    AtmosphereCanvas.setPointerCapture(Event.pointerId);
});
AtmosphereCanvas.addEventListener('pointermove', Event => {
    if (!Pointer || Parameters.View !== 'orbit') return;
    OrbitAim.Yaw += (Event.clientX - Pointer.X) * 0.005;
    OrbitAim.Pitch = Math.max(-1.1, Math.min(1.1, OrbitAim.Pitch + (Event.clientY - Pointer.Y) * 0.005));
    Pointer = {X: Event.clientX, Y: Event.clientY};
    RequestRender();
});
function EndPointer() { Pointer = null; }
AtmosphereCanvas.addEventListener('pointerup', EndPointer);
AtmosphereCanvas.addEventListener('pointercancel', EndPointer);
AtmosphereCanvas.addEventListener('lostpointercapture', EndPointer);
AtmosphereCanvas.addEventListener('webglcontextlost', Event => {
    Event.preventDefault();
    RenderNotice.hidden = false;
    document.getElementById('PreviewStatus').textContent = 'PREVIEW UNAVAILABLE';
    RenderNotice.textContent = 'The graphics context was lost. The preview will resume if the browser restores it.';
    AtmosphereCanvas.dataset.rendered = 'false';
});
AtmosphereCanvas.addEventListener('webglcontextrestored', () => {
    try { InitialiseAtmosphere(); RenderNotice.hidden = true; document.getElementById('PreviewStatus').textContent = 'LIVE PREVIEW'; }
    catch (Error) { RenderNotice.hidden = false; RenderNotice.textContent = 'Atmosphere preview unavailable: ' + Error.message; }
});
window.addEventListener('resize', RequestRender);
ApplyPlanet('Earth');
try { InitialiseAtmosphere(); }
catch (Error) {
    RenderNotice.hidden = false;
    document.getElementById('PreviewStatus').textContent = 'PREVIEW UNAVAILABLE';
    RenderNotice.textContent = 'Atmosphere preview unavailable: ' + Error.message;
    AtmosphereCanvas.dataset.rendered = 'false';
    console.error(Error);
}
