"""Convert ESP-Fi HAR .mat amplitude files to raw float32 files the C++ harness reads.

Dataset: ESP-Fi HAR (Wen, Ruan, Wang, Zhou, Gao, Li, Ad Hoc Networks, 2026), CC BY 4.0.
Usage: python3 -I convert_espfi.py <Data dir> <out dir>
Writes <out>/<split>/<activity>/<name>.f32 (950 x 52 float32) and <out>/index.csv.
"""
import os, sys
import numpy as np
from scipy.io import loadmat

src, out = sys.argv[1], sys.argv[2]
rows = []
for split in ("train_amp", "test_amp"):
    for act in sorted(os.listdir(os.path.join(src, split))):
        d = os.path.join(src, split, act)
        os.makedirs(os.path.join(out, split, act), exist_ok=True)
        for fn in sorted(os.listdir(d)):
            a = loadmat(os.path.join(d, fn))["CSIamp"].astype(np.float32)
            name = fn[:-4]
            p = os.path.join(out, split, act, name + ".f32")
            a.tofile(p)
            scen, person, actid, trial = name.split("-")
            rows.append(f"{p},{split},{act},{person},{trial},{a.shape[0]},{a.shape[1]}")
with open(os.path.join(out, "index.csv"), "w") as f:
    f.write("path,split,activity,person,trial,packets,subcarriers\n" + "\n".join(rows) + "\n")
print(len(rows), "samples")
