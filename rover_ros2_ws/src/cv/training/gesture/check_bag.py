#just a script to check that a bag is usable after recording
import argparse

import numpy as np
import rosbag2_py
from rclpy.serialization import deserialize_message

from cv_interfaces.msg import Keypoints

TOPIC = '/cv/pose/keypoints'
L_SH, R_SH, L_WR, R_WR = 5, 6, 9, 10
CONF_MIN = 0.3


def read_bag(path):
    reader = rosbag2_py.SequentialReader()
    reader.open(rosbag2_py.StorageOptions(uri=path, storage_id='mcap'),
                rosbag2_py.ConverterOptions('', ''))
    t, det, x, y, conf = [], [], [], [], []
    while reader.has_next():
        topic, data, _ = reader.read_next()
        if topic != TOPIC:
            continue
        m = deserialize_message(data, Keypoints)
        t.append(m.header.stamp.sec + m.header.stamp.nanosec * 1e-9)  # capture time
        det.append(m.detected)
        x.append(m.x); y.append(m.y); conf.append(m.confidence)
    return (np.array(t) - t[0], np.array(det), np.array(x), np.array(y), np.array(conf))


def wrist_up(y, conf, wrist):
    """True where that wrist is above shoulder height (image y grows downward)."""
    shoulder_y = (y[:, L_SH] + y[:, R_SH]) / 2
    ok = conf[:, wrist] >= CONF_MIN
    return ok & (y[:, wrist] < shoulder_y)


def bar(frac, width=10):
    n = int(round(frac * width))
    return '█' * n + '·' * (width - n)


def check(path, chunk):
    t, det, x, y, conf = read_bag(path)
    dur = t[-1]
    gaps = np.diff(t)
    lw, rw = wrist_up(y, conf, L_WR) & det, wrist_up(y, conf, R_WR) & det

    print(f'\n=== {path.rstrip("/").split("/")[-1]}')
    print(f'duration {dur:.1f}s | {len(t)} msgs | {len(t) / dur:.1f} fps | '
          f'detected {det.mean():.0%} | max frame gap {gaps.max() * 1000:.0f} ms')
    print(f'{"time":>11}  {"in frame":<15} {"L wrist up":<15} {"R wrist up":<15}')
    for s in np.arange(0, dur, chunk):
        m = (t >= s) & (t < s + chunk)
        if not m.any():
            continue
        d, l, r = det[m].mean(), lw[m].mean(), rw[m].mean()
        print(f'{s:5.0f}-{s + chunk:<4.0f}s  {bar(d)} {d:4.0%} {bar(l)} {l:4.0%} {bar(r)} {r:4.0%}')

    if det.mean() < 0.8:
        print('⚠ person missing in >20% of frames')
    if gaps.max() > 0.5:
        print('⚠ frame gap over 0.5 s (pose node stalled or camera dropped)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bags', nargs='+')
    ap.add_argument('--chunk', type=float, default=5.0, help='seconds per row')
    args = ap.parse_args()
    for b in args.bags:
        check(b, args.chunk)


if __name__ == '__main__':
    main()
