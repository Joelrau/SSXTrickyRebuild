"""Input path model: tutorial .inp frame -> pad -> action mapper -> 4-byte control word -> named quantities.


Pipeline (one call per 60 Hz frame):
  1. A 44-byte .inp frame holds the recorded control word (+0), the recorded bool-action and grab
     held masks (+4, +8) and the raw pad block (+12..+43) that playback feeds to the mapper.
  2. The mapper evaluates bool actions from the *filtered* button word and analog actions from the
     pad axes with BTNMAP rules (dead zone 0.3805, sign = -raw, optional negate).
  3. The control word is packed with a layout chosen by the current control state.
  4. The control code decodes the word into steer/crouch/brake targets, jump/boost/trick buttons.

For replay, the recorded word is authoritative (it is exactly what the game consumed while recording);
encode_word() reconstructs it from the raw pad block and is used to cross-check / for live input.
"""
from dataclasses import dataclass, field
import struct

FRAME_SIZE = 44
HEADER_SIZE = 24
FRAME_CAPACITY = 7200

UP, DOWN, RIGHT, LEFT = 1, 2, 4, 8
SQUARE, TRIANGLE, CIRCLE, START, SELECT = 16, 32, 64, 128, 256
L1, R1, CROSS, L2, R2 = 512, 1024, 2048, 4096, 8192
L3, R3 = 1 << 14, 1 << 15
SHOULDERS = L1 | R1 | L2 | R2

GRAB_COUNT = 26                 # indices 0..25 = grab/trick slots; 26 = "no trick" code in the word
NO_TRICK = 26
ACT_JUMP = 27                   # BTNMAP "Antic"
ACT_JUMP_ABORT = 28             # "AnticAbort"
ACT_BOOST = 29
ACT_CAMERA_TOGGLE = 30
ACT_CAMERA_REVERSE = 31
ACT_TWEAK = 32
ACT_LATE_SPIN_MODE = 33
ACT_RAIL_SPIN_MODE = 34
ACT_PREWIND_TURN_MODE = 35
ACT_RANDOM_GRAB = 36
ACT_RESET = 37
ACT_SHOVE_LEFT = 38
ACT_SHOVE_RIGHT = 39
ACT_LESSON_CONTINUE = 40
FIRST_BOOL_ACTION = ACT_JUMP

ALL_BITS = (1 << 32) - 1
# BTNMAP0 IS_BOOL rows in order (pressed mask, not-pressed mask). "NONE" = never; "ALL" = no restriction.
BOOL_ACTIONS = [
    (CROSS, 0), (L3, 0), (SQUARE, 0), (ALL_BITS, 0), (ALL_BITS, 0), (SQUARE, 0), (ALL_BITS, 0),
    (ALL_BITS, 0), (ALL_BITS, 0), (ALL_BITS, SHOULDERS), (SELECT, 0), (ALL_BITS, 0), (ALL_BITS, 0),
    (CROSS, 0),
]

JOY_L_X, JOY_L_POS_X, JOY_L_NEG_X, JOY_L_Y, JOY_L_POS_Y, JOY_L_NEG_Y = 1, 2, 4, 8, 16, 32
JOY_R_X, JOY_R_POS_X, JOY_R_NEG_X, JOY_R_Y, JOY_R_POS_Y, JOY_R_NEG_Y = 64, 128, 256, 512, 1024, 2048
PAD_X, PAD_POS_X, PAD_NEG_X, PAD_Y, PAD_POS_Y, PAD_NEG_Y = 4096, 8192, 1 << 14, 1 << 15, 1 << 16, 1 << 17
POS_BITS = JOY_L_POS_X | JOY_L_POS_Y | JOY_R_POS_X | JOY_R_POS_Y | PAD_POS_X | PAD_POS_Y
NEG_BITS = JOY_L_NEG_X | JOY_L_NEG_Y | JOY_R_NEG_X | JOY_R_NEG_Y | PAD_NEG_X | PAD_NEG_Y
X_BITS = (JOY_L_X | JOY_L_POS_X | JOY_L_NEG_X | JOY_R_X | JOY_R_POS_X | JOY_R_NEG_X |
          PAD_X | PAD_POS_X | PAD_NEG_X)
DEAD_ZONE = 0.38046828

