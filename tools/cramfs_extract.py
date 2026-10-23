#!/usr/bin/env python3
"""Extract readable content from a cramfs image without a cramfs tool.

The updater initrd (nflasha1/boot/INITRD.IMG) is cramfs. No cramfsck/mkcramfs
is available here, and the inode bitfields of this build did not parse with the
standard layout, so this uses a different route: scan every 4-byte-aligned
position for a zlib header, inflate what succeeds, and concatenate the results
in file order.

Caveat: cramfs stores a block UNCOMPRESSED when compression does not help, and
those blocks carry no zlib header, so they are skipped. That is fine for
recovering text (shell scripts) - the script bodies reproduced correctly - but
it leaves gaps in larger binaries, so ELF images carved from the result are
truncated. Use it for scripts and string tables, not for whole binaries.

    python3 tools/cramfs_extract.py IN.IMG OUT.bin
"""
import sys
import zlib


def extract(img: bytes):
    blocks = []
    for i in range(0, len(img) - 2, 4):
        if img[i] == 0x78 and img[i + 1] in (0x01, 0x5E, 0x9C, 0xDA):
            try:
                out = zlib.decompressobj().decompress(img[i:i + 65536])
            except zlib.error:
                continue
            if len(out) >= 20:
                blocks.append((i, out))
    blocks.sort()
    return len(blocks), b"".join(o for _, o in blocks)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    img = open(sys.argv[1], "rb").read()
    n, flat = extract(img)
    open(sys.argv[2], "wb").write(flat)
    print("%d zlib blocks -> %d bytes of readable content"
          % (n, len(flat)))


if __name__ == "__main__":
    main()
