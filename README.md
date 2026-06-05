# Bottle NPCs

A native [Dusklight](https://github.com/TwilitRealm/dusklight) mod that lets you
**bottle an NPC or enemy and release it later**, in the spirit of catching bugs
or fairies.

Bottles in the game store fixed content *types*, not arbitrary actors, so this
mod keeps its own stack of captured actors rather than touching the bottle
inventory. It's a **detour mod**: Dusklight exports its symbols, so it hooks the
bottle-swing function at runtime (via [funchook](https://github.com/kubo/funchook))
and calls game code directly through the game's own headers.

## How to use

- **Bottle:** Z-target an NPC or enemy and swing a bottle — it's removed and
  stored.
- **Release:** swing a bottle with nothing targeted — the most recently bottled
  actor reappears in front of you (with its original profile and parameters; it
  reinitializes fresh, so it won't remember prior state).

> A bottled NPC is recreated from its profile + parameters, not its live state.

## Settings

Declared in code and shown in the **Mods** tab:

| Setting | Default | Description |
| --- | --- | --- |
| Release Distance | 80 | How far in front of you a bottled NPC is released. |

## Building & deploying

Builds against the Dusklight source tree (headers), a configured Dusklight build
dir (generated + fetched headers such as Tracy), and funchook. Paths live in
[`config.sh`](config.sh) and default to siblings under `~/Github`:

```
~/Github/
    dusklight/              built once, without LTO (e.g. linux-default-relwithdebinfo)
    funchook/               git clone https://github.com/kubo/funchook
    TwilightPrinces/        your Dusklight-related repos
        bottle_npcs/        (this mod -- a self-contained repo)
            config.sh       DUSKLIGHT_DIR / DUSKLIGHT_BUILD_DIR / FUNCHOOK_DIR / DEPLOY_DIR
            build.sh        build + deploy
            deploy.sh       install the built mod into DEPLOY_DIR
            src/            bottle_npcs.cpp + mod.json
            ...
```

Edit `config.sh` (or export the vars), then:

```sh
./build.sh
```

CMake compiles in `staging/` (its scratch tree) and assembles the ready-to-copy
folder at `build/bottle_npcs/` (the library + `mod.json`); `build.sh` then calls
`./deploy.sh`, which copies it into `DEPLOY_DIR/bottle_npcs/` (the game's `mods/`
folder). Both `build/` and `staging/` are git-ignored. Restart Dusklight; the mod
is enabled by default.

> Dusklight must be built **without LTO** (the non-`release` presets already are)
> so the functions this mod hooks remain real, addressable symbols. Rebuild +
> redeploy whenever Dusklight is rebuilt.

## Files

| File | Purpose |
| --- | --- |
| `src/bottle_npcs.cpp` | Mod source (detour + direct game calls). |
| `src/mod.json` | Metadata (id, name, version, author, library). |
| `CMakeLists.txt` | CMake build (links funchook, points at the Dusklight tree). |
| `config.sh` | Paths: Dusklight tree/build dir, funchook, and deploy target. |
| `build.sh` | Build + deploy (uses `config.sh`, `deploy.sh`). |
| `deploy.sh` | Copy the built mod into the game's `mods/` folder. |
