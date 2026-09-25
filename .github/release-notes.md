online co op for twilight princess. 2 to 16 players in one world.

put crests_of_courage.dusk in dusklight's mods folder. a co op tab shows up in the menu bar.
one player presses host and reads out the room, everyone else types it and presses join.

only windows has been played. mac, linux, android and ios are built here but untested.
enemy sync, boss sync and story progress are off by default under unfinished.

this version connects to 1.5.0, so nobody has to update at the same time.

this build:
crashes:
- save and quit, or resetting, while connected no longer crashes
- fixed the game running out of memory and closing with lots of players, especially as people
  joined or changed clothes or models. three separate leaks
- fixed a stray player name drawn in the corner of the screen

world:
- no more endless textboxes with villagers (jaggle on the vines, uli after the cradle, "where has
  talo gone"). their conversation flags are only shared inside dungeons now, and changes from
  other players wait until your conversation or cutscene is over
- a chest opened in a house (like the wooden sword chest in ordon) now stays open for everyone
- things another player is carrying (like uli's cradle) no longer flicker
- teleporting to someone arrives through the right door

notifications:
- a new notification system that stacks, slides in and bursts through a backlog like steam's.
  everything is under local > notifications: what shows, timing, side, position, size, and a
  preview to see changes as you make them. dusklight's own are still an option

joining:
- joining with a file you've played keeps your own rupees, arrows, bombs, seeds and hearts
  (on by default, under local > joining). a brand-new file takes the host's
- you never arrive at the host's current health any more
