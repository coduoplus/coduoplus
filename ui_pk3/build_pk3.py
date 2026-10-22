"""Build zz_coduoplus_ui.pk3: UO's own main menu plus a "CoDUO+ SETTINGS" entry.

    python -I ui_pk3/build_pk3.py [--game "D:/Call of Duty - Copy"] [--deploy]

-> dist/zz_coduoplus_ui.pk3   (--deploy also copies it to <game>/uo/)

WHAT IT CONTAINS. One file, ui_mp/main.menu. It is NOT kept in this repo: it
is generated from the player's own uo/pakuo03.pk3 (the last pak that ships UO's
main menu), checked against the SHA-256 of the stock 1.51 file, and the one
itemDef below is inserted between QUIT and AUTO-UPDATE - the slot (y=400) that
is empty in UO's menu exactly as in CoD1's. So no Activision file is
redistributed from source control, and the menu cannot drift from UO's.

The button sets cod1x_open_modern 1; the mod's watcher (ui/modern_menu.cpp)
sees it and opens the Ctrl+M overlay. The cvar keeps its upstream name: the
same C++ handles it. It must use `setcvar`: upstream's CoD1 item said
`set cod1x_open_modern 1;`, and UO's UI module has no `set` script command -
it skips unknown ones silently, so that button did nothing.

WHAT IT DOES NOT CONTAIN, compared with upstream's CoD1 pk3:
  - ui_mp/menus.txt: upstream's copy is CoD1's stock list. A pk3 file replaces
    the whole file, so shipping it to UO would hide UO's own menus.
  - ui_mp/joinserver.menu: only adds the 1.6 / 1.5 master-list buttons, which
    do nothing while the protocol stays vanilla (netcode/protocol_patch.cpp).

The name sorts after every pakuo*.pk3, so this main.menu wins over UO's.
"""
import hashlib
import os
import re
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, '..', 'dist')
PK3_NAME = 'zz_coduoplus_ui.pk3'
STOCK_PAK = os.path.join('uo', 'pakuo03.pk3')
STOCK_MAIN_MENU_SHA256 = '609111fdac3c16fae41ee95e684725fa93617d4d23f3ab1cd2f43bb706910e93'

# Same item as upstream's "1.6X SETTINGS" (ui_pk3/ui_mp/main.menu in cod1plus/client),
# relabelled; tabs and CRLF to match UO's file.
SETTINGS_ITEM = '''\t\titemDef
\t\t{
\t\t\tname coduoplus_settings_btn
\t\t\ttext "CoDUO+ SETTINGS"
\t\t\ttype ITEM_TYPE_BUTTON
\t\t\tstyle WINDOW_STYLE_EMPTY
\t\t\ttextstyle ITEM_TEXTSTYLE_SHADOWED
\t\t\trect 385 400 250 15
\t\t\ttextalign 0
\t\t\ttextscale .4
\t\t\ttextalignx 0
\t\t\ttextaligny 14
\t\t\ttextfont UI_FONT_NORMAL
\t\t\tforecolor .98 .82 .28 .9
\t\t\tvisible 1
\t\t\tmouseEnter
\t\t\t{
\t\t\t\tplay "mouse_over";
\t\t\t}
\t\t\taction
\t\t\t{
\t\t\t\tclose mods_menu;
\t\t\t\tclose options_multi;
\t\t\t\tplay "mouse_click";
\t\t\t\tsetcvar cod1x_open_modern "1";
\t\t\t}
\t\t}

'''.replace('\n', '\r\n')


def build_main_menu(game):
    with zipfile.ZipFile(os.path.join(game, STOCK_PAK)) as z:
        stock = z.read('ui_mp/main.menu')
    sha = hashlib.sha256(stock).hexdigest()
    if sha != STOCK_MAIN_MENU_SHA256:
        sys.exit('ui_mp/main.menu in %s is not the stock 1.51 file (sha256 %s)' % (STOCK_PAK, sha))
    text = stock.decode('latin1')
    # insert right before the itemDef whose text is the auto-update entry
    m = re.search(r'\t\titemDef\s*\r\n\t\t\{\r\n\t\t\tname play\r\n\t\t\ttext "@MENU_AUTO_UPDATE"', text)
    if not m:
        sys.exit('could not find the AUTO-UPDATE item in UO main.menu')
    if 'rect 385 400' in text:
        sys.exit('UO main.menu already has an item at y=400 - layout changed?')
    out = text[:m.start()] + SETTINGS_ITEM + text[m.start():]
    # cheap structural checks: one more itemDef, braces still balanced, still CRLF
    assert out.count('itemDef') == text.count('itemDef') + 1
    assert out.count('{') == out.count('}')
    assert out.count('\n') == out.count('\r\n')
    return out.encode('latin1')


def main():
    args = sys.argv[1:]
    game = args[args.index('--game') + 1] if '--game' in args else r'D:/Call of Duty - Copy'
    os.makedirs(OUT_DIR, exist_ok=True)
    pk3 = os.path.join(OUT_DIR, PK3_NAME)
    with zipfile.ZipFile(pk3, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('ui_mp/main.menu', build_main_menu(game))
    print('built', os.path.normpath(pk3))
    if '--deploy' in args:
        dst = os.path.join(game, 'uo', PK3_NAME)
        shutil.copy2(pk3, dst)
        print('deployed ->', dst)


if __name__ == '__main__':
    main()
