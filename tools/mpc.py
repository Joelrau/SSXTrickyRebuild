"""SSX Tricky movies (DATA/VIDEO/*.MPC) -> MPEG-2 elementary stream, transport stream and WAV.
    python tools/mpc.py extracted/DATA/VIDEO converted/frontend/video [NAME ...]
(default: every .MPC in the source folder - the boot movies EABIG / SSXINTRO, the rank movies RR_*, the DVD extras
EVOLVE / TRICKS / TRACKS / CHARACTR / MUSIC / AUDIO / CV_* / SLED2PR / NBAPROMO; the port warns and skips a movie
that was not converted)
"""
# 0x2BA810
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import struct
import sys

FRAME_RATES = {1: (24000, 1001), 2: (24, 1), 3: (25, 1), 4: (30000, 1001), 5: (30, 1), 6: (50, 1),
               7: (60000, 1001), 8: (60, 1)}

EA_XA_COEF1 = (0, 240, 460, 392)
EA_XA_COEF2 = (0, 0, -208, -220)


def read_chunks(path):
    with open(path, 'rb') as f:
        d = f.read()
    o = 0
    while o + 8 <= len(d):
        tag = d[o:o + 4]
        size, = struct.unpack_from('<I', d, o + 4)
        if size < 8:
            raise ValueError('%s: bad chunk at 0x%x' % (path, o))
        yield tag, d[o + 8:o + size]
        o += size


def parse_schl(payload):
    """EA 'PT' patch header -> dict of the tags we need."""
    info = {'platform': struct.unpack_from('<H', payload, 2)[0]}
    names = {0x82: 'channels', 0x84: 'sample_rate', 0x85: 'num_samples', 0xA0: 'codec', 0x80: 'version'}
    p = 4
    while p < len(payload):
        tag = payload[p]
        p += 1
        if tag == 0xFF:
            break
        if tag in (0xFC, 0xFD, 0xFE):
            continue
        n = payload[p]
        p += 1
        val = int.from_bytes(payload[p:p + n], 'big')
        p += n
        if tag in names:
            info[names[tag]] = val
    return info


def decode_ea_xa_channel(block, start, samples, out, hist):
    """Decode one channel of one SCDl block (EA-XA) and append to out (list of ints)."""
    hist1, hist2 = hist
    p = start + 4
    left = samples
    c1t, c2t = EA_XA_COEF1, EA_XA_COEF2
    append = out.append
    while left > 0:
        info = block[p]
        c1 = c1t[info >> 4 & 3]
        c2 = c2t[info >> 4 & 3]
        sh = 20 - (info & 0x0F)
        n = min(28, left)
        for i in range(n):
            b = block[p + 1 + (i >> 1)]
            nib = (b & 0x0F) if (i & 1) else (b >> 4)
            if nib >= 8:
                nib -= 16
            s = (((nib << sh) if sh >= 0 else (nib >> -sh)) + c1 * hist1 + c2 * hist2 + 128) >> 8
            if s > 32767:
                s = 32767
            elif s < -32768:
                s = -32768
            hist2 = hist1
            hist1 = s
            append(s)
        left -= n
        p += 15
    hist[0], hist[1] = hist1, hist2


def downmix(chans):
    """N channels -> stereo. 6-channel order (from channel correlation on SSXINTRO): FL C FR SL SR LFE,
    i.e. L C R Ls Rs LFE as in AC-3."""
    n = len(chans)
    if n == 1:
        return [chans[0], chans[0]]
    if n == 2:
        return chans
    import numpy as np
    length = min(len(c) for c in chans)
    a = np.array([c[:length] for c in chans], dtype=np.float64)
    if n >= 6:
        fl, c, fr, sl, sr, lfe = a[:6]
        left = 0.55 * fl + 0.39 * c + 0.39 * sl + 0.2 * lfe
        right = 0.55 * fr + 0.39 * c + 0.39 * sr + 0.2 * lfe
    else:
        left, right = a[0], a[1]
    clip = lambda x: np.clip(x, -32768, 32767).astype(np.int16).tolist()
    return [clip(left), clip(right)]


