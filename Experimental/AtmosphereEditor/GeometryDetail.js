/*============================================================================================================================================
                                                          GEOMETRYDETAIL.JS
============================================================================================================================================*/
// 🧩 Distance-controlled cluster detail preview, RAM/VRAM 64 KB page telemetry, 3-tier material scaling, and ThemeStructure binding.
'use strict';

const GeometryCanvas = document.getElementById('GeometryCanvas');
const GeometryNotice = document.getElementById('GeometryNotice');
const GeometryDefaults = {GeometryDistance: 35, GeometryNear: 15, GeometryFar: 100, GeometryRetention: 12, GeometryCurvePower: 1.5, GeometryMaterial: 80};
const GeometryParameters = {...GeometryDefaults, Style: 'shaded', Yaw: -.45, Pitch: .18};
const GeometryAim = {Yaw: -.45, Pitch: .18};
let GeometryGl = null, GeometryProgram = null, GeometryUniforms = {}, GeometryPending = false;
let GeometryVertexBuffer = null, GeometryTriangleBuffer = null, GeometryEdgeBuffer = null;
let GeometryTriangleCount = 0, GeometryEdgeCount = 0, GeometryRings = 0, GeometryTimestamp = 0;
let GeometryPointer = null, SelectedEditor = 'atmosphere', GeometryFocusDistance = GeometryDefaults.GeometryDistance;
const GeometryMaximumRings = 60;
const GeometryMaximumTriangles = 2 * GeometryMaximumRings * 2 * (GeometryMaximumRings - 1);
const PageGranularityBytes = 65536;

const GeometryVertexSource = `#version 300 es
precision highp float;
in vec3 Position;
in vec3 Normal;
uniform vec2 Orbit;
uniform float Aspect;
uniform float CameraDistance;
out vec3 SurfacePosition;
out vec3 SurfaceNormal;
vec3 Rotate(vec3 V) {
    float CY = cos(Orbit.x), SY = sin(Orbit.x), CX = cos(Orbit.y), SX = sin(Orbit.y);
    V = vec3(CY*V.x + SY*V.z, V.y, -SY*V.x + CY*V.z);
    return vec3(V.x, CX*V.y - SX*V.z, SX*V.y + CX*V.z);
}
void main() {
    vec3 P = Rotate(Position);
    SurfacePosition = Position;
    SurfaceNormal = normalize(Rotate(Normal));
    float Depth = CameraDistance - P.z;
    float Fit = Aspect < 0.9 ? Aspect / 1.3 : 1.0;
    float Offset = Aspect < 0.9 ? 0.35 : 0.2;
    gl_Position = vec4(P.x * 2.25 * Fit / Aspect + Offset * Depth, P.y * 2.25 * Fit, 1.0067*Depth-0.2007, Depth);
}
`;
const GeometryFragmentSource = `#version 300 es
precision highp float;
in vec3 SurfacePosition;
in vec3 SurfaceNormal;
uniform float MaterialDetail;
uniform float Lines;
out vec4 Fragment;
void main() {
    if (Lines > 0.5) { Fragment = vec4(0.87,0.89,0.91,0.78); return; }
    vec3 N = normalize(SurfaceNormal);
    vec3 L = normalize(vec3(-0.5,0.8,1.1));
    float Diffuse = max(dot(N,L),0.0);
    float Rim = pow(1.0-max(N.z,0.0),3.0);
    float Pattern = sin(SurfacePosition.x*24.0)*sin(SurfacePosition.y*31.0)*sin(SurfacePosition.z*19.0);
    float Grain = mix(0.0, 0.045, MaterialDetail) * Pattern;
    float Specular = pow(max(dot(reflect(-L,N),vec3(0.0,0.0,1.0)),0.0), mix(9.0,52.0,MaterialDetail));
    vec3 Colour = vec3(0.23,0.25,0.27) * (0.22+Diffuse*1.25) + vec3(0.26,0.29,0.32)*Rim*.55 + vec3(Specular*.25) + Grain;
    Fragment = vec4(pow(max(Colour,vec3(0.0)),vec3(1.0/2.2)),1.0);
}
`;

function GeometryCompile(Type, Source)
{
    const Shader = GeometryGl.createShader(Type);
    GeometryGl.shaderSource(Shader, Source);
    GeometryGl.compileShader(Shader);
    if (!GeometryGl.getShaderParameter(Shader, GeometryGl.COMPILE_STATUS)) throw new Error(GeometryGl.getShaderInfoLog(Shader));
    return Shader;
}

