online co op for twilight princess. 2 to 16 players in one world.

put crests_of_courage.dusk in dusklight's mods folder. a co op tab shows up in the menu bar.
one player presses host and reads out the room, everyone else types it and presses join.

only windows has been played. mac, linux, android and ios are built here but untested.
enemy sync, boss sync and story progress are off by default under unfinished.

this version needs dusklight 2.0.2, and everyone has to update: it only connects to 1.7.8.

this build:
- shields should show up again, on other players and on yourself. getting a shield from another player left your game stuck halfway through swapping it until you opened the collection menu or changed area, and no shields were drawn while it was stuck. I'm fairly sure this was the cause
- other players carrying the ordon shield now have it drawn (it used to show nothing)
- twilit beasts should no longer get stuck shrieking forever and stunning everyone. I think this was also the zora's domain soft-lock
- if midna couldn't be called anymore after ordon, she should come back by herself (you'll get a notification). this happened when another player went through the ordon spring before you
- a big key (or small key, chest, switch) picked up during the item cutscene could get wiped from everyone's game when the dungeon resynced at the same moment. that shouldn't happen anymore. it won't bring back a key that's already gone, so reopen the chest if it's still there
- loading a dusklight state (the state share window) now takes you out of co-op. it replaces your whole save, and co-op was handing everything in it (all items, all skills) to everyone else. rejoin to get the shared world back. to reach another player, pick them in co-op > players
- games short on memory now borrow it from elsewhere, so other players (wolf form especially) should stop staying invisible there. tested on a low-memory setup, not on the reporters' pcs
- games that are nearly out of memory should crash less when someone loads in. their model just takes a little longer to appear
- a player whose game closes no longer stays behind as a frozen copy next to their rejoined self
- rutela should now appear in the kakariko graveyard in co-op rando when she was missing. only checked against the logs so far
- in the randomizer, a sword from another player could arrive at the wrong tier or not at all. this is hopefully fixed
- less background resync spam in ordon and other non-dungeon areas
- if your game crashes (switching tunics, going through a loading zone, anything), restart and send a report with "the game crashed last time" picked. it now includes what I need to find it
