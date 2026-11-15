// Tests for model, rng, generator and json_io (plan section 16.2).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"
#include "generator.h"
#include "json_io.h"
#include "rng.h"

// ---- Field-by-field comparison. These lists are written independently of json_io.cpp on purpose: a field
// forgotten there is silently lost by a save/load round trip, and only a separate list can notice.
static auto key(const Crop& c) {
    return std::tie(c.id, c.name, c.category, c.trayW, c.trayD, c.canRotate, c.height, c.cycleWeeks, c.harvestWindowWeeks,
                    c.lightMin, c.lightMax, c.humidityMin, c.humidityMax, c.laborPerWeek, c.waterPerWeek, c.nutrientPerWeek,
                    c.energyPerWeek, c.yieldKg, c.pricePerKg, c.trayCost, c.salvageFraction);
}
static auto key(const Shelf& s) { return std::tie(s.id, s.name, s.rack, s.level, s.width, s.depth, s.clearance, s.light, s.humidity); }
static auto key(const Resources& r) { return std::tie(r.labor, r.water, r.nutrient, r.energy); }
static auto key(const Contract& k) { return std::tie(k.id, k.customer, k.cropId, k.kgPerWeek, k.startWeek, k.endWeek, k.penaltyPerKg); }
static auto key(const ConflictPair& p) { return std::tie(p.a, p.b, p.reason); }

