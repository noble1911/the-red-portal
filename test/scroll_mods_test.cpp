#include <gtest/gtest.h>

#include "scroll_mods.h"

namespace devilution {
namespace {

// [RUNS MOD] A scroll's modifiers add up, and their magnitudes grow with depth and tier, so
// AggregateRunMods holds the combined effect under fixed ceilings (balance review 2026-10-28).

ScrollMod Mod(ScrollAffix affix, int boonPct, int banePct)
{
	return ScrollMod { affix, boonPct, banePct, 3 };
}

TEST(ScrollModsTest, StackedBanesAreCapped)
{
	// Three modifiers whose banes are all monster life.
	ScrollModSet set {};
	set.count = 3;
	set.mods[0] = Mod(ScrollAffix::Avaricious, 100, 400);
	set.mods[1] = Mod(ScrollAffix::Erudite, 100, 400);
	set.mods[2] = Mod(ScrollAffix::Hallowed, 400, 400);

	const RunModMultipliers out = AggregateRunMods(set);
	EXPECT_EQ(out.monsterHpPct, 200);
	EXPECT_EQ(out.shrinesPct, 200);
	EXPECT_EQ(out.goldPct, 100) << "plain boons are not capped";
	EXPECT_EQ(out.xpPct, 100);
}

TEST(ScrollModsTest, LootBoonsAreCapped)
{
	ScrollModSet set {};
	set.count = 3;
	set.mods[0] = Mod(ScrollAffix::Tyrannical, 50, 300); // champions
	set.mods[1] = Mod(ScrollAffix::Gilded, 400, 50);      // chests
	set.mods[2] = Mod(ScrollAffix::Teeming, 50, 300);     // extra packs

	const RunModMultipliers out = AggregateRunMods(set);
	EXPECT_EQ(out.championPct, 30);
	EXPECT_EQ(out.chestsPct, 100);
	EXPECT_EQ(out.packPct, 100);
}

TEST(ScrollModsTest, DeepRareScrollsStayWithinCaps)
{
	for (uint32_t seed = 1; seed <= 3000; seed++) {
		const RunModMultipliers out = AggregateRunMods(RollScrollMods(seed, 24, MaxScrollMods));
		ASSERT_LE(out.monsterHpPct, 200) << "seed " << seed;
		ASSERT_LE(out.monsterDmgPct, 150) << "seed " << seed;
		ASSERT_LE(out.bossHpPct, 200) << "seed " << seed;
		ASSERT_LE(out.speedPct, 50) << "seed " << seed;
		ASSERT_LE(out.collapsePct, 150) << "seed " << seed;
		ASSERT_LE(out.elemResistPct, 75) << "seed " << seed;
		ASSERT_LE(out.championPct, 30) << "seed " << seed;
		ASSERT_LE(out.packPct, 100) << "seed " << seed;
		ASSERT_LE(out.chestsPct, 100) << "seed " << seed;
		ASSERT_LE(out.shrinesPct, 200) << "seed " << seed;
	}
}

TEST(ScrollModsTest, JeweledBoonGrowsWithDepth)
{
	long shallow = 0;
	long deep = 0;
	int shallowCount = 0;
	int deepCount = 0;
	for (uint32_t seed = 1; seed <= 5000; seed++) {
		const ScrollModSet atOne = RollScrollMods(seed, 1, MaxScrollMods);
		const ScrollModSet atTwenty = RollScrollMods(seed, 20, MaxScrollMods);
		for (int i = 0; i < atOne.count; i++) {
			if (atOne.mods[i].affix == ScrollAffix::Jeweled) {
				shallow += atOne.mods[i].boonPct;
				shallowCount++;
			}
		}
		for (int i = 0; i < atTwenty.count; i++) {
			if (atTwenty.mods[i].affix == ScrollAffix::Jeweled) {
				deep += atTwenty.mods[i].boonPct;
				deepCount++;
			}
		}
	}
	ASSERT_GT(shallowCount, 50);
	ASSERT_GT(deepCount, 50);
	EXPECT_GT(deep / deepCount, shallow / shallowCount + 10);
}

} // namespace
} // namespace devilution
