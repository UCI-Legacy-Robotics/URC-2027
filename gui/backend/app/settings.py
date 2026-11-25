import re
from pathlib import Path

from pydantic import field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

BACKEND_DIR = Path(__file__).resolve().parent.parent

_HOST = re.compile(r"^[A-Za-z0-9.-]+$")


class Settings(BaseSettings):
    """Backend settings, read from `BASE_*` environment variables or `gui/backend/.env`."""

    model_config = SettingsConfigDict(env_prefix="BASE_", env_file=BACKEND_DIR / ".env")

    rover_host: str = "192.168.1.67"
    """The rover's IP or hostname on the 5 GHz video link."""
    rover_rtsp_port: int = 8554
    rover_stream_paths: list[str] = ["rover/front", "rover/arm"]
    """The rover's camera streams. The backend probes each to judge the 5 GHz link and
    which cameras are streaming. Keep in step with the paths in mediamtx/mediamtx.yml."""

    mediamtx_bin: Path = BACKEND_DIR / "bin" / "mediamtx"
    mediamtx_config: Path = BACKEND_DIR / "mediamtx" / "mediamtx.yml"
    mediamtx_restart_delay_s: float = 2.0

    @field_validator("rover_host")
    @classmethod
    def _plain_host(cls, value: str) -> str:
        # Substituted into MediaMTX's YAML config, so nothing but a bare host.
        if not _HOST.match(value):
            raise ValueError(f"not an IP or hostname: {value!r}")
        return value
