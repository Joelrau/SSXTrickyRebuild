"""Boarder motion/control state machine.

Merges the transitions of the ground/rail, air/tricks and collision/wipe-out models.  The boarder always holds
one MotionState (physics integrator) and one ControlState (input interpretation / animation phase).
Changing either runs the old state's Exit hook, stores the new id, then runs the new state's Enter
hook with the previous id.

`owner` names the model that established the condition; `todo=True` marks the few rows whose
condition still needs an export or a PCSX2 trace (listed).
"""
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional


class MotionState(IntEnum):
    NONE = 0              # only right after reset, before the first SetMotion
    AIR = 1
    GROUND = 2
    RAIL_SLIDE = 3
    STATIC = 4            # frozen: no motion update at all
    WIPE_OUT = 5
    WIPE_OUT_RECOVER = 6  # also used for getting up after the start-line sit


class ControlState(IntEnum):
    NONE = 0
    VOID = 1              # no input handling
    BUMP = 2
    CRUISE = 3
    CHANGE_FAKIE = 4
    FAKIE_CRUISE = 5
    FINISH_LINE = 6
    GATE_TO_BASE = 7
    GATE_ANTICIPATE = 8
    GATE_LAUNCH = 9
    JUMP = 10
    LAND_HARD = 11
    LAND_NORMAL = 12
    NATURAL_AIR = 13
    PREWIND = 14
    RAIL_SLIDE = 15
    SPIN = 16             # in-air trick control (spins, flips, grabs)
    SIT_AND_WAIT = 17
    GETUP_FROM_SIT = 18
    WIPE_OUT = 19
    WIPE_OUT_RECOVER = 20
    LESSON_WAIT = 21
    RESET_WAIT = 22


# Controls that run the shared "reset request" rule every frame: if the reset bit of the input word is
# set, or the boarder is on surface 0 (out of bounds), the forced-reset routine runs
# (auto_reset = not reset_request) -> STATIC + RESET_WAIT.
RESET_RULE_CONTROLS = {
    ControlState.BUMP, ControlState.CRUISE, ControlState.CHANGE_FAKIE, ControlState.FAKIE_CRUISE,
    ControlState.GATE_TO_BASE, ControlState.JUMP, ControlState.LAND_HARD, ControlState.LAND_NORMAL,
    ControlState.NATURAL_AIR, ControlState.PREWIND, ControlState.RAIL_SLIDE, ControlState.SPIN,
    ControlState.WIPE_OUT, ControlState.WIPE_OUT_RECOVER,
}

# Animation ids that gate ground transitions (channel 0), shared by B and D.
ANIM_RIDING_LOOP = 511        # 0x1FF
ANIM_FAKIE_LOOP = 514         # 0x202
ANIM_STAND_LOOP = 545         # 0x221 (also the skating push)
ANIM_GATE_PUSH = 509          # 0x1FD

LANDING_NOSE_LIMIT_DEG = -45.0   # pitch phi >= this is "nose ok"; phi < 0 means nose UP (tail first)
LANDING_HARD_VN = -2777.1052     # cm/s along the landing normal


@dataclass(frozen=True)
class Transition:
    tid: str
    context: str                     # state/routine where the call happens
    motion: Optional[MotionState]    # new motion (None = unchanged)
    control: Optional[ControlState]  # new control (None = unchanged)
    condition: str
    owner: str                       # A boarder_state, B ground_rail, C air_tricks, D collision_wipeout
    todo: bool = False


M, C = MotionState, ControlState

