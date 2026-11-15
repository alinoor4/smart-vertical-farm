// Component 4 (plan section 10): packing trays onto shelves. Expected numbers come from plan Appendix A (G4, G6), from
// hand calculation where a comment says so, and from plan/reference_model.py.
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "farm.h"
#include "packing.h"
#include "rng.h"

namespace {

using Counts = std::vector<int>;

Crop trayCrop(int id, int w, int d, bool rotate = true) {
    Crop c;
    c.id = id;
    c.name = "T" + std::to_string(id);
    c.trayW = w;
    c.trayD = d;
    c.canRotate = rotate;
    c.height = 20;   // the same for every crop, so that no pair conflicts by height
    return c;
}

Shelf shelfOf(int id, int w, int d) {
    Shelf s;
    s.id = id;
    s.name = "S" + std::to_string(id);
    s.width = w;
    s.depth = d;
    s.clearance = 100;
    return s;
}

// A scenario with only crops and shelves, and the constraint model built from it.
struct Setup {
    Scenario scenario;
    ConstraintModel constraints;
};

Setup makeSetup(const std::vector<Crop>& crops, const std::vector<Shelf>& shelves) {
    Setup setup;
    setup.scenario.crops = crops;
    setup.scenario.shelves = shelves;
    setup.constraints = buildConstraints(setup.scenario);
    return setup;
}

// Every crop on every shelf and no conflicts, whatever the sizes, so that only the geometry decides.
void allowAll(Setup& setup) {
    for (auto& row : setup.constraints.compat) std::fill(row.begin(), row.end(), char(1));
    for (auto& row : setup.constraints.conflict) std::fill(row.begin(), row.end(), ConflictReason::None);
}

struct Spot {
    int shelf = -1, x = 0, y = 0, w = 0, d = 0;
    bool operator==(const Spot& o) const { return shelf == o.shelf && x == o.x && y == o.y && w == o.w && d == o.d; }
};

Spot spotOf(const PackState& state, int trayId) {
    for (const Placement& p : state.placements)
        if (p.trayId == trayId) return {p.shelfId, p.x, p.y, p.w, p.d};
    return Spot();   // shelf -1: not placed
}

std::vector<int> ids(const std::vector<Tray>& trays) {
    std::vector<int> out;
    for (const Tray& t : trays) out.push_back(t.id);
    return out;
}

// Places the trays one by one; returns how many were placed.
int placeAll(PackState& state, const Setup& setup, const std::vector<Tray>& trays, FitPolicy policy) {
    int placed = 0;
    for (const Tray& t : trays) placed += placeOne(state, t, setup.scenario, setup.constraints, policy) ? 1 : 0;
    return placed;
}

std::string joined(const std::vector<std::string>& messages) {
    std::string out;
    for (const std::string& m : messages) out += m + "; ";
    return out;
}

}  // namespace

// G4: two shelves and four rotatable trays where First-Fit places all four and Best-Fit three, with the coordinates.
TEST(packing_g4_first_fit_and_best_fit) {
    const Setup setup = makeSetup({trayCrop(0, 80, 30), trayCrop(1, 60, 40), trayCrop(2, 45, 35), trayCrop(3, 60, 40)},
                                  {shelfOf(0, 100, 70), shelfOf(1, 100, 60)});
    const std::vector<Tray> trays = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    const CropValues values(4, std::vector<double>{1.0});
    CHECK(ids(decreasingOrder(trays, setup.scenario)) == std::vector<int>({0, 1, 3, 2}));

    const PackResult ff = packFirstFitDecreasing(trays, setup.scenario, setup.constraints, values);
    CHECK(ff.unplaced.empty() && ff.state.placements.size() == 4);
    CHECK(spotOf(ff.state, 0) == (Spot{0, 0, 0, 80, 30}));
    CHECK(spotOf(ff.state, 1) == (Spot{0, 0, 30, 60, 40}));
    CHECK(spotOf(ff.state, 3) == (Spot{1, 0, 0, 40, 60}));
    CHECK(spotOf(ff.state, 2) == (Spot{1, 40, 0, 35, 45}));
    CHECK(validatePacking(ff.state, setup.scenario, setup.constraints).empty());

    const PackResult bf = packBestFitDecreasing(trays, setup.scenario, setup.constraints, values);
    CHECK(ids(bf.unplaced) == std::vector<int>({2}) && bf.state.placements.size() == 3);
    CHECK(spotOf(bf.state, 0) == (Spot{1, 0, 0, 80, 30}));
    CHECK(spotOf(bf.state, 1) == (Spot{0, 0, 0, 40, 60}));
    CHECK(spotOf(bf.state, 3) == (Spot{0, 40, 0, 40, 60}));
    CHECK(spotOf(bf.state, 2).shelf == -1);
    CHECK(validatePacking(bf.state, setup.scenario, setup.constraints).empty());
}

// G4: the orientation chosen for the first row of an empty shelf, which maximises the number of trays of this kind that
// the space holds; on a tie the wider one.
TEST(packing_g4_new_row_orientation) {
    struct Case { int shelfW, shelfD, trayW, trayD, w, d; };
    const Case cases[] = {
        {240, 60, 60, 40, 40, 60},    // 6 blocks turned, 4 as given
        {240, 60, 50, 25, 25, 50},    // 9 against 8
        {240, 60, 50, 30, 50, 30},    // 8 against 8: the wider wins the tie
        {200, 80, 60, 40, 60, 40},    // 6 against 5
        {200, 80, 100, 30, 100, 30},  // 4; turned it would be 100 deep, and the shelf has 80
        {180, 50, 50, 25, 25, 50},    // 7 against 6
        {120, 120, 60, 40, 60, 40},   // 6 against 6: the wider wins the tie
        {120, 120, 40, 60, 60, 40},   // the same shelf and tray, given the other way round, gives the same answer
    };
    for (const Case& c : cases) {
        Setup setup = makeSetup({trayCrop(0, c.trayW, c.trayD)}, {shelfOf(0, c.shelfW, c.shelfD)});
        allowAll(setup);
        PackState state = emptyPackState(setup.scenario);
        CHECK(placeOne(state, {0, 0}, setup.scenario, setup.constraints, FitPolicy::FirstFit));
        CHECK(spotOf(state, 0) == (Spot{0, 0, 0, c.w, c.d}));
        PackState best = emptyPackState(setup.scenario);   // the rule is the same for Best-Fit
        CHECK(placeOne(best, {0, 0}, setup.scenario, setup.constraints, FitPolicy::BestFit));
        CHECK(spotOf(best, 0) == (Spot{0, 0, 0, c.w, c.d}));
    }
    // A tray that may not rotate stays as it is, and a square tray has one orientation.
    CHECK(orientationsOf(trayCrop(0, 60, 40, false)).count == 1);
    CHECK(orientationsOf(trayCrop(0, 40, 40)).count == 1);
    const Orientations turned = orientationsOf(trayCrop(0, 60, 40));
    CHECK(turned.count == 2 && turned.option[0].w == 60 && turned.option[0].d == 40 && turned.option[1].w == 40 && turned.option[1].d == 60);
}

