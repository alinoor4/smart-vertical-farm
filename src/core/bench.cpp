#include "bench.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "constraints.h"
#include "forecast.h"
#include "generator.h"
#include "knapsack.h"
#include "packing.h"
#include "pipeline.h"
#include "rng.h"
#include "scheduling.h"

namespace {

using Clock = std::chrono::steady_clock;
using Rows = std::vector<BenchRow>;

// ---- helpers ----------------------------------------------------------------------------------------------------------

double millisSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Runs `run` and returns the result of its first run; ms is the fastest of up to three runs (plan 15). A run that takes over a
// second is not repeated.
template <class Run> auto measure(Run run, double& ms) {
    auto start = Clock::now();
    auto result = run();
    ms = millisSince(start);
    for (int again = 0; again < 2 && ms <= 1000.0; ++again) {
        start = Clock::now();
        run();
        ms = std::min(ms, millisSince(start));
    }
    return result;
}

// A benchmark that timed a wrong answer would be worthless, so every answer is checked once, outside the timing.
void require(bool ok, const char* what) {
    if (!ok) throw std::logic_error(std::string("benchmark check failed: ") + what);
}

std::vector<int> seedsFor(bool quick) { return quick ? std::vector<int>{1} : std::vector<int>{1, 2, 3, 4, 5}; }

// All the values of a size or a parameter, or the two smallest when quick.
std::vector<int> valuesFor(std::vector<int> all, bool quick) {
    if (quick && all.size() > 2) all.resize(2);
    return all;
}

void addRow(Rows& rows, const char* suite, const char* algo, int n, int param, int seed, double ms, double quality, double aux1 = 0, double aux2 = 0) {
    rows.push_back({suite, algo, n, param, seed, ms, quality, aux1, aux2});
}

double percent(double part, double whole) { return whole > EPS ? 100.0 * part / whole : 100.0; }

// ---- knapsack suites --------------------------------------------------------------------------------------------------

constexpr std::array<double, kResources> kIntegerUnit{{1, 1, 1, 1, 1}};   // every weight of the integer instances is a whole number
constexpr long long kBnbNodes = 5000000;
constexpr double kBnbSeconds = 2.0;                                           // plan 15: branch and bound gets 2 seconds per instance

// The values of one group: the first copy is worth `first`, each further copy 5% less.
std::vector<double> fallingValues(double first, int copies) {
    std::vector<double> values;
    for (int k = 0; k < copies; ++k) {
        values.push_back(first);
        first *= 0.95;
    }
    return values;
}

// Does taking the first count[g] copies of every group stay inside every active capacity?
bool feasible(const KnapsackProblem& problem, const std::vector<int>& count) {
    std::array<double, kResources> used{};
    for (size_t g = 0; g < problem.groups.size(); ++g) {
        if (count[g] < 0 || count[g] > int(problem.groups[g].values.size())) return false;
        for (int r = 0; r < kResources; ++r) used[size_t(r)] += count[g] * problem.groups[g].weight[size_t(r)];
    }
    for (int r = 0; r < kResources; ++r)
        if (problem.active[size_t(r)] && used[size_t(r)] > problem.capacity[size_t(r)] + EPS) return false;
    return true;
}

// knap_1d: one resource, integer weights 1 to 30, groups of 10 copies, capacity half the total weight. A group's first value is its
// weight times a factor between 8 and 12 (plan 15 gives no rule for the values; this makes them weakly correlated with the weights,
// which is the kind of instance that gives branch and bound work).
KnapsackProblem oneResourceProblem(int copies, Rng& rng) {
    KnapsackProblem problem;
    problem.active = {{true, false, false, false, false}};
    double totalWeight = 0;
    for (int g = 0; g < copies / 10; ++g) {
        KnapsackGroup group;
        group.cropId = g;
        group.weight[0] = 1 + rng.below(30);
        const double factor = 8.0 + 4.0 * rng.unit();
        group.values = fallingValues(group.weight[0] * factor, 10);
        totalWeight += 10 * group.weight[0];
        problem.groups.push_back(group);
    }
    problem.capacity[0] = std::floor(totalWeight / 2);   // a whole number, so that the DP grid is exact
    return problem;
}

// knap_dims: 8 groups of 5 copies, integer weights 0 to 6 in each of `resources` resources, every capacity 24. A group's first
// value is (1 + its total weight) times a factor between 8 and 12.
KnapsackProblem manyResourcesProblem(int resources, Rng& rng) {
    KnapsackProblem problem;
    for (int r = 0; r < kResources; ++r) {
        problem.active[size_t(r)] = r < resources;
        problem.capacity[size_t(r)] = r < resources ? 24 : 0;
    }
    for (int g = 0; g < 8; ++g) {
        KnapsackGroup group;
        group.cropId = g;
        double weightSum = 0;
        for (int r = 0; r < resources; ++r) {
            group.weight[size_t(r)] = rng.below(7);
            weightSum += group.weight[size_t(r)];
        }
        const double factor = 8.0 + 4.0 * rng.unit();
        group.values = fallingValues((1 + weightSum) * factor, 5);
        problem.groups.push_back(group);
    }
    return problem;
}

// Greedy, DP and branch and bound on one integer problem. The DP is exact on the integer grid, so each value is given as a
// percentage of the DP's. reportProven: aux2 says whether the algorithm proved its answer (knap_dims leaves it 0).
void integerKnapsackRows(Rows& rows, const char* suite, const KnapsackProblem& problem, int n, int param, int seed, bool reportProven) {
    double greedyMs = 0, dpMs = 0, bnbMs = 0;
    const KnapsackResult greedy = measure([&] { return solveGreedy(problem); }, greedyMs);
    const KnapsackResult dp = measure([&] { return solveDp(problem, kIntegerUnit); }, dpMs);
    const KnapsackResult bnb = measure([&] { return solveBranchAndBound(problem, kBnbNodes, kBnbSeconds); }, bnbMs);
    for (const KnapsackResult* r : {&greedy, &dp, &bnb}) require(feasible(problem, r->count), "a knapsack answer exceeds a capacity");
    require(dp.provenOptimal, "the DP grid is not exact on an integer instance");
    addRow(rows, suite, "greedy", n, param, seed, greedyMs, percent(greedy.value, dp.value), 0, 0);
    addRow(rows, suite, "dp", n, param, seed, dpMs, percent(dp.value, dp.value), double(dp.states), reportProven ? 1 : 0);
    addRow(rows, suite, "bnb", n, param, seed, bnbMs, percent(bnb.value, dp.value), double(bnb.nodes), reportProven && bnb.provenOptimal ? 1 : 0);
}

void runKnap1d(bool quick, Rows& rows) {
    for (int copies : valuesFor({50, 100, 200, 400, 800, 1600}, quick))
        for (int seed : seedsFor(quick)) {
            Rng rng(static_cast<unsigned long long>(seed));
            integerKnapsackRows(rows, "knap_1d", oneResourceProblem(copies, rng), copies, 0, seed, true);
        }
}

void runKnapDims(bool quick, Rows& rows) {
    for (int resources : valuesFor({1, 2, 3, 4}, quick))
        for (int seed : seedsFor(quick)) {
            Rng rng(static_cast<unsigned long long>(seed));
            integerKnapsackRows(rows, "knap_dims", manyResourcesProblem(resources, rng), 40, resources, seed, false);
        }
}

// knap_farm: the knapsack problem the pipeline builds for a generated farm with racks = crops / 3 (steps 1 to 5 of plan 12.2; the
// pipeline is run with the cheap options, because only the problem is wanted).
KnapsackProblem farmProblem(int crops, int seed) {
    GeneratorParams params;
    params.seed = static_cast<unsigned long long>(seed);
    params.crops = crops;
    params.racks = std::max(1, crops / 3);
    PlanOptions options;
    options.knapsack = KnapsackAlgo::Greedy;
    options.scheduler = SchedAlgo::Edf;
    options.refill = false;
    return runPipeline(generateScenario(params), options).problem;
}

void runKnapFarm(bool quick, Rows& rows) {
    for (int crops : valuesFor({6, 12, 18, 24}, quick))
        for (int seed : seedsFor(quick)) {
            const KnapsackProblem problem = farmProblem(crops, seed);
            double greedyMs = 0, bnbMs = 0;
            const KnapsackResult greedy = measure([&] { return solveGreedy(problem); }, greedyMs);
            const KnapsackResult bnb = measure([&] { return solveBranchAndBound(problem, kBnbNodes, kBnbSeconds); }, bnbMs);
            require(feasible(problem, greedy.count) && feasible(problem, bnb.count), "a knapsack answer exceeds a capacity");
            // Without a proof the root bound is all there is to compare with, so both values are given against it.
            addRow(rows, "knap_farm", "greedy", crops, 0, seed, greedyMs, percent(greedy.value, bnb.upperBound), 0, 0);
            addRow(rows, "knap_farm", "bnb", crops, 0, seed, bnbMs, percent(bnb.value, bnb.upperBound), double(bnb.nodes), bnb.provenOptimal ? 1 : 0);
        }
}

// ---- packing suites ---------------------------------------------------------------------------------------------------

// The footprints of the ten tray types (the first ten crops of the catalogue) and the five shelf sizes, in cm.
constexpr int kTraySize[10][2] = {{60, 40}, {60, 30}, {60, 45}, {50, 40}, {45, 30}, {50, 30}, {40, 30}, {40, 25}, {50, 25}, {55, 28}};
constexpr int kShelfSize[5][2] = {{240, 60}, {200, 80}, {180, 50}, {150, 60}, {120, 40}};

struct PackInstance {
    Scenario scenario;             // the ten tray types as crops, the shelves; nothing else is used by the packers
    ConstraintModel constraints;   // only compat and conflict are filled, which is all the packers read
    std::vector<Tray> trays;
    CropValues values;             // a tray is worth its area, so the genetic algorithm's fitness is the placed area
    long long trayArea = 0;
};

// `trays` trays of random types, and shelves of random sizes until the trays fill `fillPercent` % of the shelf area: random shelves
// while more than the largest shelf is still missing, then the one last shelf (or none) that brings the fill closest. Each
// crop-shelf pair is compatible with probability 0.8 and each pair of types conflicts with probability 0.15.
PackInstance packInstance(int trays, int fillPercent, Rng& rng) {
    PackInstance in;
    for (int c = 0; c < 10; ++c) {
        Crop crop;
        crop.id = c;
        crop.name = "tray type " + std::to_string(c + 1);
        crop.trayW = kTraySize[c][0];
        crop.trayD = kTraySize[c][1];
        in.scenario.crops.push_back(crop);
    }
    std::vector<int> perType(10, 0);
    for (int i = 0; i < trays; ++i) {
        const int type = rng.below(10);
        in.trays.push_back({i, type});
        ++perType[size_t(type)];
        in.trayArea += kTraySize[type][0] * kTraySize[type][1];
    }
    long long shelfArea = 0, largest = 0;
    for (const int (&size)[2] : kShelfSize) largest = std::max<long long>(largest, size[0] * size[1]);
    auto addShelf = [&](int size) {
        Shelf shelf;
        shelf.id = int(in.scenario.shelves.size());
        shelf.name = "shelf " + std::to_string(shelf.id + 1);
        shelf.width = kShelfSize[size][0];
        shelf.depth = kShelfSize[size][1];
        in.scenario.shelves.push_back(shelf);
        shelfArea += static_cast<long long>(shelf.width) * shelf.depth;
    };
    const double wanted = double(in.trayArea) * 100.0 / fillPercent;   // the shelf area at which the fill is exactly fillPercent
    while (wanted - double(shelfArea) > double(largest)) addShelf(rng.below(5));
    int last = -1;                                                      // -1: no last shelf
    double bestMiss = wanted - double(shelfArea);                       // adding nothing misses by the whole remainder
    for (int size = 0; size < 5; ++size) {
        const double miss = std::fabs(wanted - double(shelfArea) - double(kShelfSize[size][0]) * kShelfSize[size][1]);
        if (miss < bestMiss) {
            bestMiss = miss;
            last = size;
        }
    }
    if (last >= 0) addShelf(last);
    const size_t shelves = in.scenario.shelves.size();
    in.constraints.compat.assign(10, std::vector<char>(shelves, 0));
    for (size_t c = 0; c < 10; ++c)
        for (size_t s = 0; s < shelves; ++s) in.constraints.compat[c][s] = rng.unit() < 0.8 ? 1 : 0;
    in.constraints.conflict.assign(10, std::vector<ConflictReason>(10, ConflictReason::None));
    for (size_t a = 0; a < 10; ++a)
        for (size_t b = a + 1; b < 10; ++b)
            if (rng.unit() < 0.15) in.constraints.conflict[a][b] = in.constraints.conflict[b][a] = ConflictReason::Explicit;
    for (int c = 0; c < 10; ++c) in.values.push_back(std::vector<double>(size_t(perType[size_t(c)]), double(kTraySize[c][0] * kTraySize[c][1])));
    return in;
}

void runPackSuite(const char* suite, int fillPercent, bool quick, Rows& rows) {
    for (int trays : valuesFor({50, 100, 200, 400, 800}, quick))
        for (int seed : seedsFor(quick)) {
            Rng rng(static_cast<unsigned long long>(seed));
            const PackInstance in = packInstance(trays, fillPercent, rng);
            GaParams ga;
            ga.seed = static_cast<unsigned long long>(seed);
            double ms[3] = {0, 0, 0};
            const PackResult results[3] = {
                measure([&] { return packFirstFitDecreasing(in.trays, in.scenario, in.constraints, in.values); }, ms[0]),
                measure([&] { return packBestFitDecreasing(in.trays, in.scenario, in.constraints, in.values); }, ms[1]),
                measure([&] { return packGenetic(in.trays, in.scenario, in.constraints, in.values, ga); }, ms[2])};
            const char* const names[3] = {"ffd", "bfd", "ga"};
            for (int a = 0; a < 3; ++a) {
                const PackResult& r = results[a];
                require(validatePacking(r.state, in.scenario, in.constraints).empty(), "a layout breaks a rule");
                require(r.state.placements.size() + r.unplaced.size() == in.trays.size(), "a tray is neither placed nor unplaced");
                addRow(rows, suite, names[a], trays, fillPercent, seed, ms[a], percent(double(r.placedArea), double(in.trayArea)), r.shelvesUsed,
                       percent(double(r.placedArea), double(r.usedShelfArea)));
            }
        }
}

void runPackTight(bool quick, Rows& rows) { runPackSuite("pack_tight", 90, quick, rows); }
void runPackLoose(bool quick, Rows& rows) { runPackSuite("pack_loose", 60, quick, rows); }

// ---- scheduling suites ------------------------------------------------------------------------------------------------

constexpr int kSchedCycle = 5, kSchedHorizon = 26;   // plan 15: p = 5, f = 0, T = 26

// The lots of one crop with the same demand in every delivery week: `perWeek` trays, a share `hardShare` of them under contract.
// One lot is one kg and sells for 10; a missed contract kg costs 30. The cumulative rounding of plan 11.2 (inside buildLots) turns
// the fractional weekly demand into whole lots, the same ones on every machine.
std::vector<Lot> steadyLots(int cycle, int horizon, double perWeek, double hardShare) {
    Crop crop;
    crop.id = 0;
    crop.name = "benchmark crop";
    crop.cycleWeeks = cycle;
    crop.harvestWindowWeeks = 0;
    crop.yieldKg = 1;
    crop.pricePerKg = 10;
    const size_t weeks = size_t(horizon - 1 + cycle) + 1;
    std::vector<double> hardKg(weeks, 0.0), penaltyRate(weeks, 30.0), spot(weeks, 0.0);
    for (size_t w = size_t(cycle); w < weeks; ++w) {
        hardKg[w] = hardShare * perWeek;
        spot[w] = (1 - hardShare) * perWeek;
    }
    return buildLots(crop, horizon, hardKg, penaltyRate, spot);
}

// A schedule must start every batch inside its lot's window and before the horizon, give a lot at most one batch, and never
// run two batches on a slot at once.
bool validSchedule(const CropSchedule& s, int cycle, int horizon) {
    std::vector<std::vector<std::pair<int, int>>> bySlot(size_t(std::max(s.slots, 0)));
    std::vector<char> used(s.lots.size(), 0);
    for (const Batch& b : s.batches) {
        if (b.slot < 0 || b.slot >= s.slots || b.lotId < 0 || b.lotId >= int(s.lots.size()) || used[size_t(b.lotId)]) return false;
        used[size_t(b.lotId)] = 1;
        const Lot& lot = s.lots[size_t(b.lotId)];
        if (b.start < lot.release || b.start > lot.latestStart || b.start >= horizon || b.end != b.start + cycle) return false;
        bySlot[size_t(b.slot)].push_back({b.start, b.end});
    }
    for (std::vector<std::pair<int, int>>& batches : bySlot) {
        std::sort(batches.begin(), batches.end());
        for (size_t i = 1; i < batches.size(); ++i)
            if (batches[i].first < batches[i - 1].second) return false;
    }
    return true;
}

CropSchedule timedSchedule(const std::vector<Lot>& lots, int slots, int cycle, int horizon, SchedAlgo algo, int lookahead, double& ms) {
    CropSchedule s = measure([&] { return scheduleCrop(lots, slots, cycle, horizon, algo, lookahead); }, ms);
    require(validSchedule(s, cycle, horizon), "a schedule breaks a rule");
    return s;
}

// The columns of the sched suites: hard lots on time %, soft lots served %, idle slot-weeks %.
void addScheduleRow(Rows& rows, const char* suite, const char* algo, int n, int param, int seed, double ms, const CropSchedule& s, int horizon) {
    addRow(rows, suite, algo, n, param, seed, ms, percent(s.hardOnTime, s.hardTotal), percent(s.softServed, s.softTotal),
           s.slots > 0 ? 100.0 * s.idleSlotWeeks / (double(s.slots) * horizon) : 0.0);
}

void runSched(bool quick, Rows& rows) {
    for (int slots : valuesFor({5, 10, 20, 40, 80}, quick))
        for (int seed : seedsFor(quick)) {
            // The demand is 1.3 times what the slots can deliver: slots / cycle trays a week.
            const std::vector<Lot> lots = steadyLots(kSchedCycle, kSchedHorizon, 1.3 * slots / kSchedCycle, 0.4);
            double edfMs = 0, lookMs = 0;
            const CropSchedule edf = timedSchedule(lots, slots, kSchedCycle, kSchedHorizon, SchedAlgo::Edf, -1, edfMs);
            const CropSchedule look = timedSchedule(lots, slots, kSchedCycle, kSchedHorizon, SchedAlgo::EdfLookahead, -1, lookMs);
            addScheduleRow(rows, "sched", "edf", slots, 0, seed, edfMs, edf, kSchedHorizon);
            addScheduleRow(rows, "sched", "lookahead", slots, 0, seed, lookMs, look, kSchedHorizon);
        }
}

void runSchedWindow(bool quick, Rows& rows) {
    constexpr int slots = 20;
    for (int weeks : valuesFor({0, 1, 2, 3, 5, 10, 26}, quick))
        for (int seed : seedsFor(quick)) {
            const std::vector<Lot> lots = steadyLots(kSchedCycle, kSchedHorizon, 1.3 * slots / kSchedCycle, 0.4);
            double ms = 0;
            const CropSchedule s = timedSchedule(lots, slots, kSchedCycle, kSchedHorizon, SchedAlgo::EdfLookahead, weeks, ms);
            addScheduleRow(rows, "sched_window", "lookahead", slots, weeks, seed, ms, s, kSchedHorizon);
        }
}

void runSchedExact(bool quick, Rows& rows) {
    constexpr int cycle = 2, slots = 2, horizon = 8;
    for (int lotCount : valuesFor({8, 12, 16, 20}, quick))
        for (int seed : seedsFor(quick)) {
            // Delivery weeks 2 to 9 are eight weeks, so lotCount / 8 trays a week gives lotCount lots.
            const std::vector<Lot> lots = steadyLots(cycle, horizon, lotCount / 8.0, 0.4);
            require(int(lots.size()) == lotCount, "the sched_exact instance does not have the lots it is named for");
            double ms[3] = {0, 0, 0};
            const CropSchedule results[3] = {timedSchedule(lots, slots, cycle, horizon, SchedAlgo::Edf, -1, ms[0]),
                                             timedSchedule(lots, slots, cycle, horizon, SchedAlgo::EdfLookahead, -1, ms[1]),
                                             timedSchedule(lots, slots, cycle, horizon, SchedAlgo::Exact, -1, ms[2])};
            // When the exact search stopped early (it says so in its note) it is not an optimum to compare with: quality -1.
            const bool stopped = !results[2].note.empty();
            const double best = results[2].revenue - results[2].penalties;
            const char* const names[3] = {"edf", "lookahead", "exact"};
            for (int a = 0; a < 3; ++a) {
                const double value = results[a].revenue - results[a].penalties;
                addRow(rows, "sched_exact", names[a], lotCount, 0, seed, ms[a], stopped ? -1.0 : percent(value, best));
            }
        }
}

// ---- forecast suite ---------------------------------------------------------------------------------------------------

void runForecast(bool quick, Rows& rows) {
    const ForecastMethod methods[4] = {ForecastMethod::SeasonalNaive, ForecastMethod::MovingAverage, ForecastMethod::Ses, ForecastMethod::HoltWinters};
    const char* const names[4] = {"naive", "ma", "ses", "hw"};
    for (int noise : valuesFor({5, 15, 30}, quick))
        for (int seed : seedsFor(quick)) {
            GeneratorParams params;
            params.seed = static_cast<unsigned long long>(seed);
            params.crops = 24;
            params.racks = 8;
            params.noiseOverride = noise / 100.0;
            const Scenario scenario = generateScenario(params);

            // The library's own hold-out scores of every crop, the training part of each history, and each crop's mean demand.
            std::vector<ForecastResult> scored;
            std::vector<std::vector<double>> training;
            std::vector<double> meanDemand;
            for (const std::vector<double>& history : scenario.orderHistory) {
                scored.push_back(forecastSeries(history, scenario.seasonLength, scenario.horizonWeeks));
                training.emplace_back(history.begin(), history.end() - scored.back().holdoutLength);
                meanDemand.push_back(std::accumulate(history.begin(), history.end(), 0.0) / double(history.size()));
            }
            for (size_t m = 0; m < 4; ++m) {
                double ms = 0;   // the method's fit and forecast on the training part of all 24 histories
                measure([&] {
                    size_t fitted = 0;
                    for (size_t c = 0; c < training.size(); ++c) fitted += forecastWith(methods[m], training[c], scenario.seasonLength, scored[c].holdoutLength).ok ? 1 : 0;
                    return fitted;
                }, ms);
                double mae = 0, rmse = 0, smape = 0;
                int count = 0;
                for (size_t c = 0; c < scored.size(); ++c) {
                    const ForecastErrors& e = scored[c].holdout[m];
                    if (!e.available || meanDemand[c] <= EPS) continue;
                    mae += 100.0 * e.mae / meanDemand[c];
                    rmse += 100.0 * e.rmse / meanDemand[c];
                    smape += e.smape;
                    ++count;
                }
                if (count > 0) addRow(rows, "forecast", names[m], int(scenario.crops.size()), noise, seed, ms, mae / count, rmse / count, smape / count);
            }
        }
}

// ---- colouring suite --------------------------------------------------------------------------------------------------

// Each pair of the `vertices` vertices is joined with probability percentEdges / 100. Neighbour lists come out ascending.
std::vector<std::vector<int>> randomGraph(int vertices, int percentEdges, Rng& rng) {
    std::vector<std::vector<int>> adjacency(static_cast<size_t>(vertices));   // not size_t(vertices): that parses as a function declaration
    for (int a = 0; a < vertices; ++a)
        for (int b = a + 1; b < vertices; ++b)
            if (rng.unit() * 100.0 < percentEdges) {
                adjacency[size_t(a)].push_back(b);
                adjacency[size_t(b)].push_back(a);
            }
    return adjacency;
}

int coloursUsed(const std::vector<int>& colouring) { return colouring.empty() ? 0 : *std::max_element(colouring.begin(), colouring.end()) + 1; }

bool properColouring(const std::vector<std::vector<int>>& adjacency, const std::vector<int>& colour) {
    for (size_t v = 0; v < adjacency.size(); ++v)
        for (int u : adjacency[v])
            if (colour[size_t(u)] == colour[v]) return false;
    return true;
}

void runColouring(bool quick, Rows& rows) {
    for (int vertices : valuesFor({10, 15, 20, 50, 100, 200, 400}, quick))
        for (int edges : {10, 30, 50})
            for (int seed : seedsFor(quick)) {
                Rng rng(static_cast<unsigned long long>(seed));
                const std::vector<std::vector<int>> graph = randomGraph(vertices, edges, rng);
                double wpMs = 0, dsaturMs = 0, exactMs = 0;
                const std::vector<int> wp = measure([&] { return colourWelshPowell(graph); }, wpMs);
                const std::vector<int> dsatur = measure([&] { return colourDsatur(graph); }, dsaturMs);
                require(properColouring(graph, wp) && properColouring(graph, dsatur), "a colouring gives two neighbours one colour");
                // The exact search only for small graphs: its aux columns say what it found, on every row of the instance.
                const bool exactRuns = vertices <= 20;
                ExactColouring exact;
                if (exactRuns) exact = measure([&] { return colourExact(graph); }, exactMs);
                const double exactColours = exactRuns ? exact.colours : -1.0, exactProven = exactRuns && exact.proven ? 1.0 : 0.0;
                addRow(rows, "colouring", "wp", vertices, edges, seed, wpMs, coloursUsed(wp), exactColours, exactProven);
                addRow(rows, "colouring", "dsatur", vertices, edges, seed, dsaturMs, coloursUsed(dsatur), exactColours, exactProven);
                if (exactRuns) addRow(rows, "colouring", "exact", vertices, edges, seed, exactMs, exact.colours, exactColours, exactProven);
            }
}

// ---- the suites -------------------------------------------------------------------------------------------------------

struct SuiteEntry {
    BenchSuiteInfo info;
    void (*run)(bool quick, Rows& rows);
};

const std::vector<SuiteEntry>& suites() {
    static const std::vector<SuiteEntry> table = {
        {{"knap_1d",
          "One resource, integer weights 1 to 30, capacity half the total weight; groups of 10 copies whose values fall 5% per copy.",
          "copies", "", "value as % of the DP optimum", "nodes (bnb) or states (dp)", "proven optimal, 0 or 1"},
         runKnap1d},
        {{"knap_dims",
          "8 groups of 5 copies, integer weights 0 to 6, every capacity 24; the number of resources grows.",
          "copies", "resources", "value as % of the optimum", "states (dp) or nodes (bnb)", "unused"},
         runKnapDims},
        {{"knap_farm",
          "The knapsack problem of a generated farm with racks = crops / 3.",
          "crops", "", "value as % of the root bound", "nodes", "proven optimal, 0 or 1"},
         runKnapFarm},
        {{"pack_tight",
          "10 tray types, shelves of five sizes, conflict probability 0.15, compatibility probability 0.8; the trays fill 90% of the shelf area.",
          "trays", "tray area as % of shelf area", "placed area as % of tray area", "shelves used", "placed area as % of the used shelves' area"},
         runPackTight},
        {{"pack_loose",
          "The same instances with the trays filling 60% of the shelf area.",
          "trays", "tray area as % of shelf area", "placed area as % of tray area", "shelves used", "placed area as % of the used shelves' area"},
         runPackLoose},
        {{"sched",
          "One crop, cycle 5 weeks, no early harvest, 26 weeks; every week the demand is 1.3 times what the slots can deliver, 40% of it under contract. "
          "Nothing in it is random, so the five seeds repeat one instance and only the timings differ.",
          "slots", "", "hard lots on time, %", "soft lots served, %", "idle slot-weeks, %"},
         runSched},
        {{"sched_window",
          "The same with 20 slots, for lookahead windows of different lengths (and no randomness: the seeds repeat one instance).",
          "slots", "lookahead weeks", "hard lots on time, %", "soft lots served, %", "idle slot-weeks, %"},
         runSchedWindow},
        {{"sched_exact",
          "Cycle 2 weeks, 2 slots, 8 weeks, a given number of lots, 40% of the weekly demand under contract (and no randomness: the seeds repeat one instance).",
          "lots", "", "value as % of the exact optimum (-1: the exact search stopped early)", "unused", "unused"},
         runSchedExact},
        {{"forecast",
          "24 generated crops with 156 weeks of history, the last 12 weeks held out; the relative noise of the history varies.",
          "crops", "noise, %", "MAE as % of the mean demand", "RMSE as % of the mean demand", "sMAPE, %"},
         runForecast},
        {{"colouring",
          "Random graphs: every pair of vertices is joined with probability param / 100.",
          "vertices", "edge probability, %", "colours used", "colours of the exact search (-1: not run, more than 20 vertices)", "exact search proved its count, 0 or 1"},
         runColouring},
    };
    return table;
}

const SuiteEntry& entryFor(const std::string& suite) {
    for (const SuiteEntry& entry : suites())
        if (entry.info.name == suite) return entry;
    throw std::invalid_argument("unknown benchmark suite " + suite);
}

// ---- CSV --------------------------------------------------------------------------------------------------------------

const char* const kCsvHeader = "suite,algo,n,param,seed,time_ms,quality,aux1,aux2";

// Whole numbers without a decimal point (counts of nodes can be large), the rest with four decimals.
std::string numberText(double value) {
    char text[48];
    if (value == std::floor(value) && std::fabs(value) < 1e15) std::snprintf(text, sizeof text, "%.0f", value);
    else std::snprintf(text, sizeof text, "%.4f", value);
    return text;
}

std::string fixed(double value, int decimals) {
    char text[48];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    return text;
}

[[noreturn]] void csvError(int line, const std::string& what) {
    throw std::runtime_error("benchmark CSV, line " + std::to_string(line) + ": " + what);
}

template <class T> T numberField(const std::string& text, int line) {
    T value{};
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (text.empty() || result.ec != std::errc() || result.ptr != end) csvError(line, "'" + text + "' is not a number");
    return value;
}

// ---- views and the summary --------------------------------------------------------------------------------------------

double median(std::vector<double> values) {   // values must not be empty
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    return values.size() % 2 == 1 ? values[mid] : (values[mid - 1] + values[mid]) / 2;
}

// Least-squares slope of ln(time) against ln(n) over the points with time >= 0.05 ms (plan 15): with x = ln n and y = ln time,
// slope = sum((x - mean x)(y - mean y)) / sum((x - mean x)^2). The points of a series have different n, so the denominator is
// positive as soon as there are two. Time O(P) for P points.
void fitExponent(BenchSeries& series) {
    std::vector<double> x, y;
    for (const BenchPoint& p : series.points)
        if (p.timeMs >= 0.05 && p.n > 0) {
            x.push_back(std::log(double(p.n)));
            y.push_back(std::log(p.timeMs));
        }
    series.exponentPoints = int(x.size());
    if (x.size() < 2) return;
    const double meanX = std::accumulate(x.begin(), x.end(), 0.0) / double(x.size());
    const double meanY = std::accumulate(y.begin(), y.end(), 0.0) / double(y.size());
    double sxx = 0, sxy = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        sxx += (x[i] - meanX) * (x[i] - meanX);
        sxy += (x[i] - meanX) * (y[i] - meanY);
    }
    series.hasExponent = true;
    series.exponent = sxy / sxx;
}

