#!/usr/bin/env bash
#
# soak.sh -- the audio soak: an hour of Day 11 with the music playing back to
# back, calls to Basin, Listen and walking (make SOAK=1), in headless Dolphin.
#
#   DOLPHIN=/path/to/dolphin-emu-nogui tools/capture/soak.sh out_dir [minutes] [wall_limit_s]
#
# SOAK_VARIANT=ridge runs the ridge time-lapse soak instead (make SOAKRIDGE=1):
# seated on Augie's bench letting time pass, days cycling, calls answered.
#
# The soak build draws and presents one frame every 10 s, each showing the
# mixer stats (blocks mixed, DSP underruns, mix time per 16 ms block). Game
# logic, the radio, Listen and all the audio run every frame as usual; the
# hidden frames just aren't drawn (the mixer thread outranks the game thread,
# so its work is the same), which lets Dolphin run the hour unthrottled.
# Dolphin dumps only the shown frames; this keeps one a minute plus the last.
# Emulated timing is approximate -- this catches a mixer that can't keep up or
# a thread that starves, not real hardware jitter.
set -euo pipefail

DOLPHIN=${DOLPHIN:-dolphin-emu-nogui}
OUT=${1:-soak}
MINUTES=${2:-61}
WALL_LIMIT=${3:-14400}
USERDIR=$(mktemp -d)
pid=
trap '[ -n "$pid" ] && kill "$pid" 2>/dev/null; rm -rf "$USERDIR"' EXIT

if [ "${SOAK_VARIANT:-}" = ridge ]; then
	make SOAKRIDGE=1
	DOL=lonelookout_soakridge.dol
else
	make SOAK=1
	DOL=lonelookout_soak.dol
fi
timeout "$WALL_LIMIT" "$DOLPHIN" -u "$USERDIR" -p headless -v Vulkan \
	-C Dolphin.Movie.DumpFrames=True -C Dolphin.Movie.DumpFramesSilent=True \
	-C GFX.Settings.DumpFramesAsImages=True -C GFX.Enhancements.DisableCopyFilter=False \
	-C GFX.Settings.AspectRatio=2 -C SYSCONF.IPL.AR=0 -C Dolphin.Core.EmulationSpeed=0 \
	-e "$PWD/$DOL" >/dev/null 2>&1 &
pid=$!
# Six shown frames a minute of game time: stop once the hour is covered.
want=$((MINUTES * 6 + 1))
while kill -0 "$pid" 2>/dev/null; do
	have=$( (ls "$USERDIR"/Dump/Frames 2>/dev/null || true) | wc -l)
	[ "$have" -ge "$want" ] && { sleep 2; kill "$pid" 2>/dev/null || true; break; }
	sleep 5
done
wait "$pid" 2>/dev/null || true

mkdir -p "$OUT"
frames=( $( (ls "$USERDIR"/Dump/Frames/*.png 2>/dev/null || true) | sort -V) )
n=${#frames[@]}
echo "soak: $n frames presented (one per 10 s of game time: ~$((n / 6)) min)"
for ((i = 0; i < n; i += 6)); do cp "${frames[$i]}" "$OUT/soak_$(printf '%03d' $((i / 6)))min.png"; done
[ "$n" -gt 0 ] && cp "${frames[$((n - 1))]}" "$OUT/soak_last.png"
