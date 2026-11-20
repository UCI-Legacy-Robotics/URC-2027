import numpy as np

from cv_gesture.preprocess import (
    NUM_FEATURES, 
    NUM_KP, 
    WINDOW_LEN, 
    normalize_frame, 
    resample_window,
)


def make_person(seed=0):
    rng = np.random.default_rng(seed)
    x = rng.uniform(200, 400, NUM_KP).astype(np.float32)
    y = rng.uniform(100, 400, NUM_KP).astype(np.float32)
    x[5], x[6] = 250.0, 350.0            # shoulders clearly apart
    y[5], y[6] = 200.0, 200.0
    conf = np.full(NUM_KP, 0.9, np.float32)
    return x, y, conf


def test_resolution_invariant():
    x, y, c = make_person()
    np.testing.assert_allclose(normalize_frame(x, y, c), normalize_frame(2 * x, 2 * y, c), atol=1e-5)


def test_position_invariant():
    x, y, c = make_person()
    np.testing.assert_allclose(normalize_frame(x, y, c), normalize_frame(x + 100, y - 50, c), atol=1e-5)


def test_no_person_or_bad_shoulders_is_zeros():
    x, y, c = make_person()
    assert not normalize_frame(x, y, c, detected=False).any()
    c[5] = 0.1
    assert not normalize_frame(x, y, c).any()


def test_low_conf_keypoint_zeroed():
    x, y, c = make_person()
    c[9] = 0.1                            # left wrist unreliable
    out = normalize_frame(x, y, c)
    assert out[9, 0] == 0 and out[9, 1] == 0


def wave(hz):
    t = np.arange(0, 2.5 + 1e-9, 1 / hz)
    frames = np.zeros((len(t), NUM_KP, 3), np.float32)
    frames[:, 9, 0] = np.sin(2 * np.pi * 0.5 * t)
    return t, frames


def test_framerate_invariant():
    slow = resample_window(*wave(10), end_time=2.5)
    fast = resample_window(*wave(30), end_time=2.5)
    assert slow.shape == fast.shape == (WINDOW_LEN, NUM_FEATURES)
    np.testing.assert_allclose(slow, fast, atol=0.05)


def test_not_enough_history_returns_none():
    t, f = wave(30)
    assert resample_window(t[:20], f[:20]) is None   # only ~0.6 s


def test_face_and_legs_hidden():
    x, y, c = make_person()
    out = normalize_frame(x, y, c)
    assert not out[[0, 1, 2, 3, 4, 13, 14, 15, 16]].any()   # face + legs all zero
    assert out[[5, 6, 7, 8, 9, 10, 11, 12]].any()           # arms + torso still there


def test_used_parts_can_be_changed():
    x, y, c = make_person()
    out = normalize_frame(x, y, c, used_parts=('arms', 'torso', 'legs'))
    assert out[[13, 14, 15, 16]].any()                      # legs kept when asked for
    assert not out[[0, 1, 2, 3, 4]].any()                   # face still hidden
