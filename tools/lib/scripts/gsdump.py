"""Minimal PCSX2 2.x GS dump (.gs / .gs.zst) parser: header, initial state, GIF packets -> primitive list.

load(path) -> (hdr, state bytes, privileged regs bytes, prims)
Each prim: idx (draw order), xfer, prim, type, ctxt, verts [(x16, y16, z, s, t, q, uv, rgba)],
plus a snapshot of the context regs TEST, ZBUF, ALPHA, TEX0, FRAME, XYOFFSET, CLAMP, TEX1.
"""
import struct, sys

def _read(path):
    data = open(path, 'rb').read()
    if path.endswith('.zst'):
        import zstandard
        data = zstandard.ZstdDecompressor().stream_reader(data).read()
    return data

CTX_REGS = {0x06: 'TEX0', 0x08: 'CLAMP', 0x14: 'TEX1', 0x18: 'XYOFFSET', 0x40: 'SCISSOR', 0x42: 'ALPHA',
            0x47: 'TEST', 0x4A: 'FBA', 0x4C: 'FRAME', 0x4E: 'ZBUF', 0x34: 'MIPTBP1', 0x36: 'MIPTBP2', 0x16: 'TEX2'}

def ctx_reg(addr):
    for base, name in CTX_REGS.items():
        if addr == base: return name, 0
        if addr == base + 1: return name, 1
    return None, None

