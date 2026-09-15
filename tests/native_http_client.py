#!/usr/bin/env python3
import socket
with socket.create_connection(("127.0.0.1", 19993), timeout=5) as s:
    s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
    data = s.recv(4096)
assert b"200 OK" in data and b"server-ok" in data
print("native HTTP server contract ok")
