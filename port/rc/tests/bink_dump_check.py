"""Offline reader for scratch/bink_dump.txt (RC_BINK_DUMP, port/rc/tods/tods_bink_dump.inc). python3 -I bink_dump_check.py DUMP.txt
Prints, per traced 0x801E88 call: the raw arguments, which arguments equal a known movie plane buffer, every read_dcs / read_colors
bundle window as int16 / bytes, the flat-block pixel each DC would give at quant 0 (pix = ((DC*0x10000 >> 11) + 0x7F) >> 8 = DC/8; pass 24),
and the plane bytes logged after the call (flat runs marked).
No game data is read by this script beyond the dump file you pass."""
import re, sys, struct
PLANES = {0x22909500: "Y0", 0x22A5AD00: "Y1", 0x229EA500: "U1a", 0x22B3BD00: "U1b", 0x22A22900: "U2a", 0x22B74100: "U2b"}
def hexbytes(s): return bytes.fromhex(s.replace(" ", ""))
call = None
colour_arrays = {}   # call -> [(tag, ea, bytes)]
planes_seen = {}     # (call, plane index) -> row bytes
for line in open(sys.argv[1], errors="replace"):
    mc = re.match(r"\[bink\] (read_colors\w*) bundle-after\+(00|04)-> ea=(\w+):\s*(.*)", line)
    if mc and call is not None: colour_arrays.setdefault(call, []).append((mc.group(1) + "+" + mc.group(2), mc.group(3), hexbytes(mc.group(4))))
    mp = re.match(r"\[bink\] plane(\d) (\w+) row(\d+) col(\d+) ea=(\w+):\s*(.*)", line)
    if mp and call is not None: planes_seen[(call, int(mp.group(1)))] = (mp.group(2), int(mp.group(3)), int(mp.group(4)), hexbytes(mp.group(6)))
    line = line.rstrip()
    m = re.match(r"\[bink\] call=(\d+) t=\S+ lr=(\S+) sp=(\S+) r3\.\.r10 = (.*)", line)
    if m:
        call = int(m.group(1)); args = [int(x, 16) for x in m.group(4).split()]
        print("== call", call, "lr", m.group(2), "args", [hex(a) for a in args])
        for r, a in zip(range(3, 11), args):
            for b, n in PLANES.items():
                if b <= a < b + 0x151800: print("   arg r%d = plane %s + 0x%X" % (r, n, a - b))
        continue
    m = re.match(r"(\[bink\] (?:read_dcs|read_colors\w*) bundle-after\+(\w+)->) ea=(\w+):\s*(.*)", line)
    if m:
        b = hexbytes(m.group(4))
        if "read_dcs" in line:
            v = struct.unpack(">%dh" % (len(b) // 2), b)
            print("  ", m.group(1), "ea", m.group(3), "int16:", list(v[:64]))
            print("      flat pixel at q0 (DC/8 clamp-free):", [(x * 0x10000 >> 11) + 0x7F >> 8 for x in v[:64]])
        else:
            print("  ", m.group(1), "ea", m.group(3), "bytes:", b[:128].hex(" "))
        continue
    m = re.match(r"\[bink\] plane(\d) (\w+) row(\d+) col(\d+) ea=(\w+):\s*(.*)", line)
    if m:
        b = hexbytes(m.group(6)); flat = [i for i in range(1, len(b)) if b[i] == b[i - 1]]
        print("   plane%s %s row %s col %s: %s  (equal neighbours: %d of %d)" % (m.group(1), m.group(2), m.group(3), m.group(4), b.hex(" "), len(flat), len(b) - 1))
        continue
    if line.startswith("[bink] frame-stream"):
        print("  ", line[:300]); continue
    if line.startswith("[bink] stream") or line.startswith("[bink] init") or "memset16" in line or "bitreader" in line:
        print("  ", line[:300])

# RAW-block finder (pass 5): a RAW block's 64 colour bytes are copied into the plane unchanged, so any 8-byte stretch of the logged plane
# row that occurs in a colour bundle array tells the block was RAW and where its row sits (offset // 8 % 8 = the row inside the block)
print("== RAW-block matches (plane row bytes found in the colour bundle arrays of the same call)")
for (c, i), (nm, row, col, b) in sorted(planes_seen.items()):
    for k in range(0, len(b) - 7, 8):
        seg = b[k:k + 8]
        for tag, ea, arr in colour_arrays.get(c, []):
            j = arr.find(seg)
            if j >= 0 and any(seg):
                print("   call %d plane%d %s row %d cols %d..%d found in %s ea=%s offset %d (row %d of the 8x8 block if the bundle data starts at 64)" % (c, i, nm, row, col + k, col + k + 7, tag, ea, j, ((j - 64) // 8) % 8))
                break
