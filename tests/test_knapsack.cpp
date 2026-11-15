// Component 3 (plan section 9): the crop-selection knapsack. Expected numbers come from plan Appendix A (G3, G6),
// from hand calculation where a comment says so, and from plan/reference_model.py.
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "farm.h"
#include "knapsack.h"
#include "rng.h"
#include "scheduling.h"

namespace {

using Counts = std::vector<int>;
const std::array<double, kResources> unitOne{{1, 1, 1, 1, 1}};

KnapsackGroup makeGroup(int cropId, std::vector<double> values, std::vector<double> weights) {
    KnapsackGroup g;
    g.cropId = cropId;
    g.values = std::move(values);
    for (size_t r = 0; r < weights.size(); ++r) g.weight[r] = weights[r];
    return g;
}

// A problem over the first capacity.size() resources; the others are switched off.
KnapsackProblem makeProblem(std::vector<double> capacity, std::vector<KnapsackGroup> groups) {
    KnapsackProblem p;
    p.groups = std::move(groups);
    p.active.fill(false);
    for (size_t r = 0; r < capacity.size(); ++r) {
        p.active[r] = true;
        p.capacity[r] = capacity[r];
    }
    return p;
}

double used(const KnapsackProblem& p, const Counts& count, int r) {
    double sum = 0;
    for (size_t g = 0; g < p.groups.size(); ++g) sum += count[g] * p.groups[g].weight[r];
    return sum;
}

bool feasible(const KnapsackProblem& p, const Counts& count) {
    for (int r = 0; r < kResources; ++r)
        if (p.active[r] && used(p, count, r) > p.capacity[r] + EPS) return false;
    return true;
}

// The best value over every count vector, found by trying them all.
double bruteForce(const KnapsackProblem& p) {
    Counts count(p.groups.size(), 0);
    double best = 0;
    for (;;) {
        if (feasible(p, count)) best = std::max(best, knapsackValue(p, count));
        size_t g = 0;
        while (g < count.size() && count[g] == int(p.groups[g].values.size())) count[g++] = 0;   // count up like an odometer
        if (g == count.size()) break;
        ++count[g];
    }
    return best;
}

// A random problem small enough to enumerate: 1 to 3 resources, weights 0 to 4, capacities 4 to 9.
KnapsackProblem randomProblem(Rng& rng, bool grouped) {
    const int m = 1 + rng.below(3);
    std::vector<double> capacity;
    for (int r = 0; r < m; ++r) capacity.push_back(4 + rng.below(6));
    std::vector<KnapsackGroup> groups;
    const int n = grouped ? 2 + rng.below(4) : 3 + rng.below(7);
    for (int g = 0; g < n; ++g) {
        std::vector<double> weights;
        for (int r = 0; r < m; ++r) weights.push_back(rng.below(5));
        std::vector<double> values;
        double v = (2 + rng.below(16)) * 0.5;
        for (int k = 0, copies = grouped ? 1 + rng.below(4) : 1; k < copies; ++k) {
            values.push_back(v);
            v = std::max(0.5, v - rng.below(3) * 0.5);       // non-increasing, and positive
        }
        groups.push_back(makeGroup(g, values, weights));
    }
    return makeProblem(capacity, groups);
}

}  // namespace

// G3. One resource of capacity 10, X weighs 6 and is worth 7, Y weighs 5 with two copies worth 5 each: greedy takes X
// (best value per weight) and then nothing fits, so 7; the optimum is the two Y, 10.
TEST(knapsack_g3_greedy_is_not_optimal) {
    const KnapsackProblem p = makeProblem({10}, {makeGroup(0, {7}, {6}), makeGroup(1, {5, 5}, {5})});
    const KnapsackResult greedy = solveGreedy(p);
    CHECK(greedy.value == 7 && greedy.count == Counts({1, 0}));
    CHECK(!greedy.provenOptimal);
    const KnapsackResult bb = solveBranchAndBound(p);
    CHECK(bb.value == 10 && bb.count == Counts({0, 2}) && bb.provenOptimal);
    CHECK_NEAR(bb.upperBound, 11, 1e-9);                     // the root bound takes X whole, then 4/5 of a Y: 7 + 4
    const KnapsackResult dp = solveDp(p, unitOne);
    CHECK(dp.value == 10 && dp.count == Counts({0, 2}) && dp.provenOptimal);
}

