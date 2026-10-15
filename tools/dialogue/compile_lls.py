#!/usr/bin/env python3
"""
compile_lls.py -- compile the radio script (data/dialogue/*.lls) into C tables.

The .lls format is a small plain-text screenplay (DESIGN.md section 11):

    # comment
    flag told_ear                  # (defs.lls) declare a story flag
    action errand                  # (defs.lls) declare a game action

    call D2_PM                     # one radio call
      day 2                        #   day, or a range: 2-11, or "any"
      time 18:00-20:30             #   window in which it may ring
      retry 21:00                  #   keep trying until (default: end of window)
      where area:ridge             #   any | area:<id> | cab | cabin | seat:<n>
                                   #   ("where cab until 17:00": anywhere after 17:00)
      needs fawns_seen, !told_ear  #   flags that must (not) be set
      after D2_AM                  #   calls that must be done
      kind story                   #   story | report | small | initiated | checkin
      garble                       #   wind eats the signal
    DOT  Kestrel, Basin. You got a minute?
    THEO Basin, Kestrel. I've got nothing but minutes.
    choice
      > "You listen for the wobble." -> wobble
      > "Readings: {dry}, {wet}." if obs_am set read_out -> read
      > [say nothing] -> quiet
    node wobble
    THEO Two strings that should be the same note...
    set asked_about_tuning
    branch told_ear -> told       # jump if a flag (or area:<id>) holds
    goto end_bit
    do errand spring              # call into the game
    end

Text may use {tokens} the game fills in (readings, bearings). Every
character must exist in the Lookout Hand font; the compiler checks.

Outputs: gen/dialogue_flags.h (flag/action/call ids, safe to include
anywhere) and gen/dialogue_data.h (the tables, included by dialogue.c).

Usage: python3 tools/dialogue/compile_lls.py data/dialogue gen
"""

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "font"))
import lookout_hand  # noqa: E402

SPEAKERS = {"DOT": 0, "THEO": 1, "CREW": 2}
KINDS = {"story": 0, "report": 1, "small": 2, "initiated": 3, "checkin": 4}
WHERE = {"any": 0, "area": 1, "cab": 2, "cabin": 3, "seat": 4}
AREAS = ["tower", "meadow", "tarn", "ridge", "burn", "cabin"]
OPS = ["LINE", "CHOICE", "OPTION", "GOTO", "BRANCH", "SET", "CLEAR", "DO", "END"]
COND_AREA = 1000          # branch/option conditions: flag+1, or COND_AREA + area; negative = not


class LlsError(Exception):
    pass


def parse_time(s, where):
    m = re.fullmatch(r"(\d{1,2}):(\d{2})", s.strip())
    if not m:
        raise LlsError(f"{where}: bad time '{s}'")
    return int(m.group(1)) * 60 + int(m.group(2))


