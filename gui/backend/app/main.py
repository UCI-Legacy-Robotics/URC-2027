import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from typing import Any

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

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


app = FastAPI(title="Legacy Robotics base station", lifespan=lifespan)
# The UI is loaded from Electron's file:// or the dev server, not from this origin.
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["GET"])


@app.get("/api/health")
def health() -> dict[str, bool]:
    return {"mediamtx": app.state.video.running}


@app.get("/api/video/stats/{path:path}")
async def video_stats(path: str) -> dict[str, Any]:
    return await app.state.video.path_stats(path)
