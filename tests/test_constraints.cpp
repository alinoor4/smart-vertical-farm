// Tests for the constraint layer (plan section 7.5): Appendix A cases G1 and G6, plus hand-built cases.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "constraints.h"
#include "json_io.h"
#include "rng.h"

using Graph = std::vector<std::vector<int>>;

static Graph makeGraph(int n, const std::vector<std::pair<int, int>>& edges) {
    Graph adj(n);
    for (const auto& e : edges) {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }
    return adj;
}

static int colourCount(const std::vector<int>& colours) { return colours.empty() ? 0 : *std::max_element(colours.begin(), colours.end()) + 1; }

// Proper: every vertex coloured, no edge inside a colour class, and the colours are exactly 0..k-1.
static bool isProper(const Graph& adj, const std::vector<int>& colours) {
    if (colours.size() != adj.size()) return false;
    for (size_t v = 0; v < adj.size(); ++v) {
        if (colours[v] < 0) return false;
        for (int u : adj[v])
            if (colours[u] == colours[v]) return false;
    }
    return std::set<int>(colours.begin(), colours.end()).size() == size_t(colourCount(colours));
}

// An oracle that shares no code with colourExact: the chromatic number by dynamic programming over vertex
// subsets, O(3^n). For each subset, try every independent set that contains its lowest vertex.
static int chromaticNumberByDp(const Graph& adj) {
    const int n = int(adj.size());
    std::vector<unsigned> neighbours(n, 0);
    for (int v = 0; v < n; ++v)
        for (int u : adj[v]) neighbours[v] |= 1u << u;
    const unsigned all = (1u << n) - 1;
    std::vector<char> independent(all + 1, 0);
    std::vector<int> colours(all + 1, 0);   // fewest independent sets that cover the subset
    independent[0] = 1;
    for (unsigned mask = 1; mask <= all; ++mask) {
        const unsigned low = mask & (~mask + 1);   // lowest vertex of the subset, as a bit
        int v = 0;
        while (!((low >> v) & 1)) ++v;
        independent[mask] = independent[mask ^ low] && (neighbours[v] & mask) == 0;
        const unsigned rest = mask ^ low;
        int best = n;
        for (unsigned s = rest;; s = (s - 1) & rest) {   // every subset s of rest
            const unsigned part = s | low;
            if (independent[part]) best = std::min(best, colours[mask ^ part] + 1);
            if (s == 0) break;
        }
        colours[mask] = best;
    }
    return colours[all];
}

// ---- Appendix A, G1

TEST(colouring_crown_graph) {
    const Graph crown = makeGraph(6, {{0, 3}, {0, 5}, {2, 1}, {2, 5}, {4, 1}, {4, 3}});
    CHECK(colourWelshPowell(crown) == std::vector<int>({0, 0, 1, 1, 2, 2}));
    CHECK(colourDsatur(crown) == std::vector<int>({0, 1, 0, 1, 0, 1}));
    const ExactColouring exact = colourExact(crown);
    CHECK(exact.colours == 2 && exact.proven);
}

TEST(colouring_exact_chromatic_numbers) {
    const Graph c5 = makeGraph(5, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 0}});
    std::vector<std::pair<int, int>> k4Edges;
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b) k4Edges.push_back({a, b});
    const Graph k4 = makeGraph(4, k4Edges);
    const Graph petersen = makeGraph(10, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 0}, {0, 5}, {1, 6}, {2, 7}, {3, 8}, {4, 9},
                                          {5, 7}, {7, 9}, {9, 6}, {6, 8}, {8, 5}});
    CHECK(colourExact(c5).colours == 3 && colourExact(c5).proven);
    CHECK(colourExact(k4).colours == 4 && colourExact(k4).proven);
    CHECK(colourExact(petersen).colours == 3 && colourExact(petersen).proven);
    CHECK(chromaticNumberByDp(c5) == 3 && chromaticNumberByDp(k4) == 4 && chromaticNumberByDp(petersen) == 3);   // the oracle itself
    CHECK(colourCount(colourWelshPowell(k4)) == 4 && colourCount(colourDsatur(k4)) == 4);   // a complete graph leaves no choice
    for (const Graph* g : {&c5, &k4, &petersen}) {
        CHECK(isProper(*g, colourWelshPowell(*g)));
        CHECK(isProper(*g, colourDsatur(*g)));
    }
}

