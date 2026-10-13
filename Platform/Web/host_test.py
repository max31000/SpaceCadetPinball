"""End-to-end check of the web build through Platform/Web/host-test.html (dev only).

Serve a directory that contains both the repository and a game data directory, e.g.
    python -m http.server 8766 --bind 127.0.0.1 --directory <parent of repo>
and run
    python Platform/Web/host_test.py --base http://127.0.0.1:8766/SpaceCadetPinball/Platform/Web/host-test.html \
        --data /pinball-data/ --browser chromium --seconds 120 --shots out/

Checks: start + events (started, optionsChanged, pauseChanged, gameOver, highScoreEntry),
frame pacing over an auto-play session, high score submit persisting across reload, options
persisting across reload, language switch. Browsers are muted (the game plays sound).
Requires: pip install playwright && playwright install chromium firefox
"""
import argparse
import json
import os
import statistics
import sys
import time

from playwright.sync_api import sync_playwright

RAF_SCRIPT = """
window.__raf = [];
(function loop(t){ window.__raf.push(t); requestAnimationFrame(loop); })(performance.now());
window.blissAutomation = true;
"""


def frame_stats(ts):
    d = [b - a for a, b in zip(ts, ts[1:])]
    if not d:
        return {}
    s = sorted(d)
    q = lambda p: s[min(len(s) - 1, int(p * len(s)))]
    median = q(0.5)
    total = (ts[-1] - ts[0]) / 1000
    return {
        "frames": len(d),
        "seconds": round(total, 1),
        "fps": round(len(d) / total, 2),
        "p50_ms": round(median, 2),
        "p95_ms": round(q(0.95), 2),
        "p99_ms": round(q(0.99), 2),
        "max_ms": round(s[-1], 1),
        "pct_missed_frames": round(100 * sum(1 for x in d if x > median * 1.5) / len(d), 3),
        "pct_over_33ms": round(100 * sum(1 for x in d if x > 33.4) / len(d), 3),
    }


class QuickDone(Exception):
    pass


class Run:
    def __init__(self, page, shots, tag):
        self.page, self.shots, self.tag = page, shots, tag
        self.checks = {}

    def ev(self, expr):
        return self.page.evaluate(expr)

    def events(self, kind=None):
        evs = self.ev("() => window.blissEvents || []")
        return [e for e in evs if kind is None or e["type"] == kind]

    def wait_event(self, kind, count=1, timeout=30000):
        self.page.wait_for_function(
            f"() => (window.blissEvents || []).filter(e => e.type === '{kind}').length >= {count}",
            timeout=timeout)

    def options(self):
        return json.loads(self.ev("() => pinball.UTF8ToString(pinball._bliss_get_options())"))

    def high_scores(self):
        return json.loads(self.ev("() => pinball.UTF8ToString(pinball._bliss_get_high_scores())"))

    def shot(self, name):
        if self.shots:
            self.page.screenshot(path=os.path.join(self.shots, f"{self.tag}-{name}.png"))

    def focus(self):
        self.ev("() => document.getElementById('game').focus()")

    def check(self, name, ok, detail=None):
        self.checks[name] = {"ok": bool(ok), **({"detail": detail} if detail is not None else {})}
        print(f"  [{'ok' if ok else 'FAIL'}] {name}" + (f": {detail}" if detail is not None else ""), flush=True)

    def load(self, url):
        t0 = time.time()
        self.page.goto(url)
        self.wait_event("started", timeout=60000)
        return round(time.time() - t0, 2)

    def sync_count(self):
        return self.ev("() => pinball.blissSyncCount")

    def wait_synced(self, before, timeout=5000):
        self.page.wait_for_function(f"() => pinball.blissSyncCount > {before}", timeout=timeout)


