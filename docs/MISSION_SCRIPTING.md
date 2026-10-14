# FIGHTO mission scripting reference

This is the reference for content authors who write missions, cutscenes, dialogue and
evidence entries. Everything here is derived from the interpreter code, not from intent:

| Topic | Source of truth |
|---|---|
| Tokenizer | `src/game/script.h`, `src/game/script.cpp` |
| Missions (`*.mis`, `missions.txt`) | `src/game/mission.cpp` |
| Cutscenes (`cutscenes.cut`) | `src/game/cutscene.cpp` |
| Dialogue (`index.txt`, `*.dlg`) | `src/game/dialogue.cpp` |
| Evidence journal (`evidence.txt`) | `src/ui/menu.cpp` (`load_evidence`, `screen_journal`) |
| Story flags | `src/game/save.cpp` (`flag_get` / `flag_set`) |
| Phases | `src/game/game.cpp` (`game_set_phase`) |

Where the code does something an author would not expect, this document says so in a
**Caution** note. The "Known issues" section at the end collects them.

---

## 1. File layout and loading

| File | Read by | Parser | When |
|---|---|---|---|
| `assets/missions/missions.txt` | `missions_init()` | script tokenizer | once, in `game_init()` |
| `assets/missions/<name>.mis` | `load_mission_file()` | script tokenizer | once, in listed order |
| `assets/missions/cutscenes.cut` | `cutscenes_load()` | script tokenizer | once, in `game_init()` |
| `assets/dialogue/index.txt` | `dialogue_load_all()` | script tokenizer | once, in `game_init()` |
| `assets/dialogue/<name>.dlg` | `load_file()` in dialogue.cpp | its own line reader | once, in listed order |
| `assets/dialogue/evidence.txt` | `load_evidence()` in menu.cpp | its own line reader | lazily, the first time the Journal is opened |

All files are read through `plat_read_asset()`: from the `assets/` folder next to the
executable on desktop, from the APK `assets/` on Android. There is no hot reload; restart
the game after editing.

Shipped content: 20 missions (14 story `m01`-`m14`, 6 side jobs `s20`-`s25`), 10 cutscenes
(26 shots), 71 conversations in 4 `.dlg` files, 16 evidence entries.

---

## 2. Script tokenizer (`script_next_line`)

Used for `missions.txt`, every `.mis` file, `cutscenes.cut` and `dialogue/index.txt`.
**Not** used for `.dlg` files or `evidence.txt` (see sections 13 and 14).

* The input is split into physical lines at `\n`. There is no line continuation.
* Each line is copied into a 640-byte buffer (`ScriptLine::buf[640]`): anything past
  **639 bytes is silently cut off**.
* Token separators are space, tab and `\r` (so CRLF files tokenize correctly).
* A token that starts with `"` runs to the next `"` and becomes one token. **There are no
  escapes**: a `"` can never appear inside a quoted string. An unterminated quote runs to
  the end of the line. `""` produces an empty token (it still counts as an argument).
  Text glued to a closing quote starts a new token (`"ab"cd` is two tokens, `ab` and `cd`).
* `#` starts a comment **only at the start of a token** (start of line or after
  whitespace). `#` inside an unquoted token (`a#b`) or inside quotes is literal.
* At most **16 tokens per line** (`SCRIPT_MAX_ARGS`, including the command word). Extra
  tokens are dropped silently, with no warning.
* Lines with no tokens (blank or comment-only) are skipped.
* Numbers: integers go through `str_to_int` (optional sign, then decimal digits, stops at
  the first other character; no hex). Floats go through `str_to_float` (sign, digits,
  optional `.fraction`, optional `e`/`E` exponent). Anything non-numeric parses as `0`.
  A missing argument takes the command's default; the tables below list the defaults.

---

## 3. `missions.txt`

One mission file name per line. The first token of each line is the file name, relative
to `assets/missions/`; the rest of the line is ignored. `#` comments are allowed.

```
# FIGHTO mission index - one .mis file per line, loaded in this order.
# Story: Act I (crime)
m01_back_on_the_blocks.mis
m02_debts_to_collect.mis
```

The list order is the mission **index**. It decides the order in the Journal, the order
of start-beacon checks, and the value `FIGHTO_AUTOTEST_MISSION` refers to (an index, not
a mission id). A missing file logs `Missing mission file missions/<name>` and is skipped.
Files past the 32nd (`MAX_MISSIONS`) are ignored without a message.

---

## 4. Mission files (`*.mis`)

A mission file is a list of lines. A line whose first token is a **header key** sets a
property of the mission; every other line is appended to the command list, in order.

> **Caution: header keys are recognised on any line, not only at the top.** The loader
> checks every line against the header keys before treating it as a command. In
> particular `reward` is both a header key and a command name, so a `reward N` line in the
> body never becomes a command: it overwrites the header reward at load time, and the
> last one in the file wins. In `m02_debts_to_collect.mis` the branch lines `reward 300`
> (line 38) and `reward 650` (line 49) both become header assignments, so Debts to Collect
> pays $650 whichever choice the player makes. Until this is fixed, use `give_money` for
> branch-specific payouts.

### 4.1 Header keys

| Key | Syntax | Meaning | Storage limit |
|---|---|---|---|
| `mission` | `mission <id>` | Numeric id. Must be unique. Completion is stored as bit `id & 31` of a 32-bit mask, so use 0-31; ids that share the low 5 bits share a completion bit. | int |
| `key` | `key <name>` | Short key used in choice flag names (`ch_<key>_<pc>`) and in log messages. | 23 chars |
| `title` | `title "<text>"` | Shown on the start prompt, HUD, Journal, result screens. Also the first objective text. | 63 chars |
| `desc` | `desc "<text>"` | Journal description. | 219 chars |
| `start` | `start <landmark> [dx] [dz]` | Start beacon at the landmark position + (dx, 0, dz). Landmark name without `@`. An unknown name puts the beacon at the world origin. | 23 chars |
| `requires` | `requires <id>` | Only available once the mission with this **id** is completed. `0` or absent means no requirement. A required id that no loaded mission has makes the mission permanently unavailable. | int |
| `requires_phase` | `requires_phase <n>` | Only available when the story phase is at least `n`. | int |
| `reward` | `reward <dollars>` | Money paid on completion (see the caution above). | int |
| `phase_after` | `phase_after <n>` | On completion, if `n` is greater than the current phase, `game_set_phase(n)` runs. | int |
| `repeatable` | `repeatable` | Never marked as done, so it stays available. A repeatable mission can therefore never satisfy another mission's `requires`. | flag |
| `side` | `side` | Side job: green beacon, `Side job:` prompt, listed under side jobs in the Journal. | flag |
| `hidden` | `hidden` | Never available and not listed in the Journal (can still be started from code). | flag |

