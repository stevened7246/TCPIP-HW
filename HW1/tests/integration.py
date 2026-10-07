"""Black-box tests against the actual Winsock executables (stdlib only)."""
import concurrent.futures
# 使用本機暫時伺服器，驗證登入、房間隔離、分段封包與各用戶端。
import pathlib
import queue
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

SERVER = pathlib.Path(sys.argv[1]).resolve()
CLIENT = pathlib.Path(sys.argv[2]).resolve()
GUI_TEST = pathlib.Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
del sys.argv[1:]

def wire(kind, *fields):
    # 與 C++ 相同：12-byte 標頭，各欄位為 4-byte 長度加原始內容。
    body = b''.join(struct.pack('!I', len(f)) + f for f in fields)
    return struct.pack('!IHHI', 0x43484154, 1, kind, len(body)) + body

def exact(s, count):
    data = b''
    while len(data) < count:
        part = s.recv(count - len(data))
        if not part:
            raise EOFError('disconnected')
        data += part
    return data

def receive(s):
    magic, version, kind, length = struct.unpack('!IHHI', exact(s, 12))
    assert (magic, version) == (0x43484154, 1)
    body, fields = exact(s, length), []
    while body:
        length = struct.unpack('!I', body[:4])[0]
        fields.append(body[4:4+length])
        body = body[4+length:]
    return kind, fields

