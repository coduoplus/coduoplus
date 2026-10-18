"""Collect the exact CoD:UO 1.51 binaries the address map was taken from.

    python -I tests/fetch_binaries.py <game-dir> <out-dir>

CoDUOMP.exe is copied from the game folder. The three MP modules are extracted
from uo/pakuo07.pk3 (the 1.51 patch pak) rather than taken from the loose
uo/*.dll files: the engine re-extracts whichever pk3's DLL a server asks for,
so the loose copies can belong to a mod (the test machine's uo_game_mp_x86.dll
does). Every file is checked against the SHA-256 of the stock 1.51 build;
a mismatch fails loudly instead of testing addresses against the wrong code.

Known variant: a later uo_game_mp_x86.dll (linked 2005-01-10, TimeDateStamp
0x41e30286, not in any stock pk3) that servers may run. When the install's
loose uo/uo_game_mp_x86.dll is exactly that build, a second set is written to
<out-dir>-2005 with it swapped in, and the site_check_2005 test checks the
server-side (uo_game) patches against it too.
"""
import hashlib
import os
import shutil
import sys
import zipfile

VARIANT_GAME_2005 = '9ad513d1a6c0b19711a1c1c2051560c723f92b2ba583bdc3604ffc1ccc9fbfd3'

STOCK = {
    'CoDUOMP.exe':         '64a5aab259160324078fb5f13f835d26128ba2c3c19d104a493f228434d9d205',
    'uo_cgame_mp_x86.dll': '06bbb23ad398949bc081235b5d533486e18e0a598418f7301d64138b781d7e1b',
    'uo_game_mp_x86.dll':  '584a0cc0a9f7c3c2d336baa0fb76622227ca2375ed7f4cbcbf119f628def199e',
    'uo_ui_mp_x86.dll':    '75638ba8a57261a0a6261ca64aba74db5c3724b1b96fd297603b99066e4caa07',
}


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    game, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    shutil.copyfile(os.path.join(game, 'CoDUOMP.exe'), os.path.join(out, 'CoDUOMP.exe'))
    with zipfile.ZipFile(os.path.join(game, 'uo', 'pakuo07.pk3')) as z:
        for name in ('uo_cgame_mp_x86.dll', 'uo_game_mp_x86.dll', 'uo_ui_mp_x86.dll'):
            with z.open(name) as src, open(os.path.join(out, name), 'wb') as dst:
                shutil.copyfileobj(src, dst)
    bad = 0
    for name, want in STOCK.items():
        got = sha256(os.path.join(out, name))
        ok = got == want
        bad += not ok
        print('%-22s %s %s' % (name, 'ok ' if ok else 'BAD', got))
    if bad:
        sys.exit('%d file(s) are not the stock CoD:UO 1.51 build' % bad)

    loose = os.path.join(game, 'uo', 'uo_game_mp_x86.dll')
    variant_dir = out + '-2005'
    if os.path.exists(loose) and sha256(loose) == VARIANT_GAME_2005:
        os.makedirs(variant_dir, exist_ok=True)
        for name in STOCK:
            src = loose if name == 'uo_game_mp_x86.dll' else os.path.join(out, name)
            shutil.copyfile(src, os.path.join(variant_dir, name))
        print('variant: 2005-01-10 uo_game_mp_x86.dll found -> %s' % variant_dir)
    elif os.path.isdir(variant_dir):
        shutil.rmtree(variant_dir)


if __name__ == '__main__':
    main()
