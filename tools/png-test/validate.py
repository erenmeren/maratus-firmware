#!/usr/bin/env python3
# Verify mono1.png is a valid 1-bit grayscale PNG that decodes losslessly back to
# the bilevel source pattern (source.bin). Independent decoder: parse chunks,
# inflate IDAT, unpack 1-bit MSB-first scanlines, compare to thresholded source.
import struct, zlib, sys

def read_chunks(b):
    assert b[:8] == bytes([0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a]), "bad PNG signature"
    i = 8
    chunks = []
    while i < len(b):
        ln = struct.unpack(">I", b[i:i+4])[0]
        typ = b[i+4:i+8].decode("ascii")
        data = b[i+8:i+8+ln]
        crc = struct.unpack(">I", b[i+8+ln:i+12+ln])[0]
        assert zlib.crc32(b[i+4:i+8+ln]) & 0xffffffff == crc, f"bad CRC on {typ}"
        chunks.append((typ, data))
        i += 12 + ln
    return chunks

with open("source.bin","rb") as f:
    raw = f.read()
W, H = struct.unpack("<II", raw[:8])
src = raw[8:]

with open("mono1.png","rb") as f:
    png = f.read()
chunks = read_chunks(png)
ihdr = dict(zip(("w","h","depth","color","comp","filter","interlace"),
                struct.unpack(">IIBBBBB", next(d for t,d in chunks if t=="IHDR"))))
assert ihdr["w"] == W and ihdr["h"] == H, f"dim mismatch {ihdr['w']}x{ihdr['h']} != {W}x{H}"
assert ihdr["depth"] == 1, f"bit depth {ihdr['depth']} != 1"
assert ihdr["color"] == 0, f"color type {ihdr['color']} != 0 (grayscale)"
assert ihdr["interlace"] == 0

idat = b"".join(d for t,d in chunks if t=="IDAT")
rawdec = zlib.decompress(idat)  # STORED blocks decompress fine
stride = (W + 7) // 8
assert len(rawdec) == H * (1 + stride), "decoded scanline length mismatch"

mismatches = 0
for y in range(H):
    assert rawdec[y*(1+stride)] == 0, "filter byte not 0"
    row = rawdec[y*(1+stride)+1 : y*(1+stride)+1+stride]
    for x in range(W):
        bit = (row[x>>3] >> (7-(x&7))) & 1     # 1=white
        want = 1 if src[y*W+x] >= 128 else 0
        if bit != want:
            mismatches += 1

if mismatches:
    print(f"FAIL: {mismatches} pixel mismatch(es)"); sys.exit(1)
print(f"OK: {W}x{H} 1-bit PNG decodes losslessly ({H*(1+stride)}B raw, all {W*H} px match)")
