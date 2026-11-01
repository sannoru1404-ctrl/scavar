#include "TestFramework.h"
#include "Csv.h"
#include "GameData.h"
#include "Rng.h"

using namespace Scavar;

TEST(Csv_ParsesQuotedFieldsAndCrlf)
{
    CsvTable t; std::string err;
    CHECK(ParseCsv("a,b,c\r\n1,\"x,y\",\"say \"\"hi\"\"\"\r\n2,,z\r\n", t, &err));
    CHECK_EQ(t.Rows.size(), size_t(2));
    CHECK_EQ(t.Cell(0, "b"), std::string("x,y"));
    CHECK_EQ(t.Cell(0, "c"), std::string("say \"hi\""));
    CHECK_EQ(t.Cell(1, "b"), std::string(""));
}

TEST(Csv_RejectsRaggedRows)
{
    CsvTable t; std::string err;
    CHECK(!ParseCsv("a,b\n1\n", t, &err));
    CHECK(!ParseCsv("a,b\n1,\"oops\n", t, &err));
}

TEST(Csv_HandlesBomAndMissingTrailingNewline)
{
    CsvTable t; std::string err;
    CHECK(ParseCsv("\xEF\xBB\xBFid,v\nA,1", t, &err));
    CHECK_EQ(t.Header[0], std::string("id"));
    CHECK_EQ(t.Rows.size(), size_t(1));
}

TEST(Rng_IsDeterministicAndUniformEnough)
{
    Rng a(42), b(42), c(43);
    for (int i = 0; i < 100; ++i) CHECK_EQ(a.NextU64(), b.NextU64());
    CHECK(a.NextU64() != c.NextU64());
    Rng r(7); int hits = 0;
    for (int i = 0; i < 100000; ++i) hits += r.Chance(0.25) ? 1 : 0;
    CHECK_NEAR(hits / 100000.0, 0.25, 0.01);
    for (int i = 0; i < 1000; ++i) { int v = r.RangeInt(3, 5); CHECK(v >= 3 && v <= 5); }
}

TEST(GameData_LoadsRealTablesFromRepo)
{
    GameData d; std::string err;
    CHECK(d.Load(SCAVAR_DATA_DIR, &err));
    if (!err.empty()) std::printf("    load error: %s\n", err.c_str());
    CHECK_EQ(d.Ammo.size(), size_t(12));
    CHECK_EQ(d.Weapons.size(), size_t(16));
    CHECK_EQ(d.Armor.size(), size_t(5));
    CHECK_EQ(d.Pois.size(), size_t(27));
    const AmmoDef* a = d.FindAmmo("A_545_PS");
    CHECK(a != nullptr);
    if (a) { CHECK_NEAR(a->Damage, 45, 1e-6); CHECK_EQ(a->PenTier, 3); }
    const PoiDef* p = d.FindPoi("POI_003");
    CHECK(p != nullptr);
    if (p) { CHECK_NEAR(p->XKm, 5.10, 1e-9); CHECK(p->Tier == RiskTier::R0); }
    const WeaponDef* w = d.FindWeapon("W_AKM");
    CHECK(w != nullptr);
    if (w) CHECK_EQ(w->FireModes.size(), size_t(2));
}

TEST(GameData_RejectsBrokenData)
{
    GameData d; std::string err;
    CHECK(!d.Load("/nonexistent-dir", &err));
    CHECK(!err.empty());
}