// The rules of 10.2 for rows, one case each.
TEST(packing_rows_and_orientations) {
    // In an existing row the narrower orientation wins. The first tray, 20 x 60 and fixed, makes a row 60 deep; then a
    // rotatable 50 x 30 tray fits it either way and takes the narrow way, 30 x 50.
    Setup narrow = makeSetup({trayCrop(0, 20, 60, false), trayCrop(1, 50, 30)}, {shelfOf(0, 200, 100)});
    allowAll(narrow);
    PackState state = emptyPackState(narrow.scenario);
    CHECK(placeAll(state, narrow, {{0, 0}, {1, 1}}, FitPolicy::FirstFit) == 2);
    CHECK(spotOf(state, 1) == (Spot{0, 20, 0, 30, 50}));
    state = emptyPackState(narrow.scenario);
    CHECK(placeAll(state, narrow, {{0, 0}, {1, 1}}, FitPolicy::BestFit) == 2);
    CHECK(spotOf(state, 1) == (Spot{0, 20, 0, 30, 50}));

    // A tray deeper than the row, and one too wide for what is left of it, open a new row below it.
    Setup rows = makeSetup({trayCrop(0, 60, 30, false), trayCrop(1, 40, 40, false), trayCrop(2, 50, 20, false)}, {shelfOf(0, 100, 100)});
    allowAll(rows);
    state = emptyPackState(rows.scenario);
    CHECK(placeAll(state, rows, {{0, 0}, {1, 1}, {2, 2}}, FitPolicy::FirstFit) == 3);
    CHECK(spotOf(state, 0) == (Spot{0, 0, 0, 60, 30}));
    CHECK(spotOf(state, 1) == (Spot{0, 0, 30, 40, 40}));    // 40 deep, the row is 30: a new row at y = 30
    CHECK(spotOf(state, 2) == (Spot{0, 40, 30, 50, 20}));   // beside the second tray, the row is 40 deep
    CHECK(state.shelves[0].rows.size() == 2 && state.shelves[0].usedDepth == 70);
    CHECK(state.shelves[0].rows[0].usedWidth == 60 && state.shelves[0].rows[1].usedWidth == 90);

    // The shelf is full when neither a row nor a new row fits; nothing changes then.
    Setup full = makeSetup({trayCrop(0, 100, 50, false)}, {shelfOf(0, 100, 100)});
    allowAll(full);
    state = emptyPackState(full.scenario);
    CHECK(placeAll(state, full, {{0, 0}, {1, 0}}, FitPolicy::FirstFit) == 2);
    const PackState before = state;
    CHECK(!placeOne(state, {2, 0}, full.scenario, full.constraints, FitPolicy::FirstFit));
    CHECK(!placeOne(state, {2, 0}, full.scenario, full.constraints, FitPolicy::BestFit));
    CHECK(state.placements.size() == before.placements.size() && state.shelves[0].rows.size() == 2 && state.shelves[0].usedDepth == 100);
    CHECK(state.shelves[0].rows[0].usedWidth == 100 && state.shelves[0].rows[1].usedWidth == 100);
}

// First-Fit tries a new row on shelf 0 before an existing row on shelf 1; Best-Fit looks at every existing row first.
TEST(packing_first_fit_and_best_fit_differ) {
    Setup setup = makeSetup({trayCrop(0, 40, 40, false), trayCrop(1, 30, 30, false)}, {shelfOf(0, 100, 50), shelfOf(1, 100, 50)});
    allowAll(setup);
    setup.constraints.compat[0][0] = 0;   // the first crop may stand on shelf 1 only
    PackState ff = emptyPackState(setup.scenario), bf = emptyPackState(setup.scenario);
    CHECK(placeAll(ff, setup, {{0, 0}, {1, 1}}, FitPolicy::FirstFit) == 2);
    CHECK(placeAll(bf, setup, {{0, 0}, {1, 1}}, FitPolicy::BestFit) == 2);
    CHECK(spotOf(ff, 0) == (Spot{1, 0, 0, 40, 40}) && spotOf(ff, 1) == (Spot{0, 0, 0, 30, 30}));
    CHECK(spotOf(bf, 0) == (Spot{1, 0, 0, 40, 40}) && spotOf(bf, 1) == (Spot{1, 40, 0, 30, 30}));
}

