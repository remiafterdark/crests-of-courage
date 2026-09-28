online co op for twilight princess. 2 to 16 players in one world.

put crests_of_courage.dusk in dusklight's mods folder. a co op tab shows up in the menu bar.
one player presses host and reads out the room, everyone else types it and presses join.

only windows has been played. mac, linux, android and ios are built here but untested.
enemy sync, boss sync and story progress are off by default under unfinished.

this version needs dusklight 2.0.2, and everyone has to update: it only connects to 1.7.5.

this build:
- fixed other players showing as paused and never appearing when the host was joined by name (a tailscale machine name, for example) instead of an ip
- fixed other players freezing for good when a phone's connection moved (wifi to mobile data, or the network changing its port)
- fixed teleporting to a player you can't see putting you under the map
- the player list says "no signal" instead of "paused" when someone's position isn't reaching you
- the update check works now (it never did)
- fixed enemies getting a new name in long sessions
- a player who falls too far behind is told so, instead of "timed out"