function InitialiseGeometry()
{
    GeometryGl = GeometryCanvas.getContext('webgl2', {alpha: true, antialias: true, preserveDrawingBuffer: true});
    if (!GeometryGl) throw new Error('WebGL 2 is not available in this browser.');
    GeometryProgram = GeometryGl.createProgram();
    GeometryGl.attachShader(GeometryProgram, GeometryCompile(GeometryGl.VERTEX_SHADER, GeometryVertexSource));
    GeometryGl.attachShader(GeometryProgram, GeometryCompile(GeometryGl.FRAGMENT_SHADER, GeometryFragmentSource));
    GeometryGl.linkProgram(GeometryProgram);
    if (!GeometryGl.getProgramParameter(GeometryProgram, GeometryGl.LINK_STATUS)) throw new Error(GeometryGl.getProgramInfoLog(GeometryProgram));
    GeometryGl.useProgram(GeometryProgram);
    GeometryVertexBuffer = GeometryGl.createBuffer();
    GeometryTriangleBuffer = GeometryGl.createBuffer();
    GeometryEdgeBuffer = GeometryGl.createBuffer();
    GeometryGl.bindBuffer(GeometryGl.ARRAY_BUFFER, GeometryVertexBuffer);
    for (const [Name, Offset] of [['Position', 0], ['Normal', 12]]) {
        const Location = GeometryGl.getAttribLocation(GeometryProgram, Name);
        GeometryGl.enableVertexAttribArray(Location);
        GeometryGl.vertexAttribPointer(Location, 3, GeometryGl.FLOAT, false, 24, Offset);
    }
    for (const Name of ['Orbit', 'Aspect', 'CameraDistance', 'MaterialDetail', 'Lines']) GeometryUniforms[Name] = GeometryGl.getUniformLocation(GeometryProgram, Name);
    GeometryRings = 0;
    UpdateGeometry();
}

function GeometryQuality(Distance)
{
    const Near = GeometryParameters.GeometryNear, Far = GeometryParameters.GeometryFar;
    const Fraction = Math.max(0, Math.min(1, (Distance - Near) / Math.max(1, Far - Near)));
    return 1 - (1 - GeometryParameters.GeometryRetention / 100) * Math.pow(Fraction, GeometryParameters.GeometryCurvePower);
}

function BuildGeometry(Rings)
{
    if (!GeometryGl || Rings === GeometryRings) return;
    GeometryRings = Rings;
    const Slices = Rings * 2, Vertices = [], Triangles = [], Edges = [];
    for (let I = 0; I <= Rings; ++I) {
        const Theta = Math.PI * I / Rings;
        for (let J = 0; J <= Slices; ++J) {
            const Phi = Math.PI * 2 * J / Slices;
            const X = Math.sin(Theta) * Math.cos(Phi), Y = Math.cos(Theta), Z = Math.sin(Theta) * Math.sin(Phi);
            const Relief = 1 + .11*Math.sin(X*5.1+Y*3.4)*Math.sin(Z*4.4-Y*2.5) + .045*Math.sin(X*11+Z*7)*Math.cos(Y*8);
            Vertices.push(X*Relief, Y*Relief, Z*Relief, X, Y, Z);
        }
    }
    function AddTriangle(A, B, C) { Triangles.push(A, B, C); Edges.push(A, B, B, C, C, A); }
    for (let I = 0; I < Rings; ++I) for (let J = 0; J < Slices; ++J) {
        const A = I*(Slices+1)+J, B = A+1, C = A+Slices+1, D = C+1;
        if (I > 0) AddTriangle(A, C, B);
        if (I < Rings-1) AddTriangle(B, C, D);
    }
    GeometryTriangleCount = Triangles.length / 3;
    GeometryEdgeCount = Edges.length;
    GeometryGl.bindBuffer(GeometryGl.ARRAY_BUFFER, GeometryVertexBuffer);
    GeometryGl.bufferData(GeometryGl.ARRAY_BUFFER, new Float32Array(Vertices), GeometryGl.STATIC_DRAW);
    GeometryGl.bindBuffer(GeometryGl.ELEMENT_ARRAY_BUFFER, GeometryTriangleBuffer);
    GeometryGl.bufferData(GeometryGl.ELEMENT_ARRAY_BUFFER, new Uint16Array(Triangles), GeometryGl.STATIC_DRAW);
    GeometryGl.bindBuffer(GeometryGl.ELEMENT_ARRAY_BUFFER, GeometryEdgeBuffer);
    GeometryGl.bufferData(GeometryGl.ELEMENT_ARRAY_BUFFER, new Uint16Array(Edges), GeometryGl.STATIC_DRAW);
}