// Best-Fit's key for an existing row: least width left, then the best depth match, then the lower shelf, then the lower row.
TEST(packing_best_fit_keys) {
    // Two shelves, each with one row made by a tray only that shelf accepts; then a 20 x 30 tray that both accept.
    auto run = [](int widthA, int depthA, int widthB, int depthB, FitPolicy policy) {
        Setup setup = makeSetup({trayCrop(0, widthA, depthA, false), trayCrop(1, widthB, depthB, false), trayCrop(2, 20, 30, false)},
                                {shelfOf(0, 100, 100), shelfOf(1, 100, 100)});
        allowAll(setup);
        setup.constraints.compat[0][1] = 0;   // crop 0 on shelf 0 only, crop 1 on shelf 1 only
        setup.constraints.compat[1][0] = 0;
        PackState state = emptyPackState(setup.scenario);
        CHECK(placeAll(state, setup, {{0, 0}, {1, 1}, {2, 2}}, policy) == 3);
        return spotOf(state, 2);
    };
    // Width first: shelf 0 would leave 10 and a depth gap of 10, shelf 1 leaves 20 and a gap of 0: shelf 0.
    CHECK(run(70, 40, 60, 30, FitPolicy::BestFit) == (Spot{0, 70, 0, 20, 30}));
    // Equal width left (20): the depth decides, shelf 1 has the closer row (30 deep, tray 30).
    CHECK(run(60, 40, 60, 30, FitPolicy::BestFit) == (Spot{1, 60, 0, 20, 30}));
    // First-Fit simply takes the first shelf with room.
    CHECK(run(60, 40, 60, 30, FitPolicy::FirstFit) == (Spot{0, 60, 0, 20, 30}));
    // Equal in both: the lower shelf.
    CHECK(run(60, 30, 60, 30, FitPolicy::BestFit) == (Spot{0, 60, 0, 20, 30}));
    // On one shelf 80 wide: a 50-wide row and a 70-wide row. A 10-wide tray leaves 20 in the first and 0 in the second:
    // Best-Fit takes the tighter second row, First-Fit the first row that fits.
    Setup rows = makeSetup({trayCrop(0, 50, 30, false), trayCrop(1, 70, 30, false), trayCrop(2, 10, 30, false)}, {shelfOf(0, 80, 100)});
    allowAll(rows);
    PackState best = emptyPackState(rows.scenario), first = emptyPackState(rows.scenario);
    CHECK(placeAll(best, rows, {{0, 0}, {1, 1}, {2, 2}}, FitPolicy::BestFit) == 3);
    CHECK(placeAll(first, rows, {{0, 0}, {1, 1}, {2, 2}}, FitPolicy::FirstFit) == 3);
    CHECK(spotOf(best, 2) == (Spot{0, 70, 30, 10, 30}));
    CHECK(spotOf(first, 2) == (Spot{0, 50, 0, 10, 30}));
    // Two rows alike in every way: the lower row.
    Setup twins = makeSetup({trayCrop(0, 50, 30, false), trayCrop(1, 20, 30, false)}, {shelfOf(0, 80, 100)});
    allowAll(twins);
    PackState state = emptyPackState(twins.scenario);
    CHECK(placeAll(state, twins, {{0, 0}, {1, 0}, {2, 1}}, FitPolicy::BestFit) == 3);   // two 50-wide trays cannot share a row of 80
    CHECK(state.shelves[0].rows.size() == 2);
    CHECK(spotOf(state, 2) == (Spot{0, 50, 0, 20, 30}));
}

// Best-Fit opens a new row only when no existing row anywhere fits: on a shelf already in use first, then the tightest depth.
TEST(packing_best_fit_new_rows) {
    // Shelf 1 holds a full-width row; shelf 0 is empty. A 50 x 30 tray goes to shelf 1 (in use) under Best-Fit and shelf 0 under First-Fit.
    auto build = [](FitPolicy policy, int depthUsedOnShelf0) {
        Setup setup = makeSetup({trayCrop(0, 100, 40, false), trayCrop(1, 100, 60, false), trayCrop(2, 50, 30, false)},
                                {shelfOf(0, 100, 100), shelfOf(1, 100, 100)});
        allowAll(setup);
        setup.constraints.compat[0][1] = 0;   // crop 0 on shelf 0 only, crop 1 on shelf 1 only
        setup.constraints.compat[1][0] = 0;
        PackState state = emptyPackState(setup.scenario);
        std::vector<Tray> trays = {{1, 1}, {2, 2}};
        if (depthUsedOnShelf0 > 0) trays.insert(trays.begin(), Tray{0, 0});
        const int placed = placeAll(state, setup, trays, policy);
        CHECK(placed == int(trays.size()));
        return spotOf(state, 2);
    };
    CHECK(build(FitPolicy::BestFit, 0) == (Spot{1, 0, 60, 50, 30}));    // shelf 1 is in use, shelf 0 is not
    CHECK(build(FitPolicy::FirstFit, 0) == (Spot{0, 0, 0, 50, 30}));
    // Both in use: shelf 0 has 100 - 40 - 30 = 30 left, shelf 1 has 100 - 60 - 30 = 10 left: the tighter shelf 1.
    CHECK(build(FitPolicy::BestFit, 40) == (Spot{1, 0, 60, 50, 30}));
    CHECK(build(FitPolicy::FirstFit, 40) == (Spot{0, 0, 40, 50, 30}));
}

TEST(packing_conflicts_and_compatibility) {
    // Two crops that conflict never share a shelf, even when there is room.
    Setup setup = makeSetup({trayCrop(0, 20, 20), trayCrop(1, 20, 20), trayCrop(2, 20, 20)}, {shelfOf(0, 100, 100)});
    ConflictPair rule;
    rule.a = 0;
    rule.b = 1;
    setup.scenario.conflicts.push_back(rule);
    setup.constraints = buildConstraints(setup.scenario);
    CHECK(setup.constraints.conflict[0][1] == ConflictReason::Explicit);
    for (FitPolicy policy : {FitPolicy::FirstFit, FitPolicy::BestFit}) {
        PackState state = emptyPackState(setup.scenario);
        CHECK(placeOne(state, {0, 0}, setup.scenario, setup.constraints, policy));
        CHECK(!placeOne(state, {1, 1}, setup.scenario, setup.constraints, policy));
        CHECK(placeOne(state, {2, 2}, setup.scenario, setup.constraints, policy));   // crop 2 conflicts with nobody
        CHECK(placeOne(state, {3, 0}, setup.scenario, setup.constraints, policy));   // crop 0 beside crop 0 is fine
        CHECK(state.shelves[0].hasCrop[0] && !state.shelves[0].hasCrop[1] && state.shelves[0].hasCrop[2]);
    }
    // With a second shelf the conflicting crop goes there.
    setup = makeSetup({trayCrop(0, 20, 20), trayCrop(1, 20, 20)}, {shelfOf(0, 100, 100), shelfOf(1, 100, 100)});
    setup.scenario.conflicts.push_back(rule);
    setup.constraints = buildConstraints(setup.scenario);
    for (FitPolicy policy : {FitPolicy::FirstFit, FitPolicy::BestFit}) {
        PackState state = emptyPackState(setup.scenario);
        CHECK(placeAll(state, setup, {{0, 0}, {1, 1}}, policy) == 2);
        CHECK(spotOf(state, 0).shelf == 0 && spotOf(state, 1).shelf == 1);
    }
    // A crop that no shelf accepts, and a tray bigger than every shelf, stay unplaced.
    Setup none = makeSetup({trayCrop(0, 20, 20), trayCrop(1, 300, 300)}, {shelfOf(0, 100, 100)});
    CHECK(!none.constraints.compat[1][0]);
    PackState state = emptyPackState(none.scenario);
    CHECK(!placeOne(state, {0, 1}, none.scenario, none.constraints, FitPolicy::FirstFit));
    allowAll(none);   // even when the constraint model says yes, a tray that does not fit the shelf is not placed
    CHECK(!placeOne(state, {0, 1}, none.scenario, none.constraints, FitPolicy::FirstFit));
    CHECK(!placeOne(state, {0, 1}, none.scenario, none.constraints, FitPolicy::BestFit));
    CHECK(state.placements.empty() && state.shelves[0].rows.empty() && !state.shelves[0].hasCrop[1]);
}

