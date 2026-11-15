#pragma once
// Reading, writing and checking scenario files (plan section 5.3). Files are a trust boundary.
#include <string>
#include <vector>

#include "model.h"

// Throws std::runtime_error with a readable message when the file cannot be read, is not valid JSON or has
// a value of the wrong type. A missing key takes the default of model.h, so always run validateScenario next.
Scenario loadScenario(const std::string& path);

// Two-space indented JSON, orderHistory rounded to two decimals. Throws std::runtime_error when it cannot write.
void saveScenario(const Scenario& scenario, const std::string& path);

// One message per problem; empty when the scenario is usable. Time O(size of the scenario).
std::vector<std::string> validateScenario(const Scenario& scenario);
