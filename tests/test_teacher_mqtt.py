"""Exercise the real Paho bridge against a local wire-level MQTT test broker.
No board, public broker or real SIoT service is used.
"""
import copy
import importlib.util
import json
from pathlib import Path
import queue
import socket
import struct
import threading
import time
import unittest

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('siot_teacher_live',root/'tools/teacher/control.py')
teacher = importlib.util.module_from_spec(spec); spec.loader.exec_module(teacher)


def frame(header, body):
    length, size = bytearray(), len(body)
    while True:
        digit = size % 128; size //= 128
        length.append(digit | (128 if size else 0))
        if not size: break
    return bytes([header])+bytes(length)+body


def exact(stream, count):
    result = b''
    while len(result) < count:
        chunk = stream.recv(count-len(result))
        if not chunk: raise EOFError()
        result += chunk
    return result


class Broker:
    def __init__(self, deny=False):
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1',0)); self.listener.listen(3); self.listener.settimeout(.2)
        self.port = self.listener.getsockname()[1]
        self.deny, self.stopping = deny, False
        self.stream = None
        self.lock = threading.Lock()
        self.subscriptions = []
        self.published = queue.Queue()
        self.worker = threading.Thread(target=self.run,daemon=True); self.worker.start()

    def send(self, packet):
        with self.lock:
            self.stream.sendall(packet)

    def inject(self, topic, payload, retained=False):
        topic = topic.encode()
        body = struct.pack('!H',len(topic))+topic+json.dumps(payload).encode()
        self.send(frame(0x31 if retained else 0x30,body))

    def run(self):
        while not self.stopping:
            try: connection, _ = self.listener.accept()
            except socket.timeout: continue
            except OSError: break
            with self.lock: self.stream = connection
            try:
                while not self.stopping:
                    header = exact(connection,1)[0]
                    size, multiplier = 0, 1
                    for _ in range(4):
                        digit = exact(connection,1)[0]; size += (digit&127)*multiplier
                        if digit < 128: break
                        multiplier *= 128
                    body = exact(connection,size)
                    kind = header >> 4
                    if kind == 1: self.send(b'\x20\x02\x00\x00')
                    elif kind == 8:
                        index, grants = 2, []
                        while index < len(body):
                            length = int.from_bytes(body[index:index+2],'big'); index += 2
                            self.subscriptions.append(body[index:index+length].decode()); index += length+1
                            grants.append(128 if self.deny else 1)
                        self.send(frame(0x90,body[:2]+bytes(grants)))
                    elif kind == 3:
                        length = int.from_bytes(body[:2],'big')
                        topic = body[2:2+length].decode()
                        self.published.put((topic,json.loads(body[4+length:])))
                        self.send(b'\x40\x02'+body[2+length:4+length])
                    elif kind == 12: self.send(b'\xd0\x00')
                    elif kind == 14: break
            except (EOFError,OSError): pass
            finally:
                with self.lock:
                    connection.close()
                    if self.stream is connection: self.stream = None

    def drop(self):
        with self.lock:
            if self.stream: self.stream.shutdown(socket.SHUT_RDWR)

    def close(self):
        self.stopping = True
        try: self.drop()
        except OSError: pass
        self.listener.close(); self.worker.join(2)


def wait_for(predicate, timeout=6):
    end = time.monotonic()+timeout
    while time.monotonic() < end:
        if predicate(): return
        time.sleep(.02)
    raise AssertionError('Timed out waiting for MQTT test condition')


class MqttTests(unittest.TestCase):
    def start(self, deny=False):
        broker = Broker(deny)
        data = json.loads((root/'tools/teacher/deployment.example.json').read_text(encoding='utf-8'))
        data['broker']['port'] = broker.port
        data['features'].update(group=True,broadcast=True)
        controller = teacher.Controller(teacher.Policy(data))
        bridge = teacher.MqttBridge(controller,'')
        controller.publisher = bridge.publish
        bridge.start()
        self.addCleanup(broker.close)
        self.addCleanup(bridge.close)
        return broker, controller

    def test_broadcast_puback_per_device_ack_and_reconnect(self):
        broker, controller = self.start()
        wait_for(lambda: controller.snapshot()['broker_ready'])
        self.assertEqual(set(broker.subscriptions), set(controller.topics))
        command = controller.send('all','','off')
        topic, payload = broker.published.get(timeout=2)
        self.assertEqual(topic,'demo_all/cmd')
        self.assertTrue(command['broker_accepted'])
        self.assertEqual(command['acks'],{})
        broker.inject('demo_dev_001/ack',{'id':payload['id'],'code':200},retained=True)
        broker.inject('demo_dev_001/state',{'temperature':23})
        wait_for(lambda: controller.snapshot()['devices']['001']['state'] is not None)
        self.assertEqual(controller.snapshot()['commands'][0]['acks'],{})
        broker.inject('demo_dev_001/ack',{'id':payload['id'],'code':200})
        broker.inject('demo_dev_002/ack',{'id':payload['id'],'code':400})
        wait_for(lambda: len(controller.snapshot()['commands'][0]['acks'])==2)
        self.assertNotIn('003',controller.snapshot()['commands'][0]['acks'])
        before = len(broker.subscriptions)
        broker.drop()
        wait_for(lambda: len(broker.subscriptions)>before and controller.snapshot()['broker_ready'])
        self.assertEqual(len(broker.subscriptions),before*2)

    def test_denied_subscription_prevents_publish(self):
        broker, controller = self.start(deny=True)
        wait_for(lambda: 'rejected' in controller.snapshot()['broker_error'])
        command = controller.send('device','001','off')
        self.assertFalse(command['broker_accepted'])
        self.assertTrue(broker.published.empty())


if __name__ == '__main__': unittest.main(verbosity=2)
