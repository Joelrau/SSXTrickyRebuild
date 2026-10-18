"""Speech playback model: EA speech event databases (Events.evt + *.hdr), the request
engine (pending slots, priorities, chance, repeat limit, follow-on chains, channel dispatch) and
variant / segment / sample selection (weights, least-recently-played, no-repeat random).

Playback logic only: the model
never reads or decodes audio, it returns which sample of which group plays and where its bytes are.

Integer semantics follow the EE (unsigned 32-bit RNG, C truncating division).
"""
import os
import struct

M32 = (1 << 32) - 1

# database id (evt byte 8) per SPEECH.INF section, and the engine channel each one is registered on
DB_FE, DB_MC, DB_CHAR, DB_NARR, DB_ANIM = 0, 1, 3, 4, 5
CHANNEL_OF_DB = {DB_MC: 0, DB_NARR: 0, DB_CHAR: 1, DB_ANIM: 1, DB_FE: 0}
SECTIONS = {"fe": DB_FE, "mc": DB_MC, "char": DB_CHAR, "narr": DB_NARR, "anim": DB_ANIM}

N_SLOTS = 16          # pending request slots
REQUEST_WORDS = 20    # word 0 = request id, words 1..19 = parameters
MAX_LINES = 8         # lines in one prepared play list
WEIGHT_MULT = [1, 4, 16, 64, 256, 1024, 4096, 16384]   # variant weight = (b & 31) * WEIGHT_MULT[b >> 5]
BYTES_PER_SECOND = 22050 * 16 // 8 // 10                 # 4410: MicroTalk 10:1 at the nominal 22050 Hz
FLAG_CAN_SHORTEN, FLAG_KEEP_ON_NEW, FLAG_PARAM_MASK, FLAG_FOLLOW_LIST, FLAG_FOLLOW_ON = 1, 4, 8, 16, 32


def request_id(db, event_id):
    """Request word 0 = event id | db << 24 (byte 2 = evt byte 9 = 0)."""
    return (db << 24) | event_id


def read_bigf(path):
    """BIGF archive: big-endian count at +8, entries {u32 offset, u32 size, name\\0} from +16."""
    data = open(path, "rb").read()
    count = struct.unpack_from(">I", data, 8)[0]
    pos, out = 16, {}
    for _ in range(count):
        offset, size = struct.unpack_from(">II", data, pos)
        end = data.index(b"\0", pos + 8)
        out[data[pos + 8:end].decode()] = data[offset:offset + size]
        pos = end + 1
    return out


def _align4(n):
    return (n + 3) & ~3


class Segment:
    def __init__(self, blob, pos):
        self.group_id, self.param_index, self.kind, self.n_cond = struct.unpack_from("<HBBb", blob, pos)
        self.cond_param = list(blob[pos + 8:pos + 12])          # per condition: request word index, 0/0xFE/0xFF special
        self.cond_mask = [struct.unpack_from("<I", blob, pos + 16 + 4 * j)[0] for j in range(max(self.n_cond, 0))]


class Variant:
    def __init__(self, blob, pos):
        self.weight_byte, self.ctx_mask, self.ctx_value, b3 = blob[pos:pos + 4]
        self.mode_rule = b3 & 3            # 0 always, 1 only in mode 2, 2 not in mode 2
        self.segments = [Segment(blob, pos + blob[pos + 4 + s] * 4) for s in range(b3 >> 2)]
        self.n_segments = b3 >> 2

    @property
    def weight(self):
        return (self.weight_byte & 31) * WEIGHT_MULT[self.weight_byte >> 5]

    def mode_ok(self, mode):
        if self.mode_rule == 0:
            return True
        if self.mode_rule == 1:
            return mode == 2
        if self.mode_rule == 2:
            return mode != 2
        return False


