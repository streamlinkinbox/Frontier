#============================================================================================================================================
#                                                         SERVETEXTURESTUDIO.PY
#============================================================================================================================================
# 🧩 Serve the pinned self-contained TextureStudio build without modifying its source or requiring runtime CDNs.

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

Repository = Path(__file__).resolve().parents[3]
Studio = Repository / 'Experimental/TextureStudio/site'


class TextureStudioRequests(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ('/', '/index.html'):
            self.send_response(302)
            self.send_header('Location', '/?studio=texture')
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


if __name__ == '__main__':
    Requests = partial(TextureStudioRequests, directory=str(Studio))
    print('Pinned TextureStudio 773b2f5; texture workspace selected; port 8774', flush=True)
    ThreadingHTTPServer(('0.0.0.0', 8774), Requests).serve_forever()
