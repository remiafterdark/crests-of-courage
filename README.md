# Crests of Courage VR

A fork of [Crests of Courage](https://github.com/remiafterdark/crests-of-courage) by remiafterdark
for the [Dusklight VR port](https://github.com/JoeyAW/TPVR). Online co-op for Twilight Princess,
2 to 16 players in one world, where everyone sees VR players' real head and hand movements.

- **VR players** (Dusklight VR port, in first person): your headset turns your Link's head, and
  his arms follow your controllers, for everyone else. That covers swords, shields and anything
  else held in your hands.
- **Flatscreen players** can use this mod on regular Dusklight too. They see VR players move,
  and play normally themselves.
- **It's its own network.** It doesn't connect to the original Crests of Courage, and the original
  doesn't connect to it. Everyone in a room needs Crests of Courage VR, and the same version.
- **Bug reports aren't sent anywhere.** Report Bug only shows where the log is. Please don't report
  this fork's bugs to the original mod.
- The VR pose needs a VR port build that has `dusk_vr_get_player_pose`
  (`sdk/include/mods/vr_pose.h`). Older VR builds still play, but they don't send VR movement.

Everything below is from the original README and still applies.

## Install

Put `crests_of_courage_vr.dusk` in Dusklight's `mods` folder, and take the original
`crests_of_courage.dusk` out if it's there. A Co-op tab shows up in the menu bar.

## Playing

One player presses Host and gets a room code. Everyone else types it and presses Join. No port
forwarding needed.

Some networks (mobile data, school, work) block that, and the game will tell you. Then use
[Tailscale](https://tailscale.com) and Join by address with the host's Tailscale address. On the same
wifi, Join by address with the host's local address works too. The port is 27716, TCP and UDP.

Joining loads the host's save. Yours is backed up first, and Restore latest backup in Advanced
brings it back.

Room codes go through a small server. `server/` has it if you want to run your own.

## Randomizer

Works with the [Dusklight randomizer](https://github.com/TwilitRealm/dusklight-randomizer). Pick
Co-op + Randomizer on the title screen. The host's seed gets sent to everyone who joins.

## Models

Everyone can wear a different model and everyone sees it. Models go in Dusklight's
`mod_data/dev.joeyaw.coop_mod_vr/models`, one folder each, and the Models tab opens that folder.
Outfits, wolf, equipment and voice can each come from a different model. A few ship with the mod.

## Unfinished

Some features are off by default under Unfinished in the Host tab, like story and enemy sync. They
can break things, so use them knowing that.

## Bugs

Use Report Bug in the Co-op tab. It sends your log and everyone else's.

## Building

Pushing to GitHub builds every platform into one `crests_of_courage.dusk` (see
`.github/workflows/build.yml`). Tagging a commit makes a release.

Locally you need CMake, Ninja and a compiler:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCOOP_PUBLIC_BUILD=ON
cmake --build build
python tools/package.py --bundle build/mods/coop_mod.dusk --models models --out crests_of_courage_vr.dusk
```

## Credits

By remiafterdark. Built on Fimmel's puppet and model loading code, the Dusklight team's port and
SDK, and the Twilight Princess decompilation. The models belong to their makers, see
[CREDITS.md](CREDITS.md). MIT covers `src/`, not `models/`, see [LICENSE](LICENSE).
