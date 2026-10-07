"""Serve the production URL prefix locally; never expose the repository itself."""
import http.server
from urllib.parse import urlsplit
import build


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(build.OUT), **kwargs)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == '/':
            self.send_response(302)
            self.send_header('Location', build.PREFIX + '/')
            self.end_headers()
            return
        if not path.startswith(build.PREFIX + '/'):
            self.send_error(404)
            return
        self.path = self.path[len(build.PREFIX):]
        super().do_GET()


if __name__ == '__main__':
    print('D3D preview: http://127.0.0.1:4173' + build.PREFIX + '/', flush=True)
    http.server.ThreadingHTTPServer(('127.0.0.1', 4173), Handler).serve_forever()
