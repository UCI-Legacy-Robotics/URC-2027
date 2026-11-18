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


def normalize_frame (x, y, conf, detected=True):
    #

    if not detected:
        return np.zeros((NUM_KP, 3), np.float32)

    x, y, conf = (np.asarray(a, np.float32) for a in (x, y, conf))
    if min(conf[L_SHOULDER], conf[R_SHOULDER]) < KP_CONF_MIN:
        return np.zeros((NUM_KP, 3), np.float32)

    cx = (x[L_SHOULDER] + x[R_SHOULDER]) / 2
    cy = (y[L_SHOULDER] + y[R_SHOULDER]) / 2
    scale = np.hypot(x[L_SHOULDER] - x[R_SHOULDER], y[L_SHOULDER] - y[R_SHOULDER])
    if scale < 1e-3:
        return np.zeros((NUM_KP, 3), np.float32)

    valid = conf >= KP_CONF_MIN
    nx = np.where(valid, (x - cx) / scale, 0.0)
    ny = np.where(valid, (y - cy) / scale, 0.0)
    return np.stack([nx, ny, conf], axis=-1).astype(np.float32)


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





    