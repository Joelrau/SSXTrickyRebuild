"""World Circuit heats model: story ("line-up") table matching, elimination table,
6-entry grid draw, heat result / heat factor / exit code, relationship carry-over.

Rider ids 0..11,
12 = none. 'rand' is the game's random stream A; pass a callable that
returns its next value so a caller can replay an exact sequence.
"""
import json
import os

NONE = 12
N_RIDERS = 12
GRID = 6
FREE, HELD, TAKEN = 2, 1, 0              # elimination-table per-heat status values
HEAT_FACTOR = {0: 0.832996129989624, 1: 0.33362993597984314, 2: 0.16681496798992157}   # f32 patterns 0x3F553F3C / 0x3EAAD18B / 0x3E2AD18B
EXIT_NEXT_HEAT, EXIT_DONE = 4, 2
DEFAULT_ID, DEFAULT_TYPE = 0x1AD, 0x39   # 0x39 = director script "Null"

# wildcard (scores 0) and "any" (scores +1) codes per record field
FIELD_CODES = (("rider", 12, 13), ("venue", 13, 14), ("mode", 10, 11), ("heat", 3, 4))


class StoryRecord:
    """12 bytes: rider, venue, mode, heat, 5 riders (12 none), u16 intro id, u8 intro type."""
    def __init__(self, raw):
        self.rider, self.venue, self.mode, self.heat = raw[0], raw[1], raw[2], raw[3]
        self.riders = list(raw[4:9])
        self.intro_id = raw[9] | (raw[10] << 8)
        self.intro_type = raw[11]

    def score(self, rider, venue, mode, heat):
        """-1 = rejected; else the number of exact or 'any' fields."""
        total = 0
        for (name, wild, anyc), want in zip(FIELD_CODES, (rider, venue, mode, heat)):
            v = getattr(self, name)
            if (v != wild and v == want) or v == anyc:
                total += 1
            elif v != wild:
                return -1
        return total


def parse_story_table(blob, count):
    return [StoryRecord(blob[12 * i:12 * i + 12]) for i in range(count)]


class StoryResult:
    def __init__(self):
        self.riders = [NONE] * 5
        self.intro_ids = [DEFAULT_ID] * 15
        self.intro_types = [DEFAULT_TYPE] * 15


def story_lookup(tables, rider, venue, mode, heat, rand):
    """Tables 0, 1, 2 in order. Per table: the records with the best score are collected (a higher
    score restarts the list); one is drawn with rand() % count (only if count > 0). Its non-12 riders
    overwrite the result riders; its id / type go to slot t."""
    out = StoryResult()
    for t, table in enumerate(tables):
        best, picks = 0, []
        for i, rec in enumerate(table):
            s = rec.score(rider, venue, mode, heat)
            if s >= 0 and s >= best:
                if s > best:
                    picks, best = [], s
                picks.append(i)
        if picks:
            rec = table[picks[rand() % len(picks)]]
            for k, r in enumerate(rec.riders):
                if r != NONE:
                    out.riders[k] = r
            out.intro_ids[t] = rec.intro_id
            out.intro_types[t] = rec.intro_type
    return out


class Elimination:
    """Per rider: status[3] (one per heat) and the eliminated flag."""
    def __init__(self):
        self.status = [[FREE] * 3 for _ in range(N_RIDERS)]
        self.flag = [0] * N_RIDERS

    def init_heat0(self, tables, player_rider, venue, mode, rand, rider_of_slot=None):
        """Heat 0 only. rider_of_slot[r] = the rider id of static rider record r (identity in the
        shipped data). Three story lookups (one per heat k) are made first."""
        rider_of_slot = rider_of_slot or list(range(N_RIDERS))
        per_heat = [story_lookup(tables, player_rider, venue, mode, k, rand).riders for k in range(3)]
        for r in range(N_RIDERS):
            self.flag[r] = 0
            self.status[r] = [FREE] * 3
            rid = rider_of_slot[r]
            for k in range(3):
                if rid == player_rider:
                    self.status[r][k] = TAKEN
                elif rid in per_heat[k]:
                    self.status[r][k] = TAKEN
                    for j in range(k):
                        self.status[r][j] = HELD
        return per_heat


