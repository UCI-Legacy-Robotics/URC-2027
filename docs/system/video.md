# Video

Rover cameras reach the GUI over the 5 GHz link; the 900 MHz link only carries the controls.

- Each camera runs in its own process (component container) with its own stream manager, so frames reach the encoder without serialization and one camera failing doesn't stop the others. Each stream manager encodes H.264 to its own MediaMTX path: `rover/front` (ZED 2i) and `rover/arm` (OV9281). See [Video stream manager](../rover/video-stream-manager.md).
- The rover's MediaMTX only sends a stream over 5 GHz while someone is watching it (on-demand pull).
- The base MediaMTX pulls each stream from the rover as RTSP over UDP, so a lost packet damages one frame instead of stalling the stream while TCP retransmits. It serves the streams over WebRTC (port 18889) to the Electron app and to browser viewers.
- `gui/backend` starts the base MediaMTX, restarts it if it exits and stops it on shutdown (see [GUI setup](../setup/gui.md#base-station-video)). Video itself doesn't go through the backend. The config is `gui/backend/mediamtx/mediamtx.yml`.
- The GUI's [video feed widget](../gui/video-feed.md) plays the streams and graphs the link's health.
- Bitrate and resolution are commands, so only the Electron app can change them. They travel over 900 MHz as MAVLink commands (see [MAVLink dialect](mavlink.md)).

```mermaid
flowchart LR
    subgraph ROVER["Rover (ROS 2)"]
        subgraph FRONTPROC["Front camera process (intra-process)"]
            ZED["ZED 2i driver<br/>(zed-ros2-wrapper)"]
            SMF["stream manager<br/>(GStreamer, H.264)"]
        end
        subgraph ARMPROC["Arm camera process (intra-process)"]
            ARMCAM["OV9281 driver<br/>(mjpeg_camera, JPEG)<br/>second OV9281 planned"]
            SMA["stream manager<br/>(JPEG decode, H.264)"]
        end
        subgraph SCIPROC["Science camera process (planned)"]
            SCICAM["science camera node<br/>A219 (science mission)"]
            SMS["stream manager"]
        end
        RMTX["MediaMTX (rover)"]
        TELEM["rover_telemetry<br/>(ROS ↔ MAVLink)"]
    end

    subgraph LINKS["Radio links"]
        R900(["900 MHz<br/>MAVLink: commands"])
        R5G(["5 GHz<br/>video streams"])
    end

    subgraph BASE["Base station laptop (no ROS)"]
        BACK["gui/backend<br/>(Python)"]
        BMTX["MediaMTX (base)"]
        APP["gui/frontend in Electron<br/>(control client)"]
    end

    VIEW["gui/frontend in a browser<br/>(read-only viewers)"]

    %% Frames into each stream manager (no serialization)
    ZED -->|"images (in-process)"| SMF
    ARMCAM -->|"JPEGs (in-process)"| SMA
    SCICAM -->|"live video frames"| SMS

    %% Video path
    SMF -->|"H.264 rover/front"| RMTX
    SMA -->|"H.264 rover/arm"| RMTX
    SMS -->|"H.264"| RMTX
    RMTX -->|"on-demand pull<br/>(only while watched)"| R5G
    R5G --> BMTX
    BMTX -->|"WebRTC"| APP
    BMTX -->|"WebRTC (2.4 GHz Wi-Fi)"| VIEW

    %% Control path: only the Electron app sends commands
    APP -->|"bitrate / resolution"| BACK
    BACK <--> R900
    R900 <--> TELEM
    TELEM -->|"bitrate / resolution"| SMF
    TELEM -->|"bitrate / resolution"| SMA
```
