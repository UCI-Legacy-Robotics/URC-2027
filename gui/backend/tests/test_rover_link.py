import asyncio

import pytest

from app.rover_link import RoverLink, probe_rtsp


async def _rtsp_server(reply: bytes | None) -> asyncio.Server:
    async def handle(
        reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        await reader.readuntil(b"\r\n\r\n")
        if reply is None:
            await asyncio.sleep(10)
        else:
            writer.write(reply)
            await writer.drain()
        writer.close()

    return await asyncio.start_server(handle, "127.0.0.1", 0)


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("reply", "state"),
    [
        (b"RTSP/1.0 200 OK\r\nCSeq: 1\r\n\r\n", "ok"),
        (b"RTSP/1.0 404 Not Found\r\nCSeq: 1\r\n\r\n", "no-stream"),
        (None, "unreachable"),
    ],
)
async def test_probe_classifies_the_rover(reply: bytes | None, state: str) -> None:
    server = await _rtsp_server(reply)
    port = server.sockets[0].getsockname()[1]
    try:
        result, _detail = await probe_rtsp("127.0.0.1", port, "rover/front", 0.3)
    finally:
        server.close()
    assert result == state


@pytest.mark.asyncio
async def test_probe_reports_a_closed_port_as_unreachable() -> None:
    server = await asyncio.start_server(lambda r, w: None, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    server.close()
    await server.wait_closed()
    result, detail = await probe_rtsp("127.0.0.1", port, "rover/front", 0.3)
    assert result == "unreachable"
    assert detail


async def _rtsp_server_streaming(paths: set[str]) -> asyncio.Server:
    """Answers DESCRIBE with 200 for `paths` and 404 for anything else."""

    async def handle(
        reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        request = (await reader.readuntil(b"\r\n\r\n")).decode()
        url = request.split(" ", 2)[1]
        found = any(url.endswith("/" + path) for path in paths)
        status = "200 OK" if found else "404 Not Found"
        writer.write(f"RTSP/1.0 {status}\r\nCSeq: 1\r\n\r\n".encode())
        await writer.drain()
        writer.close()

    return await asyncio.start_server(handle, "127.0.0.1", 0)


@pytest.mark.asyncio
async def test_link_reports_each_camera_stream() -> None:
    server = await _rtsp_server_streaming({"rover/arm"})
    port = server.sockets[0].getsockname()[1]
    link = RoverLink("127.0.0.1", port, ["rover/front", "rover/arm"], timeout_s=0.3)
    try:
        await link.probe_once()
    finally:
        server.close()
    status = link.status()
    assert status["state"] == "ok"
    assert status["streams"] == {"rover/front": False, "rover/arm": True}
    assert status["lastSeenMs"] is not None


@pytest.mark.asyncio
async def test_link_without_an_answer_is_unreachable_with_no_streams() -> None:
    server = await asyncio.start_server(lambda r, w: None, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    server.close()
    await server.wait_closed()
    link = RoverLink("127.0.0.1", port, ["rover/front", "rover/arm"], timeout_s=0.3)
    await link.probe_once()
    status = link.status()
    assert status["state"] == "unreachable"
    assert status["detail"]
    assert status["streams"] == {}
    assert status["lastSeenMs"] is None