std::string compilerText() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown compiler";
#endif
}

// The compile flags that CMake passed (see CMakeLists.txt), or, without them, whether optimisation was on.
std::string buildText() {
#ifdef FARM_BUILD_FLAGS
    return FARM_BUILD_FLAGS;
#elif defined(NDEBUG)
    return "NDEBUG defined (a release build)";
#else
    return "NDEBUG not defined (not a release build: the times are not comparable)";
#endif
}

}  // namespace

const std::vector<std::string>& benchSuiteNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> list;
        for (const SuiteEntry& entry : suites()) list.push_back(entry.info.name);
        return list;
    }();
    return names;
}

const BenchSuiteInfo& benchSuiteInfo(const std::string& suite) { return entryFor(suite).info; }

std::vector<BenchRow> runBenchSuite(const std::string& suite, bool quick) {
    Rows rows;
    entryFor(suite).run(quick, rows);
    return rows;
}

std::string benchCsv(const std::vector<BenchRow>& rows) {
    std::string out = std::string(kCsvHeader) + "\n";
    for (const BenchRow& r : rows)
        out += r.suite + "," + r.algo + "," + std::to_string(r.n) + "," + std::to_string(r.param) + "," + std::to_string(r.seed) + "," +
               numberText(r.timeMs) + "," + numberText(r.quality) + "," + numberText(r.aux1) + "," + numberText(r.aux2) + "\n";
    return out;
}