TEST(packing_place_one_checks_its_arguments) {
    const Setup setup = makeSetup({trayCrop(0, 20, 20)}, {shelfOf(0, 100, 100)});
    PackState state = emptyPackState(setup.scenario);
    CHECK_THROWS(placeOne(state, {0, 1}, setup.scenario, setup.constraints, FitPolicy::FirstFit));    // no such crop
    CHECK_THROWS(placeOne(state, {0, -1}, setup.scenario, setup.constraints, FitPolicy::FirstFit));
    PackState wrong;                                                                                   // no shelves
    CHECK_THROWS(placeOne(wrong, {0, 0}, setup.scenario, setup.constraints, FitPolicy::FirstFit));
    ConstraintModel small = setup.constraints;
    small.compat.clear();
    CHECK_THROWS(placeOne(state, {0, 0}, setup.scenario, small, FitPolicy::FirstFit));
    small = setup.constraints;
    small.conflict.clear();
    CHECK_THROWS(placeOne(state, {0, 0}, setup.scenario, small, FitPolicy::FirstFit));
    CHECK(state.placements.empty());
    CHECK_THROWS(packFirstFitDecreasing({{0, 5}}, setup.scenario, setup.constraints, CropValues(1)));
    CHECK_THROWS(decreasingOrder({{0, -1}}, setup.scenario));
}

TEST(packing_decreasing_order) {
    // Longer side first, then the shorter side, then crop id, then tray id; a turned tray counts by its sides, not its shape.
    const Setup setup = makeSetup({trayCrop(0, 40, 60), trayCrop(1, 60, 40), trayCrop(2, 60, 50), trayCrop(3, 30, 30), trayCrop(4, 70, 10)},
                                  {shelfOf(0, 100, 100)});
    const std::vector<Tray> trays = {{5, 3}, {2, 1}, {7, 0}, {1, 4}, {3, 1}, {4, 2}, {0, 0}};
    // 70x10 first (longest side 70); then 60 x 50; then the 60 x 40 pair (crops 0 and 1, by crop id, then tray id); 30 x 30 last.
    const std::vector<Tray> order = decreasingOrder(trays, setup.scenario);
    CHECK(ids(order) == std::vector<int>({1, 4, 0, 7, 2, 3, 5}));
    CHECK(ids(decreasingOrder({}, setup.scenario)).empty());
}

// G6: on the branch-and-bound selection of the flat farm (102 trays) FFD places 94 and BFD 95.
TEST(packing_g6_first_fit_and_best_fit_decreasing) {
    const Farm farm = makeFarm(flatScenario());
    const KnapsackResult selection = solveBranchAndBound(farm.problem);
    const size_t crops = farm.scenario.crops.size();
    const std::vector<int> counts = byCrop(farm.problem, selection, crops);
    const std::vector<Tray> trays = traysForCounts(counts);
    CHECK(trays.size() == 102);
    const CropValues values = valuesByCrop(farm.problem, crops);
    const double totalArea = 215200;

    struct Expected { int placed; double value; long long area; int kale, arugula; };
    const Expected ffdWant = {94, 36858.5, 185290, 1, 7}, bfdWant = {95, 37203.0, 186640, 1, 6};
    for (int variant = 0; variant < 2; ++variant) {
        const PackResult r = variant == 0 ? packFirstFitDecreasing(trays, farm.scenario, farm.constraints, values)
                                          : packBestFitDecreasing(trays, farm.scenario, farm.constraints, values);
        const Expected& want = variant == 0 ? ffdWant : bfdWant;
        CHECK(int(r.state.placements.size()) == want.placed);
        CHECK(int(r.unplaced.size()) == 102 - want.placed);
        CHECK_NEAR(r.placedValue, want.value, 1e-6);
        CHECK(r.shelvesUsed == 16);
        CHECK(r.placedArea == want.area);
        CHECK(r.totalShelfArea == 215200 && r.usedShelfArea <= r.totalShelfArea && r.usedShelfArea >= r.placedArea);
        CHECK_NEAR(100.0 * double(r.placedArea) / totalArea, variant == 0 ? 86.10 : 86.73, 0.005);
        Counts unplacedPerCrop(crops, 0);
        for (const Tray& t : r.unplaced) ++unplacedPerCrop[t.cropId];
        Counts expectedUnplaced(crops, 0);
        expectedUnplaced[2] = want.kale;      // Kale
        expectedUnplaced[4] = want.arugula;   // Arugula
        CHECK(unplacedPerCrop == expectedUnplaced);
        for (size_t c = 0; c < crops; ++c) CHECK(r.placedPerCrop[c] + unplacedPerCrop[c] == counts[c]);
        const std::vector<std::string> problems = validatePacking(r.state, farm.scenario, farm.constraints);
        CHECK(problems.empty());
        if (!problems.empty()) std::printf("%s\n", joined(problems).c_str());
        CHECK(r.algorithm == (variant == 0 ? "First-Fit Decreasing" : "Best-Fit Decreasing"));
        CHECK(r.seconds >= 0 && r.seconds < 5);
    }
}