// G3. Two resources of capacity 6 and six single-copy groups (value; weights): the optimum is 19, reached by two
// different sets, so only the value is asserted.
TEST(knapsack_g3_two_resources) {
    const KnapsackProblem p = makeProblem({6, 6}, {makeGroup(0, {9}, {3, 2}), makeGroup(1, {6}, {2, 2}), makeGroup(2, {7}, {2, 3}),
                                                  makeGroup(3, {4}, {1, 2}), makeGroup(4, {8}, {3, 3}), makeGroup(5, {3}, {1, 1})});
    CHECK(bruteForce(p) == 19);
    const KnapsackResult bb = solveBranchAndBound(p);
    CHECK(bb.value == 19 && bb.provenOptimal && feasible(p, bb.count));
    const KnapsackResult dp = solveDp(p, unitOne);
    CHECK(dp.value == 19 && dp.provenOptimal && feasible(p, dp.count));
    const KnapsackResult greedy = solveGreedy(p);
    CHECK(greedy.value <= 19 && feasible(p, greedy.count));
}

// G6, the value table: copies kept and the first and last value of each crop (cap, contract slots and demand are
// checked in the scheduling and constraint tests), N = 181 copies in all, and the five capacities.
TEST(knapsack_flat_farm_value_table_g6) {
    const Farm farm = makeFarm(flatScenario());
    const KnapsackProblem& p = farm.problem;
    CHECK(p.groups.size() == 12);
    const int copies[12] = {35, 24, 11, 9, 8, 16, 10, 16, 14, 6, 28, 4};
    const double first[12] = {340, 388.5, 216.5, 144, 344.5, 608.5, 101.5, 97.5, 900, 429.5, 849, 278.3333333333};
    const double last[12] = {124, 178.5, 216.5, 144, 344.5, 271, 101.5, 97.5, 450, 429.5, 324, 278.3333333333};
    size_t total = 0;
    for (size_t g = 0; g < p.groups.size(); ++g) {
        CHECK(p.groups[g].cropId == int(g));
        CHECK(int(p.groups[g].values.size()) == copies[g]);
        CHECK_NEAR(p.groups[g].values.front(), first[g], 1e-6);
        CHECK_NEAR(p.groups[g].values.back(), last[g], 1e-6);
        total += p.groups[g].values.size();
        CHECK(std::is_sorted(p.groups[g].values.rbegin(), p.groups[g].values.rend()));   // non-increasing
        CHECK(p.groups[g].values.back() > EPS);
    }
    CHECK(total == 181);
    CHECK_NEAR(p.capacity[0], 21.0, 1e-12);
    CHECK_NEAR(p.capacity[1], 800.0, 1e-12);
    CHECK_NEAR(p.capacity[2], 8.5, 1e-12);
    CHECK_NEAR(p.capacity[3], 400.0, 1e-12);
    CHECK_NEAR(p.capacity[4], 215200.0, 1e-9);
    for (int r = 0; r < kResources; ++r) CHECK(p.active[r]);
    // Plan 9.3's worked example, Lettuce: 340 for the ten contract trays, then 124 up to 35 trays (demand 12.6 / 0.36).
    for (int k = 0; k < 35; ++k) CHECK_NEAR(p.groups[0].values[k], k < 10 ? 340.0 : 124.0, 1e-9);
}

