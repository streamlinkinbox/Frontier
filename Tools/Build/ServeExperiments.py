#!/usr/bin/env python3
"""Serve the repository for the browser experiments, with caching made impossible.

    python3 Tools/Build/ServeExperiments.py [--port 8099]

🔴 WHY THIS EXISTS RATHER THAN `python3 -m http.server`, AND WHY NO-STORE WAS NOT ENOUGH.

   The plain handler sends no Cache-Control at all. A browser meeting a response with no freshness
   information applies a heuristic and may reuse it WITHOUT revalidating. These pages are ES modules
   that import each other, so the browser will cheerfully load a new index.html against an app.js
   from five minutes ago, and the failure is silent: new markup meets old script, a renamed control
   resolves to null, one exception kills the boot, and the result is a black canvas with no message.

   Sending `no-store` fixes the NEXT response. It does nothing about the copy already sitting in the
   cache, which stays fresh on its own terms and is reused on an ordinary reload — so the first load
   after the fix still runs the stale module, and the stack trace points at line numbers that no
   longer exist. That is a genuinely confusing half hour, and it happened here.

   The fix that does not depend on anyone remembering to hard-refresh: VERSION EVERY MODULE URL.
   Each relative import is rewritten to carry its target's modification time, so a changed file is a
   different URL and the browser cannot answer it from a cache. Edit one module and only that module
   is re-fetched; edit none and nothing is.

   Also corrects the MIME type for .mjs, which the standard handler does not know and which makes a
   module script fail to load outright.

   This is a DEVELOPMENT server. It rewrites what it serves, which is exactly what a production
   server must never do.
"""
import argparse
import functools
import http.server
import mimetypes
import re
import socketserver
import urllib.parse
from pathlib import Path

Root = Path(__file__).resolve().parents[2]

# Where "/" sends you. Set by --landing; empty means serve the ordinary directory listing.
Landing = ''

# 🔴 The content types are spelled out here rather than read back out of extensions_map. That map
#    holds only the handful of overrides the base class declares — everything else, .html included,
#    comes from the mimetypes module — so indexing it for a served file raises KeyError on the most
#    ordinary request there is. It did.
Rewritable = {
    '.html': 'text/html',
    '.js': 'text/javascript',
    '.mjs': 'text/javascript',
}

# A relative module specifier in an import statement, in any of the three forms these pages use.
Specifiers = [
    re.compile(r'''(?P<lead>\bfrom\s+)(?P<quote>['"])(?P<path>\.{1,2}/[^'"?]+)(?P=quote)'''),
    re.compile(r'''(?P<lead>\bimport\s+)(?P<quote>['"])(?P<path>\.{1,2}/[^'"?]+)(?P=quote)'''),
    re.compile(r'''(?P<lead>\bimport\(\s*)(?P<quote>['"])(?P<path>\.{1,2}/[^'"?]+)(?P=quote)'''),
]

# src= / href= on a local file in markup. Absolute URLs and anything already carrying a query are
# left alone.
Attributes = re.compile(
    r'''(?P<lead>\b(?:src|href)\s*=\s*)(?P<quote>["'])(?P<path>(?!https?:|//|data:|#)[^"'?]+)(?P=quote)''')


def Token(target: Path) -> str:
    try:
        return str(int(target.stat().st_mtime))
    except OSError:
        return '0'


def Version(text: str, directory: Path, markup: bool) -> str:
    def Replace(match):
        relative = match.group('path')
        resolved = (directory / relative).resolve()
        if not resolved.is_file():
            return match.group(0)
        quote = match.group('quote')
        return f"{match.group('lead')}{quote}{relative}?v={Token(resolved)}{quote}"

    for pattern in (Attributes,) if markup else Specifiers:
        text = pattern.sub(Replace, text)
    return text


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        '.mjs': 'text/javascript',
        '.js': 'text/javascript',
        '.wgsl': 'text/plain',
    }

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

    def do_GET(self):
        # 🔴 The repository root is a wall of forty folders with no hint that an experiment is in
        #    there. A preview opens at "/", so "/" has to be the thing you came to look at — the
        #    landing path is configuration, and without one the server is technically correct and
        #    practically useless. This cost a round trip of "I don't see it".
        if Landing and urllib.parse.urlparse(self.path).path == '/':
            self.send_response(302)
            self.send_header('Location', Landing)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        served = self.Rewrite()
        if served is None:
            super().do_GET()

    def do_HEAD(self):
        # The rewrite changes the length, so a HEAD that reported the file's own size would lie.
        if self.Rewrite(body=False) is None:
            super().do_HEAD()

    def Rewrite(self, body=True):
        """Serve a rewritten text file, or return None to let the standard handler take it."""
        path = urllib.parse.urlparse(self.path).path
        local = Path(self.translate_path(path))
        suffix = local.suffix.lower()
        if local.is_dir() or suffix not in Rewritable or not local.is_file():
            return None

        try:
            text = local.read_text(encoding='utf-8')
        except (OSError, UnicodeDecodeError):
            return None

        text = Version(text, local.parent, markup=suffix == '.html')
        payload = text.encode('utf-8')

        self.send_response(200)
        self.send_header('Content-Type', f'{Rewritable[suffix]}; charset=utf-8')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        if body:
            self.wfile.write(payload)
        return True

    def log_message(self, form, *arguments):
        line = form % arguments
        if '200' not in line:
            super().log_message(form, *arguments)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--port', type=int, default=8099)
    Parser.add_argument('--bind', default='0.0.0.0')
    Parser.add_argument('--landing', default='/Experimental/SpatialHud/index.html',
                        help='where "/" redirects; pass an empty string for a directory listing')
    Arguments = Parser.parse_args()
    Landing = Arguments.landing

    with Server((Arguments.bind, Arguments.port),
                functools.partial(Handler, directory=str(Root))) as Listening:
        print(f'ServeExperiments: {Root} on {Arguments.bind}:{Arguments.port} — '
              f'no-store, and every module URL versioned by mtime', flush=True)
        if Landing:
            print(f'ServeExperiments: / redirects to {Landing}', flush=True)
        Listening.serve_forever()