class Integration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # 請系統選可用連接埠，啟動伺服器後輪詢直到能連線。
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            cls.port = probe.getsockname()[1]
        cls.server = subprocess.Popen([str(SERVER), str(cls.port)], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for _ in range(100):
            try:
                with socket.create_connection(('127.0.0.1', cls.port), .2):
                    return
            except OSError:
                if cls.server.poll() is not None:
                    raise RuntimeError(cls.server.stderr.read().decode())
                time.sleep(.05)
        raise RuntimeError('Server startup timed out')

    @classmethod
    def tearDownClass(cls):
        out, err = cls.server.communicate(b'/quit\n', timeout=15)
        if cls.server.returncode:
            raise AssertionError((out, err))

    def connect(self, name=None):
        s = socket.create_connection(('127.0.0.1', self.port), 3)
        s.settimeout(3)
        self.addCleanup(s.close)
        if name:
            self.request(s, 1, name.encode())
        return s

    def request(self, s, kind, *fields, expected=100):
        s.sendall(wire(kind, *fields))
        result = receive(s)
        self.assertEqual(result[0], expected, result)
        return result[1]

    def test_auth_and_disconnect_cleanup(self):
        a = self.connect('unique')
        b = self.connect()
        self.request(b, 2, expected=101)
        self.request(b, 1, b'unique', expected=101)
        self.request(b, 1, b'bad name', expected=101)
        a.close()
        for _ in range(30):
            b.sendall(wire(1, b'unique'))
            if receive(b)[0] == 100:
                break
            time.sleep(.05)
        else:
            self.fail('Nickname not released on disconnect')

    def test_rooms_history_isolation(self):
        a, b = self.connect('room_a'), self.connect('room_b')
        self.request(a, 3, b'private')
        self.request(a, 4, b'private')
        self.request(a, 6, '中文訊息'.encode(), expected=103)
        b.settimeout(.2)
        with self.assertRaises(socket.timeout):
            receive(b)
        b.settimeout(3)
        self.request(b, 4, b'private')
        a.sendall(wire(6, b'hello'))
        self.assertEqual(receive(a), receive(b))
        a.sendall(wire(7))
        self.assertEqual(receive(a)[1][2], '中文訊息'.encode())
        self.assertEqual(receive(a)[1][2], b'hello')
        self.assertEqual(receive(a)[0], 100)
        self.assertIn(b'private', self.request(a, 2, expected=102)[0])
        self.request(a, 5)
        self.request(a, 6, b'no room', expected=101)
        self.request(a, 4, b'missing', expected=101)

    def test_fragmented_and_coalesced_frames(self):
        # 逐 byte 傳送及一次傳多包，驗證解析不依賴單次 recv 的分段方式。
        s = self.connect()
        for byte in wire(1, b'fragment'):
            s.sendall(bytes([byte]))
        self.assertEqual(receive(s)[0], 100)
        s.sendall(wire(2) + wire(3, b'coalesced') + wire(4, b'coalesced'))
        self.assertEqual([receive(s)[0] for _ in range(3)], [102, 100, 100])

    def test_binary_file_and_validation(self):
        # 傳送含所有 byte 值的 1 MiB 資料，比對內容，再驗證大小與檔名限制。
        a, b = self.connect('file_a'), self.connect('file_b')
        self.request(a, 3, b'files')
        self.request(a, 4, b'files')
        self.request(b, 4, b'files')
        content = bytes(range(256)) * 4096
        a.sendall(wire(8, b'data.bin', content))
        packet = receive(a)
        self.assertEqual(packet[0], 104)
        self.assertEqual(packet[1][3], content)
        self.assertEqual(receive(b), packet)
        self.request(a, 8, b'../bad', b'x', expected=101)
        self.request(a, 8, b'large', content + b'x', expected=101)
        self.request(a, 6, b'x' * 4097, expected=101)

    def test_malformed_frames(self):
        # 錯 magic、版本、payload 或欄位長度應斷線，伺服器仍需能服務新連線。
        cases = [struct.pack('!IHHI', 0, 1, 1, 0),
                 struct.pack('!IHHI', 0x43484154, 2, 1, 0),
                 struct.pack('!IHHI', 0x43484154, 1, 1, 0xffffffff),
                 struct.pack('!IHHI', 0x43484154, 1, 1, 4) + struct.pack('!I', 100)]
        for data in cases:
            with self.subTest(data=data):
                s = self.connect()
                s.sendall(data)
                try:
                    self.assertEqual(s.recv(1), b'')
                except ConnectionResetError:
                    pass
        s = self.connect('still_alive')
        self.request(s, 999, expected=101)

    def test_concurrent_broadcast(self):
        # 8 個發送者同時傳送，每個接收者都應收到完整且不交錯的 8 則訊息。
        clients = [self.connect('parallel_' + str(i)) for i in range(8)]
        self.request(clients[0], 3, b'parallel')
        for s in clients:
            self.request(s, 4, b'parallel')
        with concurrent.futures.ThreadPoolExecutor(8) as executor:
            list(executor.map(lambda pair: pair[1].sendall(wire(6, str(pair[0]).encode())), enumerate(clients)))
        for s in clients:
            packets = [receive(s) for _ in clients]
            self.assertTrue(all(p[0] == 103 for p in packets))
            self.assertEqual({p[1][2] for p in packets}, {str(i).encode() for i in range(8)})

    def test_native_client(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory, 'sample.bin')
            source.write_bytes(b'\x00\xffreal-client\n')
            process = subprocess.Popen([str(CLIENT), 'native', '127.0.0.1', str(self.port)],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       cwd=directory, text=True, encoding='utf-8')
            lines = queue.Queue()
            reader = threading.Thread(target=lambda: [lines.put(line) for line in process.stdout], daemon=True)
            reader.start()
            def until(text):
                end = time.monotonic() + 5
                while time.monotonic() < end:
                    line = lines.get(timeout=max(.01, end-time.monotonic()))
                    if text in line:
                        return
                self.fail('Client output missing: ' + text)
            def command(text):
                process.stdin.write(text + '\n')
                process.stdin.flush()
            try:
                until('Logged in')
                command('/create native_room'); until('Room created')
                command('/join native_room'); until('Joined native_room')
                command('native hello'); until('native: native hello')
                command('/file ' + str(source)); until('File from native')
                downloaded = list(pathlib.Path(directory, 'downloads').glob('*.bin'))
                self.assertEqual(len(downloaded), 1)
                self.assertEqual(downloaded[0].read_bytes(), source.read_bytes())
                command('/quit')
                self.assertEqual(process.wait(timeout=5), 0)
            finally:
                if process.poll() is None:
                    process.kill(); process.wait()
                reader.join(timeout=2)
                process.stdin.close(); process.stdout.close()

    def test_shutdown_with_partial_packet(self):
        # 刻意只傳部分標頭，確認停止伺服器能解除阻塞 recv 並正常退出。
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        process = subprocess.Popen([str(SERVER), str(port)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        clients = []
        try:
            for _ in range(100):
                try:
                    clients.append(socket.create_connection(('127.0.0.1', port), .2))
                    break
                except OSError:
                    time.sleep(.02)
            self.assertEqual(len(clients), 1)
            clients[0].settimeout(3)
            clients.append(socket.create_connection(('127.0.0.1', port), 2))
            clients[0].sendall(wire(1, b'waiting'))
            self.assertEqual(receive(clients[0])[0], 100)
            clients[1].sendall(wire(1, b'incomplete')[:7])
            process.communicate(b'/quit\n', timeout=5)
            self.assertEqual(process.returncode, 0)
            self.assertEqual(clients[0].recv(1), b'')
        finally:
            for client in clients:
                client.close()
            if process.poll() is None:
                process.kill(); process.communicate()

    @unittest.skipIf(GUI_TEST is None, 'GUI transport test executable not supplied')
    def test_gui_transport(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([str(GUI_TEST), str(self.port), directory],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_bind_error_exits(self):
        process = subprocess.Popen([str(SERVER), str(self.port)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.assertEqual(process.wait(timeout=5), 1)
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            process.communicate()

if __name__ == '__main__':
    unittest.main(verbosity=2)
