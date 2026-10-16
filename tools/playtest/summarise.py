#!/usr/bin/env python3
"""
summarise.py -- turn playtest logs into a report.

With "Playtest log" on in the game's Options, each sitting appends to
sd:/apps/lonelookout/playtest.log (see src/game/playtest.h). Copy the file off
the card (one per tester) and run:

    python3 tools/playtest/summarise.py alice.log bob.log > report.md

For each tester: real time per in-game day, how much of it was time-lapse,
stood still or spent on the radio; the calls (how long they rang, which were
missed, the choices made); where people stood still longest; what they used;
and how each sitting ended -- the last things done before a quit are often
the most useful line in the report.
"""
import collections
import os
import sys

AREAS = {-1: "between places", 0: "Kestrel Knob", 1: "Larkspur Meadow", 2: "Mirror Tarn",
         3: "Cinder Ridge", 4: "the '98 Burn", 5: "Pell's Cabin"}   # world_area in src/world/world.h


def area_name(field):
    try:
        return AREAS.get(int(field.split()[-1]), field)
    except (ValueError, IndexError):
        return field


def parse(path):
    """[(session, [event dicts])] from one log file."""
    sessions, cur = [], None
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line:
                continue
            if line.startswith("#session"):
                cur = {"head": line.split("\t")[1:], "events": []}
                sessions.append(cur)
                continue
            parts = line.split("\t")
            if len(parts) < 4:
                continue
            if cur is None:
                cur = {"head": ["?"], "events": []}
                sessions.append(cur)
            try:
                t, day = float(parts[0]), int(parts[1])
            except ValueError:
                continue
            cur["events"].append({"t": t, "day": day, "clock": parts[2], "ev": parts[3], "args": parts[4:]})
    return sessions


def fmt_min(seconds):
    if seconds < 60:
        return f"{seconds:.0f} s"
    if seconds < 600:
        m, s = divmod(int(round(seconds)), 60)
        return f"{m} min {s} s" if s else f"{m} min"
    return f"{seconds / 60:.0f} min"


def report(path):
    out = [f"## {os.path.basename(path)}", ""]
    sessions = parse(path)
    if not sessions:
        return "\n".join(out + ["(empty log)", ""])
    per_day = collections.defaultdict(lambda: {"real": 0.0, "lapse_min": 0.0, "idle": 0.0, "rang": 0,
                                               "answered": 0, "missed": 0, "delays": []})
    idles, choices, missed, uses, sounds = [], [], [], collections.Counter(), []
    area_time = collections.Counter()
    endings = []
    total = 0.0
    for si, s in enumerate(sessions, 1):
        ev = s["events"]
        if not ev:
            continue
        span = ev[-1]["t"] - ev[0]["t"]
        total += span
        # Real time per day: the gaps between consecutive events, charged to the day they happened in.
        for a, b in zip(ev, ev[1:]):
            gap = b["t"] - a["t"]
            if gap < 900:                       # longer gaps are a console left on: not counted
                per_day[a["day"]]["real"] += gap
        idle_at = None
        for e in ev:
            d = per_day[e["day"]]
            kind, args = e["ev"], e["args"]
            if kind == "ring":
                d["rang"] += 1
            elif kind == "answer":
                d["answered"] += 1
                if len(args) > 1:
                    d["delays"].append(float(args[1]))
            elif kind == "missed":
                d["missed"] += 1
                missed.append(f"Day {e['day']} {e['clock']} {args[0] if args else '?'}")
            elif kind == "choice":
                said = (args[2] if len(args) > 2 else args[-1]).strip('"')
                choices.append(f"Day {e['day']} {args[0] if args else '?'}: \"{said}\"")
            elif kind == "timelapse" and args:
                d["lapse_min"] += float(args[0])
            elif kind == "idle":
                idle_at = e
            elif kind == "idle_end" and args:
                secs = float(args[0])
                d["idle"] += secs
                if idle_at:
                    where = area_name(idle_at["args"][1]) if len(idle_at["args"]) > 1 else "?"
                    idles.append((secs, f"Day {idle_at['day']} {idle_at['clock']}, {where} "
                                        f"({idle_at['args'][0] if idle_at['args'] else '?'})"))
                idle_at = None
            elif kind == "use" and args:
                uses[args[0].split()[0]] += 1
            elif kind in ("listen", "notebook", "binoculars", "call_basin"):
                uses[kind] += 1
            elif kind == "sound" and args:
                sounds.append(f"{args[0]} (Day {e['day']})")
            elif kind == "where" and len(e["args"]) > 1:
                area_time[area_name(e["args"][1])] += 1
        last = ev[-1]
        how = {"quit": "Save and quit", "to_title": "back to the title", "ending": "reached the ending"}.get(
            last["ev"], "no clean exit (switched off, or the log was cut)")
        tail = ", ".join(f"{x['ev']}{(' ' + x['args'][0]) if x['args'] else ''}" for x in ev[-6:-1])
        endings.append(f"Sitting {si}: {fmt_min(span)}, ended on Day {last['day']} {last['clock']} -- {how}. "
                       f"Just before: {tail or '(nothing)'}")

    out += [f"{len(sessions)} sitting(s), {total / 3600:.1f} h in all.", ""]
    out += ["| Day | Real time | Time-lapse (game) | Stood still | Calls rang | Answered | Missed | Answer delay |",
            "|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for day in sorted(per_day):
        d = per_day[day]
        delay = f"{sum(d['delays']) / len(d['delays']):.0f} s" if d["delays"] else "-"
        out.append(f"| {day} | {fmt_min(d['real'])} | {d['lapse_min']:.0f} min | {fmt_min(d['idle'])} | "
                   f"{d['rang']} | {d['answered']} | {d['missed']} | {delay} |")
    out.append("")
    out += ["**How each sitting ended**", ""] + [f"- {e}" for e in endings] + [""]
    if idles:
        out += ["**Longest stretches standing still** (no input for 30 s or more)", ""]
        out += [f"- {fmt_min(s)}: {w}" for s, w in sorted(idles, reverse=True)[:10]] + [""]
    if area_time:
        out += ["**Where the minutes went** (sampled once a minute)", ""]
        out += [f"- {a}: {n} min" for a, n in area_time.most_common()] + [""]
    if missed:
        out += ["**Calls that rang out**", ""] + [f"- {m}" for m in missed] + [""]
    if choices:
        out += ["**Choices**", ""] + [f"- {c}" for c in choices] + [""]
    if uses:
        out += ["**Used**: " + ", ".join(f"{k} {n}" for k, n in uses.most_common()), ""]
    out += [f"**Sounds logged** ({len(sounds)} of 12): " + (", ".join(sounds) if sounds else "none"), ""]
    return "\n".join(out)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    print("# Lone Lookout playtest report\n")
    for path in sys.argv[1:]:
        if not os.path.exists(path):
            raise SystemExit(f"no such log: {path}")
        print(report(path))


if __name__ == "__main__":
    main()
