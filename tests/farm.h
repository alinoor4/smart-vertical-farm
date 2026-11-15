#pragma once
// The flat farm of plan Appendix A, G6, with everything the pipeline builds before it packs: the constraint model, the
// forecast-based demand of every crop and the knapsack problem. Shared by the tests that start from this farm.
#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "constraints.h"
#include "forecast.h"
#include "generator.h"
#include "knapsack.h"
#include "model.h"
#include "scheduling.h"

struct Farm {
    Scenario scenario;
    ConstraintModel constraints;
    std::vector<CropDemand> demand;
    KnapsackProblem problem;
};

inline Farm makeFarm(Scenario scenario, const std::array<bool, kResources>& active = {{true, true, true, true, true}}) {
    Farm farm;
    farm.scenario = std::move(scenario);
    farm.constraints = buildConstraints(farm.scenario);
    int longest = 0;
    for (const Crop& c : farm.scenario.crops) longest = std::max(longest, c.cycleWeeks);
    const int horizon = farm.scenario.horizonWeeks - 1 + longest;
    for (const Crop& c : farm.scenario.crops) {
        const ForecastResult f = forecastSeries(farm.scenario.orderHistory[c.id], farm.scenario.seasonLength, horizon);
        farm.demand.push_back(buildCropDemand(farm.scenario, c.id, f.forecast));
    }
    farm.problem = buildKnapsackProblem(farm.scenario, farm.demand, farm.constraints, active);
    return farm;
}

inline Scenario flatScenario() {
    GeneratorParams params;
    params.flat = true;
    return generateScenario(params);
}

// Trays per crop id (0 for crops without a group).
inline std::vector<int> byCrop(const KnapsackProblem& p, const KnapsackResult& r, size_t crops) {
    std::vector<int> out(crops, 0);
    for (size_t g = 0; g < p.groups.size(); ++g) out[p.groups[g].cropId] = r.count[g];
    return out;
}
