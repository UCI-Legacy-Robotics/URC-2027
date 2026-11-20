##keypoint preprocessing 
## needs to be used by classifier node and something else

import numpy as np
 
TARGET_HZ = 15.0
WINDOW_SEC = 2.0
WINDOW_LEN = int(TARGET_HZ * WINDOW_SEC) # 30 frames
NUM_KP = 17
NUM_FEATURES = NUM_KP * 3  # x, y, confidence <--per keypoint
L_SHOULDER, R_SHOULDER = 5, 6
KP_CONF_MIN = 0.3
L_HIP, R_HIP = 11, 12
TORSO_PER_SHOULDER = 1.6 # typical, use if hips are not visible
CLIP = 5.0

# COCO keypoint groups, by name
COCO_PARTS = {
    'face':  [0, 1, 2, 3, 4],
    'arms':  [5, 6, 7, 8, 9, 10],     # shoulders, elbows, wrists
    'torso': [11, 12],                # hips
    'legs':  [13, 14, 15, 16],
}
# parts the model is allowed to see; the rest are zeroed so the model can't use
# framing/distance shortcuts (e.g. "knees visible" = standing far back)
USED_PARTS = ('arms', 'torso')

def normalize_frame (x, y, conf, detected=True, used_parts=USED_PARTS):

    if not detected:
        return np.zeros((NUM_KP, 3), np.float32)

    x, y, conf = (np.asarray(a, np.float32) for a in (x, y, conf))
    if min(conf[L_SHOULDER], conf[R_SHOULDER]) < KP_CONF_MIN:
        return np.zeros((NUM_KP, 3), np.float32)

    cx = (x[L_SHOULDER] + x[R_SHOULDER]) / 2
    cy = (y[L_SHOULDER] + y[R_SHOULDER]) / 2
    shoulder_w = np.hypot(x[L_SHOULDER] - x[R_SHOULDER], y[L_SHOULDER] - y[R_SHOULDER])

    if min(conf[L_HIP], conf[R_HIP]) >= KP_CONF_MIN:
        # torso length doesn't shrink when you turn, unlike shoulder width
        hx, hy = (x[L_HIP] + x[R_HIP]) / 2, (y[L_HIP] + y[R_HIP]) / 2
        scale = np.hypot(hx - cx, hy - cy)
    else:
        # hips out of frame (close up): estimate torso length from shoulders
        scale = shoulder_w * TORSO_PER_SHOULDER

    if scale < 1e-3:
        return np.zeros((NUM_KP, 3), np.float32)

    valid = conf >= KP_CONF_MIN
    nx = np.clip(np.where(valid, (x - cx) / scale, 0.0), -CLIP, CLIP)
    ny = np.clip(np.where(valid, (y - cy) / scale, 0.0), -CLIP, CLIP)
    out = np.stack([nx, ny, conf], axis=-1).astype(np.float32)

    # zero every keypoint not in used_parts (done last: hips/shoulders are still needed above)
    keep = np.zeros(NUM_KP, bool)
    for part in used_parts:
        keep[COCO_PARTS[part]] = True
    out[~keep] = 0.0
    return out
    


def resample_window(times, frames, end_time=None):

    times = np.asarray(times, np.float64)
    if len(times) < 2:
        return None
    end_time = times[-1] if end_time is None else end_time
    start = end_time - WINDOW_SEC
    if times[0] > start + 1e-6:
        return None
    
    grid = np.linspace(start, end_time, WINDOW_LEN)
    flat = np.asarray(frames, np.float32).reshape(len(times), NUM_FEATURES)
    out = np.empty((WINDOW_LEN, NUM_FEATURES), np.float32)
    for j in range(NUM_FEATURES):
        out[:, j] = np.interp(grid, times, flat[:, j])
    return out


def stamp_to_sec(stamp):
    return stamp.sec + stamp.nanosec * 1e-9





    