// G6, the results: greedy 38 941.5, branch and bound 39 486.5 with the listed selection, proven, root bound 39 486.5.
TEST(knapsack_flat_farm_results_g6) {
    const Farm farm = makeFarm(flatScenario());
    const KnapsackProblem& p = farm.problem;
    //                       Lettuce, Spinach, Kale, Pak Choi, Arugula, Basil, Mint, Coriander, Microgreens, Pea Shoots, Strawberry, Tomato
    const Counts greedySel = {10, 24, 1, 0, 8, 16, 0, 0, 14, 6, 18, 0};
    const Counts bbSel = {10, 24, 11, 0, 8, 16, 0, 0, 14, 6, 13, 0};

    const KnapsackResult greedy = solveGreedy(p);
    CHECK_NEAR(greedy.value, 38941.5, 1e-6);
    CHECK(byCrop(p, greedy, 12) == greedySel);
    CHECK(feasible(p, greedy.count) && !greedy.provenOptimal);

    const KnapsackResult bb = solveBranchAndBound(p);
    CHECK_NEAR(bb.value, 39486.5, 1e-6);
    CHECK(byCrop(p, bb, 12) == bbSel);
    CHECK(bb.provenOptimal && feasible(p, bb.count));
    CHECK_NEAR(bb.upperBound, 39486.5, 1e-4);                // the bound at the root is already tight
    // The reference model needs 2269 nodes; the plan says not to assert the exact figure, only a ceiling. 100 000 is its
    // ceiling; 10 000 also guards the densest-first order of the groups, which the search needs (34 352 nodes without it).
    CHECK(bb.nodes > 1 && bb.nodes < 10000);
    // The optimum uses the whole labour budget, which is why every capacity test needs EPS: without it the answer is 39 450.5.
    const double percent[5] = {100.00, 89.62, 83.53, 82.90, 91.75};
    for (int r = 0; r < kResources; ++r) CHECK_NEAR(100 * used(p, bb.count, r) / p.capacity[r], percent[r], 0.005);
    CHECK_NEAR(used(p, bb.count, 0), 21.0, 1e-9);
    CHECK(bb.value >= greedy.value);
}

// Property: on 300 small random problems with single copies, brute force over every subset equals branch and bound
// and the dynamic programme; every result fits; greedy never beats the optimum.
TEST(knapsack_random_subsets_match_brute_force) {
    Rng rng(2026);
    int greedyLost = 0;
    for (int round = 0; round < 300; ++round) {
        const KnapsackProblem p = randomProblem(rng, false);
        const double best = bruteForce(p);
        const KnapsackResult bb = solveBranchAndBound(p), dp = solveDp(p, unitOne), greedy = solveGreedy(p);
        CHECK_NEAR(bb.value, best, 1e-9);
        CHECK_NEAR(dp.value, best, 1e-9);
        CHECK(bb.provenOptimal && dp.provenOptimal);
        CHECK(feasible(p, bb.count) && feasible(p, dp.count) && feasible(p, greedy.count));
        CHECK(greedy.value <= best + 1e-9);
        CHECK(bb.upperBound >= best - 1e-9);                 // the root bound never underestimates
        if (greedy.value < best - 1e-9) ++greedyLost;
    }
    CHECK(greedyLost >= 10);                                 // the instances do trip greedy up
}

// Property: groups with several copies of non-increasing value, where a selection is a count per group.
TEST(knapsack_random_grouped_copies_match_brute_force) {
    Rng rng(2027);
    int greedyLost = 0;
    for (int round = 0; round < 300; ++round) {
        const KnapsackProblem p = randomProblem(rng, true);
        const double best = bruteForce(p);
        const KnapsackResult bb = solveBranchAndBound(p), dp = solveDp(p, unitOne), greedy = solveGreedy(p);
        CHECK_NEAR(bb.value, best, 1e-9);
        CHECK_NEAR(dp.value, best, 1e-9);
        CHECK(feasible(p, bb.count) && feasible(p, dp.count) && feasible(p, greedy.count));
        CHECK(greedy.value <= best + 1e-9);
        if (greedy.value < best - 1e-9) ++greedyLost;
    }
    CHECK(greedyLost >= 10);
}

// Greedy's sort: density, then value, then crop id, then copy number. Each case is decided by exactly one of them.
TEST(knapsack_greedy_tie_breaks) {
    // Equal density and value, different crop ids (group 1 has the smaller id): room for one tray, the smaller id wins.
    KnapsackProblem ids = makeProblem({1}, {makeGroup(7, {3}, {1}), makeGroup(2, {3}, {1})});
    CHECK(solveGreedy(ids).count == Counts({0, 1}));
    // Equal density (4 per 1 and 8 per 2), different values: the larger value first, so the weight-2 group fits and wins.
    KnapsackProblem values = makeProblem({2}, {makeGroup(0, {4}, {1}), makeGroup(1, {8}, {2})});
    CHECK(solveGreedy(values).value == 8);
    // Copies of one group with equal values come in order of k, so all three are taken; with k descending only one would be.
    KnapsackProblem copies = makeProblem({3}, {makeGroup(0, {5, 5, 5}, {1})});
    CHECK(solveGreedy(copies).value == 15);
    // A tray that needs none of the active resources has infinite density and is always taken.
    KnapsackProblem weightless = makeProblem({1}, {makeGroup(0, {1, 1}, {0}), makeGroup(1, {50}, {1})});
    CHECK(solveGreedy(weightless).count == Counts({2, 1}));
    CHECK(solveBranchAndBound(weightless).value == 52 && solveDp(weightless, unitOne).value == 52);
}

