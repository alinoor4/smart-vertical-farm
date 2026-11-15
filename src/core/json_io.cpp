#include "json_io.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

// One macro line per struct: every member is read (a missing key keeps the default) and written.
// Light is stored as the integer 0, 1 or 2, which is nlohmann's default for enums.
// shortcut: nlohmann converts a number with a fraction into an int field by truncating (4.5 -> 4) and true/false into 1/0,
// and a key that is misspelt is ignored; upgrade by checking each field's JSON type if a hand-edited file ever hits this.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Crop, id, name, category, trayW, trayD, canRotate, height, cycleWeeks,
    harvestWindowWeeks, lightMin, lightMax, humidityMin, humidityMax, laborPerWeek, waterPerWeek, nutrientPerWeek,
    energyPerWeek, yieldKg, pricePerKg, trayCost, salvageFraction)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Shelf, id, name, rack, level, width, depth, clearance, light, humidity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Resources, labor, water, nutrient, energy)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Contract, id, customer, cropId, kgPerWeek, startWeek, endWeek, penaltyPerKg)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ConflictPair, a, b, reason)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scenario, name, seed, currency, horizonWeeks, seasonLength, lightGapCm,
    heightRatioLimit, budget, unitCost, crops, shelves, contracts, conflicts, orderHistory)

// ordered_json keeps keys in declaration order, so a saved file starts with the name and seed.
using Json = nlohmann::ordered_json;

Scenario loadScenario(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    try {
        return Json::parse(in).get<Scenario>();
    } catch (const Json::exception& e) {   // nlohmann's exceptions are not std::runtime_error
        throw std::runtime_error(path + ": " + e.what());
    }
}

void saveScenario(const Scenario& scenario, const std::string& path) {
    Json j = scenario;
    for (Json& series : j["orderHistory"])
        for (Json& value : series) value = std::round(value.get<double>() * 100.0) / 100.0;
    std::ofstream out(path, std::ios::binary);   // binary: the same bytes on every operating system
    out << j.dump(2) << '\n';
    out.flush();
    if (!out) throw std::runtime_error("cannot write " + path);
}

std::vector<std::string> validateScenario(const Scenario& s) {
    std::vector<std::string> errors;
    std::string who = "scenario";   // names the item being checked, so every message says where
    auto expect = [&](bool ok, const std::string& what) { if (!ok) errors.push_back(who + ": " + what); };
    // isfinite first: a file may hold 1e999, which parses as infinity.
    auto positive = [&](const std::string& field, double v) { expect(std::isfinite(v) && v > 0, field + " must be positive"); };
    auto nonNegative = [&](const std::string& field, double v) { expect(std::isfinite(v) && v >= 0, field + " must not be negative"); };
    auto lightOk = [](Light l) { return l >= Light::Low && l <= Light::High; };
    auto resources = [&](const std::string& prefix, const Resources& r) {
        nonNegative(prefix + ".labor", r.labor);
        nonNegative(prefix + ".water", r.water);
        nonNegative(prefix + ".nutrient", r.nutrient);
        nonNegative(prefix + ".energy", r.energy);
    };

    expect(!s.crops.empty(), "no crops");
    expect(!s.shelves.empty(), "no shelves");
    expect(s.horizonWeeks >= 1 && s.horizonWeeks <= 52, "horizonWeeks must be in 1..52");
    expect(s.seasonLength >= 2, "seasonLength must be at least 2");
    expect(std::isfinite(s.heightRatioLimit) && s.heightRatioLimit > 1, "heightRatioLimit must be greater than 1");
    resources("budget", s.budget);
    resources("unitCost", s.unitCost);

    std::set<std::string> cropNames;
    for (size_t i = 0; i < s.crops.size(); ++i) {
        const Crop& c = s.crops[i];
        who = "crop " + std::to_string(i) + " (" + c.name + ")";
        expect(c.id == int(i), "id must equal its index " + std::to_string(i));
        expect(cropNames.insert(c.name).second, "duplicate name");
        positive("trayW", c.trayW);
        positive("trayD", c.trayD);
        positive("height", c.height);
        positive("cycleWeeks", c.cycleWeeks);
        positive("yieldKg", c.yieldKg);
        nonNegative("harvestWindowWeeks", c.harvestWindowWeeks);
        nonNegative("pricePerKg", c.pricePerKg);
        nonNegative("trayCost", c.trayCost);
        nonNegative("laborPerWeek", c.laborPerWeek);
        nonNegative("waterPerWeek", c.waterPerWeek);
        nonNegative("nutrientPerWeek", c.nutrientPerWeek);
        nonNegative("energyPerWeek", c.energyPerWeek);
        expect(c.salvageFraction >= 0 && c.salvageFraction <= 1, "salvageFraction must be in 0..1");
        expect(lightOk(c.lightMin) && lightOk(c.lightMax), "lightMin and lightMax must be 0, 1 or 2");
        expect(c.lightMin <= c.lightMax, "lightMin must not exceed lightMax");
        expect(c.humidityMin <= c.humidityMax, "humidityMin must not exceed humidityMax");
    }

    std::set<std::string> shelfNames;
    for (size_t i = 0; i < s.shelves.size(); ++i) {
        const Shelf& sh = s.shelves[i];
        who = "shelf " + std::to_string(i) + " (" + sh.name + ")";
        expect(sh.id == int(i), "id must equal its index " + std::to_string(i));
        expect(shelfNames.insert(sh.name).second, "duplicate name");
        positive("width", sh.width);
        positive("depth", sh.depth);
        positive("clearance", sh.clearance);
        expect(lightOk(sh.light), "light must be 0, 1 or 2");
    }

    const int cropCount = int(s.crops.size());
    for (size_t i = 0; i < s.contracts.size(); ++i) {
        const Contract& k = s.contracts[i];
        who = "contract " + std::to_string(i) + " (" + k.customer + ")";
        expect(k.cropId >= 0 && k.cropId < cropCount, "cropId names a missing crop");
        expect(k.startWeek <= k.endWeek, "startWeek must not exceed endWeek");
        nonNegative("kgPerWeek", k.kgPerWeek);
        nonNegative("penaltyPerKg", k.penaltyPerKg);
    }

    for (size_t i = 0; i < s.conflicts.size(); ++i) {
        const ConflictPair& p = s.conflicts[i];
        who = "conflict " + std::to_string(i);
        expect(p.a >= 0 && p.a < cropCount && p.b >= 0 && p.b < cropCount, "names a missing crop");
        expect(p.a != p.b, "a crop cannot conflict with itself");
    }

    who = "orderHistory";
    expect(s.orderHistory.size() == s.crops.size(), "needs one series per crop");
    for (size_t i = 0; i < s.orderHistory.size(); ++i) {
        const std::vector<double>& series = s.orderHistory[i];
        who = "orderHistory[" + std::to_string(i) + "]";
        expect(!series.empty(), "is empty");
        expect(std::all_of(series.begin(), series.end(), [](double v) { return std::isfinite(v) && v >= 0; }),
               "has a negative or non-finite value");
    }
    return errors;
}
