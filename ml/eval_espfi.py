"""Can the board's signals tell a fall from other movement? Tested on ESP-Fi HAR.

Reads the harness output (frames made by the board's own sensing code) plus the raw
clips, builds one row of event features per clip, and tests a logistic regression
"fall vs everything else" with leave-one-person-out cross-validation.

Dataset: ESP-Fi HAR (Wen, Ruan, Wang, Zhou, Gao, Li, Ad Hoc Networks, 2026), CC BY 4.0.
Usage: python3 -I eval_espfi.py <harness.npy> <index.csv> [out.json]
"""
import csv, json, sys
import numpy as np

clips = list(np.load(sys.argv[1], allow_pickle=True))
paths = [r["path"] for r in csv.DictReader(open(sys.argv[2]))]
FEATS = ["peak_ratio", "burst_secs", "post_ratio", "pre_ratio", "shift", "peak_time"]


def profile(a):
    a = a / a.mean(axis=1, keepdims=True)
    return a.mean(axis=0)


def features(c, path):
    F = c["frames"]
    t, motion, shortm = F[:, 0] / 1000, F[:, 1], F[:, 2]
    ok = t >= 1.0                      # the board needs 1 s of packets to warm up
    t, motion, shortm = t[ok], motion[ok], shortm[ok]
    calm = np.percentile(motion, 10)   # this clip's quiet level, like the room's noise floor
    i = int(np.argmax(shortm))
    hot = shortm > 1.4 * calm
    a = b = i
    while a > 0 and hot[a - 1]: a -= 1
    while b < len(hot) - 1 and hot[b + 1]: b += 1
    amp = np.fromfile(path, dtype=np.float32).reshape(-1, 52)
    n = len(amp)
    pre, post = profile(amp[: n // 6]), profile(amp[-n // 5:])
    shift = 1 - np.corrcoef(pre, post)[0, 1]
    return [shortm[i] / calm, t[b] - t[a], motion[-20:].mean() / calm, motion[:5].mean() / calm,
            shift * 1000, t[i]]


X = np.array([features(c, p) for c, p in zip(clips, paths)])
y = np.array([c["activity"] == "fall" for c in clips]).astype(int)
act = np.array([c["activity"] for c in clips])
person = np.array([c["person"] for c in clips])


def fit_lr(X, y, l2=1.0, iters=3000, lr=0.1):
    mu, sd = X.mean(0), X.std(0) + 1e-9
    Z = (X - mu) / sd
    w, b = np.zeros(Z.shape[1]), 0.0
    pos = y.mean()
    sw = np.where(y == 1, 0.5 / pos, 0.5 / (1 - pos))   # balance falls against the rest
    for _ in range(iters):
        p = 1 / (1 + np.exp(-(Z @ w + b)))
        g = sw * (p - y)
        w -= lr * (Z.T @ g / len(y) + l2 * w / len(y))
        b -= lr * g.mean()
    # fold the scaling into the weights so the board can use raw features
    return w / sd, b - (w * mu / sd).sum()


def predict(wb, X):
    w, b = wb
    return 1 / (1 + np.exp(-(X @ w + b)))


prob = np.zeros(len(y))
for p in sorted(set(person)):
    tr, te = person != p, person == p
    prob[te] = predict(fit_lr(X[tr], y[tr]), X[te])
pred = prob >= 0.5

tp = int((pred & (y == 1)).sum()); fn = int((~pred & (y == 1)).sum())
fp = int((pred & (y == 0)).sum()); tn = int((~pred & (y == 0)).sum())
res = {
    "clips": len(y), "falls": int(y.sum()), "people": len(set(person)),
    "fall_recall": tp / (tp + fn), "precision": tp / max(1, tp + fp),
    "false_alarm_rate_non_falls": fp / (fp + tn),
    "accuracy": (tp + tn) / len(y),
    "flagged_as_fall_by_activity": {a: float(pred[act == a].mean()) for a in sorted(set(act))},
    "per_person_fall_recall": {int(p): float(pred[(person == p) & (y == 1)].mean()) for p in sorted(set(person))},
    "feature_medians": {a: dict(zip(FEATS, np.round(np.median(X[act == a], 0), 3).tolist())) for a in sorted(set(act))},
}
wb = fit_lr(X, y)
res["weights_all_data"] = dict(zip(FEATS, np.round(wb[0], 4).tolist()), bias=round(float(wb[1]), 4))
print(json.dumps(res, indent=1))
if len(sys.argv) > 3:
    json.dump(res, open(sys.argv[3], "w"), indent=1)