A_BRAKE, A_CROUCH, A_SPIN, A_FLIP, A_TURN, A_SHOVE_SIDE, A_SHOVE_FWD, A_RAIL_SPIN = range(8)
A_LATE_SPIN, A_LATE_FLIP, A_AIR_TURN, A_AIR_TILT, A_PREWIND_TURN, A_PREWIND_SPIN, A_PREWIND_FLIP, A_GATE_ROCK = range(8, 16)
# BTNMAP0 IS_ANALOG rows: (source mask, negate)
ANALOG_ACTIONS = [
    (PAD_NEG_Y | JOY_L_NEG_Y, True), (PAD_POS_Y | JOY_L_POS_Y, False), (PAD_X, True), (PAD_Y, True),
    (PAD_X | JOY_L_X, False), (JOY_R_X, False), (JOY_R_Y, False), (PAD_X, False),
    (PAD_X, True), (PAD_Y, True), (JOY_L_X, True), (JOY_L_Y, True),
    (JOY_L_X, False), (PAD_X, True), (PAD_Y, True), (JOY_L_Y, False),
]


@dataclass
class InpFrame:
    recorded_word: int        # +0  control word as consumed by the game
    recorded_bools: int       # +4  bit k = bool action (FIRST_BOOL_ACTION + k) held
    recorded_grabs: int       # +8  bit k = grab slot k held (slot assignment is per rider/lesson)
    buttons: int              # +12 raw buttons
    buttons_filtered: int     # +16 what the mapper evaluates (jump sometimes masked)
    pad_x: float              # +20 d-pad as axis
    pad_y: float
    left_x: float
    left_y: float
    right_x: float
    right_y: float

    def axis(self, group: str, use_x: bool) -> float:
        pair = {"pad": (self.pad_x, self.pad_y), "left": (self.left_x, self.left_y),
                "right": (self.right_x, self.right_y)}[group]
        return pair[0] if use_x else pair[1]


def load_inp(path):
    data = open(path, "rb").read()
    count = struct.unpack_from("<I", data, 0)[0]
    frames = []
    for i in range(count):
        v = struct.unpack_from("<5I6f", data, HEADER_SIZE + i * FRAME_SIZE)
        frames.append(InpFrame(*v))
    return frames


def bool_held(buttons: int, pressed: int, not_pressed: int) -> bool:
    if pressed == ALL_BITS:
        return False
    return (buttons & (pressed | not_pressed)) == pressed


def bool_mask(frame: InpFrame, layout: int = 0) -> int:
    """Held mask of the 14 bool actions, same packing as the recorded +4 word."""
    out = 0
    for k, (pressed, not_pressed) in enumerate((BOOL_ACTIONS, BTNMAP1_BOOL_ACTIONS)[layout]):
        if bool_held(frame.buttons_filtered, pressed, not_pressed):
            out |= 1 << k
    return out


def analog(frame: InpFrame, index: int, layout: int = 0) -> float:
    """Mapper analog read: first non-zero of d-pad, left stick, right stick; sign = -raw; dead zone.
    layout = BTNMAP number (rider_profile+0x6C)."""
    mask, negate = (ANALOG_ACTIONS, BTNMAP1_ANALOG_ACTIONS)[layout][index]
    value = 0.0
    for group, bits in (("pad", PAD_X | PAD_POS_X | PAD_NEG_X | PAD_Y | PAD_POS_Y | PAD_NEG_Y),
                        ("left", JOY_L_X | JOY_L_POS_X | JOY_L_NEG_X | JOY_L_Y | JOY_L_POS_Y | JOY_L_NEG_Y),
                        ("right", JOY_R_X | JOY_R_POS_X | JOY_R_NEG_X | JOY_R_Y | JOY_R_POS_Y | JOY_R_NEG_Y)):
        if value == 0.0 and (mask & bits):
            value = -frame.axis(group, bool(mask & bits & X_BITS))
    if (mask & POS_BITS) and value < 0.0:
        return 0.0
    if (mask & NEG_BITS) and value > 0.0:
        return 0.0
    if abs(value) < DEAD_ZONE:
        return 0.0
    return -value if negate else value