function UpdateGeometryPlot()
{
    const Points = [];
    for (let I = 0; I <= 60; ++I) {
        const Distance = I * 150 / 60, Quality = GeometryQuality(Distance);
        Points.push([I*5, 69 - Quality*57]);
    }
    const Path = Points.map((P, I) => (I ? 'L' : 'M') + P[0].toFixed(2) + ' ' + P[1].toFixed(2)).join(' ');
    document.getElementById('GeometryCurve').setAttribute('d', Path);
    document.getElementById('GeometryArea').setAttribute('d', Path + ' L300 75 L0 75 Z');
    const X = GeometryParameters.GeometryDistance / 150 * 300;
    const Y = 69 - GeometryQuality(GeometryParameters.GeometryDistance) * 57;
    document.getElementById('GeometryMarker').setAttribute('cx', X);
    document.getElementById('GeometryMarker').setAttribute('cy', Y);
    document.getElementById('GeometryMarkerLine').setAttribute('x1', X);
    document.getElementById('GeometryMarkerLine').setAttribute('x2', X);
}

function FormatKiB(Bytes)
{
    return (Bytes / 1024).toFixed(0) + ' KiB';
}

function UpdateGeometry()
{
    if (!GeometryGl) return;
    const Quality = GeometryQuality(GeometryParameters.GeometryDistance);
    BuildGeometry(Math.max(4, Math.min(GeometryMaximumRings, Math.round(Math.sqrt(Quality) * GeometryMaximumRings))));
    const Percentage = GeometryTriangleCount / GeometryMaximumTriangles * 100;
    document.getElementById('GeometryTriangles').textContent = GeometryTriangleCount.toLocaleString('en-US');
    document.getElementById('GeometryPercent').textContent = Percentage.toFixed(1) + '% of full detail';
    document.getElementById('GeometryDistanceMetric').textContent = GeometryParameters.GeometryDistance + ' m';
    document.getElementById('GeometryPreviewReadout').textContent = GeometryTriangleCount.toLocaleString('en-US') + ' TRIANGLES / ' + Percentage.toFixed(1) + '%';

    // Mirror ClusterHierarchy RAM/VRAM 64 KB page + 3-tier virtualized material telemetry
    const TotalRamBytes = Math.ceil((GeometryMaximumTriangles * 60) / PageGranularityBytes) * PageGranularityBytes;
    const ActivePages = Math.max(1, Math.ceil((GeometryTriangleCount * 48) / PageGranularityBytes));
    const MatScale = GeometryParameters.GeometryMaterial / 100;
    const Dist = GeometryParameters.GeometryDistance;
    const MatTierLabel = (Dist <= 18 * Math.max(0.25, MatScale)) ? 'FullStrata (64B)'
                       : ((Dist <= 65 * Math.max(0.25, MatScale)) ? 'Toksvig (24B)' : 'Proxy (16B)');
    constMatReadouts(TotalRamBytes, ActivePages * PageGranularityBytes, MatTierLabel);

    UpdateGeometryPlot();
    RequestGeometry();
}

function constMatReadouts(RamBytes, VramBytes, TierLabel)
{
    const RamEl = document.getElementById('GeometryRamBytes');
    const VramEl = document.getElementById('GeometryVramBytes');
    const MatEl = document.getElementById('GeometryMaterialTier');
    if (RamEl) RamEl.textContent = FormatKiB(RamBytes);
    if (VramEl) VramEl.textContent = FormatKiB(VramBytes);
    if (MatEl) MatEl.textContent = TierLabel;
}

