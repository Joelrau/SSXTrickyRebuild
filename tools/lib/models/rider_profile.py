"""Rider profile reference model.

Builds the 16 rider stat bytes (profile +0x0E..+0x1D) and the other profile fields the physics
reads, for a human player (chosen board, career attribute ratings) or a computer rider (rating
'level'), exactly as the game does; checked byte-for-byte against all 12 live profiles
.  Stat index i is profile byte +0x0E + i.

Usage:
    p = player_profile(4, board=0)                 # Moby, board 0, new save
    p = ai_profile(11, level=240, board=3)         # Marisol as an opponent rated 240
    p.stat_byte(0x1A)                              # spin stat byte (0..255)
    p.board_class, p.scale, p.stance, p.trick_set, p.mass_byte
"""
from dataclasses import dataclass
from typing import Optional, Sequence, Tuple
import numpy as np

f32 = np.float32

# Attribute groups: stat indices driven by each of the four attributes.
ATTRIBUTE_GROUPS = ((3, 5), (0, 11), (4, 8), (6, 12, 15))
PLAYER_STAT_WEIGHT = 0.8        # rider share of a grouped stat
BOARD_STAT_WEIGHT = 0.2         # board share (board byte 0..90 -> 0..18 points)
AI_LEVEL_WEIGHT = 0.25          # opponent rating -> stat points


@dataclass
class RiderProfile:
    character: int
    name: str
    stats: Tuple[int, ...]       # 16 bytes, 0..255
    board_class: int             # profile+0x44 (0 bx, 1 fr, 2 ex animation set / grab column)
    board: int                   # index in the rider's board list
    scale: float                 # profile+0x60 (size scale; collision radius = 100 * scale cm)
    stance: int                  # profile+0x40
    trick_set: int               # profile+0x5C
    mass_byte: int               # profile+0x1E (s30 of the rider-mass formula)
    computer: bool

    def stat_byte(self, offset: int) -> int:
        """Stat byte by profile offset (0x0E..0x1D)."""
        return self.stats[offset - 0x0E]


