#pragma once
// Component 4 (plan section 10): putting the trays the knapsack chose onto the shelves. Each shelf is filled in rows that
// run along its width and stack along its depth. One function, placeOne, places one tray and never breaks a rule; the
// three packers (First-Fit Decreasing, Best-Fit Decreasing, a genetic algorithm) differ only in the order and the
// policy they call it with.
#include <string>
#include <utility>
#include <vector>

#include "constraints.h"
#include "model.h"

struct Tray { int id = 0; int cropId = 0; };
struct Placement { int trayId = 0, cropId = 0, shelfId = 0, x = 0, y = 0, w = 0, d = 0; };   // w along the shelf, d into it
enum class FitPolicy { FirstFit, BestFit };

struct Row { int y = 0, depth = 0, usedWidth = 0; };
struct ShelfState {
    std::vector<Row> rows;
    int usedDepth = 0;
    std::vector<char> hasCrop;       // indexed by crop id: 1 when a tray of that crop stands on the shelf
};
struct PackState {
    std::vector<ShelfState> shelves;
    std::vector<Placement> placements;
};

// The ways a tray can lie on a shelf, as (width along the shelf, depth into it): as given, and turned a quarter when the
// crop may rotate and the tray is not square. At most two.
struct Orientation { int w = 0, d = 0; };
struct Orientations {
    Orientation option[2];
    int count = 1;
};
Orientations orientationsOf(const Crop& crop);

// A state with every shelf empty, for a scenario.
PackState emptyPackState(const Scenario& scenario);

// Places one tray (plan 10.2): on a shelf that suits its crop and holds no conflicting crop, in an existing row or a new
// row, by the first-fit or best-fit rule. Changes nothing and returns false when no place exists. Throws
// std::invalid_argument when the tray's crop, or the sizes of the state and the constraint model, do not match the
// scenario. Time O(S * (C + R)) for S shelves, C crops and R rows.
bool placeOne(PackState& state, const Tray& tray, const Scenario& scenario, const ConstraintModel& constraints,
              FitPolicy policy);

// Why a layout is wrong, one message per violation (plan 10.5); empty when it is sound: every tray lies inside its shelf
// in one of its crop's orientations, on a compatible shelf, overlaps no other tray, shares no shelf with a conflicting
// crop, and no tray id appears twice. Time O(n^2) for n trays on one shelf.
std::vector<std::string> validatePacking(const PackState& state, const Scenario& scenario, const ConstraintModel& constraints);

// What each crop's trays are worth, by crop id: valuesByCrop[c][k-1] is the value of its k-th tray. A crop with more
// trays than values earns nothing for the extra ones, and a crop with no list earns nothing.
using CropValues = std::vector<std::vector<double>>;

struct PackResult {
    std::string algorithm;
    PackState state;                           // final state, reused by the refill step
    std::vector<Tray> unplaced;
    std::vector<int> placedPerCrop;
    double placedValue = 0;                    // sum over crops of the first placedPerCrop[c] values
    int shelvesUsed = 0;
    long long placedArea = 0, usedShelfArea = 0, totalShelfArea = 0;
    int generationsRun = 0;                    // genetic algorithm only
    std::vector<double> convergence;           // genetic algorithm only: best placedValue at the start, then after each generation
    double seconds = 0;
};

// How good a layout is (plan 10.4), compared in this order: more placed value, then fewer shelves in use, then a larger
// sum over the used shelves of (placed area / shelf area)^2, which rewards filling a shelf rather than spreading out.
struct PackFitness {
    double placedValue = 0;
    int shelvesUsed = 0;
    double concentration = 0;
};
PackFitness packFitness(const PackState& state, const Scenario& scenario, const CropValues& values);
bool fitterThan(const PackFitness& a, const PackFitness& b);   // strictly better; doubles are compared with EPS

// Trays 0, 1, 2, .. for a number of trays per crop id, crops in id order, each crop's trays consecutive (pipeline step 6).
std::vector<Tray> traysForCounts(const std::vector<int>& countPerCrop);

// The trays by longer side descending, then shorter side, then crop id, then tray id (plan 10.3). Time O(n log n).
std::vector<Tray> decreasingOrder(const std::vector<Tray>& trays, const Scenario& scenario);

// Variants 1 and 2: place the trays in decreasing order with the first-fit or the best-fit rule. Trays that find no place
// are in `unplaced`, in the order tried. Time O(n log n + n * S * (C + R)), which is O(n^2) when R grows with n.
PackResult packFirstFitDecreasing(const std::vector<Tray>& trays, const Scenario& scenario, const ConstraintModel& constraints,
                                  const CropValues& values);
PackResult packBestFitDecreasing(const std::vector<Tray>& trays, const Scenario& scenario, const ConstraintModel& constraints,
                                 const CropValues& values);

struct GaParams {
    int population = 40, generations = 150, stallGenerations = 40;
    double crossoverRate = 0.9, mutationRate = 0.3;
    unsigned long long seed = 42;
};

// Variant 3 (plan 10.4): an order-based genetic algorithm whose decoder is placeOne, so every individual is a valid layout.
// The first population holds both decreasing orders, so the result is never worse than First-Fit or Best-Fit Decreasing.
// The same seed gives the same result on every machine. Throws std::invalid_argument for a population below 3, a negative
// number of generations, a stall limit below 1 or a rate outside 0..1.
// Time O(G * P * n * S * (C + R)) for G generations and a population of P; space O(P * n).
PackResult packGenetic(const std::vector<Tray>& trays, const Scenario& scenario, const ConstraintModel& constraints,
                       const CropValues& values, const GaParams& params = GaParams());
