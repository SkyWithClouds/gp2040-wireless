"""Check the application's UF2 against the linked binary and NOLOGO memory map."""
import struct
import sys
from pathlib import Path

APP_START, APP_LIMIT = 0x26000, 0xEC000
FAMILY = 0xADA52840


def check(uf2, binary):
    if not uf2 or len(uf2) % 512 or len(binary) < 8:
        raise ValueError("empty or truncated build output")
    payload = bytearray()
    blocks = len(uf2) // 512
    for i in range(blocks):
        block = uf2[i * 512:(i + 1) * 512]
        magic0, magic1, flags, addr, size, index, count, family = struct.unpack_from("<8I", block)
        if (magic0, magic1, block[-4:]) != (0x0A324655, 0x9E5D5157, b"\x30\x6f\xb1\x0a"):
            raise ValueError(f"bad magic in block {i}")
        if (flags, index, count, family) != (0x2000, i, blocks, FAMILY):
            raise ValueError(f"bad UF2 metadata in block {i}")
        if size != 256 or addr != APP_START + len(payload) or addr + size > APP_LIMIT:
            raise ValueError(f"unexpected write at {addr:#x} in block {i}")
        payload.extend(block[32:32 + size])
    if len(payload) != (len(binary) + 255) // 256 * 256 or payload[:len(binary)] != binary:
        raise ValueError("UF2 payload does not match zephyr.bin")
    sp, reset = struct.unpack_from("<2I", binary)
    if not (0x20000000 < sp <= 0x20040000 and sp % 8 == 0 and reset & 1 and
            APP_START <= reset - 1 < APP_START + len(binary)):
        raise ValueError("invalid linked stack/reset vector")
    print(f"Verified {blocks} blocks, family {FAMILY:#x}, start {APP_START:#x}, reset {reset:#x}")


if __name__ == "__main__":
    check(Path(sys.argv[1]).read_bytes(), Path(sys.argv[2]).read_bytes())
