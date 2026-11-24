"""How much fall information is in ESP32 CSI at all? A richer model on ESP-Fi HAR.

Uses the same per-packet normalisation as the board (divide by the packet's mean),
then richer features: the motion curve over time, how each subcarrier changed from
start to end, and each subcarrier's spread. Leave-one-person-out, 8 people.
Usage: python3 -I upper_bound_espfi.py <index.csv> [out.json]
"""
import csv, json, sys
import numpy as np
from sklearn.ensemble import ExtraTreesClassifier
from sklearn.metrics import confusion_matrix

rows = list(csv.DictReader(open(sys.argv[1])))


def feats(path):
    a = np.fromfile(path, dtype=np.float32).reshape(-1, 52)
    a = a / a.mean(axis=1, keepdims=True)
    n = len(a) // 25 * 25
    s = a[:n].reshape(-1, 25, 52).std(axis=1).mean(axis=1)       # motion every 0.25 s
    calm = np.percentile(s, 10)
    curve = s / calm
    pre, post = a[: len(a) // 6].mean(0), a[-len(a) // 5:].mean(0)
    order = np.sort(curve)[::-1]
    return np.concatenate([curve, np.sort(curve)[::-1][:10], post - pre, a.std(0),
                           [1 - np.corrcoef(pre, post)[0, 1], order[:4].mean(), (curve > 1.5).sum()]])


X = np.array([feats(r["path"]) for r in rows])
act = np.array([r["activity"] for r in rows])
person = np.array([int(r["person"]) for r in rows])
y = (act == "fall").astype(int)
labels = sorted(set(act))

prob = np.zeros(len(y)); pred7 = np.empty(len(y), dtype=object)
for p in sorted(set(person)):
    tr, te = person != p, person == p
    m = ExtraTreesClassifier(n_estimators=400, min_samples_leaf=2, class_weight="balanced", random_state=0, n_jobs=-1)
    prob[te] = m.fit(X[tr], y[tr]).predict_proba(X[te])[:, 1]
    m7 = ExtraTreesClassifier(n_estimators=400, min_samples_leaf=2, random_state=0, n_jobs=-1)
    pred7[te] = m7.fit(X[tr], act[tr]).predict(X[te])

out = {}
for th in (0.3, 0.4, 0.5):
    pred = prob >= th
    tp = int((pred & (y == 1)).sum()); fp = int((pred & (y == 0)).sum())
    out[f"threshold_{th}"] = {
        "fall_recall": tp / y.sum(), "false_alarm_rate_non_falls": fp / (y == 0).sum(),
        "precision": tp / max(1, tp + fp),
        "flagged_as_fall_by_activity": {a: float(pred[act == a].mean()) for a in labels},
        "per_person_fall_recall": {int(q): float(pred[(person == q) & (y == 1)].mean()) for q in sorted(set(person))},
    }
out["seven_activity_accuracy"] = float((pred7 == act).mean())
out["seven_activity_labels"] = labels
out["seven_activity_confusion"] = confusion_matrix(act, pred7, labels=labels).tolist()
print(json.dumps(out, indent=1))
if len(sys.argv) > 2:
    json.dump(out, open(sys.argv[2], "w"), indent=1)
