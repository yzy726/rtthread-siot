"""Compile the real channel implementation with all feature combinations."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CC') or shutil.which('gcc')
assert compiler, 'Install GCC or set CC'
for routes in range(8):
    for retained in [0, 1]:
        exe = root / f'tests/output/channels-{routes}-{retained}.exe'
        command = [compiler, '-std=c99', '-D_POSIX_C_SOURCE=200809L', '-O1', '-Wall', '-Wextra', '-Werror',
                   '-I'+str(root/'tests/host'), '-I'+str(root/'inc'),
                   '-DSIOT_USING_CHANNELS', f'-DEXPECT_ROUTES={routes}u',
                   f'-DEXPECT_REJECT_RETAINED={retained}',
                   '-DSIOT_CHANNEL_PROJECT="demo"', '-DSIOT_CHANNEL_DEVICE_ID="001"',
                   '-DSIOT_CHANNEL_GROUP_ID="g01"']
        for bit, name in [(1,'DEVICE'), (2,'GROUP'), (4,'BROADCAST')]:
            if routes & bit:
                command.append('-DSIOT_CHANNEL_'+name)
        if retained:
            command.append('-DSIOT_CHANNEL_REJECT_RETAINED')
        command += [str(root/'src/siot_channels.c'), str(root/'tests/test_channels.c'), '-o', str(exe)]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)
print('Channel matrix passed: 16 actual C builds/runs')
