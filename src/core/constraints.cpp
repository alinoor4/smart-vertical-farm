#include "constraints.h"

#include <algorithm>
#include <numeric>

namespace {

// A tray may be turned 90 degrees when the crop allows it; its width then lies along the shelf depth.
bool footprintFits(const Crop& c, const Shelf& s) {
    if (c.trayW <= s.width && c.trayD <= s.depth) return true;
    return c.canRotate && c.trayD <= s.width && c.trayW <= s.depth;
}

// The smallest colour that no coloured neighbour of v uses. Time O(degree of v).
int smallestFreeColour(const std::vector<std::vector<int>>& adj, const std::vector<int>& colour, int v) {
    std::vector<char> used(adj[v].size() + 1, 0);   // with d neighbours, one of the colours 0..d is free
    for (int u : adj[v])
        if (colour[u] >= 0 && colour[u] < int(used.size())) used[colour[u]] = 1;
    int k = 0;
    while (used[k]) ++k;
    return k;
}

// Vertices by degree descending, ties by index ascending: a total order, so the result is deterministic.
std::vector<int> welshPowellOrder(const std::vector<std::vector<int>>& adj) {
    std::vector<int> order(adj.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return adj[a].size() != adj[b].size() ? adj[a].size() > adj[b].size() : a < b;
    });
    return order;
}

// State of the exact search, so that the recursion has one place to keep it.
struct ExactSearch {
    const std::vector<std::vector<int>>& adj;
    std::vector<int> order;     // vertices in Welsh-Powell order
    std::vector<int> colour;    // colour of each vertex, -1 while it has none
    int best = 0;               // fewest colours of any complete colouring found so far
    long long nodes = 0, nodeLimit;
    bool stopped = false;       // the node limit was reached

    ExactSearch(const std::vector<std::vector<int>>& graph, long long limit)
        : adj(graph), order(welshPowellOrder(graph)), colour(graph.size(), -1), nodeLimit(limit) {
        const std::vector<int> start = colourDsatur(graph);
        best = 1 + *std::max_element(start.begin(), start.end());
    }

    // Colours order[i], order[i+1], ... given that the partial colouring uses `used` colours.
    void extend(int i, int used) {
        if (++nodes > nodeLimit) stopped = true;
        if (stopped || used >= best) return;
        if (i == int(order.size())) {   // a complete colouring with fewer colours than the incumbent
            best = used;
            return;
        }
        const int v = order[i];
        std::vector<char> blocked(used + 1, 0);   // every assigned colour is below `used`
        for (int u : adj[v])
            if (colour[u] >= 0) blocked[colour[u]] = 1;
        for (int k = 0; k <= used && !stopped; ++k) {   // an existing colour, or one new colour
            if (blocked[k]) continue;
            colour[v] = k;
            extend(i + 1, std::max(used, k + 1));
        }
        colour[v] = -1;
    }
};

}  // namespace

ConstraintModel buildConstraints(const Scenario& s) {
    const int numCrops = int(s.crops.size()), numShelves = int(s.shelves.size());
    ConstraintModel m;
    m.compat.assign(numCrops, std::vector<char>(numShelves, 0));
    m.whyNot.assign(numCrops, std::vector<unsigned char>(numShelves, FitsOk));
    m.maxTraysByArea.assign(numCrops, 0);
    m.pairsTotal = (long long)numCrops * numShelves;

    for (int c = 0; c < numCrops; ++c) {
        const Crop& crop = s.crops[c];
        for (int sh = 0; sh < numShelves; ++sh) {
            const Shelf& shelf = s.shelves[sh];
            unsigned char bad = FitsOk;
            if (crop.height + s.lightGapCm > shelf.clearance) bad |= Clearance;
            if (shelf.light < crop.lightMin || shelf.light > crop.lightMax) bad |= LightMismatch;
            if (shelf.humidity < crop.humidityMin || shelf.humidity > crop.humidityMax) bad |= HumidityMismatch;
            if (!footprintFits(crop, shelf)) bad |= Footprint;
            m.whyNot[c][sh] = bad;
            if (bad != FitsOk) continue;
            m.compat[c][sh] = 1;
            ++m.pairsCompatible;
            m.maxTraysByArea[c] += int((long long)shelf.width * shelf.depth / ((long long)crop.trayW * crop.trayD));
        }
    }

    std::vector<std::vector<char>> listed(numCrops, std::vector<char>(numCrops, 0));   // the explicit rules, both ways round
    for (const ConflictPair& p : s.conflicts) listed[p.a][p.b] = listed[p.b][p.a] = 1;
    m.conflict.assign(numCrops, std::vector<ConflictReason>(numCrops, ConflictReason::None));
    for (int a = 0; a < numCrops; ++a)
        for (int b = a + 1; b < numCrops; ++b) {
            const int tall = std::max(s.crops[a].height, s.crops[b].height);
            const int low = std::min(s.crops[a].height, s.crops[b].height);
            ConflictReason reason = ConflictReason::None;   // the first rule that applies gives the reason
            if (listed[a][b]) {
                reason = ConflictReason::Explicit;
            } else if (tall >= s.heightRatioLimit * low) {   // the taller crop shades the shorter one
                reason = ConflictReason::Height;
            } else {
                bool shareShelf = false;
                for (int sh = 0; sh < numShelves && !shareShelf; ++sh) shareShelf = m.compat[a][sh] && m.compat[b][sh];
                if (!shareShelf) reason = ConflictReason::NoCommonShelf;   // adds no restriction: compat already forces it
            }
            m.conflict[a][b] = m.conflict[b][a] = reason;
        }

    m.adjacency.assign(numCrops, {});
    for (int a = 0; a < numCrops; ++a)
        for (int b = 0; b < numCrops; ++b)
            if (m.conflict[a][b] != ConflictReason::None) m.adjacency[a].push_back(b);
    return m;
}

std::vector<int> colourWelshPowell(const std::vector<std::vector<int>>& adj) {
    std::vector<int> colour(adj.size(), -1);
    for (int v : welshPowellOrder(adj)) colour[v] = smallestFreeColour(adj, colour, v);
    return colour;
}

std::vector<int> colourDsatur(const std::vector<std::vector<int>>& adj) {
    const int n = int(adj.size());
    std::vector<int> colour(n, -1);
    std::vector<int> countedFor(n, 0);   // countedFor[c] == stamp: colour c was already counted for the vertex being examined
    int stamp = 0;
    for (int step = 0; step < n; ++step) {
        int best = -1, bestSaturation = -1;
        for (int v = 0; v < n; ++v) {   // ascending, and only a strictly better candidate replaces: ties go to the lower index
            if (colour[v] >= 0) continue;
            ++stamp;
            int saturation = 0;   // distinct colours among the neighbours
            for (int u : adj[v])
                if (colour[u] >= 0 && countedFor[colour[u]] != stamp) {
                    countedFor[colour[u]] = stamp;
                    ++saturation;
                }
            if (saturation > bestSaturation || (saturation == bestSaturation && adj[v].size() > adj[best].size())) {
                best = v;
                bestSaturation = saturation;
            }
        }
        colour[best] = smallestFreeColour(adj, colour, best);
    }
    return colour;
}

ExactColouring colourExact(const std::vector<std::vector<int>>& adj, long long nodeLimit) {
    ExactColouring result;
    if (adj.empty()) return result;   // no vertices, no colours
    ExactSearch search(adj, nodeLimit);
    search.extend(0, 0);
    result.colours = search.best;
    result.proven = !search.stopped;
    result.nodes = search.nodes;
    return result;
}
