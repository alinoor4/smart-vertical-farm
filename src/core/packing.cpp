#include "packing.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>
#include <set>
#include <stdexcept>

#include "rng.h"

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// ---- placeOne ---------------------------------------------------------------------------------------------------

// Where a tray goes: a shelf, a row of it (-1 for a new row) and the way the tray lies. shelf -1 means nowhere.
struct Choice {
    int shelf = -1, row = -1;
    Orientation turn;
};

// A shelf the crop may stand on and on which no conflicting crop stands (plan 10.2, "candidate shelves").
bool isCandidate(const PackState& state, const ConstraintModel& constraints, int crop, int shelf) {
    if (!constraints.compat[crop][shelf]) return false;
    const std::vector<char>& present = state.shelves[shelf].hasCrop;
    for (size_t other = 0; other < present.size(); ++other)
        if (present[other] && constraints.conflict[crop][other] != ConflictReason::None) return false;
    return true;
}

// The orientation for an existing row: it must fit the width that is left and be no deeper than the row; of those, the
// narrower one. Sums are in long long because the validator does not cap the sizes.
bool rowOrientation(const Orientations& turns, const Shelf& shelf, const Row& row, Orientation& chosen) {
    bool found = false;
    for (int i = 0; i < turns.count; ++i) {
        const Orientation& t = turns.option[i];
        if (static_cast<long long>(row.usedWidth) + t.w <= shelf.width && t.d <= row.depth && (!found || t.w < chosen.w)) {
            chosen = t;
            found = true;
        }
    }
    return found;
}

// The orientation for a new row: it must fit the shelf width and the depth that is left; of those, the one that lets more
// trays of this kind into the space that is left (the block count), and on a tie the wider one.
bool newRowOrientation(const Orientations& turns, const Shelf& shelf, int usedDepth, Orientation& chosen) {
    bool found = false;
    long long bestBlocks = 0;
    for (int i = 0; i < turns.count; ++i) {
        const Orientation& t = turns.option[i];
        if (t.w > shelf.width || static_cast<long long>(usedDepth) + t.d > shelf.depth) continue;
        const long long blocks = static_cast<long long>(shelf.width / t.w) * ((shelf.depth - usedDepth) / t.d);
        if (!found || blocks > bestBlocks || (blocks == bestBlocks && t.w > chosen.w)) {
            chosen = t;
            bestBlocks = blocks;
            found = true;
        }
    }
    return found;
}

// First-Fit: the first shelf, in id order, that has a row or room for a new row. On a shelf the rows come before a new row.
Choice chooseFirstFit(const PackState& state, const Scenario& scenario, const ConstraintModel& constraints, int crop,
                      const Orientations& turns) {
    for (int s = 0; s < int(scenario.shelves.size()); ++s) {
        if (!isCandidate(state, constraints, crop, s)) continue;
        const Shelf& shelf = scenario.shelves[s];
        const ShelfState& shelfState = state.shelves[s];
        Choice choice;
        choice.shelf = s;
        for (int r = 0; r < int(shelfState.rows.size()); ++r)
            if (rowOrientation(turns, shelf, shelfState.rows[r], choice.turn)) {
                choice.row = r;
                return choice;
            }
        if (newRowOrientation(turns, shelf, shelfState.usedDepth, choice.turn)) return choice;
    }
    return Choice();
}

