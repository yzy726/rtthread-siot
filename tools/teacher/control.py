"""Configurable classroom controller. MQTT credentials stay on this host.

The web policy controls this application; broker ACLs are required to restrict
other MQTT clients. Importing this module or --check-config never connects.
"""
from __future__ import annotations
import argparse
import copy
from collections import OrderedDict
import getpass
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import re
import threading
import time
from urllib.parse import urlsplit
import uuid


LABEL = re.compile(r'[A-Za-z0-9][A-Za-z0-9-]{0,23}\Z')
ENV_NAME = re.compile(r'[A-Za-z_][A-Za-z0-9_]*\Z')


class PolicyError(ValueError):
    pass


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


def parse_json(text):
    def invalid(value):
        raise ValueError('Non-finite JSON number: ' + value)
    def finite(value):
        number = float(value)
        if not math.isfinite(number):
            raise ValueError('Non-finite JSON number')
        return number
    return json.loads(text, object_pairs_hook=unique_object, parse_constant=invalid, parse_float=finite)


def label(value):
    if not isinstance(value, str) or not LABEL.fullmatch(value):
        raise ValueError('IDs must be 1-24 ASCII letters/digits/hyphens, starting with a letter or digit')
    return value


def port(value):
    if type(value) is not int or not 1 <= value <= 65535:
        raise ValueError('Port must be an integer from 1 to 65535')
    return value