// 0.1 + 0.1 + 0.1 is 0.30000000000000004 in floating point, so three trays of 0.1 fit a capacity of 0.3 only because
// every fit test allows EPS (plan 5.1). All three solvers must fill it.
TEST(knapsack_capacity_tolerance) {
    const KnapsackProblem p = makeProblem({0.3}, {makeGroup(0, {5, 5, 5, 5}, {0.1})});
    const std::array<double, kResources> tenth{{0.1, 0.1, 0.1, 0.1, 0.1}};
    CHECK(solveGreedy(p).count[0] == 3);
    CHECK(solveBranchAndBound(p).count[0] == 3);
    CHECK(solveDp(p, tenth).count[0] == 3);
}

// The surrogate bound (plan 9.6): two budgets that bind together. A needs all of the first budget, B all of the second,
// C half of each. Each budget on its own would allow 24 (one of A or B, C, and half of the other), but together they
// allow only 20, which greedy already holds, so the search proves it at the root.
TEST(knapsack_surrogate_bound_proves_the_root) {
    const KnapsackProblem p = makeProblem({10, 10}, {makeGroup(0, {10}, {10, 0}), makeGroup(1, {10}, {0, 10}), makeGroup(2, {9}, {5, 5})});
    const KnapsackResult r = solveBranchAndBound(p);
    CHECK(r.value == 20 && r.provenOptimal && r.nodes == 1);
    CHECK_NEAR(r.upperBound, 20, 1e-9);
    CHECK(solveGreedy(p).value == 20 && bruteForce(p) == 20);
}

// Plan 9.3 by hand, for Lettuce (out = 0.36 kg a week from a tray, operating cost 272, price 1100, salvage 20%).
TEST(knapsack_builder_values) {
    const Farm farm = makeFarm(flatScenario());
    auto groupFor = [&](const std::vector<CropDemand>& demand) {
        return buildKnapsackProblem(farm.scenario, demand, farm.constraints);
    };
    std::vector<CropDemand> demand = farm.demand;
    for (CropDemand& d : demand) { d.demand = 0; d.contractSlots = 0; d.penaltyPerWeek = 0; }   // nothing to sell for any crop...
    // ...so a tray earns only the salvage price (Lettuce: 1100 * 0.2 * 0.36 = 79.2), below its operating cost: no group at all
    CHECK(groupFor(demand).groups.empty());
    // A contract of 3 slots worth 3000 a week gives the first three trays a bonus of 1000: 79.2 - 272 + 1000 = 807.2.
    demand[0].contractSlots = 3;
    demand[0].penaltyPerWeek = 3000;
    KnapsackProblem p = groupFor(demand);
    CHECK(p.groups[0].cropId == 0 && p.groups[0].values.size() == 3);
    for (double v : p.groups[0].values) CHECK_NEAR(v, 807.2, 1e-9);
    // Demand of 0.5 kg: the first tray sells all 0.36 kg (profit 124); the second sells 0.14 and salvages 0.22 kg,
    // 1100 * (0.14 + 0.2 * 0.22) - 272 = -69.6, which stops the group.
    demand[0].contractSlots = 0;
    demand[0].penaltyPerWeek = 0;
    demand[0].demand = 0.5;
    p = groupFor(demand);
    CHECK(p.groups[0].cropId == 0 && p.groups[0].values.size() == 1);
    CHECK_NEAR(p.groups[0].values[0], 124.0, 1e-9);
    // More demand than the shelves can hold: the group stops at the area bound, here min(400, maxTraysByArea) = 72.
    demand[0].demand = 1000;
    p = groupFor(demand);
    CHECK(p.groups[0].values.size() == 72);

    // A value of zero, or of less than EPS, is not worth a tray: Lettuce with no costs and a price of 0, and with a price
    // so low that a tray earns 5e-10 PKR a week, gets no group.
    demand[0].demand = 1000;
    Scenario odd = farm.scenario;
    Crop& lettuce = odd.crops[0];
    lettuce.laborPerWeek = lettuce.waterPerWeek = lettuce.nutrientPerWeek = lettuce.energyPerWeek = lettuce.trayCost = 0;
    lettuce.pricePerKg = 0;
    CHECK(buildKnapsackProblem(odd, demand, farm.constraints).groups.empty());   // the other crops have no demand
    lettuce.pricePerKg = 5e-10 / 0.36;
    CHECK(buildKnapsackProblem(odd, demand, farm.constraints).groups.empty());
    lettuce.pricePerKg = 1;                                  // a tray now earns 0.36 a week, more than EPS
    CHECK(buildKnapsackProblem(odd, demand, farm.constraints).groups.size() == 1);

    // No crop gets more than 400 trays, however much room and demand there is: 10 times wider shelves hold about 720.
    Scenario wide = farm.scenario;
    for (Shelf& shelf : wide.shelves) shelf.width *= 10;
    const ConstraintModel roomy = buildConstraints(wide);
    CHECK(roomy.maxTraysByArea[0] > 400);
    CHECK(buildKnapsackProblem(wide, demand, roomy).groups[0].values.size() == 400);

    CHECK_THROWS(buildKnapsackProblem(farm.scenario, std::vector<CropDemand>(5), farm.constraints));
}