def write_wav(path, chans, rate):
    import array
    n = min(len(c) for c in chans)
    inter = array.array('h', [0]) * (n * len(chans))
    for ci, c in enumerate(chans):
        inter[ci::len(chans)] = array.array('h', c[:n])
    data = inter.tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, len(chans), rate, rate * 2 * len(chans), 2 * len(chans), 16))
        f.write(b'data' + struct.pack('<I', len(data)) + data)


def find_start_codes(v):
    out = []
    i = v.find(b'\x00\x00\x01')
    while i >= 0 and i + 3 < len(v):
        out.append((i, v[i + 3]))
        i = v.find(b'\x00\x00\x01', i + 3)
    return out


def split_access_units(v):
    """Split an MPEG video ES into access units: each starts at a sequence/GOP header or picture
    start and contains exactly one picture. Returns [(bytes, picture_type, temporal_ref, gop_start)]."""
    codes = find_start_codes(v)
    units = []
    unit_start = 0
    have_pic = False
    pic = None
    gop_flag = False
    pending_gop = False
    for pos, code in codes:
        if code in (0xB3, 0xB8, 0x00):
            if have_pic and (code in (0xB3, 0xB8) or code == 0x00):
                units.append((v[unit_start:pos], pic[0], pic[1], gop_flag))
                unit_start = pos
                have_pic = False
                gop_flag = False
            if code == 0xB8:
                pending_gop = True
            if code == 0x00:
                tr = (v[pos + 4] << 2) | (v[pos + 5] >> 6)
                ptype = (v[pos + 5] >> 3) & 7
                pic = (ptype, tr)
                have_pic = True
                gop_flag = pending_gop
                pending_gop = False
        elif code == 0xB7:
            pass
    if have_pic:
        units.append((v[unit_start:], pic[0], pic[1], gop_flag))
    return units


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def bits(self, s):
        for c in s:
            self.put(int(c), 1)

    def align(self):
        while self.n:
            self.put(0, 1)
        return bytes(self.out)


MBA_VLC = ['1', '011', '010', '0011', '0010', '00011', '00010', '0000111', '0000110', '00001011', '00001010',
           '00001001', '00001000', '00000111', '00000110', '0000010111', '0000010110', '0000010101', '0000010100',
           '0000010011', '0000010010', '00000100011', '00000100010', '00000100001', '00000100000', '00000011111',
           '00000011110', '00000011101', '00000011100', '00000011011', '00000011010', '00000011001', '00000011000']


def mba_bits(inc):
    s = ''
    while inc > 33:
        s += '00000001000'
        inc -= 33
    return s + MBA_VLC[inc - 1]


def repeat_picture(v, tref):
    """An MPEG-2 P-picture that repeats the previous anchor exactly: every slice codes its first and last
    macroblock as 'MC, not coded' with a zero motion vector and skips the rest (skipped P macroblocks are
    zero-vector copies). The picture coding extension flags are copied from the stream's last picture."""
    sh = v.find(b'\x00\x00\x01\xB3')
    w = (v[sh + 4] << 4) | (v[sh + 5] >> 4)
    h = ((v[sh + 5] & 0x0F) << 8) | v[sh + 6]
    se = v.find(b'\x00\x00\x01\xB5', sh)
    progressive_seq = (v[se + 5] >> 3) & 1 if se >= 0 and (v[se + 4] >> 4) == 1 else 1
    pic = v.rfind(b'\x00\x00\x01\x00')
    pce = v.find(b'\x00\x00\x01\xB5', pic)
    flags = (int.from_bytes(v[pce + 6:pce + 9], 'big') >> 6) & 0x3FFF
    frame_pred_frame_dct = (flags >> 8) & 1
    mbw = (w + 15) // 16
    mbh = (h + 15) // 16 if progressive_seq else 2 * ((h + 31) // 32)
    b = BitWriter()
    b.put(0x00000100, 32)
    b.put(tref & 0x3FF, 10)
    b.put(2, 3)
    b.put(0xFFFF, 16)
    b.put(0, 1)
    b.put(7, 3)
    b.put(0, 1)
    out = bytearray(b.align())
    b = BitWriter()
    b.put(0x000001B5, 32)
    b.put(8, 4)
    b.put(0x11FF, 16)
    flags &= ~((1 << 7) | (1 << 3) | 1)
    flags |= 3 << 10
    b.put(flags, 14)
    out += b.align()
    mv = ('' if frame_pred_frame_dct else '10') + '1' + '1'
    for row in range(mbh):
        b = BitWriter()
        b.put(0x00000101 + row, 32)
        b.put(1, 5)
        b.put(0, 1)
        b.bits('1' + '001' + mv)
        if mbw > 1:
            b.bits(mba_bits(mbw - 1) + '001' + mv)
        out += b.align()
    return bytes(out)