# Differences from BTNMAP0: AnticAbort NONE, CameraReverse TRIANGLE; Spin/Flip/PrewindSpin/PrewindFlip add
# the left stick, GateRock adds the d-pad Y, PrewindTurn is ANALOG_NULL (DATA/CONFIG/BTNMAP1.DAT).
BTNMAP1_BOOL_ACTIONS = list(BOOL_ACTIONS)
BTNMAP1_BOOL_ACTIONS[ACT_JUMP_ABORT - FIRST_BOOL_ACTION] = (ALL_BITS, 0)
BTNMAP1_BOOL_ACTIONS[ACT_CAMERA_REVERSE - FIRST_BOOL_ACTION] = (TRIANGLE, 0)
BTNMAP1_ANALOG_ACTIONS = list(ANALOG_ACTIONS)
BTNMAP1_ANALOG_ACTIONS[A_SPIN] = (PAD_X | JOY_L_X, True)
BTNMAP1_ANALOG_ACTIONS[A_FLIP] = (PAD_Y | JOY_L_Y, True)
BTNMAP1_ANALOG_ACTIONS[A_PREWIND_TURN] = (0, False)
BTNMAP1_ANALOG_ACTIONS[A_PREWIND_SPIN] = (PAD_X | JOY_L_X, True)
BTNMAP1_ANALOG_ACTIONS[A_PREWIND_FLIP] = (PAD_Y | JOY_L_Y, True)
BTNMAP1_ANALOG_ACTIONS[A_GATE_ROCK] = (PAD_Y | JOY_L_Y, False)


# Stick direction bits (BTNMAP tokens LJOY_UP..RJOY_LEFT); strict |v| > 0.5; only the FE key repeat uses them.
LJOY_UP, LJOY_DOWN, LJOY_RIGHT, LJOY_LEFT = 1 << 17, 1 << 18, 1 << 19, 1 << 20
RJOY_UP, RJOY_DOWN, RJOY_RIGHT, RJOY_LEFT = 1 << 21, 1 << 22, 1 << 23, 1 << 24
# Game bit i <- libpad active-high word (data[2] << 8 | data[3], inverted) & HW_TO_GAME[i]
HW_TO_GAME = [0x1000, 0x4000, 0x2000, 0x8000, 0x80, 0x10, 0x20, 0x800, 0x100, 0x4, 0x8, 0x40,
              0x1, 0x2, 0x200, 0x400]
# FE key-repeat rows: (button mask, key, key while any shoulder 0x3600 is held)
FE_KEYS = [(SELECT, 0x1B, 0x1B), (START, 0x0D, 0x0D), (SQUARE, 0x61, 0x41), (TRIANGLE, 0x62, 0x42),
           (CIRCLE, 0x63, 0x43), (CROSS, 0x64, 0x44), (LEFT | LJOY_LEFT, 0x4B00, 0x4700),
           (RIGHT | LJOY_RIGHT, 0x4D00, 0x4F00), (DOWN | LJOY_DOWN, 0x5000, 0x5100),
           (UP | LJOY_UP, 0x4800, 0x4900), (R1, 0x72, 0x72), (R2, 0x52, 0x52), (L1, 0x6C, 0x6C), (L2, 0x4C, 0x4C)]


def stick(byte: int) -> float:
    return (byte - 127.5) / 127.5


class PadDevice:
    """The 44-byte pad object the mapper reads. update() = poll + translate for one frame."""

    def __init__(self, key_repeat=False):
        self.prev = self.buttons = 0
        self.pad_x = self.pad_y = 0.0
        self.left_x = self.left_y = self.right_x = self.right_y = 0.0
        self.key_repeat = key_repeat
        self.fe_key, self.fe_last, self.fe_count = 0, -1, 0

    def update(self, hw_buttons: int, lx=128, ly=128, rx=128, ry=128, analog_mode=True, connected=True):
        """hw_buttons: active-high libpad word (data[2] << 8 | data[3], inverted); sticks are 0..255 bytes."""
        if not connected:
            hw_buttons, analog_mode = 0, False
        self.prev, b = self.buttons, 0
        for i, m in enumerate(HW_TO_GAME):
            if hw_buttons & m:
                b |= 1 << i
        if analog_mode:
            self.left_x, self.left_y, self.right_x, self.right_y = stick(lx), stick(ly), stick(rx), stick(ry)
        else:
            self.left_x = self.left_y = self.right_x = self.right_y = 0.0
        self.pad_x = -1.0 if b & LEFT else (1.0 if b & RIGHT else 0.0)
        self.pad_y = -1.0 if b & UP else (1.0 if b & DOWN else 0.0)
        if analog_mode:
            for v, neg, pos in ((self.left_x, LJOY_LEFT, LJOY_RIGHT), (self.left_y, LJOY_UP, LJOY_DOWN),
                                (self.right_x, RJOY_LEFT, RJOY_RIGHT), (self.right_y, RJOY_UP, RJOY_DOWN)):
                if v < -0.5:
                    b |= neg
                if v > 0.5:
                    b |= pos
        self.buttons = b
        self._fe_repeat()
        return self

    def _fe_repeat(self):
        key = 0
        for mask, k, k_shoulder in FE_KEYS:
            if self.buttons & mask:
                key = k_shoulder if self.buttons & (L1 | R1 | L2 | R2) else k
                break
        self.fe_key = 0
        if key == 0 or key != self.fe_last:
            self.fe_key, self.fe_count, self.fe_last = key, -7, key
        else:
            self.fe_count += 1
            if self.fe_count > 7 and self.key_repeat:
                self.fe_key, self.fe_count = key, 0

    def frame(self) -> "InpFrame":
        """The 32-byte block cPS2Device slot 7 returns (= .inp +12..+43), wrapped as an InpFrame."""
        return InpFrame(0, 0, 0, self.prev, self.buttons, self.pad_x, self.pad_y,
                        self.left_x, self.left_y, self.right_x, self.right_y)


