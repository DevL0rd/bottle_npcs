# Bottle NPCs

A [Dusklight](https://github.com/TwilitRealm/dusklight) code mod that lets you
**bottle almost any actor and dump it back out later**, in the spirit of catching
bugs or fairies.

Bottles in the game store fixed content *types*, not arbitrary actors, so this
mod keeps the captured actor itself and uses a Red Chu bottle purely as the
"occupied" indicator.

## How to use

- **Capture:** swing an empty bottle while Z-targeting an in-range actor — it's
  stored and removed, and the bottle becomes a Red Chu bottle. Only one actor is
  held at a time.
- **Release:** use the (Red Chu) bottle — instead of drinking, the bottle is
  dumped out and the held actor reappears in front of you.

> A bottled actor is recreated from its profile + parameters, not its live state,
> so it reinitializes fresh.

## Settings

Declared in code and shown in the **Mods** tab:

| Setting | Default | Description |
| --- | --- | --- |
| Catch Range | 150 | Maximum distance to an actor to bottle it. |
| Release Distance | 80 | How far in front of you a bottled actor is dumped out. |

## How it works

Dusklight exports its symbols, so the mod hooks game functions through the
Dusklight hook API and calls game code directly via its headers — no game-side
support code. It post-hooks `daAlink_c::procBottleSwingInit` to capture and
replaces `daAlink_c::procBottleDrinkInit` to pour the actor back out instead of
drinking.

## Building

This is a code mod built with the Dusklight mod SDK. It needs a Dusklight source
checkout (for the game headers + the `add_dusk_mod()` packaging helper); by
default it expects `~/Github/dusklight`.

```sh
./build.sh
```

That packages `bottle_npcs.dusk` straight into the game's mods folder
(`~/.local/share/TwilitRealm/Dusklight/mods` by default — override with
`MODS_DIR`, and the Dusklight path with `DUSK_DIR`). The running game
**hot-reloads** the mod when the package changes. The mod is enabled by default;
its enabled state and settings are saved by the game under `mods/.config/`.

## Files

| File | Purpose |
| --- | --- |
| `src/bottle_npcs.cpp` | Mod source (`mod_init`/`mod_tick` + the bottle hooks). |
| `src/mod.json` | Metadata (id, name, version, author, description, has_code). |
| `CMakeLists.txt` | Declares the mod via the Dusklight SDK's `add_dusk_mod()`. |
| `build.sh` | Build + package into the game's mods folder. |
