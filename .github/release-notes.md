online co op for twilight princess. 2 to 16 players in one world.

put crests_of_courage.dusk in dusklight's mods folder. a co op tab shows up in the menu bar.
one player presses host and reads out the room, everyone else types it and presses join.

only windows has been played. mac, linux, android and ios are built here but untested.
enemy sync, boss sync and story progress are off by default under unfinished.

everyone needs this version: it will not connect to 1.3.0.

this build:
randomizer, properly multiplayer:
- one check, one payout. once anybody collects a check, nobody else's copy of it can pay out
  again - it gives a rupee instead. this was the dupe that let four players turn one sword into
  the master sword
- items lying in the room vanish for everyone the moment somebody takes theirs: heart pieces,
  heart containers, small keys and rupees, not just field items
- the randomizer's own items are shared: warp portals, each dungeon's small keys, big keys, maps
  and compasses, fused shadows, mirror shards, and hidden skills as items
- progressive items count for the whole team. two players finding two sword checks at the same
  moment both end up with the sword two checks are worth, instead of one being lost
- joiners no longer pick the host's seed. join with any randomizer file and it switches itself
  to the host's seed and settings
- in a randomizer the hero's shade is gone for everyone once anyone beats him, since he pays out a
  check rather than the skill
fixes:
- the game running out of memory and closing at doors, or when holding things for a while
- the small temple of time statue falling out of the other player's hands, bumping into you, and
  being picked up by two people at once
- the item another player holds up after opening a chest shows the right model, size and shine
- warping to a player or joining puts you next to them as soon as the room loads, not seconds later
