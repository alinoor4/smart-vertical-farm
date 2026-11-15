// The pipeline (plan section 12) and the comparison tables of section 13. The flat scenario is checked end to end against plan
// Appendix A, G6; the default scenario under every combination of the options; the warnings one by one.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "farm.h"
#include "generator.h"
#include "json_io.h"
#include "pipeline.h"

namespace {

using Counts = std::vector<int>;

Scenario defaultScenario() { return generateScenario(GeneratorParams()); }

// A generated farm with three racks (seed 6, 12 crops) is too small for its selection: the packer drops trays and the refill has work.
Scenario crowdedFarm() {
    GeneratorParams params;
    params.seed = 6;
    params.crops = 12;
    params.racks = 3;
    return generateScenario(params);
}

// Every crop fits every shelf and may share it with any other, so that only the conflicts a test adds remain.
Scenario openFarm() {
    Scenario scenario = flatScenario();
    scenario.conflicts.clear();
    for (Crop& c : scenario.crops) {
        c.trayW = 20; c.trayD = 20; c.height = 20;
        c.lightMin = Light::Low; c.lightMax = Light::High; c.humidityMin = 0; c.humidityMax = 100;
    }
    for (Shelf& s : scenario.shelves) s.clearance = 1000;
    return scenario;
}

void addConflict(Scenario& scenario, int a, int b) {
    ConflictPair pair;
    pair.a = a;
    pair.b = b;
    scenario.conflicts.push_back(pair);
}

// A generated farm of 10 crops on 3 racks (seed 3), where a First-Fit and a Best-Fit refill of the First-Fit layout differ.
Scenario policyFarm() {
    GeneratorParams params;
    params.seed = 3;
    params.crops = 10;
    params.racks = 3;
    return generateScenario(params);
}

bool hasExactly(const PlanResult& r, const std::string& text) { return std::find(r.warnings.begin(), r.warnings.end(), text) != r.warnings.end(); }

bool hasWarning(const PlanResult& r, const std::string& part) {
    return std::any_of(r.warnings.begin(), r.warnings.end(), [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

// The schedule totals of the crops that have slots, the view the plan's G6 table uses.
struct SlotTotals { int hard = 0, hardOk = 0, soft = 0, softOk = 0, idle = 0; double revenue = 0, penalties = 0; };
SlotTotals totalsWithSlots(const PlanResult& r) {
    SlotTotals t;
    for (const CropSchedule& s : r.schedules) {
        if (s.slots == 0) continue;
        t.hard += s.hardTotal; t.hardOk += s.hardOnTime; t.soft += s.softTotal; t.softOk += s.softServed; t.idle += s.idleSlotWeeks;
        t.revenue += s.revenue; t.penalties += s.penalties;
    }
    return t;
}

// What the final trays use of one resource in the most loaded week, from the batches.
double peakUse(const PlanResult& r, int resource) {
    double peak = 0;
    for (const std::array<double, 4>& week : r.weeklyUse) peak = std::max(peak, week[resource]);
    return peak;
}

}  // namespace

// G6 with the plan's default options (branch and bound, First-Fit Decreasing, refill, EDF with lookahead).
TEST(pipeline_flat_farm_first_fit) {
    const Scenario scenario = flatScenario();
    const PlanResult r = runPipeline(scenario, PlanOptions());
    const PlanMetrics& m = r.metrics;

    // Selection: 102 trays, the relaxed optimum proven.
    CHECK_NEAR(m.relaxedOptimum, 39486.5, 1e-6);
    CHECK(r.knapsack.provenOptimal && r.knapsack.nodes < 10000);
    CHECK((r.selected == Counts{10, 24, 11, 0, 8, 16, 0, 0, 14, 6, 13, 0}));

    // Packing: 94 of 102, Kale 1 and Arugula 7 unplaced; the refill adds nothing.
    CHECK(r.pack.algorithm == "First-Fit Decreasing" && r.pack.state.placements.size() == 94 && r.pack.unplaced.size() == 8);
    CHECK_NEAR(r.pack.placedValue, 36858.5, 1e-6);
    CHECK(r.pack.shelvesUsed == 16 && r.pack.placedArea == 185290);
    CHECK(r.refilled == 0 && r.layout.placements.size() == 94);
    CHECK((r.finalCount == Counts{10, 24, 10, 0, 1, 16, 0, 0, 14, 6, 13, 0}));
    CHECK_NEAR(m.planValue, 36858.5, 1e-6);
    CHECK_NEAR(100 * m.planShare, 93.34, 0.005);
    CHECK_NEAR(100 * m.spaceUtilisation, 86.10, 0.005);

    // The weekly profit without the contract bonus: the bonus of the trays that serve contracts, 216 for each of the 10 Lettuce
    // trays, 210 for each of the 4 Baby Spinach, 337.5 for Basil, 450 for Microgreens and 525 for the 10 Strawberry trays.
    CHECK_NEAR(m.expectedWeeklyProfit, 36858.5 - (2160 + 840 + 1350 + 1800 + 5250), 1e-6);

    // Schedule on this plan, lookahead: 84 of 84 contracts, 223 of 330 spot lots of the crops that have slots.
    SlotTotals t = totalsWithSlots(r);
    CHECK(t.hardOk == 84 && t.hard == 84 && t.softOk == 223 && t.soft == 330 && t.idle == 140);
    CHECK_NEAR(t.revenue, 678480, 1e-6);
    CHECK_NEAR(t.penalties, 0, 1e-9);
    // The pipeline reports all crops: Pak Choi, Mint, Coriander and Cherry Tomato have lots but no slots, 112 more spot lots.
    CHECK(m.hardTotal == 84 && m.hardOnTime == 84 && m.softTotal == 442 && m.softServed == 223 && m.idleSlotWeeks == 140);
    CHECK_NEAR(m.net, 678480, 1e-6);
    CHECK_NEAR(m.contractReliability, 1.0, 1e-12);
    CHECK_NEAR(m.spotService, 223.0 / 442, 1e-12);

    // Budget use of the final plan, computed again from the crops: the plan holds 94 trays.
    double use[kResources] = {0, 0, 0, 0, 0};
    for (const Crop& c : scenario.crops) {
        const double n = r.finalCount[c.id];
        use[0] += n * c.laborPerWeek; use[1] += n * c.waterPerWeek; use[2] += n * c.nutrientPerWeek; use[3] += n * c.energyPerWeek;
        use[4] += n * c.trayW * c.trayD;
    }
    const double capacity[kResources] = {21, 800, 8.5, 400, 215200};
    for (int i = 0; i < kResources; ++i) CHECK_NEAR(m.budgetUse[i], use[i] / capacity[i], 1e-9);
    CHECK_NEAR(100 * m.budgetUse[0], 93.90, 0.005);   // 19.72 of 21 hours

    // Warnings: the two crops that lost trays, nothing else.
    CHECK(r.warnings.size() == 2 && hasWarning(r, "1 tray of crop Kale did not fit and was dropped") && hasWarning(r, "7 trays of crop Arugula"));
    // The weekly resource use never exceeds the budget (plan 12.2, step 11), and all four resources are in use.
    const double budget[4] = {21, 800, 8.5, 400};
    for (int i = 0; i < 4; ++i) CHECK(peakUse(r, i) <= budget[i] + EPS && peakUse(r, i) > 0);
    CHECK(r.weeklyUse.size() == 12);
    // The record: every stage timed, the options kept, one forecast, row and schedule per crop.
    CHECK(r.stageSeconds.size() == 7 && r.stageSeconds[0].first == "forecast" && r.stageSeconds[6].first == "schedule" && r.totalSeconds > 0);
    CHECK(r.forecast.size() == 12 && r.rows.size() == 12 && r.schedules.size() == 12 && r.cropNames[2] == "Kale" && r.scenarioName == scenario.name);
    CHECK(r.colours.welshPowell == 5 && r.colours.dsatur == 5 && r.colours.exact == 5 && r.colours.exactProven);
    CHECK(r.constraints.pairsCompatible == 100 && r.constraints.pairsTotal == 192);
}

// G6 with Best-Fit Decreasing: the refill adds one Coriander tray, 96 trays and a value of 37 300.5.
TEST(pipeline_flat_farm_best_fit_and_refill) {
    PlanOptions options;
    options.packer = PackAlgo::BestFitDecreasing;
    const PlanResult r = runPipeline(flatScenario(), options);
    CHECK(r.pack.state.placements.size() == 95 && r.pack.unplaced.size() == 7);
    CHECK_NEAR(r.pack.placedValue, 37203.0, 1e-6);
    CHECK(r.refilled == 1 && r.layout.placements.size() == 96);
    CHECK((r.finalCount == Counts{10, 24, 10, 0, 2, 16, 0, 1, 14, 6, 13, 0}));
    CHECK_NEAR(r.metrics.planValue, 37300.5, 1e-6);
    CHECK_NEAR(100 * r.metrics.planShare, 94.46, 0.005);
    CHECK(r.pack.state.placements.size() + 1 == r.layout.placements.size());   // the packer's own result is kept as it was
    // The new tray got an id from 1 000 000, and the layout is valid.
    int fresh = 0;
    for (const Placement& p : r.layout.placements) fresh += p.trayId >= 1000000 ? 1 : 0;
    CHECK(fresh == 1 && validatePacking(r.layout, flatScenario(), r.constraints).empty());

    // Without the refill the plan is the packer's own.
    options.refill = false;
    const PlanResult plain = runPipeline(flatScenario(), options);
    CHECK(plain.refilled == 0 && plain.layout.placements.size() == 95 && (plain.finalCount == Counts{10, 24, 10, 0, 2, 16, 0, 0, 14, 6, 13, 0}));
    CHECK_NEAR(plain.metrics.planValue, 37203.0, 1e-6);
}

// G6, EDF on the First-Fit plan: 67 of 84 contracts on time, a net of 651 150.
TEST(pipeline_flat_farm_edf) {
    PlanOptions options;
    options.scheduler = SchedAlgo::Edf;
    const PlanResult r = runPipeline(flatScenario(), options);
    const SlotTotals t = totalsWithSlots(r);
    CHECK(t.hardOk == 67 && t.hard == 84 && t.softOk == 246 && t.soft == 330 && t.idle == 102);
    CHECK_NEAR(t.revenue, 690360, 1e-6);
    CHECK_NEAR(t.penalties, 39210, 1e-6);
    CHECK_NEAR(r.metrics.net, 651150, 1e-6);
    CHECK_NEAR(r.metrics.contractReliability, 67.0 / 84, 1e-12);
    // The window of the lookahead through the pipeline: 0 weeks is plain EDF's 67, 1 week 69, the default 84.
    options.scheduler = SchedAlgo::EdfLookahead;
    for (const auto& [window, hard] : std::vector<std::pair<int, int>>{{0, 67}, {1, 69}, {-1, 84}, {100, 84}}) {
        options.lookaheadWeeks = window;
        CHECK(runPipeline(flatScenario(), options).metrics.hardOnTime == hard);
    }
}

// The default scenario under every combination of the three algorithm choices: a valid packing, no budget exceeded, nothing
// but valid warnings, and the same result on a second run.
TEST(pipeline_default_scenario_every_combination) {
    const Scenario scenario = defaultScenario();
    const KnapsackAlgo knapsacks[] = {KnapsackAlgo::Greedy, KnapsackAlgo::Dp, KnapsackAlgo::BranchAndBound};
    const PackAlgo packers[] = {PackAlgo::FirstFitDecreasing, PackAlgo::BestFitDecreasing, PackAlgo::Genetic};
    const SchedAlgo schedulers[] = {SchedAlgo::Edf, SchedAlgo::EdfLookahead, SchedAlgo::Exact};
    int runs = 0;
    for (KnapsackAlgo k : knapsacks)
        for (PackAlgo p : packers)
            for (SchedAlgo s : schedulers) {
                PlanOptions options;
                options.knapsack = k;
                options.packer = p;
                options.scheduler = s;
                options.dpMaxStates = 20000;   // a coarse grid: the full table takes seconds and is tested on its own
                const PlanResult r = runPipeline(scenario, options);
                const PlanResult again = runPipeline(scenario, options);
                ++runs;
                CHECK(validatePacking(r.layout, scenario, r.constraints).empty());
                CHECK(!hasWarning(r, "BUG"));
                // No budget exceeded: the final trays against the budgets, and the busiest week against the weekly budgets.
                for (int i = 0; i < kResources; ++i) CHECK(r.metrics.budgetUse[i] <= 1.0 + 1e-9);
                const double budget[4] = {scenario.budget.labor, scenario.budget.water, scenario.budget.nutrient, scenario.budget.energy};
                for (int i = 0; i < 4; ++i) CHECK(peakUse(r, i) <= budget[i] + EPS);
                CHECK(r.metrics.planValue <= r.metrics.relaxedOptimum + 1e-6 || k != KnapsackAlgo::BranchAndBound);
                // The same result twice: everything but the clock.
                CHECK(planResultToJson(r, false) == planResultToJson(again, false));
                if (k == KnapsackAlgo::BranchAndBound && p == PackAlgo::Genetic && s == SchedAlgo::EdfLookahead)
                    CHECK(r.pack.generationsRun > 0 && !r.pack.convergence.empty());
            }
    CHECK(runs == 27);
}

TEST(pipeline_empty_and_degenerate_scenarios) {
    // Zero budgets: nothing can be planted. The plan is empty, every contract is missed and every crop with contracts is warned about.
    Scenario broke = flatScenario();
    broke.budget = Resources();
    const PlanResult none = runPipeline(broke, PlanOptions());
    CHECK(none.layout.placements.empty() && none.metrics.planValue == 0 && none.metrics.relaxedOptimum == 0);
    CHECK(none.metrics.hardOnTime == 0 && none.metrics.hardTotal == 84 && none.metrics.contractReliability == 0);
    CHECK_NEAR(none.metrics.penalties, 12 * (2160 + 840 + 1350 + 1800 + 5250), 1e-6);   // the penalty of every contract lot: 12 weeks of the penalties a week
    CHECK(hasWarning(none, "Lettuce has a contract that needs 10 slots but the plan gives it 0"));
    CHECK(none.metrics.idleSlotWeeks == 0 && none.metrics.spaceUtilisation == 0);
    for (int i = 0; i < kResources; ++i) CHECK(none.metrics.budgetUse[i] == 0);
    CHECK(validatePacking(none.layout, broke, none.constraints).empty());

    // No contracts: only spot lots, and the plan is still a plan.
    Scenario noContracts = flatScenario();
    noContracts.contracts.clear();
    const PlanResult spotOnly = runPipeline(noContracts, PlanOptions());
    CHECK(spotOnly.metrics.hardTotal == 0 && spotOnly.metrics.contractReliability == 1.0 && spotOnly.metrics.penalties == 0);
    CHECK(spotOnly.layout.placements.size() > 50 && spotOnly.metrics.net > 0);
    CHECK(!hasWarning(spotOnly, "has a contract"));
    for (const CropDemand& d : spotOnly.demand) CHECK(d.contractSlots == 0);

    // No orders at all: no lots, nothing can be missed, and both service levels are 1.
    Scenario quiet = flatScenario();
    quiet.contracts.clear();
    for (std::vector<double>& series : quiet.orderHistory) series.assign(series.size(), 0.0);
    const PlanResult idle = runPipeline(quiet, PlanOptions());
    CHECK(idle.metrics.hardTotal == 0 && idle.metrics.softTotal == 0 && idle.metrics.spotService == 1.0 && idle.metrics.contractReliability == 1.0);
    for (const Crop& c : quiet.crops) CHECK(idle.schedules[c.id].cropId == c.id && idle.schedules[c.id].batches.empty());   // no lots, still its own schedule

    // One resource switched off: the area. The selection overshoots, the packer drops what does not fit.
    PlanOptions noArea;
    noArea.activeResource[4] = false;
    const PlanResult loose = runPipeline(flatScenario(), noArea);
    CHECK(!loose.problem.active[4] && loose.knapsack.value >= 39486.5 - 1e-6);
    CHECK(validatePacking(loose.layout, flatScenario(), loose.constraints).empty());
}

TEST(pipeline_warnings) {
    const Scenario flat = flatScenario();
    // A forced Holt-Winters needs two seasons of history; with 100 weeks and a season of 52 it cannot be fitted.
    Scenario shortHistory = flat;
    for (std::vector<double>& series : shortHistory.orderHistory) series.resize(100);
    PlanOptions forced;
    forced.forecast = ForecastMethod::HoltWinters;
    PlanResult r = runPipeline(shortHistory, forced);
    CHECK(hasWarning(r, "the forced forecast method Holt-Winters was unavailable for crop Lettuce; "));
    CHECK(std::count_if(r.warnings.begin(), r.warnings.end(), [](const std::string& w) { return w.find("forced forecast") != std::string::npos; }) == 12);
    CHECK(runPipeline(shortHistory, PlanOptions()).warnings.size() == 2);   // automatic: nothing to warn about forecasting
    CHECK(!hasWarning(runPipeline(flat, forced), "forced forecast"));       // 156 weeks are enough

    // Branch and bound stopped at its limit, with the gap to the bound.
    PlanOptions limited;
    limited.bnbNodeLimit = 3;
    r = runPipeline(flat, limited);
    CHECK(!r.knapsack.provenOptimal && hasWarning(r, "branch and bound stopped at its limit; the plan is within "));
    CHECK(hasWarning(r, "% of the bound 39486.5"));
    {   // the gap is (bound - value) / bound, in percent with two decimals
        const double bound = r.knapsack.upperBound, gap = 100 * (bound - r.knapsack.value) / bound;
        char text[200];
        std::snprintf(text, sizeof text, "branch and bound stopped at its limit; the plan is within %.2f%% of the bound %.1f", gap, bound);
        CHECK(gap > 0 && hasExactly(r, text));
    }
    CHECK(!hasWarning(r, "DP grid"));   // that warning belongs to the DP
    CHECK(!hasWarning(runPipeline(flat, PlanOptions()), "branch and bound stopped"));
    PlanOptions greedy;
    greedy.knapsack = KnapsackAlgo::Greedy;
    CHECK(!hasWarning(runPipeline(flat, greedy), "branch and bound stopped"));   // a heuristic is not a search that was stopped

    // The DP grid coarsened.
    PlanOptions dp;
    dp.knapsack = KnapsackAlgo::Dp;
    dp.dpMaxStates = 100000;
    r = runPipeline(flat, dp);
    CHECK(hasWarning(r, "the DP grid was coarsened, so its result is approximate (grid coarsened to 9 steps per resource)"));
    dp.activeResource = {{true, false, false, false, false}};   // one resource: an exact table
    CHECK(!hasWarning(runPipeline(flat, dp), "coarsened"));

    // A crop that fits no shelf, with the checks that fail. Cherry Tomato is 100 cm tall; make Kale taller than every clearance.
    Scenario tall = flat;
    tall.crops[2].height = 500;
    r = runPipeline(tall, PlanOptions());
    CHECK(hasWarning(r, "crop Kale has no compatible shelf: clearance fails on 16 of 16"));
    CHECK(r.finalCount[2] == 0);

    // The checks that fail are named, one check at a time: every shelf gets low light and 50 % humidity, and three crops are made
    // to fail on the light, on the humidity (Basil is also too tall for 6 shelves) and on the footprint (Mint, 4 shelves too low).
    Scenario blocked = flat;
    for (Shelf& s : blocked.shelves) { s.light = Light::Low; s.humidity = 50; }
    Crop& pakChoi = blocked.crops[3];
    pakChoi.lightMin = Light::High; pakChoi.lightMax = Light::High; pakChoi.humidityMin = 40; pakChoi.humidityMax = 60;
    Crop& basil = blocked.crops[5];
    basil.lightMin = Light::Low; basil.lightMax = Light::Low; basil.humidityMin = 80; basil.humidityMax = 95;
    Crop& mint = blocked.crops[6];
    mint.lightMin = Light::Low; mint.lightMax = Light::Low; mint.humidityMin = 40; mint.humidityMax = 60; mint.trayW = 3000;
    r = runPipeline(blocked, PlanOptions());
    CHECK(hasExactly(r, "crop Pak Choi has no compatible shelf: light fails on 16 of 16"));
    CHECK(hasExactly(r, "crop Basil has no compatible shelf: clearance fails on 6 of 16, humidity fails on 16 of 16"));
    CHECK(hasExactly(r, "crop Mint has no compatible shelf: clearance fails on 4 of 16, footprint fails on 16 of 16"));

    // Contract slots the plan cannot give: a labour budget of 6 hours cannot carry Lettuce's 10 contract trays and the rest.
    Scenario thin = flat;
    thin.budget.labor = 6;
    r = runPipeline(thin, PlanOptions());
    CHECK(hasWarning(r, "has a contract that needs"));
    for (const Crop& c : thin.crops) CHECK((r.demand[c.id].contractSlots > r.finalCount[c.id]) == hasWarning(r, "crop " + c.name + " has a contract that needs"));

    // More colour groups than shelves: three selected crops that may not share a shelf, and two shelves.
    const int trio[3] = {0, 1, 4};   // Lettuce, Baby Spinach and Arugula are all in the selection
    Scenario crowded = openFarm();
    crowded.shelves.resize(2);
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b) addConflict(crowded, trio[a], trio[b]);
    r = runPipeline(crowded, PlanOptions());
    CHECK(hasExactly(r, "the selected crops need 3 colour groups but there are only 2 shelves"));
    // As many shelves as colour groups is enough. Crop 11 conflicts with all three but is not selected: the whole conflict graph
    // needs 4 colours, the selected crops 3, and only the selected crops count.
    Scenario enough = openFarm();
    enough.shelves.resize(3);
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b) addConflict(enough, trio[a], trio[b]);
    for (int a = 0; a < 3; ++a) addConflict(enough, trio[a], 11);
    r = runPipeline(enough, PlanOptions());
    CHECK(r.selected[11] == 0 && r.selected[0] > 0 && r.selected[1] > 0 && r.selected[4] > 0 && r.colours.exact == 4);
    CHECK(!hasWarning(r, "colour groups"));

    // The three colourings through the pipeline: on the crown graph (crops 0 to 5) Welsh-Powell needs 3 colours, DSATUR and the exact search 2.
    Scenario crown = openFarm();
    const int edges[6][2] = {{0, 3}, {0, 5}, {2, 1}, {2, 5}, {4, 1}, {4, 3}};
    for (const auto& e : edges) addConflict(crown, e[0], e[1]);
    r = runPipeline(crown, PlanOptions());
    CHECK(r.colours.welshPowell == 3 && r.colours.dsatur == 2 && r.colours.exact == 2 && r.colours.exactProven);

    // The exact scheduler cannot take crops with many lots.
    PlanOptions exact;
    exact.scheduler = SchedAlgo::Exact;
    r = runPipeline(flat, exact);
    CHECK(hasWarning(r, "the exact scheduler was skipped for crop Lettuce: too many lots for the exact search"));
    int skipped = 0;
    for (size_t i = 0; i < r.schedules.size(); ++i)   // every crop that was skipped is named in its own warning
        if (!r.schedules[i].note.empty()) {
            ++skipped;
            CHECK(hasExactly(r, "the exact scheduler was skipped for crop " + flat.crops[i].name + ": " + r.schedules[i].note));
        }
    CHECK(skipped >= 2);
}

TEST(pipeline_rejects_bad_input) {
    Scenario broken = flatScenario();
    broken.budget.labor = -1;
    broken.crops[0].yieldKg = 0;
    bool threw = false;
    try {
        runPipeline(broken, PlanOptions());
    } catch (const std::invalid_argument& e) {
        threw = std::string(e.what()).find("invalid scenario:") == 0 && std::string(e.what()).find("budget.labor") != std::string::npos;
    }
    CHECK(threw);
    CHECK_THROWS(compareAll(broken, PlanOptions()));
    // Only the first five problems are listed; the rest are counted.
    Scenario many = flatScenario();
    many.budget.labor = many.budget.water = many.budget.nutrient = many.budget.energy = -1;
    for (int c = 0; c < 4; ++c) many.crops[c].yieldKg = 0;
    const size_t problems = validateScenario(many).size();
    CHECK(problems > 5);
    threw = false;
    try {
        runPipeline(many, PlanOptions());
    } catch (const std::invalid_argument& e) {
        threw = std::string(e.what()).find("\n  ... and " + std::to_string(problems - 5) + " more") != std::string::npos;
    }
    CHECK(threw);
    PlanOptions badGa;
    badGa.packer = PackAlgo::Genetic;
    badGa.ga.population = 2;
    CHECK_THROWS(runPipeline(flatScenario(), badGa));
    PlanOptions badLookahead;
    badLookahead.lookaheadWeeks = -5;
    CHECK_THROWS(runPipeline(flatScenario(), badLookahead));
}

TEST(pipeline_result_json) {
    const PlanResult r = runPipeline(flatScenario(), PlanOptions());
    const std::string withTime = planResultToJson(r, true), without = planResultToJson(r, false);
    CHECK(without.find("\"timing\"") == std::string::npos && withTime.find("\"timing\"") != std::string::npos);
    CHECK(without.find("\"relaxedOptimum\": 39486.") != std::string::npos);
    CHECK(without.find("\"scenario\": \"Sample farm, seed 42") != std::string::npos);
    CHECK(withTime.size() > without.size());
    CHECK(!without.empty() && without.front() == '{' && without.back() == '\n');
    // Written and read back, the file is the text.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "farm_test_plan_result.json";
    savePlanResult(r, path.string());
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(path);
    CHECK(text.find("\"relaxedOptimum\": 39486.") != std::string::npos && text.find("\"timing\"") != std::string::npos && text.find('\r') == std::string::npos);
    CHECK_THROWS(savePlanResult(r, (std::filesystem::temp_directory_path() / "no_such_folder_for_farm" / "x.json").string()));
}

// compareAll on the flat farm: one table per stage, the numbers of plan G6.
TEST(pipeline_compare_all_flat_farm) {
    PlanOptions options;
    options.dpMaxStates = 200000;   // a coarser grid than the plan's, so that the table is quick
    const CompareTable table = compareAll(flatScenario(), options);
    CHECK(table.stages.size() == 5);
    if (table.stages.size() != 5) return;
    const CompareStage &forecast = table.stages[0], &colouring = table.stages[1], &knapsack = table.stages[2], &packing = table.stages[3], &scheduling = table.stages[4];
    CHECK(forecast.name == "Forecast" && colouring.name == "Colouring" && knapsack.name == "Crop selection" && packing.name == "Packing" && scheduling.name == "Scheduling");
    for (const CompareStage& stage : table.stages)
        for (const CompareRow& row : stage.rows) CHECK(row.values.size() == stage.columns.size());

    CHECK(forecast.rows.size() == 4 && forecast.rows[0].name == "seasonal naive");
    CHECK(forecast.rows[0].values[0] == 12 && forecast.rows[0].values[4] == 12 && forecast.rows[1].values[4] == 0);   // seasonal naive chosen for all 12
    CHECK_NEAR(forecast.rows[0].values[1], 0.0, 1e-9);                                                                // flat history: no error

    CHECK(colouring.rows.size() == 3 && colouring.rows[0].values[0] == 5 && colouring.rows[1].values[0] == 5 && colouring.rows[2].values[0] == 5 && colouring.rows[2].values[1] == 1);

    CHECK(knapsack.rows.size() == 3 && knapsack.rows[0].name == "greedy" && knapsack.rows[1].name == "dynamic programming" && knapsack.rows[2].name == "branch and bound");
    CHECK_NEAR(knapsack.rows[0].values[0], 38941.5, 1e-6);
    CHECK_NEAR(knapsack.rows[2].values[0], 39486.5, 1e-6);
    CHECK(knapsack.rows[2].values[5] == 1 && knapsack.rows[2].values[3] > 1000 && knapsack.rows[2].values[3] < 10000);   // proven, 2269 nodes
    CHECK_NEAR(knapsack.rows[2].values[1], 100.0, 1e-9);
    CHECK(knapsack.rows[1].values[0] < knapsack.rows[0].values[0] && knapsack.rows[1].values[4] > 0 && knapsack.rows[1].note.find("coarsened") != std::string::npos);

    CHECK(packing.rows.size() == 3 && packing.rows[0].values[0] == 102);
    CHECK(packing.rows[0].values[1] == 94 && packing.rows[1].values[1] == 95 && packing.rows[2].values[1] >= 95);
    CHECK_NEAR(packing.rows[0].values[2], 36858.5, 1e-6);
    CHECK_NEAR(packing.rows[1].values[2], 37203.0, 1e-6);
    CHECK(packing.rows[2].values[2] >= 37203.0 && packing.rows[0].values[3] == 16);
    CHECK_NEAR(packing.rows[0].values[4], 86.10, 0.005);

    CHECK(scheduling.rows.size() == 3 && scheduling.rows[0].name == "EDF" && scheduling.rows[1].name == "EDF with lookahead" && scheduling.rows[2].name == "exact");
    CHECK(scheduling.rows[0].values[0] == 67 && scheduling.rows[0].values[1] == 84 && scheduling.rows[0].values[2] == 246 && scheduling.rows[0].values[4] == 102);
    CHECK(scheduling.rows[1].values[0] == 84 && scheduling.rows[1].values[2] == 223 && scheduling.rows[1].values[4] == 140);
    CHECK_NEAR(scheduling.rows[0].values[7], 651150, 1e-6);
    CHECK_NEAR(scheduling.rows[1].values[7], 678480, 1e-6);
    CHECK(scheduling.rows[2].values[7] >= scheduling.rows[1].values[7] - 1e-6 && !scheduling.rows[2].note.empty());   // some crops are too big for the exact search

    // The tables as JSON, with every stage in it.
    const std::string json = compareTableToJson(table);
    for (const CompareStage& stage : table.stages) CHECK(json.find("\"stage\": \"" + stage.name + "\"") != std::string::npos);
    CHECK(json.front() == '[');
}

// The forecast horizon is T - 1 + the longest cycle of all crops, whichever crop comes last.
TEST(pipeline_forecast_horizon) {
    Scenario scenario = flatScenario();
    scenario.crops.back().cycleWeeks = 2;
    int longest = 0;
    for (const Crop& c : scenario.crops) longest = std::max(longest, c.cycleWeeks);
    CHECK(longest > 2);
    const PlanResult r = runPipeline(scenario, PlanOptions());
    for (const ForecastResult& f : r.forecast) CHECK(int(f.forecast.size()) == scenario.horizonWeeks - 1 + longest);
}

// The schedules and the weekly profile: every crop has its own schedule, and the profile is what the batches use.
TEST(pipeline_schedule_ids_and_weekly_profile) {
    const Scenario scenario = flatScenario();
    const PlanResult r = runPipeline(scenario, PlanOptions());
    for (const Crop& c : scenario.crops) {
        CHECK(r.schedules[c.id].cropId == c.id);
        for (const Batch& b : r.schedules[c.id].batches) CHECK(b.cropId == c.id);
    }
    for (int week = 0; week < scenario.horizonWeeks; ++week) {
        double labor = 0, water = 0, nutrient = 0, energy = 0;
        for (const CropSchedule& s : r.schedules)
            for (const Batch& b : s.batches)
                if (b.start <= week && week < b.end) {
                    const Crop& c = scenario.crops[b.cropId];
                    labor += c.laborPerWeek; water += c.waterPerWeek; nutrient += c.nutrientPerWeek; energy += c.energyPerWeek;
                }
        CHECK_NEAR(r.weeklyUse[week][0], labor, 1e-9);
        CHECK_NEAR(r.weeklyUse[week][1], water, 1e-9);
        CHECK_NEAR(r.weeklyUse[week][2], nutrient, 1e-9);
        CHECK_NEAR(r.weeklyUse[week][3], energy, 1e-9);
    }
}

// A generated farm with three racks (seed 6) is too small for its selection: the packer drops 13 Microgreens trays and the refill
// spends the freed budgets on 22 more trays. The First-Fit and Best-Fit plans and their schedules are the reference model's; the
// genetic plan has no reference and is pinned.
TEST(pipeline_refill_on_a_crowded_farm) {
    const Scenario scenario = crowdedFarm();
    const size_t crops = scenario.crops.size();
    const PlanResult ffd = runPipeline(scenario, PlanOptions());
    const PlanMetrics& m = ffd.metrics;
    CHECK(ffd.knapsack.provenOptimal && ffd.knapsack.nodes == 1407);
    CHECK_NEAR(m.relaxedOptimum, 32637.534165, 1e-5);
    CHECK((ffd.selected == Counts{10, 5, 2, 0, 7, 14, 0, 0, 15, 6, 10, 0}));
    CHECK(ffd.pack.state.placements.size() == 56 && ffd.pack.unplaced.size() == 13 && ffd.pack.shelvesUsed == 10);
    CHECK_NEAR(ffd.pack.placedValue, 25887.534165, 1e-5);
    CHECK(ffd.refilled == 22 && ffd.layout.placements.size() == 78);
    CHECK((ffd.finalCount == Counts{10, 22, 2, 0, 7, 14, 1, 2, 2, 6, 12, 0}));
    CHECK_NEAR(m.planValue, 29866.534165, 1e-5);
    CHECK(validatePacking(ffd.layout, scenario, ffd.constraints).empty());
    // The new trays have the ids 1 000 000 to 1 000 021.
    std::vector<int> fresh;
    for (const Placement& p : ffd.layout.placements)
        if (p.trayId >= 1000000) fresh.push_back(p.trayId);
    std::sort(fresh.begin(), fresh.end());
    bool consecutive = fresh.size() == 22;
    for (size_t i = 0; i < fresh.size() && consecutive; ++i) consecutive = fresh[i] == 1000000 + int(i);
    CHECK(consecutive);
    // Warnings: the dropped trays, and the contract that needs more slots than the plan gives (4 slots, 15 selected, 2 placed).
    CHECK(ffd.warnings.size() == 2);
    CHECK(hasExactly(ffd, "13 trays of crop Microgreens did not fit and were dropped"));
    CHECK(hasExactly(ffd, "crop Microgreens has a contract that needs 4 slots but the plan gives it 2"));

    // The plan table, computed again from the crops and the tray values.
    const CropValues values = valuesByCrop(ffd.problem, crops);
    double bonus = 0;
    for (const Crop& c : scenario.crops) {
        const CropPlan& row = ffd.rows[c.id];
        const int n = ffd.finalCount[c.id];
        CHECK(row.selected == ffd.selected[c.id] && row.trays == n);
        CHECK(row.area == static_cast<long long>(n) * c.trayW * c.trayD);
        CHECK_NEAR(row.weeklyKg, n * c.yieldKg / c.cycleWeeks, 1e-9);
        CHECK_NEAR(row.demandKg, ffd.demand[c.id].demand, 1e-12);
        CHECK(ffd.demand[c.id].demand > 0);
        double value = 0;
        for (int k = 0; k < n && k < int(values[c.id].size()); ++k) value += values[c.id][k];
        CHECK_NEAR(row.value, value, 1e-9);
        CHECK_NEAR(row.use[0], n * c.laborPerWeek, 1e-9);
        CHECK_NEAR(row.use[3], n * c.energyPerWeek, 1e-9);
        CHECK_NEAR(row.use[4], static_cast<double>(row.area), 1e-9);
        const int slots = ffd.demand[c.id].contractSlots;
        if (slots > 0) bonus += std::min(n, slots) * ffd.demand[c.id].penaltyPerWeek / slots;
    }
    CHECK_NEAR(m.expectedWeeklyProfit, m.planValue - bonus, 1e-6);
    CHECK(ffd.rows[8].trays < ffd.demand[8].contractSlots);   // Microgreens: fewer trays than contract slots, so only part of its bonus counts

    // Schedules on this plan: the lookahead (the default) and EDF.
    CHECK(m.hardOnTime == 72 && m.hardTotal == 84 && m.softServed == 164 && m.softTotal == 446 && m.idleSlotWeeks == 127);
    CHECK_NEAR(m.revenue, 525720, 1e-6);
    CHECK_NEAR(m.penalties, 10800, 1e-6);
    CHECK_NEAR(m.net, 514920, 1e-6);
    PlanOptions edf;
    edf.scheduler = SchedAlgo::Edf;
    const PlanMetrics e = runPipeline(scenario, edf).metrics;
    CHECK(e.hardOnTime == 54 && e.hardTotal == 84 && e.softServed == 189 && e.softTotal == 446 && e.idleSlotWeeks == 86);
    CHECK_NEAR(e.revenue, 546000, 1e-6);
    CHECK_NEAR(e.penalties, 55260, 1e-6);

    // Without the refill the plan is the packer's: 56 trays on 10 of the 12 shelves, and the space figures follow.
    PlanOptions plain;
    plain.refill = false;
    const PlanResult bare = runPipeline(scenario, plain);
    CHECK(bare.refilled == 0 && bare.layout.placements.size() == 56 && bare.finalCount == bare.pack.placedPerCrop);
    long long total = 0, used = 0;
    for (const Shelf& s : scenario.shelves) {
        const long long area = static_cast<long long>(s.width) * s.depth;
        total += area;
        const bool occupied = std::any_of(bare.layout.placements.begin(), bare.layout.placements.end(), [&](const Placement& p) { return p.shelfId == s.id; });
        if (occupied) used += area;
    }
    CHECK(bare.metrics.totalShelfArea == total && bare.metrics.usedShelfArea == used && used < total);
    CHECK_NEAR(bare.metrics.spaceUtilisation, double(bare.metrics.placedArea) / total, 1e-12);
    CHECK_NEAR(bare.metrics.spaceUtilisationOfUsed, double(bare.metrics.placedArea) / used, 1e-12);

    // Best-Fit Decreasing refills Best-Fit: 23 trays.
    PlanOptions bfdOptions;
    bfdOptions.packer = PackAlgo::BestFitDecreasing;
    const PlanResult bfd = runPipeline(scenario, bfdOptions);
    CHECK(bfd.pack.state.placements.size() == 56 && bfd.pack.shelvesUsed == 9 && bfd.refilled == 23);
    CHECK((bfd.finalCount == Counts{10, 22, 2, 0, 7, 14, 1, 2, 2, 6, 13, 0}));
    CHECK_NEAR(bfd.metrics.planValue, 30190.534165, 1e-5);
    CHECK(bfd.metrics.hardOnTime == 72 && bfd.metrics.softServed == 166 && bfd.metrics.idleSlotWeeks == 127);
    CHECK_NEAR(bfd.metrics.net, 531720, 1e-6);

    // The genetic packer places 65 and refills First-Fit: 8 more trays. Pinned from this program, there is no reference for it.
    PlanOptions gaOptions;
    gaOptions.packer = PackAlgo::Genetic;
    const PlanResult ga = runPipeline(scenario, gaOptions);
    CHECK(ga.pack.state.placements.size() == 65 && ga.pack.unplaced.size() == 4 && ga.refilled == 8);
    CHECK((ga.finalCount == Counts{10, 13, 2, 0, 7, 14, 0, 0, 15, 2, 10, 0}));
    CHECK_NEAR(ga.metrics.planValue, 32347.534165, 1e-5);
}

// The refill places like the packer does. After First-Fit Decreasing the refill adds 20 trays (the reference model's number); a Best-Fit
// refill of the same layout would add 21.
TEST(pipeline_refill_follows_the_packer) {
    const PlanResult r = runPipeline(policyFarm(), PlanOptions());
    CHECK((r.selected == Counts{10, 7, 12, 0, 9, 18, 0, 0, 15, 6}));
    CHECK(r.pack.state.placements.size() == 64 && r.refilled == 20);
    CHECK((r.finalCount == Counts{10, 23, 12, 0, 10, 19, 0, 2, 2, 6}));
    CHECK_NEAR(r.metrics.planValue, 25280.5155, 1e-3);
    CHECK(r.metrics.hardOnTime == 60 && r.metrics.hardTotal == 72 && r.metrics.softServed == 203 && r.metrics.softTotal == 429 && r.metrics.idleSlotWeeks == 132);
    CHECK_NEAR(r.metrics.net, 486480, 1e-6);
}

// compareAll on the crowded farm schedules the First-Fit plan after its refill; on a short history the Holt-Winters row has no crops.
TEST(pipeline_compare_all_details) {
    PlanOptions options;
    options.dpMaxStates = 20000;
    const CompareTable table = compareAll(crowdedFarm(), options);
    CHECK(table.stages.size() == 5);
    if (table.stages.size() != 5) return;
    const CompareStage& scheduling = table.stages[4];
    CHECK(scheduling.rows.size() == 3);
    if (scheduling.rows.size() != 3) return;
    const std::vector<double> edf = {54, 84, 189, 446, 86, 546000, 55260, 490740};
    const std::vector<double> lookahead = {72, 84, 164, 446, 127, 525720, 10800, 514920};
    for (size_t i = 0; i < edf.size(); ++i) {
        CHECK_NEAR(scheduling.rows[0].values[i], edf[i], 1e-6);
        CHECK_NEAR(scheduling.rows[1].values[i], lookahead[i], 1e-6);
    }
    // The First-Fit plan of the 10-crop farm after its First-Fit refill (+20 trays).
    const CompareTable policyTable = compareAll(policyFarm(), options);
    const CompareStage& policy = policyTable.stages[4];
    const std::vector<double> policyEdf = {48, 72, 221, 429, 116, 509160, 23760, 485400};
    const std::vector<double> policyLookahead = {60, 72, 203, 429, 132, 497280, 10800, 486480};
    for (size_t i = 0; i < policyEdf.size(); ++i) {
        CHECK_NEAR(policy.rows[0].values[i], policyEdf[i], 1e-6);
        CHECK_NEAR(policy.rows[1].values[i], policyLookahead[i], 1e-6);
    }
    options.refill = false;   // the packer's own plan has fewer trays
    const CompareTable plain = compareAll(crowdedFarm(), options);
    CHECK(plain.stages.size() == 5 && (plain.stages[4].rows[0].values[0] != 54 || plain.stages[4].rows[0].values[4] != 86 || plain.stages[4].rows[0].values[7] != 490740));

    Scenario shortHistory = flatScenario();
    for (std::vector<double>& series : shortHistory.orderHistory) series.resize(100);
    const CompareTable shortTable = compareAll(shortHistory, options);
    const CompareStage& forecast = shortTable.stages[0];
    CHECK(forecast.rows.size() == 4 && forecast.rows[3].name == "Holt-Winters");
    CHECK(forecast.rows[0].values[0] == 12 && forecast.rows[3].values[0] == 0 && forecast.rows[3].values[1] == 0);   // 100 weeks are not two seasons
}
