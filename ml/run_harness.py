"""Run the C++ harness (the board's own sensing code) over every converted clip.

Usage: python3 -I run_harness.py <index.csv> <harness binary> <ms_per_packet> <out.npz> [motionTh spikeTh stillSecs maxBurstSecs]
"""
import csv, subprocess, sys
import numpy as np

idx, exe, ms, out = sys.argv[1:5]
extra = sys.argv[5:]
clips = []
for r in csv.DictReader(open(idx)):
    res = subprocess.run([exe, r["path"], r["packets"], ms] + extra, capture_output=True, text=True, check=True)
    F, E = [], []
    for line in res.stdout.splitlines():
        p = line.split(",")
        if p[0] == "F":
            F.append([float(x) for x in p[1:]])
        else:
            E.append([p[2]] + [float(x) for x in p[3:]] + [float(p[1])])
    clips.append(dict(activity=r["activity"], person=int(r["person"]), split=r["split"], trial=int(r["trial"]),
                      frames=np.array(F), events=E))
np.save(out, np.array(clips, dtype=object), allow_pickle=True)
print(len(clips), "clips ->", out)