def attribute_base(stats: Sequence[int]) -> Tuple[int, ...]:
    """Integer mean of the static percentages in each attribute group."""
    return tuple(sum(stats[i] for i in g) // len(g) for g in ATTRIBUTE_GROUPS)


def percent_to_byte(v: int) -> int:
    return (int(v) * 255) // 100


def player_stats(character: int, board: int, career_attr: Optional[Sequence[int]] = None,
                 cheat_alt: bool = False) -> Tuple[int, ...]:
    """Human player stat bytes.  career_attr: career record attribute ratings (new save = base)."""
    static = RIDERS[character][1]
    base = attribute_base(static)
    car = base if career_attr is None else career_attr
    board_stats = BOARDS[character][board][3]
    out = list(static)
    for a, group in enumerate(ATTRIBUTE_GROUPS):
        if cheat_alt:                                   # cheat flag: fixed per-attribute values
            for i in group:
                out[i] = RIDERS[character][2][a]
            continue
        board_term = f32(board_stats[a]) * f32(BOARD_STAT_WEIGHT)
        progress = f32(0.0) if base[a] == 100 else f32(car[a] - base[a]) / f32(100 - base[a])
        for i in group:
            s = f32(static[i])
            out[i] = int((s + progress * (f32(100.0) - s)) * f32(PLAYER_STAT_WEIGHT) + board_term)
    return tuple(percent_to_byte(v) for v in out)


def ai_stats(character: int, level: Optional[int]) -> Tuple[int, ...]:
    """Computer rider stat bytes.  level: opponent rating from the player's career (new save 0)."""
    static = RIDERS[character][1]
    out = list(static)
    if level is not None:
        base = attribute_base(static)
        for a, group in enumerate(ATTRIBUTE_GROUPS):
            gain = f32(0.0) if base[a] == 100 else (f32(level) * f32(AI_LEVEL_WEIGHT)) / f32(100 - base[a])
            gain = min(gain, f32(1.0))
            for i in group:
                s = f32(static[i])
                out[i] = int(f32(0.0) + s + gain * (f32(100.0) - s))
    return tuple(percent_to_byte(v) for v in out)


def player_profile(character: int, board: int = 0, career_attr=None, stance: Optional[int] = None) -> RiderProfile:
    name, _, _, mass, scale, ai_stance, trick_set = RIDERS[character]
    return RiderProfile(character, name, player_stats(character, board, career_attr), BOARDS[character][board][1],
                        board, float(f32(scale)), ai_stance if stance is None else stance, trick_set, mass, False)


def ai_profile(character: int, level: Optional[int] = 0, board: int = 0) -> RiderProfile:
    """board: the game picks it with rand() % 12; pass the drawn index."""
    name, _, _, mass, scale, stance, trick_set = RIDERS[character]
    return RiderProfile(character, name, ai_stats(character, level), BOARDS[character][board][1], board,
                        float(f32(scale)), stance, trick_set, mass, True)


def ground_profile(p: RiderProfile):
    """ground.Profile (0..1 fractions) from a rider profile."""
    import ground as gnd
    f = lambda off: p.stat_byte(off) / 255.0
    return gnd.Profile(stat_speed=f(0x0E), stat_brake=f(0x11), stat_grip=f(0x13), stat_gate=f(0x15),
                       stat_landing=f(0x16), stat_jump=f(0x17), stat_aero=f(0x19), lean_offset=p.scale)


def air_stats(p: RiderProfile):
    """air.RiderStats (bytes) from a rider profile."""
    import air
    return air.RiderStats(spin=p.stat_byte(0x1A), land=p.stat_byte(0x16), boost=p.stat_byte(0x1D),
                          air_turn=p.stat_byte(0x10), grab=p.stat_byte(0x14))


RIDERS = [  # name, stat %[16] (+0x0E..+0x1D), alt[4], mass byte (+0x1E), scale (+0x60), AI stance (+0x40), trick set (+0x5C)
    ('Eddie', (40, 75, 80, 23, 20, 23, 28, 70, 20, 70, 75, 40, 28, 60, 48, 28), (85, 95, 85, 85), 80, 0.85, 1, 0),
    ('Kaori', (28, 48, 95, 28, 13, 28, 43, 80, 13, 90, 94, 28, 43, 40, 50, 43), (85, 85, 80, 99), 60, 0.7, 0, 1),
    ('Luther', (25, 99, 80, 23, 50, 23, 13, 64, 50, 50, 55, 25, 13, 99, 95, 13), (85, 85, 99, 80), 127, 1.0, 0, 2),
    ('Mac', (28, 65, 90, 23, 15, 23, 45, 85, 15, 94, 88, 28, 45, 45, 56, 45), (80, 85, 85, 99), 65, 0.75, 0, 3),
    ('Moby', (30, 85, 80, 23, 23, 23, 35, 75, 23, 70, 80, 30, 35, 66, 84, 35), (85, 85, 90, 90), 75, 0.87, 1, 4),
    ('Zoe', (33, 75, 80, 28, 20, 28, 30, 82, 20, 75, 75, 33, 30, 56, 82, 30), (85, 90, 85, 90), 65, 0.85, 0, 5),
    ('JP', (30, 70, 85, 18, 23, 18, 40, 88, 23, 80, 65, 30, 40, 80, 72, 40), (80, 88, 83, 99), 85, 0.9, 1, 6),
    ('Elise', (30, 85, 75, 25, 25, 25, 30, 70, 25, 70, 70, 30, 30, 88, 64, 30), (80, 95, 85, 90), 70, 0.95, 1, 7),
    ('Psymon', (38, 95, 75, 15, 30, 15, 28, 70, 30, 75, 70, 38, 28, 85, 10, 28), (90, 88, 88, 85), 84, 0.85, 0, 8),
    ('Seeiah', (33, 65, 80, 23, 18, 23, 38, 75, 18, 75, 80, 33, 38, 70, 10, 38), (85, 90, 75, 99), 63, 0.9, 1, 9),
    ('Brodi', (43, 40, 75, 20, 33, 20, 15, 70, 33, 70, 65, 43, 15, 90, 10, 15), (90, 99, 90, 70), 93, 0.98, 0, 10),
    ('Marisol', (40, 55, 80, 28, 13, 28, 30, 80, 13, 80, 80, 40, 30, 50, 10, 30), (90, 95, 80, 85), 54, 0.85, 0, 11),
]

BOARDS = [  # per rider, 12 boards: (name, board class, unlock cost, stat[4] in attribute order)
    [
        ('Old School', 1, 0, (10, 0, 10, 10)),
        ('Anthem', 2, 0, (10, 10, 10, 0)),
        ('Warholinator', 1, 5, (20, 10, 10, 30)),
        ('Bell Bottom', 1, 20, (10, 20, 10, 30)),
        ('Heavy Metal', 1, 35, (30, 30, 30, 30)),
        ('Skunk Work', 1, 60, (30, 20, 30, 40)),
        ('New Recruit', 0, 90, (50, 50, 50, 40)),
        ('Grand Funk', 1, 120, (50, 40, 50, 50)),
        ('Pop & Lock', 1, 160, (70, 60, 70, 60)),
        ('Groove E', 1, 200, (70, 50, 70, 70)),
        ('Hip Hugger', 1, 240, (80, 60, 80, 80)),
        ('UBERBOARD', 1, 999, (90, 70, 90, 90)),
    ],
    [
        ('Panda Joy', 1, 0, (10, 0, 10, 10)),
        ('Lady Love Bug', 2, 0, (10, 10, 10, 0)),
        ('Skyhopper', 1, 5, (20, 10, 10, 30)),
        ('Moo Moo 22', 1, 20, (10, 20, 10, 30)),
        ('Alpha', 1, 35, (30, 30, 30, 30)),
        ('Candy', 1, 60, (30, 20, 30, 40)),
        ('Twinkle Star', 0, 90, (50, 50, 50, 40)),
        ('Kaori-anime', 1, 120, (50, 40, 50, 50)),
        ('Banana Peeler', 1, 160, (70, 60, 70, 60)),
        ('Snow Lover', 1, 200, (70, 50, 70, 70)),
        ('Princess Pie', 1, 240, (80, 60, 80, 80)),
        ('UBERBOARD', 1, 999, (90, 70, 90, 90)),
    ],
    [
        ('The Mullet', 0, 0, (10, 10, 0, 10)),
        ('Chitterling', 1, 0, (10, 0, 10, 10)),
        ('Phoenix USA', 0, 5, (20, 20, 20, 10)),
        ('Sweet Potato Pie', 0, 20, (20, 20, 10, 20)),
        ('The Gipper', 2, 35, (30, 50, 30, 20)),
        ('Growler', 0, 60, (30, 40, 30, 30)),
        ('True Grits ', 0, 90, (60, 50, 60, 40)),
        ('Ham Bone', 0, 120, (50, 60, 50, 50)),
        ('Jug Band Boogie', 0, 160, (80, 70, 60, 60)),
        ('Swamp Buggy', 0, 200, (70, 60, 70, 70)),
        ('Gears n Grease', 0, 240, (80, 80, 70, 80)),
        ('UBERBOARD', 0, 999, (90, 90, 80, 90)),
    ],
    [
        ('Wiener', 1, 0, (10, 0, 10, 10)),
        ('Hotrod', 2, 0, (10, 10, 10, 0)),
        ('Radioactive', 1, 5, (20, 10, 10, 30)),
        ('Spirit', 1, 20, (10, 20, 10, 30)),
        ('Boo Boo', 0, 35, (30, 30, 30, 30)),
        ('Flame On', 1, 60, (30, 20, 30, 40)),
        ('Buzzsaw', 1, 90, (50, 50, 50, 40)),
        ('AnimMac', 1, 120, (50, 40, 50, 50)),
        ('VeloCity', 1, 160, (70, 60, 70, 60)),
        ('Falciform', 1, 200, (70, 50, 70, 70)),
        ('Privateer', 1, 240, (80, 60, 80, 80)),
        ('UBERBOARD', 1, 999, (90, 70, 90, 90)),
    ],
    [
        ('Plasmatic', 0, 0, (10, 10, 0, 10)),
        ('Patriot', 1, 0, (10, 0, 10, 10)),
        ('Cutting', 0, 5, (20, 30, 10, 10)),
        ('Dream On', 0, 20, (20, 20, 10, 20)),
        ('Essence', 0, 35, (40, 30, 40, 20)),
        ('Greenblade', 0, 60, (30, 40, 30, 30)),
        ('Kinoptic', 2, 90, (50, 70, 60, 30)),
        ('Royal Strait', 0, 120, (50, 60, 50, 50)),
        ('Datum', 0, 160, (80, 70, 60, 60)),
        ('Sloppy Bite', 0, 200, (70, 60, 70, 70)),
        ('Hector', 0, 240, (80, 80, 70, 80)),
        ('UBERBOARD', 0, 999, (90, 90, 80, 90)),
    ],
    [
        ('Seratonin', 0, 0, (10, 10, 0, 10)),
        ('Anarchy', 1, 0, (10, 0, 10, 10)),
        ('Bad Girl', 0, 5, (20, 20, 20, 10)),
        ('Lethium Surprise', 0, 20, (20, 20, 10, 20)),
        ('Plastic You', 2, 35, (30, 50, 30, 20)),
        ('Alien Blues', 0, 60, (30, 40, 30, 30)),
        ('Chaos', 0, 90, (60, 50, 60, 40)),
        ('Latency', 0, 120, (50, 60, 50, 50)),
        ('Iridium', 0, 160, (80, 70, 60, 60)),
        ('Bomb Dropper', 0, 200, (70, 60, 70, 70)),
        ('Righteous Path', 0, 240, (80, 80, 70, 80)),
        ('UBERBOARD', 0, 999, (90, 90, 80, 90)),
    ],
    [
        ('ShowStopper', 1, 0, (10, 0, 10, 10)),
        ('JetSetter', 2, 0, (10, 10, 10, 0)),
        ('Magnate Split', 1, 5, (20, 10, 20, 20)),
        ('Eurotech', 1, 20, (20, 20, 10, 20)),
        ('Suave', 0, 35, (30, 30, 30, 30)),
        ('Resonance', 1, 60, (30, 20, 30, 40)),
        ('Chaos Crippler', 1, 90, (50, 50, 50, 40)),
        ('JP Ego-nomics', 1, 120, (50, 40, 50, 50)),
        ('Hucker Special', 1, 160, (80, 60, 60, 60)),
        ('Big Poppa', 1, 200, (70, 50, 70, 70)),
        ('Crazy Ates', 1, 240, (80, 60, 80, 80)),
        ('UBERBOARD', 1, 999, (90, 70, 90, 90)),
    ],
    [
        ('Kamoniwana', 0, 0, (10, 10, 0, 10)),
        ('Angel', 1, 0, (10, 0, 10, 10)),
        ('SmoothMaker', 0, 5, (20, 30, 10, 10)),
        ('Butterfly High', 0, 20, (20, 20, 10, 20)),
        ('True Colours', 0, 35, (40, 30, 40, 20)),
        ('Buttercup', 0, 60, (30, 40, 30, 30)),
        ('Cutting Edge', 2, 90, (50, 70, 60, 30)),
        ('Snake Eyes', 0, 120, (50, 60, 50, 50)),
        ('First Date', 0, 160, (80, 70, 60, 60)),
        ('Last Date', 0, 200, (70, 60, 70, 70)),
        ('Black Widow', 0, 240, (80, 80, 70, 80)),
        ('UBERBOARD', 0, 999, (90, 90, 80, 90)),
    ],
    [
        ('Bio Hazard', 0, 0, (10, 10, 0, 10)),
        ('Evil Eye', 1, 0, (10, 0, 10, 10)),
        ('Dolmans Land', 0, 5, (20, 30, 10, 10)),
        ('Chained Heat', 0, 20, (20, 20, 10, 20)),
        ('Mayhem', 0, 35, (40, 30, 40, 20)),
        ('Flesh Eater', 0, 60, (30, 40, 30, 30)),
        ('Torc Rules', 2, 90, (50, 70, 60, 30)),
        ('Live Wire', 0, 120, (50, 60, 50, 50)),
        ('Shock Therapy', 0, 160, (80, 70, 60, 60)),
        ('Banshee', 0, 200, (70, 60, 70, 70)),
        ('Dogs of War', 0, 240, (80, 80, 70, 80)),
        ('UBERBOARD', 0, 999, (90, 90, 80, 90)),
    ],
    [
        ('Funk-o-matic', 1, 0, (10, 0, 10, 10)),
        ('Psychadelic', 2, 0, (10, 10, 10, 0)),
        ('Potpourri', 1, 5, (20, 10, 20, 20)),
        ('Foxy Lady', 1, 20, (20, 20, 10, 20)),
        ('Lucky Stars', 0, 35, (30, 30, 30, 30)),
        ('Goddess', 1, 60, (30, 20, 30, 40)),
        ('Venusian', 1, 90, (50, 50, 50, 40)),
        ('Geodesic', 1, 120, (50, 40, 50, 50)),
        ('Time Machine', 1, 160, (80, 60, 60, 60)),
        ('Retro Beats', 1, 200, (70, 50, 70, 70)),
        ('Funky Drummer', 1, 240, (80, 60, 80, 80)),
        ('UBERBOARD', 1, 999, (90, 70, 90, 90)),
    ],
    [
        ('Now and Zen', 2, 0, (10, 10, 0, 10)),
        ('Buddha Booster', 1, 0, (10, 0, 0, 20)),
        ('Siddhartha', 2, 5, (20, 20, 20, 10)),
        ('Balance', 2, 20, (20, 20, 10, 20)),
        ('Flower Power', 2, 35, (40, 50, 20, 20)),
        ('The Absolute', 2, 60, (30, 40, 30, 30)),
        ('Yin-Yang', 0, 90, (60, 60, 50, 40)),
        ('Tsunami', 2, 120, (50, 60, 50, 50)),
        ('Master Blaster', 2, 160, (60, 70, 80, 60)),
        ('Nirvana', 2, 200, (70, 60, 70, 70)),
        ('Transcendence', 2, 240, (80, 80, 70, 80)),
        ('UBERBOARD', 2, 999, (90, 90, 90, 80)),
    ],
    [
        ('Fiesta', 2, 0, (10, 10, 0, 10)),
        ('Loco Motion', 1, 0, (10, 0, 0, 20)),
        ('Sand Fish', 2, 5, (20, 20, 20, 10)),
        ('Party Girl', 2, 20, (20, 20, 10, 20)),
        ('Dance Explosion', 2, 35, (40, 50, 20, 20)),
        ('Money Maker', 2, 60, (30, 40, 30, 30)),
        ('Wind Rider', 2, 90, (60, 60, 50, 40)),
        ('Booty Call', 0, 120, (50, 60, 50, 50)),
        ('Heart Breaker', 2, 160, (60, 70, 80, 60)),
        ('Latin Heat', 2, 200, (70, 60, 70, 70)),
        ('Rave On', 2, 240, (80, 80, 70, 80)),
        ('UBERBOARD', 2, 999, (90, 90, 90, 80)),
    ],
]


if __name__ == "__main__":
    # live check values: Moby player 85/85/90/90 board 0, Marisol AI level 240
    print(bytes(player_stats(4, 0, (85, 85, 90, 90))).hex(" "))
    print(bytes(ai_stats(11, 240)).hex(" "))