// A switched-off resource is ignored: the result equals that of the same problem with that capacity set enormous.
TEST(knapsack_inactive_resource_is_ignored) {
    const Farm full = makeFarm(flatScenario());
    const double base = solveBranchAndBound(full.problem).value;
    for (int r = 0; r < kResources; ++r) {
        std::array<bool, kResources> active{{true, true, true, true, true}};
        active[r] = false;
        const Farm off = makeFarm(flatScenario(), active);
        KnapsackProblem huge = full.problem;
        huge.capacity[r] = 1e12;
        const KnapsackResult a = solveBranchAndBound(off.problem), b = solveBranchAndBound(huge);
        CHECK(!off.problem.active[r]);
        CHECK_NEAR(a.value, b.value, 1e-6);
        CHECK(a.provenOptimal && b.provenOptimal);
        CHECK(a.value >= base - 1e-9);                       // dropping a constraint never hurts
        CHECK(feasible(off.problem, a.count));
        CHECK_NEAR(solveGreedy(off.problem).value, solveGreedy(huge).value, 1e-6);
    }
    // Labour binds on this farm (100% used), so ignoring it must raise the value and use more than the 21 hours.
    std::array<bool, kResources> noLabour{{false, true, true, true, true}};
    const Farm free = makeFarm(flatScenario(), noLabour);
    const KnapsackResult r = solveBranchAndBound(free.problem);
    CHECK(r.value > base + 1);
    CHECK(used(free.problem, r.count, 0) > 21.0 + 1e-6);
}

// A zero budget: crops that need the resource are left out and the resource is switched off.
TEST(knapsack_zero_budget_drops_groups) {
    Scenario s = flatScenario();
    s.budget.labor = 0;
    Farm farm = makeFarm(s);
    CHECK(farm.problem.groups.empty() && !farm.problem.active[0] && farm.problem.active[1]);
    for (const KnapsackResult& r : {solveGreedy(farm.problem), solveBranchAndBound(farm.problem), solveDp(farm.problem)}) {
        CHECK(r.count.empty() && r.value == 0);
    }
    CHECK(solveBranchAndBound(farm.problem).provenOptimal);

    // One crop that needs no labour stays, and labour no longer limits anything.
    s.crops[4].laborPerWeek = 0;
    farm = makeFarm(s);
    CHECK(farm.problem.groups.size() == 1 && farm.problem.groups[0].cropId == 4 && !farm.problem.active[0]);
    const KnapsackResult r = solveBranchAndBound(farm.problem);
    CHECK(r.provenOptimal && r.count[0] > 0 && feasible(farm.problem, r.count));

    // A switched-off resource with zero budget is simply ignored and drops nothing.
    s = flatScenario();
    s.budget.labor = 0;
    const Farm ignored = makeFarm(s, {{false, true, true, true, true}});
    CHECK(ignored.problem.groups.size() == 12);
}

