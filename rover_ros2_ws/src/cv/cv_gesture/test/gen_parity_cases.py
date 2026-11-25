"""Generate test cases for cv_classifier/test/test_preprocess_parity.cpp from preprocess.py.

Run (workspace sourced, so cv_gesture is importable) whenever preprocess.py changes:
    python3 test/gen_parity_cases.py

Writes test/data/parity_cases.txt: random inputs plus what preprocess.py outputs for them.
The C++ test checks preprocess.cpp (next to preprocess.py) produces the same numbers.
"""
import os

import numpy as np

from cv_gesture.preprocess import NUM_KP, normalize_frame, resample_window

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'parity_cases.txt')
rng = np.random.default_rng(0)


def f32(a):
    return ' '.join(f'{v:.9g}' for v in np.asarray(a, np.float32).ravel())


def f64(a):
    return ' '.join(f'{v:.17g}' for v in np.asarray(a, np.float64).ravel())


def random_person():
    x = rng.uniform(100, 600, NUM_KP).astype(np.float32)
    y = rng.uniform(50, 450, NUM_KP).astype(np.float32)
    conf = rng.uniform(0, 1, NUM_KP).astype(np.float32)
    conf[[5, 6]] = rng.uniform(0.35, 1, 2)                       # usually usable shoulders
    kind = rng.integers(0, 10)
    if kind == 0:
        conf[[11, 12]] = rng.uniform(0, 0.25, 2)                 # hips missing -> shoulder fallback
    elif kind == 1:
        conf[rng.integers(5, 7)] = rng.uniform(0, 0.29)          # bad shoulder -> zeros
    elif kind == 2:
        x[[11, 12]], y[[11, 12]] = x[[5, 6]], y[[5, 6]]          # hips on shoulders -> tiny scale
        conf[[11, 12]] = 0.9
    elif kind == 3:
        x[9], y[9] = 5000.0, -3000.0                             # wild wrist -> clipped
        conf[9] = 0.95
    elif kind == 4:
        conf[[0, 9, 13]] = [0.3, 0.3, 0.2999999]                 # exactly-at-threshold confidences
    return x, y, conf


def main():
    lines = []

    norm_cases = []
    for i in range(300):
        x, y, c = random_person()
        detected = rng.random() > 0.05
        out = normalize_frame(x, y, c, detected)
        norm_cases.append(f'{int(detected)} {f32(x)} {f32(y)} {f32(c)} {f32(out)}')
    lines.append(f'NORMALIZE {len(norm_cases)}')
    lines += norm_cases

    res_cases = []
    for i in range(40):
        n = int(rng.integers(5, 50))
        t0 = 1.7949e9 + rng.uniform(0, 1e5)                       # realistic epoch-seconds stamps
        times = t0 + np.cumsum(rng.uniform(0.03, 0.12, n))
        frames = np.stack([normalize_frame(*random_person()) for _ in range(n)])
        end = None if i % 2 == 0 else float(times[-1] - rng.uniform(0, 0.5))
        out = resample_window(times, frames, end_time=end)
        ok = out is not None
        line = (f'{n} {0 if end is None else 1} {f64([0.0 if end is None else end])} {int(ok)} '
                f'{f64(times)} {f32(frames)}')
        if ok:
            line += ' ' + f32(out)
        res_cases.append(line)
    lines.append(f'RESAMPLE {len(res_cases)}')
    lines += res_cases

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    n_ok = sum(c.split()[3] == '1' for c in res_cases)
    print(f'wrote {OUT}: {len(norm_cases)} normalize, {len(res_cases)} resample ({n_ok} full windows)')


if __name__ == '__main__':
    main()
