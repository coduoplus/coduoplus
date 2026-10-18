"""End-to-end smoke test of the LINUX server library, on the real coduo_lnxded 1.51b.

    python3 tests/smoke_linux.py <linux-bins-dir> [<docker volume with the stock paks>]

Runs on a Linux host with Docker (WSL works). Steps:
  1. builds the image (server/linux/Dockerfile) and coduoplus_sv.so (server/linux/build.sh);
  2. starts coduo_lnxded with the library preloaded and competitive.cfg.example as
     its competitive.cfg, on mp_harbor (server/linux/run_server.sh);
  3. checks over UDP: getstatus protocol 22 + shortversion "CoDUO+ ..."; three real
     connects against the version gate (old CoDUO+ refused, vanilla + current in);
     rcon `systeminfo` - what clients receive - holds sv_coduoplus_swing 1 and the
     competitive spec;
  4. checks the console log: every patch applied, nothing refused.

The paks volume is made once with server/linux/make_game_volume.sh (default name
coduo-uo-game). See docs/LINUX.md.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(REPO, 'tools', 're'))
from q3query import query  # noqa: E402
from q3connect import connect  # noqa: E402
from q3rcon import rcon  # noqa: E402

NAME, PORT, PW = 'coduoplus-smoke-linux', 28997, 'coduoplus-smoke'

MUST_LOG = [
    'CoDUO+ ',                                   # library loaded in coduo_lnxded
    'competitive: snaps cap 30 -> 40 applied',
    'version_patch: shortversion -> "CoDUO+ ',
    'engine: dlopen hook installed',
    'swing_fix [server]: ON in 0x',
    'ClientConnect gate installed, G_RunFrame hook installed',
    'swing_fix: server publishes sv_coduoplus_swing 1 (engine holds "1")',
    'competitive_sv: engine holds sv_competitive "snaps=40 cl_maxpackets=125',
    'version_gate: reject client ',
]
MUST_NOT_LOG = ['does not match', 'unknown build', 'FAILED', 'left vanilla', 'not hooking',
                'line refused', 'too long', 'Segmentation']


def sh(*cmd, **kw):
    return subprocess.run(list(cmd), capture_output=True, text=True, **kw)


def sysinfo_value(text, key):
    marker = '\\' + key + '\\'
    i = text.rfind(marker)
    if i >= 0:
        return text[i + len(marker):].split('\\', 1)[0].split('\n', 1)[0]
    return None


def main():
    bins = os.path.abspath(sys.argv[1])
    vol = sys.argv[2] if len(sys.argv) > 2 else 'coduo-uo-game'
    failures = []

    for cmd in (['docker', 'build', '-q', '-t', 'coduoplus-linux', os.path.join(REPO, 'server', 'linux')],
                ['docker', 'run', '--rm', '-v', REPO + ':/src', 'coduoplus-linux', 'sh', 'server/linux/build.sh']):
        r = sh(*cmd)
        if r.returncode:
            print(r.stdout, r.stderr)
            sys.exit('FAIL: %s' % ' '.join(cmd[:3]))
    print(r.stdout.strip().splitlines()[-2], '|', r.stdout.strip().splitlines()[-1].split()[-1])

    sh('docker', 'rm', '-f', NAME)
    r = sh('docker', 'run', '-d', '--name', NAME, '-e', 'COMPETITIVE=1',
           '-v', bins + ':/bins:ro', '-v', vol + ':/game:ro', '-v', REPO + ':/src:ro',
           'coduoplus-linux', 'sh', '/src/server/linux/run_server.sh', '+set', 'rconpassword', PW)
    if r.returncode:
        sys.exit('FAIL: docker run: ' + r.stderr)
    try:
        ip = sh('docker', 'inspect', '-f', '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}',
                NAME).stdout.strip()
        reply = None
        deadline = time.time() + 90
        while time.time() < deadline and not reply:
            reply = query(ip, PORT, 'getstatus')
            if not reply:
                time.sleep(1)
        time.sleep(2)
        if not reply:
            failures.append('server never answered getstatus')
        else:
            parts = reply.split('\n')[1].split('\\')[1:]
            info = dict(zip(parts[::2], parts[1::2]))
            print('server: map=%s protocol=%s shortversion=%s' % (
                info.get('mapname'), info.get('protocol'), info.get('shortversion')))
            if info.get('protocol') != '22':
                failures.append('protocol %r' % info.get('protocol'))
            if not info.get('shortversion', '').startswith('CoDUO+ '):
                failures.append('shortversion %r' % info.get('shortversion'))
            for extra, want in (({'coduoplus': '5'}, 'error\nUpdate coduoplus'),
                                ({}, 'connectResponse'), ({'coduoplus': '16'}, 'connectResponse')):
                got = connect(ip, PORT, extra)
                print('connect %-18s -> %s' % (extra or 'vanilla', got.strip().replace('\n', ' | ')))
                if not got.startswith(want):
                    failures.append('connect %r: %r' % (extra, got))
            # map change: the engine dlcloses the game module and loads a fresh copy -
            # everything must be applied again, and still reach clients
            for rnd in ('first map', 'after a map change'):
                if rnd != 'first map':
                    # a DIFFERENT map: reloading the same one keeps the module loaded
                    time.sleep(1.5)   # the engine drops rcon commands sent too close together
                    r = rcon(ip, PORT, PW, 'map mp_arnhem')
                    deadline = time.time() + 90
                    while time.time() < deadline and 'mp_arnhem' not in (query(ip, PORT, 'getinfo') or ''):
                        time.sleep(1)
                    time.sleep(2)
                    now_map = 'mp_arnhem' if 'mp_arnhem' in (query(ip, PORT, 'getinfo') or '') else '?'
                    print('map change -> %s (rcon said %r)' % (now_map, r.strip()[:60]))
                    if now_map != 'mp_arnhem':
                        failures.append('map change to mp_arnhem did not happen')
                si = rcon(ip, PORT, PW, 'systeminfo')
                for key, want in (('sv_coduoplus_swing', '1'), ('sv_competitive', 'snaps=40 ')):
                    got = sysinfo_value(si, key)
                    print('%-18s systeminfo %-18s = %r' % (rnd, key, got))
                    if got is None or not got.startswith(want):
                        failures.append('%s: systeminfo lacks %s' % (rnd, key))
    finally:
        _l = sh('docker', 'logs', NAME)
        log = _l.stdout + _l.stderr
        print('game-module loads seen:', log.count('G_RunFrame hook installed'))
        state = sh('docker', 'ps', '-a', '--filter', 'name=' + NAME, '--format', '{{.Status}}').stdout.strip()
        sh('docker', 'rm', '-f', NAME)
    if not state.startswith('Up'):
        failures.append('server not running at the end: %s' % state)
    for needle in MUST_LOG:
        if needle not in log:
            failures.append('log lacks: ' + needle)
    # once per game-module load: the map change must have re-applied both
    for needle in ('swing_fix [server]: ON in 0x', 'G_RunFrame hook installed'):
        if log.count(needle) < 2:
            failures.append('not re-applied after the map change: ' + needle)
    for line in log.splitlines():
        if '[coduoplus]' in line or 'Segmentation' in line:
            for bad in MUST_NOT_LOG:
                if bad in line:
                    failures.append('log has: ' + line.strip())
    for f in failures:
        print('FAIL', f)
    print('smoke_linux: %s (%d log checks, %d problems)' % (
        'ok' if not failures else 'FAILED', len(MUST_LOG) + len(MUST_NOT_LOG), len(failures)))
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
