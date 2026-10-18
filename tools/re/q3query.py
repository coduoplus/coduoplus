"""Send a Quake 3 connectionless query to a server and print the reply.

    python -I tools/re/q3query.py <host> <port> [getinfo|getstatus] [--wait SECONDS]

Used by the smoke test to prove a dedicated CoDUOMP.exe running the mod is up,
and to read what it advertises (protocol, shortversion, map, cvars).
--wait keeps retrying (1 s apart) until the server answers or time runs out,
which covers the seconds a server spends loading its map.
"""
import socket
import sys
import time


def query(host, port, cmd, timeout=1.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        s.sendto(b'\xff\xff\xff\xff' + cmd.encode() + b' xxx', (host, port))
        data, _ = s.recvfrom(65536)
        return data[4:].decode('latin1')
    except (socket.timeout, ConnectionResetError):
        return None
    finally:
        s.close()


def main():
    host, port = sys.argv[1], int(sys.argv[2])
    cmd = sys.argv[3] if len(sys.argv) > 3 and not sys.argv[3].startswith('--') else 'getinfo'
    wait = float(sys.argv[sys.argv.index('--wait') + 1]) if '--wait' in sys.argv else 0
    deadline = time.time() + wait
    while True:
        reply = query(host, port, cmd)
        if reply is not None or time.time() >= deadline:
            break
        time.sleep(1)
    if reply is None:
        print('no reply from %s:%d' % (host, port))
        sys.exit(1)
    head, _, rest = reply.partition('\n')
    print(head)
    info = rest.split('\n')[0]
    parts = info.split('\\')[1:]
    for k, v in zip(parts[::2], parts[1::2]):
        print('  %-24s %s' % (k, v))
    players = [p for p in rest.split('\n')[1:] if p.strip()]
    if players:
        print('  players: %d' % len(players))


if __name__ == '__main__':
    main()
