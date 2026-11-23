"""Run the Bradford and NPK steps, check their conditions, and handle recovery."""

from dataclasses import dataclass, replace
from math import isfinite
from threading import Event, Lock
from time import monotonic
from typing import Callable

from .hardware import (
    AugerJam,
    Cancellation,
    Hardware,
    HardwareError,
    HardwareUnsafe,
    SequenceCancelled,
)


@dataclass(frozen=True)
class Config:
    # Example settings. Use the measured travel, timings, and tolerances in the ROS parameters.
    near_ground_steps: int = 1000
    ten_cm_steps: int = 2000
    max_depth_steps: int = 3000
    npk_lower_steps: int = 1000
    blender_seconds: float = 5.0
    pump_seconds: float = 1.0
    command_timeout: float = 30.0
    recovery_timeout: float = 10.0
    position_tolerance: float = 0.5

    def __post_init__(self):
        counts = (
            self.near_ground_steps,
            self.ten_cm_steps,
            self.max_depth_steps,
            self.npk_lower_steps,
        )
        if any(type(value) is not int for value in counts):
            raise ValueError("Step counts must be integers")
        if not 0 < self.near_ground_steps < self.ten_cm_steps < self.max_depth_steps:
            raise ValueError("Platform targets must increase from base to near ground, 10 cm, and maximum depth")
        if self.npk_lower_steps <= 0:
            raise ValueError("NPK lowering travel must be greater than zero")
        times = (
            self.blender_seconds,
            self.pump_seconds,
            self.command_timeout,
            self.recovery_timeout,
            self.position_tolerance,
        )
        if any(not isfinite(value) or value <= 0 for value in times):
            raise ValueError(
                "Durations and position tolerance must be finite and positive"
            )
        if self.position_tolerance >= min(
            self.near_ground_steps,
            self.ten_cm_steps - self.near_ground_steps,
            self.max_depth_steps - self.ten_cm_steps,
        ):
            raise ValueError("Position tolerance is too large to tell the platform targets apart")
        if self.pump_seconds >= self.command_timeout:
            raise ValueError("The command timeout must be longer than the pumping time")


@dataclass(frozen=True)
class State:
    phase: str = "UNHOMED"
    platform_position: str = "UNKNOWN"
    npk_position: str = "UNKNOWN"
    soil_collected: bool = False
    bradford_retracted: bool = False
    ready_to_drive: bool = False
    fault: bool = False
    cuvette_index: int = -1
    detail: str = "Run home to establish the starting positions before beginning science"


@dataclass(frozen=True)
class Result:
    success: bool
    cancelled: bool
    message: str
    state: State
    names: tuple[str, ...] = ()
    values: tuple[float, ...] = ()
    units: tuple[str, ...] = ()


