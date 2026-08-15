/**
 * @file runs_icons.cpp
 *
 * [RUNS MOD] Implementation of the mod-supplied inventory icons. See runs_icons.h.
 */
#include "runs_icons.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "engine/load_file.hpp"
#include "engine/palette.h"
#include "engine/surface.hpp"
#include "utils/log.hpp"
#include "utils/surface_to_clx.hpp"

namespace devilution {

namespace {

// The "DXGM" container, shared with the gem art (LoadGemArt in runs_gems.cpp reads the
// same layout -- kept as two small readers rather than one shared one so the gem path,
// which ships working content, is not disturbed):
//
//   'D' 'X' 'G' 'M'  version(1)  spriteCount  colourCount  pad
//   colourCount x (R, G, B)
//   per sprite: width(1)  height(1)  width*height index bytes, row-major,
//               0 = transparent, otherwise colour-table index + 1
//
// True RGB rather than palette indices, because Diablo loads a different palette per
// dungeon type and cycles indices 1-31 for the lava and water animations. The colours are
// matched against whatever palette is live, avoiding the cycled range, and rebuilt when
// the palette changes.
constexpr char IconAssetPath[] = "data\\inv\\runicons.dat";
/** Palette indices below this are animated (see palette_update_caves) or reserved. */
constexpr int FirstStablePaletteIndex = 32;

struct IconArt {
	uint8_t width = 0;
	uint8_t height = 0;
	std::unique_ptr<uint8_t[]> pixels; // 0 = transparent, else colour table index + 1
};

std::array<IconArt, NumRunIcons> IconArtwork;
std::vector<SDL_Color> IconArtColors;
bool IconArtLoaded = false;
bool IconArtMissing = false;

std::array<OptionalOwnedClxSpriteList, NumRunIcons> IconSprites;
unsigned IconSpritesPaletteGeneration = 0;
bool IconSpritesBuilt = false;

/** Reads runicons.dat into IconArtwork/IconArtColors. Safe to call repeatedly. */
void LoadIconArt()
{
	if (IconArtLoaded || IconArtMissing)
		return;

	size_t size = 0;
	std::unique_ptr<uint8_t[]> data = LoadFileInMem<uint8_t>(IconAssetPath, &size);
	if (data == nullptr || size < 8 || data[0] != 'D' || data[1] != 'X' || data[2] != 'G' || data[3] != 'M') {
		LogError("Icon art {} missing or malformed; mod items keep their vanilla icon", IconAssetPath);
		IconArtMissing = true;
		return;
	}

	const int spriteCount = data[5];
	const int colorCount = data[6];
	size_t offset = 8;
	if (spriteCount < NumRunIcons || offset + static_cast<size_t>(colorCount) * 3 > size) {
		LogError("Icon art {} has {} sprites, expected at least {}", IconAssetPath, spriteCount, NumRunIcons);
		IconArtMissing = true;
		return;
	}

	IconArtColors.resize(colorCount);
	for (int i = 0; i < colorCount; i++) {
		IconArtColors[i] = SDL_Color { data[offset], data[offset + 1], data[offset + 2], 255 };
		offset += 3;
	}

	for (int i = 0; i < NumRunIcons; i++) {
		if (offset + 2 > size) {
			IconArtMissing = true;
			return;
		}
		const uint8_t w = data[offset++];
		const uint8_t h = data[offset++];
		const size_t pixelCount = static_cast<size_t>(w) * h;
		if (pixelCount == 0 || offset + pixelCount > size) {
			IconArtMissing = true;
			return;
		}
		IconArtwork[i].width = w;
		IconArtwork[i].height = h;
		IconArtwork[i].pixels = std::make_unique<uint8_t[]>(pixelCount);
		std::copy_n(&data[offset], pixelCount, IconArtwork[i].pixels.get());
		offset += pixelCount;
	}
	IconArtLoaded = true;
}

/** Nearest palette entry to `color`, skipping the animated low indices. */
uint8_t NearestStablePaletteIndex(SDL_Color color)
{
	int best = FirstStablePaletteIndex;
	int bestDistance = std::numeric_limits<int>::max();
	for (int i = FirstStablePaletteIndex; i < 256; i++) {
		const SDL_Color &candidate = logical_palette[i];
		const int dr = static_cast<int>(color.r) - candidate.r;
		const int dg = static_cast<int>(color.g) - candidate.g;
		const int db = static_cast<int>(color.b) - candidate.b;
		// Weighted to favour green, which the eye resolves best.
		const int distance = dr * dr * 2 + dg * dg * 4 + db * db;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = i;
		}
	}
	return static_cast<uint8_t>(best);
}

void BuildIconSprites()
{
	LoadIconArt();
	if (!IconArtLoaded)
		return;

	// Map the art's colour table onto the live palette once, then paint by lookup.
	std::vector<uint8_t> colorMap(IconArtColors.size());
	for (size_t i = 0; i < IconArtColors.size(); i++)
		colorMap[i] = NearestStablePaletteIndex(IconArtColors[i]);

	for (int i = 0; i < NumRunIcons; i++) {
		const IconArt &art = IconArtwork[i];
		if (art.width == 0 || art.height == 0)
			continue;
		OwnedSurface surface { art.width, art.height };
		for (int y = 0; y < art.height; y++) {
			for (int x = 0; x < art.width; x++) {
				const uint8_t value = art.pixels[static_cast<size_t>(y) * art.width + x];
				*surface.at(x, y) = value == 0 ? 0 : colorMap[value - 1];
			}
		}
		IconSprites[i].emplace(SurfaceToClx(surface, 1, 0));
	}
	IconSpritesBuilt = true;
	IconSpritesPaletteGeneration = PaletteGeneration;
}

/** Rebuilds the sprites if the palette has moved on since they were made. */
void EnsureIconSprites()
{
	if (IconSpritesBuilt && IconSpritesPaletteGeneration == PaletteGeneration)
		return;
	BuildIconSprites();
}

} // namespace

bool RunIconsAvailable()
{
	LoadIconArt();
	return IconArtLoaded;
}

void RefreshRunIconSprites()
{
	BuildIconSprites();
}

void FreeRunIconSprites()
{
	for (auto &sprite : IconSprites)
		sprite = std::nullopt;
	IconSpritesBuilt = false;
}

ClxSprite GetRunIconSprite(int curs)
{
	EnsureIconSprites();
	const int index = std::clamp(curs - RunIconCursFirst, 0, NumRunIcons - 1);
	return (*IconSprites[index])[0];
}

} // namespace devilution
