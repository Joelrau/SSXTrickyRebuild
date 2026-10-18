"""Pre-race intro model: the cPreRaceHandler step machine (steps 0..0x15), the intro
dialogue slots, the countdown clock / beeps / start-light frames.

The handler
runs once per race-state update (60 Hz). Its steps are a chain of independent 'if' blocks, so
several steps can complete in the same frame; the model reproduces that by looping inside step().
External state is passed in through a small interface (see Env) so the port can drive it.
"""

NULL_SCRIPT = 0x39                 # intro slot type "Null" = skip the slot
SCRIPT_NAMES = (
    "RaceLoaded", "FreeLoaded", "ShowLoaded", "STG_VEN_1", "STG_VEN_2", "STG_VEN_3", "STG_VEN_4",
    "UNTR_VEN", "PIPE_VEN", "STG_COM_1", "STG_COM_2", "STG_COM_3", "STG_COM_4", "GAT_6COM_1",
    "GAT_6COM_2", "GAT_6COM_3", "GAT_6COM_4", "GAT_SO", "GAT_FR_1", "GAT_FR_2", "GAT_FR_3",
    "GAT_FO_1", "GAT_FO_2", "GAT_FO_3", "CRD_1", "CRD_2", "CRD_3", "FL_WIN_1", "FL_WIN_2",
    "FL_WIN_3", "FL_LOS_1", "FL_LOS_2", "FL_LOS_3",
) + tuple("FL_USR_VS_" + n for n in ("EDD", "KAO", "LUT", "MAC", "MOB", "ZOE", "JP", "ELI", "PSY", "SEE", "BRO", "MAR")) \
  + tuple("FL_%s_VS_USR" % n for n in ("EDD", "KAO", "LUT", "MAC", "MOB", "ZOE", "JP", "ELI", "PSY", "SEE", "BRO", "MAR")) \
  + ("Null",)
OVERLAY_NEXT_RACE, OVERLAY_HELP_RACE, OVERLAY_ASYNC_LOAD = 3, 18, 8
SKIP_BUTTONS, SKIP_BUTTONS_JAPANESE = 0x880, 0xC0      # Start|Cross, Start|Circle (game pad bits)
DONE = 0x15


def gates_used(mode, venue):
    """Countdown / start-gate test: false on track 7; true for modes 0, 2, 3, 4, 5, 7, 9."""
    return venue != 7 and mode in (0, 2, 3, 4, 5, 7, 9)


class IntroSlot:
    """Race state + 0x33C + k * 100 (k = 0..2): type (director script index) and 6 lines
    {speaker, line id, aux, speech value (>= 1000 = ANIM speech page, -1 none)}."""
    def __init__(self, script_type, lines=()):
        self.script_type = script_type
        self.lines = list(lines)

    @property
    def script_name(self):
        return SCRIPT_NAMES[self.script_type]


class Env:
    """Callbacks the handler polls/drives. Override in a port or a test."""
    def loading(self):            # race state + 0x4F8 (racer streaming / async load still busy)
        return False

    def director_running(self):   # camera director still has a script
        return False

    def skip_pressed(self):       # rising edge of SKIP_BUTTONS on the first active pad (consumed)
        return False

    def line_key(self, line_id):  # racer 0's animation-table lookup of the line id (TODO meaning); < 0 = drop
        return line_id

    def help_needed(self):        # some human's rank index > 7
        return False

    def act(self, name, *args):   # actions, recorded by tests
        pass