// The limits stop the search and the answer is then the best found so far: never worse than greedy, never above the bound.
TEST(knapsack_branch_and_bound_limits) {
    const Farm farm = makeFarm(flatScenario());
    const KnapsackResult greedy = solveGreedy(farm.problem);
    const KnapsackResult cut = solveBranchAndBound(farm.problem, 50);
    CHECK(!cut.provenOptimal && cut.nodes <= 51);
    CHECK(cut.value >= greedy.value - 1e-9 && cut.value <= 39486.5 + 1e-6);
    CHECK_NEAR(cut.upperBound, 39486.5, 1e-4);
    CHECK(feasible(farm.problem, cut.count));
    CHECK(!solveBranchAndBound(farm.problem, 0).provenOptimal);   // no node allowed at all: greedy comes back unproven

    // The clock is read every 4096 nodes. Twenty-two items whose value is their weight form a hard subset-sum problem.
    Rng rng(7);
    std::vector<KnapsackGroup> groups;
    double total = 0;
    for (int g = 0; g < 22; ++g) {
        const double w = 1000 + rng.below(1000);
        total += w;
        groups.push_back(makeGroup(g, {w}, {w}));
    }
    const KnapsackProblem hard = makeProblem({std::floor(total / 2) + 0.5}, groups);
    const KnapsackResult timed = solveBranchAndBound(hard, 1000000000LL, 0.0);
    CHECK(!timed.provenOptimal && timed.nodes == 4096);
    CHECK(timed.value >= solveGreedy(hard).value - 1e-9 && feasible(hard, timed.count));
}

// The dynamic programme on the farm: exact when the table fits, coarsened (and then poor, as the plan says) when not.
TEST(knapsack_dp_on_the_farm) {
    const Farm farm = makeFarm(flatScenario());
    // One resource at a time the default units divide every weight and capacity, so the table is exact and agrees
    // with branch and bound on the same single-resource problem.
    const size_t tables[5] = {2101, 801, 851, 4001, 21521};
    for (int r = 0; r < kResources; ++r) {
        std::array<bool, kResources> only{};
        only[r] = true;
        const Farm single = makeFarm(flatScenario(), only);
        const KnapsackResult dp = solveDp(single.problem), bb = solveBranchAndBound(single.problem);
        CHECK(dp.provenOptimal && dp.note.empty() && dp.states == tables[r]);
        CHECK(bb.provenOptimal);
        CHECK_NEAR(dp.value, bb.value, 1e-6);
        CHECK(feasible(single.problem, dp.count));
    }
    // All five: far too many states, so every resource gets 17 steps (18^5 states), every tray's labour rounds up to a
    // seventeenth of the budget and the plan holds at most 17 trays.
    const KnapsackResult dp = solveDp(farm.problem);
    CHECK(dp.note == "grid coarsened to 17 steps per resource");
    CHECK(!dp.provenOptimal && dp.states == 1889568u);
    CHECK(feasible(farm.problem, dp.count));
    int trays = 0;
    for (int c : dp.count) trays += c;
    CHECK(trays > 0 && trays <= 17);
    CHECK(dp.value > 0 && dp.value < 0.5 * solveBranchAndBound(farm.problem).value);
}