// validatePacking finds each kind of violation, once, and says nothing about a sound layout.
TEST(packing_validate_finds_every_violation) {
    // Crop 0 is 60 x 40 and may turn; crops 1, 2 and 3 are 30 x 30; crop 1 conflicts with 2 and with 3; crop 2 may not
    // stand on shelf 3.
    Setup setup = makeSetup({trayCrop(0, 60, 40), trayCrop(1, 30, 30, false), trayCrop(2, 30, 30, false), trayCrop(3, 30, 30, false)},
                            {shelfOf(0, 100, 100), shelfOf(1, 100, 100), shelfOf(2, 100, 100), shelfOf(3, 100, 100)});
    ConflictPair rule;
    rule.a = 1;
    rule.b = 2;
    setup.scenario.conflicts.push_back(rule);
    rule.b = 3;
    setup.scenario.conflicts.push_back(rule);
    setup.constraints = buildConstraints(setup.scenario);
    setup.constraints.compat[2][3] = 0;

    // A sound layout with each tray alone on its shelf, so that one change makes exactly one violation.
    PackState sound = emptyPackState(setup.scenario);
    sound.placements = {{0, 0, 0, 0, 0, 60, 40}, {1, 1, 1, 0, 0, 30, 30}, {2, 2, 2, 0, 0, 30, 30}};
    CHECK(validatePacking(sound, setup.scenario, setup.constraints).empty());
    CHECK(validatePacking(emptyPackState(setup.scenario), setup.scenario, setup.constraints).empty());

    auto count = [&](auto change) {
        PackState state = sound;
        change(state.placements);
        return validatePacking(state, setup.scenario, setup.constraints).size();
    };
    using Placements = std::vector<Placement>;
    // Outside the shelf, on each side, and flush against the edge, which is fine.
    CHECK(count([](Placements& p) { p[0].x = 41; }) == 1);          // 41 + 60 = 101
    CHECK(count([](Placements& p) { p[0].x = 40; }) == 0);
    CHECK(count([](Placements& p) { p[0].y = 61; }) == 1);          // 61 + 40 = 101
    CHECK(count([](Placements& p) { p[0].y = 60; }) == 0);
    CHECK(count([](Placements& p) { p[0].x = -1; }) == 1);
    CHECK(count([](Placements& p) { p[0].y = -1; }) == 1);
    // The size must be an orientation of the crop: 60 x 40 or turned 40 x 60; a crop that may not turn has one.
    CHECK(count([](Placements& p) { p[0].w = 50; }) == 1);
    CHECK(count([](Placements& p) { p[0].w = 40; p[0].d = 60; }) == 0);
    CHECK(count([](Placements& p) { p[0].w = 40; p[0].d = 60; p[0].x = 61; }) == 1);   // turned and then 101 wide: outside only
    CHECK(count([](Placements& p) { p[1].w = 40; p[1].d = 20; }) == 1);
    // A shelf the crop may not use.
    CHECK(count([](Placements& p) { p[2].shelfId = 3; }) == 1);
    // Overlap: one column or one row of it counts, touching does not.
    CHECK(count([](Placements& p) { p[1].shelfId = 0; p[1].x = 59; p[1].y = 0; }) == 1);
    CHECK(count([](Placements& p) { p[1].shelfId = 0; p[1].x = 60; p[1].y = 0; }) == 0);
    CHECK(count([](Placements& p) { p[1].shelfId = 0; p[1].x = 0; p[1].y = 39; }) == 1);
    CHECK(count([](Placements& p) { p[1].shelfId = 0; p[1].x = 0; p[1].y = 40; }) == 0);
    CHECK(count([](Placements& p) { p[1].shelfId = 0; p[1].x = 10; p[1].y = 5; }) == 1);   // wholly inside the other tray
    // The same, with the later tray on the left of or above the earlier one.
    CHECK(count([](Placements& p) { p[0].x = 40; p[1].shelfId = 0; p[1].x = 10; p[1].y = 0; }) == 0);    // 10 + 30 = 40, touching
    CHECK(count([](Placements& p) { p[0].x = 40; p[1].shelfId = 0; p[1].x = 11; p[1].y = 0; }) == 1);
    CHECK(count([](Placements& p) { p[0].y = 60; p[1].shelfId = 0; p[1].x = 0; p[1].y = 30; }) == 0);    // 30 + 30 = 60, touching
    CHECK(count([](Placements& p) { p[0].y = 60; p[1].shelfId = 0; p[1].x = 0; p[1].y = 31; }) == 1);
    // Conflicting crops on one shelf: one message for the pair of crops, however many trays.
    CHECK(count([](Placements& p) { p[2].shelfId = 1; p[2].x = 50; }) == 1);
    CHECK(count([](Placements& p) {
              p[2].shelfId = 1; p[2].x = 50;
              Placement extra = p[2];
              extra.trayId = 9;
              extra.y = 40;
              p.push_back(extra);
          }) == 1);
    // Two different conflicting pairs on one shelf (1 with 2, and 1 with 3) are two messages.
    CHECK(count([](Placements& p) {
              p[2].shelfId = 1; p[2].x = 50;
              p.push_back({8, 3, 1, 0, 40, 30, 30});
          }) == 2);
    // The same crop twice on a shelf is no conflict; a tray id may not repeat, and three of a kind are one message.
    CHECK(count([](Placements& p) { p[2].cropId = 1; p[2].shelfId = 1; p[2].x = 50; }) == 0);
    CHECK(count([](Placements& p) { p[1].trayId = 0; }) == 1);
    CHECK(count([](Placements& p) { p[1].trayId = 0; p[2].trayId = 0; }) == 1);
    // Nonsense references are reported, not followed.
    CHECK(count([](Placements& p) { p[0].cropId = 7; }) == 1);
    CHECK(count([](Placements& p) { p[0].cropId = -1; }) == 1);
    CHECK(count([](Placements& p) { p[0].shelfId = 5; }) == 1);
    CHECK(count([](Placements& p) { p[0].shelfId = -1; }) == 1);
    // Several things wrong at once are each reported.
    CHECK(count([](Placements& p) { p[0].x = 51; p[0].w = 50; }) == 2);   // outside (51 + 50 = 101) and no orientation of the crop
}

