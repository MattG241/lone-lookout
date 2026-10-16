#!/usr/bin/env python3
"""
generate_vo.py -- voice the radio script with ElevenLabs text to speech.

Reads the line list the dialogue compiler writes (gen/vo_lines.txt), casts each
speaker from data/voices.json, and writes one <LINE_ID>.pcm per line -- raw
16-bit little-endian mono at 22,050 Hz, the format the game's SD-card loader
reads (src/audio/vo.h) -- into voices/apps/lonelookout/vo/. Copy that folder
to the SD card (or use the voices zip `make package` builds from it).

    export ELEVENLABS_API_KEY=...          # never commit it, never print it
    python3 tools/voice/generate_vo.py --plan          # what it would voice, and the cost
    python3 tools/voice/generate_vo.py --list-voices   # the voices your account can use
    python3 tools/voice/generate_vo.py --sample        # a few lines per speaker, as WAVs to listen to
    python3 tools/voice/generate_vo.py                 # voice as much as the quota allows

It voices whole calls in story order (Day 1 first, the check-ins last) and
stops before a call it can't finish, so the game never switches between a
recorded voice and the murmur inside one conversation. Lines already voiced
are skipped unless their text or casting changed, so on a free plan you can
run it again next month to carry on. Lines that read out live values
(readings, wind) aren't voiced: they keep the murmur.

Only the Python standard library is needed (ffmpeg is used only if the API
can't return PCM and falls back to MP3).
"""
import argparse
import array
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
API = "https://api.elevenlabs.io"
RATE = 22050
LINES = os.path.join(ROOT, "gen", "vo_lines.txt")
CASTING = os.path.join(ROOT, "data", "voices.json")
OUT = os.path.join(ROOT, "voices", "apps", "lonelookout", "vo")
MANIFEST = os.path.join(ROOT, "voices", "manifest.json")
MAX_SECONDS = 30              # the game's per-line buffer (VO_MAX_SECONDS in src/audio/vo.c)


# --------------------------------------------------------------------------
# The script
# --------------------------------------------------------------------------

def read_lines(path=None):
    """[(line_id, speaker, text)] in script order, without the LIVE lines."""
    out = []
    with open(path or LINES, encoding="utf-8") as f:
        for raw in f:
            if raw.startswith("#") or not raw.strip():
                continue
            parts = raw.rstrip("\n").split("\t")
            if len(parts) < 3:
                continue
            lid, spk, text = parts[0], parts[1], parts[2]
            if text.startswith("LIVE ") or "{" in text:
                continue
            out.append((lid, spk, text))
    return out


def call_of(line_id):
    return re.sub(r"_\d+$", "", line_id)


def call_order_key(call):
    """Story calls by day in script order, then the check-ins."""
    m = re.match(r"D(\d+)_", call)
    return (0, int(m.group(1))) if m else (1, 0)


def group_calls(lines):
    calls, order = {}, []
    for i, (lid, spk, text) in enumerate(lines):
        c = call_of(lid)
        if c not in calls:
            calls[c] = []
            order.append((call_order_key(c), i, c))
        calls[c].append((lid, spk, text))
    return [(c, calls[c]) for _, _, c in sorted(order)]


