#!/usr/bin/env python3
"""Probe MegaFlash GETUSERSETTINGS + READBLOCK over a2bus TCP (Bramble already running)."""
import argparse
import socket
import struct
import sys
import time

OPS = dict(PING=0x00, PHI=0x01, READ=0x02, WRITE=0x03, PEEK=0x04)


def connect(port: int) -> socket.socket:
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def rpc(s: socket.socket, op: int, *payload: int) -> int:
    s.sendall(bytes((op,) + payload))
    rsp = s.recv(2)
    if len(rsp) != 2 or rsp[0] != 0:
        raise OSError(f"bad rsp {rsp!r} for op={op:#x}")
    return rsp[1]


def wait_idle(s: socket.socket, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        st = rpc(s, OPS["PEEK"], 0)
        if (st & 0x80) == 0:
            return
        time.sleep(0.001)
    raise TimeoutError("STATUS still BUSY")


def cmd(s: socket.socket, c: int) -> None:
    rpc(s, OPS["WRITE"], 0, c & 0xFF)
    wait_idle(s)


def activate(s: socket.socket) -> None:
    # MegaFlash activation: C0C2, C0C0, C0C0, C0C3, C0C1
    rpc(s, OPS["READ"], 2)
    rpc(s, OPS["READ"], 0)
    rpc(s, OPS["READ"], 0)
    rpc(s, OPS["READ"], 3)
    rpc(s, OPS["READ"], 1)
    # ID should become 0x96
    for _ in range(20):
        v = rpc(s, OPS["PEEK"], 3)
        if v in (0x96, 0x69):
            print(f"activated id={v:#x}")
            return
        time.sleep(0.05)
    print(f"WARN id still {rpc(s, OPS['PEEK'], 3):#x}")


def read_data(s: socket.socket, n: int) -> bytes:
    out = bytearray()
    for _ in range(n):
        out.append(rpc(s, OPS["READ"], 2))
    return bytes(out)


def write_param(s: socket.socket, *vals: int) -> None:
    for v in vals:
        rpc(s, OPS["WRITE"], 1, v & 0xFF)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=19765)
    args = ap.parse_args()
    s = connect(args.port)
    print(f"PING={rpc(s, OPS['PING']):#x}")
    activate(s)

    cmd(s, 0x03)  # MODELINEAR
    cmd(s, 0x21)  # GETUSERSETTINGS
    raw = read_data(s, 7)
    print("GETUSERSETTINGS:", raw.hex(" "), list(raw))
    ver, chk, c1, c2, tzv, tzi, fd = raw
    ok = ver == 2 and chk == (2 ^ 0x5A) and tzv == 1
    print(f"validate={'OK' if ok else 'FAIL'} checkbyte={chk:#x} expect={2^0x5A:#x}")

    cmd(s, 0x00)  # reset ptrs
    write_param(s, 1, 0, 0, 0)
    cmd(s, 0x15)  # READBLOCK unit1 blk0
    cmd(s, 0x04)  # MODEINTERLEAVED
    head = read_data(s, 8)
    print("READBLOCK0 interleaved head:", head.hex(" "))
    expect = bytes([0x01, 0x91, 0x38, 0x60, 0xB0, 0xC8, 0x03, 0xD0])
    print(f"block0 match={'OK' if head == expect else 'FAIL'} expect={expect.hex(' ')}")

    # Probe unit status for units 1..6
    for u in range(1, 7):
        cmd(s, 0x00)
        write_param(s, u)
        cmd(s, 0x12)
        b0 = rpc(s, OPS["READ"], 1)
        b1 = rpc(s, OPS["READ"], 1)
        b2 = rpc(s, OPS["READ"], 1)
        st = rpc(s, OPS["PEEK"], 0)
        print(f"UNITSTATUS {u}: blocks={b0:02x}{b1:02x}{b2:02x} status={st:#x}")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
