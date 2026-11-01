#include "TestFramework.h"
#include "Items.h"

using namespace Scavar;

namespace
{
struct Loaded { GameData Game; ItemTable Table; bool Ok = false; std::string Err; };
Loaded LoadAll()
{
    Loaded l;
    l.Ok = l.Game.Load(SCAVAR_DATA_DIR, &l.Err) && l.Table.Load(SCAVAR_DATA_DIR, l.Game, &l.Err);
    if (!l.Ok) std::printf("    load error: %s\n", l.Err.c_str());
    return l;
}
int CountCategory(const ItemTable& t, const char* c) { int n = 0; for (const auto& r : t.Rows) n += r.Category == c; return n; }
}

TEST(Items_SliceMatchesContentBudget)
{
    Loaded l = LoadAll();
    CHECK(l.Ok);
    CHECK_EQ(l.Table.Rows.size(), size_t(40));
    CHECK_EQ(CountCategory(l.Table, "weapon"), 5);
    CHECK_EQ(CountCategory(l.Table, "ammo"), 4);
    CHECK_EQ(CountCategory(l.Table, "magazine"), 2);
    CHECK_EQ(CountCategory(l.Table, "armor"), 1);
    CHECK_EQ(CountCategory(l.Table, "medical"), 4);
    CHECK_EQ(CountCategory(l.Table, "food"), 6);
    CHECK_EQ(CountCategory(l.Table, "clothing"), 6);
    CHECK_EQ(CountCategory(l.Table, "pack"), 2);
    CHECK_EQ(CountCategory(l.Table, "tool"), 7);
    CHECK_EQ(CountCategory(l.Table, "misc"), 3);
}

TEST(Items_BuildCatalogAndLootTypes)
{
    Loaded l = LoadAll();
    CHECK(l.Ok);
    ItemCatalog catalog;
    CHECK(l.Table.BuildCatalog(catalog));
    const ItemDef* pm = catalog.Find("W_PM");
    CHECK(pm != nullptr);
    if (pm) { CHECK_EQ(pm->W, 2); CHECK_EQ(pm->H, 1); }
    CHECK(catalog.Find("NOPE") == nullptr);

    const auto types = l.Table.BuildLootTypes(l.Game, Release::Slice);
    int weapons = 0;
    for (const auto& t : types)
    {
        CHECK(t.Nominal >= t.Min && t.Nominal > 0);
        CHECK(!t.UsageTags.empty());
        weapons += t.IsWeapon ? 1 : 0;
    }
    CHECK_EQ(weapons, 4);              // knife is melee and is not counted against the 15% weapon cap
    CHECK(types.size() < l.Table.Rows.size()); // crafting-only items (nominal 0) are not lootable
}

TEST(Items_PacksHaveGridsAndNothingElseDoes)
{
    Loaded l = LoadAll();
    CHECK(l.Ok);
    const ItemRow* school = l.Table.Find("PACK_SCHOOL");
    CHECK(school != nullptr);
    if (school) { CHECK_EQ(school->GridW, 4); CHECK_EQ(school->GridH, 5); }
    for (const auto& r : l.Table.Rows) CHECK((r.Category == "pack") == (r.GridW > 0));
}