// Best-Fit: the existing row that leaves the least width, then the best depth match; only when no row fits anywhere, the
// new row on a shelf already in use with the tightest depth, then on an empty shelf. Ties go to the lower shelf and row.
Choice chooseBestFit(const PackState& state, const Scenario& scenario, const ConstraintModel& constraints, int crop,
                     const Orientations& turns) {
    Choice best;
    std::array<long long, 4> bestKey{};
    for (int s = 0; s < int(scenario.shelves.size()); ++s) {
        if (!isCandidate(state, constraints, crop, s)) continue;
        const Shelf& shelf = scenario.shelves[s];
        const ShelfState& shelfState = state.shelves[s];
        for (int r = 0; r < int(shelfState.rows.size()); ++r) {
            const Row& row = shelfState.rows[r];
            Orientation turn;
            if (!rowOrientation(turns, shelf, row, turn)) continue;
            const std::array<long long, 4> key = {shelf.width - row.usedWidth - turn.w, row.depth - turn.d, s, r};
            if (best.shelf < 0 || key < bestKey) {
                best = {s, r, turn};
                bestKey = key;
            }
        }
    }
    if (best.shelf >= 0) return best;
    for (int s = 0; s < int(scenario.shelves.size()); ++s) {
        if (!isCandidate(state, constraints, crop, s)) continue;
        const Shelf& shelf = scenario.shelves[s];
        const ShelfState& shelfState = state.shelves[s];
        Orientation turn;
        if (!newRowOrientation(turns, shelf, shelfState.usedDepth, turn)) continue;
        const std::array<long long, 4> key = {shelfState.rows.empty() ? 1 : 0, shelf.depth - shelfState.usedDepth - turn.d, s, 0};   // shelves in use first
        if (best.shelf < 0 || key < bestKey) {
            best = {s, -1, turn};
            bestKey = key;
        }
    }
    return best;
}

// ---- results ----------------------------------------------------------------------------------------------------

// The value of the first `count` trays of a crop.
double prefixValue(const CropValues& values, int crop, int count) {
    if (crop >= int(values.size())) return 0.0;
    double total = 0;
    for (int k = 0; k < count && k < int(values[crop].size()); ++k) total += values[crop][k];
    return total;
}

// The area placed on each shelf, 0 for a shelf with nothing on it.
std::vector<long long> placedAreaPerShelf(const PackState& state, size_t shelves) {
    std::vector<long long> area(shelves, 0);
    for (const Placement& p : state.placements) area[p.shelfId] += static_cast<long long>(p.w) * p.d;
    return area;
}

PackResult makeResult(const char* algorithm, PackState state, std::vector<Tray> unplaced, const Scenario& scenario,
                      const CropValues& values, Clock::time_point start) {
    PackResult result;
    result.algorithm = algorithm;
    result.placedPerCrop.assign(scenario.crops.size(), 0);
    for (const Placement& p : state.placements) {
        ++result.placedPerCrop[p.cropId];
        result.placedArea += static_cast<long long>(p.w) * p.d;
    }
    const std::vector<long long> area = placedAreaPerShelf(state, scenario.shelves.size());
    for (size_t s = 0; s < scenario.shelves.size(); ++s) {
        const long long shelfArea = static_cast<long long>(scenario.shelves[s].width) * scenario.shelves[s].depth;
        result.totalShelfArea += shelfArea;
        if (area[s] > 0) result.usedShelfArea += shelfArea;
    }
    const PackFitness fitness = packFitness(state, scenario, values);
    result.placedValue = fitness.placedValue;
    result.shelvesUsed = fitness.shelvesUsed;
    result.state = std::move(state);
    result.unplaced = std::move(unplaced);
    result.seconds = secondsSince(start);
    return result;
}

// ---- decreasing order and the decoder ---------------------------------------------------------------------------

// Indexes into `trays`, longer side first, then shorter side, then crop id, then tray id (and the index, so that equal
// trays keep their order).
std::vector<int> decreasingIndices(const std::vector<Tray>& trays, const Scenario& scenario) {
    for (const Tray& tray : trays)
        if (tray.cropId < 0 || tray.cropId >= int(scenario.crops.size())) throw std::invalid_argument("a tray names a crop that does not exist");
    std::vector<int> order(trays.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Crop& ca = scenario.crops[trays[a].cropId];
        const Crop& cb = scenario.crops[trays[b].cropId];
        const int longA = std::max(ca.trayW, ca.trayD), longB = std::max(cb.trayW, cb.trayD);
        if (longA != longB) return longA > longB;
        const int shortA = std::min(ca.trayW, ca.trayD), shortB = std::min(cb.trayW, cb.trayD);
        if (shortA != shortB) return shortA > shortB;
        if (trays[a].cropId != trays[b].cropId) return trays[a].cropId < trays[b].cropId;
        if (trays[a].id != trays[b].id) return trays[a].id < trays[b].id;
        return a < b;
    });
    return order;
}