// Grid rules on small problems: coarsening, rounding weights up, exactness and the unit check.
TEST(knapsack_dp_grid_rules) {
    // Capacity 1000 in steps of 1 is 1001 states; with room for 100, every resource gets 99 steps of 10.1 and a tray of
    // weight 7 rounds up to one step, so 99 trays are taken where 142 would fit.
    std::vector<double> values;
    for (int k = 0; k < 150; ++k) values.push_back(10.0 - k * 0.01);
    const KnapsackProblem big = makeProblem({1000}, {makeGroup(0, values, {7})});
    const KnapsackResult coarse = solveDp(big, unitOne, 100);
    CHECK(coarse.note == "grid coarsened to 99 steps per resource" && coarse.states == 100 && !coarse.provenOptimal);
    CHECK(coarse.count[0] == 99 && feasible(big, coarse.count));
    CHECK(solveBranchAndBound(big).count[0] == 142);
    // The coarsened unit is the capacity over the steps (10.101 here), so a weight of 10.1 is one step, not two.
    const KnapsackProblem fine = makeProblem({1000}, {makeGroup(0, values, {10.1})});
    CHECK(solveDp(fine, unitOne, 100).count[0] == 99);
    // A coarsened grid is never "proven", even when every weight is a whole number of the coarse units: here the unit is
    // 1000 / 99 and the weight is exactly one unit.
    const KnapsackProblem aligned = makeProblem({1000}, {makeGroup(0, values, {1000.0 / 99})});
    const KnapsackResult alignedResult = solveDp(aligned, unitOne, 100);
    CHECK(alignedResult.count[0] == 99 && alignedResult.note.size() > 0 && !alignedResult.provenOptimal);

    // Floating-point error must not cost a step. 2.1 / 0.3 is 7.000000000000001, yet a weight of 2.1 is 7 steps of 0.3
    // (not 8), so three trays fit a capacity of 6.3 (21 steps) exactly...
    const std::array<double, kResources> third{{0.3, 0.3, 0.3, 0.3, 0.3}}, tenth{{0.1, 0.1, 0.1, 0.1, 0.1}};
    const KnapsackResult weightEdge = solveDp(makeProblem({6.3}, {makeGroup(0, {5, 5, 5, 5}, {2.1})}), third);
    CHECK(weightEdge.count[0] == 3 && weightEdge.provenOptimal && weightEdge.states == 22);
    // ...and 3.3 / 0.1 is 32.99999999999999, yet a capacity of 3.3 is 33 steps of 0.1 (not 32): three trays of 1.1 fit.
    const KnapsackResult capacityEdge = solveDp(makeProblem({3.3}, {makeGroup(0, {5, 5, 5, 5}, {1.1})}), tenth);
    CHECK(capacityEdge.count[0] == 3 && capacityEdge.provenOptimal && capacityEdge.states == 34);

    // Capacity 10 in units of 3 is 3 steps; a weight of 4 rounds up to 2 steps, so one tray; two would really fit.
    const KnapsackProblem rounded = makeProblem({10}, {makeGroup(0, {5, 5, 5}, {4})});
    const std::array<double, kResources> three{{3, 3, 3, 3, 3}};
    const KnapsackResult r = solveDp(rounded, three);
    CHECK(r.count[0] == 1 && !r.provenOptimal && feasible(rounded, r.count) && r.note.empty());
    // A unit that divides everything is exact, even when it is not 1: weights 4 and capacity 12 in units of 2 or 4.
    const KnapsackProblem exact = makeProblem({12}, {makeGroup(0, {5, 5, 5, 5}, {4})});
    const std::array<double, kResources> four{{4, 4, 4, 4, 4}}, two{{2, 2, 2, 2, 2}};
    CHECK(solveDp(exact, four).provenOptimal && solveDp(exact, four).count[0] == 3);
    CHECK(solveDp(exact, two).provenOptimal && solveDp(exact, two).count[0] == 3);
    // The weights and the capacity are checked separately: a weight of 4 in units of 3, a capacity of 10 in units of 3.
    CHECK(!solveDp(makeProblem({12}, {makeGroup(0, {5, 5}, {4})}), three).provenOptimal);
    CHECK(!solveDp(makeProblem({10}, {makeGroup(0, {5, 5}, {3})}), three).provenOptimal);
    CHECK(solveDp(makeProblem({12}, {makeGroup(0, {5, 5}, {3})}), three).provenOptimal);

    // A group heavier than the whole grid is never taken; a resource without a unit is only an error when active.
    const KnapsackProblem heavy = makeProblem({5}, {makeGroup(0, {9}, {6}), makeGroup(1, {1}, {1})});
    CHECK(solveDp(heavy, unitOne).count == Counts({0, 1}));
    std::array<double, kResources> noUnit = unitOne;
    noUnit[0] = 0;
    CHECK_THROWS(solveDp(heavy, noUnit));
    noUnit = unitOne;
    noUnit[3] = 0;                                           // resource 3 is switched off in `heavy`
    CHECK(solveDp(heavy, noUnit).value == 1);

    // No active resource: nothing limits the plan and every copy is taken.
    KnapsackProblem none = makeProblem({}, {makeGroup(0, {2, 1}, {}), makeGroup(1, {3}, {})});
    const KnapsackResult all = solveDp(none, unitOne);
    CHECK(all.count == Counts({2, 1}) && all.value == 6 && all.provenOptimal && all.states == 1);
    CHECK(solveBranchAndBound(none).value == 6 && solveGreedy(none).value == 6);
}

