import asyncio
import contextlib
import logging
import time
from typing import Any, Literal

log = logging.getLogger(__name__)

RoverState = Literal["unknown", "ok", "no-stream", "unreachable"]


async def probe_rtsp(
    host: str, port: int, path: str, timeout_s: float
) -> tuple[RoverState, str | None]:
    """Asks the rover's MediaMTX whether `path` has a stream, with an RTSP DESCRIBE.

    `ok` and `no-stream` both mean the rover answered over the 5 GHz link.
    """
    url = f"rtsp://{host}:{port}/{path}"
    try:
        async with asyncio.timeout(timeout_s):
            reader, writer = await asyncio.open_connection(host, port)
            try:
                writer.write(f"DESCRIBE {url} RTSP/1.0\r\nCSeq: 1\r\n\r\n".encode())
                await writer.drain()
                status_line = (await reader.readline()).decode(errors="replace").strip()
            finally:
                writer.close()
                with contextlib.suppress(OSError):
                    await writer.wait_closed()
    except TimeoutError:
        return "unreachable", f"no answer from {host}:{port}"
    except OSError as err:
        return "unreachable", err.strerror or str(err)

    parts = status_line.split(" ", 2)
    code = parts[1] if len(parts) > 1 else ""
    if code == "200":
        return "ok", None
    if code == "404":
        return "no-stream", f"no stream at {path} on the rover"
    return "ok", f"rover answered {status_line!r}"


class RoverLink:
    """Probes the rover's MediaMTX every few seconds, whether or not anyone is watching."""

    def __init__(
        self,
        host: str,
        port: int,
        path: str,
        interval_s: float = 2.0,
        timeout_s: float = 2.0,
    ) -> None:
        self._host = host
        self._port = port
        self._path = path
        self._interval_s = interval_s
        self._timeout_s = timeout_s
        self._state: RoverState = "unknown"
        self._detail: str | None = None
        self._last_answer: float | None = None
        self._task: asyncio.Task[None] | None = None

    def start(self) -> None:
        self._task = asyncio.create_task(self._run())

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._task

    def status(self) -> dict[str, Any]:
        since = None
        if self._last_answer is not None:
            since = round((time.monotonic() - self._last_answer) * 1000)
        return {
            "state": self._state,
            "detail": self._detail,
            "roverHost": self._host,
            "lastSeenMs": since,
        }

    async def _run(self) -> None:
        while True:
            state, detail = await probe_rtsp(
                self._host, self._port, self._path, self._timeout_s
            )
            if state != self._state:
                log.info("rover link %s%s", state, f" ({detail})" if detail else "")
            self._state, self._detail = state, detail
            if state != "unreachable":
                self._last_answer = time.monotonic()
            await asyncio.sleep(self._interval_s)
