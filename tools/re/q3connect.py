"""Perform the connectionless half of a CoD:UO client connect and print the verdict.

    python -I tools/re/q3connect.py <host> <port> [key=value ...]

Sends getchallenge, then `connect "<userinfo>"` the way the client does: protocol 22,
and everything after "connect " Huffman-compressed (the server Huff_Decompress-es
it, see q3huff.py). Prints the server's answer:
  connectResponse        -> ClientConnect accepted the client
  print / error <text>   -> refused, with the reason (e.g. our version gate)
Extra key=value pairs are added to the userinfo (e.g. coduoplus=5). The server
then waits for the client's first packet and times the slot out - nothing else
is needed to clean up. Used by tests/smoke_dedicated.py to exercise the
server-side ClientConnect hook for real.
"""
import os
import random
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from q3huff import compress  # noqa: E402

OOB = b'\xff\xff\xff\xff'


def exchange(sock, addr, payload, timeout=3.0):
    sock.settimeout(timeout)
    sock.sendto(OOB + payload, addr)
    data, _ = sock.recvfrom(65536)
    return data[4:].decode('latin1')


def connect(host, port, extra=None):
    addr = (host, port)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        reply = exchange(s, addr, b'getchallenge')
        if not reply.startswith('challengeResponse'):
            return 'unexpected challenge reply: %r' % reply
        challenge = reply.split()[1]
        info = {
            'challenge': challenge, 'qport': str(random.randint(1000, 60000)),
            'protocol': '22', 'name': 'coduoplus-probe', 'rate': '25000', 'snaps': '20',
            'cl_punkbuster': '0', 'cl_anonymous': '0', 'cg_predictItems': '1',
        }
        info.update(extra or {})
        userinfo = ''.join('\\%s\\%s' % kv for kv in info.items())
        return exchange(s, addr, b'connect ' + compress(('"%s"' % userinfo).encode('latin1')))
    except socket.timeout:
        return 'timeout'
    finally:
        s.close()


if __name__ == '__main__':
    extra = dict(a.split('=', 1) for a in sys.argv[3:])
    print(connect(sys.argv[1], int(sys.argv[2]), extra).strip())
