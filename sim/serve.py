# Static dev server with caching disabled (so the preview always loads the latest JS/WGSL).
import http.server, functools, os
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, must-revalidate')
        self.send_header('Expires', '0')
        super().end_headers()
d = os.path.dirname(os.path.abspath(__file__))
http.server.ThreadingHTTPServer(('0.0.0.0', 8080), functools.partial(H, directory=d)).serve_forever()
