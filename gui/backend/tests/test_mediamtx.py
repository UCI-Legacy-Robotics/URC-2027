import asyncio
from collections.abc import Callable
from pathlib import Path

import pytest

from app.mediamtx import MediaMtx, render_config
from app.settings import BACKEND_DIR, Settings


def test_config_pulls_rover_front_over_udp() -> None:
    template = (BACKEND_DIR / "mediamtx" / "mediamtx.yml").read_text()
    config = render_config(template, "10.0.0.5")
    assert "source: rtsp://10.0.0.5:8554/rover/front" in config
    assert "rtspTransport: udp" in config
    assert "ROVER_HOST" not in config


@pytest.mark.parametrize("host", ["1.2.3.4\nevil: yes", "host:8554", "a b"])
def test_rover_host_rejects_anything_but_a_host(host: str) -> None:
    with pytest.raises(ValueError):
        Settings(rover_host=host)


def _fake_mediamtx(tmp_path: Path, body: str) -> Path:
    script = tmp_path / "mediamtx"
    script.write_text(f"#!/bin/sh\n{body}\n")
    script.chmod(0o755)
    return script


async def _wait_until(condition: Callable[[], bool], timeout: float = 2.0) -> None:
    async with asyncio.timeout(timeout):
        while not condition():
            await asyncio.sleep(0.01)


@pytest.mark.asyncio
async def test_restarts_after_exit_and_stops_cleanly(tmp_path: Path) -> None:
    starts = tmp_path / "starts"
    binary = _fake_mediamtx(
        tmp_path, f'echo x >> "{starts}"\ngrep -q 10.0.0.5 "$1" || exit 2'
    )
    template = tmp_path / "template.yml"
    template.write_text("source: rtsp://ROVER_HOST:8554/rover/front\n")

    video = MediaMtx(binary, template, "10.0.0.5", restart_delay_s=0.05)
    video.start()
    await _wait_until(lambda: starts.exists() and len(starts.read_text().split()) >= 2)
    await video.stop()
    assert not video.running


@pytest.mark.asyncio
async def test_stop_terminates_a_running_process(tmp_path: Path) -> None:
    binary = _fake_mediamtx(tmp_path, "exec sleep 30")
    template = tmp_path / "template.yml"
    template.write_text("")

    video = MediaMtx(binary, template, "10.0.0.5", restart_delay_s=0.05)
    video.start()
    await _wait_until(lambda: video.running)
    await video.stop()
    assert not video.running


@pytest.mark.asyncio
async def test_missing_binary_leaves_video_off(tmp_path: Path) -> None:
    video = MediaMtx(
        tmp_path / "nope", tmp_path / "t.yml", "10.0.0.5", restart_delay_s=0.05
    )
    video.start()
    assert not video.running
    await video.stop()


def test_counts_rtp_packets_lost_from_the_log(tmp_path: Path) -> None:
    video = MediaMtx(tmp_path / "m", tmp_path / "t.yml", "10.0.0.5", restart_delay_s=1)
    for line in [
        "2026/11/24 15:49:44 WAR [path rover/front] [RTSP source] 1 RTP packet lost",
        "2026/11/24 15:49:45 WAR [path rover/front] [RTSP source] 27 RTP packets lost",
        "2026/11/24 15:49:46 WAR [path rover/back] [RTSP source] 5 RTP packets lost",
        "2026/11/24 15:49:47 WAR [path rover/front] processing error: invalid FU-A packet",
    ]:
        video.record_log_line(line)
    assert video._rtp_packets_lost == {"rover/front": 28, "rover/back": 5}
