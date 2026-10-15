#!/usr/bin/env bash
#
# capture_shots.sh -- reproduce the documentation screenshots.
#
# Builds the scripted screenshot tour (make SHOTS=1), runs it in headless
# Dolphin with PNG frame dumping, and keeps one settled frame per shot.
#
#   DOLPHIN=/path/to/dolphin-emu-nogui tools/capture/capture_shots.sh docs/screenshots/phase3
#
# Notes:
#  - Dolphin's "Disable Copy Filter" enhancement is ON by default; the game's
#    soft look relies on the copy filter, so this script turns it back off.
#  - Shots are captured at the Wii's native 640x480 (forced 4:3).
#  - A Dolphin built from source needs its Data/Sys folder copied next to the
#    binary (Binaries/Sys).
set -euo pipefail

DOLPHIN=${DOLPHIN:-dolphin-emu-nogui}
OUT=${1:-docs/screenshots/latest}
RUN_SECONDS=${RUN_SECONDS:-280}
USERDIR=$(mktemp -d)
trap 'rm -rf "$USERDIR"' EXIT

make SHOTS=1
timeout "$RUN_SECONDS" "$DOLPHIN" -u "$USERDIR" -p headless -v Vulkan \
	-C Dolphin.Movie.DumpFrames=True -C Dolphin.Movie.DumpFramesSilent=True \
	-C GFX.Settings.DumpFramesAsImages=True -C GFX.Enhancements.DisableCopyFilter=False \
	-C GFX.Settings.AspectRatio=2 -C SYSCONF.IPL.AR=0 \
	-e "$PWD/lonelookout_shots.dol" >/dev/null 2>&1 || true

# Shot names, in the order of SHOTS[] in src/game/tour.c.
python3 tools/capture/split_shots.py "$USERDIR/Dump/Frames" "$OUT" \
	trailhead_day1 tower_from_trail meadow_afternoon ridge_vista from_the_deck cab_night \
	tower_at_night tarn_dawn burn_morning cabin_afternoon notebook_map binoculars fire_finder \
	storm_day4 haze_day5 rain_day7 radio_choices note_bench
