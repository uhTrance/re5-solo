# RE5 Solo

A solo Resident Evil 5 story mode. The partner (Sheva by default) is never spawned, and each co-op moment is
reworked so one player can do it. There is no ghost partner. The removed character still appears in in-engine
cutscenes.

## Streaming RE5 Solo?

The mod is free, and you're welcome to stream or record it. If you do, please share my London Marathon fundraiser
with your viewers: **[justgiving.com/page/uhtrance](https://www.justgiving.com/page/uhtrance)**. A link in your
stream description, chat or panels would mean a lot. Thank you!

This is still in testing: see [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for the bugs and gaps so far. The rest of this file
is technical notes. [KNOWLEDGE.md](KNOWLEDGE.md) is the guide for other modders: how the engine works, the methods
and the co-op-to-solo bug patterns, with worked case studies.

## Target

- Steam Gold Edition, `re5dx9.exe`: 32-bit MT Framework, fixed image base `0x400000` (no ASLR). REFramework
  does not support it.
- The exe on disk is SteamStub-wrapped (`.bind` section): analyse a memory dump, not the file. Code is spread
  over several sections (`0xc4xxxx`, `0xcaxxxx`, `0xf0xxxx`, ...).
- Steam's DRM ("INTEGRITY CHECK FAILURE", from `steamclient.dll`) fails on any changed code byte, and
  intermittently on hardware breakpoints or swapped `.rdata` vtable slots. So the mod never patches game code
  or the game's vtables. It edits heap state from a watcher thread (10 ms), and runs code on the game thread
  through per-object vtable copies (`objhook.h`: the object's vtable pointer is swapped for a private copy
  whose update slot calls the original, then ours). Default update slot is `+0x160` for `uOm` objects;
  enemies use `+0x1c` (move, uEnemy `0x9d9b80`).
- Startup FAULTs at `0x1465a71` / `0x1465a9c` are the DRM's own anti-debug traps; ignore them.

## Layout

- `dinput8.dll` (`loader.cpp`, `crashlog.cpp`, `padhook.cpp`): permanent proxy loader (the game imports
  `DINPUT8.dll`). Owns the log (`re5_solo.log`), the crash recorder, the XInput/DirectInput hooks, and memory
  the game may keep pointers to (`g_host->memory`: the boat's dummy player, the hint text).
- `re5_solo_core.dll` (everything else): the mod. The loader runs a copy (`re5_solo_core.loaded<N>.dll`) and,
  when the file changes, clears the filters, calls `CoreShutdown`, waits 300 ms, unloads it and loads the new
  copy. `CoreShutdown` must leave nothing pointing into the core: it stops the threads, removes the F10 watch
  handler and restores every per-object vtable copy. Contract in `src/host.h`.
- `build.bat` builds both plus the launcher; `build.bat deploy` copies them next to `re5dx9.exe` (the loader
  only when it changed, which needs a game restart; the core reloads within a second).
- `RE5 Solo Launcher.exe` (`launcher.cpp`, DLLs embedded via `launcher.rc`): the only file shipped. Finds the
  game via Steam (`SteamPath` + `libraryfolders.vdf`) or Browse (remembered in `HKCU\Software\RE5 Solo`);
  installs, updates, uninstalls; toggles the marker-file options; Play runs `steam://rungameid/21690`. Its
  colours follow the Windows light/dark setting and accent colour. A foreign `dinput8.dll` is kept as
  `dinput8.dll.before-re5solo` and restored on uninstall. A running game's loader is renamed to
  `dinput8.dll.old*` (cleared on the next install or uninstall).
- Install also unpacks both characters' loose scene files (`scenefiles.cpp`, see Scenes); an install without
  them counts as outdated.
- Linux (Proton), untested on real Proton: under Wine (`wine_get_version` in ntdll) the launcher also searches
  the Linux Steam folders (`STEAM_COMPAT_CLIENT_INSTALL_PATH`, `~/.steam/steam`, `~/.local/share/Steam`,
  Flatpak) through the Z: drive, and Install sets `"dinput8"="native,builtin"` in the game prefix's
  `HKCU\Software\Wine\DllOverrides` (Wine otherwise loads its built-in dinput8). In the game's own prefix
  that's the registry; from another prefix it edits `compatdata\21690\pfx\user.reg` (game closed). A missing
  override counts as outdated; uninstall removes only our exact value, and keeps it if another mod's
  `dinput8.dll` is put back. The airboat's XInput hook may not match Proton's xinput1_3 (logged, skipped).
- Crash recorder (`crashlog.cpp`): a vectored handler that only logs (the game has its own handler, no WER
  report); faults outside the exe have a separate 8-entry budget.
- Release: `dist\RE5 Solo <date>.zip` (launcher + `Read me.txt`).
- Updates: the launcher asks GitHub for the newest release on start (`update.cpp`). A tag newer than
  `RE5SOLO_VERSION` (`src/version.h`, raise it with every release; tags are `vMAJOR.MINOR.PATCH`) offers the update;
  on yes it downloads the release's `*Launcher*.exe` asset (else its zip, unpacked with Windows' `tar`), swaps itself,
  restarts with `--updated` and updates an installed mod. So attach the launcher exe to each release beside the zip.
  `RE5SOLO_UPDATE_URL` points the check at another JSON (testing); `RE5SOLO_NO_UPDATE` turns it off.

Marker files next to the exe (read when the core loads):

| File | Effect |
|---|---|
| `re5_solo_record.txt` | Vanilla co-op: Sheva spawns, nothing is hooked; co-op jump landings are logged (`record.cpp`) |
| `re5_solo_debug.txt` | F9 freezes enemies (unit move line 9 speed `+0x14` = 0); F10 arms a hardware write watch on the player's `+0x30` that logs any teleport over 300 units with the writer's EIP and callers; F11 skips the area (`jump.cpp`: the area's exit object `uOmf301`, else the level script's JumpArea target) |
| `re5_solo_jump.txt` | `area [start point] [scene]`: where F11 goes instead (debug keys only) |
| `re5_solo_jump_now.txt` | Same format: jumps there once and is deleted (works without the debug keys) |
| `re5_solo_trace.txt` | Script tracer: logs each script function's first run per param, scans for level scripts, "hot:" lines = calls per 5 s |

## Tools

- `re/re5dx9_dump.bin`: image read from the running game (`0x400000..0x1928000`, not committed).
- `re/`: `peek.py`, `poke.py`, `snap.py` (object snapshots/diffs), `findobj.py` (by vtable), `whatis.py`
  (object containing an address), `classvt.py`, `disasm.py`, `xref.py`, `tp.py x y z` (teleport),
  `fsm_dump.py` (offline level-script dump), `fsmstate.py <order>` (a script's current state), `needs2p.py`
  (functions that call getPlayer(1) or the partner getter, by class; output `needs2p.txt`), `arc_types.py`
  (type hash = `jamcrc(class) & 0x7fffffff` -> extension, `arc_types.json`).
- `tools/crashwatch.ps1`: waits for the game, shows crash-recorder lines live and, when the game exits, copies
  `re5_solo.log` (rewritten at each launch) into `crashlogs/` and lists new minidumps. `re/dmpread.py` reads
  dwords out of a minidump.
- `tools/loose_sheva.py [--chris] [--remove]`: Python version of the launcher's scene-file unpack.
- Write watch (`debug.cpp`): a hex address in `re5_solo_watch.txt` next to the exe arms a 1-byte hardware write
  watch there (checked each second, hot) and logs the first 8 distinct writers with stacks; delete to clear.
  Class/property names are a good way in: each class's vtable +0xC registers its properties (name, type,
  field address), and +0x10 is getDTI.
- Python `pefile`, `capstone`; Cheat Engine; MSVC.

## Core facts

| What | Where |
|---|---|
| Class names | `uPl00Chris` str `0x101e9fc`, `uPl01Sheba` str `0x101ea08` (Capcom spells it "Sheba") |
| DTI records | Chris `0x120328c` (vtbl `0x101aac8`), Sheba `0x12032b0` (vtbl `0x101ab20`); static init `0xf01a0f` / `0xf01a4f` |
| Object vtables | Chris `0x101da00`, Sheba `0x101dc00`; size `0x2f80`, shared uPlayer ctor `0xb7b260` |
| Player spawn loop | `0x7e99f0`: for slot 0..3, if `activeMask & (1<<slot)` -> `spawnPlayer(slot)` `0x7e9800` (thiscall, ret 4) -> `0x7e9730` (DTI lookup -> newInstance) |
| Session info | `[[0x12340a4]+0x1042c]`: `+0x478` active-slot mask (story = 3), `+0x480` control per slot (1 local, 2 remote); slots are per character (0 Chris, 1 Sheva) |
| Solo flag | `[0x12340a4]+0x10434` bit `0x80000`, the game's own one-player mode (set at `0x718597`). `requiredPlayers()` `0xa83070` = flag ? 1 : 2 |
| Chosen character | `[0x11b2158] + 0x5940*[+0x20] + 0x273c4` (0 Chris, 1 Sheva); SceNo at `+0x273e0` (setter `0x726a80`), area at `+0x273d8` |
| Partner | player `+0x2a74`, getter `0xb5bed0`; `PartnerDist` `+0x670`; `mbPartnerReady` getter `0xc40760`, setter `0xabccc0` |
| Player state / action | `+0x10e0` state, `+0x10f8` current action object, `+0x18` parent object, `+0x21a0 + 0x30*i` inventory |
| Unit lists | `[0x12340b0]` = sUnit; move line i at `+0x20 + 0x20*i` (`+0xc` first unit, `+0x14` speed, `+0x18` = i); units chain through `+8`; enemies on line 9 |
| Co-op ready bits | object `+0x18dc` low nibble = ready slots (set `0xa82c50`, test `0xa82ca0`), `+0x17fc` 2 bits/slot = side; shared ready handler `0xaa2dd0` counts against `requiredPlayers()` |
| Co-op classes | `uOmCoopDoor`, `uOmCoopGate`, `uOmCoopCardDoor`, `uOmCoopBox`, `uOmCoopTwist`, `uOmCoopSwitch_S301(_Lever)`, `uOmCoopTreasure`, `uOmCoopJumpBroad`, `uOmAreaCoop`; player actions `cPlAction_pl_act_coop_*` |
| Teleporting a player | write every position copy (list in `gap.cpp`) or the collision sweep moves him back |

## Removing the partner (`solo.cpp`, `sheva.cpp`)

- The watcher clears the partner's bit in the active-slot mask before the level's spawn loop runs, so the
  partner is never created, and sets the solo flag whenever only the player's slot is open (the game
  rewrites it on some state changes).
- With the solo flag, doors, gates and anything else using the shared ready handler work for one player.
  Objects with their own hard-coded count of 2 are handled below.
- The solo slot is the chosen character (`SoloSlot` / `SoloPlayer` / `Partner` / `PartnerSlot` in
  `objhook.h`); every fix uses it rather than slot 0. Playing as Sheva is the game's own
  character choice; the old `re5_solo_sheva.txt` (forced Sheva before she is unlocked) is gone, and the
  launcher deletes it on install and uninstall.
- Online join requests still arrive with slot 2 closed; they are left alone.
- **No split screen** (`localjoin.cpp`): offline, a second pad pressing Start (switching between keyboard and
  controller does it) drops in as player 2: `0x723460` books the join (`0x720ce0`) and the game scene restarts
  with two local players, which crashed loading the area (`0x716c80` -> `0x768d0e`; the joinable slot,
  `0xc42cb0`, is -1 with the partner out of the active mask). `0x723460` does nothing while session `+0x54`
  (byte; only reader, cleared only by the session reset `0xc43a39`) is set, so the mod holds it at 1. Backups:
  the pause manager (vtbl `0xfa821c`, scene `+0xe8`) keeps off its drop-out/join pages 7, 9, 10 (items are the
  pages they open; offline `[1 2 5 3]`, 2/3 = Restart/Quit), and a partner bit in the local mask `+0x47c` is
  dropped with `0x716720(0, slot, -1)` + `0x76b520`.
- **Dying is game over** (`death.cpp`): lethal damage leaves HP (`+0x1364`) at 1 with a revive timer
  (`+0x1bac`, seconds); with nobody to revive, the moment the solo player is dying the mod zeroes the timer and
  starts game over with `0x76d9f0` on `[0x123457c]` (as instant kills do; no-op once `+0x2ff8` is set).
- **The partner's loadout** (`inventory.cpp`): the stage record holds both 9-slot loadouts (11 entries of
  `0x2c`, id `+8`, count `+0xc`; Chris's block, Sheva's `0x420` later) in three copies, A `+0x23448` (the
  level's), B `+0x249d8`, C `+0x27618` (Item Management's Ready). A chapter load empties both blocks in A,
  writes back each spawned player's items, then copies A over B and C, so the never-spawned partner's weapons
  were lost for good. The partner's block in C is remembered while no player exists and written back into any
  copy that lost it once the player exists (verified: Chris's weapon survived a Sheva load and reached the save,
  file offset `0x3dac`). Organize can change any one copy with a player present, and those changes are adopted
  too; this is where the open duplication bug is (see Known issues). The saved file has the same blocks at
  `0x3d80` (Chris) / `0x41a0` (Sheva).