struct Decoded {
    PackState state;
    std::vector<Tray> unplaced;
};

// Starts from an empty layout and places the trays in the given order with the given policy.
Decoded decode(const std::vector<Tray>& trays, const std::vector<int>& order, FitPolicy policy, const Scenario& scenario,
               const ConstraintModel& constraints) {
    Decoded decoded;
    decoded.state = emptyPackState(scenario);
    for (int i : order)
        if (!placeOne(decoded.state, trays[i], scenario, constraints, policy)) decoded.unplaced.push_back(trays[i]);
    return decoded;
}

PackResult packDecreasing(const char* algorithm, FitPolicy policy, const std::vector<Tray>& trays, const Scenario& scenario,
                          const ConstraintModel& constraints, const CropValues& values) {
    const auto start = Clock::now();
    Decoded decoded = decode(trays, decreasingIndices(trays, scenario), policy, scenario, constraints);
    return makeResult(algorithm, std::move(decoded.state), std::move(decoded.unplaced), scenario, values, start);
}

// ---- the genetic algorithm --------------------------------------------------------------------------------------

struct Individual {
    std::vector<int> order;                  // a permutation of the tray indexes
    FitPolicy policy = FitPolicy::FirstFit;
    PackFitness fitness;
};

// Order crossover: the child keeps parent1[i..j] where it is and takes the other genes from parent2 in parent2's order.
std::vector<int> orderCrossover(const std::vector<int>& parent1, const std::vector<int>& parent2, int i, int j) {
    const int n = int(parent1.size());
    std::vector<int> child(n, -1);
    std::vector<char> taken(n, 0);
    for (int k = i; k <= j; ++k) {
        child[k] = parent1[k];
        taken[parent1[k]] = 1;
    }
    int next = 0;   // read position in parent2
    for (int k = 0; k < n; ++k) {
        if (k >= i && k <= j) continue;
        while (taken[parent2[next]]) ++next;
        child[k] = parent2[next++];
    }
    return child;
}

// The first value of the crop per square centimetre of tray, the ordering of the third starting individual.
double valueDensity(const Tray& tray, const Scenario& scenario, const CropValues& values) {
    const Crop& crop = scenario.crops[tray.cropId];
    const double first = tray.cropId < int(values.size()) && !values[tray.cropId].empty() ? values[tray.cropId][0] : 0.0;
    return first / (static_cast<double>(crop.trayW) * crop.trayD);
}

}  // namespace

Orientations orientationsOf(const Crop& crop) {
    Orientations turns;
    turns.option[0] = {crop.trayW, crop.trayD};
    if (crop.canRotate && crop.trayW != crop.trayD) {
        turns.option[1] = {crop.trayD, crop.trayW};
        turns.count = 2;
    }
    return turns;
}

PackState emptyPackState(const Scenario& scenario) {
    PackState state;
    state.shelves.assign(scenario.shelves.size(), ShelfState());
    for (ShelfState& shelf : state.shelves) shelf.hasCrop.assign(scenario.crops.size(), 0);
    return state;
}