// The genetic algorithm.
TEST(packing_genetic_basics) {
    const Setup setup = makeSetup({trayCrop(0, 80, 30), trayCrop(1, 60, 40), trayCrop(2, 45, 35), trayCrop(3, 60, 40)},
                                  {shelfOf(0, 100, 70), shelfOf(1, 100, 60)});
    const std::vector<Tray> trays = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    const CropValues values(4, std::vector<double>{10.0});
    GaParams params;
    params.population = 10;
    params.generations = 20;
    params.stallGenerations = 8;
    const PackResult ga = packGenetic(trays, setup.scenario, setup.constraints, values, params);
    CHECK(ga.algorithm == "Genetic algorithm" && validatePacking(ga.state, setup.scenario, setup.constraints).empty());
    CHECK(ga.state.placements.size() == 4 && ga.unplaced.empty());    // First-Fit Decreasing already places all four, and so must the GA
    CHECK_NEAR(ga.placedValue, 40.0, 1e-9);
    CHECK(ga.generationsRun >= 0 && ga.generationsRun <= 20 && ga.convergence.size() == size_t(ga.generationsRun) + 1);
    CHECK(std::is_sorted(ga.convergence.begin(), ga.convergence.end()));   // the best two survive, so the best never gets worse

    // Parameters.
    GaParams bad = params;
    bad.population = 2;
    CHECK_THROWS(packGenetic(trays, setup.scenario, setup.constraints, values, bad));
    bad = params;
    bad.generations = -1;
    CHECK_THROWS(packGenetic(trays, setup.scenario, setup.constraints, values, bad));
    bad = params;
    bad.stallGenerations = 0;
    CHECK_THROWS(packGenetic(trays, setup.scenario, setup.constraints, values, bad));
    for (double rate : {-0.1, 1.1, std::nan("")}) {
        bad = params;
        bad.crossoverRate = rate;
        CHECK_THROWS(packGenetic(trays, setup.scenario, setup.constraints, values, bad));
        bad = params;
        bad.mutationRate = rate;
        CHECK_THROWS(packGenetic(trays, setup.scenario, setup.constraints, values, bad));
    }
    bad = params;
    bad.population = 3;                                              // the smallest population holds the three starting individuals
    bad.generations = 0;
    bad.crossoverRate = 0;
    bad.mutationRate = 1;
    const PackResult small = packGenetic(trays, setup.scenario, setup.constraints, values, bad);
    CHECK(small.generationsRun == 0 && small.convergence.size() == 1 && small.state.placements.size() == 4);
    bad.generations = 5;                                             // rates 0 and 1 are allowed, on either side
    CHECK(packGenetic(trays, setup.scenario, setup.constraints, values, bad).state.placements.size() == 4);
    bad.crossoverRate = 1;
    bad.mutationRate = 0;
    CHECK(packGenetic(trays, setup.scenario, setup.constraints, values, bad).state.placements.size() == 4);
}

// With no trays every packer returns an empty layout, and the genetic algorithm does not try to evolve it.
TEST(packing_no_trays) {
    const Setup setup = makeSetup({trayCrop(0, 20, 20)}, {shelfOf(0, 100, 100)});
    const CropValues values(1, std::vector<double>{5.0});
    for (int variant = 0; variant < 3; ++variant) {
        const PackResult r = variant == 0 ? packFirstFitDecreasing({}, setup.scenario, setup.constraints, values)
                           : variant == 1 ? packBestFitDecreasing({}, setup.scenario, setup.constraints, values)
                                          : packGenetic({}, setup.scenario, setup.constraints, values);
        CHECK(r.state.placements.empty() && r.unplaced.empty() && r.placedPerCrop == Counts({0}));
        CHECK(r.placedValue == 0 && r.shelvesUsed == 0 && r.placedArea == 0 && r.usedShelfArea == 0 && r.totalShelfArea == 10000);
        CHECK(r.generationsRun == 0);
    }
    CHECK(packGenetic({}, setup.scenario, setup.constraints, values).convergence == std::vector<double>({0.0}));
    CHECK(traysForCounts({}).empty() && traysForCounts({0, 0}).empty() && traysForCounts({-3}).empty());
    const std::vector<Tray> made = traysForCounts({2, 0, 1});
    CHECK(made.size() == 3 && made[0].id == 0 && made[0].cropId == 0 && made[1].id == 1 && made[1].cropId == 0 && made[2].id == 2 && made[2].cropId == 2);
}

TEST(packing_fitness_order) {
    // More value first; then fewer shelves; then more concentrated. Differences below EPS do not count.
    PackFitness a, b;
    CHECK(!fitterThan(a, b) && !fitterThan(b, a));
    a.placedValue = 10;
    b.placedValue = 9;
    b.shelvesUsed = 1;
    CHECK(fitterThan(a, b) && !fitterThan(b, a));
    b.placedValue = 10 - 1e-12;
    a.shelvesUsed = 2;
    CHECK(!fitterThan(a, b) && fitterThan(b, a));               // equal value within EPS: fewer shelves wins
    a.shelvesUsed = 1;
    b.concentration = 0.5;
    a.concentration = 0.5 + 1e-12;
    CHECK(!fitterThan(a, b) && !fitterThan(b, a));              // equal concentration within EPS
    a.concentration = 0.6;
    CHECK(fitterThan(a, b) && !fitterThan(b, a));
    // packFitness: value of the prefixes, shelves in use and the squared fill of each used shelf.
    Setup setup = makeSetup({trayCrop(0, 50, 50, false), trayCrop(1, 50, 50, false)}, {shelfOf(0, 100, 100), shelfOf(1, 100, 100), shelfOf(2, 120, 50)});
    allowAll(setup);
    PackState state = emptyPackState(setup.scenario);
    state.placements = {{0, 0, 0, 0, 0, 50, 50}, {1, 0, 0, 50, 0, 50, 50}, {2, 1, 2, 0, 0, 50, 50}};   // tray id, crop, shelf, x, y, w, d
    const CropValues values = {{10, 6}, {4}};
    const PackFitness f = packFitness(state, setup.scenario, values);
    CHECK_NEAR(f.placedValue, 10 + 6 + 4, 1e-12);
    CHECK(f.shelvesUsed == 2);
    CHECK_NEAR(f.concentration, 0.5 * 0.5 + (2500.0 / 6000) * (2500.0 / 6000), 1e-12);   // shelf 0 holds 5000 of 10000, shelf 2 (120 x 50) holds 2500 of 6000
    CHECK_NEAR(packFitness(state, setup.scenario, CropValues(2)).placedValue, 0.0, 0.0);                      // crops without values earn nothing
    CHECK_NEAR(packFitness(state, setup.scenario, CropValues{{10}, {4}}).placedValue, 10 + 4, 1e-12);       // a third tray of crop 0 would earn nothing
    CHECK_NEAR(packFitness(state, setup.scenario, CropValues{{10, 6}}).placedValue, 16, 1e-12);              // a missing list earns nothing
}

