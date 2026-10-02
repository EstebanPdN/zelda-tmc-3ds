#!/usr/bin/env python3
"""Build ALttP's pinned 3DS updater dependencies without publishing binaries."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import urllib.request

root = Path(sys.argv[1]).resolve()
source = root / 'src'
source.mkdir(parents=True, exist_ok=True)
prefix = root / 'prefix'
recipe = Path(__file__).resolve().parents[1] / 'update-dependencies'
devkit = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
jobs = os.environ.get('TMC3DS_JOBS', '4')


def run(args, cwd=source, env=None):
    subprocess.run([str(x) for x in args], cwd=cwd, env=env, check=True)


for record in json.loads((recipe / 'sources.json').read_text()):
    name = record['file']
    if not name.endswith(('.tar.gz', '.tar.xz')):
        continue
    archive = source / name
    if not archive.exists():
        with urllib.request.urlopen(record['url'], timeout=60) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != record['sha256']:
        raise SystemExit('Dependency checksum mismatch: ' + name)
    folder = {'mbedtls.tar.gz': 'mbedtls-2.28.8', 'curl.tar.xz': 'curl-8.4.0',
              'jansson.tar.gz': 'jansson-2.14'}[name]
    if not (source / folder).exists():
        run(['tar', 'xf', archive])
        if folder.startswith(('curl', 'mbedtls')):
            run(['patch', '-p1', '-i', recipe / (folder.split('-')[0] + '.patch')], source / folder)

mbed = source / 'mbedtls-2.28.8'
for name in ['MBEDTLS_ENTROPY_HARDWARE_ALT', 'MBEDTLS_NO_PLATFORM_ENTROPY', 'MBEDTLS_CMAC_C']:
    run(['perl', 'scripts/config.pl', 'set', name], mbed)
for name in ['MBEDTLS_SELF_TEST', 'MBEDTLS_TIMING_C']:
    run(['perl', 'scripts/config.pl', 'unset', name], mbed)
for folder, options in [
    ('mbedtls-2.28.8', ['-DENABLE_ZLIB_SUPPORT=FALSE', '-DENABLE_TESTING=FALSE', '-DENABLE_PROGRAMS=FALSE']),
    ('jansson-2.14', ['-DJANSSON_BUILD_DOCS=OFF', '-DJANSSON_BUILD_SHARED_LIBS=OFF', '-DJANSSON_WITHOUT_TESTS=ON'])
]:
    build = root / (folder + '-build')
    run(['cmake', '-S', source / folder, '-B', build,
         '-DCMAKE_TOOLCHAIN_FILE=' + str(devkit / 'cmake/3DS.cmake'),
         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
         '-DCMAKE_INSTALL_PREFIX=' + str(prefix), *options])
    run(['cmake', '--build', build, '--parallel', jobs])
    run(['cmake', '--install', build])

# Autoconf/Make need a space-free path for their library flags.
with tempfile.TemporaryDirectory(prefix='tmc-update-') as temp:
    alias = Path(temp) / 'deps'
    alias.symlink_to(root, target_is_directory=True)
    cp = str(alias / 'prefix')
    env = dict(os.environ)
    env.update(PATH=str(devkit / 'devkitARM/bin') + ':' + env['PATH'],
               CC='arm-none-eabi-gcc', AR='arm-none-eabi-ar', RANLIB='arm-none-eabi-ranlib',
               CFLAGS='-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -mword-relocations -ffunction-sections -fdata-sections',
               CPPFLAGS='-D_3DS -D__3DS__ -I' + cp + '/include -I' + str(devkit / 'libctru/include'),
               LDFLAGS='-L' + cp + '/lib -L' + str(devkit / 'libctru/lib') + ' -specs=3dsx.specs', LIBS='-lctru')
    curl = alias / 'src/curl-8.4.0'
    run(['./configure', '--prefix=' + cp, '--host=arm-none-eabi', '--disable-shared', '--enable-static',
         '--disable-ipv6', '--disable-unix-sockets', '--disable-threaded-resolver', '--disable-manual',
         '--disable-pthreads', '--disable-socketpair', '--disable-ntlm-wb', '--with-mbedtls=' + cp,
         '--without-zlib', '--without-brotli', '--without-zstd', '--without-libpsl', '--without-libidn2',
         '--without-nghttp2', '--without-libssh2', '--disable-ldap', '--disable-ldaps',
         '--with-ca-bundle=romfs:/update-ca.pem'], curl, env)
    run(['make', '-C', 'lib', '-j' + jobs], curl, env)
    run(['make', '-C', 'lib', 'install'], curl, env)
    run(['make', '-C', 'include', 'install'], curl, env)
print('Updater libraries ready: ' + str(prefix))
