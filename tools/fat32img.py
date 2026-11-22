#!/usr/bin/env python3
"""
fat32img.py - create / inspect / modify FAT32 disk images for NeoDeck-OS
without root and without mtools.

    fat32img.py create IMG [SIZE_MB=64]       new image (same layout as the kernel's `format`)
    fat32img.py put    IMG HOSTFILE [DEST]    copy a host file in (DEST like /bin/hello.bin)
    fat32img.py mkdir  IMG PATH
    fat32img.py ls     IMG [PATH]
    fat32img.py cat    IMG PATH
    fat32img.py check  IMG                    structural consistency check (mini fsck)

Only 8.3 names are created (same rule as the kernel); long names are read fine.
"""
import struct
import sys

SECTOR = 512
EOC = 0x0FFFFFF8
EOC_MARK = 0x0FFFFFFF
DEFAULT_DATE = 0x5821   # 2024-01-01


class FatError(Exception):
    pass


class Fat32:
    def __init__(self, path, writable=False):
        self.f = open(path, "r+b" if writable else "rb")
        s = self.read_sector(0)
        if s[510:512] != b"\x55\xAA" or struct.unpack_from("<H", s, 11)[0] != SECTOR:
            raise FatError("not a FAT32 superfloppy image (no BPB at sector 0)")
        (self.spc,) = struct.unpack_from("<B", s, 13)
        (self.reserved,) = struct.unpack_from("<H", s, 14)
        (self.nfats,) = struct.unpack_from("<B", s, 16)
        (self.total,) = struct.unpack_from("<I", s, 32)
        (self.fat_size,) = struct.unpack_from("<I", s, 36)
        (self.root,) = struct.unpack_from("<I", s, 44)
        (self.fsinfo,) = struct.unpack_from("<H", s, 48)
        if struct.unpack_from("<H", s, 22)[0] != 0 or self.fat_size == 0:
            raise FatError("not FAT32")
        self.data_lba = self.reserved + self.nfats * self.fat_size
        self.clusters = (self.total - self.data_lba) // self.spc
        self.fat = [struct.unpack_from("<I", self.read_sector(self.reserved + i // 128), (i % 128) * 4)[0] & 0x0FFFFFFF
                    for i in range(self.clusters + 2)] if False else None
        self._load_fat()

    # ---- raw io -------------------------------------------------------
    def read_sector(self, lba):
        self.f.seek(lba * SECTOR)
        d = self.f.read(SECTOR)
        return d.ljust(SECTOR, b"\0")

    def write_sector(self, lba, data):
        assert len(data) == SECTOR
        self.f.seek(lba * SECTOR)
        self.f.write(data)

    def cluster_lba(self, c):
        return self.data_lba + (c - 2) * self.spc

    # ---- FAT ----------------------------------------------------------
    def _load_fat(self):
        self.f.seek(self.reserved * SECTOR)
        raw = self.f.read(self.fat_size * SECTOR)
        self.fat = list(struct.unpack("<%dI" % (len(raw) // 4), raw))

    def _flush_fat(self):
        raw = struct.pack("<%dI" % len(self.fat), *self.fat)
        for i in range(self.nfats):
            self.f.seek((self.reserved + i * self.fat_size) * SECTOR)
            self.f.write(raw)

    def chain(self, c):
        out = []
        seen = set()
        while 2 <= c < self.clusters + 2:
            if c in seen:
                raise FatError("loop in cluster chain")
            seen.add(c)
            out.append(c)
            c = self.fat[c] & 0x0FFFFFFF
            if c >= EOC:
                break
        return out

    def alloc(self):
        for c in range(2, self.clusters + 2):
            if self.fat[c] & 0x0FFFFFFF == 0:
                self.fat[c] = EOC_MARK
                return c
        raise FatError("disk full")

    # ---- directories --------------------------------------------------
    def read_dir_raw(self, cluster):
        """yield (lba, offset, 32-byte entry) for every slot of the directory"""
        for c in self.chain(cluster):
            for s in range(self.spc):
                lba = self.cluster_lba(c) + s
                data = self.read_sector(lba)
                for i in range(SECTOR // 32):
                    yield lba, i * 32, data[i * 32:(i + 1) * 32]

    def list_dir(self, cluster):
        entries = []
        lfn_parts = {}
        lfn_chk = None
        for lba, off, e in self.read_dir_raw(cluster):
            if e[0] == 0x00:
                break
            if e[0] == 0xE5:
                lfn_parts = {}
                continue
            attr = e[11]
            if attr & 0x3F == 0x0F:
                seq = e[0] & 0x1F
                chars = e[1:11] + e[14:26] + e[28:32]
                text = chars.decode("utf-16-le")
                lfn_parts[seq] = text.split("\0")[0].rstrip("\uffff")
                lfn_chk = e[13]
                continue
            if attr & 0x08:
                lfn_parts = {}
                continue
            base = e[0:8].decode("ascii", "replace").rstrip()
            ext = e[8:11].decode("ascii", "replace").rstrip()
            if e[12] & 0x08:
                base = base.lower()
            if e[12] & 0x10:
                ext = ext.lower()
            name = base + ("." + ext if ext else "")
            if lfn_parts and lfn_chk == self.checksum(e[0:11]):
                name = "".join(lfn_parts[k] for k in sorted(lfn_parts))
            lfn_parts = {}
            hi, = struct.unpack_from("<H", e, 20)
            lo, = struct.unpack_from("<H", e, 26)
            size, = struct.unpack_from("<I", e, 28)
            entries.append(dict(name=name, attr=attr, cluster=(hi << 16) | lo, size=size, lba=lba, off=off))
        return entries

    @staticmethod
    def checksum(sfn):
        s = 0
        for b in sfn:
            s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
        return s

    def find(self, cluster, name):
        for e in self.list_dir(cluster):
            if e["name"].lower() == name.lower():
                return e
        return None

    def walk(self, path):
        """resolve a directory path -> cluster"""
        cur = self.root
        for comp in [p for p in path.split("/") if p]:
            e = self.find(cur, comp)
            if not e or not e["attr"] & 0x10:
                raise FatError("no such directory: " + path)
            cur = e["cluster"] or self.root
        return cur

    @staticmethod
    def make_sfn(name):
        if "." in name:
            base, ext = name.rsplit(".", 1)
        else:
            base, ext = name, ""
        ok = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789$%'-_@~!(){}^#&")
        if not (1 <= len(base) <= 8 and len(ext) <= 3) or base.count(".") or any(c not in ok for c in base + ext):
            raise FatError("name is not a valid 8.3 name: " + name)
        if (base != base.lower() and base != base.upper()) or (ext != ext.lower() and ext != ext.upper()):
            raise FatError("mixed-case names need a long name: " + name)
        nt = (0x08 if base.lower() == base and base.upper() != base else 0) | \
             (0x10 if ext.lower() == ext and ext.upper() != ext else 0)
        return (base.upper().ljust(8) + ext.upper().ljust(3)).encode("ascii"), nt

    def make_entry(self, sfn, nt, attr, cluster, size):
        e = bytearray(32)
        e[0:11] = sfn
        e[11] = attr
        e[12] = nt
        struct.pack_into("<HHH", e, 14, 0, DEFAULT_DATE, DEFAULT_DATE)
        struct.pack_into("<H", e, 20, cluster >> 16)
        struct.pack_into("<HH", e, 22, 0, DEFAULT_DATE)
        struct.pack_into("<H", e, 26, cluster & 0xFFFF)
        struct.pack_into("<I", e, 28, size)
        return bytes(e)

    def add_entry(self, dir_cluster, entry):
        last = None
        for lba, off, e in self.read_dir_raw(dir_cluster):
            if e[0] in (0x00, 0xE5):
                sec = bytearray(self.read_sector(lba))
                sec[off:off + 32] = entry
                self.write_sector(lba, bytes(sec))
                return
        ch = self.chain(dir_cluster)
        c = self.alloc()
        self.fat[ch[-1]] = c
        for s in range(self.spc):
            self.write_sector(self.cluster_lba(c) + s, bytes(SECTOR))
        sec = bytearray(SECTOR)
        sec[0:32] = entry
        self.write_sector(self.cluster_lba(c), bytes(sec))

    def write_chain(self, data):
        first = prev = 0
        cb = self.spc * SECTOR
        for pos in range(0, len(data), cb):
            c = self.alloc()
            if prev:
                self.fat[prev] = c
            else:
                first = c
            chunk = data[pos:pos + cb].ljust(cb, b"\0")
            for s in range(self.spc):
                self.write_sector(self.cluster_lba(c) + s, chunk[s * SECTOR:(s + 1) * SECTOR])
            prev = c
        return first

    def put(self, dest, data):
        parent_path, _, leaf = dest.rstrip("/").rpartition("/")
        parent = self.walk(parent_path)
        sfn, nt = self.make_sfn(leaf)
        old = self.find(parent, leaf)
        if old:
            if old["attr"] & 0x10:
                raise FatError("is a directory: " + dest)
            for c in self.chain(old["cluster"]) if old["cluster"] else []:
                self.fat[c] = 0
            sec = bytearray(self.read_sector(old["lba"]))
            sec[old["off"]] = 0xE5
            self.write_sector(old["lba"], bytes(sec))
        first = self.write_chain(data)
        self.add_entry(parent, self.make_entry(sfn, nt, 0x20, first, len(data)))
        self.sync()

    def mkdir(self, dest):
        parent_path, _, leaf = dest.rstrip("/").rpartition("/")
        parent = self.walk(parent_path)
        sfn, nt = self.make_sfn(leaf)
        if self.find(parent, leaf):
            raise FatError("exists: " + dest)
        c = self.alloc()
        for s in range(self.spc):
            self.write_sector(self.cluster_lba(c) + s, bytes(SECTOR))
        sec = bytearray(SECTOR)
        sec[0:32] = self.make_entry(b".".ljust(11), 0, 0x10, c, 0)
        sec[32:64] = self.make_entry(b"..".ljust(11), 0, 0x10, 0 if parent == self.root else parent, 0)
        self.write_sector(self.cluster_lba(c), bytes(sec))
        self.add_entry(parent, self.make_entry(sfn, nt, 0x10, c, 0))
        self.sync()

    def read_file(self, path):
        parent_path, _, leaf = path.rstrip("/").rpartition("/")
        e = self.find(self.walk(parent_path), leaf)
        if not e or e["attr"] & 0x10:
            raise FatError("no such file: " + path)
        out = b""
        for c in (self.chain(e["cluster"]) if e["cluster"] else []):
            for s in range(self.spc):
                out += self.read_sector(self.cluster_lba(c) + s)
        return out[:e["size"]]

    def free_count(self):
        return sum(1 for c in range(2, self.clusters + 2) if self.fat[c] & 0x0FFFFFFF == 0)

    def sync(self):
        self._flush_fat()
        s = bytearray(self.read_sector(self.fsinfo))
        struct.pack_into("<II", s, 488, self.free_count(), 2)
        self.write_sector(self.fsinfo, bytes(s))
        self.f.flush()

    # ---- fsck ---------------------------------------------------------
    def check(self):
        problems = []
        # both FAT copies identical
        self.f.seek(self.reserved * SECTOR)
        f1 = self.f.read(self.fat_size * SECTOR)
        for i in range(1, self.nfats):
            self.f.seek((self.reserved + i * self.fat_size) * SECTOR)
            if self.f.read(self.fat_size * SECTOR) != f1:
                problems.append("FAT copy %d differs from FAT 1" % (i + 1))
        if self.fat[0] & 0x0FFFFFFF != 0x0FFFFFF8 or self.fat[1] & 0x0FFFFFFF < EOC:
            problems.append("FAT entries 0/1 are wrong")

        used = {}

        def claim(chain, owner):
            for c in chain:
                if c in used:
                    problems.append("cluster %d claimed by both %s and %s" % (c, used[c], owner))
                used[c] = owner

        def scan(cluster, path):
            ch = self.chain(cluster)
            if not ch or self.fat[ch[-1]] & 0x0FFFFFFF < EOC:
                problems.append("directory %s chain not terminated" % path)
            claim(ch, path)
            ents = self.list_dir(cluster)
            names = [e["name"].lower() for e in ents]
            if len(set(names)) != len(names):
                problems.append("duplicate names in " + path)
            for e in ents:
                full = path.rstrip("/") + "/" + e["name"]
                if e["name"] in (".", ".."):
                    if e["name"] == "." and e["cluster"] != cluster:
                        problems.append("'.' in %s points to %d" % (path, e["cluster"]))
                    continue
                if e["attr"] & 0x10:
                    scan(e["cluster"], full)
                else:
                    ch2 = self.chain(e["cluster"]) if e["cluster"] else []
                    need = -(-e["size"] // (self.spc * SECTOR))
                    if len(ch2) != need:
                        problems.append("%s: size %d needs %d clusters, chain has %d" % (full, e["size"], need, len(ch2)))
                    if ch2 and self.fat[ch2[-1]] & 0x0FFFFFFF < EOC:
                        problems.append("%s: chain not terminated" % full)
                    claim(ch2, full)

        scan(self.root, "/")
        for c in range(2, self.clusters + 2):
            if self.fat[c] & 0x0FFFFFFF != 0 and c not in used:
                problems.append("cluster %d is allocated but unreferenced (leak)" % c)
        s = self.read_sector(self.fsinfo)
        fc, = struct.unpack_from("<I", s, 488)
        if fc != 0xFFFFFFFF and fc != self.free_count():
            problems.append("FSInfo free count %d != real %d" % (fc, self.free_count()))
        return problems


def create(path, size_mb=64):
    total = size_mb * 1024 * 1024 // SECTOR
    spc, reserved, nfats = 1, 32, 2
    t1 = total - reserved
    t2 = (256 * spc + nfats) // 2
    fat_size = (t1 + t2 - 1) // t2
    clusters = (total - reserved - nfats * fat_size) // spc

    with open(path, "wb") as f:
        f.truncate(total * SECTOR)
        bs = bytearray(SECTOR)
        bs[0:3] = b"\xEB\x58\x90"
        bs[3:11] = b"NEODECK "
        struct.pack_into("<HBHBHHBHHHI", bs, 11, SECTOR, spc, reserved, nfats, 0, 0, 0xF8, 0, 63, 255, 0)
        struct.pack_into("<I", bs, 32, total)
        struct.pack_into("<I", bs, 36, fat_size)
        struct.pack_into("<HHI", bs, 40, 0, 0, 2)
        struct.pack_into("<HH", bs, 48, 1, 6)
        bs[64] = 0x80
        bs[66] = 0x29
        struct.pack_into("<I", bs, 67, 0x4E454F44)
        bs[71:82] = b"NEODECK    "
        bs[82:90] = b"FAT32   "
        bs[510:512] = b"\x55\xAA"
        fi = bytearray(SECTOR)
        struct.pack_into("<I", fi, 0, 0x41615252)
        struct.pack_into("<I", fi, 484, 0x61417272)
        struct.pack_into("<II", fi, 488, clusters - 1, 3)
        struct.pack_into("<I", fi, 508, 0xAA550000)
        for lba, data in ((0, bs), (6, bs), (1, fi), (7, fi)):
            f.seek(lba * SECTOR)
            f.write(bytes(data))
        fat = bytearray(fat_size * SECTOR)
        struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0x0FFFFFFF, EOC_MARK)
        for i in range(nfats):
            f.seek((reserved + i * fat_size) * SECTOR)
            f.write(bytes(fat))
    fs = Fat32(path, writable=True)
    for d in ("home", "bin", "etc", "var", "tmp"):
        fs.mkdir("/" + d)
    fs.put("/readme.txt",
           b"Welcome to NeoDeck OS! This is a simple text file created on the root directory "
           b"of your FAT32 virtual disk. Feel free to explore the filesystem, create new "
           b"directories and files, and write your own content.\n")
    return fs


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, img = argv[1], argv[2]
    try:
        if cmd == "create":
            create(img, int(argv[3]) if len(argv) > 3 else 64)
            print("created %s" % img)
        elif cmd == "put":
            fs = Fat32(img, True)
            host = argv[3]
            dest = argv[4] if len(argv) > 4 else "/" + host.replace("\\", "/").rsplit("/", 1)[-1]
            with open(host, "rb") as h:
                data = h.read()
            fs.put(dest, data)
            print("put %s -> %s (%d bytes)" % (host, dest, len(data)))
        elif cmd == "mkdir":
            Fat32(img, True).mkdir(argv[3])
        elif cmd == "ls":
            fs = Fat32(img)
            for e in fs.list_dir(fs.walk(argv[3] if len(argv) > 3 else "/")):
                print("%s %10d  %s" % ("d" if e["attr"] & 0x10 else "-", e["size"], e["name"]))
        elif cmd == "cat":
            sys.stdout.buffer.write(Fat32(img).read_file(argv[3]))
        elif cmd == "check":
            fs = Fat32(img)
            problems = fs.check()
            for p in problems:
                print("PROBLEM:", p)
            print("OK" if not problems else "%d problem(s)" % len(problems))
            return 1 if problems else 0
        else:
            print(__doc__)
            return 2
    except FatError as e:
        print("error:", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
