#!/usr/bin/env python3
"""Contract server for the native @lumora/net module.

The server binds and listens BEFORE spawning the Lumora client, which removes
the startup race that used to be papered over with `sleep 0.1` in the shell
wrapper. On macOS a connection refused mid-connect makes the client's connect
path fail with "Could not connect to server" when the runner's python3 cold
start exceeds the sleep, so binding first makes the race unwinnable. The
client's exit code propagates as the server's exit code.
"""
import base64
import hashlib
import socket
import subprocess
import sys


def serve_http(conn):
    conn.recv(8192)
    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello")


def serve_websocket(conn):
    data = conn.recv(8192)
    text = data.decode("latin1")
    key = next(
        line.split(":", 1)[1].strip()
        for line in text.split("\r\n")
        if line.lower().startswith("sec-websocket-key:")
    )
    accept = base64.b64encode(
        hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
    ).decode()
    conn.sendall(
        (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n"
        ).encode()
    )
    frame = conn.recv(1024)
    length = frame[1] & 127
    pos = 2
    mask = frame[pos : pos + 4]
    pos += 4
    payload = bytes(frame[pos + i] ^ mask[i % 4] for i in range(length))
    reply = b"pong:" + payload
    conn.sendall(bytes([0x81, len(reply)]) + reply)


def main():
    if len(sys.argv) != 5:
        sys.exit("usage: native_server.py <mode> <port> <lumora-binary> <contract-script>")
    mode, port, vm, contract = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]

    with socket.socket() as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", port))
        s.listen(1)
        # The port is listening at this point; the client cannot lose the race.
        client = subprocess.Popen([vm, contract, mode])
        try:
            conn, _ = s.accept()
            with conn:
                if mode == "http":
                    serve_http(conn)
                else:
                    serve_websocket(conn)
        finally:
            code = client.wait()
    sys.exit(code)


if __name__ == "__main__":
    main()
