#!/usr/bin/env python3
"""
compile_music.py -- compile the scores in data/music/*.txt into gen/music_data.h.

The music is composed as note data and performed by the synth (src/audio/music.c).

Score format, one piece per file:

    # a comment (a '#' straight after a note letter is a sharp)
    piece seven "Seven O'Clock"     id (what audio_music_cue() takes) and title
    tempo 66                        quarter notes per minute
    beats 4                         beats per bar
    bar [?] [~] [!]                 a new bar; flags:
                                      ?  the sequencer may drop this bar
                                      ~  it may swap this bar with the next
                                      !  coda: only played when the piece resolves (the credits)
    g D2/.5 A2/.5 D3+A3/1:.6 -/1    one voice's line in the bar, starting at beat 0
    again 3-8                       play bars 3..8 (as numbered so far) again, copied

Voices: g guitar (Karplus-Strong), h guitar harmonics, p piano, d drone, a pad.
Each token is NOTE[+NOTE...]/BEATS[:VELOCITY], or -/BEATS for a rest. NOTE is a
pitch name with octave (C4 is middle C; sharps only: C#, F#...). VELOCITY is
0..1 (default 0.7). A voice may have several lines in one bar (a chord under a
melody); each line starts at the top of the bar and must fit inside it.

Output: events (start and length in 1/48 beat), bars (first event, count,
beats, flags) and pieces (tempo, first bar, bar count).
"""
import glob
import os
import re
import sys

VOICES = {"g": 0, "h": 1, "p": 2, "d": 3, "a": 4}
FLAG_OPTIONAL, FLAG_SWAP, FLAG_CODA = 1, 2, 4
TICKS = 48
NOTE_RE = re.compile(r"^([A-G])(#?)(-?\d)$")
SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


class ScoreError(Exception):
    pass


def midi(name, where):
    m = NOTE_RE.match(name)
    if not m:
        raise ScoreError(f"{where}: bad note '{name}'")
    n = SEMI[m.group(1)] + (1 if m.group(2) else 0) + (int(m.group(3)) + 1) * 12
    if not 21 <= n <= 108:
        raise ScoreError(f"{where}: note '{name}' out of range")
    return n


def parse(path):
    piece = dict(id=None, title="", tempo=None, beats=4, bars=[], path=path)
    bar = None
    with open(path, encoding="utf-8") as f:
        for ln, raw in enumerate(f, 1):
            where = f"{os.path.basename(path)}:{ln}"
            line = re.sub(r"(^|\s)#.*$", "", raw).strip()     # '#' after a letter is a sharp
            if not line:
                continue
            word, _, rest = line.partition(" ")
            rest = rest.strip()
            if word == "piece":
                m = re.match(r'^(\w+)\s+"([^"]*)"$', rest)
                if not m:
                    raise ScoreError(f'{where}: piece <id> "<title>"')
                piece["id"], piece["title"] = m.group(1), m.group(2)
            elif word == "tempo":
                piece["tempo"] = float(rest)
            elif word == "beats":
                piece["beats"] = int(rest)
            elif word == "bar":
                flags = 0
                for ch in rest.replace(" ", ""):
                    flags |= {"?": FLAG_OPTIONAL, "~": FLAG_SWAP, "!": FLAG_CODA}.get(ch, 0)
                    if ch not in "?~!":
                        raise ScoreError(f"{where}: unknown bar flag '{ch}'")
                bar = dict(flags=flags, events=[], where=where)
                piece["bars"].append(bar)
            elif word == "again":
                m = re.match(r"^(\d+)-(\d+)$", rest)
                if not m:
                    raise ScoreError(f"{where}: again <from>-<to>")
                a, b = int(m.group(1)), int(m.group(2))
                if not 1 <= a <= b <= len(piece["bars"]):
                    raise ScoreError(f"{where}: again {a}-{b}: only {len(piece['bars'])} bars so far")
                for k in range(a - 1, b):
                    src = piece["bars"][k]
                    piece["bars"].append(dict(flags=src["flags"] & ~FLAG_CODA, events=list(src["events"]), where=where))
                bar = None
            elif word in VOICES:
                if bar is None:
                    raise ScoreError(f"{where}: notes outside a bar")
                t = 0.0
                for tok in rest.split():
                    m = re.match(r"^([^/]+)/([\d.]+)(?::([\d.]+))?$", tok)
                    if not m:
                        raise ScoreError(f"{where}: bad token '{tok}'")
                    dur = float(m.group(2))
                    vel = float(m.group(3)) if m.group(3) else 0.7
                    if dur <= 0 or not 0 < vel <= 1:
                        raise ScoreError(f"{where}: bad length or velocity in '{tok}'")
                    if m.group(1) != "-":
                        for name in m.group(1).split("+"):
                            bar["events"].append((round(t * TICKS), round(dur * TICKS), midi(name, where),
                                                  VOICES[word], round(vel * 255)))
                    t += dur
                if t > piece["beats"] + 1e-6:
                    raise ScoreError(f"{where}: line runs {t} beats in a {piece['beats']}-beat bar")
            else:
                raise ScoreError(f"{where}: unknown statement '{word}'")
    if not piece["id"] or not piece["tempo"] or not piece["bars"]:
        raise ScoreError(f"{path}: needs piece, tempo and at least one bar")
    return piece


