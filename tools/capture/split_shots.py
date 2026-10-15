#!/usr/bin/env python3
"""
split_shots.py -- pick one frame per shot from a Dolphin frame dump.

The screenshot tour (make SHOTS=1) separates shots with pure-black frames.
This finds each run of non-black frames and copies the last-but-few frame of
every run (fades have settled by then) to the output directory.

Usage: python3 tools/capture/split_shots.py <Dump/Frames dir> <out dir> [names...]
"""
import os
import re
import sys

from PIL import Image, ImageStat


def main():
    src, out = sys.argv[1], sys.argv[2]
    names = sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    frames = sorted((f for f in os.listdir(src) if f.endswith(".png")),
                    key=lambda f: int(re.findall(r"\d+", f)[-1]))
    runs, cur = [], []
    for f in frames:
        im = Image.open(os.path.join(src, f)).convert("L")
        dark = ImageStat.Stat(im.resize((64, 48))).mean[0] < 3.0
        if dark:
            if cur:
                runs.append(cur)
            cur = []
        else:
            cur.append(f)
    if cur:
        runs.append(cur)
    # Skip the boot run (before the first separator) if names are given.
    full = [r for r in runs if len(r) > 60]
    for i, r in enumerate(full):
        pick = r[-6]
        name = names[i] if i < len(names) else f"shot_{i:02d}"
        Image.open(os.path.join(src, pick)).convert("RGB").save(os.path.join(out, f"{name}.png"))
        print(f"{name}: {pick} ({len(r)} frames)")


if __name__ == "__main__":
    main()