Header lines can appear in any order. Strings longer than the limit are truncated.

### 4.2 Availability and starting

`mission_available()` returns true when the mission is not `hidden`, is not already done
(unless `repeatable`), its `requires` mission is done, and `phase >= requires_phase`.

While no mission is running, every frame each available mission whose start position is
within 40 m of the player adds an interaction (radius 2.6 m) labelled
`Mission: <title>` or `Side job: <title>`. Pressing interact starts it immediately; there is
no confirmation. Beacons (30 m light beams, gold for story, green for side jobs) are
drawn for available missions within 250 m of the camera. Only one mission runs at a time.

A new game starts the first available mission whose start position is within 30 m of
the player (the player spawns at `bus_stop`, which starts mission 1).

`mission_start()` does not move the player. It sets the objective to the title, plays the
mission-start sound and records the player's position and yaw as the initial checkpoint.

---

## 5. Positions

Commands that take a position read **exactly three tokens** in one of three forms
(`parse_pos` in mission.cpp):

| Form | Meaning |
|---|---|
| `@<landmark> <dx> <dz>` | Landmark position + (dx, 0, dz), then **snapped to the ground**. The ground probe starts 2 m above the point for landmarks at y > -10, and 1.5 m above for underground landmarks (A.E.G.I.S. rooms and the nest). An unknown landmark logs `Mission script line N: unknown landmark @name` and uses the world origin. |
| `P <dx> <dz>` | Player position + (dx, 0, dz) in **world axes** (not rotated by the player's facing). Height is the player's height; no ground snap. |
| `<x> <y> <z>` | Absolute world coordinates. No ground snap. |

Units are metres. +Y is up. Yaw angles in scripts are in **degrees**; yaw 0 faces +Z and
the facing vector is `(sin yaw, 0, cos yaw)`, so yaw 90 faces +X.

> Cutscene positions use a different form with a dy component and player-relative axes;
> see section 12.

### Landmark names

| Name | Landmark (`LandmarkId`) | In-game name |
|---|---|---|
| `home` | `LM_HOME` | Cross Family Home |
| `diner` | `LM_DINER` | Lucky Star Diner |
| `laundromat` | `LM_LAUNDROMAT` | Suds City Laundromat |
| `pawn` | `LM_PAWN` | Ace Pawn & Arms |
| `safehouse` | `LM_SAFEHOUSE` | Safehouse |
| `bus_stop` | `LM_BUS_STOP` | Harlan St. Bus Stop |
| `police` | `LM_POLICE` | BRPD 4th Precinct |
| `hospital` | `LM_HOSPITAL` | St. Agnes Hospital |
| `plaza` | `LM_PLAZA` | Liberty Plaza |
| `meridian` | `LM_CROSS_TOWER` | Meridian Tower |
| `fight_pit` | `LM_FIGHT_PIT` | The Fight Pit |
| `chop_shop` | `LM_CHOP_SHOP` | Varga's Garage |
| `warehouse` | `LM_WAREHOUSE` | Halberd Shipping Warehouse |
| `substation` | `LM_SUBSTATION` | Ironworks Substation |
| `nexus` | `LM_NEXUS` | Nexus Logistics |
| `drain_hatch` | `LM_DRAIN_HATCH` | Storm Drain Access |
| `mansion` | `LM_MANSION` | Varga Estate |
| `car_lot` | `LM_CAR_LOT` | Silverline Motors |
| `aegis_lobby` | `LM_AEGIS_LOBBY` | A.E.G.I.S. Elevator (underground) |
| `aegis_briefing` | `LM_AEGIS_BRIEFING` | Briefing Room (underground) |
| `aegis_lab` | `LM_AEGIS_LAB` | Xenotech Laboratory (underground) |
| `aegis_range` | `LM_AEGIS_RANGE` | Training Range (underground) |
| `aegis_archive` | `LM_AEGIS_ARCHIVE` | Archive Vault (underground) |
| `aegis_reactor` | `LM_AEGIS_REACTOR` | Project LANTERN (underground) |
| `nest` | `LM_DRAIN_NEST` | The Nest (underground) |

---

## 6. Execution model

* Commands run in file order. `pc` is the 0-based index into the command list
  (header lines, blank lines and comments do not count).
* Each frame in which the screen is `SCR_PLAYING`, `missions_update()`:
  1. checks the fail conditions: every `protect`ed tag, then the `timer`;
  2. executes commands from `pc` until one **blocks** (returns "not done"), up to
     **64 commands per frame**; the rest continue next frame;
  3. completes the mission when `pc` runs past the last command.
* Only the first command executed in a frame receives the frame's `dt`. A blocking
  command reached later in the same frame starts its timer at 0.
* A blocking command keeps per-step state (time spent, hold progress, "interacted") that
  resets when `pc` advances.
* Missions do not run while the game is paused or any menu or result screen is up, so
  timers stop too.
* An unknown command logs `Mission <key> line N: unknown command '<op>'` and is skipped.
* `jump`/`if_flag`/`choice` targets are looked up by `label` name at run time. A missing
  label logs `Mission <key>: label <name> not found` and jumps past the end, which
  **completes the mission**.

### 6.1 Objectives and markers

The HUD shows the mission title and the current objective at the top of the screen.
Blocking commands that take an objective argument set it on every frame they run; an
empty or missing objective keeps the previous text. When the text changes, the notify
sound plays (not during fast-forward). Commands that point somewhere set the **marker**,
a 25 m gold beam that is also drawn on the minimap, and clear it when they complete.
`timer` shows a `m:ss` countdown under the objective (red below 10 s); `hold` shows a
progress bar.

### 6.2 Interaction prompts

`talk`, `interact`, `hold` and `pickup` register an interactable each frame while they
run. The HUD draws it as `[<key>]  <text>`, where `<key>` is the bound interact key (or
`USE` in touch mode). The game already prefixes the key, so prompt texts that contain
`[E]`, as the shipped missions do, show the key twice (`[E]  Talk [E]`), and show the
wrong letter if the player rebinds interact.

---

## 7. Command reference

Notation: `<pos>` = the three position tokens from section 5; `[x=default]` = optional;
`"text"` = quote it if it contains spaces. **Blocks** = the command waits until a
condition holds. **On retry** = what happens when the command lies before the checkpoint
being restored (section 8).

### 7.1 Flow control

| Command | Blocks | Behaviour | On retry |
|---|---|---|---|
| `label <name>` | no | No-op jump target. | replayed (no-op) |
| `jump <label>` | no | Continue at `<label>`. | executed (same branch) |
| `flag <name> <value>` | no | `flag_set(name, value)`. Persists in the save. | replayed |
| `if_flag <name> <value> <label>` | no | If `flag_get(name) == value`, continue at `<label>`; otherwise fall through. Unset flags read as 0. | executed |
| `choice "<prompt>" "<opt1>" <label1> "<opt2>" <label2>` | until picked | Shows a two-option panel. The player picks with the Choice 1 / Choice 2 actions (keys `1` / `2` by default) or by clicking or tapping an option. Stores the pick (1 or 2) in the flag `ch_<key>_<pc>`, then continues at the chosen label. While the panel is up, interact and vehicle exit are disabled. Prompt is stored up to 127 chars, options up to 79, labels up to 31. | jumps using the stored `ch_` flag (2 means option 2, anything else option 1) |
| `checkpoint` | no | Records `pc + 1`, the player's (or vehicle's) position, yaw, whether in a vehicle and that vehicle's type. Shows "Checkpoint" and plays a sound. | no-op |
| `fail ["<reason>"]` | stops | Fails the mission with `<reason>` (default `Mission failed`). | skipped |
| `end` | stops | Completes the mission (same as running off the end). | skipped |

