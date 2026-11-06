"""Writes fuzzer seeds in the format tests/fuzz/fuzz_load.cpp expects: two bytes with the .shp length / 4 (big endian),
the .shp, then the start of its .dbf. Only the smaller files in data/ are used.

  python tests/fuzz/make_corpus.py data corpus
"""

import pathlib
import sys

MAX_SHP = 60000


def main(data_dir, out_dir):
    out = pathlib.Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    for shp in sorted(pathlib.Path(data_dir).rglob("*.shp")):
        content = shp.read_bytes()
        if len(content) > MAX_SHP:
            continue
        dbf_path = shp.with_suffix(".dbf")
        dbf = dbf_path.read_bytes()[:4096] if dbf_path.exists() else b""
        content += b"\0" * (-len(content) % 4)
        words = len(content) // 4
        (out / (shp.stem + ".seed")).write_bytes(bytes([words >> 8 & 0xFF, words & 0xFF]) + content + dbf)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