function RenderGeometry(Timestamp)
{
    GeometryPending = false;
    if (!GeometryGl || GeometryGl.isContextLost() || SelectedEditor !== 'geometry') return;
    let Moving = false;
    if (Number.isFinite(Timestamp)) {
        const Delta = GeometryTimestamp ? Math.min(.1, Math.max(0, (Timestamp-GeometryTimestamp)/1000)) : 1/60;
        const Blend = 1 - Math.exp(-Delta/.2);
        for (const Axis of ['Yaw','Pitch']) {
            GeometryParameters[Axis] += (GeometryAim[Axis]-GeometryParameters[Axis])*Blend;
            if (Math.abs(GeometryAim[Axis]-GeometryParameters[Axis]) < .0001) GeometryParameters[Axis] = GeometryAim[Axis];
            else Moving = true;
        }
        GeometryFocusDistance += (GeometryParameters.GeometryDistance-GeometryFocusDistance)*Blend;
        if (Math.abs(GeometryParameters.GeometryDistance-GeometryFocusDistance)<.01) GeometryFocusDistance=GeometryParameters.GeometryDistance;
        else Moving=true;
        GeometryTimestamp = Moving ? Timestamp : 0;
    }
    const Scale = Math.min(devicePixelRatio || 1, 1), Width = Math.max(1,Math.round(GeometryCanvas.clientWidth*Scale)), Height = Math.max(1,Math.round(GeometryCanvas.clientHeight*Scale));
    if (GeometryCanvas.width !== Width || GeometryCanvas.height !== Height) { GeometryCanvas.width=Width; GeometryCanvas.height=Height; }
    GeometryGl.viewport(0,0,Width,Height);
    GeometryGl.clearColor(0,0,0,0);
    GeometryGl.clear(GeometryGl.COLOR_BUFFER_BIT | GeometryGl.DEPTH_BUFFER_BIT);
    GeometryGl.enable(GeometryGl.DEPTH_TEST);
    GeometryGl.depthFunc(GeometryGl.LEQUAL);
    GeometryGl.useProgram(GeometryProgram);
    GeometryGl.uniform2f(GeometryUniforms.Orbit, GeometryParameters.Yaw, GeometryParameters.Pitch);
    GeometryGl.uniform1f(GeometryUniforms.Aspect, Width/Height);
    GeometryGl.uniform1f(GeometryUniforms.CameraDistance, 3 + 2*Math.sqrt(GeometryFocusDistance/150));
    GeometryGl.uniform1f(GeometryUniforms.MaterialDetail, GeometryParameters.GeometryMaterial/100);
    GeometryGl.uniform1f(GeometryUniforms.Lines, 0);
    GeometryGl.bindBuffer(GeometryGl.ELEMENT_ARRAY_BUFFER, GeometryTriangleBuffer);
    GeometryGl.enable(GeometryGl.POLYGON_OFFSET_FILL); GeometryGl.polygonOffset(1,1);
    GeometryGl.drawElements(GeometryGl.TRIANGLES, GeometryTriangleCount*3, GeometryGl.UNSIGNED_SHORT, 0);
    GeometryGl.disable(GeometryGl.POLYGON_OFFSET_FILL);
    if (GeometryParameters.Style === 'topology') {
        GeometryGl.uniform1f(GeometryUniforms.Lines, 1);
        GeometryGl.bindBuffer(GeometryGl.ELEMENT_ARRAY_BUFFER, GeometryEdgeBuffer);
        GeometryGl.drawElements(GeometryGl.LINES, GeometryEdgeCount, GeometryGl.UNSIGNED_SHORT, 0);
    }
    GeometryCanvas.dataset.rendered='true';
    if (Moving) RequestGeometry();
}

function RequestGeometry()
{
    if (!GeometryPending && SelectedEditor === 'geometry') { GeometryPending=true; requestAnimationFrame(RenderGeometry); }
}

function UpdateGeometryField(Name)
{
    const Input = document.getElementById(Name), Amount = Number(Input.value);
    GeometryParameters[Name] = Amount;
    const Unit = Name === 'GeometryCurvePower' ? '×' : Name === 'GeometryRetention' || Name === 'GeometryMaterial' ? '%' : 'm';
    document.getElementById(Name+'Readout').innerHTML = (Name === 'GeometryCurvePower' ? Amount.toFixed(2) : Amount.toFixed(0))+'<span>'+Unit+'</span>';
    Input.style.setProperty('--fill', ((Amount-Number(Input.min))/(Number(Input.max)-Number(Input.min))*100)+'%');
}

