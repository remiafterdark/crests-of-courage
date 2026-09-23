online co op for twilight princess. 2 to 16 players in one world.

put crests_of_courage.dusk in dusklight's mods folder. a co op tab shows up in the menu bar.
one player presses host and reads out the room, everyone else types it and presses join.

only windows has been played. mac, linux, android and ios are built here but untested.
enemy sync, boss sync and story progress are off by default under unfinished.

this build:
a crash hunt. nothing a player model owns is ever freed on the spot any more, so the renderer
can never be left reading something that was thrown away underneath it
everyone sees a model's own bow, clawshot, rod and iron ball, not just the player holding it
the body draw borrows the shared materials for one call, like it already did the joints
