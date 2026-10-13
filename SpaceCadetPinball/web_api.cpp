// C API exported to the host page in the Emscripten build, plus the host event channel
// (Module.onBlissEvent). See Doc/web.md for the contract.
// Strings are returned as pointers to a static buffer: read them with Module.UTF8ToString()
// before the next call. All bliss_* functions are safe to call before the "started" event
// (they do nothing and return 0 then).
#include "pch.h"
#include "web_api.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include "high_score.h"
#include "midi.h"
#include "options.h"
#include "pb.h"
#include "Sound.h"
#include "translations.h"
#include "winmain.h"

bool web_api::Started = false;

namespace
{
	std::string ResultBuffer;
	std::string LastOptionsJson;
	bool LastPaused = false;
	int FrameCounter = 0;

	// Host-visible names of the remappable controls (the six of the original key mapper).
	struct ControlName
	{
		const char* Name;
		GameBindings Binding;
	};

	const ControlName ControlNames[] =
	{
		{"leftFlipper", GameBindings::LeftFlipper},
		{"rightFlipper", GameBindings::RightFlipper},
		{"plunger", GameBindings::Plunger},
		{"leftTableBump", GameBindings::LeftTableBump},
		{"rightTableBump", GameBindings::RightTableBump},
		{"bottomTableBump", GameBindings::BottomTableBump},
	};

	std::string JsonEscape(const char* str)
	{
		std::string out;
		for (auto p = str; p && *p; ++p)
		{
			auto c = static_cast<unsigned char>(*p);
			switch (c)
			{
			case '"': out += "\\\"";
				break;
			case '\\': out += "\\\\";
				break;
			case '\n': out += "\\n";
				break;
			default:
				if (c < 0x20)
				{
					char buf[8];
					snprintf(buf, sizeof buf, "\\u%04x", c);
					out += buf;
				}
				else
					out += static_cast<char>(c);
			}
		}
		return out;
	}

	const char* Bool(bool value)
	{
		return value ? "true" : "false";
	}

	std::string ControlsJson()
	{
		std::string out = "{";
		bool first = true;
		for (const auto& control : ControlNames)
		{
			const auto& input = options::Options.Key[~control.Binding].Inputs[0];
			int key = input.Type == InputTypes::Keyboard ? input.Value : 0;
			if (!first)
				out += ",";
			first = false;
			out += "\"" + std::string(control.Name) + "\":{\"key\":" + std::to_string(key) +
				",\"name\":\"" + JsonEscape(key ? SDL_GetKeyName(key) : "") + "\"}";
		}
		return out + "}";
	}

	// Observable options (everything the host DOM shows), without the pause state.
	std::string OptionsJson()
	{
		auto& o = options::Options;
		char buf[384];
		snprintf(buf, sizeof buf,
		         "\"sounds\":%s,\"music\":%s,\"players\":%d,\"soundVolume\":%d,\"musicVolume\":%d,"
		         "\"language\":\"%s\",\"ups\":%d,\"highScoresEnabled\":%s,\"launchBallEnabled\":%s,"
		         "\"demoActive\":%s,\"controls\":",
		         Bool(o.Sounds), Bool(o.Music), o.Players.V, o.SoundVolume.V, o.MusicVolume.V,
		         JsonEscape(translations::GetCurrentLanguage()->ShortName).c_str(), o.UpdatesPerSecond.V,
		         Bool(winmain::HighScoresEnabled), Bool(winmain::LaunchBallEnabled), Bool(winmain::DemoActive));
		return "{" + std::string(buf) + ControlsJson() + "}";
	}

	const char* Result(const std::string& value)
	{
		ResultBuffer = value;
		return ResultBuffer.c_str();
	}

	void OptionsTouched()
	{
		winmain::PersistSettings();
		web_api::CheckOptionsChanged();
	}

	bool FindControl(const std::string& name, GameBindings& binding)
	{
		for (const auto& control : ControlNames)
		{
			if (name == control.Name)
			{
				binding = control.Binding;
				return true;
			}
		}
		return false;
	}
}

void web_api::Emit(const char* type, const char* json)
{
	EM_ASM({
		var type = UTF8ToString($0);
		var json = UTF8ToString($1);
		if (Module["blissOnEventInternal"]) Module["blissOnEventInternal"](type, json);
		var cb = Module["onBlissEvent"];
		if (typeof cb != "function") return;
		try { cb(type, json); }
		catch (e) { console.error("[bliss] onBlissEvent handler failed", e); }
	}, type, json);
}

