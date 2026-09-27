import http.server, socketserver, os, urllib.parse
PORT=8001
DIR=os.path.join(os.path.dirname(__file__), "app")
os.chdir(DIR)
class H(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        p=urllib.parse.urlparse(self.path).path
        if p=="/": p="/skeleton.html"
        # strip query
        self.path=p
        return super().do_GET()
    def end_headers(self):
        self.send_header("Cache-Control","no-store")
        super().end_headers()
socketserver.TCPServer.allow_reuse_address=True
with socketserver.TCPServer(("0.0.0.0",PORT), H) as httpd:
    print(f"Serving {DIR} at :{PORT} -> / => /skeleton.html")
    httpd.serve_forever()
