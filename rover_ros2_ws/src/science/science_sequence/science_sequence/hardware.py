"""Shared command interface, hardware replies, and cancellation handling."""

from dataclasses import dataclass
from threading import Event, Lock
from typing import Callable, Protocol


class HardwareError(RuntimeError):
    """A hardware command failed and is no longer running."""


class HardwareUnsafe(HardwareError):
    """We cannot confirm the hardware stopped. Do not start a recovery movement."""


class SequenceCancelled(Exception):
    """The operator requested cancellation."""


class Cancellation:
    """Accept cancellation until the operation has finished, without a timing race."""

    def __init__(self):
        self._event = Event()
        self._lock = Lock()
        self._closed = False

    def request(self) -> bool:
        with self._lock:
            if self._closed:
                return False
            self._event.set()
            return True

    def __call__(self) -> bool:
        return self._event.is_set()

    def finish(self):
        with self._lock:
            if self._event.is_set():
                raise SequenceCancelled()
            self._closed = True

    def close(self):
        with self._lock:
            self._closed = True


@dataclass(frozen=True)
class Reply:
    actual_value: float = 0.0
    readings: tuple[float, ...] = ()
    units: tuple[str, ...] = ()


class Hardware(Protocol):
    def execute(
        self, command: str, value: float, cancelled: Callable[[], bool], timeout: float
    ) -> Reply:
        """Wait for the command to finish. If cancelled, stop it before returning."""
        ...

    def stop(self, timeout: float):
        """Stop all activity and confirm it stopped. Keep platforms supported."""
        ...
