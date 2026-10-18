"""SSX Tricky trick record and trick-name text (reference model).

Clean-room behaviour model (source tags in the comments).

Validation (python models/trick_name.py):
  1. trick_name() reproduces the six (record, text) pairs left in race_stats (+0x6C record,
     +0x250 text) of the riders in traces/live/ride_session_2/spawn_ram.bin, byte for byte.
  2. For each of those records build_record() finds a spin/flip/grab input that produces exactly
     that 8-byte record (a consistency check of the bit layout, not a check of the real inputs).
"""

# code: build_record = FUN_00157490 (rounding FUN_00157198); trick_name = FUN_001551c0
# data: string tables .data 0x320970..0x320CEC; named-flip grids 0x320CF0 / 0x320F40
RAIL = ['', '50/50 Rail ', 'Switch 50/50 Rail ', 'BS Rail ', 'FS Rail ']  # .data 0x320b08
RAILSPIN = ['', '180 ', '360 ', '540 ', '720 ', '900 ', '1080 ', '1260 ', '1440 ', '1620 ', '1800 ']  # .data 0x320b20
PREFIX = ['', 'Switch ', 'Rail To ', 'Rail To Switch ', 'Late ']  # .data 0x320970
DIR = ['', 'FS ', 'BS ']  # .data 0x320988
SPIN = ['', '180 ', '360 ', '540 ', '720 ', '900 ', '1080 ', '1260 ', '1440 ', '1620 ', '1800 ']  # .data 0x320998
FLIPCOUNT = ['', '', 'Double ', 'Triple ']  # .data 0x3209c8
FLIP = ['', 'Front Flip ', 'Back Flip ', 'Rodeo ', 'Misty ']  # .data 0x3209d8
NAMED = ['', 'Crippler ', 'Egg Flip ', 'Mc Twist ', 'Haakon Flip ', 'Wetcat ', 'Full On ', 'Breakdancer ', 'Scratch Artist ', 'Famous ', 'DJ ', 'Mindless ', 'Banzai ', 'Shiny ', 'Torpedo ', 'Twister ', 'Nose Picker ', 'Horrifying ', 'Gross ', 'Backwash ', 'Ambulance Trip ', 'Ligament ', 'Cartilage ', 'Hospitalized ', 'Life Insurance ', 'Tripped Out ', 'Plain Brown Wrapper ', 'Brown Bag ', 'Lunch Box ', 'Grab Bag ', 'Crippled Squirrel ', 'Montezuma ', 'Homesick ', 'Double Jointed ', 'Spider ', 'Chunks ', 'Heimlich ', 'Gargle ', 'Sick ', 'Shell Cracker ', 'Deathwish ', 'Iron Lung ', 'Multiple Fracture ', 'Roadkill ', '???']  # .data 0x3209f0
SPIN2 = ['', '180 ', '360 ', '540 ', '720 ', '900 ', '1080 ', '1260 ', '1440 ', '1620 ', '1800 ']  # .data 0x320aa8
SUFFIX = ['', 'Air', 'To Fakie', 'To Rail']  # .data 0x320ad8
BAIL = ['', 'Take a Seat', 'Trouble at the Ranch', 'Have a Nice Trip', 'Look Mom, No Control', 'That was Weak', 'Simply Brutal', 'Tumbleina']  # .data 0x320ae8
LINK = ['', 'To Late ']  # .data 0x320b50
GRAB = ['', 'Canadian Bacon ', 'Chicken Salad ', 'Crail ', 'Cross ', 'Experimental ', 'Flying Squirrel ', 'Iguana ', 'Indy ', 'Japan ', 'Lein ', 'Melancholy ', 'Method ', 'Mute ', 'Nosegrab ', 'Nuclear ', 'Roast Beef ', 'Rocket ', 'Seatbelt ', 'Skunk ', 'Slob ', 'Spaghetti ', 'Stiffy ', 'Stalefish ', 'Stalemaskey ', 'Swiss Cheese ', 'Tailgrab ', 'Canadian BACK Bacon ', 'RUBBER Chicken Salad ', 'HOLY Crail ', 'Cross THE ROAD ', 'UNETHICAL Experimental ', 'SKINNED Flying Squirrel ', 'Iguana KABOB ', 'SINFUL Indy ', 'MADE IN Japan ', 'MEAN Lein ', 'GLEEFUL Melancholy ', 'Method MADNESS ', 'MUTATION ', 'NOSEBLEED ', 'Nuclear REACTOR ', 'Roast Beef JERKY ', 'Rocket BOOSTER ', 'BUCKLED Seatbelt ', 'FUNKY Skunk ', 'TOTAL Slob ', 'MEATBALLED Spaghetti ', 'IFFY Stiffy ', "FILET O'Stalefish ", 'FRESH Stalemaskey ', 'RIPE Swiss Cheese ', 'Tail WAG ', 'Can Can ', 'Heel Clicker ', 'CliffHanger ', 'SuperMan ', 'Sad Sack ', 'Hand in Hand ', 'Paddle Wheel ', 'Scooter ', 'Gut Buster ', 'Magician ', 'Proper Propeller ', 'Body Board ', 'Nac Nac ', 'Indian ', 'Bar Hop ', 'Nothing ', 'Blender ', 'Judo ', 'Footloose ', 'Benihana ', 'The Rake ', 'FrogHop ', 'Torpedo ', 'Near Miss ', 'Cordova ', 'Hart Attack ', 'Mulisha ', 'LazyBoy ', 'Madonna ', 'Airwalk ', 'Iron Cross ', 'Dervish ', 'Triple H ', 'FloorBoard ', 'Coffin Roll ', 'StuntMan ', 'Worm ', 'Pirouette Grind ', 'Bronco Buster ', 'Walking The Dog ', 'SuperMan Barspin ', 'Pommel Me ', 'HeadSpin 2 Poseur ', 'LaLaLa Lock Step ', 'Guillotine ', 'Soul Grind ', 'Hang 10 Backflip ', 'Aerial Spock 540 ', 'Combo Grab ']  # .data 0x320b58
GRID_NORMAL = [[0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 6, 16, 0, 25, 35], [0, 0, 0, 17, 0, 0, 36], [0, 0, 7, 18, 0, 26, 37], [0, 0, 8, 19, 0, 27, 43], [0, 0, 9, 0, 0, 28, 0], [0, 0, 10, 0, 0, 29, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 20, 0, 0, 39], [0, 0, 11, 21, 0, 30, 40], [0, 0, 0, 22, 0, 0, 41], [0, 0, 12, 23, 0, 31, 42], [0, 0, 13, 24, 0, 32, 43], [0, 0, 14, 0, 0, 33, 0], [0, 0, 15, 0, 0, 34, 0]]  # [spin_row][flip_col] -> NAMED index (.data 0x320CF0)
GRID_ALT = [[0, 2, 0, 0, 2, 0, 0], [0, 0, 0, 0, 1, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 5, 6, 16, 0, 25, 35], [0, 0, 0, 17, 0, 0, 36], [0, 0, 7, 18, 0, 26, 37], [0, 0, 8, 19, 0, 27, 43], [0, 0, 9, 0, 0, 28, 0], [0, 0, 10, 0, 0, 29, 0], [0, 0, 0, 0, 1, 0, 0], [0, 0, 0, 0, 0, 0, 0], [0, 3, 0, 0, 0, 0, 0], [0, 0, 0, 20, 0, 0, 39], [0, 0, 11, 21, 0, 30, 40], [0, 0, 0, 22, 0, 0, 41], [0, 0, 12, 23, 0, 31, 42], [0, 0, 13, 24, 0, 32, 43], [0, 0, 14, 0, 0, 33, 0], [0, 0, 15, 0, 0, 34, 0]]  # .data 0x320F40