for (const Name of Object.keys(GeometryDefaults)) {
    const Input = document.getElementById(Name);
    Input.addEventListener('input', () => {
        UpdateGeometryField(Name);
        if (Name === 'GeometryNear' && GeometryParameters.GeometryNear >= GeometryParameters.GeometryFar) {
            document.getElementById('GeometryFar').value = GeometryParameters.GeometryNear+1; UpdateGeometryField('GeometryFar');
        }
        if (Name === 'GeometryFar' && GeometryParameters.GeometryFar <= GeometryParameters.GeometryNear) {
            document.getElementById('GeometryNear').value = GeometryParameters.GeometryFar-1; UpdateGeometryField('GeometryNear');
        }
        UpdateGeometry();
    });
    UpdateGeometryField(Name);
}
document.getElementById('ResetGeometry').addEventListener('click', () => {
    for (const Name of Object.keys(GeometryDefaults)) { document.getElementById(Name).value=GeometryDefaults[Name]; UpdateGeometryField(Name); }
    GeometryParameters.Yaw=GeometryAim.Yaw=-.45; GeometryParameters.Pitch=GeometryAim.Pitch=.18;
    GeometryFocusDistance=GeometryDefaults.GeometryDistance; GeometryTimestamp=0;
    UpdateGeometry();
});
for (const Button of document.querySelectorAll('.geometry-view-button')) Button.addEventListener('click', () => {
    GeometryParameters.Style=Button.dataset.style;
    for (const Choice of document.querySelectorAll('.geometry-view-button')) {
        const Selected = Choice===Button; Choice.classList.toggle('selected',Selected); Choice.setAttribute('aria-pressed',String(Selected));
    }
    RequestGeometry();
});
GeometryCanvas.addEventListener('pointerdown', Event => { GeometryPointer={X:Event.clientX,Y:Event.clientY}; GeometryCanvas.setPointerCapture(Event.pointerId); });
GeometryCanvas.addEventListener('pointermove', Event => {
    if (!GeometryPointer) return;
    GeometryAim.Yaw += (Event.clientX-GeometryPointer.X)*.005;
    GeometryAim.Pitch = Math.max(-1.1,Math.min(1.1,GeometryAim.Pitch+(Event.clientY-GeometryPointer.Y)*.005));
    GeometryPointer={X:Event.clientX,Y:Event.clientY}; RequestGeometry();
});
for (const Name of ['pointerup','pointercancel','lostpointercapture']) GeometryCanvas.addEventListener(Name, () => { GeometryPointer=null; });
GeometryCanvas.addEventListener('webglcontextlost', Event => { Event.preventDefault(); GeometryNotice.hidden=false; GeometryNotice.textContent='Graphics context lost; waiting for the browser to restore it.'; GeometryCanvas.dataset.rendered='false'; });
GeometryCanvas.addEventListener('webglcontextrestored', () => { try { InitialiseGeometry(); GeometryNotice.hidden=true; } catch (Error) { GeometryNotice.hidden=false; GeometryNotice.textContent='Geometry preview unavailable: '+Error.message; } });
window.addEventListener('resize', RequestGeometry);

function SelectEditor(Name)
{
    if (!['atmosphere','geometry'].includes(Name)) return;
    SelectedEditor=Name;
    const IsGeometry=Name==='geometry';
    document.getElementById('AtmosphereViewport').hidden=IsGeometry;
    document.getElementById('AtmosphereCard').hidden=IsGeometry;
    document.getElementById('AtmosphereViews').hidden=IsGeometry;
    document.getElementById('GeometryViewport').hidden=!IsGeometry;
    document.getElementById('GeometryCard').hidden=!IsGeometry;
    document.getElementById('GeometryViews').hidden=!IsGeometry;
    document.getElementById('CurrentEditorName').textContent=IsGeometry?'Geometry detail':'Atmosphere';
    document.getElementById('FooterEditorName').textContent=(IsGeometry?'GEOMETRY DETAIL':'ATMOSPHERE')+' EDITOR · THEME BOUND';
    if (IsGeometry) {
        try { if (!GeometryGl) InitialiseGeometry(); else UpdateGeometry(); GeometryNotice.hidden=true; }
        catch (Error) { GeometryNotice.hidden=false; GeometryNotice.textContent='Geometry preview unavailable: '+Error.message; console.error(Error); }
    } else RequestRender();
}
for (const Button of document.querySelectorAll('[data-editor]')) Button.addEventListener('click', () => {
    SelectEditor(Button.dataset.editor);
    const Card = document.getElementById(Button.dataset.editor === 'geometry' ? 'GeometryCard' : 'AtmosphereCard');
    Card.querySelector('.card-pagination button').focus({preventScroll:true});
});

// Bind ThemeStructure selectors directly to document.documentElement attributes
const ThemePicker = document.getElementById('ThemePicker');
const AccentPicker = document.getElementById('AccentPicker');
const FontPicker = document.getElementById('FontPicker');
if (ThemePicker) ThemePicker.addEventListener('change', () => { document.documentElement.dataset.theme = ThemePicker.value; });
if (AccentPicker) AccentPicker.addEventListener('change', () => { document.documentElement.dataset.accent = AccentPicker.value; });
if (FontPicker) FontPicker.addEventListener('change', () => { document.documentElement.dataset.font = FontPicker.value; });