There is no gamepad mapping for Choice 1 / Choice 2. A player with only a gamepad needs the
keyboard, mouse or touch to answer a `choice`.

### 7.2 Spawning and controlling actors

| Command | Blocks | Behaviour | On retry |
|---|---|---|---|
| `spawn <tag> <kind> <preset> <pos> [yaw=0] [faction] [weapon] [mode]` | no | Spawns one actor with `<tag>` (15 chars max). `<preset>` is an appearance preset or `-` for the kind's default; it is required as a placeholder. Faction, weapon and mode are optional; unknown or omitted values keep the kind's defaults. A chosen weapon gets a full clip. Spawned actors are `persistent` and their `home` is the spawn point. Drones are raised 6 m. If the actor pool is full, nothing spawns. | replayed |
| `group <tag> <kind> <preset> <count> <pos> [radius=0] [faction] [weapon] [mode]` | no | Spawns `count` actors spread on a ring around `<pos>` (angle jittered ±0.3 rad, distance 40-100 % of `radius`), each ground-snapped and facing the player. | replayed |
| `name <tag> "<name>"` | no | Display name (23 chars) for every actor with the tag; also used in `protect` failure messages. | replayed |
| `health <tag> [hp]` | no | Sets max health and health (default: the current max). | replayed |
| `mode <tag> <mode>` | no | Applies an AI mode (table below). | replayed |
| `hostile <tag>` | no | Marks as hostile to the player and puts the AI into combat against the player. | replayed |
| `friendly <tag>` | no | Clears hostility, sets AI to `follow` **and sets the faction to A.E.G.I.S.** | replayed |
| `nonlethal <tag>` | no | Damage knocks the actor out instead of killing it. | replayed |
| `invulnerable <tag> [0\|1=1]` | no | Toggles invulnerability. | replayed |
| `face_player <tag>` | no | Turns the actors to face the player instantly. | replayed |
| `goto_ai <tag> <pos> [speed=3]` | no | AI walks to `<pos>` (AI_GOTO; the speed value goes into `scriptTimer`). | replayed |
| `slay <tag>` | no | Scripted execution: removes invulnerability and non-lethal, then deals 99999 head damage (full gore). | the actors are **despawned** instead |
| `despawn <tag>` | no | Removes every actor with the tag, plus the first vehicle with the tag unless the player is driving it. | replayed |
| `protect <tag>` | no | Adds a fail condition (up to 4 tags; names over 15 chars are cut): the mission fails with `<name> was killed` (or `An ally was killed`) when the **first** actor found with the tag is dead or no actor has the tag. | replayed |
| `unprotect <tag>` | no | Removes the tag from the protect list. | replayed |

**Kinds** (`kind_by_name`; an unknown kind spawns a `civilian`): `player`, `civilian`,
`thug`, `gunman`, `fighter`, `police`, `aegis`, `operative`, `stalker`, `brute`, `tendril`,
`overseer`, `drone`, `story`.

**Appearance presets** (`appearance_preset`; an unknown name gives a randomised human):
`damian`, `marcus`, `eli`, `tommy`, `fighter`, `brick`, `kings`, `police`, `aegis`,
`kincaid`, `thorne`, `okafor`, `scientist`, `operative`, `varga`, `varga_thug`, `stalker`,
`brute`, `tendril`, `overseer`.

