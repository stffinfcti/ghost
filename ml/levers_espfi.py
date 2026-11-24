"""What raises fall accuracy in one room? Tests on ESP-Fi HAR, same person and room.

Leave-one-trial-out within each person (train on their other tries, test on one),
so every test clip is unseen. Compares:
  - how many of the person's own examples the model gets (2, 4, 6, 9 per activity)
  - adding other people's recordings to the person's own
  - richer features (per-subcarrier motion over time)
Usage: python3 -I levers_espfi.py <index.csv> [out.json]
"""
import csv, json, sys
import numpy as np
from sklearn.ensemble import ExtraTreesClassifier

rows = list(csv.DictReader(open(sys.argv[1])))


def base_feats(a):
    n = len(a) // 25 * 25
    s = a[:n].reshape(-1, 25, 52).std(axis=1).mean(axis=1)
    curve = s / np.percentile(s, 10)
    pre, post = a[: len(a) // 6].mean(0), a[-len(a) // 5:].mean(0)
    return np.concatenate([curve, np.sort(curve)[::-1][:10], post - pre, a.std(0)])


def rich_feats(a):
    n = len(a) // 50 * 50
    seg = a[:n].reshape(-1, 50, 52)                       # 0.5 s chunks
    sub = seg.std(axis=1)                                 # motion per subcarrier per chunk
    sub = sub / np.percentile(sub, 10, axis=0, keepdims=True)
    groups = sub.reshape(sub.shape[0], 13, 4).mean(axis=2)  # 13 subcarrier groups
    i = int(np.argmax(groups.mean(1)))
    around = np.pad(groups, ((3, 3), (0, 0)), mode="edge")[i:i + 7].ravel()  # 3.5 s centred on the burst
    d = np.diff(a, axis=0)
    return np.concatenate([base_feats(a), around, [np.abs(d).mean(), np.abs(d).max(axis=0).mean()]])


A = {r["path"]: (lambda a: a / a.mean(axis=1, keepdims=True))(np.fromfile(r["path"], dtype=np.float32).reshape(-1, 52))
     for r in rows}
act = np.array([r["activity"] for r in rows]); person = np.array([int(r["person"]) for r in rows])
trial = np.array([int(r["trial"]) for r in rows]); y = (act == "fall").astype(int)
XB = np.array([base_feats(A[r["path"]]) for r in rows]); XR = np.array([rich_feats(A[r["path"]]) for r in rows])


def run(X, n_own, add_others, th=0.4, seed=0):
    rng = np.random.default_rng(seed)
    pred = np.zeros(len(y), bool)
    for p in sorted(set(person)):
        for t in range(1, 11):
            te = (person == p) & (trial == t)
            others_trials = [u for u in range(1, 11) if u != t]
            own = rng.choice(others_trials, n_own, replace=False)
            tr = (person == p) & np.isin(trial, own)
            if add_others:
                tr = tr | (person != p)
            m = ExtraTreesClassifier(300, class_weight="balanced", random_state=seed, n_jobs=-1)
            m.fit(X[tr], y[tr])
            pred[te] = m.predict_proba(X[te])[:, 1] >= th
    fa = {a: float(pred[act == a].mean()) for a in sorted(set(act)) if a != "fall"}
    return {"fall_recall": round(float(pred[y == 1].mean()), 3),
            "false_alarm_rate": round(float(pred[y == 0].mean()), 3),
            "squat_flagged": round(fa["squat"], 3)}


res = {}
for n in (2, 4, 6, 9):
    res[f"own_{n}_examples"] = run(XB, n, False)
res["own_9_plus_other_people"] = run(XB, 9, True)
res["own_9_rich_features"] = run(XR, 9, False)
res["own_9_rich_plus_other_people"] = run(XR, 9, True)
for th in (0.3, 0.5):
    res[f"own_9_rich_plus_others_threshold_{th}"] = run(XR, 9, True, th)
print(json.dumps(res, indent=1))
if len(sys.argv) > 2: json.dump(res, open(sys.argv[2], "w"), indent=1)
