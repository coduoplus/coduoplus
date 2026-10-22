"""Assemble a CoDUO+ release from the CI build outputs. See docs/RELEASES.md.

    python3 packaging/package.py --version 0.2.0 --tag v0.2.0 --repo coduoplus/coduoplus \\
        --windows <dir> --linux <dir> --out dist [--nightly]

<windows dir>: mss32.dll, norinput/mss32.dll, CoDUOPlus-Setup.exe (the hosted Windows
build). <linux dir>: coduoplus_sv.so (the hosted Linux build). Writes into --out, with
names that carry no version, so github.com/<repo>/releases/latest/download/<name> is a
link that never changes:

  CoDUOPlus-Setup.exe             players: the installer (packaging/coduoplus.iss)
  coduoplus-client.zip            players, by hand: mss32.dll, coduoplus.ini, INSTALL.txt
  coduoplus-server-windows.zip    start-coduoplus-server.bat + coduoplus-server/ (the DLL,
                                  example configs, README) - unzipped into the game folder
  coduoplus-server-linux.tar.gz   coduoplus-server-linux/: coduoplus_sv.so, the launcher,
                                  example configs, docker-compose.yml + .env.example
  mss32.dll                       what the updater downloads (manifest.json points at it)
  mss32-norinput.dll              diagnostics build without the raw-input hook
  coduoplus_sv.so                 the Linux library on its own
  docker-compose.yml              the compose file on its own
  manifest.json                   the updater feed: version + this release's mss32.dll
  SHA256SUMS                      of everything above
  notes.md                        release notes: download table + the CHANGELOG section

Runs anywhere Python 3 does (CI: ubuntu-latest); uses nothing outside the stdlib.
"""
import argparse
import hashlib
import io
import json
import os
import re
import shutil
import sys
import tarfile
import time
import zipfile

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
NOW = time.localtime()[:6]


def read(rel):
    with open(os.path.join(REPO, rel), 'rb') as f:
        return f.read()


def crlf(data):
    return data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')


def text(rel, version, windows):
    data = read(rel).replace(b'{VERSION}', version.encode())
    return crlf(data) if windows else data.replace(b'\r\n', b'\n')


def need(path):
    if not os.path.isfile(path):
        sys.exit('package.py: missing %s' % path)
    return path


def zip_files(path, entries):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, NOW)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)


def tar_files(path, entries):
    with tarfile.open(path, 'w:gz') as t:
        for name, data, mode in entries:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = mode
            info.mtime = int(time.time())
            t.addfile(info, io.BytesIO(data))


def compose_for_release(version):
    """The repo's compose file builds from the checkout (build: ../..), which a release
    download doesn't have: drop that line and pin the image to this release."""
    out = []
    for line in read('server/linux/docker-compose.yml').decode().splitlines():
        if line.strip().startswith('build:'):
            continue
        out.append(line.replace('ghcr.io/coduoplus/coduoplus-server:latest',
                                'ghcr.io/coduoplus/coduoplus-server:' + version))
    return ('\n'.join(out) + '\n').encode()


