# Crests of Courage

Online co-op for Twilight Princess, as a mod for [Dusklight](https://github.com/TwilitRealm/dusklight).
2-4 players, one shared world.

**Early release.** Enemy sync and boss sync are experimental - read the warnings below.

## Requirements

- Windows. Linux, Mac and Android aren't built yet.
- Everyone needs the same version of this mod. Different versions won't connect.

## Install

Drop `coop_mod.dusk` into Dusklight's `mods` folder. A **Co-op** tab shows up in the in-game menu bar.

## Connecting

One player hosts, everyone else joins.

- **Host:** press **Host** (default port **27716**).
- **Join:** type the host's address and press **Join**.

The first time you host, Windows asks to let Dusklight through the firewall - allow it.
The host needs port 27716 open for **TCP and UDP**. If you can't open ports, everyone can install
[Tailscale](https://tailscale.com) (free) and join with the host's Tailscale address instead.

**Joining uses the host's save.** Your own progress is backed up first - disconnect, load your save
and press **Restore latest backup** (or **Restore oldest backup** for the very first one) to get it back.

## What syncs

- Each other: animations, clothes, colors, items in hand, wolf form, Midna, the lantern and its light
- Name tags (they stick to the screen edge when someone's off-screen, and go red at low health)
- Everyone's hearts under your own, and each player's health at their feet
- Sounds and attack effects
- Items and dungeon progress (story progress is optional)
- Pots, grass, flowers, pushed blocks and other world objects
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

These are **off by default** - turn them on in Settings. The host's settings are the ones used,
so only the host needs to change them.

- **Enemy sync** - enemies fight both of you.
- **Boss sync** - only **Ook** and **Diababa** so far. Other bosses are left alone, so each of you
  fights your own copy. **Boss sync can softlock a fight** - if it does, turn it off and re-enter.

## Known issues

- One Epona each, enemy sync and boss sync are unfinished. They are off by default and sit under
  a heading in Settings that says so - turn them on knowing they can break a fight.
- The warp effect on other players is off (it could crash).

## Reporting bugs

Say what happened, where, and whose screen it was on. Logs are only written in dev mode for now.

## Building

Visual Studio 2022, CMake, Ninja, and the Dusklight source at `../dusklight`. From an
x64 Native Tools prompt:

```
cmake -S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCOOP_PUBLIC_BUILD=ON
cmake --build build_release
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

MIT - see [LICENSE](LICENSE).
