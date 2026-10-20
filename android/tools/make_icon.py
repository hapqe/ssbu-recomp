"""Writes the launcher icon (android/app/res/mipmap-*/ic_launcher.png): our own simple artwork, nothing of Nintendo's
(deliberately no circle-and-cross emblem). A violet round badge with a white "play" triangle. Pure Python (zlib)."""
import math
import os
import struct
import sys
import zlib

def png(path, size):
    rows = []
    c = (size - 1) / 2.0
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            dx, dy = (x - c) / c, (y - c) / c
            r = math.hypot(dx, dy)
            a = max(0.0, min(1.0, (0.96 - r) * size / 2))            # anti-aliased disc edge
            t = (dy + 1) / 2
            col = [int(40 + 30 * t), int(20 + 20 * t), int(90 + 60 * t)]   # deep blue-violet gradient
            # a play triangle: points (-0.32,-0.42) (-0.32,0.42) (0.45,0); soft edge
            e1 = dx + 0.32
            e2 = 0.42 - 0.42 / 0.77 * (dx + 0.32) - abs(dy)
            w = max(0.0, min(1.0, min(e1, e2) * size / 4))
            col = [int(v * (1 - w) + 245 * w) for v in col]
            row += bytes(col) + bytes([int(255 * a)])
        rows.append(bytes(row))
    raw = zlib.compress(b''.join(rows), 9)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) + chunk(b'IDAT', raw) + chunk(b'IEND', b'')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)

res = sys.argv[1]
for name, size in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
    png(os.path.join(res, 'mipmap-' + name, 'ic_launcher.png'), size)