TRANSITIONS = [
    Transition("T01", "reset (slot 9)", M.STATIC, C.VOID,
               "boarder reset: placed at the race-path start shifted (size*90 - 90) cm along the path "
               "tangent, heading = tangent, velocity 0, then frozen", "A"),
    Transition("T02", "spawn routine", M.AIR, C.NATURAL_AIR,
               "spawn with drop height > 0 (respawn 101.46 cm above the path); drop animation 0x223", "A"),
    Transition("T03", "spawn routine", M.GROUND, C.CRUISE, "spawn with drop height <= 0: snap with the ground probe", "A"),
    Transition("T04", "spawn routine", M.STATIC, C.VOID, "initial placement flag set", "A"),
    Transition("T05", "forced reset routine", M.STATIC, C.RESET_WAIT,
               "not finished and the race path allows a reset here; triggered by RESET_RULE_CONTROLS "
               "(reset bit or surface 0), stuck_accum_a > 4.492, stuck_accum_b > 12.0, position.z below "
               "world floor - 10000, or a wipe-out timeout while racing (stuck_accum_a = 1e6)", "A"),
    Transition("T06", "RESET_WAIT update", None, None,
               "fade 0.5 s; after 0.8 s respawn at the path point (crowd avoidance in 200 cm steps), "
               "velocity = horizontal tangent * 833.33 cm/s, spawn routine with drop 101.46 cm -> T02", "A"),
    Transition("T07", "tutorial lesson start", M.GROUND, C.CRUISE,
               "all 360 .inp files show LESSON_WAIT at frame 0 and ground-layout words from frame 1; the "
               "code that leaves LESSON_WAIT is not exported", "A", True),
    Transition("T08", "lesson placement", None, C.LESSON_WAIT,
               "lesson code: SetControl(LessonWait) then the pre-race placement (reset, riding loop anim)", "A"),
    Transition("T10", "CRUISE", None, C.FINISH_LINE,
               "finish_result >= 0 (finish-line trigger 0xA with no laps left) and the push animation is not playing", "B"),
    Transition("T11", "CRUISE", None, C.PREWIND,
               "jump latch set (press edge; re-arms on release), prewind lock clear, brake == 0", "B"),
    Transition("T12", "CHANGE_FAKIE", None, C.FAKIE_CRUISE, "channel-0 animation reaches the fakie loop", "B"),
    Transition("T13", "CHANGE_FAKIE", None, C.CRUISE, "riding or push loop playing and jump not held", "B"),
    Transition("T14", "CHANGE_FAKIE", None, C.PREWIND, "riding or push loop playing and jump held", "B"),
    Transition("T15", "BUMP", None, C.PREWIND, "bump animation layer changed and jump held", "B"),
    Transition("T16", "BUMP", None, C.CRUISE, "bump animation layer changed", "B"),
    Transition("T17", "FAKIE_CRUISE", None, C.CRUISE,
               "riding or push loop playing again (turn-around started by |turn| > 0.5 or forward motion)", "B"),
    Transition("T18", "LAND_HARD", None, C.CRUISE, "riding loop playing, jump not held", "B"),
    Transition("T19", "LAND_HARD", None, C.PREWIND, "riding loop playing, jump held", "B"),
    Transition("T20", "LAND_HARD", None, C.FAKIE_CRUISE, "fakie loop playing", "B"),
    Transition("T21", "LAND_NORMAL", None, C.CRUISE,
               "crouch >= absorb, absorb = clamp(-0.00045 impact_vn, 0.25, 1); jump not held", "B"),
    Transition("T22", "LAND_NORMAL", None, C.PREWIND, "crouch >= absorb and jump held", "B"),
    Transition("T23", "PREWIND", None, C.CRUISE,
               "jump not held and both prewind charges already at rest (no jump): riding loop, back to Cruise", "B"),
    Transition("T24", "PREWIND", None, C.JUMP,
               "jump released: launch_charge = crouch, crouch target 0 at 13.33/s; prewind spin/flip < 0.2 "
               "dropped, snapped to pure spin (> 80 deg) or pure flip (< 10 deg)", "B"),
    Transition("T25", "JUMP", M.AIR, None,
               "crouch has unwound to 0: J = max(charge^2 (0.0985 + 0.787 S17) f_v(|v|), 630.88) along "
               "normalize(n + 0.2 f) (rail: along up)", "B"),
    Transition("T26", "after launch", None, C.SPIN,
               "take-off helper: seeds from the prewind filters (K_PRE G_spin prewind_a, 2/3 K_PRE G_spin "
               "prewind_b; x1.6 with launch_boost_timer, x0.8 fakie)", "C"),
    Transition("T27", "GROUND motion", None, C.CHANGE_FAKIE,
               "brake == 0 and v.contact_forward < -111.11 cm/s while in CRUISE, FAKIE_CRUISE or LAND_NORMAL", "B"),
    Transition("T28", "GROUND motion", M.AIR, C.NATURAL_AIR,
               "no wall hit this frame and (probe miss or ground_height > surface[6]); if control was JUMP "
               "it stays and enters SPIN with the partial charge; launch_charge = crouch", "B"),
    Transition("T29", "GROUND/AIR motion", M.RAIL_SLIDE, C.RAIL_SLIDE,
               "rail query hit, rail point not behind (d.v_hat >= -0.2), not in a blocking animation, inside "
               "0.9 x box (150r+80(1-r), 50, 50r+30(1-r)) around origin + 30 up", "B"),
    Transition("T30", "RAIL_SLIDE control", None, C.PREWIND, "jump latch set while grinding (ollie pops along up)", "B"),
    Transition("T31", "RAIL_SLIDE motion", M.AIR, C.SPIN,
               "rail query miss or rail point outside the box; falling off adds 277.78 cm/s away from the rail", "B"),
    Transition("T32", "rail exit helper", None, C.VOID, "rail ends while control is RAIL_SLIDE", "B"),
    Transition("T40", "SIT_AND_WAIT", M.WIPE_OUT_RECOVER, C.GETUP_FROM_SIT,
               "race phase == 4; sit-up animation 0x1FE", "D"),
    Transition("T41", "GETUP_FROM_SIT", M.GROUND, C.CRUISE,
               "sit-up animation done (or already riding/stand loop); velocity += 416.67 cm/s forward", "D"),
    Transition("T42", "GATE_LAUNCH", M.GROUND, None,
               "speed == 0 and gate value >= push animation phase: v = (0.4876 + 0.7181 S15) gate_push forward", "B"),
    Transition("T43", "GATE_LAUNCH", M.GROUND, C.GATE_TO_BASE, "gate value reaches 1 (animation 0x1FD)", "B"),
    Transition("T44", "GATE_TO_BASE", M.GROUND, C.CRUISE,
               "reset bit clear, surface != 0, push animation 0x1FD finished", "B"),
    Transition("T45", "FINISH_LINE", M.STATIC, None, "stopped after the finish; speed rule not decoded", "A", True),
    Transition("T46", "race handler enter (GO)", None, C.GATE_LAUNCH,
               "every racer still in GATE_ANTICIPATE switches to GATE_LAUNCH when the race handler starts", "A"),
    Transition("T50", "NATURAL_AIR", None, C.SPIN,
               "ground filters settled and (turn input or crouch < 0.5 or trick pressed); seeds zeroed", "C"),
    Transition("T51", "SPIN late trick", None, C.SPIN,
               "phase 2, no grab, |spin_rate| < 0.7 and |flip_rate| < 0.7, stick direction pressed: VOID then SPIN "
               "(Exit folds the angles, Enter re-seeds with K_LATE G_spin)", "C"),
    Transition("T52", "AIR touchdown", M.GROUND, C.LAND_NORMAL,
               "landing test passes and (landable grab at touchdown, or pitch phi >= -45 deg and n.v >= -2777.1); "
               "board_class 2 also needs regular stance and yaw <= 90 deg. phi = -atan2(F.n, U.n): phi < 0 is "
               "nose UP (tail first)", "C"),
    Transition("T53", "AIR touchdown", M.GROUND, C.LAND_HARD,
               "landing test passes but phi < -45 deg (nose up more than 45 deg, tail first) or n.v < -2777.1; "
               "board_class 2: also fakie stance or yaw > 90 deg (stance flips)", "C"),
    Transition("T54", "landing test", None, C.VOID,
               "touchdown on a surface other than 0/6/10 with a landable animation: SetControl(Void) folds the "
               "trick angles before the angles are measured", "C"),
    Transition("T60", "wipe-out entry", M.WIPE_OUT, C.WIPE_OUT,
               "any of: landing test fails (grab not landable, surface 0/6/10, scaled pitch outside "
               "[-2.9678, 2.6841]); air body contact moving into a surface with n.up <= 0.6 + 0.3 min(1, "
               "0.00045 |v_n|); grounded on surface 6/10 after the wall test; rider hit |J| > 733.97 "
               "(530.50 in game mode 0) or any rider hit while rail sliding; wall bounce > 833.33 (up.m) + "
               "1944.44 (wall impact handler)", "D"),
    Transition("T61", "WIPE_OUT tumble (airborne)", M.AIR, C.NATURAL_AIR,
               "air_time > 0.1, predicted air time > 0.1, landing surface not 6/10, stick centred, "
               "up.n_land > 0.9 and |w| < 4 rad/s", "D"),
    Transition("T62", "WIPE_OUT tumble (grounded)", M.GROUND, C.CRUISE,
               "total > 0.5 s, phase > 0.1 s, surface not 6/10, |w| < 7, n_z > 0.349, |look-ahead.forward| > 0.8, "
               "up.n > 0.85, probe hits a non-bail surface", "D"),
    Transition("T63", "WIPE_OUT slide / timeout", M.WIPE_OUT_RECOVER, C.WIPE_OUT_RECOVER,
               "lying flat (|n x forward| < 0.1, |forward.n| > 0.9) and |v| < min(833.33 + 694.44 t_phase, 2222.22) "
               "with the probe hitting; or tumble timeout (3 s phase / 7 s total) after the finish", "D"),
    Transition("T64", "WIPE_OUT_RECOVER control", M.GROUND, C.CRUISE, "riding loop 0x1FF or stand loop 0x221 reached", "D"),
    Transition("T65", "WIPE_OUT_RECOVER control", M.GROUND, C.FAKIE_CRUISE, "fakie loop 0x202 reached", "D"),
    Transition("T66", "rider collision", M.AIR, C.NATURAL_AIR,
               "grounded target, |J| > 1000, heading within 45 deg of the stored rival heading, impulse away "
               "from the ground: knock-up launch with launch_charge = 1", "D"),
    Transition("T67", "rider collision", None, C.BUMP,
               "167.32 < |J| <= knock-down threshold, target in CRUISE or PREWIND, not rail sliding; boost -0.02", "D"),
]