def changelog_section(version, repo, tag):
    """This version's whole CHANGELOG section. Its links are relative to the repo root;
    on the release page they must point into the tree at this tag."""
    lines, keep = [], False
    for line in read('CHANGELOG.md').decode('utf-8').splitlines():
        if line.startswith('## v' + version):
            keep = True
        elif keep and (line.startswith('## v') or line.startswith('# ')):
            break
        if keep:
            lines.append(line)
    blob = 'https://github.com/%s/blob/%s/' % (repo, tag)
    return re.sub(r'\]\(([^)#:][^):]*)\)', lambda m: '](' + blob + m.group(1) + ')', '\n'.join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', required=True)
    ap.add_argument('--tag', required=True)
    ap.add_argument('--repo', required=True)
    ap.add_argument('--windows', required=True)
    ap.add_argument('--linux', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--nightly', action='store_true')
    ap.add_argument('--commit', default='')
    a = ap.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+', a.version):
        sys.exit('package.py: version must be X.Y.Z, got %r' % a.version)

    dll = read_abs(need(os.path.join(a.windows, 'mss32.dll')))
    dll_nori = read_abs(need(os.path.join(a.windows, 'norinput', 'mss32.dll')))
    setup = need(os.path.join(a.windows, 'CoDUOPlus-Setup.exe'))
    so = read_abs(need(os.path.join(a.linux, 'coduoplus_sv.so')))
    if b'coduoplus DLL_PROCESS_ATTACH' not in dll:
        sys.exit('package.py: mss32.dll is not a CoDUO+ build')
    if not so.startswith(b'\x7fELF\x01'):
        sys.exit('package.py: coduoplus_sv.so is not a 32-bit ELF')

    os.makedirs(a.out, exist_ok=True)
    out = lambda n: os.path.join(a.out, n)  # noqa: E731
    v = a.version

    shutil.copyfile(setup, out('CoDUOPlus-Setup.exe'))
    for name, data in (('mss32.dll', dll), ('mss32-norinput.dll', dll_nori), ('coduoplus_sv.so', so),
                       ('docker-compose.yml', compose_for_release(v))):
        with open(out(name), 'wb') as f:
            f.write(data)

    zip_files(out('coduoplus-client.zip'), [
        ('mss32.dll', dll),
        ('coduoplus.ini', read('coduoplus.ini')),
        ('INSTALL.txt', text('packaging/client-INSTALL.txt', v, True)),
    ])
    zip_files(out('coduoplus-server-windows.zip'), [
        ('start-coduoplus-server.bat', crlf(read('server/windows/start-coduoplus-server.bat'))),
        ('coduoplus-server/mss32.dll', dll),
        ('coduoplus-server/competitive.cfg.example', crlf(read('competitive.cfg.example'))),
        ('coduoplus-server/server.cfg.example', crlf(read('server/linux/server.cfg.example'))),
        ('coduoplus-server/README.txt', text('packaging/server-windows-README.txt', v, True)),
    ])
    d = 'coduoplus-server-linux/'
    tar_files(out('coduoplus-server-linux.tar.gz'), [
        (d + 'coduoplus_sv.so', so, 0o755),
        (d + 'coduoplus-server.sh', read('server/linux/coduoplus-server.sh').replace(b'\r\n', b'\n'), 0o755),
        (d + 'competitive.cfg.example', read('competitive.cfg.example'), 0o644),
        (d + 'server.cfg.example', read('server/linux/server.cfg.example'), 0o644),
        (d + 'docker-compose.yml', compose_for_release(v), 0o644),
        (d + '.env.example', read('server/linux/.env.example'), 0o644),
        (d + 'README.txt', text('packaging/server-linux-README.txt', v, False), 0o644),
    ])

    base = 'https://github.com/%s/releases/download/%s/' % (a.repo, a.tag)
    with open(out('manifest.json'), 'w') as f:
        json.dump({'version': v, 'download_url': base + 'mss32.dll',
                   'notes': 'CoDUO+ %s - https://github.com/%s/releases/tag/%s' % (v, a.repo, a.tag)}, f)
        f.write('\n')

    assets = ['CoDUOPlus-Setup.exe', 'coduoplus-client.zip', 'coduoplus-server-windows.zip',
              'coduoplus-server-linux.tar.gz', 'mss32.dll', 'mss32-norinput.dll', 'coduoplus_sv.so',
              'docker-compose.yml', 'manifest.json']
    with open(out('SHA256SUMS'), 'w', newline='\n') as f:
        for n in assets:
            with open(out(n), 'rb') as g:
                f.write('%s  %s\n' % (hashlib.sha256(g.read()).hexdigest(), n))

    image = 'ghcr.io/%s/coduoplus-server:%s' % (a.repo.split('/')[0].lower(), 'edge' if a.nightly else v)
    head = []
    if a.nightly:
        head += ['**Development build** of `main` at %s - not a release: the in-game updater never '
                 'offers it. Players: take the [latest release](https://github.com/%s/releases/latest) '
                 'instead.' % (a.commit[:12] or a.tag, a.repo), '']
    head += [
        '| You are | Download | |',
        '| --- | --- | --- |',
        '| **a player** | [`CoDUOPlus-Setup.exe`](%sCoDUOPlus-Setup.exe) | run it, click Next until Finish. '
        'Updates come by themselves. (Manual install: [`coduoplus-client.zip`](%scoduoplus-client.zip).) |' % (base, base),
        '| running a **Windows server** | [`coduoplus-server-windows.zip`](%scoduoplus-server-windows.zip) | '
        'unzip into the game folder, run `start-coduoplus-server.bat` |' % base,
        '| running a **Linux server** | [`coduoplus-server-linux.tar.gz`](%scoduoplus-server-linux.tar.gz) | '
        '`docker compose up -d` (image `%s`) or `./coduoplus-server.sh /path/to/coduo` |' % (base, image),
        '',
        'Windows may warn that the setup is from an unknown publisher (it is not code-signed): '
        '*More info* → *Run anyway*. Checksums: [`SHA256SUMS`](%sSHA256SUMS).' % base,
        '',
    ]
    with open(out('notes.md'), 'w', encoding='utf-8', newline='\n') as f:
        # a nightly is not a version: the section under its number is the last release's
        section = '' if a.nightly else changelog_section(v, a.repo, a.tag)
        if not section and not a.nightly:
            sys.exit("package.py: CHANGELOG.md has no '## v%s' section" % v)
        f.write('\n'.join(head) + '\n' + section + '\n')
    for n in assets + ['SHA256SUMS', 'notes.md']:
        print('%10d  %s' % (os.path.getsize(out(n)), n))


def read_abs(path):
    with open(path, 'rb') as f:
        return f.read()


if __name__ == '__main__':
    main()