def load(path):
    d = _read(path)
    assert struct.unpack_from('<I', d, 0)[0] == 0xFFFFFFFF
    hsize = struct.unpack_from('<I', d, 4)[0]
    h = struct.unpack_from('<9I', d, 8)
    hdr = dict(state_version=h[0], state_size=h[1], serial_off=h[2], serial_size=h[3], crc=h[4],
               shot_w=h[5], shot_h=h[6], shot_off=h[7], shot_size=h[8])
    hbase = 8
    hdr['serial'] = d[hbase + h[2]: hbase + h[2] + h[3]].decode('ascii', 'replace')
    hdr['shot'] = d[hbase + h[7]: hbase + h[7] + h[8]]
    pos = 8 + hsize
    state = d[pos: pos + h[1]]; pos += h[1]
    regs = d[pos: pos + 8192]; pos += 8192
    st = {'PRIM': 0, 'RGBAQ': 0, 'ST': (0.0, 0.0), 'Q': 1.0, 'UV': 0}
    ctx = [dict(), dict()]
    o = 4
    env = ['PRIM', 'PRMODECONT', 'TEXCLUT', 'SCANMSK', 'TEXA', 'FOGCOL', 'DIMX', 'DTHE', 'COLCLAMP', 'PABE',
           'BITBLTBUF', 'TRXDIR', 'TRXPOS', 'TRXREG', 'TRXREG2']
    for n in env:
        st[n] = struct.unpack_from('<Q', state, o)[0]; o += 8
    for c in range(2):
        for n in ['XYOFFSET', 'TEX0', 'TEX1', 'TEX2', 'CLAMP', 'MIPTBP1', 'MIPTBP2', 'SCISSOR', 'ALPHA', 'TEST', 'FBA', 'FRAME', 'ZBUF']:
            ctx[c][n] = struct.unpack_from('<Q', state, o)[0]; o += 8
    hdr['init_ctx'] = [dict(c) for c in ctx]
    prims = []
    verts = []
    events = []
    cur = {'xfer': 0}

    def emit(vs):
        c = (st['PRIM'] >> 9) & 1
        rec = {'idx': len(prims), 'xfer': cur['xfer'], 'prim': st['PRIM'], 'type': st['PRIM'] & 7, 'ctxt': c,
               'verts': list(vs)}
        for k in ('TEST', 'ZBUF', 'ALPHA', 'TEX0', 'FRAME', 'XYOFFSET', 'CLAMP', 'TEX1'):
            rec[k] = ctx[c].get(k, 0)
        prims.append(rec)

    def kick(x, y, z, adc):
        k = st['PRIM'] & 7
        s, t = st['ST']
        verts.append((x, y, z, s, t, st['Q'], st['UV'], st['RGBAQ']))
        n = len(verts)
        if k == 3:
            if n == 3:
                if not adc: emit(verts)
                verts.clear()
        elif k == 4:
            if n >= 3:
                if not adc: emit(verts[-3:])
                del verts[:-2]
        elif k == 5:
            if n >= 3:
                if not adc: emit([verts[0], verts[-2], verts[-1]])
                del verts[1:-1]
        elif k in (1, 6):
            if n == 2:
                if not adc: emit(verts)
                verts.clear()
        elif k == 2:
            if n >= 2:
                if not adc: emit(verts[-2:])
                del verts[:-1]
        else:
            if not adc: emit(verts)
            verts.clear()

    def write_reg(a, v):
        if a == 0x00:
            st['PRIM'] = v; verts.clear()
        elif a == 0x01:
            st['RGBAQ'] = v & 0xFFFFFFFF
            st['Q'] = struct.unpack('<f', struct.pack('<I', v >> 32))[0]
        elif a == 0x02:
            st['ST'] = struct.unpack('<2f', struct.pack('<Q', v))
        elif a == 0x03:
            st['UV'] = v
        elif a in (0x04, 0x0C):
            kick(v & 0xFFFF, (v >> 16) & 0xFFFF, (v >> 32) & 0xFFFFFF, a == 0x0C)
        elif a in (0x05, 0x0D):
            kick(v & 0xFFFF, (v >> 16) & 0xFFFF, v >> 32, a == 0x0D)
        else:
            n, c = ctx_reg(a)
            if n:
                ctx[c][n] = v
            else:
                st[a] = v

    def gif(buf):
        i = 0
        L = len(buf)
        while i + 16 <= L:
            lo, hi = struct.unpack_from('<QQ', buf, i); i += 16
            nloop = lo & 0x7FFF
            pre = (lo >> 46) & 1
            flg = (lo >> 58) & 3
            nreg = (lo >> 60) & 0xF or 16
            descs = [(hi >> (4 * j)) & 0xF for j in range(nreg)]
            if flg == 0:
                if pre:
                    write_reg(0x00, (lo >> 47) & 0x7FF)
                for _ in range(nloop):
                    for dsc in descs:
                        if i + 16 > L: return
                        a, b = struct.unpack_from('<QQ', buf, i); i += 16
                        if dsc == 0x0:
                            write_reg(0x00, a & 0x7FF)
                        elif dsc == 0x1:
                            st['RGBAQ'] = (a & 0xFF) | ((a >> 32) & 0xFF) << 8 | (b & 0xFF) << 16 | ((b >> 32) & 0xFF) << 24
                        elif dsc == 0x2:
                            st['ST'] = struct.unpack('<2f', struct.pack('<Q', a))
                            st['Q'] = struct.unpack('<f', struct.pack('<I', b & 0xFFFFFFFF))[0]
                        elif dsc == 0x3:
                            st['UV'] = (a & 0x3FFF) | ((a >> 32) & 0x3FFF) << 16
                        elif dsc == 0x4:
                            kick(a & 0xFFFF, (a >> 32) & 0xFFFF, (b >> 4) & 0xFFFFFF, (b >> 47) & 1)
                        elif dsc == 0x5:
                            kick(a & 0xFFFF, (a >> 32) & 0xFFFF, b & 0xFFFFFFFF, (b >> 47) & 1)
                        elif dsc == 0xE:
                            write_reg(b & 0xFF, a)
                        elif dsc == 0xF:
                            pass
                        else:
                            write_reg(dsc, a)
            elif flg == 1:
                n = nloop * nreg
                for j in range(n):
                    a = struct.unpack_from('<Q', buf, i + 8 * j)[0]
                    write_reg(descs[j % nreg], a)
                i += ((n + 1) // 2) * 16
            else:
                i += nloop * 16

    while pos < len(d):
        t = d[pos]; pos += 1
        if t == 0:
            size = struct.unpack_from('<I', d, pos + 1)[0]; pos += 5
            gif(d[pos: pos + size]); pos += size; cur['xfer'] += 1
        elif t == 1:
            pos += 1; events.append(('vsync', len(prims)))
        elif t == 2:
            pos += 4
        elif t == 3:
            pos += 8192
        else:
            raise ValueError('bad packet %d at %d' % (t, pos))
    hdr['xfers'] = cur['xfer']; hdr['events'] = events
    return hdr, state, regs, prims

if __name__ == '__main__':
    hdr, state, regs, prims = load(sys.argv[1])
    print({k: v for k, v in hdr.items() if k not in ('shot', 'events', 'init_ctx')}, hdr['events'][:10])
    print('prims', len(prims))
