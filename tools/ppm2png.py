#!/usr/bin/env python3
"""Convert a binary PPM (P6) to PNG using only the standard library.
Usage: ppm2png.py in.ppm out.png"""
import sys, zlib, struct

def read_ppm(path):
    data = open(path, 'rb').read()
    parts, pos = [], 0
    while len(parts) < 4:
        while data[pos:pos+1].isspace(): pos += 1
        start = pos
        while not data[pos:pos+1].isspace(): pos += 1
        parts.append(data[start:pos])
    assert parts[0] == b'P6'
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[pos+1:pos+1+w*h*3]

def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)

if __name__ == '__main__':
    write_png(sys.argv[2], *read_ppm(sys.argv[1]))