class Policy:
    def __init__(self, data, directory=Path('.')):
        if not isinstance(data, dict):
            raise ValueError('Deployment must be a JSON object')
        self.project = label(data['project'])
        flags = data['features']
        names = {'individual', 'group', 'broadcast', 'monitor'}
        if not isinstance(flags, dict) or set(flags) != names or any(type(v) is not bool for v in flags.values()):
            raise ValueError('features must contain four explicit boolean switches')
        self.features = dict(flags)
        self.devices = OrderedDict()
        rows = data['devices']
        if not isinstance(rows, list) or not 1 <= len(rows) <= 512:
            raise ValueError('Configure between 1 and 512 devices')
        for row in rows:
            device_id = label(row['id'])
            group = label(row['group'])
            name = row.get('name', device_id)
            if not isinstance(name, str) or not 1 <= len(name) <= 80 or device_id in self.devices:
                raise ValueError('Device names must be nonempty; IDs must be unique')
            self.devices[device_id] = {'id': device_id, 'name': name, 'group': group}
        self.groups = sorted({row['group'] for row in self.devices.values()})
        self.actions = OrderedDict()
        rows = data['actions']
        if not isinstance(rows, list) or len(rows) > 32:
            raise ValueError('actions must be a list of at most 32 entries')
        for row in rows:
            action_id = label(row['id'])
            params = row['params']
            name = row.get('name', action_id)
            if action_id in self.actions or not isinstance(name, str) or not 1 <= len(name) <= 80:
                raise ValueError('Action IDs must be unique and names nonempty')
            if not isinstance(params, dict) or not params or len(json.dumps(params, ensure_ascii=False, allow_nan=False).encode()) > 320:
                raise ValueError('Action params must be a nonempty JSON object of at most 320 bytes')
            self.actions[action_id] = {'id': action_id, 'name': name, 'params': copy.deepcopy(params)}
        self.broker = dict(data['broker'])
        self.web = dict(data['web'])
        for section in [self.broker, self.web]:
            if not isinstance(section.get('host'), str) or not section['host'] or '://' in section['host']:
                raise ValueError('Host must contain a hostname/IP, without a URL scheme')
            port(section['port'])
        if type(self.broker.get('tls')) is not bool or not isinstance(self.broker.get('username'), str):
            raise ValueError('Configure broker tls and username explicitly')
        if any(key in self.broker for key in ['password', 'token', 'secret']):
            raise ValueError('Use password_env instead of a credential in the JSON file')
        for name in [self.broker['password_env'], self.web['token_env']]:
            if not isinstance(name, str) or not ENV_NAME.fullmatch(name):
                raise ValueError('Invalid secret environment variable name')
        ca = self.broker.get('ca_file', '')
        if not isinstance(ca, str):
            raise ValueError('ca_file must be a path string')
        if ca:
            ca_path = (directory / ca).resolve()
            if not ca_path.is_file():
                raise ValueError('Configured CA file does not exist')
            self.broker['ca_file'] = str(ca_path)

    def prefix(self, device_id):
        return f'{self.project}_dev_{device_id}'

    def public(self):
        return {'project': self.project, 'features': self.features,
                'devices': list(self.devices.values()), 'groups': self.groups,
                'actions': [{'id': a['id'], 'name': a['name']} for a in self.actions.values()]}

    def resolve(self, scope, target, action_id):
        flag = {'device': 'individual', 'group': 'group', 'all': 'broadcast'}.get(scope)
        if not flag or not self.features[flag]:
            raise PolicyError('This command scope is disabled by the deployment configuration')
        if action_id not in self.actions:
            raise PolicyError('Action is not in the deployment allowlist')
        if scope == 'device':
            if target not in self.devices:
                raise PolicyError('Unknown device')
            topic, expected = self.prefix(target) + '/cmd', [target]
        elif scope == 'group':
            if target not in self.groups:
                raise PolicyError('Unknown group')
            topic = f'{self.project}_group_{target}/cmd'
            expected = [d['id'] for d in self.devices.values() if d['group'] == target]
        else:
            if target not in ('', None):
                raise PolicyError('A broadcast must not specify an unrelated target')
            topic, expected = f'{self.project}_all/cmd', list(self.devices)
        return topic, expected, self.actions[action_id]['params']

    def device_config(self, device_id):
        if device_id not in self.devices:
            raise ValueError('Unknown device')
        lines = ['CONFIG_SIOT_USING_CHANNELS=y',
                 f'CONFIG_SIOT_CHANNEL_PROJECT="{self.project}"',
                 f'CONFIG_SIOT_CHANNEL_DEVICE_ID="{device_id}"']
        for key, flag in [('DEVICE', 'individual'), ('GROUP', 'group'), ('BROADCAST', 'broadcast')]:
            lines.append('CONFIG_SIOT_CHANNEL_' + key + '=' + ('y' if self.features[flag] else 'n'))
        lines += [f'CONFIG_SIOT_CHANNEL_GROUP_ID="{self.devices[device_id]["group"]}"',
                  'CONFIG_SIOT_CHANNEL_REJECT_RETAINED=y']
        return '\n'.join(lines)

    def acl(self, teacher_user):
        """Export explicit Mosquitto 2.x ACL rules; never alters a broker."""
        label(teacher_user)
        if teacher_user in {'device-' + device_id for device_id in self.devices}:
            raise ValueError('Teacher and device usernames must be different')
        lines = ['# Mosquitto 2.x ACL; not a SIoT v1 config file.', 'user ' + teacher_user]
        for device_id in self.devices:
            prefix = self.prefix(device_id)
            lines.append('topic read ' + prefix + '/ack')
            if self.features['monitor']:
                lines.append('topic read ' + prefix + '/state')
            if self.features['individual']:
                lines.append('topic write ' + prefix + '/cmd')
        if self.features['group']:
            lines += ['topic write '+self.project+'_group_'+g+'/cmd' for g in self.groups]
        if self.features['broadcast']:
            lines.append('topic write '+self.project+'_all/cmd')
        fields = ['state','ack','temperature','humidity','illuminance','proximity',
                  'th_valid','ap_valid','led_red','led_blue','buzzer']
        for device_id, row in self.devices.items():
            prefix = self.prefix(device_id)
            lines += ['', 'user device-' + device_id]
            lines += ['topic write '+prefix+'/'+field for field in fields]
            if self.features['individual']:
                lines.append('topic read '+prefix+'/cmd')
            if self.features['group']:
                lines.append('topic read '+self.project+'_group_'+row['group']+'/cmd')
            if self.features['broadcast']:
                lines.append('topic read '+self.project+'_all/cmd')
        return '\n'.join(lines) + '\n'


def load_policy(path):
    path = Path(path)
    if path.stat().st_size > 1024 * 1024:
        raise ValueError('Deployment configuration is too large')
    return Policy(parse_json(path.read_text(encoding='utf-8-sig')), path.parent)


