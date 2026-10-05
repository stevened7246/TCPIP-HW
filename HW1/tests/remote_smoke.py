"""Smoke test a running chat server, or start a temporary server for local testing.
Uses real TCP clients and a valid generated PNG; it does not automate the GUI.
Remote runs create one test room (the protocol has no room deletion).
"""
import argparse
import contextlib
import hashlib
import json
import pathlib
import select
import socket
import struct
import subprocess
import time
import uuid
import zlib


def wire(kind, *fields):
    body = b''.join(struct.pack('!I', len(f)) + f for f in fields)
    return struct.pack('!IHHI', 0x43484154, 1, kind, len(body)) + body


def exact(sock, count):
    data = bytearray()
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise RuntimeError('Disconnected during packet')
        data.extend(part)
    return bytes(data)


def receive(sock):
    magic, version, kind, size = struct.unpack('!IHHI', exact(sock, 12))
    if (magic, version) != (0x43484154, 1) or size > 1024 * 1024 + 4096:
        raise RuntimeError('Invalid header')
    body, fields, offset = exact(sock, size), [], 0
    while offset < len(body):
        if len(body) - offset < 4 or len(fields) >= 16:
            raise RuntimeError('Invalid field header')
        length = struct.unpack_from('!I', body, offset)[0]
        offset += 4
        if length > len(body) - offset:
            raise RuntimeError('Invalid field length')
        fields.append(body[offset:offset + length])
        offset += length
    return kind, fields


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def request(sock, kind, *fields, expected=100):
    sock.sendall(wire(kind, *fields))
    actual, response = receive(sock)
    check(actual == expected, f'Request {kind}: expected {expected}, got {actual}: {response[:1]!r}')
    return response


def png_fixture():
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
    # Two rows of two RGB pixels; each row begins with PNG filter byte zero.
    rows = b'\0\xff\0\0\0\xff\0' + b'\0\0\0\xff\xff\xff\0'
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', 2, 2, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def run(host, port):
    suffix = uuid.uuid4().hex[:10]
    room = ('verify_' + suffix).encode()
    results = []
    with contextlib.ExitStack() as stack:
        clients = []
        names = []
        for letter in ('A', 'B', 'C'):
            sock = stack.enter_context(socket.create_connection((host, port), timeout=10))
            sock.settimeout(10)
            name = ('Test' + letter + suffix).encode()
            request(sock, 1, name)
            clients.append(sock)
            names.append(name)
        a, b, outsider = clients
        request(a, 3, room)
        check(room in request(a, 2, expected=102)[0].splitlines(), 'Room missing from list')
        request(a, 4, room)
        request(b, 4, room)
        results.append('login_create_list_join')
        for sender, receiver, name in ((a, b, names[0]), (b, a, names[1])):
            text = ('雙向中文訊息 ' + suffix).encode('utf-8')
            sender.sendall(wire(6, text))
            own = receive(sender)
            other = receive(receiver)
            check(own == other and own[0] == 103 and own[1][:3] == [room, name, text],
                  'Text broadcast mismatch')
        results.append('bidirectional_utf8_messages')
        image = png_fixture()
        for sender, receiver, name in ((a, b, names[0]), (b, a, names[1])):
            sender.sendall(wire(8, b'verification.png', image))
            own, other = receive(sender), receive(receiver)
            check(own == other and own[0] == 104 and own[1] == [room, name, b'verification.png', image],
                  'PNG bytes or metadata mismatch')
        results.append('bidirectional_real_png_byte_integrity')
        check(not select.select([outsider], [], [], 0.5)[0], 'Message/file leaked to another room')
        results.append('message_and_image_room_isolation')
        a.sendall(wire(7))
        for _ in range(2):
            kind, fields = receive(a)
            check(kind == 103 and fields[0] == room, 'History missing')
        check(receive(a)[0] == 100, 'History completion missing')
        results.append('text_history')
        request(b, 5)
        request(b, 6, b'cannot send outside room', expected=101)
        request(b, 8, b'verification.png', image, expected=101)
        request(b, 4, room)
        request(b, 6, b'rejoined', expected=103)
        check(receive(a)[1][2] == b'rejoined', 'Rejoined user cannot send')
        results.append('leave_reject_send_rejoin')
    return {'host': host, 'port': port, 'passed': results, 'png_sha256': hashlib.sha256(image).hexdigest(),
            'scope': 'TCP protocol test; GUI visual and cross-device checks are separate'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='Numeric server IPv4')
    parser.add_argument('--port', type=int, default=9000)
    parser.add_argument('--start-server', type=pathlib.Path, help='Local executable; starts and stops a temporary server')
    args = parser.parse_args()
    socket.inet_pton(socket.AF_INET, args.host)
    server = None
    try:
        if args.start_server:
            server = subprocess.Popen([str(args.start_server.resolve()), str(args.port), args.host],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError(server.stderr.read().decode(errors='replace'))
                try:
                    with socket.create_connection((args.host, args.port), timeout=.2):
                        break
                except OSError:
                    time.sleep(.05)
            else:
                raise RuntimeError('Server startup timed out')
        print(json.dumps(run(args.host, args.port), ensure_ascii=True, indent=2))
    finally:
        if server is not None:
            try:
                out, err = server.communicate(b'/quit\n', timeout=15)
            except subprocess.TimeoutExpired:
                server.kill()
                server.communicate()
                raise RuntimeError('Temporary server failed to stop')
            if server.returncode:
                raise RuntimeError(f'Server exited with {server.returncode}: {err.decode(errors="replace")}')


if __name__ == '__main__':
    main()
