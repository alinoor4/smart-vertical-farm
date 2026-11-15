#include "json_io.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

// One macro line per struct: every member is read (a missing key keeps the default) and written.
// Light is stored as the integer 0, 1 or 2, which is nlohmann's default for enums.
// shortcut: nlohmann is lenient about numbers: a fraction is truncated into an int field (4.5 -> 4), true/false load as 1/0,
// a negative seed wraps to a huge one, an integer beyond int range wraps (4294967298 -> 2, which then validates), and a
// misspelt key is ignored. Strings, null, arrays and number-for-string/bool are rejected. Upgrade by checking each
// field's JSON type if a hand-edited file ever hits this.
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
    // isfinite rejects infinity and NaN, which a generator parameter such as --jitter inf can produce (a file cannot: nlohmann rejects 1e999).
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
        expect(c.cycleWeeks <= 104, "cycleWeeks must be at most 104");   // the forecast and the lot arrays are sized by it
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

// ---- plan results -------------------------------------------------------------------------------------------------

namespace {

Json forecastToJson(const ForecastResult& f, const std::string& crop) {
    Json errors = Json::array();
    for (int m = 1; m <= 4; ++m) {
        const ForecastErrors& e = f.holdout[m - 1];
        errors.push_back({{"method", name(static_cast<ForecastMethod>(m))}, {"available", e.available}, {"mae", e.mae}, {"rmse", e.rmse}, {"smape", e.smape}});
    }
    return {{"crop", crop}, {"method", name(f.chosen)}, {"parameters", f.parameters}, {"holdoutLength", f.holdoutLength}, {"holdout", errors}, {"forecast", f.forecast}};
}

Json optionsToJson(const PlanOptions& o) {
    return {{"forecast", name(o.forecast)}, {"knapsack", name(o.knapsack)}, {"activeResource", o.activeResource}, {"bnbNodeLimit", o.bnbNodeLimit},
            {"bnbTimeLimitSeconds", o.bnbTimeLimitSeconds}, {"dpMaxStates", o.dpMaxStates}, {"packer", name(o.packer)},
            {"ga", {{"population", o.ga.population}, {"generations", o.ga.generations}, {"stallGenerations", o.ga.stallGenerations},
                    {"crossoverRate", o.ga.crossoverRate}, {"mutationRate", o.ga.mutationRate}, {"seed", o.ga.seed}}},
            {"refill", o.refill}, {"scheduler", name(o.scheduler)}, {"lookaheadWeeks", o.lookaheadWeeks}};
}

Json metricsToJson(const PlanMetrics& m) {
    return {{"relaxedOptimum", m.relaxedOptimum}, {"planValue", m.planValue}, {"planShare", m.planShare}, {"expectedWeeklyProfit", m.expectedWeeklyProfit},
            {"budgetUse", m.budgetUse}, {"placedArea", m.placedArea}, {"totalShelfArea", m.totalShelfArea}, {"usedShelfArea", m.usedShelfArea},
            {"spaceUtilisation", m.spaceUtilisation}, {"spaceUtilisationOfUsed", m.spaceUtilisationOfUsed}, {"hardTotal", m.hardTotal},
            {"hardOnTime", m.hardOnTime}, {"softTotal", m.softTotal}, {"softServed", m.softServed}, {"idleSlotWeeks", m.idleSlotWeeks},
            {"contractReliability", m.contractReliability}, {"spotService", m.spotService}, {"revenue", m.revenue}, {"penalties", m.penalties}, {"net", m.net}};
}

}  // namespace

