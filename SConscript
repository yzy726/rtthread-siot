from building import *
cwd = GetCurrentDir()
src = Glob('src/*.c') + Glob('third_party/MQTTPacket/src/*.c')
if GetDepend('SIOT_USING_EXAMPLE'):
    src += Glob('examples/*.c')
group = DefineGroup('siot', src, depend=['PKG_USING_SIOT'],
    CPPPATH=[cwd + '/inc', cwd + '/third_party/MQTTPacket/src'])
Return('group')