// The crown graph has equal degrees, so it cannot show that Welsh-Powell orders by degree. These expected colourings
// come from plan/reference_model.py; plain index order would give [0,1,0,1,0,1] and [0,0,0,1,0,2,0] for the first and last.
TEST(colouring_orders_vertices_by_degree) {
    const Graph path6 = makeGraph(6, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}});
    CHECK(colourWelshPowell(path6) == std::vector<int>({1, 0, 1, 0, 1, 0}));
    CHECK(colourDsatur(path6) == std::vector<int>({1, 0, 1, 0, 1, 0}));
    const Graph star = makeGraph(5, {{0, 1}, {0, 2}, {0, 3}, {0, 4}});   // hub first, leaves after
    CHECK(colourWelshPowell(star) == std::vector<int>({0, 1, 1, 1, 1}));
    const Graph hub = makeGraph(7, {{3, 0}, {3, 1}, {3, 2}, {3, 4}, {3, 5}, {4, 5}, {5, 6}});
    CHECK(colourWelshPowell(hub) == std::vector<int>({1, 1, 1, 0, 2, 1, 0}));
    CHECK(colourDsatur(hub) == std::vector<int>({1, 1, 1, 0, 2, 1, 0}));
    CHECK(colourExact(hub).colours == 3);
}

// Saturation is the number of distinct colours around a vertex, not the number of coloured neighbours. Counting
// neighbours gives [0,0,1,2,1,3] here, one colour too many. Expected value from plan/reference_model.py.
TEST(dsatur_counts_distinct_colours) {
    const Graph g = makeGraph(6, {{0, 2}, {0, 3}, {0, 4}, {1, 2}, {1, 4}, {1, 5}, {2, 5}, {3, 4}, {3, 5}});
    CHECK(colourDsatur(g) == std::vector<int>({0, 0, 1, 1, 2, 2}));
    CHECK(colourExact(g).colours == 3);
}

TEST(colouring_edge_cases) {
    const Graph none;
    CHECK(colourWelshPowell(none).empty() && colourDsatur(none).empty());
    CHECK(colourExact(none).colours == 0 && colourExact(none).proven);
    const Graph edgeless = makeGraph(5, {});
    CHECK(colourCount(colourWelshPowell(edgeless)) == 1 && colourCount(colourDsatur(edgeless)) == 1);
    CHECK(colourExact(edgeless).colours == 1);
    CHECK(colourExact(makeGraph(1, {})).colours == 1);
    // A node limit stops the search without a proof; the count is still a valid upper bound.
    const Graph c5 = makeGraph(5, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 0}});
    const ExactColouring limited = colourExact(c5, 1);
    CHECK(!limited.proven && limited.colours == 3);
}

TEST(colouring_random_graphs) {
    Rng rng(2026);
    int dsaturBeaten = 0;   // graphs on which the exact search found fewer colours than DSATUR
    for (int round = 0; round < 200; ++round) {
        const int n = 6 + rng.below(9);               // 6 to 14 vertices
        const double density = 0.1 + 0.6 * rng.unit();
        std::vector<std::pair<int, int>> edges;
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b)
                if (rng.unit() < density) edges.push_back({a, b});
        const Graph adj = makeGraph(n, edges);
        const std::vector<int> wp = colourWelshPowell(adj), ds = colourDsatur(adj);
        const ExactColouring exact = colourExact(adj);
        CHECK(isProper(adj, wp));
        CHECK(isProper(adj, ds));
        CHECK(exact.proven);
        CHECK(exact.colours <= colourCount(wp) && exact.colours <= colourCount(ds));   // not asserted: DSATUR <= Welsh-Powell
        CHECK(exact.colours == chromaticNumberByDp(adj));   // exactness, not only "no worse than the heuristics"
        if (exact.colours < colourCount(ds)) ++dsaturBeaten;
    }
    // The search starts from DSATUR's count, so it is only really tested on graphs where DSATUR is not optimal.
    CHECK(dsaturBeaten >= 1);   // 4 of the 200 graphs today, all with 12 to 14 vertices
}

// ---- Appendix A, G6: the constraint part, on the flat default scenario