std::vector<BenchRow> parseBenchCsv(const std::string& text) {
    std::vector<BenchRow> rows;
    std::istringstream in(text);
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lineNumber == 1) {
            if (line != kCsvHeader) csvError(1, std::string("the header must be ") + kCsvHeader);
            continue;
        }
        if (line.empty()) continue;
        std::vector<std::string> field;
        size_t from = 0;
        for (size_t comma; (comma = line.find(',', from)) != std::string::npos; from = comma + 1) field.push_back(line.substr(from, comma - from));
        field.push_back(line.substr(from));
        if (field.size() != 9) csvError(lineNumber, "expected 9 fields, found " + std::to_string(field.size()));
        BenchRow row;
        row.suite = field[0];
        row.algo = field[1];
        row.n = numberField<int>(field[2], lineNumber);
        row.param = numberField<int>(field[3], lineNumber);
        row.seed = numberField<int>(field[4], lineNumber);
        row.timeMs = numberField<double>(field[5], lineNumber);
        row.quality = numberField<double>(field[6], lineNumber);
        row.aux1 = numberField<double>(field[7], lineNumber);
        row.aux2 = numberField<double>(field[8], lineNumber);
        rows.push_back(row);
    }
    if (lineNumber == 0) csvError(1, "the file is empty");
    return rows;
}

