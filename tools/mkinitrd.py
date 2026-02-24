#!/usr/bin/env python3
import os, struct, sys

MAGIC = 0x44495244  # 'DIRD'
NAME_SIZE = 32

def pack_name(name: str) -> bytes:
    b = name.encode("ascii", errors="ignore")[:NAME_SIZE-1]
    return b + b"\x00" * (NAME_SIZE - len(b))

def build(initrd_root: str, out_path: str):
    files = []
    for entry in sorted(os.listdir(initrd_root)):
        p = os.path.join(initrd_root, entry)
        if os.path.isfile(p):
            with open(p, "rb") as f:
                data = f.read()
            files.append((entry, data))

    n = len(files)
    header_size = 8
    dirent_size = NAME_SIZE + 4 + 4
    dir_size = header_size + n * dirent_size

    offset = dir_size
    dirents = []
    blob = bytearray()
    for name, data in files:
        dirents.append((name, offset, len(data)))
        blob += data
        offset += len(data)

    with open(out_path, "wb") as out:
        out.write(struct.pack("<II", MAGIC, n))
        for name, off, length in dirents:
            out.write(pack_name(name))
            out.write(struct.pack("<II", off, length))
        out.write(blob)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: mkinitrd.py <initrd_root_dir> <out_file>")
        sys.exit(1)
    build(sys.argv[1], sys.argv[2])
