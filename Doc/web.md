# Web (Emscripten) build

## Build

Requires emsdk (tested with **6.0.10**), CMake and Ninja.

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPINBALL_WEB_DATA_DIR=/path/to/pinball-data
cmake --build build-web
# -> bin/SpaceCadetPinball.{html,js,wasm,data}
```

`PINBALL_WEB_DATA_DIR` is preloaded into the MEMFS root (`--preload-file dir@/`). Expected
content: `PINBALL.DAT`, `SOUND*.WAV`, `tahoma.ttf` (Cyrillic font for ImGui text), optional
`PINBALL.OGG` (music). Game data is not part of this repository.

## Differences from the desktop build

- Main loop: `emscripten_set_main_loop_arg(WebFrame, nullptr, 0, 1)` (requestAnimationFrame).
  Each callback advances the game in fixed `1000/UPS` ms steps from an accumulator of real
  elapsed time (max 100 ms catch-up), then renders exactly one frame. UPS default stays 120;
  the FPS option is ignored (render rate = display refresh rate).
- No ImGui menu bar and no transparent "Menu" button; Exit / full screen / menu toggle key
  bindings are ignored (the host page owns these). The high score name-entry dialog,
  "about" and control dialogs are still ImGui.
- Defaults: `Language=ru`, `FontFileName=tahoma.ttf`.
- Music: `PINBALL.OGG` (case-insensitive `.OGG`/`.ogg`) instead of `PINBALL.MID`. No OGG -> no music.
- Persistence: IDBFS mounted at `/libsdl` (SDL pref path is `/libsdl/SpaceCadetPinball/`),
  loaded with `FS.syncfs(true)` before `main()` (`Platform/Web/pre.js`). The game checks
  about once per second whether the settings `.ini` (options + high scores) changed and then
  writes it and calls `Module.blissSyncfs()` (serialized `FS.syncfs(false)`).
- The SDL2 `AudioContext` (`Module.SDL2.audioContext`) is resumed on the first
  keydown/pointerdown/touchstart.

## Exported functions

Call after `Module.blissStarted` is true (set in `postRun`). Strings are returned as pointers
to a static buffer; read them with `Module.UTF8ToString(ptr)` immediately.

| Function | Signature | Notes |
|---|---|---|
| `_bliss_new_game` | `void()` | Same as F2 |
| `_bliss_toggle_pause` | `int()` | Same as F3; returns 1 when paused afterwards |
| `_bliss_pause` | `void()` | Pause without toggling (host menu open, tab hidden) |
| `_bliss_is_paused` | `int()` | |
| `_bliss_toggle_sound` | `int()` | Returns new state |
| `_bliss_toggle_music` | `int()` | Returns new state |
| `_bliss_get_options` | `char*()` | JSON `{sounds, music, players, soundVolume, musicVolume, language, ups, fps, paused, highScoresEnabled}` |
| `_bliss_set_option` | `int(char* name, int value)` | `sounds`, `music` (0/1), `players` (1-4), `soundVolume`, `musicVolume` (0-128); returns 0 for unknown name |
| `_bliss_get_high_scores` | `char*()` | JSON `[{name, score}] x5` (empty slot: score -999) |
| `_bliss_persist` | `void()` | Flush settings to IndexedDB now (also called on `pagehide`) |

Example: `Module.ccall('bliss_set_option', 'number', ['string', 'number'], ['players', 2])`.