NAME_FIELDS = (  # (table, word, shift, mask) in sprintf order: 14 x "%s", format at .data 0x370C80
    ('RAIL', 1, 12, 15), ('RAILSPIN', 1, 4, 15), ('PREFIX', 1, 16, 15), ('DIR', 1, 27, 7),
    ('SPIN', 0, 24, 15), ('FLIPCOUNT', 1, 24, 7), ('FLIP', 1, 8, 15), ('NAMED', 0, 0, 255),
    ('SPIN2', 0, 28, 15), ('GRAB', 0, 8, 255), ('LINK', 1, 30, 3), ('GRAB', 0, 16, 255),
    ('SUFFIX', 1, 20, 15), ('BAIL', 1, 0, 15),
)
UNNAMEABLE = 44   # byte 0 = ',' (0x2C) -> NAMED[44] = "???"
COMBO_GRAB = 101  # byte 2 = 'e' (0x65) -> GRAB[101] = "Combo Grab "


def trick_name(w0, w1):
    """Text written to race_stats+0x250: concatenation of 14 table strings."""
    tabs = globals()
    out = []
    for tab, word, shift, mask in NAME_FIELDS:
        idx = ((w0 if word == 0 else w1) >> shift) & mask
        out.append(tabs[tab][idx])
    return ''.join(out)


