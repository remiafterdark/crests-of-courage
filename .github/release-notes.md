Online co-op for Twilight Princess. 2 to 16 players, one shared world.

Put `crests_of_courage.dusk` in Dusklight's `mods` folder. A Co-op tab appears in the menu bar.
One player presses Host and reads out the room, everyone else types it and presses Join.

**Only Windows has been played.** The Mac, Linux, Android and iOS builds are in here and they
compile, but nobody has run them yet. If you are on one of those, please say whether it even
starts, and send Dusklight's log if it does not.

Enemy sync, boss sync and story progress are off by default under Unfinished. Enemy sync in
particular is rough.

## Since 1.0.5

- A crash while drawing other players, found in a real crash log rather than guessed at. Materials
  are shared between every model built from the same archive, and the game hangs its texture and
  colour animations on them while it draws. Ours could walk one of those after whoever entered it
  had freed it. The guard that has always protected the shared joints now covers the shared
  materials too.
- Tears of Light counted once per player in the zone instead of once. Two separate ways of
  announcing a tear somebody else had already collected, both from a stale picture of which ones
  were taken.

## Since 1.0.4

- A boomerang somebody else threw could kill your game outright if Link's own archive was not
  resident where you were standing, which is exactly what being a wolf means. The engine builds a
  boomerang's aiming cursor out of that archive without ever checking it is there. Now it is
  checked, and the boomerang simply is not copied rather than taking the game with it.
- Bombs, boomerangs and arrows all had pools sized for two players. Past them other people's
  bombs and shots stopped appearing, and with arrows the list that tells your own shots from
  copies could wrap mid-flight and send a copy back round the party.

## Since 1.0.3

The crashes with more than a couple of players. Several things in here were sized back when one
other player was the only case, and they all gave way at about the same party size:

- The animation cache held sixteen entries and every puppet shared it. Four players need more than
  that, so loading the last puppet's animations threw away ones the first puppet was still using,
  and the game read them anyway a moment later while drawing. That is the crash where several
  people go down at once and whoever was alone does not.
- The teardown queue held 64 models where one stage change with a full room needs hundreds. Past
  the limit it freed models the frame was still drawing.
- Held and thrown objects, and blows waiting to be replayed, had eight slots between everybody.
  Past that they were silently dropped, which is why other people's pots and hits stopped landing
  in a big group.

## Since 1.0.2

- The goats are no longer synced. Each game runs its own, which is the only version where the
  herding minigame finishes.
- When the game crashes it now leaves a `coop-crash-trail.txt` next to `dusklight.exe` saying what
  it was doing. If you crash, send that file: it is the one thing that survives.

## Since 1.0.1

- Everyone's colours are their own again. With three or more players they were shared: whoever
  sent theirs last painted the whole party.
- Players past the fourth no longer turn up wearing your colours.

## Since 1.0.0

- A chest somebody else opened can no longer be opened again, including the blue chests, which
  the game does not record in the save at all and which nothing was syncing.
- Racing for the same chest no longer gives it to both of you.
- Pots stay still in the other player's hands instead of rolling while they run.
- Thrown objects land on the floor instead of sinking through it and snapping back, and a throw
  keeps tracking the real one all the way to the ground.
- New Hero of Time model, with the outfits and equipment the old one was missing.