class ScienceController:
    BRADFORD_ENTRY_POSITIONS = {
        "approach_bradford": {"BASE", "NEAR_GROUND"},
        "drill_bradford": {"TEN_CM"},
        "finish_bradford": {"MAX_DEPTH"},
    }
    BRADFORD_STEPS = frozenset(BRADFORD_ENTRY_POSITIONS)
    COMMANDS = frozenset(
        {
            "home",
            "lower_platform",
            "collect_bradford",
            "retract_bradford",
            "blend_and_measure",
            "next_cuvette",
            "measure_npk",
            "retract_npk",
            "ready_to_drive",
        }
    ) | BRADFORD_STEPS

    def __init__(self, hardware: Hardware, config: Config | None = None):
        self.hardware = hardware
        self.config = config or Config()
        self._state = State()
        self._state_lock = Lock()
        self._operation_lock = Lock()
        self._stopped = Event()
        self._stop_lock = Lock()
        self._feedback: Callable[[State], None] = lambda state: None
        self._cancel = Cancellation()
        self._blended = False
        self._cuvette_used = False
        self._hatch_may_be_closed = True

    @property
    def state(self):
        with self._state_lock:
            return replace(self._state)

    def _update(self, **changes):
        with self._state_lock:
            if self._stopped.is_set():
                return
            self._state = replace(self._state, **changes)
            snapshot = self._state
        self._feedback(snapshot)

    def emergency_stop(self):
        """Latch the sequencer and ask the driver to stop without starting recovery."""
        with self._stop_lock:
            with self._state_lock:
                self._stopped.set()
                self._state = State(
                    phase="ESTOP", fault=True, detail="Waiting for driver stop confirmation"
                )
            try:
                self.hardware.stop(self.config.recovery_timeout)
            except Exception as error:
                success = False
                detail = f"Emergency stop requested; driver stop unconfirmed: {error}"
            else:
                success = True
                detail = "Driver confirmed stop; sequencer is latched"
            with self._state_lock:
                self._state = replace(self._state, detail=detail)
            return success, detail

    def rejection_reason(self, command: str) -> str:
        s = self.state
        if command not in self.COMMANDS:
            return f"Unknown command: {command}"
        if self._stopped.is_set():
            return "Emergency stop is latched"
        if command == "home":
            return ""
        if s.fault:
            return "Recovery failed. Check the hardware and confirm its positions with home before continuing"
        if command == "lower_platform":
            return (
                ""
                if (
                    s.platform_position == "BASE"
                    and s.npk_position == "HOME"
                    and not s.soil_collected
                )
                else (
                    "Start a fresh cycle with home, with the bottom platform at base and NPK raised"
                )
            )
        if command == "collect_bradford":
            return (
                ""
                if (
                    s.platform_position == "NEAR_GROUND"
                    and s.npk_position == "HOME"
                    and not s.soil_collected
                )
                else (
                    "Collection needs the platform near ground, NPK raised, and no completed sample"
                )
            )
        if command in self.BRADFORD_STEPS:
            if (
                s.platform_position in self.BRADFORD_ENTRY_POSITIONS[command]
                and s.npk_position == "HOME"
                and not s.soil_collected
            ):
                return ""
            return f"{command} requires the preceding Bradford checkpoint and NPK raised"
        if command == "retract_bradford":
            return (
                ""
                if (
                    s.platform_position in {"NEAR_GROUND", "TEN_CM", "MAX_DEPTH"}
                    and s.npk_position == "HOME"
                )
                else (
                    "To retract the drill, its lowered position must be known and NPK must be raised"
                )
            )
        if command == "measure_npk":
            return (
                ""
                if s.platform_position == "NEAR_GROUND" and s.npk_position == "HOME"
                else ("To measure NPK, the bottom platform must be near ground and NPK must start raised")
            )
        if command == "retract_npk":
            return (
                ""
                if s.platform_position == "NEAR_GROUND"
                else ("Keep the bottom platform near ground while raising NPK")
            )
        if command == "blend_and_measure":
            return (
                ""
                if (
                    s.soil_collected
                    and not self._blended
                    and not self._cuvette_used
                    and s.cuvette_index == 0
                )
                else (
                    "Mixing needs completed soil collection and an unused first cuvette aligned during initialization"
                )
            )
        if command == "next_cuvette":
            return (
                ""
                if (
                    s.soil_collected
                    and self._blended
                    and self._cuvette_used
                    and 0 <= s.cuvette_index < 3
                )
                else (
                    "Mix the sample and use the current cuvette first. No unused cuvette is available after slot 3"
                )
            )
        if command == "ready_to_drive":
            return (
                ""
                if (
                    s.soil_collected
                    and s.bradford_retracted
                    and s.platform_position in {"NEAR_GROUND", "BASE"}
                    and s.npk_position == "HOME"
                )
                else (
                    "Finish collection, retract Bradford to near ground, and raise NPK before preparing to drive"
                )
            )
        raise AssertionError(command)

    def run(self, command, cancellation=None, feedback=None) -> Result:
        if not self._operation_lock.acquire(blocking=False):
            return Result(
                False, False, "Another science operation is still running", self.state
            )
        self._cancel = cancellation or Cancellation()
        self._feedback = feedback or (lambda state: None)
        start_position = self.state.platform_position
        try:
            reason = self.rejection_reason(command)
            if reason:
                return Result(False, False, reason, self.state)
            self._check_cancel()
            self._update(phase=command.upper(), ready_to_drive=False, detail="Starting")
            names, values, units = getattr(self, "_" + command)()
            self._cancel.finish()
            # Only report ready after finishing, when cancellation can no longer be accepted.
            self._update(
                phase="COMPLETE",
                ready_to_drive=command == "ready_to_drive",
                detail=f"{command} completed",
            )
            completed = self.state
            if completed.phase == "ESTOP":
                return Result(False, False, completed.detail, completed)
            return Result(True, False, f"{command} completed", completed, names, values, units)
        except (HardwareError, SequenceCancelled) as error:
            if self._stopped.is_set():
                return Result(False, False, self.state.detail, self.state)
            cancelled = isinstance(error, SequenceCancelled)
            message = "Cancelled by operator" if cancelled else str(error)
            self._recover(command, message, failure=error, start_position=start_position)
            return Result(False, cancelled, self.state.detail, self.state)
        except Exception as error:
            # Try to stop the hardware even if an unexpected software error occurs.
            self._recover(
                command, f"Unexpected failure: {error}", failure=HardwareUnsafe(str(error))
            )
            return Result(False, False, self.state.detail, self.state)
        finally:
            self._cancel.close()
            self._feedback = lambda state: None
            self._operation_lock.release()

    def _check_cancel(self):
        if self._stopped.is_set() or self._cancel():
            raise SequenceCancelled()

    def _command(self, command, value=0.0, *, recovery=False):
        def cancelled():
            return self._stopped.is_set() or (not recovery and self._cancel())

        if cancelled():
            raise SequenceCancelled()
        if command == "hatch" and value == 0:
            self._hatch_may_be_closed = True
        self._update(detail=f"Requesting {command}({value})")
        reply = self.hardware.execute(
            command,
            float(value),
            cancelled,
            self.config.recovery_timeout if recovery else self.config.command_timeout,
        )
        # Check that the driver actually reached the requested position or state.
        if command not in {"npk_read", "spectrometer_read", "pump"}:
            tolerance = (
                self.config.position_tolerance
                if command in {"platform_move", "npk_move", "platform_home", "npk_home"}
                else 0.01
            )
            if (
                not isfinite(reply.actual_value)
                or abs(reply.actual_value - value) > tolerance
            ):
                raise HardwareError(f"{command}: target was not confirmed")
        if command == "hatch" and value == 1:
            self._hatch_may_be_closed = False
        return reply

    def _platform(self, position, target, *, recovery=False):
        self._update(platform_position="MOVING", ready_to_drive=False)
        self._command(
            "platform_home" if position == "BASE" else "platform_move",
            target,
            recovery=recovery,
        )
        self._update(platform_position=position)

    def _npk_home(self, *, recovery=False):
        self._update(npk_position="MOVING")
        self._command("npk_home", recovery=recovery)
        self._update(npk_position="HOME")

    def _home(self):
        self.hardware.stop(self.config.recovery_timeout)
        self._npk_home()
        self._platform("BASE", 0)
        self._command("cache", 0)
        self._command("hatch", 1)
        self._command("blender_lid", 0)
        self._update(cuvette_index=-1)
        self._command("carousel_home")
        self._blended = False
        self._cuvette_used = False
        self._update(
            soil_collected=False, bradford_retracted=False, fault=False, cuvette_index=0
        )
        return (), (), ()

    def _lower_platform(self):
        self._blended = False
        self._cuvette_used = False
        self._update(soil_collected=False, bradford_retracted=False)
        self._platform("NEAR_GROUND", self.config.near_ground_steps)
        return (), (), ()

    def _collect_bradford(self):
        self._approach_bradford(pause=False)
        self._drill_bradford(pause=False)
        return self._finish_bradford()

    def _approach_bradford(self, *, pause=True):
        self._update(bradford_retracted=False)
        self._command("auger", 1)
        self._platform("TEN_CM", self.config.ten_cm_steps)
        if pause:
            self._command("auger", 0)
        return (), (), ()

    def _drill_bradford(self, *, pause=True):
        self._command("hatch", 0)
        self._command("cache", 1)
        self._command("blender_lid", 1)
        if pause:
            self._command("auger", 1)
        self._platform("MAX_DEPTH", self.config.max_depth_steps)
        if pause:
            self._command("auger", 0)
        return (), (), ()

    def _finish_bradford(self):
        self._command("auger", 0)
        self._command("hatch", 1)
        self._update(
            phase="COLLECTION_COMPLETE",
            soil_collected=True,
            detail="Reached maximum depth; soil collection is complete",
        )
        self._check_cancel()
        self._update(phase="COLLECTION_ACKNOWLEDGED", detail="Collection acknowledged")
        return (), (), ()

    def _retract_bradford(self):
        # Operator troubleshooting: stop first, then request the return motion.
        self.hardware.stop(self.config.recovery_timeout)
        self._command("cache", 0)
        self._platform("NEAR_GROUND", self.config.near_ground_steps)
        self._command("hatch", 1)
        self._update(bradford_retracted=self.state.soil_collected)
        return (), (), ()

    def _wait(self, seconds):
        deadline = monotonic() + seconds
        while monotonic() < deadline:
            self._check_cancel()
            Event().wait(min(0.02, max(0, deadline - monotonic())))

    def _blend_and_measure(self):
        self._command("blender_lid", 0)
        self._command("blender", 1)
        self._update(
            phase="MIXING", detail=f"Mixing for {self.config.blender_seconds} seconds"
        )
        self._wait(self.config.blender_seconds)
        self._command("blender", 0)
        self._blended = True
        return self._pump_and_measure()

    def _next_cuvette(self):
        next_index = self.state.cuvette_index + 1
        self._update(cuvette_index=-1)
        self._command("carousel_move", next_index * 90)
        self._update(cuvette_index=next_index)
        self._cuvette_used = False
        return self._pump_and_measure()

    def _read(self, command, names):
        reply = self._command(command)
        if (
            len(reply.readings) != len(names)
            or len(reply.units) != len(names)
            or any(not isfinite(value) for value in reply.readings)
            or any(not unit.strip() for unit in reply.units)
        ):
            raise HardwareError(f"Invalid readings or missing units from {command}")
        return names, reply.readings, reply.units

    def _pump_and_measure(self):
        self._cuvette_used = True
        self._update(phase="PUMPING")
        self._command("pump", self.config.pump_seconds)
        self._update(phase="MEASURING")
        return self._read("spectrometer_read", ("absorbance",))

    def _measure_npk(self):
        self._update(npk_position="MOVING")
        self._command("npk_move", self.config.npk_lower_steps)
        self._update(npk_position="LOWERED")
        return self._read("npk_read", ("nitrogen", "phosphorus", "potassium"))

    def _retract_npk(self):
        self._npk_home()
        return (), (), ()

    def _ready_to_drive(self):
        self._command("cache", 0)
        self._command("blender_lid", 0)
        self.hardware.stop(self.config.recovery_timeout)
        self._platform("BASE", 0)
        return (), (), ()

    def _recover(self, command, message, *, failure, start_position=None):
        if self._stopped.is_set():
            return
        self._update(phase="RECOVERING", ready_to_drive=False, detail=message)
        try:
            self.hardware.stop(self.config.recovery_timeout)
            if isinstance(failure, HardwareUnsafe):
                raise HardwareUnsafe(
                    "Cannot confirm what the hardware is doing. Further movement is blocked"
                )
            if isinstance(failure, AugerJam):
                # self._command("auger", -1, recovery=True)  # Reverse pending mechanical approval.
                if self._hatch_may_be_closed:
                    self._command("hatch", 1, recovery=True)
                if not failure.elevator_position_valid:
                    raise HardwareUnsafe("The driver did not confirm elevator position after the jam")
            elif command in self.BRADFORD_STEPS and not isinstance(failure, SequenceCancelled):
                raise HardwareError("Bradford stopped. Check the driver fault before moving the drill")

            back_to_base = command == "lower_platform" or (
                command == "approach_bradford" and start_position == "BASE"
            )
            if back_to_base:
                self._platform("BASE", 0, recovery=True)
            elif command in self.BRADFORD_STEPS or command in {
                "collect_bradford", "retract_bradford", "ready_to_drive",
            }:
                if command in self.BRADFORD_STEPS or command == "collect_bradford":
                    self._update(soil_collected=False, bradford_retracted=False)
                self._command("cache", 0, recovery=True)
                self._platform(
                    "NEAR_GROUND", self.config.near_ground_steps, recovery=True
                )
                if not isinstance(failure, AugerJam):
                    self._command("hatch", 1, recovery=True)
                self._update(bradford_retracted=self.state.soil_collected)
            elif command in {"measure_npk", "retract_npk"}:
                self._npk_home(recovery=True)
            elif command == "home":
                raise HardwareError(
                    "Homing did not finish. Run home again after checking the hardware"
                )
            self._update(phase="RECOVERED", detail=f"{message}; recovery finished successfully")
        except Exception as recovery_error:
            if self._stopped.is_set():
                return
            try:
                self.hardware.stop(self.config.recovery_timeout)
            except Exception:
                pass
            self._update(
                phase="FAULT",
                fault=True,
                platform_position="UNKNOWN",
                npk_position="UNKNOWN",
                soil_collected=False,
                bradford_retracted=False,
                detail=f"{message}; could not finish recovery: {recovery_error}",
            )