def build_grid(elim, tables, player_rider, venue, mode, heat, rand, alaska_venue=11):
    """Returns (grid of 6 rider ids, StoryResult). Grid index = start slot (6 racers)."""
    story = story_lookup(tables, player_rider, venue, mode, heat, rand)
    grid = [NONE] * GRID
    r = rand()
    p = (r & 3) + 1
    if venue == alaska_venue and (r & 3) in (1, 2):
        p = 1
    grid[p] = player_rider

    def take(rider):
        elim.flag[rider] = 0
        elim.status[rider][heat] = TAKEN

    if story.riders[0] != NONE:
        grid[p - 1] = story.riders[0]
        take(story.riders[0])
    if story.riders[1] != NONE:
        grid[p + 1] = story.riders[1]            # p <= 4, so p + 1 <= 5
        take(story.riders[1])
    for k in (2, 3, 4):
        rider = story.riders[k]
        if rider == NONE:
            continue
        for i in range(GRID):
            if grid[i] == NONE:
                grid[i] = rider
                take(rider)
                break
    draws = [rand() % 10000 if (elim.flag[x] == 0 and elim.status[x][heat] == FREE) else -1
             for x in range(N_RIDERS)]
    used = 0
    for i in range(GRID):
        if grid[i] == NONE:
            best, pick = -1, NONE
            for x in range(N_RIDERS):
                if draws[x] > best:
                    best, pick = draws[x], x
            if pick == NONE:
                # fallback: lowest rider whose bit is not yet set (only entries before i are counted)
                pick = 0 if not used & 1 else next((x for x in range(1, N_RIDERS) if not used >> x & 1), NONE)
            else:
                draws[pick] = -1
            grid[i] = pick
        used |= 1 << (grid[i] & 31)
    return grid, story


def heat_result(elim, finish_riders, player_rider, heat):
    """finish_riders = rider ids in finishing order. Returns (advance, heat_factor or None).
    Riders 4th or worse are eliminated; the player's place sets the heat factor (start boost
    meter of the next heat). The final (heat 2) never advances."""
    advance = heat != 2
    factor = None
    for place, rider in enumerate(finish_riders):
        if rider == player_rider:
            if place in HEAT_FACTOR:
                factor = HEAT_FACTOR[place]
            else:
                advance = False
        if place > 2:
            elim.flag[rider] = 1
    return advance, factor


def circuit_exit_code(mode, advance):
    """Value stored for the post-race flow: 4 = load the next heat (heat += 1); else 2 and the heat
    counter returns to 0 (failed heat, final finished, or not a circuit race)."""
    return EXIT_NEXT_HEAT if (mode == 4 and advance) else EXIT_DONE


def load_aggression(path=None):
    """table[me][other] = 4 words (0x167208); from a JSON file {"table": ...} or the exe (exe_tables)."""
    if path:
        return json.load(open(path))["table"]
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import exe_tables
    return exe_tables.aggression_table()


def initial_levels(agg, riders, human_index, heat, carried, career_levels):
    """Relationship level bytes lv[i][j] = racer i's level toward racer j (racer byte +0x20 + 4j).
    Toward an AI racer j: table[rider_i][rider_j] word 1. Toward the human: the carried value
    C+0x1C[rider_i] when heat != 0 and it is >= 0, else the career level word of rider_i.
    Then every pair: level -= {0: 2, 1: 3, 2: 1}[kind] (kind = table word 3), clamped at 0, and
    raised to at least table word 1."""
    n = len(riders)
    lv = [[0] * n for _ in range(n)]
    for i in range(n):
        for j in range(n):
            me, other = riders[i], riders[j]
            if j != human_index:
                lv[i][j] = agg[me][other][1]
            elif heat != 0 and carried[me] >= 0:
                lv[i][j] = carried[me]
            else:
                lv[i][j] = career_levels[me]
    for i in range(n):
        for j in range(n):
            me, other = riders[i], riders[j]
            v = lv[i][j] - {0: 2, 1: 3, 2: 1}.get(agg[me][other][3], 0)
            lv[i][j] = max(max(v, 0), agg[me][other][1])
    return lv


def carry_over(riders, human_index, levels_toward_human):
    """Race end (mode 4): C+0x1C[rider_j] = racer j's live level byte toward the human, for every
    other racer j; riders not in the race keep their previous value."""
    return {riders[j]: levels_toward_human[j] for j in range(len(riders)) if j != human_index}
