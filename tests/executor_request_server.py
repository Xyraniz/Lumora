#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8")
        if self.path != "/hello" or self.headers.get("X-Lumora") != "compatibility" or body != "request-body":
            self.send_response(400, "Bad Request")
            self.end_headers()
            self.wfile.write(b"bad-request")
            return
        self.send_response(201, "Created")
        self.send_header("X-Request-Method", "POST")
        self.send_header("X-Request-Body", body)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"local-request-ok")

    def log_message(self, format, *args):
        return


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", 19994), Handler).handle_request()
