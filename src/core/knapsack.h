#pragma once
// Component 3 (plan section 9): how many trays of each crop to plant, as a multi-dimensional knapsack in which every
// crop is a group of copies. The k-th tray of a crop is worth less than the one before, because the market takes
// only so much, so a solution is always "the first count of each group".
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "constraints.h"
#include "model.h"
#include "scheduling.h"

struct KnapsackGroup {
    int cropId = 0;
    std::array<double, kResources> weight{};   // what one tray uses, in the resource order of model.h
    std::vector<double> values;                // PKR per week of the 1st, 2nd, .. tray; positive and non-increasing
};

struct KnapsackProblem {
    std::vector<KnapsackGroup> groups;         // crops with at least one positive value, in crop-id order
    std::array<double, kResources> capacity{};   // not NaN for an active resource (the solvers throw); the DP also needs it finite
    std::array<bool, kResources> active{{true, true, true, true, true}};   // an inactive resource is ignored entirely
};

struct KnapsackResult {
    std::vector<int> count;                    // trays per group
    double value = 0;                          // sum of the chosen prefixes
    double upperBound = 0;                     // branch and bound: the bound at the root
    long long nodes = 0;                       // branch and bound: nodes visited
    std::size_t states = 0;                    // dynamic programming: size of the table
    bool provenOptimal = false;                // exact for the problem as given
    std::string note;                          // e.g. "grid coarsened to 17 steps per resource"
    double seconds = 0;
};

// Dynamic-programming grid units that suit the farm: labour 0.01 h, water 1 L, nutrient 0.01 L, energy 0.1 kWh,
// area 10 cm^2. Every weight of the sample farm is a whole multiple of its unit.
constexpr std::array<double, kResources> kFarmDpUnit{{0.01, 1.0, 0.01, 0.1, 10.0}};

// The problem for one scenario (plan 9.2 to 9.4). demand holds one entry per crop, as buildCropDemand returns it;
// active switches resources off. A crop that needs a resource whose budget is zero can never be chosen, so it is
// left out and that resource is switched off in the result. Time O(sum of the group sizes).
KnapsackProblem buildKnapsackProblem(const Scenario& scenario, const std::vector<CropDemand>& demand,
                                     const ConstraintModel& constraints,
                                     const std::array<bool, kResources>& active = {{true, true, true, true, true}});

// The value of taking the first count[g] copies of every group g.
double knapsackValue(const KnapsackProblem& problem, const std::vector<int>& count);

// The tray values of every crop by crop id, as packing and refilling need them (an empty list for a crop without a group).
std::vector<std::vector<double>> valuesByCrop(const KnapsackProblem& problem, std::size_t cropCount);

// Trays per crop id for a count per group (0 for a crop without a group).
std::vector<int> countsByCrop(const KnapsackProblem& problem, const std::vector<int>& count, std::size_t cropCount);

// One copy of a group: the k-th tray (from 1) of group `group`.
struct RankedCopy { int group = 0, k = 0; };

// Every copy in the order the greedy takes them (plan 9.5): value per used budget descending, then value descending, then
// crop id, then k. Both the greedy and the refill step scan this list. Time O(N log N + N m).
std::vector<RankedCopy> rankCopies(const KnapsackProblem& problem);

// Variant 1 (plan 9.5): copies by value per used budget, taken while they fit. No guarantee of optimality.
// Time O(N log N + N m) for N copies and m resources.
KnapsackResult solveGreedy(const KnapsackProblem& problem);

// Variant 2 (plan 9.6): exact depth-first search with fractional bounds, started from the greedy solution. Stops at
// nodeLimit nodes or timeLimitSeconds (the clock is read every 4096 nodes) and then returns the best found with
// provenOptimal false. upperBound is the bound at the root. Worst case exponential; O(m N) per node.
KnapsackResult solveBranchAndBound(const KnapsackProblem& problem, long long nodeLimit = 5000000,
                                   double timeLimitSeconds = 5.0);

// Variant 3 (plan 9.7): table over the budgets, one dimension per active resource, each cut into steps of unit[r].
// Needs a positive unit for every active resource. When the table would exceed maxStates the grid is coarsened and
// the answer is only approximate (always feasible). Time O(N states m), space O(states) doubles + N states bits.
KnapsackResult solveDp(const KnapsackProblem& problem, const std::array<double, kResources>& unit = kFarmDpUnit,
                       std::size_t maxStates = 2000000);
