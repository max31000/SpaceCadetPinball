# Web (Emscripten) build

The web build is an ES module factory (`createPinball`) plus a `.wasm` file. It contains no game
data: the host page supplies the original files. All game UI that upstream draws with ImGui
(menu bar, high score table / name entry, controls, about, font picker, exit prompt) is hidden;
the host page draws those as DOM dialogs, using the exported functions and events below.

## Build

Requires emsdk (tested with **6.0.10**), CMake and Ninja.

```sh
source /path/to/emsdk/emsdk_env.sh
# Release (what CI publishes): no data
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
# -> build-web/bin/SpaceCadetPinball.{js,wasm}

# Dev only: package a local data directory into /data (adds SpaceCadetPinball.data)
emcmake cmake -S . -B build-web-dev -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPINBALL_WEB_PRELOAD_DIR=/path/to/pinball-data
cmake --build build-web-dev
```

Link flags of note: `-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPinball`,
`-sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS=ogg`, `-sALLOW_MEMORY_GROWTH=1`, IDBFS,
no pthreads (no COOP/COEP requirement, works in an iframe). `--pre-js Platform/Web/pre.js`,
`--js-library Platform/Web/library_bliss.js`.

Release: `.github/workflows/web-release.yml` builds in `emscripten/emsdk:6.0.10` on every push /
pull request (check + workflow artifact) and, for a tag `web-vX.Y.Z`, attaches
`pinball-web-X.Y.Z.tgz` (+ `.sha256`) to the GitHub release of that tag. Archive root:
`SpaceCadetPinball.js`, `SpaceCadetPinball.wasm`, `LICENSE`, `README.md`.

## Host contract

```js
import createPinball from './SpaceCadetPinball.js';

const Module = await createPinball({
  canvas,                                   // HTMLCanvasElement (any id)
  locateFile: (name) => urlOf[name],        // 'SpaceCadetPinball.wasm' (and .data in dev builds)
  blissDataDir: '/data',                    // default '/data'; becomes the game's working dir
  preRun: [(M) => {                         // /data already exists here
    M.addRunDependency('data');             // optional: let downloads overlap the wasm download
    filesPromise.then((files) => {
      for (const [name, bytes] of files) M.FS.writeFile(`/data/${name}`, bytes);
      M.removeRunDependency('data');
    });
  }],
  onBlissEvent(type, json) { /* see Events */ },
  // blissUps: 120,                         // optional: pin UPS instead of measuring (P4)
});
// The promise resolves before main() has initialized the game: call bliss_* functions after
// the "started" event (before that they are no-ops returning 0).
```

Files in `/data` (names are matched case-insensitively): `PINBALL.DAT`, `SOUND*.WAV`, a TTF
font with Cyrillic glyphs named `tahoma.ttf` (option `FontFileName`; used for the texts that the
bitmap font cannot show), optional `PINBALL.OGG` (music; MIDI is not supported on the web).
`WAVEMIX.INF`, `TABLE.BMP`, `FONT.DAT` are not needed. Music can be loaded lazily: write
`PINBALL.OGG` into `/data` any time after start and call `bliss_reload_music()`; the music
option is kept even while the file is missing.

### Canvas

The canvas backing store is the table size of the original **640×480 mode: 600×416** (as the
window client area of the XP game, without the menu bar). The game never follows the page
size (no `SDL_WINDOW_RESIZABLE`); the host sets the displayed size with CSS (integer scale:
`image-rendering: pixelated`; fractional: smooth). Mouse coordinates are mapped through the
CSS size. `pre.js` gives the canvas `tabindex=0` if it has none, focuses it on pointerdown and
suppresses its context menu (right button = right flipper).

Keyboard input is bound to the canvas (`SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT="#canvas"`): the
game sees keys only while the canvas has focus, so DOM dialogs work normally. Focus the canvas
when the game page opens and after closing host dialogs/menus. Game keys (F2 new game, F3
pause, Z / `/` flippers, Space plunger, X / `.` / Up bumps) are prevented from scrolling the
page while the canvas is focused; F4 (full screen), F8 (controls), F1 are not handled by the
web build — listen for them on the canvas in the host if needed. `document.title` is never
changed by the game.

### Exported functions

Call as `Module._name(...)` (numbers) or `Module.ccall(name, ret, argTypes, args)` (strings).
Strings are returned as pointers to a static buffer: read with `Module.UTF8ToString(ptr)`
right away.