- **Save backups** (`backup.cpp`): each new version of `userdata\<account>\21690\remote\savedata.bin` is
  copied to `re5_solo_saves\savedata_<write time>.bin` next to the exe (newest 30 kept).
  `tools\re5save.py` decodes and re-encodes saves (each 8-byte block XORed with the previous encoded one,
  checksum at `+8`).

## Scenes (`scenefiles.cpp`)

In-engine scenes use `uBioActorPlayer` stand-ins (`mPlayerID` `+0x9270`); redirect `0x827050` swaps
`pawn\pl\pl00`/`pl01` paths for the loaded player's model (`player+0xa80`). With no partner the raw path is
requested and the game fails (error at `0x6f96e1`, e.g. on 3-1: `pl0100.mod`). RE5 falls back to loose files
under `nativePC_MT`, so Install unpacks every `pawn\pl\pl01\` file of `uPl01ShebaNormal.arc` and every
`pawn\pl\pl00\` file of `uPl00ChrisNormal.arc` from the player's own archives (model, textures, `.jex`
skeleton, cloth, motions, face, collision). ARC v7: `"ARC\0"`, u16 version, u16 count, 0x50-byte entries
`{name[64], type hash, packed size, size (low 29 bits), offset}`; packed entries are zlib. Files written are
listed in `re5_solo_loose.txt`; uninstall removes only those, and existing files with other bytes are left
alone.

It also unpacks `pawn\wp\wp01\` from `uWp01.arc` (Chris's knife, about 400 KB). Sheva's knife is `uWp0e.arc`.
Nutelcio's Custom Weapons Pack replaces that archive (and other `uWp*`), and evidently asks for `wp0100.mod` without packing it (inferred from a
user's crash; the pack itself not inspected). In co-op,
Chris's knife keeps it loaded. With Chris removed, the game quits with "Failed open file ... wp0100.mod".
That happens with the vanilla archive too: playing as Sheva, entering 3-2's Josh segment quit that way
(Windows logs it as an "Application Popup" event in the System log, with no fault or dump). An install made
before the knife unpack lacks those files: `tools/loose_sheva.py --knife` adds them. With the model there, the
same load then quit on `pawn\pl\pl00\motion\pl00wp01.lmt` (Chris's knife motions, also in `uWp01.arc`): Josh
plays Chris's `pl00` motions, and only Chris loads the weapon archives' Chris motion files. `--knife` therefore
also unpacks each `uWpXX.arc`'s own `pl00wpXX.lmt` (31 files); the launcher's Install does the same. Next it
quit on the knife's effect (`effect\adh\wp\wp_knif.adh`), so all of `uWp01.arc` is unpacked now (its sound and
`.efl` files ship loose already).

## Co-op objects

- **Elevators** (`elevator.cpp`, `uOmElevator` + 11 subclasses): own ready handler (vtbl `+0x184`,
  `0xac6d50`) needs 2 ready bits, ignoring the solo flag. Press = event 0 (`cActPlReady` vtbl `0xfe70e4`,
  `+0x20` elevator), `0xf1` = try go. Once the player is ready: mark the partner's bit, resend `0xf1`.
- **Twist levers and button doors** (`twist.cpp`, `uOmCoopTwist` e.g. 4-1 `uOm08e4`, ready handler
  `0xaafad0`; `uOmCoopCardDoor` e.g. 4-2 `uOm0918`, `0xaa0f30`): same fix as elevators.
- **Boxes** (`box.cpp`, `uOmCoopBox` DTI `0x11f87e4` + 5 subclasses): `+0x1a48` = players needed. Press
  (event 0, handler `+0x188` `0xa9b530`) sets ready bit, at-box bit (slot+4), `+0x1a62[slot]` pushing and the
  side. Go (`0xf1` in `+0x184`, `0xa9b1a0`) with one of two ready only shakes. Setting `+0x1a48` = 1 does not
  push. Fix: mark the partner ready/at box/pushing on the player's side and resend `0xf1`; cleared on let go.
- **Pulleys** (`pulley.cpp`, `uOmCoopSwitch_S301`: 4-2 `uOma006`, `uOm0939..093D`): go needs handle bytes
  `+0x1a51/52` both 2. One pull marks the other handle and sends `0xf1` once per pulley (repeating it broke
  later pulleys). `+0x1a50` (handles usable only with a partner, so no prompt solo) is cleared on hook.
- **Cranks** (`crank.cpp`, `uOmCrank` + 9 subclasses): wind `+0x1970` (shadow `+0x17fc`, percent `+0x197c`),
  max `+0x1974` (300); the gate follows the wind and unwinds on release. Wind is pinned at 50% of max (three
  turns of the 2-2 crank) once reached. `+0x19f5` (finished) blocks turning. Valves (`+0x19a4` bit 4, e.g. 3-2 `uOm08df`) are skipped:
  they finish at max by themselves. In `cActPlWind` the crank is `+0x20`, the player `+0x24` (reversed vs the
  jump actions), so every hook checks the class (`IsA`).
- **3-2 crocodile raft and bridge** (`croc.cpp`, crank `uOm08a6` vtbl `0xfccda0`): the raft (`uOm08a3`,
  crank `+0x1a00`) follows the crank percent. While the player stands on the raft the crank is wound at
  11.5/s (Sheva's rate); off it, the vanilla unwind (~4.4/s) takes it back. The same class raises the bridge
  (links a `uOmFollow`, max 150); it is held at max once fully wound.
- **3-1 marsh crocodiles as Sheva** (`croc.cpp`): `uEm66` waits in `cEm66ActAmbush` until its AI
  (`uEmIntel66`, vtbl `0xf97460`, its own unit) picks a target. The AI's think (`+0x94`, `0x9b93d0`) runs
  `0x9b7e70`, which only scans slots below the player count `[[0x11b27dc]+0x34]` (1 in solo), so it never
  reached Sheva in slot 1. The AI gets a vtable copy whose think (`+0x94`; `+0x98` is empty, and the update `+0x70` is not called through the object's vtable) raises the count to reach the player's slot
  for that call only. 62 places read the count; others may share the blind spot when playing as Sheva.
  The AIs think on several worker threads at once, so the raise is shared (first think in raises it, last out
  restores it under a lock). Per-think save/restore let one think drop the count back to 1 while another ran:
  that crocodile's range flags (`+0x1bb4`) read 0 for a frame, and one surfacing then (state 6, the action at
  `0x8ff9f8` sets `+0x1ba0` when flag 4 is clear) gave up, dived and waited out its 5 s cooldown (`+0x17fc`).
- **3-1 marsh checkpoints at scene 4200** (`croc.cpp`): EmMng starts on SceNo. 4210 goes to its marsh zone
  checks; 4200 (chapter start) waits for a one-off "player on the dock" check (manager 6 zone 27, run once as
  the area loads), then RSF 10, before it sets 4210. A marsh checkpoint saved at 4200 reloads far from the dock
  and the script waits for good: no crocodiles. Away from the dock at 4200, the scene and the checkpoint
  snapshot's copy (`+0x41d0`) are moved to 4210, so the next checkpoint reload starts in the marsh.
  A load from the menu reads the save file again (still 4200), and the watcher can lose the race with EmMng's
  first update. EmMng then sits in `chkRSF10_demo`, `wait` or `chkRSF10_begin` (RSF 10, which such a save lacks);
  found there with the scene at 4210 (via its `chkScenum`: the state with two SceNo transitions, from the FSM
  definition `[[order+0x10]+4]`, states at `+0x18`, count `+0xc`), its current state (`core+8`) is set back to
  `chkScenum`, which then takes its 4210 transition.
- **Two-man levers** (`lever.cpp`, `uOmSwitch` pairs, e.g. 3-3 `uOm08f3`): index `+0x19a0` (0/1), partner
  `+0x199c`; a pull sets `0x1202040[index]`, the pair goes when both are set, checked at the pull. The
  partner's byte is set when the player walks up to a lever, plus a mirror after the pull as fallback (which
  must ignore the byte the mod set). The marked lever moves with the pull but its own state (`+0x1970`) stays
  0, and the room (`aS307`) powers on only when both are 1, so once one lever is on the mod sets its partner's.
- **Lever lift** (`lift.cpp`, 3-3 `uOm0944`, a `uOmSwitch` whose object is the lift): stepping onto the
  platform (within 130 of `+0x30`, edge-triggered, lever idle) runs the pull handler's steps (`+0x1985`
  pressed, ready bit, toggle `+0x17fc`, action 3) with no player; with a player, action 3 moves him to the
  lever.
- **Lever platform** (`lift.cpp`, 3-3 `uOm090d_Floor`, lever `uOm08bc` linked only by script): position =
  blend `+0x1988` of two end points; actions 0 idle, 1 speed up, 2 slow down, 3 cruise, 4 stopped, each
  copying params `+0x17fc..+0x1808` in on start. Stepping on starts action 1 with `{blend, +-1, 0, 0.1}`; at
  the far end action 4 with `+0x17fc` = that end. Re-arms after the player has been off for 1 s.
- **Jack platforms** (`lift.cpp`, area 500 `uOm0924_Container`): in co-op you shoot the platform your partner
  stands on. Action `+0x10e0`: 1 down, 2 rising, 0 up, 3 lowering; writing it alone does nothing, the action
  call (vtbl `+0xdc`) does. Shooting a down platform while standing on another down one puts the shot one
  back and raises the player's.
- **Gap jumps and boosts** (`gap.cpp`, `uOmCoopJumpBroad`; subclass `uOmf111` = broad jump, `uOmf112` = jump
  up 4 m): the crossing is the pair animation, so solo nothing moves. Both jump actions also wait on the
  player manager's flag `+0x38`, set in its update (`0x7e9520`) only with 2 players in position (hard-coded).
  Fix: the manager hook sets the flag; once the player is ready he is put on foot and moved along the object's
  axis (`+0x80` row) away from his side: f111 600 across, f112 102 forward and 405 up (co-op recording: 101.9, 400). He is placed slightly above the ledge
  and settles onto it.
- **3-1 airboat** (`boat.cpp`, `uOm1306` vtbl `0xfbffa8`): holding aim leaves the wheel for the front deck
  (boat coasts), releasing returns. The game's swap ends in `exchange(newDriver, newDeck)` `0xa8c650`, which
  dereferences the missing player and crashes. The boat's update does the moves instead: leave = the deck half
  of exchange (and drops the driving camera mode, `0x843420(cam+0x2de0, cam+0x2dd0)`), take = the game's
  exchange with a blank dummy as deck player. Seat byte `+0x18dc` bits 4-5 wheel, 6-7 deck. A permanent
  partner = self crashes the swap.
- **Pad on the boat** (`xinput.cpp`, `dinput.cpp`, hooks in `padhook.cpp`): on a pad the boat drives with RT
  forward / LT reverse, and RT is also aim. While driving (state `0x6a`) the game sees RT = right stick up,
  LT = stick down, stick vertical centred; `boat.cpp` reads the real RT for aim. XInputGetState (IAT
  `0xf4a40c`) is Windows code, so hooking it is DRM-safe: chained onto Steam's jump when present, else
  hotpatched via the int3 padding. The aim check reads all four XInput slots.
  DirectInput pads: the aim button and right-stick axis come from the game's own layout,
  `Documents\CAPCOM\RESIDENT EVIL 5\config.ini` `[JOYPAD]` `RB` (a DualSense: 7, R2) and `RSTICK_VERT`
  (X/Y/Z/RX/RY/RZ/SLIDER0/1), not a fixed DualSense layout.
- **2-3 jeep turret** (`jeep.cpp`, `uOmJeep` `0xff17b0`, `uOm1304` `0xff1960`): one `uIdCockpit01` per
  player at `+0x1974/+0x1978`; the partner's has no player (`+0x78`) and its first step reads that player's
  inventory, crashing on load. The empty cockpit gets the solo player.
- **4-2 collapsing floor** (`collapse.cpp`, `uOma004_Collapse`): starts its scene only when game flags `0xc8`
  and `0xc9` (one per player in position) are both set; the mod sets the partner's. The QTE's motion callback
  (`0xb073c0`) reads player slot 1 outright; it is wrapped so slot 1 holds the solo player for that call. The
  callback registers and first runs inside `cActPlRun`'s start, so the hook goes in via `cActPlRun`'s type
  object (DTI `0x11ffde0`, create slot): each new `cActPlRun` gets a vtable whose start swaps the callback.
- **4-2 Chris after the QTE** (`solo.cpp`, area 301, scene 6210+): Chris is taken out of the player list and
  parked above the player instead of frozen. While the pause menu (`[[0x1234578]+0x382c]+0xe8`, state `+0x14`)
  or the game-over screen (`[0x123457c]+0x2ff8`) is up he is put back, or Retry / Quit crashed on him. The
  minimap (`uIdMap00` vtbl `0xfa6b48`, entries `+0x610`, 0x30 apart: player `+0x14`, arrow sprites
  `+0x18..+0x20`, visibility byte `+8` each) drops his entry and hides the sprites of entries with no player.
- **6-2 Uroboros chase** (`collapse.cpp`, `uOma00c_UroAction`): each press prompt (`cActAB` vtbl
  `0x100ce68`, update `0xb23340`) checks players 0 and 1 when its timer ends; a player passes on their
  `+0x18dc` bit or as AI (`+0x2da8 == 1`), an empty slot fails and leads to a hang. The update runs with the
  solo player standing in the partner's slot and bits.
- **4-1 separation** (`split.cpp`): after the `uOmAreaPlHit` trigger (x -1404), the area change sets each
  slot to start point [slot] (`0x822e60`, write `0x5837f5`); slot 0's point is the far side. Start records:
  `[[sGame+0x10430]+0x50+4*(area/100)]+4*(area%100)`, 64 bytes (area 300, record 1). The record's base is
  rewritten to its point 1 (the near side) beforehand; a per-frame correction remains as fallback.

## Level scripts and stage logic

- **Zones** (`zones.cpp`): managers `[[0x11e8660]+0xd4+4i]` (count `+8`, list `+0x14`); zone `+4` =
  definition (`+0xc` 0 any / 1 all, `+0x3c` required bits), `+8` = state (`+0x10` players inside; bit 4 = any
  player; `+0x18` its own copy of the requirement). Scripts use `AreaHit` (cFSMOrder vtbl `+0xb4`, `0xc340c0`)
  on "all of 0x13", which needs the partner. The partner's bit is moved to the player in every "all of"
  requirement, in both the definition and the state's copy (every second). Only the copy decides scenario
  zones: the 3-1 dock checkpoint (`cScenarioArg_CHP`, which sets RSF 10 "on land") never fired as Sheva
  while it still read 0x13, so the marsh crocodiles never came.
- **PlayerCheck** (`script.cpp`, cFSMOrder `+0x138`, `0xc37200`): picks a player by the condition's `+0x21`
  (0-3 = that slot, straight from the list) and checks it (`0xc2c200`) to set a flag. An empty slot does
  nothing: the 5-3 Wesker door checked player 0 and never opened as Sheva. In solo an empty slot's check runs
  on the player (the mode byte swapped for the call), and level scripts are patched in solo, not only tracing.
- **Checkpoint zones at two-man levers** (`zones.cpp`, `lever.cpp`): 6-3 (area 505) saves its checkpoint
  (scene 11229) from a `cScenario` zone, a box around the right lever that fires on any player inside when
  the pair goes; the script moves the scene to 11230 straight after. Pulling the left lever alone left it
  empty. Zone `+0x1c` bit `0x10` fires it with nobody inside (`0xcc0997`), so it is set on scenario zones
  (type 3, any of, a real scene) within reach of the lever the mod marks for the player.
- **Player scripts** (`script.cpp`): `cFSMOrderPlayer` (vtbl `0x1040c80`) at player `+0x1890`. `AreaHitPl`
  (`+0x184`, `0xc40830`) bails with no partner; the player is made his own partner for that call only.
  Forcing its result true takes the partner's branch instead.
- **1-1 equip a weapon** (`script.cpp`): stage logic at `0x73a2a9` counts armed players and at 2 sets SceNo
  0x497. At SceNo 0x489 with the player armed (inventory slot `+0x20 == 1`, `+0x2d == 1`, item `+8` !=
  0x137) the mod writes 0x497.
- **1-1 both players in the room** (`script.cpp`): stage controller `[[0x1234578]+0x3830]` (vtbl
  `0xf54310`), step byte `+0x639`. Step 0 (`0x73aa86`) waits for zone 4 of manager 6 to hold both players;
  step 1 picks SceNo 0x4fe/0x4ff by distance. With the player in that zone: SceNo = `0x4fe + slot`, step = 2.
- **Cutscene QTEs** (`script.cpp`): `sEventPlayer` `[0x11b2018]` `+0xf0` = cEventActionButton (`+8`
  active, `+0xc` players who must press, `+0x10` pressed in time, `+0x14` done, `+0x1c` result). A prompt for
  the absent player never completes and the cutscene waits forever (`0x7b2a11`). The partner's bit is set in
  `+0x10` and `+0x14`.
- **Cage** (`cage.cpp`, area 500 `room1_Event00`): waits RSF 25 (set by the ship cutscene, not rerun after a
  checkpoint), SceNo 10240, then RSF 200/201 = which slot entered the cage. Nothing sets these solo, so the
  mod answers each step as the script reaches it: 25; 10240 once the player is within 5 m of the cage (at
  SceNo 10230); 200 + partner slot. RSF bits start at stage `+0x23218`, MSB first per dword (`+0x23214` is the
  flag object's vtable; writing there crashes). The scripts' zone references to the cage zone (vtable
  `0x103d790`, manager 0 zone 16) are repointed at zone 2.
- Script internals: 86 handlers, all `order->vtbl[slot](param)` (thiscall, ret 4), names in
  `src/script_names.h` (from the `0xc29620` registrations). Level scripts = `uFSMTask` (vtbl `0xf9da80`)
  `+0x18` -> cFSMOrder (vtbl `0x103d7a8`). cFSMOrder `+0x10` -> cAIFSMAgent::Core, `+8` current state, state
  `+0x10` count / `+0x18` transitions (`+0xc` target, conditions read properties such as SceNo directly, so
  the tracer misses them). `0xc29920` "branch" sets a flag (type 0 = script FreeFlags at `+0x148`, type 3 =
  game flags). When a script waits on a SceNo nothing sets, search the code for that constant.
- Finding what a co-op moment needs: compare a record-mode (co-op) trace with a solo one.

## Bosses

- **Uroboros as Sheva** (`uroboros.cpp`): the player manager's count (`+0x34`) stays 1 with only slot 1
  filled. `uEmIntel40` re-picks its target slot (`+0x2EC`) with `0x99AEF0`, which loops over slots 0..count-1,
  so it found nobody and the Uroboros ran on the spot. The slot is overwritten with the player's; `0x99B115`
  then copies that player into the target (`+0x2C`), and a later -1 pick leaves it. Raising the count instead
  crashes (`0x731A94` reads an empty slot's position). That read is the 1-2 incinerator's area controller
  (`aS103`, vtbl `0xf53668`, `[[0x1234578]+0x3830]`) pushing players out of the door's way, which crashed as
  Sheva too; its update (`+0x1c`) runs with the player copied into slot 0 (also left in their own, as at
  Ndesu, so other threads' slot scans never miss them). Its intro walk (`+0x20c0`, action 0x21) is vanilla: it
  ends at RSF 9, set by `pulldown_demo` after shooting its arm off (RSF 1). The cutscene after the incinerator
  (scenes 1610/1620) ends in a state (`0xaf1470` enter / `0xaf1990` update / `0xaf1c80` exit) that plays the
  closing shot on `getPlayer(0)`'s camera and waits for it (camera `+0x162`); as Sheva it froze on the last frame.
  While stage flags `0x18000` are set in those scenes, slot 0 also holds the player (verified live: filling it
  un-froze the game and the flags cleared). Not the incinerator switch: its camera (`0xb35980`, `+0x17fc`) only
  runs when `+0x19a4 >= 0`, which it never is here.
- **Desperate Escape area controller** (`uroboros.cpp`, vtbl `0xf57810`, update `+0x1c` = `0x7604d0`): checks
  players 0 and 1 for a state (`0xb56600`, reads `+0x10e0`) without testing for an empty slot (`0x760970`), so
  it crashed as a cutscene was due. For that update an empty slot 0 or 1 holds the player too. Completed as Jill.
- **Lost in Nightmares is played as Jill** (`sheva.cpp`): only Jill opens its doors and plays its piano (`uOm0201_Piano`;
  Chris's prompt only sends an AI Jill), and the game offers Jill only after a clear. In areas 616/617/700/701 the
  character choice is set to Jill while no player exists. Chris-only zones there ("any of 0x11", e.g. the sheet
  music that sets flag 17, which the piano waits for) are given to Jill (`zones.cpp`), and the piano's door
  (`uOm0202_Door`, vtbl `0x100fdd0`) is kept open once it reaches the bottom by setting flag 150, the game's own
  "done" state (`piano.cpp`); otherwise it rises again as soon as nobody is playing. The Centaur Emblem door's use
  prompt (a scenario zone, manager 5 zone 1) is never created for Jill, so a held key item is used for her when she
  stands at a `uOmUseItem` that needs it (its action, vtbl `0x1016b20` `+0x188`, run from its update `+0x160`).
  Scenario zones that start a level script on an action-button press (def `+8` type 6) never take Jill's press,
  though she counts as inside and her button is available (seen at the lock: `Pick_chk` sets flag 19, which the
  lockpick `uOmf108_KeyPick` waits for). There, for 300 ms after a fresh press of the action button (F, or the
  pad's A), they are made "fire with a player inside" (type 3), so the one she stands in fires on her press
  (`zones.cpp`); so are type 5 ones without a prompt message (the study's PC). The piano's spot also holds Chris's
  look (`Moon_Chris`, his "never took piano lessons" line, which sets flag 17): flag 17 is set as she comes within
  3 m of it and his look is then skipped, so one press plays the piano.
  The ceiling trap room catches whoever walks in; the caught player shoots the lock off (flag 13) and the other
  pulls the lever by the gate (`uOm08bc`: flag 202, scene 14370, the safe switched on). Alone, the player is moved
  through the gate once the lock is off (`piano.cpp`), pulls the lever, and goes back for the safe.
  Held key items go in only on the player's own press (A on an XInput pad, `config.ini` `[JOYPAD] A` on a
  DirectInput pad, or F). The hall crank (`uOm021b`, max 150) locks only once fully wound (`crank.cpp`). Areas
  705/706 count as Lost in Nightmares too. There, camera params aimed at Chris are not pointed at Jill
  (`script.cpp`): each character has a wake-up camera of their own, and Chris's took over hers. In 705 the
  coloured cranks' grab button (crank action `+0x10f8` -> `+0x1c`, condition `0xab18f0`) only lets a human turn
  one when their partner exists and is human, so for a free crank the mod lets the player turn it alone. Wound
  fully, it locks; its ceiling (`uOm021d`, crank `+0x197c`) stays up only while the crank's action index
  (`+0x10e0`) is 1 (being wound), so for the ceiling's own update (vtbl `+0x160`) a locked crank reports 1. Without
  that, the ceiling landed in mid-air: drawn at the top with its collision on the floor, which made an invisible
  platform. With the crank reporting 1, the game's own trap runs as in co-op: an axe man under the raised ceiling
  is crushed (seen 2026-11-09, 8 ms after the lock). The mod's own drop (axe man in the box, player not near its
  middle, checked from the ceiling's update because an idle crank stops updating) is a fallback. When the trap
  goes off the lock lets go and the crank rewinds; a lock turning the rewind into waiting also sends the rewind's
  own stop for its winding-down sound (request 3, `0xab26d0` -> `0x7a4630`), which otherwise looped. The basement
  wake-up (`uroboros.cpp`, area controller vtbl `0xf579b0`, step `+0x6ec`) waits for two ready players at step 1;
  alone, the ready player moves it on. The final fight (706) saves its checkpoint (a `cScenarioArg_CHP` zone,
  scene 14770) only with a player inside its box after the intro, and Jill is placed just outside it, so at scene
  14760 the zone is fired for her (`zones.cpp`). The
  enemies there (`uEmIntel70` `0xf977b8`, `uEmIntel65` `0xf97348`) only ever target player 0, so with no target they
  get the player (AI `+0x2c`, enemy `+0x1ba4`; `uroboros.cpp`). With no audio device, radio calls wait for
  ever on their first line: each subtitle waits for its voice line to finish playing. That's a setup issue, not a
  mod bug.

- **Uroboros Mkono** (5-2, `uEm41` vtbl `0xf7d160`, AI `uEmIntel41` `0xf960f0`): its AI shares `uEmIntel40`'s
  target pick, so it gets the same target-slot fix. Part hits only do full damage while it is open
  (`cEm41ActDisclose`/`DiscloseLoop`, exposed `+0x20b6`). It opens when flag 8 of `+0x1e2c` is set, which its
  hit handler (`+0x208`, `0x8b4d80`) does once the heat `+0x2148` passes 2200, and stays open while the heat
  is above 0. Each flame hit added `(30 - n) * 3` times the unit's frame step (`+0x10`, 0.25 at 120 fps), but
  hits come at the same rate at any frame rate while the heat cools per second, so at 120 fps it never opened
  and the flamethrower did 1 damage. The hook divides each hit's heat by `+0x10` * timer `+0x2c` and sets flag
  8 as the handler would.
- **Ndesu** (2-3, `uEm45` live vtbl `0xf83828`; `0xf832e0` is another uEm45 vtable and patching it does
  nothing) (`ndesu.cpp`): prompt press `+0x230` (`0x8e08b0`) marks `+0x1bb4` bit `0x40 << slot` and acts only
  with both; `+0x234` (`0x8e09b0`) runs the players' side (type 0 crouch is player 0 only; types 1/2 need
  both). The mod marks the partner's bit and does the solo player's half.
- **Ndesu tuning** (`enemyhp.cpp`, `NdesuEaseTick`): his health, gauges and bulbs are at their default values. His
  thrown boulder (`uWp78`) has 2600 health instead of 5200. The attack stop values at `+0x1f54` for the kick, slam,
  pole swing and tackle (1500, 2600, 1200, 6000) are 10% lower, caught at their exact loaded values. The older
  With the launcher's "Ndesu's main bulb at half health" option (`re5_solo_ndesuhalf.txt`, `EnemyHpTick`)
  the main bulb's health (`+0x1f50`) is capped at 13000 (armed at 26000); his body, other bulbs and stagger
  gauges stay at their defaults. An attack's stop is checked only
  on the hit that takes `+0x1f54` from above 0 to 0 or below, and only while the stop window (`+0x448` bit
  `0x2000`) is open; a stop used up before the window opened let the attack land, so until it opens the value is
  held at 1.
- **Ndesu hit cooldown at high frame rates** (`ndesu.cpp`): an applied hit (`0x9e0840`) arms a per-part cooldown
  from the hit's `+0x34` (0.1 s for the jeep turret) in `+0x1e10[part]`, which becomes `+0x1dfc[part]` and ticks
  down by frame time; while it is above 0 the part ignores hits (`0x9e0700`). The turret fires every 0.104 s, so
  at 120 fps a bullet arriving one frame early was thrown away (about 6% of body hits; 30 fps lost none). Ndesu
  and his bulbs (`uEm4502`, `0xf83b90`) get `+0x200` (called right after the cooldown is armed) replaced: it
  shortens the new cooldown by (1/30 s - frame time). Main bulb damage per exposure at 120 fps went from about
  8,400-8,700 to 10,350 (30 fps: 9,430).
- **Ndesu as Sheva** (`ndesu.cpp`): his AI (`uEmIntel45`, vtable `0xf96c80`, `+0x280` = Ndesu) keeps its target in
  `+0x2c` and Ndesu's `+0x1ba4`, and about ten places in it fill both with `getPlayer(0)`, which is empty as Sheva:
  after his opening walk he waited forever. The walk was short too: the state change (`+0x48`, `0x9abf80`) takes
  player 0's position as the destination and skips it when slot 0 is empty, so he attacked from 6645 instead of
  about 5846. The AI's think (`+0x94`), state change and resets (`+0x68`, `+0x6c`) run with the player also in
  slot 0 (never moved out of their own: other threads scan the slots meanwhile, and a move let the pad
  rumble's "first human player" scan `0x7e6290` find nobody and crash at `0xb8a458`). An empty target is filled
  with the player after each and every 10 ms (the AI is patched within 10 ms of appearing).
- **Wesker phase 1** (6-3, `uEm80` `0xf8be60`, `uEm81` `0xf8c300`) (`wesker.cpp`): press `+0x230`
  (`0x9084b0`) -> `+0x234` (`0x9085e0`, (player, type, result)); type 1 hold (action 0x6a, holder `+0x1f64`),
  type 2 inject (only while `+0x10e0` == 0x6a; injector = presser). After the player has held him 1 s with no
  partner, the mod calls `+0x234(0, 2, 0)` from his update.
- **Volcano Wesker** (`uEm82`): no code needed; partner lookups are null-checked, so the boulder and
  rescue-Sheva parts are skipped and the fight starts directly.

## Voice and hints

- **Partner lines** (`voice.cpp`): subtitled stage lines come from `sound\bgm\sNNN_radio.stq` (sMessage
  `[0x11b26f0]+0x2f6e8`); request id = line number, radio version = id + 100, played when the speaker's slot
  has no player (`0x7d9e0e`, `0x7e6370`). Each radio request (0xa4 each from header `+0x24`: `+0x26` volume,
  `+0x94` entry, `+0x9c` stream) is pointed at its normal twin, keeping 2D playback.
- **On-screen hint** (`hint.cpp`): `sMessage::open` `0x7db3a0` (`(u8 file, u16 group, u16 index, float secs,
  s8 player, int mode)`, ret 0x18). mes_system (file 0) entry 820 ("(dummy)") is pointed at a mod buffer of
  (code, attr) u16 pairs; glyph codes in `hint.cpp` (`0x02` = "1"; no "0" or "/"). One window shows at a
  time; its record is at sMessage `+0x2f670` (2 x 0x1c: `+0` file, `+4` entry, `+0xc` countdown). The boat
  tops up the countdown while aboard and zeroes it 1 s after leaving. Empty lines are dropped, so the boat
  hint uses short lines that end before the ammo ring.
- **In-game file reader** (for a possible how-to-play file): window record 0 while reading: `+1` msg file
  (5 = file texts), `+2` document, `+4` page index, `+8` page count. Its opener is not found
  (`0x7db3a0` is a different mode).

## Colour filter option (`filter.cpp`, `re5_solo_nofilter.txt`)

- The green/yellow tint is `uColorCorrectFilter` (vtable `0x10AFF08`, a unit): 16 `Corrector`s at +0x30 (0x30 each:
  +4 `mEnable`, +8 `mType`, +0x10 `mFactor`/`mFactor2`), tone curves, gamma. Found via the `ColorCorrectFilter`
  draw-pass name and the class's own property list.
- Every frame `sLightLinker::ColorCorrect` (vtable `0xF63930`, per area, itself re-applied through the property
  setter `0x6D44F4`) copies its correctors in (`0x7C2FB1`); the filter's +0x18 (`0x482810`) builds the matrix at
  +0x330 from the enabled ones, its draw (+0x24, `0x483030`) applies it. Clearing `mEnable` anywhere else is undone
  within a frame; the mod's vtable copy clears them inside +0x18, before the original runs.
- New areas' filters are hooked within 100 ms. `sLightLinker::ViewFilter` +7 (`mEnableColorCorrect`) does nothing
  visible.

## Borderless window option (`borderless.cpp`, `localjoin.cpp`, `re5_solo_borderless.txt`)

- Tabbing out stopped the game in two ways. In exclusive fullscreen, `WM_ACTIVATEAPP` (`0x423dbe`) minimises the
  window (`ShowWindow` 7 while `[0x12345d4]+0x21`, the fullscreen flag, is set); minimised, the main loop's device
  check (`0x453fd0`) fails and no frame runs. The launcher sets `FullScreen=OFF` in
  `Documents\CAPCOM\RESIDENT EVIL 5\config.ini` with the marker (and `ON` again when it is cleared or on
  uninstall); the watcher takes the window's frame off and fits it to its monitor.
- The main loop runs without focus anyway (`0x423a02`: it only sleeps when both `[0x12337dc]+0x20`, set by
  `WM_ACTIVATEAPP`, and `+0x21` are 0; `+0x21` is set).
- The pause manager's update (`0xa308c0`) opens the menu (`+0x14` 0 -> 1) when `0xa2d0c0` says so: Start, or the
  window not in front (`0x77f4b0`: sMain `+0x10548` minimised, `+0x10549` foreground, `+0x1054a` focus, set from
  `IsIconic`/`GetForegroundWindow`/`GetFocus` each frame by `0x783af0`). With the option, the mod's copy of that
  update sets those three to "in front" for the call while the menu is closed and puts them back after; tabbing
  out no longer pauses (confirmed 2026-11-04). Pad input still stops while the window is behind (`0x7b5cb7`).
- Dead ends: the "pause requested" byte `[0x1234584]+0x2030`, scene mode `[[0x1234578]+0x382c]+4` and the pad
  table's connected bytes (`[0x11b1dd8]+0x80+0x20*pad`) don't change on tab-out; holding `[0x12337dc]+0x20` at 1
  still paused.

## 120fps fix option (`snag.cpp`, `re5_solo_120fps.txt`)

- Above 60 fps a player could get stuck for good on some spots (never at 60/30). Each frame the move
  (`0xb58840`) saves the position (`+0x1060`), applies the motion (`+0x1050` = the result), projects the step
  onto the ground (vtable `+0xd8`, `0xb6a670`, the same in all 32 player classes) and runs the collision passes
  (`+0x3c`/`+0x40` -> `0x588230`/`0x58b340`, no frame-time reads). On the snag spots the collision put the
  player back exactly at `+0x1060` every frame; at 120 fps the step is half as long and lands in the same
  rejected place each time (logged by a per-frame recorder: start never moves while the step is 3-4 units).
- Fix: in every player's vtable copy, once a player has been put back to the same start two frames running
  while moving, `+0xd8` gets the step 60 fps would take (x `1/60 s / frame time`, at most 4) before the
  original runs; the game's collision still decides where the player ends up. Never acts at 60 fps or below.
  Verified: freed a live snag at (-2003, 255, -1876) on the first try. Works in vanilla co-op too, and as a
  standalone mod (`re5-120fps-fix`).
- Enemies snag the same way: their ground step (`+0xd8`, `0x9de2e0`) uses the same `+0x1060`/`+0x1050`. Lost in
  Nightmares' axe man (`uEm27`, area 705) walked on the spot at 120 fps on the way to his jump-up point and
  managed it at 30. Patched class copies are limited, so an enemy class gets the fix only once the watcher sees one
  of its units held in place while moving. Its step starts at the 60 fps length, and if still held ten frames
  later it gets the 30 fps one (the axe man needed it). Verified: he jumps up to the ledge again.
- `snag.cpp` also owns the players' vtable copies (HookObject on `+0x1c`, every slot): `death.cpp` runs after
  the move through it.

## Known issues

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
