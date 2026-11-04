/**
 * @file run_voice.h
 *
 * [RUNS MOD] Voiced lines and stingers rendered by tools/voice/render_voice.ps1. They play
 * through their own sound handles rather than effects.tsv rows: SfxID indexes both the base
 * and Hellfire tables, which differ in length, so appended rows could not line up in both.
 * A missing clip stays quiet.
 *
 * Everything here is presentation: each client plays what its own simulation reaches, and
 * clip choice uses private noise, never the game RNG.
 */
#pragma once

#include <cstdint>
#include <string>

namespace devilution {

/** One speaker at a time per channel: a new line cuts off the last one on that channel. */
enum class VoiceChannel : uint8_t {
	Dungeon,
	Merchant,
	Gambler,
	Rumble,
};

enum class RunVoiceLine : uint8_t {
	// The dungeon itself.
	Open,
	Noticed,
	Wave,
	Collapse,
	Minute,
	Thirty,
	Ten,
	SealedInside,
	SealedOutside,
	Overtime,
	Escape,
	// The Dungeon Merchant.
	MerchantGreet,
	MerchantBuy,
	MerchantCraft,
	MerchantBroke,
	MerchantGoodbye,
};

/** @brief Plays a clip on a channel, replacing whatever that channel was saying. */
void PlayVoiceClip(VoiceChannel channel, const std::string &path);

/** @brief True while the channel's last clip is still sounding. */
bool IsVoiceChannelPlaying(VoiceChannel channel);

/** @brief Picks and plays a line, honouring its chance and whether it may interrupt. */
void PlayRunVoice(RunVoiceLine line);

/** @brief Plays a stone-breaking stinger. @param intensity 0 (distant groan) to 3 (roof coming in). */
void PlayCollapseRumble(int intensity);

/** @brief Releases every channel. Must run before the audio device closes. */
void FreeVoiceChannels();

} // namespace devilution