# BTNMAP IS_BOOLGRAB rows (easyA..hardE): exact shoulder combination (all other shoulders released).
GRAB_COMBOS = [L2, R1, R2, L1, R1 | R2, L1 | L2, L1 | R2, L2 | R2, L1 | R1, R1 | L2,
               L1 | R1 | R2, R1 | L2 | R2, L1 | L2 | R1, L1 | L2 | R2, SHOULDERS]
# Slot that combo k lands in: GRAB_SLOT_TABLE[character][board_class][k].
# character = rider_profile+0x5C (0 Eddie .. 11 Marisol), board_class = rider_profile+0x44 (0..2).
# The random-grab action picks slot GRAB_SLOT_TABLE[character][board_class][rand & 3].
GRAB_SLOT_TABLE = [
    [[22, 12, 11, 6, 13, 25, 14, 5, 9, 8, 21, 0, 2, 16, 4],
     [22, 12, 11, 6, 13, 25, 14, 5, 9, 8, 2, 0, 7, 16, 4],
     [22, 12, 11, 6, 13, 25, 14, 5, 8, 9, 3, 18, 2, 16, 4]],  # character 0
    [[11, 6, 22, 12, 25, 13, 17, 5, 15, 0, 24, 10, 8, 16, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 10, 0, 8, 2, 16, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4]],  # character 1
    [[11, 6, 22, 12, 25, 13, 14, 5, 21, 9, 2, 8, 19, 16, 4],
     [11, 6, 22, 12, 25, 13, 20, 7, 21, 9, 16, 1, 2, 8, 19],
     [11, 6, 22, 12, 25, 13, 14, 5, 8, 9, 3, 18, 2, 16, 4]],  # character 2
    [[11, 6, 22, 12, 25, 13, 0, 15, 21, 16, 2, 9, 14, 8, 4],
     [11, 6, 22, 12, 25, 13, 24, 5, 21, 23, 20, 19, 9, 2, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 8, 9, 3, 18, 2, 16, 4]],  # character 3
    [[22, 12, 11, 6, 13, 25, 1, 5, 21, 23, 0, 16, 2, 8, 4],
     [22, 12, 11, 6, 13, 25, 23, 5, 15, 8, 2, 10, 19, 9, 4],
     [22, 12, 11, 6, 13, 25, 14, 5, 8, 9, 3, 18, 2, 16, 4]],  # character 4
    [[11, 6, 22, 12, 25, 13, 15, 5, 21, 10, 2, 19, 8, 24, 4],
     [11, 6, 22, 12, 25, 13, 17, 5, 0, 24, 7, 10, 2, 23, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4]],  # character 5
    [[22, 12, 11, 6, 13, 25, 20, 14, 15, 8, 1, 9, 2, 16, 19],
     [22, 12, 11, 6, 13, 25, 15, 5, 21, 9, 2, 16, 7, 8, 19],
     [22, 12, 11, 6, 13, 25, 14, 5, 8, 9, 3, 18, 2, 16, 4]],  # character 6
    [[22, 12, 11, 6, 13, 25, 16, 7, 21, 8, 2, 0, 9, 15, 4],
     [22, 12, 11, 6, 13, 25, 16, 7, 21, 8, 2, 0, 9, 15, 4],
     [22, 12, 11, 6, 13, 25, 14, 5, 21, 8, 9, 18, 2, 7, 4]],  # character 7
    [[11, 6, 22, 12, 25, 13, 17, 5, 15, 0, 24, 10, 8, 16, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 10, 0, 8, 2, 16, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4]],  # character 8
    [[22, 12, 11, 6, 13, 25, 17, 5, 15, 0, 24, 10, 8, 16, 4],
     [22, 12, 11, 6, 13, 25, 14, 5, 21, 10, 0, 8, 2, 16, 4],
     [22, 12, 11, 6, 13, 25, 14, 5, 21, 8, 9, 18, 2, 7, 4]],  # character 9
    [[11, 6, 22, 12, 25, 13, 17, 5, 15, 0, 24, 10, 8, 16, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 10, 0, 8, 2, 16, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4]],  # character 10
    [[11, 6, 22, 12, 25, 13, 17, 5, 15, 0, 24, 10, 8, 16, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 10, 0, 8, 2, 16, 4],
     [11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4]],  # character 11
]
# Board class each tutorial rider uses (the only value that reproduces all recorded grab masks).
TUTORIAL_BOARD_CLASS = {"eddi": 1, "kaor": 1, "luth": 0, "mac": 1, "moby": 0, "zoe": 0, "jp": 1,
                        "elis": 0, "psym": 0, "seei": 1, "brod": 2, "mari": 2}
