# Crests of Courage

Online co-op for Twilight Princess, as a mod for [Dusklight](https://github.com/TwilitRealm/dusklight).
2 to 16 players in one shared world.

Windows, Mac, Linux, iOS and Android. Everyone needs the same version of the mod.

## Install

Put `crests_of_courage.dusk` in Dusklight's `mods` folder. A **Co-op** tab appears in the menu bar.

## Connecting

One player hosts, everyone else joins.

- **Host:** press Host. You get a six letter room code, or type your own room name first.
- **Join:** type that room and press Join.

Nothing to set up, no port forwarding. The games find each other and talk directly.

Some networks are too strict for that, usually mobile data, school and office networks. The game
says so when it happens. Then everyone installs [Tailscale](https://tailscale.com) (free) and uses
**Join by address** with the host's Tailscale address. On the same wifi, Join by address with the
host's local address always works. Forwarded port is 27716, TCP and UDP.

Room codes go through a small server. `server/` holds it and explains how to run your own for free.

**Joining loads the host's save**, at the host's position. Yours is backed up first: disconnect,
load your save, then press Restore latest backup in Advanced.

## What syncs

- Each other: animation, clothes, colours, held items, wolf form, Midna, the lantern
- Name tags, everyone's hearts, and health at their feet
- Sounds and attack effects
- Epona with her reins, the canoe, the Snowpeak board
- Items, chests, switches, doors, traps, turning mechanisms, Dominion Rod statues
- Torches, and timed hazards like fire vents, geysers and water columns
- What pots, grass and enemies drop. Same item for everyone, picking it up is still yours
- Twilight bugs and Tears of Light
- Anything you pick up and throw, bombs included
- Pots, grass, flowers, pushed blocks, cuccos and goats
- Boss cutscenes skip once everyone has pressed skip
- Time of day, and optional death link
- Custom models, per player

## Custom models

Everyone can look like somebody else and everyone sees it. The game's own files are untouched.

Models live one folder each in Dusklight's `mod_data/dev.remiafterdark.coop_mod/models`. The Models
tab opens that folder and reloads it after you add one.

You do not have to wear a whole model. Each part is chosen on its own:

| Part | Covers |
| --- | --- |
| Hero's clothes, Ordon clothes, Zora armor, Magic armor | the four outfits |
| Wolf | wolf form |
| Equipment | swords, shields, hookshot, everything held |
| Cutscenes | the copies of Link cutscenes use |
| Voice | the grunts and shouts, heard by everyone |

Voices are per player too, so two people can sound different to each other with no restart.
Dialogue is the exception: text is not per player, so that switch only changes your own game.

Linkle, Dark Link and the Hero of Time ship with the mod. Add your own; a folder with the same name
as a shipped one wins.

## Experimental

Off by default, under **Unfinished** in the Host tab. The host's settings are the ones used.

- **Story progress:** cutscenes and quest flags are shared. Can leave a quest stuck.
- **Enemy sync:** one set of enemies for everybody instead of a copy each. Rough.
- **Boss sync:** Ook and Diababa only. Can softlock the fight; turn it off and re-enter.
- **Hold boss fights:** the fight waits until everyone walks in. If nobody else comes, you wait.

The warp effect on other players is also off, because it could crash.

## Bugs

Say what happened, where, and whose screen it was on. The mod writes what it is doing to
Dusklight's log, so send that too.

## Building

Push to GitHub and `.github/workflows/build.yml` builds all eight platforms and packs them into one
`crests_of_courage.dusk`, under the run's artifacts. Tag a commit and it goes on a release.

Locally you need CMake, Ninja and a compiler. Dusklight's source is fetched at the pinned version
unless you keep a checkout at `../dusklight`:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCOOP_PUBLIC_BUILD=ON
cmake --build build
python tools/package.py --bundle build/mods/coop_mod.dusk --models models --out crests_of_courage.dusk
```

Leave `DUSK_GAME_EXE` unset and it links against Dusklight's version independent stub, which is
what a release wants. Setting it ties the build to one exact Dusklight build.

## Credits and licence

By **remiafterdark**. Built on **Fimmel's** puppet and model loading code, the Dusklight team's
port and SDK, and the Twilight Princess decompilation.

The models are other people's work, shipped with permission. MIT covers `src/`, not `models/`.
Both are in [CREDITS.md](CREDITS.md) and [LICENSE](LICENSE).
