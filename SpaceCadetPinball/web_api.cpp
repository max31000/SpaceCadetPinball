// C API exported to the host page in the Emscripten build (see Doc/web.md).
// All functions must be called after the runtime is initialized (Module.onRuntimeInitialized
// or later). Strings are returned as pointers to static buffers: read them with
// Module.UTF8ToString() before the next call.
#include "pch.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include "high_score.h"
#include "midi.h"
#include "options.h"
#include "pb.h"
#include "Sound.h"
#include "translations.h"
#include "winmain.h"

namespace
{
	std::string JsonEscape(const char* str)
	{
		std::string out;
		for (auto p = str; *p; ++p)
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

	std::string ResultBuffer;
}

extern "C" {

// Start a new game (F2).
EMSCRIPTEN_KEEPALIVE void bliss_new_game()
{
	winmain::new_game();
}

// Toggle pause/resume (F3). Returns 1 if the game is paused afterwards.
EMSCRIPTEN_KEEPALIVE int bliss_toggle_pause()
{
	winmain::pause();
	return winmain::single_step ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bliss_is_paused()
{
	return winmain::single_step ? 1 : 0;
}

// Pause without toggling (e.g. host menu opened / tab hidden). Resume with bliss_toggle_pause.
EMSCRIPTEN_KEEPALIVE void bliss_pause()
{
	winmain::pause(false);
}

// Toggle sound effects (F4 menu item "Sounds"). Returns the new state.
EMSCRIPTEN_KEEPALIVE int bliss_toggle_sound()
{
	options::toggle(Menu1::Sounds);
	winmain::PersistSettings();
	return options::Options.Sounds ? 1 : 0;
}

// Toggle music. Returns the new state (0 also when no music file is available).
EMSCRIPTEN_KEEPALIVE int bliss_toggle_music()
{
	options::toggle(Menu1::Music);
	winmain::PersistSettings();
	return options::Options.Music ? 1 : 0;
}

// JSON: {"sounds":bool,"music":bool,"players":1..4,"soundVolume":0..128,"musicVolume":0..128,
//        "language":"ru","ups":int,"fps":int,"paused":bool,"highScoresEnabled":bool}
EMSCRIPTEN_KEEPALIVE const char* bliss_get_options()
{
	auto& o = options::Options;
	char buf[512];
	snprintf(buf, sizeof buf,
	         "{\"sounds\":%s,\"music\":%s,\"players\":%d,\"soundVolume\":%d,\"musicVolume\":%d,"
	         "\"language\":\"%s\",\"ups\":%d,\"fps\":%d,\"paused\":%s,\"highScoresEnabled\":%s}",
	         o.Sounds ? "true" : "false", o.Music ? "true" : "false", o.Players.V,
	         o.SoundVolume.V, o.MusicVolume.V, JsonEscape(translations::GetCurrentLanguage()->ShortName).c_str(),
	         o.UpdatesPerSecond.V, o.FramesPerSecond.V,
	         winmain::single_step ? "true" : "false", winmain::HighScoresEnabled ? "true" : "false");
	ResultBuffer = buf;
	return ResultBuffer.c_str();
}

// Set one integer option. Names: "sounds" (0/1), "music" (0/1), "players" (1..4),
// "soundVolume" (0..128), "musicVolume" (0..128). Returns 1 on success, 0 for unknown name.
EMSCRIPTEN_KEEPALIVE int bliss_set_option(const char* name, int value)
{
	auto& o = options::Options;
	std::string key = name ? name : "";
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
	else
		return 0;
	winmain::PersistSettings();
	return 1;
}

// JSON array of the 5 high score entries: [{"name":"...","score":123}, ...] (UTF-8 names).
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
	ResultBuffer = out;
	return ResultBuffer.c_str();
}

// Flush settings + high scores to IndexedDB now (e.g. from pagehide). Async on the JS side.
EMSCRIPTEN_KEEPALIVE void bliss_persist()
{
	winmain::PersistSettings();
}

}
#endif
