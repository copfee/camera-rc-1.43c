"""Convert headless renderer P6 output to PNG with Python's standard library."""
import struct
import sys
import zlib
from pathlib import Path


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def convert(source, target):
    with Path(source).open("rb") as stream:
        if stream.readline().strip() != b"P6":
            raise ValueError("Expected renderer P6 file")
        width, height = map(int, stream.readline().split())
        if stream.readline().strip() != b"255":
            raise ValueError("Expected 8-bit RGB")
        pixels = stream.read()
    if len(pixels) != width * height * 3:
        raise ValueError("Truncated pixels")
    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    Path(target).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    convert(*sys.argv[1:])
