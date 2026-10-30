#============================================================================================================================================
#                                                            SERVERENDERER.PY
#============================================================================================================================================
# 🧩 Serve the earlier browser and same-origin Vulkan readbacks from one persistent, explicitly supplied validation executable.

from pathlib import Path
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json
import math
import queue
import struct
import subprocess
import threading
import zlib

Root = Path(__file__).resolve().parents[3]
Images = Root / 'Exhibits/Renderer/Images'


def EncodePng(Width, Height, Pixels):
    def Chunk(Category, Payload):
        return struct.pack('>I', len(Payload)) + Category + Payload + struct.pack('>I', zlib.crc32(Category + Payload))
    Rows = b''.join(b'\0' + Pixels[Offset:Offset + Width * 3] for Offset in range(0, len(Pixels), Width * 3))
    return b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', Width, Height, 8, 2, 0, 0, 0)) + Chunk(b'IDAT', zlib.compress(Rows)) + Chunk(b'IEND', b'')


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--executable', type=Path, required=True)
    Parser.add_argument('--driver', type=Path, required=True)
    Parser.add_argument('--shaders', type=Path, required=True)
    Parser.add_argument('--captures', type=Path, required=True)
    Parser.add_argument('--material-library', type=Path)
    Parser.add_argument('--port', type=int, default=8775)
    Arguments = Parser.parse_args()
    Captures = Arguments.captures.resolve()
    MaterialLibrary = Arguments.material_library.resolve() if Arguments.material_library else None
    NativeArguments = [str(Arguments.executable.resolve()), str(Arguments.driver.resolve()),
                       str(Arguments.shaders.resolve()), str(Captures)]
    if MaterialLibrary:
        NativeArguments.append(str(MaterialLibrary))
    Native = subprocess.Popen(NativeArguments + ['--serve'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    Responses = queue.Queue()
    def ReadResponses():
        for Line in Native.stdout:
            Responses.put(Line.strip())
        Responses.put('EXITED')
    threading.Thread(target=ReadResponses, daemon=True).start()
    Lock = threading.Lock()
    try:
        if Responses.get(timeout=60) != 'READY':
            raise RuntimeError('Vulkan readback executable did not initialize')

        class BrowserResponse(SimpleHTTPRequestHandler):
            def __init__(self, *Arguments, **Keywords):
                super().__init__(*Arguments, directory=str(Root), **Keywords)

            def SendBytes(self, Payload, Mime, Status=200, Headers=None):
                self.send_response(Status)
                self.send_header('Content-Type', Mime)
                self.send_header('Content-Length', str(len(Payload)))
                self.send_header('Cache-Control', 'no-store')
                self.send_header('X-Content-Type-Options', 'nosniff')
                for Name, Content in (Headers or {}).items():
                    self.send_header(Name, Content)
                self.end_headers()
                try:
                    self.wfile.write(Payload)
                except (BrokenPipeError, ConnectionResetError):
                    return

            def do_GET(self):
                if self.path.split('?', 1)[0] == '/':
                    self.path = '/Exhibits/Renderer/index.html'
                elif self.path.startswith('/project-browser/'):
                    self.path = '/Experimental/Project%20browser/' + self.path[len('/project-browser/'):]
                if self.path == '/api/materials' or self.path.startswith('/api/materials/'):
                    if not MaterialLibrary:
                        self.send_error(503, 'Authored material executable is not configured')
                        return
                    Names = sorted(Path.name for Path in MaterialLibrary.glob('*.material'))[:64]
                    if self.path == '/api/materials':
                        self.SendBytes(json.dumps(Names).encode(), 'application/json')
                    else:
                        Name = self.path.removeprefix('/api/materials/')
                        if Name not in Names:
                            self.send_error(404)
                            return
                        self.SendBytes((MaterialLibrary / Name).read_bytes(), 'text/plain; charset=utf-8')
                    return
                if self.path.startswith('/renderer/evidence/'):
                    Name = self.path.split('?')[0].removeprefix('/renderer/evidence/')
                    Allowed = {'PreviewPbrBracket.png', 'PreviewPbrCalibration.png', 'PreviewCadCalibration.png',
                               'PreviewMatCapCalibration.png', 'CadOrthographic.png', 'MatCapBracket.png',
                               'MaterialBrushedCopperBracket.png', 'MaterialBrushedCopperSphere.png'}
                    if Name not in Allowed or not (Images / Name).is_file():
                        self.send_error(404)
                        return
                    self.SendBytes((Images / Name).read_bytes(), 'image/png')
                    return
                # ⚠️ The evidence server never exposes repository credentials, scratch files or arbitrary source roots.
                Resolved = Path(self.translate_path(self.path)).resolve()
                PublicRoots = (Root / 'Exhibits', Root / 'Experimental/Project browser')
                if not any(Resolved.is_relative_to(Public.resolve()) for Public in PublicRoots):
                    self.send_error(403)
                    return
                if any(Part.startswith('.') for Part in Resolved.relative_to(Root).parts):
                    self.send_error(403)
                    return
                super().do_GET()

            def do_POST(self):
                if self.path != '/api/renderer':
                    self.send_error(404)
                    return
                # 📝 Same-origin Fetch only; do not expose local process control to cross-site forms.
                if self.headers.get('Sec-Fetch-Site', 'same-origin') not in ('same-origin', 'none'):
                    self.send_error(403)
                    return
                if self.headers.get('Content-Type') != 'application/json':
                    self.send_error(415)
                    return
                try:
                    Length = int(self.headers.get('Content-Length', '0'))
                    if not 1 <= Length <= 1048576:
                        raise ValueError('Invalid request length')
                    Specification = json.loads(self.rfile.read(Length))
                    if not isinstance(Specification, dict):
                        raise ValueError('Expected a material request table')
                    Names = ('Geometry', 'Mode', 'Roughness', 'Metallic', 'Coating', 'Exposure')
                    Expected = set(Names) | ({'Material'} if Specification.get('Mode') == 3 and MaterialLibrary else set())
                    if set(Specification) != Expected:
                        raise ValueError('Unexpected scattering fields')
                    for Name in Names:
                        if type(Specification[Name]) not in (int, float) or not math.isfinite(Specification[Name]):
                            raise ValueError('Scattering coefficients must be finite numbers')
                    if type(Specification['Geometry']) is not int or type(Specification['Mode']) is not int or Specification['Geometry'] not in (0, 1) or Specification['Mode'] not in ((0, 1, 2, 3) if MaterialLibrary else (0, 1, 2)):
                        raise ValueError('Unknown geometry or shading selection')
                    with Lock:
                        if Native.poll() is not None:
                            raise RuntimeError('Vulkan executable has stopped')
                        if Specification['Mode'] == 3:
                            Text = Specification['Material']
                            if not isinstance(Text, str) or len(Text.encode()) > 900000:
                                raise ValueError('Material source must be bounded TOML text')
                            (Captures / 'Current.material').write_text(Text, encoding='utf-8')
                        Native.stdin.write(' '.join(str(Specification[Name]) for Name in Names) + '\n')
                        Native.stdin.flush()
                        Response = Responses.get(timeout=60).split()
                        if Response[0] == 'ERROR':
                            raise ValueError(' '.join(Response[1:]))
                        if Response[0] == 'REJECTED':
                            raise ValueError('Scattering coefficients outside renderer limits')
                        if Response[0] != 'CAPTURE' or len(Response) not in (4, 5):
                            raise RuntimeError('Vulkan capture failed')
                        Header, Extent, Maximum, Pixels = (Captures / 'Current.ppm').read_bytes().split(b'\n', 3)
                        Width, Height = map(int, Extent.split())
                        if Header != b'P6' or Maximum != b'255' or len(Pixels) != Width * Height * 3:
                            raise RuntimeError('Incomplete device readback')
                        Encoded = EncodePng(Width, Height, Pixels)
                    self.SendBytes(Encoded, 'image/png', Headers={'X-Frontier-Pipelines': Response[3],
                        'X-Frontier-Geometry-Uploads': Response[1] + '/' + Response[2],
                        'X-Frontier-Texture-Uploads': Response[4] if len(Response) == 5 else '0'})
                except (ValueError, KeyError, TypeError) as Failure:
                    self.SendBytes(str(Failure).encode(), 'text/plain', 400)
                except (RuntimeError, OSError, queue.Empty) as Failure:
                    self.SendBytes(('Vulkan readback unavailable: ' + str(Failure)).encode(), 'text/plain', 503)

        Server = ThreadingHTTPServer(('0.0.0.0', Arguments.port), BrowserResponse)
        print(f'Native evidence review + software Vulkan readbacks on port {Arguments.port}', flush=True)
        try:
            Server.serve_forever()
        finally:
            Server.server_close()
    finally:
        Native.stdin.close()
        try:
            Native.wait(timeout=5)
        except subprocess.TimeoutExpired:
            Native.kill()
            Native.wait()


if __name__ == '__main__':
    Main()