// valuesByCrop: the values land at the crop id of their group; crops without a group get an empty list, and a group for a
// crop beyond the count is ignored.
TEST(knapsack_values_by_crop) {
    const KnapsackProblem p = makeProblem({10}, {makeGroup(1, {5, 3}, {1}), makeGroup(4, {2}, {1}), makeGroup(7, {9}, {1})});
    const std::vector<std::vector<double>> v = valuesByCrop(p, 5);
    CHECK(v.size() == 5 && v[0].empty() && v[2].empty() && v[3].empty());
    CHECK(v[1] == std::vector<double>({5, 3}) && v[4] == std::vector<double>({2}));
    CHECK(valuesByCrop(p, 4)[1] == std::vector<double>({5, 3}) && valuesByCrop(p, 4).size() == 4);   // crop 4 is beyond the count
    CHECK(valuesByCrop(makeProblem({10}, {}), 3).size() == 3 && valuesByCrop(makeProblem({10}, {}), 0).empty());
}

TEST(knapsack_value_helper_and_empty_problem) {
    const KnapsackProblem p = makeProblem({10}, {makeGroup(0, {5, 3, 1}, {1}), makeGroup(1, {4, 4}, {1})});
    CHECK(knapsackValue(p, {0, 0}) == 0 && knapsackValue(p, {2, 1}) == 12 && knapsackValue(p, {3, 2}) == 17);
    const KnapsackProblem empty = makeProblem({10}, {});
    CHECK(solveGreedy(empty).value == 0);
    const KnapsackResult bb = solveBranchAndBound(empty);
    CHECK(bb.value == 0 && bb.provenOptimal && bb.upperBound == 0 && bb.count.empty());
    CHECK(solveDp(empty, unitOne).value == 0);
    // A group with no copies is harmless (the builder never makes one, but a hand-made problem may).
    const KnapsackProblem hole = makeProblem({5}, {makeGroup(0, {}, {1}), makeGroup(1, {2}, {1})});
    CHECK(solveGreedy(hole).count == Counts({0, 1}) && solveBranchAndBound(hole).count == Counts({0, 1}));
    CHECK(solveDp(hole, unitOne).count == Counts({0, 1}) && solveDp(hole, unitOne).value == 2);
}

// A NaN budget makes every comparison false, so greedy and branch and bound would take every copy and the DP table would
// have no size; an unlimited budget has no DP grid. Both are refused instead of guessed at. An unlimited budget is fine
// for greedy and branch and bound, and a resource that is switched off may hold anything.
TEST(knapsack_refuses_nan_and_infinite_capacity) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    const std::vector<KnapsackGroup> groups = {makeGroup(0, {5, 4, 3}, {1, 1})};   // one tray uses 1 of each of two resources
    const KnapsackProblem notANumber = makeProblem({nan, 5}, groups);
    CHECK_THROWS(solveGreedy(notANumber));
    CHECK_THROWS(solveBranchAndBound(notANumber));
    CHECK_THROWS(solveDp(notANumber, unitOne));
    const KnapsackProblem unlimited = makeProblem({inf, 2}, groups);
    CHECK(solveGreedy(unlimited).count[0] == 2 && solveBranchAndBound(unlimited).count[0] == 2);
    CHECK_THROWS(solveDp(unlimited, unitOne));
    for (const double odd : {nan, inf}) {
        KnapsackProblem off = makeProblem({odd, 5}, groups);
        off.active[0] = false;
        CHECK(solveGreedy(off).count[0] == 3 && solveBranchAndBound(off).count[0] == 3 && solveDp(off, unitOne).count[0] == 3);
    }
}
