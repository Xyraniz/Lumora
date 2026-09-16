#!/usr/bin/env python3
"""Contract server for the executor `request` API.

The server binds and listens BEFORE spawning the Lumora client, which removes
the startup race that used to be papered over with `sleep 0.1` in the shell
wrapper. On macOS a connection refused mid-connect makes libcurl spin until
the full timeout instead of failing fast (curl/curl#7595), so the old race
surfaced as a 2s hang on CI runners whose python3 cold start exceeds the
sleep. Binding first makes the race unwinnable, and the client's exit code
propagates as the server's exit code.
"""
import subprocess
import sys
import threading
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


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: executor_request_server.py <lumora-binary> <contract-script>")
    vm, contract = sys.argv[1], sys.argv[2]
    server = HTTPServer(("127.0.0.1", 19994), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    # The port is listening at this point; the client cannot lose the race.
    result = subprocess.run([vm, contract])
    server.shutdown()
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
