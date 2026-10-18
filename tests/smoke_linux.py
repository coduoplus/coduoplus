"""End-to-end smoke test of the LINUX server image, on the real coduo_lnxded 1.51b.

    python3 tests/smoke_linux.py <linux-bins-dir> <game-dir>

<game-dir> holds main/ (or Main/) and uo/ with the pk3s - a Windows install seen from
WSL (/mnt/d/...) is fine, and is the harder case (see step 2). Runs on a Linux host with
Docker (WSL works). Steps:
  1. builds the server image (server/linux/Dockerfile, the image docs/DOCKER.md ships);
  2. control run on a Windows drive: the same image with CODUOPLUS_LFS=0 must find
     "0 files in pk3 files" (the 2005 engine's 32-bit stat/readdir vs 64-bit inodes) -
     proof that the run that follows really exercises the large-inode wrappers;
  3. runs the image as in production - /game, /bins, /config (competitive.cfg.example as
     competitive.cfg) read-only - on mp_harbor, and waits for Docker's HEALTHCHECK;
  4. checks over UDP: getstatus protocol 22 + shortversion "CoDUO+ ..."; three real
     connects against the version gate (old CoDUO+ refused, vanilla + current in);
     rcon `systeminfo` - what clients receive - holds sv_coduoplus_swing 1 and the spec,
     before and after a map change (the game module is reloaded and re-patched);
  5. checks the console log: every patch applied, nothing refused; `docker stop` is quick.
"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(REPO, 'tools', 're'))
from q3query import query  # noqa: E402
from q3connect import connect  # noqa: E402
from q3rcon import rcon  # noqa: E402

NAME, PORT, PW, IMAGE = 'coduoplus-smoke-linux', 28960, 'coduoplus-smoke', 'coduoplus-server:smoke'

MUST_LOG = [
    'CoDUO+ ',                                   # library loaded in coduo_lnxded
    'competitive: snaps cap 30 -> 40 applied',
    'version_patch: shortversion -> "CoDUO+ ',
    'lfs: engine stat/readdir -> 64-bit wrappers installed',
    'engine: dlopen hook installed',
    'swing_fix [server]: ON in 0x',
    'ClientConnect gate installed, G_RunFrame hook installed',
    'swing_fix: server publishes sv_coduoplus_swing 1 (engine holds "1")',
    'competitive_sv: engine holds sv_competitive "snaps=40 cl_maxpackets=125',
    'version_gate: reject client ',
    'antilag: Bullet_Fire hook installed',
    'antilag: g_antilag=1 g_antilagMaxMs=300',
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
    game = os.path.abspath(sys.argv[2])
    failures = []

    r = sh('docker', 'build', '-q', '-f', os.path.join(REPO, 'server', 'linux', 'Dockerfile'),
           '-t', IMAGE, REPO)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit('FAIL: docker build')
    print('image', IMAGE, r.stdout.strip()[:19])

    cfg = tempfile.mkdtemp(prefix='coduoplus-smoke-')
    with open(os.path.join(REPO, 'competitive.cfg.example')) as f, \
            open(os.path.join(cfg, 'competitive.cfg'), 'w') as o:
        o.write(f.read())
    os.chmod(cfg, 0o755)
    mounts = ['-v', game + ':/game:ro', '-v', bins + ':/bins:ro', '-v', cfg + ':/config:ro']

    if game.startswith('/mnt/'):
        sh('docker', 'rm', '-f', NAME + '-control')
        c = sh('docker', 'run', '--name', NAME + '-control', '-e', 'DEDICATED=1', '-e', 'CODUOPLUS_LFS=0',
               *mounts, IMAGE, '+map', 'mp_harbor', timeout=120)
        sh('docker', 'rm', '-f', NAME + '-control')
        found = '0 files in pk3 files' in c.stdout + c.stderr
        print('control (CODUOPLUS_LFS=0 on %s): %s' % (
            game, 'engine finds 0 pk3 files, as expected' if found else 'engine found its files?!'))
        if not found:
            failures.append('control run without the lfs wrappers did not fail - the test would '
                            'not prove the large-inode fix')

    sh('docker', 'rm', '-f', NAME)
    r = sh('docker', 'run', '-d', '--name', NAME, '-e', 'DEDICATED=1',
           '--health-interval', '2s', '--health-start-period', '0s', *mounts,
           IMAGE, '+set', 'rconpassword', PW, '+map', 'mp_harbor')
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
        health = ''
        for _ in range(30):
            health = sh('docker', 'inspect', '-f', '{{.State.Health.Status}}', NAME).stdout.strip()
            if health == 'healthy':
                break
            time.sleep(1)
        print('docker health:', health)
        if health != 'healthy':
            failures.append('HEALTHCHECK says %r' % health)
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
        t0 = time.time()
        sh('docker', 'stop', NAME)
        stop_s = time.time() - t0
        print('docker stop: %.1f s' % stop_s)
        if stop_s > 5:
            failures.append('docker stop took %.1f s (SIGTERM not reaching the server?)' % stop_s)
        sh('docker', 'rm', '-f', NAME)
    if not state.startswith('Up'):
        failures.append('server not running at the end: %s' % state)
    for needle in MUST_LOG:
        if needle not in log:
            failures.append('log lacks: ' + needle)
    # once per game-module load: the map change must have re-applied both
    for needle in ('swing_fix [server]: ON in 0x', 'G_RunFrame hook installed',
                   'antilag: Bullet_Fire hook installed'):
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
