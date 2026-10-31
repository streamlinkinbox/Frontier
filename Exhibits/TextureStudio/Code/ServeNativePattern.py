#============================================================================================================================================
#                                                         SERVENATIVEPATTERN.PY
#============================================================================================================================================
# 🧩 Relay bounded pointer/key input to real C++ ImGui and return its captured triangles with software-Vulkan canvas pixels.

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import queue
import struct
import subprocess
import threading
from urllib.parse import urlsplit
import zlib

Root = Path(__file__).resolve().parents[3]


def EncodePng(Pathname):
    Header, Extent, Maximum, Pixels = Pathname.read_bytes().split(b'\n', 3)
    Width, Height = map(int, Extent.split())
    if Header != b'P6' or Maximum != b'255' or len(Pixels) != Width * Height * 3:
        raise RuntimeError('Incomplete native capture')
    def Chunk(Category, Payload):
        return struct.pack('>I', len(Payload)) + Category + Payload + struct.pack('>I', zlib.crc32(Category + Payload))
    Rows = b''.join(b'\0' + Pixels[Offset:Offset + Width * 3] for Offset in range(0, len(Pixels), Width * 3))
    return b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', Width, Height, 8, 2, 0, 0, 0)) + Chunk(b'IDAT', zlib.compress(Rows, 1)) + Chunk(b'IEND', b'')


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--port', type=int, default=8776)
    Parser.add_argument('--executable', type=Path, required=True)
    Parser.add_argument('--driver', type=Path, required=True)
    Parser.add_argument('--shader', type=Path, required=True)
    Parser.add_argument('--font', type=Path, default=Root / 'EngineContent/FontArchives/DMSans/DMSans-Regular.ttf')
    Parser.add_argument('--captures', type=Path, default=Root / '_ClaudeScratch/build/NativePatternStream')
    Arguments = Parser.parse_args()
    Captures = Arguments.captures.resolve()
    Captures.mkdir(parents=True, exist_ok=True)
    Native = subprocess.Popen([str(Arguments.executable.resolve()), str(Arguments.driver.resolve()),
        str(Arguments.shader.resolve()), str(Arguments.font.resolve()), str(Captures), '--serve'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=Root)
    Responses = queue.Queue()
    def AccumulateResponses():
        for Line in Native.stdout:
            Responses.put(Line.strip())
        Responses.put('EXITED')
    threading.Thread(target=AccumulateResponses, daemon=True).start()
    Lock = threading.Lock()
    try:
        if Responses.get(timeout=60) != 'READY':
            raise RuntimeError('Native ImGui runner did not initialize')
        class NativeResponse(BaseHTTPRequestHandler):
            def Send(self, Payload, Mime, Status=200):
                self.send_response(Status)
                self.send_header('Content-Type', Mime)
                self.send_header('Content-Length', str(len(Payload)))
                self.send_header('Cache-Control', 'no-store')
                self.send_header('X-Content-Type-Options', 'nosniff')
                self.end_headers()
                try:
                    self.wfile.write(Payload)
                except (BrokenPipeError, ConnectionResetError):
                    return

            def do_GET(self):
                Route = urlsplit(self.path).path
                if Route == '/':
                    self.Send((Root / 'Exhibits/TextureStudio/NativePatternStream.html').read_bytes(), 'text/html; charset=utf-8')
                elif Route == '/frame.png':
                    with Lock:
                        self.Send(EncodePng(Captures / 'Current.ppm'), 'image/png')
                else:
                    self.send_error(404)

            def do_POST(self):
                if self.path != '/input':
                    self.send_error(404)
                    return
                Origin = self.headers.get('Origin')
                if self.headers.get('Sec-Fetch-Site', 'same-origin') not in ('same-origin', 'none') or (Origin and urlsplit(Origin).netloc != self.headers.get('Host')):
                    self.send_error(403)
                    return
                if self.headers.get('Content-Type') != 'application/json':
                    self.send_error(415)
                    return
                try:
                    Length = int(self.headers.get('Content-Length', '0'))
                    if not 1 <= Length <= 2048:
                        raise ValueError('Input payload exceeds bounds')
                    Specification = json.loads(self.rfile.read(Length))
                    if not isinstance(Specification, list) or len(Specification) != 11:
                        raise ValueError('Expected eleven native input coordinates')
                    if any(type(Position) not in (int, float) or not math.isfinite(Position) for Position in Specification):
                        raise ValueError('Input coordinates must be finite numbers')
                    X, Y, Buttons, Wheel, Ctrl, Shift, Alt, Key, Pressed, Focused, Character = Specification
                    if abs(X) > 10000 or abs(Y) > 10000 or abs(Wheel) > 20:
                        raise ValueError('Pointer coordinate exceeds bounds')
                    if any(type(Position) is not int or Position not in (0, 1) for Position in (Ctrl, Shift, Alt, Pressed, Focused)):
                        raise ValueError('Invalid input toggle')
                    if type(Buttons) is not int or not 0 <= Buttons <= 7 or type(Key) is not int or not 0 <= Key < 18:
                        raise ValueError('Unknown native key or pointer button')
                    if type(Character) is not int or not 0 <= Character <= 0x10ffff or 0xd800 <= Character <= 0xdfff:
                        raise ValueError('Invalid character scalar')
                    with Lock:
                        if Native.poll() is not None:
                            raise RuntimeError('Native runner has stopped')
                        Native.stdin.write(' '.join(str(Position) for Position in Specification) + '\n')
                        Native.stdin.flush()
                        Response = Responses.get(timeout=60).split()
                        if len(Response) != 4 or Response[0] != 'CAPTURE':
                            raise RuntimeError('Native capture failed')
                        self.Send(json.dumps({'serial': int(Response[1]), 'revision': int(Response[2]), 'layers': int(Response[3])}).encode(), 'application/json')
                except (ValueError, TypeError, json.JSONDecodeError) as Failure:
                    self.Send(str(Failure).encode(), 'text/plain', 400)
                except (RuntimeError, queue.Empty, BrokenPipeError) as Failure:
                    self.Send(str(Failure).encode(), 'text/plain', 503)

        Server = ThreadingHTTPServer(('0.0.0.0', Arguments.port), NativeResponse)
        print(f'Native ImGui pixel stream listening on 0.0.0.0:{Arguments.port}', flush=True)
        try:
            Server.serve_forever()
        finally:
            Server.server_close()
    finally:
        Native.terminate()
        try:
            Native.wait(timeout=5)
        except subprocess.TimeoutExpired:
            Native.kill()
            Native.wait()


if __name__ == '__main__':
    Main()
