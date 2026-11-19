# Science

The science sequencer on the rover runs each science task as a ROS action: **bradford**, **npk** and **pano**. The GUI starts and cancels them from the Electron app; status, results and ready flags come back over 900 MHz, and the large files (panoramas, full-resolution photos) are downloaded over 5 GHz.

## Science sequences

```mermaid
flowchart LR
    subgraph ROVER["Rover (ROS 2)"]
        subgraph SCI["Science subsystem"]
            SEQ["science sequencer<br/>state machine, ROS actions:<br/>bradford / npk / pano"]
            subgraph EMB["Embedded nodes"]
                DRILL["drill"]
                MIXER["mixer"]
                SPEC["spectrophotometer"]
                NPK["NPK sensor"]
            end
            PANO["pano pipeline<br/>(see pano diagram)"]
        end

        SCICAM["science camera node<br/>A219"]
        MEDIA["media file server<br/>(panoramas, science photos)"]
        TELEM["rover_telemetry<br/>(ROS ↔ MAVLink)"]
    end

    subgraph LINKS["Radio links"]
        R900(["900 MHz<br/>commands, status, results, ready flags"])
        R5G(["5 GHz<br/>media files"])
    end

    subgraph BASE["Base station laptop (no ROS)"]
        BACK["gui/backend<br/>(Python)"]
        APP["gui/frontend in Electron<br/>(control client)"]
    end

    VIEW["gui/frontend in a browser<br/>(read-only viewers)"]

    %% Control and status
    APP <-->|"control WebSocket<br/>(start / cancel, status)"| BACK
    BACK -->|"viewer WebSocket<br/>(status, results)"| VIEW
    BACK <--> R900
    R900 <--> TELEM
    TELEM -->|"action goal: start / cancel"| SEQ
    SEQ -->|"action feedback + result"| TELEM

    %% Bradford sequence
    SEQ -->|"1. lower / raise"| DRILL
    SEQ -->|"2. run"| MIXER
    SEQ -->|"3. measure"| SPEC
    SPEC -->|"absorbance"| SEQ
    SEQ -->|"photo of drill site"| SCICAM
    SCICAM -->|"full-res photo"| MEDIA

    %% NPK sequence
    SEQ -->|"lower + measure"| NPK
    NPK -->|"N, P, K readings"| SEQ

    %% Pano sequence
    SEQ -->|"run pano"| PANO
    PANO -->|"annotated panorama"| MEDIA

    %% File delivery
    MEDIA -->|"HTTP download"| R5G
    R5G --> BACK
    BACK -->|"panoramas + photos (HTTP)"| APP
    BACK -->|"panoramas + photos (HTTP)"| VIEW
```

## Panorama

The pano sequence rotates the rover 45° eight times, captures a frame with its heading after each turn, stitches the frames, and annotates the panorama with cardinal directions and scale.

```mermaid
flowchart LR
    subgraph ROVER["Rover (ROS 2)"]
        ZED["ZED 2i driver<br/>(zed-ros2-wrapper)"]
        SEQ["science sequencer<br/>(pano action)"]
        DRIVE["drive"]
        CAP["camera capture node<br/>image + heading"]
        STITCH["cv stitcher"]
        ANNOT["pano annotator<br/>cardinal direction + scale"]
        MEDIA["media file server"]
        TELEM["rover_telemetry<br/>(ROS ↔ MAVLink)"]
    end

    subgraph LINKS["Radio links"]
        R900(["900 MHz<br/>start, status, ready flag"])
        R5G(["5 GHz<br/>panorama file"])
    end

    subgraph BASE["Base station laptop (no ROS)"]
        BACK["gui/backend<br/>(Python)"]
        APP["gui/frontend in Electron<br/>(control client)"]
    end

    VIEW["gui/frontend in a browser<br/>(read-only viewers)"]

    %% Start
    APP -->|"start pano"| BACK
    BACK <--> R900
    R900 <--> TELEM
    TELEM -->|"action goal"| SEQ

    %% Capture loop: repeat 8 times
    SEQ -->|"1. rotate 45° (×8)"| DRIVE
    SEQ -->|"2. capture() after settling"| CAP
    ZED -->|"image + mag topics"| CAP
    CAP -->|"3. CapturedFrame<br/>(image, heading, time)"| SEQ

    %% Process
    SEQ -->|"4. stitch(frames)"| STITCH
    STITCH -->|"5. panorama"| ANNOT
    ANNOT -->|"6. annotated panorama"| SEQ
    SEQ -->|"7. save file"| MEDIA

    %% Report back
    SEQ -->|"feedback: captured i/8,<br/>result: pano ready"| TELEM
    MEDIA -->|"8. HTTP download"| R5G
    R5G --> BACK
    BACK -->|"show panorama"| APP
    BACK -->|"show panorama"| VIEW
```
