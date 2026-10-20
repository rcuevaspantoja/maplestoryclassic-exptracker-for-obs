# EXP Meter for OBS

An **OBS Studio** plugin built exclusively for **MapleStory Classic** streamers. It reads the EXP shown in the game's HUD and displays live stats on stream:

- **EXP per minute** (and per hour)
- **Session EXP**: how much EXP you have earned since you started measuring
- **Time to level up**: an estimate based on your current pace
- **Training-spot comparison**: measure a map, start a new measurement on another map, and see which one pays better

> Status: **Stage 2 – Offline HUD reading (in review).** The plugin loads in OBS, and the HUD reader works offline (CLI + tests). See [Roadmap](#roadmap).

---

## Principles

1. **It never touches the game.** The plugin doesn't read the game's memory, inject into its process, or send input to its window. It only looks at the image OBS is already capturing for the stream, which is the same thing your viewers see.
2. **It's simple for anyone.** You shouldn't need to know OBS filters or the game's internals. Pick your game source, click *Add overlay to scene*, and you're done.
3. **It's light.** Reading the HUD only processes a small strip of the frame a couple of times per second, never every frame, and never on OBS's graphics thread.
4. **It stays local.** Everything runs on your machine. The overlay is served only on `127.0.0.1`, and no data leaves your PC.

---

## How it works

![HUD reference](references/exp_bar.png)

MapleStory always draws its HUD in the same place, at the bottom of the window. That HUD shows:

```
LV. 39            EXP. 74,660 [28.00%]
```

The plugin:

1. Attaches an invisible **"EXP Meter" filter** to your game capture source. The filter doesn't change the image.
2. Finds the `EXP.` label in the bottom part of the frame, then reads the numbers next to it and the `LV.` value.
3. Feeds those readings into a **stats engine** that calculates rates, totals and ETA.
4. Publishes the results to:
   - an **overlay** (a Browser Source in your scene, which you can style with CSS),
   - a **dock panel** inside OBS (controls and comparison table),
   - optionally, **text files** for other tools.

---

## Features

### Live stats (overlay + dock)
| Stat | Description |
|---|---|
| EXP / min | Gain rate over a sliding window (default: last 5 minutes) |
| EXP / hour | The same rate, scaled to one hour |
| Session EXP | Net EXP earned during the current measurement |
| Session time | Active time of the current measurement (pauses automatically) |
| Level / % | Current level and progress, as shown in the HUD |
| Time to level | Estimated time until the next level at the current rate |

### Dock panel (inside OBS)
- **Game source** selector. It attaches the filter for you, so you never edit filters by hand.
- **Detection status**: `HUD detected ✓`, the last reading, and a confidence score, so you can see whether reading is working.
- **Start / Pause** buttons.
- **Reset** button (asks for confirmation).
- **New measurement** button, with an optional name, for comparing spots (see below).
- **Add overlay to scene**, which creates the Browser Source in the current scene automatically.
- **Settings**: averaging window, read interval, server port, text-file output.

### Overlay (Browser Source + CSS)
- Served by the plugin at `http://127.0.0.1:48620/`. The port is configurable.
- Built-in themes are chosen with a query parameter: `?theme=minimal`, `?theme=maple`, `?theme=compact`.
- All elements have stable IDs and classes (`#exp-per-min`, `#session-exp`, `#eta`, `.stat`, `.stat-label`, `.stat-value`, …). You can restyle everything from the Browser Source's **Custom CSS** field in OBS. A commented example stylesheet will be included.
- Optional comparison view: `?show=compare`.

### Training-spot comparison
A common streamer question is *"Which map is better for my level?"* EXP Meter answers it like this:

1. Train at spot A for a few minutes.
2. Press **New measurement** (in the dock, or with a configurable OBS **hotkey** so you don't have to leave the game) and optionally name it, e.g. `Ant Tunnel 2`.
3. Move to spot B and train.
4. The dock shows a **comparison table** of past measurements and highlights the best EXP/min.

Each saved measurement stores:
- its name,
- duration,
- total EXP,
- average EXP/min,
- and the level when it was taken.

The history persists across OBS restarts. You can delete single entries or clear all of it.

---

## Metrics glossary

| Term | Definition |
|---|---|
| **Reading** | One accepted capture of `(time, exp, percent, level)` from the HUD. A reading is accepted only if:<br>• its confidence is above the threshold,<br>• it is plausible compared with the previous reading,<br>• and the same value was read twice in a row. |
| **Measurement** (session) | The time span between Start/Reset/New measurement and now. All "session" stats refer to the current measurement. |
| **Session EXP** | Sum of the EXP gained during the measurement, **minus** any EXP lost (for example, on death). |
| **EXP / min** | EXP gained in the averaging window ÷ active minutes in that window. The session average (session EXP ÷ session minutes) is also available. |
| **Required EXP** | Total EXP needed for the current level. It comes from the built-in MapleStory Classic EXP table. If the level is unknown, it is estimated as `exp ÷ percent`. |
| **Time to level (ETA)** | `(required EXP − current EXP) ÷ EXP per minute`. Shown as `—` until the rate is stable. |
| **Level-up** | Detected when the level increases, or when the percent wraps from high to low. The EXP crossing the level boundary is counted correctly: `(required_prev − exp_prev) + exp_new`. |
| **EXP loss** | An EXP drop without a level change, for example on death. It is subtracted from session EXP and logged. |
| **Auto-pause** | If the HUD isn't visible for a few seconds (alt-tab, cash shop, loading screens), the session clock stops until it reappears. |

---

## Architecture

```
Game Capture ──("EXP Meter" filter)──► HUD reader ──► Stats engine ──► Shared state
                                       (locate + OCR)  (EXP/min, ETA,      │
                                                        session, history)  │
                                                   ┌───────────────────────┼──────────────┐
                                             Local HTTP server         Qt dock      (opt.) .txt
                                            (overlay + JSON/SSE)
```

| Component | Path | Responsibility |
|---|---|---|
| Plugin entry | `src/plugin-main.cpp` | Registers the filter, dock and hotkeys, and starts the server |
| Frame capture | `src/exp-filter.cpp` | Video filter. It passes the image through untouched. About every 500 ms it renders only the HUD region and copies it to the CPU asynchronously (`gs_texrender` → `gs_stage_texture` → `gs_stagesurface_map`), then hands it to a worker thread |
| HUD locator | `src/hud/locator.*` | Finds the `EXP.` label template in the bottom of the frame, trying multiple scales, and caches its position |
| Glyph OCR | `src/hud/glyph_ocr.*`, `src/hud/glyphs.hpp` | Matches each character against the HUD pixel font, which is embedded in the code as bitmaps extracted from native captures. There is one set for the EXP text and one for the level digits in the orange boxes |
| Parser | `src/hud/parser.*` | Turns `74.660 [28.00%]` into `exp = 74660, pct = 28.00`. A `.` or `,` before `[` is a thousands separator |
| Stats engine | `src/stats/` | Pure C++ with no OBS dependency, so it is unit-testable. Handles rates, totals, ETA, level-ups, losses, auto-pause and measurement history |
| Local server | `src/server/` | cpp-httplib bound to `127.0.0.1`. Routes: `/` (overlay), `/api/state` (JSON), `/events` (SSE), `POST /api/reset` |
| Dock | `src/ui/exp-dock.*` | Qt6 panel: source selector, status, stats, controls, comparison table |
| Assets | `data/` | `exp_table.json`, `overlay/` (HTML/CSS/JS), `locale/` (`en-US.ini`, `es-ES.ini`) |
| Tests | `tests/` | doctest unit tests: OCR on reference images, the parser, and the stats engine with simulated sequences |

**Tech stack:** C++17, CMake, [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate), Qt6 (comes with OBS), cpp-httplib, stb_image, doctest. Windows x64 comes first.

---

## Roadmap

Development is incremental. Each stage ends with something you can check before moving on.

| Stage | Deliverable | How to check | Status |
|---|---|---|---|
| **0. Definition** | This README | Review and approval | ✅ Done |
| **1. Skeleton** | Template-based project that builds and loads in OBS; locales; CI | OBS log shows `EXP Meter loaded` | ✅ Done |
| **2. Offline HUD reading** | Locator + OCR + parser library with tests; `expmeter-cli image.png` | `ctest` passes; the CLI prints `exp=74660 pct=28.00 lv=39` for the reference image | 🟡 In review |
| **3. OBS filter** | Filter captures the HUD region and logs readings | Correct readings in the OBS log, from the reference image or the live game | ⚪ |
| **4. Stats engine** | Rates, ETA, level-up, loss, auto-pause, with tests | Simulated tests pass; live values look sane | ⚪ |
| **5. Dock** | Source selector, status, stats, Start/Pause/Reset | Works without adding filters by hand | ⚪ |
| **6. Overlay** | Local server, themes, *Add overlay to scene*, custom CSS | Overlay visible in the scene and restyled by CSS | ⚪ |
| **7. Spot comparison** | Named measurements, history, comparison table, hotkey | Measure two maps and compare them in the dock | ⚪ |
| **8. Distribution** | Windows installer, session persistence, optional `.txt` output, user guide | Clean install, then repeat the checks from stages 3–7 | ⚪ |

---

## Requirements

- **OBS Studio** 30 or newer (Windows x64)
- MapleStory Classic captured with a **Game Capture** or **Window Capture** source, with the HUD visible
- For building:
  - Visual Studio 2022 or 2026 (or Build Tools) with the "Desktop development with C++" workload
  - CMake 3.28 or newer
  - Git

---

## Building (Windows)

From a *Developer PowerShell* for your Visual Studio version, in the repository root. Use the preset that matches your Visual Studio:

| Visual Studio | Preset |
|---|---|
| 2022 (17.x) | `windows-x64` (also used by CI) |
| 2026 (18.x) | `windows-x64-vs2026` |

```powershell
cmake --preset windows-x64-vs2026            # first run downloads and builds OBS sources, obs-deps and Qt6 (~1 GB)
cmake --build --preset windows-x64-vs2026    # output: build_x64/RelWithDebInfo/exp-meter-for-obs.dll
```

To try it, install it into the per-machine OBS plugin folder. OBS 30+ loads plugins from there, and it doesn't need admin rights:

```powershell
cmake --install build_x64 --config RelWithDebInfo --prefix "C:/ProgramData/obs-studio/plugins"
```

Then start OBS and open **Help → Log Files → View Current Log**. Look for:

```
[exp-meter-for-obs] EXP Meter loaded (version 0.1.0)
```

### HUD reader CLI and tests

The build also produces a command-line tool and the unit tests. Neither needs OBS:

```powershell
build_x64\RelWithDebInfo\expmeter-cli.exe references\game-capture.png
#   exp=74660 pct=28.00 lv=39
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

The CLI accepts any PNG or BMP screenshot. It is the quickest way to check whether a capture can be read.

CI (GitHub Actions) builds the Windows plugin on every push and uploads it as a build artifact.

---

## Contributing HUD samples

The OCR compares each character against the HUD's own pixel font, so it needs one clean, **native-scale** example of every character. Add PNG screenshots to `references/samples/`.

Current coverage:

| Font | Have (native) | Missing |
|---|---|---|
| EXP digits and symbols (`0-9 , . [ ] %`) | all except 9. The 9 is the 6 rotated 180°, which holds for every other symmetric pair in this font | a native **9** to confirm it |
| Level digits (white on orange boxes) | 1, 3, 9 | **0, 2, 4, 5, 6, 7, 8**. Until a digit has a template, a level containing it is reported as unknown, never guessed |

Also useful:

- The HUD at other game resolutions
- Captures around a level-up and after a death

Take captures **at the game's native size**, for example with OBS's *Screenshot (Source)* on the Game Capture. Use PNG, not JPG. Put the visible values in the filename, for example `exp_123456_45.67_lv52.png`. These files also become test fixtures.

---

## Repository layout (planned)

```
.
├── CMakeLists.txt / CMakePresets.json / buildspec.json
├── src/
│   ├── plugin-main.cpp
│   ├── exp-filter.cpp
│   ├── hud/        (locator, glyph_ocr, glyphs, parser, hud_reader)
│   ├── stats/      (session, history)
│   ├── server/     (http server)
│   └── ui/         (exp-dock)
├── data/
│   ├── exp_table.json  overlay/  locale/
├── tests/
├── tools/          (expmeter-cli, image loading)
├── third_party/    (stb_image, doctest)
└── references/     (game captures, samples, original spec)
```

## License
TBD. obs-plugintemplate-based plugins are usually GPL-2.0, to match OBS.