class Controller:
    def __init__(self, policy, publisher=None):
        self.policy, self.publisher = policy, publisher
        self.lock = threading.Lock()
        self.commands = OrderedDict()
        self.devices = {key: {'last_seen': None, 'state': None, 'last_ack': None} for key in policy.devices}
        self.topics = {policy.prefix(key) + '/' + suffix: (key, suffix)
                       for key in policy.devices for suffix in ['state', 'ack']}
        self.broker_ready = False
        self.broker_error = 'MQTT not connected'

    def connection(self, ready, error=''):
        with self.lock:
            self.broker_ready, self.broker_error = ready, error

    def send(self, scope, target, action):
        topic, expected, params = self.policy.resolve(scope, target, action)
        command_id = 't-' + uuid.uuid4().hex
        payload = json.dumps({'id': command_id, 'params': params}, ensure_ascii=False, separators=(',', ':'), allow_nan=False)
        command = {'id': command_id, 'topic': topic, 'scope': scope, 'target': target,
                   'created': time.time(), 'expected': expected, 'acks': {},
                   'broker_accepted': False, 'error': ''}
        with self.lock:
            self.commands[command_id] = command
            while len(self.commands) > 64:
                self.commands.popitem(last=False)
        try:
            if self.publisher is None:
                raise ConnectionError('No MQTT publisher')
            self.publisher(topic, payload)
            with self.lock:
                command['broker_accepted'] = True
        except Exception as exc:
            # Network exceptions may contain connection parameters: expose only
            # the exception type, and keep per-device acknowledgements intact.
            with self.lock:
                command['error'] = type(exc).__name__ + ': MQTT delivery not confirmed'
        with self.lock:
            return copy.deepcopy(command)

    def receive(self, topic, payload, retained=False):
        if retained or topic not in self.topics or len(payload) > 4096:
            return
        device_id, suffix = self.topics[topic]
        if suffix == 'state' and not self.policy.features['monitor']:
            return
        try:
            text = payload.decode('utf-8')
            value = parse_json(text)
        except (UnicodeError, ValueError):
            if suffix == 'ack':
                return
            value = text[:1024] if 'text' in locals() else '(invalid UTF-8)'
        if suffix == 'ack' and (not isinstance(value, dict) or not isinstance(value.get('id'), str)
                                or type(value.get('code')) is not int):
            return
        now = time.time()
        with self.lock:
            self.devices[device_id]['last_seen'] = now
            self.devices[device_id]['state' if suffix == 'state' else 'last_ack'] = value
            if suffix == 'ack':
                command = self.commands.get(value['id'])
                if command and device_id in command['expected']:
                    command['acks'][device_id] = {'code': value['code'], 'time': now}

    def snapshot(self):
        with self.lock:
            return copy.deepcopy({'broker_ready': self.broker_ready, 'broker_error': self.broker_error,
                                  'devices': self.devices, 'commands': list(self.commands.values())})


class MqttBridge:
    def __init__(self, controller, password):
        import paho.mqtt.client as mqtt
        self.controller, self.mqtt = controller, mqtt
        self.pending = set()
        self.subscribe_failed = False
        self.publish_lock = threading.Lock()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                  client_id='siot-teacher-' + uuid.uuid4().hex[:8], protocol=mqtt.MQTTv311)
        cfg = controller.policy.broker
        if cfg['username']:
            self.client.username_pw_set(cfg['username'], password)
        if cfg['tls']:
            self.client.tls_set(ca_certs=cfg.get('ca_file') or None)
            self.client.tls_insecure_set(False)
        self.client.reconnect_delay_set(1, 30)
        self.client.on_connect = self.connected
        self.client.on_disconnect = self.disconnected
        self.client.on_subscribe = self.subscribed
        self.client.on_message = lambda c, u, m: controller.receive(m.topic, m.payload, m.retain)

    def connected(self, client, userdata, flags, reason, properties):
        self.pending.clear()
        self.subscribe_failed = False
        self.controller.connection(False, 'MQTT subscriptions pending')
        if reason.is_failure:
            self.controller.connection(False, 'MQTT connection rejected')
            return
        topics = [(topic, 1) for topic, (_, suffix) in self.controller.topics.items()
                  if suffix == 'ack' or self.controller.policy.features['monitor']]
        for offset in range(0, len(topics), 16):
            result, mid = client.subscribe(topics[offset:offset+16])
            if result != self.mqtt.MQTT_ERR_SUCCESS:
                self.subscribe_failed = True
            else:
                self.pending.add(mid)
        if self.subscribe_failed:
            self.controller.connection(False, 'MQTT subscription request failed')

    def subscribed(self, client, userdata, mid, reasons, properties):
        if mid not in self.pending:
            return
        self.pending.remove(mid)
        if not reasons or any(reason.is_failure for reason in reasons):
            self.subscribe_failed = True
        if not self.pending:
            self.controller.connection(not self.subscribe_failed,
                                       'MQTT subscription rejected' if self.subscribe_failed else '')

    def disconnected(self, client, userdata, flags, reason, properties):
        self.controller.connection(False, 'MQTT disconnected; reconnecting')

    def start(self):
        cfg = self.controller.policy.broker
        self.client.connect_async(cfg['host'], cfg['port'], 60)
        self.client.loop_start()

    def publish(self, topic, payload):
        if not self.controller.snapshot()['broker_ready']:
            raise ConnectionError('MQTT not ready')
        with self.publish_lock:
            info = self.client.publish(topic, payload, qos=1, retain=False)
            if info.rc != self.mqtt.MQTT_ERR_SUCCESS:
                raise ConnectionError('MQTT publish failed')
            info.wait_for_publish(timeout=5)
            if not info.is_published():
                raise TimeoutError('MQTT PUBACK timed out')

    def close(self):
        self.client.disconnect()
        self.client.loop_stop()


