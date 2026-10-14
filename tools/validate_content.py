#!/usr/bin/env python3
"""FIGHTO static content validator.

Checks every text script the game interprets at run time, using the same parsing rules as the engine:

  assets/missions/missions.txt    mission index            src/game/mission.cpp   missions_init()
  assets/missions/*.mis           mission scripts          src/game/mission.cpp   load_mission_file(), exec()
  assets/missions/cutscenes.cut   cutscenes                src/game/cutscene.cpp  cutscenes_load()
  assets/dialogue/index.txt       dialogue file index      src/game/dialogue.cpp  dialogue_load_all()
  assets/dialogue/*.dlg           conversations            src/game/dialogue.cpp  load_file()
  assets/dialogue/evidence.txt    journal evidence texts   src/ui/menu.cpp        load_evidence()

All three script formats share the tokenizer in src/game/script.cpp: one line at a time (cut at
ScriptLine::buf - 1 bytes), at most SCRIPT_MAX_ARGS tokens (extra tokens are dropped silently),
whitespace separated, "double quoted" strings with no escapes, and '#' starting a comment only at the
start of a token.

Name tables, limits and buffer sizes are read from the C++ sources on every run, so the validator
follows engine changes (a built-in copy is used, and a NOTE printed, if a table cannot be found).
Per-command argument layouts are encoded in CMD_SIGS below and cross-checked against the commands
exec() really handles.

Severity: ERROR   content the engine ignores, truncates, misreads or cannot resolve.
          WARNING legal but almost certainly unintended (silent fallbacks, unused content, ...).
          NOTE    information.

Usage:   python3 -I tools/validate_content.py [--root DIR] [--quiet] [--werror]
Exit:    1 if there is any ERROR (or any WARNING with --werror), otherwise 0.
Python 3 standard library only.
"""

import argparse
import difflib
import os
import re
import sys

# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------
SEV_ORDER = {"ERROR": 0, "WARNING": 1, "NOTE": 2}


class Report:
    def __init__(self):
        self.items = []

    def add(self, sev, path, line, msg):
        self.items.append((sev, path or "", line or 0, msg))

    def error(self, path, line, msg):
        self.add("ERROR", path, line, msg)

    def warning(self, path, line, msg):
        self.add("WARNING", path, line, msg)

    def note(self, path, line, msg):
        self.add("NOTE", path, line, msg)

    def count(self, sev):
        return sum(1 for it in self.items if it[0] == sev)

    def render(self, quiet):
        out = []
        seen = set()
        for sev, path, line, msg in sorted(self.items, key=lambda it: (it[1], it[2], SEV_ORDER[it[0]], it[3])):
            if quiet and sev == "NOTE":
                continue
            key = (sev, path, line, msg)
            if key in seen:
                continue
            seen.add(key)
            if path and line:
                out.append("%s:%d: %s: %s" % (path, line, sev, msg))
            elif path:
                out.append("%s: %s: %s" % (path, sev, msg))
            else:
                out.append("%s: %s" % (sev, msg))
        return out


def suggest(name, choices):
    m = difflib.get_close_matches(name, list(choices), n=1, cutoff=0.6)
    return " (did you mean '%s'?)" % m[0] if m else ""


def short(s, n=40):
    return s if len(s) <= n else s[: n - 3] + "..."


def blen(s):
    """Length in bytes, as the C code sees it."""
    return len(s.encode("utf-8", "surrogateescape"))


def read_lines(path):
    """Physical lines exactly as the C loaders see them: split on '\\n' only ('\\r' is kept)."""
    with open(path, "rb") as fh:
        text = fh.read().decode("utf-8", "surrogateescape")
    parts = text.split("\n")
    if parts and parts[-1] == "":
        parts.pop()
    return parts


def truncate_bytes(s, nbytes):
    return s.encode("utf-8", "surrogateescape")[:nbytes].decode("utf-8", "surrogateescape")