class Event:
    def __init__(self, blob, pos):
        (self.id, self.timeout, self.priority, nv, ncb, self.max_repeats, self.chance,
         self.flags) = struct.unpack_from("<HHHBbbbB", blob, pos)
        voffs = [struct.unpack_from("<H", blob, pos + 12 + 2 * k)[0] for k in range(nv)]
        p = pos + 12 + _align4(nv * 2)
        self.callbacks = [tuple(blob[p + 3 * k:p + 3 * k + 3]) for k in range(ncb)]   # {id, param index, type}
        p += _align4(ncb * 3)
        self.variant_param_masks = None
        if self.flags & FLAG_PARAM_MASK:
            self.param_mask_selector = struct.unpack_from("<I", blob, p)[0]
            self.variant_param_masks = [struct.unpack_from("<H", blob, p + 4 + 2 * k)[0] for k in range(nv)]
            p += _align4(nv * 2 + 4)
        self.follow_list = None
        if self.flags & FLAG_FOLLOW_LIST:
            self.follow_list = [struct.unpack_from("<H", blob, p + 2 + 2 * k)[0] for k in range(blob[p])]
        self.variants = [Variant(blob, pos + v * 4) for v in voffs]


class EventDatabase:
    """Events.evt. Header: u16 0x0803, u16 0x073C, u32 0, u8 db, u8 0, u16 0, u16 size field (TODO),
    u16 checksum (TODO), u16 event count @0x10, u16 0, u16 shorten limit @0x14 (100 = 1.00 s),
    u16 interrupt priority @0x16 (500), u16 record offsets (words) @0x18."""
    def __init__(self, blob):
        assert struct.unpack_from("<H", blob, 0)[0] == 0x0803
        self.db, self.sub = blob[8], blob[9]
        n = struct.unpack_from("<H", blob, 0x10)[0]
        self.shorten_limit, self.interrupt_priority = struct.unpack_from("<HH", blob, 0x14)
        self.events = {}
        for i in range(n):
            ev = Event(blob, struct.unpack_from("<H", blob, 0x18 + 2 * i)[0] * 4)
            self.events.setdefault(ev.id, ev)        # lookup returns the first record with the id


class SampleGroup:
    """*.hdr: u16 group id, u16 sub id (0xFFFF none), u8 flags, u8 count, u8 history length,
    u8 block shift, u16 total size (units), u16 page count (param maximum), then per sample a
    big-endian u16 offset (bit 15 set = alias: low byte = index of the real sample) and
    (flags & 15) tag bytes; then (flags >> 4 & 7) u16 agreement keys {cond index:3, key:13};
    then, if flags & 0x80, an enabled bitmap of (count + 7) // 8 + 1 bytes; then the history
    {u8 cursor, u8 entry[history length]}; then, for paged groups, a page x sample bitmap."""
    def __init__(self, blob, name=""):
        self.name = name
        (self.id, self.sub, self.flags, self.count, self.hist_len, self.shift, self.total_units,
         self.pages) = struct.unpack_from("<HHBBBBHH", blob, 0)
        self.n_tags = self.flags & 15
        es = self.n_tags + 2
        self.entries = [blob[12 + i * es:12 + (i + 1) * es] for i in range(self.count)]
        p = 12 + self.count * es
        nk = (self.flags >> 4) & 7
        self.keys = {}
        for k in range(nk):
            hi, lo = blob[p + 2 * k], blob[p + 2 * k + 1]
            self.keys.setdefault(hi >> 5, ((hi << 8) | lo) & 0x1FFF)
        p += 2 * nk
        self.enabled = None
        if self.flags & 0x80:
            nb = (self.count + 7) // 8 + 1
            self.enabled = bytearray(blob[p + 1:p + nb])     # bit i lives in byte 1 + i // 8 of the region
            p += nb
        self.history_cursor = blob[p] if self.hist_len else 0
        self.history = list(blob[p + 1:p + 1 + self.hist_len]) if self.hist_len else []
        if self.hist_len:
            p += self.hist_len + 1
        self.page_bits = blob[p:]

    @property
    def block(self):
        return 256 * (self.shift + 1)

    def tags(self, i):
        return list(self.entries[i][2:])

    def span(self, i, page=-1):
        """(byte offset into the .dat, byte length) of sample i; alias entries point at another sample;
        the length runs to the next non-alias sample (or the total size). Paged groups add
        page * total size."""
        e = self.entries[i]
        idx = i
        if e[0] & 0x80:
            idx = e[1]
            e = self.entries[idx]
        start = ((e[0] << 8) | e[1]) * self.block
        end = self.total_units * self.block
        for j in range(idx + 1, self.count):
            f = self.entries[j]
            if not f[0] & 0x80:
                end = ((f[0] << 8) | f[1]) * self.block
                break
        base = page * self.total_units * self.block if page >= 0 else 0
        return base + start, end - start

    def length_cs(self, i):
        """Sample length in 1/100 s as the engine computes it (bytes * 100 / 4410)."""
        return self.span(i)[1] * 100 // BYTES_PER_SECOND

    def is_enabled(self, i):
        return self.enabled is None or bool(self.enabled[i >> 3] & (1 << (i & 7)))

    def page_allows(self, page, i):
        if page >= self.pages:
            return False
        bit = self.count * page + i
        return bool(self.page_bits[bit >> 3] & (1 << (bit & 7)))

    def history_age(self, i):
        """0 = not in the history; else 1 + number of newer entries."""
        if not self.hist_len:
            return 0
        c = self.history_cursor
        for age in range(self.hist_len):
            c = c - 1 if c > 0 else self.hist_len - 1
            if self.history[c] == i:
                return age + 1
        return 0

    def record_play(self, i):
        if self.enabled is not None:
            self.enabled[i >> 3] &= ~(1 << (i & 7)) & 0xFF
        if not self.hist_len:
            return
        if self.history_cursor < self.hist_len:
            self.history[self.history_cursor] = i
        self.history_cursor = self.history_cursor + 1 if self.history_cursor + 1 < self.hist_len else 0


