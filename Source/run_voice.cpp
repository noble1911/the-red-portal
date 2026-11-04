/**
 * @file run_voice.cpp
 *
 * [RUNS MOD] Implementation of voiced lines and stingers. See run_voice.h.
 */
#include "run_voice.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <fmt/format.h>

#include "engine/sound.h"
#include "msg.h"
#include "options.h"

namespace devilution {

namespace {

struct VoiceLineSet {
	VoiceChannel channel;
	const char *folder;
	const char *name;
	int clips;
	/** Out of 6: how often it plays. Recurring lines wear thin if they always fire. */
	int chance;
	/** A minor line never talks over the same speaker; anything else cuts in. */
	bool minor;
};

// Indexed by RunVoiceLine.
constexpr VoiceLineSet VoiceLines[] = {
	{ VoiceChannel::Dungeon, "dungeon", "open", 3, 6, true },
	{ VoiceChannel::Dungeon, "dungeon", "noticed", 3, 6, false },
	{ VoiceChannel::Dungeon, "dungeon", "wave", 3, 2, true },
	{ VoiceChannel::Dungeon, "dungeon", "collapse", 3, 6, false },
	{ VoiceChannel::Dungeon, "dungeon", "minute", 2, 6, false },
	{ VoiceChannel::Dungeon, "dungeon", "thirty", 2, 6, false },
	{ VoiceChannel::Dungeon, "dungeon", "ten", 2, 6, false },
	{ VoiceChannel::Dungeon, "dungeon", "sealed_in", 2, 6, false },
	{ VoiceChannel::Dungeon, "dungeon", "sealed_out", 2, 6, false },
	{ VoiceChannel::Dungeon, "dungeon", "overtime", 3, 2, true },
	{ VoiceChannel::Dungeon, "dungeon", "escape", 2, 6, false },
	{ VoiceChannel::Merchant, "merchant", "greet", 3, 4, true },
	{ VoiceChannel::Merchant, "merchant", "buy", 3, 4, true },
	{ VoiceChannel::Merchant, "merchant", "craft", 2, 6, false },
	{ VoiceChannel::Merchant, "merchant", "broke", 2, 6, false },
	{ VoiceChannel::Merchant, "merchant", "goodbye", 2, 3, true },
};

constexpr int RumbleClips = 4;

std::array<std::unique_ptr<TSnd>, 4> Channels;
int LastClip[std::size(VoiceLines)];

/** Presentation-only noise: barks must never advance the game RNG. */
uint32_t VoiceNoise()
{
	static uint32_t state = static_cast<uint32_t>(SDL_GetTicks());
	state = state * 1664525U + 1013904223U;
	return state >> 16;
}

} // namespace

void PlayVoiceClip(VoiceChannel channel, const std::string &path)
{
	// Same gate as the engine's own effects: gbBufferMsgs means a join is replaying
	// messages, and nothing should speak over that.
	if (!gbSndInited || !gbSoundOn || gbBufferMsgs != 0)
		return;
	std::unique_ptr<TSnd> &slot = Channels[static_cast<size_t>(channel)];
	// Replacing the handle cuts off whatever the channel was still saying.
	slot = nullptr;
	auto sound = SoundFileLoadWithStatus(path.c_str());
	if (!sound.has_value() || *sound == nullptr)
		return;
	slot = std::move(*sound);
	snd_play_snd(slot.get(), 0, 0, *GetOptions().Audio.soundVolume);
}

bool IsVoiceChannelPlaying(VoiceChannel channel)
{
	const std::unique_ptr<TSnd> &slot = Channels[static_cast<size_t>(channel)];
	return slot != nullptr && slot->isPlaying();
}

void PlayRunVoice(RunVoiceLine line)
{
	const auto kind = static_cast<size_t>(line);
	const VoiceLineSet &set = VoiceLines[kind];
	if (static_cast<int>(VoiceNoise() % 6) >= set.chance)
		return;
	if (set.minor && IsVoiceChannelPlaying(set.channel))
		return;
	// Never the same line twice running.
	int clip = 0;
	if (set.clips > 1)
		clip = (LastClip[kind] + 1 + static_cast<int>(VoiceNoise() % (set.clips - 1))) % set.clips;
	LastClip[kind] = clip;
	PlayVoiceClip(set.channel, fmt::format("sfx\\{}\\{}{:02d}.wav", set.folder, set.name, clip + 1));
}

void PlayCollapseRumble(int intensity)
{
	const int clip = std::clamp(intensity, 0, RumbleClips - 1);
	PlayVoiceClip(VoiceChannel::Rumble, fmt::format("sfx\\rumble\\rumble{:02d}.wav", clip + 1));
}

void FreeVoiceChannels()
{
	for (std::unique_ptr<TSnd> &slot : Channels)
		slot = nullptr;
}

} // namespace devilution
