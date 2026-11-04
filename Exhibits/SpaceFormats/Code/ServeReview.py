#============================================================================================================================================
#                                                            SERVEREVIEW.PY
#============================================================================================================================================
# 🧩 Serve the existing native-evidence HTML and exhibit files without an interactive native process or repository-private paths.

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import mimetypes
from pathlib import Path
from urllib.parse import unquote, urlsplit

Root = Path(__file__).resolve().parents[3]
Exhibits = Root / 'Exhibits'


class EvidenceRequests(BaseHTTPRequestHandler):
    def do_GET(self):
        Route = unquote(urlsplit(self.path).path)
        if Route in ['/', '/index.html']:
            File = Exhibits / 'SpaceFormats/index.html'
        else:
            File = (Root / Route.lstrip('/')).resolve()
        if not File.is_relative_to(Exhibits) or not File.is_file() or any(Part.startswith('.') for Part in File.relative_to(Exhibits).parts):
            self.send_error(404)
            return
        Payload = File.read_bytes()
        self.send_response(200)
        self.send_header('Content-Type', mimetypes.guess_type(str(File))[0] or 'application/octet-stream')
        self.send_header('Content-Length', str(len(Payload)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        try:
            self.wfile.write(Payload)
        except (BrokenPipeError, ConnectionResetError):
            return


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--port', type=int, default=8765)
    Arguments = Parser.parse_args()
    print('Native GIF evidence review on 0.0.0.0:' + str(Arguments.port), flush=True)
    ThreadingHTTPServer(('0.0.0.0', Arguments.port), EvidenceRequests).serve_forever()
