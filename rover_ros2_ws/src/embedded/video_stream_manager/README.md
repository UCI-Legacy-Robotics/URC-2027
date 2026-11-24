# video_stream_manager

Encodes rover camera images to H.264 and publishes them to the rover's MediaMTX.

## Build

```bash
colcon build --symlink-install --packages-up-to video_stream_manager
source install/setup.bash
```

See [docs/system/video.md](../../../../docs/system/video.md) for where it fits in the video pipeline.
