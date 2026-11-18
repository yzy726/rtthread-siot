"""Parse the package without a lab BSP and check its supported configuration."""
from pathlib import Path
import tempfile
import kconfiglib

root = Path(__file__).resolve().parents[1]
base = ['RT_USING_SAL','SAL_USING_LWIP','RT_USING_LWIP','RT_USING_POSIX_FS',
        'RT_USING_POSIX_SOCKET','RT_USING_POSIX_SELECT','PKG_USING_MBEDTLS',
        'RT_USING_RTC','RT_USING_SOFT_RTC','PKG_USING_PAHOMQTT','RT_USING_MSH']
text = 'mainmenu "SIoT standalone configuration test"\n'
text += 'config RT_VER_NUM\n    hex "Version"\n    default 0x40101\n'
text += 'config RT_USING_HEAP\n    bool\n    default y\n'
text += 'config FINSH_USING_MSH\n    bool\n    default y\n'
for name in base: text += f'config {name}\n    bool\n    default n\n'
text += f'source "{(root/"Kconfig").as_posix()}"\n'
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)/'Kconfig'
    path.write_text(text,encoding='utf-8')
    config = kconfiglib.Kconfig(str(path),warn=False)
    config.syms['PKG_USING_SIOT'].set_value('y')
    assert config.syms['PKG_USING_SIOT'].str_value == 'y'
    assert config.syms['SIOT_USING_TLS'].str_value == 'n'
    assert config.syms['SIOT_USING_EXAMPLE'].str_value == 'n'
    assert config.syms['SIOT_USING_CHANNELS'].str_value == 'n'
    config.syms['SIOT_USING_TLS'].set_value('y')
    assert config.syms['PKG_USING_MBEDTLS'].str_value == 'y'
    assert config.syms['RT_USING_SOFT_RTC'].str_value == 'n'
    config.syms['SIOT_USING_EXAMPLE'].set_value('y')
    assert config.syms['SIOT_EXAMPLE_HOST'].str_value == ''
    assert config.syms['SIOT_EXAMPLE_PASSWORD'].str_value == ''
    config.syms['SIOT_USING_CHANNELS'].set_value('y')
    assert config.syms['SIOT_CHANNEL_DEVICE'].str_value == 'y'
    assert config.syms['SIOT_CHANNEL_GROUP'].str_value == 'n'
    assert config.syms['SIOT_CHANNEL_BROADCAST'].str_value == 'n'
    assert config.syms['SIOT_CHANNEL_REJECT_RETAINED'].str_value == 'y'
    config.syms['SIOT_CHANNEL_DEVICE'].set_value('n')
    config.syms['SIOT_CHANNEL_GROUP'].set_value('y')
    config.syms['SIOT_CHANNEL_BROADCAST'].set_value('y')
    assert config.syms['SIOT_CHANNEL_DEVICE'].str_value == 'n'
    assert config.syms['SIOT_CHANNEL_GROUP'].str_value == 'y'
    assert config.syms['SIOT_CHANNEL_BROADCAST'].str_value == 'y'
    assert not any(name.startswith(('IOT_PLATFORM_','BSP_')) for name in config.syms)
    config.syms['RT_VER_NUM'].set_value('0x30105')
    assert config.syms['PKG_USING_SIOT'].str_value == 'n'
print('PASS standalone Kconfig, empty credentials, TLS and minimum RT-Thread version')