namespace {

// A random instance in the shape of plan 10.6: 10 tray types, 16 shelves of five different sizes, a conflict between two
// crops with probability 0.2 and a crop allowed on a shelf with probability 0.8 (so a tray may also be too big for a shelf
// it is allowed on). The constraint model is made directly, not from the scenario.
struct Instance {
    Scenario scenario;
    ConstraintModel constraints;
    std::vector<Tray> trays;
    CropValues values;
};

Instance randomInstance(unsigned long long seed) {
    Rng rng(seed);
    Instance in;
    const int shapes[5][2] = {{240, 60}, {200, 80}, {180, 50}, {120, 60}, {100, 70}};
    const int crops = 10, shelves = 16;
    for (int s = 0; s < shelves; ++s) in.scenario.shelves.push_back(shelfOf(s, shapes[s % 5][0], shapes[s % 5][1]));
    for (int c = 0; c < crops; ++c) {
        const int w = 20 + 5 * rng.below(11), d = 20 + 5 * rng.below(8);
        in.scenario.crops.push_back(trayCrop(c, w, d, rng.below(5) != 0));
    }
    in.constraints.compat.assign(crops, std::vector<char>(shelves, 0));
    in.constraints.conflict.assign(crops, std::vector<ConflictReason>(crops, ConflictReason::None));
    for (int c = 0; c < crops; ++c)
        for (int s = 0; s < shelves; ++s) in.constraints.compat[c][s] = rng.unit() < 0.8 ? 1 : 0;
    for (int a = 0; a < crops; ++a)
        for (int b = a + 1; b < crops; ++b)
            if (rng.unit() < 0.2) in.constraints.conflict[a][b] = in.constraints.conflict[b][a] = ConflictReason::Explicit;
    in.values.resize(crops);
    for (int c = 0; c < crops; ++c) {
        const int count = rng.below(25);   // about 120 trays on shelves that hold about 100 of them: some are left over
        double v = 100 + rng.below(100);
        for (int k = 0; k < count; ++k) {
            in.trays.push_back({int(in.trays.size()), c});
            in.values[c].push_back(v);
            v = std::max(1.0, v - rng.below(8));
        }
    }
    return in;
}

bool samePlacements(const PackState& a, const PackState& b) {
    if (a.placements.size() != b.placements.size()) return false;
    for (size_t i = 0; i < a.placements.size(); ++i) {
        const Placement &p = a.placements[i], &q = b.placements[i];
        if (p.trayId != q.trayId || p.cropId != q.cropId || p.shelfId != q.shelfId || p.x != q.x || p.y != q.y || p.w != q.w || p.d != q.d) return false;
    }
    return true;
}

// Every tray is placed or unplaced, once; the counts, areas and value add up.
void checkAccounting(const PackResult& r, const Instance& in) {
    std::set<int> seen;
    for (const Placement& p : r.state.placements) CHECK(seen.insert(p.trayId).second);
    for (const Tray& t : r.unplaced) CHECK(seen.insert(t.id).second);
    CHECK(seen.size() == in.trays.size());
    CHECK(r.state.placements.size() + r.unplaced.size() == in.trays.size());
    Counts placed(in.scenario.crops.size(), 0);
    long long area = 0;
    for (const Placement& p : r.state.placements) {
        ++placed[p.cropId];
        area += static_cast<long long>(p.w) * p.d;
        const Crop& crop = in.scenario.crops[in.trays[p.trayId].cropId];
        CHECK(p.cropId == crop.id && static_cast<long long>(p.w) * p.d == static_cast<long long>(crop.trayW) * crop.trayD);
    }
    CHECK(placed == r.placedPerCrop && area == r.placedArea);
    double value = 0;
    for (size_t c = 0; c < placed.size(); ++c)
        for (int k = 0; k < placed[c]; ++k) value += in.values[c][k];
    CHECK_NEAR(r.placedValue, value, 1e-9);
    long long total = 0;
    for (const Shelf& s : in.scenario.shelves) total += static_cast<long long>(s.width) * s.depth;
    CHECK(r.totalShelfArea == total);
    CHECK(r.usedShelfArea >= r.placedArea && r.usedShelfArea <= r.totalShelfArea);
    std::set<int> shelves;
    for (const Placement& p : r.state.placements) shelves.insert(p.shelfId);
    CHECK(r.shelvesUsed == int(shelves.size()));
}

}  // namespace

// Plan 10.6: on 200 seeded random instances all three packers give layouts that validatePacking accepts, every tray is placed
// or unplaced and never both, the genetic algorithm is never worse than First-Fit or Best-Fit Decreasing, and it returns the
// same result twice for the same seed.
TEST(packing_random_instances) {
    GaParams params;
    params.population = 14;
    params.generations = 10;
    params.stallGenerations = 6;
    int someUnplaced = 0, someImproved = 0;
    for (int i = 0; i < 200; ++i) {
        const Instance in = randomInstance(7000 + i);
        const PackResult ffd = packFirstFitDecreasing(in.trays, in.scenario, in.constraints, in.values);
        const PackResult bfd = packBestFitDecreasing(in.trays, in.scenario, in.constraints, in.values);
        params.seed = 100 + i;
        const PackResult ga = packGenetic(in.trays, in.scenario, in.constraints, in.values, params);
        for (const PackResult* r : {&ffd, &bfd, &ga}) {
            const std::vector<std::string> problems = validatePacking(r->state, in.scenario, in.constraints);
            CHECK(problems.empty());
            if (!problems.empty()) std::printf("instance %d, %s: %s\n", i, r->algorithm.c_str(), joined(problems).c_str());
            checkAccounting(*r, in);
        }
        const PackFitness fitFfd = packFitness(ffd.state, in.scenario, in.values), fitBfd = packFitness(bfd.state, in.scenario, in.values),
                          fitGa = packFitness(ga.state, in.scenario, in.values);
        CHECK(!fitterThan(fitFfd, fitGa) && !fitterThan(fitBfd, fitGa));
        CHECK(ga.convergence.size() == size_t(ga.generationsRun) + 1 && std::is_sorted(ga.convergence.begin(), ga.convergence.end()));
        CHECK_NEAR(ga.convergence.back(), ga.placedValue, 1e-9);
        if (!ffd.unplaced.empty()) ++someUnplaced;
        if (fitterThan(fitGa, fitFfd) || fitterThan(fitGa, fitBfd)) ++someImproved;
        if (i % 5 == 0) {   // the same seed, the same run
            const PackResult again = packGenetic(in.trays, in.scenario, in.constraints, in.values, params);
            CHECK(samePlacements(ga.state, again.state) && ga.convergence == again.convergence && ga.generationsRun == again.generationsRun);
        }
    }
    CHECK(someUnplaced > 50);     // the instances are tight enough to be interesting...
    CHECK(someImproved > 0);      // ...and the genetic search is not just the best of the starting individuals
}