def round180(deg):
    """Nearest multiple of 180; a remainder of exactly +-90 rounds toward zero.
    C remainder semantics (the sign follows the dividend)."""
    r = abs(deg) % 180
    r = r if deg >= 0 else -r
    if r < -90:
        return deg - 180 - r
    if r <= 90:
        return deg - r
    return deg + 180 - r


def _cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def build_record(spin_deg, flip_deg, switch, seg_type, late, alt_flag, grabs,
                 to_rail=False, land_switch=False, alt_select=False):
    """Record builder. Inputs are the race_stats fields when the record is built:
      spin_deg  +0x30 (int degrees; negated first when `switch`)
      flip_deg  +0x34 (int degrees; negated first when seg_type == 4)
      switch +0x10, seg_type +0x20 (0 = took off from the ground), alt_flag +0x14, late +0x18
      grabs     +0x60..+0x68: up to 3 grab-name indices, 0 terminates
      to_rail      caller argument: landing onto a rail
      land_switch  caller argument: landing-stance flag ("To Fakie" when not `switch`)
      alt_select   caller argument: rider float +0x2A8 < 0.7 (air-exit call only)
    Returns (w0, w1, valid)."""
    if switch:
        spin_deg = -spin_deg
    if seg_type == 4:
        flip_deg = -flip_deg
    spin_deg = round180(spin_deg)
    flip_deg = round180(flip_deg)
    f = abs(_cdiv(flip_deg, 360))
    s = abs(_cdiv(spin_deg, 180))
    t = 0                                 # min(f, s // 3): "540 per flip" count
    if f >= 1 and s >= 3:
        ff, ss = f, s
        while True:
            ff -= 1
            ss -= 3
            t += 1
            if ff < 1 or ss < 3:
                break
    srow = s + 10 if (spin_deg > 0 or s > 10) else s
    fcol = f + 3 if (flip_deg < 0 or f > 3) else f
    ngrabs = 0
    if grabs and grabs[0]:
        ngrabs = 1
        while ngrabs < 3 and ngrabs < len(grabs) and grabs[ngrabs]:
            ngrabs += 1
    w0 = w1 = 0
    if seg_type == 0:                     # prefix field, w1 bits 16..19
        code = 4 if late else (1 if switch else 0)
    else:
        code = 3 if switch else 2
    w1 |= code << 16
    overflow = False
    if srow < 21 and fcol < 7:
        grid = GRID_ALT if (alt_flag and alt_select) else GRID_NORMAL
        w0 |= grid[srow][fcol]
    else:
        overflow = True
    if (w0 & 0xFF) == 0:
        if s > 0:
            w1 |= (2 if spin_deg > 0 else 1) << 27                 # "BS " / "FS "
        if f == 0:
            w0 |= (s & 15) << 24                                   # plain spin
        w1 |= (f & 7) << 24                                        # "", "", "Double ", "Triple "
        if f > 0:
            if t > 0 and t == f:
                flip = 3 if flip_deg < 1 else 4                    # "Rodeo " / "Misty "
            else:
                flip = 2 if flip_deg < 1 else 1                    # "Back Flip " / "Front Flip "
            w1 |= flip << 8
        spin_after = f > 0
    else:
        spin_after = True
    if spin_after and f > 0 and s != 3 * f and s < 11:
        w0 |= (s & 15) << 28                                       # spin printed after the flip
    if to_rail:                                                    # suffix field, w1 bits 20..23
        w1 |= 3 << 20                                              # "To Rail"
    elif (w0 & 0xFF) == 0 and (t == 0 or (w0 >> 28)):
        if land_switch and not switch:
            w1 |= 2 << 20                                          # "To Fakie"
        elif s == 0 and f == 0 and ngrabs > 0:
            w1 |= 1 << 20                                          # "Air"
    if ngrabs > 0:
        w0 |= (grabs[0] & 0xFF) << 8
        if ngrabs > 1:
            w1 |= 1 << 30                                          # LINK text between the grabs
            w0 |= ((grabs[1] if ngrabs == 2 else COMBO_GRAB) & 0xFF) << 16
    if overflow:
        w0, w1 = UNNAMEABLE, 0
    valid = s > 0 or f > 0 or ngrabs > 0
    return w0, w1, valid


