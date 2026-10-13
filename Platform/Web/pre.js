// Emscripten --pre-js of the web build (MODULARIZE + EXPORT_ES6, factory `createPinball`).
// `Module` here is the object the host passed to createPinball({...}). Contract: Doc/web.md.
//
// - Data directory (`Module.blissDataDir`, default "/data"): created before the host's preRun
//   callbacks run, so they can FS.writeFile() the game files into it; it becomes the working
//   directory of the game (PINBALL.DAT, SOUND*.WAV, tahoma.ttf, optional PINBALL.OGG).
// - Persistence: IDBFS at /libsdl (parent of the SDL pref path /libsdl/SpaceCadetPinball/),
//   loaded before main(); Module.blissSyncfs() is the serialized MEMFS -> IndexedDB flush the
//   game calls after the settings .ini (options, high scores) changed.
// - Canvas: made focusable (keyboard input is bound to it), no context menu (right flipper is
//   the right mouse button). Its size is the table size; the host scales it with CSS.
// - P4 frame pacing: the display refresh rate is measured from requestAnimationFrame intervals
//   (median of the first 60) and snapped to {60,72,75,90,100,120,144,165,240}; once the game
//   has started, UPS is set to it (one simulation step per frame). Fallback 120.
//   `Module.blissUps = N` pins UPS instead; the host can call _bliss_set_ups() at any time.
// - Resumes the SDL2 AudioContext on the first user gesture (autoplay policy).
(function () {
  var dataDir = Module['blissDataDir'] || '/data';
  Module['blissDataDir'] = dataDir;

  var preRun = Module['preRun'] || [];
  if (typeof preRun === 'function') preRun = [preRun];
  preRun.unshift(function () {
    // SDL addresses the canvas as "#canvas" (window + keyboard element); map it to the host's
    // canvas element whatever its id is.
    if (typeof specialHTMLTargets !== 'undefined' && Module['canvas'])
      specialHTMLTargets['#canvas'] = Module['canvas'];
    FS.mkdirTree(dataDir);
    var mountPoint = '/libsdl';
    try { FS.mkdir(mountPoint); } catch (e) { /* exists */ }
    FS.mount(IDBFS, {}, mountPoint);
    addRunDependency('bliss-idbfs');
    FS.syncfs(true, function (err) {
      if (err) console.error('[bliss] IDBFS load failed', err);
      removeRunDependency('bliss-idbfs');
    });
  });
  // After the host (and the dev --preload-file package) put the files in place.
  preRun.push(function () { FS.chdir(dataDir); });
  Module['preRun'] = preRun;

  // ---- persistence ------------------------------------------------------------------------
  var inFlight = false, pending = false;
  Module['blissSyncCount'] = 0;
  Module['blissSyncfs'] = function () {
    if (inFlight) { pending = true; return; }
    inFlight = true;
    FS.syncfs(false, function (err) {
      inFlight = false;
      if (err) console.error('[bliss] IDBFS save failed', err);
      else Module['blissSyncCount']++;
      if (pending) { pending = false; Module['blissSyncfs'](); }
    });
  };

  // ---- frame pacing (P4) ------------------------------------------------------------------
  var UPS_STEPS = [60, 72, 75, 90, 100, 120, 144, 165, 240];
  var UPS_FALLBACK = 120;
  Module['blissSnapRefreshRate'] = function (hz) {
    if (!(hz >= 50 && hz <= 400)) return UPS_FALLBACK;
    var best = UPS_STEPS[0];
    for (var i = 1; i < UPS_STEPS.length; i++)
      if (Math.abs(UPS_STEPS[i] - hz) < Math.abs(best - hz)) best = UPS_STEPS[i];
    return best;
  };
  var measuredUps = 0, started = false;
  function applyUps() {
    if (!started) return;
    var pinned = Module['blissUps'];
    var ups = pinned > 0 ? pinned : measuredUps;
    if (ups > 0 && Module['_bliss_set_ups']) Module['blissUpsApplied'] = Module['_bliss_set_ups'](ups);
  }
  if (typeof requestAnimationFrame === 'function' && !(Module['blissUps'] > 0)) {
    var stamps = [];
    var tick = function (t) {
      if (typeof document !== 'undefined' && document.hidden) stamps = [];
      else stamps.push(t);
      if (stamps.length <= 60) { requestAnimationFrame(tick); return; }
      var d = [];
      for (var i = 1; i < stamps.length; i++) d.push(stamps[i] - stamps[i - 1]);
      d.sort(function (a, b) { return a - b; });
      var median = d[d.length >> 1];
      Module['blissRefreshHz'] = median > 0 ? 1000 / median : 0;
      measuredUps = Module['blissSnapRefreshRate'](Module['blissRefreshHz']);
      applyUps();
    };
    requestAnimationFrame(tick);
  }

  // Called by the game (web_api::Emit) before the host's onBlissEvent.
  Module['blissOnEventInternal'] = function (type) {
    if (type === 'started') {
      started = true;
      if (Module['blissUps'] > 0 || measuredUps) applyUps();
    }
  };

  // ---- input helpers ----------------------------------------------------------------------
  // SDL keycode for a DOM KeyboardEvent, the way the SDL2 Emscripten backend maps keys (by
  // keyCode, so it is independent of the active layout). For bliss_set_option("control.*").
  var SDLK_SCANCODE_MASK = 0x40000000;
  var special = {
    8: 8, 9: 9, 13: 13, 27: 27, 32: 32, 46: 127,
    16: SDLK_SCANCODE_MASK | 225, 17: SDLK_SCANCODE_MASK | 224, 18: SDLK_SCANCODE_MASK | 226,
    33: SDLK_SCANCODE_MASK | 75, 34: SDLK_SCANCODE_MASK | 78, 35: SDLK_SCANCODE_MASK | 77,
    36: SDLK_SCANCODE_MASK | 74, 37: SDLK_SCANCODE_MASK | 80, 38: SDLK_SCANCODE_MASK | 82,
    39: SDLK_SCANCODE_MASK | 79, 40: SDLK_SCANCODE_MASK | 81, 45: SDLK_SCANCODE_MASK | 73,
    186: 59, 187: 61, 188: 44, 189: 45, 190: 46, 191: 47, 192: 96, 219: 91, 220: 92, 221: 93, 222: 39,
    59: 59, 61: 61, 173: 45
  };
  Module['blissKeycodeFromEvent'] = function (e) {
    var k = e.keyCode;
    if (k >= 65 && k <= 90) return k + 32;          // a..z
    if (k >= 48 && k <= 57) return k;               // 0..9
    if (k >= 112 && k <= 123) return SDLK_SCANCODE_MASK | (58 + k - 112); // F1..F12
    if (k >= 96 && k <= 105) return SDLK_SCANCODE_MASK | (k === 96 ? 98 : 89 + k - 97); // keypad 0..9
    var right = e.location === 2;
    if (right && k === 16) return SDLK_SCANCODE_MASK | 229;
    if (right && k === 17) return SDLK_SCANCODE_MASK | 228;
    if (right && k === 18) return SDLK_SCANCODE_MASK | 230;
    return special[k] || 0;
  };

  function setupCanvas() {
    var canvas = Module['canvas'];
    if (!canvas || canvas.__blissSetup) return;
    canvas.__blissSetup = true;
    if (!canvas.hasAttribute('tabindex')) canvas.setAttribute('tabindex', '0');
    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    canvas.addEventListener('pointerdown', function () { canvas.focus({ preventScroll: true }); });
  }
  setupCanvas();

  function resumeAudio() {
    var sdl2 = Module['SDL2'];
    var ctx = sdl2 && sdl2.audioContext;
    if (ctx && ctx.state !== 'running') ctx.resume();
  }
  if (typeof window !== 'undefined') {
    ['keydown', 'pointerdown', 'touchstart'].forEach(function (type) {
      window.addEventListener(type, resumeAudio, { capture: true, passive: true });
    });
    window.addEventListener('pagehide', function () {
      if (Module['_bliss_persist'] && Module['blissStarted']) Module['_bliss_persist']();
    });
  }
})();