// The genetic packer on the G6 selection: plan 10.4 expects it to beat Best-Fit Decreasing, and the plan prototype placed
// 97 to 98 of the 102 trays with a population of 30 and 40 generations.
TEST(packing_genetic_on_the_flat_farm) {
    const Farm farm = makeFarm(flatScenario());
    const size_t crops = farm.scenario.crops.size();
    const std::vector<Tray> trays = traysForCounts(byCrop(farm.problem, solveBranchAndBound(farm.problem), crops));
    const CropValues values = valuesByCrop(farm.problem, crops);
    GaParams params;
    params.population = 30;
    params.generations = 40;
    const PackResult bfd = packBestFitDecreasing(trays, farm.scenario, farm.constraints, values);
    const PackResult ffd = packFirstFitDecreasing(trays, farm.scenario, farm.constraints, values);
    const PackResult ga = packGenetic(trays, farm.scenario, farm.constraints, values, params);
    std::printf("  genetic on the flat farm: placed %zu of 102, value %.1f (BFD %zu, %.1f), %d generations, %.2f s\n", ga.state.placements.size(),
                ga.placedValue, bfd.state.placements.size(), bfd.placedValue, ga.generationsRun, ga.seconds);
    CHECK(validatePacking(ga.state, farm.scenario, farm.constraints).empty());
    CHECK(!fitterThan(packFitness(ffd.state, farm.scenario, values), packFitness(ga.state, farm.scenario, values)));
    CHECK(!fitterThan(packFitness(bfd.state, farm.scenario, values), packFitness(ga.state, farm.scenario, values)));
    CHECK(ga.placedValue > bfd.placedValue);
    CHECK(ga.seconds < 5);
    const PackResult again = packGenetic(trays, farm.scenario, farm.constraints, values, params);
    CHECK(samePlacements(ga.state, again.state) && ga.convergence == again.convergence);
    params.seed = 43;   // another seed is another run, but still valid
    CHECK(validatePacking(packGenetic(trays, farm.scenario, farm.constraints, values, params).state, farm.scenario, farm.constraints).empty());
}

namespace {

// FNV-1a over the placements in order (tray, shelf, x, y, w, d): one number that changes when any placement does.
unsigned long long fingerprint(const PackState& state) {
    unsigned long long h = 14695981039346656037ull;
    for (const Placement& p : state.placements)
        for (int v : {p.trayId, p.shelfId, p.x, p.y, p.w, p.d}) h = (h ^ static_cast<unsigned long long>(v)) * 1099511628211ull;
    return h;
}

}  // namespace

// The genetic algorithm has no golden numbers in the plan, because the random stream of another language cannot match it.
// These values were produced by this code and confirmed to the last digit by a second implementation written from plan 10.4
// alone (the reference model's decoder, SplitMix64 re-implemented, the draw order documented in packing.cpp), on these two runs
// and 300 random instances. They pin the draw order, the elitism, the tournament, the crossover, the mutation and the stop rule.
TEST(packing_genetic_regression) {
    const Farm farm = makeFarm(flatScenario());
    const size_t crops = farm.scenario.crops.size();
    const std::vector<Tray> trays = traysForCounts(byCrop(farm.problem, solveBranchAndBound(farm.problem), crops));
    const CropValues values = valuesByCrop(farm.problem, crops);

    GaParams params;   // the plan's prototype: a population of 30 and 40 generations
    params.population = 30;
    params.generations = 40;
    PackResult r = packGenetic(trays, farm.scenario, farm.constraints, values, params);
    const std::vector<double> convergence = {
        37203.0, 37865.0, 37865.0, 37971.0, 38045.0, 38045.0, 38045.0, 38045.0, 38045.0, 38045.0, 38045.0, 38173.0, 38296.5, 38389.5,
        38389.5, 38389.5, 38405.5, 38405.5, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0,
        38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38551.0, 38696.5, 38696.5, 38696.5};
    CHECK(r.generationsRun == 40 && r.convergence.size() == convergence.size());
    for (size_t i = 0; i < convergence.size() && i < r.convergence.size(); ++i) CHECK_NEAR(r.convergence[i], convergence[i], 1e-6);
    CHECK(r.state.placements.size() == 98 && r.unplaced.size() == 4 && fingerprint(r.state) == 16274121027363858612ull);
    CHECK_NEAR(r.placedValue, 38696.5, 1e-6);

    // Another seed, with a stall limit of 5: the search stops after 14 generations because the last 5 brought nothing.
    params.population = 20;
    params.generations = 60;
    params.stallGenerations = 5;
    params.seed = 7;
    r = packGenetic(trays, farm.scenario, farm.constraints, values, params);
    const std::vector<double> stalled = {37203.0, 37203.0, 38187.5, 38257.0, 38257.0, 38298.0, 38404.0, 38404.0, 38404.0, 38404.0, 38404.0,
                                         38404.0, 38404.0, 38404.0, 38404.0};
    CHECK(r.generationsRun == 14 && r.convergence.size() == stalled.size());
    for (size_t i = 0; i < stalled.size() && i < r.convergence.size(); ++i) CHECK_NEAR(r.convergence[i], stalled[i], 1e-6);
    CHECK(r.state.placements.size() == 97 && r.unplaced.size() == 5 && fingerprint(r.state) == 8565311191349692654ull);
}