| Function | Signature | Notes |
|---|---|---|
| `bliss_new_game` | `void()` | F2 |
| `bliss_launch_ball` | `int()` | Menu "Запуск шарика"; 0 when the original item is disabled (`launchBallEnabled`) |
| `bliss_demo_toggle` | `int()` | Menu "Демонстрация"; returns 1 if demo mode is active afterwards (`demoActive`) |
| `bliss_toggle_pause` | `int()` | F3; returns 1 when paused afterwards |
| `bliss_pause` | `void()` | Pause without toggling (host menu open, tab hidden) |
| `bliss_is_paused` | `int()` | |
| `bliss_toggle_sound` | `int()` | Returns the new state |
| `bliss_toggle_music` | `int()` | Returns the new option state |
| `bliss_reload_music` | `int()` | After a late `FS.writeFile('/data/PINBALL.OGG', …)`: loads the track and starts it if music is on; 1 if the track is available |
| `bliss_get_options` | `char*()` | JSON, shape of the `optionsChanged` event plus `paused` |
| `bliss_set_option` | `int(char* name, int value)` | See below; returns 0 for an unknown name |
| `bliss_get_high_scores` | `char*()` | JSON `[{name, score}] ×5` (empty slot: `score: -999`) |
| `bliss_submit_high_score_name` | `int(char* utf8)` | Answer to `highScoreEntry`; control characters (incl. CR/LF) become spaces, then truncated to 31 bytes on a UTF-8 boundary; `""` stores the default name from the event (what the original dialog pre-fills, e.g. "Player 1") — use this for a cancelled DOM dialog; returns the 1-based rank, 0 if nothing was pending |
| `bliss_get_pending_high_score` | `char*()` | Pending name entry as JSON (payload of `highScoreEntry`) or `null` — for a remounted host page |
| `bliss_reemit_pending_high_score` | `int()` | Emit `highScoreEntry` again for the pending entry; 1 if one is pending |
| `bliss_cancel_high_score` | `int()` | Original "Отмена" semantics: dismiss the pending entry without storing the score |
| `bliss_clear_high_scores` | `void()` | "Очистить" of the original dialog |
| `bliss_set_language` | `int(char* code)` | `ru_xp` (default), `ru`, `en`, …; restarts the game instance (current game is lost); 0 for an unknown code |
| `bliss_set_ups` | `int(int)` | Simulation steps per second, clamped to 60..360; returns the applied value |
| `bliss_persist` | `void()` | Flush settings + high scores to IndexedDB now (also done on `pagehide`) |
| `bliss_set_master_volume` | `int(int percent)` | Host master volume 0–100 (clamped, default 100): the mixer plays sounds at `soundVolume × percent / 100` and music at `musicVolume × percent / 100`; the in-game options (`sounds`, `music`, `soundVolume`, `musicVolume`) are unchanged and keep working. Callable before `started` (also from `preRun`), survives `bliss_set_language`, not persisted (the host owns it). Returns the applied value |
| `bliss_get_master_volume` | `int()` | Current master volume 0–100 |

