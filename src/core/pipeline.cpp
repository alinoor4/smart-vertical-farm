#include "pipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "json_io.h"

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// A number with the given digits after the point, for the warning texts.
std::string number(double value, int digits) {
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", digits, value);
    return text;
}

// ---- steps 1 to 5 -----------------------------------------------------------------------------------------------

// Step 1: a scenario that validateScenario reports on is refused, with the first problems in the message.
void checkScenario(const Scenario& scenario) {
    const std::vector<std::string> problems = validateScenario(scenario);
    if (problems.empty()) return;
    std::string message = "invalid scenario:";
    for (size_t i = 0; i < problems.size() && i < 5; ++i) message += "\n  " + problems[i];
    if (problems.size() > 5) message += "\n  ... and " + std::to_string(problems.size() - 5) + " more";
    throw std::invalid_argument(message);
}

// Step 2: every crop forecast over the longest delivery window the plan can influence, H = T - 1 + the longest cycle.
std::vector<ForecastResult> forecastAll(const Scenario& scenario, ForecastMethod method) {
    int longest = 0;
    for (const Crop& crop : scenario.crops) longest = std::max(longest, crop.cycleWeeks);
    const int horizon = scenario.horizonWeeks - 1 + longest;
    std::vector<ForecastResult> forecasts;
    for (const Crop& crop : scenario.crops)
        forecasts.push_back(forecastSeries(scenario.orderHistory[crop.id], scenario.seasonLength, horizon, method));
    return forecasts;
}

// Step 4: lots, contract slots, demand and penalty per week of every crop.
std::vector<CropDemand> demandAll(const Scenario& scenario, const std::vector<ForecastResult>& forecasts) {
    std::vector<CropDemand> demand;
    for (const Crop& crop : scenario.crops) demand.push_back(buildCropDemand(scenario, crop.id, forecasts[crop.id].forecast));
    return demand;
}

// The colour classes of a conflict graph and how many each algorithm needs.
ColourCounts colourAll(const std::vector<std::vector<int>>& adjacency) {
    auto count = [](const std::vector<int>& colours) { return colours.empty() ? 0 : *std::max_element(colours.begin(), colours.end()) + 1; };
    ColourCounts counts;
    counts.welshPowell = count(colourWelshPowell(adjacency));
    counts.dsatur = count(colourDsatur(adjacency));
    const ExactColouring exact = colourExact(adjacency);
    counts.exact = exact.colours;
    counts.exactProven = exact.proven;
    counts.exactNodes = exact.nodes;
    return counts;
}

// Step 5: the chosen knapsack algorithm on the problem.
KnapsackResult solveSelection(const KnapsackProblem& problem, KnapsackAlgo algo, const PlanOptions& options) {
    if (algo == KnapsackAlgo::Greedy) return solveGreedy(problem);
    if (algo == KnapsackAlgo::Dp) return solveDp(problem, kFarmDpUnit, options.dpMaxStates);
    return solveBranchAndBound(problem, options.bnbNodeLimit, options.bnbTimeLimitSeconds);
}

// ---- steps 6 and 7 ----------------------------------------------------------------------------------------------

PackResult runPacker(PackAlgo algo, const std::vector<Tray>& trays, const Scenario& scenario, const ConstraintModel& constraints,
                     const CropValues& values, const GaParams& ga) {
    if (algo == PackAlgo::FirstFitDecreasing) return packFirstFitDecreasing(trays, scenario, constraints, values);
    if (algo == PackAlgo::BestFitDecreasing) return packBestFitDecreasing(trays, scenario, constraints, values);
    return packGenetic(trays, scenario, constraints, values, ga);
}

// Trays per crop in a layout.
std::vector<int> countPlaced(const PackState& layout, size_t crops) {
    std::vector<int> count(crops, 0);
    for (const Placement& p : layout.placements) ++count[p.cropId];
    return count;
}

