#include "summary.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

const char* const kResourceNames[kResources] = {"labour", "water", "nutrient", "energy", "area"};

}  // namespace

void printRunSummary(std::FILE* out, const Scenario& scenario, const PlanResult& r) {
    const PlanMetrics& m = r.metrics;
    int hardLots = 0;
    for (const CropDemand& d : r.demand)
        for (const Lot& lot : d.lots) hardLots += lot.hard ? 1 : 0;

    std::fprintf(out, "Scenario: %s\n", r.scenarioName.c_str());
    std::fprintf(out, "  %zu crops, %zu shelves, %zu contracts, horizon %d weeks, %d contract lots\n", scenario.crops.size(), scenario.shelves.size(),
                 scenario.contracts.size(), scenario.horizonWeeks, hardLots);

    std::fprintf(out, "\nForecast (%s):\n", name(r.options.forecast));
    for (size_t c = 0; c < r.forecast.size(); ++c)
        std::fprintf(out, "  %-14s %-28s %s\n", r.cropNames[c].c_str(), name(r.forecast[c].chosen), r.forecast[c].parameters.c_str());

    const ConstraintModel& k = r.constraints;
    int explicitEdges = 0, heightEdges = 0, noCommonEdges = 0;
    for (size_t a = 0; a < k.conflict.size(); ++a)
        for (size_t b = a + 1; b < k.conflict.size(); ++b) {
            explicitEdges += k.conflict[a][b] == ConflictReason::Explicit ? 1 : 0;
            heightEdges += k.conflict[a][b] == ConflictReason::Height ? 1 : 0;
            noCommonEdges += k.conflict[a][b] == ConflictReason::NoCommonShelf ? 1 : 0;
        }
    std::fprintf(out, "\nConstraints:\n");
    std::fprintf(out, "  crop-shelf pairs compatible: %lld of %lld (%.1f%% pruned)\n", k.pairsCompatible, k.pairsTotal,
                 k.pairsTotal > 0 ? 100.0 * (k.pairsTotal - k.pairsCompatible) / k.pairsTotal : 0.0);
    std::fprintf(out, "  conflict edges: %d (explicit %d, height %d, no common shelf %d)\n", explicitEdges + heightEdges + noCommonEdges, explicitEdges,
                 heightEdges, noCommonEdges);
    std::fprintf(out, "  colour groups: Welsh-Powell %d, DSATUR %d, exact %d%s\n", r.colours.welshPowell, r.colours.dsatur, r.colours.exact,
                 r.colours.exactProven ? "" : " (not proven)");

    std::fprintf(out, "\nCrop selection (%s): relaxed optimum %.1f PKR/week", name(r.options.knapsack), m.relaxedOptimum);
    if (r.options.knapsack == KnapsackAlgo::BranchAndBound)
        std::fprintf(out, ", %s, %lld nodes, bound %.1f", r.knapsack.provenOptimal ? "proven optimal" : "NOT proven", r.knapsack.nodes, r.knapsack.upperBound);
    if (r.options.knapsack == KnapsackAlgo::Dp) std::fprintf(out, ", %zu states%s%s", r.knapsack.states, r.knapsack.note.empty() ? "" : ", ", r.knapsack.note.c_str());
    std::fprintf(out, "\n");
    std::fprintf(out, "  %-14s %5s %5s %9s %8s %8s %10s %7s %8s %8s %7s\n", "crop", "sel", "trays", "area cm2", "kg/week", "demand", "value", "labour", "water", "nutrient", "energy");
    for (size_t c = 0; c < r.rows.size(); ++c) {
        const CropPlan& row = r.rows[c];
        if (row.selected == 0 && row.trays == 0) continue;
        std::fprintf(out, "  %-14s %5d %5d %9lld %8.3f %8.3f %10.1f %7.2f %8.1f %8.2f %7.1f\n", r.cropNames[c].c_str(), row.selected, row.trays, row.area,
                     row.weeklyKg, row.demandKg, row.value, row.use[0], row.use[1], row.use[2], row.use[3]);
    }

    std::fprintf(out, "\nBudget use of the final plan:\n ");
    for (int i = 0; i < kResources; ++i)
        std::fprintf(out, " %s %.2f%%%s%s", kResourceNames[i], 100.0 * m.budgetUse[i], r.problem.active[i] ? "" : " (switched off)", i + 1 < kResources ? "," : "");
    std::fprintf(out, "\n");

    std::fprintf(out, "\nPacking (%s): placed %zu of %zu trays, value %.1f, %d shelves used, placed area %lld cm2 (%.2f%% of the shelves)\n", r.pack.algorithm.c_str(),
                 r.pack.state.placements.size(), r.pack.state.placements.size() + r.pack.unplaced.size(), r.pack.placedValue, r.pack.shelvesUsed,
                 r.pack.placedArea, r.pack.totalShelfArea > 0 ? 100.0 * r.pack.placedArea / r.pack.totalShelfArea : 0.0);
    if (r.pack.generationsRun > 0 || r.options.packer == PackAlgo::Genetic) std::fprintf(out, "  genetic search: %d generations\n", r.pack.generationsRun);
    std::vector<int> unplacedPerCrop(scenario.crops.size(), 0);
    for (const Tray& t : r.pack.unplaced) ++unplacedPerCrop[t.cropId];
    std::string unplaced;
    for (size_t c = 0; c < unplacedPerCrop.size(); ++c)
        if (unplacedPerCrop[c] > 0) unplaced += (unplaced.empty() ? "" : ", ") + r.cropNames[c] + " " + std::to_string(unplacedPerCrop[c]);
    if (!unplaced.empty()) std::fprintf(out, "  unplaced: %s\n", unplaced.c_str());
    if (r.options.refill)
        std::fprintf(out, "  refill: +%d trays; final plan %zu trays, value %.1f (%.2f%% of the relaxed optimum)\n", r.refilled, r.layout.placements.size(), m.planValue, 100.0 * m.planShare);
    else
        std::fprintf(out, "  refill is off; plan value %.1f (%.2f%% of the relaxed optimum)\n", m.planValue, 100.0 * m.planShare);
    std::fprintf(out, "  space used: %.2f%% of all shelf area, %.2f%% of the shelves in use\n", 100.0 * m.spaceUtilisation, 100.0 * m.spaceUtilisationOfUsed);

    std::fprintf(out, "\nSchedule (%s):\n", name(r.options.scheduler));
    std::fprintf(out, "  %-14s %5s %12s %12s %6s\n", "crop", "slots", "contracts", "spot", "idle");
    for (const CropSchedule& s : r.schedules) {
        if (s.slots == 0 && s.hardTotal == 0) continue;
        std::fprintf(out, "  %-14s %5d %7d/%-4d %7d/%-4d %6d\n", r.cropNames[s.cropId].c_str(), s.slots, s.hardOnTime, s.hardTotal, s.softServed, s.softTotal, s.idleSlotWeeks);
    }

    std::fprintf(out, "\nTotals:\n");
    std::fprintf(out, "  relaxed optimum %.1f, plan value %.1f (%.2f%%), expected weekly profit %.1f PKR\n", m.relaxedOptimum, m.planValue, 100.0 * m.planShare, m.expectedWeeklyProfit);
    std::fprintf(out, "  contract reliability %.2f%% (%d of %d lots on time), spot service %.2f%% (%d of %d)\n", 100.0 * m.contractReliability, m.hardOnTime, m.hardTotal,
                 100.0 * m.spotService, m.softServed, m.softTotal);
    std::fprintf(out, "  idle slot-weeks %d, revenue %.1f, penalties %.1f, net over the plan %.1f PKR\n", m.idleSlotWeeks, m.revenue, m.penalties, m.net);

    std::fprintf(out, "\nWarnings: %s\n", r.warnings.empty() ? "none" : std::to_string(r.warnings.size()).c_str());
    for (const std::string& w : r.warnings) std::fprintf(out, "  - %s\n", w.c_str());

    std::fprintf(out, "\nSeconds per stage:");
    for (const auto& s : r.stageSeconds) std::fprintf(out, " %s %.3f,", s.first.c_str(), s.second);
    std::fprintf(out, " total %.3f\n", r.totalSeconds);
}

void printCompareTable(std::FILE* out, const CompareTable& table) {
    for (const CompareStage& stage : table.stages) {
        // One format per column: whole numbers when every row's value is whole, else two decimals (three for milliseconds).
        std::vector<bool> whole(stage.columns.size(), true);
        for (const CompareRow& row : stage.rows)
            for (size_t i = 0; i < row.values.size() && i < whole.size(); ++i)
                if (std::fabs(row.values[i] - std::round(row.values[i])) > 1e-9) whole[i] = false;
        std::fprintf(out, "\n%s\n", stage.name.c_str());
        std::fprintf(out, "  %-30s", "algorithm");
        for (const std::string& column : stage.columns) std::fprintf(out, " %15s", column.c_str());
        std::fprintf(out, "\n");
        for (const CompareRow& row : stage.rows) {
            std::fprintf(out, "  %-30s", row.name.c_str());
            for (size_t i = 0; i < row.values.size(); ++i) {
                const int digits = whole[i] ? 0 : stage.columns[i] == "ms" ? 3 : 2;
                std::fprintf(out, " %15.*f", digits, row.values[i]);
            }
            if (!row.note.empty()) std::fprintf(out, "  %s", row.note.c_str());
            std::fprintf(out, "\n");
        }
    }
}
