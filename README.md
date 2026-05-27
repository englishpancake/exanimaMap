# exanimaMap

A minimap overlay for [Exanima](https://www.exanima.com/) that draws your explored path on top of the level map as you play.

Based on [MapExanimaC](https://github.com/staniBosch/MapExanimaC) by staniBosch, requested permission.

---

## Features

- Transparent overlay that sits on top of the game window
- Paints an exploration trail as you move through each level
- Trail is saved per level and restored on next launch (`routes/` folder)
- Quick save/load backup of game saves (F5/F6)
- Configurable brush size, colour, and window opacity
- Corner minimap and fullscreen map modes

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
| Double click | Toggle between corner minimap and fullscreen map |
| Right click | Toggle mini mode (150px / 300px) in corner mode |
| Scroll wheel | Zoom in / out |
| F5 | Quick backup game saves (prompts for confirmation) |
| F6 | Load backup saves (prompts for confirmation) |
| F8 | Pause / resume exploration trail drawing |

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

### Memory addresses

The overlay reads your position from Exanima's memory. The addresses are stored in `[MemoryAddresses]` and are calibrated for a specific game version. **After a game update these may stop working** — the player dot will freeze but the overlay will still open. Updated addresses will need to be found with a tool like Cheat Engine and written into `config.ini`.

```ini
[MemoryAddresses]
offset_x_ptr = 0x4DEDE0
offset_y_ptr = 0x48BCB8
offset_lvl_ptr = 0x2DA030
```

These are offsets relative to the `Exanima.exe` module base, not absolute addresses.

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
