"""dataset.npz -> trained 1D temporal CNN -> gesture_cnn.onnx

Run in the PyTorch venv (ROS not needed):
    python train.py
    python train.py --val-person alex --epochs 80
Outputs go to <data>/runs/<timestamp>/.
"""
import argparse
import os
from datetime import datetime

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset

DEFAULT_DATA = '~/Documents/training/gesture/data'
NUM_KP = 17
SHOULDERS = (5, 6)
CONF_MIN = 0.3
# COCO left/right keypoint pairs, swapped when mirroring
LR_PAIRS = [(1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14), (15, 16)]
MIRROR = list(range(NUM_KP))
for a, b in LR_PAIRS:
    MIRROR[a], MIRROR[b] = b, a


# ---------------------------------------------------------------- model
class GestureCNN(nn.Module):
    """1D temporal CNN: convolutions slide along time; the 51 features are channels."""

    def __init__(self, n_features, n_classes, width=64, dropout=0.3):
        super().__init__()

        def block(c_in, c_out, dilation):
            return nn.Sequential(
                nn.Conv1d(c_in, c_out, kernel_size=5, padding=2 * dilation, dilation=dilation),
                nn.BatchNorm1d(c_out), nn.ReLU())

        # receptive field: 5 -> 13 -> 29 frames (~whole 30-frame window)
        self.features = nn.Sequential(
            block(n_features, width, 1), block(width, width, 2), block(width, width, 4))
        self.head = nn.Sequential(
            nn.AdaptiveAvgPool1d(1), nn.Flatten(), nn.Dropout(dropout), nn.Linear(width, n_classes))

    def forward(self, x):                      # x: [batch, time, features]
        return self.head(self.features(x.transpose(1, 2)))   # -> logits [batch, classes]


# ---------------------------------------------------------------- augmentation
def augment(xb):
    """Random, label-preserving changes to a batch [B, T, 51]. Training only."""
    B, T, _ = xb.shape
    k = xb.reshape(B, T, NUM_KP, 3).clone()

    # mirror left/right (covers the other arm)
    flip = torch.rand(B) < 0.5
    k[flip] = k[flip][:, :, MIRROR]
    k[flip, :, :, 0] *= -1

    # small rotation and scale (camera tilt, body proportions)
    ang = (torch.rand(B) - 0.5) * np.deg2rad(20)          # +-10 deg
    scale = 0.9 + 0.2 * torch.rand(B)
    c, s = (torch.cos(ang) * scale)[:, None, None], (torch.sin(ang) * scale)[:, None, None]
    x, y = k[..., 0].clone(), k[..., 1].clone()
    k[..., 0], k[..., 1] = c * x - s * y, s * x + c * y   # zeros stay zeros

    # keypoint jitter on visible points only
    visible = k[..., 2] >= CONF_MIN
    k[..., :2] += torch.randn_like(k[..., :2]) * 0.02 * visible[..., None]

    # drop a random joint for the whole window (occlusion); keep shoulders, they anchor the frame
    drop = torch.rand(B, NUM_KP) < 0.05
    drop[:, list(SHOULDERS)] = False
    k[drop[:, None, :, None].expand_as(k)] = 0.0

    # time stretch: same gesture done 20% faster or slower
    out = k.reshape(B, T, -1).transpose(1, 2)              # [B, 51, T]
    new_t = int(round(T * (0.8 + 0.4 * torch.rand(1).item())))
    out = F.interpolate(out, size=new_t, mode='linear', align_corners=True)
    if new_t > T:
        start = torch.randint(0, new_t - T + 1, (1,)).item()
        out = out[:, :, start:start + T]
    elif new_t < T:
        pad = T - new_t
        left = torch.randint(0, pad + 1, (1,)).item()
        out = F.pad(out, (left, pad - left), mode='replicate')
    return out.transpose(1, 2).contiguous()


# ---------------------------------------------------------------- data split
def split(person, bag, y, val_person):
    """Validation must be data the model never saw: a whole person, or whole bags."""
    people = np.unique(person)
    if val_person is None and len(people) > 1:
        val_person = str(people[-1])
    if val_person is not None:
        if val_person not in people:
            raise SystemExit(f'--val-person {val_person} not in dataset (have {list(people)})')
        val = person == val_person
        return ~val, val, f'held-out person "{val_person}"'

    # one person only (pilot): hold out the last bag of each class that has 2+ bags
    val = np.zeros(len(y), bool)
    held = []
    for c in np.unique(y):
        bags_c = sorted(np.unique(bag[y == c]))
        if len(bags_c) >= 2:
            val |= bag == bags_c[-1]
            held.append(bags_c[-1])
    if not val.any():
        raise SystemExit('Need 2+ bags in at least one class (or 2+ people) for validation.')
    return ~val, val, 'held-out bags: ' + ', '.join(held)


# ---------------------------------------------------------------- evaluation
@torch.no_grad()
def evaluate(model, X, y, loss_fn):
    model.eval()
    logits = model(torch.from_numpy(X))
    loss = loss_fn(logits, torch.from_numpy(y)).item()
    pred = logits.argmax(1).numpy()
    return loss, (pred == y).mean(), pred


