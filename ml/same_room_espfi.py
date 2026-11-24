"""Same person, same room: train on some of a person's clips, test on their others.

This is closer to how Ghost is used (trained in one home), unlike leave-one-person-out.
Usage: python3 -I same_room_espfi.py <index.csv> [out.json]
"""
import csv, json, sys
import numpy as np
from sklearn.ensemble import ExtraTreesClassifier
from sklearn.model_selection import StratifiedKFold

sys.path.insert(0, sys.path[0])
rows = list(csv.DictReader(open(sys.argv[1])))


def feats(path):
    a = np.fromfile(path, dtype=np.float32).reshape(-1, 52)
    a = a / a.mean(axis=1, keepdims=True)
    n = len(a) // 25 * 25
    s = a[:n].reshape(-1, 25, 52).std(axis=1).mean(axis=1)
    curve = s / np.percentile(s, 10)
    pre, post = a[: len(a) // 6].mean(0), a[-len(a) // 5:].mean(0)
    return np.concatenate([curve, np.sort(curve)[::-1][:10], post - pre, a.std(0)])


X = np.array([feats(r["path"]) for r in rows])
act = np.array([r["activity"] for r in rows]); person = np.array([int(r["person"]) for r in rows])
y = (act == "fall").astype(int)
pred = np.zeros(len(y), bool); pred7 = np.empty(len(y), dtype=object)
for p in sorted(set(person)):
    idx = np.where(person == p)[0]
    for tr, te in StratifiedKFold(5, shuffle=True, random_state=0).split(idx, act[idx]):
        tr, te = idx[tr], idx[te]
        m = ExtraTreesClassifier(400, min_samples_leaf=1, class_weight="balanced", random_state=0, n_jobs=-1)
        pred[te] = m.fit(X[tr], y[tr]).predict_proba(X[te])[:, 1] >= 0.4
        pred7[te] = ExtraTreesClassifier(400, random_state=0, n_jobs=-1).fit(X[tr], act[tr]).predict(X[te])
tp = int((pred & (y == 1)).sum()); fp = int((pred & (y == 0)).sum())
res = {"fall_recall": tp / y.sum(), "false_alarm_rate_non_falls": fp / (y == 0).sum(), "precision": tp / max(1, tp + fp),
       "flagged_as_fall_by_activity": {a: float(pred[act == a].mean()) for a in sorted(set(act))},
       "seven_activity_accuracy": float((pred7 == act).mean())}
print(json.dumps(res, indent=1))
if len(sys.argv) > 2: json.dump(res, open(sys.argv[2], "w"), indent=1)
