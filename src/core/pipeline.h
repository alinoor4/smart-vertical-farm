#pragma once
// The whole plan in one call (plan section 12): forecast every crop, build the constraints, choose the crops with a
// knapsack, pack the trays onto the shelves, fill what is left over, and schedule every crop's harvests.
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "constraints.h"
#include "forecast.h"
#include "knapsack.h"
#include "model.h"
#include "packing.h"
#include "scheduling.h"

enum class KnapsackAlgo { Greedy, Dp, BranchAndBound };
enum class PackAlgo { FirstFitDecreasing, BestFitDecreasing, Genetic };

struct PlanOptions {
    ForecastMethod forecast = ForecastMethod::Auto;
    KnapsackAlgo knapsack = KnapsackAlgo::BranchAndBound;
    std::array<bool, kResources> activeResource{{true, true, true, true, true}};   // any of the five, area included, may be switched off
    long long bnbNodeLimit = 5000000;
    double bnbTimeLimitSeconds = 5.0;
    std::size_t dpMaxStates = 2000000;
    PackAlgo packer = PackAlgo::FirstFitDecreasing;
    GaParams ga;
    bool refill = true;
    SchedAlgo scheduler = SchedAlgo::EdfLookahead;
    int lookaheadWeeks = -1;                                                       // -1: each crop's cycle
};

// Names for output: "seasonal naive", "branch and bound", "First-Fit Decreasing", "EDF with lookahead", ...
const char* name(ForecastMethod method);
const char* name(KnapsackAlgo algo);
const char* name(PackAlgo algo);
const char* name(SchedAlgo algo);

// One row of the selection table: what the plan holds of one crop.
struct CropPlan {
    int selected = 0;                           // trays in the relaxed selection
    int trays = 0;                              // trays in the final plan
    long long area = 0;                         // cm^2 of tray area
    double weeklyKg = 0;                        // kg a week the trays can deliver when every slot is busy: trays * yield / cycle
    double demandKg = 0;                        // mean weekly sellable kg (CropDemand::demand)
    double value = 0;                           // weekly value of the final trays
    std::array<double, kResources> use{};       // what the final trays use: labour, water, nutrient, energy, area
};

struct ColourCounts {
    int welshPowell = 0, dsatur = 0, exact = 0;
    bool exactProven = true;
    long long exactNodes = 0;
};

// The headline numbers of plan 12.4. Shares are fractions (0.93 is 93%); a share with nothing to divide by is 1 for a
// service level (nothing was due, nothing was missed) and 0 for a use of something that does not exist.
struct PlanMetrics {
    double relaxedOptimum = 0;                  // the knapsack value, PKR per week, contract bonus included
    double planValue = 0;                       // the weekly value of the final trays
    double planShare = 0;                       // planValue / relaxedOptimum
    double expectedWeeklyProfit = 0;            // planValue without the contract bonus terms it contains
    std::array<double, kResources> budgetUse{}; // final trays' use of each resource over its capacity
    long long placedArea = 0, totalShelfArea = 0, usedShelfArea = 0;
    double spaceUtilisation = 0;                // placed area / total shelf area
    double spaceUtilisationOfUsed = 0;          // placed area / area of the shelves in use
    int hardTotal = 0, hardOnTime = 0, softTotal = 0, softServed = 0, idleSlotWeeks = 0;
    double contractReliability = 1;             // hardOnTime / hardTotal
    double spotService = 1;                     // softServed / softTotal
    double revenue = 0, penalties = 0, net = 0; // over all scheduled lots; net = revenue - penalties
};

struct PlanResult {
    std::string scenarioName;
    std::vector<std::string> cropNames;
    PlanOptions options;                        // as run
    std::vector<ForecastResult> forecast;       // per crop; element 0 of a forecast is delivery week 1
    ConstraintModel constraints;
    ColourCounts colours;
    std::vector<CropDemand> demand;             // per crop: lots, contract slots, demand, penalty per week
    KnapsackProblem problem;
    KnapsackResult knapsack;                    // the relaxed selection
    std::vector<int> selected;                  // trays per crop in the relaxed selection
    PackResult pack;                            // the packer's own result, before the refill
    PackState layout;                           // the final layout, after the refill
    int refilled = 0;                           // trays the refill added
    std::vector<int> finalCount;                // trays per crop in the final layout
    std::vector<CropPlan> rows;                 // per crop
    std::vector<CropSchedule> schedules;        // per crop
    std::vector<std::array<double, 4>> weeklyUse;   // per week 0 .. T-1: labour, water, nutrient and energy in use
    PlanMetrics metrics;
    std::vector<std::string> warnings;          // plan 12.3, in the order of the stages
    std::vector<std::pair<std::string, double>> stageSeconds;
    double totalSeconds = 0;
};

// Runs plan 12.2 on a scenario. Throws std::invalid_argument, listing the first problems, when validateScenario reports
// any, and std::invalid_argument for options a stage refuses (such as a genetic algorithm population below 3). Time is the
// sum of the stages; the knapsack and the genetic packer dominate.
PlanResult runPipeline(const Scenario& scenario, const PlanOptions& options = PlanOptions());

// One table per stage, every algorithm of the stage on the same input (plan section 13). Values are numbers; `columns` names them.
struct CompareRow {
    std::string name;
    std::vector<double> values;
    std::string note;
};
struct CompareStage {
    std::string name;
    std::vector<std::string> columns;
    std::vector<CompareRow> rows;
};
struct CompareTable {
    std::vector<CompareStage> stages;           // forecast, colouring, knapsack, packing, scheduling
};

// Forecast: hold-out errors per method, averaged over the crops. Colouring: the three algorithms on the conflict graph.
// Knapsack: the three algorithms on the scenario's problem. Packing: the three packers on the branch-and-bound selection.
// Scheduling: the three schedulers on the plan of that selection packed with First-Fit Decreasing and refilled.
// The options give the forecast method, the switched-off resources, the limits and the genetic parameters. Throws like runPipeline.
CompareTable compareAll(const Scenario& scenario, const PlanOptions& options = PlanOptions());
