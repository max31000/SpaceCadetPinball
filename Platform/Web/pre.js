// Emscripten --pre-js for the web build.
// - Mounts IDBFS at /libsdl (parent of the SDL pref path /libsdl/SpaceCadetPinball/) and
//   loads it from IndexedDB before main() runs, so options + high scores survive reloads.
// - Module.blissSyncfs(): serialized MEMFS -> IndexedDB flush, called by the game after the
//   settings .ini changes (winmain::PersistSettings) and usable by the host page.
// - Resumes the SDL2 AudioContext on the first user gesture (autoplay policy).
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  var mountPoint = '/libsdl';
  try { FS.mkdir(mountPoint); } catch (e) { /* exists */ }
  FS.mount(IDBFS, {}, mountPoint);
  addRunDependency('bliss-idbfs');
  FS.syncfs(true, function (err) {
    if (err) console.error('[bliss] IDBFS load failed', err);
    removeRunDependency('bliss-idbfs');
  });
});

// Set once main() has started the frame loop (host pages can poll or wrap this).
Module['postRun'] = Module['postRun'] || [];
Module['postRun'].push(function () { Module['blissStarted'] = true; });

(function () {
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
      if (Module['_bliss_persist'] && Module['calledRun']) Module['_bliss_persist']();
    });
  }
})();