def apply(motion: MotionState, control: ControlState, t: Transition):
    """Return the (motion, control) pair after taking transition t."""
    return (t.motion if t.motion is not None else motion,
            t.control if t.control is not None else control)


def reset_rule(control: ControlState, reset_request: bool, surface_id: int, finished: bool):
    """Shared per-frame rule: returns (fires, auto_reset_flag)."""
    if finished or control not in RESET_RULE_CONTROLS:
        return False, False
    if reset_request or surface_id == 0:
        return True, not reset_request
    return False, False


def landing_pitch_class(phi_rad: float, impact_vn: float) -> str:
    """Board classes 0/1 classifier with the sign convention settled during the RE."""
    nose_ok = phi_rad >= LANDING_NOSE_LIMIT_DEG * 3.141592653589793 / 180.0
    return "LAND_NORMAL" if (nose_ok and impact_vn >= LANDING_HARD_VN) else "LAND_HARD"


if __name__ == "__main__":
    todo = sum(t.todo for t in TRANSITIONS)
    print(f"{len(TRANSITIONS)} transitions ({todo} still open)")
    for t in TRANSITIONS:
        m = t.motion.name if t.motion is not None else "-"
        c = t.control.name if t.control is not None else "-"
        print(f"{t.tid} [{t.owner}{'?' if t.todo else ''}] {t.context:28s} -> motion {m:17s} control {c:15s} {t.condition[:70]}")