CHARACTER_PREFIX = ["eddi", "kaor", "luth", "mac", "moby", "zoe", "jp", "elis", "psym", "seei", "brod", "mari"]


def grab_mask(buttons: int, character: int, board_class: int) -> int:
    """Held mask over the 26 trick slots, same packing as the recorded +8 word."""
    shoulders = buttons & SHOULDERS
    out = 0
    for k, combo in enumerate(GRAB_COMBOS):
        if shoulders == combo:
            out |= 1 << GRAB_SLOT_TABLE[character][board_class][k]
    return out


@dataclass
class MapperState:
    """Per-frame action view. held/pressed are sets of action indices."""
    held: set = field(default_factory=set)
    pressed: set = field(default_factory=set)
    axes: list = field(default_factory=list)


class Mapper:
    def __init__(self, character=None, board_class=0):
        self.prev_held = set()
        self.character, self.board_class = character, board_class

    def step(self, frame: InpFrame, use_recorded_masks: bool = True) -> MapperState:
        held = set()
        bools = frame.recorded_bools if use_recorded_masks else bool_mask(frame)
        for k in range(len(BOOL_ACTIONS)):
            if bools >> k & 1:
                held.add(FIRST_BOOL_ACTION + k)
        # grab slots: combo -> slot depends on rider and board class (GRAB_SLOT_TABLE)
        grabs = frame.recorded_grabs
        if not use_recorded_masks and self.character is not None:
            grabs = grab_mask(frame.buttons_filtered, self.character, self.board_class)
        for k in range(GRAB_COUNT):
            if grabs >> k & 1:
                held.add(k)
        pressed = held - self.prev_held
        self.prev_held = held
        return MapperState(held, pressed, [analog(frame, i) for i in range(len(ANALOG_ACTIONS))])


# control-state ids (same numbering as models/state_machine.py ControlState)
GROUND_LAYOUT = {2, 3, 4, 5, 7, 11, 12, 18}
GATE_LAYOUT = {8, 9, 17}
JUMP, NATURAL_AIR, PREWIND, RAIL, SPIN, WIPE_OUT, WIPE_OUT_RECOVER, LESSON_WAIT = 10, 13, 14, 15, 16, 19, 20, 21


def quant6(v: float) -> int:
    return int(v * 31.0) & 63            # C float->int truncates toward zero


def sign2(v: float) -> int:
    return (3 if v < -0.5 else (1 if v > 0.5 else 0))


def shove_side(m: MapperState) -> int:
    if ACT_SHOVE_LEFT in m.pressed:
        return 3
    if ACT_SHOVE_RIGHT in m.pressed:
        return 1
    return sign2(m.axes[A_SHOVE_SIDE])


