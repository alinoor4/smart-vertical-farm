#pragma once
// Component 1 (plan section 7): which crops may stand on which shelves, which crops may not share a shelf,
// and colouring of the conflict graph.
#include <vector>

#include "model.h"

enum class ConflictReason { None, Explicit, Height, NoCommonShelf };

// Why a crop may not stand on a shelf: a bit for each failed check, so one shelf can fail several.
enum WhyNot : unsigned char { FitsOk = 0, Clearance = 1, LightMismatch = 2, HumidityMismatch = 4, Footprint = 8 };

struct ConstraintModel {
    std::vector<std::vector<char>> compat;              // [crop][shelf]: 1 when the crop may stand on the shelf
    std::vector<std::vector<unsigned char>> whyNot;     // [crop][shelf]: bitmask of failed checks, for tooltips
    std::vector<std::vector<ConflictReason>> conflict;  // [crop][crop], symmetric, None on the diagonal
    std::vector<std::vector<int>> adjacency;            // conflict graph, neighbours ascending
    std::vector<int> maxTraysByArea;                    // [crop]: shelf area / tray area, summed over compatible shelves
    long long pairsTotal = 0, pairsCompatible = 0;      // crop-shelf pairs, for the "pruned x%" readout
};

// Compatibility, conflicts and the capacity bound. The scenario must have passed validateScenario.
// Time O(C*S) for compatibility and O(C^2*S) for the conflicts, with C crops and S shelves.
ConstraintModel buildConstraints(const Scenario& scenario);

// The colourings take adjacency lists (symmetric, vertices 0..n-1) and return one colour per vertex, numbered from 0.

// Welsh-Powell: vertices by degree descending (ties: lower index), each gets the smallest colour no coloured
// neighbour uses. A heuristic. Time O(C log C + E).
std::vector<int> colourWelshPowell(const std::vector<std::vector<int>>& adj);

// DSATUR: repeatedly colour the uncoloured vertex whose neighbours show the most distinct colours (ties: higher
// degree, then lower index) with the smallest free colour. A heuristic. Time O(C * (C + E)).
std::vector<int> colourDsatur(const std::vector<std::vector<int>>& adj);

struct ExactColouring {
    int colours = 0;       // the chromatic number when proven, otherwise the best count found (an upper bound)
    bool proven = true;    // false when the node limit stopped the search
    long long nodes = 0;   // search nodes visited
};

// Backtracking over vertices in Welsh-Powell order; each vertex takes an existing colour or one new colour,
// which removes the symmetry between colour names. DSATUR's count is the first incumbent and a branch is dropped
// as soon as its colour count reaches the incumbent. Exponential in C in the worst case.
ExactColouring colourExact(const std::vector<std::vector<int>>& adj, long long nodeLimit = 2000000);
