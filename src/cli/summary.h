#pragma once
// Plain-ASCII text for `farm_cli run` and `farm_cli compare` (plan section 13).
#include <cstdio>

#include "pipeline.h"

// The scenario's name and sizes; the forecast method per crop; the pruning statistics; the selection table; budget use;
// packing statistics; schedule statistics per crop; totals; warnings; seconds per stage (plan section 13, in that order).
void printRunSummary(std::FILE* out, const Scenario& scenario, const PlanResult& result);

// One table per stage.
void printCompareTable(std::FILE* out, const CompareTable& table);
