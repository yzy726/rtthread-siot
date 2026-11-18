"""Compile and exercise the real C transport against an adversarial local broker."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'tests/output'
out.mkdir(exist_ok=True)
compiler = os.environ.get('CC') or shutil.which('gcc')
if not compiler: raise SystemExit('Install GCC, or set CC to a compatible compiler.')
codec = root / 'third_party/MQTTPacket/src'
cmd = [compiler,'-std=c99','-D_POSIX_C_SOURCE=200809L','-O1','-Wall','-Wextra',
       '-Wno-unused-parameter', '-I'+str(root/'tests/host'),'-I'+str(root/'inc'),'-I'+str(codec),
       str(root/'src/siot_client.c'),str(root/'tests/test_transport.c')]
cmd += [str(path) for path in sorted(codec.glob('*.c'))]
if os.name == 'nt': cmd += ['-lws2_32']
cmd += ['-o', str(out/'test_transport.exe')]
subprocess.run(cmd, check=True)
subprocess.run([sys.executable,str(root/'tests/test_transport.py')], check=True)
subprocess.run([sys.executable,str(root/'tests/test_kconfig.py')], check=True)
subprocess.run([sys.executable,str(root/'tests/test_channels.py')], check=True)
subprocess.run([sys.executable,str(root/'tests/test_teacher.py')], check=True)
subprocess.run([sys.executable,str(root/'tests/test_teacher_mqtt.py')], check=True)
