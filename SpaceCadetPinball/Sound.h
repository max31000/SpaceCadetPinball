#pragma once
#include "maths.h"
#include "TPinballComponent.h"

struct ChannelInfo
{
	int TimeStamp;
	vector2 Position;
};

class Sound
{
public:
	static std::vector<ChannelInfo> Channels;

	static void Init(bool mixOpen, int channels, bool enableFlag, int volume);
	static void Enable(bool enableFlag);
	static void Activate();
	static void Deactivate();
	static void Close();
	static void PlaySound(Mix_Chunk* wavePtr, int time, TPinballComponent *soundSource, const char* info);
	static Mix_Chunk* LoadWaveFile(const std::string& lpName);
	static void FreeSound(Mix_Chunk* wave);
	static void SetChannels(int channels);
	static void SetVolume(int volume);
	// Host-side master volume 0..100 (web build); scales Volume, not stored in options.
	static void SetMasterVolume(int percent);
	static int ScaleByMaster(int volume);
	static int MasterVolume;
private:
	static int num_channels;
	static bool enabled_flag;
	static int Volume;
	static bool MixOpen;
};
