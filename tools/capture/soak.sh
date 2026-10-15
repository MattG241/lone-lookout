#!/usr/bin/env bash
#
# soak.sh -- the audio soak: an hour of Day 11 with the music playing back to
# back, calls to Basin, Listen and walking (make SOAK=1), in headless Dolphin.
#
#   DOLPHIN=/path/to/dolphin-emu-nogui tools/capture/soak.sh out_dir [seconds]
#
# The soak build presents one frame every 10 s of game time, each showing the
# mixer stats (blocks mixed, DSP underruns, mix time per 16 ms block). Dolphin
# dumps only those frames; this keeps one a minute plus the last, and prints
# how long the run covered. Emulated timing is approximate -- this catches a
# mixer that can't keep up or a thread that starves, not real hardware jitter.
set -euo pipefail

DOLPHIN=${DOLPHIN:-dolphin-emu-nogui}
OUT=${1:-soak}
SECONDS_WALL=${2:-4200}
USERDIR=$(mktemp -d)
trap 'rm -rf "$USERDIR"' EXIT

make SOAK=1
timeout "$SECONDS_WALL" "$DOLPHIN" -u "$USERDIR" -p headless -v Vulkan \
	-C Dolphin.Movie.DumpFrames=True -C Dolphin.Movie.DumpFramesSilent=True \
	-C GFX.Settings.DumpFramesAsImages=True -C GFX.Enhancements.DisableCopyFilter=False \
	-C GFX.Settings.AspectRatio=2 -C SYSCONF.IPL.AR=0 \
	-e "$PWD/lonelookout_soak.dol" >/dev/null 2>&1 || true

mkdir -p "$OUT"
frames=( $(ls "$USERDIR"/Dump/Frames/*.png 2>/dev/null | sort -V) )
n=${#frames[@]}
echo "soak: $n frames presented (one per 10 s of game time: ~$((n / 6)) min)"
for ((i = 0; i < n; i += 6)); do cp "${frames[$i]}" "$OUT/soak_$(printf '%03d' $((i / 6)))min.png"; done
[ "$n" -gt 0 ] && cp "${frames[$((n - 1))]}" "$OUT/soak_last.png"
