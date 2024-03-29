Import('env')
import os
import os.path
import platform as p
target_os = env.get('TARGET_OS')
target_arch = env.get('TARGET_ARCH')
samples_env = env.Clone()
src_dir = env.get('SRC_DIR')
build_dir = env.get('BUILD_DIR')

SConscript('#build_common/thread.scons', exports={'thread_env': samples_env})

######################################################################
# Build flags
######################################################################
samples_env.PrependUnique(CPPPATH=[
    '#/resource/include/',
    '#/resource/c_common/oic_malloc/include',
    '#/resource/c_common/ocrandom/include',
    '#/resource/csdk/include',
    '#/resource/csdk/stack/include',
    '#/resource/csdk/stack/include/experimental',
    '#/resource/csdk/security/include',
    '#/resource/csdk/security/include/internal',
    '#/resource/csdk/security/provisioning/include',
    '#/resource/csdk/security/provisioning/include/internal',
    '#/resource/csdk/security/provisioning/include/oxm',
    '#/resource/csdk/security/provisioning/include/cloud',
    '#/resource/csdk/connectivity/api',
    '#/resource/csdk/connectivity/inc/pkix',
    '#/extlibs/asn1cert',
    '#/resource/csdk/connectivity/lib/libcoap-4.1.1/include/',
    '#/resource/csdk/logger/include',
    '#/resource/oc_logger/include',
    '#/resource/csdk/logger/include/experimental',
    '#/extlibs/mbedtls/mbedtls/include'
])
cpp_defines = ['__WITH_DTLS__', 'TB_LOG']
libraries = ['octbstack', 'oc', 'connectivity_abstraction', 'oc_logger', 'coap', 'ocpmapi', 'ocsrm']
# identify which hardware the samples will be running on.
platform_info = p.platform()
joule = 'Linux' in platform_info and 'joule' in platform_info and \
'Ubuntu' in platform_info
raspberry_pi = 'Linux' in platform_info and \
('armv7l' in platform_info or 'armv6l' in platform_info) and \
'debian' in platform_info
if os.path.exists("/proc/device-tree/model"):
    with open("/proc/device-tree/model") as f:
        model = f.read()
        artik = 'artik' in model or 'compy' in model
else:
    artik = False
if joule or raspberry_pi or artik:
    conf = Configure(env)
    if not conf.CheckLib('mraa'):
        print("required library mraa not installed! ")
        if joule and not raspberry_pi:
            print(" To install mraa\n\
                $sudo add-apt-repository ppa:mraa/mraa\n\
                $sudo apt-get update\n\
                $sudo apt-get install libmraa1 libmraa-dev\
                 mraa-tools python-mraa python3-mraa\n")
        elif not joule and (raspberry_pi or artik):
            print("to install mraa\n\
                $git clone https://github.com/intel-iot-devkit/\
                mraa.git ../mraa\n\
                $mkdir ../mraa/build && cd ../mraa/build && cmake .. && make\n\
                $sudo make install")
        else:
            print("please install mraa")
    else:
        cpp_defines.append('WITH_MRAA')
        libraries.append('mraa')
        if joule and not raspberry_pi:
            cpp_defines.append('LED_PIN=100')
        elif not joule and raspberry_pi:
            cpp_defines.append('LED_PIN=7')
        elif artik:
            cpp_defines.append('LED_PIN=38')
            cpp_defines.append('RAW_GPIO')
        else:
            print("ERROR: unknown board")
    conf.Finish()
samples_env.AppendUnique(RPATH=[build_dir])
samples_env.AppendUnique(CPPDEFINES=cpp_defines)
samples_env.AppendUnique(CFLAGS=['-std=c99'])
samples_env.AppendUnique(CXXFLAGS=['-std=c++0x', '-Wall', '-Wextra',
                                   '-pthread', '-fpermissive'])
samples_env.AppendUnique(LIBS=['pthread', 'dl', 'm'])
samples_env.PrependUnique(LIBS=libraries)
if target_arch in ['x86_64', 'arm64']:
    samples_env.AppendUnique(CPPFLAGS=['-Llib64'])
else:
    samples_env.AppendUnique(CPPFLAGS=['-Llib'])
if env.get('SECURED') == '1':
    samples_env.AppendUnique(LIBS=['mbedtls', 'mbedx509', 'mbedcrypto'])
if target_os in ['linux']:
    samples_env.ParseConfig('pkg-config --cflags --libs sqlite3')

samples_env.AppendUnique(CPPDEFINES=['TB_LOG'])

######################################################################
# Source files and Targets
######################################################################
client = samples_env.Program('client', ['client.cpp'])
server = samples_env.Program('server', ['server.cpp'])
examples_dir = '/examples/OCFSecure/'
client_dat = samples_env.Install(
    build_dir + examples_dir, src_dir + examples_dir + 'oic_svr_db_client.dat')
server_dat = samples_env.Install(
    build_dir + examples_dir, src_dir + examples_dir + 'ocf_svr_db_server_RFOTM.dat')

list_of_samples = [client, client_dat, server, server_dat]

Alias("secureExamples", list_of_samples)

env.AppendTarget('secureExamples')