**Factions** (an unknown or omitted name keeps the kind's faction): `player`, `civilian`,
`police`, `kings`, `varga`, `aegis`, `operatives`, `alien`, `fighter`.

**Weapons** (an unknown or omitted name keeps the kind's weapon): `fists`, `pistol`,
`shotgun`, `smg`, `rifle`, `magnum`, `arc`, `lance`, `shard`, `claws`, `drone`.

**AI modes** (`apply_mode`; anything else, including `-`, does nothing):

| Mode | Effect |
|---|---|
| `hold` | AI_HOLD (holds position; used for pit fighters waiting for their round) |
| `stand` | AI_STAND, indefinitely |
| `guard` | AI_GUARD |
| `patrol` | AI_PATROL around the current position |
| `wander` | AI_WANDER |
| `follow` | AI_FOLLOW (follows the player) |
| `talk` | AI_TALK |
| `spectate` | AI_SPECTATE: watches the fight pit (goal = fight pit + (0, 0, 25)) |
| `hostile` or `combat` | same as the `hostile` command |
| `cower` | AI_COWER, indefinitely |

### 7.3 Vehicles

| Command | Blocks | Behaviour | On retry |
|---|---|---|---|
| `vehicle <tag> <type> <pos> [yaw=0] [r g b]` | no | Spawns a parked, persistent vehicle with the tag. The colour applies only when all three components (0-1) are given; otherwise it is random. Types: `sedan`, `muscle`, `suv`, `sports`, `van`, `police`, `aegis`, `taxi` (an unknown type spawns a `sedan`). | replayed |
| `lock_vehicle <tag> <0\|1>` | no | A locked vehicle with no driver cannot be entered ("This car is locked"). | replayed |

### 7.4 Player, world and progression

| Command | Blocks | Behaviour | On retry |
|---|---|---|---|
| `teleport <pos> [yaw=0]` | no | Moves the player (leaving any vehicle first) and turns the camera to `yaw`. | **replayed, see 8.2** |
| `give_weapon <name> [ammo=30]` | no | Adds ammo to reserve (capped at the weapon's maximum). The first time a weapon is given, its clip is filled and it is selected. `fists`, `claws` and `drone` are ignored. | **replayed (ammo added again)** |
| `give_money <n>` | no | `add_money(n)` with a "+$n" notification. Negative values subtract, and money never goes below 0. | **replayed (paid again)** |
| `reward <n>` | - | Intended to override the completion reward, but unreachable because `reward` is parsed as a header key (section 4). | - |
| `heal` | no | Restores full health. | replayed |
| `armor <n>` | no | Adds armour, capped at 100. | replayed |
| `rep <faction> <delta>` | no | Changes reputation, clamped to -100..100. The Kings and Varga factions turn hostile below -20. | **replayed (applied again)** |
| `wanted <0-5>` | no | Sets the wanted level. | **replayed** |
| `clear_wanted` | no | Clears the wanted level. | replayed |
| `phase <0-3>` | no | `game_set_phase` (section 11). | replayed |
| `power <0\|1>` | no | Turns street lights and window lighting on or off. | replayed |
| `rain <0-1>` | no | Rain target; the actual rain eases towards it at 0.05 per second. Outside missions the weather also changes randomly. | replayed |
| `evidence <id> <pos>` | no | Spawns evidence pickup `<id>` (0-15) at `<pos>` unless it is already collected. Collected by walking onto it, on foot. | **replayed (spawns a duplicate pickup)** |
| `unlock_safehouse` | no | Enables the safehouse bed (rest and save to slot 1). | replayed |
| `pit_win` | no | Increments the pit-win and fights-won stats. | **replayed (counted again)** |
| `timer <seconds>` | no | Starts a countdown. At 0 the mission fails with `Time ran out`. | replayed (restarts at full time) |
| `timer_stop` | no | Stops the countdown. | replayed |

### 7.5 Presentation

| Command | Blocks | Behaviour | On retry |
|---|---|---|---|
| `objective "<text>"` | no | Sets the objective text. | replayed silently |
| `notify "<text>"` | no | 4-second notification (at most 6 on screen). | suppressed |
| `say <speaker> "<text>"` | no | Queues one subtitle line (section 13). Does not wait. | skipped |
| `music <mood>` | no | Calls `audio_set_music(mood, 1.0)` with `calm`, `tension`, `combat`, `alien` or `chase` (anything else means silence). **Caution:** `game_frame()` calls `update_music()` every frame and recomputes the mood from the situation (combat nearby, wanted level, phase 3, in mission), so this command is overwritten in the same frame and currently has no audible effect. | replayed |
| `sound <name>` | no | One-shot: `screech`, `explosion`, `cheer`, `elevator`, `thunder` (anything else is silent). | suppressed |
| `fx_explosion <pos> [radius=5] [damage=0] [alien=0]` | no | Explosion 0.5 m above `<pos>`, no owner. | suppressed |
| `fx_flash` | no | White screen flash. | suppressed |
| `fx_shake [amount=0.5]` | no | Camera shake (total capped at 1.5). | suppressed |
| `shard_glow [seconds=3]` | no | Shard glow tint and pulse sound. | suppressed |

### 7.6 Blocking commands

| Command | Completes when | Objective / marker | On retry |
|---|---|---|---|
| `wait <seconds>` | the step has run for `seconds` | - | skipped |
| `dialogue <id>` | the conversation has finished (an unknown id completes at once) | - | skipped |
| `cutscene <id>` | the cutscene has ended or been skipped (an unknown id completes at once) | - | skipped |
| `fade_out` | 0.9 s have passed (fades to black) | - | skipped |
| `fade_in` | 0.6 s have passed (fades back in) | - | skipped |
| `goto <pos> [radius=4] ["obj"]` | player (or their vehicle) within `radius` in XZ and within 6 m vertically | objective; marker at `<pos>` | skipped |
| `drive_to <pos> [radius=4] ["obj"]` | like `goto`, but only counts while the player is in a vehicle (any vehicle) | objective; marker | skipped |
| `goto_actor <tag> [radius=3] ["obj"]` | 3D distance to the first actor with the tag is below `radius`; completes at once if no actor has the tag | objective; marker on the actor | skipped |
| `enter_vehicle <tag> ["obj"]` | the player is driving that vehicle; completes at once if no vehicle has the tag. **Fails** the mission (`The vehicle was destroyed`) if it is destroyed. | objective; marker on the vehicle | skipped |
| `exit_vehicle ["obj"]` | the player is on foot | objective | skipped |
| `talk <tag> ["prompt"=Talk] ["obj"=Talk]` | the player interacts within 2.6 m of the actor, who then turns to face them; completes at once if no actor has the tag | objective; marker on the actor | skipped |
| `interact <pos> [radius=2] ["prompt"=Use] ["obj"=prompt]` | the player interacts within `radius` | objective; marker | skipped |
| `hold <pos> [radius=2] [seconds=5] ["obj"=Hold position]` | on foot, within `radius` (XZ) and 3 m vertically, holding interact for `seconds` in total. Progress pauses when not holding and drains at 5 %/s outside the area. | objective; marker; progress bar | skipped |
| `pickup <tag> <pos> ["obj"=Take the item]` | the player takes the item (spawned on the first frame) by interacting within 2 m | objective; marker | skipped |
| `kill <tag> ["obj"]` | no actor with the tag is alive and conscious (knocked out counts as defeated); completes at once if none exist | objective; marker on the nearest remaining target, shown only when it is more than 12 m away | tagged actors are despawned |
| `defeat <tag> ["obj"]` | identical to `kill` (pair it with `nonlethal` for knockouts) | as `kill` | as `kill` |
| `kill_count <tag> <n> ["obj"]` | at least `n` actors with the tag are dead or knocked out (and not yet despawned) | objective only | skipped (nothing despawned) |
| `survive <seconds> ["obj"]` | the step has run for `seconds` | objective. **Caution:** sets `timerActive = false`, so it cancels any running `timer` and its own countdown is not shown on the HUD. | skipped |
| `lose_wanted ["obj"=Lose the police]` | wanted level is 0 | objective | skipped |
| `choice ...` | see 7.1 | prompt panel | see 7.1 |

---

## 8. Checkpoints, failure, retry and fast-forward

### 8.1 Failure and completion

A mission fails through `fail`, a `protect`ed actor dying, the `timer` reaching 0, a
destroyed `enter_vehicle` target, or the player dying (after a 3 s death timer, reason
`Damian died`). Being **arrested** during a mission also fails it, with the same
`Damian died` reason. The failure screen offers **Retry from checkpoint** and
**Abandon mission**.

On completion (`end` or the last command):

1. The reward (header `reward`) is paid.
2. A non-repeatable mission gets its done bit.
3. `phase_after` is applied if it is higher than the current phase.
4. Tags are released: tagged actors stay in the world but lose tag, persistence and
   invulnerability; holding or following actors go back to wandering (civilians) or
   standing. Tagged vehicles lose their tag. Tagged mission items are removed.
5. Dialogue is cleared, so a `say` just before `end` is cut off.
6. An **autosave** is written to slot 0, and the Mission Complete screen is shown.

**Abandon** (and quitting to the main menu) despawns every tagged actor and every tagged
vehicle the player is not driving, clears dialogue, skips any cutscene and releases the
camera. Saving to slot 1 is refused while a mission runs, so a mission can never be
resumed from a save.

### 8.2 Retry

`mission_retry()`:

1. Despawns tagged entities, clears dialogue, skips any cutscene, clears the wanted level.
2. Respawns the player with full health and teleports them to the checkpoint position.
3. Resets all run state (objective text, timer, protect list, marker).
4. **Fast-forwards** from command 0 to the checkpoint `pc` (table below).
5. If the checkpoint was taken in a vehicle, spawns a new untagged vehicle of the same
   type at the checkpoint and puts the player in it.

Without a `checkpoint`, retry restarts from command 0 at the place where the player
started the mission.

| During fast-forward | Commands |
|---|---|
| **Skipped** (no effect at all) | `goto`, `drive_to`, `goto_actor`, `enter_vehicle`, `exit_vehicle`, `talk`, `interact`, `hold`, `pickup`, `dialogue`, `cutscene`, `wait`, `survive`, `lose_wanted`, `kill_count`, `fade_out`, `fade_in`, `say`, `end`, `fail` |
| **Reconstructed** | `kill` / `defeat`: every actor with the tag is despawned. `slay`: despawn instead of kill. |
| **Executed** (branching reproduced) | `jump`, `if_flag`, `choice` (uses the stored `ch_` flag) |
| **Suppressed** (no output) | `notify`, `fx_explosion`, `fx_flash`, `fx_shake`, `shard_glow`, `sound`; `checkpoint` is a no-op; `objective` sets the text without the sound |
| **Replayed in full** | everything else: `spawn`, `group`, `vehicle`, `name`, `health`, `mode`, `hostile`, `friendly`, `nonlethal`, `invulnerable`, `face_player`, `goto_ai`, `despawn`, `protect`, `unprotect`, `lock_vehicle`, `flag`, `give_weapon`, `give_money`, `heal`, `armor`, `rep`, `wanted`, `clear_wanted`, `phase`, `power`, `rain`, `music`, `teleport`, `evidence`, `unlock_safehouse`, `pit_win`, `timer`, `timer_stop`, `label` |

Fast-forward is capped at 1000 steps.

> **Caution: replay side effects.** Because state commands before the checkpoint are
> replayed in full:
> * `teleport` runs **after** the player was placed at the checkpoint, so a teleport
>   before the checkpoint wins. Shipped cases: `m09` (both checkpoints restore the player
>   at `@nexus 0 -30` instead of the substation or precinct), `m13` (both checkpoints
>   restore to `@nest 0 -134`), `m07` second checkpoint (`@nest 0 -134`), `m12` first
>   checkpoint (`@aegis_lobby 0 -3`).
> * `give_money`, `give_weapon`, `rep` and `pit_win` apply again on every retry. Shipped
>   cases: `pit_win` in `m03`; `give_weapon` in `m04`, `m05`, `m07`, `m12`; `rep varga` in
>   `m11` (second checkpoint).
> * `wanted` is re-applied while `lose_wanted` is skipped: retrying `m04` from its second
>   checkpoint (after the ambush) gives the player 2 wanted stars again.
> * `evidence` spawns another pickup for the same id on each retry (for example `m07`,
>   evidence 11).
> * `timer` restarts at its full duration.
>
> Put one-time rewards and teleports **after** the last checkpoint that can follow them,
> or re-establish state explicitly after each checkpoint.

### 8.3 Debug completion

`mission_debug_complete_step()`, used by the autotest every 12 frames, completes the current
blocking step: it kills the targets of `kill`, `defeat` and `kill_count`; teleports the
player (spawning a sedan for `drive_to`) for movement steps; and finishes `talk`, `pickup`,
vehicle steps, `lose_wanted`, `dialogue`, `cutscene`, `choice` (always option 1), `wait`
and `survive`.

---

## 9. Choices and their persisted flags

`choice` stores the pick in a story flag named `ch_<key>_<pc>`, where `<key>` is the
header `key` and `<pc>` the 0-based command index of the `choice` line. The value is 1
or 2. The flag is saved like any other flag, but the code reads it **only during retry
fast-forward**. For story consequences, set your own flag in each branch, as the shipped
missions do.

Choice flags in the shipped missions (computed by running the real tokenizer over the files):

| Mission | File line | Flag | Option 1 to label | Option 2 to label | Story flag set by the branches |
|---|---|---|---|---|---|
| m02 Debts to Collect | 33 | `ch_m02_22` | `spare` | `beat` | `tommy_spared` 1 / 0 |
| m06 The Strangers | 43 | `ch_m06_31` | `join` | `demand` | `pressed_thorne` 1 (demand only) |
| m08 The Signal | 37 | `ch_m08_26` | `confront` | `tellk` | `confronted_thorne` 1 / `trust_kincaid` 1 |
| m11 Varga's Last Deal | 30 | `ch_m11_19` | `spare` | `execute` | `varga_spared` 1 / 0 |
| m11 Varga's Last Deal | 51 | `ch_m11_40` | `forgive` | `leave` | `marcus_forgiven` 1 / 0 |
| m12 Betrayal | 57 | `ch_m12_46` | `arrest` | `execute` | `thorne_dead` 0 / 1 |

> **Caution:** inserting or removing commands above a `choice` changes its `pc` and so
> its flag name. Old saves keep the old flag, which is harmless because these flags are
> only read during an in-session retry.

Flag names are stored in 24 bytes (23 characters). `flag_set` truncates longer names,
but `flag_get` compares the full name, so **a flag name over 23 characters can be written
but never read back** (and each write adds a new entry). `ch_<key>_<pc>` is at most 23
characters when the key is **16 characters or fewer** (`pc` can reach 219). There are at
most 64 flags in total (`MAX_FLAGS`), shared by all missions; once full, new flags are
silently dropped.

---

## 10. Story flags used by the game

Found with `grep -rn "flag_get\|flag_set" src` and `grep -n "flag\|if_flag" assets/missions/*.mis`:

| Flag | Set by | Read by | Effect |
|---|---|---|---|
| `tommy_spared` | m02 (1 spare / 0 beat) | m02 `if_flag`, m09 `if_flag`, ending screen | Debrief variant; Tommy helps defend the precinct in m09; ending line |
| `has_shard` | m05 (1) | nothing | informational only |
| `aegis_access` | m05 (1); autotest when unlocking | `gather_world_interactables` | Enables the Nexus and A.E.G.I.S. elevators and the Armory upgrades interaction |
| `pressed_thorne` | m06 (1, "demand" branch) | nothing | informational only |
| `drain_access` | m07 (1) | `gather_world_interactables` | Enables the storm drain hatch and ladder |
| `confronted_thorne` | m08 (1, "confront" branch) | nothing | informational only |
| `trust_kincaid` | m08 (1, "tellk" branch) | m09 and m12 `if_flag`, ending screen | Briefing variants; ending line |
| `varga_spared` | m11 (1 / 0) | ending screen | Ending line |
| `marcus_forgiven` | m11 (1 / 0) | m14 `if_flag`, ending screen | Marcus joins the final battle; ending line |
| `thorne_dead` | m12 (0 arrest / 1 execute) | ending screen | Ending line |
| `lantern_fired` | m14 (1); also cutscene `fx lantern` | `setpieces.cpp` | Mothership retreat set piece |
| `game_complete` | m14 (1) | Mission Complete screen (menu.cpp); autotest | Value 1: Continue opens the ending and sets the flag to 2 |
| `autotest_flag` | autotest save/load step | autotest | test only |
| `ch_<key>_<pc>` | `choice` | retry fast-forward | see section 9 |

---

## 11. Story phases

`g.pd.phase` (saved) is the campaign era. It changes through the `phase` command or the
`phase_after` header, and gates missions through `requires_phase`. `game_set_phase()`
clamps to 0-3, sets power on except in phase 2, applies the world visuals, enables the
invasion set pieces from phase 3 on (they are never switched off again), and shows a
notification when the phase changes.

| Phase | Name | Entered in shipped content | Effects in code |
|---|---|---|---|
| 0 | Crime | new game | Shooting during a mission reports a crime (in later phases only outside missions). A.E.G.I.S. soldiers do not drop Arc carbines. |
| 1 | A.E.G.I.S. | `m05` `phase_after 1` | m06 `requires_phase 1`. A.E.G.I.S. Arc carbines can drop. |
| 2 | Blackout | `m09` `phase 2` | Power off (street lights out, windows at 6 %), denser fog, colour saturation 0.75, half the pedestrians and traffic. |
| 3 | Invasion | `m10` `phase 3` | Invasion sky and atmosphere, invasion set pieces, default music `alien`, one third of the pedestrians and a quarter of the traffic, alien ambient spawns, no Kings street spawns, and **no new crimes or wanted levels**. |

---

## 12. Cutscenes (`cutscenes.cut`)

```
@<id>
shot <seconds> <fov> <from> <to> <lookFrom> <lookTo>
fx <name>
line <speaker> "<subtitle>"
```

* `@<id>` (first token starting with `@`) starts a cutscene (id up to 39 chars). Other
  tokens on that line are ignored.
* `shot`: duration in seconds and vertical field of view in degrees, then four positions.
  Each shot is a **hard cut** to `from` looking at `lookFrom`. The camera then moves to
  `to` / `lookTo` with smoothstep easing over the duration, and the FOV stays fixed.
* Cutscene positions are **not** the mission format:
  * `@<landmark> <dx> <dy> <dz>`: 4 tokens; world-axis offset including height. An
    unknown landmark silently uses the origin.
  * `P <right> <up> <forward>`: 4 tokens; relative to the player and **rotated by the
    player's facing**, re-evaluated every frame.
  * `<x> <y> <z>`: 3 tokens, absolute.
* `fx <name>` and `line ...` apply to the most recent shot (one of each per shot; a later
  one replaces the earlier). `fx` runs when the shot starts: `flash`, `shake`, `shard`,
  `explosion`, `screech`, `fade` (start from black and fade in), `lantern` (fires the
  LANTERN set piece, sets `lantern_fired = 1`, flash, shake, sounds).
* `line` queues the subtitle through `dialogue_say` when the shot starts (speaker up to 15
  chars, text up to 239). Its on-screen time comes from the text length (section 13),
  **not** the shot duration (the duration argument of `dialogue_say` is unused).
* Skipping: Pause (Esc / P / gamepad Start), Jump (Space / gamepad A), Enter, or a tap in
  touch mode ends the whole cutscene and clears the subtitles. While a cutscene plays,
  the HUD is hidden, letterbox bars slide in and the player takes no damage.

> **Caution: the 16-token limit truncates every shipped shot.** A shot using four 4-token
> positions has 19 tokens (`shot` + 2 + 16), but the tokenizer keeps 16. The last three
> tokens, the `lookTo` offsets, are dropped and read as 0, so `lookTo` becomes the bare
> landmark position (or the player's feet for `P`). All 26 shots in `cutscenes.cut` are
> affected: for example the first shot of `@m10_sky` was written to tilt up to
> `@plaza 0 300 -40` and instead ends looking at the plaza origin. Until
> `SCRIPT_MAX_ARGS` is raised, keep shots at 16 tokens or fewer, for example by using
> absolute 3-token positions for some of the points.

---

## 13. Dialogue (`index.txt`, `*.dlg`)

`dialogue/index.txt` uses the script tokenizer; the first token of each line is a `.dlg`
file name in `assets/dialogue/`.

`.dlg` files use their **own line reader**, not the tokenizer:

* Lines are read up to 511 characters (the rest is dropped). Trailing `\r` and spaces are trimmed.
* Empty lines and lines whose **first character** is `#` are skipped (an indented `#` is
  not a comment).
* `@<id>` starts a conversation; the whole rest of the line (up to 39 chars) is the id.
* Any other line is `speaker: text`, split at the **first** colon. The speaker is the text
  before the colon (15 chars, not trimmed); leading spaces after the colon are skipped.
  Text is up to 239 chars. Quotes are literal characters here, not delimiters. A line
  with no colon, or before the first `@`, is ignored.
* Conversation ids are global across all files; the first match wins.

```
@m01_notebook
damian: His notebook. Half of it's ash.
damian: "Night shift, Halberd Shipping, Pier 9. Crate 4471 is warm. Crates aren't supposed to be warm."
damian: "Told M. about it. He said forget it." M... Marcus.
```

Playback: `dialogue_start` copies at most **24 lines** of a conversation into the playback
queue (longer ones are cut). Each line stays for `1.4 + 0.052 × characters` seconds; Enter,
or a tap on the upper half of the screen in touch mode, skips to the next line (not while
a choice is open). Subtitles draw only when the Subtitles setting is on, but the timing
runs either way. `dialogue <id>` blocks until the queue is empty. `say` appends one line,
and lines past 24 in the queue are dropped. Starting a conversation replaces whatever is
queued. A blip sound plays per line (lower pitch for `damian`).

**Speaker ids** (`speaker_display`; an unknown id is shown verbatim in grey):

| id | Shown as | id | Shown as |
|---|---|---|---|
| `damian` | Damian | `overseer` | The Overseer |
| `marcus` | Marcus Reyes | `tech` | A.E.G.I.S. Tech |
| `eli` | Eli Cross | `soldier` | A.E.G.I.S. Soldier |
| `tommy` | Tommy Kowalski | `civilian` | Civilian |
| `varga` | Nikolai Varga | `thug` | Kings Thug |
| `kincaid` | Cmdr. Ava Kincaid | `news` | WBRK News |
| `thorne` | Director Elias Thorne | `shard` | ??? |
| `okafor` | Dr. Imani Okafor | `sal` | Sal the Promoter |
| `radio` | Radio | `ace` | Ace |
| `cop` | Officer | `maya` | Maya Ortiz |
| `announcer` | Pit Announcer | `brick` | "Brick" Halloran |
| `operative` | Operative | | |

---

## 14. Evidence journal (`evidence.txt`)

Read by `load_evidence()` the first time the Journal opens, with its own line reader:

* Lines up to 511 characters; empty lines and lines starting with `#` are skipped.
  **`\r` is not trimmed**: save the file with LF line endings.
* `@<id> <title>` starts entry `<id>` (0-15, `MAX_EVIDENCE` = 16). The title is everything
  after the first space (63 chars), or `Evidence` if there is no space. Ids outside 0-15
  are ignored.
* Following lines are joined into the body with single spaces (up to 699 chars).
  Quotes are literal.

```
@2 Fight Pit Betting Slip
A betting slip with a delivery schedule written on the back: "Pier 9 - every 2nd Tuesday - black vans - Nexus."
Somebody at the Pit was watching the docks for money.
```

The Evidence tab lists each collected id with its title and body. Pickup sources:

* **World spots** (spawned on new game or load for uncollected ids): ids 0-4 at the burned
  home, laundromat, fight pit, warehouse and Varga estate; ids 5-10 on alley sidewalks
  (`world_landmarks.cpp`, 11 spots in total).
* **Mission placed** with the `evidence` command: id 2 (m03, also a world spot), 11 (m07),
  12 (m08), 13-15 (s24).

> **Caution:** the counter reads `Evidence collected: X / N` with
> `N = min(world evidence spots, 16)` = 11, while ids 11-15 can only come from missions.
> A player who finds everything sees `16 / 11`.

---

## 15. Limits

| Limit | Value | Where | When exceeded |
|---|---|---|---|
| Missions | 32 | `MAX_MISSIONS` | further files ignored silently |
| Commands per mission | 220 | `MAX_CMDS` | `Mission <file>: too many commands` logged; extra commands dropped |
| Tokens per line | 16 | `SCRIPT_MAX_ARGS` | extra tokens dropped silently |
| Script line length | 639 bytes | `ScriptLine::buf[640]` | truncated |
| Commands executed per frame | 64 | `missions_update` | continues next frame |
| Fast-forward steps | 1000 | `fast_forward_to` | stops early |
| Protected tags | 4 (15 chars each) | `MissionRun::protect[4][16]` | ignored |
| Actor / vehicle tag | 15 chars | `Actor::tag[16]`, `Vehicle::tag[16]` | truncated |
| Actor display name | 23 chars | `Actor::name[24]` | truncated |
| Objective text | 159 chars | `MissionRun::objective[160]` | truncated |
| Story flags | 64, names 23 chars | `MAX_FLAGS`, `StoryFlag::name[24]` | dropped / unreadable (section 9) |
| Mission ids | 0-31 (bit `id & 31`) | `PlayerData::missionsDone` | ids alias |
| Conversation playback queue | 24 lines | `Playing::queue[24]` | extra lines dropped |
| `.dlg` / `evidence.txt` line | 511 chars | local `line[512]` | truncated |
| Evidence entries | 16 (ids 0-15) | `MAX_EVIDENCE` | ignored |
| Evidence body | 699 chars | `EvidenceText::body[700]` | truncated |
| Interactables per frame | 48 | `MAX_INTERACTABLES` | ignored |
| Actors / vehicles / pickups | 128 / 64 / 96 | `game.h` | spawn fails silently |

---

## 16. Worked examples (from the shipped files)

**Header and opening** (`m02_debts_to_collect.mis`, lines 3-14):

```
mission 2
key m02
title "Debts to Collect"
desc "Marcus needs eight hundred dollars from Tommy Kowalski, who runs the Suds City Laundromat. Tommy is late, and Tommy has friends in the Kings."
start diner 0 -1
requires 1
reward 400

spawn marcus story marcus @diner 2 8 200 civilian fists stand
name marcus "Marcus Reyes"
invulnerable marcus 1
talk marcus "Talk [E]" "Talk to Marcus"
```

**Choice with branches and a later flag check** (`m02`, lines 29-60; see the `reward`
caution in section 4):

```
checkpoint
goto_actor tommy 2.5 "Catch Tommy"
mode tommy cower
dialogue m02_caught
choice "Tommy can't pay. What do you do?" "Take what he has and let him go" spare "Make an example of him" beat
label spare
dialogue m02_spare
flag tommy_spared 1
rep kings 5
reward 300
jump report
label beat
dialogue m02_beat
flag tommy_spared 0
invulnerable tommy 0
nonlethal tommy
hostile tommy
defeat tommy "Teach Tommy a lesson"
dialogue m02_beat_after
rep kings -15
reward 650
label report
music calm
goto @diner 0 -1 4 "Bring the money back to Marcus"
talk marcus "Talk [E]" "Hand Marcus his money"
if_flag tommy_spared 1 report_spared
dialogue m02_report_beat
jump done
label report_spared
dialogue m02_report_spared
label done
end
```

**A flag set in one mission changing another** (`m09_blackout.mis`, lines 62-69):

```
if_flag tommy_spared 1 tommy_help
jump defend
label tommy_help
spawn tommy story tommy @police 5 -4 180 civilian shotgun follow
name tommy "Tommy Kowalski"
invulnerable tommy 1
say tommy "Damian! You let me walk once. Figured I owe you one."
label defend
```

**Timed side job** (`s21_iron_circuit.mis`, lines 12-23):

```
vehicle racer sports @chop_shop 10 -15 90 0.9 0.45 0.05
enter_vehicle racer "Get in the race car"
notify "Iron Circuit: hit every checkpoint before the timer runs out."
timer 120
music chase
drive_to @warehouse 0 -10 12 "Checkpoint 1: Halberd Shipping"
drive_to @substation 0 -6 12 "Checkpoint 2: the substation"
drive_to @fight_pit 0 -8 12 "Checkpoint 3: the Fight Pit"
drive_to @chop_shop 0 -15 10 "Finish at Varga's Garage"
timer_stop
music calm
end
```

**Escort with a protected ally** (`m10_first_contact.mis`, lines 17-19 and 35-38):

```
spawn maya story civilian @plaza 2 -1 180 civilian fists cower
name maya "Maya Ortiz"
protect maya
...
mode civs stand
mode maya stand
unprotect maya
dialogue m10_safe
```

**Cutscene** (`cutscenes.cut`, `@m05_taken`; each `shot` line is cut at 16 tokens, see
section 12):

```
@m05_taken
shot 3.0 45 P 1.5 1.2 2.5 P 1.2 1.1 2.2 P 0 1.0 0 P 0 1.2 0
fx shard
line damian "The light... it's inside my head..."
shot 4.5 50 P 0 6 -4 P 0 8 -6 P 0 0.5 0 P 0 0.5 0
fx flash
line kincaid "Target secured. He's alive. Get him on the truck, and somebody find out who those shooters were."
```

---

## 17. Authoring checklist

1. Give the mission a unique `mission` id in 0-31 and a `key` of 16 characters or fewer.
2. Add the file to `missions.txt`.
3. Do not use `reward` below the header. Use `give_money` for branch payouts.
4. Keep every line to 16 tokens or fewer (count quoted strings as one token).
5. Every `jump` / `if_flag` / `choice` target needs a matching `label`.
6. Make sure every `dialogue` / `cutscene` id exists. An unknown id completes the step at
   once, with only a log line to show for it.
7. Place `checkpoint` after expensive sections, and keep `teleport`, `give_money`,
   `give_weapon`, `rep`, `wanted`, `pit_win` and `evidence` after the last checkpoint that
   would replay them (section 8.2).
8. Keep conversations to 24 lines or fewer and flag names to 23 characters or fewer.
9. Do not rely on `music` until `update_music()` respects script moods.
10. Prompt texts do not need `[E]`: the HUD already shows the bound key.

## 18. Known issues found while writing this reference

| # | Issue | Location |
|---|---|---|
| 1 | `reward` in a mission body is parsed as a header key, so per-branch rewards are lost (m02 always pays $650). | `load_mission_file`, mission.cpp |
| 2 | Cutscene `shot` lines exceed 16 tokens, so `lookTo` offsets are dropped in all 26 shipped shots. | `SCRIPT_MAX_ARGS`, script.h; cutscenes.cut |
| 3 | `music` is overwritten every frame by `update_music()`. | game.cpp |
| 4 | Retry replays `teleport` after the checkpoint teleport (m07, m09, m12, m13), re-applies `give_money` / `give_weapon` / `rep` / `pit_win` / `wanted`, and duplicates `evidence` pickups. | `fast_forward_to`, `mission_retry` |
| 5 | `survive` silently cancels a running `timer` and shows no countdown. | `exec`, `survive` |
| 6 | An arrest during a mission fails it with "Damian died". | `game_respawn_player`, game.cpp |
| 7 | Flag names over 23 characters can be set but never read. | `flag_set` / `flag_get`, save.cpp |
| 8 | Evidence counter denominator is 11 while 16 entries are collectible. | `screen_journal`, menu.cpp |
| 9 | Interaction prompts containing `[E]` show the key twice. | hud.cpp; mission prompt strings |
| 10 | There is no gamepad binding for Choice 1 / Choice 2. | controls.cpp |
