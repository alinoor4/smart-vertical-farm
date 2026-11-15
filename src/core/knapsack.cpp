#include "knapsack.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

using Clock = std::chrono::steady_clock;
using Weights = std::array<double, kResources>;

constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr int kMaxTraysPerCrop = 400;   // the model never plants more trays of one crop than this (plan 9.2)

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// The resources a solver looks at: the active ones.
std::vector<int> activeResources(const KnapsackProblem& problem) {
    std::vector<int> result;
    for (int r = 0; r < kResources; ++r)
        if (problem.active[r]) result.push_back(r);
    return result;
}

// The active resources that have a budget to divide by; only these enter a density or a bound.
std::vector<int> budgetedResources(const KnapsackProblem& problem) {
    std::vector<int> result;
    for (int r = 0; r < kResources; ++r)
        if (problem.active[r] && problem.capacity[r] > 0) result.push_back(r);
    return result;
}

// Value per share of the budget: v / sum(w_r / B_r). Plus infinity for a copy that uses none of the budget.
double density(double value, const Weights& w, const Weights& capacity, const std::vector<int>& budgeted) {
    double used = 0;
    for (int r : budgeted) used += w[r] / capacity[r];
    return used > 0 ? value / used : kInfinity;
}

// Every comparison with NaN is false, so a NaN budget would let "does it fit" say yes to every copy (and make the sort
// orders inconsistent). The builder cannot produce one, because validateScenario rejects it; a hand-made problem can.
void refuseNaNCapacity(const KnapsackProblem& problem) {
    for (int r : activeResources(problem))
        if (std::isnan(problem.capacity[r])) throw std::invalid_argument("a knapsack capacity must be a number");
}

bool fits(const Weights& w, const Weights& remaining, const std::vector<int>& active) {
    for (int r : active)
        if (w[r] > remaining[r] + EPS) return false;
    return true;
}

KnapsackResult finish(const KnapsackProblem& problem, std::vector<int> count, Clock::time_point start) {
    KnapsackResult result;
    result.value = knapsackValue(problem, count);
    result.count = std::move(count);
    result.seconds = secondsSince(start);
    return result;
}

// ---- branch and bound -------------------------------------------------------------------------------------------

// One copy in a bound list: its value, its weight in the list's resource, and where it came from.
struct BoundItem {
    double ratio;       // value per weight; plus infinity for weight zero
    int pos, k;         // position of its group in the search order, and its number within the group
    double value, weight;
};

// Copies by ratio descending, ties by position then k: a total order.
void sortByRatio(std::vector<BoundItem>& list) {
    std::sort(list.begin(), list.end(), [](const BoundItem& a, const BoundItem& b) {
        if (a.ratio != b.ratio) return a.ratio > b.ratio;
        return a.pos != b.pos ? a.pos < b.pos : a.k < b.k;
    });
}

// Fractional knapsack over one list, skipping groups before `depth`: whole copies while they fit, then the fitting
// share of the first that does not. It relaxes the problem to one constraint, so it never underestimates.
double fractionalBound(const std::vector<BoundItem>& list, int depth, double capacity) {
    capacity = std::max(capacity, 0.0);   // rounding can leave a hair below zero
    double total = 0;
    for (const BoundItem& item : list) {
        if (item.pos < depth) continue;
        if (item.weight <= capacity) {
            capacity -= item.weight;
            total += item.value;
        } else {
            return total + item.value * capacity / item.weight;   // weight > capacity >= 0, so weight > 0
        }
    }
    return total;
}

struct BranchAndBound {
    const KnapsackProblem& problem;
    long long nodeLimit;
    double timeLimit;
    Clock::time_point start = Clock::now();

    std::vector<int> active, budgeted;           // resources the search looks at
    std::vector<int> order;                      // order[pos] = the group at position pos
    std::vector<Weights> weight;                 // by position
    std::vector<std::vector<double>> prefix;     // by position: prefix[pos][k] = value of the first k copies
    std::vector<std::vector<BoundItem>> lists;   // one per budgeted resource, then the surrogate list last
    long long nodes = 0;
    bool stopped = false;                        // a limit was reached
    double best = 0;                             // value of the incumbent
    std::vector<int> bestCount, chosen;          // by position