def encode_word(control: int, m: MapperState) -> int:
    """Rebuild the control word the player-input routine would produce for this control state."""
    held, pressed, ax = m.held, m.pressed, m.axes
    w = 0
    if control in GROUND_LAYOUT:
        w |= int(ACT_RESET in pressed) | int(ACT_JUMP in pressed) << 1 | int(ACT_JUMP in held) << 2
        w |= int(ACT_BOOST in held) << 3 | int(ACT_BOOST in pressed) << 4
        w |= quant6(ax[A_TURN]) << 5 | quant6(ax[A_CROUCH]) << 11 | quant6(ax[A_BRAKE]) << 17
        w |= shove_side(m) << 23 | sign2(ax[A_SHOVE_FWD]) << 25
    elif control in GATE_LAYOUT:
        w |= quant6(ax[A_GATE_ROCK])
    elif control == JUMP:
        w |= int(ACT_JUMP in held) << 1 | int(ACT_RESET in pressed)
    elif control == NATURAL_AIR:
        trick = ACT_RANDOM_GRAB in held or any(k in held for k in range(GRAB_COUNT))
        w |= int(trick) | int(ACT_RESET in pressed) << 1
        w |= quant6(ax[A_CROUCH]) << 2 | quant6(ax[A_TURN]) << 8
        w |= quant6(ax[A_PREWIND_SPIN]) << 14 | quant6(ax[A_PREWIND_FLIP]) << 20
        w |= shove_side(m) << 26 | sign2(ax[A_SHOVE_FWD]) << 28
    elif control == PREWIND:
        w |= quant6(ax[A_PREWIND_TURN]) << 17 | quant6(ax[A_PREWIND_SPIN]) << 5 | quant6(ax[A_PREWIND_FLIP]) << 11
        w |= int(ACT_RESET in pressed) | int(ACT_JUMP in held) << 1 | int(ACT_JUMP_ABORT in held) << 2
        w |= int(ACT_BOOST in held) << 3 | int(ACT_PREWIND_TURN_MODE in held) << 4
    elif control == RAIL:
        w |= quant6(ax[A_TURN]) << 5 | quant6(ax[A_CROUCH]) << 11 | quant6(ax[A_RAIL_SPIN]) << 17
        w |= int(ACT_JUMP in pressed) << 1 | int(ACT_JUMP in held) << 2 | int(ACT_RESET in pressed)
        w |= int(ACT_RAIL_SPIN_MODE in held) << 3 | int(ACT_BOOST in held) << 4
    elif control == SPIN:
        w |= int(ACT_RESET in pressed) << 8 | int(ACT_TWEAK in held) << 9 | int(ACT_LATE_SPIN_MODE in held) << 10
        w |= sign2(ax[A_SPIN]) << 12 | sign2(ax[A_FLIP]) << 14 | sign2(ax[A_LATE_SPIN]) << 16
        w |= sign2(ax[A_LATE_FLIP]) << 18 | sign2(ax[A_AIR_TURN]) << 20 | sign2(ax[A_AIR_TILT]) << 22
        w |= shove_side(m) << 24 | sign2(ax[A_SHOVE_FWD]) << 26
        trick = NO_TRICK
        for k in range(GRAB_COUNT):          # last held grab slot wins
            if k in held:
                trick = k
        w |= trick
    elif control == WIPE_OUT:
        w |= quant6(ax[A_AIR_TURN]) << 1 | quant6(ax[A_AIR_TILT]) << 7 | int(ACT_RESET in pressed)
    elif control == WIPE_OUT_RECOVER:
        w |= int(ACT_RESET in pressed)
    elif control == LESSON_WAIT:
        w |= int(ACT_LESSON_CONTINUE in pressed)
    return w


def s6(word: int, lo: int) -> float:
    v = (word >> lo) & 63
    return (v - 64 if v & 32 else v) / 31.0


def s2(word: int, lo: int) -> int:
    v = (word >> lo) & 3
    return v - 4 if v & 2 else v


@dataclass
class ControlInput:
    """Named quantities the control code consumes (absent fields stay at their defaults)."""
    reset_request: bool = False
    jump_held: bool = False
    jump_pressed: bool = False
    jump_abort: bool = False
    boost_held: bool = False
    boost_pressed: bool = False
    tweak: bool = False
    trick_pressed: bool = False
    trick_id: int = NO_TRICK
    turn: float = 0.0          # steer target source, + = d-pad right
    crouch: float = 0.0
    brake: float = 0.0
    rail_spin: float = 0.0
    prewind_turn: float = 0.0
    prewind_spin: float = 0.0
    prewind_flip: float = 0.0
    gate_rock: float = 0.0
    spin: int = 0
    flip: int = 0
    late_spin: int = 0
    late_flip: int = 0
    air_turn: float = 0.0
    air_tilt: float = 0.0
    shove_side: int = 0
    shove_fwd: int = 0
    lesson_continue: bool = False


