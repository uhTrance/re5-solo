# RE5 modding knowledge base

Everything learned while building RE5 Solo (a solo story mode for Resident Evil 5 and Lost in Nightmares), written
so another modder, or another modder's AI agent, can pick it up and skip the months of digging. It covers the
ground rules the game imposes, how its engine is put together, the techniques that work, the debugging method,
the recurring "co-op to solo" bug patterns with their fixes, worked case studies, and the mistakes made along the
way.

`README.md` is the per-feature reference (every object, offset and fix the mod ships). This file is the *how* and
*why*. Addresses below are for the **Steam Gold Edition `re5dx9.exe`**. The image is fixed at `0x400000` with no
ASLR, so every address is absolute. Another release or a game update moves everything.

If you are an agent reading this: treat every address as "true for this exe, verify before relying on it". The
methods (section 5) transfer to any MT Framework game. The addresses don't.

---

## 1. Ground rules the game imposes

### 1.1 Never change a code byte

The exe is SteamStub-wrapped (a `.bind` section). Steam's anti-tamper (`steamclient.dll`, message "INTEGRITY
CHECK FAILURE") fails the game when any code byte changes. Classic detours, inline hooks, `jmp` patches and
NOPs are all out. It also tripped, intermittently, on:

- hardware breakpoints (debug registers) left armed,
- swapping slots in the game's own vtables in `.rdata`.

What has never tripped it in hundreds of hours:

- writing heap data (object fields, flags, scene numbers, zone definitions),
- giving **one object** a private copy of its vtable (the object's vtable pointer is heap data),
- hooking **Windows** functions the game imports (e.g. `XInputGetState` through its import slot), since that's
  Windows code, not the game's,
- calling the game's own functions from your code.

The whole mod is built from those four.

### 1.2 Work from a memory dump, not the file

The file on disk is encrypted and packed. Dump the running image (`0x400000..0x1928000`) once and do all static
analysis on the dump (`re/re5dx9_dump.bin`, never committed, it's the game's code). Capstone and pefile in
Python are enough. Code sits in several sections (`0x4xxxxx-0xexxxxx`, `0xf0xxxx`). `.rdata` with vtables and
class-name strings sits around `0xf4xxxx-0x10xxxxx`, and static data up to `0x1928000`.

Startup first-chance exceptions at `0x1465a71` / `0x1465a9c` are the DRM's anti-debug traps. Ignore them.

### 1.3 No scripting runtime

REFramework doesn't support RE5. There is no Lua and no live console. Everything is a native DLL plus external
Python tools that read and write process memory.

---

## 2. Architecture that worked

### 2.1 Loader + hot-reloadable core

- `dinput8.dll` is a permanent proxy (the game imports `DINPUT8.dll`, so dropping a proxy in the game folder
  gets you loaded early). It owns things that must outlive a reload: the log, the crash recorder, Windows API
  hooks, and any memory the game may keep a pointer to (`g_host->memory`).
- `re5_solo_core.dll` is the mod. The loader runs a *copy* (`re5_solo_core.loaded<N>.dll`) so the original can
  be overwritten. When the file changes it calls `CoreShutdown`, waits, unloads, and loads the new copy. A
  build is live in the running game within a second. This made iteration roughly 10x faster than restarting
  the game, and it's the single best investment in the project.
- **Shutdown contract:** after `CoreShutdown` nothing may point into the core. That means stop its threads,
  restore every object that was given a vtable copy, restore any game field that holds a function pointer
  into the DLL (zone callbacks, button conditions), and remove handlers. A forgotten one is a crash a second
  after the next deploy, in a "random" place. Keep a table of every object you touched.
- Shipping: one launcher exe with both DLLs embedded as resources. It installs, updates, uninstalls, and
  toggles options through marker files (`re5_solo_*.txt` next to the exe, read when the core loads).

### 2.2 Two kinds of threads, kept apart

- **Watcher thread** (every ~10 ms): reads game state, decides, writes plain data (flags, scene, bits). It
  must never call game functions that touch game objects, because the game thread may be halfway through
  updating them.
- **Game thread**: your code runs here only from inside a hooked vtable slot (2.3). Call game functions here
  (set an action, request an area jump, play a sound).
- To hand a request from the watcher to the game thread, use a flag/struct with interlocked operations and let
  a game-thread hook pick it up (`jump.cpp`: `g_pending` + `JumpFrame` from a player's move hook).
- Wrap every game-memory access in SEH (`__try/__except`). Objects die between the moment you find them and
  the moment you use them.

### 2.3 Per-object vtable copies (the hook primitive)

`objhook.cpp`:

- `HookObject(obj, classVtable, after, updateSlot)`: copies the class vtable (0x200 dwords) once per class into
  the DLL, replaces the update slot with a trampoline that calls the original and then your `after(obj)`, and
  points this one object at the copy. It's one aligned dword write, so it's atomic.
- `PatchObject(obj, classVtable, patchFn)`: a copy with arbitrary slots replaced (`patchFn(copy)`), one copy
  per (class, patch) pair. Use it when you need to run code *before* or *instead of* the original, or wrap a
  non-update slot.
- `PatchHookedSlot`: add one more replaced slot to an object already on a HookObject copy.
- Every hooked or patched object is tracked, so shutdown can put back the class vtable.

Conventions found:

| Object family | Per-frame update slot |
|---|---|
| `uOm*` (stage objects) | `+0x160` |
| enemies (`uEm*`) | move `+0x1c` (uEnemy `0x9d9b80`) |
| players | `+0x1c` (every slot copied in `snag.cpp`) |
| area controllers (`aS*`, `[[0x1234578]+0x3830]`) | `+0x1c` |
| enemy AI (`uEmIntel*`) | think `+0x94` (the update `+0x70` is **not** called through the vtable) |

Gotchas, each learned the hard way:

- **Identity checks break once you patch.** Code that recognises an object by `*(DWORD*)obj == classVtable`
  stops recognising it the moment the object is on your copy. The ceiling-trap drop check silently never found
  a ceiling after the ceilings were patched (case 7.1). Recognise objects by DTI (`IsA`/`DescendsFrom`), or by
  "class vtable *or* our copy" (check the replaced slot).
- **Never hook one of your own copies again.** `HookObject` refuses anything that isn't a game vtable, or the
  copy's update would call itself.
- **Tables fill up in long sessions.** Objects are never untracked as they die, so a fixed table eventually
  stops tracking new ones, and a later reload leaves them pointing into a freed DLL. When full, compact by
  keeping only entries still on one of our copies. Size the class tables generously (the 120fps fix patches
  every enemy class that snags).
- Some enemy classes have several vtables (`uEm45` Ndesu: `0xf83828` is live, `0xf832e0` isn't). Read the live
  object's vtable, don't assume.

### 2.4 Calling game functions

Most are `thiscall`. From MSVC, call them as `__fastcall` with a dummy `edx`:

```cpp
template <class R = void, class... A>
R ThisCall(DWORD fn, void* self, A... args) {
    return reinterpret_cast<R(__fastcall*)(void*, void*, A...)>(fn)(self, nullptr, args...);
}
```

- Get the argument count from the callee's `ret N` (N/4 args) and from a real call site's pushes. Pushes are
  in reverse: the last `push` before the call is the first argument.
- `cdecl` helpers exist too (zone conditions such as `0xab18f0(crank, slot)`). The caller cleans up, with no
  `ret N`.
- Copy how the game calls the function, including the values of "mystery" arguments. The action setter
  (vtable `+0xdc`) is always called as `(id, 1|0, 0, -1, 0)`.

### 2.5 Windows-API hooks

The import address table entry (e.g. `XInputGetState` at IAT `0xf4a40c`) points into Windows. Hooking it doesn't
change a game byte. Chain onto whatever is already there (Steam's overlay hooks some of these). Used for pad
remapping on the airboat.

### 2.6 Crash recorder

Crashes in a game with its own exception handler are silent: no WER dialog, no dump. The recorder that finally
caught everything:

- a **vectored handler** that only *logs* first-chance faults in the exe (with a budget, so loops don't flood),
- **plus** a chained `SetUnhandledExceptionFilter` that logs `UNHANDLED <code> at <eip> (module+offset)`, the
  registers and the callers, and always writes a minidump. The game and Steam replace the unhandled filter, so
  it's re-asserted every 2 s from a thread. There's a re-entry guard (`thread_local`), and it chains to the
  previous filter.
- A small watcher script (`tools/crashwatch.ps1`) shows fault lines live and copies the log out when the game
  exits. The log is rewritten at every launch, so without it the evidence is gone on the next start.
- `re/dmpread.py` reads dwords straight out of a minidump's memory list (the stack, the faulting object).

---

## 3. How the engine is put together

MT Framework (2009). Everything below was verified live on this exe.

### 3.1 Classes, names and properties

- Every object's vtable has a `getDTI` slot implemented as `mov eax, imm32; ret`. The DTI record is at `imm32`,
  and its `+4` points at the class name (`"uPl00Chris"`, `"uOmS705ceil::cActRise"`). `+0x10` is the parent DTI,
  so `IsA` walks that chain.
- To name any object: scan its vtable's first ~16 slots for a `B8 xx xx xx xx C3` stub. That one trick
  identifies every object, action and state you find in memory. Nested names (`Class::cActFoo`) are the
  action/state classes.
- Each class's vtable `+0xC` registers its **properties** (name, type, field address). That's how named
  fields like `mLifeRate`, `mEnable` or `SceNo` map to offsets. The level scripts read properties by these
  names.
- Find a class's vtable from its name string: name string -> DTI whose `+4` points at it -> the getter stub
  `B8 <dti> C3` -> the vtable slot holding that stub. `re/classvt.py` and `re/findobj.py` do this.

### 3.2 Units and lists

- `sUnit` `[0x12340b0]`: move lines at `+0x20 + 0x20*i` (`+0xc` first unit, `+0x14` speed, `+0x18` = i).
  Units chain through `+8`. Enemies live on line 9. Walking every line and chain = every live object.
- Player manager `[0x11b27dc]`: slots at `+0x24 + 4*slot`, count `+0x34` (vtable `0xf653ac`; removeAll is slot
  `+0x14` = `0x7e94b0`). Slots are **per character**: 0 = Chris, 1 = Sheva/Jill. Playing as Sheva, slot 0 is
  empty and the count is 1, and that combination is the root of a whole bug family (pattern 6.2).
- Object basics: `+0x30` position (x, y, z float), `+0x10e0` current action/state **index**, `+0x10f8` current
  action **object**, action setter at vtable `+0xdc`. A player's `+0x1364` is HP, `+0x2a74` the partner.

### 3.3 Session, stage, scene, area, flags

- Session `[[0x12340a4]+0x1042c]`: `+0x478` active-slot mask (story = 3), `+0x480` control per slot.
- Solo flag: `[0x12340a4]+0x10434` bit `0x80000`, the game's own one-player mode. `requiredPlayers()`
  (`0xa83070`) returns 1 with it, else 2. Many co-op objects already honour it, so set it first and only fix
  what still ignores it.
- Stage record: `stage = [0x11b2158] + 0x5940 * [[0x11b2158]+0x20]`.
  - `+0x273d8` area (e.g. 705), `+0x273e0` scene number (SceNo; setter `0x726a80`), `+0x273c4` chosen character.
  - Stage flags (RSF): bit `0x80000000 >> (n & 31)` of dword `stage + 0x23218 + 4*(n >> 5)` (MSB first).
    `+0x23214` is the flag object's vtable, so writing there crashes.
  - Three copies of both characters' loadouts: A `+0x23448`, B `+0x249d8`, C `+0x27618` (case 7.10).
- Checkpoint snapshot: `[[0x1234578]+0x382c]+0x130`, a copy of the stage block. Its `+0x41d0` is the scene a
  Restart from Checkpoint loads. Watch this value to see whether a checkpoint was really saved.
- Area jump (the level scripts' `JumpArea`): with the stage controller present (`[[0x1234578]+0x3830]`),
  `0x768c10()` returns an object whose `0x71dfc0(area, startPoint, mode 2)` requests the jump and
  `0x71ae60(scene)` sets the scene to start on. Doors use mode 0. The area-exit object is `uOmf301`
  (`+0x1c20` destination area, `+0x1c24` start point).

### 3.4 Level scripts (FSM)

- Level scripts are `uFSMTask` (vtable `0xf9da80`) -> `+0x18` cFSMOrder (vtable `0x103d7a8`) -> `+0x10`
  cAIFSMAgent::Core. `+8` is the current state; a state's `+0x10` is its transition count and `+0x18` its
  transitions (`+0xc` target, `+0x20` condition).
- 86 actions, each `order->vtbl[slot](param)` (thiscall, ret 4). Names were taken from the registrations at
  `0xc29620`.
- Script files (`.fsm`, XFS format) live in the game's archives. `re/fsm_extract.py` + `fsm_dump.py` dump them
  to readable text (`=== Script / [State] / do Action args / -> target if condition`). **Read the script
  before reverse-engineering the code.** It tells you which flag, scene or zone a moment waits on.
- Conditions read properties directly (e.g. `PropertyName=SceNo; ConstS32=14760`), so a runtime action tracer
  misses them. When a script waits on a SceNo nothing sets, search the code for that constant.

### 3.5 Zones

Two families under the area root `[0x11e8660]`.

**Area-hit zones** (what scripts test with `AreaHit`): managers at `root + 0xd4 + 4*i` (count `+8`, list
`+0x14`). Zone `+4` is the definition (`+0xc` 0 = any of / 1 = all of, `+0x3c` required player bits: 1 = slot 0,
2 = slot 1, 0x10 = any player). Zone `+8` is the state (`+0x10` who's inside, `+0x18` **its own copy of the
requirement**, which is the one that actually decides scenario zones).

**Scenario zones** (`cScenario`): 9 lists at `root + 0x24 + 0x14*L` (count `+0`, array `+0xc`).

- `+4` definition (`cScenarioParam`): `+8` trigger type (6 = action-button press, 5 = quiet press, 3 = fire
  when a player is inside), `+0xc` any/all, `+0x14` scene (99999 = none), `+0x20` prompt message, `+0x3c`
  player bits.
- `+8` its `cAreaHit`: centre at `+0x30`, shape at `+0x14` (4 corners of a box).
- `+0x1c` flags: bit 2 off, bit 3 fired, **bit 4 (0x10) fire with nobody inside** (`0xcc0997`).
- `+0xc` and later: arguments. `cScenarioArg_BootFSM` starts a level script (its entries at zone `+0x340`,
  `[[+0x340+0x40]+4]+8` = the script name). **`cScenarioArg_CHP` saves a checkpoint.**
- `+0x14` an optional level callback (cdecl `fn(param)`, param `+0xc`).

To name a zone's arguments, scan the zone's dwords for pointers whose target has a DTI name containing `Arg`.
That's how the final fight's checkpoint was identified (case 7.4).

### 3.6 Actions and states

Objects switch behaviour by action objects. The current one is at `+0x10f8`, its index at `+0x10e0`, and you
change it with the setter at vtable `+0xdc` `(id, ...)`. Each action class has its own vtable (enter at `+0x18`,
update `+0x1c`, exit `+0x20` and so on; check per class). Rules learned:

- **Writing `+0x10e0` alone changes nothing visible.** It's only what *other* objects read. Calling the
  setter runs the transition.
- **Exits are often empty.** A sound or effect started on enter is stopped by the action's *own* update when
  it finishes, not by its exit (`uOmCrank::cActRewind`'s exit is a bare `ret`). Switch an object out of an
  action early and its loop keeps playing (case 7.1).
- Other objects key off your index. The ceiling stays up only while its crank's index is 1 (being wound).

### 3.7 Sound and messages

- Sound requests: `0x7a4630` on the sound manager `[0x11b1d0c]`, 5 args `(owner [obj+0x1454], id, obj, obj, 0)`.
  Per-class ids come from a small lookup (for cranks, `0xab26d0(kind)`).
- On-screen text: `sMessage::open` `0x7db3a0` `(u8 file, u16 group, u16 index, float secs, s8 player, int mode)`.
  A spare system entry (mes_system 820 "(dummy)") can be pointed at your own glyph buffer.
- Radio lines: `sound\bgm\sNNN_radio.stq`. With no audio device, a subtitle waits forever for its voice line.
  That's a test-setup issue, not a bug.

### 3.8 Saves

`userdata\<account>\21690\remote\savedata.bin`: each 8-byte block is XORed with the previous encoded block,
with a checksum at `+8` (`tools/re5save.py`). Swapping the file while the game sits at the title screen is
picked up when Start is pressed. Back up every version; a bad experiment can ruin a save for good.

---

## 4. Tools (all in the repo)

| Tool | What it's for |
|---|---|
| `re/peek.py`, `re/poke.py` | read/write a dword by address or pointer chain (`12340a4,1042c,478`: each comma dereferences, then adds) |
| `re/snap.py` | snapshot an object and diff it later: "what changed when I pressed the button" |
| `re/findobj.py`, `classvt.py`, `whatis.py` | find live objects by vtable/class, name the object containing an address |
| `re/disasm.py`, `xref.py` | disassemble the dump, find callers/references |
| `re/fsm_extract.py`, `fsm_dump.py`, `fsmstate.py` | dump level scripts offline; show a live script's current state |
| `re/needs2p.py` | list functions that call getPlayer(1) or the partner getter, grouped by class: a to-do list of co-op code |
| `re/tp.py x y z` | teleport the player (writes every position copy) |
| `re/dmpread.py` | read memory out of a minidump |
| Script tracer (`re5_solo_trace.txt`) | logs each level-script action's first run per param, plus calls per 5 s ("hot:") |
| Record mode (`re5_solo_record.txt`) | vanilla co-op with logging: record what co-op *does*, then make solo do the same |
| Write watch (`re5_solo_watch.txt`, F10) | arms a hardware write watch on an address and logs the first writers with stacks (debug only, can trip the DRM) |
| Area skipper (F11 with debug keys, `re5_solo_jump_now.txt`) | jump to the next area or to `area point scene`: reach a test spot in seconds |
| `tools/crashwatch.ps1` | live crash lines, log kept per run |

Plus ad-hoc **recorders**: 30-line Python scripts that `ReadProcessMemory` a handful of fields every 20 ms and
write a line **only when something changes** (state, action name, flag bits, position every 2 s). Most of the
hard bugs were solved by reading one of these logs. Template:

```python
key = (action_name(obj), state, flags, int(wind) // 25)   # coarse, so noise doesn't spam
if key != last:
    out.write(f"{time.strftime('%H:%M:%S')} {key}\n"); last = key
```

Name actions in the recorder (DTI trick from 3.1) so the log reads `cActRise -> cActFall -> cActLand`, not vtable
numbers.

---

## 5. Method: how problems actually got solved

1. **Reproduce cleanly.** Restart from a checkpoint before every test. A run that earlier experiments already
   changed proves nothing. One cage test failed only because an earlier poke had left a flag missing. Also
   check that nothing else is interfering: a trainer's "kill enemies" made a scripted boss "not spawn".
2. **Read the level script first** (3.4). Most co-op moments are "wait for flag X / scene Y / zone Z with both
   players". The script names the exact thing.
3. **Compare co-op and solo.** Run the moment in record mode (real co-op) and in solo with the same recorder.
   The first divergence is the bug.
4. **Name everything.** Turn vtables into class/action names, and properties into field names. A log of named
   transitions explains itself.
5. **Find who writes or reads a field:**
   - scan the dump for every instruction with that displacement (`[reg + 0x2148]`), which beats hardware
     watches that sometimes don't fire,
   - or arm the write watch,
   - or find callers of a known function by scanning for `E8 rel32` hitting it.
6. **Test the hypothesis live before writing code.** `poke` the value and watch the recorder. Examples: set
   the crank's index to 1 and see whether the ceiling goes back to rising; set a zone's fire bit and see
   whether the checkpoint scene changes. A live poke answers in seconds, a build in minutes.
7. **Then write the smallest fix that makes the game do what co-op does**, not a shortcut around it. Mark the
   partner as ready and let the object run its own logic. Fire the game's own checkpoint zone. Call the game's
   own sound stop. Shortcuts (writing the end state directly) kept leaving the object in a state its other code
   didn't expect.
8. **Hot-deploy, have the tester play it, read the logs.** Log state *changes* with enough context to explain
   them ("ceiling X has an axe man under it, not the player"). Never log per frame.
9. **Keep notes as you go.** Every fix in `README.md` lists the addresses, the cause and what was tried and
   failed. Next month's bug is usually a cousin of last month's.

---

## 6. The recurring co-op to solo bug patterns

RE5's co-op is woven through everything. The bugs fall into a few families, and once you know them a new
blocker usually takes an hour, not a day.

### 6.1 "Needs 2 ready players", hard-coded

Objects with their own ready handler count ready bits against a literal 2, ignoring the solo flag:

- elevators (`+0x184` `0xac6d50`), twist levers and card doors, co-op boxes (`+0x1a48`), pulleys (`+0x1a51/52`),
- two-man levers (`0x1202040[index]`), Ndesu's prompts (`+0x1bb4` bits), the gap-jump flag in the player
  manager (`+0x38`).

**Fix:** when the player does their half, set the partner's bit(s) the same way the game would (ready bit,
side, "at box", "pushing"), then re-send the "go" event (`0xf1`) so the object runs its own completion.
Setting a "players needed = 1" field didn't work on boxes. The completion logic has other checks.

### 6.2 Code that only looks at slot 0, or at slots below the count

- **`getPlayer(0)` used as "the player"**: enemy AIs (`uEmIntel45` Ndesu, `uEmIntel70/65` in Lost in
  Nightmares), cutscene cameras, area controllers. As Sheva/Jill slot 0 is empty.
- **Loops `for (s = 0; s < count; ++s)`**: the count is 1 in solo, so slot 1 is never reached (3-1 crocodile
  AI, Uroboros target pick). 62 places read the count.
- **Reads of an empty slot without a null check** crash outright (`aS103`'s incinerator push, the Desperate
  Escape controller, the pad-rumble scan).

**Fix:** wrap *that object's* update or think (vtable copy) so the player is *also* in the slot it reads, or
the count is raised, **only for that call**, then put it back. Never move the player *out* of their own slot,
because other threads scan the slots at the same time (moving her made the rumble scan find nobody and crash).
AIs think on several worker threads at once, so a shared raise needs a reference count under a lock: first in
raises it, last out restores it. Per-call save/restore let one thread drop the count while another was mid-think.

Raising the global count permanently is tempting and wrong. Other code then reads the empty slot 0 and crashes.

### 6.3 Zones that require both players

`AreaHit` on "all of 0x13" (player 0 + player 1 + any). **Fix:** move the partner's bit to the player's in every
"all of" requirement, **in both the definition and the state's copy** (the copy is what decides; editing only
the definition did nothing). Re-apply every second, because areas reload. Lost in Nightmares also has
character-only zones ("any of 0x11", Chris only); with Jill forced, give them to her.

### 6.4 Scripts waiting on something only the partner produces

- a cutscene QTE prompt for the absent player (`sEventPlayer` `+0xf0`): set the partner's pressed/done bits,
- the cage: RSF 25, SceNo 10240, then RSF 200/201 = "which slot entered",
- "player 2 stood in the room" (1-1), "both players armed" (1-1).

**Fix:** answer each step as the script reaches it, using the script dump to know what it waits for. Never set
everything up front; scripts that are in an earlier state ignore or mis-handle future answers.

### 6.5 Checkpoints that need the *other* player to stand somewhere

A `cScenarioArg_CHP` zone saves the checkpoint when a player is inside it. Some are placed where the partner
stands: the 6-3 right lever, and the Lost in Nightmares final fight, where Jill is spawned just outside the box
and Chris inside it. Solo, the checkpoint is never saved and a restart sends you back before the boss.
**Fix:** set zone `+0x1c` bit `0x10` ("fire with nobody inside") at the right moment (after the lever pair goes,
or once SceNo reaches the fight). The game then saves normally, saving icon and all.

### 6.6 Pair animations and swaps that dereference the partner

Gap jumps and boosts are a two-person animation, so solo nothing moves. The airboat's driver/gunner swap
`exchange(newDriver, newDeck)` crashes on the missing player. **Fix:** do the movement yourself on the game
thread (a measured offset from a co-op recording, applied to every position copy), or perform the half of the
swap that exists with a blank dummy object for the other half (allocated in loader memory so it survives a
reload).

### 6.7 "Partner keeps it running"

The partner holds a crank, winds a gate, or holds Wesker. **Fix:** lock it once the player finishes their part,
and make sure everything that *reads* the held state still sees "held" (6.8).

### 6.8 State faked in one place, read in another

The single most instructive bug (case 7.1). When you emulate the partner, list **every reader** of the state
you're faking. A locked crank looked fine to the crank but not to the ceiling (which checks the crank's action
index), nor to the sound system (the rewind's loop), nor to the drop logic (which found ceilings by their
original vtable).

### 6.9 Frame-rate dependence (above 60 fps)

Not co-op, but it showed up constantly at 120 fps:

- Collision rejecting a half-length step forever (stuck on scenery). Fix: when a unit is put back to the same
  start two frames running while moving, give it the step 60 fps would take, and 30 fps if still held.
- Per-hit accumulators scaled by the frame step while hits arrive at a fixed rate: Mkono's heat never opened
  it at 120 fps. Fix: divide that gain by the step.
- Per-part hit cooldowns that drop hits arriving one frame early at 120 fps (Ndesu).

Measure at 30, 60 and 120 fps; if 120 is worse, look for `* frameStep` on something that isn't per-frame.

---

## 7. Case studies

Each is: symptom -> what was found -> fix -> lesson. Addresses are in `README.md` under the same names.

### 7.1 Lost in Nightmares, area 705: the ceiling-trap room

**Setup.** Four coloured cranks (`uOmCrank` subclasses, e.g. `uOm021f`) each raise a ceiling (`uOm021d`,
`uOmS705ceil`). In co-op one player winds and holds while the other lures the axe man (`uEm27`, a `uEm20`)
under, then the holder lets go and the ceiling crushes him.

**Step 1: the crank can't be grabbed alone.** Its grab button's condition (`0xab18f0`, cdecl `(crank, slot)`)
only allows a human whose partner exists and is human. The button is data (`[crank+0x10f8]+0x1c`, condition at
`+0x5c`), so it's repointed at our condition, which falls back to the original and also allows the solo player
on a free crank. The button has to be put back at shutdown.

**Step 2: the lock.** Like every crank gate, once fully wound the wind is pinned at max, and the rewind that
letting go starts is turned back into waiting (the setter at `+0xdc`, id 0).

**Step 3: the invisible platform.** After letting go, the ceiling was drawn at the top but you could stand on
thin air where it would be when down. A recorder (ceiling action, ceiling state, crank action, wind, crank
`+0x1995`, RSF 20-28) showed: rising to y 400, then on let-go `cActFall` and `cActLand` in the same frame, still
at y 400. Disassembling the ceiling's actions (names found with the DTI trick: `cActLand/Rise/Fall/Stop/...`)
explained it:

- `cActRise` (`0xb33810`) stays up only while `crank+0x10e0 == 1` (crank being wound). Otherwise it goes to
  `cActFall` (id 2).
- `cActLand` collision is the floor. With the wind pinned, the fall ended at once in "landed" at the top: drawn
  up, collision down.

**Live test before coding:** poke the waiting crank's `+0x10e0` to 1. The landed ceiling went back to rising and
the platform vanished.

**First fix (wrong):** keep the crank's index at 1 permanently. The ceiling stayed up, but the crank's own logic
saw "being wound" with no winder and kept cycling rewind -> wind, which pulled the player back onto the handle.
**Real fix:** wrap the *ceiling's* update (PatchObject, slot `+0x160`, original `0xb31de0`): for that call only, a
locked waiting crank reports 1, then goes back to 0. The crank and the player see the truth; the ceiling sees
"held".

**Step 4: it won't drop on the axe man.**

- **Bug A:** the "player is under it" test used the ceiling's box (`+0x1a00..+0x1a10`), which is ~446 units each
  way, far bigger than the ceiling. The player at its edge counted as underneath. Now the player only counts
  near the middle (60% of the half-width); an enemy anywhere in the box still counts.
- **Bug B:** an idle locked crank stops updating, and the drop check lived in the crank's update. Moved it to
  the ceiling's update too.
- **Bug C (self-inflicted):** the drop check found "the ceiling of this crank" by comparing vtables with the
  ceiling class, and after step 3 every ceiling was on our copy. Fixed by recognising a ceiling by class vtable
  *or* our replaced slot (2.3).
- **Bonus:** with the crank reporting 1 during the ceiling's update, the game's **own** trap logic came back to
  life and crushed the first axe man 8 ms after the lock. As far as the disassembly shows, `cActRise` checks a zone
  (`+0x19c4`, tested at `0xb331d0` for players and enemies) and, while the crank is being wound, puts the crank into
  its rewind, so the ceiling falls. The second axe man stood in the ceiling's box but outside that zone, and the
  mod's own drop got him. So the mod's drop is the fallback, and both paths are needed.

**Step 5: the constant noise.** "Winding down" looped forever from a locked crank. The rewind action starts its
loop sound (request 1) and only its own update stops it at the bottom (requests 3 and 4). Its exit is a bare
`ret`. When the lock turns rewind into waiting, the mod now sends request 3 exactly as the rewind does
(`0xab26d0(3)` for the id, then `0x7a4630`). Also: once the ceiling's trap has gone off (ceiling no longer in
Land/Rise), the lock lets go and the crank rewinds normally. Kept locked, it sat wound beside a broken ceiling,
noisy.

**Lessons:** list every reader of the state you fake (6.8); test with a poke first; per-object state belongs
per object (one shared `static` throttle let only the first ceiling ever check, a bug a code audit caught).

### 7.2 Lost in Nightmares: examine spots fire on approach, and the piano

**Symptom 1:** sheet music, crank holes and other examine spots triggered as soon as Jill walked near them.
Cause: an earlier fix had turned press zones (type 6) into "fire with player inside" (type 3) permanently,
because Jill's button presses never reached them. Fix: on a **fresh** press of the action button (F, or the pad's
A), flip only the press zones to type 3 for 300 ms (several frames even at 30 fps, long enough for the zone pass
to see her inside), then restore them. Only the zone she's standing in fires, and only on her press. Also restore
them on shutdown and on any fault (an audit caught that a fault in a neighbouring check cleared the flip list
and left zones on type 3).

**Symptom 2:** pressing at the piano said Chris "never took piano lessons". The piano's spot holds two
zones: Chris's look at the sheet music (`Moon_Chris`, which sets flag 17) and the piano. The piano waits for
flag 17. With the flip, the press hit Chris's look every time. Fix: once flag 17 is set, his look is excluded
from the flip. For one press: flag 17 is set as Jill comes within 3 m of his look's zone (that's all his look
does besides the line).

**Lesson:** two zones sharing one spot means one press, two scripts. Read which script each zone boots
(`[[zone+0x340+0x40]+4]+8`), and guard that read: zones without a real boot hold garbage there.

### 7.3 Lost in Nightmares: the basement wake-up

**Symptom:** the camera panned but Jill's wake-up animation never played. The area controller (`aS705`,
vtable `0xf579b0`, update `+0x1c` = `0x761550`) runs a step machine at `+0x6ec`. Step 1 counts ready players
(`+0x14 >= 1` and `+0x15 >= 1`) and needs 2, and step 2 hands each player the `aS705::Motion` action. Fix: a
vtable copy of the controller. At step 1, with the solo player ready and no partner, it's moved to 2, and the
game plays her animation itself.