def make_server(controller, token, host, number):
    if len(token) < 16 or not token.isascii() or any(ord(c) < 33 or ord(c) > 126 for c in token):
        raise ValueError('The web control token must have at least 16 printable ASCII characters without spaces')
    assets = Path(__file__).parent / 'web'
    static = {'/': ('index.html', 'text/html; charset=utf-8'),
              '/app.js': ('app.js', 'text/javascript; charset=utf-8'),
              '/style.css': ('style.css', 'text/css; charset=utf-8')}

    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(10)

        def respond(self, status, value, mime='application/json; charset=utf-8'):
            if isinstance(value, bytes):
                raw = value
            else:
                raw = json.dumps(value, ensure_ascii=False, allow_nan=False).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(raw)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Security-Policy', "default-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
            self.end_headers()
            self.wfile.write(raw)

        def authorized(self):
            supplied = self.headers.get('Authorization', '')
            if not hmac.compare_digest(supplied.encode(), ('Bearer ' + token).encode()):
                self.respond(401, {'error': 'A valid control token is required'})
                return False
            return True

        def do_GET(self):
            route = urlsplit(self.path).path
            if route in static:
                name, mime = static[route]
                self.respond(200, (assets/name).read_bytes(), mime)
            elif route in ['/api/config', '/api/state']:
                if self.authorized():
                    self.respond(200, controller.policy.public() if route == '/api/config' else controller.snapshot())
            else:
                self.respond(404, {'error': 'Not found'})

        def do_POST(self):
            if urlsplit(self.path).path != '/api/command':
                self.respond(404, {'error': 'Not found'})
                return
            if not self.authorized():
                return
            try:
                if self.headers.get('Content-Type', '').split(';')[0] != 'application/json':
                    raise ValueError('Expected application/json')
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 2048:
                    raise ValueError('Invalid body length')
                data = parse_json(self.rfile.read(length).decode('utf-8'))
                if not isinstance(data, dict) or set(data) != {'scope', 'target', 'action'}:
                    raise ValueError('Expected scope, target and action only')
                if any(not isinstance(data[key], str) for key in data):
                    raise ValueError('Command fields must be strings')
                result = controller.send(data['scope'], data['target'], data['action'])
                self.respond(202 if result['broker_accepted'] else 503, result)
            except PolicyError as exc:
                self.respond(403, {'error': str(exc)})
            except (ValueError, UnicodeError) as exc:
                self.respond(400, {'error': str(exc)})

        def log_message(self, pattern, *args):
            # Do not log authorization headers, configuration or MQTT payloads.
            pass

    server = ThreadingHTTPServer((host, number), Handler)
    server.daemon_threads = True
    return server


def secret(name, prompt, required=True):
    value = os.environ.get(name, '')
    if not value and required and os.isatty(0):
        value = getpass.getpass(prompt)
    if required and not value:
        raise ValueError('Set the required secret environment variable: ' + name)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check-config', action='store_true')
    mode.add_argument('--device-config', metavar='DEVICE_ID')
    mode.add_argument('--acl', metavar='TEACHER_USER')
    args = parser.parse_args()
    policy = load_policy(args.config)
    if args.check_config:
        print('Configuration valid:', policy.project, len(policy.devices), 'devices;', policy.features)
        return
    if args.device_config:
        print(policy.device_config(args.device_config))
        return
    if args.acl:
        print(policy.acl(args.acl), end='')
        return
    token = secret(policy.web['token_env'], 'Web control token (at least 16 characters): ')
    if len(token) < 16:
        raise ValueError('The web control token must have at least 16 characters')
    password = secret(policy.broker['password_env'], 'MQTT password: ', bool(policy.broker['username']))
    controller = Controller(policy)
    bridge = MqttBridge(controller, password)
    controller.publisher = bridge.publish
    server = make_server(controller, token, policy.web['host'], policy.web['port'])
    try:
        bridge.start()
        print(f'Control page: http://{policy.web["host"]}:{policy.web["port"]}; project={policy.project}')
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        bridge.close()


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, ImportError) as error:
        raise SystemExit(type(error).__name__ + ': ' + str(error))
