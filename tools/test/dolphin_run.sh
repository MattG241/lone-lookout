#!/usr/bin/env bash
#
# dolphin_run.sh -- run a build in headless Dolphin and keep every Nth frame.
#
#   DOLPHIN=/path/to/dolphin-emu-nogui tools/test/dolphin_run.sh <dol> <out_dir> <seconds> <every> [-C Key=Value ...]
#
# Frames are dumped as PNGs (Dolphin's frame dump), and all but every Nth
# are deleted as the run goes, so an hour-long run doesn't fill the disk.
# Extra -C options set Dolphin config for the test matrix, e.g.
#   -C SYSCONF.IPL.PGS=False      480i instead of 480p
#   -C SYSCONF.IPL.AR=True        16:9
#   -C Dolphin.Core.FallbackRegion=2 -C SYSCONF.IPL.E60=False   PAL 50 Hz
# With SD=<folder>, the virtual SD card is synced with that folder (and
# written back when the game exits cleanly).
set -uo pipefail

DOLPHIN=${DOLPHIN:-dolphin-emu-nogui}
DOL=$(realpath "$1")
OUT=$2
SECONDS_=$3
EVERY=$4
shift 4
mkdir -p "$OUT"
USERDIR=$(mktemp -d)
trap 'rm -rf "$USERDIR"' EXIT
FRAMES="$USERDIR/Dump/Frames"

SDOPTS=(-C Dolphin.Core.WiiSDCardEnableFolderSync=False -C Dolphin.Core.WiiSDCard=False)
if [ -n "${SD:-}" ]; then
	SDOPTS=(-C Dolphin.Core.WiiSDCard=True -C Dolphin.Core.WiiSDCardEnableFolderSync=True
	        -C Dolphin.General.WiiSDCardSyncFolder="$(realpath "$SD")")
fi

# Keep frame n if n % EVERY == 0; leave the newest file alone (it may still be being written).
prune() {
	local final=$1 files n
	[ -d "$FRAMES" ] || return 0
	mapfile -t files < <(ls "$FRAMES" 2>/dev/null | sed -n 's/^framedump_\([0-9]*\)\.png$/\1/p' | sort -n)
	[ ${#files[@]} -gt 0 ] || return 0
	[ "$final" = 1 ] || unset 'files[${#files[@]}-1]'
	for n in "${files[@]}"; do
		if [ $((n % EVERY)) -eq 0 ]; then
			mv "$FRAMES/framedump_$n.png" "$OUT/frame_$(printf %06d "$n").png"
		else
			rm -f "$FRAMES/framedump_$n.png"
		fi
	done
}

timeout "$SECONDS_" "$DOLPHIN" -u "$USERDIR" -p headless -v Vulkan \
	-C Dolphin.Movie.DumpFrames=True -C Dolphin.Movie.DumpFramesSilent=True \
	-C GFX.Settings.DumpFramesAsImages=True -C GFX.Enhancements.DisableCopyFilter=False \
	-C GFX.Settings.AspectRatio=2 -C SYSCONF.IPL.AR=False "${SDOPTS[@]}" "$@" \
	-e "$DOL" >"$OUT/dolphin.log" 2>&1 &
PID=$!
while kill -0 $PID 2>/dev/null; do
	sleep 2
	prune 0
done
wait $PID
STATUS=$?
# A frame cut off by the timeout is incomplete: drop the newest, keep the rest.
LAST=$(ls "$FRAMES" 2>/dev/null | sed -n 's/^framedump_\([0-9]*\)\.png$/\1/p' | sort -n | tail -1)
[ -n "$LAST" ] && [ "$STATUS" = 124 ] && rm -f "$FRAMES/framedump_$LAST.png"
prune 1
echo "dolphin exit $STATUS; kept $(ls "$OUT"/frame_*.png 2>/dev/null | wc -l) frames in $OUT"
