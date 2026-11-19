"""Bags -> dataset.npz for gesture training.

Reads every bag in <data>/bags, named <class>_<person>_<anything>, normalizes
each frame with cv_gesture.preprocess, and slides a fixed-length window over
each bag. Prints a per-bag quality report.

Run with ROS + the workspace sourced (needs rosbag2_py, cv_interfaces, cv_gesture):
    python3 extract.py
    python3 extract.py --data ~/Documents/training/gesture/data --trim 2 --stride 0.25
"""
import argparse
import os
from collections import defaultdict

import numpy as np
import rosbag2_py
from rclpy.serialization import deserialize_message

from cv_interfaces.msg import Keypoints
from cv_gesture.preprocess import (
    WINDOW_SEC, normalize_frame, resample_window, stamp_to_sec,
)

TOPIC = '/cv/pose/keypoints'
DEFAULT_DATA = '~/Documents/training/gesture/data'


def read_bag(path):
    """Return capture times [N] and normalized frames [N, 17, 3] for one bag."""
    reader = rosbag2_py.SequentialReader()
    reader.open(rosbag2_py.StorageOptions(uri=path, storage_id='mcap'),
                rosbag2_py.ConverterOptions('', ''))
    times, frames = [], []
    while reader.has_next():
        topic, data, _ = reader.read_next()
        if topic != TOPIC:
            continue
        m = deserialize_message(data, Keypoints)
        times.append(stamp_to_sec(m.header.stamp))   # capture time, not bag receive time
        frames.append(normalize_frame(m.x, m.y, m.confidence, m.detected))
    order = np.argsort(times)
    return np.asarray(times)[order], np.asarray(frames)[order]


def parse_name(bag_name):
    parts = bag_name.split('_')
    if len(parts) < 2:
        raise ValueError(f'bag name "{bag_name}" must be <class>_<person>_...')
    return parts[0].lower(), parts[1].lower()


def extract_bag(times, frames, trim, stride, min_valid):
    """Slide a WINDOW_SEC window over one bag. Returns windows, end times, stats, warnings."""
    valid = frames.any(axis=(1, 2))          # frame usable = person + shoulders found
    first, last = times[0] + trim, times[-1] - trim
    windows, ends = [], []
    for end in np.arange(first + WINDOW_SEC, last + 1e-9, stride):
        in_win = (times > end - WINDOW_SEC) & (times <= end)
        if not in_win.any() or valid[in_win].mean() < min_valid:
            continue                          # person missing in too much of this window
        w = resample_window(times, frames, end_time=end)
        if w is not None:
            windows.append(w)
            ends.append(end)

    dur = times[-1] - times[0]
    fps = (len(times) - 1) / dur if dur > 0 else 0.0
    gaps = np.diff(times)
    warnings = []
    if valid.mean() < 0.8:
        warnings.append('person missing often')
    if dur < 10:
        warnings.append('short bag')
    if fps < 8:
        warnings.append('low fps')
    if (gaps > 0.5).sum() > 2:
        warnings.append(f'{(gaps > 0.5).sum()} stalls')
    if not windows:
        warnings.append('NO usable windows')
    stats = dict(dur=dur, fps=fps, valid=valid.mean())
    return windows, ends, stats, warnings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default=DEFAULT_DATA, help='folder containing bags/')
    ap.add_argument('--trim', type=float, default=2.0, help='seconds dropped at each bag end')
    ap.add_argument('--stride', type=float, default=0.25, help='seconds between windows')
    ap.add_argument('--min-valid', type=float, default=0.8,
                    help='min fraction of frames with a person in a window')
    args = ap.parse_args()

    data_dir = os.path.expanduser(args.data)
    bag_dir = os.path.join(data_dir, 'bags')
    bag_names = sorted(d for d in os.listdir(bag_dir)
                       if os.path.isfile(os.path.join(bag_dir, d, 'metadata.yaml')))
    for d in sorted(set(os.listdir(bag_dir)) - set(bag_names)):
        print(f'skipping {d}: no metadata.yaml (still recording or not stopped with Ctrl+C?)')

    X, labels, persons, bags, ends = [], [], [], [], []
    per_class = defaultdict(lambda: {'bags': 0, 'windows': 0, 'people': set()})

    print(f'\n{"bag":<32} {"dur":>6} {"fps":>5} {"person":>7} {"windows":>8}  status')
    for name in bag_names:
        label, person = parse_name(name)
        times, frames = read_bag(os.path.join(bag_dir, name))
        if len(times) < 2:
            print(f'{name:<32} ⚠ EMPTY (0 messages): were pose_node/webcam_node running and visible?')
            continue
        windows, w_ends, s, warns = extract_bag(
            times, frames, args.trim, args.stride, args.min_valid)
        status = '⚠ ' + ', '.join(warns) if warns else 'OK'
        print(f'{name:<32} {s["dur"]:5.1f}s {s["fps"]:5.1f} {s["valid"]:6.0%} '
              f'{len(windows):8d}  {status}')

        X += windows
        labels += [label] * len(windows)
        persons += [person] * len(windows)
        bags += [name] * len(windows)
        ends += w_ends
        c = per_class[label]
        c['bags'] += 1
        c['windows'] += len(windows)
        c['people'].add(person)

    if not X:
        raise SystemExit('\nNo windows extracted.')

    classes = sorted(per_class)
    y = np.array([classes.index(lbl) for lbl in labels], np.int64)

    print('\nclass      bags  windows  people')
    for c in classes:
        p = per_class[c]
        print(f'{c:<10} {p["bags"]:4d} {p["windows"]:8d}  {", ".join(sorted(p["people"]))}')
    if len(classes) < 2:
        print('\n⚠ only one class: record at least a "none" class before training')

    out = os.path.join(data_dir, 'dataset.npz')
    np.savez_compressed(
        out, X=np.stack(X).astype(np.float32), y=y, classes=np.array(classes),
        person=np.array(persons), bag=np.array(bags), window_end=np.array(ends))
    print(f'\nsaved {out}: X {np.stack(X).shape}, {len(classes)} classes')


if __name__ == '__main__':
    main()