bool placeOne(PackState& state, const Tray& tray, const Scenario& scenario, const ConstraintModel& constraints,
              FitPolicy policy) {
    if (tray.cropId < 0 || tray.cropId >= int(scenario.crops.size()) || state.shelves.size() != scenario.shelves.size() ||
        constraints.compat.size() != scenario.crops.size() || constraints.conflict.size() != scenario.crops.size())
        throw std::invalid_argument("placeOne: the tray, the state and the constraint model must match the scenario");

    const Orientations turns = orientationsOf(scenario.crops[tray.cropId]);
    const Choice choice = policy == FitPolicy::FirstFit ? chooseFirstFit(state, scenario, constraints, tray.cropId, turns)
                                                        : chooseBestFit(state, scenario, constraints, tray.cropId, turns);
    if (choice.shelf < 0) return false;

    ShelfState& shelf = state.shelves[choice.shelf];
    int rowIndex = choice.row;
    if (rowIndex < 0) {   // a new row takes the depth of its first tray
        shelf.rows.push_back({shelf.usedDepth, choice.turn.d, 0});
        shelf.usedDepth += choice.turn.d;
        rowIndex = int(shelf.rows.size()) - 1;
    }
    Row& row = shelf.rows[rowIndex];
    state.placements.push_back({tray.id, tray.cropId, choice.shelf, row.usedWidth, row.y, choice.turn.w, choice.turn.d});
    row.usedWidth += choice.turn.w;
    shelf.hasCrop[tray.cropId] = 1;
    return true;
}

std::vector<std::string> validatePacking(const PackState& state, const Scenario& scenario, const ConstraintModel& constraints) {
    std::vector<std::string> problems;
    std::vector<std::vector<const Placement*>> onShelf(scenario.shelves.size());
    std::vector<int> ids;
    for (const Placement& p : state.placements) {
        ids.push_back(p.trayId);
        const std::string tray = "tray " + std::to_string(p.trayId);
        if (p.cropId < 0 || p.cropId >= int(scenario.crops.size())) { problems.push_back(tray + " names a crop that does not exist"); continue; }
        if (p.shelfId < 0 || p.shelfId >= int(scenario.shelves.size())) { problems.push_back(tray + " names a shelf that does not exist"); continue; }
        const Crop& crop = scenario.crops[p.cropId];
        const Shelf& shelf = scenario.shelves[p.shelfId];
        const std::string where = tray + " (" + crop.name + ") on shelf " + shelf.name;

        if (p.x < 0 || p.y < 0 || static_cast<long long>(p.x) + p.w > shelf.width || static_cast<long long>(p.y) + p.d > shelf.depth)
            problems.push_back(where + " lies outside the shelf");
        const Orientations turns = orientationsOf(crop);
        bool knownSize = false;
        for (int i = 0; i < turns.count; ++i) knownSize = knownSize || (p.w == turns.option[i].w && p.d == turns.option[i].d);
        if (!knownSize) problems.push_back(where + " has size " + std::to_string(p.w) + " x " + std::to_string(p.d) + ", which is no orientation of its crop");
        if (!constraints.compat[p.cropId][p.shelfId]) problems.push_back(where + ": the crop may not stand on this shelf");
        onShelf[p.shelfId].push_back(&p);
    }

    std::sort(ids.begin(), ids.end());
    for (size_t i = 1; i < ids.size(); ++i)
        if (ids[i] == ids[i - 1] && (i < 2 || ids[i] != ids[i - 2])) problems.push_back("tray id " + std::to_string(ids[i]) + " appears more than once");

    for (size_t s = 0; s < onShelf.size(); ++s) {
        std::set<std::pair<int, int>> conflictsReported;   // one message per pair of crops, not per pair of trays
        const std::vector<const Placement*>& list = onShelf[s];
        for (size_t i = 0; i < list.size(); ++i)
            for (size_t j = i + 1; j < list.size(); ++j) {
                const Placement& a = *list[i];
                const Placement& b = *list[j];
                const bool apart = static_cast<long long>(a.x) + a.w <= b.x || static_cast<long long>(b.x) + b.w <= a.x ||
                                   static_cast<long long>(a.y) + a.d <= b.y || static_cast<long long>(b.y) + b.d <= a.y;
                if (!apart) problems.push_back("trays " + std::to_string(a.trayId) + " and " + std::to_string(b.trayId) + " overlap on shelf " + scenario.shelves[s].name);
                if (a.cropId != b.cropId && constraints.conflict[a.cropId][b.cropId] != ConflictReason::None &&
                    conflictsReported.insert({std::min(a.cropId, b.cropId), std::max(a.cropId, b.cropId)}).second)
                    problems.push_back(scenario.crops[a.cropId].name + " and " + scenario.crops[b.cropId].name + " conflict but share shelf " + scenario.shelves[s].name);
            }
    }
    return problems;
}