// Step 7, refill: spend what is left of the budgets on further trays that verifiably fit. The copies are taken in the
// greedy's order, one scan; a crop whose next tray finds no place is full, because the layout only ever fills up. The new
// trays get ids from 1 000 000. Returns the number of trays added. Time O(N log N + N S (C + R)).
int refillLayout(PackState& layout, const KnapsackProblem& problem, const Scenario& scenario, const ConstraintModel& constraints,
                 FitPolicy policy) {
    const size_t crops = scenario.crops.size();
    std::vector<int> placed = countPlaced(layout, crops);
    std::array<double, kResources> remaining = problem.capacity;
    for (const KnapsackGroup& group : problem.groups)
        for (int r = 0; r < kResources; ++r) remaining[r] -= placed[group.cropId] * group.weight[r];

    std::vector<char> full(crops, 0);
    int nextId = 1000000, added = 0;
    for (const RankedCopy& copy : rankCopies(problem)) {
        const KnapsackGroup& group = problem.groups[copy.group];
        const int crop = group.cropId;
        if (full[crop] || copy.k != placed[crop] + 1) continue;
        bool fits = true;
        for (int r = 0; r < kResources; ++r)
            if (problem.active[r] && group.weight[r] > remaining[r] + EPS) fits = false;
        if (!fits) continue;
        if (placeOne(layout, {nextId, crop}, scenario, constraints, policy)) {
            ++nextId;
            ++placed[crop];
            ++added;
            for (int r = 0; r < kResources; ++r) remaining[r] -= group.weight[r];
        } else {
            full[crop] = 1;
        }
    }
    return added;
}

// ---- step 10 ----------------------------------------------------------------------------------------------------

// Every crop scheduled with as many slots as it has trays. A crop without lots gets an empty schedule, so its trays show as idle.
std::vector<CropSchedule> scheduleAll(const Scenario& scenario, const std::vector<CropDemand>& demand, const std::vector<int>& slots,
                                      SchedAlgo algo, int lookaheadWeeks) {
    std::vector<CropSchedule> schedules;
    for (const Crop& crop : scenario.crops) {
        CropSchedule s = scheduleCrop(demand[crop.id].lots, slots[crop.id], crop.cycleWeeks, scenario.horizonWeeks, algo, lookaheadWeeks);
        s.cropId = crop.id;
        for (Batch& batch : s.batches) batch.cropId = crop.id;
        schedules.push_back(std::move(s));
    }
    return schedules;
}

// The totals of plan 12.4 over a set of schedules.
struct ScheduleTotals {
    int hardTotal = 0, hardOnTime = 0, softTotal = 0, softServed = 0, idleSlotWeeks = 0;
    double revenue = 0, penalties = 0;
};
ScheduleTotals totalsOf(const std::vector<CropSchedule>& schedules) {
    ScheduleTotals t;
    for (const CropSchedule& s : schedules) {
        t.hardTotal += s.hardTotal;
        t.hardOnTime += s.hardOnTime;
        t.softTotal += s.softTotal;
        t.softServed += s.softServed;
        t.idleSlotWeeks += s.idleSlotWeeks;
        t.revenue += s.revenue;
        t.penalties += s.penalties;
    }
    return t;
}

// ---- warnings (plan 12.3) ---------------------------------------------------------------------------------------

// Why a crop has no compatible shelf: on how many shelves each check fails.
std::string failedChecks(const ConstraintModel& constraints, int crop, size_t shelves) {
    int clearance = 0, light = 0, humidity = 0, footprint = 0;
    for (size_t s = 0; s < shelves; ++s) {
        const unsigned char why = constraints.whyNot[crop][s];
        clearance += (why & Clearance) ? 1 : 0;
        light += (why & LightMismatch) ? 1 : 0;
        humidity += (why & HumidityMismatch) ? 1 : 0;
        footprint += (why & Footprint) ? 1 : 0;
    }
    std::string text;
    auto add = [&](int count, const char* check) {
        if (count == 0) return;
        text += (text.empty() ? "" : ", ") + std::string(check) + " fails on " + std::to_string(count) + " of " + std::to_string(shelves);
    };
    add(clearance, "clearance");
    add(light, "light");
    add(humidity, "humidity");
    add(footprint, "footprint");
    return text;
}

}  // namespace

const char* name(ForecastMethod method) {
    switch (method) {
        case ForecastMethod::Auto: return "automatic";
        case ForecastMethod::SeasonalNaive: return "seasonal naive";
        case ForecastMethod::MovingAverage: return "moving average";
        case ForecastMethod::Ses: return "simple exponential smoothing";
        case ForecastMethod::HoltWinters: return "Holt-Winters";
    }
    return "?";
}

