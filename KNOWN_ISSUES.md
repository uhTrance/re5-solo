# Known issues

Current as of the 2026-11-02 v4 package. The mod has not had a full playthrough of every chapter, so expect
softlocks that aren't listed here. Saves are backed up automatically to `re5_solo_saves` next to the game's exe.

## Open bugs

- **Items can be duplicated through Organize (seen in 5-3, playing as Sheva).** Take items off the absent
  character or give one to the player, click Ready, die, then choose Continue: the absent character has the old
  items again and the player keeps theirs. v4 fixed this for Restart from Checkpoint, but it may still happen on
  this route. A further fix is being tested and isn't in a package yet. It isn't known yet whether this is
  specific to 5-3, where Jill may be involved.
- **Taking every item off the absent character may not stick.** If the level's copy of the loadout is empty at the
  same time, this looks the same as the wipe a chapter load does, so the items come back. Found by reading the code,
  not seen in play.
- **4-1: a shield enemy under the bridge doesn't spawn, and others in that group may be missing.** The game waited
  for a player on the far landing. Fixed and confirmed in development, but not in a package yet. The fix also brings
  in four more enemies about half a second later, three of them far from the player; that hasn't been compared with
  vanilla co-op, and the rest of 4-1 hasn't been replayed with them.
- **2-3 jeep:** a stray orange aim arc shows on screen.

## Not handled or not checked

- **Co-op climbs and ladders**, such as the ladder in 3-1, have no solo handling written for them.
- **The Jill fight (5-3)** has no solo handling written for it and hasn't been checked for co-op-only moves.
- **Playing as Sheva:** the game's player count stays at 1 with only slot 1 filled, and 62 places read it. Enemies
  or objects that ignore Sheva, or crashes that only happen as Sheva, probably come from this. The cases found so far
  are fixed.
- **Listed in `re/needs2p.txt` but not checked:** `uOm0201_Piano`, `uOmClutch`, `uOmS103ScrAdj`, `uOmJeepRail`,
  Jill's `uEm83`/`uEm84` pinion, and the `uEm82` pinion pairs.

## Not tested

- 5-2 levers since the change to pulling each lever in turn
- The absent character's loadout across a chapter change
- 4-1 as Sheva
- The take-all gap on Organize (see above)
- The latest Ndesu attack changes, in live play (tackle and overhead pole swing)
- Elevators through the mod's own code (the same write done by hand worked)
- Pulleys and partner voice lines
- The boat's pad remap and hint box
- DirectInput pads
- Ndesu's crouch as Sheva, which uses Chris's values as a guess
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