    BranchAndBound(const KnapsackProblem& p, long long maxNodes, double maxSeconds)
        : problem(p), nodeLimit(maxNodes), timeLimit(maxSeconds), active(activeResources(p)), budgeted(budgetedResources(p)) {
        const size_t n = p.groups.size();
        // Groups whose first copy is densest come first, so the search meets the valuable crops early.
        std::vector<double> first(n, 0.0);
        for (size_t g = 0; g < n; ++g)
            if (!p.groups[g].values.empty()) first[g] = density(p.groups[g].values[0], p.groups[g].weight, p.capacity, budgeted);
        order.resize(n);
        for (size_t g = 0; g < n; ++g) order[g] = int(g);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (first[a] != first[b]) return first[a] > first[b];
            return p.groups[a].cropId < p.groups[b].cropId;
        });

        std::vector<double> share(n, 0.0);   // by position: sum of w_r / B_r, the weight in the surrogate constraint
        for (size_t pos = 0; pos < n; ++pos) {
            const KnapsackGroup& group = p.groups[order[pos]];
            weight.push_back(group.weight);
            prefix.push_back({0.0});
            for (double v : group.values) prefix.back().push_back(prefix.back().back() + v);
            for (int r : budgeted) share[pos] += group.weight[r] / p.capacity[r];
        }

        // One list per budgeted resource, and one for the surrogate: sum_r (used_r / B_r) <= sum_r (remaining_r / B_r).
        for (size_t j = 0; j <= budgeted.size(); ++j) {
            std::vector<BoundItem> list;
            for (size_t pos = 0; pos < n; ++pos) {
                const double w = j < budgeted.size() ? weight[pos][budgeted[j]] : share[pos];
                const std::vector<double>& values = p.groups[order[pos]].values;
                for (size_t k = 0; k < values.size(); ++k)
                    list.push_back({w > 0 ? values[k] / w : kInfinity, int(pos), int(k) + 1, values[k], w});
            }
            sortByRatio(list);
            lists.push_back(std::move(list));
        }
    }

    // The smallest of the single-constraint bounds, for the groups from `depth` on and the remaining capacity rem.
    double bound(int depth, const Weights& rem) const {
        double surrogate = 0;
        for (int r : budgeted) surrogate += rem[r] / problem.capacity[r];
        double result = fractionalBound(lists.back(), depth, surrogate);
        for (size_t j = 0; j < budgeted.size(); ++j)
            result = std::min(result, fractionalBound(lists[j], depth, rem[budgeted[j]]));
        return result;
    }

    // Decides the trays of the group at position `depth` and below, with `current` already earned.
    void search(int depth, const Weights& rem, double current) {
        if (stopped) return;
        if (++nodes > nodeLimit) { stopped = true; return; }
        if ((nodes & 4095) == 0 && secondsSince(start) >= timeLimit) { stopped = true; return; }
        if (depth == int(order.size())) {
            if (current > best + EPS) { best = current; bestCount = chosen; }
            return;
        }
        if (current + bound(depth, rem) <= best + EPS) return;   // nothing below can beat the incumbent

        const std::vector<double>& values = problem.groups[order[depth]].values;
        double most = double(values.size());
        for (int r : active)
            if (weight[depth][r] > 0) most = std::min(most, std::floor((rem[r] + EPS) / weight[depth][r]));
        for (int k = int(std::max(most, 0.0)); k >= 0 && !stopped; --k) {   // the most trays first: good solutions early
            Weights next = rem;
            for (int r : active) next[r] -= k * weight[depth][r];
            chosen[depth] = k;
            search(depth + 1, next, current + prefix[depth][k]);
        }
        chosen[depth] = 0;
    }
};

}  // namespace

double knapsackValue(const KnapsackProblem& problem, const std::vector<int>& count) {
    double total = 0;
    for (size_t g = 0; g < problem.groups.size(); ++g)
        for (int k = 0; k < count[g]; ++k) total += problem.groups[g].values[k];
    return total;
}

