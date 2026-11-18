"""Adversarial local broker: run the real C transport, never the board/server."""
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time

exe = Path(__file__).resolve().parent / 'output/test_transport.exe'
def exact(s, n):
    result = b''
    while len(result) < n:
        chunk = s.recv(n-len(result))
        if not chunk: raise EOFError('client disconnected')
        result += chunk
    return result
def packet(s):
    first = exact(s, 1)[0]
    length, mul = 0, 1
    while True:
        b = exact(s, 1)[0]
        length += (b & 127) * mul
        if b < 128: break
        mul *= 128
    return first >> 4, exact(s, length)
def publish(pid=77, qos=1):
    body = b'\x00\x0ademo/value' + (struct.pack('!H', pid) if qos else b'') + b'test'
    return bytes([0x30 | qos << 1, len(body)]) + body
def serve(listener, mode, errors):
    try:
        with listener.accept()[0] as s:
            s.settimeout(10)
            kind, body = packet(s); assert kind == 1
            if mode == 'refusal': s.sendall(b'\x20\x02\x00\x05'); return
            # Deliberately split CONNACK across TCP reads.
            for b in b'\x20\x02\x00\x00': s.sendall(bytes([b])); time.sleep(.01)
            kind, body = packet(s); assert kind == 8
            pid = body[:2]
            s.sendall(b'\x90\x03' + pid + (b'\x80' if mode == 'subfail' else b'\x01'))
            if mode == 'subfail': return
            if mode == 'normal':
                kind, body = packet(s); assert kind == 3
                topic_size = struct.unpack('!H', body[:2])[0]
                pid = body[2+topic_size:4+topic_size]
                # An inbound command before PUBACK must not break an outgoing publish.
                s.sendall(publish() + b'\x40\x02' + pid + publish(qos=0))
                kind, body = packet(s); assert kind == 4 and body == b'\x00M'
                time.sleep(.1)
            elif mode == 'oversize': s.sendall(b'\x30\x80\x20')
            elif mode == 'badlength': s.sendall(b'\x30\xff\xff\xff\xff\x01')
            elif mode == 'truncated': s.sendall(b'\x30\x20\x00'); s.shutdown(socket.SHUT_WR)
            elif mode == 'short-qos1': s.sendall(b'\x32\x02\x00\x00')
            elif mode == 'missing-puback':
                # Server closes without a PUBACK; the client must not report success.
                kind, body = packet(s); assert kind == 3
                s.shutdown(socket.SHUT_WR)
            else: raise AssertionError(mode)
            time.sleep(.15)
    except Exception as e: errors.append(repr(e))
    finally: listener.close()

for mode in ['normal','refusal','subfail','oversize','badlength','truncated','short-qos1','missing-puback']:
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0)); listener.listen(1)
    port = listener.getsockname()[1]
    errors = []
    worker = threading.Thread(target=serve, args=(listener, mode, errors), daemon=True); worker.start()
    result = subprocess.run([str(exe),str(port),mode], capture_output=True, text=True, timeout=15)
    worker.join(2)
    if result.returncode or errors:
        raise SystemExit(f'FAIL {mode}: rc={result.returncode} {result.stderr} {errors}')
    print('PASS', mode)
print('Transport regression passed: 8 broker scenarios')
