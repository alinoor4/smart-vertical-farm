#pragma once
// Seeded sample-data generator (plan section 6). Same parameters, same scenario, on every machine.
#include "model.h"

struct GeneratorParams {
    unsigned long long seed = 42;
    int crops = 12;          // more than 12 creates jittered variants of the catalogue
    int racks = 4;           // rack r uses template r % 4
    bool flat = false;       // constant order history
    double jitter = 0.0;     // relative jitter on numeric crop fields
    int historyWeeks = 156;
    int horizonWeeks = 12;
};

// Time O(crops * historyWeeks). Does not validate: the caller runs validateScenario on the result.
Scenario generateScenario(const GeneratorParams& params);