def autoplay(run, seconds, flippers=True, stop_on_game_over=False):
    """Plunger every ~4 s (hold 1 s), flippers alternating every ~350 ms.
    With stop_on_game_over, keeps the last screenshots: "game over" is shown for 3 s before
    the gameOver event fires (TPinballTable::EndGame_timeout)."""
    page = run.page
    recent = []
    run.focus()
    t_end = time.time() + seconds
    next_plunge = 0
    flip = 0
    while time.time() < t_end:
        now = time.time()
        if now >= next_plunge:
            page.keyboard.down(" ")
            page.wait_for_timeout(1000)
            page.keyboard.up(" ")
            next_plunge = time.time() + 3
        if flippers:
            key = "z" if flip % 2 == 0 else "/"
            page.keyboard.down(key)
            page.wait_for_timeout(120)
            page.keyboard.up(key)
            flip += 1
        page.wait_for_timeout(230)
        if stop_on_game_over:
            if run.events("gameOver"):
                if run.shots:
                    for i, png in enumerate(recent):
                        with open(os.path.join(run.shots, f"{run.tag}-03-before-game-over-{i}.png"), "wb") as f:
                            f.write(png)
                return True
            recent = (recent + [page.screenshot()])[-3:]
    return bool(run.events("gameOver"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="http://127.0.0.1:8766/SpaceCadetPinball/Platform/Web/host-test.html")
    ap.add_argument("--data", default="/pinball-data/")
    ap.add_argument("--build", default=None)
    ap.add_argument("--browser", default="chromium")
    ap.add_argument("--seconds", type=float, default=120)
    ap.add_argument("--headed", action="store_true")
    ap.add_argument("--quick", action="store_true", help="only start + frame pacing")
    ap.add_argument("--shots", default="")
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)

    url = f"{args.base}?data={args.data}" + (f"&build={args.build}" if args.build else "")
    tag = args.browser + ("-headed" if args.headed else "")
    result = {"browser": tag}
    console = []

    with sync_playwright() as p:
        bt = getattr(p, args.browser)
        # Muted: the game plays sound effects through the host audio device otherwise.
        if args.browser == "chromium":
            browser = bt.launch(headless=not args.headed, args=["--mute-audio"])
        else:
            browser = bt.launch(headless=not args.headed, firefox_user_prefs={"media.volume_scale": "0.0"})
        try:
            result["version"] = browser.version
            ctx = browser.new_context(viewport={"width": 1000, "height": 900})
            ctx.add_init_script(RAF_SCRIPT)
            page = ctx.new_page()
            page.on("console", lambda m: console.append(f"[{m.type}] {m.text}"))
            page.on("pageerror", lambda e: console.append(f"[pageerror] {e}"))
            run = Run(page, args.shots, tag)

            print(f"{tag}: load", flush=True)
            result["load_to_started_s"] = run.load(url)
            page.wait_for_timeout(1500)  # refresh-rate measurement (60 rAF frames)
            opts = run.options()
            result["refresh_hz"] = run.ev("() => pinball.blissRefreshHz")
            result["ups"] = opts["ups"]
            run.check("started event", run.events("started"))
            run.check("canvas 600x416", run.ev("() => [game.width, game.height]") == [600, 416],
                      run.ev("() => [game.width, game.height]"))
            run.check("document.title untouched", run.ev("() => document.title") == "Pinball host test")
            run.check("language ru_xp", opts["language"] == "ru_xp")
            run.check("ups snapped", opts["ups"] in (60, 72, 75, 90, 100, 120, 144, 165, 240), opts["ups"])
            run.shot("01-start")

            # ---- auto-play: frame pacing -----------------------------------------------------
            print(f"{tag}: auto-play {args.seconds:.0f} s", flush=True)
            start_idx = run.ev("() => window.__raf.length")
            autoplay(run, args.seconds)
            ts = run.ev(f"() => window.__raf.slice({start_idx})")
            result["frames"] = frame_stats(ts)
            print(f"  frames: {result['frames']}", flush=True)
            run.shot("02-autoplay")
            if args.quick:
                raise QuickDone()

            # ---- typing into a host DOM input while the game runs ------------------------------
            n_pause = len(run.events("pauseChanged"))
            page.click("#probe")
            text = "Игрок z/ x.  42"
            page.keyboard.type(text)
            page.keyboard.press("Backspace")
            page.keyboard.press("F3")  # must not reach the game either
            page.wait_for_timeout(300)
            value = run.ev("() => document.getElementById('probe').value")
            run.check("DOM input receives typing", value == text[:-1], value)
            run.check("game ignores keys outside the canvas", len(run.events("pauseChanged")) == n_pause)

            # ---- pause events -----------------------------------------------------------------
            n = len(run.events("pauseChanged"))
            run.ev("() => pinball._bliss_toggle_pause()")
            run.wait_event("pauseChanged", n + 1, 5000)
            paused = run.events("pauseChanged")[-1]["data"]["paused"]
            run.focus()
            page.keyboard.press("F3")
            run.wait_event("pauseChanged", n + 2, 5000)
            resumed = run.events("pauseChanged")[-1]["data"]["paused"]
            run.check("pauseChanged via export + F3", paused is True and resumed is False)

            # ---- game over + high score entry (no flippers: balls drain) ---------------------
            print(f"{tag}: draining balls for gameOver", flush=True)
            run.ev("() => pinball._bliss_new_game()")
            page.wait_for_timeout(500)
            run.check("launch ball export", run.ev("() => pinball._bliss_launch_ball()") in (0, 1))
            got = autoplay(run, 240, flippers=False, stop_on_game_over=True)
            run.check("gameOver event", got, run.events("gameOver")[-1]["data"] if got else None)
            if got:
                page.wait_for_timeout(1200)
                run.shot("03-game-over")  # ru_xp: "Игра окончена" (Tahoma, Cyrillic)
                try:
                    run.wait_event("highScoreEntry", 1, 5000)
                    hse = run.events("highScoreEntry")[-1]["data"]
                    run.check("highScoreEntry event", True, hse)
                    pending = json.loads(run.ev("() => pinball.UTF8ToString(pinball._bliss_get_pending_high_score())"))
                    run.check("get_pending_high_score", pending == hse, pending)
                    n_hse = len(run.events("highScoreEntry"))
                    run.check("reemit_pending_high_score",
                              run.ev("() => pinball._bliss_reemit_pending_high_score()") == 1
                              and len(run.events("highScoreEntry")) == n_hse + 1)
                    before = run.sync_count()
                    # CR/LF must not break the settings .ini: stored as spaces.
                    rank = run.ev("() => pinball.ccall('bliss_submit_high_score_name', 'number', "
                                  "['string'], ['Тестер\\r\\nПинбола'])")
                    run.check("second submit without entry returns 0",
                              run.ev("() => pinball.ccall('bliss_submit_high_score_name', 'number', ['string'], [''])") == 0)
                    run.check("submit returns rank", rank == hse["rank"], rank)
                    run.wait_synced(before)
                except Exception as e:  # noqa: BLE001
                    run.check("highScoreEntry event", False, str(e))

            # ---- options -------------------------------------------------------------------
            n = len(run.events("optionsChanged"))
            run.ev("() => pinball.ccall('bliss_set_option', 'number', ['string', 'number'], ['sounds', 0])")
            run.ev("() => pinball.ccall('bliss_set_option', 'number', ['string', 'number'], ['players', 2])")
            ok_key = run.ev("() => pinball.ccall('bliss_set_option', 'number', ['string', 'number'], "
                            "['control.leftFlipper', pinball.blissKeycodeFromEvent({keyCode: 65})])")
            run.check("optionsChanged events", len(run.events("optionsChanged")) >= n + 3,
                      len(run.events("optionsChanged")) - n)
            run.check("control option accepted", ok_key == 1)
            page.wait_for_timeout(300)
            before_reload_scores = run.high_scores()

            # ---- reload: persistence ---------------------------------------------------------
            print(f"{tag}: reload", flush=True)
            run.load(url)
            page.wait_for_timeout(500)
            opts = run.options()
            scores = run.high_scores()
            run.check("high score persists", any(s["name"] == "Тестер  Пинбола" for s in scores)
                      and scores == before_reload_scores, scores[:2])
            run.check("options persist", opts["sounds"] is False and opts["players"] == 2
                      and opts["controls"]["leftFlipper"]["key"] == 97,
                      {k: opts[k] for k in ("sounds", "players")} | {"leftFlipper": opts["controls"]["leftFlipper"]})
            run.ev("() => pinball.ccall('bliss_set_option', 'number', ['string', 'number'], ['controlsDefault', 1])")
            run.ev("() => pinball.ccall('bliss_set_option', 'number', ['string', 'number'], ['sounds', 1])")
            run.ev("() => pinball.ccall('bliss_set_option', 'number', ['string', 'number'], ['players', 1])")

            # ---- language switch -------------------------------------------------------------
            n = len(run.events("optionsChanged"))
            run.check("set_language ru", run.ev("() => pinball.ccall('bliss_set_language', 'number', ['string'], ['ru'])") == 1)
            page.wait_for_function(
                f"() => blissEvents.filter(e => e.type === 'optionsChanged').slice({n}).some(e => e.data.language === 'ru')",
                timeout=10000)
            page.wait_for_timeout(1500)
            run.focus()
            page.keyboard.press("F3")  # community "ru": pause message in Russian
            page.wait_for_timeout(800)
            run.shot("04-lang-ru-paused")
            page.keyboard.press("F3")
            run.check("unknown language rejected",
                      run.ev("() => pinball.ccall('bliss_set_language', 'number', ['string'], ['xx'])") == 0)
            run.ev("() => pinball.ccall('bliss_set_language', 'number', ['string'], ['ru_xp'])")
            page.wait_for_timeout(1500)
            run.focus()
            page.keyboard.press("F3")  # ru_xp: original English pause message (bitmap font)
            page.wait_for_timeout(800)
            run.shot("05-lang-ru_xp-paused")
            run.check("back to ru_xp", run.options()["language"] == "ru_xp")
        except QuickDone:
            pass
        finally:
            result["checks"] = run.checks if "run" in locals() else {}
            result["console_errors"] = [c for c in console if c.startswith(("[error]", "[pageerror]"))][:20]
            browser.close()

    print(json.dumps(result, ensure_ascii=False, indent=1))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=1)
    failed = [k for k, v in result["checks"].items() if not v["ok"]]
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
