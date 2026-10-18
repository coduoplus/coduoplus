"""Minimal PE32 reader for the reverse-engineering helpers.

Maps virtual addresses to file offsets, lists sections, finds strings and
references to them. Only what the coduoplus RE workflow needs - no
dependencies beyond the standard library, so it runs anywhere Python does.
"""
import re
import struct


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        e = struct.unpack_from('<I', self.data, 0x3c)[0]
        nsec = struct.unpack_from('<H', self.data, e + 6)[0]
        self.timestamp = struct.unpack_from('<I', self.data, e + 8)[0]
        opt_size = struct.unpack_from('<H', self.data, e + 20)[0]
        opt = e + 24
        self.base = struct.unpack_from('<I', self.data, opt + 28)[0]
        self.size_of_image = struct.unpack_from('<I', self.data, opt + 56)[0]
        self.sections = []
        o = opt + opt_size
        for _ in range(nsec):
            name = self.data[o:o + 8].rstrip(b'\0').decode('latin1')
            vsize, rva, rsize, roff = struct.unpack_from('<IIII', self.data, o + 8)
            chars = struct.unpack_from('<I', self.data, o + 36)[0]
            self.sections.append(dict(name=name, rva=rva, vsize=vsize,
                                      roff=roff, rsize=rsize, chars=chars))
            o += 40

    # --- address conversions -------------------------------------------------
    def rva_to_off(self, rva):
        for s in self.sections:
            if s['rva'] <= rva < s['rva'] + s['rsize']:
                return s['roff'] + rva - s['rva']
        return None

    def va_to_off(self, va):
        return self.rva_to_off(va - self.base)

    def off_to_va(self, off):
        for s in self.sections:
            if s['roff'] <= off < s['roff'] + s['rsize']:
                return self.base + s['rva'] + off - s['roff']
        return None

    def read_va(self, va, n):
        off = self.va_to_off(va)
        if off is None:
            raise ValueError('VA 0x%x is not backed by file data' % va)
        return self.data[off:off + n]

    def section_of_va(self, va):
        rva = va - self.base
        for s in self.sections:
            if s['rva'] <= rva < s['rva'] + max(s['vsize'], s['rsize']):
                return s['name']
        return None

    def is_code_va(self, va):
        rva = va - self.base
        for s in self.sections:
            if s['rva'] <= rva < s['rva'] + s['rsize']:
                return bool(s['chars'] & 0x20000000)  # IMAGE_SCN_MEM_EXECUTE
        return False

    # --- searching ------------------------------------------------------------
    def find_bytes(self, needle):
        """All VAs where `needle` occurs (file-backed data only)."""
        out = []
        for m in re.finditer(re.escape(needle), self.data):
            va = self.off_to_va(m.start())
            if va is not None:
                out.append(va)
        return out

    def find_string(self, s):
        """VAs of a NUL-terminated string, exact match."""
        needle = s.encode('latin1') + b'\0'
        hits = []
        for va in self.find_bytes(needle):
            off = self.va_to_off(va)
            if off == 0 or self.data[off - 1] == 0:  # must start a string
                hits.append(va)
        return hits

    def xrefs_imm32(self, target, code_only=True):
        """VAs where the 32-bit little-endian value `target` appears.

        For this 2004-era MSVC code every absolute reference (push imm32,
        mov reg/mem, call [abs]) encodes the address as a plain imm32/disp32,
        so a raw scan finds them; the caller disassembles to see which.
        """
        hits = self.find_bytes(struct.pack('<I', target))
        return [h for h in hits if not code_only or self.is_code_va(h)]

    def calls_to(self, target):
        """VAs of E8 rel32 calls whose destination is `target`."""
        out = []
        for s in self.sections:
            if not s['chars'] & 0x20000000:
                continue
            start, end = s['roff'], s['roff'] + s['rsize']
            d = self.data
            i = d.find(b'\xe8', start, end)
            while i != -1 and i + 5 <= end:
                rel = struct.unpack_from('<i', d, i + 1)[0]
                va = self.off_to_va(i)
                if va + 5 + rel == target:
                    out.append(va)
                i = d.find(b'\xe8', i + 1, end)
        return out