class Compiler:
    def __init__(self):
        self.flags = {}
        self.actions = {}
        self.calls = []
        self.strings = []
        self.string_index = {}
        self.font = set(lookout_hand.slot_codes())

    # -- strings ------------------------------------------------------------
    def string(self, text, where):
        for ch in text:
            if ch != " " and ord(ch) not in self.font:
                raise LlsError(f"{where}: character {ch!r} (U+{ord(ch):04X}) is not in the Lookout Hand font")
        if text not in self.string_index:
            self.string_index[text] = len(self.strings)
            self.strings.append(text)
        return self.string_index[text]

    # -- conditions ---------------------------------------------------------
    def cond(self, token, where):
        neg = token.startswith("!")
        name = token[1:] if neg else token
        if name.startswith("area:"):
            a = name[5:]
            if a not in AREAS:
                raise LlsError(f"{where}: unknown area '{a}'")
            v = COND_AREA + AREAS.index(a)
        else:
            if name not in self.flags:
                raise LlsError(f"{where}: undeclared flag '{name}' (declare it in defs.lls)")
            v = self.flags[name] + 1
        return -v if neg else v

    def flag(self, name, where):
        if name not in self.flags:
            raise LlsError(f"{where}: undeclared flag '{name}' (declare it in defs.lls)")
        return self.flags[name]

    # -- parsing ------------------------------------------------------------
    def parse_file(self, path):
        with open(path, encoding="utf-8") as f:
            lines = f.read().split("\n")
        call = None
        for no, raw in enumerate(lines, 1):
            where = f"{os.path.basename(path)}:{no}"
            line = raw.rstrip()
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            word, _, rest = stripped.partition(" ")
            rest = rest.strip()
            if word in ("flag", "action", "day", "time", "retry", "where", "needs", "after", "kind", "garble"):
                rest = rest.split("#")[0].strip()      # trailing comments on declarations and headers
            if word == "flag":
                for name in rest.replace(",", " ").split():
                    self.flags.setdefault(name, len(self.flags))
                continue
            if word == "action":
                for name in rest.replace(",", " ").split():
                    self.actions.setdefault(name, len(self.actions))
                continue
            if word == "call":
                call = dict(id=rest, where=where, day=(1, 11), time=(0, 24 * 60), retry=None,
                            where_kind=0, where_arg=0, where_until=0xFFFF, needs=[], after=[], kind="story",
                            garble=False,
                            body=[], header=True)
                self.calls.append(call)
                continue
            if call is None:
                raise LlsError(f"{where}: '{word}' outside a call")
            if call["header"] and raw.startswith((" ", "\t")) and word in (
                    "day", "time", "retry", "where", "needs", "after", "kind", "garble"):
                self.header(call, word, rest, where)
                continue
            call["header"] = False
            call["body"].append((word, rest, where, raw))

    def header(self, call, word, rest, where):
        if word == "day":
            if rest == "any":
                call["day"] = (1, 11)
            elif "-" in rest:
                a, b = rest.split("-")
                call["day"] = (int(a), int(b))
            else:
                call["day"] = (int(rest), int(rest))
        elif word == "time":
            a, b = rest.split("-")
            call["time"] = (parse_time(a, where), parse_time(b, where))
        elif word == "retry":
            call["retry"] = parse_time(rest, where)
        elif word == "where":
            if " until " in rest:
                rest, _, until = rest.partition(" until ")
                call["where_until"] = parse_time(until, where)
                rest = rest.strip()
            kind, _, arg = rest.partition(":")
            if kind not in WHERE:
                raise LlsError(f"{where}: bad where '{rest}'")
            call["where_kind"] = WHERE[kind]
            if kind == "area":
                if arg not in AREAS:
                    raise LlsError(f"{where}: unknown area '{arg}'")
                call["where_arg"] = AREAS.index(arg)
            elif kind == "seat":
                call["where_arg"] = int(arg)
        elif word == "needs":
            call["needs"] = [t.strip() for t in rest.split(",") if t.strip()]
        elif word == "after":
            call["after"] = [t.strip() for t in rest.split(",") if t.strip()]
        elif word == "kind":
            if rest not in KINDS:
                raise LlsError(f"{where}: bad kind '{rest}'")
            call["kind"] = rest
        elif word == "garble":
            call["garble"] = True

    # -- code generation ----------------------------------------------------
    def assemble(self):
        ops = []
        call_index = {c["id"]: i for i, c in enumerate(self.calls)}
        if len(call_index) != len(self.calls):
            raise LlsError("duplicate call id")
        for c in self.calls:
            c["first"] = len(ops)
            labels, fixups = {}, []
            body = c["body"]
            i = 0
            while i < len(body):
                word, rest, where, raw = body[i]
                i += 1
                if word in SPEAKERS:
                    if not rest:
                        raise LlsError(f"{where}: empty line")
                    ops.append(dict(op="LINE", speaker=SPEAKERS[word], str=self.string(rest, where)))
                elif word == "choice":
                    opts = []
                    while i < len(body) and body[i][0] == ">":
                        _, orest, owhere, _ = body[i]
                        i += 1
                        m = re.fullmatch(r"(.+?)\s*->\s*(\w+)", orest)
                        if not m:
                            raise LlsError(f"{owhere}: option needs '-> node'")
                        text, target = m.group(1).strip(), m.group(2)
                        cond, setf = 0, -1
                        mm = re.fullmatch(r"(.*?)\s+set\s+(\w+)", text)
                        if mm:
                            text, setf = mm.group(1).strip(), self.flag(mm.group(2), owhere)
                        mm = re.fullmatch(r"(.*?)\s+if\s+(!?[\w:]+)", text)
                        if mm:
                            text, cond = mm.group(1).strip(), self.cond(mm.group(2), owhere)
                        opts.append((text, cond, setf, target, owhere))
                    if not opts or len(opts) > 4:
                        raise LlsError(f"{where}: a choice needs 1-4 options")
                    ops.append(dict(op="CHOICE", count=len(opts)))
                    for text, cond, setf, target, owhere in opts:
                        fixups.append((len(ops), target, owhere))
                        ops.append(dict(op="OPTION", str=self.string(text, owhere), cond=cond, set=setf))
                elif word == "node":
                    if rest in labels:
                        raise LlsError(f"{where}: node '{rest}' defined twice")
                    labels[rest] = len(ops)
                elif word == "goto":
                    fixups.append((len(ops), rest, where))
                    ops.append(dict(op="GOTO"))
                elif word == "branch":
                    m = re.fullmatch(r"(!?[\w:]+)\s*->\s*(\w+)", rest)
                    if not m:
                        raise LlsError(f"{where}: branch needs 'cond -> node'")
                    fixups.append((len(ops), m.group(2), where))
                    ops.append(dict(op="BRANCH", cond=self.cond(m.group(1), where)))
                elif word in ("set", "clear"):
                    ops.append(dict(op=word.upper(), set=self.flag(rest, where)))
                elif word == "do":
                    name, _, arg = rest.partition(" ")
                    if name not in self.actions:
                        raise LlsError(f"{where}: undeclared action '{name}'")
                    ops.append(dict(op="DO", act=self.actions[name], arg=self.string(arg.strip(), where) if arg else 0))
                elif word == "end":
                    ops.append(dict(op="END"))
                else:
                    raise LlsError(f"{where}: unknown statement '{word}'")
            ops.append(dict(op="END"))
            for at, target, where in fixups:
                if target not in labels:
                    raise LlsError(f"{where}: no node '{target}' in call {c['id']}")
                ops[at]["target"] = labels[target]
            # Conditions and ordering.
            c["needs_c"] = [self.cond(t, c["where"]) for t in c["needs"]]
            for a in c["after"]:
                if a not in call_index:
                    raise LlsError(f"{c['where']}: call {c['id']} is after unknown call '{a}'")
            c["after_c"] = [call_index[a] for a in c["after"]]
            if len(c["needs_c"]) > 4 or len(c["after_c"]) > 2:
                raise LlsError(f"{c['where']}: at most 4 needs and 2 afters")
        return ops

    def emit(self, out_dir):
        ops = self.assemble()

        def cstr(s):
            b = s.encode("utf-8")
            out = []
            for ch in b:
                if ch == 0x22:
                    out.append('\\"')
                elif ch == 0x5C:
                    out.append("\\\\")
                elif 32 <= ch < 127:
                    out.append(chr(ch))
                else:
                    out.append("\\%03o" % ch)
            return '"' + "".join(out) + '"'

        def ident(s):
            return re.sub(r"\W", "_", s).upper()

        flags_h = ["/* Generated by tools/dialogue/compile_lls.py -- do not edit. */",
                   "#ifndef LL_DIALOGUE_FLAGS_H", "#define LL_DIALOGUE_FLAGS_H", ""]
        for name, i in sorted(self.flags.items(), key=lambda kv: kv[1]):
            flags_h.append(f"#define FLAG_{ident(name)} {i}")
        flags_h.append(f"#define DLG_NUM_FLAGS {len(self.flags)}")
        flags_h.append("")
        for name, i in sorted(self.actions.items(), key=lambda kv: kv[1]):
            flags_h.append(f"#define ACT_{ident(name)} {i}")
        flags_h.append("")
        for i, c in enumerate(self.calls):
            flags_h.append(f"#define CALL_{ident(c['id'])} {i}")
        flags_h += [f"#define DLG_NUM_CALLS {len(self.calls)}", f"#define DLG_COND_AREA {COND_AREA}",
                    "", "#endif", ""]

        data_h = ["/* Generated by tools/dialogue/compile_lls.py -- do not edit. */",
                  "#ifndef LL_DIALOGUE_DATA_H", "#define LL_DIALOGUE_DATA_H", "",
                  '#include "dialogue_flags.h"', "",
                  f"#define DLG_NUM_OPS {len(ops)}", f"#define DLG_NUM_STRINGS {len(self.strings)}", "",
                  "static const char *const DLG_STRINGS[DLG_NUM_STRINGS] = {"]
        for s in self.strings:
            data_h.append(f"\t{cstr(s)},")
        data_h.append("};")
        data_h.append("")
        data_h.append("static const dlg_op DLG_OPS[DLG_NUM_OPS] = {")
        for o in ops:
            data_h.append("\t{ DLG_%s, %d, %d, %d, %d, %d, %d }," % (
                o["op"], o.get("speaker", o.get("count", o.get("act", 0))), o.get("cond", 0), o.get("set", -1),
                o.get("str", o.get("arg", 0)), o.get("target", 0), 0))
        data_h.append("};")
        data_h.append("")
        data_h.append("static const dlg_call DLG_CALLS[DLG_NUM_CALLS] = {")
        for c in self.calls:
            needs = (c["needs_c"] + [0, 0, 0, 0])[:4]
            after = (c["after_c"] + [-1, -1])[:2]
            t0, t1 = c["time"]
            retry = c["retry"] if c["retry"] is not None else t1
            data_h.append('\t{ "%s", %d, %d, %d, %d, %d, %d, %d, %d, %d, { %d, %d, %d, %d }, { %d, %d }, %d, %d },' % (
                c["id"], c["first"], c["day"][0], c["day"][1], t0, t1, retry, c["where_kind"], c["where_arg"],
                c["where_until"], *needs, *after, KINDS[c["kind"]], 1 if c["garble"] else 0))
        data_h.append("};")
        data_h += ["", "#endif", ""]

        # The voice-over manifest: every line's id (as dlg_line_id makes it), who says it, and the words.
        firsts = sorted(c["first"] for c in self.calls) + [len(ops)]
        names = {v: k for k, v in SPEAKERS.items()}
        vo = ["# Radio lines for voice-over. Record each as <ID>.pcm (16-bit little-endian mono, 22,050 Hz,",
              "# no header) in sd:/apps/lonelookout/vo/. Lines marked LIVE read out values and keep the murmur.",
              "# Generated by tools/dialogue/compile_lls.py.", ""]
        for c in self.calls:
            end = firsts[firsts.index(c["first"]) + 1]
            k = 0
            for pc in range(c["first"], end):
                o = ops[pc]
                if o["op"] != "LINE":
                    continue
                k += 1
                text = self.strings[o["str"]]
                live = "LIVE " if "{" in text else ""
                vo.append(f"{c['id']}_{k:02d}\t{names.get(o['speaker'], '?')}\t{live}{text}")
        self.vo_manifest = "\n".join(vo) + "\n"
        return "\n".join(flags_h), "\n".join(data_h), dict(calls=len(self.calls), ops=len(ops),
                                                            strings=len(self.strings),
                                                            words=sum(len(s.split()) for s in self.strings),
                                                            vo=self.vo_manifest)


def compile_dir(src_dir):
    c = Compiler()
    files = sorted(glob.glob(os.path.join(src_dir, "*.lls")))
    defs = [f for f in files if os.path.basename(f) == "defs.lls"]
    for path in defs + [f for f in files if f not in defs]:
        c.parse_file(path)
    return c.emit(None)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "data/dialogue"
    out = sys.argv[2] if len(sys.argv) > 2 else "gen"
    try:
        flags_h, data_h, stats = compile_dir(src)
    except LlsError as e:
        print(f"compile_lls: {e}", file=sys.stderr)
        sys.exit(1)
    os.makedirs(out, exist_ok=True)
    for name, text in (("dialogue_flags.h", flags_h), ("dialogue_data.h", data_h)):
        with open(os.path.join(out, name), "w") as f:
            f.write(text)
    print(f"dialogue: {stats['calls']} calls, {stats['ops']} ops, {stats['strings']} strings, ~{stats['words']} words")


if __name__ == "__main__":
    main()