def hold_pictures(units, v, count):
    """Append `count` repeat pictures to the last GOP (before a trailing sequence_end_code)."""
    if count <= 0 or not units:
        return units
    es, ptype, tref, gop = units[-1]
    tail = b''
    if es.endswith(b'\x00\x00\x01\xB7'):
        es, tail = es[:-4], es[-4:]
    last_gop = 0
    for i, u in enumerate(units):
        if u[3]:
            last_gop = i
    top = max(u[2] for u in units[last_gop:])
    units = units[:-1] + [(es, ptype, tref, gop)]
    for k in range(count):
        units.append((repeat_picture(v, top + 1 + k), 2, top + 1 + k, False))
    if tail:
        e = units[-1]
        units[-1] = (e[0] + tail,) + e[1:]
    return units


def crc32_mpeg(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


def ts_timestamp(prefix, t):
    return bytes([
        (prefix << 4) | (((t >> 30) & 7) << 1) | 1,
        (t >> 22) & 0xFF,
        (((t >> 15) & 0x7F) << 1) | 1,
        (t >> 7) & 0xFF,
        ((t & 0x7F) << 1) | 1,
    ])


class TsWriter:
    PMT_PID = 0x100
    VIDEO_PID = 0x1011

    def __init__(self, f, bdav=True):
        self.f = f
        self.bdav = bdav
        self.cc = {}

    def _packet(self, pid, payload, pusi, pcr=None, arrival=0):
        cc = self.cc.get(pid, 0)
        self.cc[pid] = (cc + 1) & 15
        header = bytearray([0x47, (0x40 if pusi else 0) | (pid >> 8), pid & 0xFF, 0])
        adapt = b''
        need_adapt = pcr is not None or len(payload) < 184
        if need_adapt:
            flags = 0x10 if pcr is not None else 0
            body = bytearray([flags])
            if pcr is not None:
                base = pcr
                body += bytes([(base >> 25) & 0xFF, (base >> 17) & 0xFF, (base >> 9) & 0xFF, (base >> 1) & 0xFF,
                               ((base & 1) << 7) | 0x7E, 0])
            room = 184 - 1 - len(body)
            take = min(room, len(payload))
            stuffing = room - take
            body += b'\xff' * stuffing
            adapt = bytes([len(body)]) + bytes(body)
            payload = payload[:take]
            header[3] = 0x30 | cc
        else:
            payload = payload[:184]
            header[3] = 0x10 | cc
        pkt = bytes(header) + adapt + payload
        assert len(pkt) == 188, len(pkt)
        if self.bdav:
            self.f.write(struct.pack('>I', arrival & 0x3FFFFFFF))
        self.f.write(pkt)
        return len(payload)

    def write_psi(self, arrival=0):
        pat = bytearray([0x00, 0xB0, 0, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01, 0xE0 | (self.PMT_PID >> 8),
                         self.PMT_PID & 0xFF])
        pat[2] = len(pat) - 3 + 4
        pat += struct.pack('>I', crc32_mpeg(pat))
        self._packet(0, b'\x00' + bytes(pat) + b'\xff' * (183 - len(pat)), True, arrival=arrival)
        pmt = bytearray([0x02, 0xB0, 0, 0x00, 0x01, 0xC1, 0x00, 0x00, 0xE0 | (self.VIDEO_PID >> 8),
                         self.VIDEO_PID & 0xFF, 0xF0, 0x00,
                         0x02, 0xE0 | (self.VIDEO_PID >> 8), self.VIDEO_PID & 0xFF, 0xF0, 0x00])
        pmt[2] = len(pmt) - 3 + 4
        pmt += struct.pack('>I', crc32_mpeg(pmt))
        self._packet(self.PMT_PID, b'\x00' + bytes(pmt) + b'\xff' * (183 - len(pmt)), True, arrival=arrival)

    def write_pes(self, es, pts, dts, pcr):
        has_dts = dts is not None and dts != pts
        hdr_data = ts_timestamp(3 if has_dts else 2, pts) + (ts_timestamp(1, dts) if has_dts else b'')
        pes = b'\x00\x00\x01\xE0' + b'\x00\x00' + bytes([0x80, 0xC0 if has_dts else 0x80, len(hdr_data)]) + hdr_data + es
        first = True
        pos = 0
        arrival = pcr * 300
        while pos < len(pes):
            n = self._packet(self.VIDEO_PID, pes[pos:], first, pcr=pcr if first else None, arrival=arrival)
            pos += n
            first = False


def write_m2ts(path, units, rate):
    """Mux video access units into a transport stream with 90 kHz PTS/DTS from the picture headers."""
    num, den = rate
    tick = 90000 * den / num
    delay = 2
    with open(path, 'wb') as f:
        ts = TsWriter(f, bdav=path.lower().endswith('.m2ts'))
        gop_base = 0
        gop_max = -1
        for i, (es, ptype, tref, gop_start) in enumerate(units):
            if gop_start:
                gop_base = gop_base + gop_max + 1 if i > 0 else 0
                gop_max = -1
            gop_max = max(gop_max, tref)
            pts = int(round((gop_base + tref + delay) * tick)) + 9000
            dts = int(round((i + delay - 1) * tick)) + 9000
            if i % 15 == 0:
                ts.write_psi(arrival=max(0, dts - 4500) * 300)
            ts.write_pes(es, pts, dts, max(0, dts - 4500))


def convert(path, out_dir):
    name = os.path.splitext(os.path.basename(path))[0].upper()
    video = bytearray()
    audio_info = None
    chans = None
    for tag, payload in read_chunks(path):
        if tag == b'MPCh':
            video += payload
        elif tag == b'SCHl':
            audio_info = parse_schl(payload)
            chans = [[] for _ in range(audio_info.get('channels', 1))]
            hists = [[0, 0] for _ in chans]
        elif tag == b'SCDl' and chans is not None:
            nch = len(chans)
            samples, = struct.unpack_from('<I', payload, 0)
            offs = struct.unpack_from('<%dI' % nch, payload, 4)
            base = 4 + 4 * nch
            for c in range(nch):
                decode_ea_xa_channel(payload, base + offs[c], samples, chans[c], hists[c])
    os.makedirs(out_dir, exist_ok=True)
    base = os.path.join(out_dir, name)
    with open(base + '.m2v', 'wb') as f:
        f.write(video)
    sh = video.find(b'\x00\x00\x01\xB3')
    w = (video[sh + 4] << 4) | (video[sh + 5] >> 4)
    h = ((video[sh + 5] & 0x0F) << 8) | video[sh + 6]
    rate_code = video[sh + 7] & 0x0F
    rate = FRAME_RATES.get(rate_code, (30000, 1001))
    mpeg2 = video.find(b'\x00\x00\x01\xB5', sh) == sh + 12 or b'\x00\x00\x01\xB5' in video[:200]
    units = split_access_units(bytes(video))
    frames = len(units)
    hold = 0
    if audio_info and chans and audio_info.get('sample_rate') and mpeg2:
        audio_s = min(len(c) for c in chans) / audio_info['sample_rate']
        hold = max(0, int(audio_s * rate[0] / rate[1]) - frames)
    write_m2ts(base + '.m2ts', hold_pictures(units, bytes(video), hold), rate)
    facts = dict(name=name, width=w, height=h, fps=rate[0] / rate[1], frames=frames,
                 video='MPEG-2' if mpeg2 else 'MPEG-1', duration=frames * rate[1] / rate[0], hold_pictures=hold)
    if audio_info and chans:
        facts['audio'] = dict(audio_info, decoded_samples=min(len(c) for c in chans))
        stereo = downmix(chans)
        write_wav(base + '.wav', stereo, audio_info.get('sample_rate', 48000))
    with open(base + '.json', 'w') as f:
        json.dump(facts, f, indent=1)
    print(json.dumps(facts))
    return facts


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    names = sys.argv[3:] or sorted(f[:-4] for f in os.listdir(src) if f.upper().endswith('.MPC'))
    for n in names:
        convert(os.path.join(src, n.upper() + '.MPC'), dst)
    return 0


if __name__ == '__main__':
    sys.exit(main())
