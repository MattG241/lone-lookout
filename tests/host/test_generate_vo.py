#!/usr/bin/env python3
"""
test_generate_vo.py -- tools/voice/generate_vo.py against a fake ElevenLabs
server: casting, the quota and the model's credit rate, voicing whole calls
only, resuming, re-voicing changed lines, the PCM it writes, and that the API
key never reaches the output.
"""
import array
import contextlib
import io
import json
import math
import os
import sys
import tempfile
import threading
import zipfile
import shutil
import subprocess
from http.server import BaseHTTPRequestHandler, HTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "voice"))
import generate_vo as g  # noqa: E402

KEY = "sk_test_never_print_me"
STATE = {"used": 0.0, "limit": 1000, "tts": 0, "bad_key": 0}
VOICES = [{"voice_id": "v_lily", "name": "Lily", "category": "premade", "labels": {"gender": "female"}},
          {"voice_id": "v_eric", "name": "Eric", "category": "premade", "labels": {"gender": "male"}},
          {"voice_id": "v_bill", "name": "Bill", "category": "premade", "labels": {"gender": "male"}}]


class Fake(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def authed(self):
        if self.headers.get("xi-api-key") != KEY:
            STATE["bad_key"] += 1
            self.reply(401, {"detail": {"status": "invalid_api_key"}})
            return False
        return True

    def do_GET(self):
        if not self.authed():
            return
        if self.path.startswith("/v1/voices"):
            self.reply(200, {"voices": VOICES})
        elif self.path.startswith("/v1/user/subscription"):
            self.reply(200, {"character_count": int(STATE["used"]), "character_limit": STATE["limit"],
                             "next_character_count_reset_unix": 1800000000})
        elif self.path.startswith("/v1/models"):
            self.reply(200, [{"model_id": "eleven_flash_v2_5", "model_rates": {"character_cost_multiplier": 0.5}}])
        else:
            self.reply(404, {"detail": "no"})

    def do_POST(self):
        if not self.authed():
            return
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert "pcm_22050" in self.path, self.path
        cost = len(body["text"]) * 0.5
        if STATE["used"] + cost > STATE["limit"]:
            self.reply(401, {"detail": {"status": "quota_exceeded"}})
            return
        STATE["used"] += cost
        STATE["tts"] += 1
        # Half a second of silence, then a tone as long as the text, then silence.
        n = 22050 // 2
        tone = int(22050 * len(body["text"]) / 15)
        s = array.array("h", [0] * n + [int(9000 * math.sin(i * 0.12)) for i in range(tone)] + [0] * n)
        if sys.byteorder != "little":
            s.byteswap()
        self.reply(200, s.tobytes(), "audio/pcm")


checks = fails = 0


def check(cond, msg):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print("FAIL:", msg)


def run(*argv):
    out = io.StringIO()
    sys.argv = ["generate_vo.py", *argv]
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        try:
            g.main()
        except SystemExit as e:
            print(f"[exit {e}]")
    text = out.getvalue()
    check(KEY not in text, "the key appeared in the output")
    return text


def main():
    srv = HTTPServer(("127.0.0.1", 0), Fake)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    tmp = tempfile.mkdtemp()
    lines = os.path.join(tmp, "vo_lines.txt")
    with open(lines, "w", encoding="utf-8") as f:
        f.write("# test\n\n"
                "CI_QUIET_01\tTHEO\tBasin, Kestrel. Is it always this quiet?\n"
                "D1_ARRIVE_01\tDOT\tKestrel, Basin. Radio check, over.\n"
                "D1_ARRIVE_02\tTHEO\tBasin, Kestrel. Loud and clear — over.\n"
                "D1_ARRIVE_03\tTHEO\tLIVE Dry {dry}, wet {wet}.\n"
                "D2_AM_01\tDOT\tMorning, Kestrel. Readings when you have them.\n"
                "D2_AM_02\tCREW\tHarrow crew on the line.\n")
    g.API = f"http://127.0.0.1:{srv.server_port}"
    g.LINES = lines
    g.OUT = os.path.join(tmp, "vo")
    g.MANIFEST = os.path.join(tmp, "manifest.json")
    os.environ["ELEVENLABS_API_KEY"] = KEY

    # Order: story calls by day, check-ins last; LIVE lines left out.
    calls = g.group_calls(g.read_lines())
    check([c for c, _ in calls] == ["D1_ARRIVE", "D2_AM", "CI_QUIET"], f"call order {[c for c, _ in calls]}")
    check(all("{" not in t for _, ls in calls for _, _, t in ls), "LIVE lines are skipped")
    check(g.speakable("Loud and clear — over.") == "Loud and clear, over.", "dashes read as a pause")

    # The plan prices calls at the model's rate and spends nothing.
    out = run("--plan")
    check("Lily" in out and "Eric" in out and "Bill" in out, "casting picks the listed voices")
    check("0.5 credits per character" in out, "the model's rate is read from the API")
    check(STATE["tts"] == 0, "--plan voices nothing")

    # A budget that covers Day 1 but not Day 2: Day 2 is left whole for later.
    d1 = sum(len(g.speakable(t)) for _, _, t in calls[0][1]) * 0.5
    out = run("--budget", str(int(d1) + 5))
    check(os.path.exists(os.path.join(g.OUT, "D1_ARRIVE_01.pcm")), "Day 1 voiced")
    check("ElevenLabs" in open(os.path.join(g.OUT, "ELEVENLABS.txt")).read(), "the attribution marker is written")
    check(not os.path.exists(os.path.join(g.OUT, "D2_AM_01.pcm")), "Day 2 not started: whole calls only")
    check("stopping before D2_AM" in out, "says where it stopped")

    # The PCM: little-endian 16-bit, silence trimmed to ~80 ms, peak near -3 dBFS.
    raw = open(os.path.join(g.OUT, "D1_ARRIVE_01.pcm"), "rb").read()
    s = array.array("h", raw)
    if sys.byteorder != "little":
        s.byteswap()
    lead = next(i for i, v in enumerate(s) if abs(v) > 400)
    check(1000 < lead < 2500, f"leading silence trimmed to ~80 ms ({lead} samples)")
    peak = max(abs(v) for v in s)
    check(22000 < peak < 23300, f"peak normalised to -3 dBFS ({peak})")
    check(abs(s[0]) < 50 and abs(s[-1]) < 50, "ends fade to silence")

    # Resume: nothing is re-voiced; the rest is done when credits allow.
    before = STATE["tts"]
    out = run()
    check(os.path.exists(os.path.join(g.OUT, "D2_AM_02.pcm")), "the rest voiced on the next run")
    check(os.path.exists(os.path.join(g.OUT, "CI_QUIET_01.pcm")), "check-ins voiced last")
    check(STATE["tts"] - before == 3, f"only the missing lines were voiced ({STATE['tts'] - before})")
    before = STATE["tts"]
    run()
    check(STATE["tts"] == before, "a third run voices nothing")

    # A changed line is voiced again, and only that one.
    with open(lines, "a", encoding="utf-8") as f:
        f.write("")
    txt = open(lines, encoding="utf-8").read().replace("Radio check, over.", "Radio check. Over.")
    open(lines, "w", encoding="utf-8").write(txt)
    before = STATE["tts"]
    run()
    check(STATE["tts"] - before == 1, f"one changed line re-voiced ({STATE['tts'] - before})")

    # Out of credits mid-call: stops cleanly and keeps what it has.
    STATE["limit"] = int(STATE["used"]) + 3
    txt = txt.replace("Readings when you have them.", "Readings whenever you have them, Kestrel.")
    open(lines, "w", encoding="utf-8").write(txt)
    out = run()
    check("stopping before D2_AM" in out or "out of credits" in out, "stops when the quota runs out")
    check(json.load(open(g.MANIFEST))["lines"].get("D1_ARRIVE_01"), "the manifest survives")

    # A wrong key fails loudly, without echoing the key.
    os.environ["ELEVENLABS_API_KEY"] = "wrong"
    out = run("--plan")
    check("401" in out or "HTTP" in out or "exit" in out, "a bad key is reported")
    check(STATE["bad_key"] > 0, "the bad key was rejected by the server")

    # An offline plan is useful before creating a key and never hits TTS.
    del os.environ["ELEVENLABS_API_KEY"]
    before = STATE["tts"]
    out = run("--plan")
    check("Offline plan" in out and "5 lines" in out, "offline planning works without a key")
    check(STATE["tts"] == before, "offline planning spends nothing")

    # A typo must not silently select no calls and appear to succeed.
    out = run("--calls", "D1_ARRIVEE", "--plan")
    check("exit 2" in out, "unknown call IDs are rejected")

    # Package only the game recordings and attribution, never local state or temporary files.
    open(os.path.join(g.OUT, "unfinished.pcm.tmp"), "w").write("partial")
    open(os.path.join(g.OUT, "private.json"), "w").write("not for the card")
    packed = g.pack_voices(os.path.join(tmp, "voices.zip"))
    with zipfile.ZipFile(packed) as z:
        check(len([n for n in z.namelist() if n.endswith('.pcm')]) == 5, "all five recordings are packaged")
        check(all(n.startswith('apps/lonelookout/vo/') for n in z.namelist()), "zip extracts to the SD-card layout")
        check(not any(n.endswith('.tmp') or n.endswith('.json') for n in z.namelist()), "zip excludes temporary and local files")

    # Samples respect the same explicit spending limit as full generation.
    os.environ["ELEVENLABS_API_KEY"] = KEY
    STATE["limit"] = 10000
    before = STATE["tts"]
    out = run("--sample", "--budget", "1")
    check("budget" in out and STATE["tts"] == before, "sample budget is checked before generation")

    # A fresh checkout can plan voices with just Python's standard library.
    fresh = os.path.join(tmp, "fresh")
    for rel in ("tools/voice", "tools/dialogue", "tools/font", "data/dialogue", "data"):
        os.makedirs(os.path.join(fresh, rel), exist_ok=True)
    for rel in ("tools/voice/generate_vo.py", "tools/dialogue/compile_lls.py", "tools/font/lookout_hand.py", "data/voices.json"):
        shutil.copyfile(os.path.join(g.ROOT, rel), os.path.join(fresh, rel))
    for name in os.listdir(os.path.join(g.ROOT, "data", "dialogue")):
        if name.endswith(".lls"):
            shutil.copyfile(os.path.join(g.ROOT, "data", "dialogue", name), os.path.join(fresh, "data", "dialogue", name))
    env = dict(os.environ)
    env.pop("ELEVENLABS_API_KEY", None)
    p = subprocess.run([sys.executable, "-S", "tools/voice/generate_vo.py", "--plan"], cwd=fresh, env=env,
                       capture_output=True, text=True, check=False)
    check(p.returncode == 0 and "505 lines" in p.stdout, f"fresh checkout plans without a build or key: {p.stderr[:300]}")
    check(os.path.isfile(os.path.join(fresh, "gen", "vo_lines.txt")), "voice tool prepares its own dialogue list")
    check(KEY not in p.stdout + p.stderr, "fresh-checkout output contains no key")

    srv.shutdown()
    print(f"{checks} checks, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
