"""End-to-end test of the board's fall state machine on ESP-Fi HAR clips.

Thresholds per person come from the quietest 10% of that person's frames (standing in
for the 30 s empty-room calibration, which the dataset does not have). The clips are
only 9.5 s long, so "still for 10 s" is shortened to the value given.
Usage: python3 -I pipeline_espfi.py <index.csv> <harness> <base.npy> <stillSecs> [out.json]
"""
import csv, json, subprocess, sys
from collections import defaultdict
import numpy as np

rows = list(csv.DictReader(open(sys.argv[1])))
exe, base, still = sys.argv[2], np.load(sys.argv[3], allow_pickle=True), sys.argv[4]
m, s = defaultdict(list), defaultdict(list)
for c in base:
    m[c["person"]].append(c["frames"][10:, 1]); s[c["person"]].append(c["frames"][10:, 2])
th = {p: (1.6 * np.percentile(np.concatenate(m[p]), 10), 1.6 * np.percentile(np.concatenate(s[p]), 10)) for p in m}
fired = defaultdict(list); outcomes = defaultdict(lambda: defaultdict(int))
for r in rows:
    mt, st = th[int(r["person"])]
    out = subprocess.run([exe, r["path"], r["packets"], "10", f"{mt:.5f}", f"{st:.5f}", still, "6"],
                         capture_output=True, text=True, check=True).stdout
    ev = [l.split(",")[2] for l in out.splitlines() if l.startswith("E")]
    fired[r["activity"]].append("fall" in ev)
    for e in ev: outcomes[r["activity"]][e] += 1
res = {"stillSecs": float(still),
       "alert_rate_by_activity": {a: round(float(np.mean(v)), 3) for a, v in sorted(fired.items())},
       "event_outcomes_by_activity": {a: dict(v) for a, v in sorted(outcomes.items())}}
print(json.dumps(res, indent=1))
if len(sys.argv) > 5: json.dump(res, open(sys.argv[5], "w"), indent=1)