TEST(default_farm_constraints) {
    const Scenario s = loadScenario("data/flat_scenario.json");
    const ConstraintModel m = buildConstraints(s);
    const int shelvesPerCrop[12] = {12, 12, 4, 12, 12, 7, 12, 10, 6, 6, 6, 1};
    const int maxTrays[12] = {72, 96, 20, 88, 124, 67, 148, 138, 50, 38, 28, 4};   // the Cap column of G6's knapsack table
    for (int c = 0; c < 12; ++c) {
        CHECK(std::count(m.compat[c].begin(), m.compat[c].end(), 1) == shelvesPerCrop[c]);
        CHECK(m.maxTraysByArea[c] == maxTrays[c]);
    }
    CHECK(m.pairsCompatible == 100 && m.pairsTotal == 192);

    int explicitEdges = 0, heightEdges = 0, otherEdges = 0;
    for (int a = 0; a < 12; ++a)
        for (int b = 0; b < 12; ++b) {
            CHECK(m.conflict[a][b] == m.conflict[b][a]);
            if (a >= b) continue;
            if (m.conflict[a][b] == ConflictReason::Explicit) ++explicitEdges;
            else if (m.conflict[a][b] == ConflictReason::Height) ++heightEdges;
            else if (m.conflict[a][b] != ConflictReason::None) ++otherEdges;   // the default data never produces NoCommonShelf
        }
    CHECK(explicitEdges == 8 && heightEdges == 22 && otherEdges == 0);

    size_t degreeSum = 0;
    for (int a = 0; a < 12; ++a) {
        degreeSum += m.adjacency[a].size();
        CHECK(std::is_sorted(m.adjacency[a].begin(), m.adjacency[a].end()));
    }
    CHECK(degreeSum == 60);   // 30 edges, each counted from both ends
    CHECK(colourCount(colourWelshPowell(m.adjacency)) == 5);
    CHECK(colourCount(colourDsatur(m.adjacency)) == 5);
    const ExactColouring exact = colourExact(m.adjacency);
    CHECK(exact.colours == 5 && exact.proven);
}

// ---- Hand-built scenarios: every branch of section 7.1

static Crop makeCrop(int id, int w, int d, int height) {
    Crop c;
    c.id = id;
    c.name = "C" + std::to_string(id);
    c.category = "leafy";
    c.trayW = w;
    c.trayD = d;
    c.height = height;
    c.lightMin = Light::Medium;
    c.lightMax = Light::High;
    c.humidityMin = 50;
    c.humidityMax = 70;
    return c;
}

static Shelf makeShelf(int id, int w, int d, int clearance, Light light, double humidity) {
    Shelf s;
    s.id = id;
    s.name = "S" + std::to_string(id);
    s.width = w;
    s.depth = d;
    s.clearance = clearance;
    s.light = light;
    s.humidity = humidity;
    return s;
}

TEST(why_not_reports_every_failed_check) {
    Scenario s;   // lightGapCm is 10, so a 20 cm crop needs 30 cm of clearance
    s.crops = {makeCrop(0, 60, 40, 20), makeCrop(1, 60, 40, 20)};
    s.crops[1].canRotate = false;
    s.shelves = {
        makeShelf(0, 100, 100, 30, Light::Medium, 60),    // fits, clearance exactly enough
        makeShelf(1, 100, 100, 29, Light::Medium, 60),    // one centimetre too low
        makeShelf(2, 100, 100, 30, Light::Low, 60),       // too dark
        makeShelf(3, 100, 100, 30, Light::Medium, 70.5),  // too humid
        makeShelf(4, 50, 30, 30, Light::Medium, 60),      // too small in either orientation
        makeShelf(5, 40, 60, 30, Light::High, 50),        // fits only turned 90 degrees; light and humidity at the range ends
        makeShelf(6, 100, 100, 29, Light::Medium, 70.5),  // two failures at once
        makeShelf(7, 100, 100, 30, Light::High, 70),
    };
    const ConstraintModel m = buildConstraints(s);
    const int rotatable[8] = {FitsOk, Clearance, LightMismatch, HumidityMismatch, Footprint, FitsOk, Clearance | HumidityMismatch, FitsOk};
    const int fixed[8] = {FitsOk, Clearance, LightMismatch, HumidityMismatch, Footprint, Footprint, Clearance | HumidityMismatch, FitsOk};
    for (int sh = 0; sh < 8; ++sh) {
        CHECK(m.whyNot[0][sh] == rotatable[sh]);
        CHECK(m.whyNot[1][sh] == fixed[sh]);
        CHECK(m.compat[0][sh] == (rotatable[sh] == FitsOk));
        CHECK(m.compat[1][sh] == (fixed[sh] == FitsOk));
    }
    CHECK(m.pairsTotal == 16 && m.pairsCompatible == 5);
    CHECK(m.maxTraysByArea[0] == 4 + 1 + 4);   // shelves 0, 5 and 7: 10 000 / 2 400, 2 400 / 2 400, 10 000 / 2 400
    CHECK(m.maxTraysByArea[1] == 4 + 4);
}

