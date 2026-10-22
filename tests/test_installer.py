"""The player installer (packaging/coduoplus.iss), built and run for real, silently.

    python -I tests/test_installer.py <ISCC.exe> <dir-with-mss32.dll> [<setup.exe>]

Builds CoDUOPlus-Setup.exe from the given mss32.dll (or tests the given setup), then
runs it against throw-away game folders. No game needed: the "game" is a stand-in
CoDUOMP.exe carrying CoD:UO 1.51's PE timestamp (all the installer reads of it) and a
stand-in Miles mss32.dll, so this runs on a hosted CI runner. Checks:
  - fresh install: Miles DLL -> mss32_original.dll, ours -> mss32.dll, coduoplus.ini;
  - install over it (an update): the original stays Miles, the player's ini is kept;
  - uninstall: the game folder is back to exactly what it was, logs/pk3s gone;
  - refusals: a folder without CoDUOMP.exe, and a CoDUO+ mss32.dll without the
    original next to it (the game could not start), leave the folder untouched.
Installs run with /CURRENTUSER, so the test needs no administrator rights.
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..'))
UO_151_PE_TIMESTAMP = 0x41B53340
MILES = b'MZ fake Miles Sound System mss32.dll\x00' + bytes(4096)

failures = []


def check(ok, what):
    print('  [%s] %s' % ('OK' if ok else 'FAIL', what))
    if not ok:
        failures.append(what)


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def fake_exe(timestamp):
    # DOS header with e_lfanew = 0x80, then "PE\0\0" + IMAGE_FILE_HEADER (i386)
    dos = bytearray(0x80)
    dos[0:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3C, 0x80)
    return bytes(dos) + b'PE\x00\x00' + struct.pack('<HHI', 0x014C, 1, timestamp) + bytes(64)


def make_game(root, exe_ts=UO_151_PE_TIMESTAMP, mss32=MILES, original=None):
    os.makedirs(os.path.join(root, 'uo'))
    with open(os.path.join(root, 'CoDUOMP.exe'), 'wb') as f:
        f.write(fake_exe(exe_ts))
    if mss32 is not None:
        with open(os.path.join(root, 'mss32.dll'), 'wb') as f:
            f.write(mss32)
    if original is not None:
        with open(os.path.join(root, 'mss32_original.dll'), 'wb') as f:
            f.write(original)
    return root


def listing(root):
    out = {}
    for d, _, files in os.walk(root):
        for n in files:
            p = os.path.join(d, n)
            out[os.path.relpath(p, root)] = sha(p)
    return out


def run(setup, args, log):
    return subprocess.run([setup, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
                           '/LOG=' + log] + args, timeout=120).returncode


def uninstall(game, log):
    u = os.path.join(game, 'coduoplus-uninstall', 'unins000.exe')
    if not os.path.exists(u):
        return None
    rc = subprocess.run([u, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/LOG=' + log], timeout=120).returncode
    # the uninstaller copies itself to %TEMP% and returns at once: wait for it to finish
    import time
    for _ in range(60):
        if not os.path.exists(u):
            break
        time.sleep(0.5)
    time.sleep(1)
    return rc


def main():
    iscc, dll_dir = sys.argv[1], sys.argv[2]
    ours = os.path.join(dll_dir, 'mss32.dll')
    work = tempfile.mkdtemp(prefix='coduoplus-installer-')
    try:
        if len(sys.argv) > 3:
            setup = sys.argv[3]
        else:
            r = subprocess.run([iscc, '/Q', '/DVersion=0.0.0', '/DDllDir=' + os.path.abspath(dll_dir),
                                '/O' + work, os.path.join(REPO, 'packaging', 'coduoplus.iss')],
                               capture_output=True, text=True)
            if r.returncode:
                print(r.stdout, r.stderr)
                sys.exit('FAIL: ISCC')
            setup = os.path.join(work, 'CoDUOPlus-Setup.exe')
        print('setup:', setup)

        print('fresh install:')
        game = make_game(os.path.join(work, 'game'))
        before = listing(game)
        rc = run(setup, ['/DIR=' + game], os.path.join(work, 'install1.log'))
        check(rc == 0, 'setup exit code 0 (got %s)' % rc)
        check(os.path.exists(os.path.join(game, 'mss32.dll')) and
              sha(os.path.join(game, 'mss32.dll')) == sha(ours), 'mss32.dll is CoDUO+')
        check(os.path.exists(os.path.join(game, 'mss32_original.dll')) and
              sha(os.path.join(game, 'mss32_original.dll')) == hashlib.sha256(MILES).hexdigest(),
              'the Miles DLL is now mss32_original.dll')
        check(os.path.exists(os.path.join(game, 'coduoplus.ini')) and
              sha(os.path.join(game, 'coduoplus.ini')) == sha(os.path.join(REPO, 'coduoplus.ini')),
              'coduoplus.ini installed')
        check(os.path.exists(os.path.join(game, 'coduoplus-uninstall', 'unins000.exe')),
              'uninstaller in coduoplus-uninstall\\')

        print('install over it (update):')
        ini = os.path.join(game, 'coduoplus.ini')
        with open(ini, 'ab') as f:
            f.write(b'\r\n; the player changed something\r\n')
        ini_sha = sha(ini)
        rc = run(setup, ['/DIR=' + game], os.path.join(work, 'install2.log'))
        check(rc == 0, 'setup exit code 0 (got %s)' % rc)
        check(sha(os.path.join(game, 'mss32_original.dll')) == hashlib.sha256(MILES).hexdigest(),
              'mss32_original.dll is still Miles (CoDUO+ not mistaken for it)')
        check(sha(os.path.join(game, 'mss32.dll')) == sha(ours), 'mss32.dll is CoDUO+')
        check(sha(ini) == ini_sha, "the player's coduoplus.ini is kept")

        print('uninstall:')
        for n in ('coduoplus.log', 'coduoplus.prev.log', os.path.join('uo', 'zz_coduoplus_ui.pk3')):
            with open(os.path.join(game, n), 'wb') as f:
                f.write(b'written by the mod at run time')
        rc = uninstall(game, os.path.join(work, 'uninstall.log'))
        check(rc == 0, 'uninstaller exit code 0 (got %s)' % rc)
        after = listing(game)
        check(after == before, 'game folder exactly as before the install%s' % (
            '' if after == before else ': ' + repr(sorted(set(after.items()) ^ set(before.items())))))
        check(not os.path.exists(os.path.join(game, 'coduoplus-uninstall')), 'uninstaller folder removed')

        print('refusals:')
        empty = os.path.join(work, 'not-a-game')
        os.makedirs(empty)
        rc = run(setup, ['/DIR=' + empty], os.path.join(work, 'refuse1.log'))
        check(rc != 0 and os.listdir(empty) == [], 'folder without CoDUOMP.exe: refused, untouched (rc %s)' % rc)
        with open(ours, 'rb') as f:
            broken = make_game(os.path.join(work, 'broken'), mss32=f.read())
        before = listing(broken)
        rc = run(setup, ['/DIR=' + broken], os.path.join(work, 'refuse2.log'))
        check(rc != 0 and listing(broken) == before,
              'CoDUO+ mss32.dll without mss32_original.dll: refused, untouched (rc %s)' % rc)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print('test_installer: %s' % ('%d FAILED' % len(failures) if failures else 'all passed'))
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
