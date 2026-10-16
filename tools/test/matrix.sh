#!/usr/bin/env bash
#
# matrix.sh -- the Dolphin test matrix (DESIGN.md section 11).
#
#   DOLPHIN=/path/to/dolphin-emu-nogui tools/test/matrix.sh <out_dir> [row ...]
#
# Each row runs a scripted build in headless Dolphin and keeps a frame every
# two seconds; the developer overlay in those frames carries what to check
# (video mode, aspect, Nunchuk, SD and voice files, frame time, triangles,
# audio underruns). Rows:
#
#   ntsc_480p  ntsc_480i  pal50  pal60  wide_16x9   video modes (walk test)
#   remote_only                                     no Nunchuk (make REMOTETEST=1)
#   sd_vo  no_sd                                    with an SD card holding a voice file, and none
#   menus                                           title, options, save and continue (make MENUTEST=1)
#   perf                                            the worst views (make PERF=1)
#
# With no rows given, all of them run. The ridge time-lapse soak is longer
# and separate: tools/capture/soak.sh.
set -euo pipefail

OUT=${1:?usage: matrix.sh <out_dir> [row ...]}
shift
ROWS=("$@")
[ ${#ROWS[@]} -gt 0 ] || ROWS=(ntsc_480p ntsc_480i pal50 pal60 wide_16x9 remote_only sd_vo no_sd menus perf)
RUN=$(dirname "$0")/dolphin_run.sh
mkdir -p "$OUT"
OUT=$(realpath "$OUT")

NTSC=(-C Dolphin.Core.FallbackRegion=1 -C SYSCONF.IPL.E60=False)
PAL=(-C Dolphin.Core.FallbackRegion=2)

# A virtual SD card with one recorded line (a 2.5 s tone) and no save.
make_sd() {
	rm -rf "$1"
	mkdir -p "$1/apps/lonelookout/vo"
	python3 - "$1/apps/lonelookout/vo/D1_ARRIVE_01.pcm" <<'EOF'
import math, struct, sys
n = 55125
with open(sys.argv[1], "wb") as f:
    for i in range(n):
        env = min(1.0, i / 2000, (n - i) / 2000)
        f.write(struct.pack("<h", int(6000 * env * math.sin(2 * math.pi * 220 * i / 22050))))
EOF
}

for row in "${ROWS[@]}"; do
	echo "== $row"
	case $row in
	ntsc_480p)   make -s WALKTEST=1;   "$RUN" lonelookout_walktest.dol "$OUT/$row" 150 120 "${NTSC[@]}" -C SYSCONF.IPL.PGS=True ;;
	ntsc_480i)   make -s WALKTEST=1;   "$RUN" lonelookout_walktest.dol "$OUT/$row" 150 120 "${NTSC[@]}" -C SYSCONF.IPL.PGS=False ;;
	pal50)       make -s WALKTEST=1;   "$RUN" lonelookout_walktest.dol "$OUT/$row" 150 100 "${PAL[@]}" -C SYSCONF.IPL.E60=False -C SYSCONF.IPL.PGS=False ;;
	pal60)       make -s WALKTEST=1;   "$RUN" lonelookout_walktest.dol "$OUT/$row" 150 120 "${PAL[@]}" -C SYSCONF.IPL.E60=True -C SYSCONF.IPL.PGS=False ;;
	wide_16x9)   make -s WALKTEST=1;   "$RUN" lonelookout_walktest.dol "$OUT/$row" 150 120 "${NTSC[@]}" -C SYSCONF.IPL.PGS=True \
	                 -C SYSCONF.IPL.AR=True -C GFX.Settings.AspectRatio=1 ;;
	remote_only) make -s REMOTETEST=1; "$RUN" lonelookout_remotetest.dol "$OUT/$row" 150 120 "${NTSC[@]}" ;;
	sd_vo)       make -s WALKTEST=1;   make_sd "$OUT/$row.sd"
	             SD="$OUT/$row.sd" "$RUN" lonelookout_walktest.dol "$OUT/$row" 150 120 "${NTSC[@]}" ;;
	no_sd)       make -s WALKTEST=1;   "$RUN" lonelookout_walktest.dol "$OUT/$row" 150 120 "${NTSC[@]}" ;;
	menus)       make -s MENUTEST=1;   make_sd "$OUT/$row.sd"
	             SD="$OUT/$row.sd" "$RUN" lonelookout_menutest.dol "$OUT/$row" 240 50 "${NTSC[@]}"
	             ls -l "$OUT/$row.sd/apps/lonelookout/" ;;
	perf)        make -s PERF=1
	             "$RUN" lonelookout_perf.dol "$OUT/$row/raw" 270 1 "${NTSC[@]}"
	             python3 "$(dirname "$0")/../capture/split_shots.py" "$OUT/$row/raw" "$OUT/$row" ridge_sw ridge_w ridge_s \
	                 ridge_bench trailhead_n deck_sw deck_n meadow_e meadow_w burn_nw cabin_n tarn_seat rain_day11 smoke_day5
	             rm -rf "$OUT/$row/raw" ;;
	*) echo "unknown row $row" >&2; exit 2 ;;
	esac
done