### 7.4 Lost in Nightmares: the final fight's checkpoint (and a misdiagnosis)

**Symptom:** restart from checkpoint during the final fight went back to before the boss (scene 14750).

**Investigation:** the area has two scenario zones: scene 99999 (the intro trigger, which only boots
`ev706_00`: SceNo 14755 -> event -> 14760, fight) and scene 14770. The 14770 zone's box (x 605..1270,
z -1500..-620) never had Jill inside: the intro places her at (482, -818), just outside its edge. In co-op, Chris
starts inside it.

**The misdiagnosis:** the first fix fired the zone at scene 14760, and the area "reloaded to 14750" a second
later. The conclusion was "that zone sends players back", and the fix was reverted. It was wrong: the tester had
pressed Restart within two seconds, before the save had run. What settled it was **naming the zone's argument**:
`cScenarioArg_CHP`, the checkpoint type. Firing it by hand during the fight showed the saving icon, the snapshot
scene (`[[0x1234578]+0x382c]+0x130+0x41d0`) became 14770, and the fight carried on. The fix went back in.

**Lessons:** a reload right after your change may be the user, not your change. Check the logs for the order of
events (the loader logs "players removed", "uIdSaving00" etc.), and name the objects before concluding what
they do. Watch the snapshot scene to know whether a checkpoint was really saved.

