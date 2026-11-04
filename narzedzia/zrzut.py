"""Zrzut ekranu Flippera przez RPC (bez plików .proto - ręczne kodowanie).

Użycie: python zrzut.py <port> <out.png> [skala]
"""
import struct
import sys
import time
import zlib

import serial


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def read_varint(buf, pos):
    shift = val = 0
    while True:
        b = buf[pos]
        pos += 1
        val |= (b & 0x7F) << shift
        if not b & 0x80:
            return val, pos
        shift += 7


def fields(msg):
    pos, out = 0, []
    while pos < len(msg):
        key, pos = read_varint(msg, pos)
        num, wt = key >> 3, key & 7
        if wt == 0:
            val, pos = read_varint(msg, pos)
        elif wt == 2:
            ln, pos = read_varint(msg, pos)
            val, pos = msg[pos:pos + ln], pos + ln
        else:
            raise ValueError(f"wire type {wt}")
        out.append((num, val))
    return out


def main_msg(command_id, field_num):
    body = b"\x08" + varint(command_id) + varint((field_num << 3) | 2) + b"\x00"
    return varint(len(body)) + body


def read_exact(s, n):
    data = b""
    deadline = time.time() + 5
    while len(data) < n:
        if time.time() > deadline:
            raise TimeoutError("brak danych z Flippera")
        data += s.read(n - len(data))
    return data


def read_msg(s):
    raw = b""
    while True:
        b = read_exact(s, 1)
        raw += b
        if not b[0] & 0x80:
            break
    ln, _ = read_varint(raw, 0)
    return read_exact(s, ln)


def write_png(path, pixels, scale):
    h, w = len(pixels), len(pixels[0])
    fg, bg = (0, 0, 0), (255, 140, 0)
    raw = bytearray()
    for row in pixels:
        line = bytearray()
        for p in row:
            line += bytes(fg if p else bg) * scale
        for _ in range(scale):
            raw += b"\x00" + line

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    ihdr = struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def input_msg(command_id, key, typ):
    inner = b"\x08" + varint(key) + b"\x10" + varint(typ)
    body = b"\x08" + varint(command_id) + varint((23 << 3) | 2) + varint(len(inner)) + inner
    return varint(len(body)) + body


def grab(port, press_back=False):
    s = serial.Serial(port, 230400, timeout=0.2)
    try:
        s.reset_input_buffer()
        s.write(b"\r")
        buf, deadline = b"", time.time() + 5
        while b">: " not in buf:
            if time.time() > deadline:
                raise TimeoutError("brak promptu CLI (zamknij qFlipper)")
            buf += s.read(256)
        time.sleep(0.1)
        s.reset_input_buffer()
        s.write(b"start_rpc_session\r")
        s.read_until(b"\n")
        if press_back:
            # BACK=5: PRESS(0), SHORT(2), RELEASE(1)
            for cid, typ in ((10, 0), (11, 2), (12, 1)):
                s.write(input_msg(cid, 5, typ))
                read_msg(s)
            time.sleep(1.0)
        s.write(main_msg(1, 20))  # gui_start_screen_stream_request
        for _ in range(20):
            msg = read_msg(s)
            for num, val in fields(msg):
                if num == 22:  # gui_screen_frame
                    data = dict(fields(val))[1]
                    s.write(main_msg(2, 21))  # gui_stop_screen_stream_request
                    s.write(main_msg(3, 19))  # stop_session
                    time.sleep(0.2)
                    return data
        raise RuntimeError("nie przyszła klatka ekranu")
    finally:
        s.close()


if __name__ == "__main__":
    port, out = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    data = grab(port, press_back="--back" in sys.argv)
    # Układ stron u8g2: 8 stron po 128 bajtów, bit 0 = górny piksel strony
    px = [[bool(data[(y // 8) * 128 + x] >> (y % 8) & 1) for x in range(128)] for y in range(64)]
    write_png(out, px, scale)
    print(f"zapisano {out}")