void web_api::Frame()
{
	if (!Started)
	{
		Started = true;
		LastPaused = winmain::single_step;
		LastOptionsJson = OptionsJson();
		char buf[96];
		int w = 0, h = 0;
		if (winmain::MainWindow)
			SDL_GetWindowSize(winmain::MainWindow, &w, &h);
		snprintf(buf, sizeof buf, "{\"version\":\"%s\",\"width\":%d,\"height\":%d}", winmain::Version, w, h);
		EM_ASM({ Module["blissStarted"] = true; });
		Emit("started", buf);
		return;
	}

	if (LastPaused != winmain::single_step)
	{
		LastPaused = winmain::single_step;
		Emit("pauseChanged", LastPaused ? "{\"paused\":true}" : "{\"paused\":false}");
	}

	// Keyboard shortcuts (F4-style toggles, restarts) change options outside of the API.
	if (++FrameCounter >= 15)
	{
		FrameCounter = 0;
		CheckOptionsChanged();
	}
}

void web_api::CheckOptionsChanged()
{
	if (!Started)
		return;
	auto json = OptionsJson();
	if (json == LastOptionsJson)
		return;
	LastOptionsJson = json;
	Emit("optionsChanged", json.c_str());
}

void web_api::GameOver(const int* scores, int playerCount, bool demo, bool cheats)
{
	int best = 0;
	std::string list = "[";
	for (int i = 0; i < playerCount; i++)
	{
		best = std::max(best, scores[i]);
		if (i)
			list += ",";
		list += std::to_string(scores[i]);
	}
	list += "]";
	auto json = "{\"score\":" + std::to_string(best) + ",\"scores\":" + list + ",\"players\":" +
		std::to_string(playerCount) + ",\"demo\":" + Bool(demo) + ",\"cheats\":" + Bool(cheats) + "}";
	Emit("gameOver", json.c_str());
}

void web_api::HighScoreEntry(int score, int position, const char* defaultName, int pending)
{
	auto json = "{\"score\":" + std::to_string(score) + ",\"position\":" + std::to_string(position) +
		",\"rank\":" + std::to_string(position + 1) + ",\"name\":\"" + JsonEscape(defaultName) +
		"\",\"pending\":" + std::to_string(pending) + "}";
	Emit("highScoreEntry", json.c_str());
}