def confusion(y, pred, classes):
    n = len(classes)
    m = np.zeros((n, n), int)
    np.add.at(m, (y, pred), 1)
    w = max(len(c) for c in classes) + 2
    lines = ['true \\ pred'.ljust(w) + ''.join(c[:8].rjust(9) for c in classes)]
    for i, c in enumerate(classes):
        lines.append(c.ljust(w) + ''.join(f'{v:9d}' for v in m[i]))
    return lines


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', default=DEFAULT_DATA)
    ap.add_argument('--val-person', default=None)
    ap.add_argument('--epochs', type=int, default=60)
    ap.add_argument('--patience', type=int, default=15, help='stop after N epochs w/o improvement')
    ap.add_argument('--batch', type=int, default=32)
    ap.add_argument('--lr', type=float, default=1e-3)
    ap.add_argument('--seed', type=int, default=0)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    data_dir = os.path.expanduser(args.data)
    run_dir = os.path.join(data_dir, 'runs', datetime.now().strftime('%Y%m%d-%H%M%S'))
    report = []

    def log(msg=''):
        print(msg)
        report.append(msg)

    d = np.load(os.path.join(data_dir, 'dataset.npz'))
    X, y = d['X'].astype(np.float32), d['y'].astype(np.int64)
    classes = [str(c) for c in d['classes']]
    person, bag = d['person'].astype(str), d['bag'].astype(str)
    if len(classes) < 2:
        raise SystemExit(f'Only one class ({classes[0]}). Record at least "none" bags too.')

    tr, va, how = split(person, bag, y, args.val_person)
    log(f'dataset {X.shape}, classes {classes}')
    log(f'train {tr.sum()} windows / val {va.sum()} windows ({how})')
    counts = np.bincount(y[tr], minlength=len(classes))
    for c, n in zip(classes, counts):
        log(f'  train {c:<10} {n:6d}')
    if (counts == 0).any():
        raise SystemExit('A class has no training windows; record more bags for it.')

    weights = torch.tensor(counts.sum() / (len(classes) * counts), dtype=torch.float32)
    loss_fn = nn.CrossEntropyLoss(weight=weights)       # stops "none" from dominating
    model = GestureCNN(X.shape[2], len(classes))
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    loader = DataLoader(TensorDataset(torch.from_numpy(X[tr]), torch.from_numpy(y[tr])),
                        batch_size=args.batch, shuffle=True,
                        drop_last=bool(tr.sum() % args.batch == 1))   # BatchNorm can't take a batch of 1
    log(f'model params: {sum(p.numel() for p in model.parameters()):,}\n')

    best_loss, best_state, best_epoch = float('inf'), None, 0
    for epoch in range(1, args.epochs + 1):
        model.train()
        total = 0.0
        for xb, yb in loader:
            loss = loss_fn(model(augment(xb)), yb)
            opt.zero_grad()
            loss.backward()
            opt.step()
            total += loss.item() * len(xb)
        val_loss, val_acc, _ = evaluate(model, X[va], y[va], loss_fn)
        mark = ''
        if val_loss < best_loss:
            best_loss, best_epoch, mark = val_loss, epoch, '  *'
            best_state = {k: v.clone() for k, v in model.state_dict().items()}
        log(f'epoch {epoch:3d}  train loss {total / tr.sum():.3f}  '
            f'val loss {val_loss:.3f}  val acc {val_acc:.1%}{mark}')
        if epoch - best_epoch >= args.patience:
            log(f'no improvement for {args.patience} epochs, stopping')
            break

    # ---- final report on the best epoch
    model.load_state_dict(best_state)
    model.eval()
    _, acc, pred = evaluate(model, X[va], y[va], loss_fn)
    log(f'\nbest epoch {best_epoch}: val acc {acc:.1%}\n')
    for line in confusion(y[va], pred, classes):
        log(line)
    log('\nper-bag val accuracy:')
    for b in np.unique(bag[va]):
        m = bag[va] == b
        log(f'  {b:<34} {(pred[m] == y[va][m]).mean():6.1%}  ({m.sum()} windows)')

    # ---- save + export
    os.makedirs(run_dir)
    torch.save(best_state, os.path.join(run_dir, 'best.pt'))
    with open(os.path.join(run_dir, 'classes.txt'), 'w') as f:
        f.write('\n'.join(classes) + '\n')

    onnx_path = os.path.join(run_dir, 'gesture_cnn.onnx')
    dummy = torch.zeros(1, X.shape[1], X.shape[2])        # node runs one window at a time
    torch.onnx.export(model, (dummy,), onnx_path,
                      input_names=['keypoints'], output_names=['logits'],
                      external_data=False)                 # one self-contained .onnx file

    import onnxruntime as ort                              # verify export matches PyTorch
    sess = ort.InferenceSession(onnx_path, providers=['CPUExecutionProvider'])
    sample = X[va][:1]
    onnx_out = sess.run(None, {sess.get_inputs()[0].name: sample})[0]
    with torch.no_grad():
        torch_out = model(torch.from_numpy(sample)).numpy()
    log(f'\nONNX vs PyTorch max diff: {np.abs(onnx_out - torch_out).max():.2e} (should be ~1e-6)')

    log(f'saved to {run_dir}')
    with open(os.path.join(run_dir, 'report.txt'), 'w') as f:
        f.write('\n'.join(report) + '\n')


if __name__ == '__main__':
    main()