`bliss_set_option` names: `sounds`, `music` (0/1), `players` (1–4), `soundVolume`,
`musicVolume` (0–128), `control.<leftFlipper|rightFlipper|plunger|leftTableBump|rightTableBump|bottomTableBump>`
(SDL keycode of the keyboard binding, 0 clears it), `controlsDefault` (restore default
controls). `Module.blissKeycodeFromEvent(keyboardEvent)` converts a DOM key event into the SDL
keycode the game uses (by `keyCode`, layout independent — like SDL's own mapping).

Other `Module` members: `blissStarted` (bool), `blissRefreshHz` (measured), `blissUpsApplied`,
`blissSyncfs()` (serialized IndexedDB flush), `blissSyncCount`, `blissSnapRefreshRate(hz)`.

### Events (`onBlissEvent(type, json)`)

| Type | Payload |
|---|---|
| `started` | `{version, width, height}` — first frame rendered; canvas size |
| `gameOver` | `{score, scores: [per player], players, demo, cheats}` — `score` is the best score |
| `highScoreEntry` | `{score, position (0-based), rank (1-based), name (default, e.g. "Player 1"), pending}` — show the name dialog, answer with `bliss_submit_high_score_name` / `bliss_cancel_high_score`; with several players the next entry follows after the answer |
| `pauseChanged` | `{paused}` — from F3, the exports, or game logic |
| `optionsChanged` | `{sounds, music, players, soundVolume, musicVolume, language, ups, highScoresEnabled, launchBallEnabled, demoActive, controls: {leftFlipper: {key, name}, …}}` — after exports and keyboard toggles (checked every 15 frames) |

Exceptions thrown by the handler are caught and logged.

## Frame pacing (decision P4)

The browser drives the loop: `emscripten_set_main_loop_arg(WebFrame, nullptr, 0 /*rAF*/, 1)`.
UPS is set to the **display refresh rate**: `pre.js` takes the median of 60
`requestAnimationFrame` intervals (restarting while the tab is hidden; repeated when the tab
becomes visible, when the device pixel ratio changes, e.g. the window moved to another
monitor, and every 60 s — UPS is re-applied only when the snapped value changes), snaps it to the nearest
of {60, 72, 75, 90, 100, 120, 144, 165, 240} (implausible values → fallback 120) and calls
`bliss_set_ups` once the game has started. Each frame renders once and advances the game in fixed steps of `1000/UPS` ms with
hysteresis: a frame 0.5–1.5 steps long runs exactly one step while the accumulated drift stays
within (−1, 2) steps, so with UPS = refresh rate there is one step per frame without
interpolation and rAF jitter never produces 0/2-step frames; otherwise the accumulator is
rounded (long frames, refresh ≠ UPS), which keeps real-time game speed.
Catch-up is limited to 100 ms. The host can override with `blissUps` or `bliss_set_ups()`.
Before the measurement completes the game runs at the default 120 UPS.

## Language (decision P3)

Default language is `ru_xp`: the strings of the **original Russian Windows XP `pinball.exe`**
(string table, main menu, high score and key mapper dialogs), generated by
`tools/gen_ru_xp.py <pinball.exe>` into `SpaceCadetPinball/translations_ru_xp.inc` (committed;
short UI strings only). As in the Russian XP release, in-game messages stay English
(e.g. "Game Paused / F3 to Resume") and only a few texts are Russian (window title, "Игра
окончена", dialogs, menus). Ids missing from the exe fall back to English. The community
translation stays available as `ru`. (`ru_xp` also appears in the native Language menu.)

Deviation from the original (web build only; native keeps upstream behaviour): XP draws the info box with a bitmap font that only has ASCII
glyphs (bytes are masked with 0x7F, so the Russian "Игра окончена" came out garbled). The
port draws messages containing non-ASCII characters with the TTF font instead
(`TTextBox::UseBitmapFont`); ASCII messages keep the original bitmap font.

## Persistence

IDBFS is mounted at `/libsdl` (SDL pref path `/libsdl/SpaceCadetPinball/`) and loaded before
`main()`. Options, controls and high scores live in the ImGui `.ini` there. The game checks
about once per second (and after every export that changes options / high scores) whether the
`.ini` content changed, writes it and calls `Module.blissSyncfs()`. The flush on `pagehide`
is best-effort: `FS.syncfs` is asynchronous and the browser may tear the page down before
IndexedDB commits, so changes from the last second before closing can be lost (high score
submits and option exports flush immediately).

## Other differences from the desktop build

- No ImGui menu bar / "Menu" button; Exit, full screen (F4), menu toggle and controls dialog
  (F8) key bindings are ignored (the host page owns these, e.g. CSS pseudo-fullscreen). Resolution is fixed to the 640×480 mode.
- Defaults: `Language=ru_xp`, `FontFileName=tahoma.ttf`.
- Music: `PINBALL.OGG` instead of `PINBALL.MID` (no MIDI synth in the browser). No OGG → no music.
- The SDL2 `AudioContext` is resumed on the first keydown/pointerdown/touchstart.

## Dev host page and end-to-end test

`Platform/Web/host-test.html` does exactly what the production host does (fetch the files,
`createPinball`, write them in `preRun`, DOM high score dialog). `Platform/Web/host_test.py`
(Playwright, muted browsers) checks start, events, frame pacing over an auto-play session,
high score submit + reload persistence, options persistence and language switching:

```sh
python -m http.server 8766 --bind 127.0.0.1 --directory <dir containing the repo and data>
python Platform/Web/host_test.py --browser chromium --seconds 120 --data /pinball-data/ --shots out/
```
