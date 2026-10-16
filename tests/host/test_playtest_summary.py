#!/usr/bin/env python3
"""test_playtest_summary.py -- tools/playtest/summarise.py on a made-up log in the
format src/game/playtest.c writes."""
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "playtest"))
import summarise  # noqa: E402

LOG = """#session\tLone Lookout 1.1\tnunchuk
5.0\t1\t15:30\tnew_season
60.0\t1\t15:41\twhere\t250 480\tarea -1\tmode 0
100.0\t1\t15:48\tring\tD1_ARRIVE
112.0\t1\t15:50\tanswer\tD1_ARRIVE\t12
140.0\t1\t15:55\tchoice\tD1_ARRIVE\t1\t"It's a radio?"
200.0\t1\t16:05\tidle\t263 300\tarea 0\tmode 0
290.0\t1\t16:20\tidle_end\t90
300.0\t1\t16:22\tuse\tsit 4
330.0\t1\t18:30\ttimelapse\t120
400.0\t1\t19:10\tring\tD1_NIGHT
460.0\t1\t19:20\tmissed\tD1_NIGHT
500.0\t1\t19:30\tuse\tsleep 0
510.0\t2\t06:00\tday
600.0\t2\t06:15\tlisten
610.0\t2\t06:17\tsound\tthe creek
700.0\t2\t06:31\tquit
#session\tLone Lookout 1.1\tremote only
3.0\t2\t06:31\tcontinue
40.0\t2\t06:40\tnotebook\t0
"""


def main():
    fails = checks = 0

    def check(cond, msg):
        nonlocal fails, checks
        checks += 1
        if not cond:
            fails += 1
            print("FAIL:", msg)

    path = os.path.join(tempfile.mkdtemp(), "tester.log")
    open(path, "w").write(LOG)
    r = summarise.report(path)
    check("2 sitting(s)" in r, "two sittings")
    check("| 1 | 8 min 25 s | 120 min | 1 min 30 s | 2 | 1 | 1 | 12 s |" in r, "Day 1 row:\n" + r)
    check("Day 1 19:20 D1_NIGHT" in r, "the missed call is listed")
    check("1 min 30 s: Day 1 16:05, Kestrel Knob (263 300)" in r, "the idle stretch, with its place")
    check("Save and quit" in r, "the first sitting ended cleanly")
    check("no clean exit" in r, "the second didn't")
    check("the creek (Day 2)" in r and "(1 of 12)" in r, "sounds logged")
    check("sit 1" in r and "sleep 1" in r and "listen 1" in r, "uses counted")
    check('D1_ARRIVE: "It\'s a radio?"' in r, "choices listed once-quoted")
    print(f"{checks} checks, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
