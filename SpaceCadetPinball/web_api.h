#pragma once

// Host page integration of the Emscripten build (see Doc/web.md). All functions are no-ops in
// desktop builds so call sites need no #ifdefs.
class web_api
{
public:
#ifdef __EMSCRIPTEN__
	// Calls Module.onBlissEvent(type, json) if the host provided it.
	static void Emit(const char* type, const char* json);
	// Once per animation frame: "started", "pauseChanged", periodic "optionsChanged" check.
	static void Frame();
	// Emits "optionsChanged" when the observable options differ from the last emitted state.
	static void CheckOptionsChanged();
	// pb::end_game hook: scores in player order.
	static void GameOver(const int* scores, int playerCount, bool demo, bool cheats);
	static void HighScoreEntry(int score, int position, const char* defaultName, int pending);
	static bool Started;
#else
	static void Emit(const char*, const char*) { }
	static void Frame() { }
	static void CheckOptionsChanged() { }
	static void GameOver(const int*, int, bool, bool) { }
	static void HighScoreEntry(int, int, const char*, int) { }
#endif
};