extern "C" {

// Start a new game (F2).
EMSCRIPTEN_KEEPALIVE void bliss_new_game()
{
	if (web_api::Started)
		winmain::new_game();
}

// Menu "Запуск шарика" (Launch Ball). Returns 0 when the original menu item would be disabled.
EMSCRIPTEN_KEEPALIVE int bliss_launch_ball()
{
	if (!web_api::Started || !winmain::LaunchBallEnabled)
		return 0;
	winmain::end_pause();
	pb::launch_ball();
	return 1;
}

// Menu "Демонстрация" (Demo): toggles the attract/demo mode. Returns 1 if demo is active after.
EMSCRIPTEN_KEEPALIVE int bliss_demo_toggle()
{
	if (!web_api::Started)
		return 0;
	winmain::end_pause();
	pb::toggle_demo();
	web_api::CheckOptionsChanged();
	return winmain::DemoActive ? 1 : 0;
}

// Toggle pause/resume (F3). Returns 1 if the game is paused afterwards.
EMSCRIPTEN_KEEPALIVE int bliss_toggle_pause()
{
	if (!web_api::Started)
		return 0;
	winmain::pause();
	return winmain::single_step ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bliss_is_paused()
{
	return web_api::Started && winmain::single_step ? 1 : 0;
}

// Pause without toggling (e.g. host menu opened / tab hidden). Resume with bliss_toggle_pause.
EMSCRIPTEN_KEEPALIVE void bliss_pause()
{
	if (web_api::Started)
		winmain::pause(false);
}

// Toggle sound effects. Returns the new state.
EMSCRIPTEN_KEEPALIVE int bliss_toggle_sound()
{
	if (!web_api::Started)
		return 0;
	options::toggle(Menu1::Sounds);
	OptionsTouched();
	return options::Options.Sounds ? 1 : 0;
}

// Toggle music. Returns the new state (0 also when no music file is available).
EMSCRIPTEN_KEEPALIVE int bliss_toggle_music()
{
	if (!web_api::Started)
		return 0;
	options::toggle(Menu1::Music);
	OptionsTouched();
	return options::Options.Music ? 1 : 0;
}

// JSON of the observable options plus "paused" (same shape as the optionsChanged event).
EMSCRIPTEN_KEEPALIVE const char* bliss_get_options()
{
	if (!web_api::Started)
		return Result("{}");
	auto json = OptionsJson();
	json.pop_back();
	json += std::string(",\"paused\":") + Bool(winmain::single_step) + "}";
	return Result(json);
}

// Set one integer option. Returns 1 on success, 0 for an unknown name / not started.
//   sounds, music: 0/1; players: 1..4; soundVolume, musicVolume: 0..128;
//   control.<leftFlipper|rightFlipper|plunger|leftTableBump|rightTableBump|bottomTableBump>:
//       SDL keycode for the keyboard slot (0 clears it);
//   controlsDefault: any value, restores the default controls.
EMSCRIPTEN_KEEPALIVE int bliss_set_option(const char* name, int value)
{
	if (!web_api::Started)
		return 0;
	auto& o = options::Options;
	std::string key = name ? name : "";
	GameBindings binding{};
	if (key == "sounds")
	{
		if ((value != 0) != o.Sounds.V)
			options::toggle(Menu1::Sounds);
	}
	else if (key == "music")
	{
		if ((value != 0) != o.Music.V)
			options::toggle(Menu1::Music);
	}
	else if (key == "players")
	{
		value = Clamp(value, 1, 4);
		options::toggle(static_cast<Menu1>(static_cast<int>(Menu1::OnePlayer) + value - 1));
	}
	else if (key == "soundVolume")
	{
		o.SoundVolume = Clamp(value, options::MinVolume, options::MaxVolume);
		Sound::SetVolume(o.SoundVolume);
	}
	else if (key == "musicVolume")
	{
		o.MusicVolume = Clamp(value, options::MinVolume, options::MaxVolume);
		midi::SetVolume(o.MusicVolume);
	}
	else if (key.rfind("control.", 0) == 0 && FindControl(key.substr(8), binding))
	{
		o.Key[~binding].Inputs[0] = value > 0
			                            ? GameInput{InputTypes::Keyboard, value}
			                            : GameInput{};
	}
	else if (key == "controlsDefault")
	{
		for (const auto& control : ControlNames)
		{
			auto& option = o.Key[~control.Binding];
			for (int i = 0; i < 3; i++)
				option.Inputs[i] = option.Defaults[i];
		}
	}
	else
		return 0;
	OptionsTouched();
	return 1;
}

// JSON array of the 5 high score entries: [{"name":"...","score":123}, ...] (UTF-8 names,
// empty slot: score -999).
EMSCRIPTEN_KEEPALIVE const char* bliss_get_high_scores()
{
	std::string out = "[";
	for (int i = 0; i < 5; i++)
	{
		const auto& entry = high_score::highscore_table[i];
		char name[sizeof entry.Name + 1]{};
		memcpy(name, entry.Name, sizeof entry.Name);
		if (i)
			out += ",";
		out += "{\"name\":\"" + JsonEscape(name) + "\",\"score\":" + std::to_string(entry.Score) + "}";
	}
	out += "]";
	return Result(out);
}

// Answer to the "highScoreEntry" event: store the pending score under this UTF-8 name
// (truncated to 31 bytes). An empty name stores the default name of the entry ("Player N",
// what the original dialog pre-fills). Returns the 1-based rank, 0 when no entry is pending.
EMSCRIPTEN_KEEPALIVE int bliss_submit_high_score_name(const char* name)
{
	if (!web_api::Started)
		return 0;
	auto rank = high_score::SubmitPendingName(name ? name : "");
	if (rank > 0)
		winmain::PersistSettings();
	return rank;
}

// Dismiss the pending high score entry without storing it. Returns 1 if one was pending.
EMSCRIPTEN_KEEPALIVE int bliss_cancel_high_score()
{
	return web_api::Started && high_score::CancelPendingEntry() ? 1 : 0;
}

// Clear the high score table ("Очистить" in the original dialog).
EMSCRIPTEN_KEEPALIVE void bliss_clear_high_scores()
{
	if (!web_api::Started)
		return;
	high_score::ClearAndSave();
	winmain::PersistSettings();
}

// Switch the language: "ru_xp" (original Russian XP texts, default), "ru" (community full
// translation), "en", ... Restarts the game instance like the desktop Language menu.
// Returns 1 if accepted, 0 for an unknown code.
EMSCRIPTEN_KEEPALIVE int bliss_set_language(const char* code)
{
	if (!web_api::Started || !translations::IsKnownLanguage(code))
		return 0;
	if (strcmp(translations::GetCurrentLanguage()->ShortName, code) != 0)
	{
		translations::SetCurrentLanguage(code);
		winmain::Restart();
	}
	return 1;
}

// Simulation steps per second (60..360). The web build renders one frame per
// requestAnimationFrame and targets one step per frame, so this should equal the display
// refresh rate; pre.js measures it and calls this automatically (see Doc/web.md).
// Returns the applied value.
EMSCRIPTEN_KEEPALIVE int bliss_set_ups(int ups)
{
	if (!web_api::Started)
		return 0;
	ups = Clamp(ups, options::MinUps, options::MaxUps);
	options::Options.UpdatesPerSecond = ups;
	options::Options.FramesPerSecond = ups;
	winmain::UpdateFrameRate();
	web_api::CheckOptionsChanged();
	return ups;
}

// Call after writing PINBALL.OGG into the data dir later than start (lazy music loading).
// Returns 1 if the music track is available; it starts playing when music is enabled.
EMSCRIPTEN_KEEPALIVE int bliss_reload_music()
{
	return web_api::Started && midi::ReloadMusic() ? 1 : 0;
}

// Flush settings + high scores to IndexedDB now (e.g. from pagehide). Async on the JS side.
EMSCRIPTEN_KEEPALIVE void bliss_persist()
{
	if (web_api::Started)
		winmain::PersistSettings();
}

}
#endif