PackFitness packFitness(const PackState& state, const Scenario& scenario, const CropValues& values) {
    std::vector<int> count(scenario.crops.size(), 0);
    for (const Placement& p : state.placements) ++count[p.cropId];
    PackFitness fitness;
    for (size_t c = 0; c < count.size(); ++c) fitness.placedValue += prefixValue(values, int(c), count[c]);
    const std::vector<long long> area = placedAreaPerShelf(state, scenario.shelves.size());
    for (size_t s = 0; s < area.size(); ++s) {
        if (area[s] == 0) continue;
        ++fitness.shelvesUsed;
        const double share = static_cast<double>(area[s]) / (static_cast<double>(scenario.shelves[s].width) * scenario.shelves[s].depth);
        fitness.concentration += share * share;
    }
    return fitness;
}

bool fitterThan(const PackFitness& a, const PackFitness& b) {
    if (a.placedValue > b.placedValue + EPS) return true;
    if (a.placedValue < b.placedValue - EPS) return false;
    if (a.shelvesUsed != b.shelvesUsed) return a.shelvesUsed < b.shelvesUsed;
    return a.concentration > b.concentration + EPS;
}

std::vector<Tray> traysForCounts(const std::vector<int>& countPerCrop) {
    std::vector<Tray> trays;
    for (size_t c = 0; c < countPerCrop.size(); ++c)
        for (int k = 0; k < countPerCrop[c]; ++k) trays.push_back({int(trays.size()), int(c)});
    return trays;
}

std::vector<Tray> decreasingOrder(const std::vector<Tray>& trays, const Scenario& scenario) {
    std::vector<Tray> sorted;
    for (int i : decreasingIndices(trays, scenario)) sorted.push_back(trays[i]);
    return sorted;
}

PackResult packFirstFitDecreasing(const std::vector<Tray>& trays, const Scenario& scenario, const ConstraintModel& constraints,
                                  const CropValues& values) {
    return packDecreasing("First-Fit Decreasing", FitPolicy::FirstFit, trays, scenario, constraints, values);
}

PackResult packBestFitDecreasing(const std::vector<Tray>& trays, const Scenario& scenario, const ConstraintModel& constraints,
                                 const CropValues& values) {
    return packDecreasing("Best-Fit Decreasing", FitPolicy::BestFit, trays, scenario, constraints, values);
}

