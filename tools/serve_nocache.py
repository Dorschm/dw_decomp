#!/usr/bin/env python3
"""Static file server with caching disabled, for testing web builds
(plain http.server lets the browser reuse a stale dw.wasm).

Usage: serve_nocache.py [port] [root]"""
import http.server
import os
import re
import sys
from functools import partial


class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def do_POST(self):
        """POST /__shot/<name>.png -> <root>/dw_decomp/.tmp_test/shots/
        (screenshots from tools/web_drive.js)"""
        if not self.path.startswith('/__shot/'):
            self.send_error(404)
            return
        name = re.sub(r'[^A-Za-z0-9_.-]', '_', self.path[8:])[:80] or 'shot.png'
        data = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        d = os.path.join(self.directory, 'dw_decomp', '.tmp_test', 'shots')
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, name), 'wb') as f:
            f.write(data)
        self.send_response(204)
        self.end_headers()


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
root = sys.argv[2] if len(sys.argv) > 2 else '.'
http.server.ThreadingHTTPServer(
    ('', port), partial(NoCache, directory=root)).serve_forever()