const char* name(KnapsackAlgo algo) {
    switch (algo) {
        case KnapsackAlgo::Greedy: return "greedy";
        case KnapsackAlgo::Dp: return "dynamic programming";
        case KnapsackAlgo::BranchAndBound: return "branch and bound";
    }
    return "?";
}

const char* name(PackAlgo algo) {
    switch (algo) {
        case PackAlgo::FirstFitDecreasing: return "First-Fit Decreasing";
        case PackAlgo::BestFitDecreasing: return "Best-Fit Decreasing";
        case PackAlgo::Genetic: return "Genetic algorithm";
    }
    return "?";
}

const char* name(SchedAlgo algo) {
    switch (algo) {
        case SchedAlgo::Edf: return "EDF";
        case SchedAlgo::EdfLookahead: return "EDF with lookahead";
        case SchedAlgo::Exact: return "exact";
    }
    return "?";
}

PlanResult runPipeline(const Scenario& scenario, const PlanOptions& options) {
    const auto begin = Clock::now();
    PlanResult result;
    auto stage = [&](const char* label, Clock::time_point since) { result.stageSeconds.push_back({label, secondsSince(since)}); };

    checkScenario(scenario);                                                       // step 1
    const size_t crops = scenario.crops.size(), shelves = scenario.shelves.size();
    result.scenarioName = scenario.name;
    for (const Crop& crop : scenario.crops) result.cropNames.push_back(crop.name);
    result.options = options;

    auto since = Clock::now();
    result.forecast = forecastAll(scenario, options.forecast);                     // step 2
    stage("forecast", since);

    since = Clock::now();
    result.constraints = buildConstraints(scenario);                               // step 3
    result.colours = colourAll(result.constraints.adjacency);
    stage("constraints", since);

    since = Clock::now();
    result.demand = demandAll(scenario, result.forecast);                          // step 4
    stage("lots", since);

    since = Clock::now();                                                          // step 5
    result.problem = buildKnapsackProblem(scenario, result.demand, result.constraints, options.activeResource);
    result.knapsack = solveSelection(result.problem, options.knapsack, options);
    result.selected = countsByCrop(result.problem, result.knapsack.count, crops);
    stage("knapsack", since);

    since = Clock::now();                                                          // step 6
    const CropValues values = valuesByCrop(result.problem, crops);
    result.pack = runPacker(options.packer, traysForCounts(result.selected), scenario, result.constraints, values, options.ga);
    result.layout = result.pack.state;
    stage("packing", since);

    since = Clock::now();                                                          // step 7
    if (options.refill) {
        const FitPolicy policy = options.packer == PackAlgo::BestFitDecreasing ? FitPolicy::BestFit : FitPolicy::FirstFit;   // the genetic packer refills with First-Fit
        result.refilled = refillLayout(result.layout, result.problem, scenario, result.constraints, policy);
    }
    stage("refill", since);

    result.finalCount = countPlaced(result.layout, crops);                         // step 8
    const double planValue = packFitness(result.layout, scenario, values).placedValue;

    since = Clock::now();
    const std::vector<std::string> bugs = validatePacking(result.layout, scenario, result.constraints);   // step 9
    result.schedules = scheduleAll(scenario, result.demand, result.finalCount, options.scheduler, options.lookaheadWeeks);   // step 10
    stage("schedule", since);

    // Step 11: the weekly resource profile, from the batches that are running in each week.
    result.weeklyUse.assign(scenario.horizonWeeks, std::array<double, 4>{{0, 0, 0, 0}});
    for (const CropSchedule& s : result.schedules) {
        const Crop& crop = scenario.crops[s.cropId];
        const std::array<double, 4> rate = {crop.laborPerWeek, crop.waterPerWeek, crop.nutrientPerWeek, crop.energyPerWeek};
        for (const Batch& batch : s.batches)
            for (int t = batch.start; t < batch.end && t < scenario.horizonWeeks; ++t)
                for (int r = 0; r < 4; ++r) result.weeklyUse[t][r] += rate[r];
    }

    // The selection table and the headline numbers (plan 12.4).
    PlanMetrics& m = result.metrics;
    m.relaxedOptimum = result.knapsack.value;
    m.planValue = planValue;
    m.planShare = m.relaxedOptimum > 0 ? planValue / m.relaxedOptimum : 0.0;
    double bonus = 0;
    result.rows.assign(crops, CropPlan());
    for (const Crop& crop : scenario.crops) {
        CropPlan& row = result.rows[crop.id];
        const CropDemand& market = result.demand[crop.id];
        row.selected = result.selected[crop.id];
        row.trays = result.finalCount[crop.id];
        row.area = static_cast<long long>(row.trays) * crop.trayW * crop.trayD;
        row.weeklyKg = row.trays * crop.yieldKg / crop.cycleWeeks;
        row.demandKg = market.demand;
        for (int k = 0; k < row.trays && k < int(values[crop.id].size()); ++k) row.value += values[crop.id][k];
        row.use = {row.trays * crop.laborPerWeek, row.trays * crop.waterPerWeek, row.trays * crop.nutrientPerWeek,
                   row.trays * crop.energyPerWeek, static_cast<double>(row.area)};
        if (market.contractSlots > 0) bonus += std::min(row.trays, market.contractSlots) * market.penaltyPerWeek / market.contractSlots;
    }
    m.expectedWeeklyProfit = planValue - bonus;
    for (int r = 0; r < kResources; ++r) {
        double used = 0;
        for (const CropPlan& row : result.rows) used += row.use[r];
        m.budgetUse[r] = result.problem.capacity[r] > 0 ? used / result.problem.capacity[r] : 0.0;
    }
    std::vector<long long> shelfArea(shelves, 0);
    for (const Placement& p : result.layout.placements) {
        m.placedArea += static_cast<long long>(p.w) * p.d;
        shelfArea[p.shelfId] += static_cast<long long>(p.w) * p.d;
    }
    for (size_t s = 0; s < shelves; ++s) {
        const long long area = static_cast<long long>(scenario.shelves[s].width) * scenario.shelves[s].depth;
        m.totalShelfArea += area;
        if (shelfArea[s] > 0) m.usedShelfArea += area;
    }
    m.spaceUtilisation = m.totalShelfArea > 0 ? double(m.placedArea) / m.totalShelfArea : 0.0;
    m.spaceUtilisationOfUsed = m.usedShelfArea > 0 ? double(m.placedArea) / m.usedShelfArea : 0.0;
    const ScheduleTotals totals = totalsOf(result.schedules);
    m.hardTotal = totals.hardTotal;
    m.hardOnTime = totals.hardOnTime;
    m.softTotal = totals.softTotal;
    m.softServed = totals.softServed;
    m.idleSlotWeeks = totals.idleSlotWeeks;
    m.contractReliability = totals.hardTotal > 0 ? double(totals.hardOnTime) / totals.hardTotal : 1.0;
    m.spotService = totals.softTotal > 0 ? double(totals.softServed) / totals.softTotal : 1.0;
    m.revenue = totals.revenue;
    m.penalties = totals.penalties;
    m.net = totals.revenue - totals.penalties;

    // Warnings, in the order of the stages.
    std::vector<std::string>& warnings = result.warnings;
    for (const Crop& crop : scenario.crops)
        if (options.forecast != ForecastMethod::Auto && result.forecast[crop.id].chosen != options.forecast)
            warnings.push_back(std::string("the forced forecast method ") + name(options.forecast) + " was unavailable for crop " + crop.name + "; " +
                               name(result.forecast[crop.id].chosen) + " was used");
    for (const Crop& crop : scenario.crops) {
        const std::vector<char>& row = result.constraints.compat[crop.id];
        if (std::find(row.begin(), row.end(), char(1)) == row.end())
            warnings.push_back("crop " + crop.name + " has no compatible shelf: " + failedChecks(result.constraints, crop.id, shelves));
    }
    if (options.knapsack == KnapsackAlgo::BranchAndBound && !result.knapsack.provenOptimal) {
        const double bound = result.knapsack.upperBound;
        const double gap = bound > 0 ? (bound - result.knapsack.value) / bound : 0.0;
        warnings.push_back("branch and bound stopped at its limit; the plan is within " + number(100 * gap, 2) + "% of the bound " + number(bound, 1));
    }
    if (options.knapsack == KnapsackAlgo::Dp && !result.knapsack.note.empty())
        warnings.push_back("the DP grid was coarsened, so its result is approximate (" + result.knapsack.note + ")");
    for (const Crop& crop : scenario.crops) {
        const int dropped = result.selected[crop.id] - result.finalCount[crop.id];
        if (dropped > 0)
            warnings.push_back(std::to_string(dropped) + " tray" + (dropped == 1 ? "" : "s") + " of crop " + crop.name + " did not fit and " + (dropped == 1 ? "was" : "were") + " dropped");
    }
    for (const Crop& crop : scenario.crops) {
        const CropDemand& market = result.demand[crop.id];
        if (market.contractSlots > result.finalCount[crop.id])
            warnings.push_back("crop " + crop.name + " has a contract that needs " + std::to_string(market.contractSlots) + " slots but the plan gives it " +
                               std::to_string(result.finalCount[crop.id]));
    }
    {   // the selected crops need one shelf per colour class at least; if there are fewer shelves, trays are dropped
        std::vector<int> planted;
        for (const Crop& crop : scenario.crops)
            if (result.selected[crop.id] > 0) planted.push_back(crop.id);
        std::vector<int> position(crops, -1);
        for (size_t i = 0; i < planted.size(); ++i) position[planted[i]] = int(i);
        std::vector<std::vector<int>> induced(planted.size());
        for (size_t i = 0; i < planted.size(); ++i)
            for (int other : result.constraints.adjacency[planted[i]])
                if (position[other] >= 0) induced[i].push_back(position[other]);
        const ExactColouring need = colourExact(induced);
        if (need.colours > int(shelves))
            warnings.push_back("the selected crops need " + std::to_string(need.colours) + " colour groups but there are only " + std::to_string(shelves) + " shelves");
    }
    for (const CropSchedule& s : result.schedules)
        if (!s.note.empty()) warnings.push_back("the exact scheduler was skipped for crop " + scenario.crops[s.cropId].name + ": " + s.note);
    for (const std::string& bug : bugs) warnings.push_back("BUG in the packing, please report: " + bug);

    result.totalSeconds = secondsSince(begin);
    return result;
}

