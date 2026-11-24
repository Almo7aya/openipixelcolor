#!/usr/bin/env python3
"""Encode / decode firmware update files.

An update file sent over Bluetooth is the plain firmware image with every byte
transformed independently of its position:

    encode:  enc = ((dec ^ 0x4A) + 0x8A) & 0xFF
    decode:  dec = ((enc - 0x8A) & 0xFF) ^ 0x4A

The panel applies the reverse mapping itself when it receives an update
(firmware/src/app/ota.c: out = (in - 0x0A) ^ 0xCA, an equivalent form).

Usage:
    python fwcodec.py encode build/fw.bin [build/fw.ota.bin]
    python fwcodec.py decode update.bin [update.decoded.bin]
    python fwcodec.py check  update.bin update.decoded.bin
    python fwcodec.py info   build/fw.ota.bin    # OTA parameters for an ENCODED file
"""
import argparse
import hashlib
import sys
import zlib
from pathlib import Path

OTA_CHUNK = 0x3000        # 12 KiB per cmd 0xC001 packet
OTA_MAX_SIZE = 0xF7800    # ota_begin rejects larger images

XOR_KEY = 0x4A
ADD_KEY = 0x8A

ENCODE_TABLE = bytes(((b ^ XOR_KEY) + ADD_KEY) & 0xFF for b in range(256))
DECODE_TABLE = bytes(((b - ADD_KEY) & 0xFF) ^ XOR_KEY for b in range(256))
assert all(DECODE_TABLE[ENCODE_TABLE[b]] == b for b in range(256))


def encode(data: bytes) -> bytes:
    return data.translate(ENCODE_TABLE)


def decode(data: bytes) -> bytes:
    return data.translate(DECODE_TABLE)


def _default_out(src: Path, mode: str) -> Path:
    stem = src.stem.removesuffix('.decoded')
    return src.with_name(f'{stem}.decoded{src.suffix}' if mode == 'decode' else f'{stem}.encoded{src.suffix}')


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('decode', 'encode'):
        p = sub.add_parser(name)
        p.add_argument('src', type=Path)
        p.add_argument('dst', type=Path, nargs='?')
        p.add_argument('-f', '--force', action='store_true', help='overwrite dst if it exists')
    p = sub.add_parser('check', help='verify that DECODED == decode(ENCODED)')
    p.add_argument('encoded', type=Path)
    p.add_argument('decoded', type=Path)
    p = sub.add_parser('info', help='print the OTA parameters (size, CRC-32s, chunks) for an ENCODED image')
    p.add_argument('encoded', type=Path)
    p.add_argument('--chunks', action='store_true', help='also list per-chunk CRC-32s')
    a = ap.parse_args(argv)

    if a.cmd == 'info':
        enc = a.encoded.read_bytes()
        dec = decode(enc)
        n = (len(enc) + OTA_CHUNK - 1) // OTA_CHUNK
        print(f'size                 : {len(enc)} (0x{len(enc):X}){"  ** exceeds OTA max 0xF7800 **" if len(enc) > OTA_MAX_SIZE else ""}')
        print(f'chunks (12 KiB)      : {n}')
        print(f'crc32(encoded)       : 0x{zlib.crc32(enc):08X}   <- sent in cmd 0xC000 (ota_begin)')
        print(f'crc32(decoded)       : 0x{zlib.crc32(dec):08X}   <- stored by device in ota_info @flash 0x1FF000')
        print(f'decoded vector[0]    : {dec[:4].hex()} ({"NDS32 j" if dec[0] == 0x48 else "unexpected"})')
        if a.chunks:
            for i in range(n):
                c = enc[i * OTA_CHUNK:(i + 1) * OTA_CHUNK]
                print(f'  chunk {i:3d} off 0x{i * OTA_CHUNK:06X} len 0x{len(c):04X} crc32 0x{zlib.crc32(c):08X}')
        return 0

    if a.cmd == 'check':
        enc, dec = a.encoded.read_bytes(), a.decoded.read_bytes()
        if len(enc) != len(dec):
            print(f'size mismatch: {len(enc)} vs {len(dec)}')
            return 1
        out = decode(enc)
        bad = next((i for i, (x, y) in enumerate(zip(out, dec)) if x != y), None)
        if bad is not None:
            print(f'MISMATCH at 0x{bad:X}: decode gives {out[bad]:02X}, file has {dec[bad]:02X}')
            return 1
        print(f'OK: {len(enc)} bytes match (sha256 decoded {hashlib.sha256(dec).hexdigest()})')
        return 0

    dst = a.dst or _default_out(a.src, a.cmd)
    if dst.exists() and not a.force:
        print(f'{dst} exists; pass -f to overwrite', file=sys.stderr)
        return 1
    data = a.src.read_bytes()
    dst.write_bytes(decode(data) if a.cmd == 'decode' else encode(data))
    print(f'{a.cmd}d {len(data)} bytes -> {dst}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
