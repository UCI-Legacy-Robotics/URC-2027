import asyncio
import contextlib
import json
import logging
import re
import tempfile
import urllib.request
from collections import Counter
from pathlib import Path
from typing import Any

log = logging.getLogger(__name__)

# MediaMTX doesn't expose a pulled stream's packet loss in its API, only in its log.
_RTP_LOST = re.compile(r"\[path (\S+)\] \[RTSP source\] (\d+) RTP packets? lost")


def render_config(template: str, rover_host: str) -> str:
    return template.replace("ROVER_HOST", rover_host)


class MediaMtx:
    """Runs the base station MediaMTX as a child process and restarts it if it exits."""

    def __init__(
        self,
        binary: Path,
        config_template: Path,
        rover_host: str,
        restart_delay_s: float,
        api_url: str = "http://127.0.0.1:19997",
    ) -> None:
        self._binary = binary
        self._template = config_template
        self._rover_host = rover_host
        self._restart_delay_s = restart_delay_s
        self._api_url = api_url
        self._rtp_packets_lost: Counter[str] = Counter()
        self._process: asyncio.subprocess.Process | None = None
        self._task: asyncio.Task[None] | None = None
        self._dir = tempfile.TemporaryDirectory(prefix="mediamtx-")

    @property
    def running(self) -> bool:
        return self._process is not None and self._process.returncode is None

    def record_log_line(self, line: str) -> None:
        if match := _RTP_LOST.search(line):
            self._rtp_packets_lost[match[1]] += int(match[2])

    async def path_stats(self, path: str) -> dict[str, Any]:
        """Running totals for one path, since the backend started."""
        stats: dict[str, Any] = {"rtpPacketsLost": self._rtp_packets_lost[path]}
        try:
            info = await asyncio.to_thread(self._api_get, f"/v3/paths/get/{path}")
        except OSError:
            return {**stats, "ready": False}
        return {
            **stats,
            "ready": info.get("ready", False),
            "inboundBytes": info.get("inboundBytes", 0),
            "inboundFramesInError": info.get("inboundFramesInError", 0),
        }

    def _api_get(self, route: str) -> dict[str, Any]:
        with urllib.request.urlopen(self._api_url + route, timeout=2) as response:
            return json.load(response)

    def start(self) -> None:
        if not self._binary.is_file():
            log.error(
                "MediaMTX not found at %s; video is off. Run scripts/install_mediamtx.sh",
                self._binary,
            )
            return
        self._task = asyncio.create_task(self._supervise())

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._task
        self._dir.cleanup()

    async def _supervise(self) -> None:
        config = Path(self._dir.name) / "mediamtx.yml"
        config.write_text(render_config(self._template.read_text(), self._rover_host))
        while True:
            try:
                await self._run_once(config)
            except asyncio.CancelledError:
                await self._terminate()
                raise
            log.warning("MediaMTX exited; restarting in %.1f s", self._restart_delay_s)
            await asyncio.sleep(self._restart_delay_s)

    async def _run_once(self, config: Path) -> None:
        self._process = await asyncio.create_subprocess_exec(
            str(self._binary),
            str(config),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
        )
        log.info(
            "MediaMTX started (pid %d), pulling from %s",
            self._process.pid,
            self._rover_host,
        )
        assert self._process.stdout
        async for raw in self._process.stdout:
            line = raw.decode(errors="replace").rstrip()
            self.record_log_line(line)
            log.info("mediamtx: %s", line)
        await self._process.wait()

    async def _terminate(self) -> None:
        process = self._process
        if process is None or process.returncode is not None:
            return
        process.terminate()
        try:
            await asyncio.wait_for(process.wait(), timeout=5)
        except TimeoutError:
            process.kill()
            await process.wait()