template <class T> static void expectSameItems(const std::vector<T>& a, const std::vector<T>& b) {
    CHECK(a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(key(a[i]) == key(b[i]));
}

static void expectSame(const Scenario& a, const Scenario& b) {
    CHECK(std::tie(a.name, a.seed, a.currency, a.horizonWeeks, a.seasonLength, a.lightGapCm, a.heightRatioLimit) ==
          std::tie(b.name, b.seed, b.currency, b.horizonWeeks, b.seasonLength, b.lightGapCm, b.heightRatioLimit));
    CHECK(key(a.budget) == key(b.budget));
    CHECK(key(a.unitCost) == key(b.unitCost));
    expectSameItems(a.crops, b.crops);
    expectSameItems(a.shelves, b.shelves);
    expectSameItems(a.contracts, b.contracts);
    expectSameItems(a.conflicts, b.conflicts);
    CHECK(a.orderHistory == b.orderHistory);
}

static std::string tempPath(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }

static std::string writeTemp(const char* name, const std::string& text) {
    const std::string path = tempPath(name);
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

static GeneratorParams flatParams() {
    GeneratorParams p;
    p.flat = true;
    return p;
}

TEST(rng_seed_42_matches_the_published_values) {
    Rng rng(42);
    CHECK(rng.next() == 13679457532755275413ull);
    CHECK(rng.next() == 2949826092126892291ull);
    CHECK(rng.next() == 5139283748462763858ull);
}

TEST(default_farm_has_the_planned_shape) {
    const Scenario s = generateScenario(GeneratorParams());
    CHECK(s.crops.size() == 12);
    CHECK(s.shelves.size() == 16);
    CHECK(s.contracts.size() == 5);
    CHECK(s.conflicts.size() == 8);
    long long area = 0;
    for (const Shelf& shelf : s.shelves) area += (long long)shelf.width * shelf.depth;
    CHECK(area == 215200);                                    // section 6.2
    CHECK(s.shelves[3].name == "R1-L4" && s.shelves[15].name == "R4-L4");
    CHECK(s.crops[11].name == "Cherry Tomato" && s.crops[11].height == 70 && s.crops[11].cycleWeeks == 12);
    CHECK_NEAR(s.budget.labor, 21.0, 1e-12);
    CHECK_NEAR(s.budget.nutrient, 8.5, 1e-12);
    // Section 6.3: trays per week times the crop's yield, from the first week a harvest is possible.
    const double kg[5] = {3.6, 0.9, 0.6, 3.5, 1.2};
    const int crop[5] = {0, 5, 8, 10, 1};
    for (int i = 0; i < 5; ++i) {
        CHECK_NEAR(s.contracts[i].kgPerWeek, kg[i], 1e-12);
        CHECK(s.contracts[i].cropId == crop[i]);
        CHECK(s.contracts[i].startWeek == s.crops[crop[i]].cycleWeeks);
    }
    for (const std::vector<double>& series : s.orderHistory) CHECK(series.size() == 156);
    CHECK(validateScenario(s).empty());
}

TEST(generator_is_deterministic_and_seed_sensitive) {
    GeneratorParams p;
    p.crops = 24;
    p.jitter = 0.1;
    expectSame(generateScenario(p), generateScenario(p));
    GeneratorParams q = p;
    q.seed = 43;
    CHECK(generateScenario(p).orderHistory != generateScenario(q).orderHistory);
}

TEST(flat_history_is_the_base_demand) {
    const Scenario s = generateScenario(flatParams());
    CHECK(std::count(s.orderHistory[0].begin(), s.orderHistory[0].end(), 9.0) == 156);      // Lettuce
    CHECK(std::count(s.orderHistory[9].begin(), s.orderHistory[9].end(), 1.35) == 156);     // Pea Shoots
    CHECK(generateScenario(GeneratorParams()).orderHistory[0] != s.orderHistory[0]);
}

TEST(more_racks_and_crops_scale_the_farm) {
    GeneratorParams p;
    p.racks = 8;
    p.crops = 24;
    const Scenario s = generateScenario(p);
    CHECK(s.shelves.size() == 32);
    CHECK(s.shelves[16].name == "R5-L1" && s.shelves[16].width == 240);   // rack 5 uses template 1 again
    CHECK_NEAR(s.budget.labor, 42.0, 1e-12);
    CHECK(s.crops[12].name == "Lettuce B" && s.crops[23].name == "Cherry Tomato B");
    CHECK(s.crops[12].yieldKg != s.crops[0].yieldKg);                     // variants are jittered
    CHECK(s.contracts.size() == 5);                                        // only for the first twelve crops
    CHECK(s.conflicts.size() == 26);                                       // 6 herbs x 4 fruiting crops, plus the 2 named pairs
    CHECK(validateScenario(s).empty());

    GeneratorParams few;
    few.crops = 6;                                                         // no Mint, so no named pair; Basil is the last herb
    const Scenario f = generateScenario(few);
    CHECK(f.contracts.size() == 3 && f.conflicts.empty());
    CHECK(validateScenario(f).empty());
}

TEST(committed_scenarios_match_the_generator) {
    const Scenario flat = loadScenario("data/flat_scenario.json");
    const Scenario standard = loadScenario("data/default_scenario.json");
    CHECK(validateScenario(flat).empty());
    CHECK(validateScenario(standard).empty());
    expectSame(generateScenario(flatParams()), flat);                      // guards the file the golden numbers come from
    expectSame(generateScenario(GeneratorParams()), standard);
}

TEST(save_and_load_keep_every_field) {
    GeneratorParams p;
    p.seed = 7;
    p.crops = 24;
    p.jitter = 0.1;
    Scenario a = generateScenario(p);
    // Make every field differ from its default, so a field missing from json_io.cpp cannot hide.
    a.horizonWeeks = 8; a.currency = "USD"; a.seasonLength = 40; a.lightGapCm = 12; a.heightRatioLimit = 2.5;
    a.crops[0].canRotate = false; a.crops[0].salvageFraction = 0.35;
    a.contracts[0].endWeek = 30;
    a.orderHistory[0][0] = 1.23456;                                        // saveScenario rounds the history to two decimals
    const std::string path = tempPath("farm_test_roundtrip.json");
    saveScenario(a, path);
    a.orderHistory[0][0] = 1.23;
    expectSame(a, loadScenario(path));
    std::filesystem::remove(path);
    CHECK_THROWS(saveScenario(a, "data/no_such_folder/x.json"));
}

TEST(load_rejects_bad_files_and_defaults_missing_keys) {
    CHECK_THROWS(loadScenario("data/no_such_file.json"));
    const char* bad[] = {"", "{ not json", "[]", "5", "{\"horizonWeeks\": \"twelve\"}", "{\"crops\": [{\"name\": 5}]}",
                         "{\"crops\": [{\"canRotate\": 1}]}", "{\"horizonWeeks\": null}"};
    for (const char* text : bad) {
        const std::string path = writeTemp("farm_test_bad.json", text);
        CHECK_THROWS(loadScenario(path));
        std::filesystem::remove(path);
    }
    const std::string path = writeTemp("farm_test_empty.json", "{}");
    const Scenario s = loadScenario(path);
    std::filesystem::remove(path);
    CHECK(s.horizonWeeks == 12 && s.currency == "PKR" && s.crops.empty());
    CHECK(!validateScenario(s).empty());                                   // defaults load, but an empty scenario is not usable
}

// Each row breaks one rule of section 5.3 in an otherwise valid scenario and names text that its message must contain.
struct Broken { const char* expect; void (*apply)(Scenario&); };
static const double infinity = std::numeric_limits<double>::infinity();
static const Broken brokenCases[] = {
    {"no crops",              [](Scenario& s) { s.crops.clear(); s.orderHistory.clear(); s.contracts.clear(); s.conflicts.clear(); }},
    {"no shelves",            [](Scenario& s) { s.shelves.clear(); }},
    {"crop 3 (Pak Choi): id must equal", [](Scenario& s) { s.crops[3].id = 9; }},
    {"shelf 2 (R1-L3): id must equal",   [](Scenario& s) { s.shelves[2].id = 9; }},
    {"crop 1 (Lettuce): duplicate",      [](Scenario& s) { s.crops[1].name = s.crops[0].name; }},
    {"shelf 1 (R1-L1): duplicate",       [](Scenario& s) { s.shelves[1].name = s.shelves[0].name; }},
    {"trayW must",            [](Scenario& s) { s.crops[0].trayW = 0; }},
    {"trayD must",            [](Scenario& s) { s.crops[0].trayD = 0; }},
    {"height must",           [](Scenario& s) { s.crops[0].height = 0; }},
    {"cycleWeeks must",       [](Scenario& s) { s.crops[0].cycleWeeks = 0; }},
    {"cycleWeeks must be at most 104", [](Scenario& s) { s.crops[0].cycleWeeks = 105; }},
    {"cycleWeeks must be at most 104", [](Scenario& s) { s.crops[0].cycleWeeks = 2000000000; }},
    {"yieldKg must",          [](Scenario& s) { s.crops[0].yieldKg = 0; }},
    {"width must",            [](Scenario& s) { s.shelves[0].width = 0; }},
    {"depth must",            [](Scenario& s) { s.shelves[0].depth = 0; }},
    {"clearance must",        [](Scenario& s) { s.shelves[0].clearance = 0; }},
    {"yieldKg must",          [](Scenario& s) { s.crops[0].yieldKg = infinity; }},   // e.g. from --jitter inf; a file cannot hold it
    {"harvestWindowWeeks must", [](Scenario& s) { s.crops[0].harvestWindowWeeks = -1; }},
    {"pricePerKg must",       [](Scenario& s) { s.crops[0].pricePerKg = -1; }},
    {"trayCost must",         [](Scenario& s) { s.crops[0].trayCost = -1; }},
    {"laborPerWeek must",     [](Scenario& s) { s.crops[0].laborPerWeek = -1; }},
    {"waterPerWeek must",     [](Scenario& s) { s.crops[0].waterPerWeek = -1; }},
    {"nutrientPerWeek must",  [](Scenario& s) { s.crops[0].nutrientPerWeek = -1; }},
    {"energyPerWeek must",    [](Scenario& s) { s.crops[0].energyPerWeek = -1; }},
    {"budget.labor must",     [](Scenario& s) { s.budget.labor = -1; }},
    {"budget.water must",     [](Scenario& s) { s.budget.water = -1; }},
    {"budget.nutrient must",  [](Scenario& s) { s.budget.nutrient = -1; }},
    {"budget.energy must",    [](Scenario& s) { s.budget.energy = -1; }},
    {"unitCost.labor must",   [](Scenario& s) { s.unitCost.labor = -1; }},
    {"unitCost.water must",   [](Scenario& s) { s.unitCost.water = -1; }},
    {"unitCost.nutrient must", [](Scenario& s) { s.unitCost.nutrient = -1; }},
    {"unitCost.energy must",  [](Scenario& s) { s.unitCost.energy = -1; }},
    {"salvageFraction must",  [](Scenario& s) { s.crops[0].salvageFraction = 1.5; }},
    {"salvageFraction must",  [](Scenario& s) { s.crops[0].salvageFraction = -0.1; }},
    {"lightMin must not exceed", [](Scenario& s) { s.crops[0].lightMin = Light::High; s.crops[0].lightMax = Light::Low; }},
    {"humidityMin must not exceed", [](Scenario& s) { s.crops[0].humidityMin = 90; s.crops[0].humidityMax = 10; }},
    {"crop 0 (Lettuce): lightMin and lightMax must be 0, 1 or 2", [](Scenario& s) { s.crops[0].lightMax = Light(7); }},
    {"shelf 0 (R1-L1): light must be 0, 1 or 2", [](Scenario& s) { s.shelves[0].light = Light(-1); }},
    {"horizonWeeks must",     [](Scenario& s) { s.horizonWeeks = 0; }},
    {"horizonWeeks must",     [](Scenario& s) { s.horizonWeeks = 53; }},
    {"seasonLength must",     [](Scenario& s) { s.seasonLength = 1; }},
    {"heightRatioLimit must", [](Scenario& s) { s.heightRatioLimit = 1.0; }},
    {"contract 0 (Restaurant group (sample)): cropId names a missing crop", [](Scenario& s) { s.contracts[0].cropId = 99; }},
    {"cropId names a missing crop", [](Scenario& s) { s.contracts[0].cropId = -1; }},
    {"startWeek must",        [](Scenario& s) { s.contracts[0].startWeek = 10; s.contracts[0].endWeek = 5; }},
    {"kgPerWeek must",        [](Scenario& s) { s.contracts[0].kgPerWeek = -1; }},
    {"penaltyPerKg must",     [](Scenario& s) { s.contracts[0].penaltyPerKg = -1; }},
    {"conflict 0: names a missing crop", [](Scenario& s) { s.conflicts[0].b = 99; }},
    {"conflict 0: a crop cannot conflict with itself", [](Scenario& s) { s.conflicts[0].b = s.conflicts[0].a; }},
    {"orderHistory: needs one series per crop", [](Scenario& s) { s.orderHistory.pop_back(); }},
    {"orderHistory[0]: is empty", [](Scenario& s) { s.orderHistory[0].clear(); }},
    {"orderHistory[0]: has a negative or non-finite value", [](Scenario& s) { s.orderHistory[0][3] = -1; }},
    {"orderHistory[0]: has a negative or non-finite value", [](Scenario& s) { s.orderHistory[0][3] = infinity; }},
};

TEST(validate_rejects_every_rule) {
    CHECK(validateScenario(generateScenario(GeneratorParams())).empty());   // the baseline the cases break
    for (const Broken& broken : brokenCases) {
        Scenario s = generateScenario(GeneratorParams());
        broken.apply(s);
        bool found = false;
        for (const std::string& message : validateScenario(s)) found = found || message.find(broken.expect) != std::string::npos;
        if (!found) std::printf("validateScenario did not report: %s\n", broken.expect);
        CHECK(found);
    }
}
