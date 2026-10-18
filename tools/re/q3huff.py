"""Quake 3 adaptive Huffman, as used on the `connect` packet.

CoDUOMP.exe's SV_ConnectionlessPacket (0x462602..0x46263d) does
    if (!strncmp("connect", data + 4, 7)) Huff_Decompress(msg, 12);
i.e. everything after "\\xff\\xff\\xff\\xffconnect " is compressed with id's adaptive
Huffman coder (code/qcommon/huffman.c, GPL). This is a straight port of its
compression side - same node list, same block-leader bookkeeping, same bit order -
plus the decompressor, so the pair can round-trip itself.

    compress(data) -> bytes      (2-byte big-endian length, then the bit stream)
    decompress(blob) -> bytes
"""

HMAX = 256
NYT = HMAX
INTERNAL_NODE = HMAX + 1


class _Node:
    __slots__ = ('left', 'right', 'parent', 'next', 'prev', 'head', 'weight', 'symbol')

    def __init__(self):
        self.left = self.right = self.parent = self.next = self.prev = None
        self.head = None  # a _Cell: "pointer to the highest ranked node in my block"
        self.weight = 0
        self.symbol = 0


class _Cell:
    """Stands in for huffman.c's node_t** (a slot holding a node pointer)."""
    __slots__ = ('node',)

    def __init__(self):
        self.node = None


class _Huff:
    def __init__(self):
        self.free = []  # freelist of cells
        nyt = _Node()
        nyt.symbol = NYT
        self.tree = self.lhead = nyt
        self.loc = [None] * (HMAX + 1)
        self.loc[NYT] = nyt

    def get_ppnode(self):
        return self.free.pop() if self.free else _Cell()

    def free_ppnode(self, cell):
        self.free.append(cell)

    def swap(self, n1, n2):
        p1, p2 = n1.parent, n2.parent
        if p1:
            if p1.left is n1:
                p1.left = n2
            else:
                p1.right = n2
        else:
            self.tree = n2
        if p2:
            if p2.left is n2:
                p2.left = n1
            else:
                p2.right = n1
        else:
            self.tree = n1
        n1.parent, n2.parent = p2, p1

    @staticmethod
    def swaplist(n1, n2):
        n1.next, n2.next = n2.next, n1.next
        n1.prev, n2.prev = n2.prev, n1.prev
        if n1.next is n1:
            n1.next = n2
        if n2.next is n2:
            n2.next = n1
        if n1.next:
            n1.next.prev = n1
        if n2.next:
            n2.next.prev = n2
        if n1.prev:
            n1.prev.next = n1
        if n2.prev:
            n2.prev.next = n2

    def increment(self, node):
        while node is not None:
            if node.next is not None and node.next.weight == node.weight:
                lnode = node.head.node
                if lnode is not node.parent:
                    self.swap(lnode, node)
                self.swaplist(lnode, node)
            if node.prev and node.prev.weight == node.weight:
                node.head.node = node.prev
            else:
                node.head.node = None
                self.free_ppnode(node.head)
            node.weight += 1
            if node.next and node.next.weight == node.weight:
                node.head = node.next.head
            else:
                node.head = self.get_ppnode()
                node.head.node = node
            if not node.parent:
                return
            # recursion in huffman.c: increment(parent), then fix up the list
            parent = node.parent
            self.increment(parent)
            if node.prev is node.parent:
                self.swaplist(node, node.parent)
                if node.head.node is node:
                    node.head.node = node.parent
            return

    def add_ref(self, ch):
        if self.loc[ch] is None:
            tnode, tnode2 = _Node(), _Node()
            tnode2.symbol = INTERNAL_NODE
            tnode2.weight = 1
            tnode2.next = self.lhead.next
            if self.lhead.next:
                self.lhead.next.prev = tnode2
                if self.lhead.next.weight == 1:
                    tnode2.head = self.lhead.next.head
                else:
                    tnode2.head = self.get_ppnode()
                    tnode2.head.node = tnode2
            else:
                tnode2.head = self.get_ppnode()
                tnode2.head.node = tnode2
            self.lhead.next = tnode2
            tnode2.prev = self.lhead

            tnode.symbol = ch
            tnode.weight = 1
            tnode.next = self.lhead.next
            if self.lhead.next:
                self.lhead.next.prev = tnode
                if self.lhead.next.weight == 1:
                    tnode.head = self.lhead.next.head
                else:
                    tnode.head = self.get_ppnode()
                    tnode.head.node = tnode2
            else:
                tnode.head = self.get_ppnode()
                tnode.head.node = tnode
            self.lhead.next = tnode
            tnode.prev = self.lhead
            tnode.left = tnode.right = None

            if self.lhead.parent:
                if self.lhead.parent.left is self.lhead:
                    self.lhead.parent.left = tnode2
                else:
                    self.lhead.parent.right = tnode2
            else:
                self.tree = tnode2
            tnode2.right = tnode
            tnode2.left = self.lhead
            tnode2.parent = self.lhead.parent
            self.lhead.parent = tnode.parent = tnode2
            self.loc[ch] = tnode
            self.increment(tnode2.parent)
        else:
            self.increment(self.loc[ch])


class _BitWriter:
    def __init__(self):
        self.bits = []

    def add(self, b):
        self.bits.append(b)

    def to_bytes(self):
        out = bytearray((len(self.bits) + 7) // 8)
        for i, b in enumerate(self.bits):
            out[i >> 3] |= b << (i & 7)
        return bytes(out)


def _send(node, child, w):
    path = []
    while node is not None:
        if child is not None:
            path.append(1 if node.right is child else 0)
        child, node = node, node.parent
    for b in reversed(path):
        w.add(b)


def _transmit(h, ch, w):
    if h.loc[ch] is None:
        _transmit(h, NYT, w)
        for i in range(7, -1, -1):
            w.add((ch >> i) & 1)
    else:
        _send(h.loc[ch], None, w)


def compress(data):
    h, w = _Huff(), _BitWriter()
    for ch in data:
        _transmit(h, ch, w)
        h.add_ref(ch)
    size = len(data)
    # huffman.c: bloc starts at 16 (after the size), ends with bloc += 8 -> one spare byte
    body = w.to_bytes()
    nbytes = (16 + len(w.bits) + 8) >> 3
    out = bytes([size >> 8, size & 0xff]) + body
    return out + bytes(max(0, nbytes - len(out)))


def decompress(blob):
    size = (blob[0] << 8) | blob[1]
    bits = blob[2:]
    pos = 0

    def bit():
        nonlocal pos
        b = (bits[pos >> 3] >> (pos & 7)) & 1
        pos += 1
        return b

    h, out = _Huff(), bytearray()
    for _ in range(size):
        node = h.tree
        while node.symbol == INTERNAL_NODE:
            node = node.right if bit() else node.left
        ch = node.symbol
        if ch == NYT:
            ch = 0
            for _ in range(8):
                ch = (ch << 1) | bit()
        out.append(ch)
        h.add_ref(ch)
    return bytes(out)


if __name__ == '__main__':
    import os
    import random
    for n in (0, 1, 2, 7, 64, 500, 1400):
        for _ in range(20):
            d = bytes(random.choice(b'\\abcdefgkmnopqrstuvxyz0123456789"_ ') for _ in range(n))
            assert decompress(compress(d)) == d, (n, d)
    d = os.urandom(3000)
    assert decompress(compress(d)) == d
    print('q3huff: round-trip ok')
