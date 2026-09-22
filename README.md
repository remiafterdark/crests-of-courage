# Crests of Courage

Online co-op for Twilight Princess, as a mod for [Dusklight](https://github.com/TwilitRealm/dusklight).
2-16 players, one shared world.

**Early release.** Enemy sync and boss sync are experimental - read the warnings below.

## Requirements

- Anywhere Dusklight runs: Windows, Mac, Linux, iOS and Android.
- Everyone needs the same version of this mod. Different versions won't connect.

## Install

Drop `crests_of_courage.dusk` into Dusklight's `mods` folder (delete any old `coop_mod.dusk` there). A **Co-op** tab shows up in the in-game menu bar.

## Connecting

One player hosts, everyone else joins.

- **Host:** press **Host**. You get a six-letter **room code**, or type a **Room name** of your own
  first.
- **Join:** type the code or name and press **Join**.

There's nothing to set up. The games find each other and connect directly, with no port
forwarding.

Some networks are too strict for that, like some mobile data and some school or office networks.
If yours is, the game tells you. Then everyone can install [Tailscale](https://tailscale.com)
(free) and use **Join by address** with the host's Tailscale address. On the same wifi, **Join by
address** with the host's local address always works. For a forwarded port it's 27716, TCP and UDP.

The first time you host, your device may ask whether to let Dusklight through its firewall. Allow
it, or nobody can reach you.

Room codes need a small room server. `server/` has it, and `server/README.md` explains how to run
your own for free.

**Joining uses the host's save.** Your own progress is backed up first - disconnect, load your save
and press **Restore latest backup** (or **Restore oldest backup** for the very first one) to get it back.

## What syncs

- Each other: animations, clothes, colors, items in hand, wolf form, Midna, the lantern and its light
- Name tags (they stick to the screen edge when someone's off-screen, and go red at low health)
- Everyone's hearts under your own, and each player's health at their feet
- Sounds and attack effects
- Each other's Epona, reins and all, and the canoe and the Snowpeak board
- Items, chests and dungeon progress: switches, doors, traps, turning mechanisms, Dominion Rod
  statues
- Timed hazards: fire vents, flame jets, geysers and water columns
- What pots, grass and enemies drop (everyone gets the same item; picking it up is still yours)
- Twilight bugs and Tears of Light, even with enemy sync off
- Things you pick up and throw, bombs included
- Pots, grass, flowers, pushed blocks and other world objects
- Skipping a boss cutscene: it's skipped once everyone has pressed skip
- Time of day
- Optional death link
- Custom models, per player - see below

## Custom models

Every player can look like somebody else, and everyone sees it. The game's own files are left
alone: nothing here replaces the Link your copy of the game ships with.

Models live in one folder per model under Dusklight's `mod_data/dev.remiafterdark.coop_mod/models`. The
**Models** tab has a button that opens it, and a **Reload** button for after you add one.

You do not have to wear a whole model. Each part chooses its own, so you can wear one model's
hero's clothes, another's Ordon clothes, a third's sword and hookshot, and a fourth's voice:

| Part | What it covers |
| --- | --- |
| Hero's clothes, Ordon clothes, Zora armor, Magic armor | the four outfits |
| Wolf | wolf form |
| Equipment | swords, shields, the hookshot - everything held |
| Cutscenes | the copies of Link that cutscenes use |
| Voice | the grunts and shouts, heard by everyone |

Voices and models are per player, so two people wearing different models sound and look different
to each other, with no restarts. **Dialogue** is the exception - text is not per player, so there is
one switch that rewrites it in your own game only.

Linkle, Dark Link and the Hero of Time ship with the mod, so the tab is not empty on a fresh
install. Anyone can add more, and a folder of your own with the same name as a shipped one wins.

## Experimental

These are **off by default** - turn them on under **Unfinished** in the Co-op window's **Host**
tab. The host's settings are the ones used, so only the host needs to change them.

- **Story progress** - cutscenes and quest flags are shared. Can leave a quest stuck.
- **Enemy sync** - one set of enemies shared by everyone, instead of a copy each.
- **Boss sync** - only **Ook** and **Diababa** so far. Other bosses are left alone, so each of you
  fights your own copy. **Boss sync can softlock a fight** - if it does, turn it off and re-enter.
- **Hold boss fights** - the fight waits at the door until everyone has walked in. If someone
  never arrives, you wait.

## Known issues

- Everything under Experimental is unfinished. It is off by default and sits under **Unfinished** in the
  Host tab - turn it on knowing it can break a fight or a quest.
- The warp effect on other players is off (it could crash).

## Reporting bugs

Say what happened, where, and whose screen it was on. Logs are only written in dev mode for now.

## Building

**Every platform at once:** push to GitHub. `.github/workflows/build.yml` builds Windows (x64 and
ARM64), Linux (x86_64 and ARM64), macOS (Apple Silicon and Intel), iOS and Android, and packs them
all into one `crests_of_courage.dusk` - Dusklight loads the right one for the device. It is under
the run's artifacts; push a tag and it is attached to a release as well.

**Locally:** CMake and Ninja, plus a compiler for your platform (Visual Studio 2022 on Windows).
Dusklight's source is fetched automatically at the pinned version unless you keep a checkout at
`../dusklight`:

```
cmake -S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCOOP_PUBLIC_BUILD=ON
cmake --build build_release
python tools/package.py --bundle build_release/mods/coop_mod.dusk --models models --out crests_of_courage.dusk
```

Leaving `DUSK_GAME_EXE` unset links against Dusklight's version-independent stub, which is what a
release should use. Setting it ties the build to one exact Dusklight build (fine for local testing).

## Credits

- **Fimmel** - the original puppet and model-loading code the other players' models are built on.
- The Dusklight team, for the port and the mod SDK.

Models that ship with the mod, used with their authors' permission:

- **Linkle** - Ditrey. Concept art by ThenMichael, modelling help from SkilarBabcock.
- **Dark Link** - LoadingError.
- **Hero of Time** - SkilarBabcock.

## License

MIT for the code in `src/` - see [LICENSE](LICENSE).

The models in `models/` are not covered by it: they belong to the people in
[CREDITS.md](CREDITS.md) and are here with their permission. Forking this repo does not carry that
permission with it.