void saveBenchCsv(const std::vector<BenchRow>& rows, const std::string& path) {
    std::ofstream out(path, std::ios::binary);   // binary: LF line ends on every platform
    out << benchCsv(rows);
    out.flush();
    if (!out) throw std::runtime_error("cannot write " + path);
}

std::vector<BenchRow> loadBenchCsv(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::ostringstream text;
    text << in.rdbuf();
    try {
        return parseBenchCsv(text.str());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

BenchSuiteView benchView(const std::vector<BenchRow>& rows) {
    BenchSuiteView view;
    if (rows.empty()) return view;
    view.suite = rows.front().suite;
    view.xIsN = std::any_of(rows.begin(), rows.end(), [&](const BenchRow& r) { return r.n != rows.front().n; });
    std::vector<int> params;
    for (const BenchRow& r : rows) params.push_back(r.param);
    std::sort(params.begin(), params.end());
    const bool severalParams = std::unique(params.begin(), params.end()) - params.begin() > 1;

    // One series per algorithm, and per parameter when the points differ in size; the order of first appearance.
    std::vector<std::map<int, std::pair<std::vector<double>, std::vector<double>>>> cells;   // per series: x -> times, qualities
    for (const BenchRow& r : rows) {
        const int seriesParam = view.xIsN ? r.param : 0;
        size_t index = 0;
        while (index < view.series.size() && !(view.series[index].algo == r.algo && view.series[index].param == seriesParam)) ++index;
        if (index == view.series.size()) {
            BenchSeries series;
            series.algo = r.algo;
            series.param = seriesParam;
            series.label = view.xIsN && severalParams ? r.algo + " " + std::to_string(r.param) : r.algo;
            view.series.push_back(series);
            cells.emplace_back();
        }
        auto& cell = cells[index][view.xIsN ? r.n : r.param];
        cell.first.push_back(r.timeMs);
        cell.second.push_back(r.quality);
    }
    for (size_t i = 0; i < view.series.size(); ++i) {
        BenchSeries& series = view.series[i];
        for (const auto& entry : cells[i]) {
            BenchPoint point;
            point.n = view.xIsN ? entry.first : rows.front().n;
            point.param = view.xIsN ? series.param : entry.first;
            point.timeMs = median(entry.second.first);
            point.quality = median(entry.second.second);
            series.points.push_back(point);
        }
        if (view.xIsN) fitExponent(series);
    }
    return view;
}

std::vector<BenchSuiteView> loadBenchResults(const std::string& dir) {
    std::vector<BenchSuiteView> views;
    for (const std::string& name : benchSuiteNames()) {
        const std::filesystem::path file = std::filesystem::path(dir) / (name + ".csv");
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) continue;
        BenchSuiteView view = benchView(loadBenchCsv(file.string()));
        view.suite = name;   // the file name decides, so a wrong suite column cannot name a suite that does not exist
        views.push_back(view);
    }
    return views;
}

std::string benchSummary(const std::vector<BenchSuiteView>& views, const std::string& machine) {
    std::string out = "# Benchmark summary\n\n";
    out += "Written by `farm_cli bench` from the CSV files beside it. Do not edit by hand.\n\n";
    out += "- Machine: " + (machine.empty() ? std::string("not recorded (pass --machine \"CPU, cores, RAM\")") : machine) + "\n";
    out += "- Logical processors: " + std::to_string(std::thread::hardware_concurrency()) + "\n";
    out += "- Compiler: " + compilerText() + "\n";
    out += "- Build flags: " + buildText() + "\n";
    out += "- Seeds 1 to 5 per configuration (a quick run: the two smallest sizes, seed 1). Each run is timed three times and the fastest kept; a run over a second is timed once.\n";
    out += "- Branch and bound stops after 2 seconds per instance and may report that it did not prove its answer. That is a result, not a failure.\n";
    out += "- Tables show medians over the seeds. The measured exponent of a series is the least-squares slope of log(time) against log(n) over the points whose median time is at least 0.05 ms.\n";

    for (const BenchSuiteView& view : views) {
        const BenchSuiteInfo& info = benchSuiteInfo(view.suite);
        out += "\n## " + info.name + "\n\n" + info.what + "\n\n";
        out += "n counts " + info.nName + ". ";
        if (!info.paramName.empty()) out += "param is " + info.paramName + ". ";
        out += "quality is " + info.qualityName + ".\n\n";

        const bool showParam = !info.paramName.empty();
        out += std::string("| algorithm | ") + (showParam ? "param | " : "") + "n | median time (ms) | median quality |\n";
        out += std::string("|---|") + (showParam ? "---|" : "") + "---|---|---|\n";
        for (const BenchSeries& series : view.series)
            for (const BenchPoint& p : series.points) {
                out += "| " + series.algo + " | ";
                if (showParam) out += std::to_string(p.param) + " | ";
                out += std::to_string(p.n) + " | " + fixed(p.timeMs, 4) + " | " + fixed(p.quality, 2) + " |\n";
            }
        if (view.xIsN) {
            out += "\nMeasured exponents:\n\n";
            for (const BenchSeries& series : view.series) {
                if (series.hasExponent) out += "- " + series.label + ": " + fixed(series.exponent, 2) + " (from " + std::to_string(series.exponentPoints) + " points)\n";
                else out += "- " + series.label + ": none (fewer than two points with a time of at least 0.05 ms)\n";
            }
        } else {
            out += "\nEvery row has the same n, so there is no exponent against n for this suite.\n";
        }
    }
    return out;
}