### 7.5 4-2: crashes after the collapsing-floor QTE (playing as Sheva)

The 4-2 collapse needs Chris, so he is spawned as a hidden helper and then taken out of the player list
(`TakeOutChris`), so the rest of the game treats Sheva as alone. **Symptom:** crashes on the next area change
and on quitting to the menu. Cause: the game unloads an area's players through the player manager's `removeAll`
(manager vtable slot `+0x14`, `0x7e94b0`), which runs `removePlayer` (`0x7e6c20`) on each of the 4 slots. Out of
the list, Chris was skipped and lived on into the next area, where quitting to the menu ran his code into the
freed stage (fault at `0xc6c487` via `0xb83494`, read out of the minidump). Fix: the manager (already on a vtable
copy) gets its `removeAll` slot replaced. Just before the original runs, Chris goes back into his slot and is
removed with the others; the count still included him, because taking him out never lowered it. Taking him out
is an interlocked exchange, so the watcher and the game thread can't both act on him. A fallback clears his
weapon-unit pointers (`+0x21d0 + 0x30*i`, 24 entries, seen in the crash dump) if he ever outlives an area anyway.

**Lesson:** an object you hide from the game must still be destroyed by the game. Give it back before teardown.

### 7.6 Silent crashes and the crash recorder

**Symptom:** the game closed with no dialog, no dump, and nothing in the log. Cause: the vectored handler saw
first-chance exceptions, but the game handles some and lets others reach an unhandled filter, which the game and
Steam replace, so ours was gone. Fix: section 2.6. The second-chance path now always writes
`UNHANDLED code at eip (module+offset)`, the registers, the call stack and a minidump.

