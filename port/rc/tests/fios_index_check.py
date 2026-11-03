#!/usr/bin/env python3
"""Check FIOS's hdd1 disc-cache index (cache.idx) for block dates that can never match.

Layout read off a ToD index (R&C pass 19; numbers only):
  0x00  "fioscach" magic, 8 bytes of header hash
  0x18  u32 record count (1792 on ToD)   0x1C  u32 block size (0x100000)
  0x200 records, (file size - 0x200) / count bytes each (0x200 on ToD):
        +0x00 u64 last-use stamp  +0x08 u64 modification date (ns)  +0x10 u64 second date (ns)
        +0x18 u64 file offset     +0x20 u32 block length            +0x28 path (NUL-terminated)
FIOS (func_007B27B8 @0x007B27B8) gives a newly filled block the oldest date of the file's other indexed
blocks, clamped to >= 1, and stats the file only when it has no other block; a read whose date mismatches
(func_007AE968 @0x007AE968) does not invalidate the block. So one block dated 0 or 1 (written while
cellFsStat/cellFsFstat returned mtime 0, before patch 7011) makes every later fill of that file 1, and
those blocks are dropped at every read. The per-file report shows such files.

Usage (game-derived file: run inside tools/sandbox.sh):
  fios_index_check.py CACHE_IDX [--disc USRDIR]   exit 1 if any indexed block has a date <= 1
  fios_index_check.py --selftest
"""
import os
import struct
import sys
import tempfile
from collections import defaultdict

HDR = 0x200


def parse(data):
    if data[:8] != b"fioscach":
        raise ValueError("not a FIOS cache index (magic)")
    count, block = struct.unpack(">II", data[0x18:0x20])
    if count == 0 or (len(data) - HDR) % count:
        raise ValueError("record count %d does not divide the file" % count)
    rsz = (len(data) - HDR) // count
    rows = []
    for i in range(count):
        r = data[HDR + i * rsz: HDR + (i + 1) * rsz]
        stamp, mdate, date2, off, length = struct.unpack(">QQQQI", r[:0x24])
        path = r[0x28:].split(b"\0", 1)[0].decode("utf-8", "replace")
        rows.append((i, stamp, mdate, date2, off, length, path))
    return block, rows


def report(data, disc=None, out=sys.stdout):
    block, rows = parse(data)
    files = defaultdict(list)
    for row in rows:
        if row[6]:
            files[row[6]].append(row)
    bad_total = 0
    for path in sorted(files):
        lst = files[path]
        bad = [r for r in lst if r[2] <= 1]
        bad_total += len(bad)
        size = None
        if disc:
            try:
                size = os.stat(disc + path).st_size
            except OSError:
                pass
        past = [r for r in lst if size is not None and r[4] >= size]
        dates = sorted({r[2] for r in lst if r[2] > 1})
        out.write("%-60s blocks %4d  date<=1 %4d  past-EOF %d  dates %s\n" % (
            path[-60:], len(lst), len(bad), len(past), ",".join(str(d) for d in dates[:3]) or "-"))
    out.write("indexed blocks %d, date<=1 %d, free %d\n" % (
        sum(len(v) for v in files.values()), bad_total, sum(1 for r in rows if not r[6])))
    return bad_total


def selftest():
    def rec(stamp, mdate, off, length, path):
        r = struct.pack(">QQQQI", stamp, mdate, mdate, off, length) + b"\0" * 4 + path.encode() + b"\0"
        return r + b"\0" * (0x200 - len(r))
    hdr = b"fioscach" + b"\0" * 8 + b"\0" * 8 + struct.pack(">II", 4, 0x100000)
    hdr += b"\0" * (HDR - len(hdr))
    real = 1194543948000000000
    good = hdr + rec(5, real, 0, 0x100000, "/a.psarc") + rec(6, real, 0x100000, 0x100000, "/a.psarc") \
        + rec(0, 0, 0, 0, "") + rec(0, 0, 0, 0, "")
    poisoned = hdr + rec(5, 0, 0, 0x100000, "/b.psarc") + rec(6, 1, 0x100000, 0x100000, "/b.psarc") \
        + rec(7, real, 0, 0x100000, "/a.psarc") + rec(0, 1, 0, 0, "")
    sink = open(os.devnull, "w")
    assert report(good, out=sink) == 0
    assert report(poisoned, out=sink) == 2
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(d + "/x")
        with open(d + "/a.psarc", "wb") as f:
            f.write(b"\0" * 0x100000)
        block, rows = parse(good)
        assert block == 0x100000 and rows[1][4] == 0x100000
        lines = []

        class W:
            def write(self, s):
                lines.append(s)
        report(good, disc=d, out=W())
        assert "past-EOF 1" in lines[0], lines[0]
    try:
        parse(b"xx" + good[2:])
        raise AssertionError("bad magic accepted")
    except ValueError:
        pass
    print("fios_index_check selftest: OK")
    return 0


def main(argv):
    if argv[1:] == ["--selftest"]:
        return selftest()
    if len(argv) not in (2, 4) or (len(argv) == 4 and argv[2] != "--disc"):
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1], "rb") as f:
        data = f.read()
    disc = argv[3] if len(argv) == 4 else None
    return 1 if report(data, disc) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
