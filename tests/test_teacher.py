"""Policy enforcement and authenticated HTTP tests, without a live server."""
import copy
import importlib.util
import json
from pathlib import Path
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('siot_teacher', root/'tools/teacher/control.py')
teacher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(teacher)
example = json.loads((root/'tools/teacher/deployment.example.json').read_text(encoding='utf-8'))


class TeacherTests(unittest.TestCase):
    def policy(self, **features):
        data = copy.deepcopy(example)
        data['features'].update(features)
        return teacher.Policy(data)

    def test_defaults_and_disabled_scopes(self):
        policy = self.policy()
        self.assertEqual(policy.resolve('device','001','off')[0], 'demo_dev_001/cmd')
        for scope, target in [('group','g01'), ('all','')]:
            with self.assertRaises(teacher.PolicyError): policy.resolve(scope,target,'off')
        with self.assertRaises(teacher.PolicyError): policy.resolve('device','unlisted','off')
        with self.assertRaises(teacher.PolicyError): policy.resolve('device','001','unlisted')
        policy = self.policy(individual=False, group=False, broadcast=False)
        with self.assertRaises(teacher.PolicyError): policy.resolve('device','001','off')

    def test_routes_and_generated_board_configuration(self):
        policy = self.policy(group=True, broadcast=True)
        topic, expected, params = policy.resolve('group','g01','red-on')
        self.assertEqual(topic, 'demo_group_g01/cmd')
        self.assertEqual(expected, ['001','002'])
        self.assertEqual(params, {'led_red':1})
        self.assertEqual(policy.resolve('all','','off')[:2], ('demo_all/cmd', ['001','002','003']))
        with self.assertRaises(teacher.PolicyError): policy.resolve('all','001','off')
        generated = policy.device_config('002')
        self.assertIn('CONFIG_SIOT_CHANNEL_DEVICE_ID="002"', generated)
        self.assertIn('CONFIG_SIOT_CHANNEL_BROADCAST=y', generated)
        self.assertNotIn('PASSWORD', generated)

    def test_acl_export_respects_features_and_separates_devices(self):
        acl = self.policy().acl('teacher')
        teacher_acl, device_acl = acl.split('user device-001',1)
        self.assertNotIn('demo_all/cmd', acl)
        self.assertNotIn('demo_group_g01/cmd', acl)
        self.assertIn('topic write demo_dev_001/cmd', teacher_acl)
        first_device = device_acl.split('user device-002')[0]
        self.assertIn('topic read demo_dev_001/cmd', first_device)
        self.assertNotIn('topic write demo_dev_001/cmd', first_device)
        self.assertNotIn('demo_dev_002/', first_device)
        enabled = self.policy(group=True,broadcast=True).acl('teacher')
        self.assertIn('topic write demo_all/cmd', enabled)
        self.assertIn('topic read demo_group_g01/cmd', enabled)
        with self.assertRaises(ValueError): self.policy().acl('device-001')
        with self.assertRaises(ValueError): self.policy().acl('teacher\nuser attacker')

    def test_bad_config_and_json(self):
        for name in ['', 'a/b', 'a_b', '-first', 'a+', 'a#', 'a'*25, '中文']:
            data = copy.deepcopy(example); data['project'] = name
            with self.assertRaises(ValueError): teacher.Policy(data)
        data = copy.deepcopy(example); data['features']['broadcast'] = 'false'
        with self.assertRaises(ValueError): teacher.Policy(data)
        data = copy.deepcopy(example); data['devices'].append(data['devices'][0])
        with self.assertRaises(ValueError): teacher.Policy(data)
        data = copy.deepcopy(example); data['broker']['password'] = 'not-allowed'
        with self.assertRaises(ValueError): teacher.Policy(data)
        for text in ['{"id":1,"id":2}', '{"value":NaN}', '{"value":1e999}']:
            with self.assertRaises(ValueError): teacher.parse_json(text)

    def test_puback_is_not_execution(self):
        sent = []
        policy = self.policy(group=True)
        controller = teacher.Controller(policy, lambda topic,payload: sent.append((topic,payload)))
        command = controller.send('group','g01','off')
        self.assertTrue(command['broker_accepted'])
        self.assertEqual(command['acks'], {})
        ack = json.dumps({'id':command['id'],'code':200}).encode()
        controller.receive('demo_dev_003/ack', ack)  # outsider is not a group confirmation
        controller.receive('demo_dev_001/ack', ack, retained=True)
        self.assertEqual(controller.snapshot()['commands'][0]['acks'], {})
        controller.receive('demo_dev_001/ack', ack)
        self.assertEqual(list(controller.snapshot()['commands'][0]['acks']), ['001'])
        controller.receive('demo_dev_002/ack', json.dumps({'id':command['id'],'code':400}).encode())
        self.assertEqual(controller.snapshot()['commands'][0]['acks']['002']['code'], 400)
        self.assertLessEqual(len(command['id']),39)
        self.assertEqual(json.loads(sent[0][1])['params'], {'led_red':0,'led_blue':0,'buzzer':0})

    def test_inflight_ack_and_network_failure(self):
        controller = teacher.Controller(self.policy())
        def publish(topic, payload):
            command_id = json.loads(payload)['id']
            controller.receive('demo_dev_001/ack', json.dumps({'id':command_id,'code':200}).encode())
            raise TimeoutError('private transport detail')
        controller.publisher = publish
        command = controller.send('device','001','off')
        self.assertFalse(command['broker_accepted'])
        self.assertEqual(command['acks']['001']['code'],200)
        self.assertNotIn('private transport detail', command['error'])

    def test_monitoring_and_history_bound(self):
        controller = teacher.Controller(self.policy(monitor=False), lambda *args: None)
        controller.receive('demo_dev_001/state', b'{"temperature":25}')
        self.assertIsNone(controller.snapshot()['devices']['001']['state'])
        controller = teacher.Controller(self.policy(), lambda *args: None)
        controller.receive('demo_dev_001/state', b'{"temperature":25}')
        controller.receive('other_dev_001/state', b'{"temperature":99}')
        self.assertEqual(controller.snapshot()['devices']['001']['state'], {'temperature':25})
        for _ in range(70): controller.send('device','001','off')
        self.assertEqual(len(controller.snapshot()['commands']),64)

    def test_http_auth_and_policy_enforced_on_server(self):
        published = []
        controller = teacher.Controller(self.policy(), lambda *args: published.append(args))
        token = 'test-control-token-12345'
        server = teacher.make_server(controller, token, '127.0.0.1', 0)
        worker = threading.Thread(target=server.serve_forever,daemon=True); worker.start()
        base = 'http://127.0.0.1:'+str(server.server_address[1])
        def request(path, body=None, credential=token, raw=False):
            headers = {'Authorization':'Bearer '+credential,'Content-Type':'application/json'}
            data = body.encode() if raw else json.dumps(body).encode() if body is not None else None
            try:
                with urlopen(Request(base+path, data=data, headers=headers),timeout=3) as reply:
                    return reply.status, json.loads(reply.read())
            except HTTPError as error:
                return error.code, json.loads(error.read())
        try:
            self.assertEqual(request('/api/config',credential='wrong')[0],401)
            status, public = request('/api/config')
            self.assertEqual(status,200)
            self.assertNotIn('broker',public)
            self.assertNotIn('token',json.dumps(public))
            self.assertEqual(request('/api/command',{'scope':'all','target':'','action':'off'})[0],403)
            self.assertEqual(published,[])
            self.assertEqual(request('/api/command',{'scope':'device','target':'001','action':'off'},credential='wrong')[0],401)
            self.assertEqual(request('/api/command','{"scope":"all","scope":"device"}',raw=True)[0],400)
            self.assertEqual(request('/api/command',{'scope':'device','target':'001','action':'off','topic':'bypass'})[0],400)
            status, command = request('/api/command',{'scope':'device','target':'001','action':'off'})
            self.assertEqual(status,202)
            self.assertEqual(len(published),1)
            self.assertEqual(command['acks'],{})
            self.assertEqual(request('/api/state')[0],200)
        finally:
            server.shutdown(); server.server_close(); worker.join(2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