def speakable(text):
    """The subtitle text as something to say: stage directions and brackets out."""
    t = re.sub(r"\[[^\]]*\]", "", text)
    t = re.sub(r"\s*(—|--)\s*", ", ", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t


# --------------------------------------------------------------------------
# The API
# --------------------------------------------------------------------------

class ApiError(Exception):
    def __init__(self, status, body):
        super().__init__(f"HTTP {status}: {body[:300]}")
        self.status, self.body = status, body


def api(key, method, path, body=None, accept="application/json", query=None):
    url = API + path + ("?" + urllib.parse.urlencode(query) if query else "")
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("xi-api-key", key)
    req.add_header("Accept", accept)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    for attempt in range(6):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            text = e.read().decode("utf-8", "replace")
            if e.code == 429 or e.code >= 500:          # busy or rate-limited: wait and retry
                time.sleep(min(60, 2 ** attempt * 2))
                continue
            raise ApiError(e.code, text) from None
        except urllib.error.URLError as e:
            if attempt == 5:
                raise
            time.sleep(2 ** attempt)
    raise ApiError(429, "still rate-limited after retries")


def get_json(key, path):
    return json.loads(api(key, "GET", path))


def quota(key):
    """(credits used, credit limit, reset time) for this billing period."""
    s = get_json(key, "/v1/user/subscription")
    return s.get("character_count", 0), s.get("character_limit", 0), s.get("next_character_count_reset_unix")


def model_rate(key, model_id):
    """Credits per character for a model (1.0 if the API doesn't say)."""
    try:
        for m in json.loads(api(key, "GET", "/v1/models")):
            if m.get("model_id") == model_id:
                rates = m.get("model_rates") or {}
                return float(rates.get("character_cost_multiplier", 1.0))
    except (ApiError, ValueError):
        pass
    return 1.0


def list_voices(key):
    return get_json(key, "/v1/voices").get("voices", [])


def cast(key, casting):
    """{speaker: (voice_id, voice_name, settings)} from the first available voice."""
    voices = list_voices(key)
    by_name = {v["name"].split(" ")[0].lower(): v for v in voices}
    by_full = {v["name"].lower(): v for v in voices}
    by_id = {v["voice_id"]: v for v in voices}
    out = {}
    for spk, spec in casting["speakers"].items():
        pick = None
        for want in spec["voices"]:
            pick = by_id.get(want) or by_full.get(want.lower()) or by_name.get(want.lower())
            if pick:
                break
        if not pick:
            raise SystemExit(f"none of {spec['voices']} for {spk} is on this account: "
                             f"edit data/voices.json (see --list-voices)")
        out[spk] = (pick["voice_id"], pick["name"], spec.get("settings", {}))
    return out


def synthesize(key, voice_id, model, settings, text, prev_text, next_text, seed):
    body = {
        "text": text,
        "model_id": model,
        "voice_settings": {"use_speaker_boost": True, **settings},
        "seed": seed,
    }
    if prev_text:
        body["previous_text"] = prev_text
    if next_text:
        body["next_text"] = next_text
    path = f"/v1/text-to-speech/{voice_id}"
    try:
        raw = api(key, "POST", path, body, accept="audio/*", query={"output_format": f"pcm_{RATE}"})
        return array.array("h", raw if len(raw) % 2 == 0 else raw[:-1])
    except ApiError as e:
        if e.status not in (400, 403, 422) or "format" not in e.body.lower():
            raise
    # This plan can't have PCM: take MP3 and decode it with ffmpeg.
    mp3 = api(key, "POST", path, body, accept="audio/mpeg", query={"output_format": "mp3_22050_32"})
    if not shutil.which("ffmpeg"):
        raise SystemExit("the API returned MP3 and ffmpeg isn't installed to decode it")
    pcm = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", "pipe:0", "-f", "s16le", "-ac", "1",
                          "-ar", str(RATE), "pipe:1"], input=mp3, capture_output=True, check=True).stdout
    return array.array("h", pcm)


# --------------------------------------------------------------------------
# Audio
# --------------------------------------------------------------------------

