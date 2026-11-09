# Known issues

Current as of 1.1.0 (2026-11-09). The mod has not had a full playthrough of every chapter, so expect softlocks
that aren't listed here. Saves are backed up automatically to `re5_solo_saves` next to the game's exe.

## Open bugs

- **Items can be duplicated through Organize (seen in 5-3, playing as Sheva).** Take items off the absent
  character or give one to the player, click Ready, die, then choose Continue: the absent character has the old
  items again and the player keeps theirs. v4 fixed this for Restart from Checkpoint. 1.0.0 has a further fix (any
  copy that changes is adopted, with or without a player), but it hasn't been retested on this route. It isn't
  known yet whether this is specific to 5-3, where Jill may be involved.
- **Taking every item off the absent character may not stick.** If the level's copy of the loadout is empty at the
  same time, this looks the same as the wipe a chapter load does, so the items come back. Found by reading the code,
  not seen in play.
- **The absent character's loadout across a chapter change** may need more work. Switching character in the menu
  wiped the other character's weapons before 1.0.0; that is fixed.
- **3-1 airboat on other people's controllers:** a tester's pad still couldn't drive the boat. The mod remaps the
  boat's controls for XInput pads and for DirectInput pads (from the game's `config.ini` layout); DirectInput pads
  haven't been tested at all.
- **2-3 Ndesu as Sheva: ducking may not always dodge.** The game only ever crouches player 0 (Chris) on the duck
  prompt; as Sheva the mod plays the same crouch on her, with Chris's values as a guess. She seems to get hit
  sometimes anyway.
- **2-3 jeep:** a stray orange aim arc shows on screen.
- **120fps fix on stairs:** some stairs (e.g. Lost in Nightmares' area 700) still catch a little at 120 fps, mostly on
  corners. The fix now widens the step only while that frees the player and backs off on stairs, where it made things worse.

## Not handled or not checked

- **The Jill fight (5-3)** has no solo handling written for it and hasn't been checked for co-op-only moves.
- **Playing as Sheva:** the game's player count stays at 1 with only slot 1 filled, and 62 places read it. Enemies
  or objects that ignore Sheva, or crashes that only happen as Sheva, probably come from this. The cases found so far
  are fixed.
- **Listed in `re/needs2p.txt` but not checked:** `uOm0201_Piano`, `uOmClutch`, `uOmS103ScrAdj`, `uOmJeepRail`,
  Jill's `uEm83`/`uEm84` pinion, and the `uEm82` pinion pairs.

## Not tested

- Lost in Nightmares (DLC) has been played through once as Jill, but parts were reached by checkpoint or by
  skipping areas, not in one run: the trap-room move and the one-press piano (flag 17 set on approach; the piano
  worked in two presses) haven't been seen from a fresh start. Area 705's radio calls were skipped by hand (no
  audio device in testing), and one crank-enable flag (2) was set by hand. The final fight's checkpoint was seen
  saving when fired by hand; the automatic version hasn't been seen yet.
- The take-all gap on Organize (see above)
- The borderless option on monitors other than 4K, and with more than one monitor (the window fits the one it is on)
- Linux and Proton, including the launcher's Wine setup and the boat's XInput hook

## Limitations

- **Only the Steam Gold Edition (`re5dx9.exe`) is supported.** The mod uses fixed addresses, so a game update or
  another release will break it.
- **Steam's integrity check** is why the mod never patches game code. The debug option's hardware write watch can
  still trigger it occasionally.
- **Other `dinput8.dll` mods don't run alongside this one.** The launcher keeps the other file as
  `dinput8.dll.before-re5solo` and restores it on uninstall.
- **Custom weapon packs:** Nutelcio's Custom Weapons Pack replaces `uWp01.arc` and has caused a "Failed open file ...
  wp0100.mod" quit. The current install unpacks that file, but the pack hasn't been tested with it. Other packs that
  replace `uWp*` archives may do the same.
- **Older installs:** one made before the knife unpack can quit in 3-2 as Sheva. Install again from the launcher.
- **Online:** join requests aren't blocked, and online play with the mod isn't supported. Split screen is disabled.
- **The removed character still appears in in-engine cutscenes.** This is intended.