std::string planResultToJson(const PlanResult& r, bool includeTiming) {
    Json j;
    j["scenario"] = r.scenarioName;
    j["options"] = optionsToJson(r.options);

    j["forecast"] = Json::array();
    for (size_t c = 0; c < r.forecast.size(); ++c) j["forecast"].push_back(forecastToJson(r.forecast[c], r.cropNames[c]));

    int explicitEdges = 0, heightEdges = 0, noCommonEdges = 0;
    for (size_t a = 0; a < r.constraints.conflict.size(); ++a)
        for (size_t b = a + 1; b < r.constraints.conflict.size(); ++b) {
            const ConflictReason reason = r.constraints.conflict[a][b];
            explicitEdges += reason == ConflictReason::Explicit ? 1 : 0;
            heightEdges += reason == ConflictReason::Height ? 1 : 0;
            noCommonEdges += reason == ConflictReason::NoCommonShelf ? 1 : 0;
        }
    std::vector<int> compatibleShelves;
    for (const std::vector<char>& row : r.constraints.compat) compatibleShelves.push_back(int(std::count(row.begin(), row.end(), char(1))));
    j["constraints"] = {{"pairsTotal", r.constraints.pairsTotal}, {"pairsCompatible", r.constraints.pairsCompatible}, {"compatibleShelvesPerCrop", compatibleShelves},
                        {"conflictEdges", {{"explicit", explicitEdges}, {"height", heightEdges}, {"noCommonShelf", noCommonEdges}}},
                        {"colours", {{"welshPowell", r.colours.welshPowell}, {"dsatur", r.colours.dsatur}, {"exact", r.colours.exact},
                                     {"exactProven", r.colours.exactProven}, {"exactNodes", r.colours.exactNodes}}}};

    j["demand"] = Json::array();
    for (size_t c = 0; c < r.demand.size(); ++c) {
        const CropDemand& d = r.demand[c];
        const int hard = int(std::count_if(d.lots.begin(), d.lots.end(), [](const Lot& l) { return l.hard; }));
        j["demand"].push_back({{"crop", r.cropNames[c]}, {"lots", d.lots.size()}, {"hardLots", hard}, {"contractSlots", d.contractSlots},
                               {"demand", d.demand}, {"penaltyPerWeek", d.penaltyPerWeek}});
    }

    j["knapsack"] = {{"algorithm", name(r.options.knapsack)}, {"copies", [&] { size_t n = 0; for (const KnapsackGroup& g : r.problem.groups) n += g.values.size(); return n; }()},
                     {"value", r.knapsack.value}, {"upperBound", r.knapsack.upperBound}, {"nodes", r.knapsack.nodes}, {"states", r.knapsack.states},
                     {"provenOptimal", r.knapsack.provenOptimal}, {"note", r.knapsack.note}, {"capacity", r.problem.capacity}, {"active", r.problem.active}};

    j["selection"] = Json::array();
    for (size_t c = 0; c < r.rows.size(); ++c) {
        const CropPlan& row = r.rows[c];
        j["selection"].push_back({{"crop", r.cropNames[c]}, {"selected", row.selected}, {"trays", row.trays}, {"area", row.area}, {"weeklyKg", row.weeklyKg},
                                  {"demandKg", row.demandKg}, {"value", row.value}, {"use", row.use}});
    }

    Json placements = Json::array();
    for (const Placement& p : r.layout.placements) placements.push_back(Json::array({p.trayId, p.cropId, p.shelfId, p.x, p.y, p.w, p.d}));
    Json unplaced = Json::array();
    for (const Tray& t : r.pack.unplaced) unplaced.push_back(Json::array({t.id, t.cropId}));
    j["packing"] = {{"algorithm", r.pack.algorithm}, {"placed", r.pack.state.placements.size()}, {"unplaced", unplaced}, {"value", r.pack.placedValue},
                    {"shelvesUsed", r.pack.shelvesUsed}, {"placedArea", r.pack.placedArea}, {"usedShelfArea", r.pack.usedShelfArea},
                    {"totalShelfArea", r.pack.totalShelfArea}, {"generationsRun", r.pack.generationsRun}, {"convergence", r.pack.convergence},
                    {"refilled", r.refilled}, {"finalCount", r.finalCount}, {"planValue", r.metrics.planValue},
                    {"layoutColumns", Json::array({"tray", "crop", "shelf", "x", "y", "w", "d"})}, {"layout", placements}};

    j["schedule"] = Json::array();
    for (const CropSchedule& s : r.schedules) {
        Json batches = Json::array();
        for (const Batch& b : s.batches) batches.push_back(Json::array({b.slot, b.lotId, b.start, b.end}));
        j["schedule"].push_back({{"crop", r.cropNames[s.cropId]}, {"algorithm", name(r.options.scheduler)}, {"slots", s.slots}, {"hardTotal", s.hardTotal},
                                 {"hardOnTime", s.hardOnTime}, {"softTotal", s.softTotal}, {"softServed", s.softServed}, {"idleSlotWeeks", s.idleSlotWeeks},
                                 {"revenue", s.revenue}, {"penalties", s.penalties}, {"note", s.note}, {"batchColumns", Json::array({"slot", "lot", "start", "end"})},
                                 {"batches", batches}});
    }
    j["weeklyUse"] = r.weeklyUse;
    j["metrics"] = metricsToJson(r.metrics);
    j["warnings"] = r.warnings;

    if (includeTiming) {
        Json stages = Json::object();
        for (const auto& s : r.stageSeconds) stages[s.first] = s.second;
        j["timing"] = {{"stages", stages}, {"total", r.totalSeconds}, {"knapsack", r.knapsack.seconds}, {"packing", r.pack.seconds}};
    }
    return j.dump(2) + "\n";
}

namespace {

void writeText(const std::string& text, const std::string& path) {
    std::ofstream out(path, std::ios::binary);   // binary: the same bytes on every operating system
    out << text;
    out.flush();
    if (!out) throw std::runtime_error("cannot write " + path);
}

}  // namespace

void savePlanResult(const PlanResult& result, const std::string& path) { writeText(planResultToJson(result), path); }

std::string compareTableToJson(const CompareTable& table) {
    Json j = Json::array();
    for (const CompareStage& stage : table.stages) {
        Json rows = Json::array();
        for (const CompareRow& row : stage.rows) rows.push_back({{"name", row.name}, {"values", row.values}, {"note", row.note}});
        j.push_back({{"stage", stage.name}, {"columns", stage.columns}, {"rows", rows}});
    }
    return j.dump(2) + "\n";
}

void saveCompareTable(const CompareTable& table, const std::string& path) { writeText(compareTableToJson(table), path); }
