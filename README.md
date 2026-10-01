# Crests of Courage

Online co-op for Twilight Princess, as a mod for [Dusklight](https://github.com/TwilitRealm/dusklight).
2 to 16 players in one world.

Everyone needs the same version of the mod.

## Install

Put `crests_of_courage.dusk` in Dusklight's `mods` folder. A Co-op tab shows up in the menu bar.

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
`mod_data/dev.remiafterdark.coop_mod/models`, one folder each, and the Models tab opens that folder.
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
python tools/package.py --bundle build/mods/coop_mod.dusk --models models --out crests_of_courage.dusk
```

## Credits

By remiafterdark. Built on Fimmel's puppet and model loading code, the Dusklight team's port and
SDK, and the Twilight Princess decompilation. The models belong to their makers, see
[CREDITS.md](CREDITS.md). MIT covers `src/`, not `models/`, see [LICENSE](LICENSE).