def load_section(root, section):
    """Return (EventDatabase, [SampleGroup]) for one SPEECH.INF section directory."""
    arc = read_bigf(os.path.join(root, section, "headers.big"))
    evt = next(v for k, v in arc.items() if k.lower().endswith(".evt"))
    groups = [SampleGroup(v, k.replace("\\", "/")) for k, v in arc.items() if k.lower().endswith(".hdr")]
    return EventDatabase(evt), groups


class SpeechRng:
    """Six-word add-with-carry generator; seed s is spread as s * 65537 plus fixed offsets."""
    OFFSETS = (-231928234, -2010044695, -970662609, 117621916, -1640677507, 1876900708)

    def __init__(self, seed=0, state=None):
        if state is not None:
            self.s = list(state)
        else:
            base = (seed * 65537) & M32
            self.s = [(base + o) & M32 for o in self.OFFSETS]

    def next(self):
        s0, s1, s2, s3, s4, s5 = self.s
        u1 = (s5 + s4) & M32
        c = 1 if (u1 < s5 or u1 < s4) else 0
        u2 = (u1 + s3 + c) & M32
        u3 = (u2 + s2 + (1 if u2 < s3 else 0)) & M32
        s5 = (s5 + 1) & M32
        u4 = (u3 + s1 + (1 if u3 < s2 else 0)) & M32
        s0 = (u4 + s0 + (1 if u4 < s1 else 0)) & M32
        s1, s2, s3, s4 = u4, u3, u2, u1
        if s5 == 0:
            s4 = (u1 + 1) & M32
            if s4 == 0:
                s3 = (u2 + 1) & M32
                if s3 == 0:
                    s2 = (u3 + 1) & M32
                    if s2 == 0:
                        s1 = (u4 + 1) & M32
                        if s1 == 0:
                            s0 = (s0 + 1) & M32
        self.s = [s0, s1, s2, s3, s4, s5]
        return s0

    def below(self, n):
        return ((self.next() >> 16) * n) >> 16


