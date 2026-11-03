/*==========================================================================================================================================
                                           FRONTIER · SOFTWARE 3D SCENE PREVIEW
===========================================================================================================================================*/
/* Dependency-free canvas renderer for the geometry-backed object records. It draws the procedural Sun/Atmosphere/Clouds
   state and four depth-sorted primitive meshes. Drag to orbit, wheel to zoom, click a mesh to select its record. */
'use strict';

window.FrontierScenePreview3D = (() =>
{
    const ToneHex = {
        blue: '#8fb4e8', amber: '#f0b45e', red: '#e87568', green: '#7ed09a',
        violet: '#b9a5e8', cyan: '#65c7bd', white: '#d8dadd'
    };
    let Canvas = null;
    let Ctx = null;
    let GetState = () => ({ environment: [], objects: [], selected: '' });
    let OnSelect = () => {};
    let Width = 0, Height = 0, PixelRatio = 1;
    let Pointer = null;
    let LastCenters = [];
    const Camera = { yaw: -0.58, pitch: 0.42, distance: 10.5, focal: 640 };
    const clamp = (Value, Minimum, Maximum) => Math.min(Maximum, Math.max(Minimum, Value));
    const rad = Degrees => Degrees * Math.PI / 180;

    function init(CanvasElement, StateReader, SelectCallback)
    {
        Canvas = CanvasElement;
        Ctx = Canvas ? Canvas.getContext('2d') : null;
        GetState = StateReader || GetState;
        OnSelect = SelectCallback || OnSelect;
        if (!Canvas || !Ctx) return { resize, resetCamera };
        Canvas.addEventListener('pointerdown', Event =>
        {
            Pointer = { id: Event.pointerId, x: Event.clientX, y: Event.clientY, moved: false };
            try { Canvas.setPointerCapture(Event.pointerId); } catch { /* capture is optional */ }
        });
        Canvas.addEventListener('pointermove', Event =>
        {
            if (!Pointer || Pointer.id !== Event.pointerId) return;
            const DX = Event.clientX - Pointer.x;
            const DY = Event.clientY - Pointer.y;
            if (Math.abs(DX) + Math.abs(DY) > 3) Pointer.moved = true;
            Camera.yaw += DX * 0.006;
            Camera.pitch = clamp(Camera.pitch + DY * 0.004, 0.12, 1.15);
            Pointer.x = Event.clientX;
            Pointer.y = Event.clientY;
        });
        const EndPointer = Event =>
        {
            if (!Pointer || Pointer.id !== Event.pointerId) return;
            if (!Pointer.moved) pick(Event.clientX, Event.clientY);
            Pointer = null;
            try { Canvas.releasePointerCapture(Event.pointerId); } catch { /* capture may already be gone */ }
        };
        Canvas.addEventListener('pointerup', EndPointer);
        Canvas.addEventListener('pointercancel', EndPointer);
        Canvas.addEventListener('wheel', Event =>
        {
            Event.preventDefault();
            Camera.distance = clamp(Camera.distance + Math.sign(Event.deltaY) * 0.6, 5.5, 18);
        }, { passive: false });
        window.addEventListener('resize', resize);
        resize();
        requestAnimationFrame(frame);
        return { resize, resetCamera };
    }

    function resize()
    {
        if (!Canvas || !Ctx) return;
        const Bounds = Canvas.getBoundingClientRect();
        Width = Math.max(0, Math.round(Bounds.width || Canvas.clientWidth || 0));
        Height = Math.max(0, Math.round(Bounds.height || Canvas.clientHeight || 0));
        PixelRatio = Math.min(2, window.devicePixelRatio || 1);
        if (!Width || !Height) return;
        const PixelWidth = Math.round(Width * PixelRatio);
        const PixelHeight = Math.round(Height * PixelRatio);
        if (Canvas.width !== PixelWidth || Canvas.height !== PixelHeight)
        {
            Canvas.width = PixelWidth;
            Canvas.height = PixelHeight;
        }
        Ctx.setTransform(PixelRatio, 0, 0, PixelRatio, 0, 0);
    }

    function resetCamera()
    {
        Camera.yaw = -0.58;
        Camera.pitch = 0.42;
        Camera.distance = 10.5;
    }

    function frame(Now)
    {
        requestAnimationFrame(frame);
        if (!Canvas || !Ctx || Canvas.hidden) return;
        const Surface = Canvas.closest('.composition-surface');
        if (!Surface || Surface.dataset.view !== '3d') return;
        resize();
        if (!Width || !Height) return;
        draw(GetState() || {}, Now || 0);
    }

    function hexToRgb(Hex)
    {
        const Clean = String(Hex || '#8fb4e8').replace('#', '');
        const Full = Clean.length === 3 ? Clean.split('').map(Character => Character + Character).join('') : Clean;
        const Integer = parseInt(Full, 16);
        return [(Integer >> 16) & 255, (Integer >> 8) & 255, Integer & 255];
    }

    function shade(Hex, Factor, Alpha = 1)
    {
        const [R, G, B] = hexToRgb(Hex);
        const Lift = 0.32 + 0.68 * clamp(Factor, 0, 1);
        return `rgba(${Math.round(R * Lift)},${Math.round(G * Lift)},${Math.round(B * Lift)},${Alpha})`;
    }

    function rotateY(Point, Angle)
    {
        const C = Math.cos(Angle), S = Math.sin(Angle);
        return [Point[0] * C + Point[2] * S, Point[1], -Point[0] * S + Point[2] * C];
    }
    function rotateX(Point, Angle)
    {
        const C = Math.cos(Angle), S = Math.sin(Angle);
        return [Point[0], Point[1] * C - Point[2] * S, Point[1] * S + Point[2] * C];
    }
    function rotateZ(Point, Angle)
    {
        const C = Math.cos(Angle), S = Math.sin(Angle);
        return [Point[0] * C - Point[1] * S, Point[0] * S + Point[1] * C, Point[2]];
    }

    function project(Point)
    {
        const Target = [0, 1.05, 0];
        let P = [Point[0] - Target[0], Point[1] - Target[1], Point[2] - Target[2]];
        P = rotateY(P, -Camera.yaw);
        P = rotateX(P, -Camera.pitch);
        const Depth = Math.max(0.15, P[2] + Camera.distance);
        const Scale = Camera.focal / Depth;
        return [Width / 2 + P[0] * Scale, Height / 2 - P[1] * Scale, Depth];
    }

    function boxGeometry()
    {
        const Vertices = [
            [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
            [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]
        ];
        const Faces = [
            [0, 1, 2, 3], [4, 5, 6, 7], [0, 4, 7, 3],
            [1, 5, 6, 2], [3, 2, 6, 7], [0, 1, 5, 4]
        ];
        return { Vertices, Faces };
    }

    function coneGeometry(Segments = 10)
    {
        const Vertices = [[0, 1, 0]];
        for (let Index = 0; Index < Segments; ++Index)
        {
            const Angle = Index / Segments * Math.PI * 2;
            Vertices.push([Math.cos(Angle), -1, Math.sin(Angle)]);
        }
        const Faces = [];
        for (let Index = 0; Index < Segments; ++Index)
            Faces.push([0, 1 + Index, 1 + (Index + 1) % Segments]);
        Faces.push(Array.from({ length: Segments }, (_, Index) => Segments - Index));
        return { Vertices, Faces };
    }

    function icosahedronGeometry()
    {
        const T = (1 + Math.sqrt(5)) / 2;
        const Raw = [
            [-1, T, 0], [1, T, 0], [-1, -T, 0], [1, -T, 0],
            [0, -1, T], [0, 1, T], [0, -1, -T], [0, 1, -T],
            [T, 0, -1], [T, 0, 1], [-T, 0, -1], [-T, 0, 1]
        ];
        const Norm = Math.sqrt(1 + T * T);
        const Vertices = Raw.map(Point => Point.map(Value => Value / Norm));
        const Faces = [
            [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
            [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
            [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
            [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]
        ];
        return { Vertices, Faces };
    }

    function geometryFor(Shape)
    {
        if (Shape === 'cone') return coneGeometry(10);
        if (Shape === 'icosahedron') return icosahedronGeometry();
        return boxGeometry();
    }

    function transformVertex(Record, Vertex)
    {
        const Scale = Record.scale || { x: 1, y: 1, z: 1 };
        const Position = Record.position || { x: 0, y: 0, z: 0 };
        const Rotation = Record.rotation || { x: 0, y: 0, z: 0 };
        let P = [Vertex[0] * Scale.x, Vertex[1] * Scale.y, Vertex[2] * Scale.z];
        P = rotateX(P, rad(Rotation.x));
        P = rotateY(P, rad(Rotation.y));
        P = rotateZ(P, rad(Rotation.z));
        return [P[0] + Position.x, P[1] + Position.y, P[2] + Position.z];
    }

    function faceNormal(Points, Face)
    {
        const A = Points[Face[0]], B = Points[Face[1]], C = Points[Face[2]];
        const U = [B[0] - A[0], B[1] - A[1], B[2] - A[2]];
        const V = [C[0] - A[0], C[1] - A[1], C[2] - A[2]];
        const Cross = [U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0]];
        const Length = Math.hypot(...Cross) || 1;
        return Cross.map(Value => Value / Length);
    }

    function environmentMap(State)
    {
        return Object.fromEntries((State.environment || []).map(Record => [Record.id, Record]));
    }

    function drawSky(Environment)
    {
        const Atmosphere = Environment.atmosphere || {};
        const Sun = Environment.sun || {};
        const Mie = clamp(Number(Atmosphere.mie) || 2.3, 0, 6) / 6;
        const Rayleigh = clamp(Number(Atmosphere.rayleigh) || 1, 0, 3) / 3;
        const Gradient = Ctx.createLinearGradient(0, 0, 0, Height);
        Gradient.addColorStop(0, `rgb(${Math.round(20 + Rayleigh * 10)},${Math.round(28 + Rayleigh * 18)},${Math.round(46 + Rayleigh * 26)})`);
        Gradient.addColorStop(0.48, `rgb(${Math.round(75 + Mie * 18)},${Math.round(92 + Mie * 14)},${Math.round(112 + Mie * 10)})`);
        Gradient.addColorStop(1, '#121516');
        Ctx.fillStyle = Gradient;
        Ctx.fillRect(0, 0, Width, Height);
        const Elevation = clamp(Number(Sun.elevationDeg) || 38, -12, 90);
        const Azimuth = clamp(Number(Sun.azimuthDeg) || 142, 0, 360);
        const SunX = Width * 0.74 + (Azimuth - 180) / 180 * Width * 0.08;
        const SunY = Height * 0.14 + (1 - clamp(Elevation / 90, 0, 1)) * Height * 0.25;
        const Glow = Ctx.createRadialGradient(SunX, SunY, 0, SunX, SunY, 100);
        Glow.addColorStop(0, 'rgba(255,229,170,.95)');
        Glow.addColorStop(0.14, 'rgba(240,180,94,.38)');
        Glow.addColorStop(1, 'rgba(240,180,94,0)');
        Ctx.fillStyle = Glow;
        Ctx.fillRect(SunX - 105, SunY - 105, 210, 210);
        Ctx.beginPath();
        Ctx.arc(SunX, SunY, 13, 0, Math.PI * 2);
        Ctx.fillStyle = 'rgba(255,239,202,.95)';
        Ctx.fill();
        const Illuminance = clamp(Number(Sun.illuminanceKlx) || 98, 0, 150) / 150;
        for (let Index = 0; Index < 12; ++Index)
        {
            const Angle = Index / 12 * Math.PI * 2;
            const Inner = 18, Outer = 26 + Illuminance * 18;
            Ctx.beginPath();
            Ctx.moveTo(SunX + Math.cos(Angle) * Inner, SunY + Math.sin(Angle) * Inner);
            Ctx.lineTo(SunX + Math.cos(Angle) * Outer, SunY + Math.sin(Angle) * Outer);
            Ctx.strokeStyle = 'rgba(255,225,160,.36)';
            Ctx.lineWidth = 1;
            Ctx.stroke();
        }
        return { SunX, SunY, Elevation };
    }

    function drawClouds(Environment, Now, Selected)
    {
        const Clouds = Environment.clouds || {};
        const Coverage = clamp(Number(Clouds.coveragePct) || 42, 0, 100) / 100;
        const Wind = clamp(Number(Clouds.windMs) || 6.5, 0, 30);
        const Drift = (Now * 0.000012 * (0.4 + Wind)) % 1.4;
        for (let Index = 0; Index < 7; ++Index)
        {
            const Fraction = ((Index * 0.23 + Drift) % 1.4) - 0.2;
            const X = Fraction * Width;
            const Y = Height * (0.20 + (Index % 3) * 0.055);
            const RX = Width * (0.10 + (Index % 4) * 0.022);
            const RY = Height * (0.028 + (Index % 3) * 0.008);
            const Alpha = 0.025 + Coverage * 0.11;
            const CloudGradient = Ctx.createRadialGradient(X, Y, 0, X, Y, RX);
            CloudGradient.addColorStop(0, `rgba(220,228,232,${Alpha})`);
            CloudGradient.addColorStop(1, 'rgba(220,228,232,0)');
            Ctx.fillStyle = CloudGradient;
            Ctx.beginPath();
            Ctx.ellipse(X, Y, RX, RY * 1.8, 0, 0, Math.PI * 2);
            Ctx.fill();
        }
        if (Selected === 'clouds' || String(Selected).startsWith('clouds:'))
        {
            Ctx.strokeStyle = 'rgba(143,180,232,.6)';
            Ctx.setLineDash([5, 6]);
            Ctx.strokeRect(Width * 0.08, Height * 0.15, Width * 0.84, Height * 0.20);
            Ctx.setLineDash([]);
        }
    }

    function drawGround(Sun)
    {
        const Elevation = clamp(Number(Sun.elevationDeg) || 38, -12, 90) / 90;
        Ctx.lineWidth = 1;
        for (let Index = -10; Index <= 10; ++Index)
        {
            const Position = Index;
            const A = project([Position, 0, -10]);
            const B = project([Position, 0, 10]);
            const C = project([-10, 0, Position]);
            const D = project([10, 0, Position]);
            Ctx.strokeStyle = `rgba(180,200,210,${0.045 + Elevation * 0.045})`;
            Ctx.beginPath(); Ctx.moveTo(A[0], A[1]); Ctx.lineTo(B[0], B[1]); Ctx.stroke();
            Ctx.beginPath(); Ctx.moveTo(C[0], C[1]); Ctx.lineTo(D[0], D[1]); Ctx.stroke();
        }
        const Horizon = Ctx.createLinearGradient(0, Height * 0.48, 0, Height);
        Horizon.addColorStop(0, 'rgba(15,18,19,0)');
        Horizon.addColorStop(1, 'rgba(8,10,11,.88)');
        Ctx.fillStyle = Horizon;
        Ctx.fillRect(0, Height * 0.48, Width, Height * 0.52);
    }

    function drawObjects(State, Environment)
    {
        const Sun = Environment.sun || {};
        const Elevation = rad(clamp(Number(Sun.elevationDeg) || 38, -12, 90));
        const Azimuth = rad(clamp(Number(Sun.azimuthDeg) || 142, 0, 360));
        const Light = [Math.cos(Elevation) * Math.sin(Azimuth), Math.sin(Elevation), Math.cos(Elevation) * Math.cos(Azimuth)];
        const SelectedParent = String(State.selected || '').split(':')[0];
        const Objects = State.objects || [];
        const DrawList = [];
        LastCenters = [];
        Objects.forEach((Record, ObjectIndex) =>
        {
            const Geometry = geometryFor(Record.shape);
            const World = Geometry.Vertices.map(Vertex => transformVertex(Record, Vertex));
            const Center = project(Record.position || { x: 0, y: 0, z: 0 });
            LastCenters.push({ id: Record.id, x: Center[0], y: Center[1], depth: Center[2] });
            Geometry.Faces.forEach((Face, FaceIndex) =>
            {
                const Points = Face.map(Index => project(World[Index]));
                const Depth = Points.reduce((Sum, Point) => Sum + Point[2], 0) / Points.length;
                const Normal = faceNormal(World, Face);
                const Dot = Math.abs(Normal[0] * Light[0] + Normal[1] * Light[1] + Normal[2] * Light[2]);
                DrawList.push({ Record, ObjectIndex, FaceIndex, Points, Depth, Brightness: Dot });
            });
        });
        DrawList.sort((A, B) => B.Depth - A.Depth);
        DrawList.forEach(Entry =>
        {
            const Tone = ToneHex[Entry.Record.tone] || ToneHex.blue;
            const Selected = SelectedParent === Entry.Record.id;
            Ctx.beginPath();
            Entry.Points.forEach((Point, Index) => Index ? Ctx.lineTo(Point[0], Point[1]) : Ctx.moveTo(Point[0], Point[1]));
            Ctx.closePath();
            Ctx.fillStyle = shade(Tone, Entry.Brightness, Selected ? 0.92 : 0.80);
            Ctx.strokeStyle = Selected ? 'rgba(255,255,255,.68)' : 'rgba(255,255,255,.14)';
            Ctx.lineWidth = Selected ? 1.5 : 0.7;
            Ctx.fill();
            Ctx.stroke();
        });
        Objects.forEach((Record, Index) =>
        {
            const Center = LastCenters[Index];
            if (!Center) return;
            const Selected = SelectedParent === Record.id;
            Ctx.font = `${Selected ? '600 ' : ''}10px 'DM Sans', system-ui, sans-serif`;
            Ctx.textAlign = 'center';
            Ctx.fillStyle = Selected ? '#ffffff' : 'rgba(220,224,228,.72)';
            Ctx.fillText(`O${Index + 1} · ${Record.name}`, Center.x, Center.y - 34);
            Ctx.beginPath();
            Ctx.arc(Center.x, Center.y - 28, Selected ? 3.5 : 2.2, 0, Math.PI * 2);
            Ctx.fillStyle = Selected ? '#ffffff' : ToneHex[Record.tone] || '#8fb4e8';
            Ctx.fill();
        });
    }

    function drawHud(State)
    {
        const Objects = State.objects || [];
        const Triangles = Objects.reduce((Sum, Record) => Sum + (Number(Record.triangles) || 0), 0);
        Ctx.font = "9px 'DM Sans', system-ui, sans-serif";
        Ctx.textAlign = 'left';
        Ctx.fillStyle = 'rgba(220,224,228,.62)';
        Ctx.fillText(`${Objects.length} geometry records · ${Triangles} triangles · DRAG TO ORBIT · WHEEL TO ZOOM`, 16, Height - 16);
        Ctx.textAlign = 'right';
        Ctx.fillStyle = 'rgba(103,211,138,.82)';
        Ctx.fillText('LIVE 3D PREVIEW', Width - 16, Height - 16);
    }

    function draw(State, Now)
    {
        const Environment = environmentMap(State);
        Ctx.clearRect(0, 0, Width, Height);
        drawSky(Environment);
        drawClouds(Environment, Now, State.selected);
        drawGround(Environment.sun || {});
        drawObjects(State, Environment);
        drawHud(State);
    }

    function pick(ClientX, ClientY)
    {
        if (!Canvas) return;
        const Bounds = Canvas.getBoundingClientRect();
        const X = ClientX - Bounds.left;
        const Y = ClientY - Bounds.top;
        let Best = null;
        let BestDistance = 34;
        LastCenters.forEach(Center =>
        {
            const Distance = Math.hypot(Center.x - X, Center.y - 30 - Y);
            if (Distance < BestDistance) { BestDistance = Distance; Best = Center; }
        });
        if (Best) OnSelect(Best.id);
    }

    return { init, resize, resetCamera };
})();
