#!/usr/bin/env python3
import base64, hashlib, socket, sys
mode, port = sys.argv[1], int(sys.argv[2])
with socket.socket() as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port)); s.listen(1)
    c, _ = s.accept()
    with c:
        data = c.recv(8192)
        if mode == "http":
            c.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello")
        else:
            text = data.decode("latin1")
            key = next(line.split(":", 1)[1].strip() for line in text.split("\r\n") if line.lower().startswith("sec-websocket-key:"))
            accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
            c.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())
            frame = c.recv(1024); length = frame[1] & 127; pos = 2
            mask = frame[pos:pos+4]; pos += 4
            payload = bytes(frame[pos+i] ^ mask[i % 4] for i in range(length))
            reply = b"pong:" + payload
            c.sendall(bytes([0x81, len(reply)]) + reply)
