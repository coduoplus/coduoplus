"""Send one rcon command to a CoD/Quake 3 server and print the reply.

    python -I tools/re/q3rcon.py <host> <port> <password> <command...>

The tests use `systeminfo`: it prints the CS_SYSTEMINFO string exactly as clients
receive it, i.e. proof that sv_coduoplus_swing / sv_competitive* really go out.
"""
import socket
import sys


def rcon(host, port, password, command, timeout=2.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    out = []
    try:
        s.sendto(b'\xff\xff\xff\xffrcon ' + password.encode() + b' ' + command.encode(), (host, port))
        while True:  # long replies come in several packets
            data, _ = s.recvfrom(65536)
            text = data[4:].decode('latin1')
            out.append(text[6:] if text.startswith('print\n') else text)
            s.settimeout(0.3)
    except (socket.timeout, ConnectionResetError):
        pass
    finally:
        s.close()
    return ''.join(out)


def info_from(reply):
    """systeminfo prints `key   value` lines; return them as a dict."""
    d = {}
    for line in reply.splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2:
            d[parts[0]] = parts[1].strip()
    return d


if __name__ == '__main__':
    print(rcon(sys.argv[1], int(sys.argv[2]), sys.argv[3], ' '.join(sys.argv[4:])), end='')