TEST(conflict_reasons_follow_the_rules_in_order) {
    Scenario s;
    s.crops = {makeCrop(0, 30, 30, 20), makeCrop(1, 30, 30, 20), makeCrop(2, 30, 30, 50), makeCrop(3, 30, 30, 40)};
    s.shelves = {makeShelf(0, 200, 200, 100, Light::Medium, 60)};   // every crop fits, so only rules 1 and 2 can apply
    s.conflicts = {{2, 1, "listed with the larger id first"}};
    ConstraintModel m = buildConstraints(s);
    CHECK(m.conflict[0][1] == ConflictReason::None);
    CHECK(m.conflict[0][2] == ConflictReason::Height);       // 50 >= 2.0 * 20
    CHECK(m.conflict[0][3] == ConflictReason::Height);       // 40 >= 2.0 * 20: the limit itself counts
    CHECK(m.conflict[1][2] == ConflictReason::Explicit);     // also a height conflict, but the explicit rule comes first
    CHECK(m.conflict[2][1] == ConflictReason::Explicit);     // symmetric
    CHECK(m.conflict[1][3] == ConflictReason::Height);
    CHECK(m.conflict[2][3] == ConflictReason::None);         // 50 < 2.0 * 40
    CHECK(m.conflict[2][2] == ConflictReason::None);
    CHECK(m.adjacency == Graph({{2, 3}, {2, 3}, {0, 1}, {0, 1}}));

    s.heightRatioLimit = 2.5;                                // the limit comes from the scenario
    m = buildConstraints(s);
    CHECK(m.conflict[0][2] == ConflictReason::Height);       // 50 >= 2.5 * 20
    CHECK(m.conflict[0][3] == ConflictReason::None);         // 40 < 2.5 * 20
    CHECK(m.conflict[1][3] == ConflictReason::None);
    CHECK(m.conflict[1][2] == ConflictReason::Explicit);
}

TEST(no_common_shelf_is_recorded_as_a_conflict) {
    Scenario s;   // equal heights, nothing listed: only the shelves can separate the crops
    s.crops = {makeCrop(0, 30, 30, 20), makeCrop(1, 30, 30, 20), makeCrop(2, 30, 30, 20)};
    s.crops[0].humidityMin = 40;   // a dry crop, 40 to 50 %
    s.crops[0].humidityMax = 50;
    s.crops[1].humidityMin = 60;   // a humid crop, 60 to 70 %
    s.crops[1].humidityMax = 70;   // crop 2 keeps 50 to 70 %
    s.shelves = {makeShelf(0, 100, 100, 100, Light::Medium, 45), makeShelf(1, 100, 100, 100, Light::Medium, 65)};
    const ConstraintModel m = buildConstraints(s);
    CHECK(m.compat[0] == std::vector<char>({1, 0}));
    CHECK(m.compat[1] == std::vector<char>({0, 1}));
    CHECK(m.compat[2] == std::vector<char>({0, 1}));
    CHECK(m.conflict[0][1] == ConflictReason::NoCommonShelf);
    CHECK(m.conflict[1][0] == ConflictReason::NoCommonShelf);
    CHECK(m.conflict[0][2] == ConflictReason::NoCommonShelf);
    CHECK(m.conflict[1][2] == ConflictReason::None);         // both may stand on shelf 1
    CHECK(m.adjacency == Graph({{1, 2}, {0}, {0}}));
}
