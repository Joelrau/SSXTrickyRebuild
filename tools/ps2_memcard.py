#!/usr/bin/env python3
"""List / extract files from a PS2 memory card image (PCSX2 .ps2, raw with or without ECC spare).
Usage:
  python ps2_memcard.py CARD.ps2 list
  python ps2_memcard.py CARD.ps2 extract DIRNAME OUTDIR     # extracts every file in /DIRNAME
"""
import os
import struct
import sys

MAGIC = b"Sony PS2 Memory Card Format "
DF_EXISTS, DF_DIR, DF_FILE = 0x8000, 0x0020, 0x0010
END = 0xFFFFFFFF


class Card:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.raw = f.read()
        if not self.raw.startswith(MAGIC):
            raise ValueError("not a PS2 memory card image")
        sb = self.raw[:0x154]
        (self.page_len, self.ppc, self.ppb, _u, self.clusters_per_card,
         self.alloc_offset, self.alloc_end, self.rootdir) = struct.unpack_from("<4H4I", sb, 0x28)
        self.ifc_list = struct.unpack_from("<32I", sb, 0x50)
        n_pages = self.clusters_per_card * self.ppc
        # Raw page stride: 512 data + 16 spare (ECC) if the image is that large.
        self.stride = self.page_len + 16 if len(self.raw) >= n_pages * (self.page_len + 16) else self.page_len
        self.cluster_size = self.page_len * self.ppc
        self.epc = self.cluster_size // 4  # u32 entries per cluster

    def cluster(self, abs_cluster):
        out = bytearray()
        for p in range(self.ppc):
            off = (abs_cluster * self.ppc + p) * self.stride
            out += self.raw[off:off + self.page_len]
        return bytes(out)

    def fat(self, rel):
        idx = rel // self.epc
        ind = struct.unpack_from("<I", self.cluster(self.ifc_list[idx // self.epc]), (idx % self.epc) * 4)[0]
        return struct.unpack_from("<I", self.cluster(ind), (rel % self.epc) * 4)[0]

    def chain_data(self, rel, size):
        out = bytearray()
        while rel != END and len(out) < size:
            out += self.cluster(rel + self.alloc_offset)
            e = self.fat(rel)
            if e == END or not (e & 0x80000000):
                break
            rel = e & 0x7FFFFFFF
        return bytes(out[:size])

    def read_dir(self, rel, count):
        data = self.chain_data(rel, count * 512)
        ents = []
        for i in range(count):
            e = data[i * 512:(i + 1) * 512]
            mode, length = struct.unpack_from("<HxxI", e, 0)
            clus = struct.unpack_from("<I", e, 0x10)[0]
            name = e[0x40:0x60].split(b"\0")[0].decode("ascii", "replace")
            mod = struct.unpack_from("<xBBBBBH", e, 0x18)   # sec, min, hour, day, month, year
            if mode & DF_EXISTS:
                ents.append(dict(name=name, mode=mode, length=length, cluster=clus, modified=mod))
        return ents

    def root(self):
        first = self.chain_data(self.rootdir, 512)
        count = struct.unpack_from("<I", first, 4)[0]
        return self.read_dir(self.rootdir, count)

    def listdir(self, name):
        for e in self.root():
            if e["name"] == name and e["mode"] & DF_DIR:
                return self.read_dir(e["cluster"], e["length"])
        raise KeyError(name)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    card = Card(argv[1])
    if argv[2] == "list":
        print(f"page {card.page_len}+{card.stride - card.page_len} spare, {card.ppc} pages/cluster, "
              f"{card.clusters_per_card} clusters, alloc_offset {card.alloc_offset}")
        for d in card.root():
            if d["name"] in (".", "..") or not d["mode"] & DF_DIR:
                continue
            s, mi, h, dd, mo, y = d["modified"]
            print(f"{d['name']}/  ({y:04d}-{mo:02d}-{dd:02d} {h:02d}:{mi:02d})")
            for f in card.read_dir(d["cluster"], d["length"]):
                if f["name"] in (".", ".."):
                    continue
                print(f"    {f['name']:<34} {f['length']:>8}")
    elif argv[2] == "extract":
        dirname, outdir = argv[3], argv[4]
        os.makedirs(outdir, exist_ok=True)
        for f in card.listdir(dirname):
            if f["mode"] & DF_FILE:
                data = card.chain_data(f["cluster"], f["length"])
                with open(os.path.join(outdir, f["name"]), "wb") as o:
                    o.write(data)
                print(f"extracted {f['name']} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
