import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from fastapi import FastAPI

from app.mediamtx import MediaMtx
from app.settings import Settings

logging.basicConfig(
    level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s"
)


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncIterator[None]:
    settings = Settings()
    video = MediaMtx(
        settings.mediamtx_bin,
        settings.mediamtx_config,
        settings.rover_host,
        settings.mediamtx_restart_delay_s,
    )
    video.start()
    app.state.video = video
    yield
    await video.stop()


app = FastAPI(title="URC-2027 base station", lifespan=lifespan)


@app.get("/api/health")
def health() -> dict[str, bool]:
    return {"mediamtx": app.state.video.running}
