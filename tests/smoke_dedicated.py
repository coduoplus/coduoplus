"""End-to-end smoke test: run the real game with the mod, as a dedicated server.

    python -I tests/smoke_dedicated.py <game-dir> [--port 28999] [--keep]

Needs the build already deployed (deploy.ps1). Starts CoDUOMP.exe in that
folder as a dedicated server on mp_harbor, waits for it to answer a getinfo
query, then checks:

  - the server is alive and advertises protocol 22 and our shortversion
    (proves the proxy loaded, DllMain ran, and the version patch is live);
  - coduoplus.log has the line every ENGINE patch writes when it applies;
  - coduoplus.log has none of the lines a patch writes when it refuses
    (byte mismatch, abort, skip);
  - nothing was restyled: a dedicated server has no game window;
  - the server-side ClientConnect hook (version gate) really decides: a real
    connect handshake (tools/re/q3connect.py) claiming an old CoDUO+ build is
    refused with our message, while a vanilla client and a current one get
    connectResponse.

A dedicated server opens only a console window, so this can run while someone
is using the PC. It does NOT cover the renderer, input or cgame - those need a
client run (docs/TESTING.md).
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 're'))
from q3query import query  # noqa: E402
from q3connect import connect  # noqa: E402

MUST_LOG = [
    'DLL_PROCESS_ATTACH',
    'version_patch: shortversion redirige -> "CoDUO+ ',
    'protocol_patch: protocol 22',
    'competitive: caps applied (snaps->40, cl_maxpackets->125)',
    'r_displayRefresh engine cap 200 -> 1000 @0x004c4001 patched',
    # applied only with `fullscreen = off`; the shipped ini says on (vanilla)
    ('fullscreen_patch: r_fullscreen default redirige', 'fullscreen_patch: disabled in config'),
    'frame_limiter: Com_EventLoop call at CoDUOMP+0x3c6a7 -> our wait',
    'rinput: GetCursorPos IAT hook installed',
    'gpu_sync: SwapBuffers hooked (auto',
    'version_gate: client userinfo coduoplus=16',
    'settings_menu: registered cvars',
    # server half of the swing fix: uo_game patched and the flag published
    'swing_fix [server]: ON in 0x',
    'swing_fix: server publishes sv_coduoplus_swing 1',
    'version_gate: ClientConnect gate installed in ',
    'version_gate: reject client ',
]
# Phrases the patches log when they refuse to write (wrong bytes, wrong build).
MUST_NOT_LOG = ['abort', 'SKIPPED', 'annule', 'PARTIAL', 'expected 0x', 'inattendu',
                'not CoDUOMP.exe', 'borderless applied', 'do not match']


def info_dict(reply):
    line = reply.split('\n')[1] if '\n' in reply else ''
    parts = line.split('\\')[1:]
    return dict(zip(parts[::2], parts[1::2]))


def main():
    game = sys.argv[1]
    port = int(sys.argv[sys.argv.index('--port') + 1]) if '--port' in sys.argv else 28999
    log_path = os.path.join(game, 'coduoplus.log')
    if os.path.exists(log_path):
        os.remove(log_path)

    exe = os.path.join(game, 'CoDUOMP.exe')
    proc = subprocess.Popen([exe, '+set', 'dedicated', '1', '+set', 'net_port', str(port),
                             '+set', 'sv_punkbuster', '0', '+map', 'mp_harbor'], cwd=game)
    failures = []
    try:
        reply = None
        deadline = time.time() + 60
        while time.time() < deadline and proc.poll() is None:
            reply = query('127.0.0.1', port, 'getstatus')
            if reply:
                break
            time.sleep(1)
        time.sleep(2)  # let the watcher thread's first ticks land in the log

        if proc.poll() is not None:
            failures.append('game exited early with code %s' % proc.returncode)
        if not reply:
            failures.append('server never answered getstatus on port %d' % port)
        else:
            info = info_dict(reply)
            print('server: map=%s protocol=%s shortversion=%s' % (
                info.get('mapname'), info.get('protocol'), info.get('shortversion')))
            if info.get('protocol') != '22':
                failures.append('protocol is %r, expected 22' % info.get('protocol'))
            if not info.get('shortversion', '').startswith('CoDUO+ '):
                failures.append('shortversion is %r, expected "CoDUO+ ..."' % info.get('shortversion'))
            if info.get('mapname') != 'mp_harbor':
                failures.append('mapname is %r' % info.get('mapname'))
            # version gate, end to end: (userinfo extra, expected reply prefix)
            for extra, want in (({'coduoplus': '5'}, 'error\nUpdate coduoplus'),
                                ({}, 'connectResponse'),
                                ({'coduoplus': '16'}, 'connectResponse')):
                got = connect('127.0.0.1', port, extra)
                print('connect %-18s -> %s' % (extra or 'vanilla', got.strip().replace('\n', ' | ')))
                if not got.startswith(want):
                    failures.append('connect %r: got %r, expected %r...' % (extra, got, want))
    finally:
        if '--keep' not in sys.argv and proc.poll() is None:
            proc.kill()
            proc.wait()

    log = open(log_path, encoding='latin1').read() if os.path.exists(log_path) else ''
    if not log:
        failures.append('coduoplus.log missing or empty - did the proxy load?')
    for needle in MUST_LOG:
        options = needle if isinstance(needle, tuple) else (needle,)
        if not any(n in log for n in options):
            failures.append('log lacks: %s' % ' | '.join(options))
    for line in log.splitlines():
        for bad in MUST_NOT_LOG:
            if bad in line:
                failures.append('log has refusal: %s' % line.strip())

    for f in failures:
        print('FAIL', f)
    print('smoke_dedicated: %s (%d checks of the log, %d problems)' % (
        'ok' if not failures else 'FAILED', len(MUST_LOG) + len(MUST_NOT_LOG), len(failures)))
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