// ---- compareAll -------------------------------------------------------------------------------------------------

CompareTable compareAll(const Scenario& scenario, const PlanOptions& options) {
    checkScenario(scenario);
    const size_t crops = scenario.crops.size();
    const std::vector<ForecastResult> forecasts = forecastAll(scenario, options.forecast);
    const ConstraintModel constraints = buildConstraints(scenario);
    const std::vector<CropDemand> demand = demandAll(scenario, forecasts);
    const KnapsackProblem problem = buildKnapsackProblem(scenario, demand, constraints, options.activeResource);
    const CropValues values = valuesByCrop(problem, crops);
    auto milliseconds = [](Clock::time_point since) { return 1000.0 * secondsSince(since); };
    CompareTable table;

    {   // forecast: hold-out errors per method, averaged over the crops where the method could be fitted
        CompareStage stage{"Forecast", {"crops scored", "MAE", "RMSE", "sMAPE %", "chosen for crops"}, {}};
        for (int m = 1; m <= 4; ++m) {
            double mae = 0, rmse = 0, smape = 0;
            int scored = 0, chosen = 0;
            for (const ForecastResult& f : forecasts) {
                const ForecastErrors& e = f.holdout[m - 1];
                if (e.available) { ++scored; mae += e.mae; rmse += e.rmse; smape += e.smape; }
                if (int(f.chosen) == m) ++chosen;
            }
            const double n = scored > 0 ? scored : 1;
            stage.rows.push_back({name(static_cast<ForecastMethod>(m)), {double(scored), mae / n, rmse / n, smape / n, double(chosen)}, ""});
        }
        table.stages.push_back(stage);
    }
    {   // colouring of the conflict graph
        CompareStage stage{"Colouring", {"colour groups", "proven", "nodes", "ms"}, {}};
        auto since = Clock::now();
        const std::vector<int> wp = colourWelshPowell(constraints.adjacency);
        stage.rows.push_back({"Welsh-Powell", {double(*std::max_element(wp.begin(), wp.end()) + 1), 0, 0, milliseconds(since)}, "a heuristic"});
        since = Clock::now();
        const std::vector<int> ds = colourDsatur(constraints.adjacency);
        stage.rows.push_back({"DSATUR", {double(*std::max_element(ds.begin(), ds.end()) + 1), 0, 0, milliseconds(since)}, "a heuristic"});
        since = Clock::now();
        const ExactColouring exact = colourExact(constraints.adjacency);
        stage.rows.push_back({"exact", {double(exact.colours), exact.proven ? 1.0 : 0.0, double(exact.nodes), milliseconds(since)}, exact.proven ? "" : "node limit reached"});
        table.stages.push_back(stage);
    }
    KnapsackResult bnb;
    {   // crop selection
        CompareStage stage{"Crop selection", {"value", "% of best", "bound", "nodes", "states", "proven", "ms"}, {}};
        KnapsackResult results[3];
        double ms[3];
        const KnapsackAlgo algos[3] = {KnapsackAlgo::Greedy, KnapsackAlgo::Dp, KnapsackAlgo::BranchAndBound};
        for (int i = 0; i < 3; ++i) {
            const auto since = Clock::now();
            results[i] = solveSelection(problem, algos[i], options);
            ms[i] = milliseconds(since);
        }
        const double best = std::max({results[0].value, results[1].value, results[2].value});
        for (int i = 0; i < 3; ++i)
            stage.rows.push_back({name(algos[i]), {results[i].value, best > 0 ? 100 * results[i].value / best : 0.0, results[i].upperBound, double(results[i].nodes),
                                                   double(results[i].states), results[i].provenOptimal ? 1.0 : 0.0, ms[i]}, results[i].note});
        bnb = results[2];
        table.stages.push_back(stage);
    }
    const std::vector<int> selected = countsByCrop(problem, bnb.count, crops);
    const std::vector<Tray> trays = traysForCounts(selected);
    PackResult ffd;
    {   // packing, on the branch-and-bound selection, before any refill
        CompareStage stage{"Packing", {"trays", "placed", "value", "shelves used", "utilisation %", "ms"}, {}};
        const PackAlgo algos[3] = {PackAlgo::FirstFitDecreasing, PackAlgo::BestFitDecreasing, PackAlgo::Genetic};
        for (int i = 0; i < 3; ++i) {
            const auto since = Clock::now();
            PackResult r = runPacker(algos[i], trays, scenario, constraints, values, options.ga);
            const double ms = milliseconds(since);
            stage.rows.push_back({name(algos[i]), {double(trays.size()), double(r.state.placements.size()), r.placedValue, double(r.shelvesUsed),
                                                   r.totalShelfArea > 0 ? 100.0 * r.placedArea / r.totalShelfArea : 0.0, ms}, ""});
            if (i == 0) ffd = std::move(r);
        }
        table.stages.push_back(stage);
    }
    {   // scheduling, on the First-Fit Decreasing plan of that selection after the refill
        PackState layout = ffd.state;
        if (options.refill) refillLayout(layout, problem, scenario, constraints, FitPolicy::FirstFit);
        const std::vector<int> finalCount = countPlaced(layout, crops);
        CompareStage stage{"Scheduling", {"hard on time", "hard lots", "soft served", "soft lots", "idle slot-weeks", "revenue", "penalties", "net", "ms"}, {}};
        const SchedAlgo algos[3] = {SchedAlgo::Edf, SchedAlgo::EdfLookahead, SchedAlgo::Exact};
        for (SchedAlgo algo : algos) {
            const auto since = Clock::now();
            const std::vector<CropSchedule> schedules = scheduleAll(scenario, demand, finalCount, algo, options.lookaheadWeeks);
            const double ms = milliseconds(since);
            const ScheduleTotals t = totalsOf(schedules);
            int fallbacks = 0;
            for (const CropSchedule& s : schedules) fallbacks += s.note.empty() ? 0 : 1;
            stage.rows.push_back({name(algo), {double(t.hardOnTime), double(t.hardTotal), double(t.softServed), double(t.softTotal), double(t.idleSlotWeeks),
                                               t.revenue, t.penalties, t.revenue - t.penalties, ms},
                                  fallbacks > 0 ? std::to_string(fallbacks) + " crop(s) showed the lookahead schedule instead" : ""});
        }
        table.stages.push_back(stage);
    }
    return table;
}