def compile_dir(src_dir):
    pieces = [parse(p) for p in sorted(glob.glob(os.path.join(src_dir, "*.txt")))]
    events, bars, rows = [], [], []
    for pc in pieces:
        first_bar = len(bars)
        for b in pc["bars"]:
            ev = sorted(b["events"])
            bars.append((len(events), len(ev), pc["beats"], b["flags"]))
            events.extend(ev)
        rows.append((pc["id"], pc["title"], pc["tempo"], first_bar, len(pc["bars"])))

    def cstr(s):
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

    out = ["/* Generated by tools/music/compile_music.py from data/music/ -- do not edit. */",
           "#ifndef LL_MUSIC_DATA_H", "#define LL_MUSIC_DATA_H", "",
           f"#define MUS_TICKS {TICKS}", f"#define MUS_NUM_EVENTS {len(events)}",
           f"#define MUS_NUM_BARS {len(bars)}", f"#define MUS_NUM_PIECES {len(rows)}",
           f"#define MUS_OPTIONAL {FLAG_OPTIONAL}", f"#define MUS_SWAP {FLAG_SWAP}", f"#define MUS_CODA {FLAG_CODA}", "",
           "static const mus_event MUS_EVENTS[MUS_NUM_EVENTS] = {"]
    for e in events:
        out.append("\t{ %d, %d, %d, %d, %d }," % e)
    out += ["};", "", "static const mus_bar MUS_BARS[MUS_NUM_BARS] = {"]
    for b in bars:
        out.append("\t{ %d, %d, %d, %d }," % b)
    out += ["};", "", "static const mus_piece MUS_PIECES[MUS_NUM_PIECES] = {"]
    for r in rows:
        out.append("\t{ %s, %s, %.1ff, %d, %d }," % (cstr(r[0]), cstr(r[1]), r[2], r[3], r[4]))
    out += ["};", "", "#endif", ""]
    secs = sum(p["beats"] * sum(1 for b in p["bars"] if not b["flags"] & FLAG_CODA) * 60.0 / p["tempo"] for p in pieces)
    stats = dict(pieces=len(rows), bars=len(bars), events=len(events), seconds=secs,
                 each={p["id"]: p["beats"] * len(p["bars"]) * 60.0 / p["tempo"] for p in pieces})
    return "\n".join(out), stats


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "data/music"
    out = sys.argv[2] if len(sys.argv) > 2 else "gen"
    try:
        text, stats = compile_dir(src)
    except ScoreError as e:
        print(f"compile_music: {e}", file=sys.stderr)
        sys.exit(1)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "music_data.h"), "w") as f:
        f.write(text)
    print(f"music: {stats['pieces']} pieces, {stats['bars']} bars, {stats['events']} notes")


if __name__ == "__main__":
    main()