// The random draws, in order, so that a seed always gives the same run. Starting population: one shuffle (n - 1 draws,
// i from n-1 down to 1, swapping with below(i + 1)) and one draw for the policy per random individual. Each child:
// three draws for the first parent, three for the second, one unit draw for the crossover, two draws for its cut points
// when it happens, one draw for the policy, one unit draw for the swap and two draws for its positions when it happens,
// one unit draw for the policy flip.
PackResult packGenetic(const std::vector<Tray>& trays, const Scenario& scenario, const ConstraintModel& constraints,
                       const CropValues& values, const GaParams& params) {
    if (params.population < 3 || params.generations < 0 || params.stallGenerations < 1 || !(params.crossoverRate >= 0 && params.crossoverRate <= 1) ||
        !(params.mutationRate >= 0 && params.mutationRate <= 1))
        throw std::invalid_argument("packGenetic: population must be at least 3, generations not negative, the stall limit at least 1 and the rates within 0..1");
    const auto start = Clock::now();
    const std::vector<int> decreasing = decreasingIndices(trays, scenario);
    const int n = int(trays.size()), size = params.population;
    Rng rng(params.seed);

    auto evaluate = [&](Individual& individual) {
        individual.fitness = packFitness(decode(trays, individual.order, individual.policy, scenario, constraints).state, scenario, values);
    };

    std::vector<Individual> population(3);
    population[0].order = decreasing;                       // (a) decreasing order, First-Fit
    population[1].order = decreasing;                       // (b) decreasing order, Best-Fit
    population[1].policy = FitPolicy::BestFit;
    population[2].order = decreasing;                       // (c) most valuable per square centimetre first, First-Fit
    std::sort(population[2].order.begin(), population[2].order.end(), [&](int a, int b) {
        const double densityA = valueDensity(trays[a], scenario, values), densityB = valueDensity(trays[b], scenario, values);
        if (densityA != densityB) return densityA > densityB;
        if (trays[a].cropId != trays[b].cropId) return trays[a].cropId < trays[b].cropId;
        return trays[a].id != trays[b].id ? trays[a].id < trays[b].id : a < b;
    });
    for (int i = 3; i < size; ++i) {                        // the rest: shuffles of the decreasing order, a random policy
        Individual individual;
        individual.order = decreasing;
        for (int k = n - 1; k > 0; --k) std::swap(individual.order[k], individual.order[rng.below(k + 1)]);
        individual.policy = rng.below(2) == 0 ? FitPolicy::FirstFit : FitPolicy::BestFit;
        population.push_back(individual);
    }
    for (Individual& individual : population) evaluate(individual);

    // The index of the fittest individual, and of the fittest of the others; the lower index wins a tie.
    auto bestTwo = [](const std::vector<Individual>& group) {
        int first = 0, second = -1;
        for (int i = 1; i < int(group.size()); ++i)
            if (fitterThan(group[i].fitness, group[first].fitness)) { second = first; first = i; }
            else if (second < 0 || fitterThan(group[i].fitness, group[second].fitness)) second = i;
        return std::array<int, 2>{{first, second}};
    };
    auto tournament = [&]() {   // three uniform draws with replacement; the fittest wins, the earlier draw on a tie
        int winner = rng.below(size);
        for (int round = 0; round < 2; ++round) {
            const int challenger = rng.below(size);
            if (fitterThan(population[challenger].fitness, population[winner].fitness)) winner = challenger;
        }
        return winner;
    };

    PackFitness best = population[bestTwo(population)[0]].fitness;
    int generationsRun = 0, stalled = 0;
    std::vector<double> convergence = {best.placedValue};
    while (n > 0 && generationsRun < params.generations && stalled < params.stallGenerations) {
        const std::array<int, 2> elite = bestTwo(population);
        std::vector<Individual> next = {population[elite[0]], population[elite[1]]};   // the best two survive unchanged
        while (int(next.size()) < size) {
            const Individual& parent1 = population[tournament()];
            const Individual& parent2 = population[tournament()];
            Individual child;
            if (rng.unit() < params.crossoverRate) {
                int i = rng.below(n), j = rng.below(n);
                if (i > j) std::swap(i, j);
                child.order = orderCrossover(parent1.order, parent2.order, i, j);
            } else {
                child.order = parent1.order;
            }
            child.policy = rng.below(2) == 0 ? parent1.policy : parent2.policy;
            if (rng.unit() < params.mutationRate) {
                const int i = rng.below(n), j = rng.below(n);
                std::swap(child.order[i], child.order[j]);
            }
            if (rng.unit() < 0.05) child.policy = child.policy == FitPolicy::FirstFit ? FitPolicy::BestFit : FitPolicy::FirstFit;
            evaluate(child);
            next.push_back(std::move(child));
        }
        population = std::move(next);
        ++generationsRun;
        const PackFitness top = population[bestTwo(population)[0]].fitness;
        if (fitterThan(top, best)) { best = top; stalled = 0; } else { ++stalled; }
        convergence.push_back(top.placedValue);
    }

    const Individual& winner = population[bestTwo(population)[0]];
    Decoded decoded = decode(trays, winner.order, winner.policy, scenario, constraints);
    PackResult result = makeResult("Genetic algorithm", std::move(decoded.state), std::move(decoded.unplaced), scenario, values, start);
    result.generationsRun = generationsRun;
    result.convergence = std::move(convergence);
    return result;
}