KnapsackProblem buildKnapsackProblem(const Scenario& scenario, const std::vector<CropDemand>& demand,
                                     const ConstraintModel& constraints, const std::array<bool, kResources>& active) {
    if (demand.size() != scenario.crops.size() || constraints.maxTraysByArea.size() != scenario.crops.size())
        throw std::invalid_argument("buildKnapsackProblem needs one demand and one tray bound per crop");

    KnapsackProblem problem;
    long long area = 0;
    for (const Shelf& shelf : scenario.shelves) area += static_cast<long long>(shelf.width) * shelf.depth;
    const Resources& b = scenario.budget;
    problem.capacity = {b.labor, b.water, b.nutrient, b.energy, double(area)};
    problem.active = active;

    std::array<bool, kResources> noBudget{};   // active resources whose budget is zero
    for (int r = 0; r < kResources; ++r) noBudget[r] = active[r] && problem.capacity[r] <= 0;

    const Resources& cost = scenario.unitCost;
    for (const Crop& c : scenario.crops) {
        KnapsackGroup group;
        group.cropId = c.id;
        group.weight = {c.laborPerWeek, c.waterPerWeek, c.nutrientPerWeek, c.energyPerWeek,
                        static_cast<double>(c.trayW) * c.trayD};
        bool impossible = false;   // needs a resource that has no budget
        for (int r = 0; r < kResources; ++r)
            if (noBudget[r] && group.weight[r] > 0) impossible = true;
        if (impossible) continue;

        const CropDemand& market = demand[c.id];
        const double perWeek = c.yieldKg / c.cycleWeeks;   // kg a slot yields per week
        const double operating = c.trayCost / c.cycleWeeks + c.laborPerWeek * cost.labor + c.waterPerWeek * cost.water +
                                 c.nutrientPerWeek * cost.nutrient + c.energyPerWeek * cost.energy;
        const int cap = std::min(kMaxTraysPerCrop, constraints.maxTraysByArea[c.id]);
        for (int k = 1; k <= cap; ++k) {
            // The k-th tray sells what the market still takes after the first k-1, the rest at the salvage price.
            const double sold = std::min(std::max(market.demand - (k - 1) * perWeek, 0.0), perWeek);
            const double revenue = c.pricePerKg * (sold + c.salvageFraction * (perWeek - sold));
            // A tray that a contract needs carries the penalty its delivery avoids.
            const double bonus = market.contractSlots > 0 && k <= market.contractSlots ? market.penaltyPerWeek / market.contractSlots : 0.0;
            const double value = revenue - operating + bonus;
            if (value <= EPS) break;
            group.values.push_back(value);
        }
        if (!group.values.empty()) problem.groups.push_back(group);
    }
    for (int r = 0; r < kResources; ++r)
        if (noBudget[r]) problem.active[r] = false;   // the groups that remain do not use it
    return problem;
}

KnapsackResult solveGreedy(const KnapsackProblem& problem) {
    const auto start = Clock::now();
    refuseNaNCapacity(problem);
    const std::vector<int> active = activeResources(problem), budgeted = budgetedResources(problem);

    struct Copy { double density, value; int cropId, group, k; };
    std::vector<Copy> copies;
    for (size_t g = 0; g < problem.groups.size(); ++g) {
        const KnapsackGroup& group = problem.groups[g];
        for (size_t k = 0; k < group.values.size(); ++k)
            copies.push_back({density(group.values[k], group.weight, problem.capacity, budgeted), group.values[k],
                              group.cropId, int(g), int(k) + 1});
    }
    std::sort(copies.begin(), copies.end(), [](const Copy& a, const Copy& b) {   // a total order, so the result is deterministic
        if (a.density != b.density) return a.density > b.density;
        if (a.value != b.value) return a.value > b.value;
        return a.cropId != b.cropId ? a.cropId < b.cropId : a.k < b.k;
    });

    Weights remaining = problem.capacity;
    std::vector<int> count(problem.groups.size(), 0);
    for (const Copy& copy : copies) {
        const Weights& w = problem.groups[copy.group].weight;
        if (copy.k != count[copy.group] + 1 || !fits(w, remaining, active)) continue;   // only a crop's next tray, and only if it fits
        for (int r : active) remaining[r] -= w[r];
        ++count[copy.group];
    }
    return finish(problem, count, start);
}

KnapsackResult solveBranchAndBound(const KnapsackProblem& problem, long long nodeLimit, double timeLimitSeconds) {
    refuseNaNCapacity(problem);   // before the constructor, which sorts by densities
    BranchAndBound bb(problem, nodeLimit, timeLimitSeconds);
    const size_t n = problem.groups.size();

    const KnapsackResult greedy = solveGreedy(problem);   // the first incumbent, so the answer is never worse than greedy
    bb.best = greedy.value;
    bb.bestCount.assign(n, 0);
    for (size_t pos = 0; pos < n; ++pos) bb.bestCount[pos] = greedy.count[bb.order[pos]];
    bb.chosen.assign(n, 0);

    const double rootBound = bb.bound(0, problem.capacity);
    bb.search(0, problem.capacity, 0.0);

    std::vector<int> count(n, 0);
    for (size_t pos = 0; pos < n; ++pos) count[bb.order[pos]] = bb.bestCount[pos];
    KnapsackResult result = finish(problem, count, bb.start);
    result.upperBound = rootBound;
    result.nodes = bb.nodes;
    result.provenOptimal = !bb.stopped;
    return result;
}

// ---- dynamic programming ----------------------------------------------------------------------------------------

namespace {

// One copy that fits the grid: its group, its value and its position in the flattened table.
struct DpItem {
    int group;
    double value;
    std::size_t offset;
};

}  // namespace

