import asyncio
import contextlib
import logging
import tempfile
from pathlib import Path

log = logging.getLogger(__name__)


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
    ) -> None:
        self._binary = binary
        self._template = config_template
        self._rover_host = rover_host
        self._restart_delay_s = restart_delay_s
        self._process: asyncio.subprocess.Process | None = None
        self._task: asyncio.Task[None] | None = None
        self._dir = tempfile.TemporaryDirectory(prefix="mediamtx-")

    @property
    def running(self) -> bool:
        return self._process is not None and self._process.returncode is None

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
        async for line in self._process.stdout:
            log.info("mediamtx: %s", line.decode(errors="replace").rstrip())
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