class PreRaceHandler:
    def __init__(self, env, mode, venue, slots, entry="normal"):
        """entry: 'normal' (new race), 'next_heat' (race state +0xE0, WC heat reload) or
        'restart' (race state +0xDC)."""
        self.env, self.mode, self.venue, self.slots = env, mode, venue, slots
        self.step = {"normal": 2, "next_heat": 0, "restart": 0x12}[entry]
        env.act("overlays_close_all")
        if entry == "normal":
            env.act("career_venue_counter")
            env.act("audio_intro_flag", 0)
        elif entry == "next_heat":
            env.act("audio_intro_flag", 0)
            env.act("audio_intro_start")
        env.act("director_reset")
        env.act("director_play", "PreLoad")
        if not gates_used(mode, venue):
            env.act("track_event", "NoCountdown")
        env.act("race_state_hook")                # race-state virtual (+0x6C), TODO

    # one slot (k = 0..2): select lines, post their speech, play the director script
    def _start_slot(self, k):
        slot = self.slots[k]
        lines = [ln for ln in slot.lines if ln[3] >= 0 and self.env.line_key(ln[1]) >= 0]
        lines.sort(key=lambda ln: self.env.line_key(ln[1]))      # ascending key, ties keep slot order
        values = [ln[3] for ln in lines]
        if values:
            self.env.act("intro_speech_begin")
            for v in values:
                if v >= 1000:
                    self.env.act("anim_speech", v)
        return values

    def _play_slot_script(self, k):
        self.env.act("director_reset")
        self.env.act("director_play", self.slots[k].script_name)
        self.env.act("fade_in", 1.0)

    def _stop_intro(self):
        self.env.act("intro_speech_end")
        self.env.act("director_reset")

    def _skip(self):
        if self.env.skip_pressed():
            self._stop_intro()
            return True
        return False

    def _card_decision(self):
        if self.env.help_needed():
            self.env.act("overlay_open", OVERLAY_HELP_RACE)
            return 0x10
        if self.mode == 1:
            return 0x12
        self.env.act("overlay_open", OVERLAY_NEXT_RACE)
        return 0x11

    def update(self):
        """One race-state update. Returns the step after the frame."""
        e = self.env
        e.act("loading_service")
        s = self.step
        if s == 0:
            e.act("director_reset")
            e.act("director_play", "PreLoad")
            s = 1
        if s == 1 and not e.loading():
            s = 2
        for k, (start, wait_load, play, wait) in enumerate(((2, 3, 4, 5), (6, 7, 8, 9), (10, 11, 12, 13))):
            if s == start:
                if k == 1:
                    e.act("audio_intro_start")
                if self.slots[k].script_type == NULL_SCRIPT:
                    s = (6, 10, 0xE)[k]
                else:
                    if k == 2:
                        e.act("racers_intro_reset")
                    self._start_slot(k)
                    s = play                  # the speech post helper always returns 0: the load wait is skipped
            if s == wait_load and not e.loading():
                s = play
            if s == play:
                if k == 0:
                    e.act("audio_intro_start")
                self._play_slot_script(k)
                s = wait
            if s == wait:
                if not e.director_running():
                    s = (6, 10, 0xE)[k]
                elif self._skip():
                    s = (6, 0xE, 0xE)[k]      # a skip during slot 1 also skips slot 2
        if s == 0xE:
            e.act("fade", 1.0)
            e.act("place_racers", gates_used(self.mode, self.venue))
            s = 0xF if e.loading() else self._card_decision()
        if s == 0xF and not e.loading():
            s = self._card_decision()
        if s == 0x10 and self._skip():
            e.act("overlay_close", OVERLAY_HELP_RACE)
            if self.mode == 1:
                s = 0x12
            else:
                e.act("overlay_open", OVERLAY_NEXT_RACE)
                s = 0x11
        if s == 0x11 and self._skip():
            e.act("overlay_close", OVERLAY_HELP_RACE)
            e.act("overlay_close", OVERLAY_NEXT_RACE)
            s = 0x12
        if s == 0x12:
            e.act("intro_music_end")
            e.act("audio_intro_flag", 1)
            s = 0x13 if e.loading() else 0x14
        if s == 0x13 and not e.loading():
            s = 0x14
        if s == 0x14:
            e.act("director_reset")
            e.act("intro_speech_end")
            e.act("place_racers", gates_used(self.mode, self.venue))
            e.act("phase_next")               # -> StartRace (one frame) -> Countdown
            s = DONE
        self.step = s
        return s


CLOCK_START, CLOCK_STEP = -300, 2
LIGHT_TIMES = (1.0, 0.5, 0.5, 0.5)       # CountDownStart: TexFlip message 2 values 1..4 after these waits


def countdown_timeline(enter_frame):
    """Frame numbers (race-state frame counter) of the countdown events, given the frame on which
    the countdown handler is entered (its first update runs on the same frame).
    Clock after the update of frame f: -300 + 2 * (f - enter + 1). Beep (zbxsfx #78) after each
    update with clock < 0 and clock % 60 == 0; GO = the update that reaches clock >= 0
    (phase 4 enter runs on the next update). Start lights: the track's CountDownStart script is
    started on the enter frame; its wait clock alignment (same frame or next) is TODO, so the
    light frames are given relative to the script start."""
    def clock(f):
        return CLOCK_START + CLOCK_STEP * (f - enter_frame + 1)
    beeps, f = [], enter_frame
    while clock(f) < 0:
        if clock(f) % 60 == 0:
            beeps.append(f)
        f += 1
    go = f
    lights, t = [], 0.0
    for dt in LIGHT_TIMES:
        t += dt
        lights.append(round(t * 60))
    return {"clock": clock, "beeps": beeps, "go": go, "race_enter": go + 1,
            "light_offsets": lights}