def decode_word(control: int, w: int) -> ControlInput:
    c = ControlInput()
    if control in GROUND_LAYOUT:
        c.reset_request, c.jump_pressed, c.jump_held = bool(w & 1), bool(w & 2), bool(w & 4)
        c.boost_held, c.boost_pressed = bool(w & 8), bool(w & 16)
        c.turn, c.crouch, c.brake = s6(w, 5), s6(w, 11), s6(w, 17)
        c.shove_side, c.shove_fwd = s2(w, 23), s2(w, 25)
    elif control in GATE_LAYOUT:
        c.gate_rock = s6(w, 0)
    elif control == JUMP:
        c.reset_request, c.jump_held = bool(w & 1), bool(w & 2)
    elif control == NATURAL_AIR:
        c.trick_pressed, c.reset_request = bool(w & 1), bool(w & 2)
        c.crouch, c.turn, c.prewind_spin, c.prewind_flip = s6(w, 2), s6(w, 8), s6(w, 14), s6(w, 20)
        c.shove_side, c.shove_fwd = s2(w, 26), s2(w, 28)
    elif control == PREWIND:
        c.reset_request, c.jump_held, c.jump_abort, c.boost_held = bool(w & 1), bool(w & 2), bool(w & 4), bool(w & 8)
        c.prewind_spin, c.prewind_flip, c.prewind_turn = s6(w, 5), s6(w, 11), s6(w, 17)
    elif control == RAIL:
        c.reset_request, c.jump_pressed, c.jump_held, c.boost_held = bool(w & 1), bool(w & 2), bool(w & 4), bool(w & 16)
        c.turn, c.crouch, c.rail_spin = s6(w, 5), s6(w, 11), s6(w, 17)
    elif control == SPIN:
        c.trick_id = w & 255
        c.reset_request, c.tweak = bool(w >> 8 & 1), bool(w >> 9 & 1)
        c.trick_pressed = c.trick_id != NO_TRICK
        c.spin, c.flip, c.late_spin, c.late_flip = s2(w, 12), s2(w, 14), s2(w, 16), s2(w, 18)
        c.air_turn, c.air_tilt = float(s2(w, 20)), float(s2(w, 22))
        c.shove_side, c.shove_fwd = s2(w, 24), s2(w, 26)
    elif control == WIPE_OUT:
        c.reset_request = bool(w & 1)
        c.air_turn, c.air_tilt = s6(w, 1), s6(w, 7)
    elif control == WIPE_OUT_RECOVER:
        c.reset_request = bool(w & 1)
    elif control == LESSON_WAIT:
        c.lesson_continue = bool(w & 1)
    return c


def infer_controls(word: int, m: MapperState):
    """Control states whose layout reproduces the recorded word from the raw pad (replay cross-check)."""
    candidates = sorted(GROUND_LAYOUT | GATE_LAYOUT | {JUMP, NATURAL_AIR, PREWIND, RAIL, SPIN,
                                                       WIPE_OUT, WIPE_OUT_RECOVER, LESSON_WAIT})
    return [c for c in candidates if encode_word(c, m) == word]


def tutorial_mapper(path):
    """Mapper configured like the tutorial: BTNMAP layout 0 and the rider's grab slots."""
    import os
    name = os.path.basename(path).lower()[:-6]
    return Mapper(CHARACTER_PREFIX.index(name), TUTORIAL_BOARD_CLASS[name])


if __name__ == "__main__":
    import sys
    from collections import Counter
    total, matched, used = 0, 0, Counter()
    for path in sys.argv[1:]:
        mapper = tutorial_mapper(path)
        for fr in load_inp(path):
            m = mapper.step(fr, use_recorded_masks=False)     # pure raw-pad path
            hits = infer_controls(fr.recorded_word, m)
            total += 1
            matched += bool(hits)
            used[tuple(hits[:3])] += 1
    print(f"frames {total}, word rebuilt from raw pad by at least one layout: {matched} "
          f"({100.0 * matched / max(total, 1):.2f}%)")
    for k, v in used.most_common(12):
        print(v, k)
