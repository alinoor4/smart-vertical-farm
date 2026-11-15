#pragma once
// Reading, writing and checking scenario files (plan section 5.3). Files are a trust boundary.
#include <string>
#include <vector>

#include "model.h"
#include "pipeline.h"

// Throws std::runtime_error with a readable message when the file cannot be read, is not valid JSON or has
// a value of the wrong type. A missing key takes the default of model.h, so always run validateScenario next.
Scenario loadScenario(const std::string& path);

// Two-space indented JSON, orderHistory rounded to two decimals. Throws std::runtime_error when it cannot write.
void saveScenario(const Scenario& scenario, const std::string& path);

// One message per problem; empty when the scenario is usable. Time O(size of the scenario).
std::vector<std::string> validateScenario(const Scenario& scenario);

// A plan result as JSON (plan 12.1), indented by two spaces. Everything but the times is deterministic: two runs of the same
// scenario and options give the same text. The times are under the key "timing" and left out when includeTiming is false.
std::string planResultToJson(const PlanResult& result, bool includeTiming = true);
void savePlanResult(const PlanResult& result, const std::string& path);   // throws std::runtime_error when it cannot write

// The comparison tables as JSON; the millisecond columns are real timings.
std::string compareTableToJson(const CompareTable& table);
void saveCompareTable(const CompareTable& table, const std::string& path);
