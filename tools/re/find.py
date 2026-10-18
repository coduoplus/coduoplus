"""String / xref finder for the coduoplus RE workflow.

  python -I tools/re/find.py <binary> str  <text> [<text> ...]   strings + code refs
  python -I tools/re/find.py <binary> imm  <hex-va> [...]        code refs to a VA
  python -I tools/re/find.py <binary> call <hex-va> [...]        E8 calls to a VA
  python -I tools/re/find.py <binary> bytes <hex-bytes>          raw byte search
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE  # noqa: E402


def main():
    pe = PE(sys.argv[1])
    cmd, args = sys.argv[2], sys.argv[3:]
    if cmd == 'str':
        for s in args:
            for va in pe.find_string(s):
                refs = pe.xrefs_imm32(va)
                print('%r @0x%08x refs: %s' % (s, va, ' '.join('0x%x' % (r - 1) for r in refs)))
    elif cmd == 'imm':
        for a in args:
            print('0x%s refs: %s' % (a, ' '.join('0x%x' % r for r in pe.xrefs_imm32(int(a, 16)))))
    elif cmd == 'call':
        for a in args:
            print('calls to 0x%s: %s' % (a, ' '.join('0x%x' % r for r in pe.calls_to(int(a, 16)))))
    elif cmd == 'bytes':
        print(' '.join('0x%x' % v for v in pe.find_bytes(bytes.fromhex(''.join(args)))))


if __name__ == '__main__':
    main()