KnapsackResult solveDp(const KnapsackProblem& problem, const std::array<double, kResources>& unitIn, std::size_t maxStates) {
    const auto start = Clock::now();
    const std::vector<int> res = activeResources(problem);   // one table dimension per active resource
    const int m = int(res.size());
    Weights unit = unitIn;
    for (int r : res) {
        if (!(unit[r] > 0)) throw std::invalid_argument("solveDp needs a positive unit for every active resource");
        // An unlimited budget has no grid (the search for the grid size below would never end); NaN fails this too.
        if (!std::isfinite(problem.capacity[r])) throw std::invalid_argument("solveDp needs a finite capacity for every active resource");
    }

    // Steps per resource at the requested units. Kept as doubles until the grid is known to be small.
    std::vector<double> steps(m);
    for (int j = 0; j < m; ++j) steps[j] = std::floor(problem.capacity[res[j]] / unit[res[j]] + EPS);
    // The table size if no resource gets more than `limit` steps.
    auto tableSize = [&](double limit) {
        double size = 1;
        for (int j = 0; j < m; ++j) size *= std::min(steps[j], limit) + 1;
        return size;
    };

    std::string note;
    bool coarsened = false;
    if (m > 0 && tableSize(kInfinity) > double(maxStates)) {
        // Too big: give every resource at most G steps, the largest G whose table still fits (at least 1).
        double low = 1, high = *std::max_element(steps.begin(), steps.end());
        while (low < high) {
            const double mid = std::floor((low + high + 1) / 2);
            if (tableSize(mid) <= double(maxStates)) low = mid; else high = mid - 1;
        }
        for (int j = 0; j < m; ++j) {
            if (steps[j] <= low) continue;
            unit[res[j]] = problem.capacity[res[j]] / low;
            steps[j] = low;
            coarsened = true;
        }
        if (coarsened) note = "grid coarsened to " + std::to_string(static_cast<long long>(low)) + " steps per resource";
    }

    // The table is flattened in mixed radix: index = sum of coordinate_j * radix_j.
    std::vector<std::size_t> radix(m), stepCount(m);
    std::size_t states = 1;
    for (int j = 0; j < m; ++j) {
        stepCount[j] = static_cast<std::size_t>(steps[j]);
        radix[j] = states;
        states *= stepCount[j] + 1;
    }

    // Weights rounded up to whole steps, so that every table solution is feasible for the real weights.
    const size_t n = problem.groups.size();
    std::vector<std::vector<std::size_t>> whole(n, std::vector<std::size_t>(m, 0));
    std::vector<char> usable(n, 1);   // a group with a weight beyond the grid can never be taken
    std::vector<DpItem> items;
    for (size_t g = 0; g < n; ++g) {
        std::size_t offset = 0;
        for (int j = 0; j < m && usable[g]; ++j) {
            const double w = std::max(std::ceil(problem.groups[g].weight[res[j]] / unit[res[j]] - EPS), 0.0);
            if (w > steps[j]) { usable[g] = 0; break; }
            whole[g][j] = static_cast<std::size_t>(w);
            offset += whole[g][j] * radix[j];
        }
        if (usable[g])
            for (double v : problem.groups[g].values) items.push_back({int(g), v, offset});
    }

    std::vector<double> best(states, 0.0);                          // best value within the allowance of each state
    std::vector<bool> took(items.size() * states, false);           // took[i * states + s]: item i improved state s
    std::vector<std::size_t> coordinate(m);
    for (size_t i = 0; i < items.size(); ++i) {
        const std::vector<std::size_t>& need = whole[items[i].group];
        for (int j = 0; j < m; ++j) coordinate[j] = stepCount[j];   // start at the last state
        // Descending order: best[s - offset] must still hold the value from before this item.
        for (std::size_t s = states; s-- > 0;) {
            bool enough = true;
            for (int j = 0; j < m; ++j)
                if (coordinate[j] < need[j]) { enough = false; break; }
            if (enough && best[s - items[i].offset] + items[i].value > best[s] + EPS) {
                best[s] = best[s - items[i].offset] + items[i].value;
                took[i * states + s] = true;
            }
            for (int j = 0; j < m; ++j) {   // move to the previous state: count down in mixed radix
                if (coordinate[j] > 0) { --coordinate[j]; break; }
                coordinate[j] = stepCount[j];
            }
        }
    }

    std::vector<int> count(n, 0);
    std::size_t s = states - 1;
    for (size_t i = items.size(); i-- > 0;)
        if (took[i * states + s]) {
            ++count[items[i].group];
            s -= items[i].offset;
        }

    // Exact only if no resource was coarsened and every weight and capacity is a whole number of units.
    auto wholeUnits = [](double amount, double u) { return std::fabs(amount / u - std::round(amount / u)) <= EPS; };
    bool exact = !coarsened;
    for (int r : res) {
        exact = exact && wholeUnits(problem.capacity[r], unit[r]);
        for (const KnapsackGroup& group : problem.groups) exact = exact && wholeUnits(group.weight[r], unit[r]);
    }

    KnapsackResult result = finish(problem, count, start);
    result.states = states;
    result.provenOptimal = exact;
    result.note = note;
    return result;
}