class NoRepeatRandom:
    """Random in [0, n) avoiding recent results with the same tag (shared 32-entry history).
    Up to 32 draws; the first draw not among the last min(n/2, 10) same-tag results wins. If all 32
    are recent, the draw whose history entry is oldest is recorded but the LAST draw is returned
    (engine quirk)."""
    def __init__(self, rng):
        self.rng = rng
        self.hist = [(0xFFFF, 0xFFFF)] * 32     # (value, tag)
        self.cursor = 0

    def _find(self, value, depth, tag):
        depth = min(depth, 32)
        seen, c = 0, self.cursor
        for _ in range(33):
            if seen >= depth:
                return -1
            v, t = self.hist[c]
            if t == tag:
                if v == value:
                    return seen
                seen += 1
            c = c - 1 if c > 0 else 31
        return -1

    def _record(self, value, tag):
        self.cursor = self.cursor + 1 if self.cursor < 31 else 0
        self.hist[self.cursor] = (value & 0xFFFF, tag & 0xFFFF)

    def draw(self, n, tag):
        depth = min(n // 2, 10)
        best_pos, best = -1, self.rng.below(n)
        for tries in range(1, 33):
            v = self.rng.below(n)
            pos = self._find(v, depth, tag & 0xFFFF)
            if pos == -1:
                self._record(v, tag)
                return v
            if pos > best_pos:
                best_pos, best = pos, v
            if tries >= 32:
                self._record(best, tag)
                return v
        return v


class ChosenSegment:
    def __init__(self, group_index, page):
        self.group_index, self.page = group_index, page
        self.list_start, self.list_count, self.pick = 0, 0, 0


class Line:
    """One resolved event: [(group, sample index, byte offset, byte length)]."""
    def __init__(self, request, event, variant_index, samples):
        self.request, self.event, self.variant_index, self.samples = request, event, variant_index, samples


class SpeechEngine:
    def __init__(self, seed=0, rng_state=None):
        self.rng = SpeechRng(seed, rng_state)
        self.nrr = NoRepeatRandom(self.rng)
        self.dbs = {}                    # db id -> (EventDatabase, channel)
        self.groups = []                 # global sample group table (registration order)
        self.slots = [None] * N_SLOTS
        self.head = {}                   # channel -> slot of the current non-follow-on request
        self.last_posted = {}            # channel -> last posted slot
        self.pending = {}                # channel -> pending count
        self.last_event = {}             # channel -> (event id, play count)
        self.seq, self.last_time = 0, 0
        self.clock = lambda: 0           # the game never installs the clock callback: time is always 0
        self.agreement = (-1, 0)         # single (key, tag bit) store for 0xFE / 0xFF conditions
        self.must_play_queued = False
        self.prepared = None             # (channel, [Line]) waiting to be handed to the player

    # registration
    def register(self, evdb, groups, channel=None):
        self.dbs[evdb.db] = (evdb, CHANNEL_OF_DB.get(evdb.db, 0) if channel is None else channel)
        self.groups.extend(groups)

    def _event(self, req0):
        db = (req0 >> 24) & 0xFF
        if db not in self.dbs:
            return None, None
        evdb, ch = self.dbs[db]
        return evdb.events.get(req0 & 0xFFFF), ch

    def _group_index(self, gid, sub=None):
        for i, g in enumerate(self.groups):
            if g.id == gid and (sub is None or g.sub == sub):
                return i
        return -1

    # posting
    def post(self, request):
        """request = list of up to 20 words. Returns True if a slot was taken."""
        req = (list(request) + [0] * REQUEST_WORDS)[:REQUEST_WORDS]
        ev, ch = self._event(req[0])
        if ev is None:
            return False
        follow = bool(ev.flags & FLAG_FOLLOW_ON)
        if not follow:
            if ev.chance < self.nrr.draw(100, 0xFFFF):
                return False
            priority = ev.priority
        else:
            h, lp = self.head.get(ch, -1), self.last_posted.get(ch, -1)
            if not (0 <= h < N_SLOTS and 0 <= lp < N_SLOTS):
                return False
            priority = self.slots[h]["event"].priority
        s = self._take_slot(priority, ch, follow)
        if s < 0:
            if follow:
                self._free(self.head.get(ch, -1))
            return False
        now = self.clock()
        self.seq = self.seq + 1 if now == self.last_time else 0
        self.last_time = now
        self.slots[s] = {"event": ev, "channel": ch, "time": now, "seq": self.seq, "follow": follow,
                         "next": -1, "req": req}
        if follow:
            self.slots[self.last_posted[ch]]["next"] = s
        else:
            self.pending[ch] = self.pending.get(ch, 0) + 1
            self.head[ch] = s
        self.last_posted[ch] = s
        return True

    def _take_slot(self, priority, ch, follow):
        for i in range(N_SLOTS):
            if self.slots[i] is None:
                return i
        protect = self.head.get(ch, -1) if follow else -1
        now = self.clock()
        for i, sl in enumerate(self.slots):          # expired pending requests first
            t = sl["event"].timeout
            if not sl["follow"] and t and (now - sl["time"]) & M32 > t and i != protect:
                self._free(i)
                return i
        for i, sl in enumerate(self.slots):          # then the first lower-or-equal priority request
            if not sl["follow"] and sl["event"].priority <= priority and sl["channel"] == ch and i != protect:
                self.pending[ch] -= 1
                self.slots[i] = None
                return i
        return -1

    def _free(self, i):
        if not (0 <= i < N_SLOTS) or self.slots[i] is None or self.slots[i]["follow"]:
            return
        sl = self.slots[i]
        ch = sl["channel"]
        self.slots[i] = None
        if self.head.get(ch) == i:
            self.head[ch] = -1
        if self.last_posted.get(ch) == i:
            self.last_posted[ch] = -1
        n = sl["next"]
        while 0 <= n < N_SLOTS and self.slots[n] is not None and self.slots[n]["follow"]:
            if self.last_posted.get(ch) == n:
                self.last_posted[ch] = -1
            nxt = self.slots[n]["next"]
            self.slots[n] = None
            n = nxt
        self.pending[ch] = max(0, self.pending.get(ch, 0) - 1)

    # dispatch
    def _pick(self, ch, allowed=None):
        now = self.clock()
        best, best_pri, best_age, best_seq = -1, 0, M32, 0
        for i, sl in enumerate(self.slots):
            if sl is None or sl["channel"] != ch or sl["follow"]:
                continue
            ev = sl["event"]
            ok = allowed is None or ev.id in allowed
            age = (now - sl["time"]) & M32
            if ev.timeout and age > ev.timeout:
                self._free(i)
                continue
            if not ok:
                continue
            if ev.priority > best_pri:
                best, best_pri, best_age, best_seq = i, ev.priority, age, sl["seq"]
            elif ev.priority == best_pri and (age < best_age or (age == best_age and sl["seq"] > best_seq)):
                best, best_age, best_seq = i, age, sl["seq"]
        return best

    def service(self, ch):
        """Resolve and 'play' the best pending request on channel ch (and its follow-on chain).
        Returns the list of Lines handed to the game's stream player (empty if none).
        A prepared play list that was not handed over (the last chain line failed) is played by the
        next call instead (engine behaviour)."""
        if self.prepared and self.prepared[0] == ch:
            return self._play(ch)
        if not self.pending.get(ch):
            return []
        last = self.last_event.get(ch)
        allowed = None
        if last:
            ev_last = self._find_event(ch, last[0])
            if ev_last is not None and ev_last.follow_list:
                allowed = set(ev_last.follow_list)
        s = self._pick(ch, allowed)
        if s < 0 and allowed is not None:
            s = self._pick(ch, None)
        if s < 0:
            return []                              # (the accept hook is not installed: always accepted)
        self._drop_older(s)
        self.prepared = None
        ok, i = False, s
        while True:
            line = self.resolve(self.slots[i]["req"])
            ok = line is not None
            if ok and (self.prepared is None or self.prepared[0] == ch) and                     (self.prepared is None or len(self.prepared[1]) < MAX_LINES):
                self.prepared = (ch, (self.prepared[1] if self.prepared else []) + [line])
            nxt = self.slots[i]["next"]
            if nxt == -1 or not ok:
                break
            i = nxt
        self._free(s)
        return self._play(ch) if ok else []

    def _play(self, ch):
        lines = self.prepared[1] if self.prepared and self.prepared[0] == ch else []
        self.prepared = None
        for k, line in enumerate(lines):
            for (gi, si, _, _) in line.samples:
                self.groups[gi].record_play(si)
            if k == 0:
                eid = line.event.id
                prev = self.last_event.get(ch)
                self.last_event[ch] = (eid, prev[1] + 1) if prev and prev[0] == eid else (eid, 1)
        return lines

    def _find_event(self, ch, eid):
        for evdb, c in self.dbs.values():
            if c == ch and eid in evdb.events:
                return evdb.events[eid]
        return None

    def _drop_older(self, s):
        sl = self.slots[s]
        self.must_play_queued = False
        for i, other in enumerate(self.slots):
            if i == s or other is None or other["channel"] != sl["channel"] or other["follow"]:
                continue
            older = other["time"] < sl["time"] or (other["time"] == sl["time"] and other["seq"] < sl["seq"])
            if older:
                if not other["event"].flags & FLAG_KEEP_ON_NEW:
                    self._free(i)
            elif other["event"].chance == 100:
                self.must_play_queued = True

    # resolution
    def resolve(self, req):
        ev, ch = self._event(req[0])
        if ev is None:
            return None
        last = self.last_event.get(ch)
        if last and last[0] == ev.id and 0 < ev.max_repeats <= last[1]:
            return None
        mode = 0                                    # the channel state setter is never called
        if self.must_play_queued:
            mode = min(mode + 1, 2)
        if not ev.flags & FLAG_CAN_SHORTEN and mode == 1:
            mode = 0
        evdb = self.dbs[(req[0] >> 24) & 0xFF][0]
        limit = evdb.shorten_limit if mode else 0
        ctx, unknown = self._context(ev, req)
        order = self._variant_order(ev)
        pmask = 0
        if ev.flags & FLAG_PARAM_MASK:
            for w in range(16):                       # the mask shift runs out after 16 words
                sel = (ev.param_mask_selector >> ((w & 15) * 2)) & 3
                v = req[w + 1]
                if (sel == 1 and v != 0) or (sel == 2 and v != M32):
                    pmask |= 1 << w
        for vi in order:
            if vi >= len(ev.variants):
                break
            var = ev.variants[vi]
            if ev.flags & FLAG_PARAM_MASK and ev.variant_param_masks[vi] != pmask:
                continue
            if var.n_segments >= 13 or not var.mode_ok(mode):
                continue
            if ((var.ctx_value ^ ctx) & var.ctx_mask & ~unknown) & 0xFF:
                continue
            work = self._candidates(var, req)
            if work is None:
                continue
            segs, lst = work
            picks = self._shorten(segs, lst, limit) if mode == 1 else self._pick_samples(var, segs, lst)
            if picks is None:
                continue
            samples = []
            for seg, si in zip(segs, picks):
                off, ln = self.groups[seg.group_index].span(si, seg.page)
                samples.append((seg.group_index, si, off, ln))
            return Line(req, ev, vi, samples)
        return None

    def _context(self, ev, req):
        """Parameter callbacks: record k gives context bit 1 << (7 - k). Type 4 = request word != 0;
        other types ask the game's query hook, which SSX Tricky never installs (bit unknown)."""
        ctx = unknown = 0
        for k, (cb_id, idx, kind) in enumerate(ev.callbacks):
            if idx >= REQUEST_WORDS:
                continue
            bit = (1 << (7 - k)) & 0xFF
            if kind == 4:
                if req[idx] != 0:
                    ctx |= bit
            else:
                unknown |= bit
        return ctx, unknown

    def _variant_order(self, ev):
        weights = [v.weight for v in ev.variants]
        total, order = sum(weights), []
        while total > 0:
            r = self.nrr.draw(total, 0xFFFF)
            k = 0
            r -= weights[0]
            while r >= 0 and k + 1 < len(weights):
                k += 1
                r -= weights[k]
            order.append(k)
            total -= weights[k]
            weights[k] = 0
        order += [i for i, v in enumerate(ev.variants) if v.weight == 0]
        return order

    def _candidates(self, var, req):
        segs, lst = [], []
        for seg in var.segments:
            if seg.kind == 1:
                gi = self._group_index(seg.group_id, req[seg.param_index] & 0xFFFF) if seg.param_index < 20 else -1
                page = -1
            elif seg.kind == 0:
                gi, page = self._group_index(seg.group_id), -1
            elif seg.kind == 2:
                gi = self._group_index(seg.group_id)
                v = req[seg.param_index] if seg.param_index < REQUEST_WORDS else 0
                page = v if gi >= 0 and self.groups[gi].pages != 0xFFFF and v < self.groups[gi].pages else -1
                if page < 0:
                    gi = -1
            else:
                gi, page = -1, -1
            if gi < 0:
                return None
            cs = ChosenSegment(gi, page)
            cs.list_start = len(lst)
            g = self.groups[gi]
            fresh, oldest, oldest_age = 0, -1, 0
            for i in range(g.count):
                if not self._conditions_ok(g, i, seg, req):
                    continue
                if page >= 0 and not g.page_allows(page, i):
                    continue
                if not g.is_enabled(i):
                    continue
                age = g.history_age(i)
                if age < 1:
                    if len(lst) < 100:
                        lst.append(i)
                        fresh += 1
                elif age > oldest_age:
                    oldest, oldest_age = i, age
            if fresh == 0 and oldest != -1 and len(lst) < 100:
                lst.append(oldest)
                fresh = 1
            if fresh == 0:
                return None
            cs.list_count = fresh
            cs.seg = seg
            segs.append(cs)
        return segs, lst

    @staticmethod
    def _conditions_ok(g, i, seg, req):
        if seg.n_cond >= 5:
            return False
        tags = g.tags(i)
        for j in range(max(seg.n_cond, 0)):
            tag = tags[j] if j < len(tags) else 0xFF
            if tag > 31:
                return False
            bit = 1 << tag
            if not bit & seg.cond_mask[j]:
                return False
            p = seg.cond_param[j]
            if p not in (0, 0xFE, 0xFF) and not bit & (req[p] if p < REQUEST_WORDS else 0):
                return False
        return True

    def _pick_samples(self, var, segs, lst):
        picks = []
        for cs in segs:
            g, seg = self.groups[cs.group_index], cs.seg
            chosen, special = None, False
            for j in range(max(seg.n_cond, 0)):
                code = seg.cond_param[j]
                if code == 0xFE:                     # random pick, publish the chosen tag
                    special = True
                    r = self.nrr.draw(cs.list_count, cs.group_index)
                    chosen = lst[cs.list_start + r]
                    key = g.keys.get(j, -1)
                    tag = g.tags(chosen)[j] if j < g.n_tags else 0xFF
                    self.agreement = (key, 0 if tag == 0xFF else 1 << (tag & 31))
                    break
                if code == 0xFF:                     # agree with the published tag
                    special = True
                    key = g.keys.get(j, -1)
                    bits = self.agreement[1] if self.agreement[0] == key else 0
                    order = lst[cs.list_start:cs.list_start + cs.list_count]
                    self._shuffle(order)
                    lst[cs.list_start:cs.list_start + cs.list_count] = order
                    for s in order:
                        tag = g.tags(s)[j] if j < g.n_tags else 0xFF
                        b = 0 if tag == 0xFF else 1 << (tag & 31)
                        if b & bits:
                            chosen = s
                    if chosen is not None:
                        break
            if chosen is None:
                if special:
                    return None
                chosen = lst[cs.list_start + self.nrr.draw(cs.list_count, cs.group_index)]
            picks.append(chosen)
        return picks

    def _shuffle(self, items):
        n = len(items)
        for i in range(n // 2):
            j = self.nrr.draw(n, n)
            items[i], items[j] = items[j], items[i]

    def _shorten(self, segs, lst, limit):
        """Mode 1: shuffle each candidate list, then step through combinations (last segment
        fastest) until the summed length is below the database limit (1/100 s)."""
        for cs in segs:
            sub = lst[cs.list_start:cs.list_start + cs.list_count]
            n = cs.list_count
            for i in range(n // 2):
                j = self.nrr.draw(n, cs.group_index)
                sub[i], sub[j] = sub[j], sub[i]
            lst[cs.list_start:cs.list_start + n] = sub
            cs.pick = 0
        while True:
            total = sum(self.groups[cs.group_index].span(lst[cs.list_start + cs.pick])[1] for cs in segs)
            if total * 100 // BYTES_PER_SECOND < limit:
                return [lst[cs.list_start + cs.pick] for cs in segs]
            k = len(segs) - 1
            while k >= 0:
                segs[k].pick += 1
                if segs[k].pick < segs[k].list_count:
                    break
                segs[k].pick = 0
                k -= 1
            if k < 0:
                return None


LANGUAGE_MASK = {0: 1, 1: 1, 2: 2, 3: 8, 4: 4}          # app+0x58 -> language bit


def narration_requests(script, language):
    """NASCRIPT.INF element list -> NARR requests in post order (camera op 0x21).
    'b' break points (-1) are not requests. Values < 1000 are ignored."""
    lang = LANGUAGE_MASK.get(language, 1)
    out = []
    for v in script:
        if v < 1000:
            continue
        if v < 2000:
            out.append([request_id(DB_NARR, 0x8002), v - 1000, lang])
        elif v < 3000:
            out.append([request_id(DB_NARR, 0x8004), v - 2000, lang])
        elif v < 4000:
            out.append([request_id(DB_NARR, 0x8003), v - 3000, lang])
        elif v < 5000:
            out.append([request_id(DB_NARR, 0x8005), v - 4000, lang])
        elif v < 6000:
            out.append([request_id(DB_NARR, 0x8015), 1 << ((v - 5001) & 31)])
        elif v < 7000:
            out.append([request_id(DB_NARR, 0x8016), v - 6000, lang])
    return out


def parse_nascript(text, section, language_suffix):
    """NASCRIPT.INF: [<VENUE>] then [<VENUE>_<LANG>] (second pass overrides by key).
    Key = script id; value = comma list of ints, 'b'/'B' = break point (-1)."""
    scripts, cur = {}, None
    wanted = (section.upper(), (section + language_suffix).upper())
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            cur = line.strip("[]").strip().upper()
            continue
        if cur not in wanted or "=" not in line:
            continue
        key, val = line.split("=", 1)
        items = []
        for tok in val.strip().strip('"').split(","):
            tok = tok.strip()
            if not tok:
                continue
            items.append(-1 if tok in ("b", "B") else int(tok))
        scripts[int(key.strip())] = items
    return scripts


def anim_speech_request(value):
    """Animation event value >= 1000 (intro dialogue line / FE clip): ANIM request, page = value."""
    return [request_id(DB_ANIM, 0xA000), value]


def commentary_allowed(chance_row, level, rand_a):
    """Announcer class gate: plays if (rand_a & 0x3FF) < chance_row[level];
    level = min(per-race level, int(option float)); the option is clamped to [0, 1], so level is 0
    or 1 in practice (classes 8 and 16 never play, all others always)."""
    return (rand_a & 0x3FF) < chance_row[level]
