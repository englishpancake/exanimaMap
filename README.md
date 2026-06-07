# exanimaMap

A minimap overlay for [Exanima](https://www.baremettle.com/) that draws your explored path on top of the level map as you play.

Based on [MapExanimaC](https://github.com/staniBosch/MapExanimaC) by staniBosch, requested permission.

![Demo](https://media.giphy.com/media/lmtwnKIx08nTBjMMZB/giphy.gif)

---

## Features

- Transparent overlay that sits on top of the game window
- Paints an exploration trail as you move through each level
- Trail is saved per level and restored on next launch (`routes/` folder)
- Quick save/load backup of game saves (F5/F6) - will store backups in a folder 'backUP' next to your save files
- Configurable brush size, colour, and window opacity
- Corner minimap and fullscreen map modes
- Option to move map to follow the direction your camera is aiming at

## Requirements

- Windows 10 or 11
- Exanima running in **windowed mode**
- No additional installs — the exe is self-contained

## Setup

Grab a release from releases - on your right side.

1. Extract the zip so the folder looks like this:
   ```
   exanimaMap.exe
   assets/
     config.ini
     ... (map images and icons)
   ```
2. Launch Exanima in windowed mode
3. Launch `exanimaMap.exe` — it will wait up to 5 minutes for the game to appear

## Controls

| Input | Action |
|---|---|
| Left click + drag | Move the minimap window |
| Double click / M| Toggle between corner minimap and fullscreen map |
| Right click | Toggle mini mode (150px / 300px) in corner mode |
| Scroll wheel | Zoom in / out |
| F5 | Quick backup game saves (prompts for confirmation) |
| F6 | Load backup saves (prompts for confirmation) |
| F8 | Pause / resume exploration trail drawing |
| F9 | Close the map overlay |

Keyboard shortcuts only fire when Exanima or the map overlay is the focused window.

## Configuration

All settings live in `assets/config.ini`.

### App settings

| Key | Default | Description |
|---|---|---|
| `full_window_screen` | `1` | Stretch Exanima to fill the monitor on launch |
| `pathToExanimaSaves` | *(auto)* | Path to your Exanima saves folder. Defaults to `%APPDATA%\Exanima` if not set |
| `quickSave` | `1` | Enable F5/F6 backup shortcuts |
| `brush_enabled` | `1` | Whether the exploration trail starts enabled (`0` = paused at launch) |
| `brush_radius` | `2` | Trail brush size in pixels (`1` = hair-thin, `2` = fine, `5` = medium, `18` = thick) |
| `brush_color` | `AA1111` | Trail colour as hex RGB (e.g. `AA1111` = dark red, `26E526` = green, `000000` = black) |
| `opacity` | `60` | Map opacity, 0–100 |
| `cursor_color` | `default` | Player icon colour as hex RGB, default is the metallic Exanima menu cursor.
| `rotate_map` | `0` | Whether the map is static or rotates to view centering on where the player camera is looking.

### Memory addresses (auto-located)

Idea suggested by Yew who provided an example - https://github.com/ALEHACKsp/memory

The overlay reads your position, level and camera rotation from Exanima's memory.
**You no longer configure these** — they are located automatically at startup by
**AOB (array-of-bytes) scanning**, so the overlay keeps working across game
updates without any manual address-finding. There is no `[MemoryAddresses]`
section in `config.ini` anymore.

How it works: instead of hardcoding the data addresses (which shift on every game
update — the recurring cause of "the player dot froze"), the overlay scans
`Exanima.exe` for the stable **instructions** that reference those values and
reads the address out of the instruction at runtime. Code patterns survive
updates even when the data moves, so the mod shouldn't break every update.

- **Player X / Y** — found via the instruction that writes the position
  (`mov rdx,[rax+0xAA0]; mov [X],rdx`). Y is the float 8 bytes after X.
- **Level** — found via a unique level-read instruction.
- **Camera rotation X / Y** — derived from the player-X address by a fixed
  in-section delta (`rotY = X − 0x1FE0`, `rotX = X − 0x1F40`), a relationship that
  survives the section shifts that come every game update.

## Exploration trail

Explored paths are saved automatically every 30 seconds and on level change or close. Files are stored in `routes/` next to the exe:

```
routes/
  explored_lvl2.dat   ← Level 1 (Underground)
  explored_lvl3.dat   ← Level 2
  ...
```

Delete a `.dat` file to reset that level's trail.

## Building & Releasing

This repo includes a GitHub Actions workflow that automatically builds and publishes a release when you push a version tag.

This is mostly if you want to update and build it yourself without faffing about with build tools; just fork the repo and away you go.

**To publish a release via GitHub Actions**

1. Make sure your repo has Actions enabled and workflow permissions set to *Read and write* (`Settings → Actions → General → Workflow permissions`)
2. Push a tag:
   ```
   git tag v1.0.0
   git push origin v1.0.0
   ```
3. GitHub Actions will build the exe, package it with `assets/` into a zip, and create a GitHub Release automatically

The release zip extracts to:
```
exanimaMap.exe
assets/
  config.ini
  ... (map images and icons)
```

## Credits

- Original project: [MapExanimaC](https://github.com/staniBosch/MapExanimaC) by staniBosch
- The community of "Council of Conservers"
- Yew for every map used in assets/ - generated using their map generator
- Jango for his Exanima RFC Editor - Tile Node Terrain Props, and especially Jango's Heresy Manual V2

## TODO

Add more detailed maps - You can technically modify the maps freely without any problems; as long as you don't change their size.

Figure out if there's a niceway to detect save, to hold a seperate routes/exploration trail for each save.
