#!/usr/bin/env python3
"""
Small HTTP server to serve this workspace and map root to mpmc_animation.html.
Usage: python3 serve_animation.py --port 8000 --open
"""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import argparse
import os
import webbrowser


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        # allow access from local pages
        self.send_header('Access-Control-Allow-Origin', '*')
        SimpleHTTPRequestHandler.end_headers(self)

    def do_GET(self):
        # If requesting root, serve the animation file
        if self.path in ('', '/'):
            self.path = '/mpmc_animation.html'
        return super().do_GET()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', '-p', type=int, default=8000)
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--open', action='store_true', help='Open browser to the animation')
    args = parser.parse_args()

    cwd = os.getcwd()
    os.chdir(cwd)
    addr = (args.host, args.port)
    httpd = ThreadingHTTPServer(addr, Handler)
    url = f'http://127.0.0.1:{args.port}/'
    print(f'Serving {cwd} at {url} (press Ctrl-C to stop)')
    if args.open:
        try:
            webbrowser.open(url)
        except Exception:
            pass
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print('\nShutting down')
        httpd.server_close()


if __name__ == '__main__':
    main()