# ---------------------------------------------------------------------------
# C++ source scraping
# ---------------------------------------------------------------------------
def strip_cpp_comments(src):
    """Blank out // and /* */ comments, keeping string literals and line numbers intact."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in src[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(src[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def match_brace(src, open_idx):
    depth, i, n = 0, open_idx, len(src)
    while i < n:
        c = src[i]
        if c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def braced_after(src, pattern):
    """Text of the first {...} block that directly follows a match of `pattern` (definitions, not declarations)."""
    if not src:
        return None
    for m in re.finditer(pattern, src):
        j = m.end()
        while j < len(src) and src[j] in " \t\r\n":
            j += 1
        if j < len(src) and src[j] == "{":
            e = match_brace(src, j)
            if e > 0:
                return src[j : e + 1]
    return None


def literals(s):
    return [x.replace('\\"', '"') for x in re.findall(r'"((?:[^"\\\n]|\\.)*)"', s or "")]


def uniq(seq):
    out = []
    for x in seq:
        if x not in out:
            out.append(x)
    return out


def str_eq_args(body, var):
    """All "x" in str_eq(var, "x") inside `body`."""
    if not body:
        return []
    return uniq(re.findall(r"str_eq\(\s*" + var + r'\s*,\s*"([^"]*)"\s*\)', body))


def array_literal(src, name):
    body = braced_after(src, name + r"\s*\[[^\]]*\]\s*=")
    return literals(body) if body else []


def table_in_func(src, func):
    body = braced_after(src, r"static\s+int\s+" + func + r"\s*\([^)]*\)")
    return array_literal(body, r"names") if body else []


def define(src, name):
    m = re.search(r"#define\s+" + name + r"\s+(\d+)", src or "")
    return int(m.group(1)) if m else None


def struct_char(src, struct, field):
    """Size of the last dimension of `char field[...]` inside `struct Name { ... }`."""
    body = braced_after(src, r"\bstruct\s+" + struct + r"\b")
    if not body:
        return None
    m = re.search(r"\bchar\s+" + field + r"((?:\s*\[\s*\w+\s*\])+)\s*[;,]", body)
    if not m:
        return None
    dims = re.findall(r"\[\s*(\w+)\s*\]", m.group(1))
    return int(dims[-1]) if dims and dims[-1].isdigit() else define(src, dims[-1]) if dims else None


def local_char(body, name):
    m = re.search(r"\bchar\s+" + name + r"\s*\[\s*(\d+)\s*\]", body or "")
    return int(m.group(1)) if m else None


FALLBACK = {
    "script_max_args": 16,
    "script_buf": 640,
    "max_missions": 32,
    "max_cmds": 220,
    "landmarks": ["home", "diner", "laundromat", "pawn", "safehouse", "bus_stop", "police", "hospital", "plaza",
                  "meridian", "fight_pit", "chop_shop", "warehouse", "substation", "nexus", "drain_hatch", "mansion",
                  "car_lot", "aegis_lobby", "aegis_briefing", "aegis_lab", "aegis_range", "aegis_archive",
                  "aegis_reactor", "nest"],
    "kinds": ["player", "civilian", "thug", "gunman", "fighter", "police", "aegis", "operative", "stalker", "brute",
              "tendril", "overseer", "drone", "story"],
    "weapons": ["fists", "pistol", "shotgun", "smg", "rifle", "magnum", "arc", "lance", "shard", "claws", "drone"],
    "factions": ["player", "civilian", "police", "kings", "varga", "aegis", "operatives", "alien", "fighter"],
    "vtypes": ["sedan", "muscle", "suv", "sports", "van", "police", "aegis", "taxi"],
    "modes": ["hold", "stand", "guard", "patrol", "wander", "follow", "talk", "spectate", "hostile", "combat", "cower"],
    "music": ["calm", "tension", "combat", "alien", "chase"],
    "sounds": ["screech", "explosion", "cheer", "elevator", "thunder"],
    "headers": ["mission", "key", "title", "desc", "start", "requires", "requires_phase", "reward", "phase_after",
                "repeatable", "side", "hidden"],
    "fx": ["flash", "shake", "shard", "explosion", "screech", "fade", "lantern"],
    "speakers": ["damian", "marcus", "eli", "tommy", "varga", "kincaid", "thorne", "okafor", "radio", "cop",
                 "announcer", "brick", "operative", "overseer", "tech", "soldier", "civilian", "thug", "news",
                 "shard", "sal", "ace", "maya"],
    "presets": ["damian", "marcus", "eli", "tommy", "fighter", "brick", "kings", "varga_thug", "varga", "police",
                "aegis", "operative", "kincaid", "thorne", "okafor", "scientist", "stalker", "brute", "tendril",
                "overseer"],
    "kind_presets": ["damian", "civilian", "kings", "fighter", "police", "aegis", "operative", "stalker", "brute",
                     "tendril", "overseer"],
    "max_evidence": 16,
    "max_flags": 64,
    "give_weapon_min": 0,
    "phase_range": (0, 3),
    "initial_phase": 0,
    "wanted_range": (0, 5),
    "line_duration": (1.4, 0.052),
    "font_range": (32, 127),
    "ev_append_limit": 690,
    # buffer sizes (bytes, including the terminating NUL)
    "key": 24, "title": 64, "desc": 220, "startLm": 24,
    "objective": 160, "choicePrompt": 128, "choiceOpt": 80, "choiceLabel": 32, "failReason": 96, "protect": 16,
    "choice_fname": 48, "talk_prompt": 80, "mission_path": 128,
    "actor_tag": 16, "actor_name": 24, "vehicle_tag": 16, "pickup_tag": 16, "flag_name": 24,
    "notify": 128, "interact": 80,
    "shot_speaker": 16, "shot_line": 240, "shot_fx": 16, "cut_id": 40,
    "dline_speaker": 16, "dline_text": 240, "conv_id": 40, "dlg_queue": 24, "dlg_line": 512, "dlg_path": 128,
    "ev_title": 64, "ev_body": 700, "ev_line": 512,
}


class Facts:
    """Everything the validator needs to know about the engine, scraped from the C++ sources."""

    def __init__(self, root, rep):
        self.root = root
        self.rep = rep
        self.fallbacks = []
        S = {}
        for rel in ("src/game/script.h", "src/game/mission.cpp", "src/game/cutscene.cpp", "src/game/dialogue.cpp",
                    "src/game/game.h", "src/ui/menu.cpp", "src/game/character.cpp", "src/game/actors.cpp",
                    "src/game/game.cpp", "src/game/police.cpp", "tools/bake_font.py"):
            p = os.path.join(root, rel)
            try:
                with open(p, encoding="utf-8", errors="replace") as fh:
                    txt = fh.read()
                S[rel] = strip_cpp_comments(txt) if not rel.endswith(".py") else txt
            except OSError:
                S[rel] = ""
                rep.note(rel, 0, "source file not found; using the validator's built-in copy of its rules")
        sh, mi, cu, dl, gh = (S["src/game/script.h"], S["src/game/mission.cpp"], S["src/game/cutscene.cpp"],
                              S["src/game/dialogue.cpp"], S["src/game/game.h"])
        me, ch, ac, gc, po = (S["src/ui/menu.cpp"], S["src/game/character.cpp"], S["src/game/actors.cpp"],
                              S["src/game/game.cpp"], S["src/game/police.cpp"])
        exec_body = braced_after(mi, r"static\s+bool\s+exec\s*\([^)]*\)")
        load_body = braced_after(mi, r"static\s+bool\s+load_mission_file\s*\([^)]*\)")
        self.exec_body = exec_body
        g = self._get

        self.script_max_args = g("script_max_args", define(sh, "SCRIPT_MAX_ARGS"), "src/game/script.h")
        self.script_buf = g("script_buf", struct_char(sh, "ScriptLine", "buf"), "src/game/script.h")
        self.max_missions = g("max_missions", define(mi, "MAX_MISSIONS"), "src/game/mission.cpp")
        self.max_cmds = g("max_cmds", define(mi, "MAX_CMDS"), "src/game/mission.cpp")
        self.landmarks = g("landmarks", table_in_func(mi, "landmark_by_name"), "src/game/mission.cpp")
        self.kinds = g("kinds", table_in_func(mi, "kind_by_name"), "src/game/mission.cpp")
        self.weapons = g("weapons", table_in_func(mi, "weapon_by_name"), "src/game/mission.cpp")
        self.factions = g("factions", table_in_func(mi, "faction_by_name"), "src/game/mission.cpp")
        self.vtypes = g("vtypes", table_in_func(mi, "vtype_by_name"), "src/game/mission.cpp")
        self.modes = g("modes", str_eq_args(braced_after(mi, r"static\s+void\s+apply_mode\s*\([^)]*\)"), "mode"),
                       "src/game/mission.cpp")
        self.music = g("music", str_eq_args(exec_body, "m"), "src/game/mission.cpp")
        self.sounds = g("sounds", str_eq_args(exec_body, "s"), "src/game/mission.cpp")
        self.headers = g("headers", str_eq_args(load_body, "op"), "src/game/mission.cpp")
        self.exec_cmds = str_eq_args(exec_body, "op")
        if not self.exec_cmds:
            self.fallbacks.append("exec() commands (src/game/mission.cpp)")
            self.exec_cmds = list(CMD_SIGS)
        m = re.search(r'"give_weapon"\s*\)\s*\)\s*\{[^}]*?if\s*\(\s*w\s*>\s*(-?\d+)\s*\)', exec_body or "")
        self.give_weapon_min = g("give_weapon_min", int(m.group(1)) if m else None, "src/game/mission.cpp")
        for f in ("key", "title", "desc", "startLm"):
            setattr(self, "sz_" + f, g(f, struct_char(mi, "MissionDef", f), "src/game/mission.cpp"))
        for f in ("objective", "choicePrompt", "choiceOpt", "choiceLabel", "failReason", "protect"):
            setattr(self, "sz_" + f, g(f, struct_char(mi, "MissionRun", f), "src/game/mission.cpp"))
        self.sz_choice_fname = g("choice_fname", local_char(exec_body, "fname"), "src/game/mission.cpp")
        self.sz_talk_prompt = g("talk_prompt", local_char(exec_body, "prompt"), "src/game/mission.cpp")
        self.sz_mission_path = g("mission_path", local_char(load_body, "path"), "src/game/mission.cpp")

        self.cut_landmarks = g("landmarks", array_literal(cu, r"g_lmNames"), "src/game/cutscene.cpp")
        self.fx = g("fx", str_eq_args(cu, r"s\.fx"), "src/game/cutscene.cpp")
        self.sz_shot_speaker = g("shot_speaker", struct_char(cu, "Shot", "speaker"), "src/game/cutscene.cpp")
        self.sz_shot_line = g("shot_line", struct_char(cu, "Shot", "line"), "src/game/cutscene.cpp")
        self.sz_shot_fx = g("shot_fx", struct_char(cu, "Shot", "fx"), "src/game/cutscene.cpp")
        self.sz_cut_id = g("cut_id", struct_char(cu, "Cutscene", "id"), "src/game/cutscene.cpp")

        spk_block = braced_after(dl, r"g_speakers\s*\[[^\]]*\]\s*=")
        self.speakers = g("speakers", uniq(re.findall(r'\{\s*"([^"]*)"\s*,', spk_block or "")), "src/game/dialogue.cpp")
        self.sz_dline_speaker = g("dline_speaker", struct_char(dl, "DLine", "speaker"), "src/game/dialogue.cpp")
        self.sz_dline_text = g("dline_text", struct_char(dl, "DLine", "text"), "src/game/dialogue.cpp")
        self.sz_conv_id = g("conv_id", struct_char(dl, "Conversation", "id"), "src/game/dialogue.cpp")
        m = re.search(r"\bDLine\s+queue\s*\[\s*(\d+)\s*\]", dl)
        self.dlg_queue = g("dlg_queue", int(m.group(1)) if m else None, "src/game/dialogue.cpp")
        dl_load = braced_after(dl, r"static\s+bool\s+load_file\s*\([^)]*\)")
        self.sz_dlg_line = g("dlg_line", local_char(dl_load, "line"), "src/game/dialogue.cpp")
        self.sz_dlg_path = g("dlg_path", local_char(dl_load, "path"), "src/game/dialogue.cpp")
        m = re.search(r"line_duration\s*\([^)]*\)\s*\{\s*return\s+([\d.]+)f?\s*\+\s*\(float\)\s*strlen\(\s*\w+\s*\)"
                      r"\s*\*\s*([\d.]+)f?", dl)
        self.line_duration = g("line_duration", (float(m.group(1)), float(m.group(2))) if m else None,
                               "src/game/dialogue.cpp")

        self.max_evidence = g("max_evidence", define(gh, "MAX_EVIDENCE"), "src/game/game.h")
        self.max_flags = g("max_flags", define(gh, "MAX_FLAGS"), "src/game/game.h")
        self.sz_actor_tag = g("actor_tag", struct_char(gh, "Actor", "tag"), "src/game/game.h")
        self.sz_actor_name = g("actor_name", struct_char(gh, "Actor", "name"), "src/game/game.h")
        self.sz_vehicle_tag = g("vehicle_tag", struct_char(gh, "Vehicle", "tag"), "src/game/game.h")
        self.sz_pickup_tag = g("pickup_tag", struct_char(gh, "Pickup", "tag"), "src/game/game.h")
        self.sz_flag_name = g("flag_name", struct_char(gh, "StoryFlag", "name"), "src/game/game.h")
        self.sz_notify = g("notify", struct_char(gh, "Notification", "text"), "src/game/game.h")
        self.sz_interact = g("interact", struct_char(gh, "Interactable", "text"), "src/game/game.h")

        self.sz_ev_title = g("ev_title", struct_char(me, "EvidenceText", "title"), "src/ui/menu.cpp")
        self.sz_ev_body = g("ev_body", struct_char(me, "EvidenceText", "body"), "src/ui/menu.cpp")
        ev_load = braced_after(me, r"static\s+void\s+load_evidence\s*\([^)]*\)")
        self.sz_ev_line = g("ev_line", local_char(ev_load, "line"), "src/ui/menu.cpp")
        m = re.search(r"len\s*<\s*(\d+)\s*\)", ev_load or "")
        self.ev_append_limit = g("ev_append_limit", int(m.group(1)) if m else None, "src/ui/menu.cpp")

        self.presets = g("presets", str_eq_args(braced_after(ch, r"Appearance\s+appearance_preset\s*\([^)]*\)"),
                                                "name"), "src/game/character.cpp")
        kinds_block = braced_after(ac, r"g_kinds\s*\[[^\]]*\]\s*=")
        self.kind_presets = g("kind_presets", uniq(re.findall(r'"([^"]+)"\s*\}', kinds_block or "")),
                              "src/game/actors.cpp")

        gsp = braced_after(gc, r"void\s+game_set_phase\s*\([^)]*\)")
        m = re.search(r"clamp\w*\(\s*phase\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", gsp or "")
        self.phase_range = g("phase_range", (int(m.group(1)), int(m.group(2))) if m else None, "src/game/game.cpp")
        m = re.search(r"g\.pd\.phase\s*=\s*(\d+)\s*;", gc)
        self.initial_phase = g("initial_phase", int(m.group(1)) if m else None, "src/game/game.cpp")
        ws = braced_after(po, r"void\s+wanted_set\s*\([^)]*\)")
        m = re.search(r"clamp\w*\(\s*level\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", ws or "")
        self.wanted_range = g("wanted_range", (int(m.group(1)), int(m.group(2))) if m else None, "src/game/police.cpp")
        m = re.search(r"for\s+cp\s+in\s+range\(\s*(\d+)\s*,\s*(\d+)\s*\)", S["tools/bake_font.py"])
        self.font_range = g("font_range", (int(m.group(1)), int(m.group(2))) if m else None, "tools/bake_font.py")

        if self.landmarks != self.cut_landmarks:
            rep.note("src/game/cutscene.cpp", 0, "cutscene landmark table differs from mission.cpp landmark_by_name(): "
                     "%s" % sorted(set(self.landmarks) ^ set(self.cut_landmarks)))
        if self.fallbacks:
            rep.note("", 0, "could not read from the C++ sources, using built-in values for: " + ", ".join(self.fallbacks))
        self._scan_cpp_refs()

    def _get(self, key, value, src):
        if value is None or value == [] or value == "":
            self.fallbacks.append("%s (%s)" % (key, src))
            return FALLBACK[key]
        return value

    def _scan_cpp_refs(self):
        """Literal flag / dialogue / cutscene names used from C++ (autotest.cpp is test-only and ignored)."""
        self.cpp_flag_sets = {}   # name -> list of (rel, line, value or None)
        self.cpp_flag_gets = {}   # name -> list of (rel, line)
        self.cpp_dialogues = {}
        self.cpp_cutscenes = {}
        src_root = os.path.join(self.root, "src")
        for dirpath, _dirs, files in os.walk(src_root):
            for fn in sorted(files):
                if not fn.endswith((".cpp", ".h")) or fn.startswith("autotest"):
                    continue
                p = os.path.join(dirpath, fn)
                rel = os.path.relpath(p, self.root).replace(os.sep, "/")
                try:
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        txt = strip_cpp_comments(fh.read())
                except OSError:
                    continue

                def lineno(pos):
                    return txt.count("\n", 0, pos) + 1

                for m in re.finditer(r'\bflag_set\(\s*"([^"]+)"\s*,\s*([^)]*)\)', txt):
                    v = m.group(2).strip()
                    self.cpp_flag_sets.setdefault(m.group(1), []).append(
                        (rel, lineno(m.start()), int(v) if re.fullmatch(r"-?\d+", v) else None))
                for m in re.finditer(r'\bflag_get\(\s*"([^"]+)"\s*\)', txt):
                    self.cpp_flag_gets.setdefault(m.group(1), []).append((rel, lineno(m.start())))
                for m in re.finditer(r'\bdialogue_start\(\s*"([^"]+)"\s*\)', txt):
                    self.cpp_dialogues.setdefault(m.group(1), []).append((rel, lineno(m.start())))
                for m in re.finditer(r'\bcutscene_start\(\s*"([^"]+)"\s*\)', txt):
                    self.cpp_cutscenes.setdefault(m.group(1), []).append((rel, lineno(m.start())))


# ---------------------------------------------------------------------------
# Tokenizer (mirror of script_next_line in src/game/script.cpp)
# ---------------------------------------------------------------------------
WS = " \t\r"


def tokenize(s):
    """Tokens of one line with no token limit, plus tokenizer issues."""
    toks, issues = [], []
    q, n = 0, len(s)
    while q < n:
        while q < n and s[q] in WS:
            q += 1
        if q >= n or s[q] == "#":
            break
        if s[q] == '"':
            start = q + 1
            end = s.find('"', start)
            if end < 0:
                toks.append(s[start:])
                issues.append(("unterminated", s[q:]))
                break
            toks.append(s[start:end])
            q = end + 1
            if q < n and s[q] not in WS and s[q] != "#":
                issues.append(("glued", s[start - 1 : end + 1] + s[q : q + 10]))
        else:
            start = q
            while q < n and s[q] not in WS:
                q += 1
            tok = s[start:q]
            toks.append(tok)
            if '"' in tok:
                issues.append(("quote_in_token", tok))
            if q < n:
                q += 1
    return toks, issues


INT_RE = re.compile(r"^[+-]?\d+$")
FLOAT_RE = re.compile(r"^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$")


def c_int(s):
    """str_to_int() from src/core/base.cpp."""
    m = re.match(r"[ \t]*([+-]?)(\d*)", s)
    v = int(m.group(2)) if m.group(2) else 0
    return -v if m.group(1) == "-" else v


def c_float(s):
    """str_to_float() from src/core/base.cpp (approximately)."""
    m = re.match(r"[ \t]*([+-]?)(\d*)(?:\.(\d*))?(?:[eE]([+-]?\d*))?", s)
    txt = (m.group(2) or "0") + "." + (m.group(3) or "0")
    try:
        v = float(txt) * (10 ** int(m.group(4))) if m.group(4) not in (None, "", "+", "-") else float(txt)
    except (ValueError, OverflowError):
        v = 0.0
    return -v if m.group(1) == "-" else v


def fmt_num(v):
    return ("%g" % v) if isinstance(v, float) else str(v)


# ---------------------------------------------------------------------------
# Mission command signatures (derived from exec() in src/game/mission.cpp).
# Each argument: (type, optional, extra).  'pos' consumes 3 tokens (parse_pos: "@landmark dx dz",
# "P dx dz" or "x y z"); 'color' consumes 3 tokens or none.
# ---------------------------------------------------------------------------
R, O = False, True
CMD_SIGS = {
    "objective": [("text", R, "objective")],
    "label": [("label_def", R, None)],
    "jump": [("label_ref", R, None)],
    "flag": [("flag_name", R, None), ("int", R, "value")],
    "if_flag": [("flag_name", R, None), ("int", R, "value"), ("label_ref", R, None)],
    "checkpoint": [],
    "spawn": [("actor_def", R, None), ("kind", R, None), ("preset", R, None), ("pos", R, None), ("float", R, "yaw"),
              ("faction_opt", O, None), ("weapon_opt", O, None), ("mode_opt", O, None)],
    "group": [("actor_def", R, None), ("kind", R, None), ("preset", R, None), ("count", R, "count"), ("pos", R, None),
              ("float", R, "radius"), ("faction_opt", O, None), ("weapon_opt", O, None), ("mode_opt", O, None)],
    "hostile": [("actor", R, None)],
    "friendly": [("actor", R, None)],
    "nonlethal": [("actor", R, None)],
    "invulnerable": [("actor", R, None), ("bool", O, "0|1")],
    "mode": [("actor", R, None), ("mode", R, None)],
    "despawn": [("actor_or_vehicle", R, None)],
    "face_player": [("actor", R, None)],
    "name": [("actor", R, None), ("text", R, "actor_name")],
    "health": [("actor", R, None), ("float", R, "health")],
    "slay": [("actor", R, None)],
    "goto_ai": [("actor", R, None), ("pos", R, None), ("float", O, "speed")],
    "protect": [("protect", R, None)],
    "unprotect": [("unprotect", R, None)],
    "vehicle": [("vehicle_def", R, None), ("vtype", R, None), ("pos", R, None), ("float", R, "yaw"),
                ("color", O, None)],
    "lock_vehicle": [("vehicle", R, None), ("bool", R, "0|1")],
    "give_weapon": [("weapon_give", R, None), ("int", O, "ammo")],
    "give_money": [("int", R, "amount")],
    "reward": [("int", R, "amount")],
    "heal": [],
    "armor": [("float", R, "amount")],
    "rep": [("faction", R, None), ("int", R, "amount")],
    "wanted": [("wanted", R, None)],
    "clear_wanted": [],
    "phase": [("phase", R, None)],
    "power": [("bool", R, "0|1")],
    "rain": [("rain", R, None)],
    "music": [("music", R, None)],
    "notify": [("text", R, "notify")],
    "teleport": [("pos", R, None), ("float", O, "yaw")],
    "evidence": [("evidence", R, None), ("pos", R, None)],
    "fx_explosion": [("pos", R, None), ("float", O, "radius"), ("float", O, "damage"), ("int", O, "flag")],
    "fx_flash": [],
    "fx_shake": [("float", O, "amount")],
    "shard_glow": [("float", O, "amount")],
    "sound": [("sound", R, None)],
    "unlock_safehouse": [],
    "pit_win": [],
    "say": [("speaker", R, None), ("text", R, "say")],
    "timer": [("float", R, "seconds")],
    "timer_stop": [],
    "fail": [("text", O, "fail")],
    "end": [],
    "wait": [("float", R, "seconds")],
    "dialogue": [("dialogue_id", R, None)],
    "cutscene": [("cutscene_id", R, None)],
    "fade_out": [],
    "fade_in": [],
    "goto": [("pos", R, None), ("float", O, "radius"), ("text", O, "objective")],
    "drive_to": [("pos", R, None), ("float", O, "radius"), ("text", O, "objective")],
    "goto_actor": [("actor", R, None), ("float", O, "radius"), ("text", O, "objective")],
    "enter_vehicle": [("vehicle", R, None), ("text", O, "objective")],
    "exit_vehicle": [("text", O, "objective")],
    "talk": [("actor", R, None), ("text", O, "talk_prompt"), ("text", O, "objective")],
    "interact": [("pos", R, None), ("float", O, "radius"), ("text", O, "interact"), ("text", O, "objective")],
    "hold": [("pos", R, None), ("float", O, "radius"), ("float", O, "seconds"), ("text", O, "objective")],
    "pickup": [("pickup_def", R, None), ("pos", R, None), ("text", O, "objective")],
    "kill": [("actor", R, None), ("text", O, "objective")],
    "defeat": [("actor", R, None), ("text", O, "objective")],
    "kill_count": [("actor", R, None), ("count", R, "count"), ("text", O, "objective")],
    "survive": [("float", R, "seconds"), ("text", O, "objective")],
    "lose_wanted": [("text", O, "objective")],
    "choice": [("text", R, "choicePrompt"), ("text", R, "choiceOpt"), ("label_ref", R, "choice"),
               ("text", R, "choiceOpt"), ("label_ref", R, "choice")],
}

HEADER_SIGS = {
    "mission": [("int", R, "id")],
    "key": [("text", R, "key")],
    "title": [("text", R, "title")],
    "desc": [("text", R, "desc")],
    "start": [("landmark_bare", R, None), ("float", O, "dx"), ("float", O, "dz")],
    "requires": [("int", R, "mission id")],
    "requires_phase": [("phase", R, None)],
    "reward": [("int", R, "amount")],
    "phase_after": [("phase", R, None)],
    "repeatable": [],
    "side": [],
    "hidden": [],
}

TYPE_NAMES = {
    "label_def": "label", "label_ref": "label", "flag_name": "flag", "actor_def": "tag", "actor": "tag",
    "actor_or_vehicle": "tag", "protect": "tag", "unprotect": "tag", "vehicle_def": "tag", "vehicle": "tag",
    "pickup_def": "tag", "kind": "kind", "preset": "preset|-", "faction_opt": "faction", "weapon_opt": "weapon",
    "mode_opt": "mode", "mode": "mode", "vtype": "vehicle-type", "weapon_give": "weapon", "faction": "faction",
    "wanted": "level", "phase": "phase", "rain": "0..1", "music": "mood", "sound": "sound", "speaker": "speaker",
    "dialogue_id": "dialogue-id", "cutscene_id": "cutscene-id", "evidence": "evidence-id", "pos": "pos",
    "color": "r g b", "landmark_bare": "landmark",
}

TEXT_NAMES = {
    "objective": "objective text", "actor_name": "display name", "notify": "text", "say": "text",
    "fail": "failure reason", "interact": "prompt", "talk_prompt": "prompt", "choicePrompt": "prompt",
    "choiceOpt": "option text", "key": "key", "title": "title", "desc": "description",
}

# Blocking commands: what happens when the tag they wait on does not exist.
NO_TAG_EFFECT = {
    "kill": "completes immediately", "defeat": "completes immediately", "kill_count": "can never complete",
    "talk": "is skipped", "goto_actor": "is skipped", "enter_vehicle": "is skipped",
}
# Commands that fast_forward_to() re-executes on every retry and that are not idempotent.
NON_IDEMPOTENT = {"give_money": "adds the money again", "rep": "changes reputation again",
                  "pit_win": "counts another pit win"}


def usage(op, sig):
    parts = []
    for typ, opt, extra in sig:
        if typ in ("text",):
            name = '"%s"' % extra
        elif typ in ("int", "float", "count", "bool"):
            name = extra or typ
        else:
            name = TYPE_NAMES.get(typ, typ)
        parts.append("[%s]" % name if opt else "<%s>" % name)
    return " ".join([op] + parts)


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------
class Cmd:
    __slots__ = ("op", "args", "line", "pc")

    def __init__(self, op, args, line, pc):
        self.op, self.args, self.line, self.pc = op, args, line, pc


class Mission:
    def __init__(self, path, rel, index_line):
        self.path, self.rel, self.index_line = path, rel, index_line
        self.headers = {}           # key -> list of (line, args)
        self.first_cmd_line = None
        self.cmds = []
        self.ncmds = 0
        self.label_defs = {}        # name -> [lines]
        self.label_refs = []        # (name, line)
        self.actor_defs = {}        # tag -> first line
        self.vehicle_defs = {}
        self.pickup_defs = {}
        self.protects = {}          # tag -> first line
        self.tag_refs = []          # (kind, tag, line, op)
        self.flag_sets = []         # (name, value, line)
        self.flag_tests = []        # (name, value, line)
        self.dlg_refs = []          # (id, line)
        self.cut_refs = []
        self.evidence_refs = []     # (id, line)
        self.phase_cmds = []
        self.id = 0
        self.key = ""
        self.title = ""
        self.requires = 0
        self.requires_phase = 0
        self.phase_after = 0
        self.repeatable = self.side = self.hidden = False

    def hline(self, key):
        h = self.headers.get(key)
        return h[-1][0] if h else self.index_line


# ---------------------------------------------------------------------------
# Validator
# ---------------------------------------------------------------------------
class Validator:
    def __init__(self, root, rep):
        self.root = root
        self.r = rep
        self.f = Facts(root, rep)
        f = self.f
        self.caps = {
            "objective": f.sz_objective, "actor_name": f.sz_actor_name, "notify": f.sz_notify,
            "say": f.sz_dline_text, "fail": f.sz_failReason, "interact": f.sz_interact,
            "talk_prompt": min(f.sz_talk_prompt, f.sz_interact), "choicePrompt": f.sz_choicePrompt,
            "choiceOpt": f.sz_choiceOpt, "key": f.sz_key, "title": f.sz_title, "desc": f.sz_desc,
        }
        self.cap_src = {
            "objective": "MissionRun::objective", "actor_name": "Actor::name", "notify": "Notification::text",
            "say": "DLine::text", "fail": "MissionRun::failReason", "interact": "Interactable::text",
            "talk_prompt": "talk prompt buffer", "choicePrompt": "MissionRun::choicePrompt",
            "choiceOpt": "MissionRun::choiceOpt", "key": "MissionDef::key", "title": "MissionDef::title",
            "desc": "MissionDef::desc",
        }
        self.missions = []
        self.convs = {}       # id -> (rel, line, count)
        self.cutscenes = {}   # id -> (rel, line)
        self.evidence = {}    # id -> (rel, line)
        self.unlisted_convs = {}
        unknown_sigs = [c for c in f.exec_cmds if c not in CMD_SIGS and c not in f.headers]
        for c in unknown_sigs:
            rep.note("src/game/mission.cpp", 0, "exec() handles command '%s' but the validator has no argument "
                     "layout for it; its arguments are not checked (add it to CMD_SIGS)" % c)
        self.unknown_sig_cmds = set(unknown_sigs)

    # ----------------------------------------------------------- generic helpers
    def rel(self, path):
        return os.path.relpath(path, self.root).replace(os.sep, "/")

    def check_charset(self, rel, ln, text, what):
        lo, hi = self.f.font_range
        bad = sorted({c for c in text if not (lo <= ord(c) < hi)})
        if bad:
            shown = ", ".join("U+%04X" % ord(c) if ord(c) < 0xDC80 or ord(c) > 0xDCFF else "byte 0x%02X" % (ord(c) - 0xDC00)
                              for c in bad[:5])
            self.r.warning(rel, ln, "%s contains characters outside the font's glyph range %d..%d (%s): they "
                           "render as missing glyphs" % (what, lo, hi - 1, shown))

    def check_text(self, rel, ln, text, cap, what, src):
        if cap and blen(text) > cap - 1:
            self.r.error(rel, ln, "%s is %d bytes; %s holds %d, so it is cut to \"%s\"" % (
                what, blen(text), src, cap - 1, short(truncate_bytes(text, cap - 1)[-30:], 30)))
        self.check_charset(rel, ln, text, what)

    def script_lines(self, path, rel, overflow_hint=None):
        """Lines as script_next_line() returns them, after reporting tokenizer-level problems."""
        f = self.f
        cap = f.script_buf - 1
        out = []
        for ln, raw in enumerate(read_lines(path), 1):
            if blen(raw) > cap:
                self.r.error(rel, ln, "line is %d bytes but script_next_line() keeps only %d (ScriptLine::buf[%d]); "
                             "the rest is cut off" % (blen(raw), cap, f.script_buf))
            toks, issues = tokenize(raw)
            for kind, info in issues:
                if kind == "unterminated":
                    self.r.error(rel, ln, "unbalanced quote: the string %s runs to the end of the line" % short(info, 30))
                elif kind == "glued":
                    self.r.warning(rel, ln, "no whitespace after the closing quote in %s: the text after the "
                                   "quote becomes a separate token" % short(info, 40))
                else:
                    self.r.warning(rel, ln, "'\"' inside token '%s' does not start a string (quotes only open a "
                                   "string at the start of a token)" % short(info, 30))
            if not toks:
                continue
            if len(toks) > f.script_max_args:
                dropped = toks[f.script_max_args:]
                hint = overflow_hint(toks) if overflow_hint else ""
                self.r.error(rel, ln, "%d tokens but SCRIPT_MAX_ARGS is %d: the last %d token(s) (%s) are silently "
                             "dropped%s" % (len(toks), f.script_max_args, len(dropped),
                                            " ".join(short(t, 20) for t in dropped), hint))
            out.append((ln, toks))
        return out

    def num(self, rel, ln, ctx, tok, what, integer):
        ok = INT_RE.match(tok) if integer else FLOAT_RE.match(tok)
        if ok:
            return int(tok) if integer else float(tok)
        parsed = c_int(tok) if integer else c_float(tok)
        kind = "an integer" if integer else "a number"
        if integer and FLOAT_RE.match(tok):
            self.r.error(rel, ln, "%s: %s must be an integer, got '%s' (str_to_int() reads %d)" % (ctx, what, tok, parsed))
        else:
            self.r.error(rel, ln, "%s: %s must be %s, got '%s' (read as %s)" % (ctx, what, kind, short(tok, 30),
                                                                               fmt_num(parsed)))
        return None

    # ----------------------------------------------------------- argument checking
    def check_args(self, m, rel, ln, op, args, sig, is_header=False):
        """Validate args against sig. Returns list of parsed values (pos -> tuple)."""
        vals = []
        i = 0
        ctx = "'%s'" % op
        for typ, opt, extra in sig:
            if i >= len(args):
                if not opt:
                    self.r.error(rel, ln, "%s: missing %s (usage: %s)" % (
                        ctx, ('"%s"' % extra) if typ == "text" else (extra or TYPE_NAMES.get(typ, typ)), usage(op, sig)))
                break
            if typ == "pos":
                if len(args) - i < 3:
                    self.r.error(rel, ln, "%s: a position needs 3 tokens ('@landmark dx dz', 'P dx dz' or 'x y z'), "
                                 "got %d (usage: %s)" % (ctx, len(args) - i, usage(op, sig)))
                    i = len(args)
                    break
                vals.append(self.check_pos(rel, ln, ctx, args[i : i + 3]))
                i += 3
            elif typ == "color":
                rem = len(args) - i
                if rem < 3:
                    self.r.error(rel, ln, "%s: the colour needs 3 numbers (r g b); with only %d, exec() ignores it "
                                 "(it only reads a colour when all three are present)" % (ctx, rem))
                    i = len(args)
                    break
                for k in range(3):
                    self.num(rel, ln, ctx, args[i + k], "colour component", False)
                i += 3
            else:
                vals.append(self.check_value(m, rel, ln, op, typ, extra, args[i], is_header))
                i += 1
        if i < len(args):
            extra_toks = args[i:]
            hint = ""
            if any(" " not in t for t in extra_toks) and any(t[0] == "text" for t in sig):
                hint = " (is a text argument missing its quotes?)"
            self.r.error(rel, ln, "%s: %d extra token(s) ignored: %s%s (usage: %s)" % (
                ctx, len(extra_toks), " ".join(short(t, 20) for t in extra_toks), hint, usage(op, sig)))
        return vals

    def check_pos(self, rel, ln, ctx, toks):
        f = self.f
        a = toks[0]
        if a.startswith("@"):
            name = a[1:]
            if name not in f.landmarks:
                self.r.error(rel, ln, "%s: unknown landmark '%s'%s; parse_pos() logs an error and uses the world "
                             "origin" % (ctx, a, suggest(name, f.landmarks)))
            self.num(rel, ln, ctx, toks[1], "dx", False)
            self.num(rel, ln, ctx, toks[2], "dz", False)
            return ("@", name)
        if a == "P":
            self.num(rel, ln, ctx, toks[1], "dx", False)
            self.num(rel, ln, ctx, toks[2], "dz", False)
            return ("P",)
        if not FLOAT_RE.match(a):
            hint = " - did you mean '@%s'?" % a if a in f.landmarks else ""
            self.r.error(rel, ln, "%s: position must be '@landmark dx dz', 'P dx dz' or 'x y z'; '%s' is read as "
                         "x=%s%s" % (ctx, short(a, 20), fmt_num(c_float(a)), hint))
        else:
            self.num(rel, ln, ctx, a, "x", False)
        self.num(rel, ln, ctx, toks[1], "y", False)
        self.num(rel, ln, ctx, toks[2], "z", False)
        return ("xyz",)

    def check_name(self, rel, ln, ctx, tok, table, what, consequence, allow=()):
        if tok in table or tok in allow:
            return True
        self.r.error(rel, ln, "%s: unknown %s '%s'%s; %s" % (ctx, what, tok, suggest(tok, table), consequence))
        return False

    def check_value(self, m, rel, ln, op, typ, extra, tok, is_header):
        f = self.f
        ctx = "'%s'" % op
        if typ == "int":
            return self.num(rel, ln, ctx, tok, extra or "value", True)
        if typ == "float":
            return self.num(rel, ln, ctx, tok, extra or "value", False)
        if typ == "count":
            v = self.num(rel, ln, ctx, tok, extra or "count", True)
            if v is not None and v < 1:
                self.r.warning(rel, ln, "%s: count %d spawns/requires nothing" % (ctx, v))
            return v
        if typ == "bool":
            v = self.num(rel, ln, ctx, tok, "flag", True)
            if v is not None and v not in (0, 1):
                self.r.warning(rel, ln, "%s: expected 0 or 1, got %d (treated as %s)" % (ctx, v, "on" if v else "off"))
            return v
        if typ == "text":
            cap = self.caps.get(extra)
            self.check_text(rel, ln, tok, cap, "%s %s" % (ctx, TEXT_NAMES.get(extra, extra)),
                            "%s[%d]" % (self.cap_src.get(extra, extra), cap or 0))
            return tok
        if typ == "label_def":
            if not tok:
                self.r.error(rel, ln, "'label' without a name")
            m.label_defs.setdefault(tok, []).append(ln)
            return tok
        if typ == "label_ref":
            m.label_refs.append((tok, ln))
            if extra == "choice" and blen(tok) > f.sz_choiceLabel - 1:
                self.r.error(rel, ln, "choice label '%s' is %d bytes; MissionRun::choiceLabel[%d] truncates it, so "
                             "find_label() will not find it" % (tok, blen(tok), f.sz_choiceLabel))
            return tok
        if typ == "flag_name":
            if blen(tok) > f.sz_flag_name - 1:
                self.r.error(rel, ln, "flag name '%s' is %d bytes; flag_set() stores only %d (StoryFlag::name[%d]), "
                             "so flag_get() with the full name never matches" % (tok, blen(tok), f.sz_flag_name - 1,
                                                                                 f.sz_flag_name))
            return tok
        if typ in ("actor_def", "vehicle_def", "pickup_def"):
            cap = {"actor_def": f.sz_actor_tag, "vehicle_def": f.sz_vehicle_tag, "pickup_def": f.sz_pickup_tag}[typ]
            if blen(tok) > cap - 1:
                self.r.error(rel, ln, "tag '%s' is %d bytes; tags hold %d, so later commands using the full tag "
                             "never match it" % (tok, blen(tok), cap - 1))
            if not tok:
                self.r.error(rel, ln, "%s: empty tag" % ctx)
            d = {"actor_def": m.actor_defs, "vehicle_def": m.vehicle_defs, "pickup_def": m.pickup_defs}[typ]
            d.setdefault(tok, ln)
            return tok
        if typ in ("actor", "vehicle", "actor_or_vehicle", "unprotect"):
            m.tag_refs.append((typ, tok, ln, op))
            return tok
        if typ == "protect":
            if blen(tok) > f.sz_protect - 1:
                self.r.error(rel, ln, "protect tag '%s' is longer than %d bytes (MissionRun::protect)" % (tok, f.sz_protect - 1))
            m.protects.setdefault(tok, ln)
            m.tag_refs.append(("actor", tok, ln, op))
            return tok
        if typ == "kind":
            self.check_name(rel, ln, ctx, tok, f.kinds, "actor kind", "kind_by_name() silently spawns a civilian")
            return tok
        if typ == "preset":
            if tok != "-" and tok not in f.presets and tok not in f.kind_presets:
                self.r.warning(rel, ln, "%s: unknown appearance preset '%s'%s; appearance_preset() silently falls "
                               "back to a random look (use '-' for the kind's default)" % (
                                   ctx, tok, suggest(tok, f.presets)))
            return tok
        if typ in ("faction", "faction_opt"):
            self.check_name(rel, ln, ctx, tok, f.factions, "faction",
                            "faction_by_name() returns -1 and the %s" % (
                                "faction is left unchanged" if typ == "faction_opt" else "command does nothing"),
                            ("-",) if typ == "faction_opt" else ())
            return tok
        if typ == "weapon_opt":
            self.check_name(rel, ln, ctx, tok, f.weapons, "weapon",
                            "weapon_by_name() returns -1 and the kind's default weapon is kept", ("-",))
            return tok
        if typ == "weapon_give":
            if self.check_name(rel, ln, ctx, tok, f.weapons, "weapon", "the command does nothing"):
                if f.weapons.index(tok) <= f.give_weapon_min:
                    self.r.error(rel, ln, "%s: '%s' is ignored (exec() only gives weapons with index > %d)" % (
                        ctx, tok, f.give_weapon_min))
            return tok
        if typ in ("mode", "mode_opt"):
            self.check_name(rel, ln, ctx, tok, f.modes, "AI mode", "apply_mode() silently does nothing",
                            ("-",) if typ == "mode_opt" else ())
            return tok
        if typ == "vtype":
            self.check_name(rel, ln, ctx, tok, f.vtypes, "vehicle type", "vtype_by_name() silently spawns a sedan")
            return tok
        if typ == "wanted":
            v = self.num(rel, ln, ctx, tok, "level", True)
            lo, hi = f.wanted_range
            if v is not None and not lo <= v <= hi:
                self.r.error(rel, ln, "%s: level %d is outside %d..%d (wanted_set() clamps it)" % (ctx, v, lo, hi))
            return v
        if typ == "phase":
            v = self.num(rel, ln, ctx, tok, "phase", True)
            lo, hi = f.phase_range
            if v is not None and not lo <= v <= hi:
                self.r.error(rel, ln, "%s: phase %d is outside %d..%d (game_set_phase() clamps it)" % (ctx, v, lo, hi))
            if v is not None and op == "phase" and not is_header:
                m.phase_cmds.append(max(lo, min(hi, v)))
            return v
        if typ == "rain":
            v = self.num(rel, ln, ctx, tok, "rain", False)
            if v is not None and not 0.0 <= v <= 1.0:
                self.r.warning(rel, ln, "%s: rain %g is outside 0..1 (save_game()/load_game() clamp it)" % (ctx, v))
            return v
        if typ == "music":
            self.check_name(rel, ln, ctx, tok, f.music, "music mood",
                            "it maps to MUSIC_NONE (silence); write 'music none' if silence is intended", ("none",))
            return tok
        if typ == "sound":
            self.check_name(rel, ln, ctx, tok, f.sounds, "sound", "the command plays nothing")
            return tok
        if typ == "speaker":
            self.check_speaker(rel, ln, tok, f.sz_dline_speaker, "DLine::speaker")
            return tok
        if typ == "dialogue_id":
            m.dlg_refs.append((tok, ln))
            return tok
        if typ == "cutscene_id":
            m.cut_refs.append((tok, ln))
            return tok
        if typ == "evidence":
            v = self.num(rel, ln, ctx, tok, "evidence id", True)
            if v is not None:
                if not 0 <= v < f.max_evidence:
                    self.r.error(rel, ln, "%s: evidence id %d is outside 0..%d (MAX_EVIDENCE); exec() spawns nothing"
                                 % (ctx, v, f.max_evidence - 1))
                else:
                    m.evidence_refs.append((v, ln))
            return v
        if typ == "landmark_bare":
            if tok not in f.landmarks:
                hint = " - 'start' takes a bare landmark name without '@'" if tok[1:] in f.landmarks else \
                    suggest(tok, f.landmarks)
                self.r.error(rel, ln, "'start': unknown landmark '%s'%s; mission_start_pos() falls back to the world "
                             "origin" % (tok, hint))
            if blen(tok) > f.sz_startLm - 1:
                self.r.error(rel, ln, "'start': landmark name longer than %d bytes (MissionDef::startLm)" % (f.sz_startLm - 1))
            return tok
        return tok

    def check_speaker(self, rel, ln, spk, cap, src):
        f = self.f
        if spk in f.speakers:
            return
        if spk.strip() in f.speakers:
            self.r.error(rel, ln, "speaker '%s' has surrounding whitespace, so it does not match speaker id '%s' "
                         "(the raw text is shown as the name)" % (spk, spk.strip()))
            return
        msg = "unknown speaker '%s'%s; speaker_display() shows the raw id in grey" % (spk, suggest(spk, f.speakers))
        if blen(spk) > cap - 1:
            msg += " (and it is cut to %d bytes by %s)" % (cap - 1, src)
        self.r.error(rel, ln, msg)

    # ----------------------------------------------------------- missions
    def load_missions(self):
        f = self.f
        mdir = os.path.join(self.root, "assets", "missions")
        idx = os.path.join(mdir, "missions.txt")
        irel = self.rel(idx)
        if not os.path.isfile(idx):
            self.r.error(irel, 0, "missions.txt missing: missions_init() loads no missions")
            return
        listed = {}
        for ln, toks in self.script_lines(idx, irel):
            name = toks[0]
            if len(toks) > 1:
                self.r.error(irel, ln, "only the first token is used as the file name; ignored: %s" % " ".join(toks[1:]))
            if blen("missions/" + name) > f.sz_mission_path - 1:
                self.r.error(irel, ln, "path 'missions/%s' does not fit load_mission_file()'s path[%d]" % (
                    name, f.sz_mission_path))
            if name in listed:
                self.r.error(irel, ln, "'%s' is already listed on line %d; it is loaded twice" % (name, listed[name]))
            listed.setdefault(name, ln)
            path = os.path.join(mdir, name)
            if not os.path.isfile(path):
                self.r.error(irel, ln, "mission file '%s' does not exist" % name)
                continue
            if len(self.missions) >= f.max_missions:
                self.r.error(irel, ln, "'%s' is mission #%d but MAX_MISSIONS is %d: load_mission_file() drops it" % (
                    name, len(self.missions) + 1, f.max_missions))
                continue
            self.missions.append(self.parse_mission(path, self.rel(path), ln))
        for fn in sorted(os.listdir(mdir)):
            if fn.endswith(".mis") and fn not in listed:
                self.r.warning(self.rel(os.path.join(mdir, fn)), 0, "not listed in missions.txt, so it is never loaded")

    def parse_mission(self, path, rel, index_line):
        f = self.f
        m = Mission(path, rel, index_line)
        headers = set(f.headers)
        for ln, toks in self.script_lines(path, rel):
            op, args = toks[0], toks[1:]
            if op in headers:
                m.headers.setdefault(op, []).append((ln, args))
                sig = HEADER_SIGS.get(op)
                if sig is None:
                    self.r.note(rel, ln, "header '%s' is handled by load_mission_file() but unknown to the validator" % op)
                    continue
                vals = self.check_args(m, rel, ln, op, args, sig, is_header=True)
                if m.first_cmd_line is not None and op != "reward":
                    self.r.warning(rel, ln, "header '%s' appears after the first command (line %d); headers are applied "
                                   "at load time wherever they are written" % (op, m.first_cmd_line))
                self.apply_header(m, op, args, vals)
                continue
            if m.first_cmd_line is None:
                m.first_cmd_line = ln
            pc = m.ncmds
            m.ncmds += 1
            if pc == f.max_cmds:
                self.r.error(rel, ln, "command #%d exceeds MAX_CMDS (%d): load_mission_file() drops this and every "
                             "following command" % (pc + 1, f.max_cmds))
            cmd = Cmd(op, args, ln, pc)
            if pc < f.max_cmds:
                m.cmds.append(cmd)
            if op not in f.exec_cmds:
                self.r.error(rel, ln, "unknown command '%s'%s; exec() logs an error and skips it" % (
                    op, suggest(op, list(f.exec_cmds) + list(headers))))
                continue
            if op in self.unknown_sig_cmds:
                continue
            vals = self.check_args(m, rel, ln, op, args, CMD_SIGS[op])
            if op == "flag" and len(vals) >= 2:
                m.flag_sets.append((args[0], vals[1], ln))
            elif op == "if_flag" and len(vals) >= 2:
                m.flag_tests.append((args[0], vals[1], ln))
        self.check_mission_body(m)
        return m

    def apply_header(self, m, op, args, vals):
        v = vals[0] if vals else None
        if op == "mission" and v is not None:
            m.id = v
        elif op == "key":
            m.key = args[0] if args else ""
        elif op == "title":
            m.title = args[0] if args else ""
        elif op == "requires" and v is not None:
            m.requires = v
        elif op == "requires_phase" and v is not None:
            m.requires_phase = v
        elif op == "phase_after" and v is not None:
            m.phase_after = v
        elif op in ("repeatable", "side", "hidden"):
            setattr(m, op, True)

    def check_mission_body(self, m):
        f = self.f
        rel = m.rel
        # headers --------------------------------------------------------
        for key in ("mission", "key", "title"):
            if key not in m.headers:
                self.r.error(rel, m.first_cmd_line or 1, "missing '%s' header%s" % (
                    key, " (the id defaults to 0)" if key == "mission" else ""))
        if "desc" not in m.headers:
            self.r.warning(rel, 1, "missing 'desc' header (the journal shows an empty description)")
        if "start" not in m.headers and not m.hidden:
            self.r.error(rel, 1, "missing 'start' header: the start marker is placed at the world origin")
        for key, lst in m.headers.items():
            if key == "reward":
                continue
            if len(lst) > 1 and key not in ("repeatable", "side", "hidden"):
                self.r.error(rel, lst[-1][0], "header '%s' is given %d times (lines %s); the last one wins" % (
                    key, len(lst), ", ".join(str(x[0]) for x in lst)))
        rw = m.headers.get("reward", [])
        body = [x for x in rw if m.first_cmd_line is not None and x[0] > m.first_cmd_line]
        if body:
            last_ln, last_args = rw[-1]
            for ln, args in body:
                self.r.error(rel, ln, "'reward' is a header key: load_mission_file() consumes every 'reward' line at "
                             "load time, so exec()'s runtime 'reward' override is unreachable and this line does not "
                             "depend on the branch taken; the last one (line %d: %s) is the reward for every outcome"
                             % (last_ln, " ".join(last_args) or "?"))
        elif len(rw) > 1:
            self.r.error(rel, rw[-1][0], "header 'reward' is given %d times (lines %s); the last one wins" % (
                len(rw), ", ".join(str(x[0]) for x in rw)))
        # mission ids ----------------------------------------------------
        if m.key and blen("ch_%s_%d" % (m.key, len(m.cmds))) > f.sz_choice_fname - 1:
            self.r.error(rel, m.hline("key"), "key '%s' is too long for the choice flag name buffer fname[%d]" % (
                m.key, f.sz_choice_fname))
        # labels ---------------------------------------------------------
        for name, lines in m.label_defs.items():
            if len(lines) > 1:
                for ln in lines[1:]:
                    self.r.error(rel, ln, "duplicate label '%s' (first defined on line %d; find_label() always "
                                 "jumps to the first)" % (name, lines[0]))
        refd = set()
        for name, ln in m.label_refs:
            refd.add(name)
            if name not in m.label_defs:
                self.r.error(rel, ln, "label '%s' is not defined%s; find_label() returns the end of the script, so "
                             "the mission silently completes" % (name, suggest(name, m.label_defs)))
        for name, lines in m.label_defs.items():
            if name not in refd:
                self.r.warning(rel, lines[0], "label '%s' is never used by jump/if_flag/choice" % name)
        # control flow ---------------------------------------------------
        self.check_flow(m)
        # tags -----------------------------------------------------------
        reported = set()
        for kind, tag, ln, op in m.tag_refs:
            if kind == "unprotect":
                if tag not in m.protects:
                    self.r.warning(rel, ln, "'unprotect %s': this tag is never protected in this mission" % tag)
                continue
            if kind == "actor":
                defs = [m.actor_defs]
            elif kind == "vehicle":
                defs = [m.vehicle_defs]
            else:
                defs = [m.actor_defs, m.vehicle_defs]
            first = [d[tag] for d in defs if tag in d]
            if not first:
                what = {"actor": "spawned (spawn/group)", "vehicle": "created by 'vehicle'",
                        "actor_or_vehicle": "spawned or created"}[kind]
                eff = NO_TAG_EFFECT.get(op, "has no effect")
                self.r.error(rel, ln, "'%s %s': tag '%s' is never %s in this mission (tags do not survive between "
                             "missions), so '%s' %s" % (op, tag, tag, what, op, eff))
            elif min(first) > ln and (tag, "order") not in reported:
                reported.add((tag, "order"))
                self.r.warning(rel, ln, "'%s %s' comes before the first spawn of '%s' (line %d)" % (
                    op, tag, tag, min(first)))
        if len(m.protects) > 4:
            self.r.warning(rel, 0, "%d protect commands; MissionRun::protect holds 4, later ones are ignored" %
                           len(m.protects))
        # retry / fast-forward replay -------------------------------------
        cps = [c for c in m.cmds if c.op == "checkpoint"]
        for c in m.cmds:
            if c.op in NON_IDEMPOTENT:
                later = [cp for cp in cps if cp.pc > c.pc]
                if later:
                    self.r.warning(rel, c.line, "'%s' runs before the checkpoint on line %d: fast_forward_to() "
                                   "re-executes it on every retry from that checkpoint and %s" % (
                                       c.op, later[0].line, NON_IDEMPOTENT[c.op]))

    def check_flow(self, m):
        """Commands no path from the start can reach (jump/if_flag/choice/end/fail aware)."""
        n = len(m.cmds)
        if not n:
            self.r.warning(m.rel, 0, "mission has no commands: it completes as soon as it starts")
            return
        first_label = {}
        for c in m.cmds:
            if c.op == "label" and c.args:
                first_label.setdefault(c.args[0], c.pc)

        def target(name):
            return first_label.get(name, n)

        def succ(c):
            a = c.args
            if c.op == "jump":
                return [target(a[0])] if a else [n]
            if c.op == "if_flag":
                return [c.pc + 1, target(a[2])] if len(a) > 2 else [c.pc + 1]
            if c.op == "choice":
                return [target(a[2]) if len(a) > 2 else n, target(a[4]) if len(a) > 4 else n]
            if c.op in ("end", "fail"):
                return []
            return [c.pc + 1]

        seen = set()
        stack = [0]
        while stack:
            p = stack.pop()
            if p >= n or p in seen:
                continue
            seen.add(p)
            stack.extend(succ(m.cmds[p]))
        run = []
        for c in m.cmds + [None]:
            if c is not None and c.pc not in seen:
                run.append(c)
                continue
            if run and any(x.op != "label" for x in run):
                a, b = run[0].line, run[-1].line
                self.r.warning(m.rel, a, "unreachable: no path from the start reaches line%s %s (code after an "
                               "unconditional jump/end/fail that no label leads back into)" % (
                                   "s" if a != b else "", "%d-%d" % (a, b) if a != b else str(a)))
            run = []

    # ----------------------------------------------------------- cross-mission
    def check_missions_global(self):
        f = self.f
        ms = self.missions
        by_id = {}
        by_key = {}
        for m in ms:
            if "mission" not in m.headers:
                continue
            ln = m.hline("mission")
            if not 0 <= m.id <= 31:
                self.r.error(m.rel, ln, "mission id %d is outside 0..31: missionsDone is a 32-bit mask indexed by "
                             "id & 31, so it aliases mission %d" % (m.id, m.id & 31))
            elif m.id == 0:
                self.r.warning(m.rel, ln, "mission id 0 can never be required ('requires 0' means no prerequisite)")
            if m.id in by_id:
                self.r.error(m.rel, ln, "duplicate mission id %d (also %s); they share one completion bit" % (
                    m.id, by_id[m.id].rel))
            else:
                by_id[m.id] = m
            if m.key:
                if m.key in by_key:
                    self.r.error(m.rel, m.hline("key"), "duplicate mission key '%s' (also %s): choice flags "
                                 "'ch_<key>_<pc>' collide" % (m.key, by_key[m.key].rel))
                by_key.setdefault(m.key, m)
        direct_bad = set()
        for m in ms:
            if "requires" not in m.headers:
                continue
            ln = m.hline("requires")
            if m.requires < 0:
                self.r.warning(m.rel, ln, "requires %d is treated as 'no prerequisite' (only ids > 0 count)" % m.requires)
                continue
            if m.requires == 0:
                continue
            targets = [x for x in ms if x.id == m.requires and "mission" in x.headers]
            setters = [x for x in ms if (x.id & 31) == (m.requires & 31) and not x.repeatable]
            if not targets:
                self.r.error(m.rel, ln, "requires mission %d, which does not exist: mission_available() never "
                             "offers this mission" % m.requires)
                direct_bad.add(m)
            elif m.requires == m.id:
                self.r.error(m.rel, ln, "mission requires itself")
                direct_bad.add(m)
            elif not setters:
                self.r.error(m.rel, ln, "requires mission %d (%s), which is repeatable: mission_complete() never "
                             "sets the done bit of repeatable missions, so this mission is never offered" % (
                                 m.requires, targets[-1].rel))
                direct_bad.add(m)
        # Reachability through the requires chain and phase gates (mission_available()).
        lo, hi = f.phase_range
        phase = f.initial_phase
        reach = []
        changed = True
        while changed:
            changed = False
            for m in ms:
                if m in reach or m.hidden:
                    continue
                if m.requires > 0 and not self.requires_met(m, reach):
                    continue
                if m.requires_phase > phase:
                    continue
                reach.append(m)
                phase = max([phase, min(hi, max(lo, m.phase_after))] + m.phase_cmds)
                changed = True
        for m in ms:
            if m in reach:
                continue
            if m.hidden:
                self.r.note(m.rel, m.hline("hidden"), "hidden: mission_available() never offers it; only C++ code "
                            "can start it")
                continue
            if m in direct_bad:
                continue
            why = []
            if m.requires > 0 and not self.requires_met(m, reach):
                t = [x for x in ms if (x.id & 31) == (m.requires & 31) and not x.repeatable]
                why.append("its prerequisite mission %d (%s) is itself unreachable" % (
                    m.requires, ", ".join(x.rel for x in t) or "?"))
            if m.requires_phase > phase:
                why.append("requires_phase %d is never reached (the highest phase any reachable mission sets is %d)"
                           % (m.requires_phase, phase))
            self.r.error(m.rel, m.hline("requires") if "requires" in m.headers else m.hline("mission"),
                         "mission can never become available: " + ("; ".join(why) or "requires chain is circular"))

    def requires_met(self, m, reach):
        """mission_available(): a mission with that exact id must exist, and the done bit (id & 31) must be set,
        which happens when any non-repeatable mission with the same bit completes."""
        if not any(x.id == m.requires for x in self.missions):
            return False
        bit = m.requires & 31
        return any(x in reach and not x.repeatable and (x.id & 31) == bit for x in self.missions)

    # ----------------------------------------------------------- dialogue
    def load_dialogue(self):
        f = self.f
        ddir = os.path.join(self.root, "assets", "dialogue")
        idx = os.path.join(ddir, "index.txt")
        irel = self.rel(idx)
        if not os.path.isfile(idx):
            self.r.error(irel, 0, "index.txt missing: dialogue_load_all() loads no conversations")
            return
        listed = {}
        for ln, toks in self.script_lines(idx, irel):
            name = toks[0]
            if len(toks) > 1:
                self.r.error(irel, ln, "only the first token is used as the file name; ignored: %s" % " ".join(toks[1:]))
            if blen("dialogue/" + name) > f.sz_dlg_path - 1:
                self.r.error(irel, ln, "path 'dialogue/%s' does not fit load_file()'s path[%d]" % (name, f.sz_dlg_path))
            if name in listed:
                self.r.error(irel, ln, "'%s' is already listed on line %d; its conversations are loaded twice" % (
                    name, listed[name]))
            listed.setdefault(name, ln)
            path = os.path.join(ddir, name)
            if not os.path.isfile(path):
                self.r.error(irel, ln, "dialogue file '%s' does not exist" % name)
                continue
            self.parse_dlg(path, self.convs, report=True)
        for fn in sorted(os.listdir(ddir)):
            if fn.endswith(".dlg") and fn not in listed:
                path = os.path.join(ddir, fn)
                self.r.warning(self.rel(path), 0, "not listed in dialogue/index.txt, so its conversations are never "
                               "loaded")
                self.parse_dlg(path, self.unlisted_convs, report=True)

    def parse_dlg(self, path, convs, report):
        """Mirror of load_file() in src/game/dialogue.cpp."""
        f = self.f
        rel = self.rel(path)
        cap = f.sz_dlg_line - 1
        cur = None
        for ln, raw in enumerate(read_lines(path), 1):
            if blen(raw) > cap:
                self.r.error(rel, ln, "line is %d bytes; load_file() keeps only %d (line[%d])" % (
                    blen(raw), cap, f.sz_dlg_line))
                raw = truncate_bytes(raw, cap)
            line = raw.rstrip("\r ")
            if not line or line[0] == "#":
                continue
            if line[0] == "@":
                cid = line[1:]
                if not cid:
                    self.r.error(rel, ln, "'@' without a conversation id")
                elif blen(cid) > f.sz_conv_id - 1:
                    self.r.error(rel, ln, "conversation id '%s' is %d bytes; Conversation::id holds %d, so it cannot "
                                 "be found by its full name" % (cid, blen(cid), f.sz_conv_id - 1))
                elif re.search(r"[\s#\"]", cid):
                    self.r.warning(rel, ln, "conversation id '%s' contains whitespace, '#' or '\"'; the whole rest "
                                   "of the line is the id" % cid)
                if cid in convs:
                    prev = convs[cid]
                    self.r.error(rel, ln, "duplicate conversation id '%s' (first in %s:%d); dialogue_start() always "
                                 "plays the first" % (cid, prev["rel"], prev["line"]))
                    cur = {"rel": rel, "line": ln, "count": 0, "dup": True}
                else:
                    cur = {"rel": rel, "line": ln, "count": 0, "dup": False}
                    convs[cid] = cur
                continue
            colon = line.find(":")
            if colon < 0:
                self.r.error(rel, ln, "line has no 'speaker:' separator and is ignored%s" % (
                    " ('#' only starts a comment in column 1)" if line.lstrip().startswith("#") else ""))
                continue
            if cur is None:
                self.r.error(rel, ln, "dialogue line before the first '@id' is ignored")
                continue
            spk = line[:colon]
            text = line[colon + 1 :].lstrip(" ")
            self.check_speaker(rel, ln, spk, f.sz_dline_speaker, "DLine::speaker")
            if not text:
                self.r.warning(rel, ln, "empty dialogue text")
            self.check_text(rel, ln, text, f.sz_dline_text, "dialogue text", "DLine::text[%d]" % f.sz_dline_text)
            cur["count"] += 1
            if cur["count"] == f.dlg_queue + 1:
                self.r.error(rel, ln, "conversation '%s' has more than %d lines: dialogue_start() queues only %d, so "
                             "this line and the rest are never shown" % (
                                 self.conv_name(convs, cur), f.dlg_queue, f.dlg_queue))
        for cid, c in convs.items():
            if c["rel"] == rel and c["count"] == 0:
                self.r.warning(rel, c["line"], "conversation '%s' has no lines: 'dialogue %s' ends immediately" % (cid, cid))

    @staticmethod
    def conv_name(convs, cur):
        for k, v in convs.items():
            if v is cur:
                return k
        return "?"

    # ----------------------------------------------------------- cutscenes
    def load_cutscenes(self):
        f = self.f
        path = os.path.join(self.root, "assets", "missions", "cutscenes.cut")
        rel = self.rel(path)
        if not os.path.isfile(path):
            self.r.error(rel, 0, "cutscenes.cut missing: cutscenes_load() fails and no cutscene can play")
            return
        scenes = []       # list of dicts
        cur = None
        shot = None        # last shot globally (cutscenes_load attaches line/fx to g_shots.last())
        for ln, toks in self.script_lines(path, rel, self.shot_overflow_hint):
            op, args = toks[0], toks[1:]
            if op.startswith("@"):
                cid = op[1:]
                if not cid:
                    self.r.error(rel, ln, "'@' without a cutscene id")
                if blen(cid) > f.sz_cut_id - 1:
                    self.r.error(rel, ln, "cutscene id '%s' longer than %d bytes (Cutscene::id)" % (cid, f.sz_cut_id - 1))
                if args:
                    self.r.error(rel, ln, "tokens after the cutscene id are ignored: %s" % " ".join(args))
                if cid in self.cutscenes:
                    self.r.error(rel, ln, "duplicate cutscene id '%s' (first on line %d); cutscene_start() always "
                                 "plays the first" % (cid, self.cutscenes[cid][1]))
                else:
                    self.cutscenes[cid] = (rel, ln)
                cur = {"id": cid, "line": ln, "shots": []}
                scenes.append(cur)
                continue
            if op == "shot":
                if cur is None:
                    self.r.error(rel, ln, "'shot' before the first '@id' is ignored")
                    continue
                shot = self.parse_shot(rel, ln, args)
                cur["shots"].append(shot)
                continue
            if op in ("line", "fx"):
                if shot is None:
                    self.r.error(rel, ln, "'%s' before the first shot is ignored" % op)
                    continue
                if cur is None or not cur["shots"]:
                    self.r.error(rel, ln, "'%s' comes before the first shot of '@%s', so cutscenes_load() attaches it "
                                 "to the last shot of the previous cutscene" % (op, cur["id"] if cur else "?"))
                    continue
                if op == "line":
                    if len(args) < 2:
                        self.r.error(rel, ln, "'line' needs a speaker and a quoted subtitle (usage: line <speaker> "
                                     "\"text\"); without text nothing is shown")
                    if len(args) > 2:
                        self.r.error(rel, ln, "'line': %d extra token(s) ignored: %s (is the subtitle missing its "
                                     "quotes?)" % (len(args) - 2, " ".join(short(t, 15) for t in args[2:])))
                    if args:
                        self.check_speaker(rel, ln, args[0], min(f.sz_shot_speaker, f.sz_dline_speaker), "Shot::speaker")
                    if len(args) > 1:
                        self.check_text(rel, ln, args[1], min(f.sz_shot_line, f.sz_dline_text), "subtitle",
                                        "Shot::line[%d]" % f.sz_shot_line)
                    if shot.get("text") is not None:
                        self.r.error(rel, ln, "second 'line' for the same shot replaces the one on line %d" % shot["text_line"])
                    shot["text"] = args[1] if len(args) > 1 else ""
                    shot["text_line"] = ln
                else:
                    if len(args) != 1:
                        self.r.error(rel, ln, "'fx' takes exactly one effect name (usage: fx <%s>)" % "|".join(f.fx))
                    if args:
                        if args[0] not in f.fx:
                            self.r.error(rel, ln, "unknown fx '%s'%s; start_shot() ignores it" % (
                                args[0], suggest(args[0], f.fx)))
                        if blen(args[0]) > f.sz_shot_fx - 1:
                            self.r.error(rel, ln, "fx name longer than %d bytes (Shot::fx)" % (f.sz_shot_fx - 1))
                    if shot.get("fx_line"):
                        self.r.error(rel, ln, "second 'fx' for the same shot replaces the one on line %d" % shot["fx_line"])
                    shot["fx_line"] = ln
                continue
            self.r.error(rel, ln, "unknown cutscene command '%s'%s; cutscenes_load() ignores it" % (
                op, suggest(op, ["shot", "line", "fx"])))
        for sc in scenes:
            if not sc["shots"]:
                self.r.warning(rel, sc["line"], "cutscene '%s' has no shots: it ends as soon as it starts" % sc["id"])
                continue
            self.check_subtitle_timing(rel, sc)

    def shot_overflow_hint(self, toks):
        """Describe what a 'shot' line loses when tokens past SCRIPT_MAX_ARGS are dropped (missing -> 0)."""
        if toks[0] != "shot":
            return ""
        lim = self.f.script_max_args
        i, out = 3, []
        for which in ("from", "to", "lookFrom", "lookTo"):
            if i >= len(toks):
                break
            need = 4 if toks[i].startswith("@") or toks[i] == "P" else 3
            span = toks[i : i + need]
            if i + need > lim:
                kept = [t if i + k < lim else "0" for k, t in enumerate(span)]
                out.append("%s '%s' is read as '%s'" % (which, " ".join(span), " ".join(kept)))
            i += need
        if not out:
            return ""
        return ": " + "; ".join(out) + " (one shot fits at most %d tokens: use the 3-token 'x y z' form for some " \
            "positions or raise SCRIPT_MAX_ARGS)" % lim

    def parse_shot(self, rel, ln, args):
        f = self.f
        ctx = "'shot'"
        use = "shot <seconds> <fov> <from> <to> <lookFrom> <lookTo>"
        shot = {"line": ln, "dur": 0.0, "text": None}
        if len(args) < 2:
            self.r.error(rel, ln, "'shot' needs a duration, a fov and 4 positions (usage: %s)" % use)
            return shot
        d = self.num(rel, ln, ctx, args[0], "duration", False)
        fov = self.num(rel, ln, ctx, args[1], "fov", False)
        shot["dur"] = max(d or 0.0, 0.01)
        if d is not None and d <= 0:
            self.r.warning(rel, ln, "'shot' duration %g: the shot is skipped (cutscene_update() uses at least 0.01s)" % d)
        if fov is not None and not 1 <= fov < 180:
            self.r.warning(rel, ln, "'shot' fov %g is outside a sensible 1..179 degree range" % fov)
        i = 2
        for which in ("from", "to", "lookFrom", "lookTo"):
            if i >= len(args):
                self.r.error(rel, ln, "'shot': missing the %s position (usage: %s)" % (which, use))
                return shot
            a = args[i]
            if a.startswith("@") or a == "P":
                need = 4
                if a.startswith("@") and a[1:] not in f.cut_landmarks:
                    self.r.error(rel, ln, "'shot': unknown landmark '%s'%s; parse_cpos() silently uses the world "
                                 "origin" % (a, suggest(a[1:], f.cut_landmarks)))
                comps = args[i + 1 : i + 4]
            else:
                need = 3
                comps = args[i : i + 3]
                if not FLOAT_RE.match(a):
                    hint = " - did you mean '@%s'?" % a if a in f.cut_landmarks else ""
                    self.r.error(rel, ln, "'shot' %s: position must be '@landmark dx dy dz', 'P right up forward' or "
                                 "'x y z'; '%s' is read as x=%s%s" % (which, a, fmt_num(c_float(a)), hint))
                    comps = args[i + 1 : i + 3]
            if len(args) - i < need:
                self.r.error(rel, ln, "'shot': the %s position needs %d tokens, only %d left (usage: %s)" % (
                    which, need, len(args) - i, use))
                return shot
            for k, c in enumerate(comps):
                if FLOAT_RE.match(c) or (need == 3 and k == 0 and not FLOAT_RE.match(a)):
                    continue
                self.num(rel, ln, ctx, c, "%s component" % which, False)
            i += need
        if i < len(args):
            self.r.error(rel, ln, "'shot': %d extra token(s) ignored: %s" % (len(args) - i, " ".join(args[i:])))
        return shot

    def check_subtitle_timing(self, rel, sc):
        """Subtitles go through the dialogue queue (dialogue_say); cutscene_skip() clears it when the scene ends."""
        base, per = self.f.line_duration
        t = 0.0
        busy_until = 0.0
        for s in sc["shots"]:
            text = s.get("text")
            if text:
                start = max(t, busy_until)
                busy_until = start + base + blen(text) * per
                s["sub"] = (start, busy_until)
            t += s["dur"]
        end = t
        for s in sc["shots"]:
            if "sub" not in s:
                continue
            a, b = s["sub"]
            if b > end + 1e-6:
                if a >= end:
                    self.r.warning(rel, s["text_line"], "subtitle never appears: the queued lines before it run past "
                                   "the end of cutscene '%s' (%.1fs), and cutscene_skip() clears the queue" % (sc["id"], end))
                else:
                    self.r.warning(rel, s["text_line"], "subtitle is cut off: it is shown from %.1fs and needs %.1fs, "
                                   "but cutscene '%s' ends at %.1fs and cutscene_skip() clears it" % (
                                       a, b - a, sc["id"], end))

    # ----------------------------------------------------------- evidence
    def load_evidence(self):
        f = self.f
        path = os.path.join(self.root, "assets", "dialogue", "evidence.txt")
        rel = self.rel(path)
        if not os.path.isfile(path):
            self.r.error(rel, 0, "evidence.txt missing: every journal entry is titled 'Evidence' with no text")
            return
        cap = f.sz_ev_line - 1
        cur = -1
        bodies = {}
        truncated = set()
        for ln, raw in enumerate(read_lines(path), 1):
            if blen(raw) > cap:
                self.r.error(rel, ln, "line is %d bytes; load_evidence() keeps only %d (line[%d])" % (
                    blen(raw), cap, f.sz_ev_line))
                raw = truncate_bytes(raw, cap)
            if "\r" in raw:
                self.r.warning(rel, ln, "carriage return: load_evidence() does not strip '\\r', it ends up in the text")
            if raw.startswith("#"):
                continue
            if raw.startswith("@"):
                mm = re.match(r"@(\d+)(?: (.*))?$", raw)
                cur = c_int(raw[1:])
                sp = raw.find(" ")
                title = raw[sp + 1 :] if sp >= 0 else "Evidence"
                if not mm:
                    self.r.error(rel, ln, "malformed entry header '%s': expected '@<id> <title>' (str_to_int() reads "
                                 "id %d, the title is everything after the first space)" % (short(raw, 30), cur))
                if not 0 <= cur < f.max_evidence:
                    self.r.error(rel, ln, "evidence id %d is outside 0..%d (MAX_EVIDENCE): the entry and its text are "
                                 "ignored" % (cur, f.max_evidence - 1))
                    continue
                if cur in self.evidence:
                    self.r.error(rel, ln, "duplicate evidence id %d (first on line %d): the title is replaced and the "
                                 "text appended" % (cur, self.evidence[cur][1]))
                else:
                    self.evidence[cur] = (rel, ln)
                if sp < 0:
                    self.r.warning(rel, ln, "evidence %d has no title (shown as 'Evidence')" % cur)
                self.check_text(rel, ln, title, f.sz_ev_title, "evidence title", "EvidenceText::title[%d]" % f.sz_ev_title)
                bodies.setdefault(cur, 0)
                continue
            if not raw:
                continue
            if not 0 <= cur < f.max_evidence:
                self.r.error(rel, ln, "text outside a valid '@<id> <title>' entry is ignored")
                continue
            if not raw.strip():
                self.r.warning(rel, ln, "whitespace-only line is appended to the evidence text")
            self.check_charset(rel, ln, raw, "evidence text")
            n = bodies[cur]
            if 0 < n < f.ev_append_limit:
                n += 1
            room = f.sz_ev_body - 1 - n
            if blen(raw) > room and cur not in truncated:
                truncated.add(cur)
                self.r.error(rel, ln, "evidence %d text exceeds EvidenceText::body[%d]: it is cut off here (%d of %d "
                             "bytes of this line fit)" % (cur, f.sz_ev_body, max(room, 0), blen(raw)))
            bodies[cur] = min(f.sz_ev_body - 1, n + blen(raw))
        for eid, (erel, eln) in self.evidence.items():
            if bodies.get(eid, 0) == 0:
                self.r.warning(erel, eln, "evidence %d has no text" % eid)

    # ----------------------------------------------------------- cross-file checks
    def check_cross(self):
        f = self.f
        loaded = self.missions
        # dialogue / cutscene references
        used_dlg, used_cut = set(f.cpp_dialogues), set(f.cpp_cutscenes)
        for m in loaded:
            for cid, ln in m.dlg_refs:
                used_dlg.add(cid)
                if cid not in self.convs:
                    hint = " (it is defined in %s, which index.txt does not list)" % self.unlisted_convs[cid]["rel"] \
                        if cid in self.unlisted_convs else suggest(cid, self.convs)
                    self.r.error(m.rel, ln, "dialogue '%s' does not exist%s; dialogue_start() logs an error and the "
                                 "step is skipped" % (cid, hint))
            for cid, ln in m.cut_refs:
                used_cut.add(cid)
                if cid not in self.cutscenes:
                    self.r.error(m.rel, ln, "cutscene '%s' does not exist%s; cutscene_start() logs an error and the "
                                 "step is skipped" % (cid, suggest(cid, self.cutscenes)))
        for name, refs in f.cpp_dialogues.items():
            if name not in self.convs:
                self.r.error(refs[0][0], refs[0][1], "dialogue_start(\"%s\"): no such conversation" % name)
        for name, refs in f.cpp_cutscenes.items():
            if name not in self.cutscenes:
                self.r.error(refs[0][0], refs[0][1], "cutscene_start(\"%s\"): no such cutscene" % name)
        for cid, c in self.convs.items():
            if cid not in used_dlg:
                self.r.warning(c["rel"], c["line"], "conversation '%s' is never played (no mission 'dialogue %s')" % (
                    cid, cid))
        for cid, (rel, ln) in self.cutscenes.items():
            if cid not in used_cut:
                self.r.warning(rel, ln, "cutscene '%s' is never played (no mission 'cutscene %s')" % (cid, cid))
        # evidence
        placed = {}
        for m in loaded:
            for eid, ln in m.evidence_refs:
                if eid in placed and placed[eid][0] is not m:
                    self.r.warning(m.rel, ln, "evidence %d is also placed by %s:%d" % (eid, placed[eid][0].rel,
                                                                                      placed[eid][1]))
                placed.setdefault(eid, (m, ln))
                if self.evidence is not None and eid not in self.evidence:
                    self.r.warning(m.rel, ln, "evidence %d has no entry in dialogue/evidence.txt: the journal shows "
                                   "'Evidence' with no text" % eid)
        # flags
        sets = {}   # name -> set of values (None = unknown)
        set_where = {}
        for m in loaded:
            for name, val, ln in m.flag_sets:
                sets.setdefault(name, set()).add(val)
                set_where.setdefault(name, (m.rel, ln))
            for c in m.cmds:
                if c.op == "choice":
                    fname = "ch_%s_%d" % (m.key, c.pc)
                    if blen(fname) > f.sz_flag_name - 1:
                        self.r.error(m.rel, c.line, "choice flag '%s' is %d bytes; flag_set() stores %d, so retrying "
                                     "from a later checkpoint (fast_forward_to) cannot read the choice back" % (
                                         fname, blen(fname), f.sz_flag_name - 1))
                    sets.setdefault(fname, set()).update({1, 2})
                    set_where.setdefault(fname, (m.rel, c.line))
        for name, lst in f.cpp_flag_sets.items():
            for rel, ln, val in lst:
                sets.setdefault(name, set()).add(val)
                set_where.setdefault(name, (rel, ln))
        tested = set(f.cpp_flag_gets)
        for m in loaded:
            for name, val, ln in m.flag_tests:
                tested.add(name)
                if name not in sets:
                    self.r.warning(m.rel, ln, "if_flag tests '%s', which no mission ('flag') or C++ flag_set() ever "
                                   "sets (flag_get() returns 0)%s" % (name, suggest(name, sets)))
                elif val is not None and val != 0 and None not in sets[name] and val not in sets[name]:
                    self.r.warning(m.rel, ln, "if_flag '%s' %d can never be true: the flag is only ever set to %s" % (
                        name, val, ", ".join(str(v) for v in sorted(sets[name]))))
        for name, refs in f.cpp_flag_gets.items():
            if name not in sets:
                self.r.warning(refs[0][0], refs[0][1], "flag_get(\"%s\"): no mission or C++ code ever sets this flag"
                               % name)
        for name, (rel, ln) in set_where.items():
            if name not in tested and not name.startswith("ch_"):
                self.r.note(rel, ln, "flag '%s' is set but never tested (no if_flag, no C++ flag_get)" % name)
        if len(sets) > f.max_flags:
            self.r.warning("", 0, "%d distinct story flags (including choice flags); PlayerData stores at most %d "
                           "(MAX_FLAGS), later ones are silently dropped" % (len(sets), f.max_flags))

    def run(self):
        self.load_missions()
        self.load_cutscenes()
        self.load_dialogue()
        self.load_evidence()
        self.check_missions_global()
        self.check_cross()


def main(argv=None):
    ap = argparse.ArgumentParser(description="Validate FIGHTO content scripts (missions, cutscenes, dialogue, evidence).")
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    help="repository root (default: the parent of tools/)")
    ap.add_argument("--quiet", "-q", action="store_true", help="hide NOTE lines")
    ap.add_argument("--werror", action="store_true", help="exit with status 1 on warnings too")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.root)
    rep = Report()
    v = Validator(root, rep)
    v.run()
    for line in rep.render(args.quiet):
        print(line)
    ne, nw, nn = rep.count("ERROR"), rep.count("WARNING"), rep.count("NOTE")
    print("validate_content: %d mission(s), %d cutscene(s), %d conversation(s), %d evidence entr%s checked: "
          "%d error(s), %d warning(s), %d note(s)" % (
              len(v.missions), len(v.cutscenes), len(v.convs), len(v.evidence),
              "y" if len(v.evidence) == 1 else "ies", ne, nw, nn))
    return 1 if ne or (args.werror and nw) else 0


if __name__ == "__main__":
    sys.exit(main())