def finish(samples):
    """Trim silence (keeping 80 ms either side), peak to -3 dBFS, cap at 30 s.
    Samples are native-endian host shorts here; written little-endian."""
    if sys.byteorder != "little":
        samples.byteswap()
    n = len(samples)
    if n == 0:
        return samples
    thresh = 400
    first = next((i for i in range(n) if abs(samples[i]) > thresh), 0)
    last = next((i for i in range(n - 1, -1, -1) if abs(samples[i]) > thresh), n - 1)
    pad = RATE * 80 // 1000
    s = samples[max(0, first - pad):min(n, last + pad)]
    peak = max(1, max(abs(v) for v in s))
    gain = min(4.0, 23197.0 / peak)                    # -3 dBFS, never more than +12 dB
    s = array.array("h", (max(-32768, min(32767, int(v * gain))) for v in s))
    fade = min(len(s) // 2, RATE * 10 // 1000)        # 10 ms fades: no clicks at the cuts
    for i in range(fade):
        s[i] = int(s[i] * i / fade)
        s[-1 - i] = int(s[-1 - i] * i / fade)
    return s[:RATE * MAX_SECONDS]


def write_pcm(path, s):
    out = array.array("h", s)
    if sys.byteorder != "little":
        out.byteswap()
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        out.tofile(f)
    os.replace(tmp, path)


def write_wav(path, s):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        out = array.array("h", s)
        if sys.byteorder != "little":
            out.byteswap()
        w.writeframes(out.tobytes())


# --------------------------------------------------------------------------

def line_hash(text, speaker_cast, model):
    vid, _, settings = speaker_cast
    key = json.dumps([text, vid, settings, model], sort_keys=True)
    return hashlib.sha1(key.encode()).hexdigest()[:16]


def load_manifest():
    try:
        with open(MANIFEST, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"lines": {}}


def save_manifest(m):
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    tmp = MANIFEST + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(m, f, indent=1, sort_keys=True)
    os.replace(tmp, MANIFEST)


def plan(calls, manifest, casting_hash):
    """Calls still to voice, in order, with their characters."""
    todo = []
    for call, lines in calls:
        missing = [l for l in lines
                   if manifest["lines"].get(l[0], {}).get("hash") != casting_hash(l)
                   or not os.path.exists(os.path.join(OUT, l[0] + ".pcm"))]
        if missing:
            todo.append((call, lines, sum(len(speakable(t)) for _, _, t in lines)))
    return todo


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan", action="store_true", help="show what would be voiced and its cost, then stop")
    ap.add_argument("--list-voices", action="store_true", help="list the voices this account can use")
    ap.add_argument("--sample", action="store_true", help="voice a few lines per speaker as WAVs in voices/samples")
    ap.add_argument("--budget", type=int, default=0, help="spend at most this many credits (default: what's left)")
    ap.add_argument("--calls", nargs="*", help="only these calls (e.g. D7_SEVEN D11_LAST)")
    ap.add_argument("--model", help="override the model in data/voices.json")
    args = ap.parse_args()

    with open(CASTING, encoding="utf-8") as f:
        casting = json.load(f)
    model = args.model or casting.get("model", "eleven_flash_v2_5")
    calls = group_calls(read_lines())
    if args.calls:
        calls = [(c, l) for c, l in calls if c in set(args.calls)]
    total_chars = sum(len(speakable(t)) for _, ls in calls for _, _, t in ls)

    key = os.environ.get("ELEVENLABS_API_KEY", "").strip()
    if not key:
        n = sum(len(ls) for _, ls in calls)
        print(f"{n} lines in {len(calls)} calls, {total_chars} characters to voice.")
        raise SystemExit("ELEVENLABS_API_KEY isn't set: add it to the environment (never to the repo).")

    if args.list_voices:
        try:
            voices = list_voices(key)
        except ApiError as e:
            raise SystemExit(f"ElevenLabs: {e}")
        for v in sorted(voices, key=lambda v: v["name"]):
            labels = ", ".join(f"{k}: {v}" for k, v in (v.get("labels") or {}).items())
            print(f"{v['name']:<28} {v['voice_id']}  [{v.get('category', '')}] {labels}")
        return

    try:
        cast_map = cast(key, casting)
        used, limit, reset = quota(key)
    except ApiError as e:
        if e.status == 401:
            raise SystemExit("ElevenLabs rejected the API key (HTTP 401): check ELEVENLABS_API_KEY "
                             "and that the key is allowed to use text to speech, voices and user info.")
        raise SystemExit(f"ElevenLabs: {e}")
    for spk, (vid, name, _) in cast_map.items():
        print(f"{spk:<5} -> {name} ({vid})")
    rate = model_rate(key, model)
    left = max(0, limit - used)
    when = time.strftime("%d %b", time.gmtime(reset)) if reset else "?"
    print(f"model {model}: {rate:g} credits per character; quota {used:,} of {limit:,} used, "
          f"{left:,} left (resets {when})")

    if args.sample:
        os.makedirs(os.path.join(ROOT, "voices", "samples"), exist_ok=True)
        picks = {}
        for call, lines in calls:
            for lid, spk, text in lines:
                if spk in cast_map and len(picks.setdefault(spk, [])) < 3 and 40 < len(text) < 140:
                    picks[spk].append((lid, spk, text))
        cost = sum(len(speakable(t)) for ls in picks.values() for _, _, t in ls) * rate
        if cost > left:
            raise SystemExit(f"the samples need {cost:.0f} credits and {left} are left")
        for spk, ls in picks.items():
            for lid, _, text in ls:
                vid, _, settings = cast_map[spk]
                s = finish(synthesize(key, vid, model, settings, speakable(text), None, None, 1234))
                path = os.path.join(ROOT, "voices", "samples", f"{spk}_{lid}.wav")
                write_wav(path, s)
                print(f"  {path}  {len(s) / RATE:4.1f} s  {text}")
        return

    manifest = load_manifest()

    def h(line):
        return line_hash(speakable(line[2]), cast_map[line[1]], model)

    todo = plan(calls, manifest, h)
    need = sum(c for _, _, c in todo) * rate
    print(f"{len(todo)} calls to voice, about {need:,.0f} credits")
    if args.plan:
        for call, lines, chars in todo:
            print(f"  {call:<14} {len(lines):3d} lines {chars:5d} chars  ~{chars * rate:,.0f} credits")
        return

    budget = min(left, args.budget) if args.budget else left
    spent = 0.0
    done_calls = 0
    os.makedirs(OUT, exist_ok=True)
    # The game credits ElevenLabs when this file is on the card (the free plan asks for attribution).
    with open(os.path.join(OUT, "ELEVENLABS.txt"), "w", encoding="utf-8") as f:
        f.write("Voices made with ElevenLabs text to speech (elevenlabs.io).\n")
        f.write(f"Model: {model}\n")
        for spk, (vid, name, _) in cast_map.items():
            f.write(f"{spk}: {name}\n")
    for call, lines, chars in todo:
        cost = chars * rate
        if spent + cost > budget:
            print(f"stopping before {call}: it needs ~{cost:.0f} credits and ~{budget - spent:.0f} are left "
                  f"(run again when the quota resets)")
            break
        for i, (lid, spk, text) in enumerate(lines):
            if spk not in cast_map:
                continue
            line = (lid, spk, text)
            out = os.path.join(OUT, lid + ".pcm")
            if manifest["lines"].get(lid, {}).get("hash") == h(line) and os.path.exists(out):
                continue
            vid, _, settings = cast_map[spk]
            prev_t = speakable(lines[i - 1][2]) if i > 0 else None
            next_t = speakable(lines[i + 1][2]) if i + 1 < len(lines) else None
            seed = int(hashlib.sha1(lid.encode()).hexdigest()[:8], 16)
            try:
                s = finish(synthesize(key, vid, model, settings, speakable(text), prev_t, next_t, seed))
            except ApiError as e:
                if e.status in (401, 402) and "quota" in e.body.lower():
                    print(f"out of credits at {lid}; {done_calls} calls finished. Run again when the quota resets.")
                    save_manifest(manifest)
                    return
                raise
            write_pcm(out, s)
            manifest["lines"][lid] = {"hash": h(line), "seconds": round(len(s) / RATE, 2),
                                      "voice": cast_map[spk][1], "model": model}
            save_manifest(manifest)
        spent += cost
        done_calls += 1
        print(f"  {call:<14} {len(lines):3d} lines  (~{spent:,.0f} credits so far)")
    print(f"{done_calls} calls voiced into {os.path.relpath(OUT, ROOT)}/")


if __name__ == "__main__":
    main()