### 7.7 Stuck on scenery at 120 fps, and on stairs

Diagnosed with a per-frame recorder: the start position never changed while the step was 3-4 units. The
collision put the player back to `+0x1060` every frame, because the half-length step at 120 fps always landed in
the same rejected spot. Fix in 6.9.

Stairs were harder. Widening the step on every catch made walking on stairs *worse* (each widened step was
itself rejected). The fix only widens while that actually frees the unit. It backs off after fruitless frames,
cools down, and escalates to the 30 fps step only after 2 fruitless widened frames. Some stair corners still
catch a little. The tester accepted that rather than risk making it worse.

**Lesson:** when a fix for one case hurts another, gate it on evidence that it helps (here, "did the last widened
step get anywhere?").

### 7.8 Mkono (5-2) can't be hurt at 120 fps

Part hits only do full damage while it's open. It opens when its heat (`+0x2148`) passes 2200, and each flame
hit adds `(30 - n) * 3 * frameStep`. Hits come ~10/s at any frame rate, so at 120 fps (step 0.25) the meter never
filled. Hardware watches didn't fire. **Scanning the dump for every instruction with displacement `0x2148`** found
the writer in seconds. Fix: wrap the hit handler (`+0x208`) and divide the gain by the step.

### 7.9 3-1 crocodiles ignore Sheva, and the threading trap

Their AI scans slots below the count (1), so it never sees slot 1. Wrapping the think to raise the count fixed
it, except for the occasional crocodile that surfaced, gave up and dived. Cause: AIs think on several worker
threads at once, and per-call save/restore let one thread put the count back while another was mid-think. Fix:
a shared reference count (first in raises, last out restores) under a lock.

**Lesson:** the game is multithreaded in places you don't expect. Any global you change temporarily needs a
reference count.

### 7.10 The absent character's weapons vanish

The stage record keeps both characters' loadouts in three copies (A the level's, B, C Item Management's).
A chapter load empties A, writes back each **spawned** player's items, and copies A over B and C. The
never-spawned partner's weapons were wiped. Fix: remember the partner's block from C while no player exists,
and write it back into any copy that lost it once the player exists. Verified by reading the save file's blocks
afterwards.

### 7.11 Cutscenes need the absent character's model

Scenes use stand-in actors that load the *partner's* model by path. With no partner the archive isn't loaded
and the game quits ("Failed open file ... pl0100.mod"). RE5 falls back to loose files under `nativePC_MT`, so the
installer unpacks that character's files out of the player's own archives (ARC v7, zlib entries). It ran into
more missing files one at a time: the knife model, the knife's motions (`pl00wpXX.lmt`), then its effect, so in
the end the whole weapon archive is unpacked. The quit isn't a crash: Windows logs it as an "Application Popup"
in the System event log, which is where to look when the game just closes.

### 7.12 Tabbing out pauses the game

Two causes. Exclusive fullscreen minimises on `WM_ACTIVATEAPP`, and the pause manager opens the menu when the
window isn't in front (sMain `+0x10548..a`, set each frame from `IsIconic`/`GetForegroundWindow`/`GetFocus`).
Fix: borderless window (the config's `FullScreen=OFF` plus the frame removed), and a vtable copy of the pause
manager's update that reports "in front" for that call while the menu is closed. Dead ends are recorded in the
README, so nobody retries them.

---

## 8. Mistakes worth not repeating

- **Hex arithmetic slips.** `0x1091dba0 + 0x10e0` is `0x1091ec80`, not `0x1092ec80`. A poke 64 KB off target
  overwrote a float in some other object (put back within seconds). Let the tool do the addition: use
  `poke.py base,offset` pointer chains, or compute in Python, never in your head.
- **Recognising objects by vtable after you've patched them** (7.1, bug C).
- **`static` state in a function that runs for many objects** (one throttle timer, one "last seen" value shared
  by every ceiling). Keep per-object state beside the object's own entry.
- **Blaming your change for something the user did** (7.4). Check timestamps and the log order first.
- **Sampling a paused game.** If nothing in the object changes, check that the player's own fields change before
  concluding a hook isn't running. The game may be paused or tabbed out.
- **Faking a state permanently instead of only for the reader that needs it** (7.1, first fix).
- **Fixing the symptom.** Setting "players needed = 1" on boxes, writing the end state of a platform, zeroing a
  wind value: each left the object in a state its other code didn't expect. Make the game run its own logic.
- **Assuming the exit undoes the enter** (sounds, effects).
- **Leaving a test tool on in a release** (the F11 area skip was on for everyone until an audit caught it; now
  it needs the debug-keys option).
- **Shell editing on Windows:** Git Bash `sed -i` turns CRLF files into LF, and heredocs mangle `\\` and quotes in
  C++ escapes. Write edit scripts to a file and run them, or use a real editor/tool.
- **Log spam.** A malformed control file re-read and logged every 100 ms; a per-frame log line hides the one that
  matters. Log on change, and delete bad control files after reading.

---

## 9. Releasing

- Version in `src/version.h`, tags `vMAJOR.MINOR.PATCH`. The launcher checks
  `api.github.com/repos/<owner>/<repo>/releases/latest` on start, offers newer tags, and downloads the
  `*Launcher*.exe` asset (else the zip, unpacked with Windows' `tar`). It checks the download against the
  asset's listed `size` and `digest` (`sha256:...`, which GitHub provides per asset), accepts https only, swaps
  itself (a running exe can be renamed, not overwritten), and restarts with `--updated`, which installs the mod
  files and deletes the old exe.
- Test the updater against a local server serving fake release JSON (good exe, good zip, wrong digest, wrong
  size, no digest), then once against the real release with a build that reports an older version.
- Attach the launcher exe beside the zip on every release, or the updater falls back to the zip.
- Never commit the game dump, extracted scripts, saves (they contain the Steam ID), logs or dumps.

---

## 10. Address quick reference (Steam Gold `re5dx9.exe`)

| What | Address / offset |
|---|---|
| Stage record | `[0x11b2158] + 0x5940*[[0x11b2158]+0x20]`; area `+0x273d8`, SceNo `+0x273e0`, character `+0x273c4`, RSF `+0x23218` |
| Checkpoint snapshot scene | `[[0x1234578]+0x382c]+0x130` then `+0x41d0` |
| Stage controller | `[[0x1234578]+0x3830]` |
| Session | `[[0x12340a4]+0x1042c]`: `+0x478` active mask, `+0x480` control |
| Solo flag | `[0x12340a4]+0x10434` bit `0x80000`; `requiredPlayers()` `0xa83070` |
| Player manager | `[0x11b27dc]`: slots `+0x24`, count `+0x34`; vtable `0xf653ac`; removeAll `+0x14` = `0x7e94b0` |
| Player spawn loop | `0x7e99f0` -> `spawnPlayer(slot)` `0x7e9800` |
| Units | `[0x12340b0]`: line i at `+0x20+0x20*i` (`+0xc` first, `+0x18` index), chain `+8`, enemies line 9 |
| Area root (zones) | `[0x11e8660]`: area-hit managers `+0xd4+4i`; scenario lists `+0x24+0x14*L` |
| Area jump | `0x768c10()` -> obj; `0x71dfc0(area, point, mode)`; `0x71ae60(scene)` |
| Area exit object | `uOmf301` DTI `0x11f8e5c`: `+0x1c20` area, `+0x1c24` point |
| Object basics | `+0x30` pos, `+0x10e0` action index, `+0x10f8` action object, vtable `+0xdc` set action |
| Level scripts | `uFSMTask` `0xf9da80` -> `+0x18` cFSMOrder `0x103d7a8` -> `+0x10` core (`+8` state) |
| Sound request | `0x7a4630` on `[0x11b1d0c]`, `(owner [obj+0x1454], id, obj, obj, 0)` |
| Message window | `sMessage::open` `0x7db3a0` |
| Cutscene QTE | `[0x11b2018]+0xf0` (cEventActionButton) |
| Pause manager | vtable `0xfa821c`, update `0xa308c0` |
| Crank (uOmCrank) | DTI `0x11f9a28`; wind `+0x1970`, max `+0x1974`, in-use `+0x1995`, flags `+0x19a4`; actions wait `0xfccb58`, rewind `0xfccb80` |
| Ceiling (705) | vtable `0x1010d08`, update `+0x160` = `0xb31de0`, crank `+0x197c`, box `+0x1a00..+0x1a10`; Land `0x101097c`, Rise `0x10109a4` |
| 705 area controller | vtable `0xf579b0`, update `+0x1c` = `0x761550`, step `+0x6ec` |

For everything else (each co-op object, boss and option), see `README.md`.