RAM_SCAN_LO, RAM_SCAN_HI, TEXT_BASE = 4 << 20, 31 << 20, 1 << 20


def _riders(d):
    import struct
    out = []
    for x in range(RAM_SCAN_LO, RAM_SCAN_HI, 16):         # cBoarder objects: *(x+0x5AE0) -> handler, *handler == x
        h = struct.unpack_from('<I', d, x + 0x5AE0)[0]
        if TEXT_BASE < h < len(d) - 4 and struct.unpack_from('<I', d, h)[0] == x:
            out.append(x)
    return out


def _validate():
    import os, struct, itertools
    import sys
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    import traces
    p = str(traces.path('live', 'ride_session_2', 'spawn_ram.bin'))
    d = open(p, 'rb').read()
    ok = n = rebuilt = 0
    for r in _riders(d):
        st = r + 0x5820
        w0, w1 = struct.unpack_from('<II', d, st + 0x6C)
        text = d[st + 0x250:st + 0x250 + 0x80].split(b'\0')[0].decode('latin-1')
        if not text:
            continue
        n += 1
        good = trick_name(w0, w1) == text
        ok += good
        grabs = [(w0 >> 8) & 0xFF]
        if (w1 >> 30) & 1:
            second = (w0 >> 16) & 0xFF
            grabs += [1, 2] if second == COMBO_GRAB else [second]
        found = None
        for sp, fl, sw, ty, la, ls, alt in itertools.product(
                range(-1800, 1801, 180), range(-1440, 1441, 180), (0, 1), (0, 1, 2, 3, 4), (0, 1), (0, 1), (0, 1)):
            if ty and la:
                continue
            rec = build_record(sp, fl, sw, ty, la, alt, [g for g in grabs if g],
                               land_switch=bool(ls), alt_select=bool(alt))
            if rec[0] == w0 and rec[1] == w1:
                found = (sp, fl, sw, ty, la, ls)
                break
        rebuilt += found is not None
        desc = ("spin %d flip %d switch %d type %d late %d land_switch %d" % found) if found else "none"
        print(f"rider {r:#x} rec {w0:08x} {w1:08x} text {'OK ' if good else 'BAD'} {text!r}  rebuild: {desc}")
    print(f"{ok}/{n} names match RAM; {rebuilt}/{n} records rebuildable")


if __name__ == '__main__':
    _validate()
